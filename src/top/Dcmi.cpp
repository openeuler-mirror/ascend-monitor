#include "top/Dcmi.h"
#include "top/Proc.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <map>
#include <sstream>
#include <dlfcn.h>
#ifdef ASCEND_MONITOR_TUI_HAVE_DCMI
#include <dcmi_interface_api.h>
#endif

namespace ascend {
static std::string cannVersion() {
    const std::vector<std::string> paths = {
        "/usr/local/Ascend/ascend-toolkit/latest/aarch64-linux/ascend_toolkit_install.info",
        "/usr/local/Ascend/ascend-toolkit/latest/x86_64-linux/ascend_toolkit_install.info",
        "/usr/local/Ascend/ascend-toolkit/latest/version.cfg",
        "/usr/local/Ascend/nnrt/latest/ascend_nnrt_install.info",
        "/usr/local/Ascend/cann/share/info/runtime/version.info"
    };
    for (const auto& path : paths) {
        std::istringstream in(readText(path)); std::string line;
        while (std::getline(in, line)) for (const std::string prefix : {"version=", "Version="}) {
            if (line.rfind(prefix, 0) == 0) { auto version = line.substr(prefix.size());
                auto end = version.find_first_of(" \t\r"); if (end != std::string::npos) version.resize(end);
                if (!version.empty()) return safeText(version); }
        }
    }
    return "N/A";
}
struct Dcmi::Impl {
    void* library = nullptr;
    std::string error, cann = cannVersion(), driver = "N/A";
#ifdef ASCEND_MONITOR_TUI_HAVE_DCMI
#define DCMI_FN(name) decltype(&name) name##_ = nullptr
    DCMI_FN(dcmi_init); DCMI_FN(dcmi_get_card_list); DCMI_FN(dcmi_get_device_num_in_card);
    DCMI_FN(dcmi_get_driver_version); DCMI_FN(dcmi_get_device_chip_info);
    DCMI_FN(dcmi_get_device_logic_id); DCMI_FN(dcmi_get_device_phyid_from_logicid);
    DCMI_FN(dcmi_get_device_pcie_info_v2); DCMI_FN(dcmi_get_device_temperature);
    DCMI_FN(dcmi_get_device_power_info); DCMI_FN(dcmi_get_device_utilization_rate);
    DCMI_FN(dcmi_get_device_memory_info_v3); DCMI_FN(dcmi_get_device_resource_info);
    DCMI_FN(dcmi_get_device_share_enable);
#undef DCMI_FN
    struct ShareReading {
        std::optional<bool> enabled;
        Clock::time_point refreshAfter{};
    };
    std::map<std::pair<int, int>, ShareReading> shareReadings;
    std::optional<bool> deviceShare(int card, int chip) {
        if (!dcmi_get_device_share_enable_) return std::nullopt;
        auto& reading = shareReadings[{card, chip}];
        if (Clock::now() >= reading.refreshAfter) {
            int flag = -1;
            int rc = dcmi_get_device_share_enable_(card, chip, &flag);
            reading.enabled.reset();
            if (rc == 0 && (flag == 0 || flag == 1)) reading.enabled = flag == 1;
            // Cache failures too; a failed refresh must not retain an old On/Off value.
            reading.refreshAfter = Clock::now() + std::chrono::seconds(5);
        }
        return reading.enabled;
    }
    Impl() {
        for (const char* path : {"/usr/local/Ascend/driver/lib64/driver/libdcmi.so", "libdcmi.so"}) {
            library = dlopen(path, RTLD_NOW | RTLD_LOCAL); if (library) break;
        }
        if (!library) { auto detail = dlerror(); error = std::string("DCMI unavailable: ") + (detail ? detail : "library not found"); return; }
#define LOAD(name) name##_ = reinterpret_cast<decltype(name##_)>(dlsym(library, #name))
        LOAD(dcmi_init); LOAD(dcmi_get_card_list); LOAD(dcmi_get_device_num_in_card);
        LOAD(dcmi_get_driver_version); LOAD(dcmi_get_device_chip_info);
        LOAD(dcmi_get_device_logic_id); LOAD(dcmi_get_device_phyid_from_logicid);
        LOAD(dcmi_get_device_pcie_info_v2); LOAD(dcmi_get_device_temperature);
        LOAD(dcmi_get_device_power_info); LOAD(dcmi_get_device_utilization_rate);
        LOAD(dcmi_get_device_memory_info_v3); LOAD(dcmi_get_device_resource_info);
        LOAD(dcmi_get_device_share_enable);
#undef LOAD
        if (!dcmi_init_ || !dcmi_get_card_list_ || !dcmi_get_device_num_in_card_) { error = "DCMI required entry points unavailable"; return; }
        int rc = dcmi_init_(); if (rc != 0) { error = "dcmi_init failed: " + std::to_string(rc); return; }
        char version[256]{};
        if (dcmi_get_driver_version_ && dcmi_get_driver_version_(version, sizeof(version)) == 0) driver = safeText(std::string(version, strnlen(version, sizeof(version))));
    }
    void collect(Snapshot& s) {
        if (!error.empty()) { s.error = error; return; }
        std::array<int, MAX_CARD_NUM> cards{}; int count = 0;
        int rc = dcmi_get_card_list_(&count, cards.data(), static_cast<int>(cards.size()));
        if (rc || count < 0 || count > static_cast<int>(cards.size())) { s.error = "DCMI card enumeration failed: " + std::to_string(rc); return; }
        for (int i = 0; i < count; ++i) {
            int chips = 0; rc = dcmi_get_device_num_in_card_(cards[i], &chips);
            if (rc || chips < 0 || chips > 128) { s.error = "Incomplete device enumeration, card " + std::to_string(cards[i]); continue; }
            for (int chip = 0; chip < chips; ++chip) {
                Device d; d.card = cards[i]; d.chip = chip;
                d.deviceShare = deviceShare(d.card, chip);
                int logical = -1;
                if (dcmi_get_device_logic_id_ && dcmi_get_device_logic_id_(&logical, d.card, chip) == 0 && logical >= 0) {
                    d.logical = static_cast<unsigned>(logical); unsigned physical = 0;
                    if (dcmi_get_device_phyid_from_logicid_ && dcmi_get_device_phyid_from_logicid_(*d.logical, &physical) == 0) d.physical = physical;
                }
                dcmi_chip_info info{};
                if (dcmi_get_device_chip_info_ && dcmi_get_device_chip_info_(d.card, chip, &info) == 0)
                    d.name = safeText(std::string(reinterpret_cast<char*>(info.chip_name), strnlen(reinterpret_cast<char*>(info.chip_name), sizeof(info.chip_name))));
                dcmi_pcie_info_all pci{};
                if (dcmi_get_device_pcie_info_v2_ && dcmi_get_device_pcie_info_v2_(d.card, chip, &pci) == 0) {
                    std::ostringstream out; out << std::hex << std::setfill('0') << std::setw(4) << pci.domain << ':' << std::setw(2) << pci.bdf_busid << ':' << std::setw(2) << pci.bdf_deviceid << '.' << pci.bdf_funcid; d.pci = out.str();
                }
                int temperature = 0;
                if (dcmi_get_device_temperature_ && dcmi_get_device_temperature_(d.card, chip, &temperature) == 0 && temperature >= -40 && temperature < 200) d.temperature = temperature;
                // DCMI chip power unit is 0.1 W. Never substitute MCU/card power.
                int power = 0;
                if (dcmi_get_device_power_info_ && dcmi_get_device_power_info_(d.card, chip, &power) == 0 && power >= 0) d.power = power / 10.0;
                unsigned utilization = 0;
                if (dcmi_get_device_utilization_rate_ && dcmi_get_device_utilization_rate_(d.card, chip, DCMI_UTILIZATION_RATE_AICORE, &utilization) == 0 && utilization <= 100) d.core = utilization;
                dcmi_get_memory_info_stru mem{};
                if (dcmi_get_device_memory_info_v3_ && dcmi_get_device_memory_info_v3_(d.card, chip, &mem) == 0 && mem.memory_size > 0 && mem.memory_available <= mem.memory_size) {
                    d.memory.kind = "DDR"; d.memory.total = static_cast<double>(mem.memory_size) * 1024 * 1024;
                    d.memory.used = static_cast<double>(mem.memory_size - mem.memory_available) * 1024 * 1024;
                }
                // API contract: proc_num is output; driver writes at most 32 entries.
                std::array<dcmi_proc_mem_info, 32> procs{}; int n = 0;
                if (dcmi_get_device_resource_info_ && dcmi_get_device_resource_info_(d.card, chip, procs.data(), &n) == 0 && n >= 0 && n <= static_cast<int>(procs.size())) {
                    d.processesAvailable = true;
                    for (int j = 0; j < n; ++j) if (procs[j].proc_id > 0) {
                        Process p; p.card = d.card; p.chip = d.chip; p.physical = d.physical; p.pid = procs[j].proc_id;
                        p.npuBytes = static_cast<double>(procs[j].proc_mem_usage); s.processes.push_back(std::move(p));
                    }
                }
                s.devices.push_back(std::move(d));
            }
        }
        for (auto it = shareReadings.begin(); it != shareReadings.end();) {
            bool present = std::any_of(s.devices.begin(), s.devices.end(), [&](const Device& d) {
                return it->first == std::make_pair(d.card, d.chip);
            });
            if (!present) it = shareReadings.erase(it); else ++it;
        }
        auto key = [](const auto& d) { return std::make_tuple(d.physical.value_or(~0u), d.card, d.chip); };
        std::sort(s.devices.begin(), s.devices.end(), [&](const Device& a, const Device& b) { return key(a) < key(b); });
        std::sort(s.processes.begin(), s.processes.end(), [&](const Process& a, const Process& b) { return key(a) != key(b) ? key(a) < key(b) : a.pid < b.pid; });
        if (s.devices.empty() && s.error.empty()) s.error = "No NPU devices detected";
    }
#else
    Impl() { error = "Built without DCMI support; rebuild on the NPU host with ASCEND_MONITOR_TUI_DCMI=ON"; }
    void collect(Snapshot& s) { s.error = error; }
#endif
    ~Impl() { if (library) dlclose(library); }
};
Dcmi::Dcmi() : impl_(std::make_unique<Impl>()) {}
Dcmi::~Dcmi() = default;
void Dcmi::collect(Snapshot& s) { s.driver = impl_->driver; s.cann = impl_->cann; impl_->collect(s); }
}

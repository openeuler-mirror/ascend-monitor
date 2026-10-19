#include "top/Dcmi.h"
#include <dcmi_interface_api.h>
#include <dlfcn.h>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace ascend;
namespace {
bool shareSymbol = true;
int flag = 1, result = 0, chips = 2, calls[2]{};
int init() { return 0; }
int cards(int* count, int* ids, int) { *count = 1; ids[0] = 3; return 0; }
int devices(int, int* count) { *count = chips; return 0; }
int share(int card, int chip, int* enabled) {
    if (card != 3 || chip < 0 || chip > 1) return -1;
    ++calls[chip];
    *enabled = chip == 0 ? flag : 0;
    return chip == 0 ? result : 0;
}
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
Snapshot sample(Dcmi& dcmi) {
    Snapshot s; dcmi.collect(s);
    check(s.error.empty() && s.devices.size() == static_cast<size_t>(chips), "device enumeration");
    return s;
}
}
// Override dynamic loading only in this test executable. No real driver is loaded or called.
extern "C" void* dlopen(const char*, int) noexcept { return reinterpret_cast<void*>(1); }
extern "C" int dlclose(void*) noexcept { return 0; }
extern "C" void* dlsym(void*, const char* name) noexcept {
    if (!std::strcmp(name, "dcmi_init")) return reinterpret_cast<void*>(&init);
    if (!std::strcmp(name, "dcmi_get_card_list")) return reinterpret_cast<void*>(&cards);
    if (!std::strcmp(name, "dcmi_get_device_num_in_card")) return reinterpret_cast<void*>(&devices);
    if (!std::strcmp(name, "dcmi_get_device_share_enable") && shareSymbol) return reinterpret_cast<void*>(&share);
    return nullptr;
}
int main() {
    try {
        Dcmi dcmi;
        auto s = sample(dcmi);
        check(s.devices[0].deviceShare == true && s.devices[1].deviceShare == false, "per-chip On and Off");
        auto json = snapshotJson(s);
        check(json.find("\"device_share\":true") != std::string::npos &&
              json.find("\"device_share\":false") != std::string::npos, "JSON boolean values");
        flag = 0;
        s = sample(dcmi);
        check(s.devices[0].deviceShare == true && calls[0] == 1 && calls[1] == 1, "cached within 5s");
        chips = 1; sample(dcmi); chips = 2; sample(dcmi);
        check(calls[0] == 1 && calls[1] == 2, "removed chip cache discarded");

        result = -1;
        std::this_thread::sleep_for(std::chrono::milliseconds(5050));
        s = sample(dcmi);
        check(!s.devices[0].deviceShare && calls[0] == 2, "failure replaces cached On with N/A");
        check(snapshotJson(s).find("\"device_share\":null") != std::string::npos, "JSON unknown is null");
        result = 0;
        s = sample(dcmi);
        check(!s.devices[0].deviceShare && calls[0] == 2, "failure cached without retry storm");
        std::this_thread::sleep_for(std::chrono::milliseconds(5050));
        s = sample(dcmi);
        check(s.devices[0].deviceShare == false && calls[0] == 3, "retry recovers to Off after 5s");

        flag = 2;
        Dcmi invalid;
        check(!sample(invalid).devices[0].deviceShare, "unexpected flag is N/A");
        shareSymbol = false;
        int before = calls[0];
        Dcmi missing;
        check(!sample(missing).devices[0].deviceShare && calls[0] == before, "missing optional symbol is N/A");
        std::cout << "device-share collection, cache, recovery and JSON tests passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}

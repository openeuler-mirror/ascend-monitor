// Test-only telemetry provider; never linked into the installed executable.
#include "top/Tui.h"
#include "top/Dcmi.h"
#include "top/Proc.h"
#include <cstdlib>
#include <fstream>
namespace ascend {
struct Dcmi::Impl {};
Dcmi::Dcmi() : impl_(std::make_unique<Impl>()) {}
Dcmi::~Dcmi() = default;
void Dcmi::collect(Snapshot& s) {
    std::string phase;
    if (auto path = std::getenv("ASCEND_TEST_PHASE_FILE")) std::ifstream(path) >> phase;
    s.host.user = "tester"; s.host.hostname = "fixture";
    s.host.cpu = 12; s.host.memory = {1024.0 * 1024 * 1024, 10.0 * 1024 * 1024 * 1024, ""};
    s.host.swap = {0, 0, ""};
    for (int i = 0; i < 3; ++i) {
        Device d; d.card = i; d.chip = 0; d.physical = i; d.logical = i;
        d.name = "TEST-NPU";
        d.memory = {(i == 0 ? 200.0 : i == 1 ? 600.0 : 950.0) * 1024 * 1024, 1024.0 * 1024 * 1024, "DDR"};
        d.core = 85; d.temperature = 40; d.processesAvailable = phase != "missing" || i != 1;
        s.devices.push_back(d);
        Process p; p.card = i; p.chip = 0; p.physical = i;
        p.pid = i == 1 ? 84 : 42; p.startTicks = i == 1 ? 8400 : (phase == "reuse" ? 4201 : 4200);
        if (phase == "unavailable" && p.pid == 42) p.startTicks.reset();
        p.user = "tester"; p.command = i == 1 ? "other worker" : "chosen worker --model test";
        p.cpu = i == 1 ? 10 : 250; p.rssBytes = (i == 1 ? 1024.0 : 512.0) * 1024 * 1024;
        p.memoryPercent = i == 1 ? 10 : 5; p.elapsed = 360;
        p.npuBytes = (phase == "reorder" && i == 1 ? 900.0 : 100.0 * (i + 1)) * 1024 * 1024;
        s.processes.push_back(p);
    }
}
ProcReader::ProcReader(std::string root) : root_(std::move(root)) {}
void ProcReader::collect(Snapshot&) {}
}
int main(int argc, char** argv) { return ascend::runTui(.2, argc > 1 && std::string(argv[1]) == "--ascii"); }

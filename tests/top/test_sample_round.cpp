#include "top/SampleRound.h"
#include "top/Dcmi.h"
#include "top/Proc.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace ascend;

namespace {
bool dcmiError = false;
bool dcmiThrows = false;
bool procThrows = false;
uint64_t processStartTicks = 900;
Clock::time_point hostCompleted;

void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
}

namespace ascend {
struct Dcmi::Impl { int rounds = 0; };
Dcmi::Dcmi() : impl_(std::make_unique<Impl>()) {}
Dcmi::~Dcmi() = default;
void Dcmi::collect(Snapshot& snapshot) {
    if (dcmiError) { snapshot.error = "DCMI unavailable"; return; }
    Device device;
    device.card = 3; device.chip = 0; device.physical = 2;
    device.processesAvailable = true;
    device.deviceShare = ++impl_->rounds == 1;
    device.memory = {0.0, 1024.0, "DDR"};
    snapshot.devices.push_back(device);
    Process process;
    process.pid = 42; process.card = 3; process.chip = 0;
    process.physical = 2; process.npuBytes = 256.0;
    snapshot.processes.push_back(process);
    Device second = device;
    second.chip = 1; second.physical = 3;
    snapshot.devices.push_back(second);
    process.chip = 1; process.physical = 3; process.npuBytes = 512.0;
    snapshot.processes.push_back(process);
    if (dcmiThrows) throw std::runtime_error("DCMI threw");
}

ProcReader::ProcReader(std::string root) : root_(std::move(root)) {}
void ProcReader::collect(Snapshot& snapshot) {
    snapshot.host.memory = {512.0, 1024.0, ""};
    snapshot.host.cpu = previousCpu_ ? Value(25.0) : Value{};
    previousCpu_ = CpuTicks{100, 20};
    std::map<int, Value> cpuByPid;
    for (auto& process : snapshot.processes) {
        auto [cpu, firstChip] = cpuByPid.emplace(process.pid, Value{});
        if (firstChip) {
            auto prior = previousProcesses_.find(process.pid);
            cpu->second = prior != previousProcesses_.end()
                && prior->second.ticks.start == processStartTicks ? Value(125.0) : Value{};
            previousProcesses_[process.pid] = {{100, processStartTicks, 0}, Clock::now()};
        }
        process.cpu = cpu->second;
        process.startTicks = processStartTicks;
        process.rssBytes = 128.0;
    }
    if (procThrows) throw std::runtime_error("/proc threw");
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    hostCompleted = Clock::now();
}
}

int main() {
    try {
        SampleRound sampling;
        auto before = Clock::now();
        auto first = sampling.collect();
        auto after = Clock::now();
        check(first.sampled >= hostCompleted && first.sampled <= after,
              "sample time must follow both sources");
        check(first.sampled >= before && first.collectionSeconds >= 0.002,
              "collection duration must include both sources");
        check(first.devices.size() == 2 && first.devices[0].memory.percent() == Value(0.0)
              && !first.devices[0].core,
              "valid zero and unavailable metrics must remain distinct");
        check(first.processes.size() == 2 && first.processes[0].npuBytes == Value(256.0)
              && !first.processes[0].cpu && first.processes[0].startTicks == 900,
              "host data must enrich device process occupancy");
        check(first.processes[1].pid == 42 && first.processes[1].chip == 1
              && first.processes[1].npuBytes == Value(512.0),
              "same PID occupancy on another chip must remain separate");
        check(!first.host.cpu && first.devices[0].deviceShare == true,
              "first sample keeps delta unavailable and device share state");

        auto second = sampling.collect();
        check(second.host.cpu == Value(25.0) && second.devices[0].deviceShare == false
              && second.processes[0].cpu == Value(125.0)
              && second.processes[0].startTicks == 900,
              "source state must survive consecutive rounds");

        processStartTicks = 901;
        auto reused = sampling.collect();
        check(reused.processes[0].startTicks == 901 && !reused.processes[0].cpu
              && reused.processes[1].startTicks == 901 && !reused.processes[1].cpu,
              "PID reuse must invalidate CPU deltas on both chips");

        dcmiError = true;
        auto partial = sampling.collect();
        check(partial.error == "DCMI unavailable" && partial.devices.empty()
              && partial.host.memory.used == Value(512.0),
              "DCMI error must retain readable host data");
        check(partial.sampled >= hostCompleted,
              "partial sample must also use completion time");

        dcmiThrows = true;
        bool threw = false;
        dcmiError = false;
        try { sampling.collect(); }
        catch (const SampleRoundError& error) {
            threw = std::string(error.what()) == "DCMI threw" && error.partial().devices.size() == 2;
        }
        check(threw, "source exceptions must retain partial device data for the caller");

        dcmiThrows = false;
        procThrows = true;
        threw = false;
        try { sampling.collect(); }
        catch (const SampleRoundError& error) {
            threw = std::string(error.what()) == "/proc threw"
                && error.partial().devices.size() == 2
                && error.partial().host.memory.used == Value(512.0);
        }
        check(threw, "host source exceptions must retain readable partial data");
        std::cout << "sampling round behavior passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

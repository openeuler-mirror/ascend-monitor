#pragma once
#include <chrono>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace ascend {
using Value = std::optional<double>;
using Clock = std::chrono::steady_clock;
struct Memory {
    Value used, total; // bytes, capacity (never bandwidth utilization)
    std::string kind;
    Value percent() const;
};
struct Device {
    int card = -1, chip = -1;
    std::optional<unsigned> physical, logical;
    std::string name = "N/A", pci = "N/A";
    Value temperature, power, core;
    std::optional<bool> deviceShare;
    Memory memory;
    bool processesAvailable = false;
};
struct Process {
    int card = -1, chip = -1, pid = -1;
    std::optional<unsigned> physical;
    std::string user = "N/A", command = "N/A";
    Value npuBytes, cpu, memoryPercent, elapsed;
    std::optional<uint64_t> startTicks; // /proc starttime; never use PID alone for selection.
    Value rssBytes;
};
struct Host {
    Value cpu, load1, load5, load15;
    Memory memory, swap;
    std::string user, hostname;
    bool root = false;
};
struct Snapshot {
    Clock::time_point sampled = Clock::now();
    std::chrono::system_clock::time_point wall = std::chrono::system_clock::now();
    Host host;
    std::vector<Device> devices;
    std::vector<Process> processes;
    std::string driver = "N/A", cann = "N/A", error;
    double collectionSeconds = 0;
};
struct Aggregate { Value memory, core; size_t memoryCount = 0, coreCount = 0, total = 0; };
Aggregate aggregate(const Snapshot& snapshot);
struct HistoryPoint { Clock::time_point time; Value cpu, memory, swap, npuMemory, npuCore; };
class History {
public:
    void append(const Snapshot& snapshot);
    void appendPoint(HistoryPoint point);
    const std::deque<HistoryPoint>& points() const { return points_; }
private:
    std::deque<HistoryPoint> points_;
};
std::string number(Value value, int precision = 1, const std::string& suffix = "");
std::string bytes(Value value);
std::string duration(Value seconds);
std::string safeText(const std::string& text);
std::string snapshotJson(const Snapshot& snapshot);
}

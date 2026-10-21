#pragma once
#include "top/ProcessTable.h"
#include <set>
#include <utility>

namespace ascend {
struct ProcessIdentity {
    int pid;
    uint64_t startTicks;
    bool operator==(const ProcessIdentity& other) const { return pid == other.pid && startTicks == other.startTicks; }
    bool operator!=(const ProcessIdentity& other) const { return !(*this == other); }
    bool matches(const Process& process) const;
};
class ProcessSelection {
public:
    bool select(const Process& process);
    void move(const std::vector<const Process*>& rows, int delta);
    void reconcile(const std::vector<const Process*>& rows);
    std::optional<size_t> index(const std::vector<const Process*>& rows) const;
    std::optional<ProcessIdentity> identity() const { return identity_; }
    void clear() { identity_.reset(); }
private:
    std::optional<ProcessIdentity> identity_;
    int card_ = -1, chip_ = -1;
};
enum class ProcessState { Available, Unavailable, Exited };
struct ProcessMetrics {
    ProcessState state = ProcessState::Unavailable;
    Clock::time_point sampled{};
    std::string user = "N/A", command = "N/A";
    Value npuBytes, cpu, hostBytes, hostPercent, elapsed;
    std::set<std::pair<int, int>> devices;
};
// Collect only the selected process, inside the sampler, including while help/details are open.
// History fields: cpu = CPU %, memory = RSS bytes, npuMemory = summed NPU bytes.
class ProcessInspection {
public:
    explicit ProcessInspection(double interval = 1) : interval_(interval), history_(interval) {}
    void target(std::optional<ProcessIdentity> identity);
    void append(const Snapshot& snapshot);
    std::optional<ProcessIdentity> identity() const { return identity_; }
    const ProcessMetrics& current() const { return current_; }
    const History& history() const { return history_.history(); }
private:
    double interval_;
    std::optional<ProcessIdentity> identity_;
    ProcessMetrics current_;
    BufferedHistory history_;
};
std::set<std::pair<int, int>> processDevices(const Snapshot& snapshot, const ProcessIdentity& identity);
}

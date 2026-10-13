#pragma once
#include "top/Model.h"
#include <map>
#include <utility>

namespace ascend {
struct CpuTicks { uint64_t total = 0, idle = 0; };
struct ProcessTicks { uint64_t cpu = 0, start = 0, rss = 0; };
std::optional<CpuTicks> parseCpu(const std::string& text);
std::optional<ProcessTicks> parseProcessStat(const std::string& text);
class ProcReader {
public:
    explicit ProcReader(std::string root = "/proc");
    void collect(Snapshot& snapshot);
private:
    std::string root_;
    std::optional<CpuTicks> previousCpu_;
    struct Previous { ProcessTicks ticks; Clock::time_point time; };
    std::map<int, Previous> previousProcesses_;
    Host identity_;
};
std::string readText(const std::string& path);
}

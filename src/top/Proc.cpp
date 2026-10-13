#include "top/Proc.h"
#include <algorithm>
#include <fstream>
#include <iterator>
#include <limits>
#include <pwd.h>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace ascend {
std::string readText(const std::string& path) {
    std::ifstream f(path); if (!f) return {};
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}
std::optional<CpuTicks> parseCpu(const std::string& text) {
    std::istringstream in(text); std::string cpu; in >> cpu;
    if (cpu != "cpu") return {};
    uint64_t fields[8]{}; for (auto& f : fields) if (!(in >> f)) return {};
    CpuTicks out; for (auto f : fields) out.total += f;
    out.idle = fields[3] + fields[4]; // idle + iowait; exclude duplicate guest ticks
    return out;
}
std::optional<ProcessTicks> parseProcessStat(const std::string& text) {
    auto end = text.rfind(')'); if (end == std::string::npos) return {};
    std::istringstream in(text.substr(end + 1)); std::vector<std::string> fields; std::string f;
    while (in >> f) fields.push_back(f);
    if (fields.size() < 22) return {};
    try {
        auto value = [&](size_t n) { size_t used = 0; if (fields[n].empty() || fields[n][0] == '-') throw 0;
            auto v = std::stoull(fields[n], &used); if (used != fields[n].size()) throw 0; return v; };
        return ProcessTicks{value(11) + value(12), value(19), value(21)};
    } catch (...) { return {}; }
}
static std::string username(uid_t uid) {
    char buf[16384]; passwd entry{}, *result = nullptr;
    if (getpwuid_r(uid, &entry, buf, sizeof(buf), &result) == 0 && result) return safeText(entry.pw_name);
    return std::to_string(uid);
}
ProcReader::ProcReader(std::string root) : root_(std::move(root)) {
    identity_.root = geteuid() == 0; identity_.user = username(geteuid());
    char host[256]{}; identity_.hostname = gethostname(host, sizeof(host) - 1) == 0 ? safeText(host) : "N/A";
}
void ProcReader::collect(Snapshot& s) {
    s.host = identity_;
    auto ticks = parseCpu(readText(root_ + "/stat"));
    if (ticks && previousCpu_ && ticks->total > previousCpu_->total && ticks->idle >= previousCpu_->idle) {
        double total = ticks->total - previousCpu_->total, idle = ticks->idle - previousCpu_->idle;
        if (idle <= total) s.host.cpu = 100 * (total - idle) / total;
    }
    previousCpu_ = ticks;
    std::istringstream load(readText(root_ + "/loadavg")); double l1, l5, l15;
    if (load >> l1 >> l5 >> l15) { s.host.load1 = l1; s.host.load5 = l5; s.host.load15 = l15; }
    std::istringstream mem(readText(root_ + "/meminfo")); std::map<std::string, double> values; std::string line;
    while (std::getline(mem, line)) { std::istringstream in(line); std::string key, unit; double v;
        if (in >> key >> v >> unit && unit == "kB" && v >= 0) values[key] = v * 1024; }
    auto setMemory = [&](Memory& m, const char* totalKey, const char* freeKey) {
        auto t = values.find(totalKey), f = values.find(freeKey);
        if (t != values.end()) m.total = t->second;
        if (t != values.end() && f != values.end() && t->second >= f->second) m.used = t->second - f->second;
    };
    setMemory(s.host.memory, "MemTotal:", "MemAvailable:");
    setMemory(s.host.swap, "SwapTotal:", "SwapFree:");
    double uptime = -1; std::istringstream(readText(root_ + "/uptime")) >> uptime;
    long hz = sysconf(_SC_CLK_TCK), pageSize = sysconf(_SC_PAGESIZE);
    std::map<int, Process> cache; std::map<int, Previous> next;
    for (auto& p : s.processes) {
        auto found = cache.find(p.pid);
        if (found == cache.end()) {
            Process info; info.pid = p.pid; std::string dir = root_ + "/" + std::to_string(p.pid);
            auto stat = parseProcessStat(readText(dir + "/stat")); auto now = Clock::now();
            if (stat) {
                if (hz > 0 && uptime >= static_cast<double>(stat->start) / hz) info.elapsed = uptime - static_cast<double>(stat->start) / hz;
                if (pageSize > 0 && s.host.memory.total && *s.host.memory.total > 0)
                    info.memoryPercent = static_cast<double>(stat->rss) * pageSize * 100 / *s.host.memory.total;
                auto prev = previousProcesses_.find(p.pid);
                if (prev != previousProcesses_.end() && hz > 0 && prev->second.ticks.start == stat->start && stat->cpu >= prev->second.ticks.cpu) {
                    double elapsed = std::chrono::duration<double>(now - prev->second.time).count();
                    if (elapsed > 0) info.cpu = (stat->cpu - prev->second.ticks.cpu) * 100.0 / hz / elapsed;
                }
                struct stat st{}; if (::stat(dir.c_str(), &st) == 0) info.user = username(st.st_uid);
                auto command = readText(dir + "/cmdline");
                if (command.empty()) command = readText(dir + "/comm");
                if (!command.empty()) { info.command = safeText(command); while (!info.command.empty() && info.command.back() == ' ') info.command.pop_back(); }
                // PID reuse or exit between files must not mix identities.
                auto confirm = parseProcessStat(readText(dir + "/stat"));
                if (!confirm || confirm->start != stat->start) info = Process{};
                else next.emplace(p.pid, Previous{*stat, now});
            }
            found = cache.emplace(p.pid, info).first;
        }
        p.user = found->second.user; p.command = found->second.command; p.cpu = found->second.cpu;
        p.memoryPercent = found->second.memoryPercent; p.elapsed = found->second.elapsed;
    }
    previousProcesses_ = std::move(next);
}
}

#include "top/Model.h"
#include "top/Graph.h"
#include "top/Proc.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unistd.h>
using namespace ascend;
static void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
static void near(Value v, double want) { check(v && std::abs(*v - want) < .00001, "numeric value mismatch"); }
static std::string statText(uint64_t cpu, uint64_t start, uint64_t rss) {
    // fields 3..24; comm intentionally contains spaces and ')'.
    return "42 (worker ) name) S 1 2 3 4 5 6 7 8 9 10 " + std::to_string(cpu) + " 0 0 0 0 0 0 0 " + std::to_string(start) + " 0 " + std::to_string(rss);
}
static void write(const std::filesystem::path& path, const std::string& data) { std::ofstream(path) << data; }
int main() {
    try {
        auto full = brailleGraph({100.0, 100.0}, 2);
        check(full.size() == 2 && full[0] == L"\u28ff" && full[1] == L"\u28ff", "full Braille cells");
        auto baseline = brailleGraph({0.0, 0.0}, 2);
        check(baseline[0] == L" " && baseline[1] == L"\u28c0", "zero is bottom dots");
        auto missingGraph = brailleGraph({Value{}, Value{}, 0.0}, 1);
        check(missingGraph[0] == L" \u2840", "missing is blank; odd column retained");
        check(brailleGraph({0.0, 0.0}, 2, true)[0] == L"\u2809", "inverted zero is top dots");
        check(brailleGraph({50.0, Value{}}, 2)[0] == L" ", "half-height pixel precision");
        check(brailleGraph({-1.0, 101.0}, 1)[0] == L"\u28f8", "graph values clamped");
        check(brailleGraph({17.3, 17.3}, 4, true)[0] == L"\u283f", "memory uses three dots at 17.3 percent");
        check(brailleGraph({2.0, 2.0}, 5)[4] == L"\u28c0", "low CPU stays on baseline");
        check(brailleGraph({0.0}, 0).empty(), "zero-height graph");
        Memory missing; check(!missing.percent(), "missing memory must stay missing");
        near(Memory{0.0, 100.0, "DDR"}.percent(), 0); near(Memory{0.0, 0.0, ""}.percent(), 0);
        check(!Memory{101.0, 100.0, "DDR"}.percent(), "invalid memory rejected");
        Snapshot s; Device d; d.memory = {50.0, 100.0, "DDR"}; d.core = 0; s.devices.push_back(d);
        d.memory = {0.0, 900.0, "DDR"}; d.core = 80; s.devices.push_back(d); s.devices.push_back(Device{});
        auto a = aggregate(s); near(a.memory, 5); near(a.core, 40); check(a.memoryCount == 2 && a.coreCount == 2 && a.total == 3, "partial coverage");
        History h; auto base = Clock::now(); s.sampled = base; h.append(s); h.append(s); check(h.points().size() == 1, "no duplicate history");
        s.sampled += std::chrono::seconds(301); s.devices.clear(); h.append(s); check(!h.points().back().npuMemory, "missing sample is gap");
        s.sampled = base + std::chrono::seconds(601); h.append(s); check(h.points().size() == 2, "history bounded by time");
        auto cpu = parseCpu("cpu 100 2 30 400 10 5 6 7 1000 2000\ncpu0 1 2");
        check(cpu && cpu->total == 560 && cpu->idle == 410, "guest ticks not counted twice");
        check(!parseCpu("cpu 1 2"), "short cpu stat rejected");
        auto p = parseProcessStat(statText(123, 400, 200)); check(p && p->cpu == 123 && p->start == 400 && p->rss == 200, "stat parentheses parsing");
        check(!parseProcessStat("42 (x) S broken"), "short process stat rejected");
        char path[] = "/tmp/ascend-monitor-tui-test-XXXXXX"; auto root = mkdtemp(path); check(root, "mkdtemp");
        struct Cleanup { std::filesystem::path p; ~Cleanup() { std::filesystem::remove_all(p); } } cleanup{root};
        std::filesystem::path dir(root); std::filesystem::create_directory(dir / "42");
        write(dir / "stat", "cpu 100 0 0 100 0 0 0 0"); write(dir / "loadavg", "1 2 3 1/2 42");
        write(dir / "meminfo", "MemTotal: 10000 kB\nMemAvailable: 2500 kB\nSwapTotal: 0 kB\nSwapFree: 0 kB\n");
        write(dir / "uptime", "10000 0"); write(dir / "42/stat", statText(100, 400, 100)); write(dir / "42/cmdline", std::string("worker\0--flag\0", 14));
        ProcReader reader(root); Snapshot first; Process proc; proc.pid = 42; proc.card = 1; proc.chip = 0; proc.npuBytes = 100;
        first.processes.push_back(proc); proc.chip = 1; proc.npuBytes = 200; first.processes.push_back(proc);
        reader.collect(first); check(!first.host.cpu && !first.processes[0].cpu, "first delta unavailable"); near(first.host.memory.percent(), 75); near(first.host.swap.percent(), 0);
        write(dir / "stat", "cpu 125 0 0 175 0 0 0 0"); write(dir / "42/stat", statText(150, 400, 100));
        Snapshot second; second.processes = first.processes; reader.collect(second); near(second.host.cpu, 25);
        check(second.processes[0].cpu && second.processes[0].cpu == second.processes[1].cpu, "same PID sampled once across devices");
        near(second.processes[0].npuBytes, 100); near(second.processes[1].npuBytes, 200);
        write(dir / "42/stat", statText(1, 500, 100)); Snapshot reused; reused.processes = first.processes; reader.collect(reused); check(!reused.processes[0].cpu, "PID reuse resets delta");
        std::filesystem::remove(dir / "42/stat"); Snapshot exited; exited.processes = first.processes; reader.collect(exited); check(!exited.processes[0].cpu && exited.processes[0].command == "N/A", "exited process unavailable");
        check(safeText("a\033\nb") == "a  b", "terminal control injection sanitized");
        check(duration(360001) == "100:00:01", "long elapsed duration");
        check(snapshotJson(exited).find("\"cpu_percent\":null") != std::string::npos, "JSON missing vs zero");
        std::cout << "ascend-monitor-tui core tests passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}

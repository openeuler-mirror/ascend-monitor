#include "top/ProcessView.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace ascend;
static void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
static void near(Value value, double want) { check(value && std::abs(*value - want) < .0001, "numeric reading mismatch"); }
int main() {
    try {
        Snapshot s; const auto base = Clock::now(); s.sampled = base;
        Device d; d.card = 6; d.chip = 0; d.processesAvailable = true;
        s.devices.push_back(d); d.chip = 1; s.devices.push_back(d);
        Process p; p.pid = 100; p.startTicks = 1000; p.card = 6; p.chip = 0;
        p.npuBytes = 10; p.cpu = 250; p.rssBytes = 80; p.memoryPercent = 20; p.command = "worker";
        s.processes.push_back(p); p.chip = 1; p.npuBytes = 30; s.processes.push_back(p);
        p.pid = 200; p.startTicks = 2000; p.npuBytes = 60; s.processes.push_back(p);
        ProcessSelection selection;
        ProcessSort sort;
        auto rows = sortedProcesses(s, sort);
        check(!selection.identity() && !selection.index(rows), "startup unselected");
        selection.move(rows, 1); check(selection.identity()->pid == 200 && selection.index(rows) == 0, "down selects first");
        selection.move(rows, 1); check(selection.identity()->pid == 100, "down selects next");
        sort.descending = false; rows = sortedProcesses(s, sort);
        check(selection.index(rows) == 1, "sort follows row identity");
        auto linked = processDevices(s, *selection.identity());
        check(linked.size() == 2 && linked.count({6, 0}) && linked.count({6, 1}), "all process chips linked");
        s.processes.erase(s.processes.begin() + 1); rows = sortedProcesses(s, sort);
        selection.reconcile(rows); check(selection.identity()->pid == 100 && selection.index(rows), "follow same process after device row disappears");
        s.processes[0].startTicks.reset(); rows = sortedProcesses(s, sort);
        selection.reconcile(rows); check(selection.identity() && !selection.index(rows), "temporary identity failure cannot select another row");
        selection.clear(); selection.move(rows, 1); check(selection.identity()->pid == 200, "navigation skips unverified identity");
        selection.clear(); selection.move({}, 1); check(!selection.identity(), "empty list stays unselected");
        s.processes[0].startTicks = 1000; selection.select(s.processes[0]);
        s.processes[0].startTicks = 1001; rows = sortedProcesses(s, sort);
        selection.reconcile(rows); check(!selection.identity(), "PID reuse clears selection");
        s.processes[0].startTicks = 1000;
        p = s.processes[0]; p.chip = 1; p.npuBytes = 30; s.processes.push_back(p);
        ProcessInspection view(1); view.target(ProcessIdentity{100, 1000}); view.append(s);
        near(view.current().npuBytes, 40); near(view.current().cpu, 250); near(view.current().hostBytes, 80);
        check(view.current().devices.size() == 2 && view.history().points().empty(), "first unfinished bucket has no invented history");
        s.sampled = base + std::chrono::milliseconds(500); s.processes[0].cpu = 350; view.append(s);
        view.append(s); // Repeated renders must not duplicate samples.
        s.sampled = base + std::chrono::seconds(1); view.append(s);
        near(view.history().points().back().cpu, 300);
        near(view.history().points().back().npuMemory, 40);
        near(view.history().points().back().memory, 80);
        view.target(ProcessIdentity{100, 1000}); check(view.history().points().size() == 1, "same target preserves history");
        s.devices[1].processesAvailable = false;
        s.sampled += std::chrono::seconds(1); view.append(s);
        check(!view.current().npuBytes && view.current().cpu, "partial NPU data not presented as complete total");
        s.devices[1].processesAvailable = true;
        s.sampled += std::chrono::seconds(1); view.append(s);
        check(!view.history().points().back().npuMemory, "partial-data window is a gap");
        for (auto& proc : s.processes) if (proc.pid == 100) proc.startTicks.reset();
        s.sampled += std::chrono::seconds(1); view.append(s);
        check(view.current().state == ProcessState::Unavailable && !view.current().cpu && !view.current().npuBytes,
              "unverified /proc readings are not assigned to old process");
        for (auto& proc : s.processes) if (proc.pid == 100) proc.startTicks = 1001;
        s.sampled += std::chrono::seconds(1); view.append(s);
        check(view.current().state == ProcessState::Exited && view.current().command == "worker", "PID reuse cannot replace detail target");
        check(!view.current().npuBytes && !view.current().hostBytes, "exit has no fake zero readings");
        s.sampled += std::chrono::seconds(700); view.append(s);
        check(!view.history().points().empty() && view.history().points().size() <= 601, "history bounded after long outage");
        view.target(ProcessIdentity{200, 2000}); check(view.history().points().empty(), "different process resets history");
        view.append(s); near(view.current().npuBytes, 60);
        view.target({}); check(view.history().points().empty() && !view.identity(), "deselect releases history");
        std::cout << "process selection and details tests passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}

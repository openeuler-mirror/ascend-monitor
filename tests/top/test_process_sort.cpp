#include "top/ProcessTable.h"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace ascend;
static void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
int main() {
    try {
        Snapshot s;
        auto add = [&](int pid, int chip, Value npu, Value ram, Value cpu) {
            Process p; p.pid = pid; p.card = 5; p.chip = chip; p.physical = chip;
            p.npuBytes = npu; p.memoryPercent = ram; p.cpu = cpu; s.processes.push_back(p);
        };
        add(20, 0, 5, 40, 250); add(10, 1, 30, 10, 0);
        add(20, 1, 10, 40, 250); // Same PID on two chips: never merge or add CPU/RAM.
        add(40, 0, {}, {}, {}); add(30, 0, 0, 0, 0);
        ProcessSort sort;
        auto rows = sortedProcesses(s, sort);
        check(rows[0]->pid == 10 && rows[1]->chip == 1 && rows[2]->chip == 0,
              "global NPU memory order crosses device groups");
        check(rows[3]->pid == 30 && rows[4]->pid == 40, "zero before missing descending");
        sort.select(ProcessSortField::NpuMemory);
        rows = sortedProcesses(s, sort);
        check(!sort.descending && rows[0]->pid == 30 && rows[4]->pid == 40, "ascending keeps missing last");
        sort.select(ProcessSortField::HostMemory);
        rows = sortedProcesses(s, sort);
        check(sort.descending && rows[0]->pid == 20 && rows[0]->chip == 0 && rows[1]->chip == 1,
              "new field descends; equal values tie by PID and device");
        check(rows[0]->memoryPercent == 40 && rows[1]->memoryPercent == 40, "host readings not summed");
        sort.next(); check(sort.field == ProcessSortField::Cpu && sort.descending, "RAM to CPU");
        rows = sortedProcesses(s, sort);
        check(rows[0]->pid == 20 && rows[1]->pid == 20 && rows[2]->pid == 10 && rows[3]->pid == 30,
              "CPU >100 and zero ties");
        sort.select(ProcessSortField::Cpu); check(!sort.descending, "repeat click reverses");
        sort.next(); check(sort.field == ProcessSortField::NpuMemory && sort.descending, "CPU to NPU");
        sort.next(); check(sort.field == ProcessSortField::HostMemory && sort.descending, "NPU to RAM");
        check(s.processes[0].pid == 20 && s.processes[1].pid == 10, "snapshot order untouched");
        for (int width : {84, 114, 194}) {
            ProcessHeader header{17, 3, width};
            for (const auto& c : processSortColumns) {
                check(header.hit(17, 3 + c.offset) == c.field, "column left boundary");
                check(header.hit(17, 3 + c.offset + c.width - 1) == c.field, "column right boundary");
                check(!header.hit(18, 3 + c.offset), "body is not clickable header");
            }
            check(!header.hit(17, 26) && !header.hit(17, 53), "only three sortable columns");
        }
        check(!ProcessHeader{}.hit(0, 27), "hidden header has no target");
        check(!ProcessHeader{17, 3, 30}.hit(17, 39), "clipped area not clickable");
        s.processes[0].cpu = std::numeric_limits<double>::quiet_NaN();
        s.processes[1].cpu = std::numeric_limits<double>::infinity();
        sort.field = ProcessSortField::Cpu;
        for (bool descending : {true, false}) {
            sort.descending = descending; rows = sortedProcesses(s, sort);
            check(rows[0]->pid != 10 && rows[0] != &s.processes[0], "nonfinite values treated as unavailable");
        }
        check(sortedProcesses(Snapshot{}, sort).empty(), "empty processes");
        std::cout << "process sorting tests passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}

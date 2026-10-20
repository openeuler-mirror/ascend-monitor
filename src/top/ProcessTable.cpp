#include "top/ProcessTable.h"
#include <algorithm>
#include <cmath>
#include <tuple>

namespace ascend {
void ProcessSort::select(ProcessSortField selected) {
    descending = selected == field ? !descending : true;
    field = selected;
}
void ProcessSort::next() {
    switch (field) {
    case ProcessSortField::NpuMemory: field = ProcessSortField::HostMemory; break;
    case ProcessSortField::HostMemory: field = ProcessSortField::Cpu; break;
    case ProcessSortField::Cpu: field = ProcessSortField::NpuMemory; break;
    }
    descending = true;
}
std::optional<ProcessSortField> ProcessHeader::hit(int row, int column) const {
    if (y < 0 || row != y || column < x || column >= x + width) return {};
    for (const auto& c : processSortColumns)
        if (column >= x + c.offset && column < x + c.offset + c.width) return c.field;
    return {};
}
std::vector<const Process*> sortedProcesses(const Snapshot& snapshot, const ProcessSort& sort) {
    std::vector<const Process*> rows;
    for (const auto& p : snapshot.processes) rows.push_back(&p);
    auto value = [&](const Process& p) -> Value {
        Value v;
        switch (sort.field) {
        case ProcessSortField::NpuMemory: v = p.npuBytes; break;
        case ProcessSortField::HostMemory: v = p.memoryPercent; break;
        case ProcessSortField::Cpu: v = p.cpu; break;
        }
        return v && std::isfinite(*v) ? v : Value{};
    };
    std::stable_sort(rows.begin(), rows.end(), [&](const Process* a, const Process* b) {
        auto av = value(*a), bv = value(*b);
        if (av.has_value() != bv.has_value()) return av.has_value(); // Missing always last.
        if (av && *av != *bv) return sort.descending ? *av > *bv : *av < *bv;
        return std::make_tuple(a->pid, a->physical.value_or(~0u), a->card, a->chip)
             < std::make_tuple(b->pid, b->physical.value_or(~0u), b->card, b->chip);
    });
    return rows;
}
}

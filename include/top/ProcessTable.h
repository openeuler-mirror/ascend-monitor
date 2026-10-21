#pragma once
#include "top/Model.h"
#include <array>

namespace ascend {
enum class ProcessSortField { NpuMemory, HostMemory, Cpu };
struct ProcessSort {
    ProcessSortField field = ProcessSortField::NpuMemory;
    bool descending = true;
    void select(ProcessSortField selected);
    void next();
};
struct ProcessSortColumn {
    ProcessSortField field;
    const char* label;
    int offset, width;
};
// Offsets relative to the process table's first text column; shared by drawing and hit testing.
inline constexpr std::array<ProcessSortColumn, 3> processSortColumns{{
    {ProcessSortField::NpuMemory, "NPU-MEM", 24, 12},
    {ProcessSortField::Cpu, "%CPU", 36, 7},
    {ProcessSortField::HostMemory, "%MEM", 43, 7},
}};
struct ProcessHeader {
    int y = -1, x = 0, width = 0;
    std::optional<ProcessSortField> hit(int row, int column) const;
};
std::vector<const Process*> sortedProcesses(const Snapshot& snapshot, const ProcessSort& sort);
}

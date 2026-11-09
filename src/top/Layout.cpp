#include "top/Layout.h"
#include <algorithm>

namespace ascend {
MainLayout mainLayout(int rows, int columns, std::size_t devices, bool forceCompact) {
    MainLayout layout;
    if (rows < 24 || columns < 90) return layout;
    layout.valid = true;
    constexpr int minimumProcessRows = 5;
    // Include one placeholder row even when no devices were discovered.
    const auto slots = std::max<std::size_t>(1, devices);
    layout.plotHeight = rows >= 45 ? 3 : 1;
    layout.historyHeight = 5 + 2 * layout.plotHeight;
    // Detailed devices use two data rows plus a separator. Compact devices
    // share one separator and the process frame shares the history border.
    const int detailedBudget = rows - 15 - layout.historyHeight - minimumProcessRows;
    layout.compact = forceCompact || slots > static_cast<std::size_t>(std::max(0, detailedBudget / 3));
    const int fixed = layout.compact ? 13 : 15;
    const int stride = layout.compact ? 1 : 3;
    const int fullHistoryBudget = rows - fixed - layout.historyHeight - minimumProcessRows;
    layout.historySummary = layout.compact && rows < 32
        && slots > static_cast<std::size_t>(std::max(0, fullHistoryBudget));
    if (layout.historySummary) layout.historyHeight = 2;
    const int capacity = (rows - fixed - layout.historyHeight - minimumProcessRows) / stride;
    layout.visibleDevices = static_cast<int>(std::min(devices, static_cast<std::size_t>(capacity)));
    layout.deviceTop = layout.compact ? 6 : 7;
    const int deviceHeight = std::max(1, layout.visibleDevices) * stride;
    layout.historyTop = layout.deviceTop + deviceHeight + (layout.compact ? 1 : 0);
    layout.processTop = layout.historyTop + layout.historyHeight + (layout.compact ? 0 : 2);
    layout.processDataTop = layout.processTop + 4;
    layout.processRows = rows - layout.processDataTop - 2;
    return layout;
}
}

#pragma once
#include <cstddef>

namespace ascend {
struct MainLayout {
    bool valid = false;
    bool compact = false;
    bool historySummary = false;
    int plotHeight = 0;
    int historyHeight = 0;
    int deviceTop = 0;
    int visibleDevices = 0;
    int historyTop = 0;
    int processTop = 0;
    int processDataTop = 0;
    int processRows = 0;
};

// Reserve five process rows before assigning the remaining space to devices.
MainLayout mainLayout(int rows, int columns, std::size_t devices, bool forceCompact);
}

#include "top/Layout.h"
#include <iostream>
#include <stdexcept>
using namespace ascend;
static void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
int main() {
    try {
        for (int height = 24; height <= 100; ++height) {
            for (int width : {90, 120, 200}) for (int count : {0, 1, 3, 8, 16, 256}) {
                for (bool forced : {false, true}) {
                    auto layout = mainLayout(height, width, count, forced);
                    check(layout.valid && layout.processRows >= 5, "five process rows must fit at every supported size");
                    check(layout.visibleDevices <= count && (count == 0 || layout.visibleDevices > 0), "device viewport must fit and be usable");
                    check(layout.processDataTop + layout.processRows + 2 == height, "bottom border and message row reserved");
                    check(!forced || layout.compact, "manual compact survives resizing");
                    check(layout.compact || layout.visibleDevices == count, "auto detailed shows every device");
                    check(layout.historyTop > layout.deviceTop && layout.processTop >= layout.historyTop + layout.historyHeight,
                          "panels must not overlap");
                }
            }
        }
        auto small = mainLayout(24, 90, 16, false);
        check(small.compact && small.historySummary && small.visibleDevices == 4 && small.processRows == 5, "24-row regression");
        auto medium = mainLayout(28, 120, 8, false);
        check(medium.visibleDevices == 8 && medium.processRows == 5, "28-row budget");
        auto regular = mainLayout(36, 120, 8, false);
        check(!regular.historySummary && regular.visibleDevices == 8 && regular.processRows == 8, "36-row history preserved");
        check(!mainLayout(55, 120, 8, false).compact, "auto restores detailed devices when they fit");
        check(!mainLayout(23, 90, 8, false).valid && !mainLayout(24, 89, 8, true).valid, "minimum terminal size");
        std::cout << "layout budget tests passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}

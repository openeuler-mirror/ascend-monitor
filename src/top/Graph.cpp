#include "top/Graph.h"
#include <algorithm>
#include <cmath>

namespace ascend {
std::vector<Value> historyColumns(const History& history, Value HistoryPoint::*member,
    int count, double secondsPerColumn, Clock::time_point end) {
    if (count <= 0 || secondsPerColumn <= 0) return {};
    std::vector<Value> columns(static_cast<size_t>(count));
    // A process-wide steady-clock lattice keeps an old column's timestamp
    // fixed while the visible window scrolls in whole columns.
    auto slotOf = [&](Clock::time_point time) {
        return std::llround(std::chrono::duration<double>(time.time_since_epoch()).count()
                            / secondsPerColumn);
    };
    const auto endSlot = slotOf(end);
    auto columnOf = [&](const HistoryPoint& point) {
        return static_cast<long long>(count) - 1 - (endSlot - slotOf(point.time));
    };
    // Place completed snapshots first so interpolation never changes a measured peak.
    for (const auto& point : history.points()) {
        if (point.time > end) break;
        auto col = columnOf(point);
        if (col >= 0 && col < count) columns[col] = point.*member;
    }
    // Connect normal collection delays only. Missing readings and long stalls
    // remain gaps; recorded samples in History are never modified.
    const double maxGap = secondsPerColumn + std::max(2.0, secondsPerColumn * 0.5);
    const auto& points = history.points();
    for (size_t i = 1; i < points.size(); ++i) {
        const auto& older = points[i - 1];
        const auto& newer = points[i];
        if (newer.time > end) break;
        const auto& from = older.*member;
        const auto& to = newer.*member;
        if (!from || !to || !std::isfinite(*from) || !std::isfinite(*to)) continue;
        double gap = std::chrono::duration<double>(newer.time - older.time).count();
        if (gap <= 0 || gap > maxGap) continue;
        auto first = std::max(0LL, columnOf(older) + 1);
        auto last = std::min(static_cast<long long>(count), columnOf(newer));
        for (auto col = first; col < last; ++col) {
            if (columns[col]) continue;
            auto slot = endSlot - (count - 1 - col);
            auto at = Clock::time_point{} + std::chrono::duration_cast<Clock::duration>(
                std::chrono::duration<double>(slot * secondsPerColumn));
            double fraction = std::clamp(std::chrono::duration<double>(at - older.time).count() / gap, 0.0, 1.0);
            columns[col] = *from + (*to - *from) * fraction;
        }
    }
    return columns;
}

std::vector<std::wstring> brailleGraph(const std::vector<Value>& columns, int rows, bool inverted) {
    if (rows <= 0) return {};
    std::vector<std::wstring> grid(static_cast<size_t>(rows), std::wstring((columns.size() + 1) / 2, L' '));
    // Unicode Braille Patterns: left dots 1,2,3,7; right dots 4,5,6,8.
    constexpr unsigned bits[2][4] = {{0, 1, 2, 6}, {3, 4, 5, 7}};
    const int pixels = rows * 4;
    for (size_t x = 0; x < columns.size(); ++x) {
        if (!columns[x] || !std::isfinite(*columns[x])) continue;
        // Quantize the partial terminal row separately; full rows have four dots.
        // This keeps low percentages from gaining an extra baseline dot.
        double cellHeight = std::clamp(*columns[x], 0.0, 100.0) * rows / 100.0;
        int whole = static_cast<int>(cellHeight);
        int partial = std::min(4, static_cast<int>(std::nearbyint((cellHeight - whole) * 5.0)));
        int count = std::clamp(whole * 4 + partial, 1, pixels);
        for (int p = 0; p < count; ++p) {
            int y = inverted ? p : pixels - 1 - p;
            auto& cell = grid[y / 4][x / 2];
            unsigned mask = cell == L' ' ? 0 : static_cast<unsigned>(cell) - 0x2800;
            cell = static_cast<wchar_t>(0x2800 | mask | (1u << bits[x % 2][y % 4]));
        }
    }
    return grid;
}
}

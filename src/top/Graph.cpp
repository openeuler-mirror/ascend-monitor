#include "top/Graph.h"
#include <algorithm>
#include <cmath>

namespace ascend {
std::vector<Value> historyColumns(const History& history, Value HistoryPoint::*member,
    int count, double secondsPerColumn, Clock::time_point end) {
    if (count <= 0 || secondsPerColumn <= 0) return {};
    std::vector<Value> columns(static_cast<size_t>(count));
    const auto& points = history.points();
    if (points.empty()) return columns;
    // Anchor on immutable sample slots, not the latest acquisition's jitter.
    auto last = std::find_if(points.rbegin(), points.rend(),
        [end](const HistoryPoint& point) { return point.time <= end; });
    if (last == points.rend()) return columns;
    auto plotEnd = last->plotTime;
    // Hold at most two periods across short delays; retain real outage gaps.
    size_t next = 0;
    const HistoryPoint* previous = nullptr;
    auto period = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(secondsPerColumn));
    for (int col = 0; col < count; ++col) {
        auto time = plotEnd - period * (count - 1 - col);
        while (next < points.size() && points[next].plotTime <= time) previous = &points[next++];
        if (!previous) continue;
        double age = std::chrono::duration<double>(time - previous->plotTime).count();
        if (age <= secondsPerColumn * 2.0) columns[col] = previous->*member;
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

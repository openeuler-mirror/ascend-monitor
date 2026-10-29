#include "top/Graph.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace ascend;
static void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int main() {
    try {
        History history;
        Snapshot sample;
        const auto origin = Clock::time_point{} + std::chrono::seconds(1000);
        auto add = [&](int ms, Value cpu) {
            sample.sampled = origin + std::chrono::milliseconds(ms);
            sample.host.cpu = cpu;
            history.append(sample);
        };
        add(0, 10);
        check(history.points().size() == 1 && history.points().back().cpu == Value(10),
              "first completed snapshot is recorded immediately");
        add(1600, 90);
        add(3200, 20);
        add(3200, 60);
        check(history.points().size() == 3 && history.points()[1].cpu == Value(90),
              "recorded peak is neither averaged nor duplicated");
        auto columns = historyColumns(history, &HistoryPoint::cpu, 8, 1.0, sample.sampled);
        check(columns[4] == Value(10) && columns[6] == Value(90) && columns[7] == Value(20),
              "actual sampled values and peaks remain unchanged");
        check(columns[5] && std::abs(*columns[5] - 60.0) < 1e-9,
              "a short gap is linearly interpolated on a fixed time slot");
        auto wide = historyColumns(history, &HistoryPoint::cpu, 12, 1.0, sample.sampled);
        check(std::equal(columns.begin(), columns.end(), wide.end() - 8),
              "resizing preserves recorded positions and peaks");
        History fixed;
        Snapshot fixedSample;
        const auto fixedOrigin = Clock::time_point{} + std::chrono::seconds(1000);
        auto fixedAdd = [&](int ms, double cpu) {
            fixedSample.sampled = fixedOrigin + std::chrono::milliseconds(ms);
            fixedSample.host.cpu = cpu;
            fixed.append(fixedSample);
        };
        fixedAdd(0, 10);
        fixedAdd(1600, 90);
        fixedAdd(3200, 20);
        auto before = historyColumns(fixed, &HistoryPoint::cpu, 8, 1.0, fixedSample.sampled);
        check(before == historyColumns(fixed, &HistoryPoint::cpu, 8, 1.0,
                            fixedSample.sampled + std::chrono::milliseconds(100)),
              "redraws within the same absolute slot do not reshape history");
        fixedAdd(4800, 10);
        auto after = historyColumns(fixed, &HistoryPoint::cpu, 8, 1.0, fixedSample.sampled);
        check(std::equal(before.begin() + 2, before.end(), after.begin()),
              "completed history keeps its shape when two fixed time slots scroll left");
        add(4800, {});
        auto missing = historyColumns(history, &HistoryPoint::cpu, 8, 1.0, sample.sampled);
        check(!missing.back() && !missing[6] && missing[4] == Value(90),
              "missing reading stays blank and blocks interpolation");
        add(12000, 30);
        auto outage = historyColumns(history, &HistoryPoint::cpu, 8, 1.0, sample.sampled);
        check(std::count_if(outage.begin(), outage.end(), [](Value value) { return value.has_value(); }) == 1
                  && outage.back() == Value(30),
              "long interruption has no carried or interpolated readings");
        add(613000, 40);
        check(history.points().size() == 1 && history.points().back().cpu == Value(40),
              "history retains only the latest 600 seconds");
        check(historyColumns(History{}, &HistoryPoint::cpu, 8, 1.0, sample.sampled)
                  == std::vector<Value>(8), "empty history stays blank");
        std::cout << "Direct snapshot history, short-gap interpolation and outages passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

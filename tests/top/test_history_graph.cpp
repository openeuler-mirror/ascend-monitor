#include "top/Graph.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace ascend;
static void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int main() {
    try {
        BufferedHistory buffered;
        Snapshot sample; auto epoch = Clock::now();
        auto add = [&](int ms, Value cpu) {
            sample.sampled = epoch + std::chrono::milliseconds(ms);
            sample.host.cpu = cpu; sample.host.memory = {20.0, 100.0, ""};
            sample.host.swap = {0.0, 0.0, ""}; buffered.append(sample);
        };
        add(0, 10); add(500, 30);
        check(buffered.history().points().empty(), "unfinished bucket is not plotted");
        add(1000, 50);
        check(buffered.history().points().size() == 1 && buffered.history().points().back().cpu == Value(20),
              "one second produces the mean of its two samples");
        auto frozen = historyColumns(buffered.history(), &HistoryPoint::cpu, 8, 1, sample.sampled);
        add(1500, 70);
        check(historyColumns(buffered.history(), &HistoryPoint::cpu, 8, 1, sample.sampled) == frozen,
              "half second updates cannot mutate finalized points");
        add(2000, {}); add(2500, 90); add(3000, 0);
        check(buffered.history().points()[1].cpu == Value(60), "boundary sample belongs to the next bucket");
        check(!buffered.history().points()[2].cpu, "partial missing bucket stays missing");
        add(6500, 40);
        check(buffered.history().points()[3].cpu == Value(0), "zero is preserved");
        check(!buffered.history().points()[4].cpu && !buffered.history().points()[5].cpu,
              "outage buckets are explicit gaps, not interpolated readings");
        add(7000, 60);
        check(buffered.history().points().back().cpu == Value(40), "recovery resumes on fixed boundaries");
        // A recorded peak must only shift, never be resampled out of history.
        for (double interval : {0.2, 1.0, 2.5}) {
            History peaks(interval); Snapshot peak; auto origin = Clock::now();
            std::vector<Value> previous;
            for (int i = 0; i < 3200; ++i) {
                peak.sampled = origin + std::chrono::milliseconds(static_cast<int>(interval * 1000 * (i + (i % 2 ? 0.1 : 0.0))));
                peak.host.cpu = i % 2 ? 12.0 : 2.0; peaks.append(peak);
                auto frame = historyColumns(peaks, &HistoryPoint::cpu, 8, interval, peak.sampled);
                if (!previous.empty())
                    check(std::equal(previous.begin() + 1, previous.end(), frame.begin()),
                          "recorded peaks must shift unchanged on every new sample");
                check(frame.back() == peak.host.cpu, "newest peak is visible immediately");
                auto wide = historyColumns(peaks, &HistoryPoint::cpu, 17, interval, peak.sampled);
                check(std::equal(frame.begin(), frame.end(), wide.end() - 8), "resize keeps recorded peaks");
                previous = frame;
            }
        }
        History history; Snapshot s; auto start = Clock::now();
        for (int i = 0; i <= 10; ++i) {
            s.sampled = start + std::chrono::milliseconds(i * 1000 + (i % 2 ? 100 : 0));
            s.host.cpu = 2; s.host.memory = {17.0, 100.0, ""}; history.append(s);
        }
        auto columns = historyColumns(history, &HistoryPoint::cpu, 11, 1.0, s.sampled);
        int gaps = static_cast<int>(std::count(columns.begin(), columns.end(), Value{}));
        std::cout << "Continuous 1s sampling with 100ms jitter: " << gaps << " blank columns\n";
        check(gaps == 0, "scheduler jitter must not create holes");
        auto wider = historyColumns(history, &HistoryPoint::cpu, 21, 1.0, s.sampled);
        check(std::equal(columns.begin(), columns.end(), wider.end() - 11), "resizing preserves shared time scale and newest values");
        check(std::all_of(wider.begin(), wider.begin() + 10, [](Value v) { return !v; }), "do not prefill before first sample");
        auto memory = historyColumns(history, &HistoryPoint::memory, 11, 1.0, s.sampled);
        for (size_t i = 0; i < memory.size(); ++i) check(memory[i].has_value() == columns[i].has_value(), "host metrics stay synchronized");
        auto end = s.sampled;
        s.sampled = start + std::chrono::seconds(11); s.host.cpu = {}; history.append(s);
        auto missing = historyColumns(history, &HistoryPoint::cpu, 12, 1.0, s.sampled);
        check(!missing.back() && missing[10], "explicit missing reading stays blank immediately");
        auto stable = historyColumns(history, &HistoryPoint::cpu, 11, 1.0, end);
        check(stable == columns, "later samples cannot move an older snapshot's graph");
        s.sampled = start + std::chrono::seconds(12); s.host.cpu = 0; history.append(s);
        s.sampled = start + std::chrono::seconds(20); s.host.cpu = 10; history.append(s);
        auto outage = historyColumns(history, &HistoryPoint::cpu, 9, 1.0, s.sampled);
        check(outage.front() && *outage.front() == 0, "zero reading is valid");
        check(outage[2] && !outage[3] && !outage[7] && outage[8], "long outage remains blank until recovery");
        auto empty = historyColumns(History{}, &HistoryPoint::cpu, 10, 1.0, end);
        check(std::all_of(empty.begin(), empty.end(), [](Value v) { return !v; }), "empty history");
        std::cout << "History continuity, shared cadence, resize, missing values and outage checks passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}

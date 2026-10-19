#include "top/Model.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace ascend {
Value Memory::percent() const {
    if (!used || !total || *total < 0 || *used < 0 || *used > *total) return {};
    if (*total == 0) return 0.0;
    return *used * 100.0 / *total;
}
Aggregate aggregate(const Snapshot& s) {
    Aggregate a; a.total = s.devices.size(); double used = 0, total = 0, core = 0;
    for (const auto& d : s.devices) {
        if (d.memory.percent()) { used += *d.memory.used; total += *d.memory.total; ++a.memoryCount; }
        if (d.core) { core += *d.core; ++a.coreCount; }
    }
    if (a.memoryCount && total > 0) a.memory = used * 100 / total;
    if (a.coreCount) a.core = core / a.coreCount;
    return a;
}
void History::append(const Snapshot& s) {
    auto a = aggregate(s);
    appendPoint({s.sampled, s.host.cpu, s.host.memory.percent(), s.host.swap.percent(), a.memory, a.core, {}});
}
void History::appendPoint(HistoryPoint point) {
    if (!points_.empty() && point.time <= points_.back().time) return;
    if (!origin_) origin_ = point.time;
    auto elapsed = std::chrono::duration<double>(point.time - *origin_).count();
    auto tick = std::max(lastTick_ + 1, static_cast<int64_t>(std::llround(elapsed / interval_)));
    auto period = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(interval_));
    point.plotTime = *origin_ + period * tick;
    lastTick_ = tick;
    points_.push_back(point);
    while (!points_.empty() && point.time - points_.front().time > std::chrono::seconds(600)) points_.pop_front();
}
void BufferedHistory::append(const Snapshot& s) {
    if (last_ && s.sampled <= *last_) return;
    last_ = s.sampled;
    if (!origin_) origin_ = s.sampled;
    int64_t next = (s.sampled - *origin_) / period_;
    if (next > bucket_) {
        std::array<Value, 5> values{};
        for (size_t i = 0; i < values.size(); ++i)
            if (count_ && !missing_[i]) values[i] = sums_[i] / count_;
        history_.appendPoint({*origin_ + period_ * (bucket_ + 1),
            values[0], values[1], values[2], values[3], values[4], {}});
        // Explicit empty buckets preserve outages without interpolating readings.
        int64_t retained = std::chrono::duration_cast<Clock::duration>(std::chrono::seconds(600)) / period_;
        for (int64_t gap = std::max(bucket_ + 1, next - retained); gap < next; ++gap)
            history_.appendPoint({*origin_ + period_ * (gap + 1), {}, {}, {}, {}, {}, {}});
        bucket_ = next; count_ = 0; sums_.fill(0); missing_.fill(false);
    }
    auto a = aggregate(s);
    std::array<Value, 5> values{s.host.cpu, s.host.memory.percent(), s.host.swap.percent(), a.memory, a.core};
    for (size_t i = 0; i < values.size(); ++i) {
        if (values[i] && std::isfinite(*values[i])) sums_[i] += *values[i];
        else missing_[i] = true;
    }
    ++count_;
}
std::string number(Value v, int precision, const std::string& suffix) {
    if (!v || !std::isfinite(*v)) return "N/A";
    std::ostringstream out; out << std::fixed << std::setprecision(precision) << *v << suffix; return out.str();
}
std::string bytes(Value v) {
    if (!v) return "N/A";
    if (*v >= 1024.0 * 1024 * 1024) return number(*v / (1024.0 * 1024 * 1024), 2, "GiB");
    return number(*v / (1024.0 * 1024), 0, "MiB");
}
std::string duration(Value v) {
    if (!v || *v < 0) return "N/A";
    auto n = static_cast<uint64_t>(*v); std::ostringstream out;
    out << n / 3600 << ':' << std::setfill('0') << std::setw(2) << (n / 60) % 60 << ':' << std::setw(2) << n % 60;
    return out.str();
}
std::string safeText(const std::string& s) {
    std::string out; for (unsigned char c : s) out += (c >= 32 && c != 127) ? static_cast<char>(c) : ' '; return out;
}
static std::string quote(const std::string& s) {
    std::ostringstream out; out << '"';
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') out << '\\' << c;
        else if (c < 32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c) << std::dec;
        else out << c;
    }
    out << '"'; return out.str();
}
static std::string jsonValue(Value v) { return v && std::isfinite(*v) ? number(v, 6) : "null"; }
static std::string id(std::optional<unsigned> v) { return v ? std::to_string(*v) : "null"; }
std::string snapshotJson(const Snapshot& s) {
    std::ostringstream o; auto a = aggregate(s);
    o << "{\"driver\":" << quote(s.driver) << ",\"cann\":" << quote(s.cann) << ",\"error\":" << quote(s.error)
      << ",\"collection_seconds\":" << s.collectionSeconds
      << ",\"host\":{\"cpu_percent\":" << jsonValue(s.host.cpu) << ",\"memory_used_bytes\":" << jsonValue(s.host.memory.used)
      << ",\"memory_total_bytes\":" << jsonValue(s.host.memory.total) << ",\"swap_used_bytes\":" << jsonValue(s.host.swap.used)
      << ",\"swap_total_bytes\":" << jsonValue(s.host.swap.total) << ",\"load1\":" << jsonValue(s.host.load1)
      << ",\"load5\":" << jsonValue(s.host.load5) << ",\"load15\":" << jsonValue(s.host.load15) << "},\"aggregate\":{\"memory_percent\":"
      << jsonValue(a.memory) << ",\"core_percent\":" << jsonValue(a.core) << ",\"memory_devices\":" << a.memoryCount
      << ",\"core_devices\":" << a.coreCount << ",\"total_devices\":" << a.total << "},\"devices\":[";
    bool first = true;
    for (const auto& d : s.devices) {
        if (!first) o << ','; first = false;
        o << "{\"physical_id\":" << id(d.physical) << ",\"logical_id\":" << id(d.logical) << ",\"card\":" << d.card << ",\"chip\":" << d.chip
          << ",\"name\":" << quote(d.name) << ",\"pci\":" << quote(d.pci) << ",\"temperature_c\":" << jsonValue(d.temperature)
          << ",\"chip_power_w\":" << jsonValue(d.power) << ",\"core_percent\":" << jsonValue(d.core)
          << ",\"device_share\":" << (d.deviceShare ? (*d.deviceShare ? "true" : "false") : "null")
          << ",\"memory_kind\":" << quote(d.memory.kind) << ",\"memory_used_bytes\":" << jsonValue(d.memory.used)
          << ",\"memory_total_bytes\":" << jsonValue(d.memory.total) << ",\"processes_available\":" << (d.processesAvailable ? "true" : "false") << '}';
    }
    o << "],\"processes\":["; first = true;
    for (const auto& p : s.processes) {
        if (!first) o << ','; first = false;
        o << "{\"physical_id\":" << id(p.physical) << ",\"card\":" << p.card << ",\"chip\":" << p.chip << ",\"pid\":" << p.pid
          << ",\"user\":" << quote(p.user) << ",\"npu_bytes\":" << jsonValue(p.npuBytes) << ",\"cpu_percent\":" << jsonValue(p.cpu)
          << ",\"memory_percent\":" << jsonValue(p.memoryPercent) << ",\"elapsed_seconds\":" << jsonValue(p.elapsed)
          << ",\"command\":" << quote(p.command) << '}';
    }
    o << "]}"; return o.str();
}
}

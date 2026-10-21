#include "top/ProcessView.h"
#include <algorithm>
#include <cmath>

namespace ascend {
bool ProcessIdentity::matches(const Process& p) const { return p.pid == pid && p.startTicks && *p.startTicks == startTicks; }
bool ProcessSelection::select(const Process& p) {
    if (!p.startTicks) return false;
    identity_ = ProcessIdentity{p.pid, *p.startTicks}; card_ = p.card; chip_ = p.chip;
    return true;
}
std::optional<size_t> ProcessSelection::index(const std::vector<const Process*>& rows) const {
    if (identity_) for (size_t i = 0; i < rows.size(); ++i)
        if (identity_->matches(*rows[i]) && rows[i]->card == card_ && rows[i]->chip == chip_) return i;
    return {};
}
void ProcessSelection::move(const std::vector<const Process*>& rows, int delta) {
    if (rows.empty()) return;
    auto at = index(rows);
    int next = at ? std::clamp(static_cast<int>(*at) + delta, 0, static_cast<int>(rows.size()) - 1)
                  : (delta < 0 ? static_cast<int>(rows.size()) - 1 : 0);
    for (int i = next; i >= 0 && i < static_cast<int>(rows.size()); i += delta < 0 ? -1 : 1)
        if (select(*rows[i])) break;
}
void ProcessSelection::reconcile(const std::vector<const Process*>& rows) {
    if (!identity_ || index(rows)) return;
    for (const auto* p : rows) if (identity_->matches(*p)) { select(*p); return; }
    // A temporary /proc failure is not evidence of exit or a new identity.
    for (const auto* p : rows) if (p->pid == identity_->pid && !p->startTicks) return;
    clear();
}
std::set<std::pair<int, int>> processDevices(const Snapshot& s, const ProcessIdentity& identity) {
    std::set<std::pair<int, int>> result;
    for (const auto& p : s.processes) if (identity.matches(p)) result.emplace(p.card, p.chip);
    return result;
}
void ProcessInspection::target(std::optional<ProcessIdentity> identity) {
    if (identity_ == identity) return;
    identity_ = identity; current_ = {}; history_ = BufferedHistory(interval_);
}
void ProcessInspection::append(const Snapshot& s) {
    if (!identity_ || s.sampled <= current_.sampled) return;
    ProcessMetrics next;
    next.sampled = s.sampled; next.user = current_.user; next.command = current_.command;
    bool complete = s.error.empty() && !s.devices.empty()
        && std::all_of(s.devices.begin(), s.devices.end(), [](const Device& d) { return d.processesAvailable; });
    bool unknownIdentity = false, found = false, validNpu = true;
    double npu = 0;
    for (const auto& p : s.processes) {
        if (p.pid == identity_->pid && !p.startTicks) unknownIdentity = true;
        if (!identity_->matches(p)) continue;
        if (!found) {
            next.user = p.user; next.command = p.command; next.cpu = p.cpu;
            next.hostBytes = p.rssBytes; next.hostPercent = p.memoryPercent; next.elapsed = p.elapsed;
        }
        found = true;
        // One contribution per device; host metrics are copied only once.
        if (next.devices.emplace(p.card, p.chip).second) {
            if (p.npuBytes && std::isfinite(*p.npuBytes) && *p.npuBytes >= 0) npu += *p.npuBytes;
            else validNpu = false;
        }
    }
    if (found) {
        next.state = ProcessState::Available;
        if (complete && !unknownIdentity && validNpu) next.npuBytes = npu;
    } else next.state = complete && !unknownIdentity ? ProcessState::Exited : ProcessState::Unavailable;
    current_ = std::move(next);
    history_.appendValues(s.sampled, {current_.cpu, current_.hostBytes, {}, current_.npuBytes, {}});
}
}

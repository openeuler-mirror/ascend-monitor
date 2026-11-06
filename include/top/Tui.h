#pragma once
#include "top/Model.h"
#include "top/ProcessView.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <memory>
#include <thread>
namespace ascend {
struct SamplerFrame {
    std::shared_ptr<const Snapshot> snapshot;
    History history;
    ProcessInspection inspection;
    bool stale = false;
};
class Sampler {
public:
    explicit Sampler(double interval);
    ~Sampler();
    SamplerFrame frame();
    void setInterval(double interval);
    void inspect(std::optional<ProcessIdentity> identity);
private:
    void expectCollection(Clock::time_point start);
    std::mutex mutex_;
    std::condition_variable wake_;
    bool stop_ = false;
    double interval_;
    bool collecting_ = true;
    Clock::time_point freshUntil_{};
    std::thread worker_;
    std::shared_ptr<const Snapshot> latest_;
    History history_;
    ProcessInspection inspection_;
};
int runTui(double interval, bool ascii = false);
}

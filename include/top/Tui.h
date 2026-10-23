#pragma once
#include "top/Model.h"
#include "top/ProcessView.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <memory>
#include <thread>
namespace ascend {
class Sampler {
public:
    explicit Sampler(double interval);
    ~Sampler();
    std::shared_ptr<const Snapshot> latest();
    History history();
    void inspect(std::optional<ProcessIdentity> identity);
    ProcessInspection inspection();
private:
    std::mutex mutex_;
    std::condition_variable wake_;
    bool stop_ = false;
    std::thread worker_;
    std::shared_ptr<const Snapshot> latest_;
    BufferedHistory history_;
    ProcessInspection inspection_;
};
int runTui(double interval, bool ascii = false);
}

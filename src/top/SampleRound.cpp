#include "top/SampleRound.h"
#include "top/Dcmi.h"
#include "top/Proc.h"
#include <utility>

namespace ascend {
struct SampleRound::Impl {
    Dcmi dcmi;
    ProcReader proc;
};

SampleRoundError::SampleRoundError(std::string message, Snapshot partial)
    : std::runtime_error(std::move(message)), partial_(std::move(partial)) {}

SampleRound::SampleRound() : impl_(std::make_unique<Impl>()) {}
SampleRound::~SampleRound() = default;

Snapshot SampleRound::collect() {
    auto start = Clock::now();
    Snapshot snapshot;
    try {
        impl_->dcmi.collect(snapshot);
        impl_->proc.collect(snapshot);
    } catch (const std::exception& error) {
        throw SampleRoundError(error.what(), std::move(snapshot));
    }
    snapshot.sampled = Clock::now();
    snapshot.wall = std::chrono::system_clock::now();
    snapshot.collectionSeconds = std::chrono::duration<double>(snapshot.sampled - start).count();
    return snapshot;
}
}

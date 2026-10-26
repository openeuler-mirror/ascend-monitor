#pragma once
#include "top/Model.h"
#include <memory>
#include <stdexcept>
#include <string>

namespace ascend {
class SampleRoundError : public std::runtime_error {
public:
    SampleRoundError(std::string message, Snapshot partial);
    const Snapshot& partial() const noexcept { return partial_; }
private:
    Snapshot partial_;
};

// Forms one complete snapshot while retaining source state across rounds.
class SampleRound {
public:
    SampleRound();
    ~SampleRound();
    Snapshot collect();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}

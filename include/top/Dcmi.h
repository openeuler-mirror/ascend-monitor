#pragma once
#include "top/Model.h"
#include <memory>
namespace ascend {
class Dcmi {
public:
    Dcmi();
    ~Dcmi();
    Dcmi(const Dcmi&) = delete;
    Dcmi& operator=(const Dcmi&) = delete;
    void collect(Snapshot& snapshot);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}

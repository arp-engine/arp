#pragma once
#include "core/cpu_allocator.h"

#include <algorithm>
#include <cmath>

namespace arp::core {
// Bounded exact samples, nearest-rank percentiles. Collection does not grow
// after construction. Sorting occurs only when the caller finishes a run.
class FrameTimeStatistics final {
public:
    explicit FrameTimeStatistics(std::size_t capacity) : capacity_(capacity)
    {
        samples_.reserve(capacity);
    }

    bool add(double milliseconds)
    {
        if (!std::isfinite(milliseconds) || milliseconds < 0 || samples_.size() == capacity_)
            return false;
        samples_.push_back(milliseconds);
        sorted_ = false;
        return true;
    }

    bool percentile(double fraction, double& output)
    {
        if (samples_.empty() || !std::isfinite(fraction) || fraction <= 0 || fraction > 1)
            return false;
        if (!sorted_) {
            std::sort(samples_.begin(), samples_.end());
            sorted_ = true;
        }
        output = samples_[std::size_t(std::ceil(fraction * samples_.size())) - 1];
        return true;
    }

    std::size_t count() const { return samples_.size(); }

private:
    std::size_t capacity_;
    bool sorted_ = false;
    pmr::Vector<double> samples_;
};
} // namespace arp::core

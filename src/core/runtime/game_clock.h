#pragma once
#include "reflection/reflection_macros.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace arp::core {
struct FixedStepFrame {
    std::uint32_t steps = 0;
    double stepSeconds = 0, realDeltaSeconds = 0, gameDeltaSeconds = 0;
    double debtSeconds = 0, interpolation = 0;
};
ACLASS(Ownership = Engine, Lifecycle = Explicit)

class GameClock final {
public:
    GameClock(double fixedSeconds, std::uint32_t maxCatchup)
        : fixed_(fixedSeconds), maxCatchup_(maxCatchup)
    {}

    bool valid() const { return std::isfinite(fixed_) && fixed_ > 0 && maxCatchup_ > 0; }
    AFUNC()

    bool setTimeScale(double scale)
    {
        if (!std::isfinite(scale) || scale < 0)
            return false;
        scale_ = scale;
        return true;
    }
    AFUNC()

    void setPaused(bool paused) { paused_ = paused; }
    AFUNC()

    bool paused() const { return paused_; }

    bool advance(double realDelta, FixedStepFrame& out)
    {
        out = {};
        if (!valid() || !std::isfinite(realDelta) || realDelta < 0
            || !std::isfinite(realDelta * scale_ + accumulator_))
            return false;
        out.realDeltaSeconds = realDelta;
        out.stepSeconds = fixed_;
        // Pausing also freezes existing debt.
        if (!paused_) {
            accumulator_ += realDelta * scale_;
            while (out.steps < maxCatchup_ && accumulator_ + fixed_ * 1e-10 >= fixed_) {
                accumulator_ = std::max(0.0, accumulator_ - fixed_);
                ++out.steps;
                ++ticks_;
            }
        }
        out.gameDeltaSeconds = out.steps * fixed_;
        refundableSteps_ = out.steps;
        out.debtSeconds = accumulator_;
        out.interpolation = std::min(1.0, accumulator_ / fixed_);
        return true;
    }
    AFUNC()

    std::uint64_t ticks() const { return ticks_; }

    // If a step changes state (upgrade/death), preserve unexecuted scheduled
    // ticks as debt. They must not count as completed simulation time.
    bool returnUnexecutedSteps(std::uint32_t count)
    {
        if (count > refundableSteps_)
            return false;
        ticks_ -= count;
        accumulator_ += count * fixed_;
        refundableSteps_ -= count;
        return true;
    }

    double debtSeconds() const { return accumulator_; }

    double simulationSeconds() const { return ticks_ * fixed_; }

    void reset()
    {
        accumulator_ = 0;
        ticks_ = 0;
        refundableSteps_ = 0;
    }

private:
    double fixed_, scale_ = 1, accumulator_ = 0;
    std::uint32_t maxCatchup_;
    std::uint32_t refundableSteps_ = 0;
    std::uint64_t ticks_ = 0;
    bool paused_ = false;
};
} // namespace arp::core

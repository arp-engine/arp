#pragma once
#include "core/math/curve.h"
#include "core/time/timer_service.h"

namespace arp::core::runtime {
// A tween borrows its immutable curve for its lifetime; curve keys map [0,1]
// to the explicit weight. Existing FloatCurve owns interpolation semantics.
class Tween final {
public:
    bool start(PoolHandle owner,
        TimeDomain domain,
        const FloatCurve& curve,
        double seconds,
        float from,
        float to)
    {
        if (!owner || !curve.valid() || !std::isfinite(seconds) || seconds <= 0
            || !std::isfinite(from) || !std::isfinite(to))
            return false;
        for (const auto& key : curve.keys)
            if (!std::isfinite(key.time) || !std::isfinite(key.value))
                return false;
        if (curve.keys.front().time != 0 || curve.keys.back().time != 1)
            return false;
        owner_ = owner;
        domain_ = domain;
        curve_ = &curve;
        duration_ = seconds;
        elapsed_ = 0;
        from_ = from;
        to_ = to;
        value_ = from;
        active_ = true;
        return true;
    }

    bool advance(double gameSeconds, double uiSeconds)
    {
        if (!std::isfinite(gameSeconds) || gameSeconds < 0 || !std::isfinite(uiSeconds)
            || uiSeconds < 0)
            return false;
        if (!active_)
            return true;
        elapsed_ =
            std::min(duration_, elapsed_ + (domain_ == TimeDomain::Game ? gameSeconds : uiSeconds));
        value_ = from_ + (to_ - from_) * curve_->sample(float(elapsed_ / duration_));
        active_ = elapsed_ < duration_;
        return true;
    }

    void cancelOwner(PoolHandle owner)
    {
        if (owner == owner_)
            active_ = false;
    }

    float value() const { return value_; }

    bool active() const { return active_; }

private:
    PoolHandle owner_;
    TimeDomain domain_ = TimeDomain::Game;
    const FloatCurve* curve_ = nullptr;
    double duration_ = 0, elapsed_ = 0;
    float from_ = 0, to_ = 0, value_ = 0;
    bool active_ = false;
};
} // namespace arp::core::runtime

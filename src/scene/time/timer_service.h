#pragma once
#include "core/container/generation_pool.h"

#include <cmath>

namespace arp::core::runtime {
enum class TimeDomain { Game, Ui };

struct TimerEvent {
    PoolHandle timer, owner;
    std::uint32_t eventId = 0;
};

class TimerService final {
public:
    TimerService(std::uint32_t timerCapacity, std::uint32_t eventCapacity)
        : timers_(timerCapacity), eventCapacity_(eventCapacity)
    {
        events_.reserve(eventCapacity);
    }

    PoolHandle schedule(
        PoolHandle owner, TimeDomain domain, double seconds, bool repeat, std::uint32_t eventId)
    {
        if (!owner || !std::isfinite(seconds) || seconds <= 0) {
            diagnostic_ = "CORE-TIMER-CONFIG";
            return {};
        }
        auto h = timers_.emplace(Timer{owner, domain, seconds, seconds, repeat, eventId});
        diagnostic_ = timers_.diagnostic();
        return h;
    }

    bool contains(PoolHandle h) const { return timers_.get(h) != nullptr; }

    bool cancel(PoolHandle h) { return timers_.erase(h); }

    void cancelOwner(PoolHandle owner)
    {
        timers_.forEach([&](PoolHandle h, Timer& t) {
            if (t.owner == owner)
                timers_.erase(h);
        });
    }

    // Events are borrowed until next successful advance. Overflow is atomic:
    // timers and previous events remain unchanged; caller retains elapsed debt.
    bool advance(double gameSeconds, double uiSeconds)
    {
        diagnostic_ = nullptr;
        if (!std::isfinite(gameSeconds) || gameSeconds < 0 || !std::isfinite(uiSeconds)
            || uiSeconds < 0)
            return fail("CORE-TIMER-DELTA");
        std::uint64_t count = 0;
        timers_.forEach([&](PoolHandle, Timer& t) {
            const double delta = t.domain == TimeDomain::Game ? gameSeconds : uiSeconds;
            if (delta < t.remaining)
                return;
            const double due = t.repeat ? 1 + std::floor((delta - t.remaining) / t.interval) : 1;
            if (due > eventCapacity_ || count > eventCapacity_)
                count = std::uint64_t(eventCapacity_) + 1;
            else
                count += static_cast<std::uint64_t>(due);
        });
        if (count > eventCapacity_)
            return fail("CORE-TIMER-EVENT-CAPACITY");
        events_.clear();
        timers_.forEach([&](PoolHandle h, Timer& t) {
            t.remaining -= t.domain == TimeDomain::Game ? gameSeconds : uiSeconds;
            while (t.remaining <= 0) {
                events_.push_back({h, t.owner, t.eventId});
                if (!t.repeat) {
                    timers_.erase(h);
                    break;
                }
                t.remaining += t.interval;
            }
        });
        return true;
    }

    const auto& events() const { return events_; }

    const char* diagnostic() const { return diagnostic_; }

    std::uint32_t liveCount() const { return timers_.liveCount(); }

private:
    struct Timer {
        PoolHandle owner;
        TimeDomain domain;
        double remaining, interval;
        bool repeat;
        std::uint32_t eventId;
    };

    bool fail(const char* id)
    {
        diagnostic_ = id;
        return false;
    }

    GenerationPool<Timer> timers_;
    core::pmr::Vector<TimerEvent> events_{&core::defaultCpuMemoryResource()};
    std::uint32_t eventCapacity_;
    const char* diagnostic_ = nullptr;
};
} // namespace arp::core::runtime

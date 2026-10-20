#pragma once
#include "2d/sprite_resources.h"

namespace arp::scene2d {
enum class SpriteAnimationEventKind { Frame, Loop, Completed };

struct SpriteAnimationEvent {
    SpriteAnimationEventKind kind;
    std::uint32_t frame, eventId;
};

class SpriteAnimator final {
public:
    explicit SpriteAnimator(std::uint32_t eventBudget) : eventBudget_(eventBudget)
    {
        events_.reserve(eventBudget);
    }

    bool play(resource::ResourceHandle<SpriteAnimationResource> animation)
    {
        if (!animation.ready())
            return fail("ARP-2D-ANIMATION-NOT-READY");
        animation_ = animation;
        frame_ = 0;
        elapsed_ = 0;
        completed_ = false;
        events_.clear();
        diagnostic_ = nullptr;
        return true;
    }

    void stop()
    {
        animation_ = {};
        events_.clear();
        completed_ = true;
    }

    bool setSpeed(double speed)
    {
        if (!std::isfinite(speed) || speed < 0)
            return false;
        speed_ = speed;
        return true;
    }

    void setPaused(bool paused) { paused_ = paused; }

    bool advance(double gameSeconds)
    {
        diagnostic_ = nullptr;
        if (!std::isfinite(gameSeconds) || gameSeconds < 0 || !std::isfinite(gameSeconds * speed_))
            return fail("ARP-2D-ANIMATION-DELTA");
        if (!animation_.ready())
            return fail("ARP-2D-ANIMATION-STALE");
        events_.clear();
        if (paused_ || completed_)
            return true;
        const auto& frames = animation_.get()->frames();
        auto index = frame_;
        double time = elapsed_ + gameSeconds * speed_;
        bool complete = false;
        // Local state is committed only when the bounded event stream fits.
        while (time >= frames[index].seconds) {
            time -= frames[index].seconds;
            if (index + 1 < frames.size()) {
                ++index;
                if (!emit({SpriteAnimationEventKind::Frame, index, frames[index].eventId}))
                    return false;
            } else if (animation_.get()->loop()) {
                index = 0;
                if (!emit({SpriteAnimationEventKind::Loop, 0, 0})
                    || !emit({SpriteAnimationEventKind::Frame, 0, frames[0].eventId}))
                    return false;
            } else {
                complete = true;
                time = 0;
                if (!emit({SpriteAnimationEventKind::Completed, index, 0}))
                    return false;
                break;
            }
        }
        frame_ = index;
        elapsed_ = time;
        completed_ = complete;
        return true;
    }

    const core::pmr::String* spriteId() const
    {
        return animation_.ready() ? &animation_.get()->frames()[frame_].spriteId : nullptr;
    }

    resource::ResourceHandle<SpriteResource> sprite() const
    {
        return animation_.ready() ? animation_.get()->frames()[frame_].sprite
                                  : resource::ResourceHandle<SpriteResource>{};
    }

    std::uint32_t frame() const { return frame_; }

    bool completed() const { return completed_; }

    const auto& events() const { return events_; }

    const char* diagnostic() const { return diagnostic_; }

private:
    bool fail(const char* id)
    {
        diagnostic_ = id;
        events_.clear();
        return false;
    }

    bool emit(SpriteAnimationEvent event)
    {
        if (events_.size() >= eventBudget_)
            return fail("ARP-2D-ANIMATION-EVENT-CAPACITY");
        events_.push_back(event);
        return true;
    }

    resource::ResourceHandle<SpriteAnimationResource> animation_;
    core::pmr::Vector<SpriteAnimationEvent> events_{&core::defaultCpuMemoryResource()};
    std::uint32_t eventBudget_, frame_ = 0;
    double elapsed_ = 0, speed_ = 1;
    bool paused_ = false, completed_ = false;
    const char* diagnostic_ = nullptr;
};
} // namespace arp::scene2d

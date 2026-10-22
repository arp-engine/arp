#pragma once
#include "scene/scene.h"
#include "scene/time/tween.h"

#include <cmath>
#include <stdexcept>

namespace arp::scene {
ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct TimerComponentEditorProperties {
    APROP(Serialize, Editor)
    float seconds = 1.0f;
    APROP(Serialize, Editor)
    bool repeat = false;
    APROP(Serialize, Editor)
    bool startOnEnable = false;
    APROP(Serialize, Editor)
    arp::core::pmr::String eventName{&arp::core::defaultCpuMemoryResource()};
};

ACLASS(Ownership = Engine, Lifecycle = Explicit)

class TimerComponent final : public Component {
public:
    explicit TimerComponent(std::uint32_t events)
        : timer_(1, events), event_("timer.elapsed", &arp::core::defaultCpuMemoryResource())
    {}

    const char* scriptTypeName() const override { return "TimerComponent"; }
    AFUNC()

    bool start_timer(double seconds, bool repeat, const core::pmr::String& event)
    {
        if (!std::isfinite(seconds) || seconds <= 0.0 || event.empty())
            return false;
        if (handle_)
            timer_.cancel(handle_);
        event_ = event;
        handle_ = timer_.schedule(
            {this, 0, id().value()}, core::runtime::TimeDomain::Game, seconds, repeat, 0);
        return bool(handle_);
    }
    AFUNC()

    arp::reflection::object editorAuthoringProperties() const override
    {
        return arp::reflection::object(
            TimerComponentEditorProperties{seconds_, repeat_, startOnEnable_, event_});
    }

    bool applyEditorAuthoringProperties(const arp::reflection::object& value) override
    {
        if (!value.is<TimerComponentEditorProperties>() || handle_)
            return false;
        const auto& properties = value.cast<const TimerComponentEditorProperties&>();
        if (!std::isfinite(properties.seconds) || properties.seconds <= 0.0f
            || (properties.startOnEnable && properties.eventName.empty()))
            return false;
        seconds_ = properties.seconds;
        repeat_ = properties.repeat;
        startOnEnable_ = properties.startOnEnable;
        event_ = properties.eventName;
        return true;
    }

    void onEnable(const SceneUpdateContext&) override
    {
        if (startOnEnable_ && !start_timer(seconds_, repeat_, event_))
            throw std::runtime_error("ARP-TIMER-AUTHORING-START-FAILED");
    }

    void onDisable(const SceneUpdateContext&) override { cancel_timer(); }

    void cancel_timer()
    {
        if (handle_)
            timer_.cancel(handle_);
        handle_ = {};
    }

    void onFixedUpdate(const SceneUpdateContext& context) override
    {
        if (!timer_.advance(context.deltaSeconds, 0))
            throw std::runtime_error(timer_.diagnostic());
        for (const auto& event : timer_.events()) {
            (void)event;
            if (!gameObject()->send_event(event_, {}))
                throw std::runtime_error("ARP-TIMER-SCENE-EVENT-CAPACITY");
        }
    }

    void onDestroy(const SceneUpdateContext&) override { cancel_timer(); }

private:
    core::runtime::TimerService timer_;
    core::PoolHandle handle_;
    core::pmr::String event_;
    float seconds_ = 1.0f;
    bool repeat_ = false;
    bool startOnEnable_ = false;
};

ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct TweenComponentEditorProperties {
    APROP(Serialize, Editor)
    float seconds = 1.0f;
    APROP(Serialize, Editor)
    float from = 0.0f;
    APROP(Serialize, Editor)
    float to = 1.0f;
    APROP(Serialize, Editor)
    bool unscaled = false;
    APROP(Serialize, Editor)
    bool startOnEnable = false;
};

ACLASS(Ownership = Engine, Lifecycle = Explicit)

class TweenComponent final : public Component {
public:
    TweenComponent() { curve_.keys = {{0, 0}, {1, 1}}; }

    const char* scriptTypeName() const override { return "TweenComponent"; }
    AFUNC()

    bool start_tween(double seconds, float from, float to, bool unscaled)
    {
        if (!std::isfinite(seconds) || seconds <= 0.0 || !std::isfinite(from) || !std::isfinite(to))
            return false;
        unscaled_ = unscaled;
        return tween_.start({this, 0, id().value()},
            unscaled ? core::runtime::TimeDomain::Ui : core::runtime::TimeDomain::Game,
            curve_,
            seconds,
            from,
            to);
    }
    AFUNC()

    arp::reflection::object editorAuthoringProperties() const override
    {
        return arp::reflection::object(
            TweenComponentEditorProperties{seconds_, from_, to_, unscaled_, startOnEnable_});
    }

    bool applyEditorAuthoringProperties(const arp::reflection::object& value) override
    {
        if (!value.is<TweenComponentEditorProperties>() || tween_.active())
            return false;
        const auto& properties = value.cast<const TweenComponentEditorProperties&>();
        if (!std::isfinite(properties.seconds) || properties.seconds <= 0.0f
            || !std::isfinite(properties.from) || !std::isfinite(properties.to))
            return false;
        seconds_ = properties.seconds;
        from_ = properties.from;
        to_ = properties.to;
        unscaled_ = properties.unscaled;
        startOnEnable_ = properties.startOnEnable;
        return true;
    }

    void onEnable(const SceneUpdateContext&) override
    {
        if (startOnEnable_ && !start_tween(seconds_, from_, to_, unscaled_))
            throw std::runtime_error("ARP-TWEEN-AUTHORING-START-FAILED");
    }

    float value() const { return tween_.value(); }
    AFUNC()

    bool completed() const { return !tween_.active(); }

    void onUpdate(const SceneUpdateContext& context) override
    {
        if (unscaled_ && !tween_.advance(0, context.unscaledDeltaSeconds))
            throw std::runtime_error("ARP-TWEEN-DELTA");
    }

    void onFixedUpdate(const SceneUpdateContext& context) override
    {
        if (!unscaled_ && !tween_.advance(context.deltaSeconds, 0))
            throw std::runtime_error("ARP-TWEEN-DELTA");
    }

    void onDestroy(const SceneUpdateContext&) override
    {
        tween_.cancelOwner({this, 0, id().value()});
    }

private:
    core::FloatCurve curve_;
    core::runtime::Tween tween_;
    float seconds_ = 1.0f;
    float from_ = 0.0f;
    float to_ = 1.0f;
    bool unscaled_ = false;
    bool startOnEnable_ = false;
};
} // namespace arp::scene

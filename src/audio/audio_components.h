#pragma once

#include "audio/audio_system.h"
#include "scene/component.h"

#include <cmath>

namespace arp::audio {

struct AudioSourceComponentDesc {
    AudioSystem* system = nullptr;
    AudioClipView clip{};
    float volume = 1.0f;
    bool loop = false;
    bool playOnEnable = true;
    AudioBus bus = AudioBus::Sfx;
    AudioSpatialParams spatial{};
};

ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct AudioSourceEditorProperties {
    APROP(Editor)
    float volume = 1.0f;
    APROP(Editor)
    bool loop = false;
    APROP(Editor)
    bool playOnEnable = true;
};

// Scene-facing bridge. It owns no PCM data: the referenced AudioClipResource
// must remain loaded while this component is enabled.
ACLASS(Ownership = Engine, Lifecycle = Explicit)

class AudioSourceComponent : public scene::Component {
public:
    const char* scriptTypeName() const override { return "AudioSourceComponent"; }

    explicit AudioSourceComponent(AudioSourceComponentDesc desc);
    ~AudioSourceComponent() override;

    void onEnable(const scene::SceneUpdateContext&) override;
    void onDisable(const scene::SceneUpdateContext&) override;
    void onDestroy(const scene::SceneUpdateContext&) override;

    bool playClip(AudioClipView clip, float volume, AudioBus bus, bool loop) noexcept
    {
        if (!clip.valid() || !std::isfinite(volume) || volume < 0 || volume > 1)
            return false;
        stop();
        desc_.clip = clip;
        desc_.volume = volume;
        desc_.bus = bus;
        desc_.loop = loop;
        if (!desc_.system)
            return true;
        voice_ = desc_.system->play({clip, volume, loop, bus, desc_.spatial});
        return voice_.valid();
    }

    bool setBusVolume(AudioBus bus, float volume) noexcept
    {
        if (!std::isfinite(volume) || volume < 0 || volume > 1 || int(bus) < 0
            || bus >= AudioBus::Count)
            return false;
        return !desc_.system || desc_.system->setBusVolume(bus, volume) == AudioStatus::Ready;
    }
    AFUNC()

    void stop_playback() { stop(); }

    VoiceId voice() const noexcept { return voice_; }

    bool playing() const noexcept { return voice_.valid(); }

    arp::reflection::object editorAuthoringProperties() const override
    {
        return arp::reflection::object(
            AudioSourceEditorProperties{desc_.volume, desc_.loop, desc_.playOnEnable});
    }

    bool applyEditorAuthoringProperties(const arp::reflection::object& value) override
    {
        if (!value.is<AudioSourceEditorProperties>() || voice_.valid())
            return false;
        const auto& properties = value.cast<const AudioSourceEditorProperties&>();
        if (!std::isfinite(properties.volume) || properties.volume < 0.0f)
            return false;
        desc_.volume = properties.volume;
        desc_.loop = properties.loop;
        desc_.playOnEnable = properties.playOnEnable;
        return true;
    }

    AudioStatus setVolume(float volume) noexcept;
    AudioStatus setSpatialParams(AudioSpatialParams spatial) noexcept;
    void setClip(AudioClipView clip) noexcept;

private:
    void stop() noexcept;
    AudioSourceComponentDesc desc_{};
    VoiceId voice_{};
};

class AudioListenerComponent final : public scene::Component {
public:
    const char* scriptTypeName() const override { return "AudioListenerComponent"; }

    explicit AudioListenerComponent(AudioSystem& system) : system_(&system) {}

    void setWorldPosition(core::Vec3 position) noexcept;
    void onDisable(const scene::SceneUpdateContext&) override;

private:
    AudioSystem* system_ = nullptr;
};

} // namespace arp::audio

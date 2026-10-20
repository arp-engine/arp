#pragma once
#include "audio/audio_components.h"
#include "resource/audio_clip_resource.h"

namespace arp::resource {
// Resource-aware extension retains the exact PCM generation used by the
// existing AudioSourceComponent. It reuses that component's voice lifecycle.
ACLASS(Ownership = Engine, Lifecycle = Explicit)

class AudioResourceSourceComponent final : public audio::AudioSourceComponent {
public:
    AudioResourceSourceComponent(ResourceManager& resources, audio::AudioSystem* audio)
        : AudioSourceComponent(sourceDesc(audio)), resources_(resources)
    {}

    const char* scriptTypeName() const override { return "AudioResourceSourceComponent"; }
    AFUNC()

    bool play_resource(const core::pmr::String& id, float volume, int bus, bool loop)
    {
        auto clip = resources_.find<AudioClipResource>(id);
        if (!clip.ready() || bus < 0 || bus >= int(audio::AudioBus::Count))
            return false;
        if (!playClip(clip.get()->view(), volume, audio::AudioBus(bus), loop))
            return false;
        clip_ = clip;
        return true;
    }
    AFUNC()

    bool set_bus_volume(int bus, float volume)
    {
        return bus >= 0 && bus < int(audio::AudioBus::Count)
            && setBusVolume(audio::AudioBus(bus), volume);
    }

private:
    static audio::AudioSourceComponentDesc sourceDesc(audio::AudioSystem* audio)
    {
        audio::AudioSourceComponentDesc desc;
        desc.system = audio;
        desc.playOnEnable = false;
        return desc;
    }

    ResourceManager& resources_;
    ResourceHandle<AudioClipResource> clip_;
};
} // namespace arp::resource

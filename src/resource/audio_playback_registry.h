#pragma once

#include "audio/audio_system.h"
#include "resource/audio_clip_resource.h"

namespace arp::resource {

class AudioPlaybackRegistry final : public ResourceLifecycleObserver {
public:
    AudioPlaybackRegistry(ResourceManager& manager, audio::AudioSystem& audio);
    ~AudioPlaybackRegistry() override;
    audio::VoiceId play(
        const ResourceHandle<AudioClipResource>& clip, audio::AudioPlayRequest request) noexcept;
    bool beforeResourceUnload(NameView resourceId) noexcept override;
    bool beforeRetiredResourcesRelease() noexcept override;

private:
    struct Entry {
        core::pmr::String resourceId{&core::defaultCpuMemoryResource()};
        ResourceGeneration generation = 0u;
        audio::VoiceId voice{};
        bool stopRequested = false;
    };

    void stop(Entry& entry) noexcept;
    ResourceManager& manager_;
    audio::AudioSystem& audio_;
    std::array<Entry, audio::kMaxAudioVoices> entries_{};
};

} // namespace arp::resource

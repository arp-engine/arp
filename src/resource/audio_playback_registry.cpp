#include "resource/audio_playback_registry.h"

namespace arp::resource {
AudioPlaybackRegistry::AudioPlaybackRegistry(ResourceManager& manager, audio::AudioSystem& audio)
    : manager_(manager), audio_(audio)
{
    manager_.addLifecycleObserver(*this);
}

AudioPlaybackRegistry::~AudioPlaybackRegistry()
{
    manager_.removeLifecycleObserver(*this);
}

audio::VoiceId AudioPlaybackRegistry::play(
    const ResourceHandle<AudioClipResource>& clip, audio::AudioPlayRequest request) noexcept
{
    if (!clip.ready())
        return {};
    request.clip = clip.get()->view();
    const auto voice = audio_.play(request);
    if (!voice.valid())
        return {};
    for (auto& entry : entries_)
        if (!entry.voice.valid()) {
            entry.resourceId = clip.id();
            entry.generation = clip.generation();
            entry.voice = voice;
            entry.stopRequested = false;
            return voice;
        }
    audio_.stop(voice);
    return {};
}

void AudioPlaybackRegistry::stop(Entry& entry) noexcept
{
    if (entry.voice.valid() && !entry.stopRequested) {
        audio_.stop(entry.voice);
        entry.stopRequested = true;
    }
}

bool AudioPlaybackRegistry::beforeResourceUnload(NameView resourceId) noexcept
{
    bool complete = true;
    for (auto& entry : entries_)
        if (entry.resourceId == resourceId && entry.voice.valid()) {
            stop(entry);
            if (!audio_.completionObserved(entry.voice))
                complete = false;
            else
                entry = {};
        }
    return complete;
}

bool AudioPlaybackRegistry::beforeRetiredResourcesRelease() noexcept
{
    bool complete = true;
    for (auto& entry : entries_)
        if (entry.voice.valid()) {
            stop(entry);
            if (!audio_.completionObserved(entry.voice))
                complete = false;
            else
                entry = {};
        }
    return complete;
}
} // namespace arp::resource

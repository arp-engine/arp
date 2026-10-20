#include "audio/audio_components.h"

namespace arp::audio {

AudioSourceComponent::AudioSourceComponent(AudioSourceComponentDesc desc) : desc_(desc)
{}

AudioSourceComponent::~AudioSourceComponent()
{
    stop();
}

void AudioSourceComponent::onEnable(const scene::SceneUpdateContext&)
{
    if (!desc_.playOnEnable || voice_.valid() || desc_.system == nullptr)
        return;
    voice_ = desc_.system->play({desc_.clip, desc_.volume, desc_.loop, desc_.bus, desc_.spatial});
}

void AudioSourceComponent::onDisable(const scene::SceneUpdateContext&)
{
    stop();
}

void AudioSourceComponent::onDestroy(const scene::SceneUpdateContext&)
{
    stop();
}

AudioStatus AudioSourceComponent::setVolume(float volume) noexcept
{
    if (volume < 0.0f)
        return AudioStatus::InvalidConfiguration;
    desc_.volume = volume;
    return voice_.valid() && desc_.system != nullptr ? desc_.system->setVolume(voice_, volume)
                                                     : AudioStatus::Ready;
}

AudioStatus AudioSourceComponent::setSpatialParams(AudioSpatialParams spatial) noexcept
{
    desc_.spatial = spatial;
    return voice_.valid() && desc_.system != nullptr ? desc_.system->setPosition(voice_, spatial)
                                                     : AudioStatus::Ready;
}

void AudioSourceComponent::setClip(AudioClipView clip) noexcept
{
    stop();
    desc_.clip = clip;
}

void AudioSourceComponent::stop() noexcept
{
    if (voice_.valid() && desc_.system != nullptr)
        desc_.system->stop(voice_);
    voice_ = {};
}

void AudioListenerComponent::setWorldPosition(core::Vec3 position) noexcept
{
    if (system_ != nullptr)
        system_->setListener({position});
}

void AudioListenerComponent::onDisable(const scene::SceneUpdateContext&)
{
    if (system_ != nullptr)
        system_->setListener({});
}

} // namespace arp::audio

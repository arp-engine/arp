#include "resource/audio_clip_resource.h"

namespace arp::resource {

AudioClipResource::AudioClipResource(
    core::pmr::String resourceId, core::pmr::Vector<float> samples, std::uint32_t channels)
    : ResourceBase(ResourceReference{
          std::move(resourceId),
          core::pmr::String(resourceTypeName(), &core::defaultCpuMemoryResource()),
          {},
          core::pmr::String("Contract Complete", &core::defaultCpuMemoryResource()),
          {},
      }),
      samples_(std::move(samples)), channels_(channels)
{}

AudioClipResource::AudioClipResource(
    core::pmr::String resourceId, std::filesystem::path packagePath)
    : ResourceBase(ResourceReference{
          std::move(resourceId),
          core::pmr::String(resourceTypeName(), &core::defaultCpuMemoryResource()),
          {},
          core::pmr::String("Contract Complete", &core::defaultCpuMemoryResource()),
          {},
      }),
      packagePath_(std::move(packagePath))
{}

ResourceLoadResult AudioClipResource::load(
    ResourceManager& manager, const ResourceLoadContext& context)
{
    if (!packagePath_.empty()) {
        const auto byteBudget = context.maxDecodedBytes == 0u
            ? static_cast<std::size_t>(64u * 1024u * 1024u)
            : context.maxDecodedBytes;
        auto package = readAudioPackage(packagePath_, byteBudget);
        if (!package.success) {
            ResourceLoadResult result;
            result.state = ResourceState::Failed;
            result.resourceId = reference().id;
            result.resourceType = reference().type;
            result.diagnostics = std::move(package.diagnostics);
            return result;
        }
        samples_ = std::move(package.data.samples);
        channels_ = package.data.channels;
        playback_ = package.data.playback;
    }
    auto clip = view();
    if (!clip.valid()) {
        ResourceLoadResult result;
        result.state = ResourceState::Failed;
        result.resourceId = reference().id;
        result.resourceType = reference().type;
        result.diagnostics = core::pmr::String(
            "AudioClipResource requires non-empty 48000 Hz mono or stereo PCM frames",
            &core::defaultCpuMemoryResource());
        return result;
    }
    if (context.maxDecodedBytes != 0u && processedByteCount() > context.maxDecodedBytes) {
        ResourceLoadResult result;
        result.state = ResourceState::Failed;
        result.resourceId = reference().id;
        result.resourceType = reference().type;
        result.diagnostics =
            core::pmr::String("AudioClipResource PCM data exceeds configured decoded-byte budget",
                &core::defaultCpuMemoryResource());
        return result;
    }
    return ResourceBase::load(manager, context);
}

void AudioClipResource::appendContentFingerprint(core::StableHash64& hasher) const noexcept
{
    hasher.appendU64(channels_);
    hasher.appendU64(playback_.loop ? 1u : 0u);
    hasher.appendU64(playback_.loopStartFrame);
    hasher.appendU64(playback_.loopEndFrame);
    hasher.appendU64(samples_.size());
    for (const auto sample : samples_)
        hasher.appendFloat(sample);
}

audio::AudioClipView AudioClipResource::view() const noexcept
{
    const auto frameCount = channels_ == 0u ? 0u : samples_.size() / channels_;
    return {samples_.data(),
        frameCount,
        audio::kAudioSampleRate,
        channels_,
        playback_.loop,
        playback_.loopStartFrame,
        playback_.loopEndFrame};
}

} // namespace arp::resource

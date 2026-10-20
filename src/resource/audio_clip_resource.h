#pragma once

#include "audio/audio_system.h"
#include "resource/audio_package.h"
#include "resource/resource_contract.h"
#include "resource/resource_manager.h"

namespace arp::resource {

inline constexpr ResourceContractDescriptor kAudioClipContract{
    "AudioClipResource", "audio.clip/v1"};

// Runtime clips are deliberately PCM-only. Decoding and resampling belong to
// the import/package step so the audio callback never touches a codec or file.
class AudioClipResource final : public ResourceBase {
public:
    static constexpr const char* resourceTypeName() noexcept { return "AudioClipResource"; }

    AudioClipResource() = default;
    AudioClipResource(
        core::pmr::String resourceId, core::pmr::Vector<float> samples, std::uint32_t channels);
    AudioClipResource(core::pmr::String resourceId, std::filesystem::path packagePath);

    ResourceLoadResult load(ResourceManager& manager, const ResourceLoadContext& context) override;

    ResourceGraphStage processingStage() const noexcept override
    {
        return ResourceGraphStage::TypedProcessing;
    }

    std::size_t processedByteCount() const noexcept override
    {
        return samples_.size() * sizeof(float);
    }

    void appendContentFingerprint(core::StableHash64& hasher) const noexcept override;

    audio::AudioClipView view() const noexcept;

    std::uint32_t channels() const noexcept { return channels_; }

    const AudioPackagePlayback& playback() const noexcept { return playback_; }

private:
    core::pmr::Vector<float> samples_{&core::defaultCpuMemoryResource()};
    std::uint32_t channels_ = 0u;
    AudioPackagePlayback playback_{};
    std::filesystem::path packagePath_;
};

} // namespace arp::resource

#pragma once

#include "audio/miniaudio_backend.h"
#include "core/container/array_view.h"

namespace arp::audio {

// Import-only API. The caller persists the returned PCM through
// AudioClipResource; the device callback never decodes a source file.
ARP_API DecodedAudioPcm importAudioFile(const char* path, std::size_t maxDecodedBytes);

enum class AudioSourceFormat : std::uint8_t { Unspecified, Wave };

struct AudioMemoryImportDesc {
    core::ArrayView<const std::uint8_t> bytes;
    AudioSourceFormat format = AudioSourceFormat::Unspecified;
    std::size_t maxEncodedBytes = 0;
    std::size_t maxDecodedBytes = 0;
    std::size_t maxDecoderBytes = 0;
};

// Synchronous CPU preparation only. Input is borrowed until return; PCM owns
// its storage and never retains the source or accesses a file/device/network.
ARP_API DecodedAudioPcm importAudioMemory(const AudioMemoryImportDesc&);

} // namespace arp::audio

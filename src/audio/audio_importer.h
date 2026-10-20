#pragma once

#include "audio/miniaudio_backend.h"

namespace arp::audio {

// Import-only API. The caller persists the returned PCM through
// AudioClipResource; the device callback never decodes a source file.
ARP_API DecodedAudioPcm importAudioFile(const char* path, std::size_t maxDecodedBytes);

} // namespace arp::audio

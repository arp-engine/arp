#include "audio/audio_importer.h"

namespace arp::audio {
DecodedAudioPcm importAudioFile(const char* path, std::size_t maxDecodedBytes)
{
    return decodeAudioFileToRuntimePcm(path, maxDecodedBytes);
}
} // namespace arp::audio

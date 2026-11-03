#include "audio/audio_importer.h"

namespace arp::audio {
DecodedAudioPcm importAudioFile(const char* path, std::size_t maxDecodedBytes)
{
    return decodeAudioFileToRuntimePcm(path, maxDecodedBytes);
}

DecodedAudioPcm importAudioMemory(const AudioMemoryImportDesc& desc)
{
    return decodeAudioMemoryToRuntimePcm(desc);
}
} // namespace arp::audio

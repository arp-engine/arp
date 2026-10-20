#include "resource/audio_package.h"

#include "audio/audio_system.h"
#include "core/file_io.h"
#include "core/stable_hash.h"

#include <cstring>
#include <fstream>

namespace arp::resource {
namespace {
constexpr std::size_t kHeaderBytes = 64u;
constexpr char kMagic[8] = {'A', 'R', 'P', 'A', 'U', 'D', '0', '1'};

void appendU32(core::pmr::Vector<std::uint8_t>& bytes, std::uint32_t value)
{
    for (int i = 0; i < 4; ++i)
        bytes.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
}

void appendU64(core::pmr::Vector<std::uint8_t>& bytes, std::uint64_t value)
{
    for (int i = 0; i < 8; ++i)
        bytes.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
}

bool readU32(const core::pmr::Vector<std::uint8_t>& bytes, std::size_t& at, std::uint32_t& value)
{
    if (at + 4u > bytes.size())
        return false;
    value = 0u;
    for (int i = 0; i < 4; ++i)
        value |= static_cast<std::uint32_t>(bytes[at++]) << (i * 8);
    return true;
}

bool readU64(const core::pmr::Vector<std::uint8_t>& bytes, std::size_t& at, std::uint64_t& value)
{
    if (at + 8u > bytes.size())
        return false;
    value = 0u;
    for (int i = 0; i < 8; ++i)
        value |= static_cast<std::uint64_t>(bytes[at++]) << (i * 8);
    return true;
}

std::uint64_t payloadHash(const float* samples, std::size_t count)
{
    core::StableHash64 hash;
    hash.appendBytes(samples, count * sizeof(float));
    return hash.value();
}
} // namespace

core::pmr::String writeAudioPackage(const std::filesystem::path& path, const AudioPackageData& data)
{
    if (data.channels != audio::kAudioOutputChannels || data.samples.empty()
        || data.samples.size() % data.channels != 0u)
        return "audio package requires non-empty 48 kHz stereo PCM";
    const auto frames = data.samples.size() / data.channels;
    if (data.playback.loop
        && (data.playback.loopEndFrame <= data.playback.loopStartFrame
            || data.playback.loopEndFrame > frames))
        return "audio package loop bounds are invalid";
    core::pmr::Vector<std::uint8_t> bytes{&core::defaultCpuMemoryResource()};
    bytes.insert(bytes.end(), kMagic, kMagic + 8);
    appendU32(bytes, kAudioPackageVersion);
    appendU32(bytes, static_cast<std::uint32_t>(kHeaderBytes));
    appendU32(bytes, audio::kAudioSampleRate);
    appendU32(bytes, data.channels);
    appendU32(bytes, data.playback.loop ? 1u : 0u);
    appendU32(bytes, 0u);
    appendU64(bytes, frames);
    appendU64(bytes, data.playback.loopStartFrame);
    appendU64(bytes, data.playback.loop ? data.playback.loopEndFrame : frames);
    appendU64(bytes, payloadHash(data.samples.data(), data.samples.size()));
    const auto* pcm = reinterpret_cast<const std::uint8_t*>(data.samples.data());
    bytes.insert(bytes.end(), pcm, pcm + data.samples.size() * sizeof(float));
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        return "cannot open audio package output path";
    output.write(
        reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return output
        ? core::pmr::String(&core::defaultCpuMemoryResource())
        : core::pmr::String("cannot write audio package", &core::defaultCpuMemoryResource());
}

AudioPackageResult readAudioPackage(const std::filesystem::path& path, std::size_t maxPayloadBytes)
{
    AudioPackageResult result;
    auto file = core::readBinaryFile(path);
    if (!file.success) {
        result.diagnostics = file.diagnostics;
        return result;
    }
    auto& bytes = file.bytes;
    std::size_t at = 0u;
    if (bytes.size() < kHeaderBytes || std::memcmp(bytes.data(), kMagic, 8) != 0) {
        result.diagnostics = "audio package magic is invalid";
        return result;
    }
    at = 8u;
    std::uint32_t version = 0u, headerBytes = 0u, rate = 0u, channels = 0u, flags = 0u,
                  reserved = 0u;
    std::uint64_t frames = 0u, loopStart = 0u, loopEnd = 0u, hash = 0u;
    if (!readU32(bytes, at, version) || !readU32(bytes, at, headerBytes)
        || !readU32(bytes, at, rate) || !readU32(bytes, at, channels) || !readU32(bytes, at, flags)
        || !readU32(bytes, at, reserved) || !readU64(bytes, at, frames)
        || !readU64(bytes, at, loopStart) || !readU64(bytes, at, loopEnd)
        || !readU64(bytes, at, hash)) {
        result.diagnostics = "audio package header is truncated";
        return result;
    }
    const auto payloadBytes = frames * channels * sizeof(float);
    if (version != kAudioPackageVersion || headerBytes != kHeaderBytes
        || rate != audio::kAudioSampleRate || channels != audio::kAudioOutputChannels
        || reserved != 0u || payloadBytes > maxPayloadBytes
        || bytes.size() != kHeaderBytes + payloadBytes) {
        result.diagnostics = "audio package header or payload contract is invalid";
        return result;
    }
    if ((flags & ~1u) != 0u || ((flags & 1u) != 0u && (loopEnd <= loopStart || loopEnd > frames))) {
        result.diagnostics = "audio package loop contract is invalid";
        return result;
    }
    result.data.samples.resize(static_cast<std::size_t>(frames * channels));
    std::memcpy(result.data.samples.data(),
        bytes.data() + kHeaderBytes,
        static_cast<std::size_t>(payloadBytes));
    if (payloadHash(result.data.samples.data(), result.data.samples.size()) != hash) {
        result.data.samples.clear();
        result.diagnostics = "audio package payload hash does not match";
        return result;
    }
    result.data.channels = channels;
    result.data.playback = {(flags & 1u) != 0u, loopStart, loopEnd};
    result.success = true;
    return result;
}

} // namespace arp::resource

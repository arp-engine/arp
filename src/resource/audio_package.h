#pragma once

#include "core/memory/cpu_allocator.h"

#include <cstdint>
#include <filesystem>

namespace arp::resource {

constexpr std::uint32_t kAudioPackageVersion = 1u;

struct AudioPackagePlayback {
    bool loop = false;
    std::uint64_t loopStartFrame = 0u;
    std::uint64_t loopEndFrame = 0u;
};

struct AudioPackageData {
    core::pmr::Vector<float> samples{&core::defaultCpuMemoryResource()};
    std::uint32_t channels = 0u;
    AudioPackagePlayback playback{};
};

struct AudioPackageResult {
    bool success = false;
    AudioPackageData data{};
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
};

ARP_API AudioPackageResult readAudioPackage(
    const std::filesystem::path& path, std::size_t maxPayloadBytes);
ARP_API core::pmr::String writeAudioPackage(
    const std::filesystem::path& path, const AudioPackageData& data);

} // namespace arp::resource

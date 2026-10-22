#pragma once

#include "core/memory/cpu_allocator.h"

#include <cstddef>

namespace arp::audio {

class AudioSystem;

struct DecodedAudioPcm {
    core::pmr::Vector<float> samples{&core::defaultCpuMemoryResource()};
    std::uint32_t channels = 0u;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};

    bool success() const noexcept { return !samples.empty() && channels != 0u; }
};

ARP_API DecodedAudioPcm decodeAudioFileToRuntimePcm(const char* path, std::size_t maxBytes);

class MiniaudioBackend final {
public:
    // Opaque to consumers; public only so the C device callback can retain it.
    struct State;

    explicit MiniaudioBackend(AudioSystem& system);
    ~MiniaudioBackend();
    MiniaudioBackend(const MiniaudioBackend&) = delete;
    MiniaudioBackend& operator=(const MiniaudioBackend&) = delete;

    bool start() noexcept;
    void stop() noexcept;
    bool running() const noexcept;

private:
    core::UniquePtr<State> state_;
};

} // namespace arp::audio

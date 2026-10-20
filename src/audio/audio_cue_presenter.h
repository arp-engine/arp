#pragma once

#include "audio/audio_system.h"

namespace arp::audio {

// Gameplay-facing mapping layer. Games publish stable cue identifiers and
// register package-owned PCM views; they never call a native audio backend.
class AudioCuePresenter final {
public:
    explicit AudioCuePresenter(AudioSystem& system) : system_(system) {}

    bool registerCue(std::uint32_t cueId, AudioPlayRequest request) noexcept;
    VoiceId present(std::uint32_t cueId) noexcept;

private:
    struct Entry {
        std::uint32_t cueId = 0u;
        AudioPlayRequest request{};
    };

    static constexpr std::size_t kMaxCues = 128u;
    AudioSystem& system_;
    std::array<Entry, kMaxCues> entries_{};
};

} // namespace arp::audio

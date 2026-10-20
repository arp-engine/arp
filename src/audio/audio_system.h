#pragma once

#include "core/api.h"
#include "core/cpu_allocator.h"
#include "core/math.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace arp::audio {

class MiniaudioBackend;

constexpr std::uint32_t kAudioSampleRate = 48000u;
constexpr std::uint32_t kAudioOutputChannels = 2u;
constexpr std::size_t kMaxAudioVoices = 64u;
constexpr std::size_t kAudioCommandCapacity = 256u;

enum class AudioStatus : std::uint8_t {
    Ready,
    DeviceUnavailable,
    DeviceStartFailed,
    InvalidConfiguration,
    CommandQueueFull
};
enum class AudioBus : std::uint8_t { Master, Music, Sfx, Ui, Count };

struct AudioSpatialParams {
    core::Vec3 position{};
    float minDistance = 1.0f;
    float maxDistance = 25.0f;
    float spatialBlend = 0.0f;
};

struct AudioListenerState {
    core::Vec3 position{};
};

struct AudioClipView {
    const float* samples = nullptr;
    std::uint64_t frameCount = 0u;
    std::uint32_t sampleRate = kAudioSampleRate;
    std::uint32_t channels = kAudioOutputChannels;
    bool hasLoopPoints = false;
    std::uint64_t loopStartFrame = 0u;
    std::uint64_t loopEndFrame = 0u;

    bool valid() const noexcept
    {
        return samples != nullptr && frameCount != 0u && sampleRate == kAudioSampleRate
            && (channels == 1u || channels == kAudioOutputChannels)
            && (!hasLoopPoints || (loopEndFrame > loopStartFrame && loopEndFrame <= frameCount));
    }
};

struct VoiceId {
    std::uint64_t value = 0u;

    bool valid() const noexcept { return value != 0u; }
};

struct AudioPlayRequest {
    AudioClipView clip{};
    float volume = 1.0f;
    bool loop = false;
    AudioBus bus = AudioBus::Sfx;
    AudioSpatialParams spatial{};
};

struct AudioSystemDesc {
    std::uint32_t maxVoices = static_cast<std::uint32_t>(kMaxAudioVoices);
    bool startDevice = true;
};

struct AudioTelemetry {
    std::uint32_t activeVoices = 0u;
    std::uint64_t submittedCommands = 0u;
    std::uint64_t droppedCommands = 0u;
    std::uint64_t renderedFrames = 0u;
    bool deviceRunning = false;
    std::array<float, static_cast<std::size_t>(AudioBus::Count)> busVolumes{1.0f, 1.0f, 1.0f, 1.0f};
};

class ARP_API AudioSystem final {
public:
    explicit AudioSystem(AudioSystemDesc desc = {});
    ~AudioSystem();
    AudioSystem(const AudioSystem&) = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;
    AudioStatus status() const noexcept;
    bool startDevice() noexcept;
    void stopDevice() noexcept;
    VoiceId play(AudioClipView clip, float volume = 1.0f, bool loop = false) noexcept;
    VoiceId play(const AudioPlayRequest& request) noexcept;
    AudioStatus stop(VoiceId voice) noexcept;
    AudioStatus setVolume(VoiceId voice, float volume) noexcept;
    AudioStatus setPosition(VoiceId voice, AudioSpatialParams spatial) noexcept;
    AudioStatus setBusVolume(AudioBus bus, float volume) noexcept;
    AudioStatus setListener(AudioListenerState listener) noexcept;
    bool completionObserved(VoiceId voice) const noexcept;
    void suspend() noexcept;
    void resume() noexcept;
    // Deterministic offline rendering path used by unit and hidden-window smoke tests.
    void render(float* outputInterleavedStereo, std::uint32_t frameCount) noexcept;
    AudioTelemetry telemetry() const noexcept;

private:
    enum class CommandType : std::uint8_t {
        Play,
        Stop,
        SetVolume,
        SetPosition,
        SetBusVolume,
        SetListener,
        Suspend,
        Resume
    };

    struct Command {
        CommandType type = CommandType::Stop;
        VoiceId voice{};
        AudioPlayRequest request{};
        float volume = 1.0f;
        AudioBus bus = AudioBus::Sfx;
        AudioListenerState listener{};
    };

    struct Voice {
        VoiceId id{};
        AudioClipView clip{};
        std::uint64_t frameCursor = 0u;
        float volume = 1.0f;
        bool loop = false;
        bool active = false;
        AudioBus bus = AudioBus::Sfx;
        AudioSpatialParams spatial{};
    };

    bool enqueue(const Command& command) noexcept;
    bool dequeue(Command& command) noexcept;
    void consumeCommands() noexcept;
    void apply(const Command& command) noexcept;
    void renderVoice(Voice& voice, float* output, std::uint32_t frameCount) noexcept;
    AudioSystemDesc desc_{};
    std::array<Voice, kMaxAudioVoices> voices_{};
    std::array<Command, kAudioCommandCapacity> commands_{};
    std::atomic<std::uint32_t> commandWrite_{0u};
    std::atomic<std::uint32_t> commandRead_{0u};
    std::atomic<std::uint64_t> nextVoice_{1u};
    std::atomic<std::uint64_t> submittedCommands_{0u}, droppedCommands_{0u}, renderedFrames_{0u};
    std::atomic<std::uint32_t> activeVoices_{0u};
    std::array<std::atomic<std::uint64_t>, kMaxAudioVoices> completedVoices_{};
    std::atomic<std::uint32_t> completionWrite_{0u};
    std::array<float, static_cast<std::size_t>(AudioBus::Count)> busVolumes_{
        1.0f, 1.0f, 1.0f, 1.0f};
    AudioListenerState listener_{};
    bool suspended_ = false;
    AudioStatus status_ = AudioStatus::Ready;
    core::UniquePtr<MiniaudioBackend> backend_;
    void recordCompletion(VoiceId voice) noexcept;
};

} // namespace arp::audio

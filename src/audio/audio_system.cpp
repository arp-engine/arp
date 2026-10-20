#include "audio/audio_system.h"

#include "audio/miniaudio_backend.h"

#include <cmath>
#include <cstring>

namespace arp::audio {

namespace {
constexpr std::size_t busIndex(AudioBus bus)
{
    return static_cast<std::size_t>(bus);
}

bool validSpatial(const AudioSpatialParams& value)
{
    return value.minDistance > 0.0f && value.maxDistance >= value.minDistance
        && value.spatialBlend >= 0.0f && value.spatialBlend <= 1.0f;
}
} // namespace

AudioSystem::AudioSystem(AudioSystemDesc desc) : desc_(desc)
{
    if (desc_.maxVoices == 0u || desc_.maxVoices > kMaxAudioVoices) {
        status_ = AudioStatus::InvalidConfiguration;
        return;
    }
    backend_ = core::makeUnique<MiniaudioBackend>(*this);
    if (desc_.startDevice)
        startDevice();
}

AudioSystem::~AudioSystem()
{
    stopDevice();
}

AudioStatus AudioSystem::status() const noexcept
{
    return status_;
}

bool AudioSystem::startDevice() noexcept
{
    if (status_ == AudioStatus::InvalidConfiguration || !backend_)
        return false;
    if (backend_->running())
        return true;
    if (!backend_->start()) {
        status_ = AudioStatus::DeviceUnavailable;
        return false;
    }
    status_ = AudioStatus::Ready;
    return true;
}

void AudioSystem::stopDevice() noexcept
{
    if (backend_)
        backend_->stop();
}

VoiceId AudioSystem::play(AudioClipView clip, float volume, bool loop) noexcept
{
    return play({clip, volume, loop});
}

VoiceId AudioSystem::play(const AudioPlayRequest& request) noexcept
{
    if (!request.clip.valid() || request.volume < 0.0f
        || busIndex(request.bus) >= busVolumes_.size() || !validSpatial(request.spatial))
        return {};
    VoiceId voice{nextVoice_.fetch_add(1u, std::memory_order_relaxed)};
    Command command;
    command.type = CommandType::Play;
    command.voice = voice;
    command.request = request;
    return enqueue(command) ? voice : VoiceId{};
}

AudioStatus AudioSystem::stop(VoiceId voice) noexcept
{
    Command command;
    command.type = CommandType::Stop;
    command.voice = voice;
    return !voice.valid() ? AudioStatus::InvalidConfiguration
                          : (enqueue(command) ? AudioStatus::Ready : AudioStatus::CommandQueueFull);
}

AudioStatus AudioSystem::setVolume(VoiceId voice, float volume) noexcept
{
    Command command;
    command.type = CommandType::SetVolume;
    command.voice = voice;
    command.volume = volume;
    return !voice.valid() || volume < 0.0f
        ? AudioStatus::InvalidConfiguration
        : (enqueue(command) ? AudioStatus::Ready : AudioStatus::CommandQueueFull);
}

AudioStatus AudioSystem::setPosition(VoiceId voice, AudioSpatialParams spatial) noexcept
{
    Command command;
    command.type = CommandType::SetPosition;
    command.voice = voice;
    command.request.spatial = spatial;
    return !voice.valid() || !validSpatial(spatial)
        ? AudioStatus::InvalidConfiguration
        : (enqueue(command) ? AudioStatus::Ready : AudioStatus::CommandQueueFull);
}

AudioStatus AudioSystem::setBusVolume(AudioBus bus, float volume) noexcept
{
    Command command;
    command.type = CommandType::SetBusVolume;
    command.bus = bus;
    command.volume = volume;
    return busIndex(bus) >= busVolumes_.size() || volume < 0.0f
        ? AudioStatus::InvalidConfiguration
        : (enqueue(command) ? AudioStatus::Ready : AudioStatus::CommandQueueFull);
}

AudioStatus AudioSystem::setListener(AudioListenerState listener) noexcept
{
    Command command;
    command.type = CommandType::SetListener;
    command.listener = listener;
    return enqueue(command) ? AudioStatus::Ready : AudioStatus::CommandQueueFull;
}

bool AudioSystem::completionObserved(VoiceId voice) const noexcept
{
    if (!voice.valid())
        return true;
    for (const auto& completed : completedVoices_)
        if (completed.load(std::memory_order_acquire) == voice.value)
            return true;
    return false;
}

void AudioSystem::suspend() noexcept
{
    Command command;
    command.type = CommandType::Suspend;
    enqueue(command);
}

void AudioSystem::resume() noexcept
{
    Command command;
    command.type = CommandType::Resume;
    enqueue(command);
}

bool AudioSystem::enqueue(const Command& command) noexcept
{
    const auto write = commandWrite_.load(std::memory_order_relaxed);
    const auto next = (write + 1u) % kAudioCommandCapacity;
    if (next == commandRead_.load(std::memory_order_acquire)) {
        droppedCommands_.fetch_add(1u, std::memory_order_relaxed);
        return false;
    }
    commands_[write] = command;
    commandWrite_.store(next, std::memory_order_release);
    submittedCommands_.fetch_add(1u, std::memory_order_relaxed);
    return true;
}

bool AudioSystem::dequeue(Command& command) noexcept
{
    const auto read = commandRead_.load(std::memory_order_relaxed);
    if (read == commandWrite_.load(std::memory_order_acquire))
        return false;
    command = commands_[read];
    commandRead_.store((read + 1u) % kAudioCommandCapacity, std::memory_order_release);
    return true;
}

void AudioSystem::consumeCommands() noexcept
{
    Command command;
    while (dequeue(command))
        apply(command);
}

void AudioSystem::apply(const Command& command) noexcept
{
    if (command.type == CommandType::Play) {
        for (std::uint32_t i = 0u; i < desc_.maxVoices; ++i)
            if (!voices_[i].active) {
                auto& voice = voices_[i];
                voice = {};
                voice.id = command.voice;
                voice.clip = command.request.clip;
                voice.volume = command.request.volume;
                voice.loop = command.request.loop || command.request.clip.hasLoopPoints;
                voice.bus = command.request.bus;
                voice.spatial = command.request.spatial;
                voice.active = true;
                activeVoices_.fetch_add(1u, std::memory_order_relaxed);
                return;
            }
        droppedCommands_.fetch_add(1u, std::memory_order_relaxed);
        return;
    }
    if (command.type == CommandType::SetBusVolume) {
        busVolumes_[busIndex(command.bus)] = command.volume;
        return;
    }
    if (command.type == CommandType::SetListener) {
        listener_ = command.listener;
        return;
    }
    if (command.type == CommandType::Suspend) {
        suspended_ = true;
        return;
    }
    if (command.type == CommandType::Resume) {
        suspended_ = false;
        return;
    }
    for (auto& voice : voices_)
        if (voice.active && voice.id.value == command.voice.value) {
            if (command.type == CommandType::Stop) {
                voice.active = false;
                activeVoices_.fetch_sub(1u, std::memory_order_relaxed);
                recordCompletion(voice.id);
            } else if (command.type == CommandType::SetVolume)
                voice.volume = command.volume;
            else if (command.type == CommandType::SetPosition)
                voice.spatial = command.request.spatial;
            return;
        }
}

void AudioSystem::render(float* output, std::uint32_t frames) noexcept
{
    if (output == nullptr)
        return;
    std::memset(output, 0, static_cast<std::size_t>(frames) * kAudioOutputChannels * sizeof(float));
    consumeCommands();
    if (!suspended_)
        for (auto& voice : voices_)
            if (voice.active)
                renderVoice(voice, output, frames);
    renderedFrames_.fetch_add(frames, std::memory_order_relaxed);
}

void AudioSystem::renderVoice(Voice& voice, float* output, std::uint32_t frames) noexcept
{
    const auto range = core::distance(voice.spatial.position, listener_.position);
    const auto attenuated = range <= voice.spatial.minDistance
        ? 1.0f
        : (range >= voice.spatial.maxDistance ? 0.0f
                                              : 1.0f
                      - (range - voice.spatial.minDistance)
                          / (voice.spatial.maxDistance - voice.spatial.minDistance));
    const auto offsetX = voice.spatial.position.x - listener_.position.x;
    const auto pan = range > 0.0001f ? offsetX / range : 0.0f;
    const auto spatialLeft = std::sqrt(0.5f * (1.0f - pan));
    const auto spatialRight = std::sqrt(0.5f * (1.0f + pan));
    const auto gain =
        voice.volume * busVolumes_[busIndex(AudioBus::Master)] * busVolumes_[busIndex(voice.bus)];
    const auto leftGain = gain
        * ((1.0f - voice.spatial.spatialBlend)
            + voice.spatial.spatialBlend * attenuated * spatialLeft);
    const auto rightGain = gain
        * ((1.0f - voice.spatial.spatialBlend)
            + voice.spatial.spatialBlend * attenuated * spatialRight);
    const auto loopEnd = voice.clip.hasLoopPoints ? voice.clip.loopEndFrame : voice.clip.frameCount;
    const auto loopStart = voice.clip.hasLoopPoints ? voice.clip.loopStartFrame : 0u;
    for (std::uint32_t frame = 0u; frame < frames; ++frame) {
        if (voice.frameCursor == loopEnd) {
            if (!voice.loop) {
                voice.active = false;
                activeVoices_.fetch_sub(1u, std::memory_order_relaxed);
                recordCompletion(voice.id);
                return;
            }
            voice.frameCursor = loopStart;
        }
        const auto sample = voice.frameCursor * voice.clip.channels;
        const float left = voice.clip.samples[sample];
        const float right = voice.clip.channels == 1u ? left : voice.clip.samples[sample + 1u];
        output[frame * 2u] += left * leftGain;
        output[frame * 2u + 1u] += right * rightGain;
        ++voice.frameCursor;
    }
}

void AudioSystem::recordCompletion(VoiceId voice) noexcept
{
    const auto index = completionWrite_.fetch_add(1u, std::memory_order_relaxed) % kMaxAudioVoices;
    completedVoices_[index].store(voice.value, std::memory_order_release);
}

AudioTelemetry AudioSystem::telemetry() const noexcept
{
    return {activeVoices_.load(),
        submittedCommands_.load(),
        droppedCommands_.load(),
        renderedFrames_.load(),
        backend_ && backend_->running(),
        busVolumes_};
}

} // namespace arp::audio

#define MA_API static
#define MINIAUDIO_IMPLEMENTATION
#include "audio/miniaudio_backend.h"

#include "audio/audio_system.h"
#include "miniaudio.h"

namespace arp::audio {

DecodedAudioPcm decodeAudioFileToRuntimePcm(const char* path, std::size_t maxBytes)
{
    DecodedAudioPcm result;
    if (path == nullptr || maxBytes == 0u) {
        result.diagnostics = "audio import requires a source path and decoded-byte budget";
        return result;
    }
    ma_decoder_config config =
        ma_decoder_config_init(ma_format_f32, kAudioOutputChannels, kAudioSampleRate);
    ma_decoder decoder{};
    if (ma_decoder_init_file(path, &config, &decoder) != MA_SUCCESS) {
        result.diagnostics = "miniaudio could not decode the source audio file";
        return result;
    }
    ma_uint64 frames = 0u;
    const auto lengthStatus = ma_decoder_get_length_in_pcm_frames(&decoder, &frames);
    const auto requiredBytes = frames * kAudioOutputChannels * sizeof(float);
    if (lengthStatus != MA_SUCCESS || frames == 0u || requiredBytes > maxBytes) {
        ma_decoder_uninit(&decoder);
        result.diagnostics = lengthStatus != MA_SUCCESS
            ? "audio source does not provide a deterministic frame length"
            : "audio source exceeds decoded-byte budget";
        return result;
    }
    result.samples.resize(static_cast<std::size_t>(frames) * kAudioOutputChannels);
    ma_uint64 readFrames = 0u;
    const auto readStatus =
        ma_decoder_read_pcm_frames(&decoder, result.samples.data(), frames, &readFrames);
    ma_decoder_uninit(&decoder);
    if (readStatus != MA_SUCCESS || readFrames != frames) {
        result.samples.clear();
        result.diagnostics = "audio source ended before its declared PCM frame length";
        return result;
    }
    result.channels = kAudioOutputChannels;
    return result;
}

struct MiniaudioBackend::State {
    explicit State(AudioSystem& owner) : system(owner) {}

    AudioSystem& system;
    ma_device device{};
    bool initialized = false;
    bool isRunning = false;
};

namespace {
void dataCallback(ma_device* device, void* output, const void*, ma_uint32 frames)
{
    auto* state = static_cast<MiniaudioBackend::State*>(device->pUserData);
    state->system.render(static_cast<float*>(output), frames);
}
} // namespace

MiniaudioBackend::MiniaudioBackend(AudioSystem& system) : state_(core::makeUnique<State>(system))
{}

MiniaudioBackend::~MiniaudioBackend()
{
    stop();
}

bool MiniaudioBackend::start() noexcept
{
    if (state_->isRunning)
        return true;
    auto config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = kAudioOutputChannels;
    config.sampleRate = kAudioSampleRate;
    config.dataCallback = dataCallback;
    config.pUserData = state_.get();
    if (ma_device_init(nullptr, &config, &state_->device) != MA_SUCCESS)
        return false;
    state_->initialized = true;
    if (ma_device_start(&state_->device) != MA_SUCCESS) {
        stop();
        return false;
    }
    state_->isRunning = true;
    return true;
}

void MiniaudioBackend::stop() noexcept
{
    if (state_ && state_->initialized)
        ma_device_uninit(&state_->device);
    if (state_) {
        state_->initialized = false;
        state_->isRunning = false;
    }
}

bool MiniaudioBackend::running() const noexcept
{
    return state_ && state_->isRunning;
}

} // namespace arp::audio

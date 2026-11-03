#define MA_API static
#define MINIAUDIO_IMPLEMENTATION
#include "audio/miniaudio_backend.h"

#include "audio/audio_importer.h"
#include "audio/audio_system.h"
#include "miniaudio.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace arp::audio {

namespace {
void failImport(DecodedAudioPcm& result, const char* id, const char* message) noexcept
{
    core::pmr::Vector<float>{}.swap(result.samples);
    result.channels = 0;
    result.diagnosticId = id;
    try {
        result.diagnostics = message;
    } catch (...) {
        result.diagnostics.clear();
    }
}

// Miniaudio has its own allocator callback ABI. Only the adapter belongs here;
// reservations, ownership inspection and actual allocations use core.
class DecoderMemory final {
public:
    enum class Failure { None, Budget, Allocation };

    explicit DecoderMemory(std::size_t maximum) : quota_(maximum) {}

    ma_allocation_callbacks callbacks() noexcept { return {this, allocate, reallocate, release}; }

    std::uint64_t live() const noexcept { return quota_.liveBytes(); }

    std::uint64_t peak() const noexcept { return allocatedPeakBytes_; }

    Failure failure() const noexcept { return failure_; }

private:
    static core::CpuAllocationDesc description(std::size_t bytes = 0) noexcept
    {
        return {bytes,
            alignof(std::max_align_t),
            core::CpuAllocationIntent::AssetImport,
            "miniaudio-decoder"};
    }

    static void* allocate(std::size_t bytes, void* context) noexcept
    {
        auto& self = *static_cast<DecoderMemory*>(context);
        if (!bytes)
            return nullptr;
        if (!self.quota_.tryReserve(bytes)) {
            self.failure_ = Failure::Budget;
            return nullptr;
        }
        try {
            auto result = core::defaultCpuAllocator().allocate(description(bytes));
            if (result.ptr) {
                self.allocatedPeakBytes_ =
                    std::max(self.allocatedPeakBytes_, self.quota_.liveBytes());
                return result.ptr;
            }
        } catch (...) {
        }
        self.quota_.release(bytes);
        self.failure_ = Failure::Allocation;
        return nullptr;
    }

    static void release(void* memory, void* context) noexcept
    {
        if (!memory)
            return;
        auto& self = *static_cast<DecoderMemory*>(context);
        const auto owned = core::inspectCpuAllocationCharge(memory);
        if (!owned.found) {
            self.failure_ = Failure::Allocation;
            return;
        }
        core::defaultCpuAllocator().deallocate(memory, description(owned.sizeBytes));
        self.quota_.release(owned.sizeBytes);
    }

    static void* reallocate(void* memory, std::size_t bytes, void* context) noexcept
    {
        if (!bytes) {
            release(memory, context);
            return nullptr;
        }
        if (!memory)
            return allocate(bytes, context);
        const auto owned = core::inspectCpuAllocationCharge(memory);
        if (!owned.found) {
            static_cast<DecoderMemory*>(context)->failure_ = Failure::Allocation;
            return nullptr;
        }
        // Old and new blocks overlap in lifetime and are both charged. Failure
        // leaves the old allocation owned by the decoder, as realloc requires.
        auto* replacement = allocate(bytes, context);
        if (!replacement)
            return nullptr;
        std::memcpy(replacement, memory, std::min(bytes, owned.sizeBytes));
        release(memory, context);
        return replacement;
    }

    core::CpuMemoryQuota quota_;
    std::uint64_t allocatedPeakBytes_ = 0;
    Failure failure_ = Failure::None;
};

class AudioDecoder final {
public:
    ~AudioDecoder() { close(); }

    bool openFile(const char* path, const ma_decoder_config& config)
    {
        initialized_ = ma_decoder_init_file(path, &config, &decoder_) == MA_SUCCESS;
        return initialized_;
    }

    bool openMemory(const AudioMemoryImportDesc& source, const ma_decoder_config& config)
    {
        source_ = source.bytes;
        cursor_ = 0;
        // The locked direct-memory helper repeats backend cleanup when converter
        // initialization fails. The public callback initializer has one cleanup
        // owner and consumes the same explicit Wave backend.
        initialized_ =
            ma_decoder_init(readMemory, seekMemory, this, &config, &decoder_) == MA_SUCCESS;
        return initialized_;
    }

    void close() noexcept
    {
        if (initialized_)
            ma_decoder_uninit(&decoder_);
        initialized_ = false;
    }

    void decode(std::size_t maximum, DecodedAudioPcm& result)
    {
        ma_uint64 frames = 0;
        const auto length = ma_decoder_get_length_in_pcm_frames(&decoder_, &frames);
        constexpr auto frameBytes = kAudioOutputChannels * sizeof(float);
        if (length != MA_SUCCESS || !frames) {
            failImport(result,
                "AUDIO-IMPORT-LENGTH",
                length != MA_SUCCESS ? "audio source does not provide a deterministic frame length"
                                     : "audio source exceeds decoded-byte budget");
            return;
        }
        if (frames > maximum / frameBytes
            || frames > std::numeric_limits<std::size_t>::max() / frameBytes) {
            failImport(
                result, "AUDIO-IMPORT-DECODED-BUDGET", "audio source exceeds decoded-byte budget");
            return;
        }
        result.samples.resize(static_cast<std::size_t>(frames) * kAudioOutputChannels);
        ma_uint64 readFrames = 0;
        const auto status =
            ma_decoder_read_pcm_frames(&decoder_, result.samples.data(), frames, &readFrames);
        if (status != MA_SUCCESS || readFrames != frames) {
            failImport(result,
                "AUDIO-IMPORT-SHORT-READ",
                "audio source ended before its declared PCM frame length");
            return;
        }
        result.channels = kAudioOutputChannels;
    }

private:
    static ma_result readMemory(
        ma_decoder* decoder, void* output, std::size_t requested, std::size_t* read) noexcept
    {
        auto& self = *static_cast<AudioDecoder*>(decoder->pUserData);
        if (read)
            *read = 0;
        if (requested && !output)
            return MA_INVALID_ARGS;
        const auto count = std::min(requested, self.source_.size() - self.cursor_);
        if (count)
            std::memcpy(output, self.source_.data() + self.cursor_, count);
        self.cursor_ += count;
        if (read)
            *read = count;
        return requested && !count ? MA_AT_END : MA_SUCCESS;
    }

    static ma_result seekMemory(
        ma_decoder* decoder, ma_int64 offset, ma_seek_origin origin) noexcept
    {
        auto& self = *static_cast<AudioDecoder*>(decoder->pUserData);
        const std::size_t anchor = origin == ma_seek_origin_start ? 0
            : origin == ma_seek_origin_current                    ? self.cursor_
                                                                  : self.source_.size();
        if (origin != ma_seek_origin_start && origin != ma_seek_origin_current
            && origin != ma_seek_origin_end)
            return MA_BAD_SEEK;
        if (offset >= 0) {
            const auto distance = static_cast<ma_uint64>(offset);
            if (distance > self.source_.size() - anchor)
                return MA_BAD_SEEK;
            self.cursor_ = anchor + static_cast<std::size_t>(distance);
        } else {
            const auto distance = static_cast<ma_uint64>(-(offset + 1)) + 1;
            if (distance > anchor)
                return MA_BAD_SEEK;
            self.cursor_ = anchor - static_cast<std::size_t>(distance);
        }
        return MA_SUCCESS;
    }

    ma_decoder decoder_{};
    core::ArrayView<const std::uint8_t> source_;
    std::size_t cursor_ = 0;
    bool initialized_ = false;
};
} // namespace

DecodedAudioPcm decodeAudioFileToRuntimePcm(const char* path, std::size_t maxBytes)
{
    DecodedAudioPcm result;
    if (!path || !maxBytes) {
        failImport(result,
            "AUDIO-IMPORT-CONFIG",
            "audio import requires a source path and decoded-byte budget");
        return result;
    }
    const auto config =
        ma_decoder_config_init(ma_format_f32, kAudioOutputChannels, kAudioSampleRate);
    AudioDecoder decoder;
    if (!decoder.openFile(path, config)) {
        failImport(
            result, "AUDIO-IMPORT-DECODE", "miniaudio could not decode the source audio file");
        return result;
    }
    decoder.decode(maxBytes, result);
    return result;
}

DecodedAudioPcm decodeAudioMemoryToRuntimePcm(const AudioMemoryImportDesc& desc)
{
    DecodedAudioPcm result;
    if (!desc.bytes.data() || !desc.bytes.size() || !desc.maxEncodedBytes || !desc.maxDecodedBytes
        || !desc.maxDecoderBytes || desc.maxEncodedBytes == SIZE_MAX
        || desc.maxDecodedBytes == SIZE_MAX || desc.maxDecoderBytes == SIZE_MAX) {
        failImport(result,
            "AUDIO-IMPORT-CONFIG",
            "audio memory import requires source bytes and finite positive budgets");
        return result;
    }
    if (desc.format != AudioSourceFormat::Wave) {
        failImport(result,
            "AUDIO-IMPORT-FORMAT-UNSUPPORTED",
            "audio memory import requires an explicit supported Wave format");
        return result;
    }
    if (desc.bytes.size() > desc.maxEncodedBytes) {
        failImport(
            result, "AUDIO-IMPORT-ENCODED-BUDGET", "audio source exceeds encoded-byte budget");
        return result;
    }
    DecoderMemory memory(desc.maxDecoderBytes);
    AudioDecoder decoder;
    try {
        auto config = ma_decoder_config_init(ma_format_f32, kAudioOutputChannels, kAudioSampleRate);
        config.encodingFormat = ma_encoding_format_wav;
        config.allocationCallbacks = memory.callbacks();
        // No custom decoders: explicit Wave failure cannot select MP3/FLAC.
        if (!decoder.openMemory(desc, config))
            failImport(result,
                "AUDIO-IMPORT-FORMAT-MISMATCH",
                "miniaudio could not decode the declared Wave source");
        else
            decoder.decode(desc.maxDecodedBytes, result);
    } catch (...) {
        failImport(result, "AUDIO-IMPORT-ALLOCATION", "audio import allocation failed");
    }
    decoder.close();
    result.decoderLiveBytes = memory.live();
    result.decoderPeakBytes = memory.peak();
    if (memory.failure() != DecoderMemory::Failure::None)
        failImport(result,
            memory.failure() == DecoderMemory::Failure::Budget ? "AUDIO-IMPORT-DECODER-BUDGET"
                                                               : "AUDIO-IMPORT-ALLOCATION",
            "audio decoder allocation was rejected");
    if (result.decoderLiveBytes)
        failImport(result,
            "AUDIO-IMPORT-DECODER-LIFETIME",
            "audio decoder retained internal allocations after close");
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

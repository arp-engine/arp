#pragma once

#include "audio/audio_importer.h"
#include "core/diagnostics/diagnostic_error.h"
#include "core/hash/sha256.h"
#include "tests/fixtures/queen_source_runtime.h"

#include <algorithm>
#include <array>

namespace audio_fixture {

struct Input {
    const char* name;
    const char* sourceSha256;
    const char* pcmSha256;
    std::size_t samples;
};

// Original authored PCM16/48000/mono WAVs. PCM hashes are independently
// calculated from signed little-endian PCM16, duplicated to stereo float32.
inline constexpr std::array<Input, 4> inputs{
    {{"hit.wav",
         "sha256:b69a5e1e544a49bc2e3510ded36c4ab4cb5d6a52503a10463976ec1db023a9fb",
         "sha256:93f9c8443c01898923660e9fb420c6f0871bac333d783517b61b029d428b10e1",
         7680},
        {"upgrade.wav",
            "sha256:909779b443b694566d694e30698a06f06c6b00a060669bca86b458eea5295c92",
            "sha256:bbc68d25509f369c8d9b6d264eac5fe7ebdbb8a1f2788c3c19c316775a2bf1a7",
            34560},
        {"menu.wav",
            "sha256:b73ca59f9f2fef1868784e357ce4919b61f44fe9951d798fcc45330520bc0eff",
            "sha256:939bbab4adca993e2ac830d19857fc9666214a5680653d4f41ca906b62db82dc",
            13440},
        {"music.wav",
            "sha256:f6aac27862cdd5a30c5fdd7532daef1fdb0f67209406075bc72e0bf6174d5301",
            "sha256:b562dd5612412b3751f9fd4bc0fcf01d7b12d2f5b83e261d98f10460bda4cf9e",
            268800}}};

inline constexpr std::size_t maxSourceBytes = 2u * 1024u * 1024u;
inline constexpr std::size_t maxPcmBytes = 4u * 1024u * 1024u;
inline constexpr std::size_t maxDecoderBytes = 1024u * 1024u;

inline arp::audio::AudioMemoryImportDesc request(const arp::core::pmr::Vector<std::uint8_t>& bytes)
{
    return {{bytes.data(), bytes.size()},
        arp::audio::AudioSourceFormat::Wave,
        maxSourceBytes,
        maxPcmBytes,
        maxDecoderBytes};
}

inline void require(bool condition, const char* diagnostic)
{
    if (!condition)
        throw arp::core::DiagnosticError(diagnostic);
}

inline void verify(const Input& input, const arp::audio::DecodedAudioPcm& pcm)
{
    require(pcm.success() && pcm.channels == 2 && !pcm.decoderLiveBytes
            && pcm.samples.size() == input.samples,
        "AUDIO-MEMORY-PROBE-PCM");
    const auto hash = arp::core::Sha256::hexDigest(
        {reinterpret_cast<const char*>(pcm.samples.data()), pcm.samples.size() * sizeof(float)});
    require(hash == input.pcmSha256, "AUDIO-MEMORY-PROBE-PCM-HASH");
}

// Borrowed callback target stays alive until its owned runtime has drained.
// Both native HTTP and actual browser Fetch execute this exact input cycle.
class SourceCycle final {
public:
    explicit SourceCycle(std::string_view url)
        : runtime_(queen_source_fixture::create(url, maxSourceBytes))
    {}

    ~SourceCycle()
    {
        if (runtime_.owner)
            runtime_.owner->shutdown();
    }

    bool tick()
    {
        require(!diagnostic_, diagnostic_ ? diagnostic_ : "AUDIO-MEMORY-PROBE-STATE");
        if (index_ == inputs.size()) {
            runtime_.owner->shutdown();
            runtime_.owner.reset();
            runtime_.mounts.reset();
            for (std::size_t i = 0; i < inputs.size(); ++i) {
                auto pcm = arp::audio::importAudioMemory(request(bytes_[i]));
                verify(inputs[i], pcm);
                peak_ = std::max(peak_, pcm.decoderPeakBytes);
                arp::core::pmr::Vector<std::uint8_t>{}.swap(bytes_[i]);
                verify(inputs[i], pcm); // PCM survives both IO and source owners.
            }
            return true;
        }
        require(runtime_.owner->dispatch().success(), "AUDIO-MEMORY-PROBE-DISPATCH");
        require(!diagnostic_, diagnostic_ ? diagnostic_ : "AUDIO-MEMORY-PROBE-STATE");
        if (!pending_ && index_ < inputs.size()) {
            arp::core::io::FileReadRequest request;
            request.contractVersion = 1;
            request.pathScope = arp::core::io::FilePathScope::MountedResource;
            request.mount = runtime_.mount;
            request.path = inputs[index_].name;
            request.maxBytes = maxSourceBytes;
            require(runtime_.owner->fileService().read(request, {complete, this})
                    == arp::core::io::FileIoStatus::Accepted,
                "AUDIO-MEMORY-PROBE-SUBMIT");
            pending_ = true;
        }
        return false;
    }

    std::uint64_t peak() const noexcept { return peak_; }

private:
    static void complete(void* context, arp::core::io::FileReadResult result) noexcept
    {
        auto& self = *static_cast<SourceCycle*>(context);
        self.pending_ = false;
        if (result.status != arp::core::io::FileIoStatus::Success) {
            self.diagnostic_ =
                result.diagnosticId ? result.diagnosticId : "AUDIO-MEMORY-PROBE-READ";
            return;
        }
        try {
            const auto hash = arp::core::Sha256::hexDigest(
                {reinterpret_cast<const char*>(result.bytes.data()), result.bytes.size()});
            if (hash != inputs[self.index_].sourceSha256) {
                self.diagnostic_ = "AUDIO-MEMORY-PROBE-SOURCE-HASH";
                return;
            }
            self.bytes_[self.index_++] = std::move(result.bytes);
        } catch (...) {
            self.diagnostic_ = "AUDIO-MEMORY-PROBE-ALLOCATION";
        }
    }

    queen_source_fixture::Runtime runtime_;
    std::array<arp::core::pmr::Vector<std::uint8_t>, 4> bytes_;
    std::size_t index_ = 0;
    std::uint64_t peak_ = 0;
    const char* diagnostic_ = nullptr;
    bool pending_ = false;
};
} // namespace audio_fixture

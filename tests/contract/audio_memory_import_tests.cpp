#include "audio/audio_importer.h"
#include "core/io/file_io.h"
#include "tests/fixtures/audio_memory_sources.h"
#include "tests/fixtures/audio_wave.h"

#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>

namespace {
using namespace arp;
constexpr auto sourceBudget = audio_fixture::maxSourceBytes;
constexpr auto pcmBudget = audio_fixture::maxPcmBytes;
using audio_fixture::request;

void successAndBudgets(const core::pmr::Vector<std::uint8_t>& bytes)
{
    auto desc = request(bytes);
    auto result = audio::importAudioMemory(desc);
    if (!result.success())
        std::fprintf(stderr,
            "%s: %s live=%llu peak=%llu\n",
            result.diagnosticId,
            result.diagnostics.c_str(),
            (unsigned long long)result.decoderLiveBytes,
            (unsigned long long)result.decoderPeakBytes);
    assert(result.success() && result.channels == 2 && !result.diagnosticId);
    assert(!result.decoderLiveBytes && result.decoderPeakBytes > 0);
    const auto peak = result.decoderPeakBytes;
    desc.maxEncodedBytes = bytes.size();
    desc.maxDecodedBytes = result.samples.size() * sizeof(float);
    desc.maxDecoderBytes = peak;
    auto exact = audio::importAudioMemory(desc);
    assert(exact.success() && exact.samples == result.samples && !exact.decoderLiveBytes);
    for (unsigned fault = 0; fault < 9; ++fault) {
        auto bad = desc;
        const char* expected = nullptr;
        if (fault == 0) {
            bad.format = audio::AudioSourceFormat::Unspecified;
            expected = "AUDIO-IMPORT-FORMAT-UNSUPPORTED";
        } else if (fault == 1) {
            --bad.maxEncodedBytes;
            expected = "AUDIO-IMPORT-ENCODED-BUDGET";
        } else if (fault == 2) {
            --bad.maxDecodedBytes;
            expected = "AUDIO-IMPORT-DECODED-BUDGET";
        } else if (fault == 3 || fault == 4) {
            bad.maxDecoderBytes = fault == 3 ? 1 : peak - 1;
            expected = "AUDIO-IMPORT-DECODER-BUDGET";
        } else {
            if (fault == 5)
                bad.bytes = {};
            if (fault == 6)
                bad.maxDecoderBytes = 0;
            if (fault == 7)
                bad.maxEncodedBytes = SIZE_MAX;
            if (fault == 8)
                bad.maxDecodedBytes = 0;
            expected = "AUDIO-IMPORT-CONFIG";
        }
        auto rejected = audio::importAudioMemory(bad);
        if (rejected.decoderLiveBytes)
            std::fprintf(stderr,
                "fault=%u leaked=%llu id=%s\n",
                fault,
                (unsigned long long)rejected.decoderLiveBytes,
                rejected.diagnosticId);
        assert(!rejected.success() && rejected.samples.empty() && rejected.channels == 0);
        if (!rejected.diagnosticId || std::strcmp(rejected.diagnosticId, expected) != 0)
            std::fprintf(stderr,
                "fault=%u expected=%s actual=%s live=%llu peak=%llu\n",
                fault,
                expected,
                rejected.diagnosticId ? rejected.diagnosticId : "none",
                (unsigned long long)rejected.decoderLiveBytes,
                (unsigned long long)rejected.decoderPeakBytes);
        assert(rejected.diagnosticId && std::strcmp(rejected.diagnosticId, expected) == 0);
        assert(rejected.decoderLiveBytes == 0);
    }
    {
        auto quota = core::makeShared<core::CpuMemoryQuota>(1);
        core::CpuMemoryQuotaScope scope(quota);
        const auto denied = audio::importAudioMemory(desc);
        assert(!denied.success() && denied.diagnosticId
            && std::strcmp(denied.diagnosticId, "AUDIO-IMPORT-ALLOCATION") == 0);
        assert(!denied.decoderLiveBytes && !quota->liveBytes());
    }
}

void fixtureCycle()
{
    constexpr std::array<std::int16_t, 8> values{{16384, -16384, 0, 8192, -8192, 32767, -32768, 0}};
    for (const auto channels : {1u, 2u})
        for (const auto rate : {24000u, 48000u}) {
            const auto bytes =
                audio_fixture::Wave::pcm16(channels, rate, {values.data(), values.size()});
            successAndBudgets(bytes);
            if (rate == 48000) {
                const auto decoded = audio::importAudioMemory(request(bytes));
                for (std::size_t i = 0; i < values.size(); ++i)
                    if (channels == 1)
                        assert(decoded.samples[i * 2] == values[i] / 32768.f
                            && decoded.samples[i * 2 + 1] == values[i] / 32768.f);
                    else
                        assert(decoded.samples[i] == values[i] / 32768.f);
            }
        }
    auto bytes = audio_fixture::Wave::pcm16(1, 48000, {values.data(), values.size()});
    for (unsigned fault = 0; fault < 3; ++fault) {
        auto invalid = bytes;
        if (fault == 0)
            invalid[0] = 'X';
        if (fault == 1)
            invalid.resize(12);
        if (fault == 2)
            invalid.resize(invalid.size() - 2);
        const auto result = audio::importAudioMemory(request(invalid));
        assert(!result.success() && result.diagnosticId && !result.decoderLiveBytes);
    }
    // A declared, huge PCM body cannot overflow a size calculation or allocate
    // outside the finite decoded-byte admission, even with a tiny input owner.
    for (unsigned i = 40; i < 44; ++i)
        bytes[i] = 0xff;
    const auto huge = audio::importAudioMemory(request(bytes));
    assert(!huge.success() && !huge.decoderLiveBytes);
}

void legacyFixtures()
{
    constexpr std::array<std::int16_t, 8> values{{16384, -16384, 0, 8192, -8192, 32767, -32768, 0}};
    const auto path = std::filesystem::temp_directory_path()
        / ("arp-audio-memory-"
            + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".wav");
    for (const auto channels : {1u, 2u})
        for (const auto rate : {24000u, 48000u}) {
            const auto bytes =
                audio_fixture::Wave::pcm16(channels, rate, {values.data(), values.size()});
            {
                std::ofstream output(path, std::ios::binary);
                output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
                assert(output.good());
            }
            const auto old = audio::importAudioFile(path.string().c_str(), pcmBudget);
            const auto prepared = audio::importAudioMemory(request(bytes));
            assert(old.success() && prepared.success() && old.channels == prepared.channels
                && old.samples == prepared.samples);
        }
    assert(std::filesystem::remove(path));
}

void originalFiles(const std::filesystem::path& root)
{
    for (const auto& inputIdentity : audio_fixture::inputs) {
        const auto* name = inputIdentity.name;
        const auto path = root / name;
        auto input = core::readBinaryFile(path, sourceBudget);
        assert(input.success);
        const auto before = audio::importAudioFile(path.string().c_str(), pcmBudget);
        const auto after = audio::importAudioMemory(request(input.bytes));
        assert(before.success() && after.success() && before.channels == after.channels);
        assert(before.samples == after.samples && !after.decoderLiveBytes);
        audio_fixture::verify(inputIdentity, before);
        audio_fixture::verify(inputIdentity, after);
        std::printf("original %s encoded=%zu samples=%zu decoderPeak=%llu\n",
            name,
            input.bytes.size(),
            after.samples.size(),
            (unsigned long long)after.decoderPeakBytes);
    }
}
} // namespace

int main(int argc, char** argv)
{
    if (argc == 3 && std::strcmp(argv[1], "--http") == 0) {
        audio_fixture::SourceCycle cycle(argv[2]);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (!cycle.tick()) {
            assert(std::chrono::steady_clock::now() < deadline);
            std::this_thread::yield();
        }
        std::printf("PASS engine HTTP4 -> retired IO/source -> original PCM; decoder peak=%llu\n",
            (unsigned long long)cycle.peak());
        return 0;
    }
    legacyFixtures();
    fixtureCycle();
    const auto before = core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < (argc > 1 && std::strcmp(argv[1], "--smoke") == 0 ? 20u : 1u); ++i)
        fixtureCycle();
    const auto after = core::defaultCpuAllocator().stats();
    std::printf("audio memory core %llu -> %llu live %llu -> %llu\n",
        (unsigned long long)before.liveBytes,
        (unsigned long long)after.liveBytes,
        (unsigned long long)(before.allocationCount - before.deallocationCount),
        (unsigned long long)(after.allocationCount - after.deallocationCount));
    assert(before.liveBytes == after.liveBytes);
    assert(before.allocationCount - before.deallocationCount
        == after.allocationCount - after.deallocationCount);
    if (argc == 3 && std::strcmp(argv[1], "--original") == 0)
        originalFiles(argv[2]);
    std::puts("PASS explicit Wave memory decode, budgets and owned PCM lifecycle");
}

#include "core/diagnostic_error.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/frontend/transfer_texture_readback_batch.h"

#include <cassert>
#include <type_traits>

namespace {
using namespace arp::rhi;

class LegacyExtension : public TextureReadbackExtension {
public:
    bool schedule(CommandEncoder&, const GpuTexture&, std::string_view, std::string&) override
    {
        assert(false && "tracked requests must not fall back to legacy");
        return false;
    }

    bool poll(TextureReadbackSample&) override { return false; }
};

class ContractExtension final : public LegacyExtension {
public:
    bool supportsTrackedRequests() const override { return true; }

    bool scheduleTracked(CommandEncoder&,
        const GpuTexture&,
        std::string_view,
        TextureReadbackIdentity identity,
        arp::core::pmr::String& diagnostic) override
    {
        last = identity;
        diagnostic = accepted ? "" : "injected scheduling failure";
        return accepted;
    }

    bool accepted = true;
    TextureReadbackIdentity last;
};
} // namespace

int main()
{
    static_assert(!std::is_copy_constructible_v<TextureReadbackBatch<2>>);
    static_assert(!std::is_move_constructible_v<TextureReadbackBatch<2>>);
    // Graph execution reports std::exception after the recording stack has
    // unwound. Diagnostics must own their source through that boundary.
    char temporaryDiagnostic[] = "recording failed";
    try {
        throw arp::core::DiagnosticError(temporaryDiagnostic);
    } catch (const std::exception& error) {
        temporaryDiagnostic[0] = 'X';
        assert(std::string_view(error.what()) == "recording failed");
    }
    auto device = createNullDevice();
    auto texture = device->createTexture({"BatchInput",
        {2, 1, 1},
        TextureFormat::Rgba8Unorm,
        TextureUsage::CopySrc,
        MemoryUsage::GpuOnly,
        ResourceState::CopySrc});
    auto commands = device->createCommandEncoder({"BatchContract"});
    assert(texture && commands);
    ContractExtension extension;
    const auto prepare = [&](auto& batch) {
        assert(batch.begin(120, 17));
        assert(batch.expect("color", *texture));
        assert(batch.seal());
        assert(batch.schedule(extension, *commands, 0, *texture));
    };
    const auto goodSample = [&] {
        TextureReadbackSample sample;
        sample.label = "color";
        sample.identity = extension.last;
        sample.source = texture->id();
        sample.textureFormat = texture->desc().format;
        sample.width = 2;
        sample.height = 1;
        sample.completion = TextureReadbackCompletion::Completed;
        const std::array<std::uint8_t, 8> pixels{0, 17, 128, 255, 255, 128, 17, 0};
        applyTextureReadbackStatistics(
            sample, sample.textureFormat, pixels.data(), pixels.size(), 2, 1, 8);
        return sample;
    };
    TextureReadbackBatch<2> empty;
    assert(!empty.finish());
    assert(empty.failed());
    TextureReadbackBatch<2> success;
    prepare(success);
    auto sample = goodSample();
    assert(!success.complete());
    assert(success.consume(sample) && success.complete() && success.finish());
    assert(!success.consume(sample)); // A duplicate invalidates even an apparent PASS.
    assert(success.failed() && !success.finish());
    assert(!success.begin(121, 18));
    for (unsigned mutation = 0; mutation < 13; ++mutation) {
        TextureReadbackBatch<2> batch;
        prepare(batch);
        auto bad = goodSample();
        switch (mutation) {
        case 0:
            ++bad.identity.frameSerial;
            break;
        case 1:
            ++bad.identity.resourceGeneration;
            break;
        case 2:
            ++bad.identity.requestId;
            break;
        case 3:
            ++bad.source.value;
            break;
        case 4:
            bad.label = "other";
            break;
        case 5:
            ++bad.width;
            break;
        case 6:
            bad.textureFormat = TextureFormat::Bgra8Unorm;
            break;
        case 7:
            bad.completion = TextureReadbackCompletion::Failed;
            break;
        case 8:
            bad.completion = TextureReadbackCompletion::Abandoned;
            break;
        case 9:
            bad.channelStatisticsVersion = 0;
            break;
        case 10:
            bad.channelStatistics.channels[3].finiteCount = 1;
            break;
        case 11:
            bad.diagnostic = "GPU diagnostic";
            break;
        case 12:
            bad.channelStatistics.channelCount = 0;
            break;
        }
        assert(!batch.consume(bad));
        assert(!batch.finish());
        const auto firstError = batch.diagnostic();
        assert(!batch.fail("later error"));
        assert(batch.diagnostic() == firstError);
    }
    TextureReadbackBatch<2> missing;
    prepare(missing);
    assert(!missing.finish());
    TextureReadbackBatch<2> partial;
    assert(partial.begin(120, 17));
    assert(partial.expect("color", *texture) && partial.expect("second", *texture));
    assert(partial.seal());
    assert(partial.schedule(extension, *commands, 0, *texture));
    assert(partial.consume(goodSample()));
    assert(!partial.complete() && !partial.finish());
    TextureReadbackBatch<1> overflow;
    assert(overflow.begin(120, 17));
    assert(overflow.expect("color", *texture));
    assert(!overflow.expect("second", *texture));
    TextureReadbackBatch<2> duplicateExpected;
    assert(duplicateExpected.begin(120, 17));
    assert(duplicateExpected.expect("color", *texture));
    assert(!duplicateExpected.expect("color", *texture));
    TextureReadbackBatch<2> rejected;
    assert(rejected.begin(120, 17));
    assert(rejected.expect("color", *texture) && rejected.seal());
    extension.accepted = false;
    assert(!rejected.schedule(extension, *commands, 0, *texture));
    assert(rejected.counts().scheduled == 0 && !rejected.finish());
    assert(rejected.diagnostic() == "injected scheduling failure");
    LegacyExtension legacy;
    TextureReadbackBatch<2> unsupported;
    assert(unsupported.begin(120, 17));
    assert(unsupported.expect("color", *texture) && unsupported.seal());
    assert(!unsupported.schedule(legacy, *commands, 0, *texture));
    arp::core::pmr::String diagnostic;
    assert(!legacy.scheduleTracked(*commands, *texture, "color", {1, 120, 17}, diagnostic));
    assert(diagnostic == "RHI-READBACK-TRACKED-MODE-UNSUPPORTED");
    // Independent consumers can observe the same source, label and frame.
    // A result from one batch must never satisfy the other batch.
    extension.accepted = true;
    TextureReadbackBatch<2> firstBatch, secondBatch;
    prepare(firstBatch);
    const auto firstSample = goodSample();
    prepare(secondBatch);
    const auto secondSample = goodSample();
    assert(!firstBatch.consume(secondSample));
    assert(!secondBatch.consume(firstSample));
    assert(firstBatch.diagnostic() == "RHI-READBACK-RESULT-UNEXPECTED");
    assert(secondBatch.diagnostic() == "RHI-READBACK-RESULT-UNEXPECTED");
    assert(firstBatch.counts().completed == 0u && secondBatch.counts().completed == 0u);
    TextureReadbackBatch<1> smallBatch;
    TextureReadbackBatch<3> largerBatch;
    prepare(smallBatch);
    prepare(largerBatch);
    assert(smallBatch.requestIdentity(0).requestId != largerBatch.requestIdentity(0).requestId);
    assert(!smallBatch.requestIdentity(1).requestId);
    assert(!smallBatch.consume(goodSample()));

    constexpr std::array<std::string_view, 3> labels{{"first", "middle", "last"}};
    for (std::uint32_t missingIndex = 0; missingIndex < labels.size(); ++missingIndex) {
        TextureReadbackBatch<3> manifest;
        assert(manifest.begin(120, 17));
        for (const auto label : labels)
            assert(manifest.declareExpected(label));
        for (std::uint32_t i = 0; i < labels.size(); ++i)
            if (i != missingIndex)
                assert(manifest.bindExpected(i, *texture));
        assert(!manifest.seal());
        assert(manifest.diagnostic() == "RHI-READBACK-EXPECTED-SOURCE-MISSING");
        assert(manifest.counts().expected == labels.size());
        assert(manifest.counts().scheduled == 0u && manifest.counts().completed == 0u);
        assert(!manifest.schedule(extension, *commands, 0u, *texture));
        assert(!manifest.finish());
        assert(manifest.diagnostic() == "RHI-READBACK-EXPECTED-SOURCE-MISSING");
    }
    for (std::uint32_t failedIndex = 0; failedIndex < labels.size(); ++failedIndex) {
        TextureReadbackBatch<3> manifest;
        assert(manifest.begin(120, 17));
        for (const auto label : labels)
            assert(manifest.declareExpected(label));
        for (std::uint32_t i = 0; i < labels.size(); ++i)
            assert(manifest.bindExpected(i, *texture));
        assert(manifest.seal());
        for (std::uint32_t i = 0; i <= failedIndex; ++i) {
            extension.accepted = i != failedIndex;
            assert(manifest.schedule(extension, *commands, i, *texture) == extension.accepted);
        }
        assert(manifest.counts().expected == labels.size());
        assert(manifest.counts().scheduled == failedIndex);
        assert(!manifest.finish());
        assert(manifest.diagnostic() == "injected scheduling failure");
    }
    TextureReadbackBatch<2> rebound;
    assert(rebound.begin(120, 17) && rebound.declareExpected("first"));
    assert(rebound.bindExpected(0u, *texture));
    const auto originalIdentity = rebound.requestIdentity(0u);
    assert(!rebound.bindExpected(0u, *texture));
    assert(rebound.diagnostic() == "RHI-READBACK-EXPECTED-SOURCE-INVALID");
    assert(rebound.counts().expected == 1u && rebound.requestIdentity(0u) == originalIdentity);
    TextureReadbackBatch<2> sealed;
    assert(sealed.begin(120, 17) && sealed.expect("color", *texture) && sealed.seal());
    assert(!sealed.declareExpected("late") && sealed.counts().expected == 1u);
    TextureReadbackBatch<2> undeclared;
    assert(undeclared.begin(120, 17));
    assert(!undeclared.bindExpected(0u, *texture));
    assert(undeclared.counts().expected == 0u && !undeclared.requestIdentity(0u).requestId);
}

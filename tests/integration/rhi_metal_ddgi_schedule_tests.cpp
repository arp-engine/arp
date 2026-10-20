#include "fixtures/recorded_resource_state_checks.h"
#include "renderer/gi/ddgi_renderer.h"
#include "rhi/backends/metal/metal_device.h"
#include "rhi/gpu_extensions.h"
#include "tools/render_feature_lab/postprocess_fixture.h"

#include <cassert>
#include <iostream>

int main()
{
    using namespace arp;
    rhi_metal::MetalRhiDeviceDesc desc;
    desc.capabilityContract = rhi::semantics::nativeBackendCapabilityContract(rhi::Backend::Metal);
    desc.capabilityContract.queriedFromNativeDevice = true;
    desc.capabilityContract.queriedExtendedFeatures = true;
    // Test envelope: three 2x2x2-probe renderers during lifecycle validation,
    // 32x32 scene attachments, 64x64 terrain, BVH and hierarchy uploads. The
    // 87,408-byte terrain hierarchy is the largest initial single upload.
    desc.memoryBudgetBytes = 64u * 1024u * 1024u;
    desc.uploadStagingPageSizeBytes = 128u * 1024u;
    desc.uploadStagingMaxPageCount = 8u;
    auto device = rhi_metal::createMetalRhiDevice(desc);
    assert(device);
    tests::checkRecordedResourceStates(*device);
    auto* readback = static_cast<rhi::TextureReadbackExtension*>(
        device->queryInterface(rhi::kExtensionTextureReadback));
    assert(readback);
    // The distance atlas is the largest attachment: 2 * 16 by 2 * 2 * 16,
    // RGBA16F. The fixture polls each result before scheduling the next one.
    constexpr auto atlasWidth = 2u * renderer::DdgiRenderer::kDistancePhysicalTexels;
    constexpr auto atlasHeight = 4u * renderer::DdgiRenderer::kDistancePhysicalTexels;
    const auto row = core::alignCpuAllocationOffsetUp(
        atlasWidth * rhi::bytesPerPixel(rhi::TextureFormat::Rgba16Float),
        rhi::kTextureUploadRowPitchAlignment);
    assert(row.success);
    core::pmr::String diagnostic;
    assert(readback->configureBudget({2u * row.alignedValue * atlasHeight, 1u}, diagnostic));
    const auto result =
        tools::render_feature_lab::runDdgiFeatureFixtureFrames(*device, {32u, 32u}, 3u);
    if (!result.success)
        std::cerr << result.diagnostic << '\n';
    assert(result.success && result.graphPassCount == 10u);
    assert(result.ddgiReadbackAvailable && result.ddgiReadbackFrames == 3u);
    assert(result.ddgiFiniteReadbackFrames == 3u);
    for (std::size_t frame = 0u; frame != result.ddgiFrameEvidence.size(); ++frame) {
        for (std::size_t attachment = 0u; attachment != 3u; ++attachment) {
            const auto& channels = result.ddgiFrameEvidence[frame].channels[attachment];
            for (std::size_t channel = 0u; channel != 3u; ++channel) {
                const auto& stats = channels.channels[channel];
                std::cout << "DDGI baseline frame=" << frame << " attachment=" << attachment
                          << " channel=" << channel << " min=" << stats.minValue
                          << " max=" << stats.maxValue << " mean=" << stats.meanValue << '\n';
                // The authored nonzero sky illuminates unoccluded probes; the
                // triangle/terrain yields finite hit distances below maxRay=8.
                // Resolve RGB is currently zero and has a separate domain gate.
                if (attachment == 1u)
                    assert(stats.maxValue > 0.0 && stats.meanValue > 0.0);
                if (attachment == 2u && channel == 0u)
                    assert(stats.minValue > 0.0 && stats.minValue < 8.0 && stats.maxValue <= 8.0
                        && stats.maxValue > stats.minValue);
            }
        }
    }
    unsigned lifecycleChecks = 0u;
    for (const auto& metric : result.attachmentMetrics) {
        if (metric.metric == "resize_recreate_reset" || metric.metric == "destroy_recreate_reset") {
            assert(metric.available && metric.value == 1.0f);
            ++lifecycleChecks;
        }
    }
    assert(lifecycleChecks == 2u);
    for (const auto fault : {tools::render_feature_lab::DdgiFixtureFault::AfterHistoryCopy,
             tools::render_feature_lab::DdgiFixtureFault::AfterResolve}) {
        std::cout << "DDGI retry fault=" << static_cast<int>(fault) << std::endl;
        const auto recovered = tools::render_feature_lab::runDdgiFeatureFixtureFrames(
            *device, {32u, 32u}, 3u, nullptr, fault);
        if (!recovered.success)
            std::cerr << recovered.diagnostic << '\n';
        assert(recovered.success && recovered.ddgiRejectedFrames == 1u);
        assert(result.ddgiFrameEvidence.size() == 3u && recovered.ddgiFrameEvidence.size() == 3u);
        for (std::size_t frame = 0u; frame != 3u; ++frame) {
            const auto& expected = result.ddgiFrameEvidence[frame];
            const auto& actual = recovered.ddgiFrameEvidence[frame];
            assert(actual.newlyExposedProbes == expected.newlyExposedProbes);
            assert(actual.readSlot == expected.readSlot);
            for (std::size_t attachment = 0u; attachment != 3u; ++attachment) {
                assert(actual.hashes[attachment] == expected.hashes[attachment]);
                const auto& expectedChannels = expected.channels[attachment];
                const auto& actualChannels = actual.channels[attachment];
                assert(expectedChannels.valid && actualChannels.valid);
                assert(expectedChannels.channelCount == 4u && actualChannels.channelCount == 4u);
                for (std::size_t channel = 0u; channel != 4u; ++channel) {
                    const auto& a = actualChannels.channels[channel];
                    const auto& e = expectedChannels.channels[channel];
                    assert(a.sampleCount > 0u && a.sampleCount == e.sampleCount);
                    assert(a.finiteCount == a.sampleCount && e.finiteCount == e.sampleCount);
                    assert(a.minValue == e.minValue && a.maxValue == e.maxValue);
                    assert(a.meanValue == e.meanValue && a.centerValue == e.centerValue);
                }
            }
        }
    }
    device->waitIdle();
    std::cout << "DDGI real Metal frames=" << result.ddgiReadbackFrames
              << " submissions=" << result.submittedFrameCount
              << " liveBytes=" << device->memoryStats().allocatedBytes << '\n';
    assert(device->memoryStats().allocatedBytes == 0u);
}

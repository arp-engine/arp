#include "renderer/render_graph/compiled_frame_graph.h"
#include "renderer/render_graph/frame_graph_resource_pool.h"
#include "renderer/render_graph/render_graph.h"
#include "tests/fixtures/capability_override_device.h"
#include "tests/fixtures/vulkan_heap_owner_fixture.h"

#include <cassert>
#include <cstdio>

int main()
{
    using namespace arp;
    tests::VulkanHeadlessRuntime native;
    if (!native.runtime() || !native.runtime()->valid())
        return 77;
    auto device = tests::VulkanHeapOwnerFixture::makeDevice(native.runtime(), false);
    assert(device && !device->capabilities().features.transientAliasing);
    tests::CapabilityOverrideDevice observed(*device, nullptr);
    observed.failTexturePlacementQuery(true);
    render_graph::FrameGraph graph(observed);
    rhi::TextureDesc desc;
    desc.extent = {8u, 4u, 1u};
    desc.format = rhi::TextureFormat::Rgba8Unorm;
    desc.usage = rhi::TextureUsage::RenderAttachment | rhi::TextureUsage::Sampled;
    for (unsigned frame = 0u; frame < 3u; ++frame) {
        const auto texture = graph.createTransientTexture(desc);
        graph.addPass("OrdinaryNativeImageBarrier")
            .write(texture, rhi::ResourceState::ColorAttachment)
            .execute([](render_graph::RenderContext&) {});
        const auto compiled = graph.compile();
        assert(compiled.aliasPlan().empty());
        assert(compiled.transientPeakBytes() == 8u * 4u * 4u);
        assert(compiled.transientPhysicalPeakBytes() == 0u);
        graph.execute(compiled);
        const auto payload = rhi_vulkan::vulkanRhiNativePayload(graph.texture(texture));
        assert(payload && payload->backendVulkanDeviceHandle() == native.device());
        assert(payload->backendVulkanImageHandle());
        graph.reset();
        // Explicit cold test completion, never a frame-loop implementation.
        device->waitIdle();
        graph.resourcePool().recycleCompleted();
    }
    assert(observed.texturePlacementQueryCount() == 0u);
    assert(device->queue().submitCount() == 3u);
    const auto stats = graph.resourcePool().report();
    assert(stats.cachedBytes == 8u * 4u * 4u && stats.pendingBytes == 0u);
    assert(stats.hitCount == 2u && stats.missCount == 1u);
    assert(stats.nativePhysicalObservation.state
        == rhi::semantics::PhysicalBudgetObservationState::Unknown);
    std::puts(
        "Vulkan ordinary cache: real image/barrier/submit/completion/reuse; placement queries=0");
}

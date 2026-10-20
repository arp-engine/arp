#include "fixtures/capability_override_device.h"
#include "renderer/render_graph/compiled_frame_graph.h"
#include "renderer/render_graph/render_graph.h"
#include "renderer/render_graph/transient_resource_allocator.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/gpu_common.h"

#include <cassert>
#include <stdexcept>

namespace {

void enableAliasing(arp::rhi::DeviceCapabilities& caps)
{
    caps.memory.transientAliasing = true;
    caps.features.transientAliasing = true;
}

arp::rhi::TextureDesc transientTextureDesc(std::pmr::string label,
    arp::rhi::Extent3D extent = {128, 128, 1},
    arp::rhi::TextureFormat format = arp::rhi::TextureFormat::Rgba16Float)
{
    return arp::rhi::TextureDesc{
        std::move(label),
        extent,
        format,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
        1,
    };
}

/// Compute size info for a texture desc, matching the Null backend's
/// estimateTextureSize logic.
arp::render_graph::TransientTextureSizeInfo computeSizeInfo(const arp::rhi::TextureDesc& desc)
{
    arp::render_graph::TransientTextureSizeInfo info;
    info.handleId = 0; // filled by caller
    const auto bpp = arp::rhi::bytesPerPixel(desc.format);
    info.sizeBytes = static_cast<std::uint64_t>(desc.extent.width)
        * static_cast<std::uint64_t>(desc.extent.height)
        * static_cast<std::uint64_t>(desc.extent.depth) * bpp;
    info.alignmentBytes = std::max<std::uint64_t>(256, bpp);
    return info;
}

// ---------------------------------------------------------------------------
// Original aliasing test (updated for new report fields)
// ---------------------------------------------------------------------------

void test_basic_aliasing_report()
{
    using namespace arp::render_graph;

    auto device = arp::rhi::createNullDevice();
    arp::tests::CapabilityOverrideDevice aliasDevice(*device, enableAliasing);
    FrameGraph graph(aliasDevice);

    const auto transientDesc = transientTextureDesc("TransientA");
    auto a = graph.createTransientTexture(transientDesc);
    auto descB = transientDesc;
    descB.label = "TransientA_B";
    auto descC = transientDesc;
    descC.label = "TransientA_C";
    auto b = graph.createTransientTexture(descB);
    auto c = graph.createTransientTexture(descC);

    graph.addPass("WriteA")
        .write(a, arp::rhi::ResourceState::ColorAttachment)
        .execute([](RenderContext&) {});
    graph.addPass("WriteB")
        .write(b, arp::rhi::ResourceState::ColorAttachment)
        .execute([](RenderContext&) {});
    graph.addPass("ReadAB")
        .read(a, arp::rhi::ResourceState::ShaderRead)
        .read(b, arp::rhi::ResourceState::ShaderRead)
        .execute([](RenderContext&) {});
    graph.addPass("WriteC")
        .write(c, arp::rhi::ResourceState::ColorAttachment)
        .execute([](RenderContext&) {});

    const auto compiled = graph.compile();
    FrameGraphTransientResourceAllocator allocator;
    const auto report = allocator.allocate(compiled);
    assert(report.success);
    assert(report.transientTextureCount == 3u);
    assert(report.physicalTextureAllocationCount == 2u);
    assert(report.aliasReuseCount == 1u);
    assert(report.backendFallbacksHiddenFromUpperLayer);
    assert(!report.nativeHandleExposedToUpperLayer);
    assert(report.assignments.size() == 3u);
    assert(report.assignments[0].aliasGroup == report.assignments[2].aliasGroup);
    assert(report.assignments[0].physicalAllocationIndex
        == report.assignments[2].physicalAllocationIndex);
    assert(report.assignments[0].aliasGroup != report.assignments[1].aliasGroup);
    assert(compiled.aliasHandoffs().size() == 1u);
    assert(compiled.aliasHandoffs()[0].before.id == a.id);
    assert(compiled.aliasHandoffs()[0].after.id == c.id);

    // Phase 3: physical allocations are present.
    assert(report.physicalAllocations.size() == 2u);

    CompiledFrameGraph empty;
    const auto emptyReport = allocator.allocate(empty);
    assert(emptyReport.success);
    assert(emptyReport.transientTextureCount == 0u);
    assert(emptyReport.physicalTextureAllocationCount == 0u);
    assert(emptyReport.assignments.empty());
    assert(emptyReport.physicalAllocations.empty());
}

// ---------------------------------------------------------------------------
// Phase 3 tests
// ---------------------------------------------------------------------------

void test_transient_not_eagerly_allocated()
{
    using namespace arp::render_graph;

    auto device = arp::rhi::createNullDevice();
    FrameGraph graph(*device);

    auto tex = graph.createTransientTexture(transientTextureDesc("DeferredTex"));
    graph.addPass("WriteTex")
        .write(tex, arp::rhi::ResourceState::ColorAttachment)
        .execute([](RenderContext&) {});

    // After createTransientTexture + before compile/execute, accessing the
    // texture should fail because allocation is deferred.
    bool threw = false;
    try {
        (void)graph.texture(tex);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);

    // After compile + execute, the texture is allocated from the pool.
    graph.execute();
    assert(graph.texture(tex).desc().label == std::pmr::string("DeferredTex"));
}

void test_alias_group_shares_physical_allocation()
{
    using namespace arp::render_graph;

    auto device = arp::rhi::createNullDevice();
    arp::tests::CapabilityOverrideDevice aliasDevice(*device, enableAliasing);
    FrameGraph graph(aliasDevice);

    auto a = graph.createTransientTexture(transientTextureDesc("SharedA"));
    auto b = graph.createTransientTexture(transientTextureDesc("SharedB"));

    // A and B have non-overlapping lifetimes -> same alias group.
    graph.addPass("WriteA")
        .write(a, arp::rhi::ResourceState::ColorAttachment)
        .execute([](RenderContext&) {});
    graph.addPass("ReadA").read(a, arp::rhi::ResourceState::ShaderRead).execute([](RenderContext&) {
    });
    graph.addPass("WriteB")
        .write(b, arp::rhi::ResourceState::ColorAttachment)
        .execute([](RenderContext&) {});
    graph.addPass("ReadB").read(b, arp::rhi::ResourceState::ShaderRead).execute([](RenderContext&) {
    });

    const auto compiled = graph.compile();

    // Build size info for the allocator.
    std::pmr::vector<TransientTextureSizeInfo> sizeInfos;
    {
        auto info = computeSizeInfo(transientTextureDesc("SharedA"));
        info.handleId = a.id;
        sizeInfos.push_back(info);
    }
    {
        auto info = computeSizeInfo(transientTextureDesc("SharedB"));
        info.handleId = b.id;
        sizeInfos.push_back(info);
    }

    FrameGraphTransientResourceAllocator allocator;
    const auto report = allocator.allocate(compiled, sizeInfos);
    assert(report.success);
    assert(report.transientTextureCount == 2u);
    // A and B share one alias group -> one physical allocation.
    assert(report.physicalTextureAllocationCount == 1u);
    assert(report.physicalAllocations.size() == 1u);
    // The physical allocation has 2 members (both textures).
    assert(report.physicalAllocations[0].members.size() == 2u);
    // Both members share offset 0 (aliased into the same backing memory).
    assert(report.physicalAllocations[0].members[0].offsetBytes == 0u);
    assert(report.physicalAllocations[0].members[1].offsetBytes == 0u);
    // Size is the max of member sizes (they're equal here).
    assert(report.physicalAllocations[0].sizeBytes > 0u);
    assert(report.physicalAllocations[0].alignmentBytes > 0u);
}

void test_pool_reuses_across_frames()
{
    using namespace arp::render_graph;

    auto device = arp::rhi::createNullDevice();
    FrameGraph graph(*device);

    const auto desc = transientTextureDesc("PooledTex", {64, 64, 1});

    // Frame 1: Null reports aliasing=false, so the bounded pool is used.
    auto tex1 = graph.createTransientTexture(desc);
    graph.addPass("Frame1Write")
        .write(tex1, arp::rhi::ResourceState::ColorAttachment)
        .execute([](RenderContext&) {});
    graph.execute();
    assert(graph.executionTrace().size() == 1u);
    assert(graph.resourcePool().missCount() == 1u);
    assert(graph.resourcePool().hitCount() == 0u);
    graph.reset();

    // Frame 2: Null completion is immediate; reset queued the resource and
    // the next acquire polls completion, normalizes it, then reuses it.
    auto tex2 = graph.createTransientTexture(desc);
    graph.addPass("Frame2Write")
        .write(tex2, arp::rhi::ResourceState::ColorAttachment)
        .execute([](RenderContext&) {});
    graph.execute();
    assert(graph.executionTrace().size() == 1u);
    assert(graph.resourcePool().missCount() == 1u);
    assert(graph.resourcePool().hitCount() == 1u);
    graph.reset();

    // Frame 3: works again.
    auto tex3 = graph.createTransientTexture(desc);
    graph.addPass("Frame3Write")
        .write(tex3, arp::rhi::ResourceState::ColorAttachment)
        .execute([](RenderContext&) {});
    graph.execute();
    assert(graph.executionTrace().size() == 1u);
    assert(graph.resourcePool().missCount() == 1u);
    assert(graph.resourcePool().hitCount() == 2u);

    // Direct pool test: verify the FrameGraphResourcePool still caches
    // textures independently (not through transient path).
    {
        FrameGraphResourcePool pool(*device);
        const auto poolDesc = transientTextureDesc("DirectPool", {32, 32, 1});
        auto t1 = pool.acquireTexture(poolDesc);
        assert(t1 != nullptr);
        assert(pool.missCount() == 1u);
        assert(pool.hitCount() == 0u);

        pool.releaseTexture(std::move(t1));
        assert(pool.poolSize() == 1u);

        auto t2 = pool.acquireTexture(poolDesc);
        assert(t2 != nullptr);
        assert(pool.missCount() == 1u);
        assert(pool.hitCount() == 1u);
    }
}

void test_allocator_produces_size_alignment()
{
    using namespace arp::render_graph;

    auto device = arp::rhi::createNullDevice();
    arp::tests::CapabilityOverrideDevice aliasDevice(*device, enableAliasing);
    FrameGraph graph(aliasDevice);

    const auto desc = transientTextureDesc("SizeTex", {256, 128, 1});
    auto tex = graph.createTransientTexture(desc);
    graph.addPass("WriteSizeTex")
        .write(tex, arp::rhi::ResourceState::ColorAttachment)
        .execute([](RenderContext&) {});

    const auto compiled = graph.compile();

    std::pmr::vector<TransientTextureSizeInfo> sizeInfos;
    auto info = computeSizeInfo(desc);
    info.handleId = tex.id;
    sizeInfos.push_back(info);

    FrameGraphTransientResourceAllocator allocator;
    const auto report = allocator.allocate(compiled, sizeInfos);
    assert(report.success);
    assert(report.physicalAllocations.size() == 1u);

    const auto& alloc = report.physicalAllocations[0];
    // 256 * 128 * 1 * 8 (Rgba16Float) = 262144 bytes.
    assert(alloc.sizeBytes == 262144u);
    assert(alloc.alignmentBytes >= 256u);
    assert(alloc.members.size() == 1u);
    assert(alloc.members[0].memberSizeBytes == 262144u);
    assert(alloc.members[0].offsetBytes == 0u);

    CompiledFrameGraph alignedCompiled;
    alignedCompiled.addAlias({"AlignmentDominatesSize", {77u}, 1u});
    alignedCompiled.addResourceLifetime({
        "AlignmentDominatesSize",
        ResourceKind::Texture,
        77u,
        0u,
        0u,
        "WriteAlignmentDominatesSize",
        "WriteAlignmentDominatesSize",
        "WriteAlignmentDominatesSize",
        {},
    });
    std::pmr::vector<TransientTextureSizeInfo> alignmentInfos;
    alignmentInfos.push_back({77u, 256u, 65536u});
    const auto missingBudgetReport = allocator.allocate(alignedCompiled, alignmentInfos);
    assert(!missingBudgetReport.success);
    assert(missingBudgetReport.diagnostics == "RG-ALIAS-PHYSICAL-PEAK-EXCEEDS-COMPILED-BUDGET");
    alignedCompiled.setTransientResourceBudget(256u, 1u, 65535u);
    const auto insufficientBudgetReport = allocator.allocate(alignedCompiled, alignmentInfos);
    assert(!insufficientBudgetReport.success);
    assert(
        insufficientBudgetReport.diagnostics == "RG-ALIAS-PHYSICAL-PEAK-EXCEEDS-COMPILED-BUDGET");
    alignedCompiled.setTransientResourceBudget(256u, 1u, 65536u);
    const auto alignedReport = allocator.allocate(alignedCompiled, alignmentInfos);
    assert(alignedReport.success);
    assert(alignedReport.physicalAllocations[0].sizeBytes == 65536u);
}

void test_null_alias_capability_degrades()
{
    using namespace arp::render_graph;

    auto device = arp::rhi::createNullDevice();
    FrameGraph graph(*device);

    auto a = graph.createTransientTexture(transientTextureDesc("NullAliasA"));
    auto b = graph.createTransientTexture(transientTextureDesc("NullAliasB"));

    graph.addPass("WriteNullA")
        .write(a, arp::rhi::ResourceState::ColorAttachment)
        .execute([](RenderContext&) {});
    graph.addPass("ReadNullA")
        .read(a, arp::rhi::ResourceState::ShaderRead)
        .execute([](RenderContext&) {});
    graph.addPass("WriteNullB")
        .write(b, arp::rhi::ResourceState::ColorAttachment)
        .execute([](RenderContext&) {});
    graph.addPass("ReadNullB")
        .read(b, arp::rhi::ResourceState::ShaderRead)
        .execute([](RenderContext&) {});

    const auto compiled = graph.compile();
    FrameGraphTransientResourceAllocator allocator;
    assert(!device->capabilities().features.transientAliasing);
    assert(compiled.aliasPlan().empty());
    // Default capability: aliasedMemory=false and there is no fake alias plan.
    const auto report = allocator.allocate(compiled);
    assert(report.success);
    assert(!report.aliasedMemory);

    // Execution takes the ordinary bounded pool path.
    graph.execute();
    assert(graph.executionTrace().size() == 4u);
}

void test_cross_queue_resources_never_alias()
{
    using namespace arp::render_graph;
    auto nullDevice = arp::rhi::createNullDevice();
    arp::tests::CapabilityOverrideDevice aliasDevice(*nullDevice, enableAliasing);
    FrameGraph graph(aliasDevice);
    const auto a = graph.createTransientTexture(transientTextureDesc("QueueA"));
    const auto b = graph.createTransientTexture(transientTextureDesc("QueueB"));
    graph.addPass("GraphicsOwner")
        .queue(QueueRole::Graphics)
        .write(a, arp::rhi::ResourceState::ColorAttachment)
        .execute([](RenderContext&) {});
    graph.addPass("ComputeOwner")
        .queue(QueueRole::Compute)
        .write(b, arp::rhi::ResourceState::ShaderWrite)
        .execute([](RenderContext&) {});

    const auto compiled = graph.compile();
    assert(compiled.aliasPlan().size() == 2u);
    assert(compiled.aliasPlan()[0].aliasGroup != compiled.aliasPlan()[1].aliasGroup);
    assert(compiled.aliasHandoffs().empty());
}

void test_texture_and_buffer_handle_ids_have_distinct_lifetimes()
{
    using namespace arp::render_graph;
    auto nullDevice = arp::rhi::createNullDevice();
    arp::tests::CapabilityOverrideDevice aliasDevice(*nullDevice, enableAliasing);
    FrameGraph graph(aliasDevice);

    const auto texture = graph.createTransientTexture(transientTextureDesc("TextureHandleOne"));
    const auto buffer = graph.createTransientBuffer({
        "BufferHandleOne",
        256u,
        arp::rhi::ResourceUsage::StorageBuffer,
        arp::rhi::MemoryUsage::Transient,
        arp::rhi::ResourceState::Undefined,
    });
    assert(texture.id == buffer.id);

    graph.addPass("WriteTexture")
        .write(texture, arp::rhi::ResourceState::ColorAttachment)
        .execute([](RenderContext&) {});
    graph.addPass("WriteBuffer")
        .write(buffer, arp::rhi::ResourceState::ShaderWrite)
        .execute([](RenderContext&) {});

    const auto compiled = graph.compile();
    FrameGraphTransientResourceAllocator allocator;
    const auto report = allocator.allocate(compiled);
    assert(report.success);
    assert(report.lifetimeProofValidated);
}

void test_graph_buffer_lifetime_preserves_explicit_memory_intent()
{
    auto device = arp::rhi::createNullDevice();
    arp::render_graph::FrameGraph graph(*device);
    for (const auto memory : {arp::rhi::MemoryUsage::GpuOnly,
             arp::rhi::MemoryUsage::CpuToGpu,
             arp::rhi::MemoryUsage::GpuToCpu,
             arp::rhi::MemoryUsage::Transient}) {
        const auto handle = graph.createTransientBuffer({"GraphBufferLifetime",
            128u,
            arp::rhi::ResourceUsage::StorageBuffer | arp::rhi::ResourceUsage::CopyDst,
            memory,
            arp::rhi::ResourceState::Undefined});
        assert(graph.isTransient(handle));
        const auto& desc = graph.buffer(handle).desc();
        assert(desc.memory == memory);
        assert(desc.sizeBytes == 128u);
        assert(desc.initialState == arp::rhi::ResourceState::Undefined);
        assert(desc.usage
            == (arp::rhi::ResourceUsage::StorageBuffer | arp::rhi::ResourceUsage::CopyDst));
    }
    graph.reset();
}

void test_missing_placement_requirements_rejects_compile()
{
    for (const auto mutator :
        {arp::tests::CapabilityOverrideDevice::Mutator{nullptr}, enableAliasing}) {
        auto device = arp::rhi::createNullDevice();
        arp::tests::CapabilityOverrideDevice observed(*device, mutator);
        observed.failTexturePlacementQuery(true);
        arp::render_graph::FrameGraph graph(observed);
        const auto texture =
            graph.createTransientTexture(transientTextureDesc("MissingPlacementRequirements"));
        graph.addPass("PlacementMetadataRequired")
            .write(texture, arp::rhi::ResourceState::ColorAttachment)
            .execute([](arp::render_graph::RenderContext&) {});
        bool rejected = false;
        try {
            const auto compiled = graph.compile();
            (void)compiled;
        } catch (const std::exception& error) {
            rejected =
                std::string_view(error.what()) == "RG-TEXTURE-PLACEMENT-REQUIREMENTS-UNAVAILABLE";
        }
        // Ordinary logical cache accounting must not depend on an alias query.
        assert(rejected == (mutator != nullptr));
        assert(observed.texturePlacementQueryCount() == (mutator ? 1u : 0u));
        assert(device->memoryStats().allocatedBytes == 0u);
    }
}

void test_logical_cache_and_alias_placement_budgets_are_separate()
{
    using namespace arp;
    auto device = rhi::createNullDevice();
    tests::CapabilityOverrideDevice observed(*device, enableAliasing);
    render_graph::FrameGraph graph(observed);
    const auto desc =
        transientTextureDesc("LogicalAndPlacement", {3u, 2u, 1u}, rhi::TextureFormat::Rgba8Unorm);
    const auto first = graph.createTransientTexture(desc);
    const auto second = graph.createTransientTexture(desc);
    graph.addPass("First")
        .write(first, rhi::ResourceState::ColorAttachment)
        .execute([](render_graph::RenderContext&) {});
    graph.addPass("Second")
        .write(second, rhi::ResourceState::ColorAttachment)
        .execute([](render_graph::RenderContext&) {});
    const auto compiled = graph.compile();
    assert(compiled.aliasPlan().size() == 2u);
    assert(compiled.transientPeakBytes() == 2u * 3u * 2u * 4u);
    const auto placement = device->queryTexturePlacementRequirements(desc);
    std::uint64_t aligned = 0u;
    assert(core::roundUpUnsigned(placement.sizeBytes, placement.alignmentBytes, aligned));
    assert(compiled.transientPhysicalPeakBytes() == aligned);
    assert(compiled.transientPhysicalPeakBytes() != compiled.transientPeakBytes());
    assert(observed.texturePlacementQueryCount() == 2u);
}

void test_ordinary_logical_budget_overflow_rejects_before_allocation()
{
    using namespace arp;
    auto device = rhi::createNullDevice();
    tests::CapabilityOverrideDevice observed(*device, nullptr);
    render_graph::FrameGraph graph(observed);
    // Four exact R8 logical footprints of 2^62 bytes overflow uint64_t.
    const auto desc = transientTextureDesc(
        "AggregateOverflow", {1u << 31u, 1u << 31u, 1u}, rhi::TextureFormat::R8Unorm);
    for (unsigned index = 0u; index < 4u; ++index) {
        const auto texture = graph.createTransientTexture(desc);
        graph.addPass("Overflow")
            .write(texture, rhi::ResourceState::ColorAttachment)
            .execute([](render_graph::RenderContext&) {});
    }
    bool rejected = false;
    try {
        (void)graph.compile();
    } catch (const std::exception& error) {
        rejected = std::string_view(error.what()) == "RHI-CACHE-ACCOUNTING-BYTE-OVERFLOW";
    }
    assert(rejected && device->memoryStats().allocatedBytes == 0u);
    assert(observed.texturePlacementQueryCount() == 0u);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--placement-query") {
        test_missing_placement_requirements_rejects_compile();
        return 0;
    }
    test_missing_placement_requirements_rejects_compile();
    test_logical_cache_and_alias_placement_budgets_are_separate();
    test_ordinary_logical_budget_overflow_rejects_before_allocation();
    test_graph_buffer_lifetime_preserves_explicit_memory_intent();
    test_basic_aliasing_report();
    test_transient_not_eagerly_allocated();
    test_alias_group_shares_physical_allocation();
    test_pool_reuses_across_frames();
    test_allocator_produces_size_alignment();
    test_null_alias_capability_degrades();
    test_cross_queue_resources_never_alias();
    test_texture_and_buffer_handle_ids_have_distinct_lifetimes();
    return 0;
}

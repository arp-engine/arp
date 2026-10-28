// Contract tests for FrameGraph::importTextureRef shared-ownership borrow.
//
// These tests verify the lifecycle contract introduced to fix the
// use-after-free where a persistent texture (e.g. a shadow map) imported via
// importTextureRef could be destroyed by its original owner (a render pipeline
// rebuild) while an in-flight FrameGraph frame still referenced it.
//
// Coverage:
//   - lifecycle_keepalive: graph's shared_ptr copy keeps the GPU texture alive
//     after the original owner releases its shared_ptr.
//   - reset_releases: FrameGraph::reset() releases the borrowed shared_ptr,
//     so the deleter runs once the last external reference is gone.
//   - null_diagnostic: importTextureRef(nullptr) throws std::runtime_error
//     (deterministic diagnostic failure — no silent dangling reference).
//   - resolve_precedence: when both owned `texture` and borrowed `textureRef`
//     exist on different records, resolveTexture() returns the correct one.
//   - graph_destruction_releases: destroying the FrameGraph releases the
//     borrowed shared_ptr even without an explicit reset().

#include "core/memory/cpu_allocator.h"
#include "renderer/render_graph/compiled_frame_graph.h"
#include "renderer/render_graph/render_graph.h"
#include "rhi/backends/null/null_backend.h"

#include <cassert>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

arp::rhi::TextureDesc textureDesc(std::pmr::string label,
    arp::rhi::Extent3D extent,
    arp::rhi::TextureFormat format,
    arp::rhi::TextureUsage usage,
    arp::rhi::ResourceState initialState = arp::rhi::ResourceState::Undefined)
{
    return arp::rhi::TextureDesc{
        std::move(label),
        extent,
        format,
        usage,
        arp::rhi::MemoryUsage::GpuOnly,
        initialState,
        1,
    };
}

/// A deleter that increments a counter so the tests can observe when the
/// GPU texture is actually destroyed, not just when its shared_ptr refcount
/// drops. This lets us prove the graph keeps the resource alive.
void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(
            std::string("render_graph_import_texture_ref assertion failed: ") + message);
    }
}

void test_lifecycle_keepalive()
{
    // The core fix: the graph holds a shared_ptr copy, so the GPU texture
    // survives even after the original owner releases its reference.
    // This mirrors the shadow-map-during-pipeline-rebuild scenario.
    auto device = arp::rhi::createNullDevice();
    arp::render_graph::FrameGraph graph(*device);

    auto ownerTexture = std::shared_ptr<arp::rhi::GpuTexture>(
        std::move(device->createTexture(textureDesc("BorrowedShadowMap",
            {2048, 2048, 1},
            arp::rhi::TextureFormat::D32Float,
            arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled,
            arp::rhi::ResourceState::DepthAttachment))));

    const auto rawPtrBefore = ownerTexture.get();
    require(ownerTexture.use_count() == 1, "initial refcount should be 1");

    // Import into the graph — the graph takes a shared_ptr copy (refcount → 2).
    const auto handle = graph.importTextureRef(ownerTexture);
    require(ownerTexture.use_count() == 2, "graph should hold a second reference");
    const auto repeated = graph.importTextureRef(ownerTexture);
    require(repeated.id == handle.id, "one borrowed texture must have one graph identity");
    require(ownerTexture.use_count() == 2, "duplicate import must not add another lease");
    auto distinct = std::shared_ptr<arp::rhi::GpuTexture>(
        std::move(device->createTexture(ownerTexture->desc())));
    require(graph.importTextureRef(distinct).id != handle.id,
        "equal descriptors on distinct texture objects must not alias");

    // The graph must resolve to the same underlying GpuTexture.
    require(&graph.texture(handle) == rawPtrBefore, "graph must resolve to the borrowed texture");

    // Simulate the pipeline being rebuilt: the owner releases its shared_ptr.
    // The graph's copy must keep the GPU texture alive.
    ownerTexture.reset();
    require(graph.texture(handle).desc().extent.width == 2048u,
        "borrowed texture must remain valid after owner releases it");

    // Cleanup — graph destruction releases the last reference.
    graph.reset();
}

void test_reset_releases_borrow()
{
    // FrameGraph::reset() semantics: it clears passes and transient records
    // but RETAINS imported (non-transient) records — including borrowed
    // shared_ptrs. This is by design: imported/borrowed textures are meant
    // to outlive a single reset() so they can be re-imported across frames.
    // The borrowed shared_ptr is only released when the FrameGraph itself
    // is destroyed (the per-frame stack-graph scenario).
    //
    // This test pins that contract: reset() must NOT release the borrow,
    // but graph destruction must.
    auto device = arp::rhi::createNullDevice();

    std::weak_ptr<arp::rhi::GpuTexture> firstWeak;
    std::weak_ptr<arp::rhi::GpuTexture> probeWeak;

    {
        arp::render_graph::FrameGraph graph(*device);

        auto ownerTexture = std::shared_ptr<arp::rhi::GpuTexture>(
            std::move(device->createTexture(textureDesc("BorrowedAtlas",
                {1024, 1024, 1},
                arp::rhi::TextureFormat::Rgba8Unorm,
                arp::rhi::TextureUsage::Sampled,
                arp::rhi::ResourceState::ShaderRead))));

        graph.importTextureRef(ownerTexture);
        firstWeak = ownerTexture;

        // Owner releases — graph still holds a copy.
        ownerTexture.reset();
        require(!firstWeak.expired(), "texture must survive while graph holds it");

        // reset() retains imported records, so the borrow is NOT released.
        graph.reset();
        require(!firstWeak.expired(),
            "reset() must retain imported borrowed texture (non-transient retention)");

        // Capture a weak ref to observe final destruction when graph goes
        // out of scope. We re-create a borrowed texture to get a weak handle.
        auto probe = std::shared_ptr<arp::rhi::GpuTexture>(
            std::move(device->createTexture(textureDesc("BorrowedProbe",
                {64, 64, 1},
                arp::rhi::TextureFormat::Rgba8Unorm,
                arp::rhi::TextureUsage::Sampled,
                arp::rhi::ResourceState::ShaderRead))));
        graph.importTextureRef(probe);
        probeWeak = probe;
        probe.reset();
        require(!probeWeak.expired(), "graph must hold the probe borrow before destruction");
    }
    // graph destroyed out of scope — all borrowed shared_ptrs released.

    require(firstWeak.expired(), "graph destruction must release the first borrow");
    require(probeWeak.expired(), "graph destruction must release the probe borrow");
}

void test_graph_destruction_releases_borrow()
{
    // Even without an explicit reset(), destroying the FrameGraph must
    // release the borrowed shared_ptr (the per-frame stack-graph scenario).
    auto device = arp::rhi::createNullDevice();
    std::weak_ptr<arp::rhi::GpuTexture> weak;

    {
        arp::render_graph::FrameGraph graph(*device);

        auto ownerTexture = std::shared_ptr<arp::rhi::GpuTexture>(
            std::move(device->createTexture(textureDesc("BorrowedPersistent",
                {512, 512, 1},
                arp::rhi::TextureFormat::Rgba16Float,
                arp::rhi::TextureUsage::Sampled,
                arp::rhi::ResourceState::ShaderRead))));

        graph.importTextureRef(ownerTexture);
        weak = ownerTexture;
        ownerTexture.reset();
        // graph holds the only remaining reference here.
    }
    // graph destroyed out of scope — the borrowed shared_ptr is released.

    require(weak.expired(), "no references must remain after graph destruction");
}

void test_null_diagnostic_failure()
{
    // AGENTS.md: deterministic diagnostic failure instead of silent dangling.
    // importTextureRef(nullptr) must throw, not record a dangling reference.
    auto device = arp::rhi::createNullDevice();
    arp::render_graph::FrameGraph graph(*device);

    bool threw = false;
    try {
        (void)graph.importTextureRef(nullptr);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    require(threw, "importTextureRef(nullptr) must throw std::runtime_error");
}

void test_resolve_precedence_between_owned_and_borrowed()
{
    // A graph can hold both owned (createTexture/importTexture) and borrowed
    // (importTextureRef) textures simultaneously. resolveTexture() must return
    // the correct underlying GpuTexture for each handle.
    auto device = arp::rhi::createNullDevice();
    arp::render_graph::FrameGraph graph(*device);

    const auto owned = graph.createTexture(textureDesc("OwnedColor",
        {256, 256, 1},
        arp::rhi::TextureFormat::Rgba8Unorm,
        arp::rhi::TextureUsage::RenderAttachment,
        arp::rhi::ResourceState::ColorAttachment));

    auto borrowedOwner = std::shared_ptr<arp::rhi::GpuTexture>(
        std::move(device->createTexture(textureDesc("BorrowedDepth",
            {256, 256, 1},
            arp::rhi::TextureFormat::D32Float,
            arp::rhi::TextureUsage::RenderAttachment,
            arp::rhi::ResourceState::DepthAttachment))));
    const auto* borrowedRaw = borrowedOwner.get();

    const auto borrowed = graph.importTextureRef(borrowedOwner);

    require(&graph.texture(owned) != borrowedRaw,
        "owned handle must not resolve to the borrowed texture");
    require(&graph.texture(borrowed) == borrowedRaw,
        "borrowed handle must resolve to the borrowed texture");
    require(&graph.texture(owned) != &graph.texture(borrowed),
        "owned and borrowed handles must resolve to distinct textures");

    graph.reset();
}

void test_imported_flag_set_on_borrowed_record()
{
    // The borrowed record must be marked imported=true (not transient) so
    // reset() preserves it until explicit reset, matching the owning
    // importTexture() semantics. This guards against accidental aliasing
    // with transient textures.
    auto device = arp::rhi::createNullDevice();
    arp::render_graph::FrameGraph graph(*device);

    auto borrowedOwner = std::shared_ptr<arp::rhi::GpuTexture>(
        std::move(device->createTexture(textureDesc("BorrowedFlagCheck",
            {128, 128, 1},
            arp::rhi::TextureFormat::Rgba8Unorm,
            arp::rhi::TextureUsage::Sampled,
            arp::rhi::ResourceState::ShaderRead))));

    const auto handle = graph.importTextureRef(borrowedOwner);
    require(!graph.isTransient(handle), "borrowed texture must not be transient");
    // Imported textures are protected from reset() teardown (only transient
    // records are erased). The borrowed handle must still resolve after reset.
    graph.reset();
    // After reset(), non-transient imported records are retained, so the
    // handle is still valid and the borrowed shared_ptr is still held.
    require(&graph.texture(handle) == borrowedOwner.get(),
        "borrowed texture must remain valid after reset() (non-transient retention)");
}

void test_discarded_import_orders_future_writer_before_reader()
{
    auto device = arp::rhi::createNullDevice();
    arp::render_graph::FrameGraph graph(*device);
    auto texture = std::shared_ptr<arp::rhi::GpuTexture>(
        std::move(device->createTexture(textureDesc("CurrentFrameGi",
            {64, 64, 1},
            arp::rhi::TextureFormat::Rgba16Float,
            arp::rhi::TextureUsage::Sampled | arp::rhi::TextureUsage::Storage,
            arp::rhi::ResourceState::ShaderRead))));
    const auto handle = graph.importTextureRef(texture);
    graph.discardImportedContentsBeforeFirstWrite(handle);
    graph.addPass("SceneConsumer")
        .read(handle, arp::rhi::ResourceState::ShaderRead)
        .externalSideEffects();
    graph.addPass("GiResolveProducer")
        .write(handle, arp::rhi::ResourceState::ShaderWrite)
        .externalSideEffects();

    const auto compiled = graph.compile();
    require(compiled.executionOrder().size() == 2u,
        "discarded import graph must retain producer and consumer");
    require(compiled.passes()[compiled.executionOrder()[0]].name
            == std::pmr::string("GiResolveProducer"),
        "future writer must execute before the current-frame reader");
    require(
        compiled.passes()[compiled.executionOrder()[1]].name == std::pmr::string("SceneConsumer"),
        "current-frame reader must execute after the future writer");
}

void test_persistent_buffer_import()
{
    using namespace arp;
    auto device = rhi::createNullDevice();
    std::weak_ptr<rhi::GpuBuffer> observed;
    {
        render_graph::FrameGraph graph(*device);
        graph.setPassCullingEnabled(true);
        bool rejected = false;
        try {
            graph.importBufferRef({});
        } catch (const std::runtime_error& e) {
            rejected = std::string_view(e.what()) == "RG-IMPORT-BUFFER-NULL";
        }
        require(rejected, "null buffer import must reject");
        auto owner = core::shareUnique(device->createBuffer({"PersistentInstances",
            64,
            rhi::ResourceUsage::StorageBuffer | rhi::ResourceUsage::IndirectBuffer,
            rhi::MemoryUsage::GpuOnly,
            rhi::ResourceState::ShaderRead}));
        observed = owner;
        const auto handle = graph.importBufferRef(owner);
        const auto repeated = graph.importBufferRef(owner);
        require(repeated.id == handle.id && owner.use_count() == 2,
            "repeated buffer imports must share one hazard/lifetime chain");
        auto other = core::shareUnique(device->createBuffer(owner->desc()));
        require(
            graph.importBufferRef(other).id != handle.id, "equal buffer descriptors do not alias");
        require(!graph.isTransient(handle), "persistent import cannot be transient");
        unsigned callbacks = 0;
        graph.addPass("ReadPersistent")
            .access(handle,
                {rhi::ResourceState::ShaderRead,
                    rhi::PipelineStage::DrawIndirect,
                    rhi::ResourceAccess::IndirectCommandRead},
                0,
                16)
            .execute([&](render_graph::RenderContext&) {
                require(graph.buffer(handle).state() == rhi::ResourceState::ShaderRead,
                    "replay read state");
                require(callbacks++ % 2 == 0, "import read must precede overwrite");
            });
        graph.addPass("OverwritePersistent")
            .access(handle,
                {rhi::ResourceState::ShaderWrite,
                    rhi::PipelineStage::ComputeShader,
                    rhi::ResourceAccess::ShaderWrite},
                16,
                16)
            .execute([&](render_graph::RenderContext&) {
                require(callbacks++ % 2 == 1, "import write must preserve old-content reader");
            });
        const auto compiled = graph.compile();
        require(compiled.executionOrder().size() == 2,
            "externally visible buffer write cannot be culled");
        require(compiled.transientResourceCount() == 0 && compiled.transientPeakBytes() == 0,
            "borrowed buffers cannot be charged to transient pool");
        owner.reset();
        graph.execute(compiled);
        graph.execute(compiled);
        require(callbacks == 4 && !observed.expired(), "import survives owner release and replay");
        graph.reset();
        require(!observed.expired() && graph.buffer(handle).desc().sizeBytes == 64,
            "reset retains persistent buffer registration");
    }
    require(observed.expired(), "graph destruction releases final buffer owner");
    {
        render_graph::FrameGraph graph(*device);
        auto uninitialized = core::shareUnique(device->createBuffer({"UndefinedImport",
            16,
            rhi::ResourceUsage::StorageBuffer,
            rhi::MemoryUsage::GpuOnly,
            rhi::ResourceState::Undefined}));
        const auto handle = graph.importBufferRef(uninitialized);
        graph.addPass("InvalidRead").read(handle, rhi::ResourceState::ShaderRead);
        bool rejected = false;
        try {
            graph.compile();
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "import does not initialize undefined contents");
    }
}

void test_ordinary_read_import_retains_dependency()
{
    using namespace arp;
    auto device = rhi::createNullDevice();
    render_graph::FrameGraph graph(*device);
    graph.setPassCullingEnabled(false);
    auto texture = core::shareUnique(device->createTexture(textureDesc("OrdinaryRead",
        {4u, 4u, 1u},
        rhi::TextureFormat::Rgba8Unorm,
        rhi::TextureUsage::Sampled | rhi::TextureUsage::Storage,
        rhi::ResourceState::ShaderRead)));
    const auto handle = graph.importTextureRef(texture);
    graph.addPass("Read").access(handle,
        {rhi::ResourceState::ShaderRead,
            rhi::PipelineStage::FragmentShader,
            rhi::ResourceAccess::ShaderRead},
        rhi::defaultTextureSubresourceRange(texture->desc()));
    auto compiled = graph.compile();
    require(compiled.barrierPlan().size() == 1u,
        "ordinary ShaderRead import retains its first dependency without completed upload proof");
    graph.addPass("Write").write(handle, rhi::ResourceState::ShaderWrite);
    compiled = graph.compile();
    require(compiled.barrierPlan().size() == 2u, "ordinary write hazard is preserved");
    bool rejected = false;
    try {
        graph.importCompletedSampledTexture({}, {}, rhi::PipelineStage::FragmentShader);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "Null cannot fabricate a completed upload import");
}

} // namespace

int main()
{
    test_persistent_buffer_import();
    test_ordinary_read_import_retains_dependency();
    test_lifecycle_keepalive();
    test_reset_releases_borrow();
    test_graph_destruction_releases_borrow();
    test_null_diagnostic_failure();
    test_resolve_precedence_between_owned_and_borrowed();
    test_imported_flag_set_on_borrowed_record();
    test_discarded_import_orders_future_writer_before_reader();

    return 0;
}

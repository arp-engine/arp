#pragma once
#include "original_bindless_geometry_fixture.h"
#include "rhi/backends/vulkan/vulkan_bindless_root.h"
#include "rhi/backends/vulkan/vulkan_original_geometry_content.h"
#include "vulkan_completed_texture_root_fixture.h"

namespace arp::test {
struct VulkanOriginalGeometryDataFixture : VulkanCompletedTextureRootFixture {
    using Content = rhi_vulkan::VulkanOriginalGeometryContent;

    struct Initialized {
        std::array<std::shared_ptr<const Content>, 2u> actors;
        std::array<Content::Quotas, 2u> quotas;
        std::array<std::weak_ptr<const Content>, 2u> retired;
    };

    std::shared_ptr<Initialized> initialized = core::makeShared<Initialized>();
    bool instanceRate = false;
    bool executionGeometry = false;
    static constexpr std::uint32_t geometryBufferCount =
        std::tuple_size<decltype(Initialized::actors)>::value;

    void appendGeometrySources(rhi::GpuDevice& device,
        core::pmr::Vector<rhi::OriginalCommandResourceSources::Source>& sources,
        const rhi::OriginalCommandResourceLeaseBudget& budget,
        const std::shared_ptr<core::CpuMemoryQuota>& quota) const
    {
        using namespace rhi;
        using C = OriginalHostGeometryContentContract;
        constexpr std::uint64_t cpu = 65536u;
        for (std::size_t i = 0u; i < 2u; ++i) {
            auto& q = initialized->quotas[i];
            q = {core::makeShared<core::CpuMemoryQuota>(cpu),
                core::makeShared<core::CpuMemoryQuota>(cpu),
                core::makeShared<core::CpuMemoryQuota>(cpu),
                quota};
            BufferDesc desc{i ? "root-sealed-indices" : "root-sealed-vertices",
                i ? 8u : 64u,
                i ? ResourceUsage::IndexBuffer : ResourceUsage::VertexBuffer,
                MemoryUsage::CpuToGpu,
                ResourceState::ShaderRead};
            C::Budget b{1u,
                true,
                1u,
                C::retainedOwnerBound,
                nativeStagingCeiling,
                cpu,
                cpu,
                cpu,
                budget.maxCpuMetadataBytes,
                C::Staging::VertexContentIntent{1u,
                    true,
                    16u,
                    instanceRate ? VertexInputRate::Instance : VertexInputRate::Vertex,
                    4u}};
            std::array<std::uint8_t, 8u> indices{0xffu, 0xffu, 0u, 0u, 2u, 0u, 1u, 0u};
            std::array<float, 16u> vertices{99.f,
                99.f,
                99.f,
                99.f,
                -1.f,
                -1.f,
                0.f,
                1.f,
                1.f,
                -1.f,
                0.f,
                1.f,
                0.f,
                1.f,
                0.f,
                1.f};
            if (executionGeometry)
                vertices = {99.f,
                    99.f,
                    99.f,
                    99.f,
                    -1.f,
                    -1.f,
                    0.f,
                    1.f,
                    3.f,
                    -1.f,
                    0.f,
                    1.f,
                    -1.f,
                    3.f,
                    0.f,
                    1.f};
            if (i)
                b.definition = C::Staging::IndexContentIntent{1u,
                    true,
                    IndexFormat::Uint16,
                    core::UnsignedElementSpan::ByteOrder::LittleEndian,
                    3u};
            const void* bytes = i ? static_cast<const void*>(indices.data())
                                  : static_cast<const void*>(vertices.data());
            const char* id = nullptr;
            const auto queried = Content::queryOriginalAllocationBytes(device, desc, id);
            assert(queried && !id && queried <= nativeStagingCeiling);
            b.maxRetainedBufferBytes = queried;
            auto undersized = b;
            --undersized.maxRetainedBufferBytes;
            assert(!Content::prepareOriginal(device, desc, bytes, desc.sizeBytes, undersized, q, id)
                && id);
            const auto leaseBaseline = q.lease->liveBytes();
            auto invalidQuotas = q;
            invalidQuotas.payload = q.metadata;
            assert(
                !Content::prepareOriginal(device, desc, bytes, desc.sizeBytes, b, invalidQuotas, id)
                && id);
            {
                core::CpuMemoryQuotaLease pressure(q.metadata, cpu);
                assert(pressure.charged()
                    && !Content::prepareOriginal(device, desc, bytes, desc.sizeBytes, b, q, id));
            }
            assert(!q.metadata->liveBytes() && !q.payload->liveBytes() && !q.resource->liveBytes()
                && q.lease->liveBytes() == leaseBaseline);
            auto* extension = static_cast<BindlessCrossExtension*>(
                device.queryInterface(kExtensionBindlessCross));
            assert(extension);
            const auto allocation = extension->queryOriginalInitializedBufferAllocation(
                device, desc, b.contractVersion, id);
            assert(!id && allocation >= desc.sizeBytes && allocation <= nativeStagingCeiling);
            auto actor = std::dynamic_pointer_cast<const Content>(
                extension->prepareOriginalInitializedBuffer(
                    device, {desc, bytes, desc.sizeBytes, b, q}, id));
            if (!actor)
                std::fprintf(stderr, "Root initialized geometry: %s\n", id ? id : "missing");
            assert(actor && !id && actor->initializedBytes() == desc.sizeBytes
                && !actor->nativeExecutionProven());
            auto forged = std::shared_ptr<const Content>(q.metadata, actor.get());
            assert(Content::validateOriginal(forged));
            forged.reset();
            const auto* buffer = actor->originalLease()->originalObject();
            const auto native = rhi_vulkan::vulkanRhiNativePayload(*buffer);
            assert(native && native->backendMappedData()
                && std::memcmp(native->backendMappedData(), bytes, desc.sizeBytes) == 0);
            assert(!device.writeBuffer(*const_cast<GpuBuffer*>(buffer), bytes, desc.sizeBytes)
                    .success);
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{q.metadata, q.payload, q.resource, q.lease}, [&]() noexcept {
                    return actor->validate();
                }));
            initialized->actors[i] = std::move(actor);
            sources.push_back(initialized->actors[i]->originalLease());
            vertices.fill(-99.f);
            indices.fill(0xffu);
            assert(!initialized->actors[i]->validate());
        }
    }

    void configureGeometryPipeline(rhi::RenderPipelineDesc& desc) const
    {
        OriginalBindlessGeometryDataFixture data;
        data.instanceRate = instanceRate;
        data.configureGeometryPipeline(desc);
    }
};

class VulkanOriginalBindlessGeometryFixture final : public OriginalBindlessGeometryFixture {
public:
    std::shared_ptr<VulkanOriginalGeometryDataFixture::Initialized> initialized;

    void operator()(const std::shared_ptr<const rhi::OriginalCommandBindlessRootSource>& root,
        const std::shared_ptr<const rhi::OriginalCommandResourceSources>& inventory,
        rhi::GpuDevice* device) const
    {
        OriginalBindlessGeometryFixture::verify(root,
            inventory,
            device,
            [&](const auto& inputs,
                const auto& stream,
                const auto& fetch,
                const auto& graph,
                const auto& state,
                const auto& sequence) {
                using Content = VulkanOriginalGeometryDataFixture::Content;
                assert(initialized && initialized->actors[0] && initialized->actors[1]);
                const auto analyze =
                    [&](const auto& point, const auto& lease, auto& out) noexcept -> const char* {
                    return Content::analyzeInitializedIndices(initialized->actors[1],
                        lease,
                        point.index.format,
                        point.index.offset,
                        point.draw.firstIndex,
                        point.draw.indices,
                        point.draw.vertexOffset,
                        out);
                };
                const auto vertex = [&](const auto& point,
                                        const auto&,
                                        const auto& layout,
                                        const auto& fields,
                                        const auto& lease) noexcept -> const char* {
                    return Content::validateInitializedVertices(initialized->actors[0],
                        lease,
                        layout,
                        fields.offset,
                        point.fetch.vertexCount,
                        point.fetch.firstVertex,
                        point.draw.instances,
                        point.draw.firstInstance);
                };
                std::size_t emitted = 0u;
                const auto observed = stream->visitInitializedIndexedDrawFetch(
                    fetch, analyze, vertex, [&](const auto&) noexcept -> const char* {
                        ++emitted;
                        return nullptr;
                    });
                if (observed.diagnostic)
                    std::fprintf(
                        stderr, "Root genuine initialized fetch: %s\n", observed.diagnostic);
                assert(!observed.diagnostic && emitted == 1u && observed.analyzedIndices == 3u
                    && observed.streams == 1u);
                rhi::IndexInputConsumptionContract::DrawRange output;
                assert(Content::analyzeInitializedIndices(initialized->actors[1],
                    initialized->actors[0]->originalLease(),
                    rhi::IndexFormat::Uint16,
                    2u,
                    0u,
                    3u,
                    0,
                    output));
                assert(Content::analyzeInitializedIndices(initialized->actors[1],
                    initialized->actors[1]->originalLease(),
                    rhi::IndexFormat::Uint32,
                    2u,
                    0u,
                    3u,
                    0,
                    output));
                emitted = 0u;
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                    std::array{initialized->quotas[0].metadata,
                        initialized->quotas[0].payload,
                        initialized->quotas[0].resource,
                        initialized->quotas[1].metadata,
                        initialized->quotas[1].payload,
                        initialized->quotas[1].resource},
                    [&]() noexcept {
                        return stream
                            ->visitInitializedIndexedDrawFetch(fetch,
                                analyze,
                                vertex,
                                [&](const auto&) noexcept -> const char* {
                                    ++emitted;
                                    return nullptr;
                                })
                            .diagnostic;
                    }));
                assert(emitted == 1u && inputs);
                std::puts(
                    "BINDLESS_VULKAN_ORIGINAL_ROOT_INITIALIZED_GEOMETRY1 1 authentic-frozen-content real-mapped-write-flush sealed-index-vertex same-lease same-stream hot0 pending-and-execution-closed");
                using namespace rhi;
                constexpr std::uint64_t cpu = 65536u;
                auto contentQuota = core::makeShared<core::CpuMemoryQuota>(cpu);
                OriginalFiniteGraphicsContentPlanBudget content{
                    9u, true, root->size(), 1u, 1u, root->size(), 2u, 512u, 8192u, 1u, true};
                const char* contentId = nullptr;
                const auto prepareContent = [&](const auto& limits, const auto& quota) {
                    return OriginalCommandStateTransaction::
                        prepareOriginalBindlessGraphicsOwnership(
                            graph, stream, *device, state, sequence, limits, quota, contentId);
                };
                auto tx = prepareContent(content, contentQuota);
                if (!tx)
                    std::fprintf(stderr,
                        "Root geometry Content9/Geometry1: %s\n",
                        contentId ? contentId : "missing");
                assert(tx && !contentId && tx->hasOriginalBindlessGraphicsGeometryProfile()
                    && !tx->hasOriginalBindlessRootShaderResourceProfile() && tx->journalEmpty());
                assert(!tx->compareOriginalBindlessGraphicsStream(stream));
                const auto counts = *tx->originalMixedContentCounts();
                assert(counts.attachmentControls == 1u && counts.completedControls
                    && counts.drawReads == counts.completedControls);
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                    std::array{contentQuota}, [&]() noexcept {
                        return tx->validate();
                    }));
                tx.reset();
                assert(!contentQuota->liveBytes());
                content.maxCompletedControls = counts.completedControls;
                content.maxSampledSteps = counts.drawReads;
                content.maxCommandVisits = counts.commandVisits;
                content.maxProjectionVisits = counts.projectionVisits;
                for (const auto member :
                    {&OriginalFiniteGraphicsContentPlanBudget::maxAttachmentSteps,
                        &OriginalFiniteGraphicsContentPlanBudget::maxSampledSteps,
                        &OriginalFiniteGraphicsContentPlanBudget::maxCommandVisits,
                        &OriginalFiniteGraphicsContentPlanBudget::maxProjectionVisits}) {
                    auto less = content;
                    --(less.*member);
                    assert(!prepareContent(less, contentQuota) && contentId
                        && !contentQuota->liveBytes());
                    assert(!graph->validate() && !stream->validate());
                }
                assert(!prepareContent(content, stream->originalMetadataQuota()) && contentId);
                {
                    core::CpuMemoryQuotaLease pressure(contentQuota, cpu);
                    assert(
                        pressure.charged() && !prepareContent(content, contentQuota) && contentId);
                }
                assert(!contentQuota->liveBytes());
                tx = prepareContent(content, contentQuota);
                assert(tx && !contentId);
                auto legacy = content;
                legacy.bindlessGeometryContractVersion = 0u;
                legacy.bindlessGeometryOptIn = false;
                assert(!prepareContent(legacy, contentQuota) && contentId && tx->journalEmpty());
                std::puts(
                    "BINDLESS_VULKAN_ORIGINAL_ROOT_GEOMETRY_CONTENT9_1 1 same-graph same-stream exact-geometry-footprint completed-sampled-content original-attachment fault-copy budget-rollback hot0 pending-and-indexed-execution-closed");
            });
        if (!device) {
            assert(initialized);
            for (std::size_t i = 0u; i < initialized->actors.size(); ++i) {
                if (initialized->actors[i])
                    assert(VulkanOriginalGeometryDataFixture::Content::validateOriginal(
                        initialized->actors[i]));
                else
                    assert(initialized->retired[i].expired());
                initialized->actors[i].reset();
                assert(!initialized->quotas[i].metadata->liveBytes()
                    && !initialized->quotas[i].payload->liveBytes());
            }
            return;
        }
        using C = rhi_vulkan::VulkanBindlessRootNativeClosure;
        constexpr std::uint64_t bytes = 65536u;
        const auto quota = core::makeShared<core::CpuMemoryQuota>(bytes);
        const auto entries = static_cast<std::uint32_t>(
            root->elements().size() + 1u + VulkanOriginalGeometryDataFixture::geometryBufferCount);
        rhi::OriginalBindlessRootNativeClosureBudget budget{
            2u, true, entries, entries * 2u + 2u, 512u, bytes};
        const char* id = nullptr;
        const auto prepare = [&](const auto& limits) {
            return C::prepareOriginal(root, inventory, *device, limits, quota, id);
        };
        auto first = prepare(budget);
        if (!first)
            std::fprintf(stderr, "Root Geometry closure2: %s\n", id ? id : "missing");
        assert(first && !id && first->resourceEntryCount() == entries
            && !first->nativeExecutionProven() && first->closureContractVersion() == 2u);
        budget.maxNativeOwners = first->nativeOwnerCount();
        budget.maxRetainedOwners = first->retainedOwnerCount();
        first.reset();
        assert(!quota->liveBytes());
        for (const auto member : {&rhi::OriginalBindlessRootNativeClosureBudget::maxResourceEntries,
                 &rhi::OriginalBindlessRootNativeClosureBudget::maxNativeOwners,
                 &rhi::OriginalBindlessRootNativeClosureBudget::maxRetainedOwners}) {
            auto smaller = budget;
            --(smaller.*member);
            assert(!prepare(smaller) && id && !quota->liveBytes());
        }
        auto closure = prepare(budget);
        assert(closure && !id);
        auto forged = std::shared_ptr<const C>(quota, closure.get());
        assert(C::validateOriginal(forged));
        forged.reset();
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(std::array{quota}, [&]() noexcept {
            return closure->validate();
        }));
        // The geometry inventory is retained by this exact closure. Descriptors
        // and geometry share its owner projection; no second resource graph.
        assert(core::SharedOwnerProjection::sameOwner(closure->originalInventory(), inventory));
        closure.reset();
        assert(!quota->liveBytes());
        std::puts(
            "BINDLESS_VULKAN_ORIGINAL_ROOT_GEOMETRY_CLOSURE2 1 genuine-buffers same-inventory owner-dedup exact-capacity hot-allocation0 cold-only execution-closed");

        budget.contractVersion = 3u;
        budget.maxInitializedGeometryActors = 2u;
        budget.maxInitializedGeometryBytes =
            initialized->actors[0]->initializedBytes() + initialized->actors[1]->initializedBytes();
        budget.maxRetainedOwners = 512u;
        const auto prepareInitialized =
            [&](const auto& limits, const auto& actors, std::size_t count = 2u) {
                return C::prepareOriginal(
                    root, inventory, *device, limits, quota, id, {actors.data(), count});
            };
        auto third = prepareInitialized(budget, initialized->actors);
        if (!third)
            std::fprintf(stderr, "Root initialized closure3: %s\n", id ? id : "missing");
        assert(third && !id && third->initializedGeometryCount() == 2u
            && third->initializedGeometryBytes() == 72u && third->closureContractVersion() == 3u
            && !third->nativeExecutionProven());
        budget.maxRetainedOwners = third->retainedOwnerCount();
        third.reset();
        assert(!quota->liveBytes());
        auto smaller = budget;
        --smaller.maxRetainedOwners;
        assert(!prepareInitialized(smaller, initialized->actors) && id && !quota->liveBytes());
        smaller = budget;
        --smaller.maxInitializedGeometryActors;
        assert(!prepareInitialized(smaller, initialized->actors) && id && !quota->liveBytes());
        smaller = budget;
        --smaller.maxInitializedGeometryBytes;
        assert(!prepareInitialized(smaller, initialized->actors) && id && !quota->liveBytes());
        assert(!prepare(budget) && id && !quota->liveBytes());
        assert(!prepareInitialized(budget, initialized->actors, 1u) && id && !quota->liveBytes());
        const auto actorBytes = initialized->quotas[0].metadata->liveBytes();
        assert(!C::prepareOriginal(root,
                   inventory,
                   *device,
                   budget,
                   initialized->quotas[0].metadata,
                   id,
                   {initialized->actors.data(), initialized->actors.size()})
            && core::StringView(id) == "RHI-VULKAN-BINDLESS-ROOT-NATIVE-QUOTA-DOMAIN"
            && initialized->quotas[0].metadata->liveBytes() == actorBytes);
        auto invalid = initialized->actors;
        invalid[1] = invalid[0];
        assert(!prepareInitialized(budget, invalid) && id && !quota->liveBytes());
        invalid = initialized->actors;
        invalid[1] = std::shared_ptr<const VulkanOriginalGeometryDataFixture::Content>(
            quota, invalid[1].get());
        assert(!prepareInitialized(budget, invalid) && id && !quota->liveBytes());
        invalid = {};
        auto old = budget;
        old.contractVersion = 2u;
        old.maxInitializedGeometryActors = 0u;
        old.maxInitializedGeometryBytes = 0u;
        assert(!prepareInitialized(old, initialized->actors) && id && !quota->liveBytes());
        {
            core::CpuMemoryQuotaLease pressure(quota, bytes);
            assert(pressure.charged() && !prepareInitialized(budget, initialized->actors) && id);
        }
        assert(!quota->liveBytes());
        third = prepareInitialized(budget, initialized->actors);
        assert(third && !id);
        for (std::size_t i = 0u; i < initialized->actors.size(); ++i) {
            const auto index =
                inventory->originalLeaseIndex(initialized->actors[i]->originalLease());
            assert(core::SharedOwnerProjection::sameOwner(
                third->initializedGeometry(index), initialized->actors[i]));
            initialized->retired[i] = initialized->actors[i];
            initialized->actors[i].reset();
            assert(!initialized->retired[i].expired());
        }
        assert(!third->initializedGeometry(SIZE_MAX));
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(std::array{quota}, [&]() noexcept {
            return third->validate();
        }));
        third.reset();
        assert(!quota->liveBytes());
        for (std::size_t i = 0u; i < initialized->retired.size(); ++i) {
            assert(initialized->retired[i].expired());
            // Weak control-block storage is independently charged too.
            initialized->retired[i].reset();
            assert(!initialized->quotas[i].metadata->liveBytes()
                && !initialized->quotas[i].payload->liveBytes());
        }
        std::puts(
            "BINDLESS_VULKAN_ORIGINAL_ROOT_INITIALIZED_CLOSURE3 1 authentic-actors exact-canonical-leases caller-drop-retained owner-dedup byte-capacity failure-rollback hot0 cold-only pending-and-execution-closed");
    }
};
} // namespace arp::test

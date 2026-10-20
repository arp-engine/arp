#pragma once
#include "ordinary_completed_depth_source_fixture.h"
#include "original_mixed_depth_storage_plan_fixture.h"
#include "original_ordinary_depth_root_fixture.h"
#include "rhi/frontend/completed_depth_read_input.h"
#include "rhi/frontend/completed_depth_texture_source.h"
#include "rhi/frontend/descriptor_completed_texture.h"
#include "vulkan_completed_texture_root_fixture.h"
#include "vulkan_ordinary_depth_graphics_execution_fixture.h"
#include "vulkan_original_bindless_graphics_content_fixture.h"
#include "vulkan_original_depth_root_preparation_fixture.h"

namespace arp::test {
// Actual depth producer uses the existing public Graphics view and original
// Content9 journal/fence. Publication is cold and grants no depth shader access.
class VulkanOriginalCompletedDepthPublicationFixture final {
public:
    std::shared_ptr<const rhi::BindlessShaderPackage> depthPackage;
    std::shared_ptr<const rhi::BindlessShaderPackage> ordinaryGraphicsPackage;
    bool mixedContent = false;

    void operator()(const std::shared_ptr<const rhi::OriginalCommandBindlessRootSource>& root,
        const std::shared_ptr<const rhi::OriginalCommandResourceSources>& inventory,
        rhi::GpuDevice* device) const
    {
        using namespace rhi;
        using Proof = OriginalCompletedDepthTextureSource;
        if (!device) {
            assert(!root && !inventory);
            return;
        }
        std::shared_ptr<const OriginalCommandTextureViewSource> target;
        for (const auto& source : inventory->sources())
            if (const auto* view =
                    std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(&source))
                if ((*view)->originalObject()->desc().usage == TextureUsage::RenderAttachment)
                    target = *view;
        assert(target && target->originalObject()->desc().format == TextureFormat::D32Float);
        const auto& quota = target->originalMetadataQuota();
        const auto baseline = quota->liveBytes();
        {
            const char* id = nullptr;
            TextureViewDesc vd = target->originalObject()->desc();
            vd.usage = TextureUsage::Sampled;
            core::UniquePtr<GpuTextureView> view;
            {
                core::CpuMemoryQuotaScope allocation(quota);
                view =
                    device->createTextureView(*target->originalParentLease()->originalObject(), vd);
            }
            assert(view);
            auto lease = OriginalCommandTextureViewLease::prepareOriginal(
                view, {1u, true, 2u, quota->limitBytes()}, quota, id);
            assert(lease && !id);
            auto sampled = OriginalCommandTextureViewSource::prepareOriginal(lease,
                target->originalParentLease(),
                {1u,
                    true,
                    OriginalCommandTextureViewSource::completeRetainedOwnerCount,
                    quota->limitBytes()},
                quota,
                id);
            assert(sampled && !id);
            const Proof::Budget budget{
                2u, true, 1u, quota->limitBytes(), Proof::completeRetainedOwnerCount};
            const auto prepare = [&] {
                return Proof::prepareOriginal(sampled, budget, quota, id);
            };
            assert(!prepare() && id);
            VulkanOriginalBindlessGraphicsContentFixture producer;
            producer.execute = true;
            producer(root, inventory, device);
            auto legacy = budget;
            legacy.contractVersion = 1u;
            assert(!Proof::prepareOriginal(sampled, legacy, quota, id));
            auto shortBudget = budget;
            --shortBudget.maxCells;
            assert(!Proof::prepareOriginal(sampled, shortBudget, quota, id));
            shortBudget = budget;
            --shortBudget.maxRetainedOwners;
            assert(!Proof::prepareOriginal(sampled, shortBudget, quota, id));
            CpuQuotaFactoryFixture pressure(quota);
            const auto peak = pressure.measureSingleUse([] {}, prepare);
            pressure.verifySingleUse(peak, [] {}, prepare);
            auto proof = prepare();
            assert(proof && !id && !proof->nativeExecutionProven());
            assert(proof->cells().size() == 1u && proof->cells().front().controlVersion() == 4u);
            assert(!OriginalCompletedTextureDescriptorContract::validateSource(proof, 4u));
            assert(OriginalCompletedTextureDescriptorContract::validateSource(proof, 3u));
            const OriginalCompletedDepthReadInputContract::Budget readBudget{
                1u, true, 1u, 16u, Proof::completeRetainedOwnerCount};
            assert(
                OriginalCompletedDepthReadInputContract::inspect({proof,
                                                                     ResourceState::ShaderRead,
                                                                     PipelineStage::ComputeShader,
                                                                     ResourceAccess::ShaderRead},
                    sampled,
                    readBudget)
                    .diagnostic);
            auto nativeRead = readBudget;
            nativeRead.contractVersion = 2u;
            assert(
                !OriginalCompletedDepthReadInputContract::inspect({proof,
                                                                      ResourceState::ShaderRead,
                                                                      PipelineStage::ComputeShader,
                                                                      ResourceAccess::ShaderRead},
                    sampled,
                    nativeRead)
                    .diagnostic);
            DescriptorLayoutDesc layout;
            layout.name = "native completed depth4";
            layout.bindings.push_back(
                {0u, DescriptorType::SampledTexture, ShaderStage::Fragment, 1u});
            auto& binding = layout.bindings.front();
            binding.depthTexture = true;
            binding.textureViewDimensionConstrained = true;
            binding.textureViewDimension = TextureViewDimension::Texture2D;
            std::shared_ptr<DescriptorArena> arena;
            {
                core::CpuMemoryQuotaScope allocation(quota);
                arena = core::shareUnique(
                    device->createOriginalDescriptorArena({"native completed depth4", 1u, 1u}));
            }
            assert(arena);
            auto set =
                device->allocateOriginalDescriptorSet(arena, {"native completed depth4", layout});
            assert(set);
            DescriptorBindingWrite write;
            write.type = DescriptorType::SampledTexture;
            write.resource = sampled->resource();
            write.resourceKind = DescriptorResourceKind::TextureView;
            write.resourceState = ResourceState::ShaderRead;
            const OriginalCompletedTextureDescriptorRequest publication{
                4u, true, 1u, 1u, Proof::completeRetainedOwnerCount};
            const auto revision = set->contentRevision();
            auto wrong = publication;
            wrong.contractVersion = 3u;
            assert(!set->writeOriginalCompletedTexture(*device, write, proof, wrong).success
                && set->contentRevision() == revision);
            const auto published =
                set->writeOriginalCompletedTexture(*device, write, proof, publication);
            if (!published.success)
                std::fprintf(
                    stderr, "Vulkan depth publication4: %s\n", published.diagnostics.c_str());
            assert(published.success && set->validateBindingElement(0u, 0u).success);
            assert(
                !CpuQuotaFactoryFixture::verifyHotNoAllocation(std::array{quota}, [&]() noexcept {
                    return proof->validate();
                }));
            const auto oldGeneration = proof->cells().front().contentGeneration();
            auto selectedProof = proof;
            assert(depthPackage);
            if (mixedContent) {
                auto closure = VulkanOriginalDepthRootPreparationFixture::prepare(proof,
                    depthPackage,
                    *device,
                    quota,
                    true,
                    false,
                    0u,
                    &OriginalMixedDepthStoragePlanFixture::verify<
                        rhi_vulkan::VulkanBindlessRootNativeClosure>);
                auto metadata = closure->originalMetadataQuota();
                closure.reset();
                assert(!metadata->liveBytes());
            } else {
                auto closure = VulkanOriginalDepthRootPreparationFixture::prepare(
                    proof, depthPackage, *device, quota);
                auto nativeQuota = closure->originalMetadataQuota();
                producer.attachmentClearOptIn = true;
                producer(root, inventory, device);
                assert(proof->validate() && !set->validateBindingElement(0u, 0u).success);
                assert(closure->validate());
                closure.reset();
                assert(!nativeQuota->liveBytes());
                const auto staleRevision = set->contentRevision();
                assert(
                    !set->writeOriginalCompletedTexture(*device, write, proof, publication).success
                    && set->contentRevision() == staleRevision);
                auto fresh = prepare();
                selectedProof = fresh;
                assert(fresh && !id && fresh->cells().front().contentGeneration() > oldGeneration);
                assert(
                    set->writeOriginalCompletedTexture(*device, write, fresh, publication).success);
                assert(set->validateBindingElement(0u, 0u).success);
                closure = VulkanOriginalDepthRootPreparationFixture::prepare(
                    fresh, depthPackage, *device, quota);
                nativeQuota = closure->originalMetadataQuota();
                closure.reset();
                assert(!nativeQuota->liveBytes());
                for (const bool fault : {false, true}) {
                    closure = VulkanOriginalDepthRootPreparationFixture::prepare(
                        fresh, depthPackage, *device, quota, true, fault);
                    nativeQuota = closure->originalMetadataQuota();
                    closure.reset();
                    assert(!nativeQuota->liveBytes());
                    assert(!fresh->validate() && set->validateBindingElement(0u, 0u).success);
                }
                if (const auto* selected =
                        std::getenv("ARP_VULKAN_PUBLIC_DEPTH_EXECUTION_TERMINAL")) {
                    const auto terminal =
                        static_cast<unsigned>(std::strtoul(selected, nullptr, 10));
                    assert(terminal >= 1u && terminal <= 4u);
                    closure = VulkanOriginalDepthRootPreparationFixture::prepare(
                        fresh, depthPackage, *device, quota, true, false, terminal);
                    nativeQuota = closure->originalMetadataQuota();
                    closure.reset();
                    assert(!nativeQuota->liveBytes());
                }
            }
            if (!mixedContent) {
                OriginalOrdinaryDepthRootFixture::Prepared ordinary;
                std::shared_ptr<const rhi_vulkan::VulkanBindlessRootNativeClosure> ordinaryNative;
                if (ordinaryGraphicsPackage) {
                    TextureDesc colorDesc;
                    colorDesc.label = "ordinary depth/color completed color";
                    colorDesc.extent = {2u, 2u, 1u};
                    colorDesc.format = TextureFormat::Rgba8Unorm;
                    VulkanCompletedTextureRootFixture upload;
                    // Same declared device memory ceiling as the enclosing runner.
                    upload.nativeStagingCeiling = 64u * 1024u * 1024u;
                    auto color = upload.createCompletedTexture(*device, colorDesc, quota, 0u);
                    TextureViewDesc colorViewDesc;
                    colorViewDesc.format = colorDesc.format;
                    colorViewDesc.usage = TextureUsage::Sampled;
                    colorViewDesc.dimension = TextureViewDimension::Texture2D;
                    colorViewDesc.mipLevelCount = colorDesc.mipLevelCount;
                    auto colorObject =
                        device->createTextureView(*color->originalObject(), colorViewDesc);
                    assert(colorObject);
                    auto colorLease = OriginalCommandTextureViewLease::prepareOriginal(
                        colorObject, {1u, true, 2u, quota->limitBytes()}, quota, id);
                    assert(colorLease && !id);
                    auto colorView = OriginalCommandTextureViewSource::prepareOriginal(colorLease,
                        color,
                        {1u,
                            true,
                            OriginalCommandTextureViewSource::completeRetainedOwnerCount,
                            quota->limitBytes()},
                        quota,
                        id);
                    assert(colorView && !id);
                    for (const bool fault : {false, true}) {
                        ordinaryNative.reset();
                        ordinary = {};
                        // CPU plan rollback, recorded cancellation and genuine
                        // submission each consume a non-recycled generation.
                        ordinary = OriginalOrdinaryDepthRootFixture::prepare(*device,
                            ordinaryGraphicsPackage,
                            selectedProof,
                            colorView,
                            fault,
                            1u + 1u + 1u);
                        OriginalOrdinaryDepthRootFixture::verifyCombinedPlan(ordinary, *device);
                        auto nativeQuota =
                            core::makeShared<core::CpuMemoryQuota>(quota->limitBytes());
                        // Old native closure1 still cannot consume this cold Root.
                        const auto before = nativeQuota->liveBytes();
                        auto denied = rhi_vulkan::VulkanBindlessRootNativeClosure::prepareOriginal(
                            ordinary.root,
                            ordinary.inventory,
                            *device,
                            {1u, true, 9u, 128u, 128u, nativeQuota->limitBytes()},
                            nativeQuota,
                            id);
                        assert(!denied && id
                            && core::StringView(id)
                                == "RHI-VULKAN-BINDLESS-ORDINARY-IMAGE-UNSUPPORTED"
                            && nativeQuota->liveBytes() == before);
                        using Closure = rhi_vulkan::VulkanBindlessRootNativeClosure;
                        std::size_t physical = ordinary.root->elements().size()
                            + (ordinary.root->originalSnapshot()->originalBufferSource(
                                   BindlessSnapshot::OriginalBufferRole::FaultReadback)
                                    ? 1u
                                    : 0u);
                        for (const auto& domain : ordinary.root->ordinaryDomains())
                            physical += domain.source->frozenElements().size();
                        const auto nativeBound =
                            2u * physical + 2u + ordinary.root->ordinaryDomains().size();
                        std::uint32_t cpuBound = 0u;
                        assert(!ordinary.inventory->cpuOwnerCount(
                            4u + static_cast<std::uint32_t>(physical), UINT32_MAX, cpuBound));
                        assert(nativeBound <= UINT32_MAX - cpuBound);
                        OriginalBindlessRootNativeClosureBudget closureBudget{16u,
                            true,
                            static_cast<std::uint32_t>(physical),
                            static_cast<std::uint32_t>(nativeBound),
                            static_cast<std::uint32_t>(nativeBound) + cpuBound,
                            nativeQuota->limitBytes()};
                        std::uint64_t depthBytes = 0u;
                        assert(!semantics::TextureUploadDestinationValidation::footprint(
                            selectedProof->originalViewSource()
                                ->originalParentLease()
                                ->originalObject()
                                ->desc(),
                            depthBytes));
                        closureBudget.completedDepthInputs = {3u,
                            true,
                            selectedProof->cells().size(),
                            depthBytes,
                            Proof::completeRetainedOwnerCount};
                        const auto prepareNative = [&](const auto& limits) {
                            return Closure::prepareOriginal(ordinary.root,
                                ordinary.inventory,
                                *device,
                                limits,
                                nativeQuota,
                                id);
                        };
                        auto native = prepareNative(closureBudget);
                        if (!native)
                            std::fprintf(
                                stderr, "ordinary Closure16: %s\n", id ? id : "missing diagnostic");
                        assert(native && !id && !native->nativeExecutionProven());
                        closureBudget.maxNativeOwners = native->nativeOwnerCount();
                        closureBudget.maxRetainedOwners = native->retainedOwnerCount();
                        assert(native->resourceEntryCount() == physical);
                        native.reset();
                        assert(nativeQuota->liveBytes() == before);
                        native = prepareNative(closureBudget);
                        assert(native && !id && !native->validate());
                        for (const auto member :
                            {&OriginalBindlessRootNativeClosureBudget::maxResourceEntries,
                                &OriginalBindlessRootNativeClosureBudget::maxNativeOwners,
                                &OriginalBindlessRootNativeClosureBudget::maxRetainedOwners}) {
                            auto less = closureBudget;
                            --(less.*member);
                            const auto live = nativeQuota->liveBytes();
                            assert(!prepareNative(less) && id && nativeQuota->liveBytes() == live
                                && !native->validate()
                                && !OriginalCommandBindlessRootSource::validateOriginal(
                                    ordinary.root, *device));
                        }
                        auto wrong = closureBudget;
                        wrong.completedDepthInputs.contractVersion = 2u;
                        assert(!prepareNative(wrong) && id && !native->validate());
                        const auto live = nativeQuota->liveBytes();
                        wrong = closureBudget;
                        --wrong.completedDepthInputs.maxTextureTexelBytes;
                        assert(!prepareNative(wrong) && id && nativeQuota->liveBytes() == live
                            && !native->validate());
                        assert(
                            !Closure::prepareOriginal(
                                ordinary.root, inventory, *device, closureBudget, nativeQuota, id)
                            && id && nativeQuota->liveBytes() == live);
                        assert(!Closure::prepareOriginal(
                                   root, inventory, *device, closureBudget, nativeQuota, id)
                            && id
                            && core::StringView(id)
                                == "RHI-BINDLESS-ORDINARY-GRAPHICS-DEPTH-CLOSURE-ROOT"
                            && nativeQuota->liveBytes() == live);
                        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                            std::array{nativeQuota, quota}, [&]() noexcept {
                                return Closure::validateOriginal(native);
                            }));
                        native.reset();
                        assert(nativeQuota->liveBytes() == before);
                        CpuQuotaFactoryFixture closurePressure(nativeQuota);
                        const auto peak = closurePressure.measureSingleUse([] {},
                            [&] {
                                return prepareNative(closureBudget);
                            });
                        closurePressure.verifySingleUse(
                            peak,
                            [] {},
                            [&] {
                                return prepareNative(closureBudget);
                            });
                        assert(nativeQuota->liveBytes() == before);
                        ordinaryNative = prepareNative(closureBudget);
                        assert(ordinaryNative && !id && !ordinaryNative->validate());
                        std::puts(
                            "BINDLESS_ORDINARY_DEPTH_CLOSURE16_OK actual-descriptors physical-proof selected-fragment exact-less-owners quota-rollback hot0 execution-closed");
                        VulkanOrdinaryDepthGraphicsExecutionFixture::execute(
                            ordinary, *device, closureBudget, fault);
                    }
                }
                OrdinaryCompletedDepthSourceFixture::verify(
                    *device, set, arena, selectedProof, [&] {
                        producer.attachmentClearOptIn = true;
                        ResourceBarrierDesc barrier{target->originalParentLease()->resource(),
                            defaultTextureSubresourceRange(
                                target->originalParentLease()->originalObject()->desc()),
                            ResourceState::ShaderRead,
                            ResourceState::DepthAttachment};
                        barrier.sourceStages =
                            PipelineStage::ComputeShader | PipelineStage::FragmentShader;
                        barrier.sourceAccess = ResourceAccess::ShaderRead;
                        producer.completedAttachmentBarrier = barrier;
                        producer(root, inventory, device);
                    });
                if (ordinary.root)
                    assert(OriginalCommandBindlessRootSource::validateOriginal(
                        ordinary.root, *device));
                if (ordinaryNative)
                    assert(ordinaryNative->validate());
            }
            std::puts(
                "BINDLESS_VULKAN_PUBLIC_DEPTH_PUBLICATION4 source2 control4 single-cell public-direct-graphics cancel-retry real-fence fault0 reclear stale-proof atomic-refresh retirement publication-alone-no-execution-grant");
        }
        assert(quota->liveBytes() == baseline);
    }
};
} // namespace arp::test

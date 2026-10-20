#pragma once
#include "metal_completed_texture_root_fixture.h"
#include "original_ordinary_depth_root_fixture.h"
#include "rhi/backends/metal/metal_bindless_root.h"

namespace arp::test {
// Reuses the genuine producer proof, original upload issuer and shared Root2 /
// Content18 fixture. No synthetic content or comparison execution is issued.
class MetalOrdinaryDepthClosureFixture final {
    static void require(const char* id)
    {
        if (id)
            std::fprintf(stderr, "Metal ordinary Closure16: %s\n", id);
        assert(!id);
    }

public:
    inline static void (*executionCheck)(const OriginalOrdinaryDepthRootFixture::Prepared&,
        rhi::GpuDevice&,
        const rhi::OriginalBindlessRootNativeClosureBudget&,
        bool) = nullptr;

    static void configureRegistration(rhi::OriginalResourceRegistrationBudget& b)
    {
        // One upload staging buffer; Root Results+three snapshot buffers;
        // uploaded color+independent attachment, their views, two arenas/sets
        // and the three distinct ordinary samplers. Depth reuses the producer.
        constexpr auto buffers = 5u, textures = 2u, views = 2u, arenas = 2u, sets = 2u,
                       samplers = 3u;
        b.maxBuffers += buffers;
        b.maxTextures += textures;
        b.maxTextureViews += views;
        b.maxDescriptorArenas += arenas;
        b.maxDescriptorSets += sets;
        b.samplers.maxSamplers += samplers;
        b.maxRetainedOwners += (buffers + textures + views + arenas + samplers)
                * rhi::OriginalResourceRegistrationContract::perResourceOwnerCount
            + sets * rhi::OriginalResourceRegistrationContract::perDescriptorOwnerCount;
    }

    static void verify(rhi::GpuDevice& device,
        const std::shared_ptr<const rhi::BindlessShaderPackage>& package,
        const std::shared_ptr<const rhi::OriginalCompletedDepthTextureSource>& proof,
        const std::shared_ptr<const rhi::OriginalCommandResourceSources>& foreignInventory)
    {
        using namespace rhi;
        using Closure = rhi_metal::MetalRhiBindlessRootNativeClosure;
        const auto& quota = proof->originalMetadataQuota();
        const auto baseline = quota->liveBytes();
        {
            const char* id = nullptr;
            TextureDesc desc;
            desc.label = "Metal ordinary depth/color uploaded color";
            desc.format = TextureFormat::Rgba8Unorm;
            desc.extent = {2u, 2u, 1u};
            MetalCompletedTextureUploadFixture upload;
            auto color = upload.createCompletedTexture(device, desc, quota, 0u);
            TextureViewDesc vd;
            vd.format = desc.format;
            vd.usage = TextureUsage::Sampled;
            vd.mipLevelCount = desc.mipLevelCount;
            auto object = device.createTextureView(*color->originalObject(), vd);
            assert(object);
            auto lease = OriginalCommandTextureViewLease::prepareOriginal(
                object, {1u, true, 2u, quota->limitBytes()}, quota, id);
            require(id);
            auto source = OriginalCommandTextureViewSource::prepareOriginal(lease,
                color,
                {1u,
                    true,
                    OriginalCommandTextureViewSource::completeRetainedOwnerCount,
                    quota->limitBytes()},
                quota,
                id);
            require(id);
            for (const bool fault : {false, true}) {
                const auto ordinary = OriginalOrdinaryDepthRootFixture::prepare(
                    device, package, proof, source, fault, executionCheck ? 3u : 2u);
                OriginalOrdinaryDepthRootFixture::verifyCombinedPlan(ordinary, device);
                const auto nativeQuota =
                    core::makeShared<core::CpuMemoryQuota>(quota->limitBytes());
                assert(
                    !rhi_metal::prepareMetalRhiOriginalOrdinaryColorDescriptors(ordinary.root, 2u));
                assert(!rhi_metal::metalRhiNativePayloadFor(
                    *ordinary.root->ordinaryDomains().front().source->originalObject()));
                std::size_t count = ordinary.root->elements().size() + 1u;
                for (const auto& domain : ordinary.root->ordinaryDomains())
                    count += domain.source->frozenElements().size();
                // Same upper bounds as the native owner projection: ten per
                // physical resource, eight per group and nine pipeline slots.
                const auto owners =
                    10u * count + 8u * (ordinary.root->ordinaryDomains().size() + 1u) + 9u;
                std::uint32_t cpu = 0u;
                require(ordinary.inventory->cpuOwnerCount(4u, UINT32_MAX, cpu));
                assert(owners <= UINT32_MAX - cpu);
                OriginalBindlessRootNativeClosureBudget b{16u,
                    true,
                    static_cast<std::uint32_t>(count),
                    static_cast<std::uint32_t>(owners),
                    static_cast<std::uint32_t>(owners) + cpu,
                    nativeQuota->limitBytes()};
                std::uint64_t texels = 0u;
                require(semantics::TextureUploadDestinationValidation::footprint(
                    proof->originalViewSource()->originalParentLease()->originalObject()->desc(),
                    texels));
                b.completedDepthInputs = {3u,
                    true,
                    proof->cells().size(),
                    texels,
                    OriginalCompletedDepthTextureSource::completeRetainedOwnerCount};
                const auto prepare = [&](const auto& limits) {
                    return Closure::prepareOriginal(
                        ordinary.root, ordinary.inventory, device, limits, nativeQuota, id);
                };
                auto native = prepare(b);
                require(id);
                assert(native && !native->nativeExecutionProven()
                    && native->resourceEntryCount() == count);
                b.maxNativeOwners = native->nativeOwnerCount();
                b.maxRetainedOwners = native->retainedOwnerCount();
                native.reset();
                assert(!nativeQuota->liveBytes());
                native = prepare(b);
                require(id);
                assert(native && !native->validate());
                for (const auto member :
                    {&OriginalBindlessRootNativeClosureBudget::maxResourceEntries,
                        &OriginalBindlessRootNativeClosureBudget::maxNativeOwners,
                        &OriginalBindlessRootNativeClosureBudget::maxRetainedOwners}) {
                    auto less = b;
                    --(less.*member);
                    const auto live = nativeQuota->liveBytes();
                    assert(!prepare(less) && id && nativeQuota->liveBytes() == live
                        && !native->validate());
                }
                auto wrong = b;
                --wrong.completedDepthInputs.maxTextureTexelBytes;
                const auto live = nativeQuota->liveBytes();
                assert(!prepare(wrong) && id && nativeQuota->liveBytes() == live);
                wrong = b;
                wrong.contractVersion = 1u;
                wrong.completedDepthInputs = {};
                assert(!prepare(wrong) && id && nativeQuota->liveBytes() == live);
                assert(!Closure::prepareOriginal(
                           ordinary.root, foreignInventory, device, b, nativeQuota, id)
                    && id && nativeQuota->liveBytes() == live);
                assert(native->ordinaryColorGraphicsExecutionDiagnostic());
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                    std::array{nativeQuota, quota}, [&]() noexcept {
                        return native->validate();
                    }));
                native.reset();
                assert(!nativeQuota->liveBytes());
                CpuQuotaFactoryFixture pressure(nativeQuota);
                const auto peak = pressure.measureSingleUse([] {},
                    [&] {
                        return prepare(b);
                    });
                pressure.verifySingleUse(
                    peak,
                    [] {},
                    [&] {
                        return prepare(b);
                    });
                assert(!nativeQuota->liveBytes());
                std::printf(
                    "BINDLESS_METAL_ORDINARY_DEPTH_CLOSURE16_OK fault=%d physical-proof selected-fragment exact-less-owners quota-rollback hot0 execution-closed\n",
                    fault);
                if (executionCheck)
                    executionCheck(ordinary, device, b, fault);
            }
        }
        assert(quota->liveBytes() == baseline);
    }
};
} // namespace arp::test

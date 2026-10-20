#pragma once

#include "cpu_quota_factory_fixture.h"
#include "rhi/backends/webgpu/webgpu_ordinary_resource_retention.h"
#include "webgpu_generic_command_projection_fixture.h"
#include "webgpu_ordinary_descriptor_fixture.h"

#include <cassert>
#include <cstdio>
#include <string_view>

namespace arp::test {

class WebGpuOrdinaryResourceFixture final {
public:
    static void verifyContract()
    {
        using namespace rhi_webgpu;
        WebGpuOrdinaryResourceBudget budget;
        assert(std::string_view(WebGpuOrdinaryResourceProjection::validateBudget(budget))
            == "RHI-WEBGPU-ORDINARY-RESOURCE-VERSION");
        budget.contractVersion = 1u;
        assert(std::string_view(WebGpuOrdinaryResourceProjection::validateBudget(budget))
            == "RHI-WEBGPU-ORDINARY-RESOURCE-OPT-IN");
        budget.prototypeOptIn = true;
        budget.retention = {
            rhi::kCommandRetentionBudgetContractVersion, true, 0u, 0u, 0u, 0u, metadataBytes};
        assert(!WebGpuOrdinaryResourceProjection::validateBudget(budget));
        WebGpuRhiResourceRegistry registry;
        const char* diagnostic = nullptr;
        auto quota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        const auto factory = [&] {
            return WebGpuOrdinaryResourceProjection::prepare(
                registry, nullptr, 0u, budget, quota, diagnostic);
        };
        auto empty = factory();
        assert(empty && empty->matches() && empty->resources().empty() && !empty->retainedBytes());
        empty.reset();
        assert(!quota->liveBytes());
        assert(!WebGpuOrdinaryResourceProjection::prepare(
            registry, nullptr, 0u, budget, {}, diagnostic));
        assert(std::string_view(diagnostic) == "RHI-COMMAND-RETENTION-CPU-QUOTA-MISMATCH");
        auto wrongQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes - 1u);
        assert(!WebGpuOrdinaryResourceProjection::prepare(
            registry, nullptr, 0u, budget, wrongQuota, diagnostic));
        assert(std::string_view(diagnostic) == "RHI-COMMAND-RETENTION-CPU-QUOTA-MISMATCH");
        const auto missing = rhi::allocateGpuResourceId();
        assert(!WebGpuOrdinaryResourceProjection::prepare(
            registry, &missing, 1u, budget, quota, diagnostic));
        assert(std::string_view(diagnostic) == "RHI-WEBGPU-ORDINARY-RESOURCE-MANIFEST-CAPACITY");
        budget.maxResources = 1u;
        assert(!WebGpuOrdinaryResourceProjection::prepare(
            registry, &missing, 1u, budget, quota, diagnostic));
        assert(std::string_view(diagnostic) == "RHI-WEBGPU-ORDINARY-RESOURCE-MISSING"
            && !quota->liveBytes());
        budget.maxResources = UINT32_MAX;
        assert(!factory()
            && std::string_view(diagnostic)
                == "RHI-WEBGPU-ORDINARY-RESOURCE-PREPARATION-ALLOCATION");
        assert(!quota->liveBytes());
        budget.maxResources = 0u;
        budget.retention.maxQueryPools = 1u;
        assert(std::string_view(WebGpuOrdinaryResourceProjection::validateBudget(budget))
            == "RHI-WEBGPU-ORDINARY-RESOURCE-QUERY-UNSUPPORTED");
        WebGpuOrdinaryResourceObservation unavailable;
        assert(!unavailable.matches());
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        WebGpuRhiRegisteredResource resource;
        assert(std::string_view(unavailable.capture(missing, resource, nullptr))
            == "RHI-WEBGPU-ORDINARY-RESOURCE-NATIVE-UNAVAILABLE");
#endif
    }

#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void verifyActual()
    {
        using namespace rhi_webgpu;
        using namespace rhi;
        auto registry = core::makeShared<WebGpuRhiResourceRegistry>();
        const BufferDesc bufferDesc{
            "ordinary original buffer with a label deliberately absent from cold snapshots",
            64u,
            ResourceUsage::UniformBuffer | ResourceUsage::StorageBuffer | ResourceUsage::CopyDst,
            MemoryUsage::CpuToGpu};
        TextureDesc textureDesc{
            "ordinary original two-mip texture with a label deliberately absent from cold snapshots",
            {4u, 4u, 1u},
            TextureFormat::Rgba8Unorm,
            TextureUsage::Sampled | TextureUsage::CopyDst};
        textureDesc.mipLevelCount = 2u;
        const auto textureBytes = estimateWebGpuRhiTextureBytes(textureDesc);
        const auto ledgerBytes = bufferDesc.sizeBytes + textureBytes;
        WebGpuRhiAllocator allocator({"ordinary resource original ledger", ledgerBytes, false});
        auto bufferAllocation =
            allocator.allocate({bufferDesc.label, bufferDesc.sizeBytes, bufferDesc.memory});
        auto textureAllocation =
            allocator.allocate({textureDesc.label, textureBytes, textureDesc.memory});
        assert(bufferAllocation.success && textureAllocation.success);
        auto buffer = createWebGpuRhiBuffer(
            allocateGpuResourceId(), bufferDesc, bufferAllocation, {true, true}, registry);
        auto texture = createWebGpuRhiTexture(
            allocateGpuResourceId(), textureDesc, textureAllocation, {true, true}, registry);
        assert(buffer && texture);
        registry->registerBuffer(*buffer);
        registry->registerTexture(*texture);
        const auto textureId = texture->id();
        TextureViewDesc viewDesc{"ordinary view original range with a long label",
            TextureFormat::Rgba8Unorm,
            TextureUsage::Sampled};
        auto viewA = createWebGpuRhiTextureView(
            allocateGpuResourceId(), textureId, *texture, viewDesc, {true, true}, registry);
        viewDesc.baseMipLevel = 1u;
        auto viewB = createWebGpuRhiTextureView(
            allocateGpuResourceId(), textureId, *texture, viewDesc, {true, true}, registry);
        auto sampler = createWebGpuRhiSampler(allocateGpuResourceId(),
            {"ordinary sampler original state with a long label"},
            {true, true},
            registry);
        assert(viewA && viewB && sampler);
        registry->registerTextureView(*viewA);
        registry->registerTextureView(*viewB);
        registry->registerSampler(*sampler);
        const std::array ids{buffer->id(), viewA->id(), viewB->id(), sampler->id()};
        WebGpuOrdinaryResourceBudget budget{1u,
            true,
            {kCommandRetentionBudgetContractVersion, true, 8u, 0u, 0u, 0u, metadataBytes},
            4u,
            2u,
            1u,
            ledgerBytes};
        auto quota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        const char* diagnostic = nullptr;
        const auto factory = [&] {
            return WebGpuOrdinaryResourceProjection::prepare(
                *registry, ids.data(), ids.size(), budget, quota, diagnostic);
        };
        CpuQuotaFactoryFixture admission(quota);
        const auto peak = admission.measure(factory);
        admission.verify(peak, factory);
        assert(!quota->liveBytes());
        {
            core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes());
            assert(pressure.charged() && !factory());
        }
        assert(!quota->liveBytes());
        budget.maxTextureViews = 1u;
        assert(!factory()
            && std::string_view(diagnostic)
                == "RHI-WEBGPU-ORDINARY-RESOURCE-NATIVE-OBJECT-CAPACITY");
        budget.maxTextureViews = 2u;
        budget.maxSamplers = 0u;
        assert(!factory()
            && std::string_view(diagnostic)
                == "RHI-WEBGPU-ORDINARY-RESOURCE-NATIVE-OBJECT-CAPACITY");
        budget.maxSamplers = 1u;
        budget.retention.maxResourceOwners = 7u;
        assert(
            !factory() && std::string_view(diagnostic) == "RHI-COMMAND-RETENTION-OWNER-CAPACITY");
        budget.retention.maxResourceOwners = 8u;
        budget.maxRetainedAllocationBytes = ledgerBytes - 1u;
        assert(!factory()
            && std::string_view(diagnostic)
                == "RHI-WEBGPU-ORDINARY-RESOURCE-ALLOCATION-BYTE-CAPACITY");
        budget.maxRetainedAllocationBytes = ledgerBytes;
        budget.maxResources = 3u;
        assert(!factory()
            && std::string_view(diagnostic) == "RHI-WEBGPU-ORDINARY-RESOURCE-MANIFEST-CAPACITY");
        budget.maxResources = 4u;
        const std::array duplicates{ids[1], ids[1]};
        assert(!WebGpuOrdinaryResourceProjection::prepare(
            *registry, duplicates.data(), duplicates.size(), budget, quota, diagnostic));
        assert(std::string_view(diagnostic) == "RHI-WEBGPU-ORDINARY-RESOURCE-MANIFEST-DUPLICATE"
            && !quota->liveBytes());
        WebGpuOrdinaryDescriptorFixture::verifyActual(
            registry, *buffer, *viewA, *sampler, ledgerBytes);
        auto nativeA = webGpuNativePayloadFor(*viewA);
        auto nativeSampler = webGpuNativePayloadFor(*sampler);
        auto nativeBuffer = webGpuNativePayloadFor(*buffer);
        auto nativeTexture = webGpuNativePayloadFor(*texture);
        bool unresolvedUsageRejected = false;
        RhiErrorHandler::get().setCallback(
            [](RhiSeverity severity, const char* id, void* state) {
                *static_cast<bool*>(state) = severity == RhiSeverity::Error
                    && std::string_view(id) == "RHI-WEBGPU-TEXTURE-VIEW-USAGE-UNRESOLVED";
            },
            &unresolvedUsageRejected);
        const auto beforeUnresolved = core::defaultCpuAllocator().stats().allocationCount;
        assert(!getOrCreateWebGpuNativeTextureViewPayload(*nativeTexture, {}));
        RhiErrorHandler::get().setCallback(nullptr);
        assert(unresolvedUsageRejected
            && core::defaultCpuAllocator().stats().allocationCount == beforeUnresolved);
        const auto key = nativeA->creationKey;
        ++nativeA->creationKey.baseMipLevel;
        assert(!factory()
            && std::string_view(diagnostic)
                == "RHI-WEBGPU-ORDINARY-RESOURCE-REGISTERED-IDENTITY-CHANGED");
        nativeA->creationKey = key;
        auto originalParent = nativeA->parentTexture;
        nativeA->parentTexture =
            std::shared_ptr<WebGpuNativeTexturePayload>(nativeBuffer, originalParent.get());
        assert(!factory()
            && std::string_view(diagnostic) == "RHI-WEBGPU-ORDINARY-RESOURCE-ORIGINAL-PARENT");
        nativeA->parentTexture = originalParent;
        const auto samplerDevice = nativeSampler->ownerDevice;
        nativeSampler->ownerDevice = nullptr;
        assert(!factory()
            && std::string_view(diagnostic)
                == "RHI-WEBGPU-ORDINARY-RESOURCE-REGISTERED-IDENTITY-CHANGED");
        nativeSampler->ownerDevice = samplerDevice;
        {
            // A factory can be passed an already-used allocation token. The new
            // projection must refuse two distinct backing objects under that charge.
            auto aliasTexture = createWebGpuRhiTexture(
                allocateGpuResourceId(), textureDesc, textureAllocation, {true, true}, registry);
            assert(aliasTexture);
            registry->registerTexture(*aliasTexture);
            viewDesc.baseMipLevel = 0u;
            auto aliasView = createWebGpuRhiTextureView(allocateGpuResourceId(),
                aliasTexture->id(),
                *aliasTexture,
                viewDesc,
                {true, true},
                registry);
            assert(aliasView);
            registry->registerTextureView(*aliasView);
            const std::array aliases{ids[1], aliasView->id()};
            assert(!WebGpuOrdinaryResourceProjection::prepare(
                *registry, aliases.data(), aliases.size(), budget, quota, diagnostic));
            assert(std::string_view(diagnostic) == "RHI-WEBGPU-ORDINARY-RESOURCE-ALLOCATION-ALIAS"
                && !quota->liveBytes());
        }
        // Direct capture must also reject a forged control block for the same native object.
        {
            auto registered = registry->find(ids[3]);
            assert(registered);
            registered->sampler =
                std::shared_ptr<WebGpuNativeSamplerPayload>(nativeBuffer, nativeSampler.get());
            WebGpuOrdinaryResourceObservation observed;
            assert(std::string_view(observed.capture(ids[3], *registered, nullptr))
                == "RHI-WEBGPU-ORDINARY-RESOURCE-ORIGINAL-OWNER");
        }
        {
            auto registered = registry->find(ids[1]);
            auto parent = registry->find(textureId);
            assert(registered && parent);
            registered->textureView =
                std::shared_ptr<WebGpuNativeTextureViewPayload>(nativeBuffer, nativeA.get());
            WebGpuOrdinaryResourceObservation observed;
            assert(std::string_view(observed.capture(ids[1], *registered, &*parent))
                == "RHI-WEBGPU-ORDINARY-RESOURCE-ORIGINAL-PARENT");
        }
        WebGpuOrdinaryTransferBudget transferBudget{1u,
            true,
            {kCommandRetentionBudgetContractVersion, true, 3u, 0u, 0u, 0u, metadataBytes},
            1u,
            bufferDesc.sizeBytes};
        auto transferQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        const auto transfers = [&] {
            return WebGpuOrdinaryTransferRetention::prepare(
                *registry, ids.data(), 1u, transferBudget, transferQuota, diagnostic);
        };
        auto commandProjection = WebGpuGenericCommandProjectionFixture::verifyResources(factory,
            transfers,
            ids,
            ledgerBytes,
            nativeBuffer.get(),
            nativeA.get(),
            webGpuNativePayloadFor(*viewB).get(),
            nativeSampler.get());
        auto retained = factory();
        assert(retained && !diagnostic && retained->matches()
            && retained->retainedBytes() == ledgerBytes);
        assert(retained->storage().resourceOwnerCount() == 8u);
        assert(retained->find(ids[1])->viewDesc().label.empty());
        assert(retained->find(ids[0])->backing().bufferDesc.label.empty());
        assert(retained->find(ids[1])->backing().textureDesc.label.empty());
        const auto before = core::defaultCpuAllocator().stats().allocationCount;
        {
            core::CpuMemoryQuotaScope scope(quota);
            core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes() - quota->liveBytes());
            assert(pressure.charged() && retained->matches() && retained->find(ids[2]));
            ++nativeA->creationKey.arrayLayerCount;
            assert(!retained->matches() && commandProjection->validate());
            nativeA->creationKey = key;
            nativeSampler->ownerDevice = nullptr;
            assert(!retained->matches() && commandProjection->validate());
            nativeSampler->ownerDevice = samplerDevice;
            nativeA->parentTexture =
                std::shared_ptr<WebGpuNativeTexturePayload>(nativeBuffer, originalParent.get());
            assert(!retained->matches() && commandProjection->validate());
            nativeA->parentTexture = originalParent;
            assert(retained->matches()
                && core::defaultCpuAllocator().stats().allocationCount == before);
        }
        // Destroying the public parent closes new binding, while the already captured graph remains alive.
        texture.reset();
        assert(!factory()
            && std::string_view(diagnostic) == "RHI-WEBGPU-ORDINARY-RESOURCE-ORIGINAL-PARENT");
        assert(retained->matches());
        bufferAllocation.releaseToken.reset();
        textureAllocation.releaseToken.reset();
        nativeA.reset();
        nativeSampler.reset();
        nativeBuffer.reset();
        nativeTexture.reset();
        buffer.reset();
        viewA.reset();
        viewB.reset();
        sampler.reset();
        assert(allocator.memoryStats().allocatedBytes == ledgerBytes && retained->matches()
            && !commandProjection->validate());
        retained.reset();
        assert(quota->liveBytes() && !commandProjection->validate());
        commandProjection.reset();
        assert(!quota->liveBytes() && !transferQuota->liveBytes());
        // originalParent is the one remaining explicitly caller-owned texture root.
        assert(allocator.memoryStats().allocatedBytes == textureBytes);
        originalParent.reset();
        assert(
            !allocator.memoryStats().allocatedBytes && !allocator.memoryStats().liveResourceCount);
        std::printf("BINDLESS_RHI_ORDINARY_RESOURCE_PROJECTION_OK %llu %llu 8 2 1\n",
            static_cast<unsigned long long>(peak),
            static_cast<unsigned long long>(ledgerBytes));
    }
#endif
private:
    static constexpr std::uint64_t metadataBytes = 1024u * 1024u;
};

} // namespace arp::test

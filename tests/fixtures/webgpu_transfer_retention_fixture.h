#pragma once

#include "cpu_quota_factory_fixture.h"
#include "rhi/backends/webgpu/webgpu_staging_upload_pool.h"
#include "rhi/backends/webgpu/webgpu_transfer_retention.h"
#include "rhi/frontend/transfer_validation.h"

#include <cassert>
#include <cstdio>

namespace arp::test {

class WebGpuTransferRetentionFixture final {
public:
    static void verifyBudgetGates()
    {
        using namespace rhi_webgpu;
        WebGpuOrdinaryTransferBudget budget;
        assert(std::string_view(WebGpuOrdinaryTransferRetention::validateBudget(budget))
            == "RHI-WEBGPU-TRANSFER-VERSION");
        budget.contractVersion = 1u;
        assert(std::string_view(WebGpuOrdinaryTransferRetention::validateBudget(budget))
            == "RHI-WEBGPU-TRANSFER-PROTOTYPE-OPT-IN");
        budget.prototypeOptIn = true;
        budget.retention = {
            rhi::kCommandRetentionBudgetContractVersion, true, 0u, 0u, 0u, 0u, 8192u};
        assert(!WebGpuOrdinaryTransferRetention::validateBudget(budget));
        WebGpuRhiResourceRegistry registry;
        const char* diagnostic = nullptr;
        auto quota = core::makeShared<core::CpuMemoryQuota>(budget.retention.maxCpuMetadataBytes);
        auto empty = WebGpuOrdinaryTransferRetention::prepare(
            registry, nullptr, 0u, budget, quota, diagnostic);
        assert(empty && !diagnostic && empty->resources().empty() && !empty->retainedBytes());
        assert(empty->storage().resourceOwnerCount() == 0u);
        empty.reset();
        assert(!quota->liveBytes());
        budget.maxTransferResources = UINT32_MAX;
        assert(!WebGpuOrdinaryTransferRetention::prepare(
            registry, nullptr, 0u, budget, quota, diagnostic));
        assert(std::string_view(diagnostic) == "RHI-WEBGPU-TRANSFER-PREPARATION-ALLOCATION"
            && !quota->liveBytes());
        budget.maxTransferResources = 0u;
        budget.retention.maxResourceOwners = UINT32_MAX;
        assert(!WebGpuOrdinaryTransferRetention::prepare(
            registry, nullptr, 0u, budget, quota, diagnostic));
        assert(std::string_view(diagnostic) == "RHI-COMMAND-RETENTION-PREPARATION-ALLOCATION-FAILED"
            && !quota->liveBytes());
        budget.retention.maxResourceOwners = 0u;
        const auto missing = rhi::allocateGpuResourceId();
        assert(!WebGpuOrdinaryTransferRetention::prepare(
            registry, &missing, 1u, budget, quota, diagnostic));
        assert(std::string_view(diagnostic) == "RHI-WEBGPU-TRANSFER-MANIFEST-CAPACITY");
        budget.maxTransferResources = 1u;
        assert(!WebGpuOrdinaryTransferRetention::prepare(
            registry, &missing, 1u, budget, quota, diagnostic));
        assert(std::string_view(diagnostic) == "RHI-WEBGPU-TRANSFER-RESOURCE-MISSING"
            && !quota->liveBytes());
        budget.retention.maxQueryPools = 1u;
        assert(std::string_view(WebGpuOrdinaryTransferRetention::validateBudget(budget))
            == "RHI-WEBGPU-TRANSFER-QUERY-UNSUPPORTED");
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        budget.retention.maxQueryPools = 0u;
        core::pmr::String nativeDiagnostic;
        const WebGpuOrdinaryTransferCompletionBudget completionBudget{
            1u, true, 1u, quota->limitBytes()};
        assert(!prepareWebGpuOrdinaryTransferCompletionOwner(
            completionBudget, quota, nativeDiagnostic));
        assert(nativeDiagnostic == "RHI-WEBGPU-TRANSFER-NATIVE-UNAVAILABLE");
        assert(!createWebGpuNativeOrdinaryTransferCommandPayload(
            registry, nullptr, 0u, budget, quota, {}, {}, nativeDiagnostic));
        assert(nativeDiagnostic == "RHI-WEBGPU-TRANSFER-NATIVE-UNAVAILABLE" && !quota->liveBytes());
#endif
        const rhi::TextureDesc texture{
            "padded row footprint", {2u, 2u, 1u}, rhi::TextureFormat::Rgba8Unorm};
        const rhi::TextureSubresourceRange range;
        using Validation = rhi::semantics::TextureTransferValidation;
        assert(Validation::copyLayoutValid(texture, range, texture.extent, 256u, 2u, 0u, 264u));
        assert(!Validation::copyLayoutValid(texture, range, texture.extent, 256u, 2u, 0u, 263u));
        assert(!Validation::copyLayoutValid(texture, range, texture.extent, 256u, 2u, 0u, 16u));
        assert(Validation::copyLayoutValid(texture, range, texture.extent, 256u, 2u, 8u, 272u));
        assert(!Validation::copyLayoutValid(
            texture, range, texture.extent, 256u, 2u, UINT64_MAX - 7u, UINT64_MAX));
    }

#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void verifyImmediateTextureUpload()
    {
        using namespace rhi_webgpu;
        using namespace rhi;
        auto registry = core::makeShared<WebGpuRhiResourceRegistry>();
        WebGpuRhiAllocator allocator({"immediate texture original ledger", 16u, false});
        TextureDesc desc{"immediate texture",
            {2u, 2u, 1u},
            TextureFormat::Rgba8Unorm,
            TextureUsage::CopySrc | TextureUsage::CopyDst,
            MemoryUsage::GpuOnly,
            ResourceState::CopyDst};
        auto allocation = allocator.allocate({desc.label, 16u, desc.memory});
        auto texture = createWebGpuRhiTexture(
            allocateGpuResourceId(), desc, allocation, {true, true}, registry);
        assert(texture);
        registry->registerTexture(*texture);
        std::array<std::uint8_t, 512u> pixels{};
        const auto device = emscripten_webgpu_get_device();
        auto limits = WGPU_LIMITS_INIT;
        assert(device && wgpuDeviceGetLimits(device, &limits) == WGPUStatus_Success);
        wgpuDeviceRelease(device);
        assert(limits.minUniformBufferOffsetAlignment
            && limits.maxUniformBufferBindingSize <= UINT32_MAX);
        const auto contract = semantics::nativeBackendCapabilityContract(Backend::WebGPU);
        for (bool immediate : {false, true}) {
            auto pool = core::makeShared<WebGpuRhiStagingUploadPool>(WebGpuRhiStagingUploadPoolDesc{
                "explicit CPU/JS copy budget", pixels.size(), 256u, immediate});
            auto stats = core::makeShared<WebGpuNativeCommandStats>();
            auto encoder = createWebGpuRhiCommandEncoder({},
                registry,
                {true, true},
                stats,
                contract,
                limits.minUniformBufferOffsetAlignment,
                static_cast<std::uint32_t>(limits.maxUniformBufferBindingSize),
                limits.minUniformBufferOffsetAlignment,
                false,
                pool);
            assert(encoder);
            const auto before = encoder->lastValidation().recordedCommandCount;
            auto pass = encoder->beginTransferPass({"immediate upload contract"});
            // Unproven generic uploads now reject at pass admission, before
            // exposing a wrapper or touching either declared staging policy.
            assert(!pass);
            const auto rejected = encoder->lastValidation();
            const auto ledger = pool->stats();
            assert(!ledger.liveBytes && !ledger.liveSliceCount
                && !ledger.liveImmediateQueueWriteBytes);
            assert(texture->state() == ResourceState::CopyDst
                && allocator.memoryStats().allocatedBytes == 16u);
            assert(!stats->nativeQueueSubmitCount);
            assert(
                !rejected.success && rejected.diagnostics == webGpuGenericRecordingUsageDiagnostic);
            assert(rejected.recordedCommandCount == before
                && !stats->nativeBufferToTextureCopyRecordCount);
            assert(!ledger.immediateQueueWriteCount && !ledger.peakBytes
                && !ledger.peakImmediateQueueWriteBytes);
        }
        std::puts("BINDLESS_RHI_GENERIC_TEXTURE_UPLOAD_ADMISSION_OK");
    }

    static void verifyNativeManifest()
    {
        using namespace rhi_webgpu;
        using namespace rhi;
        auto registry = core::makeShared<WebGpuRhiResourceRegistry>();
        WebGpuRhiAllocator allocator({"private transfer original allocation ledger", 80u, false});
        BufferDesc bufferDesc{"long transfer buffer label is never copied in observation",
            64u,
            ResourceUsage::CopySrc | ResourceUsage::CopyDst,
            MemoryUsage::CpuToGpu,
            ResourceState::CopySrc};
        TextureDesc textureDesc{"long transfer texture label is never copied in observation",
            {2u, 2u, 1u},
            TextureFormat::Rgba8Unorm,
            TextureUsage::CopySrc | TextureUsage::CopyDst,
            MemoryUsage::GpuOnly,
            ResourceState::CopyDst};
        auto bufferAllocation =
            allocator.allocate({bufferDesc.label, bufferDesc.sizeBytes, bufferDesc.memory});
        auto textureAllocation = allocator.allocate(
            {textureDesc.label, estimateWebGpuRhiTextureBytes(textureDesc), textureDesc.memory});
        assert(bufferAllocation.success && textureAllocation.success);
        auto buffer = createWebGpuRhiBuffer(
            allocateGpuResourceId(), bufferDesc, bufferAllocation, {true, true}, registry);
        auto texture = createWebGpuRhiTexture(
            allocateGpuResourceId(), textureDesc, textureAllocation, {true, true}, registry);
        assert(buffer && texture);
        registry->registerBuffer(*buffer);
        registry->registerTexture(*texture);
        const std::array ids{buffer->id(), texture->id()};
        WebGpuOrdinaryTransferBudget budget{1u,
            true,
            {kCommandRetentionBudgetContractVersion, true, 5u, 0u, 0u, 0u, 1024u * 1024u},
            2u,
            80u};
        auto quota = core::makeShared<core::CpuMemoryQuota>(budget.retention.maxCpuMetadataBytes);
        const char* diagnostic = nullptr;
        const auto factory = [&] {
            return WebGpuOrdinaryTransferRetention::prepare(
                *registry, ids.data(), ids.size(), budget, quota, diagnostic);
        };
        CpuQuotaFactoryFixture admission(quota);
        const auto peak = admission.measure(factory);
        admission.verify(peak, factory);
        assert(diagnostic && !quota->liveBytes());
        {
            core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes());
            assert(pressure.charged() && !factory() && quota->liveBytes() == quota->limitBytes());
        }
        assert(!quota->liveBytes());
        budget.retention.maxResourceOwners = 4u;
        assert(
            !factory() && std::string_view(diagnostic) == "RHI-COMMAND-RETENTION-OWNER-CAPACITY");
        budget.retention.maxResourceOwners = 5u;
        budget.maxTransferResources = 1u;
        assert(
            !factory() && std::string_view(diagnostic) == "RHI-WEBGPU-TRANSFER-MANIFEST-CAPACITY");
        budget.maxTransferResources = 2u;
        budget.maxRetainedResourceBytes = 79u;
        assert(!factory()
            && std::string_view(diagnostic) == "RHI-WEBGPU-TRANSFER-RESOURCE-BYTE-CAPACITY");
        budget.maxRetainedResourceBytes = 80u;
        const std::array duplicate{ids[0], ids[0]};
        assert(!WebGpuOrdinaryTransferRetention::prepare(
            *registry, duplicate.data(), duplicate.size(), budget, quota, diagnostic));
        assert(std::string_view(diagnostic) == "RHI-WEBGPU-TRANSFER-MANIFEST-DUPLICATE");
        assert(!quota->liveBytes());
        auto nativeBuffer = webGpuNativePayloadFor(*buffer);
        auto originalToken = nativeBuffer->allocationReleaseToken;
        nativeBuffer->allocationReleaseToken = textureAllocation.releaseToken;
        assert(!factory()
            && std::string_view(diagnostic) == "RHI-WEBGPU-TRANSFER-REGISTERED-IDENTITY-CHANGED");
        nativeBuffer->allocationReleaseToken =
            std::shared_ptr<void>(textureAllocation.releaseToken, originalToken.get());
        assert(!factory()
            && std::string_view(diagnostic) == "RHI-WEBGPU-TRANSFER-REGISTERED-IDENTITY-CHANGED");
        nativeBuffer->allocationReleaseToken = originalToken;
        auto retained = factory();
        assert(retained && !diagnostic && retained->retainedBytes() == 80u && retained->matches());
        assert(retained->storage().resourceOwnerCount() == 5u);
        const auto* observed = retained->find(ids[0]);
        assert(
            observed && observed->bufferDesc.label.empty() && observed->textureDesc.label.empty());
        const auto before = core::defaultCpuAllocator().stats().allocationCount;
        {
            core::CpuMemoryQuotaScope scope(quota);
            core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes() - quota->liveBytes());
            assert(pressure.charged() && retained->matches() && retained->find(ids[1]));
            nativeBuffer->allocationReleaseToken = textureAllocation.releaseToken;
            assert(!retained->matches());
            nativeBuffer->allocationReleaseToken = originalToken;
            buffer->sealWrites();
            assert(observed->writeSeal->transferWritesSealed());
            assert(core::defaultCpuAllocator().stats().allocationCount == before);
        }
        bufferAllocation.releaseToken.reset();
        textureAllocation.releaseToken.reset();
        nativeBuffer.reset();
        buffer.reset();
        texture.reset();
        assert(!registry->find(ids[0]) && !registry->find(ids[1]));
        assert(allocator.memoryStats().allocatedBytes == 80u && retained->matches());
        retained.reset();
        // originalToken is intentionally still a caller-owned allocation root.
        assert(allocator.memoryStats().allocatedBytes == 64u && !quota->liveBytes());
        originalToken.reset();
        assert(
            !allocator.memoryStats().allocatedBytes && !allocator.memoryStats().liveResourceCount);
        std::printf(
            "BINDLESS_RHI_TRANSFER_MANIFEST_OK %llu\n", static_cast<unsigned long long>(peak));
    }
#endif
};

} // namespace arp::test

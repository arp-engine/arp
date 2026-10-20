#pragma once

#include "rhi/backends/webgpu/webgpu_backend_objects.h"
#include "rhi/backends/webgpu/webgpu_native_payloads.h"
#include "rhi/frontend/command_encoder_base.h"

#include <cassert>
#include <cstdio>

namespace arp::test {
// Public negative admission; direct journal checks are metadata-only evidence.
class WebGpuPublicTransitionAdmissionFixture final {
public:
    explicit WebGpuPublicTransitionAdmissionFixture(rhi::GpuDevice& device)
        : buffer_(device.createBuffer({"unproven transition original buffer",
              64u,
              rhi::ResourceUsage::CopyDst | rhi::ResourceUsage::CopySrc,
              rhi::MemoryUsage::CpuToGpu})),
          texture_(device.createTexture({"unproven transition original texture",
              {4u, 4u, 1u},
              rhi::TextureFormat::Rgba8Unorm,
              rhi::TextureUsage::CopyDst | rhi::TextureUsage::Sampled,
              rhi::MemoryUsage::GpuOnly,
              rhi::ResourceState::Undefined,
              3u,
              2u})),
          encoder_(device.createCommandEncoder({"unproven transition encoder remains alive"}))
    {
        assert(buffer_ && texture_ && encoder_);
    }

    void verify()
    {
        using namespace rhi;
        const auto count = encoder_->lastValidation().recordedCommandCount;
        const auto bufferWrite = makeInferredResourceBarrier(
            buffer_->id(), {}, ResourceState::Undefined, ResourceState::CopyDst);
        const auto range = defaultTextureSubresourceRange(texture_->desc());
        const auto textureWrite = makeInferredResourceBarrier(
            texture_->id(), range, ResourceState::Undefined, ResourceState::CopyDst);
        verifyUniformStateMetadata();
        const auto reject = [&](const ResourceBarrierResult& result,
                                const ResourceBarrierDesc& desc,
                                bool texture) {
            const auto* expected = rhi_webgpu::webGpuNativeDeviceAvailable()
                ? rhi_webgpu::webGpuGenericRecordingUsageDiagnostic
                : "RHI-WEBGPU-BARRIER-NATIVE-PAYLOAD-MISSING";
            assert(!result.success && result.diagnostics == expected);
            assert(result.resource == desc.resource && result.before == desc.before
                && result.after == desc.after && result.subresourceAware == texture
                && result.stageAccessAware && result.sourceStages == desc.sourceStages
                && result.destinationStages == desc.destinationStages
                && result.sourceAccess == desc.sourceAccess
                && result.destinationAccess == desc.destinationAccess);
            const auto validation = encoder_->lastValidation();
            assert(!validation.success && validation.diagnostics == result.diagnostics
                && validation.recordedCommandCount == count);
            assert(buffer_->state() == ResourceState::Undefined
                && texture_->state() == ResourceState::Undefined);
            assert(texture_->subresourceState(range) == ResourceState::Undefined);
        };
        reject(encoder_->transition(*buffer_, bufferWrite), bufferWrite, false);
        reject(encoder_->transition(*buffer_, ResourceState::Undefined, ResourceState::CopyDst),
            bufferWrite,
            false);
        reject(encoder_->transition(*texture_, textureWrite), textureWrite, true);
        reject(encoder_->transition(
                   *texture_, range, ResourceState::Undefined, ResourceState::CopyDst),
            textureWrite,
            true);
        reject(encoder_->transition(*texture_, ResourceState::Undefined, ResourceState::CopyDst),
            textureWrite,
            true);
        {
            core::MutationJournal independent;
            assert(
                rhi_webgpu::transitionWebGpuRhiBuffer(*buffer_, bufferWrite, &independent).success);
            assert(rhi_webgpu::transitionWebGpuRhiTexture(*texture_, textureWrite, &independent)
                    .success);
            assert(buffer_->state() == ResourceState::CopyDst
                && texture_->state() == ResourceState::CopyDst);
        }
        assert(buffer_->state() == ResourceState::Undefined
            && texture_->state() == ResourceState::Undefined);
        reject(encoder_->transition(*buffer_, bufferWrite), bufferWrite, false);
        reject(encoder_->transition(*texture_, textureWrite), textureWrite, true);
        std::puts("BINDLESS_RHI_PUBLIC_TRANSITION_PRE_MUTATION_OK 7 journal0 state0 record0");
        const ResourceMemoryBarrierDesc bufferMemory{buffer_->id(),
            PipelineStage::Transfer,
            PipelineStage::ComputeShader,
            ResourceAccess::TransferWrite,
            ResourceAccess::ShaderRead};
        const ResourceMemoryBarrierDesc textureMemory{texture_->id(),
            PipelineStage::Transfer,
            PipelineStage::FragmentShader,
            ResourceAccess::TransferWrite,
            ResourceAccess::ShaderRead};
        const auto rejectMemory = [&](const ResourceBarrierResult& result,
                                      const ResourceMemoryBarrierDesc& desc) {
            const auto* expected = rhi_webgpu::webGpuNativeDeviceAvailable()
                ? rhi_webgpu::webGpuGenericRecordingUsageDiagnostic
                : "RHI-WEBGPU-MEMORY-BARRIER-NATIVE-PAYLOAD-MISSING";
            assert(!result.success && result.diagnostics == expected
                && result.resource == desc.resource && result.before == ResourceState::Undefined
                && result.after == ResourceState::Undefined && result.stageAccessAware
                && result.sourceStages == desc.sourceStages
                && result.destinationStages == desc.destinationStages
                && result.sourceAccess == desc.sourceAccess
                && result.destinationAccess == desc.destinationAccess);
            const auto validation = encoder_->lastValidation();
            assert(!validation.success && validation.diagnostics == result.diagnostics
                && validation.recordedCommandCount == count);
            assert(buffer_->state() == ResourceState::Undefined
                && texture_->state() == ResourceState::Undefined
                && texture_->subresourceState(range) == ResourceState::Undefined);
        };
        rejectMemory(encoder_->memoryBarrier(*buffer_, bufferMemory), bufferMemory);
        rejectMemory(encoder_->memoryBarrier(*texture_, textureMemory), textureMemory);
        std::puts("BINDLESS_RHI_PUBLIC_MEMORY_ADMISSION_OK 2 state0 record0");
        RenderPassDesc pass;
        pass.hasDepthStencilAttachment = true;
        pass.depthStencilAttachment.target = texture_->id();
        pass.depthStencilAttachment.format = TextureFormat::D32Float;
        pass.depthStencilAttachment.depthClearValue = 0.375f;
        // A negative authority test deliberately supplies no native depth view.
        // Admission must reject before inspecting or retaining that target.
        const auto checkPass = [&] {
            const auto validation = encoder_->lastValidation();
            const auto* expected = rhi_webgpu::webGpuNativeDeviceAvailable()
                ? rhi_webgpu::webGpuGenericRecordingUsageDiagnostic
                : "RHI-WEBGPU-PASS-NATIVE-PAYLOAD-MISSING";
            assert(!validation.success && validation.diagnostics == expected
                && validation.recordedCommandCount == count);
            assert(buffer_->state() == ResourceState::Undefined
                && texture_->state() == ResourceState::Undefined);
        };
        assert(!encoder_->beginRenderPass(pass));
        checkPass();
        assert(!encoder_->beginComputePass({"unproven public Compute"}));
        checkPass();
        assert(!encoder_->beginTransferPass({"unproven public Transfer"}));
        checkPass();
        std::puts("BINDLESS_RHI_PUBLIC_PASS_PRE_MUTATION_OK 3 state0 record0");
    }

private:
    void verifyUniformStateMetadata()
    {
        using namespace rhi;
        const auto full = defaultTextureSubresourceRange(texture_->desc());
        const auto write = makeInferredResourceBarrier(
            texture_->id(), full, ResourceState::Undefined, ResourceState::CopyDst);
        core::MutationJournal rejected;
        unsigned negatives = 0u, partialWrites = 0u;
        const auto payload = rhi_webgpu::webGpuNativePayloadFor(*texture_);
        const bool normalized = payload && payload->hasNormalizedRecordedState();
        const auto assertUniform = [&](ResourceState state) {
            assert(texture_->state() == state && texture_->subresourceState(full) == state);
            for (std::uint32_t mip = 0u; mip < full.mipLevelCount; ++mip)
                for (std::uint32_t layer = 0u; layer < full.arrayLayerCount; ++layer)
                    assert(texture_->subresourceState({mip, 1u, layer, 1u, full.aspect}) == state);
        };
        const auto reject = [&](const TextureSubresourceRange& range) {
            auto partial = write;
            partial.textureSubresources = range;
            const auto result =
                rhi_webgpu::transitionWebGpuRhiTexture(*texture_, partial, &rejected);
            if (normalized && !payload->normalizedRecordedState().geometry().validate(range)) {
                assert(result.success && !rejected.empty()
                    && texture_->subresourceState(range) == ResourceState::CopyDst);
                assert(!payload->recordedState().observe());
                rejected.rollback();
                assertUniform(ResourceState::Undefined);
                ++partialWrites;
                return;
            }
            const auto* expected = normalized
                ? payload->normalizedRecordedState().geometry().validate(range)
                : "RHI-WEBGPU-BARRIER-SUBRESOURCE-STATE-UNPROVEN";
            assert(!result.success && expected && result.diagnostics == expected
                && result.resource == partial.resource && result.before == partial.before
                && result.after == partial.after && result.subresourceAware
                && result.stageAccessAware && result.sourceStages == partial.sourceStages
                && result.destinationStages == partial.destinationStages
                && result.sourceAccess == partial.sourceAccess
                && result.destinationAccess == partial.destinationAccess && rejected.empty());
            assertUniform(ResourceState::Undefined);
            ++negatives;
        };
        for (std::uint32_t mip = 0u; mip < full.mipLevelCount; ++mip)
            for (std::uint32_t layer = 0u; layer < full.arrayLayerCount; ++layer)
                reject({mip, 1u, layer, 1u, full.aspect});
        reject({0u, full.mipLevelCount, 0u, 1u, full.aspect});
        reject({0u, 1u, 0u, full.arrayLayerCount, full.aspect});
        const TextureSubresourceRange malformed[]{{full.mipLevelCount, 1u, 0u, 1u, full.aspect},
            {0u, 0u, 0u, 1u, full.aspect},
            {UINT32_MAX, 1u, 0u, 1u, full.aspect},
            {0u, UINT32_MAX, 0u, 1u, full.aspect},
            {0u, 1u, full.arrayLayerCount, 1u, full.aspect},
            {0u, 1u, 0u, 0u, full.aspect},
            {0u, 1u, UINT32_MAX, 1u, full.aspect},
            {0u, 1u, 0u, UINT32_MAX, full.aspect},
            {0u, full.mipLevelCount, 0u, full.arrayLayerCount, TextureAspect::Depth},
            {0u,
                full.mipLevelCount,
                0u,
                full.arrayLayerCount,
                static_cast<TextureAspect>(UINT32_MAX)}};
        for (const auto& range : malformed)
            reject(range);
        // These positive operations prove CPU metadata and allocation behavior
        // only. Native recording and queue execution remain closed.
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        {
            core::MutationJournal accepted;
            assert(rhi_webgpu::transitionWebGpuRhiTexture(*texture_, write, &accepted).success
                && !accepted.empty() && rejected.empty());
            assertUniform(ResourceState::CopyDst);
        }
        assertUniform(ResourceState::Undefined);
        {
            core::MutationJournal accepted;
            assert(rhi_webgpu::transitionWebGpuRhiTexture(*texture_, write, &accepted).success);
            accepted.commit();
            assert(accepted.empty());
        }
        assertUniform(ResourceState::CopyDst);
        const auto read = makeInferredResourceBarrier(
            texture_->id(), full, ResourceState::CopyDst, ResourceState::ShaderRead);
        {
            core::MutationJournal accepted;
            assert(rhi_webgpu::transitionWebGpuRhiTexture(*texture_, read, &accepted).success);
            assertUniform(ResourceState::ShaderRead);
        }
        assertUniform(ResourceState::CopyDst);
        assert(rhi_webgpu::transitionWebGpuRhiTexture(*texture_,
            makeInferredResourceBarrier(
                texture_->id(), full, ResourceState::CopyDst, ResourceState::Undefined))
                .success);
        assertUniform(ResourceState::Undefined);
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations
            && negatives + partialWrites == 18u);
        {
            core::MutationJournal accepted, conflicting;
            assert(rhi_webgpu::transitionWebGpuRhiTexture(*texture_, write, &accepted).success);
            const auto result =
                rhi_webgpu::transitionWebGpuRhiTexture(*texture_, read, &conflicting);
            assert(!result.success && result.diagnostics == "RHI-RESOURCE-STATE-RECORDING-CONFLICT"
                && conflicting.empty());
        }
        assertUniform(ResourceState::Undefined);
        if (normalized) {
            assert(negatives == 10u && partialWrites == 8u);
            std::puts(
                "BINDLESS_RHI_NORMALIZED_STATE_ADMISSION_OK partial8 invalid10 exact-range pending-rollback whole-commit allocation0 public-native-gate-closed");
        } else
            std::puts(
                "BINDLESS_RHI_UNIFORM_STATE_ADMISSION_OK 18 partial0 journal0 whole-rollback-commit allocation0");
    }

    core::UniquePtr<rhi::GpuBuffer> buffer_;
    core::UniquePtr<rhi::GpuTexture> texture_;
    core::UniquePtr<rhi::CommandEncoder> encoder_;
};
} // namespace arp::test

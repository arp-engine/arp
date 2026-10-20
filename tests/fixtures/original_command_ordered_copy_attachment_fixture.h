#pragma once

#include "original_command_stream_copy_fixture.h"
#include "rhi/frontend/command_recording.h"
#include "rhi/semantics/command_lowering.h"

namespace arp::test {
class OriginalCommandOrderedCopyAttachmentFixture final {
public:
    static void verifyContract()
    {
        using C = rhi::OriginalCommandOrderedCopyAttachmentContract;
        using K = rhi::CommandUsageKind;
        using Cold = rhi::OriginalCommandNativeColdContract;
        for (auto kind : {K::BufferUpload,
                 K::TextureUpload,
                 K::TextureReadback,
                 K::Draw,
                 K::Dispatch,
                 K::Timestamp,
                 K::BufferBarrier,
                 K::PushDebugGroup,
                 K::AliasBarrier})
            assert(!C::supports(kind));
        for (auto kind : {K::BufferCopy,
                 K::TextureToBufferCopy,
                 K::BeginRenderPass,
                 K::TextureBarrier,
                 K::Finish})
            assert(C::supports(kind));
        rhi::ResourceBarrierDesc desc;
        desc.resource = {1u};
        desc.before = rhi::ResourceState::CopyDst;
        desc.after = rhi::ResourceState::ColorAttachment;
        desc.sourceStages = rhi::PipelineStage::Transfer;
        desc.sourceAccess = rhi::ResourceAccess::TransferWrite;
        desc.destinationStages = rhi::PipelineStage::ColorAttachmentOutput;
        desc.destinationAccess =
            rhi::ResourceAccess::ColorAttachmentRead | rhi::ResourceAccess::ColorAttachmentWrite;
        const rhi::TextureSubresourceRange whole{0u, 2u, 0u, 1u, rhi::TextureAspect::Color};
        desc.textureSubresources = whole;
        assert(!C::transition(desc, whole));
        desc.before = rhi::ResourceState::ColorAttachment;
        desc.after = rhi::ResourceState::CopySrc;
        desc.sourceStages = rhi::PipelineStage::ColorAttachmentOutput;
        desc.sourceAccess =
            rhi::ResourceAccess::ColorAttachmentRead | rhi::ResourceAccess::ColorAttachmentWrite;
        desc.destinationStages = rhi::PipelineStage::Transfer;
        desc.destinationAccess = rhi::ResourceAccess::TransferRead;
        assert(!C::transition(desc, whole));
        desc.sourceAccess = rhi::ResourceAccess::ColorAttachmentWrite;
        assert(C::transition(desc, whole));
        desc.sourceAccess =
            rhi::ResourceAccess::ColorAttachmentRead | rhi::ResourceAccess::ColorAttachmentWrite;
        desc.textureSubresources.mipLevelCount = 1u;
        assert(C::transition(desc, whole));
        desc.textureSubresources = whole;
        desc.textureSubresources.aspect = rhi::TextureAspect::Depth;
        assert(C::transition(desc, desc.textureSubresources));
        Cold::Budget b{2u, true, 9u, 1u, 1u, 2u, 3u, 3u, 1u, 1u, 1u, 67u, 65536u, 1u, 2u};
        Cold::Requirements r{
            2u, 9u, 1u, 1u, 2u, 3u, 3u, 1u, 1u, 1u, 1u, 1u, {1u, true, 1u, 8u, 8u}};
        assert(!Cold::validateRequirements(r, b));
        --b.maxRenderPasses;
        assert(Cold::validateRequirements(r, b));
        ++b.maxRenderPasses;
        r.contractVersion = 1u;
        assert(Cold::validateRequirements(r, b));
        r.contractVersion = 2u;
        b.contractVersion = 1u;
        assert(Cold::validateIntent(b));
        b.contractVersion = 2u;
        b.maxRenderPasses = 0u;
        r.renderPasses = 0u;
        assert(!Cold::validateRequirements(r, b));
        b.prototypeOptIn = false;
        assert(Cold::validateRequirements(r, b));
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto d =
                arp::rhi::semantics::OriginalCommandOrderedCopyAttachmentLowering::lowering(
                    backend);
            assert(d.diagnostic && !d.recordingAvailable && !d.executionAvailable);
            assert(d.nativeProjectionAvailable == (backend == rhi::Backend::WebGPU));
        }
        std::puts(
            "BINDLESS_RHI_ORDERED_COPY_ATTACHMENT_CONTRACT_OK cold2 explicit-opt-in variable-command-pass-count scopes3-3 queue1 loss1 existing-loss1 separate capacity1-8-8 whole-exact-scopes unsupported-families-closed four-outcomes");
    }
};
} // namespace arp::test

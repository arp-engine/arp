#pragma once
#include "rhi/frontend/command_transfer_validation.h"

#include <cassert>
#include <cstdio>

namespace arp::test {
class CompletedDepthWriteReturnContractFixture final {
public:
    static void verify()
    {
        using namespace rhi;
        using C = OriginalCompletedDepthWriteReturnContract;
        C::Intent intent{1u, true, PipelineStage::FragmentShader, 1u};
        TextureDesc texture;
        texture.extent = {2u, 2u, 1u};
        texture.format = TextureFormat::D32Float;
        texture.mipLevelCount = 2u;
        texture.usage = TextureUsage::Sampled | TextureUsage::RenderAttachment;
        ResourceBarrierDesc barrier{{1u},
            defaultTextureSubresourceRange(texture),
            ResourceState::ShaderRead,
            ResourceState::DepthAttachment};
        barrier.sourceStages = intent.previousStages;
        barrier.sourceAccess = ResourceAccess::ShaderRead;
        barrier.destinationStages = PipelineStage::DepthStencil;
        barrier.destinationAccess =
            ResourceAccess::DepthStencilRead | ResourceAccess::DepthStencilWrite;
        assert(C::selected(intent) && !C::selected({}));
        assert(!C::transition(barrier, texture, intent));
        assert(OriginalCommandOrderedCopyAttachmentContract::transition(
            barrier, barrier.textureSubresources));
        for (const auto version : {0u, 2u, UINT32_MAX}) {
            auto wrong = intent;
            wrong.contractVersion = version;
            assert(C::transition(barrier, texture, wrong));
        }
        for (const auto stages : {PipelineStage::None,
                 PipelineStage::Transfer,
                 PipelineStage::FragmentShader | PipelineStage::DepthStencil}) {
            auto wrong = intent;
            wrong.previousStages = stages;
            assert(C::transition(barrier, texture, wrong));
        }
        for (const auto stages : {PipelineStage::VertexShader,
                 PipelineStage::ComputeShader,
                 PipelineStage::VertexShader | PipelineStage::FragmentShader}) {
            auto selected = intent;
            selected.previousStages = stages;
            auto exact = barrier;
            exact.sourceStages = stages;
            assert(!C::transition(exact, texture, selected));
            assert(C::transition(barrier, texture, selected));
        }
        for (unsigned mutation = 0u; mutation < 12u; ++mutation) {
            auto b = barrier;
            auto t = texture;
            auto i = intent;
            switch (mutation) {
            case 0u:
                i.prototypeOptIn = false;
                break;
            case 1u:
                i.maxTransitions = 0u;
                break;
            case 2u:
                b.before = ResourceState::DepthAttachment;
                break;
            case 3u:
                b.after = ResourceState::CopySrc;
                break;
            case 4u:
                b.sourceAccess = ResourceAccess::ShaderWrite;
                break;
            case 5u:
                b.destinationStages = PipelineStage::FragmentShader;
                break;
            case 6u:
                b.destinationAccess = ResourceAccess::DepthStencilWrite;
                break;
            case 7u:
                --b.textureSubresources.mipLevelCount;
                break;
            case 8u:
                t.format = TextureFormat::Rgba8Unorm;
                break;
            case 9u:
                t.usage = TextureUsage::RenderAttachment;
                break;
            case 10u:
                t.sampleCount = 4u;
                break;
            default:
                t.arrayLayerCount = 2u;
                break;
            }
            assert(C::transition(b, t, i));
        }
        OriginalCommandCopyPassLoweringBudget budget{2u, true, 0u, 0u, 128u, 65536u, 1u};
        budget.depthWriteReturn = intent;
        assert(!OriginalCommandCopyPassLoweringContract::validateIntent(budget));
        for (const auto version : {0u, 1u, 3u, 4u, 5u}) {
            auto wrong = budget;
            wrong.contractVersion = version;
            assert(OriginalCommandCopyPassLoweringContract::validateIntent(wrong));
        }
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU, Backend::WebGL}) {
            const auto d =
                semantics::OriginalCompletedDepthWriteReturnLowering::lowering(backend, 1u);
            assert(d.transitionAvailable == (backend == Backend::WebGPU));
            assert(d.validatorOnly == (backend == Backend::Null));
            assert(bool(d.diagnostic) == (backend != Backend::WebGPU));
            assert(semantics::OriginalCompletedDepthWriteReturnLowering::lowering(backend, 2u)
                    .diagnostic);
        }
        std::puts(
            "BINDLESS_DEPTH_WRITE_RETURN1_CONTRACT_OK exact-stages whole-D32 bounded opt-in legacy-refused five-backend no-completion-grant");
    }
};
} // namespace arp::test

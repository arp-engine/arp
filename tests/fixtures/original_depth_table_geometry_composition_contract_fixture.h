#pragma once
#include "rhi/frontend/command_usage.h"
#include "rhi/semantics/command_lowering.h"

namespace arp::test {
// Workload contract over the existing L2 vocabulary, not a new execution profile.
class OriginalDepthTableGeometryCompositionContractFixture final {
public:
    using K = rhi::CommandUsageKind;
    using D = rhi::CommandReferenceDomain;
    static constexpr std::uint32_t commandCount = 31u, referenceCount = 22u, descriptorCount = 21u;
    static constexpr std::uint32_t geometryFirst = 13u, pipeline = 16u, table = 17u;
    static constexpr std::uint32_t renderBegin = 16u, pipelineBind = 17u, tableBind = 20u;
    static constexpr std::uint32_t indexedDraw = 24u, readbackCommand = 28u, destination = 11u;

    static constexpr auto commands() noexcept
    {
        return std::array<rhi::CommandUsage, commandCount>{{{K::BufferMemoryBarrier, 0u, 1u},
            {K::BufferMemoryBarrier, 1u, 1u},
            {K::BufferMemoryBarrier, 2u, 1u},
            {K::TextureBarrier, 3u, 1u},
            {K::BeginRenderPass, 4u, 1u},
            {K::EndRenderPass, 5u, 0u},
            {K::TextureBarrier, 5u, 1u},
            {K::TextureBarrier, 6u, 1u},
            {K::BeginRenderPass, 7u, 1u},
            {K::EndRenderPass, 8u, 0u},
            {K::TextureBarrier, 8u, 1u},
            {K::TextureBarrier, 9u, 1u},
            {K::BeginRenderPass, 10u, 1u},
            {K::EndRenderPass, 11u, 0u},
            {K::TextureBarrier, 11u, 1u},
            {K::TextureBarrier, 12u, 1u},
            {K::BeginRenderPass, 13u, 1u},
            {K::BindGraphicsPipeline, 14u, 1u},
            {K::SetViewport, 15u, 0u},
            {K::SetScissor, 15u, 0u},
            {K::BindRenderDescriptorTable, 15u, 1u},
            {K::BindIndexBuffer, 16u, 1u},
            {K::BindVertexBuffer, 17u, 1u},
            {K::BindVertexBuffer, 18u, 1u},
            {K::DrawIndexed, 19u, 0u},
            {K::EndRenderPass, 19u, 0u},
            {K::TextureBarrier, 19u, 1u},
            {K::BeginTransferPass, 20u, 0u},
            {K::TextureReadback, 20u, 2u},
            {K::EndTransferPass, 22u, 0u},
            {K::Finish, 22u, 0u}}};
    }

    static constexpr auto references() noexcept
    {
        return std::array<rhi::OriginalCommandUsage::Reference, referenceCount>{{{13u},
            {14u},
            {15u},
            {0u},
            {8u},
            {0u},
            {2u},
            {9u},
            {2u},
            {4u},
            {10u},
            {4u},
            {6u},
            {7u},
            {16u},
            {17u},
            {13u},
            {14u},
            {15u},
            {6u},
            {6u},
            {12u}}};
    }

    static void verify()
    {
        constexpr std::array<D, referenceCount> domains{{D::Buffer,
            D::Buffer,
            D::Buffer,
            D::Texture,
            D::TextureView,
            D::Texture,
            D::Texture,
            D::TextureView,
            D::Texture,
            D::Texture,
            D::TextureView,
            D::Texture,
            D::Texture,
            D::TextureView,
            D::Pipeline,
            D::DescriptorTable,
            D::Buffer,
            D::Buffer,
            D::Buffer,
            D::Texture,
            D::Texture,
            D::Readback}};
        const auto program = commands();
        const auto validate = [&](const auto& c, const auto& d) {
            return rhi::CommandUsageSequenceContract::validate(c.data(),
                c.size(),
                d.size(),
                [&](std::size_t i) {
                    return d[i];
                },
                {});
        };
        assert(!validate(program, domains));
        assert(
            !rhi::semantics::CommandRecordingOrder::validate(program.data(), program.size(), 0u));
        auto wrong = domains;
        wrong[15u] = D::Descriptor;
        assert(validate(program, wrong));
        wrong = domains;
        wrong[18u] = D::TextureView;
        assert(validate(program, wrong));
        wrong = domains;
        wrong.back() = D::Texture;
        assert(validate(program, wrong));
        auto truncated = program;
        truncated[readbackCommand].referenceCount = 1u;
        assert(validate(truncated, domains));
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto decision = rhi::semantics::OriginalCommandNativeClosureLowering::
                geometryReadbackLossBoundLowering(backend);
            assert(decision.resourceClosureAvailable == (backend == rhi::Backend::WebGPU));
            assert(!decision.executionAvailable);
        }
        assert(rhi::OriginalCommandNativeClosureContract::pipelineSourceVersion(7u, true, false)
            == 2u);
        assert(!rhi::OriginalCommandNativeClosureContract::pipelineSourceVersion(7u, true, true));
        std::puts(
            "ORIGINAL_TABLE_GEOMETRY_COMPOSITION_CONTRACT_OK commands31 references22 descriptors21 table-only indexedDraw1 vertex2 readback1 four-backends public-closed");
    }
};
} // namespace arp::test

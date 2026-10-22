#pragma once

#include "core/memory/cpu_allocator.h"
#include "rhi/frontend/command_model.h"

#include <array>
#include <cassert>
#include <initializer_list>

namespace arp::test {
class CommandUsageContractFixture final {
public:
    static void verify()
    {
        using K = rhi::CommandUsageKind;
        using D = rhi::CommandReferenceDomain;
        static_assert(static_cast<std::uint32_t>(K::BufferBarrier) == 0u);
        static_assert(static_cast<std::uint32_t>(K::Finish) == 43u);
        static_assert(static_cast<std::uint32_t>(D::Buffer) == 0u);
        static_assert(static_cast<std::uint32_t>(D::IndirectProcessor) == 12u);
        std::array<bool, static_cast<std::size_t>(K::Count)> covered{};
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        const auto signatures = [&](std::initializer_list<K> kinds,
                                    std::initializer_list<D> signature) {
            for (const auto kind : kinds) {
                const auto index = static_cast<std::size_t>(kind);
                assert(index < covered.size() && !covered[index]);
                covered[index] = true;
                const auto valid = [&](std::size_t count, auto&& map) {
                    return rhi::CommandUsageContract::validReferences(kind, count, map);
                };
                assert(valid(signature.size(), [&](std::size_t i) {
                    return signature.begin()[i];
                }));
                assert(!valid(signature.size() + 1u, [&](std::size_t i) {
                    return i < signature.size() ? signature.begin()[i] : D::Count;
                }));
                if (signature.size()) {
                    assert(!valid(signature.size() - 1u, [&](std::size_t i) {
                        return signature.begin()[i];
                    }));
                    for (std::size_t bad = 0u; bad < signature.size(); ++bad)
                        assert(!valid(signature.size(), [&](std::size_t i) {
                            return i == bad ? D::Count : signature.begin()[i];
                        }));
                }
            }
        };
        signatures({K::BufferBarrier,
                       K::BufferMemoryBarrier,
                       K::BindTessellationFactorBuffer,
                       K::BindVertexBuffer,
                       K::BindIndexBuffer,
                       K::DrawIndexedIndirect,
                       K::DispatchIndirect},
            {D::Buffer});
        signatures({K::TextureBarrier, K::TextureMemoryBarrier, K::RetainTexture}, {D::Texture});
        signatures({K::BufferCopy, K::DrawIndexedIndirectCount}, {D::Buffer, D::Buffer});
        signatures({K::BufferToTextureCopy}, {D::Buffer, D::Texture});
        signatures({K::TextureToBufferCopy}, {D::Texture, D::Buffer});
        signatures({K::TextureToTextureCopy}, {D::Texture, D::Texture});
        signatures({K::TextureUpload}, {D::Texture, D::Upload});
        signatures({K::BufferUpload}, {D::Buffer, D::Buffer, D::Upload});
        signatures({K::TextureReadback}, {D::Texture, D::Readback});
        signatures({K::RetainIndexedPipeline}, {D::Pipeline, D::BindlessRoot});
        signatures({K::RetainIndexedSnapshot,
                       K::ClearSnapshotFault,
                       K::PrepareBindlessGraphicsPass,
                       K::BindPreparedComputeBinding,
                       K::BindPreparedGraphicsBinding},
            {D::BindlessRoot});
        signatures({K::ClearFaultBuffer}, {D::Buffer, D::BindlessRoot});
        signatures({K::RecordIndirectProducer}, {D::IndirectProducer});
        signatures({K::RecordIndirectProcessor}, {D::IndirectProcessor});
        signatures({K::IndirectProcessorDraw}, {D::IndirectProcessor, D::BindlessRoot});
        signatures({K::BufferFaultReadback}, {D::Buffer, D::BindlessRoot, D::Readback});
        signatures({K::SnapshotFaultReadback}, {D::BindlessRoot, D::Readback});
        signatures({K::ProcessorFaultReadback}, {D::IndirectProcessor, D::Readback});
        signatures({K::BindGraphicsPipeline, K::BindComputePipeline}, {D::Pipeline});
        signatures({K::ExecuteRenderBundle}, {D::RenderBundle});
        signatures({K::ExecuteSecondaryCommandBuffer}, {D::SecondaryCommandBuffer});
        signatures(
            {K::BindRenderDescriptorTable, K::BindComputeDescriptorTable}, {D::DescriptorTable});
        signatures({K::BindRenderDescriptor, K::BindComputeDescriptor}, {D::Descriptor});
        signatures({K::Timestamp, K::PipelineStatistics}, {D::Query});
        signatures({K::PushConstants}, {D::Upload});
        signatures({K::EndRenderPass,
                       K::SetViewport,
                       K::SetScissor,
                       K::Draw,
                       K::DrawIndexed,
                       K::BeginComputePass,
                       K::EndComputePass,
                       K::BeginTransferPass,
                       K::EndTransferPass,
                       K::NextSubpass,
                       K::Dispatch,
                       K::PushDebugGroup,
                       K::PopDebugGroup,
                       K::InsertDebugMarker,
                       K::Finish},
            {});
        covered[static_cast<std::size_t>(K::AliasBarrier)] = true;
        for (const auto source : {D::Buffer, D::Texture})
            for (const auto destination : {D::Buffer, D::Texture}) {
                const auto valid = [&](std::size_t count, D a, D b) {
                    return rhi::CommandUsageContract::validReferences(
                        K::AliasBarrier, count, [&](std::size_t i) {
                            return i == 0u ? a : b;
                        });
                };
                assert(valid(2u, source, destination) && !valid(0u, source, destination)
                    && !valid(1u, source, destination) && !valid(3u, source, destination)
                    && !valid(2u, D::TextureView, destination) && !valid(2u, source, D::Count));
            }
        covered[static_cast<std::size_t>(K::LegacySizeOnlyBufferCopy)] = true;
        for (const auto count : {0u, 1u, 2u, UINT32_MAX})
            assert(!rhi::CommandUsageContract::validReferences(
                K::LegacySizeOnlyBufferCopy, count, [](std::size_t) {
                    assert(false);
                    return D::Buffer;
                }));
        covered[static_cast<std::size_t>(K::BeginRenderPass)] = true;
        assert(!rhi::CommandUsageContract::validReferences(K::BeginRenderPass, 0u, [](std::size_t) {
            return D::TextureView;
        }));
        assert(rhi::CommandUsageContract::validReferences(K::BeginRenderPass, 3u, [](std::size_t) {
            return D::TextureView;
        }));
        assert(
            !rhi::CommandUsageContract::validReferences(K::BeginRenderPass, 3u, [](std::size_t i) {
                return i == 2u ? D::Texture : D::TextureView;
            }));
        assert(!rhi::CommandUsageContract::validReferences(K::Count, 0u, [](std::size_t) {
            assert(false);
            return D::Count;
        }));
        assert(!rhi::CommandUsageContract::validReferences(
            static_cast<K>(UINT32_MAX), 0u, [](std::size_t) {
                assert(false);
                return D::Count;
            }));
        for (const auto value : covered)
            assert(value);
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
    }
};
} // namespace arp::test

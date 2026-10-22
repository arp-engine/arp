#pragma once

#include "core/memory/cpu_allocator.h"
#include "rhi/frontend/command_recording_order.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <initializer_list>
#include <string_view>

namespace arp::test {
class CommandRecordingOrderFixture final {
public:
    static void verify()
    {
        using K = rhi::CommandUsageKind;
        using Order = rhi::semantics::CommandRecordingOrder;
        using Scope = Order::Scope;
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        const auto accept = [](Order& order, K kind) {
            assert(!order.append(kind));
        };
        const auto reject = [](Order& order, K kind, std::string_view expected) {
            const auto scope = order.scope();
            const auto depth = order.debugDepth();
            const auto count = order.consumedCommands();
            const auto complete = order.complete();
            const auto* diagnostic = order.append(kind);
            assert(diagnostic && diagnostic == expected && order.scope() == scope
                && order.debugDepth() == depth && order.consumedCommands() == count
                && order.complete() == complete);
        };
        // An independent expected scope inventory fails when a new enum is
        // added without defining and testing its L2 context.
        std::array<bool, static_cast<std::size_t>(K::Count)> covered{};
        const auto scopes = [&](std::initializer_list<K> kinds, std::uint32_t expected) {
            for (const auto kind : kinds) {
                const auto index = static_cast<std::size_t>(kind);
                assert(!covered[index]);
                covered[index] = true;
                assert(Order::requiredScopes(kind) == expected);
            }
        };
        constexpr auto e = 1u << static_cast<unsigned>(Scope::Encoder),
                       r = 1u << static_cast<unsigned>(Scope::Render),
                       c = 1u << static_cast<unsigned>(Scope::Compute),
                       t = 1u << static_cast<unsigned>(Scope::Transfer);
        scopes({K::BufferBarrier,
                   K::TextureBarrier,
                   K::BufferMemoryBarrier,
                   K::TextureMemoryBarrier,
                   K::AliasBarrier,
                   K::BeginRenderPass,
                   K::BeginComputePass,
                   K::BeginTransferPass,
                   K::Finish,
                   K::ClearFaultBuffer,
                   K::ClearSnapshotFault,
                   K::RecordIndirectProducer,
                   K::RecordIndirectProcessor,
                   K::BufferFaultReadback,
                   K::SnapshotFaultReadback,
                   K::ProcessorFaultReadback,
                   K::PrepareBindlessGraphicsPass,
                   K::PushDebugGroup,
                   K::PopDebugGroup,
                   K::InsertDebugMarker},
            e);
        scopes({K::EndRenderPass,
                   K::BindGraphicsPipeline,
                   K::BindPreparedGraphicsBinding,
                   K::ExecuteRenderBundle,
                   K::ExecuteSecondaryCommandBuffer,
                   K::SetViewport,
                   K::SetScissor,
                   K::BindRenderDescriptor,
                   K::BindRenderDescriptorTable,
                   K::BindVertexBuffer,
                   K::BindIndexBuffer,
                   K::BindTessellationFactorBuffer,
                   K::Draw,
                   K::DrawIndexed,
                   K::DrawIndexedIndirect,
                   K::DrawIndexedIndirectCount,
                   K::IndirectProcessorDraw,
                   K::NextSubpass},
            r);
        scopes({K::EndComputePass,
                   K::BindComputePipeline,
                   K::BindPreparedComputeBinding,
                   K::BindComputeDescriptor,
                   K::BindComputeDescriptorTable,
                   K::Dispatch,
                   K::DispatchIndirect},
            c);
        scopes({K::EndTransferPass,
                   K::BufferCopy,
                   K::BufferToTextureCopy,
                   K::TextureToBufferCopy,
                   K::TextureToTextureCopy,
                   K::BufferUpload,
                   K::TextureUpload,
                   K::TextureReadback},
            t);
        scopes({K::Timestamp, K::PipelineStatistics, K::PushConstants}, r | c);
        scopes(
            {K::RetainTexture, K::RetainIndexedPipeline, K::RetainIndexedSnapshot}, e | r | c | t);
        scopes({K::LegacySizeOnlyBufferCopy}, 0u);
        for (const auto value : covered)
            assert(value);
        assert(
            !Order::requiredScopes(K::Count) && !Order::requiredScopes(static_cast<K>(UINT32_MAX)));

        // Exercise every context, not just the scope lookup. All valid
        // inherited state is established before each independent trial.
        for (const auto scope : {Scope::Encoder, Scope::Render, Scope::Compute, Scope::Transfer}) {
            for (std::size_t i = 0u; i < covered.size(); ++i) {
                const auto kind = static_cast<K>(i);
                Order order(2u);
                accept(order, K::PushDebugGroup);
                if (scope == Scope::Render) {
                    accept(order, K::BeginRenderPass);
                    accept(order, K::BindIndexBuffer);
                    accept(order, K::BindGraphicsPipeline);
                } else if (scope == Scope::Compute) {
                    accept(order, K::BeginComputePass);
                    accept(order, K::BindComputePipeline);
                } else if (scope == Scope::Transfer)
                    accept(order, K::BeginTransferPass);
                if (kind == K::LegacySizeOnlyBufferCopy)
                    reject(order, kind, "RHI-COMMAND-ORDER-KIND");
                else if (!(Order::requiredScopes(kind) & (1u << static_cast<unsigned>(scope))))
                    reject(order, kind, "RHI-COMMAND-ORDER-SCOPE");
                else if (kind == K::Finish)
                    reject(order, kind, "RHI-COMMAND-ORDER-DEBUG-UNCLOSED");
                else
                    accept(order, kind);
            }
        }
        Order order(2u);
        reject(order, K::Count, "RHI-COMMAND-ORDER-KIND");
        reject(order, static_cast<K>(UINT32_MAX), "RHI-COMMAND-ORDER-KIND");
        reject(order, K::PopDebugGroup, "RHI-COMMAND-ORDER-DEBUG-UNDERFLOW");
        accept(order, K::PushDebugGroup);
        accept(order, K::PushDebugGroup);
        reject(order, K::PushDebugGroup, "RHI-COMMAND-ORDER-DEBUG-CAPACITY");
        accept(order, K::BeginRenderPass);
        accept(order, K::BindVertexBuffer);
        accept(order, K::BindIndexBuffer);
        for (const auto kind : {K::SetViewport,
                 K::SetScissor,
                 K::BindRenderDescriptor,
                 K::BindRenderDescriptorTable,
                 K::PushConstants,
                 K::Draw,
                 K::DrawIndexed,
                 K::DrawIndexedIndirect,
                 K::DrawIndexedIndirectCount})
            reject(order, kind, "RHI-COMMAND-ORDER-PIPELINE");
        accept(order, K::BindGraphicsPipeline);
        accept(order, K::DrawIndexed);
        accept(order,
            K::NextSubpass); // Compatibility must be checked against the actual original pass/pipeline.
        accept(order, K::EndRenderPass);
        accept(order, K::BeginRenderPass);
        reject(order, K::DrawIndexed, "RHI-COMMAND-ORDER-PIPELINE");
        accept(order, K::BindGraphicsPipeline);
        for (const auto kind :
            {K::DrawIndexed, K::DrawIndexedIndirect, K::DrawIndexedIndirectCount})
            reject(order, kind, "RHI-COMMAND-ORDER-INDEX");
        accept(order, K::BindIndexBuffer);
        accept(order, K::DrawIndexed);
        accept(order, K::EndRenderPass);
        accept(order, K::BeginComputePass);
        for (const auto kind : {K::BindComputeDescriptor,
                 K::BindComputeDescriptorTable,
                 K::PushConstants,
                 K::Dispatch,
                 K::DispatchIndirect})
            reject(order, kind, "RHI-COMMAND-ORDER-PIPELINE");
        accept(order, K::BindComputePipeline);
        accept(order, K::Dispatch);
        accept(order, K::EndComputePass);
        accept(order, K::BeginComputePass);
        reject(order, K::Dispatch, "RHI-COMMAND-ORDER-PIPELINE");
        accept(order, K::EndComputePass);
        accept(order, K::BeginTransferPass);
        accept(order, K::BufferCopy);
        accept(order, K::EndTransferPass);
        reject(order, K::Finish, "RHI-COMMAND-ORDER-DEBUG-UNCLOSED");
        accept(order, K::PopDebugGroup);
        accept(order, K::PopDebugGroup);
        accept(order, K::Finish);
        assert(order.complete());
        for (std::size_t i = 0u; i < covered.size(); ++i)
            reject(order, static_cast<K>(i), "RHI-COMMAND-ORDER-CLOSED");
        Order disabled(0u);
        reject(disabled, K::PushDebugGroup, "RHI-COMMAND-ORDER-DEBUG-CAPACITY");
        Order maximal(UINT32_MAX);
        accept(maximal, K::PushDebugGroup);
        accept(maximal, K::PopDebugGroup);
        accept(maximal, K::Finish);

        const std::array<rhi::CommandUsage, 10> stream{{{K::PushDebugGroup},
            {K::BeginTransferPass},
            {K::BufferCopy},
            {K::EndTransferPass},
            {K::BeginComputePass},
            {K::BindComputePipeline},
            {K::Dispatch},
            {K::EndComputePass},
            {K::PopDebugGroup},
            {K::Finish}}};
        assert(!Order::validate(stream.data(), stream.size(), 1u));
        assert(std::string_view(Order::validate(stream.data(), stream.size() - 1u, 1u))
            == "RHI-COMMAND-ORDER-INCOMPLETE");
        assert(std::string_view(Order::validate(stream.data(), stream.size(), 0u))
            == "RHI-COMMAND-ORDER-DEBUG-CAPACITY");
        assert(std::string_view(Order::validate(nullptr, 1u, 1u)) == "RHI-COMMAND-ORDER-INPUT");
        assert(
            std::string_view(Order::validate(stream.data(), 0u, 1u)) == "RHI-COMMAND-ORDER-INPUT");
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        std::puts(
            "BINDLESS_RHI_COMMAND_ORDER_OK full-vocabulary scope pipeline index reset debug finish allocation0");
    }
};
} // namespace arp::test

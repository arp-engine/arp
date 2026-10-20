#pragma once

#include "rhi/frontend/command_model.h"

namespace arp::rhi::semantics {

// Structural L2 validation only. This dry consumer never grants native work,
// checks resource state, or substitutes for the original ordered usage ticket.
class CommandRecordingOrder final {
public:
    enum class Scope { Encoder, Render, Compute, Transfer, Finished };

private:
    static constexpr std::uint32_t scopeBit(Scope scope) noexcept
    {
        return 1u << static_cast<std::uint32_t>(scope);
    }

public:
    explicit CommandRecordingOrder(std::uint32_t maxDebugDepth) noexcept
        : maxDebugDepth_(maxDebugDepth)
    {}

    const char* append(CommandUsageKind kind) noexcept
    {
        using K = CommandUsageKind;
        if (scope_ == Scope::Finished)
            return "RHI-COMMAND-ORDER-CLOSED";
        const auto required = requiredScopes(kind);
        if (!required)
            return "RHI-COMMAND-ORDER-KIND";
        if (!(required & scopeBit(scope_)))
            return "RHI-COMMAND-ORDER-SCOPE";
        if (requiresPipeline(kind) && !pipeline_)
            return "RHI-COMMAND-ORDER-PIPELINE";
        if (requiresIndex(kind) && !index_)
            return "RHI-COMMAND-ORDER-INDEX";
        if (kind == K::PushDebugGroup && debugDepth_ == maxDebugDepth_)
            return "RHI-COMMAND-ORDER-DEBUG-CAPACITY";
        if (kind == K::PopDebugGroup && !debugDepth_)
            return "RHI-COMMAND-ORDER-DEBUG-UNDERFLOW";
        if (kind == K::Finish && debugDepth_)
            return "RHI-COMMAND-ORDER-DEBUG-UNCLOSED";
        switch (kind) {
        case K::BeginRenderPass:
            scope_ = Scope::Render;
            pipeline_ = index_ = false;
            break;
        case K::BeginComputePass:
            scope_ = Scope::Compute;
            pipeline_ = index_ = false;
            break;
        case K::BeginTransferPass:
            scope_ = Scope::Transfer;
            pipeline_ = index_ = false;
            break;
        case K::EndRenderPass:
        case K::EndComputePass:
        case K::EndTransferPass:
            scope_ = Scope::Encoder;
            pipeline_ = index_ = false;
            break;
        case K::BindGraphicsPipeline:
        case K::BindComputePipeline:
        case K::BindPreparedComputeBinding:
        case K::BindPreparedGraphicsBinding:
            pipeline_ = true;
            break;
        case K::BindIndexBuffer:
            index_ = true;
            break;
        case K::PushDebugGroup:
            ++debugDepth_;
            break;
        case K::PopDebugGroup:
            --debugDepth_;
            break;
        case K::Finish:
            scope_ = Scope::Finished;
            break;
        default:
            break;
        }
        ++consumed_;
        return nullptr;
    }

    bool complete() const noexcept { return scope_ == Scope::Finished; }

    Scope scope() const noexcept { return scope_; }

    std::size_t consumedCommands() const noexcept { return consumed_; }

    std::uint32_t debugDepth() const noexcept { return debugDepth_; }

    static const char* validate(
        const CommandUsage* commands, std::size_t count, std::uint32_t maxDebugDepth) noexcept
    {
        if (!commands || !count)
            return "RHI-COMMAND-ORDER-INPUT";
        CommandRecordingOrder order(maxDebugDepth);
        for (std::size_t i = 0u; i < count; ++i)
            if (const auto* id = order.append(commands[i].kind))
                return id;
        return order.complete() ? nullptr : "RHI-COMMAND-ORDER-INCOMPLETE";
    }

    static std::uint32_t requiredScopes(CommandUsageKind kind) noexcept
    {
        using K = CommandUsageKind;
        constexpr auto e = scopeBit(Scope::Encoder), r = scopeBit(Scope::Render),
                       c = scopeBit(Scope::Compute), t = scopeBit(Scope::Transfer);
        switch (kind) {
        case K::BufferBarrier:
        case K::TextureBarrier:
        case K::BufferMemoryBarrier:
        case K::TextureMemoryBarrier:
        case K::AliasBarrier:
        case K::BeginRenderPass:
        case K::BeginComputePass:
        case K::BeginTransferPass:
        case K::Finish:
        case K::ClearFaultBuffer:
        case K::ClearSnapshotFault:
        case K::RecordIndirectProducer:
        case K::RecordIndirectProcessor:
        case K::BufferFaultReadback:
        case K::SnapshotFaultReadback:
        case K::ProcessorFaultReadback:
        case K::PrepareBindlessGraphicsPass:
        case K::PushDebugGroup:
        case K::PopDebugGroup:
        case K::InsertDebugMarker:
            return e;
        case K::EndRenderPass:
        case K::BindGraphicsPipeline:
        case K::BindPreparedGraphicsBinding:
        case K::ExecuteRenderBundle:
        case K::ExecuteSecondaryCommandBuffer:
        case K::SetViewport:
        case K::SetScissor:
        case K::BindRenderDescriptor:
        case K::BindRenderDescriptorTable:
        case K::BindVertexBuffer:
        case K::BindIndexBuffer:
        case K::BindTessellationFactorBuffer:
        case K::Draw:
        case K::DrawIndexed:
        case K::DrawIndexedIndirect:
        case K::DrawIndexedIndirectCount:
        case K::IndirectProcessorDraw:
        case K::NextSubpass:
            return r;
        case K::EndComputePass:
        case K::BindComputePipeline:
        case K::BindPreparedComputeBinding:
        case K::BindComputeDescriptor:
        case K::BindComputeDescriptorTable:
        case K::Dispatch:
        case K::DispatchIndirect:
            return c;
        case K::BufferCopy:
        case K::BufferToTextureCopy:
        case K::TextureToBufferCopy:
        case K::TextureToTextureCopy:
        case K::BufferUpload:
        case K::TextureUpload:
        case K::TextureReadback:
        case K::EndTransferPass:
            return t;
        case K::Timestamp:
        case K::PipelineStatistics:
        case K::PushConstants:
            return r | c;
        // Ownership retention alone performs no GPU operation. Its complete
        // original source/lifetime semantics still need the separate contract.
        case K::RetainTexture:
        case K::RetainIndexedPipeline:
        case K::RetainIndexedSnapshot:
            return e | r | c | t;
        case K::LegacySizeOnlyBufferCopy:
        case K::Count:
            return 0u;
        }
        return 0u;
    }

private:
    static bool requiresPipeline(CommandUsageKind kind) noexcept
    {
        using K = CommandUsageKind;
        switch (kind) {
        case K::SetViewport:
        case K::SetScissor:
        case K::BindRenderDescriptor:
        case K::BindComputeDescriptor:
        case K::BindRenderDescriptorTable:
        case K::BindComputeDescriptorTable:
        case K::PushConstants:
        case K::Draw:
        case K::DrawIndexed:
        case K::DrawIndexedIndirect:
        case K::DrawIndexedIndirectCount:
        case K::Dispatch:
        case K::DispatchIndirect:
        case K::IndirectProcessorDraw:
            return true;
        default:
            return false;
        }
    }

    static bool requiresIndex(CommandUsageKind kind) noexcept
    {
        using K = CommandUsageKind;
        return kind == K::DrawIndexed || kind == K::DrawIndexedIndirect
            || kind == K::DrawIndexedIndirectCount;
    }

    const std::uint32_t maxDebugDepth_;
    Scope scope_ = Scope::Encoder;
    std::uint32_t debugDepth_ = 0u;
    bool pipeline_ = false, index_ = false;
    std::size_t consumed_ = 0u;
};
} // namespace arp::rhi::semantics

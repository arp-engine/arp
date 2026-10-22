#pragma once

#include "core/container/bounded_sequence.h"
#include "core/ownership/quota_bound_shared_identity.h"
#include "rhi/frontend/command_attachment_validation.h"
#include "rhi/frontend/command_model.h"
#include "rhi/frontend/pipeline_state_copy.h"
#include "rhi/gpu_commands.h"

#include <cstring>
#include <optional>
#include <string_view>
#include <variant>

// region Command scalar arguments
namespace arp::rhi {

// Explicit original scalar values. Resource/reference ownership, complete pass/
// transfer/dependency descriptors and variable input use their own contracts.
class CommandScalarArguments final {
public:
    struct None {};

    struct Draw {
        std::uint32_t vertices, instances, firstVertex, firstInstance;
    };

    struct DrawIndexed {
        std::uint32_t indices, instances, firstIndex;
        std::int32_t vertexOffset;
        std::uint32_t firstInstance;
    };

    struct DrawIndirect {
        std::uint64_t offset;
        std::uint32_t count, stride;
    };

    struct DrawIndirectCount {
        std::uint64_t offset, countOffset;
        std::uint32_t maxCount, stride;
    };

    struct Dispatch {
        std::uint32_t x, y, z;
    };

    struct DispatchIndirect {
        std::uint64_t offset;
    };

    struct VertexBinding {
        std::uint32_t slot;
        std::uint64_t offset;
    };

    struct IndexBinding {
        IndexFormat format;
        std::uint64_t offset;
    };

    struct TessellationBinding {
        std::uint64_t offset;
        std::uint32_t stride;
    };

    struct DescriptorBinding {
        std::uint32_t set;
    };

    struct Query {
        std::uint32_t index;
    };

    struct Push {
        ShaderStage stages;
        std::uint32_t offset, size;
    };

    struct Processor {
        bool consumerRequired;
    };

    template <class Value>
    static std::optional<CommandScalarArguments> capture(
        CommandUsageKind kind, const Value& value) noexcept
    {
        if (!accepts(kind, value))
            return {};
        return CommandScalarArguments(kind, value);
    }

    CommandUsageKind kind() const noexcept { return kind_; }

    template <class Value> const Value* get() const noexcept
    {
        return std::get_if<Value>(&values_);
    }

    template <class Value> bool matches(CommandUsageKind kind, const Value& value) const noexcept
    {
        const auto* original = get<Value>();
        return kind == kind_ && original && same(*original, value);
    }

    bool matches(const CommandScalarArguments& input) const noexcept
    {
        if (kind_ != input.kind_)
            return false;
        return std::visit(
            [&](const auto& value) {
                return matches(input.kind_, value);
            },
            input.values_);
    }

    template <class Value> bool write(CommandUsageKind kind, Value& output) const noexcept
    {
        const auto* original = get<Value>();
        if (kind != kind_ || !original)
            return false;
        output = *original;
        return true;
    }

    static bool accepts(CommandUsageKind kind, None) noexcept
    {
        using K = CommandUsageKind;
        switch (kind) {
        // These have no additional scalar beyond their separate complete
        // original descriptor/source. Accepting None does not prove that source.
        case K::BeginRenderPass:
        case K::BufferBarrier:
        case K::TextureBarrier:
        case K::BufferMemoryBarrier:
        case K::TextureMemoryBarrier:
        case K::AliasBarrier:
        case K::BufferCopy:
        case K::BufferToTextureCopy:
        case K::TextureToBufferCopy:
        case K::TextureToTextureCopy:
        case K::BufferUpload:
        case K::TextureUpload:
        case K::TextureReadback:
        // Text-only inputs are copied by the independently bounded variable store.
        case K::BeginComputePass:
        case K::BeginTransferPass:
        case K::PushDebugGroup:
        case K::InsertDebugMarker:
        case K::EndRenderPass:
        case K::EndComputePass:
        case K::EndTransferPass:
        case K::BindGraphicsPipeline:
        case K::BindComputePipeline:
        case K::ExecuteRenderBundle:
        case K::ExecuteSecondaryCommandBuffer:
        case K::NextSubpass:
        case K::PopDebugGroup:
        case K::Finish:
        case K::RetainTexture:
        case K::RetainIndexedPipeline:
        case K::RetainIndexedSnapshot:
        case K::ClearFaultBuffer:
        case K::ClearSnapshotFault:
        case K::RecordIndirectProducer:
        case K::IndirectProcessorDraw:
        case K::BufferFaultReadback:
        case K::SnapshotFaultReadback:
        case K::ProcessorFaultReadback:
        case K::PrepareBindlessGraphicsPass:
        case K::BindPreparedComputeBinding:
        case K::BindPreparedGraphicsBinding:
            return true;
        default:
            return false;
        }
    }

private:
    template <class Value>
    CommandScalarArguments(CommandUsageKind kind, const Value& value) noexcept
        : kind_(kind), values_(value)
    {}

    static bool accepts(CommandUsageKind k, Draw) noexcept { return k == CommandUsageKind::Draw; }

    static bool accepts(CommandUsageKind k, DrawIndexed) noexcept
    {
        return k == CommandUsageKind::DrawIndexed;
    }

    static bool accepts(CommandUsageKind k, DrawIndirect) noexcept
    {
        return k == CommandUsageKind::DrawIndexedIndirect;
    }

    static bool accepts(CommandUsageKind k, DrawIndirectCount) noexcept
    {
        return k == CommandUsageKind::DrawIndexedIndirectCount;
    }

    static bool accepts(CommandUsageKind k, Dispatch) noexcept
    {
        return k == CommandUsageKind::Dispatch;
    }

    static bool accepts(CommandUsageKind k, DispatchIndirect) noexcept
    {
        return k == CommandUsageKind::DispatchIndirect;
    }

    static bool accepts(CommandUsageKind k, VertexBinding) noexcept
    {
        return k == CommandUsageKind::BindVertexBuffer;
    }

    static bool accepts(CommandUsageKind k, IndexBinding) noexcept
    {
        return k == CommandUsageKind::BindIndexBuffer;
    }

    static bool accepts(CommandUsageKind k, TessellationBinding) noexcept
    {
        return k == CommandUsageKind::BindTessellationFactorBuffer;
    }

    static bool accepts(CommandUsageKind k, DescriptorBinding) noexcept
    {
        return k == CommandUsageKind::BindRenderDescriptor
            || k == CommandUsageKind::BindComputeDescriptor
            || k == CommandUsageKind::BindRenderDescriptorTable
            || k == CommandUsageKind::BindComputeDescriptorTable;
    }

    static bool accepts(CommandUsageKind k, Query) noexcept
    {
        return k == CommandUsageKind::Timestamp || k == CommandUsageKind::PipelineStatistics;
    }

    static bool accepts(CommandUsageKind k, Push) noexcept
    {
        return k == CommandUsageKind::PushConstants;
    }

    static bool accepts(CommandUsageKind k, Processor) noexcept
    {
        return k == CommandUsageKind::RecordIndirectProcessor;
    }

    static bool accepts(CommandUsageKind k, const ViewportState&) noexcept
    {
        return k == CommandUsageKind::SetViewport;
    }

    static bool accepts(CommandUsageKind k, const ScissorState&) noexcept
    {
        return k == CommandUsageKind::SetScissor;
    }

    static bool same(None, None) noexcept { return true; }

    static bool same(const Draw& a, const Draw& b) noexcept
    {
        return a.vertices == b.vertices && a.instances == b.instances
            && a.firstVertex == b.firstVertex && a.firstInstance == b.firstInstance;
    }

    static bool same(const DrawIndexed& a, const DrawIndexed& b) noexcept
    {
        return a.indices == b.indices && a.instances == b.instances && a.firstIndex == b.firstIndex
            && a.vertexOffset == b.vertexOffset && a.firstInstance == b.firstInstance;
    }

    static bool same(const DrawIndirect& a, const DrawIndirect& b) noexcept
    {
        return a.offset == b.offset && a.count == b.count && a.stride == b.stride;
    }

    static bool same(const DrawIndirectCount& a, const DrawIndirectCount& b) noexcept
    {
        return a.offset == b.offset && a.countOffset == b.countOffset && a.maxCount == b.maxCount
            && a.stride == b.stride;
    }

    static bool same(const Dispatch& a, const Dispatch& b) noexcept
    {
        return a.x == b.x && a.y == b.y && a.z == b.z;
    }

    static bool same(const DispatchIndirect& a, const DispatchIndirect& b) noexcept
    {
        return a.offset == b.offset;
    }

    static bool same(const VertexBinding& a, const VertexBinding& b) noexcept
    {
        return a.slot == b.slot && a.offset == b.offset;
    }

    static bool same(const IndexBinding& a, const IndexBinding& b) noexcept
    {
        return a.format == b.format && a.offset == b.offset;
    }

    static bool same(const TessellationBinding& a, const TessellationBinding& b) noexcept
    {
        return a.offset == b.offset && a.stride == b.stride;
    }

    static bool same(const DescriptorBinding& a, const DescriptorBinding& b) noexcept
    {
        return a.set == b.set;
    }

    static bool same(const Query& a, const Query& b) noexcept { return a.index == b.index; }

    static bool same(const Push& a, const Push& b) noexcept
    {
        return a.stages == b.stages && a.offset == b.offset && a.size == b.size;
    }

    static bool same(const Processor& a, const Processor& b) noexcept
    {
        return a.consumerRequired == b.consumerRequired;
    }

    static bool same(const ViewportState& a, const ViewportState& b) noexcept
    {
        static_assert(sizeof(ViewportState) == 6u * sizeof(float));
        return !std::memcmp(&a, &b, sizeof(ViewportState));
    }

    static bool same(const ScissorState& a, const ScissorState& b) noexcept
    {
        return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
    }

    CommandUsageKind kind_;
    std::variant<None,
        Draw,
        DrawIndexed,
        DrawIndirect,
        DrawIndirectCount,
        Dispatch,
        DispatchIndirect,
        VertexBinding,
        IndexBinding,
        TessellationBinding,
        DescriptorBinding,
        Query,
        Push,
        Processor,
        ViewportState,
        ScissorState>
        values_;
};

// endregion Command scalar arguments

// region Command resource dependency arguments

// Complete original field storage; deliberately separate from state/range/
// stage/access/lowering validation and original resource ownership admission.
class ResourceDependencyArguments final {
public:
    static std::optional<ResourceDependencyArguments> capture(
        CommandUsageKind kind, const ResourceBarrierDesc& d) noexcept
    {
        if (kind != CommandUsageKind::BufferBarrier && kind != CommandUsageKind::TextureBarrier)
            return {};
        return ResourceDependencyArguments(kind, d);
    }

    static std::optional<ResourceDependencyArguments> capture(
        CommandUsageKind kind, const ResourceMemoryBarrierDesc& d) noexcept
    {
        if (kind != CommandUsageKind::BufferMemoryBarrier
            && kind != CommandUsageKind::TextureMemoryBarrier)
            return {};
        return ResourceDependencyArguments(kind, d);
    }

    static ResourceDependencyArguments capture(const AliasingBarrierDesc& d) noexcept
    {
        return ResourceDependencyArguments(CommandUsageKind::AliasBarrier, d);
    }

    CommandUsageKind kind() const noexcept { return kind_; }

    GpuResourceId firstResource() const noexcept
    {
        return std::visit(
            [](const auto& d) {
                using T = std::decay_t<decltype(d)>;
                if constexpr (std::is_same_v<T, AliasingBarrierDesc>)
                    return d.beforeResource;
                else
                    return d.resource;
            },
            values_);
    }

    GpuResourceId secondResource() const noexcept
    {
        const auto* alias = std::get_if<AliasingBarrierDesc>(&values_);
        return alias ? alias->afterResource : GpuResourceId{};
    }

    template <class Desc> bool matches(CommandUsageKind kind, const Desc& d) const noexcept
    {
        const auto* original = std::get_if<Desc>(&values_);
        return kind == kind_ && original && same(*original, d);
    }

    bool matches(const ResourceDependencyArguments& input) const noexcept
    {
        return std::visit(
            [&](const auto& desc) {
                return matches(input.kind_, desc);
            },
            input.values_);
    }

    template <class Desc> bool write(CommandUsageKind kind, Desc& d) const noexcept
    {
        const auto* original = std::get_if<Desc>(&values_);
        if (kind != kind_ || !original)
            return false;
        d = *original;
        return true;
    }

private:
    template <class Desc>
    ResourceDependencyArguments(CommandUsageKind kind, const Desc& d) noexcept
        : kind_(kind), values_(d)
    {}

    static bool same(const ResourceBarrierDesc& a, const ResourceBarrierDesc& b) noexcept
    {
        return a.resource == b.resource && a.textureSubresources == b.textureSubresources
            && a.before == b.before && a.after == b.after && a.sourceStages == b.sourceStages
            && a.destinationStages == b.destinationStages && a.sourceAccess == b.sourceAccess
            && a.destinationAccess == b.destinationAccess;
    }

    static bool same(
        const ResourceMemoryBarrierDesc& a, const ResourceMemoryBarrierDesc& b) noexcept
    {
        return a.resource == b.resource && a.sourceStages == b.sourceStages
            && a.destinationStages == b.destinationStages && a.sourceAccess == b.sourceAccess
            && a.destinationAccess == b.destinationAccess;
    }

    static bool same(const AliasingBarrierDesc& a, const AliasingBarrierDesc& b) noexcept
    {
        return a.beforeResource == b.beforeResource && a.afterResource == b.afterResource
            && a.aliasGroup == b.aliasGroup && a.sourceStages == b.sourceStages
            && a.destinationStages == b.destinationStages && a.sourceAccess == b.sourceAccess
            && a.destinationAccess == b.destinationAccess && a.sourceQueueRole == b.sourceQueueRole
            && a.destinationQueueRole == b.destinationQueueRole
            && a.ownershipHandoff == b.ownershipHandoff;
    }

    CommandUsageKind kind_;
    std::variant<ResourceBarrierDesc, ResourceMemoryBarrierDesc, AliasingBarrierDesc> values_;
};

// endregion Command resource dependency arguments

// region Command render pass arguments

struct RenderPassArgumentsBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxColorAttachments = 0u, maxSubpasses = 0u;
    std::uint32_t maxSubpassAttachmentIndices = 0u, maxDependencies = 0u;
    std::uint64_t maxTextBytes = 0u, maxCpuMetadataBytes = 0u;
};

// Immutable complete input storage. It provides no native owner/state proof or
// execution permission; consumers must still validate their full L2 contract.
class RenderPassArguments final : public core::QuotaBoundSharedObjectIdentity {
    static_assert(
        sizeof(Color) == 4u * sizeof(float), "clear component representation must have no padding");

    class ConstructionKey {
        friend class RenderPassArguments;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    RenderPassArguments(ConstructionKey, const RenderPassDesc& source)
        : desc_(RenderPassStateCopy::copy(source))
    {}

    static const char* validateBudget(const RenderPassArgumentsBudget& b) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-RENDER-PASS-ARGUMENTS-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-RENDER-PASS-ARGUMENTS-OPT-IN";
        if (!b.maxCpuMetadataBytes)
            return "RHI-RENDER-PASS-ARGUMENTS-BUDGET";
        return nullptr;
    }

    static const char* validateInput(
        const RenderPassDesc& source, const RenderPassArgumentsBudget& b) noexcept
    {
        auto remaining = b;
        return consumeInputCapacity(source, remaining);
    }

    // Reuse the local snapshot bounds in complete graph composition. On any
    // failure preserve all caller capacities; successful consumption is exact.
    const char* consumeCapacity(RenderPassArgumentsBudget& remaining) const noexcept
    {
        return consumeInputCapacity(desc_, remaining);
    }

    static const char* consumeInputCapacity(
        const RenderPassDesc& source, RenderPassArgumentsBudget& b) noexcept
    {
        if (const auto* id = validateBudget(b))
            return id;
        if (source.colorAttachments.size() > b.maxColorAttachments
            || source.subpasses.size() > b.maxSubpasses
            || source.subpassDependencies.size() > b.maxDependencies)
            return "RHI-RENDER-PASS-ARGUMENTS-COUNT";
        std::size_t indices = 0u;
        for (const auto& subpass : source.subpasses) {
            for (const auto count :
                {subpass.colorAttachmentIndices.size(), subpass.inputAttachmentIndices.size()}) {
                if (count > b.maxSubpassAttachmentIndices - indices)
                    return "RHI-RENDER-PASS-ARGUMENTS-COUNT";
                indices += count;
            }
        }
        std::uint64_t text = 0u;
        const auto fits = [&](std::size_t count) {
            if (count > b.maxTextBytes - text)
                return false;
            text += count;
            return true;
        };
        if (!fits(source.colorTargetName.size()) || !fits(source.label.size())
            || !fits(source.depthStencilAttachment.label.size()))
            return "RHI-RENDER-PASS-ARGUMENTS-TEXT";
        for (const auto& color : source.colorAttachments)
            if (!fits(color.label.size()))
                return "RHI-RENDER-PASS-ARGUMENTS-TEXT";
        for (const auto& color : source.colorAttachments)
            if (!semantics::validAttachmentLoadOp(color.loadOp)
                || !semantics::validAttachmentStoreOp(color.storeOp))
                return "RHI-RENDER-PASS-ATTACHMENT-OP-INVALID";
        if (source.hasDepthStencilAttachment)
            if (const auto* id = semantics::DepthStencilAttachmentInputContract::diagnostic(
                    source.depthStencilAttachment))
                return id;
        b.maxColorAttachments -= static_cast<std::uint32_t>(source.colorAttachments.size());
        b.maxSubpasses -= static_cast<std::uint32_t>(source.subpasses.size());
        b.maxDependencies -= static_cast<std::uint32_t>(source.subpassDependencies.size());
        b.maxSubpassAttachmentIndices -= static_cast<std::uint32_t>(indices);
        b.maxTextBytes -= text;
        return nullptr;
    }

    static std::shared_ptr<const RenderPassArguments> prepareOriginal(const RenderPassDesc& source,
        const RenderPassArgumentsBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        diagnostic = validateInput(source, budget);
        if (diagnostic)
            return {};
        if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes) {
            diagnostic = "RHI-RENDER-PASS-ARGUMENTS-QUOTA";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope scope(quota);
            return shareOriginal(core::makeUnique<RenderPassArguments>(ConstructionKey{}, source));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-RENDER-PASS-ARGUMENTS-ALLOCATION";
            return {};
        } catch (const std::length_error&) {
            diagnostic = "RHI-RENDER-PASS-ARGUMENTS-ALLOCATION";
            return {};
        }
    }

    const RenderPassDesc& desc() const noexcept { return desc_; }

    bool matches(const RenderPassDesc& input) const noexcept
    {
        if (input.colorTargetName != desc_.colorTargetName || input.label != desc_.label
            || input.hasDepthStencilAttachment != desc_.hasDepthStencilAttachment
            || input.layeredRendering != desc_.layeredRendering
            || input.colorAttachments.size() != desc_.colorAttachments.size()
            || input.subpasses.size() != desc_.subpasses.size()
            || input.subpassDependencies.size() != desc_.subpassDependencies.size())
            return false;
        for (std::size_t i = 0u; i < input.colorAttachments.size(); ++i) {
            const auto& a = input.colorAttachments[i];
            const auto& b = desc_.colorAttachments[i];
            if (a.label != b.label || a.target != b.target || a.format != b.format
                || a.loadOp != b.loadOp || a.storeOp != b.storeOp
                || a.hasResolveTarget != b.hasResolveTarget || a.resolveTarget != b.resolveTarget
                || std::memcmp(&a.clearColor, &b.clearColor, sizeof(Color)))
                return false;
        }
        const auto& a = input.depthStencilAttachment;
        const auto& b = desc_.depthStencilAttachment;
        if (a.label != b.label || a.target != b.target || a.format != b.format
            || a.depthLoadOp != b.depthLoadOp || a.depthStoreOp != b.depthStoreOp
            || a.stencilLoadOp != b.stencilLoadOp || a.stencilStoreOp != b.stencilStoreOp
            || std::memcmp(&a.depthClearValue, &b.depthClearValue, sizeof(float))
            || a.stencilClearValue != b.stencilClearValue || a.depthReadOnly != b.depthReadOnly
            || a.stencilReadOnly != b.stencilReadOnly)
            return false;
        for (std::size_t i = 0u; i < input.subpasses.size(); ++i) {
            const auto& x = input.subpasses[i];
            const auto& y = desc_.subpasses[i];
            if (x.colorAttachmentIndices != y.colorAttachmentIndices
                || x.inputAttachmentIndices != y.inputAttachmentIndices
                || x.useDepthStencilAttachment != y.useDepthStencilAttachment
                || x.readDepthInputAttachment != y.readDepthInputAttachment)
                return false;
        }
        for (std::size_t i = 0u; i < input.subpassDependencies.size(); ++i) {
            const auto& x = input.subpassDependencies[i];
            const auto& y = desc_.subpassDependencies[i];
            if (x.srcSubpass != y.srcSubpass || x.dstSubpass != y.dstSubpass)
                return false;
        }
        return true;
    }

private:
    const RenderPassDesc desc_;
};

// endregion Command render pass arguments

// region Command variable arguments

struct CommandVariableArgumentsBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxCommands = 0u, maxDynamicOffsets = 0u;
    std::uint64_t maxTextBytes = 0u, maxPushBytes = 0u, maxCpuMetadataBytes = 0u;
};

// Immutable fields only. Complete descriptor/source/scalar legality and native
// recording authority remain outside this storage prerequisite.
class CommandVariableArguments final : public core::QuotaBoundSharedObjectIdentity {
    class ConstructionKey {
        friend class CommandVariableArguments;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    struct Input {
        CommandScalarArguments scalar;
        std::string_view text;
        const std::uint32_t* offsets = nullptr;
        std::uint32_t offsetCount = 0u;
        const void* bytes = nullptr;
        std::uint32_t byteCount = 0u;
    };

    struct Record {
        CommandScalarArguments scalar;
        std::size_t firstText, textCount, firstOffset, offsetCount, firstByte, byteCount;
    };

    CommandVariableArguments(ConstructionKey, const CommandVariableArgumentsBudget& b)
        : records_(b.maxCommands), text_(static_cast<std::size_t>(b.maxTextBytes)),
          offsets_(b.maxDynamicOffsets), bytes_(static_cast<std::size_t>(b.maxPushBytes))
    {}

    static const char* validateBudget(const CommandVariableArgumentsBudget& b) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-COMMAND-ARGUMENTS-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-COMMAND-ARGUMENTS-OPT-IN";
        if (!b.maxCommands || !b.maxCpuMetadataBytes || b.maxTextBytes > SIZE_MAX
            || b.maxPushBytes > SIZE_MAX)
            return "RHI-COMMAND-ARGUMENTS-BUDGET";
        return nullptr;
    }

    static const char* validateInput(const Input& input) noexcept
    {
        using K = CommandUsageKind;
        const auto kind = input.scalar.kind();
        if ((input.text.size() && !input.text.data()) || (input.offsetCount && !input.offsets)
            || (input.byteCount && !input.bytes))
            return "RHI-COMMAND-ARGUMENTS-POINTER";
        const bool text = kind == K::BeginComputePass || kind == K::BeginTransferPass
            || kind == K::PushDebugGroup || kind == K::InsertDebugMarker || kind == K::BufferUpload
            || kind == K::TextureUpload || kind == K::TextureReadback;
        const bool offsets = kind == K::BindRenderDescriptor || kind == K::BindComputeDescriptor;
        if ((!text && !input.text.empty()) || (!offsets && input.offsetCount)
            || (kind != K::PushConstants && input.byteCount))
            return "RHI-COMMAND-ARGUMENTS-PAYLOAD";
        if (kind == K::PushConstants) {
            const auto* push = input.scalar.get<CommandScalarArguments::Push>();
            if (!push || push->size != input.byteCount)
                return "RHI-COMMAND-ARGUMENTS-PUSH-SIZE";
        }
        return nullptr;
    }

    static std::shared_ptr<const CommandVariableArguments> prepareOriginal(const Input* input,
        std::size_t count,
        const CommandVariableArgumentsBudget& b,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        diagnostic = validateBudget(b);
        if (diagnostic)
            return {};
        if (!quota || quota->limitBytes() != b.maxCpuMetadataBytes) {
            diagnostic = "RHI-COMMAND-ARGUMENTS-QUOTA";
            return {};
        }
        if (!input || !count || count > b.maxCommands) {
            diagnostic = "RHI-COMMAND-ARGUMENTS-COUNT";
            return {};
        }
        std::uint64_t text = 0u, offsets = 0u, bytes = 0u;
        for (std::size_t i = 0u; i < count; ++i) {
            if ((diagnostic = validateInput(input[i])))
                return {};
            if (input[i].text.size() > b.maxTextBytes - text
                || input[i].offsetCount > b.maxDynamicOffsets - offsets
                || input[i].byteCount > b.maxPushBytes - bytes) {
                diagnostic = "RHI-COMMAND-ARGUMENTS-CAPACITY";
                return {};
            }
            text += input[i].text.size();
            offsets += input[i].offsetCount;
            bytes += input[i].byteCount;
        }
        try {
            core::CpuMemoryQuotaScope scope(quota);
            auto result = core::makeUnique<CommandVariableArguments>(ConstructionKey{}, b);
            for (std::size_t i = 0u; i < count; ++i) {
                const auto& value = input[i];
                result->records_.append({value.scalar,
                    result->text_.values().size(),
                    value.text.size(),
                    result->offsets_.values().size(),
                    value.offsetCount,
                    result->bytes_.values().size(),
                    value.byteCount});
                for (const char c : value.text)
                    result->text_.append(c);
                for (std::uint32_t j = 0u; j < value.offsetCount; ++j)
                    result->offsets_.append(value.offsets[j]);
                const auto* source = static_cast<const unsigned char*>(value.bytes);
                for (std::uint32_t j = 0u; j < value.byteCount; ++j)
                    result->bytes_.append(source[j]);
            }
            return shareOriginal(std::move(result));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-COMMAND-ARGUMENTS-ALLOCATION";
            return {};
        } catch (const std::length_error&) {
            diagnostic = "RHI-COMMAND-ARGUMENTS-ALLOCATION";
            return {};
        }
    }

    const auto& records() const noexcept { return records_.values(); }

    std::size_t storedTextBytes() const noexcept { return text_.values().size(); }

    // Shared scalar/usage binding and actual stored variable-category bounds.
    // These do not validate typed descriptors, state or native execution.
    const char* validateUsage(const CommandUsage* commands,
        std::size_t count,
        const char* countDiagnostic,
        const char* orderDiagnostic) const noexcept
    {
        if (!commands || count != records().size())
            return countDiagnostic;
        for (std::size_t i = 0u; i < count; ++i)
            if (!matchesUsageKind(i, commands[i].kind))
                return orderDiagnostic;
        return nullptr;
    }

    bool matchesUsageKind(std::size_t command, CommandUsageKind kind) const noexcept
    {
        return command < records().size() && records()[command].scalar.kind() == kind;
    }

    const char* validateCapacity(const CommandVariableArgumentsBudget& b) const noexcept
    {
        if (const auto* id = validateBudget(b))
            return id;
        return records().size() > b.maxCommands || text_.values().size() > b.maxTextBytes
                || offsets_.values().size() > b.maxDynamicOffsets
                || bytes_.values().size() > b.maxPushBytes
            ? "RHI-COMMAND-ARGUMENTS-CAPACITY"
            : nullptr;
    }

    std::string_view text(std::size_t record) const noexcept
    {
        if (record >= records().size() || !records()[record].textCount)
            return {};
        const auto& r = records()[record];
        return {text_.values().data() + r.firstText, r.textCount};
    }

    const std::uint32_t* offsets(std::size_t record) const noexcept
    {
        if (record >= records().size() || !records()[record].offsetCount)
            return nullptr;
        return offsets_.values().data() + records()[record].firstOffset;
    }

    const unsigned char* bytes(std::size_t record) const noexcept
    {
        if (record >= records().size() || !records()[record].byteCount)
            return nullptr;
        return bytes_.values().data() + records()[record].firstByte;
    }

    bool matches(std::size_t index, const Input& input) const noexcept
    {
        if (index >= records().size() || validateInput(input))
            return false;
        const auto& r = records()[index];
        if (!r.scalar.matches(input.scalar) || text(index) != input.text
            || r.offsetCount != input.offsetCount || r.byteCount != input.byteCount)
            return false;
        for (std::size_t i = 0u; i < r.offsetCount; ++i)
            if (offsets(index)[i] != input.offsets[i])
                return false;
        return !r.byteCount || !std::memcmp(bytes(index), input.bytes, r.byteCount);
    }

private:
    core::BoundedSequence<Record> records_;
    core::BoundedSequence<char> text_;
    core::BoundedSequence<std::uint32_t> offsets_;
    core::BoundedSequence<unsigned char> bytes_;
};
} // namespace arp::rhi

// endregion Command variable arguments

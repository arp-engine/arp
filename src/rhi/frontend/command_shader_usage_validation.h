#pragma once

#include "core/unsigned_range.h"
#include "rhi/frontend/command_arguments.h"
#include "rhi/frontend/command_recording_order.h"
#include "rhi/frontend/command_shader_validation.h"
#include "rhi/frontend/resource_state_sequence.h"
#include "rhi/semantics/command_lowering.h"
#include "rhi/semantics/viewport_scissor_validation.h"

#include <cmath>
#include <type_traits>

// region typed_shader_usage_scope
namespace arp::rhi::semantics {
// Usage-list prerequisite only, not a frozen synchronization decision. The
// caller still owes an exact complete original resource/binding projection.
// WebGPU CRD 2026-09-15 sections 3.4.3/3.4.4: buffers are whole subresources;
// each dispatch is a scope; every render setBindGroup contributes to its pass.
class TypedShaderUsageScopeContract final {
    using K = CommandUsageKind;

public:
    enum class Kind { Unspecified, ComputeDispatch, RenderPass, TransferCopy };
    enum class Contribution {
        Unspecified,
        RenderBinding,
        ShaderConsumption,
        Attachment,
        Readback,
        GeometryBinding,
        GeometryConsumption
    };
    enum class BufferUsage {
        Unspecified,
        StorageRead,
        Storage,
        CopyDst,
        Vertex,
        Index,
        UniformRead
    };
    enum class TextureUsage { Unspecified, SampledRead, Attachment, CopySrc, StorageRead, Storage };

    struct Scope {
        Kind kind = Kind::Unspecified;
        std::size_t first = SIZE_MAX, last = SIZE_MAX;

        bool matches(const Scope& other) const noexcept
        {
            return kind == other.kind && first == other.first && last == other.last;
        }
    };

    struct BufferUse {
        GpuResourceId resource;
        std::uint64_t bufferBytes = 0u, offsetBytes = 0u, sizeBytes = 0u;
        std::size_t commandIndex = SIZE_MAX;
        std::uint32_t set = 0u, binding = 0u;
        Scope scope;
        Contribution contribution = Contribution::Unspecified;
        BufferUsage usage = BufferUsage::Unspecified;
        ResourceAccessScope access;
    };

    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint64_t maxCommands = 0u, maxContributions = 0u, maxPairChecks = 0u;
    };

    struct TextureUse {
        GpuResourceId resource;
        std::uint64_t registrationGeneration = 0u;
        TextureSubresourceRange range;
        std::size_t commandIndex = SIZE_MAX;
        Scope scope;
        Contribution contribution = Contribution::Unspecified;
        TextureUsage usage = TextureUsage::Unspecified;
        ResourceAccessScope access;
    };

    struct Result {
        const char* diagnostic = nullptr;
        std::uint64_t contributions = 0u, pairChecks = 0u;

        bool synchronizationProven() const noexcept { return false; }

        bool nativeExecutionProven() const noexcept { return false; }
    };

    static constexpr auto lowering(Backend backend) noexcept
    {
        return arp::rhi::semantics::OriginalTypedNativeColdLowering::lowering(backend);
    }

    static const char* validateBudget(const Budget& budget) noexcept
    {
        if (budget.contractVersion != 1u && budget.contractVersion != 2u
            && budget.contractVersion != 3u && budget.contractVersion != 4u
            && budget.contractVersion != 5u)
            return "RHI-TYPED-USAGE-SCOPE-VERSION";
        if (!budget.prototypeOptIn)
            return "RHI-TYPED-USAGE-SCOPE-OPT-IN";
        return budget.maxCommands && budget.maxContributions ? nullptr
                                                             : "RHI-TYPED-USAGE-SCOPE-BUDGET";
    }

    template <class Operations>
    static const char* validateOperations(
        const Operations& operations, const Budget& budget) noexcept
    {
        if (const auto* id = validateBudget(budget))
            return id;
        if (operations.empty() || operations.size() > budget.maxCommands)
            return "RHI-TYPED-USAGE-SCOPE-COMMAND-CAPACITY";
        CommandRecordingOrder order(0u);
        for (std::size_t i = 0u; i < operations.size(); ++i) {
            const auto& op = operations[i];
            if (op.commandIndex != i)
                return "RHI-TYPED-USAGE-SCOPE-POSITION";
            if (!supported(op.kind, budget.contractVersion))
                return "RHI-TYPED-USAGE-SCOPE-COMMAND-PROFILE";
            if (const auto* id = order.append(op.kind))
                return id;
        }
        return order.complete() ? nullptr : "RHI-COMMAND-ORDER-INCOMPLETE";
    }

    template <class Operations>
    static const char* resolve(const Operations& operations,
        const Budget& budget,
        std::size_t command,
        Scope& output) noexcept
    {
        output = {};
        if (const auto* id = validateOperations(operations, budget))
            return id;
        if (command >= operations.size())
            return "RHI-TYPED-USAGE-SCOPE-POSITION";
        if (operations[command].kind == K::Dispatch) {
            output = {Kind::ComputeDispatch, command, command};
            return nullptr;
        }
        if (budget.contractVersion >= 3u && operations[command].kind == K::TextureReadback) {
            output = {Kind::TransferCopy, command, command};
            return nullptr;
        }
        const auto selected = operations[command].kind;
        if (selected != K::BindRenderDescriptor && selected != K::Draw
            && !(budget.contractVersion >= 4u && selected == K::DrawIndexed)
            && !(budget.contractVersion != 1u && selected == K::BindRenderDescriptorTable)
            && !(budget.contractVersion >= 4u
                && (selected == K::BindVertexBuffer || selected == K::BindIndexBuffer)))
            return "RHI-TYPED-USAGE-SCOPE-CONTRIBUTION-POSITION";
        std::size_t begin = SIZE_MAX;
        for (std::size_t i = 0u; i < operations.size(); ++i) {
            if (operations[i].kind == K::BeginRenderPass)
                begin = i;
            if (operations[i].kind == K::EndRenderPass) {
                if (begin < command && command < i) {
                    output = {Kind::RenderPass, begin, i};
                    return nullptr;
                }
                begin = SIZE_MAX;
            }
        }
        return "RHI-TYPED-USAGE-SCOPE-RENDER-POSITION";
    }

    template <class Operations>
    static const char* resolveRenderPass(const Operations& operations,
        const Budget& budget,
        std::size_t begin,
        Scope& output) noexcept
    {
        output = {};
        if (const auto* id = validateOperations(operations, budget))
            return id;
        if (begin >= operations.size() || operations[begin].kind != K::BeginRenderPass)
            return "RHI-TYPED-USAGE-SCOPE-RENDER-POSITION";
        for (std::size_t end = begin + 1u; end < operations.size(); ++end)
            if (operations[end].kind == K::EndRenderPass) {
                output = {Kind::RenderPass, begin, end};
                return nullptr;
            }
        return "RHI-TYPED-USAGE-SCOPE-RENDER-POSITION";
    }

    template <class Operations, class Uses>
    static Result inspect(
        const Operations& operations, const Uses& uses, const Budget& budget) noexcept
    {
        Result result;
        if ((result.diagnostic = validateOperations(operations, budget)))
            return result;
        if (uses.empty() || uses.size() > budget.maxContributions) {
            result.diagnostic = "RHI-TYPED-USAGE-SCOPE-CONTRIBUTION-CAPACITY";
            return result;
        }
        // The pair budget is an independently authored cold traversal ceiling.
        std::uint64_t pairs = 0u;
        if (!core::countUnorderedPairsUnsigned<std::uint64_t>(uses.size(), pairs)
            || pairs > budget.maxPairChecks) {
            result.diagnostic = "RHI-TYPED-USAGE-SCOPE-PAIR-CAPACITY";
            return result;
        }
        for (const auto& use : uses) {
            Scope expected;
            if constexpr (std::is_same_v<std::decay_t<decltype(use)>, TextureUse>) {
                result.diagnostic = use.usage == TextureUsage::Attachment
                    ? resolveRenderPass(operations, budget, use.commandIndex, expected)
                    : resolve(operations, budget, use.commandIndex, expected);
            } else
                result.diagnostic = resolve(operations, budget, use.commandIndex, expected);
            if (result.diagnostic)
                return result;
            if ((result.diagnostic = validateUse(
                     use, expected, operations[use.commandIndex].kind, budget.contractVersion)))
                return result;
        }
        for (std::size_t i = 0u; i < uses.size(); ++i)
            for (std::size_t j = 0u; j < i; ++j)
                if ((result.diagnostic = validatePair(uses[i], uses[j])))
                    return result;
        result.contributions = uses.size();
        result.pairChecks = pairs;
        return result;
    }

    // Valid L2 state/access alone cannot prove positional shader visibility.
    static const char* rejectUnfrozenTransition(
        const ResourceBarrierDesc& desc, ResourceState current) noexcept
    {
        if (const auto* id = RecordedResourceStateSequence::validateBarrier(desc, current))
            return id;
        return "RHI-TYPED-USAGE-SCOPE-DEPENDENCY-NOT-FROZEN";
    }

    static const char* rejectUnfrozenMemory(
        const ResourceMemoryBarrierDesc& desc, ResourceState current) noexcept
    {
        if (const auto* id = RecordedResourceStateSequence::validateMemoryBarrier(desc, current))
            return id;
        return "RHI-TYPED-USAGE-SCOPE-DEPENDENCY-NOT-FROZEN";
    }

    // Reuse the same value checks in bounded original projection visitors.
    // These checks confer neither contribution completeness nor sync authority.
    template <class Operations>
    static const char* validateBufferContribution(
        const Operations& operations, const Budget& budget, const BufferUse& use) noexcept
    {
        Scope expected;
        if (const auto* id = resolve(operations, budget, use.commandIndex, expected))
            return id;
        return validateUse(
            use, expected, operations[use.commandIndex].kind, budget.contractVersion);
    }

    static const char* validateBufferPair(const BufferUse& lhs, const BufferUse& rhs) noexcept
    {
        return validatePair(lhs, rhs);
    }

    template <class Operations>
    static const char* validateTextureContribution(
        const Operations& operations, const Budget& budget, const TextureUse& use) noexcept
    {
        if (budget.contractVersion != 2u && budget.contractVersion != 3u
            && budget.contractVersion != 4u && budget.contractVersion != 5u)
            return "RHI-TYPED-USAGE-SCOPE-TEXTURE-PROFILE";
        Scope expected;
        const auto* id = use.usage == TextureUsage::Attachment
            ? resolveRenderPass(operations, budget, use.commandIndex, expected)
            : resolve(operations, budget, use.commandIndex, expected);
        if (id)
            return id;
        return validateUse(
            use, expected, operations[use.commandIndex].kind, budget.contractVersion);
    }

private:
    static const char* validateUse(
        const TextureUse& use, const Scope& expected, K command, std::uint32_t version) noexcept
    {
        if (version < 2u || version > 5u)
            return "RHI-TYPED-USAGE-SCOPE-TEXTURE-PROFILE";
        if (!use.scope.matches(expected))
            return "RHI-TYPED-USAGE-SCOPE-BINDING";
        if (!use.resource.isValid() || !use.registrationGeneration || !use.range.mipLevelCount
            || !use.range.arrayLayerCount)
            return "RHI-TYPED-USAGE-SCOPE-TEXTURE-IDENTITY";
        if (const auto* error = validateResourceAccessScope(use.access))
            return error;
        if (use.usage == TextureUsage::CopySrc)
            return version >= 3u && expected.kind == Kind::TransferCopy
                    && use.contribution == Contribution::Readback
                    && use.access.state == ResourceState::CopySrc
                    && use.access.stages == PipelineStage::Transfer
                    && use.access.access == ResourceAccess::TransferRead
                ? nullptr
                : "RHI-TYPED-USAGE-SCOPE-TEXTURE-READBACK-ACCESS";
        if (use.usage == TextureUsage::Attachment)
            return use.contribution == Contribution::Attachment
                    && ((use.range.aspect == TextureAspect::Color
                            && use.access.state == ResourceState::ColorAttachment)
                        || (use.range.aspect == TextureAspect::Depth
                            && use.access.state == ResourceState::DepthAttachment))
                ? nullptr
                : "RHI-TYPED-USAGE-SCOPE-TEXTURE-ATTACHMENT-ACCESS";
        const bool storage =
            use.usage == TextureUsage::Storage || use.usage == TextureUsage::StorageRead;
        if (use.usage != TextureUsage::SampledRead && !(version == 5u && storage))
            return "RHI-TYPED-USAGE-SCOPE-TEXTURE-PROFILE";
        const bool bind = command == K::BindRenderDescriptorTable
            || (version == 5u && command == K::BindRenderDescriptor);
        if (use.contribution
            != (bind ? Contribution::RenderBinding : Contribution::ShaderConsumption))
            return "RHI-TYPED-USAGE-SCOPE-CONTRIBUTION-KIND";
        const auto stages = expected.kind == Kind::ComputeDispatch
            ? PipelineStage::ComputeShader
            : PipelineStage::VertexShader | PipelineStage::FragmentShader;
        if (storage) {
            if (use.range.aspect != TextureAspect::Color)
                return "RHI-TYPED-USAGE-SCOPE-TEXTURE-STORAGE-ASPECT";
            const auto access = ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite;
            return (use.access.stages & stages) == use.access.stages
                    && (use.access.access & access) == use.access.access
                    && (use.usage != TextureUsage::StorageRead
                        || use.access.access == ResourceAccess::ShaderRead)
                ? nullptr
                : "RHI-TYPED-USAGE-SCOPE-TEXTURE-SHADER-ACCESS";
        }
        return use.access.state == ResourceState::ShaderRead
                && use.access.access == ResourceAccess::ShaderRead
                && (use.access.stages & stages) == use.access.stages
            ? nullptr
            : "RHI-TYPED-USAGE-SCOPE-TEXTURE-SHADER-ACCESS";
    }

public:
    static const char* validateTexturePair(const TextureUse& lhs, const TextureUse& rhs) noexcept
    {
        if (lhs.resource != rhs.resource)
            return nullptr;
        if (!lhs.registrationGeneration || lhs.registrationGeneration != rhs.registrationGeneration)
            return "RHI-TYPED-USAGE-SCOPE-TEXTURE-IDENTITY";
        if (!lhs.scope.matches(rhs.scope) || lhs.range.aspect != rhs.range.aspect
            || !core::overlapsUnsignedRanges(lhs.range.baseMipLevel,
                lhs.range.mipLevelCount,
                rhs.range.baseMipLevel,
                rhs.range.mipLevelCount)
            || !core::overlapsUnsignedRanges(lhs.range.baseArrayLayer,
                lhs.range.arrayLayerCount,
                rhs.range.baseArrayLayer,
                rhs.range.arrayLayerCount))
            return nullptr;
        const auto readonly = [](TextureUsage usage) noexcept {
            return usage == TextureUsage::SampledRead || usage == TextureUsage::StorageRead;
        };
        if (readonly(lhs.usage) && readonly(rhs.usage) && lhs.access.state == rhs.access.state
            && lhs.access.access == ResourceAccess::ShaderRead
            && rhs.access.access == ResourceAccess::ShaderRead)
            return nullptr;
        if (lhs.usage == TextureUsage::Storage && rhs.usage == TextureUsage::Storage)
            return lhs.contribution == Contribution::ShaderConsumption
                    && rhs.contribution == Contribution::ShaderConsumption
                    && lhs.commandIndex == rhs.commandIndex
                ? "RHI-TYPED-USAGE-SCOPE-TEXTURE-WRITABLE-ALIAS"
                : nullptr;
        return lhs.usage == TextureUsage::Attachment && rhs.usage == TextureUsage::Attachment
            ? "RHI-ORIGINAL-DEPENDENCY-PROJECTION-ATTACHMENT-ALIAS"
            : "RHI-TYPED-USAGE-SCOPE-TEXTURE-USAGE-ALIAS";
    }

private:
    static bool supported(K kind, std::uint32_t version) noexcept
    {
        switch (kind) {
        case K::BeginRenderPass:
        case K::EndRenderPass:
        case K::BeginComputePass:
        case K::EndComputePass:
        case K::BindGraphicsPipeline:
        case K::BindComputePipeline:
        case K::BindRenderDescriptor:
        case K::BindComputeDescriptor:
        case K::Draw:
        case K::Dispatch:
        case K::SetViewport:
        case K::SetScissor:
        case K::BufferBarrier:
        case K::TextureBarrier:
        case K::BufferMemoryBarrier:
        case K::TextureMemoryBarrier:
        case K::Finish:
            return true;
        case K::BindRenderDescriptorTable:
        case K::BindComputeDescriptorTable:
            return version >= 2u;
        case K::BeginTransferPass:
        case K::TextureReadback:
        case K::EndTransferPass:
            return version >= 3u;
        case K::BindVertexBuffer:
        case K::BindIndexBuffer:
        case K::DrawIndexed:
            return version >= 4u;
        default:
            return false;
        }
    }

    static const char* validateUse(
        const BufferUse& use, const Scope& expected, K command, std::uint32_t version) noexcept
    {
        if (!use.scope.matches(expected))
            return "RHI-TYPED-USAGE-SCOPE-BINDING";
        if (!use.resource.isValid()
            || !core::containsUnsignedRange<std::uint64_t>(
                0u, use.bufferBytes, use.offsetBytes, use.sizeBytes))
            return "RHI-TYPED-USAGE-SCOPE-BUFFER-RANGE";
        if (expected.kind == Kind::TransferCopy)
            return command == K::TextureReadback && use.usage == BufferUsage::CopyDst
                    && use.contribution == Contribution::Readback
                    && use.access.state == ResourceState::CopyDst
                    && use.access.stages == PipelineStage::Transfer
                    && use.access.access == ResourceAccess::TransferWrite
                ? nullptr
                : "RHI-TYPED-USAGE-SCOPE-BUFFER-READBACK-ACCESS";
        if (use.usage == BufferUsage::Vertex || use.usage == BufferUsage::Index) {
            if (version != 4u && version != 5u)
                return "RHI-TYPED-USAGE-SCOPE-GEOMETRY-PROFILE";
            const bool vertex = use.usage == BufferUsage::Vertex;
            const bool position = use.contribution == Contribution::GeometryBinding
                ? command == (vertex ? K::BindVertexBuffer : K::BindIndexBuffer)
                : use.contribution == Contribution::GeometryConsumption
                    && (command == K::DrawIndexed || (vertex && command == K::Draw));
            return expected.kind == Kind::RenderPass && position
                    && use.access.state == ResourceState::ShaderRead
                    && use.access.stages == PipelineStage::VertexInput
                    && use.access.access
                        == (vertex ? ResourceAccess::VertexAttributeRead
                                   : ResourceAccess::IndexRead)
                ? nullptr
                : "RHI-TYPED-USAGE-SCOPE-GEOMETRY-ACCESS";
        }
        if (use.usage != BufferUsage::StorageRead && use.usage != BufferUsage::Storage
            && !(version == 5u && use.usage == BufferUsage::UniformRead))
            return "RHI-TYPED-USAGE-SCOPE-BUFFER-PROFILE";
        if (version != 5u && command == K::BindRenderDescriptor)
            return use.contribution == Contribution::RenderBinding
                ? nullptr
                : "RHI-TYPED-USAGE-SCOPE-CONTRIBUTION-KIND";
        const bool bind = command == K::BindRenderDescriptor
            || (version == 5u && command == K::BindRenderDescriptorTable);
        if (use.contribution
            != (bind ? Contribution::RenderBinding : Contribution::ShaderConsumption))
            return "RHI-TYPED-USAGE-SCOPE-CONTRIBUTION-KIND";
        if (const auto* id = validateResourceAccessScope(use.access))
            return id;
        const auto stages = expected.kind == Kind::ComputeDispatch
            ? PipelineStage::ComputeShader
            : PipelineStage::VertexShader | PipelineStage::FragmentShader;
        const auto access = ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite;
        if ((use.access.stages & stages) != use.access.stages
            || (use.access.access & access) != use.access.access
            || ((use.usage == BufferUsage::StorageRead || use.usage == BufferUsage::UniformRead)
                && resourceAccessWrites(use.access.access)))
            return "RHI-TYPED-USAGE-SCOPE-SHADER-ACCESS";
        return nullptr;
    }

    static const char* validatePair(const TextureUse& lhs, const TextureUse& rhs) noexcept
    {
        return validateTexturePair(lhs, rhs);
    }

    static const char* validatePair(const BufferUse& lhs, const BufferUse& rhs) noexcept
    {
        if (lhs.contribution == Contribution::ShaderConsumption
            && rhs.contribution == Contribution::ShaderConsumption
            && lhs.commandIndex == rhs.commandIndex && lhs.set == rhs.set
            && lhs.binding == rhs.binding)
            return "RHI-TYPED-USAGE-SCOPE-DUPLICATE-BINDING";
        if (lhs.resource != rhs.resource)
            return nullptr;
        if (lhs.bufferBytes != rhs.bufferBytes)
            return "RHI-TYPED-USAGE-SCOPE-BUFFER-SIZE-BINDING";
        if (!lhs.scope.matches(rhs.scope))
            return nullptr;
        // Whole buffer compatibility ignores binding byte-range disjointness.
        const auto inputRead = [](BufferUsage usage) noexcept {
            return usage == BufferUsage::Vertex || usage == BufferUsage::Index
                || usage == BufferUsage::StorageRead || usage == BufferUsage::UniformRead;
        };
        if (lhs.usage != rhs.usage && !(inputRead(lhs.usage) && inputRead(rhs.usage)))
            return "RHI-TYPED-USAGE-SCOPE-INCOMPATIBLE-BUFFER-USAGE";
        if (lhs.contribution == Contribution::ShaderConsumption
            && rhs.contribution == Contribution::ShaderConsumption
            && lhs.commandIndex == rhs.commandIndex) {
            // Storage usage denotes a writable binding even if this shader
            // invocation declares only reads. Alias validation is independent.
            if (lhs.usage == BufferUsage::Storage
                && core::overlapsUnsignedRanges(
                    lhs.offsetBytes, lhs.sizeBytes, rhs.offsetBytes, rhs.sizeBytes))
                return "RHI-TYPED-USAGE-SCOPE-WRITABLE-ALIAS";
        }
        return nullptr;
    }
};

// endregion typed_shader_usage_scope

// region original_typed_render_state

// Typed render state completeness/1. State begins empty in each pass. Every
// consuming draw needs explicit original setters in that pass; this validator
// emits no setters and never derives values from attachment extent or defaults.
class OriginalTypedRenderStateContract final {
public:
    template <class Records>
    static const char* validate(const Records& records, std::uint64_t maximum) noexcept
    {
        if (records.size() > maximum)
            return "RHI-ORIGINAL-TYPED-RENDER-STATE-COMMAND-CAPACITY";
        bool open = false, viewport = false, scissor = false;
        using K = CommandUsageKind;
        for (const auto& record : records) {
            const auto kind = record.original.kind;
            if (kind == K::BeginRenderPass) {
                if (open)
                    return "RHI-ORIGINAL-TYPED-RENDER-STATE-PASS";
                open = true;
                viewport = scissor = false;
            } else if (kind == K::EndRenderPass) {
                if (!open)
                    return "RHI-ORIGINAL-TYPED-RENDER-STATE-PASS";
                open = false;
                viewport = scissor = false;
            } else if (kind == K::SetViewport) {
                if (!open)
                    return "RHI-ORIGINAL-TYPED-RENDER-STATE-PASS";
                const auto* value = record.scalar.template get<ViewportState>();
                if (!value)
                    return "RHI-ORIGINAL-TYPED-RENDER-STATE-VIEWPORT-SCALAR";
                if (const auto* id = viewportCommandDiagnostic(*value))
                    return id;
                if (!std::isfinite(value->x) || !std::isfinite(value->y)
                    || !std::isfinite(value->width) || !std::isfinite(value->height)
                    || !std::isfinite(value->minDepth) || !std::isfinite(value->maxDepth)
                    || value->minDepth < 0.0f || value->maxDepth > 1.0f)
                    return "RHI-ORIGINAL-TYPED-RENDER-STATE-VIEWPORT-RANGE";
                viewport = true;
            } else if (kind == K::SetScissor) {
                if (!open)
                    return "RHI-ORIGINAL-TYPED-RENDER-STATE-PASS";
                const auto* value = record.scalar.template get<ScissorState>();
                if (!value)
                    return "RHI-ORIGINAL-TYPED-RENDER-STATE-SCISSOR-SCALAR";
                if (const auto* id = scissorCommandDiagnostic(*value))
                    return id;
                scissor = true;
            } else if (kind == K::Draw || kind == K::DrawIndexed) {
                if (!open)
                    return "RHI-ORIGINAL-TYPED-RENDER-STATE-PASS";
                if (!viewport || !scissor)
                    return "RHI-ORIGINAL-TYPED-RENDER-STATE-INCOMPLETE";
            }
        }
        return open ? "RHI-ORIGINAL-TYPED-RENDER-STATE-PASS" : nullptr;
    }
};
} // namespace arp::rhi::semantics

// endregion original_typed_render_state

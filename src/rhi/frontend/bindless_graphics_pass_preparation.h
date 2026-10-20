#pragma once
#include "rhi/frontend/bindless_attachment_clear.h"
#include "rhi/frontend/command_attachment_validation.h"
#include "rhi/frontend/command_model.h"
#include "rhi/semantics/command_lowering.h"

#include <array>

namespace arp::rhi {
// Cold preparation for one original Root Graphics pass. These are object
// counts, not GPU allocation bytes. Nested original owners retain their own
// budgets; maxRetainedOwners bounds the additional, deduplicated direct closure.
// Preparing a pass grants no recording, submission or completion authority.
struct OriginalBindlessGraphicsPassBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxAttachmentViews = 0u, maxNativeObjects = 0u, maxRetainedOwners = 0u;
    std::uint64_t maxCpuMetadataBytes = 0u;
    OriginalBindlessAttachmentClearIntent attachmentClear;
    // /2 selects one authentic pass in a produced-geometry stream. The old
    // /1 contract remains single-pass and cannot borrow this selection.
    std::size_t commandIndex = SIZE_MAX;
    std::uint64_t maxCommandVisits = 0u;
};

class OriginalBindlessGraphicsPassContract final {
public:
    using Budget = OriginalBindlessGraphicsPassBudget;
    // Same finite attachment limit as the L2 render-pass validator.
    static constexpr std::uint32_t maxColorAttachments = 8u;
    static constexpr std::uint32_t maxAttachments = maxColorAttachments + 1u;

    struct RootSelection {
        std::size_t source = SIZE_MAX;
        std::uint64_t visits = 0u;
    };

    // Select from authentic command ordinals. A pass must bind exactly one
    // distinct graphics Root, though repeated binds of that Root are legal.
    template <class Operations, class References>
    static const char* selectRoot(const Operations& operations,
        const References& references,
        std::size_t commandIndex,
        std::uint64_t maxVisits,
        RootSelection& output) noexcept
    {
        output = {};
        RootSelection result;
        bool active = false, found = false, ended = false;
        for (const auto& op : operations) {
            if (result.visits == maxVisits)
                return "RHI-BINDLESS-GRAPHICS-PASS-COMMAND-CAPACITY";
            ++result.visits;
            if (op.kind == CommandUsageKind::BeginRenderPass) {
                if (active)
                    return "RHI-BINDLESS-GRAPHICS-PASS-SCOPE";
                active = op.commandIndex == commandIndex;
                if (active) {
                    if (found)
                        return "RHI-BINDLESS-GRAPHICS-PASS-SELECTION-DUPLICATE";
                    found = true;
                }
            } else if (active && op.kind == CommandUsageKind::BindPreparedGraphicsBinding) {
                if (op.referenceCount != 1u || op.firstReference >= references.size())
                    return "RHI-BINDLESS-GRAPHICS-PASS-ROOT-CORRESPONDENCE";
                const auto source = references[op.firstReference].sourceIndex;
                if (source == SIZE_MAX || (result.source != SIZE_MAX && result.source != source))
                    return "RHI-BINDLESS-GRAPHICS-PASS-ROOT-CORRESPONDENCE";
                result.source = source;
            } else if (active && op.kind == CommandUsageKind::EndRenderPass) {
                active = false;
                ended = true;
            }
        }
        if (!found || !ended || active || result.source == SIZE_MAX)
            return "RHI-BINDLESS-GRAPHICS-PASS-SELECTION-MISSING";
        output = result;
        return nullptr;
    }

    // Immutable L2 compatibility facts captured by the actual pipeline factory.
    // No borrowed shader-package/layout pointers or caller-supplied state can
    // substitute for the backend's sealed pipeline observation.
    struct PipelineState {
        bool singleSubpass = false;
        std::uint32_t colorCount = 0u, samples = 0u;
        std::array<TextureFormat, maxColorAttachments> colors{};
        TextureFormat depth = TextureFormat::Unknown;
    };

    static PipelineState capturePipeline(const RenderPipelineDesc& source) noexcept
    {
        PipelineState result;
        result.colorCount = effectiveColorTargetCount(source);
        result.samples = source.multisample.sampleCount;
        result.depth = effectiveDepthStencilFormat(source);
        result.singleSubpass = source.subpassIndex == 0u
            && source.renderPassCompatibility.colorAttachmentFormats.empty()
            && source.renderPassCompatibility.depthStencilFormat == TextureFormat::Unknown
            && source.renderPassCompatibility.subpasses.empty()
            && source.renderPassCompatibility.subpassDependencies.empty();
        for (std::uint32_t i = 0u; i < result.colorCount && i < maxColorAttachments; ++i)
            result.colors[i] = effectiveColorTargetAt(source, i).format;
        return result;
    }

    static constexpr auto lowering(Backend backend, std::uint32_t version = 1u) noexcept
    {
        return semantics::OriginalBindlessGraphicsPassLowering::lowering(backend, version);
    }

    static constexpr const char* validateBudget(const Budget& b) noexcept
    {
        if (b.contractVersion != 1u && b.contractVersion != 2u)
            return "RHI-BINDLESS-GRAPHICS-PASS-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-BINDLESS-GRAPHICS-PASS-OPT-IN";
        if (b.contractVersion == 2u ? b.commandIndex == SIZE_MAX || !b.maxCommandVisits
                                    : b.commandIndex != SIZE_MAX || b.maxCommandVisits)
            return "RHI-BINDLESS-GRAPHICS-PASS-SELECTION-SCHEMA";
        if (const auto* id =
                OriginalBindlessAttachmentClearContract::validateIntent(b.attachmentClear))
            return id;
        if (!b.maxAttachmentViews || !b.maxNativeObjects || !b.maxRetainedOwners
            || !b.maxCpuMetadataBytes)
            return "RHI-BINDLESS-GRAPHICS-PASS-BUDGET";
        return nullptr;
    }

    // Consume original command ordinals, never a second pass list. Verify
    // that every graphics bind in the selected scope names this actual Root.
    // Pending retains the resulting native pass, not these borrowed inputs.
    template <class Stream, class Root>
    static const char* selectPass(const Stream& stream,
        const Root& root,
        const Budget& budget,
        std::size_t& selected) noexcept
    {
        selected = SIZE_MAX;
        if (const auto* id = validateBudget(budget))
            return id;
        const auto& operations = stream.operations();
        if (budget.contractVersion == 1u) {
            std::size_t count = 0u;
            for (const auto& op : operations)
                if (op.kind == CommandUsageKind::BeginRenderPass) {
                    selected = op.commandIndex;
                    ++count;
                }
            return count == 1u ? nullptr : "RHI-BINDLESS-GRAPHICS-PASS-SINGLE-PASS";
        }
        if (stream.originalBudget().bindlessRoots.contractVersion != 6u)
            return "RHI-BINDLESS-GRAPHICS-PASS-PRODUCED-STREAM";
        if (operations.size() > budget.maxCommandVisits)
            return "RHI-BINDLESS-GRAPHICS-PASS-COMMAND-CAPACITY";
        const auto& usage = *stream.originalInputs()->originalScalars()->originalUsage();
        const auto source = usage.originalSources()->originalLeaseIndex(root);
        if (source == SIZE_MAX)
            return "RHI-BINDLESS-GRAPHICS-PASS-ROOT-SOURCE";
        RootSelection selection;
        if (const auto* id = selectRoot(operations,
                usage.references(),
                budget.commandIndex,
                budget.maxCommandVisits,
                selection))
            return id;
        if (selection.source != source)
            return "RHI-BINDLESS-GRAPHICS-PASS-ROOT-CORRESPONDENCE";
        selected = budget.commandIndex;
        return nullptr;
    }

    static constexpr const char* validateCounts(
        const Budget& b, std::uint64_t attachments, std::uint64_t owners) noexcept
    {
        return validateCounts(b, attachments, attachments + 2u, owners);
    }

    // Backend-observed newly created native objects. Retained attachment views
    // are charged by their original owners; they need not be recreated.
    static constexpr const char* validateCounts(const Budget& b,
        std::uint64_t attachments,
        std::uint64_t nativeObjects,
        std::uint64_t owners) noexcept
    {
        if (const auto* id = validateBudget(b))
            return id;
        if (!attachments || attachments > maxAttachments)
            return "RHI-BINDLESS-GRAPHICS-PASS-ATTACHMENT-COUNT";
        if (attachments > b.maxAttachmentViews || !nativeObjects
            || nativeObjects > b.maxNativeObjects)
            return "RHI-BINDLESS-GRAPHICS-PASS-NATIVE-CAPACITY";
        if (owners > b.maxRetainedOwners)
            return "RHI-BINDLESS-GRAPHICS-PASS-OWNER-CAPACITY";
        return nullptr;
    }

    static const char* inspectPass(
        const RenderPassDesc& pass, const PipelineState& pipeline) noexcept
    {
        if (pass.colorAttachments.size() > maxColorAttachments
            || (pass.colorAttachments.empty() && !pass.hasDepthStencilAttachment)
            || !pass.subpasses.empty() || !pass.subpassDependencies.empty()
            || pass.layeredRendering)
            return "RHI-BINDLESS-GRAPHICS-PASS-PROFILE";
        if (pipeline.samples != 1u || !pipeline.singleSubpass
            || pipeline.colorCount != pass.colorAttachments.size()
            || pipeline.depth
                != (pass.hasDepthStencilAttachment ? pass.depthStencilAttachment.format
                                                   : TextureFormat::Unknown))
            return "RHI-BINDLESS-GRAPHICS-PASS-PIPELINE-COMPATIBILITY";
        for (std::size_t i = 0u; i < pass.colorAttachments.size(); ++i) {
            const auto& color = pass.colorAttachments[i];
            if (!color.target.isValid() || color.loadOp != LoadOp::Clear
                || color.storeOp != StoreOp::Store || color.hasResolveTarget
                || (color.format != TextureFormat::Rgba8Unorm
                    && color.format != TextureFormat::Rgba16Float)
                || pipeline.colors[i] != color.format)
                return "RHI-BINDLESS-GRAPHICS-PASS-COLOR-PROFILE";
        }
        if (pass.hasDepthStencilAttachment) {
            const auto& depth = pass.depthStencilAttachment;
            if (!depth.target.isValid() || depth.format != TextureFormat::D32Float
                || depth.depthLoadOp != LoadOp::Clear || depth.depthStoreOp != StoreOp::Store
                || depth.depthReadOnly || depth.stencilReadOnly)
                return "RHI-BINDLESS-GRAPHICS-PASS-DEPTH-PROFILE";
        }
        return pass.hasDepthStencilAttachment
            ? semantics::DepthStencilAttachmentInputContract::diagnostic(
                  pass.depthStencilAttachment)
            : nullptr;
    }

    static const char* inspectPass(
        const RenderPassDesc& pass, const RenderPipelineDesc& pipeline) noexcept
    {
        return inspectPass(pass, capturePipeline(pipeline));
    }
};
} // namespace arp::rhi

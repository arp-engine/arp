#pragma once

#include "rhi/frontend/command_stream_program.h"

namespace arp::rhi {

// Cold storage projection of the exact original pass inputs. Repeated uses of
// the same immutable snapshot consume separate capacities. No copied command
// model, native payload, mutation journal or recording permission is created.
class OriginalRenderPassStorageContract final {
public:
    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint64_t maxPasses = 0u, maxDepthAttachments = 0u, maxAttachmentReferences = 0u;
        RenderPassArgumentsBudget arguments;
    };

    struct Counts {
        std::uint64_t passes = 0u, depthAttachments = 0u, attachmentReferences = 0u;
        std::uint64_t colorAttachments = 0u, subpasses = 0u, attachmentIndices = 0u;
        std::uint64_t dependencies = 0u, textBytes = 0u;

        bool matches(const Counts& other) const noexcept
        {
            return std::tie(passes,
                       depthAttachments,
                       attachmentReferences,
                       colorAttachments,
                       subpasses,
                       attachmentIndices,
                       dependencies,
                       textBytes)
                == std::tie(other.passes,
                    other.depthAttachments,
                    other.attachmentReferences,
                    other.colorAttachments,
                    other.subpasses,
                    other.attachmentIndices,
                    other.dependencies,
                    other.textBytes);
        }
    };

    struct Result {
        const char* diagnostic = nullptr;
        Counts counts;
    };

    struct Decision {
        bool validatorOnly = false;
        bool nativeRecordingAvailable = false;
        const char* diagnostic = "RHI-RENDER-PASS-STORAGE-BACKEND";
    };

    static const char* validate(const Budget& budget) noexcept
    {
        if (budget.contractVersion != 1u)
            return "RHI-RENDER-PASS-STORAGE-VERSION";
        if (!budget.prototypeOptIn)
            return "RHI-RENDER-PASS-STORAGE-OPT-IN";
        if (!budget.maxPasses || !budget.maxAttachmentReferences)
            return "RHI-RENDER-PASS-STORAGE-BUDGET";
        return RenderPassArguments::validateBudget(budget.arguments);
    }

    static Decision lowering(Backend backend, const Budget& budget) noexcept
    {
        if (const auto* id = validate(budget))
            return {false, false, id};
        switch (backend) {
        case Backend::Null:
            return {true, false, "RHI-RENDER-PASS-STORAGE-NULL-VALIDATOR"};
        case Backend::Vulkan:
            return {false, false, "RHI-RENDER-PASS-STORAGE-VULKAN-PAYLOAD-MISSING"};
        case Backend::Metal:
            return {false, false, "RHI-RENDER-PASS-STORAGE-METAL-PAYLOAD-MISSING"};
        case Backend::WebGPU:
            return {false, false, "RHI-RENDER-PASS-STORAGE-WEBGPU-PAYLOAD-MISSING"};
        case Backend::WebGL:
            return {false, false, "RHI-RENDER-PASS-STORAGE-WEBGL-UNSUPPORTED"};
        }
        return {};
    }

    static Result inspect(const std::shared_ptr<const OriginalCommandStreamProgram>& program,
        const Budget& budget) noexcept
    {
        const auto reject = [](const char* id) {
            return Result{id, {}};
        };
        if (const auto* id = validate(budget))
            return reject(id);
        if (const auto* id = OriginalCommandStreamProgram::validateOriginal(program))
            return reject(id);
        if (!program->recordingBudget())
            return reject("RHI-RENDER-PASS-STORAGE-RECORDING-BUDGET");
        if (budget.arguments.maxCpuMetadataBytes != program->originalMetadataQuota()->limitBytes())
            return reject("RHI-RENDER-PASS-STORAGE-RECORDING-QUOTA");
        auto remaining = budget.arguments;
        Counts counts;
        const auto& inputs = *program->originalInputs();
        for (const auto& operation : program->operations()) {
            if (operation.kind != CommandUsageKind::BeginRenderPass)
                continue;
            const auto* descriptor = inputs.descriptorAt(operation.commandIndex);
            const auto* original = descriptor
                ? std::get_if<std::shared_ptr<const RenderPassArguments>>(descriptor)
                : nullptr;
            if (!original || !*original)
                return reject("RHI-RENDER-PASS-STORAGE-ORIGINAL-PASS");
            if (counts.passes == budget.maxPasses)
                return reject("RHI-RENDER-PASS-STORAGE-PASS-CAPACITY");
            const auto depth = std::uint64_t{(*original)->desc().hasDepthStencilAttachment};
            if (depth > budget.maxDepthAttachments - counts.depthAttachments)
                return reject("RHI-RENDER-PASS-STORAGE-DEPTH-CAPACITY");
            if (operation.referenceCount
                > budget.maxAttachmentReferences - counts.attachmentReferences)
                return reject("RHI-RENDER-PASS-STORAGE-REFERENCE-CAPACITY");
            if (const auto* id = (*original)->consumeCapacity(remaining))
                return reject(id);
            ++counts.passes;
            counts.depthAttachments += depth;
            counts.attachmentReferences += operation.referenceCount;
        }
        if (counts.passes != program->counts().renderPasses
            || counts.attachmentReferences != program->counts().attachmentReferences)
            return reject("RHI-RENDER-PASS-STORAGE-PROGRAM-CORRESPONDENCE");
        counts.colorAttachments =
            budget.arguments.maxColorAttachments - remaining.maxColorAttachments;
        counts.subpasses = budget.arguments.maxSubpasses - remaining.maxSubpasses;
        counts.attachmentIndices =
            budget.arguments.maxSubpassAttachmentIndices - remaining.maxSubpassAttachmentIndices;
        counts.dependencies = budget.arguments.maxDependencies - remaining.maxDependencies;
        counts.textBytes = budget.arguments.maxTextBytes - remaining.maxTextBytes;
        return {nullptr, counts};
    }

    // Full admission precedes callbacks, so a late shortage cannot leave a
    // consumer with a partially prepared prefix. Consumers reuse these exact
    // snapshots and command positions, never materialize a parallel pass graph.
    template <class Visitor>
    static Result visit(const std::shared_ptr<const OriginalCommandStreamProgram>& program,
        const Budget& budget,
        Visitor&& visitor) noexcept
    {
        auto result = inspect(program, budget);
        if (result.diagnostic)
            return result;
        for (const auto& operation : program->operations()) {
            if (operation.kind != CommandUsageKind::BeginRenderPass)
                continue;
            const auto& pass = std::get<std::shared_ptr<const RenderPassArguments>>(
                *program->originalInputs()->descriptorAt(operation.commandIndex));
            if (const auto* id = visitor(operation, pass))
                return {id, {}};
        }
        return result;
    }

    // Borrow canonical view/parent sources in the existing positional order.
    // Every literal occurrence is visited, including repeated snapshots. The
    // Program retains all lifetimes; this creates no secondary source inventory.
    template <class Visitor>
    static Result visitAttachments(
        const std::shared_ptr<const OriginalCommandStreamProgram>& program,
        const Budget& budget,
        Visitor&& visitor) noexcept
    {
        auto result = inspect(program, budget);
        if (result.diagnostic)
            return result;
        if (const auto* id = visitAttachmentsInScope(program, std::forward<Visitor>(visitor)))
            return {id, {}};
        return result;
    }

private:
    friend class OriginalRenderPassRecordingContract;

    // Only the serialized recording inspection may reuse its outer admission
    // for nested joins. No public unchecked traversal or persistent proof is
    // exposed; every logical attachment visit still consumes its original budget.
    template <class Visitor>
    static const char* visitAttachmentsInScope(
        const std::shared_ptr<const OriginalCommandStreamProgram>& program,
        Visitor&& visitor) noexcept
    {
        for (const auto& operation : program->operations()) {
            if (operation.kind != CommandUsageKind::BeginRenderPass)
                continue;
            const auto& pass = std::get<std::shared_ptr<const RenderPassArguments>>(
                *program->originalInputs()->descriptorAt(operation.commandIndex));
            const auto* diagnostic = [&]() noexcept -> const char* {
                const auto& usage = *program->originalInputs()->originalScalars()->originalUsage();
                return RenderPassReferenceSequenceContract::visit(
                    pass->desc(),
                    operation.referenceCount,
                    [&](std::size_t operand,
                        GpuResourceId,
                        TextureFormat,
                        RenderPassReferenceSequenceContract::Role role,
                        std::size_t colorIndex) noexcept -> const char* {
                        const auto sourceIndex =
                            usage.references()[operation.firstReference + operand].sourceIndex;
                        const auto& source =
                            std::get<std::shared_ptr<const OriginalCommandTextureViewSource>>(
                                usage.originalSources()->sources()[sourceIndex]);
                        return visitor(operation, *pass, operand, role, colorIndex, source);
                    },
                    "RHI-RENDER-PASS-STORAGE-ORIGINAL-REFERENCE");
            }();
            if (diagnostic)
                return diagnostic;
        }
        return nullptr;
    }
};

} // namespace arp::rhi

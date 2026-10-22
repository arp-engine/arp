#pragma once
#include "rhi/frontend/command_recording.h"
#include "rhi/semantics/bindless_initialized_graphics.h"

namespace arp::rhi {
// InitializedGraphics/2 is explicit Prototype intent over authenticated shader
// interfaces and original initialized ranges. Renderer record contents and
// conformance output transports are deliberately outside RHI admission.
struct OriginalBindlessInitializedGraphicsInputs {
    std::uint32_t contractVersion = 0u;
    bool optIn = false;
    std::uint64_t maxDescriptorVisits = 0u, maxCommandVisits = 0u;

    constexpr bool empty() const noexcept
    {
        return !contractVersion && !optIn && !maxDescriptorVisits && !maxCommandVisits;
    }

    constexpr const char* diagnostic() const noexcept
    {
        if (empty())
            return nullptr;
        if (contractVersion != semantics::BindlessInitializedGraphicsContract::version)
            return "RHI-BINDLESS-INITIALIZED-GRAPHICS-VERSION";
        if (!optIn)
            return "RHI-BINDLESS-INITIALIZED-GRAPHICS-OPT-IN";
        return maxDescriptorVisits && maxCommandVisits ? nullptr
                                                       : "RHI-BINDLESS-INITIALIZED-GRAPHICS-BUDGET";
    }
};

class OriginalBindlessInitializedGraphicsContract final {
public:
    using Inputs = OriginalBindlessInitializedGraphicsInputs;

    static const char* validateExecutionBudget(Backend backend,
        const Inputs& input,
        const OriginalBindlessRootNativeClosureBudget& closure) noexcept
    {
        if (const auto* id = input.diagnostic())
            return id;
        if (input.empty())
            return nullptr;
        if (const auto* id = OriginalBindlessRootNativeClosureContract::validateBudget(closure))
            return id;
        return semantics::BindlessInitializedGraphicsContract::lowering(backend,
            input.contractVersion,
            closure.contractVersion,
            OriginalBindlessRootNativeClosureContract::hasInitializedGeometry(closure),
            !closure.initializedShader.empty(),
            !closure.producedAttachments.empty());
    }

    static const char* validateRootInput(
        const std::shared_ptr<const OriginalCommandBindlessRootSource>& root,
        const Inputs& input) noexcept
    {
        if (const auto* id = input.diagnostic())
            return id;
        if (const auto* id = OriginalCommandBindlessRootSource::validateOriginal(root))
            return id;
        const auto* prepared = std::get_if<std::shared_ptr<const BindlessPreparedGraphicsBinding>>(
            &root->originalPrepared());
        if (!prepared)
            return input.empty() ? nullptr : "RHI-BINDLESS-INITIALIZED-GRAPHICS-PIPELINE";
        if (!*prepared || !(*prepared)->owners() || !(*prepared)->owners()->desc().pipeline)
            return "RHI-BINDLESS-INITIALIZED-GRAPHICS-PIPELINE";
        const auto& pipeline = *(*prepared)->owners()->desc().pipeline;
        const auto& package = root->authorization()->package();
        const auto* vp = pipeline.bindlessShaderModule(ShaderStage::Vertex, 1u);
        const auto* fp = pipeline.bindlessShaderModule(ShaderStage::Fragment, 1u);
        const BindlessPackageModule *vertex = nullptr, *fragment = nullptr;
        for (const auto& module : package.modules()) {
            if (&module == vp)
                vertex = &module;
            if (&module == fp)
                fragment = &module;
        }
        if (!vertex || !fragment)
            return input.empty() && !vp && !fp ? nullptr
                                               : "RHI-BINDLESS-INITIALIZED-GRAPHICS-ENTRY-OWNER";
        const auto selection = semantics::BindlessInitializedGraphicsContract::select(
            vertex->initializedGraphics, fragment->initializedGraphics);
        if (selection.diagnostic)
            return selection.diagnostic;
        if (!selection.recognized)
            return input.empty() ? nullptr : "RHI-BINDLESS-INITIALIZED-GRAPHICS-PROFILE-REQUIRED";
        return input.empty() ? "RHI-BINDLESS-INITIALIZED-GRAPHICS-INPUTS-REQUIRED" : nullptr;
    }

    template <class Closure>
    static const char* inspectNative(const std::shared_ptr<const Closure>& closure,
        const std::shared_ptr<const OriginalCommandStreamProgram>& stream,
        const Inputs& input) noexcept
    {
        if (const auto* id = Closure::validateOriginal(closure))
            return id;
        if (const auto* id = validateRootInput(closure->originalRoot(), input))
            return id;
        if (input.empty())
            return nullptr;
        if (const auto* id = OriginalCommandStreamProgram::validateOriginal(stream))
            return id;
        const auto& root = *closure->originalRoot();
        const auto& inventory = closure->originalInventory();
        if (!core::SharedOwnerProjection::sameOwner(inventory,
                stream->originalInputs()->originalScalars()->originalUsage()->originalSources())
            || !root.originalSnapshot()->sealed())
            return "RHI-BINDLESS-INITIALIZED-GRAPHICS-ROOT";
        using Owner = std::decay_t<decltype(closure->initializedShader(std::size_t{}))>;
        using Content = typename Owner::element_type;
        std::uint64_t visits = 0u;
        const auto inspect = [&](const auto& declaration) noexcept -> const char* {
            if (visits == input.maxDescriptorVisits)
                return "RHI-BINDLESS-INITIALIZED-GRAPHICS-DESCRIPTOR-CAPACITY";
            ++visits;
            const auto type = declaration.write.type;
            if (type != DescriptorType::UniformBuffer
                && type != DescriptorType::ReadOnlyStorageBuffer)
                return nullptr;
            if (declaration.dynamicOffset || declaration.sourceIndex >= inventory->sources().size())
                return "RHI-BINDLESS-INITIALIZED-GRAPHICS-BUFFER-SOURCE";
            const auto* lease = std::get_if<std::shared_ptr<const OriginalCommandBufferLease>>(
                &inventory->sources()[declaration.sourceIndex]);
            if (!lease)
                return "RHI-BINDLESS-INITIALIZED-GRAPHICS-BUFFER-SOURCE";
            return Content::visitInitializedShader(
                closure->initializedShader(declaration.sourceIndex),
                *lease,
                type,
                [&](const void*, std::size_t size) noexcept -> const char* {
                    DescriptorBufferBindingRangeContract::Resolved range;
                    if (const auto* id = DescriptorBufferBindingRangeContract::resolve(
                            declaration.write.bufferBindingRange, size, 0u, range))
                        return id;
                    return core::containsUnsignedRange(declaration.bufferAccessOffset,
                               declaration.bufferAccessBytes,
                               range.offsetBytes,
                               range.sizeBytes)
                        ? nullptr
                        : "RHI-BINDLESS-INITIALIZED-GRAPHICS-BUFFER-RANGE";
                });
        };
        for (const auto& element : root.elements()) {
            // Snapshot metadata is owned and initialized by the snapshot, with
            // its own original lease and journal checks. It is not a caller's
            // immutable shader initializer.
            const auto& metadata = root.authorization()->package().artifact().snapshotMetadata;
            if (metadata.contractVersion && element.declaration.write.binding == metadata.binding)
                continue;
            if (const auto* id = inspect(element.declaration))
                return id;
        }
        for (const auto& domain : root.ordinaryDomains())
            for (const auto& element : domain.source->frozenElements())
                if (const auto* id = inspect(element.declaration))
                    return id;
        std::uint64_t commands = 0u;
        for ([[maybe_unused]] const auto& op : stream->operations()) {
            if (commands == input.maxCommandVisits)
                return "RHI-BINDLESS-INITIALIZED-GRAPHICS-COMMAND-CAPACITY";
            ++commands;
        }
        return nullptr;
    }
};
} // namespace arp::rhi

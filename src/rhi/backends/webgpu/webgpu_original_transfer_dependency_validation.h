#pragma once
#include "core/utf8.h"
#include "rhi/backends/webgpu/webgpu_original_transfer_state_validation.h"
#include "rhi/semantics/transfer_dependency_lowering.h"

namespace arp::rhi_webgpu {
// OriginalTransferDependencyProfile/1: complete immutable input mapping only.
class WebGpuOriginalTransferDependencyValidation final {
    using K = rhi::CommandUsageKind;

public:
    struct Mapping {
        Mapping(const char* id) noexcept : diagnostic(id) {}

        Mapping() = default;
        const char* diagnostic = nullptr;
        std::array<rhi::semantics::TransferDependencyLowering::Decision, 2> dependencies;
    };

    static const char* validate(
        const std::shared_ptr<const WebGpuOriginalResourceStates>& states) noexcept
    {
        if (!states || !states->hasQuotaBoundSharedOwner(states))
            return "RHI-WEBGPU-ORIGINAL-STATE-OWNER";
        if (const auto* id = states->validate())
            return id;
        return validateInitialMapping(states);
    }

    // OriginalDependencyInitialMapping/1. This never validates current state
    // and cannot replace exact transaction pending/committed evidence.
    static const char* validateInitialMapping(
        const std::shared_ptr<const WebGpuOriginalResourceStates>& states) noexcept
    {
        return initialMapping(states).diagnostic;
    }

    static Mapping initialMapping(
        const std::shared_ptr<const WebGpuOriginalResourceStates>& states) noexcept
    {
        if (!states || !states->hasQuotaBoundSharedOwner(states))
            return "RHI-WEBGPU-ORIGINAL-STATE-OWNER";
        if (const auto* id = states->validateSourceOwners())
            return id;
        const auto& inputs = *states->inputs();
        const auto& usage = *inputs.scalars()->usage();
        const auto& graph = *usage.projection();
        const auto& commands = usage.commands();
        if (commands.size() != 6u || !dependency(commands[0].kind) || !dependency(commands[1].kind)
            || commands[2].kind != K::BeginTransferPass || commands[4].kind != K::EndTransferPass
            || commands[5].kind != K::Finish)
            return "RHI-WEBGPU-ORIGINAL-DEPENDENCY-LANGUAGE";
        if (!graph.transfer() || graph.resources() || !graph.pipelines().empty()
            || !graph.physicalGroups().empty() || graph.references().size() != 4u || inputs.passes()
            || inputs.descriptors().size() != 3u || inputs.descriptors()[0].commandIndex != 0u
            || inputs.descriptors()[1].commandIndex != 1u
            || inputs.descriptors()[2].commandIndex != 3u
            || states->sourceReferences() != graph.transfer()->resources().size()
            || states->states().size() != graph.transfer()->resources().size())
            return "RHI-WEBGPU-ORIGINAL-DEPENDENCY-SOURCE-COVERAGE";
        const auto* copy =
            std::get_if<rhi::TransferCopyArguments>(&inputs.descriptors()[2].arguments);
        if (!copy || commands[3].kind != copy->kind())
            return "RHI-WEBGPU-ORIGINAL-DEPENDENCY-LANGUAGE";
        if (const auto* id =
                WebGpuOriginalTransferStateValidation::validateInitialSequence(states, 0u)
                    .diagnostic)
            return id;
        const auto label = inputs.scalars()->arguments()->text(2u);
        if (label.find('\0') != std::string_view::npos || core::Utf8Decoder::validate(label))
            return "RHI-WEBGPU-ORIGINAL-DEPENDENCY-LABEL-ENCODING";
        Mapping mapping;
        for (std::size_t i = 0u; i < mapping.dependencies.size(); ++i) {
            const auto* d =
                std::get_if<rhi::ResourceDependencyArguments>(&inputs.descriptors()[i].arguments);
            if (!d || d->kind() != commands[i].kind)
                return "RHI-WEBGPU-ORIGINAL-STATE-DESCRIPTOR";
            rhi::ResourceBarrierDesc transition;
            rhi::ResourceMemoryBarrierDesc memory;
            const bool changes =
                commands[i].kind == K::BufferBarrier || commands[i].kind == K::TextureBarrier;
            if (changes ? !d->write(d->kind(), transition) : !d->write(d->kind(), memory))
                return "RHI-WEBGPU-ORIGINAL-STATE-DESCRIPTOR";
            const auto resource = changes ? transition.resource : memory.resource;
            if (resource != (i == 0u ? copy->source() : copy->destination()))
                return "RHI-WEBGPU-ORIGINAL-DEPENDENCY-COPY-BINDING";
            const auto* source = graph.transfer()->find(resource);
            if (!source)
                return "RHI-WEBGPU-ORIGINAL-DEPENDENCY-SOURCE-COVERAGE";
            rhi::ResourceState current;
            if (const auto* id = WebGpuOriginalTransferStateValidation::stateBefore(
                    *states, resource, i, current))
                return id;
            using Lowering = rhi::semantics::TransferDependencyLowering;
            const auto decision = changes
                ? Lowering::transition(rhi::Backend::WebGPU,
                      1u,
                      true,
                      transition,
                      current,
                      bool(source->texture),
                      source->texture ? rhi::defaultTextureSubresourceRange(source->textureDesc)
                                      : rhi::TextureSubresourceRange{})
                : Lowering::memory(rhi::Backend::WebGPU, 1u, true, memory, current);
            if (decision.diagnostic)
                return decision.diagnostic;
            if (decision.outcome != rhi::CapabilityMode::Equivalent
                || decision.operation != Lowering::Operation::ImplicitOrderedUsage)
                return "RHI-WEBGPU-ORIGINAL-DEPENDENCY-LOWERING-UNPROVEN";
            mapping.dependencies[i] = decision;
        }
        return mapping;
    }

    static const char* executionDiagnostic(
        const std::shared_ptr<const WebGpuOriginalResourceStates>& states) noexcept
    {
        if (const auto* id = validate(states))
            return id;
        return webGpuGenericRecordingUsageDiagnostic;
    }

private:
    static bool dependency(K kind) noexcept
    {
        return kind == K::BufferBarrier || kind == K::TextureBarrier
            || kind == K::BufferMemoryBarrier || kind == K::TextureMemoryBarrier;
    }
};
} // namespace arp::rhi_webgpu

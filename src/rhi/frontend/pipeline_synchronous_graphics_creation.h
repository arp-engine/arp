#pragma once

#include "rhi/gpu_capabilities.h"
#include "rhi/gpu_pipeline.h"
#include "rhi/semantics/pipeline_creation_lowering.h"

namespace arp::rhi {

// Cold creation from the two original selected-entry artifacts. CPU ceilings
// cover the receipt and native builder metadata, not opaque driver allocations.
// This opt-in never grants recording/execution or a public device capability.
class OriginalSynchronousGraphicsCreationContract final {
public:
    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint64_t maxTargetBytes = 0u, maxLabelBytes = 0u, maxCpuOwnerBytes = 0u,
                      maxCpuNativeBytes = 0u;
    };

    struct Facts {
        bool device = false, methods = false;
    };

    struct Decision {
        CapabilityMode mode = CapabilityMode::DiagnosticFailure;
        bool validatorOnly = false, creationEligible = false, executionAvailable = false;
        const char* diagnostic = "RHI-SYNC-GRAPHICS-CREATION-BACKEND";
    };

    // Version 1 is the selected-entry, vertex-ID, single-target profile.
    // Optional pipeline features require independent queried/enablement facts
    // before an expanded version may admit them.
    static const char* validateProfile(const RenderPipelineDesc& d) noexcept
    {
        if (!d.layout || !d.shaderPackage)
            return "RHI-SYNC-GRAPHICS-CREATION-INPUT";
        if (!d.layout->desc().descriptorTables.empty() || d.renderPassCompatibility.enabled()
            || d.subpassIndex || d.topology == PrimitiveTopology::PatchList
            || d.patchControlPointCount || !d.tessellationControlShader.entryPoint.empty()
            || !d.tessellationEvaluationShader.entryPoint.empty() || !d.vertexBuffers.empty()
            || d.colorTargets.size() != 1u || d.multisample.sampleCount != 1u
            || d.depthStencil.depthBiasClamp != 0.0f)
            return "RHI-SYNC-GRAPHICS-CREATION-PROFILE";
        for (const auto& layout : d.layout->desc().descriptorLayouts)
            for (const auto& binding : layout.bindings)
                if (binding.type != DescriptorType::SampledTexture
                    && binding.type != DescriptorType::Sampler
                    && binding.type != DescriptorType::UniformBuffer
                    && binding.type != DescriptorType::ReadOnlyStorageBuffer)
                    return "RHI-SYNC-GRAPHICS-CREATION-PROFILE";
        return nullptr;
    }

    static constexpr const char* validateBudget(const Budget& b) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-SYNC-GRAPHICS-CREATION-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-SYNC-GRAPHICS-CREATION-OPT-IN";
        return b.maxTargetBytes && b.maxLabelBytes && b.maxCpuOwnerBytes && b.maxCpuNativeBytes
            ? nullptr
            : "RHI-SYNC-GRAPHICS-CREATION-BUDGET";
    }

    static constexpr Decision lowering(Backend backend, const Budget& b, Facts f) noexcept
    {
        if (const auto* id = validateBudget(b))
            return {CapabilityMode::DiagnosticFailure, false, false, false, id};
        return semantics::PipelineCreationLowering::synchronousGraphicsCreation<Decision>(
            backend, f.device, f.methods);
    }
};

} // namespace arp::rhi

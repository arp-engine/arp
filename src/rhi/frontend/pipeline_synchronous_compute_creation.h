#pragma once

#include "rhi/gpu_pipeline.h"
#include "rhi/semantics/pipeline_creation_lowering.h"

namespace arp::rhi {

// Cold immutable selected-entry creation. Independent owner/native metadata
// ceilings exclude opaque driver allocations and grant no command authority.
class OriginalSynchronousComputeCreationContract final {
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
        const char* diagnostic = "RHI-SYNC-COMPUTE-CREATION-BACKEND";
    };

    static constexpr const char* validateBudget(const Budget& b) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-SYNC-COMPUTE-CREATION-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-SYNC-COMPUTE-CREATION-OPT-IN";
        return b.maxTargetBytes && b.maxLabelBytes && b.maxCpuOwnerBytes && b.maxCpuNativeBytes
            ? nullptr
            : "RHI-SYNC-COMPUTE-CREATION-BUDGET";
    }

    // /1 has one ordinary set0 with explicit buffer or sampled texture inputs,
    // no tables, samplers, push constants, storage images or optional shader
    // capabilities. The selected binary must also prove its exact LocalSize.
    static const char* validateProfile(const ComputePipelineDesc& d) noexcept
    {
        if (!d.layout || !d.shaderPackage)
            return "RHI-SYNC-COMPUTE-CREATION-INPUT";
        const auto& layout = d.layout->desc();
        if (d.shaderPackage->entrypoints.size() != 1u
            || d.computeShader.stage != ShaderStage::Compute || !layout.descriptorTables.empty()
            || !layout.pushConstantRanges.empty() || layout.descriptorSets.size() != 1u
            || layout.descriptorSets.front().set != 0u || layout.descriptorLayouts.size() != 1u)
            return "RHI-SYNC-COMPUTE-CREATION-PROFILE";
        for (const auto& binding : layout.descriptorLayouts.front().bindings)
            if (binding.stages != ShaderStage::Compute || binding.arrayCount != 1u
                || binding.dynamicOffset || binding.tableBacked
                || (binding.type != DescriptorType::UniformBuffer
                    && binding.type != DescriptorType::ReadOnlyStorageBuffer
                    && binding.type != DescriptorType::StorageBuffer
                    && binding.type != DescriptorType::SampledTexture))
                return "RHI-SYNC-COMPUTE-CREATION-PROFILE";
        return nullptr;
    }

    static constexpr Decision lowering(Backend backend, const Budget& b, Facts f) noexcept
    {
        if (const auto* id = validateBudget(b))
            return {CapabilityMode::DiagnosticFailure, false, false, false, id};
        return semantics::PipelineCreationLowering::synchronousComputeCreation<Decision>(
            backend, f.device, f.methods);
    }
};

} // namespace arp::rhi

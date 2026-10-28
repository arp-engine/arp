#pragma once
#include "rhi/frontend/pipeline_depth_table_lowering_validation.h"
#include "rhi/frontend/pipeline_shader_interface_validation.h"
#include "rhi/gpu_capabilities.h"
#include "rhi/gpu_common.h"
#include "rhi/semantics/pipeline_creation_lowering.h"
#include "rhi/semantics/webgpu_error_scope.h"

// region Pipeline artifact creation budget validation
namespace arp::rhi {
class OriginalArtifactCreationBudgetValidation final {
public:
    template <class Budget>
    static const char* validateEnvelope(
        const Budget& b, const char* version, const char* optIn, const char* capacity) noexcept
    {
        if (b.contractVersion != 1u)
            return version;
        if (!b.prototypeOptIn)
            return optIn;
        if (b.maxCallbacks < rhi_webgpu::WebGpuErrorScopeContract::count || !b.maxLabelBytes
            || !b.maxDiagnosticBytes || !b.maxCpuOwnerBytes || !b.maxCpuCallbackBytes)
            return capacity;
        return nullptr;
    }
};

// endregion Pipeline artifact creation budget validation

// region Pipeline creation validation
// region original_accepted_pipeline_factory

class OriginalAcceptedPipelineFactoryContract final {
public:
    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint64_t maxCpuWrapperBytes = 0u;
    };

    struct Decision {
        CapabilityMode mode = CapabilityMode::DiagnosticFailure;
        bool validatorOnly = false, factoryEligible = false, executionAvailable = false;
        const char* diagnostic = nullptr;
    };

    struct Diagnostics {
        const char *version, *optIn, *budget, *backend, *nullBackend, *vulkan, *metal, *missing;
    };

    static constexpr const char* validateBudget(const Budget& b, Diagnostics d) noexcept
    {
        if (b.contractVersion != 1u)
            return d.version;
        if (!b.prototypeOptIn)
            return d.optIn;
        return b.maxCpuWrapperBytes ? nullptr : d.budget;
    }

    static constexpr Decision lowering(Backend backend, bool accepted, Diagnostics d) noexcept
    {
        return semantics::PipelineCreationLowering::OriginalAcceptedPipelineFactory_lowering<
            Decision>(backend, accepted, d);
    }
};
} // namespace arp::rhi

// endregion original_accepted_pipeline_factory

// region original_accepted_graphics_pipeline_factory
namespace arp::rhi {
class OriginalAcceptedGraphicsPipelineFactoryContract final {
    using Shared = OriginalAcceptedPipelineFactoryContract;
    static constexpr Shared::Diagnostics diagnostics{"RHI-ACCEPTED-GRAPHICS-FACTORY-VERSION",
        "RHI-ACCEPTED-GRAPHICS-FACTORY-OPT-IN",
        "RHI-ACCEPTED-GRAPHICS-FACTORY-BUDGET",
        "RHI-ACCEPTED-GRAPHICS-FACTORY-BACKEND",
        "RHI-ACCEPTED-GRAPHICS-FACTORY-NULL-VALIDATOR-ONLY",
        "RHI-ACCEPTED-GRAPHICS-FACTORY-VULKAN-PAYLOAD-MISSING",
        "RHI-ACCEPTED-GRAPHICS-FACTORY-METAL-PAYLOAD-MISSING",
        "RHI-ACCEPTED-GRAPHICS-FACTORY-CREATION-MISSING"};

public:
    using Budget = Shared::Budget;
    using Decision = Shared::Decision;

    static constexpr const char* validateBudget(const Budget& b) noexcept
    {
        if (b.contractVersion == 2u)
            return !b.prototypeOptIn    ? diagnostics.optIn
                : !b.maxCpuWrapperBytes ? diagnostics.budget
                                        : nullptr;
        return Shared::validateBudget(b, diagnostics);
    }

    static constexpr Decision lowering(
        Backend backend, bool acceptedOriginalCreation = false, std::uint32_t version = 1u) noexcept
    {
        if (version == 2u)
            return semantics::PipelineCreationLowering::acceptedSynchronousFactory<Decision>(
                backend,
                acceptedOriginalCreation,
                diagnostics.missing,
                "RHI-ACCEPTED-GRAPHICS-FACTORY-SYNC-PROFILE");
        if (version != 1u)
            return {CapabilityMode::DiagnosticFailure, false, false, false, diagnostics.version};
        return Shared::lowering(backend, acceptedOriginalCreation, diagnostics);
    }
};

// endregion original_accepted_graphics_pipeline_factory

// region original_accepted_compute_pipeline_factory

class OriginalAcceptedComputePipelineFactoryContract final {
    using Shared = OriginalAcceptedPipelineFactoryContract;
    static constexpr Shared::Diagnostics diagnostics{"RHI-ACCEPTED-COMPUTE-FACTORY-VERSION",
        "RHI-ACCEPTED-COMPUTE-FACTORY-OPT-IN",
        "RHI-ACCEPTED-COMPUTE-FACTORY-BUDGET",
        "RHI-ACCEPTED-COMPUTE-FACTORY-BACKEND",
        "RHI-ACCEPTED-COMPUTE-FACTORY-NULL-VALIDATOR-ONLY",
        "RHI-ACCEPTED-COMPUTE-FACTORY-VULKAN-PAYLOAD-MISSING",
        "RHI-ACCEPTED-COMPUTE-FACTORY-METAL-PAYLOAD-MISSING",
        "RHI-ACCEPTED-COMPUTE-FACTORY-CREATION-MISSING"};

public:
    using Budget = Shared::Budget;
    using Decision = Shared::Decision;

    static constexpr const char* validateBudget(const Budget& b) noexcept
    {
        if (b.contractVersion == 2u)
            return !b.prototypeOptIn    ? diagnostics.optIn
                : !b.maxCpuWrapperBytes ? diagnostics.budget
                                        : nullptr;
        return Shared::validateBudget(b, diagnostics);
    }

    static constexpr Decision lowering(
        Backend backend, bool acceptedOriginalCreation = false, std::uint32_t version = 1u) noexcept
    {
        if (version == 2u)
            return semantics::PipelineCreationLowering::acceptedSynchronousFactory<Decision>(
                backend,
                acceptedOriginalCreation,
                diagnostics.missing,
                "RHI-ACCEPTED-COMPUTE-FACTORY-SYNC-PROFILE");
        if (version != 1u)
            return {CapabilityMode::DiagnosticFailure, false, false, false, diagnostics.version};
        return Shared::lowering(backend, acceptedOriginalCreation, diagnostics);
    }
};

// endregion original_accepted_compute_pipeline_factory

// region original_pipeline_artifact_consumption

class OriginalPipelineArtifactConsumptionContract final {
public:
    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint64_t maxTargetBytes = 0u, maxCpuMetadataBytes = 0u;
    };

    struct Decision {
        CapabilityMode mode = CapabilityMode::DiagnosticFailure;
        bool validatorOnly = false, executionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-ARTIFACT-CONSUMPTION-BACKEND";
    };

    static constexpr const char* validateBudget(Budget budget) noexcept
    {
        if (budget.contractVersion != 1u)
            return "RHI-ORIGINAL-ARTIFACT-CONSUMPTION-VERSION";
        if (!budget.prototypeOptIn)
            return "RHI-ORIGINAL-ARTIFACT-CONSUMPTION-OPT-IN";
        if (!budget.maxTargetBytes || !budget.maxCpuMetadataBytes)
            return "RHI-ORIGINAL-ARTIFACT-CONSUMPTION-BUDGET";
        return nullptr;
    }

    static constexpr const char* creationValidationDiagnostic() noexcept
    {
        return "RHI-ORIGINAL-ARTIFACT-CREATION-RECEIPT-MISSING";
    }

    // WebGPU availability is an actual native device query, never backend-name
    // inference. This selected profile grants no canonical command execution.
    static constexpr Decision lowering(Backend backend, bool nativeDeviceAvailable = false) noexcept
    {
        return semantics::PipelineCreationLowering::OriginalPipelineArtifactConsumption_lowering<
            Decision>(backend, nativeDeviceAvailable);
    }
};

// endregion original_pipeline_artifact_consumption

// region original_artifact_compute_creation

class OriginalArtifactComputeCreationContract final {
public:
    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint32_t maxCallbacks = 0u;
        std::uint64_t maxLabelBytes = 0u, maxDiagnosticBytes = 0u, maxCpuOwnerBytes = 0u,
                      maxCpuCallbackBytes = 0u;
        OriginalPipelineArtifactConsumptionContract::Budget native;
    };

    static const char* validateBudget(const Budget& b) noexcept
    {
        if (const auto* id = OriginalArtifactCreationBudgetValidation::validateEnvelope(b,
                "RHI-ARTIFACT-CREATION-VERSION",
                "RHI-ARTIFACT-CREATION-OPT-IN",
                "RHI-ARTIFACT-CREATION-BUDGET"))
            return id;
        return rhi::OriginalPipelineArtifactConsumptionContract::validateBudget(b.native);
    }
};

// endregion original_artifact_compute_creation

// region original_artifact_graphics_creation

class OriginalArtifactGraphicsCreationContract final {
public:
    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint32_t maxCallbacks = 0u;
        std::uint64_t maxLabelBytes = 0u, maxDiagnosticBytes = 0u, maxCpuOwnerBytes = 0u,
                      maxCpuCallbackBytes = 0u;
        OriginalGraphicsArtifactConsumptionContract::Budget native;
    };

    static const char* validateBudget(const Budget& b) noexcept
    {
        if (const auto* id = OriginalArtifactCreationBudgetValidation::validateEnvelope(b,
                "RHI-GRAPHICS-CREATION-VERSION",
                "RHI-GRAPHICS-CREATION-OPT-IN",
                "RHI-GRAPHICS-CREATION-BUDGET"))
            return id;
        return OriginalGraphicsArtifactConsumptionContract::validateBudget(b.native);
    }
};

// endregion original_artifact_graphics_creation
// endregion Pipeline creation validation

// region Pipeline depth table creation validation
// region original_depth_table_expansion_creation

class OriginalDepthTableExpansionCreationContract final {
public:
    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint32_t maxCallbacks = 0u;
        std::uint64_t maxLabelBytes = 0u, maxDiagnosticBytes = 0u, maxCpuOwnerBytes = 0u,
                      maxCpuCallbackBytes = 0u;
        OriginalDepthTableExpansionNativeEmissionContract::Budget native;
    };

    static const char* validateBudget(const Budget& b) noexcept
    {
        if (const auto* id = OriginalArtifactCreationBudgetValidation::validateEnvelope(b,
                "RHI-ORIGINAL-TABLE-CREATION-VERSION",
                "RHI-ORIGINAL-TABLE-CREATION-OPT-IN",
                "RHI-ORIGINAL-TABLE-CREATION-BUDGET"))
            return id;
        return OriginalDepthTableExpansionNativeEmissionContract::validateBudget(b.native);
    }

    struct Decision {
        CapabilityMode mode = CapabilityMode::DiagnosticFailure;
        bool validatorOnly = false, creationEligible = false, executionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-TABLE-CREATION-BACKEND";
    };

    static Decision lowering(Backend backend,
        const OriginalDepthTableExpansionNativeEmissionContract::Decision& emission) noexcept
    {
        return semantics::PipelineCreationLowering::OriginalDepthTableExpansionCreation_lowering<
            Decision>(backend, emission);
    }

    struct WrapperDecision {
        CapabilityMode mode = CapabilityMode::DiagnosticFailure;
        bool validatorOnly = false, acceptanceEligible = false, executionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-TABLE-WRAPPER-BACKEND";
    };

    static WrapperDecision wrapperLowering(
        Backend backend, bool readyOriginalActor, bool actualDescriptorLimits) noexcept
    {
        return semantics::PipelineCreationLowering::
            OriginalDepthTableExpansionCreation_wrapperLowering<WrapperDecision>(
                backend, readyOriginalActor, actualDescriptorLimits);
    }
};
} // namespace arp::rhi

namespace arp::rhi {
// Explicit publication request for an accepted expanded actor in Registration/8.
// This uses the existing wrapper/candidate transaction, never command authority.
class OriginalAcceptedTableExpansionPipelineContract final {
    using Shared = OriginalAcceptedPipelineFactoryContract;
    static constexpr Shared::Diagnostics diagnostics{"RHI-ORIGINAL-TABLE-FACTORY-VERSION",
        "RHI-ORIGINAL-TABLE-FACTORY-OPT-IN",
        "RHI-ORIGINAL-TABLE-FACTORY-BUDGET",
        "RHI-ORIGINAL-TABLE-FACTORY-BACKEND",
        "RHI-ORIGINAL-TABLE-FACTORY-NULL-VALIDATOR-ONLY",
        "RHI-ORIGINAL-TABLE-FACTORY-VULKAN-PAYLOAD-MISSING",
        "RHI-ORIGINAL-TABLE-FACTORY-METAL-PAYLOAD-MISSING",
        "RHI-ORIGINAL-TABLE-FACTORY-CREATION-MISSING"};

public:
    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint64_t maxCpuWrapperBytes = 0u;
    };

    using Decision = Shared::Decision;

    static constexpr const char* validateBudget(Budget b) noexcept
    {
        return Shared::validateBudget(
            {b.contractVersion, b.prototypeOptIn, b.maxCpuWrapperBytes}, diagnostics);
    }

    static constexpr Decision lowering(Backend backend, bool accepted) noexcept
    {
        return Shared::lowering(backend, accepted, diagnostics);
    }
};
} // namespace arp::rhi

// endregion original_depth_table_expansion_creation
// endregion Pipeline depth table creation validation

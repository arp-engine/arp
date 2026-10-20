#pragma once
#include "rhi/gpu_bindless.h"

namespace arp::rhi::semantics {
// Cold original scalar buffers: /1 Compute; /2 Fragment with an unindexed
// Vertex stage; /3 adds Fragment sampled 2D/2DArray and independent samplers.
// /4 prepares depth Snapshots only, with one Compute or Fragment stage.
// /5 additionally prepares a pipeline/physical binding over authentic depth.
// /6 prepares the Native Compute Root over a single Source2/control4 cell.
// /7 publishes bounded RGBA16F Compute cell snapshots on the three devices.
// Authorization and prepared pipelines remain independently closed for /7.
// /8 adds verified storage pipeline/physical binding preparation only.
// /9 retains authentic completed D32 and RGBA16F storage/sampled cells in
// one cold snapshot. Authorization and all old native closures stay closed.
// /10 adds shared authorization and authentic per-entry physical preparation
// for that mixed domain. /9 remains snapshot-only; old closures stay closed.
// /11 prepares ordinary color Graphics; /12 adds scalar ordinary Fragment
// depth/comparison declarations only. Neither publishes an executable plan.
// None can record, submit, publish a result or
// select an application mode. Capabilities are independent exact versions.
class OriginalBindlessPreparationLowering final {
public:
    struct Decision {
        bool preparationAvailable = false;
        bool validatorOnly = false;
        bool executionAvailable = false;
        const char* diagnostic = "RHI-BINDLESS-ORIGINAL-PREPARATION-BACKEND";
    };

    static constexpr Decision lowering(Backend backend, std::uint32_t version = 1u) noexcept
    {
        if (version != 1u && version != 2u && version != 3u && version != 4u && version != 5u
            && version != 6u && version != 7u && version != 8u && version != 9u && version != 10u
            && version != 11u && version != 12u)
            return {false, false, false, "RHI-BINDLESS-ORIGINAL-PREPARATION-VERSION"};
        if (version == 12u) {
            switch (backend) {
            case Backend::Null:
                return {false,
                    true,
                    false,
                    "RHI-BINDLESS-ORDINARY-DEPTH-PREPARATION-NULL-VALIDATOR-ONLY"};
            case Backend::WebGPU:
                return {true, false, false, nullptr};
            case Backend::Vulkan:
                return {
                    false, false, false, "RHI-VULKAN-BINDLESS-ORDINARY-DEPTH-PREPARATION-PROFILE"};
            case Backend::Metal:
                return {
                    false, false, false, "RHI-METAL-BINDLESS-ORDINARY-DEPTH-PREPARATION-PROFILE"};
            case Backend::WebGL:
                return {false,
                    false,
                    false,
                    "RHI-WEBGL1-BINDLESS-ORDINARY-DEPTH-PREPARATION-UNSUPPORTED"};
            }
            return {};
        }
        if (version == 11u) {
            switch (backend) {
            case Backend::Null:
                return {false,
                    true,
                    false,
                    "RHI-BINDLESS-ORDINARY-COLOR-PREPARATION-NULL-VALIDATOR-ONLY"};
            case Backend::WebGPU:
                return {true, false, false, nullptr};
            case Backend::Vulkan:
                return {false,
                    false,
                    false,
                    "RHI-VULKAN-BINDLESS-ORDINARY-COLOR-PREPARATION-UNAVAILABLE"};
            case Backend::Metal:
                return {false,
                    false,
                    false,
                    "RHI-METAL-BINDLESS-ORDINARY-COLOR-PREPARATION-UNAVAILABLE"};
            case Backend::WebGL:
                return {false,
                    false,
                    false,
                    "RHI-WEBGL1-BINDLESS-ORDINARY-COLOR-PREPARATION-UNSUPPORTED"};
            }
            return {};
        }
        if (version == 7u || version == 8u || version == 9u || version == 10u) {
            switch (backend) {
            case Backend::Null:
                return {false, true, false, "RHI-BINDLESS-ORIGINAL-PREPARATION-NULL-PROFILE"};
            case Backend::Vulkan:
            case Backend::Metal:
            case Backend::WebGPU:
                return {true, false, false, nullptr};
            case Backend::WebGL:
                break;
            }
            return {};
        }
        if (version == 6u) {
            switch (backend) {
            case Backend::Null:
                return {false, true, false, "RHI-BINDLESS-ORIGINAL-PREPARATION-NULL-PROFILE"};
            case Backend::Vulkan:
                return {true, false, false, nullptr};
            case Backend::Metal:
                return {true, false, false, nullptr};
            case Backend::WebGPU:
                return {false, false, false, "RHI-BINDLESS-ORIGINAL-PREPARATION-WEBGPU-PROFILE"};
            case Backend::WebGL:
                break;
            }
            return {};
        }
        switch (backend) {
        case Backend::Null:
            return {false, false, false, "RHI-BINDLESS-ORIGINAL-PREPARATION-NULL-PROFILE"};
        case Backend::Vulkan:
            return {false, false, false, "RHI-BINDLESS-ORIGINAL-PREPARATION-VULKAN-PROFILE"};
        case Backend::Metal:
            return {false, false, false, "RHI-BINDLESS-ORIGINAL-PREPARATION-METAL-UNAVAILABLE"};
        case Backend::WebGPU:
            return {true, false, false, nullptr};
        case Backend::WebGL:
            break;
        }
        return {};
    }

    static constexpr bool capabilityAvailable(
        const BindlessCapabilities& caps, std::uint32_t version) noexcept
    {
        return caps.limitsQueried
            && ((version == 12u
                    && caps.originalOrdinaryDepthGraphicsPreparationContractVersion == 12u
                    && (caps.execution == BindlessExecution::Emulated
                        || caps.execution == BindlessExecution::DiagnosticFailure))
                || (version == 11u
                    && caps.originalOrdinaryColorGraphicsPreparationContractVersion == 11u)
                || (version == 1u && caps.originalPreparationContractVersion == 1u)
                || (version == 2u && caps.originalGraphicsPreparationContractVersion == 2u)
                || (version == 3u && caps.originalSampledGraphicsPreparationContractVersion == 3u)
                || (version == 4u && caps.originalDepthSnapshotPreparationContractVersion == 4u)
                || (version == 5u && caps.originalDepthRootPreparationContractVersion == 5u)
                || (version == 6u && caps.originalNativeDepthRootPreparationContractVersion == 6u
                    && caps.execution == BindlessExecution::Native)
                || ((version == 7u && caps.originalStoragePreparationContractVersion == 7u
                        || version == 8u
                            && caps.originalStorageRootPreparationContractVersion == 8u)
                    && (caps.execution == BindlessExecution::Native
                        || caps.execution == BindlessExecution::Emulated
                        || caps.execution == BindlessExecution::DiagnosticFailure))
                || ((version == 9u
                        || (version == 10u
                            && caps.originalMixedImageRootPreparationContractVersion == 10u
                            && caps.originalStorageRootPreparationContractVersion == 8u))
                    && caps.originalMixedImageSnapshotPreparationContractVersion == 9u
                    && caps.originalStoragePreparationContractVersion == 7u
                    && (caps.execution == BindlessExecution::Native
                            ? caps.originalNativeDepthRootPreparationContractVersion == 6u
                            : (caps.execution == BindlessExecution::Emulated
                                  || caps.execution == BindlessExecution::DiagnosticFailure)
                                && caps.originalDepthSnapshotPreparationContractVersion == 4u)));
    }

    static constexpr bool hasMixedImageSnapshotPlan(const BindlessLoweringPlan& plan) noexcept
    {
        return hasPlan(plan, 9u);
    }

    static constexpr bool hasMixedImageBindingPlan(const BindlessLoweringPlan& plan) noexcept
    {
        return hasPlan(plan, 10u);
    }

    static constexpr bool hasMixedImageExecutionPlan(const BindlessLoweringPlan& plan) noexcept
    {
        return plan.success && plan.executable && !plan.originalPreparationContractVersion
            && ((plan.originalExecutionContractVersion == 11u
                    && plan.execution == BindlessExecution::Native)
                || (plan.originalExecutionContractVersion == 12u
                    && plan.execution == BindlessExecution::Emulated));
    }

    static constexpr bool hasMixedImageResourcePlan(const BindlessLoweringPlan& plan) noexcept
    {
        return hasMixedImageBindingPlan(plan) || hasMixedImageExecutionPlan(plan);
    }

    // Shared physical projection, not an alias for hasStoragePlan: a depth
    // element requires its original completed proof, all color cells theirs.
    static constexpr bool hasImageCellBindingPlan(const BindlessLoweringPlan& plan) noexcept
    {
        return hasStoragePlan(plan) || hasMixedImageResourcePlan(plan)
            || hasProducedImageReadPlan(plan);
    }

    static constexpr bool hasProducedImageReadPlan(const BindlessLoweringPlan& plan) noexcept
    {
        return plan.success && plan.executable && !plan.originalPreparationContractVersion
            && ((plan.originalExecutionContractVersion == 15u
                    && plan.execution == BindlessExecution::Native)
                || (plan.originalExecutionContractVersion == 16u
                    && plan.execution == BindlessExecution::Emulated));
    }

    static constexpr const char* imageProofDiagnostic(const BindlessLoweringPlan& plan,
        const DescriptorBindingDesc& binding,
        bool completedDepth,
        bool colorCell) noexcept
    {
        if (hasProducedImageReadPlan(plan))
            return binding.type == DescriptorType::SampledTexture
                    && binding.stages == ShaderStage::Compute && !binding.depthTexture
                    && binding.textureViewDimension == TextureViewDimension::Texture2D
                    && !completedDepth && colorCell
                ? nullptr
                : "RHI-BINDLESS-PRODUCED-IMAGE-PUBLICATION";
        const bool depth = hasMixedImageResourcePlan(plan)
            && binding.type == DescriptorType::SampledTexture && binding.depthTexture;
        return hasImageCellBindingPlan(plan) && completedDepth == depth && colorCell == !depth
            ? nullptr
            : "RHI-BINDLESS-IMAGE-CELL-BINDING-PROOF";
    }

    // Shared publication ABI; cold preparation does not grant execution.
    static constexpr bool hasStoragePlan(const BindlessLoweringPlan& plan) noexcept
    {
        return hasPlan(plan, 8u)
            || (plan.success && plan.executable && !plan.originalPreparationContractVersion
                && ((plan.originalExecutionContractVersion == 9u
                        && plan.execution == BindlessExecution::Native)
                    || (plan.originalExecutionContractVersion == 10u
                        && plan.execution == BindlessExecution::Emulated)));
    }

    static constexpr bool hasPlan(const BindlessLoweringPlan& plan, std::uint32_t version) noexcept
    {
        return (version == 1u || version == 2u || version == 3u || version == 4u || version == 5u
                   || version == 6u || version == 7u || version == 8u || version == 9u
                   || version == 10u || version == 11u || version == 12u)
            && plan.success && !plan.executable
            && plan.originalPreparationContractVersion == version
            && !plan.originalExecutionContractVersion
            && (version == 7u || version == 8u || version == 9u || version == 10u
                    ? plan.execution == BindlessExecution::Native
                        || plan.execution == BindlessExecution::Emulated
                    : plan.execution
                        == (version == 6u ? BindlessExecution::Native
                                          : BindlessExecution::Emulated));
    }
};
} // namespace arp::rhi::semantics

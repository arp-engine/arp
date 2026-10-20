#pragma once
#include "rhi/gpu_bindless.h"

namespace arp::rhi::semantics {
// OriginalExecution/1 is an independent, buffer-only Compute Root domain.
// Declaring intent or preparing issuers never authorizes native execution.
// The backend must also prove the original queue/fault/map/seal completion.
class OriginalBindlessExecutionLowering final {
public:
    struct Decision {
        bool executionAvailable = false;
        bool completionAvailable = false;
        const char* diagnostic = "RHI-BINDLESS-ORIGINAL-EXECUTION-BACKEND";
    };

    static constexpr bool capabilityAvailable(
        const BindlessCapabilities& caps, std::uint32_t version) noexcept
    {
        return caps.limitsQueried
            && ((version == 15u
                    && caps.originalNativeProducedGeometryExecutionContractVersion == 15u
                    && caps.execution == BindlessExecution::Native)
                || (version == 16u
                    && caps.originalEmulatedProducedGeometryExecutionContractVersion == 16u
                    && (caps.execution == BindlessExecution::Emulated
                        || caps.execution == BindlessExecution::DiagnosticFailure))
                || (version == 14u
                    && caps.originalOrdinaryDepthGraphicsExecutionContractVersion == 14u
                    && (caps.execution == BindlessExecution::Emulated
                        || caps.execution == BindlessExecution::DiagnosticFailure))
                || (version == 13u
                    && caps.originalOrdinaryColorGraphicsExecutionContractVersion == 13u
                    && (caps.execution == BindlessExecution::Emulated
                        || caps.execution == BindlessExecution::DiagnosticFailure))
                || (version == 1u && caps.originalExecutionContractVersion == 1u)
                || (version == 2u && caps.originalFragmentGraphicsExecutionContractVersion == 2u)
                || (version == 3u
                    && caps.originalSampledFragmentGraphicsExecutionContractVersion == 3u)
                || (version == 4u
                    && caps.originalArrayFragmentGraphicsExecutionContractVersion == 4u)
                || (version == 5u && caps.originalDepthComputeExecutionContractVersion == 5u)
                || (version == 6u && caps.originalNativeDepthComputeExecutionContractVersion == 6u
                    && caps.execution == BindlessExecution::Native)
                || (version == 7u && caps.originalNativeBufferComputeExecutionContractVersion == 7u
                    && caps.execution == BindlessExecution::Native)
                || (version == 8u && caps.originalNativeColorComputeExecutionContractVersion == 8u
                    && caps.execution == BindlessExecution::Native)
                || (version == 9u && caps.originalNativeStorageComputeExecutionContractVersion == 9u
                    && caps.execution == BindlessExecution::Native)
                || (version == 10u
                    && caps.originalEmulatedStorageComputeExecutionContractVersion == 10u
                    && (caps.execution == BindlessExecution::Emulated
                        || caps.execution == BindlessExecution::DiagnosticFailure))
                || (version == 11u && caps.originalNativeMixedComputeExecutionContractVersion == 11u
                    && caps.execution == BindlessExecution::Native)
                || (version == 12u
                    && caps.originalEmulatedMixedComputeExecutionContractVersion == 12u
                    && (caps.execution == BindlessExecution::Emulated
                        || caps.execution == BindlessExecution::DiagnosticFailure)));
    }

    static constexpr Decision lowering(Backend backend, std::uint32_t version = 1u) noexcept
    {
        if (version == 15u || version == 16u) {
            // Kept fail-closed until the complete native admission is enabled
            // together with its platform payload and completion checks.
            switch (backend) {
            case Backend::Null:
                return {false, false, "RHI-BINDLESS-PRODUCED-EXECUTION-NULL-VALIDATOR"};
            case Backend::Vulkan:
            case Backend::Metal:
            case Backend::WebGPU:
                return {false, false, "RHI-BINDLESS-PRODUCED-EXECUTION-VALIDATION-REQUIRED"};
            case Backend::WebGL:
                return {false, false, "RHI-WEBGL1-BINDLESS-PRODUCED-EXECUTION-UNSUPPORTED"};
            }
            return {};
        }
        if (version == 14u) {
            switch (backend) {
            case Backend::Null:
                return {false, false, "RHI-BINDLESS-ORDINARY-DEPTH-EXECUTION-NULL-VALIDATOR-ONLY"};
            case Backend::WebGPU:
                return {true, true, nullptr};
            case Backend::Vulkan:
                return {false, false, "RHI-VULKAN-BINDLESS-ORDINARY-DEPTH-CALLBACK-PROFILE"};
            case Backend::Metal:
                return {false, false, "RHI-METAL-BINDLESS-ORDINARY-DEPTH-CALLBACK-PROFILE"};
            case Backend::WebGL:
                return {false, false, "RHI-WEBGL1-BINDLESS-ORDINARY-DEPTH-EXECUTION-UNSUPPORTED"};
            }
            return {};
        }
        if (version == 13u) {
            switch (backend) {
            case Backend::Null:
                return {false, false, "RHI-BINDLESS-ORDINARY-COLOR-EXECUTION-NULL-VALIDATOR-ONLY"};
            case Backend::WebGPU:
                return {true, true, nullptr};
            case Backend::Vulkan:
                return {false, false, "RHI-VULKAN-BINDLESS-ORDINARY-COLOR-CALLBACK-PROFILE"};
            case Backend::Metal:
                return {false, false, "RHI-METAL-BINDLESS-ORDINARY-COLOR-CALLBACK-PROFILE"};
            case Backend::WebGL:
                return {false, false, "RHI-WEBGL1-BINDLESS-ORDINARY-COLOR-EXECUTION-UNSUPPORTED"};
            }
            return {};
        }
        if (version == 11u || version == 12u) {
            switch (backend) {
            case Backend::Null:
                return {false, false, "RHI-BINDLESS-ORIGINAL-EXECUTION-NULL-PROFILE"};
            case Backend::Vulkan:
            case Backend::Metal:
                return version == 11u
                    ? Decision{true, true, nullptr}
                    : Decision{false,
                          false,
                          "RHI-BINDLESS-ORIGINAL-EXECUTION-NATIVE-MIXED-CALLBACK-PROFILE"};
            case Backend::WebGPU:
                return version == 12u
                    ? Decision{true, true, nullptr}
                    : Decision{false,
                          false,
                          "RHI-BINDLESS-ORIGINAL-EXECUTION-WEBGPU-MIXED-FENCE-PROFILE"};
            case Backend::WebGL:
                return {false, false, "RHI-BINDLESS-ORIGINAL-EXECUTION-WEBGL-MIXED-UNAVAILABLE"};
            }
            return {};
        }
        if (version == 10u) {
            switch (backend) {
            case Backend::Null:
                return {false, false, "RHI-BINDLESS-ORIGINAL-EXECUTION-NULL-PROFILE"};
            case Backend::Vulkan:
                return {false,
                    false,
                    "RHI-BINDLESS-ORIGINAL-EXECUTION-VULKAN-CALLBACK-STORAGE-PROFILE"};
            case Backend::Metal:
                return {
                    false, false, "RHI-BINDLESS-ORIGINAL-EXECUTION-METAL-CALLBACK-STORAGE-PROFILE"};
            case Backend::WebGPU:
                return {true, true, nullptr};
            case Backend::WebGL:
                break;
            }
            return {};
        }
        if (version == 9u) {
            switch (backend) {
            case Backend::Null:
                return {false, false, "RHI-BINDLESS-ORIGINAL-EXECUTION-NULL-PROFILE"};
            case Backend::Vulkan:
                return {true, true, nullptr};
            case Backend::Metal:
                return {true, true, nullptr};
            case Backend::WebGPU:
                return {false, false, "RHI-BINDLESS-ORIGINAL-EXECUTION-WEBGPU-STORAGE-PROFILE"};
            case Backend::WebGL:
                break;
            }
            return {};
        }
        if (version == 8u) {
            switch (backend) {
            case Backend::Null:
                return {false, false, "RHI-BINDLESS-ORIGINAL-EXECUTION-NULL-PROFILE"};
            case Backend::Vulkan:
                return {true, true, nullptr};
            case Backend::Metal:
                return {true, true, nullptr};
            case Backend::WebGPU:
                return {
                    false, false, "RHI-BINDLESS-ORIGINAL-EXECUTION-WEBGPU-NATIVE-COLOR-PROFILE"};
            case Backend::WebGL:
                break;
            }
            return {};
        }
        if (version == 7u) {
            switch (backend) {
            case Backend::Null:
                return {false, false, "RHI-BINDLESS-ORIGINAL-EXECUTION-NULL-PROFILE"};
            case Backend::Vulkan:
                return {false, false, "RHI-BINDLESS-ORIGINAL-EXECUTION-VULKAN-BUFFER-PROFILE"};
            case Backend::Metal:
                return {true, true, nullptr};
            case Backend::WebGPU:
                return {
                    false, false, "RHI-BINDLESS-ORIGINAL-EXECUTION-WEBGPU-NATIVE-BUFFER-PROFILE"};
            case Backend::WebGL:
                break;
            }
            return {};
        }
        if (version == 6u) {
            switch (backend) {
            case Backend::Null:
                return {false, false, "RHI-BINDLESS-ORIGINAL-EXECUTION-NULL-PROFILE"};
            case Backend::Vulkan:
                return {true, true, nullptr};
            case Backend::Metal:
                return {true, true, nullptr};
            case Backend::WebGPU:
                return {false, false, "RHI-BINDLESS-ORIGINAL-EXECUTION-WEBGPU-PROFILE"};
            case Backend::WebGL:
                break;
            }
            return {};
        }
        if (version != 1u && version != 2u && version != 3u && version != 4u && version != 5u)
            return {false, false, "RHI-BINDLESS-ORIGINAL-EXECUTION-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {false, false, "RHI-BINDLESS-ORIGINAL-EXECUTION-NULL-PROFILE"};
        case Backend::Vulkan:
            return {false, false, "RHI-BINDLESS-ORIGINAL-EXECUTION-VULKAN-PROFILE"};
        case Backend::Metal:
            return {false, false, "RHI-BINDLESS-ORIGINAL-EXECUTION-METAL-UNAVAILABLE"};
        case Backend::WebGPU:
            return {true, true, nullptr};
        case Backend::WebGL:
            break;
        }
        return {};
    }
};
} // namespace arp::rhi::semantics

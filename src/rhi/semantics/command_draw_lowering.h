#pragma once
#include "rhi/gpu_common.h"

namespace arp::rhi::semantics {
// OriginalIndexedDrawFetch/1 is CPU projection only. Native record/submit
// authority requires the same root's dependency and completion joins.
class OriginalIndexedDrawFetchLowering final {
public:
    struct Decision {
        bool validatorOnly = false, projectionAvailable = false, executionAvailable = false;
        const char* diagnostic = nullptr;
    };

    static constexpr Decision current(Backend backend, std::uint32_t version = 1u) noexcept
    {
        if (version < 1u || version > 3u)
            return {false, false, false, "RHI-ORIGINAL-INDEXED-FETCH-VERSION"};
        if (version == 3u) {
            switch (backend) {
            case Backend::Null:
                return {true, true, false, "RHI-ORIGINAL-PRODUCED-INDEXED-FETCH-NULL-VALIDATOR"};
            case Backend::Vulkan:
            case Backend::Metal:
            case Backend::WebGPU:
                return {
                    false, true, false, "RHI-ORIGINAL-PRODUCED-INDEXED-FETCH-NATIVE-JOIN-MISSING"};
            case Backend::WebGL:
                return {false, false, false, "RHI-WEBGL1-ORIGINAL-INDEXED-FETCH-UNAVAILABLE"};
            }
            return {false, false, false, "RHI-ORIGINAL-INDEXED-FETCH-BACKEND-UNKNOWN"};
        }
        if (version == 2u) {
            switch (backend) {
            case Backend::Null:
                return {true, true, false, "RHI-ORIGINAL-ROOT-INDEXED-FETCH-NULL-VALIDATOR"};
            case Backend::Vulkan:
                return {false,
                    true,
                    false,
                    "RHI-ORIGINAL-ROOT-INDEXED-FETCH-VULKAN-CONTENT-JOIN-MISSING"};
            case Backend::Metal:
                return {false,
                    true,
                    false,
                    "RHI-ORIGINAL-ROOT-INDEXED-FETCH-METAL-CONTENT-JOIN-MISSING"};
            case Backend::WebGPU:
                return {false,
                    true,
                    false,
                    "RHI-ORIGINAL-ROOT-INDEXED-FETCH-WEBGPU-CONTENT-JOIN-MISSING"};
            case Backend::WebGL:
                return {false, false, false, "RHI-WEBGL1-ORIGINAL-INDEXED-FETCH-UNAVAILABLE"};
            }
            return {false, false, false, "RHI-ORIGINAL-INDEXED-FETCH-BACKEND-UNKNOWN"};
        }
        switch (backend) {
        case Backend::Null:
            return {true, true, false, "RHI-ORIGINAL-INDEXED-FETCH-NULL-VALIDATOR"};
        case Backend::Vulkan:
            return {false, false, false, "RHI-ORIGINAL-INDEXED-FETCH-VULKAN-CONTENT-JOIN-MISSING"};
        case Backend::Metal:
            return {false, false, false, "RHI-ORIGINAL-INDEXED-FETCH-METAL-CONTENT-JOIN-MISSING"};
        case Backend::WebGPU:
            return {false, true, false, "RHI-ORIGINAL-INDEXED-FETCH-WEBGPU-RECORDING-UNPROVEN"};
        case Backend::WebGL:
            return {false, false, false, "RHI-WEBGL1-ORIGINAL-INDEXED-FETCH-UNAVAILABLE"};
        }
        return {false, false, false, "RHI-ORIGINAL-INDEXED-FETCH-BACKEND-UNKNOWN"};
    }
};

// Versioned value-only decisions. Template results preserve existing public decision types.
class CommandDrawLowering final {
public:
    template <class Result>
    static constexpr Result IndirectArgumentLowering_current(
        Backend backend, std::uint32_t kIndirectArgumentConsumptionContractVersion) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {kIndirectArgumentConsumptionContractVersion,
                true,
                false,
                CapabilityMode::Equivalent,
                "RHI-INDIRECT-ARGUMENT-VALIDATOR-ONLY"};
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return {};
        case Backend::WebGL:
            return {kIndirectArgumentConsumptionContractVersion,
                false,
                false,
                CapabilityMode::DiagnosticFailure,
                "RHI-WEBGL1-INDIRECT-DRAW-UNSUPPORTED"};
        }
        return {kIndirectArgumentConsumptionContractVersion,
            false,
            false,
            CapabilityMode::DiagnosticFailure,
            "RHI-INDIRECT-ARGUMENT-BACKEND-UNKNOWN"};
    }

    template <class Result>
    static constexpr Result GpuIndirectProducerLowering_current(
        Backend backend, std::uint32_t kGpuIndirectProducerContractVersion) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {kGpuIndirectProducerContractVersion,
                true,
                false,
                CapabilityMode::Equivalent,
                "RHI-GPU-INDIRECT-PRODUCER-VALIDATOR-ONLY"};
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return {};
        case Backend::WebGL:
            return {kGpuIndirectProducerContractVersion,
                false,
                false,
                CapabilityMode::DiagnosticFailure,
                "RHI-WEBGL1-GPU-INDIRECT-PRODUCER-UNSUPPORTED"};
        }
        return {kGpuIndirectProducerContractVersion,
            false,
            false,
            CapabilityMode::DiagnosticFailure,
            "RHI-GPU-INDIRECT-PRODUCER-BACKEND-UNKNOWN"};
    }

    template <class Result>
    static Result GraphicsStorageWriteLowering_current(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {true,
                false,
                CapabilityMode::Equivalent,
                "RHI-GRAPHICS-STORAGE-WRITE-VALIDATOR-ONLY"};
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return {};
        case Backend::WebGL:
            return {false,
                false,
                CapabilityMode::DiagnosticFailure,
                "RHI-WEBGL1-GRAPHICS-STORAGE-WRITE-UNSUPPORTED"};
        }
        return {false,
            false,
            CapabilityMode::DiagnosticFailure,
            "RHI-GRAPHICS-STORAGE-WRITE-BACKEND-UNKNOWN"};
    }

    template <class Result>
    static constexpr Result InstanceAttributeFetchLowering_current(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {1u,
                true,
                false,
                CapabilityMode::Equivalent,
                "RHI-INSTANCE-ATTRIBUTE-FETCH-VALIDATOR-ONLY"};
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return {};
        case Backend::WebGL:
            return {1u,
                false,
                false,
                CapabilityMode::DiagnosticFailure,
                "RHI-WEBGL1-INSTANCE-ATTRIBUTE-FETCH-UNAVAILABLE"};
        }
        return {1u,
            false,
            false,
            CapabilityMode::DiagnosticFailure,
            "RHI-INSTANCE-ATTRIBUTE-FETCH-BACKEND-UNKNOWN"};
    }
};
} // namespace arp::rhi::semantics

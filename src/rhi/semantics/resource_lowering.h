#pragma once
#include "rhi/gpu_common.h"

namespace arp::rhi::semantics {
// Versioned value-only decisions. Template results preserve existing public decision types.
class ResourceLowering final {
public:
    template <class Result>
    static constexpr Result HostVisibleDeviceLocalBuffer_lowering(
        Backend backend, bool nativeCapability) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {CapabilityMode::DiagnosticFailure,
                true,
                false,
                "RHI-HOST-DEVICE-BUFFER-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return nativeCapability ? Result{CapabilityMode::Native, false, true, nullptr}
                                    : Result{CapabilityMode::DiagnosticFailure,
                                          false,
                                          false,
                                          "RHI-HOST-DEVICE-BUFFER-CAPABILITY"};
        case Backend::Metal:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                "RHI-HOST-DEVICE-BUFFER-METAL-PAYLOAD-MISSING"};
        case Backend::WebGPU:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                "RHI-HOST-DEVICE-BUFFER-WEBGPU-PLACEMENT-UNAVAILABLE"};
        case Backend::WebGL:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                "RHI-HOST-DEVICE-BUFFER-WEBGL-PLACEMENT-UNAVAILABLE"};
        }
        return {};
    }

    template <class Result, class Disposition>
    static constexpr Result OriginalNativeAllocationObservation_lowering(
        Backend backend, std::uint32_t version) noexcept
    {
        if (version != 1u)
            return {};
        switch (backend) {
        case Backend::Null:
            return {Disposition::ValidatorOnly,
                false,
                "RHI-NATIVE-ALLOCATION-OBSERVATION-NULL-VALIDATOR"};
        case Backend::Metal:
            return {Disposition::Prototype, true, nullptr};
        case Backend::Vulkan:
            return {Disposition::DiagnosticFailure,
                false,
                "RHI-NATIVE-ALLOCATION-OBSERVATION-VULKAN-PROFILE-UNAVAILABLE"};
        case Backend::WebGPU:
            return {Disposition::DiagnosticFailure,
                false,
                "RHI-NATIVE-ALLOCATION-OBSERVATION-WEBGPU-NATIVE-QUERY-UNAVAILABLE"};
        case Backend::WebGL:
            return {Disposition::DiagnosticFailure,
                false,
                "RHI-NATIVE-ALLOCATION-OBSERVATION-WEBGL-UNAVAILABLE"};
        }
        return {};
    }

    template <class Decision>
    static constexpr Decision OriginalDescriptorNativeLossRegistration_lowering(
        Backend backend, std::uint32_t version) noexcept
    {
        if (version == 3u) {
            switch (backend) {
            case Backend::Null:
                return {true, false, false, "RHI-ORIGINAL-DESCRIPTOR-LOSS-NULL-VALIDATOR-ONLY"};
            case Backend::Vulkan:
                return {false,
                    false,
                    false,
                    "RHI-ORIGINAL-DESCRIPTOR-LOSS-VULKAN-ROOT-FACTORY-PROFILE"};
            case Backend::Metal:
                return {false, false, false, "RHI-ORIGINAL-DESCRIPTOR-LOSS-METAL-MISSING"};
            case Backend::WebGPU:
                return {false, true, false, nullptr};
            case Backend::WebGL:
                return {false, false, false, "RHI-WEBGL1-DESCRIPTOR-LOSS-JOIN-UNAVAILABLE"};
            }
            return {};
        }
        if (version != 1u && version != 2u)
            return {false, false, false, "RHI-ORIGINAL-DESCRIPTOR-LOSS-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {true, false, false, "RHI-ORIGINAL-DESCRIPTOR-LOSS-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {false, true, false, nullptr};
        case Backend::Metal:
            return {false, true, false, nullptr};
        case Backend::WebGPU:
            return {
                false, false, false, "RHI-ORIGINAL-DESCRIPTOR-LOSS-WEBGPU-SCOPED-JOIN-REQUIRED"};
        case Backend::WebGL:
            return {false, false, false, "RHI-WEBGL1-DESCRIPTOR-LOSS-JOIN-UNAVAILABLE"};
        }
        return {};
    }

    template <class Result>
    static constexpr Result OriginalSamplerNativeLossJoin_lowering(
        Backend backend, std::uint32_t version) noexcept
    {
        if (version == 3u) {
            switch (backend) {
            case Backend::Null:
                return {
                    3u, true, false, false, false, "RHI-ORIGINAL-SYNC-SAMPLER-LOSS-NULL-VALIDATOR"};
            case Backend::Vulkan:
                return {3u, false, true, false, false, nullptr};
            case Backend::Metal:
                return {
                    3u, false, false, false, false, "RHI-ORIGINAL-SYNC-SAMPLER-LOSS-METAL-MISSING"};
            case Backend::WebGPU:
                return {3u,
                    false,
                    false,
                    false,
                    false,
                    "RHI-ORIGINAL-SYNC-SAMPLER-LOSS-WEBGPU-MISSING"};
            case Backend::WebGL:
                return {3u,
                    false,
                    false,
                    false,
                    false,
                    "RHI-ORIGINAL-SYNC-SAMPLER-LOSS-WEBGL-UNSUPPORTED"};
            }
            return {};
        }
        if (version == 2u) {
            switch (backend) {
            case Backend::Null:
                return {
                    2u, true, false, false, false, "RHI-ORIGINAL-SAMPLER-LOSS-NULL-VALIDATOR-ONLY"};
            case Backend::Vulkan:
                return {2u, false, true, false, false, nullptr};
            case Backend::Metal:
                return {2u, false, true, false, false, nullptr};
            case Backend::WebGPU:
                // Registration/3 already authenticates this descriptor-only
                // registry's actual WebGPU loss subscription and device domain.
                return {2u, false, true, false, false, nullptr};
            case Backend::WebGL:
                return {2u, false, false, false, false, "RHI-WEBGL1-SAMPLER-LOSS-JOIN-UNAVAILABLE"};
            }
            return {};
        }
        if (version != 1u)
            return {0u, false, false, false, false, "RHI-ORIGINAL-SAMPLER-LOSS-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {1u, true, false, false, false, "RHI-ORIGINAL-SAMPLER-LOSS-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {
                1u, false, false, false, false, "RHI-ORIGINAL-SAMPLER-LOSS-VULKAN-JOIN-MISSING"};
        case Backend::Metal:
            return {1u, false, false, false, false, "RHI-ORIGINAL-SAMPLER-LOSS-METAL-JOIN-MISSING"};
        case Backend::WebGPU:
            return {1u, false, true, false, false, nullptr};
        case Backend::WebGL:
            return {1u, false, false, false, false, "RHI-WEBGL1-SAMPLER-LOSS-JOIN-UNAVAILABLE"};
        }
        return {};
    }

    template <class Result>
    static constexpr Result GeneralSamplerDescriptorReference_lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return {1u, CapabilityMode::Equivalent, true, false, nullptr};
        case Backend::WebGL:
            return {1u,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                "RHI-WEBGL1-SAMPLER-REFERENCE-UNAVAILABLE"};
        }
        return {};
    }

    template <class Result>
    static constexpr Result OriginalQueryNativeLossJoin_lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {true, false, false, false, "RHI-ORIGINAL-QUERY-LOSS-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            // Native pool creation/result paths already report device loss to
            // the sole runtime subscription. Registration grants no recording.
            return {false, true, false, false, nullptr};
        case Backend::Metal:
            return {false, false, false, false, "RHI-ORIGINAL-QUERY-LOSS-METAL-SOURCE-MISSING"};
        case Backend::WebGPU:
            return {false, false, false, false, "RHI-ORIGINAL-QUERY-LOSS-WEBGPU-SOURCE-MISSING"};
        case Backend::WebGL:
            return {false, false, false, false, "RHI-WEBGL1-QUERY-LOSS-UNAVAILABLE"};
        }
        return {};
    }

    template <class Result>
    static constexpr Result OriginalQueryRegistration_lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {1u,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-ORIGINAL-QUERY-REGISTRATION-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {1u, CapabilityMode::Equivalent, true, false, false, false, nullptr};
        case Backend::Metal:
            return {1u,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-ORIGINAL-QUERY-METAL-ISSUER-UNAVAILABLE"};
        case Backend::WebGPU:
            return {1u,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-ORIGINAL-QUERY-WEBGPU-ISSUER-UNAVAILABLE"};
        case Backend::WebGL:
            return {1u,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-WEBGL1-QUERY-UNSUPPORTED"};
        }
        return {};
    }

    template <class Result>
    static constexpr Result OriginalHeapRegistration_lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
        case Backend::Vulkan:
            return {1u, CapabilityMode::Equivalent, true, false, false, false, nullptr};
        case Backend::Metal:
            return {1u,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-ORIGINAL-HEAP-METAL-ISSUER-UNAVAILABLE"};
        case Backend::WebGPU:
            return {1u,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-ORIGINAL-HEAP-WEBGPU-ISSUER-UNAVAILABLE"};
        case Backend::WebGL:
            return {1u,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-ORIGINAL-HEAP-WEBGL-ISSUER-UNAVAILABLE"};
        }
        return {};
    }

    template <class Result>
    static constexpr Result OriginalSamplerRegistration_lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return {1u, CapabilityMode::Equivalent, true, false, false, false, nullptr};
        case Backend::WebGL:
            return {1u,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-WEBGL1-SAMPLER-REGISTRATION-UNAVAILABLE"};
        }
        return {};
    }

    template <class Result>
    static constexpr Result DeviceLossRegistration_lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {false, true, "RHI-DEVICE-LOSS-REGISTRATION-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {false, false, "RHI-DEVICE-LOSS-REGISTRATION-VULKAN-UNIMPLEMENTED"};
        case Backend::Metal:
            return {false, false, "RHI-DEVICE-LOSS-REGISTRATION-METAL-UNIMPLEMENTED"};
        case Backend::WebGPU:
            return {true, false, "RHI-DEVICE-LOSS-REGISTRATION-NATIVE-PROTOTYPE"};
        case Backend::WebGL:
            return {false, false, "RHI-WEBGL1-LOSS-SUBSCRIPTION-UNAVAILABLE"};
        }
        return {};
    }

    template <class Result>
    static constexpr Result OriginalDeviceNativeResultLoss_lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {false, true, false, "RHI-ORIGINAL-RESULT-LOSS-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {true, false, false, nullptr};
        case Backend::Metal:
            return {true, false, false, nullptr};
        case Backend::WebGPU:
            return {
                false, false, false, "RHI-ORIGINAL-RESULT-LOSS-WEBGPU-CALLBACK-SOURCE-REQUIRED"};
        case Backend::WebGL:
            return {false, false, false, "RHI-WEBGL1-RESULT-LOSS-SOURCE-UNAVAILABLE"};
        }
        return {};
    }

    template <class Result>
    static constexpr Result OriginalDeviceNativeLoss_lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {false, true, false, "RHI-ORIGINAL-DEVICE-LOSS-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {false, false, false, "RHI-ORIGINAL-DEVICE-LOSS-VULKAN-UNIMPLEMENTED"};
        case Backend::Metal:
            return {false, false, false, "RHI-ORIGINAL-DEVICE-LOSS-METAL-UNIMPLEMENTED"};
        case Backend::WebGPU:
            return {true, false, false, nullptr};
        case Backend::WebGL:
            return {false, false, false, "RHI-WEBGL1-ORIGINAL-LOSS-SUBSCRIPTION-UNAVAILABLE"};
        }
        return {};
    }

    template <class Result>
    static constexpr Result OriginalDeviceLifetime_lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return {1u, CapabilityMode::Equivalent, true, false, false, false, nullptr};
        case Backend::WebGL:
            return {1u,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-WEBGL1-ORIGINAL-DEVICE-LIFETIME-UNAVAILABLE"};
        }
        return {};
    }

    template <class Result>
    static constexpr Result OriginalDeviceNativeLossObserver_lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {true, false, false, false, "RHI-ORIGINAL-LOSS-OBSERVER-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {false, false, false, false, "RHI-ORIGINAL-LOSS-OBSERVER-VULKAN-MISSING"};
        case Backend::Metal:
            return {false, false, false, false, "RHI-ORIGINAL-LOSS-OBSERVER-METAL-MISSING"};
        case Backend::WebGPU:
            return {false,
                true,
                false,
                false,
                "RHI-ORIGINAL-LOSS-OBSERVER-WEBGPU-ISSUANCE-MISSING",
                true};
        case Backend::WebGL:
            return {false, false, false, false, "RHI-WEBGL1-LOSS-OBSERVER-UNAVAILABLE", false};
        }
        return {};
    }

    template <class Result>
    static constexpr Result OriginalTypedNativeLossJoin_lowering(
        Backend backend, std::uint32_t version = 1u) noexcept
    {
        if (version == 3u || version == 4u) {
            switch (backend) {
            case Backend::Null:
                return {false,
                    true,
                    false,
                    false,
                    false,
                    false,
                    "RHI-ORIGINAL-TYPED-LOSS-NULL-VALIDATOR-ONLY"};
            case Backend::Vulkan:
                return {true, false, true, version == 4u, false, false, nullptr};
            case Backend::Metal:
                // /4 is an independent synchronous complete-group loss join.
                // Native publication and command payloads remain separate gates.
                return {true, false, true, version == 4u, false, false, nullptr};
            case Backend::WebGPU:
                return {false,
                    false,
                    false,
                    false,
                    false,
                    false,
                    "RHI-ORIGINAL-TYPED-LOSS-SYNC-WEBGPU-ASYNC-REQUIRED"};
            case Backend::WebGL:
                return {false,
                    false,
                    false,
                    false,
                    false,
                    false,
                    "RHI-WEBGL1-TYPED-LOSS-JOIN-UNAVAILABLE"};
            }
            return {};
        }
        if (version != 1u && version != 2u)
            return {};
        switch (backend) {
        case Backend::Null:
            return {false,
                true,
                false,
                false,
                false,
                false,
                "RHI-ORIGINAL-TYPED-LOSS-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {false,
                false,
                false,
                false,
                false,
                false,
                "RHI-ORIGINAL-TYPED-LOSS-VULKAN-JOIN-MISSING"};
        case Backend::Metal:
            return {false,
                false,
                false,
                false,
                false,
                false,
                "RHI-ORIGINAL-TYPED-LOSS-METAL-JOIN-MISSING"};
        case Backend::WebGPU:
            return {true, false, true, version == 1u, false, false, nullptr};
        case Backend::WebGL:
            return {
                false, false, false, false, false, false, "RHI-WEBGL1-TYPED-LOSS-JOIN-UNAVAILABLE"};
        }
        return {};
    }

    template <class Result>
    static constexpr Result OriginalResourceRegistration_lowering(
        Backend backend, std::uint32_t version) noexcept
    {
        switch (backend) {
        case Backend::Null:
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return {version,
                CapabilityMode::Equivalent,
                true,
                false,
                false,
                false,
                nullptr,
                version >= 2u,
                version >= 3u,
                version >= 4u};
        case Backend::WebGL:
            return {version,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-WEBGL1-ORIGINAL-REGISTRATION-UNAVAILABLE",
                false,
                false,
                false};
        }
        return {};
    }

    template <class Result>
    static constexpr Result TextureContentAccessPolicy_lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {true, false, "RHI-TEXTURE-DESTINATION-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {false, false, "RHI-TEXTURE-DESTINATION-NATIVE-BINDING-UNIMPLEMENTED"};
        case Backend::Metal:
            return {false, false, "RHI-TEXTURE-DESTINATION-NATIVE-BINDING-UNIMPLEMENTED"};
        case Backend::WebGPU:
            return {false, true, "RHI-TEXTURE-DESTINATION-CREATION-EVIDENCE-REQUIRED"};
        case Backend::WebGL:
            return {false, false, "RHI-WEBGL1-ORIGINAL-TEXTURE-CONTENT-UNAVAILABLE"};
        }
        return {};
    }

    template <class Result, class Disposition>
    static constexpr Result OriginalTextureContentCreation_lowering(
        Backend backend, std::uint32_t version = 1u) noexcept
    {
        if (version < 1u || version > 7u)
            return {};
        switch (backend) {
        case Backend::Null:
            return {version,
                Disposition::ValidatorOnly,
                false,
                false,
                "RHI-CANONICAL-CONTENT-CREATION-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            if (version == 3u || version == 4u || version == 6u)
                return {version,
                    Disposition::Prototype,
                    true,
                    false,
                    "RHI-CANONICAL-CONTENT-COMPLETION-UNPROVEN"};
            return {version,
                Disposition::DiagnosticFailure,
                false,
                false,
                "RHI-CANONICAL-CONTENT-CREATION-VULKAN-PAYLOAD-MISSING"};
        case Backend::Metal:
            if (version == 3u || version == 4u || version == 6u)
                return {version,
                    Disposition::Prototype,
                    true,
                    false,
                    "RHI-CANONICAL-CONTENT-COMPLETION-UNPROVEN"};
            return {version,
                Disposition::DiagnosticFailure,
                false,
                false,
                "RHI-CANONICAL-CONTENT-CREATION-METAL-PAYLOAD-MISSING"};
        case Backend::WebGPU:
            if (version == 3u || version == 4u || version == 6u)
                return {version,
                    Disposition::DiagnosticFailure,
                    false,
                    false,
                    "RHI-CANONICAL-CONTENT-CREATION-WEBGPU-NATIVE-PROFILE-UNAVAILABLE"};
            return {version,
                Disposition::Prototype,
                true,
                false,
                "RHI-CANONICAL-CONTENT-COMPLETION-UNPROVEN"};
        case Backend::WebGL:
            return {version,
                Disposition::DiagnosticFailure,
                false,
                false,
                "RHI-WEBGL1-ORIGINAL-TEXTURE-CREATION-UNAVAILABLE"};
        }
        return {};
    }

    template <class Result>
    static constexpr Result NormalizedTextureStateCreation_lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {true, true, false, "RHI-TEXTURE-STATE-NULL-VALIDATOR"};
        case Backend::Vulkan:
            return {true, false, false, "RHI-TEXTURE-STATE-NATIVE-EXECUTION-UNPROVEN"};
        case Backend::Metal:
            return {true, false, false, "RHI-TEXTURE-STATE-NATIVE-EXECUTION-UNPROVEN"};
        case Backend::WebGPU:
            return {true, false, false, "RHI-TEXTURE-STATE-NATIVE-EXECUTION-UNPROVEN"};
        case Backend::WebGL:
            return {false, false, false, "RHI-WEBGL1-ORIGINAL-TEXTURE-STATE-UNAVAILABLE"};
        }
        return {false, false, false, "RHI-TEXTURE-STATE-BACKEND-UNKNOWN"};
    }

    template <class Result, class Disposition>
    static constexpr Result OriginalTextureContentStaging_lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {Disposition::ValidatorOnly,
                false,
                "RHI-CANONICAL-CONTENT-STAGING-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {Disposition::DiagnosticFailure,
                false,
                "RHI-CANONICAL-CONTENT-STAGING-VULKAN-PAYLOAD-MISSING"};
        case Backend::Metal:
            return {Disposition::DiagnosticFailure,
                false,
                "RHI-CANONICAL-CONTENT-STAGING-METAL-PAYLOAD-MISSING"};
        case Backend::WebGPU:
            return {Disposition::Prototype,
                true,
                "RHI-CANONICAL-TEXTURE-UPLOAD-ORDERED-EXECUTION-MISSING"};
        case Backend::WebGL:
            return {Disposition::DiagnosticFailure,
                false,
                "RHI-WEBGL1-ORIGINAL-TEXTURE-STAGING-UNAVAILABLE"};
        }
        return {};
    }
};
} // namespace arp::rhi::semantics

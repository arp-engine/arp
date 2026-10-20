#pragma once

#include "rhi/frontend/command_model.h"
#include "rhi/gpu_common.h"
#include "rhi/semantics/command_input_admission_lowering.h"

// region original_command_state_admission
namespace arp::rhi::semantics {
class CompletedDepthReadInputLowering final {
public:
    struct Decision {
        bool validatorOnly = false, nativeInputValidationAvailable = false;
        bool executionAvailable = false;
        const char* diagnostic = "RHI-COMPLETED-DEPTH-READ-INPUT-BACKEND";
    };

    static constexpr Decision lowering(Backend backend, std::uint32_t version = 1u) noexcept
    {
        if (version != 1u && version != 2u && version != 3u)
            return {false, false, false, "RHI-COMPLETED-DEPTH-READ-INPUT-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {true, false, false, "RHI-COMPLETED-DEPTH-READ-INPUT-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return version == 2u || version == 3u
                ? Decision{false, true, false, nullptr}
                : Decision{
                      false, false, false, "RHI-COMPLETED-DEPTH-READ-INPUT-VULKAN-UNAVAILABLE"};
        case Backend::Metal:
            return version == 2u || version == 3u
                ? Decision{false, true, false, nullptr}
                : Decision{false, false, false, "RHI-COMPLETED-DEPTH-READ-INPUT-METAL-UNAVAILABLE"};
        case Backend::WebGPU:
            return version == 1u
                ? Decision{false, true, false, nullptr}
                : Decision{false, false, false, "RHI-COMPLETED-DEPTH-READ-INPUT-WEBGPU-PROFILE"};
        case Backend::WebGL:
            return {false, false, false, "RHI-WEBGL1-COMPLETED-DEPTH-READ-INPUT-UNAVAILABLE"};
        }
        return {};
    }
};

class GeometryInputAccessLowering final {
public:
    // GeometryInputAccess/1 is an explicit barrier mapping prerequisite. It
    // cannot authorize a typed command, DrawIndexed, or bindless execution.
    struct GeometryInputAccessDecision {
        std::uint32_t contractVersion = 1u;
        CapabilityMode mode = CapabilityMode::DiagnosticFailure;
        bool validatorOnly = false, barrierMappingAvailable = false;
        const char* diagnostic = "RHI-GEOMETRY-ACCESS-BACKEND";
    };

    static constexpr GeometryInputAccessDecision geometryInputAccessLowering(
        Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {1u, CapabilityMode::Equivalent, true, true, nullptr};
        case Backend::Vulkan:
            return {1u, CapabilityMode::Native, false, true, nullptr};
        case Backend::Metal:
            // Independently created buffers use queried tracked hazards;
            // the existing encoder boundary is the native dependency.
            return {1u, CapabilityMode::Native, false, true, nullptr};
        case Backend::WebGPU:
            return {1u,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                "RHI-GEOMETRY-ACCESS-WEBGPU-DEPENDENCY-UNPROVEN"};
        case Backend::WebGL:
            return {1u,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                "RHI-WEBGL1-GEOMETRY-ACCESS-UNAVAILABLE"};
        }
        return {};
    }

    template <class Barrier>
    static const char* geometryInputBarrierDiagnostic(
        Backend backend, const Barrier& barrier) noexcept
    {
        using S = decltype(barrier.sourceStages);
        using A = decltype(barrier.sourceAccess);
        const auto stages = barrier.sourceStages | barrier.destinationStages;
        const auto access = barrier.sourceAccess | barrier.destinationAccess;
        return (stages & S::VertexInput) != S::None
                || (access & (A::VertexAttributeRead | A::IndexRead)) != A::None
            ? geometryInputAccessLowering(backend).diagnostic
            : nullptr;
    }
};

class OriginalCommandStateAdmissionLowering final {
public:
    using Disposition = arp::rhi::semantics::OriginalCommandAdmissionLowering::Disposition;

    struct Decision {
        std::uint32_t contractVersion = 1u;
        Disposition disposition = Disposition::DiagnosticFailure;
        bool originalIssuerAvailable = false, executionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-STATE-BACKEND-UNKNOWN";
    };

    // /1: private state controls/snapshots and selected prototypes do not expose
    // a complete public L2 original device-issued registration/observation graph.
    static constexpr Decision lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {1u,
                Disposition::ValidatorOnly,
                false,
                false,
                "RHI-ORIGINAL-STATE-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {1u,
                Disposition::DiagnosticFailure,
                false,
                false,
                "RHI-ORIGINAL-STATE-VULKAN-ISSUER-UNIMPLEMENTED"};
        case Backend::Metal:
            return {1u,
                Disposition::DiagnosticFailure,
                false,
                false,
                "RHI-ORIGINAL-STATE-METAL-ISSUER-UNIMPLEMENTED"};
        case Backend::WebGPU:
            return {1u,
                Disposition::DiagnosticFailure,
                false,
                false,
                "RHI-ORIGINAL-STATE-WEBGPU-ISSUER-UNIMPLEMENTED"};
        case Backend::WebGL:
            return {1u,
                Disposition::DiagnosticFailure,
                false,
                false,
                "RHI-WEBGL1-ORIGINAL-STATE-UNAVAILABLE"};
        }
        return {};
    }

    static constexpr const char* admissionDiagnostic(
        Backend backend, const OriginalCommandStateAdmissionBudget& budget) noexcept
    {
        if (const auto* id = OriginalCommandStateAdmissionContract::validateIntent(budget))
            return id;
        return lowering(backend).diagnostic;
    }

    // RootState /1 uses original committed cells and the existing timeline.
    // Four backends share CPU projection; no native execution is granted.
    struct BindlessRootDecision {
        std::uint32_t contractVersion = 1u;
        CapabilityMode mode = CapabilityMode::DiagnosticFailure;
        bool validatorOnly = false, projectionAvailable = false, executionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-BINDLESS-STATE-BACKEND";
    };

    static constexpr BindlessRootDecision bindlessRootLowering(
        Backend backend, std::uint32_t version = 1u) noexcept
    {
        if (version < 1u || version > 6u)
            return {version,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-BINDLESS-STATE-VERSION"};
        switch (backend) {
        case Backend::WebGL:
            return {version,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-WEBGL1-BINDLESS-STATE-UNAVAILABLE"};
        case Backend::Null:
            return {version, CapabilityMode::Equivalent, true, true, false, nullptr};
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return {version, CapabilityMode::Equivalent, false, true, false, nullptr};
        }
        return {};
    }

    // Selected CPU State2 only. Baseline/full native State1 stays closed.
    struct TypedDecision {
        std::uint32_t contractVersion = 2u;
        CapabilityMode mode = CapabilityMode::DiagnosticFailure;
        bool validatorOnly = false, projectionAvailable = false, stateAvailable = false,
             executionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-TYPED-STATE-BACKEND";
        bool retainedReadbackStateAvailable = false;
    };

    static constexpr TypedDecision typedLowering(
        Backend backend, std::uint32_t version = 2u) noexcept
    {
        if (version != 2u && version != 3u && version != 4u && version != 5u)
            return {version,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-ORIGINAL-TYPED-STATE-VERSION"};
        const auto projection =
            arp::rhi::semantics::OriginalCommandDescriptorConsumptionLowering::lowering(
                backend, version - 1u);
        return {version,
            projection.mode,
            projection.validatorOnly,
            projection.projectionAvailable,
            projection.projectionAvailable,
            false,
            projection.diagnostic};
    }

    // TypedReadbackState/1: independent CPU-only Source2 capture in State3.
    // It never grants native closure, copy, mapping or completion authority.
    static constexpr TypedDecision typedReadbackLowering(
        Backend backend, std::uint32_t version = 1u, std::uint32_t stateVersion = 3u) noexcept
    {
        if (version != 1u || (stateVersion != 3u && stateVersion != 4u))
            return {stateVersion,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-ORIGINAL-STATE-READBACK-VERSION"};
        switch (backend) {
        case Backend::WebGL:
            return {stateVersion,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-WEBGL1-TYPED-READBACK-STATE-UNAVAILABLE"};
        case Backend::Null:
            return {stateVersion,
                CapabilityMode::Equivalent,
                true,
                false,
                false,
                false,
                "RHI-ORIGINAL-TYPED-READBACK-STATE-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {stateVersion,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-ORIGINAL-TYPED-READBACK-STATE-VULKAN-JOIN-MISSING"};
        case Backend::Metal:
            return {stateVersion,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-ORIGINAL-TYPED-READBACK-STATE-METAL-JOIN-MISSING"};
        case Backend::WebGPU:
            return {
                stateVersion, CapabilityMode::Equivalent, false, true, true, false, nullptr, true};
        }
        return {stateVersion,
            CapabilityMode::DiagnosticFailure,
            false,
            false,
            false,
            false,
            "RHI-ORIGINAL-TYPED-READBACK-STATE-BACKEND"};
    }

    struct TransactionDecision {
        std::uint32_t contractVersion = 3u;
        CapabilityMode mode = CapabilityMode::DiagnosticFailure;
        bool validatorOnly = false, ownershipAvailable = false, planAvailable = false,
             executionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-TEXTURE-TRANSACTION-BACKEND";
    };

    static constexpr TransactionDecision textureTransactionLowering(
        Backend backend, std::uint32_t version = 3u) noexcept
    {
        if (version != 3u && version != 5u)
            return {version,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-ORIGINAL-TEXTURE-TRANSACTION-VERSION"};
        switch (backend) {
        case Backend::WebGL:
            return {version,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-WEBGL1-TEXTURE-TRANSACTION-UNAVAILABLE"};
        case Backend::Null:
            return {version,
                CapabilityMode::Equivalent,
                true,
                false,
                false,
                false,
                "RHI-ORIGINAL-TEXTURE-TRANSACTION-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {version,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-ORIGINAL-TEXTURE-TRANSACTION-VULKAN-JOIN-UNAVAILABLE"};
        case Backend::Metal:
            return {version,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-ORIGINAL-TEXTURE-TRANSACTION-METAL-JOIN-UNAVAILABLE"};
        case Backend::WebGPU:
            return {version,
                CapabilityMode::Equivalent,
                false,
                true,
                true,
                false,
                "RHI-ORIGINAL-TEXTURE-TRANSACTION-NATIVE-CLOSURE-UNAVAILABLE"};
        }
        return {version,
            CapabilityMode::DiagnosticFailure,
            false,
            false,
            false,
            false,
            "RHI-ORIGINAL-TEXTURE-TRANSACTION-BACKEND"};
    }

    struct ContentSequenceDecision {
        std::uint32_t contractVersion = 2u;
        CapabilityMode mode = CapabilityMode::DiagnosticFailure;
        bool validatorOnly = false, validationAvailable = false, executionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-CONTENT-SEQUENCE-BACKEND";
    };

    static constexpr ContentSequenceDecision textureContentSequenceLowering(
        Backend backend, std::uint32_t version = 2u) noexcept
    {
        // Sequence4 defines initialized storage writes. It cannot grant a
        // native command, state transition, resource creation or completion.
        if (version == 4u) {
            switch (backend) {
            case Backend::WebGL:
                return {version,
                    CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    "RHI-WEBGL1-CONTENT-SEQUENCE-UNAVAILABLE"};
            case Backend::Null:
                return {4u,
                    CapabilityMode::Equivalent,
                    true,
                    true,
                    false,
                    "RHI-ORIGINAL-CONTENT-STORAGE-SEQUENCE-NULL-VALIDATOR-ONLY"};
            case Backend::Vulkan:
                return {4u,
                    CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    "RHI-ORIGINAL-CONTENT-STORAGE-SEQUENCE-VULKAN-JOIN-UNAVAILABLE"};
            case Backend::Metal:
                return {4u,
                    CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    "RHI-ORIGINAL-CONTENT-STORAGE-SEQUENCE-METAL-JOIN-UNAVAILABLE"};
            case Backend::WebGPU:
                return {4u,
                    CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    "RHI-ORIGINAL-CONTENT-STORAGE-SEQUENCE-WEBGPU-JOIN-UNAVAILABLE"};
            }
            return {4u,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-CONTENT-STORAGE-SEQUENCE-BACKEND"};
        }
        if (version != 2u && version != 3u)
            return {version,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-CONTENT-SEQUENCE-VERSION"};
        if (version == 3u) {
            switch (backend) {
            case Backend::WebGL:
                return {version,
                    CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    "RHI-WEBGL1-CONTENT-SEQUENCE-UNAVAILABLE"};
            case Backend::Null:
                return {3u,
                    CapabilityMode::Equivalent,
                    true,
                    true,
                    false,
                    "RHI-ORIGINAL-CONTENT-READBACK-SEQUENCE-NULL-VALIDATOR-ONLY"};
            case Backend::Vulkan:
                return {3u,
                    CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    "RHI-ORIGINAL-CONTENT-READBACK-SEQUENCE-VULKAN-JOIN-UNAVAILABLE"};
            case Backend::Metal:
                return {3u,
                    CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    "RHI-ORIGINAL-CONTENT-READBACK-SEQUENCE-METAL-JOIN-UNAVAILABLE"};
            case Backend::WebGPU:
                return {3u,
                    CapabilityMode::Equivalent,
                    false,
                    true,
                    false,
                    "RHI-ORIGINAL-CONTENT-READBACK-SEQUENCE-NATIVE-CLOSURE-UNAVAILABLE"};
            }
            return {3u,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-CONTENT-READBACK-SEQUENCE-BACKEND"};
        }
        switch (backend) {
        case Backend::WebGL:
            return {version,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-WEBGL1-CONTENT-SEQUENCE-UNAVAILABLE"};
        case Backend::Null:
            return {2u,
                CapabilityMode::Equivalent,
                true,
                true,
                false,
                "RHI-ORIGINAL-CONTENT-SEQUENCE-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {2u,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-CONTENT-SEQUENCE-VULKAN-JOIN-UNAVAILABLE"};
        case Backend::Metal:
            return {2u,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-CONTENT-SEQUENCE-METAL-JOIN-UNAVAILABLE"};
        case Backend::WebGPU:
            return {2u,
                CapabilityMode::Equivalent,
                false,
                true,
                false,
                "RHI-ORIGINAL-CONTENT-SEQUENCE-NATIVE-CLOSURE-UNAVAILABLE"};
        }
        return {};
    }

    static constexpr ContentSequenceDecision textureContentPlanLowering(
        Backend backend, std::uint32_t version = 2u) noexcept
    {
        // CPU planning/proposal only. Authentic backend completion needs a
        // separately versioned execution consumer; no old issuer inherits it.
        if (version == 15u || version == 16u) {
            switch (backend) {
            case Backend::WebGL:
                return {version,
                    CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    "RHI-WEBGL1-CONTENT-PLAN-UNAVAILABLE"};
            case Backend::Null:
                return {version,
                    CapabilityMode::Equivalent,
                    true,
                    true,
                    false,
                    "RHI-MIXED-DEPTH-STORAGE-NULL-VALIDATOR-ONLY"};
            case Backend::Vulkan:
            case Backend::Metal:
                return {version,
                    version == 15u ? CapabilityMode::Equivalent : CapabilityMode::DiagnosticFailure,
                    false,
                    version == 15u,
                    false,
                    "RHI-MIXED-DEPTH-STORAGE-NATIVE-EXECUTION-UNAVAILABLE"};
            case Backend::WebGPU:
                return {version,
                    version == 16u ? CapabilityMode::Equivalent : CapabilityMode::DiagnosticFailure,
                    false,
                    version == 16u,
                    false,
                    "RHI-MIXED-DEPTH-STORAGE-EMULATED-EXECUTION-UNAVAILABLE"};
            }
            return {version,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-MIXED-DEPTH-STORAGE-BACKEND"};
        }
        if (version == 13u || version == 14u) {
            switch (backend) {
            case Backend::WebGL:
                return {version,
                    CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    "RHI-WEBGL1-CONTENT-PLAN-UNAVAILABLE"};
            case Backend::Null:
                return {version,
                    CapabilityMode::Equivalent,
                    true,
                    true,
                    false,
                    "RHI-STORAGE-CONTENT-PLAN-NULL-VALIDATOR-ONLY"};
            case Backend::Vulkan:
                return version == 13u ? ContentSequenceDecision{version,
                                            CapabilityMode::Equivalent,
                                            false,
                                            true,
                                            false,
                                            "RHI-STORAGE-CONTENT-PLAN-VULKAN-EXECUTION-UNAVAILABLE"}
                                      : ContentSequenceDecision{version,
                                            CapabilityMode::DiagnosticFailure,
                                            false,
                                            false,
                                            false,
                                            "RHI-STORAGE-CONTENT-PLAN-VULKAN-PROFILE"};
            case Backend::Metal:
                return version == 13u ? ContentSequenceDecision{version,
                                            CapabilityMode::Equivalent,
                                            false,
                                            true,
                                            false,
                                            "RHI-STORAGE-CONTENT-PLAN-METAL-EXECUTION-UNAVAILABLE"}
                                      : ContentSequenceDecision{version,
                                            CapabilityMode::DiagnosticFailure,
                                            false,
                                            false,
                                            false,
                                            "RHI-STORAGE-CONTENT-PLAN-METAL-PROFILE"};
            case Backend::WebGPU:
                return version == 14u ? ContentSequenceDecision{version,
                                            CapabilityMode::Equivalent,
                                            false,
                                            true,
                                            false,
                                            "RHI-STORAGE-CONTENT-PLAN-WEBGPU-EXECUTION-UNAVAILABLE"}
                                      : ContentSequenceDecision{version,
                                            CapabilityMode::DiagnosticFailure,
                                            false,
                                            false,
                                            false,
                                            "RHI-STORAGE-CONTENT-PLAN-WEBGPU-PROFILE"};
            }
            return {version,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-STORAGE-CONTENT-PLAN-BACKEND"};
        }
        if (version == 12u) {
            switch (backend) {
            case Backend::WebGL:
                return {version,
                    CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    "RHI-WEBGL1-CONTENT-PLAN-UNAVAILABLE"};
            case Backend::Null:
                return {12u,
                    CapabilityMode::Equivalent,
                    true,
                    true,
                    false,
                    "RHI-DEPTH-ROOT-READ-PLAN-NULL-VALIDATOR-ONLY"};
            case Backend::Vulkan:
                return {12u,
                    CapabilityMode::Equivalent,
                    false,
                    true,
                    false,
                    "RHI-DEPTH-ROOT-READ-PLAN-VULKAN-EXECUTION-UNAVAILABLE"};
            case Backend::Metal:
                return {12u,
                    CapabilityMode::Equivalent,
                    false,
                    true,
                    false,
                    "RHI-DEPTH-ROOT-READ-PLAN-METAL-EXECUTION-UNAVAILABLE"};
            case Backend::WebGPU:
                return {12u,
                    CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    "RHI-DEPTH-ROOT-READ-PLAN-WEBGPU-PROFILE"};
            }
            return {12u,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-DEPTH-ROOT-READ-PLAN-BACKEND"};
        }
        if (version == 11u) {
            switch (backend) {
            case Backend::WebGL:
                return {version,
                    CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    "RHI-WEBGL1-CONTENT-PLAN-UNAVAILABLE"};
            case Backend::Null:
                return {11u,
                    CapabilityMode::Equivalent,
                    true,
                    true,
                    false,
                    "RHI-DEPTH-ROOT-READ-PLAN-NULL-VALIDATOR-ONLY"};
            case Backend::Vulkan:
                return {11u,
                    CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    "RHI-DEPTH-ROOT-READ-PLAN-VULKAN-JOIN-UNAVAILABLE"};
            case Backend::Metal:
                return {11u,
                    CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    "RHI-DEPTH-ROOT-READ-PLAN-METAL-JOIN-UNAVAILABLE"};
            case Backend::WebGPU:
                return {11u,
                    CapabilityMode::Equivalent,
                    false,
                    true,
                    false,
                    "RHI-DEPTH-ROOT-READ-PLAN-WEBGPU-EXECUTION-UNAVAILABLE"};
            }
            return {11u,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-DEPTH-ROOT-READ-PLAN-BACKEND"};
        }
        if (version == 7u) {
            switch (backend) {
            case Backend::WebGL:
                return {version,
                    CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    "RHI-WEBGL1-CONTENT-PLAN-UNAVAILABLE"};
            case Backend::Null:
                return {7u,
                    CapabilityMode::Equivalent,
                    true,
                    true,
                    false,
                    "RHI-UNDEFINED-TEXTURE-UPLOAD-PLAN-NULL-VALIDATOR-ONLY"};
            case Backend::Vulkan:
                return {7u,
                    CapabilityMode::Equivalent,
                    false,
                    true,
                    false,
                    "RHI-UNDEFINED-TEXTURE-UPLOAD-PLAN-VULKAN-NATIVE-CLOSURE-UNAVAILABLE"};
            case Backend::Metal:
                return {7u,
                    CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    "RHI-UNDEFINED-TEXTURE-UPLOAD-PLAN-METAL-JOIN-UNAVAILABLE"};
            case Backend::WebGPU:
                return {7u,
                    CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    "RHI-UNDEFINED-TEXTURE-UPLOAD-PLAN-WEBGPU-JOIN-UNAVAILABLE"};
            }
            return {7u,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-UNDEFINED-TEXTURE-UPLOAD-PLAN-BACKEND"};
        }
        if (version != 2u && version != 3u)
            return {version,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-CANONICAL-CONTENT-PLAN-VERSION"};
        if (version == 3u) {
            switch (backend) {
            case Backend::WebGL:
                return {version,
                    CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    "RHI-WEBGL1-CONTENT-PLAN-UNAVAILABLE"};
            case Backend::Null:
                return {3u,
                    CapabilityMode::Equivalent,
                    true,
                    false,
                    false,
                    "RHI-ORIGINAL-CONTENT-READBACK-PLAN-NULL-VALIDATOR-ONLY"};
            case Backend::Vulkan:
                return {3u,
                    CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    "RHI-ORIGINAL-CONTENT-READBACK-PLAN-VULKAN-JOIN-UNAVAILABLE"};
            case Backend::Metal:
                return {3u,
                    CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    "RHI-ORIGINAL-CONTENT-READBACK-PLAN-METAL-JOIN-UNAVAILABLE"};
            case Backend::WebGPU:
                return {3u,
                    CapabilityMode::Equivalent,
                    false,
                    true,
                    false,
                    "RHI-ORIGINAL-CONTENT-READBACK-PLAN-NATIVE-CLOSURE-UNAVAILABLE"};
            }
            return {3u,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-CONTENT-READBACK-PLAN-BACKEND"};
        }
        switch (backend) {
        case Backend::WebGL:
            return {version,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-WEBGL1-CONTENT-PLAN-UNAVAILABLE"};
        case Backend::Null:
            return {2u,
                CapabilityMode::Equivalent,
                true,
                false,
                false,
                "RHI-ORIGINAL-CONTENT-PLAN-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {2u,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-CONTENT-PLAN-VULKAN-JOIN-UNAVAILABLE"};
        case Backend::Metal:
            return {2u,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-CONTENT-PLAN-METAL-JOIN-UNAVAILABLE"};
        case Backend::WebGPU:
            return {2u,
                CapabilityMode::Equivalent,
                false,
                true,
                false,
                "RHI-ORIGINAL-CONTENT-PLAN-NATIVE-CLOSURE-UNAVAILABLE"};
        }
        return {};
    }

    struct FiniteGraphicsContentDecision {
        std::uint32_t contractVersion = 6u;
        CapabilityMode mode = CapabilityMode::DiagnosticFailure;
        bool validatorOnly = false, validationAvailable = false, rootAvailable = false,
             executionAvailable = false;
        const char* diagnostic = "RHI-FINITE-GRAPHICS-CONTENT-BACKEND";
    };

    // Content18 declares a shared color/attachment + completed ordinary depth
    // transaction. Null/Vulkan/Metal share the original CPU journal composition;
    // native closure, submission and completion remain explicitly unavailable.
    static constexpr FiniteGraphicsContentDecision bindlessGraphicsDepthContentPlanLowering(
        Backend backend, std::uint32_t version = 18u) noexcept
    {
        if (version != 18u && version != 19u)
            return {version,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-BINDLESS-GRAPHICS-DEPTH-CONTENT-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {version,
                CapabilityMode::Equivalent,
                true,
                true,
                true,
                false,
                "RHI-BINDLESS-GRAPHICS-DEPTH-CONTENT-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {version,
                CapabilityMode::DiagnosticFailure,
                false,
                true,
                version == 18u,
                false,
                "RHI-VULKAN-BINDLESS-GRAPHICS-DEPTH-CONTENT-JOIN-UNAVAILABLE"};
        case Backend::Metal:
            return {version,
                CapabilityMode::DiagnosticFailure,
                false,
                true,
                version == 18u,
                false,
                "RHI-METAL-BINDLESS-GRAPHICS-DEPTH-CONTENT-JOIN-UNAVAILABLE"};
        case Backend::WebGPU:
            return {version,
                CapabilityMode::DiagnosticFailure,
                false,
                true,
                version == 19u,
                false,
                "RHI-WEBGPU-BINDLESS-GRAPHICS-DEPTH-CONTENT-JOIN-UNAVAILABLE"};
        case Backend::WebGL:
            return {version,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-WEBGL1-BINDLESS-GRAPHICS-DEPTH-CONTENT-UNSUPPORTED"};
        }
        return {version,
            CapabilityMode::DiagnosticFailure,
            false,
            false,
            false,
            false,
            "RHI-BINDLESS-GRAPHICS-DEPTH-CONTENT-BACKEND"};
    }

    static constexpr FiniteGraphicsContentDecision bindlessGraphicsContentPlanLowering(
        Backend backend, std::uint32_t geometryVersion = 0u) noexcept
    {
        if (geometryVersion > 2u)
            return {9u,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-BINDLESS-GRAPHICS-CONTENT-GEOMETRY-VERSION"};
        if (geometryVersion == 1u || geometryVersion == 2u) {
            switch (backend) {
            case Backend::WebGL:
                return {9u,
                    CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    false,
                    "RHI-WEBGL1-BINDLESS-GRAPHICS-CONTENT-UNAVAILABLE"};
            case Backend::Null:
                return {9u,
                    CapabilityMode::Equivalent,
                    true,
                    true,
                    true,
                    false,
                    "RHI-BINDLESS-GRAPHICS-GEOMETRY-NULL-VALIDATOR-ONLY"};
            case Backend::Vulkan:
                return {9u,
                    CapabilityMode::Equivalent,
                    false,
                    true,
                    true,
                    false,
                    "RHI-BINDLESS-GRAPHICS-GEOMETRY-VULKAN-EXECUTION-UNAVAILABLE"};
            case Backend::Metal:
                return {9u,
                    CapabilityMode::Equivalent,
                    false,
                    true,
                    true,
                    false,
                    "RHI-BINDLESS-GRAPHICS-GEOMETRY-METAL-EXECUTION-UNAVAILABLE"};
            case Backend::WebGPU:
                return {9u,
                    CapabilityMode::Equivalent,
                    false,
                    true,
                    true,
                    false,
                    "RHI-BINDLESS-GRAPHICS-GEOMETRY-WEBGPU-EXECUTION-UNAVAILABLE"};
            }
            return {};
        }
        switch (backend) {
        case Backend::WebGL:
            return {9u,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-WEBGL1-BINDLESS-GRAPHICS-CONTENT-UNAVAILABLE"};
        case Backend::Null:
            return {9u,
                CapabilityMode::Equivalent,
                true,
                true,
                true,
                false,
                "RHI-BINDLESS-GRAPHICS-CONTENT-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {9u,
                CapabilityMode::Equivalent,
                false,
                true,
                true,
                false,
                "RHI-BINDLESS-GRAPHICS-CONTENT-VULKAN-EXECUTION-UNAVAILABLE"};
        case Backend::Metal:
            return {9u,
                CapabilityMode::Equivalent,
                false,
                true,
                true,
                false,
                "RHI-BINDLESS-GRAPHICS-CONTENT-METAL-EXECUTION-UNAVAILABLE"};
        case Backend::WebGPU:
            return {9u,
                CapabilityMode::Equivalent,
                false,
                true,
                true,
                false,
                "RHI-BINDLESS-GRAPHICS-CONTENT-WEBGPU-EXECUTION-UNAVAILABLE"};
        }
        return {9u,
            CapabilityMode::DiagnosticFailure,
            false,
            false,
            false,
            false,
            "RHI-BINDLESS-GRAPHICS-CONTENT-BACKEND"};
    }

    // Canonical upload control1 is independent of Vulkan control3. This
    // decision admits only CPU planning, never recording or completion.
    static constexpr FiniteGraphicsContentDecision bindlessCompletedReadPlanLowering(
        Backend backend, std::uint32_t version = 1u) noexcept
    {
        // /3: canonical control1 RGBA8/RGBA16F whole 2D/array/single Cube
        // (Read18). CPU planning is separate from native closure/execution.
        if (version == 3u) {
            switch (backend) {
            case Backend::Null:
                return {9u,
                    CapabilityMode::Equivalent,
                    true,
                    true,
                    true,
                    false,
                    "RHI-BINDLESS-GRAPHICS-CANONICAL-COLOR-NULL-VALIDATOR-ONLY"};
            case Backend::WebGPU:
                return {9u,
                    CapabilityMode::Equivalent,
                    false,
                    true,
                    true,
                    false,
                    "RHI-WEBGPU-BINDLESS-GRAPHICS-CANONICAL-COLOR-EXECUTION-SEPARATE"};
            case Backend::Vulkan:
                return {9u,
                    CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    false,
                    "RHI-VULKAN-BINDLESS-GRAPHICS-CANONICAL-COLOR-PROFILE"};
            case Backend::Metal:
                return {9u,
                    CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    false,
                    "RHI-METAL-BINDLESS-GRAPHICS-CANONICAL-COLOR-PROFILE"};
            case Backend::WebGL:
                return {9u,
                    CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    false,
                    "RHI-WEBGL1-BINDLESS-CANONICAL-COLOR-UNAVAILABLE"};
            }
            return {};
        }
        // /2: native control3 RGBA8/RGBA16F whole color views (Read17).
        // This decision authorizes CPU planning only, never an issuer.
        if (version == 2u) {
            switch (backend) {
            case Backend::Null:
                return {9u,
                    CapabilityMode::Equivalent,
                    true,
                    true,
                    true,
                    false,
                    "RHI-BINDLESS-GRAPHICS-HDR-READ-NULL-VALIDATOR-ONLY"};
            case Backend::Vulkan:
                return {9u,
                    CapabilityMode::Equivalent,
                    false,
                    true,
                    true,
                    false,
                    "RHI-BINDLESS-GRAPHICS-HDR-READ-EXECUTION-SEPARATE"};
            case Backend::Metal:
                return {9u,
                    CapabilityMode::Equivalent,
                    false,
                    true,
                    true,
                    false,
                    "RHI-METAL-BINDLESS-GRAPHICS-HDR-READ-EXECUTION-SEPARATE"};
            case Backend::WebGPU:
                return {9u,
                    CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    false,
                    "RHI-WEBGPU-BINDLESS-GRAPHICS-HDR-READ-UNAVAILABLE"};
            case Backend::WebGL:
                return {9u,
                    CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    false,
                    "RHI-WEBGL1-BINDLESS-COMPLETED-READ-UNAVAILABLE"};
            }
            return {};
        }
        if (version != 1u)
            return {9u,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-BINDLESS-GRAPHICS-COMPLETED-READ-VERSION"};
        switch (backend) {
        case Backend::WebGL:
            return {9u,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-WEBGL1-BINDLESS-COMPLETED-READ-UNAVAILABLE"};
        case Backend::Null:
            return {9u,
                CapabilityMode::Equivalent,
                true,
                true,
                true,
                false,
                "RHI-BINDLESS-GRAPHICS-COMPLETED-READ-NULL-VALIDATOR-ONLY"};
        case Backend::WebGPU:
            return {9u,
                CapabilityMode::Equivalent,
                false,
                true,
                true,
                false,
                "RHI-BINDLESS-GRAPHICS-COMPLETED-READ-EXECUTION-UNAVAILABLE"};
        case Backend::Vulkan:
            return {9u,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-BINDLESS-GRAPHICS-COMPLETED-READ-VULKAN-PROFILE"};
        case Backend::Metal:
            return {9u,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-BINDLESS-GRAPHICS-COMPLETED-READ-METAL-UNAVAILABLE"};
        }
        return {};
    }

    // Independent mixed-content value schema. Neither completed-read /4-/5
    // nor attachment /2-/3 acquires mixed-root or native authority from /6.
    static constexpr FiniteGraphicsContentDecision finiteGraphicsContentPlanLowering(
        Backend backend, std::uint32_t version = 6u) noexcept
    {
        if (version != 6u)
            return {version,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-FINITE-GRAPHICS-CONTENT-VERSION"};
        switch (backend) {
        case Backend::WebGL:
            return {version,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-WEBGL1-FINITE-GRAPHICS-CONTENT-UNAVAILABLE"};
        case Backend::Null:
            return {version,
                CapabilityMode::Equivalent,
                true,
                true,
                false,
                false,
                "RHI-FINITE-GRAPHICS-CONTENT-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {version,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-FINITE-GRAPHICS-CONTENT-VULKAN-JOIN-UNAVAILABLE"};
        case Backend::Metal:
            return {version,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-FINITE-GRAPHICS-CONTENT-METAL-JOIN-UNAVAILABLE"};
        case Backend::WebGPU:
            return {version,
                CapabilityMode::Equivalent,
                false,
                true,
                true,
                false,
                "RHI-FINITE-GRAPHICS-CONTENT-WEBGPU-NATIVE-CLOSURE-UNAVAILABLE"};
        }
        return {version,
            CapabilityMode::DiagnosticFailure,
            false,
            false,
            false,
            false,
            "RHI-FINITE-GRAPHICS-CONTENT-BACKEND"};
    }

    // Shared CPU read-plan inspection only. No backend receives recording,
    // submission or completion authority from an unchanged content stamp.
    static constexpr ContentSequenceDecision textureCompletedReadPlanLowering(
        Backend backend, std::uint32_t version = 4u) noexcept
    {
        if (version == 10u || version == 17u || version == 18u) {
            const auto mixed = bindlessCompletedReadPlanLowering(backend,
                version == 18u       ? 3u
                    : version == 17u ? 2u
                                     : 1u);
            return {version,
                mixed.mode,
                mixed.validatorOnly,
                mixed.validationAvailable,
                false,
                mixed.diagnostic};
        }
        if (version != 4u && version != 5u && version != 8u)
            return {version,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-COMPLETED-TEXTURE-READ-PLAN-VERSION"};
        switch (backend) {
        case Backend::WebGL:
            return {version,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-WEBGL1-COMPLETED-TEXTURE-READ-PLAN-UNAVAILABLE"};
        case Backend::Null:
            return {version,
                CapabilityMode::Equivalent,
                true,
                true,
                false,
                "RHI-COMPLETED-TEXTURE-READ-PLAN-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {version,
                CapabilityMode::Equivalent,
                false,
                true,
                false,
                "RHI-COMPLETED-TEXTURE-READ-PLAN-VULKAN-EXECUTION-UNAVAILABLE"};
        case Backend::Metal:
            return {version,
                CapabilityMode::Equivalent,
                false,
                true,
                false,
                "RHI-COMPLETED-TEXTURE-READ-PLAN-METAL-EXECUTION-UNAVAILABLE"};
        case Backend::WebGPU:
            return {version,
                CapabilityMode::Equivalent,
                false,
                true,
                false,
                "RHI-COMPLETED-TEXTURE-READ-PLAN-WEBGPU-EXECUTION-UNAVAILABLE"};
        }
        return {version,
            CapabilityMode::DiagnosticFailure,
            false,
            false,
            false,
            "RHI-COMPLETED-TEXTURE-READ-PLAN-BACKEND"};
    }
};
} // namespace arp::rhi::semantics

// endregion original_command_state_admission

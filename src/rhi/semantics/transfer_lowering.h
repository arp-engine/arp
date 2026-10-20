#pragma once
#include "rhi/gpu_common.h"

namespace arp::rhi::semantics {
class OriginalBindlessDirectResultCopyLowering final {
public:
    struct Decision {
        bool projectionAvailable = false, validatorOnly = false, executionAvailable = false;
        const char* diagnostic = "RHI-BINDLESS-DIRECT-RESULT-COPY-BACKEND";
    };

    static constexpr Decision lowering(Backend backend, std::uint32_t version) noexcept
    {
        if (version != 1u)
            return {false, false, false, "RHI-BINDLESS-DIRECT-RESULT-COPY-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {true, true, false, nullptr};
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return {true, false, false, nullptr};
        case Backend::WebGL:
            return {false, false, false, "RHI-WEBGL1-DIRECT-RESULT-COPY-UNSUPPORTED"};
        }
        return {};
    }
};

// Binding /2 can inspect the same authentic Pending journal and fault lease.
// This is retained CPU correspondence, never record/map/completion authority.
class OriginalBufferReadbackBindingLowering final {
public:
    struct Decision {
        std::uint32_t contractVersion = 0u;
        bool cpuMetadataImplemented = false, pendingAssociationImplemented = false;
        bool validatorOnly = false, nativeExecutionImplemented = false;
        const char* diagnostic = "RHI-ORIGINAL-BUFFER-READBACK-BINDING-BACKEND-UNKNOWN";
    };

    static constexpr Decision lowering(Backend backend, std::uint32_t version) noexcept
    {
        if (version < 1u || version > 6u)
            return {version,
                false,
                false,
                false,
                false,
                "RHI-ORIGINAL-BUFFER-READBACK-BINDING-VERSION"};
        if (version == 3u) {
            switch (backend) {
            case Backend::Null:
                return {3u, true, true, true, false, nullptr};
            case Backend::Vulkan:
                return {3u, true, true, false, false, nullptr};
            case Backend::Metal:
                return {3u, true, true, false, false, nullptr};
            case Backend::WebGPU:
                return {3u,
                    false,
                    false,
                    false,
                    false,
                    "RHI-ORIGINAL-GEOMETRY-READBACK-BINDING-WEBGPU-UNAVAILABLE"};
            case Backend::WebGL:
                return {version,
                    false,
                    false,
                    false,
                    false,
                    "RHI-WEBGL1-BUFFER-READBACK-BINDING-UNAVAILABLE"};
            }
            return {};
        }
        switch (backend) {
        case Backend::Null:
            return {version, true, version >= 2u, true, false, nullptr};
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return {version, true, version >= 2u, false, false, nullptr};
        case Backend::WebGL:
            return {version,
                false,
                false,
                false,
                false,
                "RHI-WEBGL1-BUFFER-READBACK-BINDING-UNAVAILABLE"};
        }
        return {};
    }
};

// Portable buffer request/result metadata is a CPU prerequisite. A real
// submission, map receipt and drained callbacks remain independent authority.
class BufferReadbackRequestLowering final {
public:
    struct Decision {
        std::uint32_t contractVersion = 0u;
        bool cpuMetadataImplemented = false, validatorOnly = false;
        bool nativeExecutionImplemented = false;
        const char* diagnostic = "RHI-BUFFER-READBACK-BACKEND-UNKNOWN";
    };

    static constexpr Decision lowering(Backend backend, std::uint32_t version = 1u) noexcept
    {
        if (version != 1u)
            return {version, false, false, false, "RHI-BUFFER-READBACK-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {version, true, true, false, nullptr};
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return {version, true, false, false, nullptr};
        case Backend::WebGL:
            return {version, false, false, false, "RHI-WEBGL1-BUFFER-READBACK-REQUEST-UNAVAILABLE"};
        }
        return {};
    }
};

// SourceLease2 retains the existing adopted command owner. CPU ownership is
// backend-neutral and never supplies recording/content/completion authority.
class TextureReadbackSourceLeaseLowering final {
public:
    struct Decision {
        std::uint32_t contractVersion = 0u;
        bool cpuMetadataImplemented = false, validatorOnly = false;
        bool nativeExecutionImplemented = false;
        const char* diagnostic = "RHI-READBACK-SOURCE-LEASE-BACKEND-UNKNOWN";
    };

    static constexpr Decision lowering(Backend backend, std::uint32_t version = 1u) noexcept
    {
        if (version != 1u && version != 2u)
            return {0u, false, false, false, "RHI-READBACK-SOURCE-LEASE-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {version,
                true,
                true,
                false,
                version == 1u ? "RHI-TEXTURE-READBACK-ORIGINAL-OWNER-UNPROVEN"
                              : "RHI-READBACK-SOURCE-LEASE-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return {version,
                true,
                false,
                false,
                version == 1u ? "RHI-TEXTURE-READBACK-ORIGINAL-OWNER-UNPROVEN"
                              : "RHI-READBACK-SOURCE-LEASE-COMPLETION-UNPROVEN"};
        case Backend::WebGL:
            return {version, false, false, false, "RHI-WEBGL1-READBACK-SOURCE-LEASE-UNAVAILABLE"};
        }
        return {};
    }
};

// Versioned value-only decisions. Template results preserve existing public decision types.
class TransferLowering final {
public:
    template <class Result>
    static constexpr Result TextureReadbackInputSnapshot_lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {true, true, "RHI-TEXTURE-READBACK-ORIGINAL-OWNER-UNPROVEN"};
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return {true, false, "RHI-TEXTURE-READBACK-ORIGINAL-OWNER-UNPROVEN"};
        case Backend::WebGL:
            return {false, false, "RHI-WEBGL1-READBACK-INPUT-SNAPSHOT-UNAVAILABLE"};
        }
        return {};
    }

    template <class Result, class Disposition>
    static constexpr Result BorrowedTextureReadbackRetirement_lowering(
        Backend backend, bool originalBoundedBudgetConfigured) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {1u, Disposition::Unavailable, false, "RHI-READBACK-BORROWED-NULL-UNAVAILABLE"};
        case Backend::Vulkan:
            return {1u,
                Disposition::RetiredDiagnosticFailure,
                false,
                "RHI-READBACK-BORROWED-VULKAN-RETIRED"};
        case Backend::Metal:
            return {1u,
                Disposition::ExplicitBoundedLegacy,
                originalBoundedBudgetConfigured,
                originalBoundedBudgetConfigured ? "RHI-READBACK-BORROWED-METAL-BOUNDED-LEGACY"
                                                : "RHI-READBACK-BUDGET-REQUIRED"};
        case Backend::WebGPU:
            return {1u,
                Disposition::RetiredDiagnosticFailure,
                false,
                "RHI-READBACK-BORROWED-WEBGPU-RETIRED"};
        case Backend::WebGL:
            return {
                1u, Disposition::Unavailable, false, "RHI-WEBGL1-READBACK-BORROWED-UNAVAILABLE"};
        }
        return {};
    }

    template <class Result>
    static constexpr Result TextureReadbackColdBudgetPlan_lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {true, true, "RHI-READBACK-COLD-BUDGET-VALIDATOR-ONLY"};
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return {true, false, "RHI-READBACK-COLD-BUDGET-NATIVE-EXECUTION-REQUIRED"};
        case Backend::WebGL:
            return {false, false, "RHI-WEBGL1-READBACK-COLD-BUDGET-UNAVAILABLE"};
        }
        return {};
    }

    template <class Result, class Outcome>
    static Result TextureReadbackContentIntent_lower(Backend backend, Result result) noexcept
    {
        switch (backend) {
        case Backend::Null:
            result.outcome = Outcome::ValidatorOnly;
            result.diagnostic = "RHI-READBACK-CONTENT-NULL-VALIDATOR-ONLY";
            break;
        case Backend::Vulkan:
        case Backend::Metal:
            result.diagnostic = "RHI-READBACK-CONTENT-BACKEND-UNIMPLEMENTED";
            break;
        case Backend::WebGPU:
            result.outcome = Outcome::Prototype;
            result.diagnostic = nullptr;
            break;
        case Backend::WebGL:
            result.outcome = Outcome::DiagnosticFailure;
            result.diagnostic = "RHI-WEBGL1-READBACK-CONTENT-UNAVAILABLE";
            break;
        default:
            result.diagnostic = "RHI-READBACK-CONTENT-BACKEND-UNKNOWN";
            break;
        }
        return result;
    }

    template <class Result>
    static constexpr Result TextureReadbackContentCoverage_lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return {true, false, "RHI-TEXTURE-READBACK-COVERAGE-EXECUTION-UNPROVEN"};
        case Backend::WebGL:
            return {false, false, "RHI-WEBGL1-READBACK-COVERAGE-UNAVAILABLE"};
        }
        return {};
    }

    template <class Result>
    static constexpr Result TextureReadbackStatisticsPlan_lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {true, true, "RHI-READBACK-STATISTICS-VALIDATOR-ONLY"};
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return {true, false, "RHI-READBACK-STATISTICS-NATIVE-PUBLICATION-REQUIRED"};
        case Backend::WebGL:
            return {false, false, "RHI-WEBGL1-READBACK-STATISTICS-UNAVAILABLE"};
        }
        return {};
    }

    template <class Result, class Disposition>
    static constexpr Result TextureReadbackOwnedPreparation_lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {
                1u, Disposition::ValidatorOnly, false, "RHI-READBACK-OWNED-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {1u,
                Disposition::DiagnosticFailure,
                false,
                "RHI-READBACK-OWNED-VULKAN-UNIMPLEMENTED"};
        case Backend::Metal:
            return {1u,
                Disposition::DiagnosticFailure,
                false,
                "RHI-READBACK-OWNED-METAL-UNIMPLEMENTED"};
        case Backend::WebGPU:
            return {1u,
                Disposition::PrototypeRequiresDeviceContext,
                false,
                "RHI-READBACK-OWNED-DEVICE-CONTEXT-UNIMPLEMENTED"};
        case Backend::WebGL:
            return {
                1u, Disposition::DiagnosticFailure, false, "RHI-WEBGL1-READBACK-OWNED-UNAVAILABLE"};
        }
        return {};
    }

    template <class Result>
    static constexpr Result TextureUploadPackedCopyMapping_lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return {true, false, "RHI-TEXTURE-PACKED-COPY-EXECUTION-UNPROVEN"};
        case Backend::WebGL:
            return {false, false, "RHI-WEBGL1-PACKED-COPY-UNAVAILABLE"};
        }
        return {};
    }

    template <class Result>
    static constexpr Result TextureUploadGenerationValidation_lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {true, false, "RHI-TEXTURE-UPLOAD-ORIGINAL-GENERATION-OWNER-UNPROVEN"};
        case Backend::Vulkan:
            return {false, false, "RHI-TEXTURE-UPLOAD-ORIGINAL-GENERATION-OWNER-UNPROVEN"};
        case Backend::Metal:
            return {false, false, "RHI-TEXTURE-UPLOAD-ORIGINAL-GENERATION-OWNER-UNPROVEN"};
        case Backend::WebGPU:
            return {false, false, "RHI-TEXTURE-UPLOAD-ORIGINAL-GENERATION-OWNER-UNPROVEN"};
        case Backend::WebGL:
            return {false, false, "RHI-WEBGL1-UPLOAD-GENERATION-UNAVAILABLE"};
        }
        return {};
    }

    static constexpr const char* uploadCopyGate(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return "RHI-TEXTURE-COPY-LOWERING-NULL-VALIDATOR-ONLY";
        case Backend::Vulkan:
        case Backend::Metal:
            return "RHI-TEXTURE-COPY-LOWERING-BACKEND-UNIMPLEMENTED";
        case Backend::WebGPU:
            break;
        case Backend::WebGL:
            return "RHI-WEBGL1-UPLOAD-COPY-UNAVAILABLE";
        default:
            return "RHI-TEXTURE-COPY-LOWERING-BACKEND-INVALID";
        }
        return nullptr;
    }

    static constexpr const char* readbackCopyGate(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return "RHI-READBACK-COPY-LOWERING-NULL-VALIDATOR-ONLY";
        case Backend::Vulkan:
        case Backend::Metal:
            return "RHI-READBACK-COPY-LOWERING-BACKEND-UNIMPLEMENTED";
        case Backend::WebGPU:
            break;
        case Backend::WebGL:
            return "RHI-WEBGL1-READBACK-COPY-UNAVAILABLE";
        default:
            return "RHI-READBACK-COPY-LOWERING-BACKEND-INVALID";
        }
        return nullptr;
    }

    template <class Decision, class Facts, class ValidateContext>
    static constexpr Decision resolveOwnedReadback(
        Backend backend, const Facts& facts, ValidateContext validate) noexcept
    {
        auto result =
            TextureReadbackOwnedPreparation_lowering<Decision, decltype(Decision{}.disposition)>(
                backend);
        if (backend != Backend::WebGPU)
            return result;
        result.diagnostic = validate(facts);
        if (!result.diagnostic) {
            result.disposition = decltype(result.disposition)::PrototypeContextReady;
            result.executionAvailable = true;
        }
        return result;
    }
};
} // namespace arp::rhi::semantics

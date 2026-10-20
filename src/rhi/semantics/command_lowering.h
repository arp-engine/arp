#pragma once

#include "rhi/frontend/descriptor_buffer_write_validation.h"
#include "rhi/frontend/descriptor_validation.h"
#include "rhi/gpu_common.h"
#include "rhi/gpu_resources.h"
#include "rhi/semantics/command_input_admission_lowering.h"
#include "rhi/semantics/transfer_dependency_lowering.h"

// region Command input binding lowering
// region original_command_typed_source
namespace arp::rhi::semantics {
class OriginalBindlessGraphicsPassLowering final {
public:
    struct Decision {
        std::uint32_t contractVersion = 1u;
        CapabilityMode mode = CapabilityMode::DiagnosticFailure;
        bool preparationAvailable = false, validatorOnly = false, executionAvailable = false;
        const char* diagnostic = "RHI-BINDLESS-GRAPHICS-PASS-BACKEND";
    };

    static constexpr Decision lowering(Backend backend, std::uint32_t version = 1u) noexcept
    {
        if (version != 1u && version != 2u)
            return {version,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-BINDLESS-GRAPHICS-PASS-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {version,
                CapabilityMode::Equivalent,
                false,
                true,
                false,
                "RHI-BINDLESS-GRAPHICS-PASS-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {version, CapabilityMode::Native, true, false, false, nullptr};
        case Backend::Metal:
            return {version, CapabilityMode::Native, true, false, false, nullptr};
        case Backend::WebGPU:
            return {version,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-BINDLESS-GRAPHICS-PASS-WEBGPU-UNAVAILABLE"};
        case Backend::WebGL:
            break;
        }
        return {};
    }
};

// Payload correspondence alone never promotes the public execution gate.
class OriginalBindlessRootNativeClosureLowering final {
public:
    struct Decision {
        std::uint32_t contractVersion = 1u;
        CapabilityMode mode = CapabilityMode::DiagnosticFailure;
        bool nativeCorrespondenceAvailable = false, validatorOnly = false;
        bool executionAvailable = false;
        const char* diagnostic = "RHI-BINDLESS-ROOT-NATIVE-CLOSURE-BACKEND";
    };

    static constexpr Decision lowering(Backend backend, std::uint32_t version = 1u) noexcept
    {
        if (version < 1u || version > 21u)
            return {version,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-BINDLESS-ROOT-NATIVE-CLOSURE-VERSION"};
        if (version == 21u) {
            switch (backend) {
            case Backend::Null:
                return {version,
                    CapabilityMode::Equivalent,
                    false,
                    true,
                    false,
                    "RHI-BINDLESS-PRODUCED-CLOSURE-NULL-VALIDATOR"};
            case Backend::Vulkan:
            case Backend::Metal:
                return {version, CapabilityMode::Native, true, false, false, nullptr};
            case Backend::WebGPU:
                return {version, CapabilityMode::Equivalent, true, false, false, nullptr};
            case Backend::WebGL:
                return {version,
                    CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    "RHI-WEBGL1-BINDLESS-PRODUCED-CLOSURE-UNSUPPORTED"};
            }
            return {};
        }
        if (version == 19u || version == 20u) {
            switch (backend) {
            case Backend::Null:
                return {version,
                    CapabilityMode::Equivalent,
                    false,
                    true,
                    false,
                    "RHI-BINDLESS-CANONICAL-DEPTH-CLOSURE-NULL-VALIDATOR-ONLY"};
            case Backend::WebGPU:
                return {version, CapabilityMode::Equivalent, true, false, false, nullptr};
            case Backend::Vulkan:
                return {version,
                    CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    "RHI-VULKAN-BINDLESS-CANONICAL-DEPTH-CLOSURE-UNAVAILABLE"};
            case Backend::Metal:
                return {version,
                    CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    "RHI-METAL-BINDLESS-CANONICAL-DEPTH-CLOSURE-UNAVAILABLE"};
            case Backend::WebGL:
                return {version,
                    CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    "RHI-WEBGL1-BINDLESS-CANONICAL-DEPTH-CLOSURE-UNSUPPORTED"};
            }
            return {};
        }
        if (version == 17u || version == 18u) {
            switch (backend) {
            case Backend::Null:
                return {version,
                    CapabilityMode::Equivalent,
                    false,
                    true,
                    false,
                    "RHI-BINDLESS-CANONICAL-COLOR-CLOSURE-NULL-VALIDATOR-ONLY"};
            case Backend::WebGPU:
                return {version, CapabilityMode::Equivalent, true, false, false, nullptr};
            case Backend::Vulkan:
                return {version,
                    CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    "RHI-VULKAN-BINDLESS-CANONICAL-COLOR-CLOSURE-UNAVAILABLE"};
            case Backend::Metal:
                return {version,
                    CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    "RHI-METAL-BINDLESS-CANONICAL-COLOR-CLOSURE-UNAVAILABLE"};
            case Backend::WebGL:
                return {version,
                    CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    "RHI-WEBGL1-BINDLESS-CANONICAL-COLOR-CLOSURE-UNSUPPORTED"};
            }
            return {};
        }
        if (version == 16u) {
            switch (backend) {
            case Backend::Null:
                return {version,
                    CapabilityMode::Equivalent,
                    false,
                    true,
                    false,
                    "RHI-BINDLESS-ORDINARY-DEPTH-CLOSURE-NULL-VALIDATOR-ONLY"};
            case Backend::Vulkan:
            case Backend::Metal:
                return {version, CapabilityMode::Native, true, false, false, nullptr};
            case Backend::WebGPU:
                return {version,
                    CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    "RHI-WEBGPU-BINDLESS-ORDINARY-DEPTH-CLOSURE-UNAVAILABLE"};
            case Backend::WebGL:
                return {version,
                    CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    "RHI-WEBGL1-BINDLESS-ORDINARY-DEPTH-CLOSURE-UNSUPPORTED"};
            }
            return {};
        }
        if (version == 14u || version == 15u) {
            switch (backend) {
            case Backend::Null:
                return {version,
                    CapabilityMode::Equivalent,
                    false,
                    true,
                    false,
                    "RHI-BINDLESS-ROOT-MIXED-NULL-VALIDATOR-ONLY"};
            case Backend::Vulkan:
            case Backend::Metal:
                if (version == 14u)
                    return {version, CapabilityMode::Native, true, false, false, nullptr};
                break;
            case Backend::WebGPU:
                if (version == 15u)
                    return {version, CapabilityMode::Equivalent, true, false, false, nullptr};
                break;
            case Backend::WebGL:
                break;
            }
            return {version,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-BINDLESS-ROOT-MIXED-EXECUTABLE-CLOSURE-PROFILE"};
        }
        if (version == 13u) {
            switch (backend) {
            case Backend::Null:
                return {version,
                    CapabilityMode::Equivalent,
                    false,
                    true,
                    false,
                    "RHI-BINDLESS-ROOT-MIXED-NULL-VALIDATOR-ONLY"};
            case Backend::Vulkan:
            case Backend::Metal:
                return {version, CapabilityMode::Native, true, false, false, nullptr};
            case Backend::WebGPU:
                return {version, CapabilityMode::Equivalent, true, false, false, nullptr};
            case Backend::WebGL:
                break;
            }
            return {};
        }
        if (version == 12u) {
            switch (backend) {
            case Backend::Null:
                return {version,
                    CapabilityMode::Equivalent,
                    false,
                    true,
                    false,
                    "RHI-BINDLESS-ROOT-STORAGE-NULL-VALIDATOR-ONLY"};
            case Backend::Vulkan:
                return {version,
                    CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    "RHI-BINDLESS-ROOT-STORAGE-VULKAN-CALLBACK-PROFILE"};
            case Backend::Metal:
                return {version,
                    CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    "RHI-BINDLESS-ROOT-STORAGE-METAL-CALLBACK-PROFILE"};
            case Backend::WebGPU:
                return {version, CapabilityMode::Equivalent, true, false, false, nullptr};
            case Backend::WebGL:
                break;
            }
            return {};
        }
        if (version == 11u) {
            switch (backend) {
            case Backend::Null:
                return {version,
                    CapabilityMode::Equivalent,
                    false,
                    true,
                    false,
                    "RHI-BINDLESS-ROOT-STORAGE-NULL-VALIDATOR-ONLY"};
            case Backend::Vulkan:
                return {version, CapabilityMode::Native, true, false, false, nullptr};
            case Backend::Metal:
                return {version, CapabilityMode::Native, true, false, false, nullptr};
            case Backend::WebGPU:
                return {version,
                    CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    "RHI-BINDLESS-ROOT-STORAGE-WEBGPU-EXECUTION-UNAVAILABLE"};
            case Backend::WebGL:
                break;
            }
            return {};
        }
        if (version == 10u) {
            switch (backend) {
            case Backend::Null:
                return {version,
                    CapabilityMode::Equivalent,
                    false,
                    true,
                    false,
                    "RHI-BINDLESS-ROOT-STORAGE-NULL-VALIDATOR-ONLY"};
            case Backend::Vulkan:
            case Backend::Metal:
                return {version, CapabilityMode::Native, true, false, false, nullptr};
            case Backend::WebGPU:
                return {version, CapabilityMode::Equivalent, true, false, false, nullptr};
            case Backend::WebGL:
                break;
            }
            return {};
        }
        if (version == 9u) {
            switch (backend) {
            case Backend::Null:
                return {version,
                    CapabilityMode::Equivalent,
                    false,
                    true,
                    false,
                    "RHI-BINDLESS-ROOT-DEPTH-NULL-VALIDATOR-ONLY"};
            case Backend::Vulkan:
                return {version, CapabilityMode::Native, true, false, false, nullptr};
            case Backend::Metal:
                return {version, CapabilityMode::Native, true, false, false, nullptr};
            case Backend::WebGPU:
                return {version,
                    CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    "RHI-BINDLESS-ROOT-DEPTH-WEBGPU-PROFILE"};
            case Backend::WebGL:
                break;
            }
            return {};
        }
        if (version == 7u || version == 8u) {
            switch (backend) {
            case Backend::Null:
                return {version,
                    CapabilityMode::Equivalent,
                    false,
                    true,
                    false,
                    "RHI-BINDLESS-ROOT-DEPTH-NULL-VALIDATOR-ONLY"};
            case Backend::Vulkan:
                return {version,
                    CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    "RHI-BINDLESS-ROOT-DEPTH-VULKAN-PROFILE"};
            case Backend::Metal:
                return {version,
                    CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    "RHI-BINDLESS-ROOT-DEPTH-METAL-UNAVAILABLE"};
            case Backend::WebGPU:
                return {version, CapabilityMode::Equivalent, true, false, false, nullptr};
            case Backend::WebGL:
                break;
            }
            return {};
        }
        if (version == 5u || version == 6u) {
            switch (backend) {
            case Backend::Null:
                return {version,
                    CapabilityMode::Equivalent,
                    false,
                    true,
                    false,
                    "RHI-BINDLESS-ROOT-SAMPLED-NULL-VALIDATOR-ONLY"};
            case Backend::Vulkan:
                return {version,
                    CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    "RHI-BINDLESS-ROOT-SAMPLED-VULKAN-PROFILE"};
            case Backend::Metal:
                return {version,
                    CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    "RHI-BINDLESS-ROOT-SAMPLED-METAL-UNAVAILABLE"};
            case Backend::WebGPU:
                return {version, CapabilityMode::Equivalent, true, false, false, nullptr};
            case Backend::WebGL:
                break;
            }
            return {};
        }
        if (version == 4u) {
            switch (backend) {
            case Backend::Null:
                return {4u,
                    CapabilityMode::Equivalent,
                    false,
                    true,
                    false,
                    "RHI-BINDLESS-ROOT-ATTACHMENT-NULL-VALIDATOR-ONLY"};
            case Backend::Vulkan:
                return {4u,
                    CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    "RHI-BINDLESS-ROOT-ATTACHMENT-VULKAN-PROFILE"};
            case Backend::Metal:
                return {4u,
                    CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    "RHI-BINDLESS-ROOT-ATTACHMENT-METAL-UNAVAILABLE"};
            case Backend::WebGPU:
                return {4u, CapabilityMode::Equivalent, true, false, false, nullptr};
            case Backend::WebGL:
                break;
            }
            return {};
        }
        if (version == 3u) {
            switch (backend) {
            case Backend::Null:
                return {3u,
                    CapabilityMode::Equivalent,
                    false,
                    true,
                    false,
                    "RHI-BINDLESS-ROOT-INITIALIZED-GEOMETRY-NULL-VALIDATOR-ONLY"};
            case Backend::Vulkan:
                return {3u, CapabilityMode::Native, true, false, false, nullptr};
            case Backend::Metal:
                return {3u, CapabilityMode::Native, true, false, false, nullptr};
            case Backend::WebGPU:
                return {3u, CapabilityMode::Equivalent, true, false, false, nullptr};
            case Backend::WebGL:
                break;
            }
            return {};
        }
        if (version == 2u) {
            switch (backend) {
            case Backend::Null:
                return {2u,
                    CapabilityMode::Equivalent,
                    false,
                    true,
                    false,
                    "RHI-BINDLESS-ROOT-NATIVE-CLOSURE-NULL-VALIDATOR-ONLY"};
            case Backend::Vulkan:
                return {2u, CapabilityMode::Native, true, false, false, nullptr};
            case Backend::Metal:
                return {2u,
                    CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    "RHI-BINDLESS-ROOT-GEOMETRY-CLOSURE-METAL-UNAVAILABLE"};
            case Backend::WebGPU:
                return {2u,
                    CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    "RHI-BINDLESS-ROOT-GEOMETRY-CLOSURE-WEBGPU-UNAVAILABLE"};
            case Backend::WebGL:
                break;
            }
            return {};
        }
        switch (backend) {
        case Backend::Null:
            return {1u,
                CapabilityMode::Equivalent,
                false,
                true,
                false,
                "RHI-BINDLESS-ROOT-NATIVE-CLOSURE-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {1u, CapabilityMode::Native, true, false, false, nullptr};
        case Backend::Metal:
            // Typed buffer/texture-view/sampler correspondence over the same
            // original inventory. Texture parents and allocation owners are
            // retained too. Image execution needs its independent content and
            // completion contract; correspondence never grants that authority.
            return {1u, CapabilityMode::Native, true, false, false, nullptr};
        case Backend::WebGPU:
            // OriginalPreparation/1: buffer-only Compute correspondence.
            // Recording/submission authority remains unavailable.
            return {1u, CapabilityMode::Equivalent, true, false, false, nullptr};
        case Backend::WebGL:
            break;
        }
        return {};
    }
};

// Original snapshot children use the existing Buffer lease language. This
// decision grants CPU correspondence only, never BindlessRoot/native execution.
class OriginalBindlessRootSourceLowering final {
public:
    static constexpr const char* nativeClosureDiagnostic(std::uint32_t version) noexcept
    {
        return version == 1u ? nullptr : "RHI-BINDLESS-ROOT-ORDINARY-NATIVE-CLOSURE-UNAVAILABLE";
    }

    static constexpr const char* nativeClosureDiagnostic(
        Backend backend, std::uint32_t version) noexcept
    {
        // Both native closures can freeze authentic ordinary scalar buffers.
        // This correspondence never authorizes recording or completion.
        return (backend == Backend::Vulkan || backend == Backend::Metal
                   || backend == Backend::WebGPU)
                && version == 2u
            ? nullptr
            : nativeClosureDiagnostic(version);
    }

    struct Decision {
        std::uint32_t contractVersion = 1u;
        CapabilityMode mode = CapabilityMode::DiagnosticFailure;
        bool sourceAvailable = false, executionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-BINDLESS-ROOT-BACKEND";
    };

    static constexpr Decision lowering(Backend backend, std::uint32_t version = 1u) noexcept
    {
        if (version != 1u && version != 2u)
            return {version,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                "RHI-ORIGINAL-BINDLESS-ROOT-VERSION"};
        switch (backend) {
        case Backend::Null:
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return {version, CapabilityMode::Equivalent, true, false, nullptr};
        case Backend::WebGL:
            break;
        }
        return {};
    }
};

class OriginalBindlessBufferSourceLowering final {
public:
    struct Decision {
        std::uint32_t contractVersion = 0u;
        CapabilityMode mode = CapabilityMode::DiagnosticFailure;
        bool sourceAvailable = false, validatorOnly = false, executionAvailable = false;
        const char* diagnostic = "RHI-BINDLESS-ORIGINAL-BUFFERS-BACKEND-UNKNOWN";
    };

    static constexpr Decision lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {1u, CapabilityMode::Equivalent, true, true, false, nullptr};
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return {1u, CapabilityMode::Equivalent, true, false, false, nullptr};
        case Backend::WebGL:
            break;
        }
        return {};
    }
};

// Independent CPU correspondence only. Native consumers must separately prove
// the original range/content, copy/map and genuine completion contract.
class OriginalRetainedReadbackCorrespondenceLowering final {
public:
    struct Decision {
        std::uint32_t contractVersion = 0u;
        bool cpuMetadataImplemented = false, validatorOnly = false;
        bool nativeExecutionImplemented = false;
        const char* diagnostic = "RHI-ORIGINAL-RETAINED-READBACK-BACKEND-UNKNOWN";
    };

    static constexpr Decision lowering(Backend backend, std::uint32_t version) noexcept
    {
        if (version != 1u)
            return {0u, false, false, false, "RHI-ORIGINAL-RETAINED-READBACK-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {
                version, true, true, false, "RHI-ORIGINAL-RETAINED-READBACK-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return {
                version, true, false, false, "RHI-ORIGINAL-RETAINED-READBACK-COMPLETION-UNPROVEN"};
        case Backend::WebGL:
            break;
        }
        return {};
    }
};

class OriginalCommandTypedSourceLowering final {
public:
    struct Decision {
        CapabilityMode mode = CapabilityMode::DiagnosticFailure;
        bool validatorOnly = false, sourceAdapterAvailable = false, executionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-TYPED-SOURCE-BACKEND-UNKNOWN";
    };

    // Version-controlled absence of original typed owned adapters/native profiles.
    // No caller fact, opt-in, hydrated flag, ordinary native handle or query-set
    // creation success can change these outcomes in this version.
    static constexpr Decision lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {CapabilityMode::DiagnosticFailure,
                true,
                false,
                false,
                "RHI-ORIGINAL-TYPED-SOURCE-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-TYPED-SOURCE-VULKAN-PAYLOAD-MISSING"};
        case Backend::Metal:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-TYPED-SOURCE-METAL-PAYLOAD-MISSING"};
        case Backend::WebGPU:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-TYPED-SOURCE-WEBGPU-PAYLOAD-MISSING"};
        case Backend::WebGL:
            break;
        }
        return {};
    }
};

// endregion original_command_typed_source

// region original_command_pipeline_binding

class OriginalCommandPipelineBindingLowering final {
public:
    struct Decision {
        CapabilityMode mode = CapabilityMode::DiagnosticFailure;
        bool validatorOnly = false, positionalAvailable = false, executionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-TYPED-INPUT-BACKEND";
    };

    static constexpr Decision lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {CapabilityMode::DiagnosticFailure,
                true,
                false,
                false,
                "RHI-ORIGINAL-TYPED-INPUT-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-TYPED-INPUT-VULKAN-PAYLOAD-MISSING"};
        case Backend::Metal:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-TYPED-INPUT-METAL-PAYLOAD-MISSING"};
        case Backend::WebGPU:
            return {CapabilityMode::Equivalent, false, true, false, nullptr};
        case Backend::WebGL:
            break;
        }
        return {};
    }
};

// endregion original_command_pipeline_binding

// region original_command_descriptor_binding

class OriginalCommandDescriptorBindingLowering final {
public:
    struct TableDecision {
        CapabilityMode mode = CapabilityMode::DiagnosticFailure;
        bool validatorOnly = false, positionalAvailable = false, executionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-TYPED-INPUT-TABLE-PROFILE-UNAVAILABLE";
    };

    // TableLowering1 excludes the independently selected scalar publication.
    // Ordinary array actors are absent; metadata-only Sources cannot replace them.
    static constexpr TableDecision tableLowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {
                CapabilityMode::DiagnosticFailure, true, false, false, tableProfileDiagnostic()};
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return {};
        case Backend::WebGL:
            // OriginalDepthTableScalarExpansion/1 has a frozen four-key
            // compiler schema; it is not a WebGL shader package or table actor.
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-WEBGL1-ORIGINAL-TYPED-INPUT-TABLE-UNAVAILABLE"};
        }
        return {};
    }

    static constexpr const char* tableProfileDiagnostic() noexcept
    {
        return "RHI-ORIGINAL-TYPED-INPUT-TABLE-PROFILE-UNAVAILABLE";
    }

    // OriginalDepthTableBinding/1, consumed only by explicitly opted-in Inputs4.
    // Positional validation does not grant state, recording or submission.
    static constexpr TableDecision depthTableLowering(
        Backend backend, bool committedOriginalPublication = false) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {CapabilityMode::DiagnosticFailure,
                true,
                false,
                false,
                "RHI-ORIGINAL-TABLE-BINDING-PAYLOAD-UNAVAILABLE"};
        case Backend::Vulkan:
        case Backend::Metal:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-TABLE-BINDING-PAYLOAD-UNAVAILABLE"};
        case Backend::WebGPU:
            if (committedOriginalPublication)
                return {CapabilityMode::Equivalent, false, true, false, nullptr};
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-TABLE-BINDING-PAYLOAD-UNAVAILABLE"};
        case Backend::WebGL:
            break;
        }
        return {};
    }
};

// endregion original_command_descriptor_binding

// region original_command_stream

class OriginalCommandStreamLowering final {
public:
    static constexpr std::uint32_t version = 1u;
    using Disposition = arp::rhi::semantics::OriginalCommandAdmissionLowering::Disposition;

    struct Decision {
        std::uint32_t contractVersion = version;
        Disposition disposition = Disposition::DiagnosticFailure;
        bool nativeExecutionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-STREAM-BACKEND-UNKNOWN";
    };

    // RootState /2 joins the same immutable program, without native authority.
    static constexpr Decision bindlessRootLowering(
        Backend backend, std::uint32_t version = 2u) noexcept
    {
        if (version < 2u || version > 6u)
            return {version,
                Disposition::DiagnosticFailure,
                false,
                "RHI-ORIGINAL-BINDLESS-STREAM-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {version,
                Disposition::ValidatorOnly,
                false,
                "RHI-ORIGINAL-BINDLESS-STREAM-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {version,
                Disposition::DiagnosticFailure,
                false,
                "RHI-ORIGINAL-BINDLESS-STREAM-VULKAN-EXECUTOR-MISSING"};
        case Backend::Metal:
            return {version,
                Disposition::DiagnosticFailure,
                false,
                "RHI-ORIGINAL-BINDLESS-STREAM-METAL-EXECUTOR-MISSING"};
        case Backend::WebGPU:
            return {version,
                Disposition::DiagnosticFailure,
                false,
                "RHI-ORIGINAL-BINDLESS-STREAM-WEBGPU-EXECUTOR-MISSING"};
        case Backend::WebGL:
            break;
        }
        return {version,
            Disposition::DiagnosticFailure,
            false,
            "RHI-ORIGINAL-BINDLESS-STREAM-BACKEND-UNKNOWN"};
    }

    // Version-controlled complete-program executor absence. A selected private
    // CopyPass executor does not implement this broader contract. No backend
    // identity or successful CPU compilation can promote these gates.
    static constexpr Decision lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {version,
                Disposition::ValidatorOnly,
                false,
                "RHI-ORIGINAL-STREAM-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {version,
                Disposition::DiagnosticFailure,
                false,
                "RHI-ORIGINAL-STREAM-VULKAN-EXECUTOR-MISSING"};
        case Backend::Metal:
            return {version,
                Disposition::DiagnosticFailure,
                false,
                "RHI-ORIGINAL-STREAM-METAL-EXECUTOR-MISSING"};
        case Backend::WebGPU:
            return {version,
                Disposition::DiagnosticFailure,
                false,
                "RHI-ORIGINAL-STREAM-WEBGPU-EXECUTOR-MISSING"};
        case Backend::WebGL:
            break;
        }
        return {};
    }

    static constexpr Decision typedLowering(
        Backend backend, std::uint32_t contractVersion = 2u) noexcept
    {
        if (contractVersion != 2u && contractVersion != 3u && contractVersion != 4u
            && contractVersion != 5u)
            return {contractVersion,
                Disposition::DiagnosticFailure,
                false,
                "RHI-ORIGINAL-STREAM-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {contractVersion,
                Disposition::ValidatorOnly,
                false,
                "RHI-ORIGINAL-STREAM-TYPED-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {contractVersion,
                Disposition::DiagnosticFailure,
                false,
                "RHI-ORIGINAL-STREAM-TYPED-VULKAN-ACTOR-MISSING"};
        case Backend::Metal:
            return {contractVersion,
                Disposition::DiagnosticFailure,
                false,
                "RHI-ORIGINAL-STREAM-TYPED-METAL-ACTOR-MISSING"};
        case Backend::WebGPU:
            return {contractVersion,
                Disposition::DiagnosticFailure,
                false,
                "RHI-ORIGINAL-STREAM-TYPED-WEBGPU-NATIVE-CLOSED"};
        case Backend::WebGL:
            break;
        }
        return {contractVersion,
            Disposition::DiagnosticFailure,
            false,
            "RHI-ORIGINAL-STREAM-TYPED-BACKEND-UNKNOWN"};
    }
};

// endregion original_command_stream
// endregion Command input binding lowering

// region Command native closure lowering

// region original_command_native_closure

class OriginalCommandNativeClosureLowering final {
public:
    enum class Disposition { ValidatorOnly, Prototype, DiagnosticFailure };

    struct Decision {
        Disposition disposition = Disposition::DiagnosticFailure;
        bool resourceClosureAvailable = false, recordingIssuerAvailable = false,
             executionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-NATIVE-CLOSURE-BACKEND-UNKNOWN";
    };

    // AttachmentReadbackResourceClosure/1 composes State1 resource custody
    // with ContentPlan3. Authentic Output3/Join4 is checked before preparation
    // and retained separately by Cold5; this decision grants no executor.
    static constexpr Decision attachmentReadbackLossBoundLowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {Disposition::ValidatorOnly,
                false,
                false,
                false,
                "RHI-ORIGINAL-NATIVE-CLOSURE-ATTACHMENT-READBACK-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {Disposition::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-NATIVE-CLOSURE-ATTACHMENT-READBACK-VULKAN-JOIN-MISSING"};
        case Backend::Metal:
            return {Disposition::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-NATIVE-CLOSURE-ATTACHMENT-READBACK-METAL-JOIN-MISSING"};
        case Backend::WebGPU:
            return {Disposition::Prototype,
                true,
                false,
                false,
                "RHI-ORIGINAL-NATIVE-CLOSURE-ATTACHMENT-READBACK-WEBGPU-PRIVATE-PROTOTYPE"};
        case Backend::WebGL:
            break;
        }
        return {};
    }

    // Closure10 has an independent Graphics ContentPlan6 domain. Value intent
    // grants only explicit private WebGPU custody; recording and execution
    // require independent successor contracts.
    static constexpr Decision finiteGraphicsLossBoundLowering(Backend backend) noexcept
    {
        using D = Disposition;
        switch (backend) {
        case Backend::Null:
            return {D::ValidatorOnly,
                false,
                false,
                false,
                "RHI-ORIGINAL-GRAPHICS-CUSTODY-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {D::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-GRAPHICS-CUSTODY-VULKAN-JOIN-MISSING"};
        case Backend::Metal:
            return {D::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-GRAPHICS-CUSTODY-METAL-JOIN-MISSING"};
        case Backend::WebGPU:
            return {D::Prototype,
                true,
                false,
                false,
                "RHI-ORIGINAL-GRAPHICS-CUSTODY-WEBGPU-RECORDING-UNPROVEN"};
        case Backend::WebGL:
            break;
        }
        return {D::DiagnosticFailure,
            false,
            false,
            false,
            "RHI-ORIGINAL-GRAPHICS-CUSTODY-BACKEND-UNKNOWN"};
    }

    // Closure8 retains committed GeneralWrite1 and canonical memory/sampler
    // owners. This cold join grants no recording or completion authority.
    static constexpr Decision generalResourcesLossBoundLowering(
        Backend backend, std::uint32_t version = 1u) noexcept
    {
        if (version != 1u && version != 2u)
            return {Disposition::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-GENERAL-CUSTODY-VERSION"};
        if (version == 2u) {
            switch (backend) {
            case Backend::Null:
                return {Disposition::ValidatorOnly,
                    false,
                    false,
                    false,
                    "RHI-ORIGINAL-GENERAL-FINITE-CUSTODY-NULL-VALIDATOR-ONLY"};
            case Backend::Vulkan:
                return {Disposition::DiagnosticFailure,
                    false,
                    false,
                    false,
                    "RHI-ORIGINAL-GENERAL-FINITE-CUSTODY-VULKAN-JOIN-MISSING"};
            case Backend::Metal:
                return {Disposition::DiagnosticFailure,
                    false,
                    false,
                    false,
                    "RHI-ORIGINAL-GENERAL-FINITE-CUSTODY-METAL-JOIN-MISSING"};
            case Backend::WebGPU:
                return {Disposition::Prototype,
                    true,
                    false,
                    false,
                    "RHI-ORIGINAL-GENERAL-FINITE-CUSTODY-WEBGPU-RECORDING-UNPROVEN"};
            case Backend::WebGL:
                break;
            }
            return {};
        }
        using D = Disposition;
        switch (backend) {
        case Backend::Null:
            return {D::ValidatorOnly,
                false,
                false,
                false,
                "RHI-ORIGINAL-GENERAL-CUSTODY-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {D::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-GENERAL-CUSTODY-VULKAN-JOIN-MISSING"};
        case Backend::Metal:
            return {D::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-GENERAL-CUSTODY-METAL-JOIN-MISSING"};
        case Backend::WebGPU:
            return {D::Prototype,
                true,
                false,
                false,
                "RHI-ORIGINAL-GENERAL-CUSTODY-WEBGPU-RECORDING-UNPROVEN"};
        case Backend::WebGL:
            break;
        }
        return {};
    }

    // OriginalIndexContentCustody/1 retains initialized sources in the existing
    // closure. It grants neither native geometry recording nor completion.
    static constexpr Decision indexContentLossBoundLowering(Backend backend) noexcept
    {
        using D = Disposition;
        switch (backend) {
        case Backend::Null:
            return {D::ValidatorOnly,
                false,
                false,
                false,
                "RHI-ORIGINAL-INDEX-CUSTODY-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {D::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-INDEX-CUSTODY-VULKAN-JOIN-MISSING"};
        case Backend::Metal:
            return {D::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-INDEX-CUSTODY-METAL-JOIN-MISSING"};
        case Backend::WebGPU:
            return {D::Prototype,
                true,
                false,
                false,
                "RHI-ORIGINAL-INDEX-CUSTODY-WEBGPU-RECORDING-UNPROVEN"};
        case Backend::WebGL:
            break;
        }
        return {};
    }

    static constexpr Decision geometryContentLossBoundLowering(Backend backend) noexcept
    {
        using D = Disposition;
        switch (backend) {
        case Backend::Null:
            return {D::ValidatorOnly,
                false,
                false,
                false,
                "RHI-ORIGINAL-GEOMETRY-CUSTODY-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {D::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-GEOMETRY-CUSTODY-VULKAN-JOIN-MISSING"};
        case Backend::Metal:
            return {D::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-GEOMETRY-CUSTODY-METAL-JOIN-MISSING"};
        case Backend::WebGPU:
            return {D::Prototype,
                true,
                false,
                false,
                "RHI-ORIGINAL-GEOMETRY-CUSTODY-WEBGPU-RECORDING-UNPROVEN"};
        case Backend::WebGL:
            break;
        }
        return {};
    }

    // Closure7 is an independent composition. Existing Geometry6 or Join2
    // availability never grants same-root composition. Only explicit Closure7
    // cold custody is implemented; frozen recording and execution remain closed.
    static constexpr Decision geometryReadbackLossBoundLowering(Backend backend) noexcept
    {
        using D = Disposition;
        switch (backend) {
        case Backend::Null:
            return {D::ValidatorOnly,
                false,
                false,
                false,
                "RHI-ORIGINAL-GEOMETRY-READBACK-CUSTODY-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {D::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-GEOMETRY-READBACK-CUSTODY-VULKAN-JOIN-MISSING"};
        case Backend::Metal:
            return {D::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-GEOMETRY-READBACK-CUSTODY-METAL-JOIN-MISSING"};
        case Backend::WebGPU:
            return {D::Prototype,
                true,
                false,
                false,
                "RHI-ORIGINAL-GEOMETRY-READBACK-CUSTODY-WEBGPU-RECORDING-UNPROVEN"};
        case Backend::WebGL:
            break;
        }
        return {};
    }

    static constexpr Decision geometryPendingReadLowering(
        Backend backend, std::uint32_t version = 1u) noexcept
    {
        if (version != 1u)
            return {Disposition::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-GEOMETRY-PENDING-READ-VERSION"};
        auto result = geometryContentLossBoundLowering(backend);
        if (backend == Backend::WebGPU)
            result.diagnostic = "RHI-ORIGINAL-GEOMETRY-PENDING-READ-WEBGPU-PRIVATE-PROTOTYPE";
        return result;
    }

    static constexpr Decision generalPendingReadLowering(
        Backend backend, std::uint32_t version = 1u) noexcept
    {
        if (version == 3u) {
            switch (backend) {
            case Backend::Null:
                return {Disposition::ValidatorOnly,
                    false,
                    false,
                    false,
                    "RHI-ORIGINAL-GRAPHICS-PENDING-READ-NULL-VALIDATOR-ONLY"};
            case Backend::Vulkan:
                return {Disposition::DiagnosticFailure,
                    false,
                    false,
                    false,
                    "RHI-ORIGINAL-GRAPHICS-PENDING-READ-VULKAN-JOIN-MISSING"};
            case Backend::Metal:
                return {Disposition::DiagnosticFailure,
                    false,
                    false,
                    false,
                    "RHI-ORIGINAL-GRAPHICS-PENDING-READ-METAL-JOIN-MISSING"};
            case Backend::WebGPU:
                return {Disposition::Prototype,
                    true,
                    false,
                    false,
                    "RHI-ORIGINAL-GRAPHICS-PENDING-READ-WEBGPU-PRIVATE-PROTOTYPE"};
            case Backend::WebGL:
                break;
            }
            return {Disposition::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-GRAPHICS-PENDING-READ-BACKEND"};
        }
        if (version != 1u && version != 2u)
            return {Disposition::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-GENERAL-PENDING-READ-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {Disposition::ValidatorOnly,
                false,
                false,
                false,
                "RHI-ORIGINAL-GENERAL-PENDING-READ-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {Disposition::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-GENERAL-PENDING-READ-VULKAN-JOIN-MISSING"};
        case Backend::Metal:
            return {Disposition::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-GENERAL-PENDING-READ-METAL-JOIN-MISSING"};
        case Backend::WebGPU:
            return {Disposition::Prototype,
                true,
                false,
                false,
                "RHI-ORIGINAL-GENERAL-PENDING-READ-WEBGPU-PRIVATE-PROTOTYPE"};
        case Backend::WebGL:
            break;
        }
        return {};
    }

    // /2 selected metadata closure: exact Stream2/State2/root and actual
    // immutable native payload joins are mandatory. No /1 adaptation.
    static constexpr Decision typedLowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {Disposition::ValidatorOnly,
                false,
                false,
                false,
                "RHI-ORIGINAL-NATIVE-CLOSURE-TYPED-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {Disposition::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-NATIVE-CLOSURE-TYPED-VULKAN-ISSUER-MISSING"};
        case Backend::Metal:
            return {Disposition::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-NATIVE-CLOSURE-TYPED-METAL-ISSUER-MISSING"};
        case Backend::WebGPU:
            return {Disposition::Prototype,
                true,
                false,
                false,
                "RHI-ORIGINAL-NATIVE-CLOSURE-TYPED-WEBGPU-RECORDING-UNPROVEN"};
        case Backend::WebGL:
            break;
        }
        return {};
    }

    // Original registry versions1-6 explicitly refuse native-loss-bound typed
    // factories. Registration7 requires the exact weak subscription join;
    // unbound /2 metadata availability cannot enable this join.
    static constexpr Decision typedLossBoundLowering(Backend backend) noexcept
    {
        using D = Disposition;
        switch (backend) {
        case Backend::Null:
            return {D::ValidatorOnly,
                false,
                false,
                false,
                "RHI-ORIGINAL-NATIVE-CLOSURE-TYPED-LOSS-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {D::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-NATIVE-CLOSURE-TYPED-LOSS-VULKAN-JOIN-MISSING"};
        case Backend::Metal:
            return {D::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-NATIVE-CLOSURE-TYPED-LOSS-METAL-JOIN-MISSING"};
        case Backend::WebGPU:
            return {D::Prototype,
                true,
                false,
                false,
                "RHI-ORIGINAL-NATIVE-CLOSURE-TYPED-LOSS-WEBGPU-RECORDING-UNPROVEN"};
        case Backend::WebGL:
            break;
        }
        return {};
    }

    // Closure3 owns the selected original Registry8 table/content graph.
    // Availability of Closure2 metadata never authorizes this independent join.
    static constexpr Decision tableLossBoundLowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {Disposition::ValidatorOnly,
                false,
                false,
                false,
                "RHI-ORIGINAL-NATIVE-CLOSURE-TABLE-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {Disposition::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-NATIVE-CLOSURE-TABLE-VULKAN-JOIN-MISSING"};
        case Backend::Metal:
            return {Disposition::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-NATIVE-CLOSURE-TABLE-METAL-JOIN-MISSING"};
        case Backend::WebGPU:
            return {Disposition::Prototype,
                true,
                false,
                false,
                "RHI-ORIGINAL-NATIVE-CLOSURE-TABLE-WEBGPU-RECORDING-UNPROVEN"};
        case Backend::WebGL:
            break;
        }
        return {};
    }

    // Closure4 retains the same original mixed readback program and Join2.
    // Metadata custody is independent of all recording/map/completion gates.
    static constexpr Decision tableReadbackLossBoundLowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {Disposition::ValidatorOnly,
                false,
                false,
                false,
                "RHI-ORIGINAL-NATIVE-CLOSURE-TABLE-READBACK-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {Disposition::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-NATIVE-CLOSURE-TABLE-READBACK-VULKAN-JOIN-MISSING"};
        case Backend::Metal:
            return {Disposition::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-NATIVE-CLOSURE-TABLE-READBACK-METAL-JOIN-MISSING"};
        case Backend::WebGPU:
            return {Disposition::Prototype,
                true,
                false,
                false,
                "RHI-ORIGINAL-NATIVE-CLOSURE-TABLE-READBACK-WEBGPU-RECORDING-UNPROVEN"};
        case Backend::WebGL:
            break;
        }
        return {};
    }

    // Availability describes a version-controlled selected resource closure,
    // never inferred DeviceCapabilities. Runtime native absence still refuses.
    static constexpr Decision lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {Disposition::ValidatorOnly,
                false,
                false,
                false,
                "RHI-ORIGINAL-NATIVE-CLOSURE-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {Disposition::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-NATIVE-CLOSURE-VULKAN-UNIMPLEMENTED"};
        case Backend::Metal:
            return {Disposition::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-NATIVE-CLOSURE-METAL-UNIMPLEMENTED"};
        case Backend::WebGPU:
            return {Disposition::Prototype,
                true,
                false,
                false,
                "RHI-ORIGINAL-NATIVE-CLOSURE-WEBGPU-RECORDING-UNPROVEN"};
        case Backend::WebGL:
            break;
        }
        return {};
    }
};

// endregion original_command_native_closure
// endregion Command native closure lowering

// region Command source lowering

// region original_command_descriptor_source

class OriginalCommandDescriptorSourceLowering final {
public:
    struct Decision {
        CapabilityMode mode = CapabilityMode::DiagnosticFailure;
        bool freezeAvailable = false, positionalAdapterAvailable = false,
             executionAvailable = false;
    };

    static constexpr Decision lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return {CapabilityMode::Equivalent, true, false, false};
        case Backend::WebGL:
            break;
        }
        return {};
    }

    struct FiniteDecision : Decision {
        const char* diagnostic = "RHI-ORIGINAL-FINITE-SOURCE-BACKEND";
    };

    static constexpr FiniteDecision completedDepthLowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return {{CapabilityMode::Equivalent, true, false, false}, nullptr};
        case Backend::WebGL:
            return {{CapabilityMode::DiagnosticFailure, false, false, false},
                "RHI-WEBGL1-ORIGINAL-COMPLETED-DEPTH-SOURCE-UNAVAILABLE"};
        }
        return {{CapabilityMode::DiagnosticFailure, false, false, false},
            "RHI-ORIGINAL-COMPLETED-DEPTH-SOURCE-BACKEND"};
    }

    static constexpr FiniteDecision generalFiniteLowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {{CapabilityMode::DiagnosticFailure, false, false, false},
                "RHI-ORIGINAL-GENERAL-SOURCE-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {{CapabilityMode::DiagnosticFailure, false, false, false},
                "RHI-ORIGINAL-GENERAL-SOURCE-VULKAN-ADAPTER-MISSING"};
        case Backend::Metal:
            return {{CapabilityMode::DiagnosticFailure, false, false, false},
                "RHI-ORIGINAL-GENERAL-SOURCE-METAL-ADAPTER-MISSING"};
        case Backend::WebGPU:
            return {{CapabilityMode::Equivalent, true, false, false}, nullptr};
        case Backend::WebGL:
            break;
        }
        return {{CapabilityMode::DiagnosticFailure, false, false, false},
            "RHI-ORIGINAL-GENERAL-SOURCE-BACKEND"};
    }

    static constexpr FiniteDecision finiteLowering(
        Backend backend, std::uint32_t version = 3u) noexcept
    {
        const auto decision = version == 4u
            ? OriginalFiniteTargetMemoryWriteContract::sourceDiagnosticDecision(backend)
            : OriginalFiniteDescriptorWriteContract::sourceDiagnosticDecision(backend);
        return {{decision.mode, decision.preparationAvailable, false, false}, decision.diagnostic};
    }
};

// endregion original_command_descriptor_source

// region original_command_pipeline_source

class OriginalCommandPipelineSourceLowering final {
public:
    struct Decision {
        CapabilityMode mode = CapabilityMode::DiagnosticFailure;
        bool validatorOnly = false, custodyAvailable = false, positionalAdapterAvailable = false,
             executionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-PIPELINE-SOURCE-BACKEND";
    };

    static constexpr Decision lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {CapabilityMode::DiagnosticFailure,
                true,
                false,
                false,
                false,
                "RHI-ORIGINAL-PIPELINE-SOURCE-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-ORIGINAL-PIPELINE-SOURCE-VULKAN-PAYLOAD-MISSING"};
        case Backend::Metal:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-ORIGINAL-PIPELINE-SOURCE-METAL-PAYLOAD-MISSING"};
        case Backend::WebGPU:
            return {CapabilityMode::Equivalent, false, true, false, false, nullptr};
        case Backend::WebGL:
            break;
        }
        return {};
    }
};

// endregion original_command_pipeline_source

// region original_command_texture_upload_packing_source

class OriginalCommandTextureUploadPackingSourceLowering final {
public:
    enum class Disposition { ValidatorOnly, Prototype, DiagnosticFailure };

    struct Decision {
        Disposition disposition = Disposition::DiagnosticFailure;
        bool privateStagingJoinAvailable = false, contentExecutionAvailable = false;
        const char* diagnostic = "RHI-CANONICAL-TEXTURE-STAGING-BACKEND-UNKNOWN";
    };

    static constexpr Decision lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {Disposition::ValidatorOnly,
                false,
                false,
                "RHI-CANONICAL-TEXTURE-STAGING-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {Disposition::DiagnosticFailure,
                false,
                false,
                "RHI-CANONICAL-TEXTURE-STAGING-VULKAN-PAYLOAD-MISSING"};
        case Backend::Metal:
            return {Disposition::DiagnosticFailure,
                false,
                false,
                "RHI-CANONICAL-TEXTURE-STAGING-METAL-PAYLOAD-MISSING"};
        case Backend::WebGPU:
            return {Disposition::Prototype,
                true,
                false,
                "RHI-CANONICAL-TEXTURE-STAGING-CONTENT-EXPECTATION-MISSING"};
        case Backend::WebGL:
            break;
        }
        return {};
    }
};

// endregion original_command_texture_upload_packing_source
// endregion Command source lowering

// region Command native lifecycle lowering

// region original_command_native_lifecycle

// Immutable staging initialization only. It does not issue texture content,
// recording or completion authority, and never reuses WebGPU callback budgets.
class OriginalTextureUploadStagingLowering final {
public:
    enum class Disposition { ValidatorOnly, DiagnosticFailure, Prototype };

    struct Decision {
        Disposition disposition = Disposition::DiagnosticFailure;
        bool factoryAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-TEXTURE-STAGING-BACKEND";
    };

    static constexpr Decision lowering(Backend backend, std::uint32_t version = 1u) noexcept
    {
        if (version != 1u && version != 2u)
            return {Disposition::DiagnosticFailure, false, "RHI-ORIGINAL-TEXTURE-STAGING-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {Disposition::ValidatorOnly,
                false,
                "RHI-ORIGINAL-TEXTURE-STAGING-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {Disposition::Prototype, true, nullptr};
        case Backend::Metal:
            return {Disposition::Prototype, true, nullptr};
        case Backend::WebGPU:
            return {Disposition::DiagnosticFailure,
                false,
                "RHI-ORIGINAL-TEXTURE-STAGING-WEBGPU-NATIVE-PROFILE-UNAVAILABLE"};
        case Backend::WebGL:
            break;
        }
        return {};
    }
};

class OriginalBindlessRootCallbackColdLowering final {
public:
    struct Decision {
        bool preparationAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-ROOT-CALLBACK-COLD-BACKEND";
    };

    static constexpr Decision lowering(Backend backend, std::uint32_t version = 1u) noexcept
    {
        if (version == 7u) {
            switch (backend) {
            case Backend::Null:
                return {false, "RHI-BINDLESS-PRODUCED-GEOMETRY-COLD-NULL-VALIDATOR"};
            case Backend::Vulkan:
            case Backend::Metal:
            case Backend::WebGPU:
                return {false, "RHI-BINDLESS-PRODUCED-GEOMETRY-NATIVE-ADMISSION-REQUIRED"};
            case Backend::WebGL:
                return {false, "RHI-WEBGL1-BINDLESS-PRODUCED-GEOMETRY-UNSUPPORTED"};
            }
            return {};
        }
        if (version != 1u && version != 2u && version != 3u && version != 4u && version != 5u
            && version != 6u)
            return {false, "RHI-ORIGINAL-ROOT-CALLBACK-COLD-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {false, "RHI-ORIGINAL-ROOT-CALLBACK-COLD-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {false, "RHI-ORIGINAL-ROOT-CALLBACK-COLD-VULKAN-PROFILE"};
        case Backend::Metal:
            return {false, "RHI-ORIGINAL-ROOT-CALLBACK-COLD-METAL-UNAVAILABLE"};
        case Backend::WebGPU:
            return {true, nullptr};
        case Backend::WebGL:
            return {false, "RHI-WEBGL1-BINDLESS-ROOT-CALLBACK-COLD-UNSUPPORTED"};
        }
        return {};
    }
};

class OriginalBindlessRootColdCommandLowering final {
public:
    struct Decision {
        bool preparationAvailable = false, executionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-ROOT-COLD-COMMAND-BACKEND";
    };

    static constexpr Decision lowering(Backend backend, std::uint32_t version = 1u) noexcept
    {
        if (version != 1u && version != 2u)
            return {false, false, "RHI-ORIGINAL-ROOT-COLD-COMMAND-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {false, false, "RHI-ORIGINAL-ROOT-COLD-COMMAND-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {false, false, "RHI-ORIGINAL-ROOT-COLD-COMMAND-VULKAN-PROFILE"};
        case Backend::Metal:
            return {false, false, "RHI-ORIGINAL-ROOT-COLD-COMMAND-METAL-UNAVAILABLE"};
        case Backend::WebGPU:
            return {true, false, nullptr};
        case Backend::WebGL:
            break;
        }
        return {};
    }
};

class OriginalBindlessRootCallbackCustodyLowering final {
public:
    struct Decision {
        bool preparationAvailable = false, executionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-ROOT-CALLBACK-CUSTODY-BACKEND";
    };

    static constexpr Decision lowering(Backend backend, std::uint32_t version = 1u) noexcept
    {
        if (version != 1u)
            return {false, false, "RHI-ORIGINAL-ROOT-CALLBACK-CUSTODY-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {false, false, "RHI-ORIGINAL-ROOT-CALLBACK-CUSTODY-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {false, false, "RHI-ORIGINAL-ROOT-CALLBACK-CUSTODY-VULKAN-PROFILE"};
        case Backend::Metal:
            return {false, false, "RHI-ORIGINAL-ROOT-CALLBACK-CUSTODY-METAL-UNAVAILABLE"};
        case Backend::WebGPU:
            return {true, false, nullptr};
        case Backend::WebGL:
            break;
        }
        return {};
    }
};

class OriginalBindlessRootIssuerLowering final {
public:
    struct Decision {
        bool preparationAvailable = false, executionAvailable = false, completionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-ROOT-ISSUER-BACKEND";
    };

    static constexpr Decision lowering(Backend backend, std::uint32_t version = 1u) noexcept
    {
        if (version != 1u)
            return {false, false, false, "RHI-ORIGINAL-ROOT-ISSUER-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {false, false, false, "RHI-ORIGINAL-ROOT-ISSUER-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {false, false, false, "RHI-ORIGINAL-ROOT-ISSUER-VULKAN-PROFILE"};
        case Backend::Metal:
            return {false, false, false, "RHI-ORIGINAL-ROOT-ISSUER-METAL-UNAVAILABLE"};
        case Backend::WebGPU:
            return {true, false, false, nullptr};
        case Backend::WebGL:
            break;
        }
        return {};
    }
};

class OriginalCommandNativeLifecycleLowering final {
public:
    enum class Disposition { ValidatorOnly, DiagnosticFailure, Prototype };

    struct DeviceIdleDecision {
        Disposition disposition = Disposition::DiagnosticFailure;
        bool completionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-IDLE-COMPLETION-BACKEND-UNKNOWN";
    };

    // Only the listed callback-free domains consume device-wide completion.
    // Successful native idle proves the exact retained runtime's queue work;
    // it grants neither a journal revival nor completion on another device.
    static constexpr DeviceIdleDecision deviceIdleLowering(
        Backend backend, std::uint32_t lifecycleVersion) noexcept
    {
        if (lifecycleVersion != 6u && lifecycleVersion != 7u && lifecycleVersion != 8u
            && lifecycleVersion != 9u && lifecycleVersion != 14u && lifecycleVersion != 15u
            && lifecycleVersion != 17u && lifecycleVersion != 19u && lifecycleVersion != 21u)
            return {Disposition::DiagnosticFailure, false, "RHI-ORIGINAL-IDLE-COMPLETION-PROFILE"};
        switch (backend) {
        case Backend::Null:
            return {Disposition::ValidatorOnly,
                false,
                "RHI-ORIGINAL-IDLE-COMPLETION-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {Disposition::Prototype, true, nullptr};
        case Backend::Metal:
            return {Disposition::DiagnosticFailure,
                false,
                "RHI-ORIGINAL-IDLE-COMPLETION-METAL-MISSING"};
        case Backend::WebGPU:
            return {Disposition::DiagnosticFailure,
                false,
                "RHI-ORIGINAL-IDLE-COMPLETION-WEBGPU-MISSING"};
        case Backend::WebGL:
            break;
        }
        return {};
    }

    struct Decision {
        std::uint32_t contractVersion = 1u;
        Disposition disposition = Disposition::DiagnosticFailure;
        bool sealedIssuerAvailable = false, executionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-NATIVE-LIFECYCLE-BACKEND-UNKNOWN";
    };

    // Native resource closure alone grants none of these lifecycle capabilities.
    static constexpr Decision lowering(Backend backend, std::uint32_t version = 1u) noexcept
    {
        if (version == 21u || version == 22u) {
            switch (backend) {
            case Backend::Null:
                return {version,
                    Disposition::ValidatorOnly,
                    false,
                    false,
                    "RHI-BINDLESS-PRODUCED-GEOMETRY-LIFECYCLE-NULL-VALIDATOR"};
            case Backend::Vulkan:
            case Backend::Metal:
            case Backend::WebGPU:
                return {version,
                    Disposition::DiagnosticFailure,
                    false,
                    false,
                    "RHI-BINDLESS-PRODUCED-GEOMETRY-NATIVE-ADMISSION-REQUIRED"};
            case Backend::WebGL:
                return {version,
                    Disposition::DiagnosticFailure,
                    false,
                    false,
                    "RHI-WEBGL1-BINDLESS-PRODUCED-GEOMETRY-UNSUPPORTED"};
            }
            return {};
        }
        if (version == 19u) {
            switch (backend) {
            case Backend::Null:
                return {version,
                    Disposition::ValidatorOnly,
                    false,
                    false,
                    "RHI-GRAPHICS-DEPTH-LIFECYCLE-NULL-VALIDATOR"};
            case Backend::Vulkan:
                return {version, Disposition::Prototype, true, true, nullptr};
            case Backend::Metal:
                return {version, Disposition::Prototype, true, true, nullptr};
            case Backend::WebGPU:
                return {version,
                    Disposition::DiagnosticFailure,
                    false,
                    false,
                    "RHI-GRAPHICS-DEPTH-LIFECYCLE-WEBGPU-UNAVAILABLE"};
            case Backend::WebGL:
                return {version,
                    Disposition::DiagnosticFailure,
                    false,
                    false,
                    "RHI-GRAPHICS-DEPTH-LIFECYCLE-WEBGL-UNAVAILABLE"};
            }
            return {};
        }
        if (version == 17u || version == 18u) {
            switch (backend) {
            case Backend::Null:
                return {version,
                    Disposition::ValidatorOnly,
                    false,
                    false,
                    "RHI-MIXED-ROOT-NULL-VALIDATOR-ONLY"};
            case Backend::Vulkan:
            case Backend::Metal:
                if (version == 17u)
                    return {version, Disposition::Prototype, true, true, nullptr};
                break;
            case Backend::WebGPU:
                if (version == 18u)
                    return {version, Disposition::Prototype, true, true, nullptr};
                break;
            case Backend::WebGL:
                break;
            }
            return {version,
                Disposition::DiagnosticFailure,
                false,
                false,
                "RHI-MIXED-ROOT-LIFECYCLE-PROFILE"};
        }
        if (version == 16u) {
            switch (backend) {
            case Backend::Null:
                return {version,
                    Disposition::ValidatorOnly,
                    false,
                    false,
                    "RHI-STORAGE-ROOT-NULL-VALIDATOR-ONLY"};
            case Backend::Vulkan:
                return {version,
                    Disposition::DiagnosticFailure,
                    false,
                    false,
                    "RHI-STORAGE-ROOT-VULKAN-CALLBACK-PROFILE"};
            case Backend::Metal:
                return {version,
                    Disposition::DiagnosticFailure,
                    false,
                    false,
                    "RHI-STORAGE-ROOT-METAL-CALLBACK-PROFILE"};
            case Backend::WebGPU:
                return {version, Disposition::Prototype, true, true, nullptr};
            case Backend::WebGL:
                break;
            }
            return {};
        }
        if (version == 15u) {
            switch (backend) {
            case Backend::Null:
                return {version,
                    Disposition::ValidatorOnly,
                    false,
                    false,
                    "RHI-STORAGE-ROOT-NULL-VALIDATOR-ONLY"};
            case Backend::Vulkan:
                return {version, Disposition::Prototype, true, true, nullptr};
            case Backend::Metal:
                return {version, Disposition::Prototype, true, true, nullptr};
            case Backend::WebGPU:
                return {version,
                    Disposition::DiagnosticFailure,
                    false,
                    false,
                    "RHI-STORAGE-ROOT-WEBGPU-EXECUTION-UNAVAILABLE"};
            case Backend::WebGL:
                break;
            }
            return {};
        }
        if (version == 14u) {
            // Native control4 depth reads: Content12, Pending2, genuine fence
            // and Completion8. No callback or whole-color completion authority.
            switch (backend) {
            case Backend::Null:
                return {version,
                    Disposition::ValidatorOnly,
                    false,
                    false,
                    "RHI-ORIGINAL-ROOT-NATIVE-DEPTH-NULL-VALIDATOR-ONLY"};
            case Backend::Vulkan:
                return {version, Disposition::Prototype, true, true, nullptr};
            case Backend::Metal:
                return {version, Disposition::Prototype, true, true, nullptr};
            case Backend::WebGPU:
                return {version,
                    Disposition::DiagnosticFailure,
                    false,
                    false,
                    "RHI-ORIGINAL-ROOT-NATIVE-DEPTH-WEBGPU-PROFILE"};
            case Backend::WebGL:
                break;
            }
            return {};
        }
        if (version == 13u) {
            // Completed per-mip depth reads use Content11 and the original
            // queue/scope/map drain. Cold preparation alone grants no issuer.
            switch (backend) {
            case Backend::Null:
                return {version,
                    Disposition::ValidatorOnly,
                    false,
                    false,
                    "RHI-ORIGINAL-ROOT-DEPTH-NULL-VALIDATOR-ONLY"};
            case Backend::Vulkan:
                return {version,
                    Disposition::DiagnosticFailure,
                    false,
                    false,
                    "RHI-ORIGINAL-ROOT-DEPTH-VULKAN-PROFILE"};
            case Backend::Metal:
                return {version,
                    Disposition::DiagnosticFailure,
                    false,
                    false,
                    "RHI-ORIGINAL-ROOT-DEPTH-METAL-ISSUER-MISSING"};
            case Backend::WebGPU:
                return {version, Disposition::Prototype, true, true, nullptr};
            case Backend::WebGL:
                break;
            }
            return {};
        }
        if (version == 12u) {
            // Fragment buffers plus original Content9 attachments; same
            // queue/scope/map receipts and journal as RootCallback11.
            switch (backend) {
            case Backend::Null:
                return {version,
                    Disposition::ValidatorOnly,
                    false,
                    false,
                    "RHI-ORIGINAL-ROOT-GRAPHICS-NULL-VALIDATOR-ONLY"};
            case Backend::Vulkan:
                return {version,
                    Disposition::DiagnosticFailure,
                    false,
                    false,
                    "RHI-ORIGINAL-ROOT-GRAPHICS-VULKAN-PROFILE"};
            case Backend::Metal:
                return {version,
                    Disposition::DiagnosticFailure,
                    false,
                    false,
                    "RHI-ORIGINAL-ROOT-GRAPHICS-METAL-ISSUER-MISSING"};
            case Backend::WebGPU:
                return {version, Disposition::Prototype, true, true, nullptr};
            case Backend::WebGL:
                break;
            }
            return {};
        }
        if (version == 11u) {
            // Original buffer Compute Root, authentic queue/scope/map receipts,
            // original fault custody, same journal and completion publication.
            switch (backend) {
            case Backend::Null:
                return {version,
                    Disposition::ValidatorOnly,
                    false,
                    false,
                    "RHI-ORIGINAL-ROOT-LIVE-NULL-VALIDATOR-ONLY"};
            case Backend::Vulkan:
                return {version,
                    Disposition::DiagnosticFailure,
                    false,
                    false,
                    "RHI-ORIGINAL-ROOT-LIVE-VULKAN-PROFILE"};
            case Backend::Metal:
                return {version,
                    Disposition::DiagnosticFailure,
                    false,
                    false,
                    "RHI-ORIGINAL-ROOT-LIVE-METAL-ISSUER-MISSING"};
            case Backend::WebGPU:
                return {version, Disposition::Prototype, true, true, nullptr};
            case Backend::WebGL:
                break;
            }
            return {};
        }
        if (version == 10u) {
            // RootCallback cold seals and capacity are inspectable; only a
            // matching canonical native issuer may grant execution later.
            switch (backend) {
            case Backend::Null:
                return {version,
                    Disposition::ValidatorOnly,
                    false,
                    false,
                    "RHI-ORIGINAL-ROOT-CALLBACK-NULL-VALIDATOR-ONLY"};
            case Backend::Vulkan:
                return {version,
                    Disposition::DiagnosticFailure,
                    false,
                    false,
                    "RHI-ORIGINAL-ROOT-CALLBACK-VULKAN-PROFILE"};
            case Backend::Metal:
                return {version,
                    Disposition::DiagnosticFailure,
                    false,
                    false,
                    "RHI-ORIGINAL-ROOT-CALLBACK-METAL-ISSUER-MISSING"};
            case Backend::WebGPU:
                return {version,
                    Disposition::DiagnosticFailure,
                    false,
                    false,
                    "RHI-ORIGINAL-ROOT-CALLBACK-WEBGPU-ISSUER-MISSING"};
            case Backend::WebGL:
                break;
            }
            return {};
        }
        if (version == 9u) {
            // Same original Root/9 journal, cold prepared native attachments,
            // actual Graphics draw, completion-gated fault and cell retirement.
            switch (backend) {
            case Backend::Null:
                return {version,
                    Disposition::ValidatorOnly,
                    false,
                    false,
                    "RHI-BINDLESS-GRAPHICS-FENCE-NULL-VALIDATOR-ONLY"};
            case Backend::Vulkan:
                return {version, Disposition::Prototype, true, true, nullptr};
            case Backend::Metal:
                return {version, Disposition::Prototype, true, true, nullptr};
            case Backend::WebGPU:
                return {version,
                    Disposition::DiagnosticFailure,
                    false,
                    false,
                    "RHI-BINDLESS-GRAPHICS-FENCE-WEBGPU-ISSUER-MISSING"};
            case Backend::WebGL:
                break;
            }
            return {};
        }
        if (version == 8u) {
            // Completed read-only original texture Root, same /4 program and
            // journal, genuine whole-image barriers and actual queue/fence.
            // This scoped opt-in does not publish general scene capabilities.
            switch (backend) {
            case Backend::Null:
                return {version,
                    Disposition::ValidatorOnly,
                    false,
                    false,
                    "RHI-BINDLESS-TEXTURE-FENCE-NULL-VALIDATOR-ONLY"};
            case Backend::Vulkan:
                return {version, Disposition::Prototype, true, true, nullptr};
            case Backend::Metal:
                return {version, Disposition::Prototype, true, true, nullptr};
            case Backend::WebGPU:
                return {version,
                    Disposition::DiagnosticFailure,
                    false,
                    false,
                    "RHI-BINDLESS-TEXTURE-FENCE-WEBGPU-ISSUER-MISSING"};
            case Backend::WebGL:
                break;
            }
            return {};
        }
        if (version == 7u) {
            // Independent ContentPlan /7 whole-upload stream, immutable genuine
            // staging, exact original graph/seal/journal and real driver fence.
            switch (backend) {
            case Backend::Null:
                return {version,
                    Disposition::ValidatorOnly,
                    false,
                    false,
                    "RHI-ORIGINAL-UPLOAD-FENCE-NULL-VALIDATOR-ONLY"};
            case Backend::Vulkan:
                return {version, Disposition::Prototype, true, true, nullptr};
            // Tracked immutable staging, ordered native blit encoder and the
            // existing native completed handler consume the same /7 journal.
            // Device-idle remains unavailable as a substitute for this handler.
            case Backend::Metal:
                return {version, Disposition::Prototype, true, true, nullptr};
            case Backend::WebGPU:
                return {version,
                    Disposition::DiagnosticFailure,
                    false,
                    false,
                    "RHI-ORIGINAL-UPLOAD-FENCE-WEBGPU-ISSUER-MISSING"};
            case Backend::WebGL:
                break;
            }
            return {};
        }
        if (version == 6u) {
            // Explicit RootFaultFence/6 opt-in: buffer-only Compute stream,
            // original /4 journal, authentic payload and real fence/map receipt.
            switch (backend) {
            case Backend::Null:
                return {version,
                    Disposition::ValidatorOnly,
                    false,
                    false,
                    "RHI-ORIGINAL-ROOT-FENCE-NULL-VALIDATOR-ONLY"};
            case Backend::Vulkan:
                return {version, Disposition::Prototype, true, true, nullptr};
            case Backend::Metal:
                // Same buffer Root/journal, exact native command completion.
                // Metal's backend-owned completed handler replaces fence polling;
                // no caller callback/map or additional completion receipt exists.
                return {version, Disposition::Prototype, true, true, nullptr};
            case Backend::WebGPU:
                return {version,
                    Disposition::DiagnosticFailure,
                    false,
                    false,
                    "RHI-ORIGINAL-ROOT-FENCE-WEBGPU-ISSUER-MISSING"};
            case Backend::WebGL:
                break;
            }
            return {};
        }
        // Graphics5 is an independent CPU seal schema. No native issuer is
        // authorized until the matching Graphics cold/command chain is proven.
        if (version == 5u) {
            switch (backend) {
            case Backend::Null:
                return {version,
                    Disposition::ValidatorOnly,
                    false,
                    false,
                    "RHI-ORIGINAL-GRAPHICS-LIFECYCLE-NULL-VALIDATOR-ONLY"};
            case Backend::Vulkan:
                return {version,
                    Disposition::DiagnosticFailure,
                    false,
                    false,
                    "RHI-ORIGINAL-GRAPHICS-LIFECYCLE-VULKAN-ISSUER-MISSING"};
            case Backend::Metal:
                return {version,
                    Disposition::DiagnosticFailure,
                    false,
                    false,
                    "RHI-ORIGINAL-GRAPHICS-LIFECYCLE-METAL-ISSUER-MISSING"};
            case Backend::WebGPU:
                return {version,
                    Disposition::DiagnosticFailure,
                    false,
                    false,
                    "RHI-ORIGINAL-GRAPHICS-LIFECYCLE-WEBGPU-ISSUER-MISSING"};
            case Backend::WebGL:
                break;
            }
            return {};
        }
        if (version != 1u && version != 2u && version != 3u && version != 4u)
            return {version,
                Disposition::DiagnosticFailure,
                false,
                false,
                "RHI-ORIGINAL-NATIVE-LIFECYCLE-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {version,
                Disposition::ValidatorOnly,
                false,
                false,
                "RHI-ORIGINAL-NATIVE-LIFECYCLE-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {version,
                Disposition::DiagnosticFailure,
                false,
                false,
                "RHI-ORIGINAL-NATIVE-LIFECYCLE-VULKAN-UNIMPLEMENTED"};
        case Backend::Metal:
            return {version,
                Disposition::DiagnosticFailure,
                false,
                false,
                "RHI-ORIGINAL-NATIVE-LIFECYCLE-METAL-UNIMPLEMENTED"};
        case Backend::WebGPU:
            return {version,
                Disposition::DiagnosticFailure,
                false,
                false,
                "RHI-ORIGINAL-NATIVE-LIFECYCLE-WEBGPU-UNIMPLEMENTED"};
        case Backend::WebGL:
            break;
        }
        return {};
    }
};

// endregion original_command_native_lifecycle

// region original_command_native_cold

class OriginalCommandNativeColdLowering final {
public:
    // Read-only planning is portable. Actual issuers and native custody remain
    // unavailable until the same version is consumed by private native payloads.
    static constexpr auto lowering(Backend backend) noexcept
    {
        return arp::rhi::semantics::OriginalCommandNativeLifecycleLowering::lowering(backend);
    }

    struct UploadDecision {
        arp::rhi::semantics::OriginalCommandNativeClosureLowering::Disposition disposition = arp::
            rhi::semantics::OriginalCommandNativeClosureLowering::Disposition::DiagnosticFailure;
        bool coldJoinAvailable = false, recordingAvailable = false, executionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-NATIVE-COLD-UPLOAD-BACKEND-UNKNOWN";
    };

    using TypedDecision = UploadDecision;

    static constexpr UploadDecision generalOutputLowering(
        Backend backend, std::uint32_t version) noexcept
    {
        using D = OriginalCommandNativeClosureLowering::Disposition;
        if (version != 1u && version != 2u && version != 3u)
            return {D::DiagnosticFailure, false, false, false, "RHI-GENERAL-OUTPUT-COLD-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {D::ValidatorOnly,
                false,
                false,
                false,
                "RHI-GENERAL-OUTPUT-COLD-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {D::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-GENERAL-OUTPUT-COLD-VULKAN-MISSING"};
        case Backend::Metal:
            return {
                D::DiagnosticFailure, false, false, false, "RHI-GENERAL-OUTPUT-COLD-METAL-MISSING"};
        case Backend::WebGPU:
            return {D::Prototype,
                true,
                false,
                false,
                "RHI-GENERAL-OUTPUT-COLD-WEBGPU-EXECUTION-MISSING"};
        case Backend::WebGL:
            break;
        }
        return {};
    }

    static constexpr TypedDecision typedLowering(Backend backend) noexcept
    {
        using D = arp::rhi::semantics::OriginalCommandNativeClosureLowering::Disposition;
        switch (backend) {
        case Backend::Null:
            return {D::ValidatorOnly,
                false,
                false,
                false,
                "RHI-ORIGINAL-NATIVE-COLD-TYPED-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {D::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-NATIVE-COLD-TYPED-VULKAN-LOWERING-MISSING"};
        case Backend::Metal:
            return {D::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-NATIVE-COLD-TYPED-METAL-LOWERING-MISSING"};
        case Backend::WebGPU:
            return {D::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-NATIVE-COLD-TYPED-WEBGPU-FROZEN-LOWERING-MISSING"};
        case Backend::WebGL:
            break;
        }
        return {};
    }

    static constexpr UploadDecision uploadLowering(Backend backend) noexcept
    {
        using D = arp::rhi::semantics::OriginalCommandNativeClosureLowering::Disposition;
        switch (backend) {
        case Backend::Null:
            return {D::ValidatorOnly,
                false,
                false,
                false,
                "RHI-ORIGINAL-NATIVE-COLD-UPLOAD-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {D::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-NATIVE-COLD-UPLOAD-VULKAN-PAYLOAD-MISSING"};
        case Backend::Metal:
            return {D::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-NATIVE-COLD-UPLOAD-METAL-PAYLOAD-MISSING"};
        case Backend::WebGPU:
            return {
                D::Prototype, true, false, false, "RHI-ORIGINAL-NATIVE-COLD-UPLOAD-COLD-JOIN-ONLY"};
        case Backend::WebGL:
            break;
        }
        return {};
    }
};

// endregion original_command_native_cold

// GeneralOutput/1 pairs only the original standalone Cold5/Join4 profile.
// Private Prototype execution reuses the original canonical copy/map issuers;
// every retained validation authenticates the exact Pending state handoff.
class OriginalGeneralOutputExecutionLowering final {
public:
    struct Decision {
        OriginalCommandNativeClosureLowering::Disposition disposition =
            OriginalCommandNativeClosureLowering::Disposition::DiagnosticFailure;
        bool profilePairingAvailable = false, recordingAvailable = false,
             submissionAvailable = false, nativeSyncAvailable = false;
        const char* diagnostic = "RHI-GENERAL-OUTPUT-EXECUTION-BACKEND-UNKNOWN";
    };

    static constexpr Decision lowering(Backend backend, std::uint32_t version) noexcept
    {
        using D = OriginalCommandNativeClosureLowering::Disposition;
        if (version == 3u) {
            using D = OriginalCommandNativeClosureLowering::Disposition;
            switch (backend) {
            case Backend::Null:
                return {D::ValidatorOnly,
                    false,
                    false,
                    false,
                    false,
                    "RHI-GRAPHICS-OUTPUT-EXECUTION-NULL-VALIDATOR-ONLY"};
            case Backend::Vulkan:
                return {D::DiagnosticFailure,
                    false,
                    false,
                    false,
                    false,
                    "RHI-GRAPHICS-OUTPUT-EXECUTION-VULKAN-JOIN-MISSING"};
            case Backend::Metal:
                return {D::DiagnosticFailure,
                    false,
                    false,
                    false,
                    false,
                    "RHI-GRAPHICS-OUTPUT-EXECUTION-METAL-JOIN-MISSING"};
            case Backend::WebGPU:
                return {D::Prototype,
                    true,
                    true,
                    true,
                    false,
                    "RHI-GRAPHICS-OUTPUT-EXECUTION-WEBGPU-PRIVATE-PROTOTYPE"};
            case Backend::WebGL:
                break;
            }
            return {};
        }
        if (version != 1u && version != 2u)
            return {D::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-GENERAL-OUTPUT-EXECUTION-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {D::ValidatorOnly,
                false,
                false,
                false,
                false,
                "RHI-GENERAL-OUTPUT-EXECUTION-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {D::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-GENERAL-OUTPUT-EXECUTION-VULKAN-PAYLOAD-MISSING"};
        case Backend::Metal:
            return {D::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-GENERAL-OUTPUT-EXECUTION-METAL-PAYLOAD-MISSING"};
        case Backend::WebGPU:
            return {D::Prototype,
                true,
                true,
                true,
                false,
                "RHI-GENERAL-OUTPUT-EXECUTION-WEBGPU-PRIVATE-PROTOTYPE"};
        case Backend::WebGL:
            break;
        }
        return {};
    }
};

// region original_command_native_cold_command

class OriginalCommandNativeColdCommandLowering final {
public:
    struct Decision {
        arp::rhi::semantics::OriginalCommandNativeClosureLowering::Disposition disposition = arp::
            rhi::semantics::OriginalCommandNativeClosureLowering::Disposition::DiagnosticFailure;
        bool nativeConnectionAvailable = false, recordingAvailable = false,
             executionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-NATIVE-COLD-COMMAND-BACKEND-UNKNOWN";
    };

    static constexpr Decision lowering(Backend backend) noexcept
    {
        using D = arp::rhi::semantics::OriginalCommandNativeClosureLowering::Disposition;
        switch (backend) {
        case Backend::Null:
            return {D::ValidatorOnly,
                false,
                false,
                false,
                "RHI-ORIGINAL-NATIVE-COLD-COMMAND-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {D::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-NATIVE-COLD-COMMAND-VULKAN-UNIMPLEMENTED"};
        case Backend::Metal:
            return {D::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-NATIVE-COLD-COMMAND-METAL-UNIMPLEMENTED"};
        case Backend::WebGPU:
            return {D::Prototype,
                true,
                false,
                false,
                "RHI-ORIGINAL-NATIVE-COLD-COMMAND-WEBGPU-RECORDING-UNPROVEN"};
        case Backend::WebGL:
            break;
        }
        return {};
    }
};

// endregion original_command_native_cold_command

// region original_command_callback_custody

class OriginalCommandCallbackCustodyLowering final {
public:
    struct Decision {
        arp::rhi::semantics::OriginalCommandNativeClosureLowering::Disposition disposition = arp::
            rhi::semantics::OriginalCommandNativeClosureLowering::Disposition::DiagnosticFailure;
        bool nativeLossObservationAvailable = false, recordingAvailable = false,
             executionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-CALLBACK-CUSTODY-BACKEND-UNKNOWN";
    };

    static constexpr Decision lowering(Backend backend) noexcept
    {
        using D = arp::rhi::semantics::OriginalCommandNativeClosureLowering::Disposition;
        switch (backend) {
        case Backend::Null:
            return {D::ValidatorOnly,
                false,
                false,
                false,
                "RHI-ORIGINAL-CALLBACK-CUSTODY-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {D::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-CALLBACK-CUSTODY-VULKAN-UNIMPLEMENTED"};
        case Backend::Metal:
            return {D::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-CALLBACK-CUSTODY-METAL-UNIMPLEMENTED"};
        case Backend::WebGPU:
            return {D::Prototype,
                true,
                false,
                false,
                "RHI-ORIGINAL-CALLBACK-CUSTODY-WEBGPU-RECORDING-UNPROVEN"};
        case Backend::WebGL:
            break;
        }
        return {};
    }
};

// endregion original_command_callback_custody

// region original_command_issuer_preparation

class OriginalCommandIssuerPreparationLowering final {
public:
    struct Decision {
        arp::rhi::semantics::OriginalCommandNativeClosureLowering::Disposition disposition = arp::
            rhi::semantics::OriginalCommandNativeClosureLowering::Disposition::DiagnosticFailure;
        bool issuerStorageAvailable = false, recordingAvailable = false, executionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-ISSUER-PREPARATION-BACKEND-UNKNOWN";
    };

    static constexpr Decision lowering(Backend backend) noexcept
    {
        using D = arp::rhi::semantics::OriginalCommandNativeClosureLowering::Disposition;
        switch (backend) {
        case Backend::Null:
            return {D::ValidatorOnly,
                false,
                false,
                false,
                "RHI-ORIGINAL-ISSUER-PREPARATION-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {D::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-ISSUER-PREPARATION-VULKAN-UNIMPLEMENTED"};
        case Backend::Metal:
            return {D::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-ISSUER-PREPARATION-METAL-UNIMPLEMENTED"};
        case Backend::WebGPU:
            return {D::Prototype,
                true,
                false,
                false,
                "RHI-ORIGINAL-ISSUER-PREPARATION-WEBGPU-RECORDING-UNPROVEN"};
        case Backend::WebGL:
            break;
        }
        return {};
    }
};

// endregion original_command_issuer_preparation

// region original_command_queue_submission

class OriginalCommandQueueSubmissionLowering final {
public:
    struct Decision {
        arp::rhi::semantics::OriginalCommandNativeClosureLowering::Disposition disposition = arp::
            rhi::semantics::OriginalCommandNativeClosureLowering::Disposition::DiagnosticFailure;
        bool submissionAvailable = false, completionCommitAvailable = false,
             nativeSyncAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-QUEUE-SUBMISSION-BACKEND-UNKNOWN";
    };

    // Selected WebGPU Prototype consumes explicit intent and exact native
    // receipt payload. Other issuers and general NativeSync remain closed.
    static constexpr Decision lowering(Backend backend) noexcept
    {
        using D = arp::rhi::semantics::OriginalCommandNativeClosureLowering::Disposition;
        switch (backend) {
        case Backend::Null:
            return {D::ValidatorOnly,
                false,
                false,
                false,
                "RHI-ORIGINAL-QUEUE-SUBMISSION-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {D::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-QUEUE-SUBMISSION-VULKAN-UNIMPLEMENTED"};
        case Backend::Metal:
            return {D::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-QUEUE-SUBMISSION-METAL-UNIMPLEMENTED"};
        case Backend::WebGPU:
            return {
                D::Prototype, true, true, false, "RHI-ORIGINAL-QUEUE-SUBMISSION-WEBGPU-PROTOTYPE"};
        case Backend::WebGL:
            break;
        }
        return {};
    }
};

// endregion original_command_queue_submission

// region original_command_scoped_recording

class OriginalCommandScopedRecordingLowering final {
public:
    struct Decision {
        arp::rhi::semantics::OriginalCommandNativeClosureLowering::Disposition disposition = arp::
            rhi::semantics::OriginalCommandNativeClosureLowering::Disposition::DiagnosticFailure;
        bool recordingAvailable = false, executionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-SCOPED-RECORDING-BACKEND-UNKNOWN";
    };

    static constexpr Decision lowering(Backend backend) noexcept
    {
        using D = arp::rhi::semantics::OriginalCommandNativeClosureLowering::Disposition;
        switch (backend) {
        case Backend::Null:
            return {D::ValidatorOnly,
                false,
                false,
                "RHI-ORIGINAL-SCOPED-RECORDING-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {D::DiagnosticFailure,
                false,
                false,
                "RHI-ORIGINAL-SCOPED-RECORDING-VULKAN-UNIMPLEMENTED"};
        case Backend::Metal:
            return {D::DiagnosticFailure,
                false,
                false,
                "RHI-ORIGINAL-SCOPED-RECORDING-METAL-UNIMPLEMENTED"};
        case Backend::WebGPU:
            return {D::Prototype,
                true,
                false,
                "RHI-ORIGINAL-SCOPED-RECORDING-WEBGPU-SUBMISSION-UNPROVEN"};
        case Backend::WebGL:
            break;
        }
        return {};
    }
};

// endregion original_command_scoped_recording
// endregion Command native lifecycle lowering

// region Command readback lowering

// region original_command_readback_result

class OriginalCommandReadbackResultLowering final {
public:
    enum class Disposition { ValidatorOnly, DiagnosticFailure };

    struct Decision {
        Disposition disposition = Disposition::DiagnosticFailure;
        bool executionAvailable = false, mapAvailable = false, publicationAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-READBACK-RESULT-BACKEND-UNKNOWN";
    };

    static constexpr Decision lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {Disposition::ValidatorOnly,
                false,
                false,
                false,
                "RHI-ORIGINAL-READBACK-RESULT-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {Disposition::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-READBACK-RESULT-VULKAN-PAYLOAD-MISSING"};
        case Backend::Metal:
            return {Disposition::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-READBACK-RESULT-METAL-PAYLOAD-MISSING"};
        case Backend::WebGPU:
            return {Disposition::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-READBACK-RESULT-WEBGPU-PAYLOAD-MISSING"};
        case Backend::WebGL:
            break;
        }
        return {};
    }
};

// endregion original_command_readback_result

// region original_command_mixed_readback_result
class OriginalCommandMixedReadbackResultLowering final {
public:
    struct Decision {
        bool cpuMetadataImplemented = false, validatorOnly = false;
        bool recordingAvailable = false, mapAvailable = false, publicationAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-MIXED-READBACK-RESULT-BACKEND-UNKNOWN";
    };

    static constexpr Decision lowering(Backend backend, std::uint32_t version) noexcept
    {
        if (version != 2u)
            return {
                false, false, false, false, false, "RHI-ORIGINAL-MIXED-READBACK-RESULT-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {true,
                true,
                false,
                false,
                false,
                "RHI-ORIGINAL-MIXED-READBACK-RESULT-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {true,
                false,
                false,
                false,
                false,
                "RHI-ORIGINAL-MIXED-READBACK-RESULT-VULKAN-EXECUTION-UNPROVEN"};
        case Backend::Metal:
            return {true,
                false,
                false,
                false,
                false,
                "RHI-ORIGINAL-MIXED-READBACK-RESULT-METAL-EXECUTION-UNPROVEN"};
        case Backend::WebGPU:
            return {true,
                false,
                false,
                false,
                false,
                "RHI-ORIGINAL-MIXED-READBACK-RESULT-WEBGPU-EXECUTION-UNPROVEN"};
        case Backend::WebGL:
            break;
        }
        return {};
    }
};

// endregion original_command_mixed_readback_result

// region original_command_ordered_copy_attachment

class OriginalCommandOrderedCopyAttachmentLowering final {
public:
    struct Decision {
        arp::rhi::semantics::OriginalCommandNativeClosureLowering::Disposition disposition = arp::
            rhi::semantics::OriginalCommandNativeClosureLowering::Disposition::DiagnosticFailure;
        bool nativeProjectionAvailable = false, recordingAvailable = false,
             executionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-ORDERED-COPY-ATTACHMENT-BACKEND-UNKNOWN";
    };

    static constexpr Decision lowering(Backend backend) noexcept
    {
        using D = arp::rhi::semantics::OriginalCommandNativeClosureLowering::Disposition;
        switch (backend) {
        case Backend::Null:
            return {D::ValidatorOnly,
                false,
                false,
                false,
                "RHI-ORIGINAL-ORDERED-COPY-ATTACHMENT-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {D::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-ORDERED-COPY-ATTACHMENT-VULKAN-PAYLOAD-MISSING"};
        case Backend::Metal:
            return {D::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-ORDERED-COPY-ATTACHMENT-METAL-PAYLOAD-MISSING"};
        case Backend::WebGPU:
            return {D::Prototype,
                true,
                false,
                false,
                "RHI-ORIGINAL-ORDERED-COPY-ATTACHMENT-WEBGPU-PRIVATE-PROFILE"};
        case Backend::WebGL:
            break;
        }
        return {};
    }
};

// endregion original_command_ordered_copy_attachment

class OriginalCompletedDepthWriteReturnLowering final {
public:
    struct Decision {
        bool transitionAvailable = false, validatorOnly = false;
        const char* diagnostic = "RHI-DEPTH-WRITE-RETURN-BACKEND";
    };

    static constexpr Decision lowering(Backend backend, std::uint32_t version) noexcept
    {
        if (version != 1u)
            return {false, false, "RHI-DEPTH-WRITE-RETURN-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {false, true, "RHI-DEPTH-WRITE-RETURN-NULL-VALIDATOR"};
        case Backend::Vulkan:
            return {false, false, "RHI-DEPTH-WRITE-RETURN-VULKAN-PROFILE"};
        case Backend::Metal:
            return {false, false, "RHI-DEPTH-WRITE-RETURN-METAL-PROFILE"};
        case Backend::WebGPU:
            // Normative ordered texture usages on the genuine queue; the
            // existing Root still owns all logical state and content commits.
            return {true, false, nullptr};
        case Backend::WebGL:
            return {false, false, "RHI-DEPTH-WRITE-RETURN-WEBGL-UNSUPPORTED"};
        }
        return {};
    }
};

// region original_command_readback_result_storage

class OriginalCommandReadbackResultStorageLowering final {
public:
    struct Decision {
        bool coldStorageAvailable = false, mapAvailable = false, publicationAvailable = false;
        arp::rhi::semantics::OriginalCommandAdmissionLowering::Disposition disposition =
            arp::rhi::semantics::OriginalCommandAdmissionLowering::Disposition::DiagnosticFailure;
        const char* diagnostic = "RHI-ORIGINAL-READBACK-STORAGE-BACKEND-UNKNOWN";
    };

    static constexpr Decision lowering(Backend backend) noexcept
    {
        using D = arp::rhi::semantics::OriginalCommandAdmissionLowering::Disposition;
        switch (backend) {
        case Backend::Null:
            return {true,
                false,
                false,
                D::ValidatorOnly,
                "RHI-ORIGINAL-READBACK-STORAGE-NULL-COLD-ONLY"};
        case Backend::Vulkan:
            return {true,
                false,
                false,
                D::ValidatorOnly,
                "RHI-ORIGINAL-READBACK-STORAGE-VULKAN-COLD-ONLY"};
        case Backend::Metal:
            return {true,
                false,
                false,
                D::ValidatorOnly,
                "RHI-ORIGINAL-READBACK-STORAGE-METAL-COLD-ONLY"};
        case Backend::WebGPU:
            return {true,
                false,
                false,
                D::ValidatorOnly,
                "RHI-ORIGINAL-READBACK-STORAGE-WEBGPU-COLD-ONLY"};
        case Backend::WebGL:
            break;
        }
        return {};
    }

    static constexpr Decision typedLowering(Backend backend, std::uint32_t version) noexcept
    {
        using D = OriginalCommandAdmissionLowering::Disposition;
        if (version != 2u)
            return {false,
                false,
                false,
                D::DiagnosticFailure,
                "RHI-ORIGINAL-TYPED-READBACK-STORAGE-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {true,
                false,
                false,
                D::ValidatorOnly,
                "RHI-ORIGINAL-TYPED-READBACK-STORAGE-NULL-COLD-ONLY"};
        case Backend::Vulkan:
            return {false,
                false,
                false,
                D::DiagnosticFailure,
                "RHI-ORIGINAL-TYPED-READBACK-STORAGE-VULKAN-PAYLOAD-MISSING"};
        case Backend::Metal:
            return {false,
                false,
                false,
                D::DiagnosticFailure,
                "RHI-ORIGINAL-TYPED-READBACK-STORAGE-METAL-PAYLOAD-MISSING"};
        case Backend::WebGPU:
            return {true,
                false,
                false,
                D::ValidatorOnly,
                "RHI-ORIGINAL-TYPED-READBACK-STORAGE-WEBGPU-MAP-CLOSED"};
        case Backend::WebGL:
            break;
        }
        return {};
    }

    static constexpr Decision bufferLowering(Backend backend, std::uint32_t version) noexcept
    {
        using D = OriginalCommandAdmissionLowering::Disposition;
        if (version != 3u)
            return {false,
                false,
                false,
                D::DiagnosticFailure,
                "RHI-ORIGINAL-BUFFER-READBACK-STORAGE-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {false,
                false,
                false,
                D::ValidatorOnly,
                "RHI-ORIGINAL-BUFFER-READBACK-STORAGE-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {false,
                false,
                false,
                D::DiagnosticFailure,
                "RHI-ORIGINAL-BUFFER-READBACK-STORAGE-VULKAN-PROFILE"};
        case Backend::Metal:
            return {false,
                false,
                false,
                D::DiagnosticFailure,
                "RHI-ORIGINAL-BUFFER-READBACK-STORAGE-METAL-UNAVAILABLE"};
        case Backend::WebGPU:
            return {true,
                false,
                false,
                D::ValidatorOnly,
                "RHI-ORIGINAL-BUFFER-READBACK-STORAGE-WEBGPU-MAP-CLOSED"};
        case Backend::WebGL:
            break;
        }
        return {};
    }
};

// endregion original_command_readback_result_storage

// region original_command_readback_map

class OriginalCommandReadbackMapLowering final {
public:
    struct Decision {
        arp::rhi::semantics::OriginalCommandNativeClosureLowering::Disposition disposition = arp::
            rhi::semantics::OriginalCommandNativeClosureLowering::Disposition::DiagnosticFailure;
        bool mapAvailable = false, publicationAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-CANONICAL-MAP-BACKEND-UNKNOWN";
    };

    static constexpr Decision lowering(Backend backend) noexcept
    {
        using D = arp::rhi::semantics::OriginalCommandNativeClosureLowering::Disposition;
        switch (backend) {
        case Backend::Null:
            return {
                D::ValidatorOnly, false, false, "RHI-ORIGINAL-CANONICAL-MAP-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {D::DiagnosticFailure,
                false,
                false,
                "RHI-ORIGINAL-CANONICAL-MAP-VULKAN-PAYLOAD-MISSING"};
        case Backend::Metal:
            return {D::DiagnosticFailure,
                false,
                false,
                "RHI-ORIGINAL-CANONICAL-MAP-METAL-PAYLOAD-MISSING"};
        case Backend::WebGPU:
            return {D::Prototype,
                true,
                false,
                "RHI-ORIGINAL-CANONICAL-MAP-WEBGPU-PROTOTYPE-UNPUBLISHED"};
        case Backend::WebGL:
            break;
        }
        return {};
    }
};

// endregion original_command_readback_map

// region original_command_ordered_readback

class OriginalCommandOrderedReadbackLowering final {
public:
    struct Decision {
        arp::rhi::semantics::OriginalCommandNativeClosureLowering::Disposition disposition = arp::
            rhi::semantics::OriginalCommandNativeClosureLowering::Disposition::DiagnosticFailure;
        bool nativeProjectionAvailable = false, recordingAvailable = false,
             executionAvailable = false, mapAvailable = false, publicationAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-ORDERED-READBACK-BACKEND-UNKNOWN";
    };

    static constexpr Decision lowering(Backend backend) noexcept
    {
        using D = arp::rhi::semantics::OriginalCommandNativeClosureLowering::Disposition;
        switch (backend) {
        case Backend::Null:
            return {D::ValidatorOnly,
                false,
                false,
                false,
                false,
                false,
                "RHI-ORIGINAL-ORDERED-READBACK-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {D::DiagnosticFailure,
                false,
                false,
                false,
                false,
                false,
                "RHI-ORIGINAL-ORDERED-READBACK-VULKAN-PAYLOAD-MISSING"};
        case Backend::Metal:
            return {D::DiagnosticFailure,
                false,
                false,
                false,
                false,
                false,
                "RHI-ORIGINAL-ORDERED-READBACK-METAL-PAYLOAD-MISSING"};
        case Backend::WebGPU:
            return {D::Prototype,
                true,
                true,
                true,
                false,
                false,
                "RHI-ORIGINAL-ORDERED-READBACK-PROTOTYPE-NATIVE-COPIES-ONLY"};
        case Backend::WebGL:
            break;
        }
        return {};
    }

    static const char* transition(const ResourceBarrierDesc& desc,
        bool texture,
        const TextureSubresourceRange& whole = {}) noexcept
    {
        const auto decision = semantics::TransferDependencyLowering::transition(
            Backend::WebGPU, 1u, true, desc, desc.before, texture, whole);
        if (decision.diagnostic)
            return decision.diagnostic;
        return decision.outcome == CapabilityMode::Equivalent
                && decision.operation
                    == semantics::TransferDependencyLowering::Operation::ImplicitOrderedUsage
            ? nullptr
            : "RHI-ORIGINAL-READBACK-DEPENDENCY-UNPROVEN";
    }
};

// endregion original_command_ordered_readback
// endregion Command readback lowering

// region Command shader lowering

// region original_typed_native_cold

class OriginalTypedNativeColdLowering final {
public:
    struct AdmissionDecision {
        arp::rhi::semantics::OriginalCommandNativeClosureLowering::Disposition disposition = arp::
            rhi::semantics::OriginalCommandNativeClosureLowering::Disposition::DiagnosticFailure;
        bool admissionAvailable = false, recordingAvailable = false, executionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-TYPED-COLD-ADMISSION-BACKEND";
    };

    static constexpr AdmissionDecision admissionLowering(
        Backend backend, std::uint32_t version = 1u) noexcept
    {
        using D = arp::rhi::semantics::OriginalCommandNativeClosureLowering::Disposition;
        // Graphics8 binds genuine Payload8/Lifecycle5 CPU custody only. The
        // WebGPU private join never grants recording or execution authority.
        if (version == 8u) {
            switch (backend) {
            case Backend::Null:
                return {D::ValidatorOnly,
                    false,
                    false,
                    false,
                    "RHI-ORIGINAL-GRAPHICS-COLD-NULL-VALIDATOR-ONLY"};
            case Backend::Vulkan:
                return {D::DiagnosticFailure,
                    false,
                    false,
                    false,
                    "RHI-ORIGINAL-GRAPHICS-COLD-VULKAN-JOIN-MISSING"};
            case Backend::Metal:
                return {D::DiagnosticFailure,
                    false,
                    false,
                    false,
                    "RHI-ORIGINAL-GRAPHICS-COLD-METAL-JOIN-MISSING"};
            case Backend::WebGPU:
                return {D::Prototype,
                    true,
                    false,
                    false,
                    "RHI-ORIGINAL-GRAPHICS-COLD-WEBGPU-ADMISSION-ONLY"};
            case Backend::WebGL:
                break;
            }
            return {};
        }
        // Cold4 admits Payload4's original geometry counts and Lifecycle1
        // Cold5 binds exact composed Payload5 with Lifecycle2 and Result2/Map1.
        // ownership only. Native recording and completion stay independent.
        if (version < 1u || version > 7u)
            return {D::DiagnosticFailure, false, false, false, "RHI-ORIGINAL-TYPED-COLD-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {D::ValidatorOnly,
                false,
                false,
                false,
                "RHI-ORIGINAL-TYPED-COLD-ADMISSION-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {D::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-TYPED-COLD-ADMISSION-VULKAN-MISSING"};
        case Backend::Metal:
            return {D::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-TYPED-COLD-ADMISSION-METAL-MISSING"};
        case Backend::WebGPU:
            return {
                D::Prototype, true, false, false, "RHI-WEBGPU-TYPED-COLD-LIVE-EXECUTOR-MISSING"};
        case Backend::WebGL:
            break;
        }
        return {};
    }

    // The complete frozen native count/synchronization mapping is absent. Intent
    // or requirement success cannot manufacture that mapping or execute a call.
    static constexpr auto lowering(Backend backend) noexcept
    {
        return arp::rhi::semantics::OriginalCommandNativeColdLowering::typedLowering(backend);
    }
};

// endregion original_typed_native_cold

// region original_typed_usage_projection

class OriginalTypedUsageProjectionLowering final {
public:
    // Exact CPU projection is supported only for genuine selected WebGPU
    // actors. This never opens the separately frozen native lowering gates.
    static constexpr auto projectionLowering(Backend backend, std::uint32_t version = 1u) noexcept
    {
        if (version == 3u || version == 4u || version == 5u || version == 6u) {
            using D = OriginalRenderDescriptorUsageContract::Decision;
            // Usage3 keeps Consumption2/RenderUsage2; Usage4 consumes the exact
            // State4/Stream4 geometry footprint with Consumption3/RenderUsage3.
            // Neither CPU projection authorizes native transfer or geometry.
            switch (backend) {
            case Backend::Null:
                return D{CapabilityMode::DiagnosticFailure,
                    true,
                    false,
                    false,
                    false,
                    "RHI-ORIGINAL-USAGE-PROJECTION-NULL-VALIDATOR-ONLY"};
            case Backend::Vulkan:
                return D{CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    false,
                    "RHI-ORIGINAL-USAGE-PROJECTION-VULKAN-JOIN-MISSING"};
            case Backend::Metal:
                return D{CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    false,
                    "RHI-ORIGINAL-USAGE-PROJECTION-METAL-JOIN-MISSING"};
            case Backend::WebGPU:
                return D{CapabilityMode::Equivalent, false, true, false, false, nullptr};
            case Backend::WebGL:
                break;
            }
            return D{};
        }
        return OriginalRenderDescriptorUsageContract::lowering(backend, version);
    }

    static constexpr auto nativeLowering(Backend backend) noexcept
    {
        return arp::rhi::semantics::OriginalTypedNativeColdLowering::lowering(backend);
    }
};

// endregion original_typed_usage_projection

// region original_typed_state_bound_usage

class OriginalTypedStateBoundUsageLowering final {
public:
    struct Decision {
        std::uint32_t contractVersion = 1u;
        CapabilityMode outcome = CapabilityMode::DiagnosticFailure;
        bool projectionAvailable = false, executionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-STATE-BOUND-USAGE-BACKEND-UNKNOWN";
    };

    // /1 reuses State2's captured original device/source join for complete
    // compute/render buffer and supported attachment contributions only.
    static constexpr Decision lowering(Backend backend, std::uint32_t version = 1u) noexcept
    {
        if (version != 1u && version != 2u && version != 3u && version != 4u && version != 5u
            && version != 6u)
            return {version,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                "RHI-ORIGINAL-STATE-BOUND-USAGE-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {version,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                "RHI-ORIGINAL-STATE-BOUND-USAGE-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {version,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                "RHI-ORIGINAL-STATE-BOUND-USAGE-VULKAN-JOIN-MISSING"};
        case Backend::Metal:
            return {version,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                "RHI-ORIGINAL-STATE-BOUND-USAGE-METAL-JOIN-MISSING"};
        case Backend::WebGPU:
            return {version, CapabilityMode::Equivalent, true, false, nullptr};
        case Backend::WebGL:
            break;
        }
        return {};
    }
};

// endregion original_typed_state_bound_usage

// region original_typed_dependency_projection

class OriginalTypedDependencyProjectionLowering final {
public:
    enum class Disposition { ValidatorOnly, EquivalentCpuProjection, DiagnosticFailure };

    struct Decision {
        Disposition disposition = Disposition::DiagnosticFailure;
        bool projectionAvailable = false, executionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-DEPENDENCY-PROJECTION-BACKEND-UNKNOWN";
    };

    static constexpr Decision lowering(Backend backend, std::uint32_t version = 1u) noexcept
    {
        if (version != 1u && version != 2u && version != 3u && version != 4u && version != 5u
            && version != 6u)
            return {Disposition::DiagnosticFailure,
                false,
                false,
                "RHI-ORIGINAL-DEPENDENCY-PROJECTION-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {Disposition::ValidatorOnly,
                false,
                false,
                "RHI-ORIGINAL-DEPENDENCY-PROJECTION-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {Disposition::DiagnosticFailure,
                false,
                false,
                "RHI-ORIGINAL-DEPENDENCY-PROJECTION-VULKAN-JOIN-MISSING"};
        case Backend::Metal:
            return {Disposition::DiagnosticFailure,
                false,
                false,
                "RHI-ORIGINAL-DEPENDENCY-PROJECTION-METAL-JOIN-MISSING"};
        case Backend::WebGPU:
            return {Disposition::EquivalentCpuProjection, true, false, nullptr};
        case Backend::WebGL:
            break;
        }
        return {};
    }

    static constexpr auto nativeLowering(Backend backend) noexcept
    {
        return arp::rhi::semantics::OriginalTypedUsageProjectionLowering::nativeLowering(backend);
    }

    // Freeze2 retains Projection2/Closure3; Freeze3 retains Projection3/Closure4;
    // Freeze4 retains Projection4/Closure6's original geometry-content join;
    // Freeze5 retains Projection4/Closure7's exact geometry/readback custody.
    // Freeze6 consumes general Projection5/Closure8 under that independently
    // authored native custody ceiling; CPU graph ceilings are unchanged.
    // Frozen CPU candidates grant neither native visibility nor execution.
    static constexpr Decision freezeLowering(Backend backend, std::uint32_t version) noexcept
    {
        // Freeze8 is the independent Closure10/ContentPlan6 Graphics intent.
        // This is a private CPU candidate retaining original native custody.
        // It grants no executor payload, recording, visibility or completion.
        if (version == 8u) {
            switch (backend) {
            case Backend::Null:
                return {Disposition::ValidatorOnly,
                    false,
                    false,
                    "RHI-ORIGINAL-GRAPHICS-FREEZE-NULL-VALIDATOR-ONLY"};
            case Backend::Vulkan:
                return {Disposition::DiagnosticFailure,
                    false,
                    false,
                    "RHI-ORIGINAL-GRAPHICS-FREEZE-VULKAN-JOIN-MISSING"};
            case Backend::Metal:
                return {Disposition::DiagnosticFailure,
                    false,
                    false,
                    "RHI-ORIGINAL-GRAPHICS-FREEZE-METAL-JOIN-MISSING"};
            case Backend::WebGPU:
                return {Disposition::EquivalentCpuProjection,
                    true,
                    false,
                    "RHI-ORIGINAL-GRAPHICS-FREEZE-WEBGPU-CPU-CANDIDATE-ONLY"};
            case Backend::WebGL:
                break;
            }
            return {Disposition::DiagnosticFailure,
                false,
                false,
                "RHI-ORIGINAL-GRAPHICS-FREEZE-BACKEND-UNKNOWN"};
        }
        // Freeze7 consumes dry Projection6 and finite Closure9 custody only.
        if (version < 1u || version > 7u)
            return {Disposition::DiagnosticFailure,
                false,
                false,
                "RHI-ORIGINAL-DEPENDENCY-FREEZE-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {Disposition::ValidatorOnly,
                false,
                false,
                "RHI-ORIGINAL-DEPENDENCY-FREEZE-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {Disposition::DiagnosticFailure,
                false,
                false,
                "RHI-ORIGINAL-DEPENDENCY-FREEZE-VULKAN-JOIN-MISSING"};
        case Backend::Metal:
            return {Disposition::DiagnosticFailure,
                false,
                false,
                "RHI-ORIGINAL-DEPENDENCY-FREEZE-METAL-JOIN-MISSING"};
        case Backend::WebGPU:
            return {Disposition::EquivalentCpuProjection, true, false, nullptr};
        case Backend::WebGL:
            break;
        }
        return {};
    }
};

// endregion original_typed_dependency_projection

// region original_typed_native_payload

class OriginalTypedNativePayloadLowering final {
public:
    struct Decision {
        arp::rhi::semantics::OriginalCommandNativeClosureLowering::Disposition disposition = arp::
            rhi::semantics::OriginalCommandNativeClosureLowering::Disposition::DiagnosticFailure;
        bool payloadAvailable = false, recordingAvailable = false, executionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-TYPED-PAYLOAD-BACKEND";
    };

    static constexpr Decision lowering(Backend backend, std::uint32_t version = 1u) noexcept
    {
        using D = arp::rhi::semantics::OriginalCommandNativeClosureLowering::Disposition;
        // Independent Payload8/Freeze8/Closure10 input preparation. This private
        // Prototype never grants recording, submission or completion authority.
        if (version == 8u) {
            switch (backend) {
            case Backend::Null:
                return {D::ValidatorOnly,
                    false,
                    false,
                    false,
                    "RHI-ORIGINAL-GRAPHICS-PAYLOAD-NULL-VALIDATOR-ONLY"};
            case Backend::Vulkan:
                return {D::DiagnosticFailure,
                    false,
                    false,
                    false,
                    "RHI-ORIGINAL-GRAPHICS-PAYLOAD-VULKAN-JOIN-MISSING"};
            case Backend::Metal:
                return {D::DiagnosticFailure,
                    false,
                    false,
                    false,
                    "RHI-ORIGINAL-GRAPHICS-PAYLOAD-METAL-JOIN-MISSING"};
            case Backend::WebGPU:
                return {D::Prototype,
                    true,
                    false,
                    false,
                    "RHI-ORIGINAL-GRAPHICS-PAYLOAD-WEBGPU-INPUT-ONLY"};
            case Backend::WebGL:
                break;
            }
            return {};
        }
        // Payload4 borrows Closure6's initialized geometry and Freeze4 only.
        // Payload5 composes original geometry and Join3 through Freeze5/Closure7.
        // Payload6 consumes GeneralWrite1's canonical scalar resources through
        // Freeze6/Closure8; tables, geometry and readback remain disjoint.
        // Native recording/submission/completion remain independently gated.
        // Payload7 consumes only Freeze7/Closure9; no execution authority.
        if (version < 1u || version > 7u)
            return {
                D::DiagnosticFailure, false, false, false, "RHI-ORIGINAL-TYPED-PAYLOAD-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {D::ValidatorOnly,
                false,
                false,
                false,
                "RHI-ORIGINAL-TYPED-PAYLOAD-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {D::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-TYPED-PAYLOAD-VULKAN-MISSING"};
        case Backend::Metal:
            return {D::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-TYPED-PAYLOAD-METAL-MISSING"};
        case Backend::WebGPU:
            return {D::Prototype,
                true,
                false,
                false,
                "RHI-WEBGPU-TYPED-PAYLOAD-LIFECYCLE-EXECUTOR-MISSING"};
        case Backend::WebGL:
            break;
        }
        return {};
    }
};

// endregion original_typed_native_payload

// region original_typed_native_cold_command

class OriginalTypedNativeColdCommandLowering final {
public:
    struct Decision {
        arp::rhi::semantics::OriginalCommandNativeClosureLowering::Disposition disposition = arp::
            rhi::semantics::OriginalCommandNativeClosureLowering::Disposition::DiagnosticFailure;
        bool connectionAvailable = false, recordingAvailable = false, executionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-TYPED-COLD-COMMAND-BACKEND";
    };

    static constexpr Decision lowering(Backend backend, std::uint32_t version = 1u) noexcept
    {
        using D = arp::rhi::semantics::OriginalCommandNativeClosureLowering::Disposition;
        if (version < 1u || version > 8u)
            return {D::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-TYPED-COLD-COMMAND-VERSION"};
        if (version == 8u) {
            switch (backend) {
            case Backend::Null:
                return {D::ValidatorOnly,
                    false,
                    false,
                    false,
                    "RHI-ORIGINAL-GRAPHICS-COLD-COMMAND-NULL-VALIDATOR-ONLY"};
            case Backend::Vulkan:
                return {D::DiagnosticFailure,
                    false,
                    false,
                    false,
                    "RHI-ORIGINAL-GRAPHICS-COLD-COMMAND-VULKAN-JOIN-MISSING"};
            case Backend::Metal:
                return {D::DiagnosticFailure,
                    false,
                    false,
                    false,
                    "RHI-ORIGINAL-GRAPHICS-COLD-COMMAND-METAL-JOIN-MISSING"};
            case Backend::WebGPU:
                return {D::Prototype,
                    true,
                    false,
                    false,
                    "RHI-ORIGINAL-GRAPHICS-COLD-COMMAND-WEBGPU-CONNECTION-ONLY"};
            case Backend::WebGL:
                break;
            }
            return {};
        }
        // Command6/7 connect exact Cold6/7, Payload6/7 and Lifecycle3/4 custody.
        // Callback, issuer, recording and execution require independent gates.
        // Command5 connects only the exact composed Cold5/Lifecycle2 chain.
        // Connection creates no encoder, receiver, map or completion authority.
        // Geometry4 consumes its exact original Cold4 chain; legacy and mixed
        // readback profiles do not authorize geometry execution. Public gates stay closed.
        switch (backend) {
        case Backend::Null:
            return {D::ValidatorOnly,
                false,
                false,
                false,
                "RHI-ORIGINAL-TYPED-COLD-COMMAND-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {D::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-TYPED-COLD-COMMAND-VULKAN-MISSING"};
        case Backend::Metal:
            return {D::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-TYPED-COLD-COMMAND-METAL-MISSING"};
        case Backend::WebGPU:
            return {D::Prototype,
                true,
                false,
                false,
                "RHI-WEBGPU-TYPED-COLD-COMMAND-LIVE-EXECUTOR-MISSING"};
        case Backend::WebGL:
            break;
        }
        return {};
    }
};

// endregion original_typed_native_cold_command

// region original_typed_callback_custody

class OriginalTypedCallbackCustodyLowering final {
public:
    struct Decision {
        bool validatorOnly = false, observerPreparationAvailable = false,
             recordingAvailable = false, executionAvailable = false;
        const char* diagnostic = "RHI-TYPED-CALLBACK-CUSTODY-BACKEND";
    };

    static constexpr Decision lowering(Backend backend, std::uint32_t version = 1u) noexcept
    {
        if (version < 1u || version > 8u)
            return {false, false, false, false, "RHI-TYPED-CALLBACK-CUSTODY-VERSION"};
        if (version == 8u) {
            switch (backend) {
            case Backend::Null:
                return {true,
                    false,
                    false,
                    false,
                    "RHI-ORIGINAL-GRAPHICS-CALLBACK-NULL-VALIDATOR-ONLY"};
            case Backend::Vulkan:
                return {false,
                    false,
                    false,
                    false,
                    "RHI-ORIGINAL-GRAPHICS-CALLBACK-VULKAN-CUSTODY-MISSING"};
            case Backend::Metal:
                return {false,
                    false,
                    false,
                    false,
                    "RHI-ORIGINAL-GRAPHICS-CALLBACK-METAL-CUSTODY-MISSING"};
            case Backend::WebGPU:
                return {false,
                    true,
                    false,
                    false,
                    "RHI-ORIGINAL-GRAPHICS-CALLBACK-WEBGPU-OBSERVER-ONLY"};
            case Backend::WebGL:
                break;
            }
            return {};
        }
        // Callback6/7 join exact General Command6/7, Cold6/7 and Lifecycle3/4
        // graphs. Observer preparation grants no native issuer or execution.
        // Geometry4 consumes its exact original Cold4 chain; legacy and mixed
        // readback profiles do not authorize geometry execution. Public gates stay closed.
        switch (backend) {
        case Backend::Null:
            return {true, false, false, false, "RHI-TYPED-CALLBACK-CUSTODY-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {false, false, false, false, "RHI-TYPED-CALLBACK-CUSTODY-VULKAN-MISSING"};
        case Backend::Metal:
            return {false, false, false, false, "RHI-TYPED-CALLBACK-CUSTODY-METAL-MISSING"};
        case Backend::WebGPU:
            return {
                false, true, false, false, "RHI-TYPED-CALLBACK-CUSTODY-WEBGPU-RECORDING-MISSING"};
        case Backend::WebGL:
            break;
        }
        return {};
    }
};

// TypedReadbackMapCustody/1 prepares exact map/scope receipts in the existing
// original callback graph. It grants neither native map issuance nor completion.
class OriginalTypedReadbackMapCustodyLowering final {
public:
    struct Decision {
        bool validatorOnly = false, preparationAvailable = false;
        bool mappingAvailable = false, completionAvailable = false;
        const char* diagnostic = "RHI-TYPED-READBACK-MAP-CUSTODY-BACKEND";
    };

    static constexpr Decision lowering(Backend backend, std::uint32_t version = 1u) noexcept
    {
        if (version != 1u)
            return {false, false, false, false, "RHI-TYPED-READBACK-MAP-CUSTODY-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {
                true, false, false, false, "RHI-TYPED-READBACK-MAP-CUSTODY-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {false, false, false, false, "RHI-TYPED-READBACK-MAP-CUSTODY-VULKAN-MISSING"};
        case Backend::Metal:
            return {false, false, false, false, "RHI-TYPED-READBACK-MAP-CUSTODY-METAL-MISSING"};
        case Backend::WebGPU:
            return {false, true, false, false, "RHI-TYPED-READBACK-MAP-CUSTODY-COMPLETION-MISSING"};
        case Backend::WebGL:
            break;
        }
        return {};
    }
};

// endregion original_typed_callback_custody

// region original_typed_issuer_preparation

class OriginalTypedIssuerPreparationLowering final {
public:
    struct Decision {
        bool validatorOnly = false, preparationAvailable = false, recordingAvailable = false,
             submissionAvailable = false;
        const char* diagnostic = "RHI-TYPED-ISSUER-PREPARATION-BACKEND";
    };

    static constexpr Decision lowering(Backend backend, std::uint32_t version = 1u) noexcept
    {
        if (version < 1u || version > 8u)
            return {false, false, false, false, "RHI-TYPED-ISSUER-PREPARATION-VERSION"};
        // Issuer8 prepares the exact original Graphics graph under explicit
        // Prototype opt-in. Preparation grants no recording or submission.
        if (version == 8u) {
            switch (backend) {
            case Backend::Null:
                return {
                    true, false, false, false, "RHI-ORIGINAL-GRAPHICS-ISSUER-NULL-VALIDATOR-ONLY"};
            case Backend::Vulkan:
                return {
                    false, false, false, false, "RHI-ORIGINAL-GRAPHICS-ISSUER-VULKAN-JOIN-MISSING"};
            case Backend::Metal:
                return {
                    false, false, false, false, "RHI-ORIGINAL-GRAPHICS-ISSUER-METAL-JOIN-MISSING"};
            case Backend::WebGPU:
                return {false,
                    true,
                    false,
                    false,
                    "RHI-ORIGINAL-GRAPHICS-ISSUER-WEBGPU-RECORDING-MISSING"};
            case Backend::WebGL:
                break;
            }
            return {};
        }
        // Issuer6/7 prepare exact General Callback6/7 and Lifecycle3/4 graphs;
        // native recording and submission consume independent execution gates.
        // Geometry4 consumes its exact original Cold4 chain; legacy and mixed
        // readback profiles do not authorize geometry execution. Public gates stay closed.
        switch (backend) {
        case Backend::Null:
            return {true, false, false, false, "RHI-TYPED-ISSUER-PREPARATION-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {false, false, false, false, "RHI-TYPED-ISSUER-PREPARATION-VULKAN-MISSING"};
        case Backend::Metal:
            return {false, false, false, false, "RHI-TYPED-ISSUER-PREPARATION-METAL-MISSING"};
        case Backend::WebGPU:
            return {
                false, true, false, false, "RHI-TYPED-ISSUER-PREPARATION-WEBGPU-RECORDING-MISSING"};
        case Backend::WebGL:
            break;
        }
        return {};
    }
};

// endregion original_typed_issuer_preparation

// region original_typed_native_execution

class OriginalTypedNativeExecutionLowering final {
public:
    struct Decision {
        bool validatorOnly = false, prototype = false, recordingAvailable = false,
             submissionAvailable = false;
        const char* diagnostic = "RHI-TYPED-NATIVE-EXECUTION-BACKEND";
    };

    static constexpr Decision lowering(Backend backend,
        std::uint32_t version = 1u,
        std::uint32_t readbackCompletionVersion = 0u,
        std::uint32_t generalSubmissionVersion = 0u) noexcept
    {
        if (generalSubmissionVersion && version != 6u && version != 7u && version != 8u)
            return {false, false, false, false, "RHI-TYPED-NATIVE-GENERAL-SUBMISSION-PROFILE"};
        if (generalSubmissionVersion
            && generalSubmissionVersion
                != (version == 8u       ? 3u
                        : version == 7u ? 2u
                                        : 1u))
            return {false, false, false, false, "RHI-TYPED-NATIVE-GENERAL-SUBMISSION-VERSION"};
        if (version == 5u) {
            if (!readbackCompletionVersion)
                return {backend == Backend::Null,
                    false,
                    false,
                    false,
                    "RHI-TYPED-NATIVE-READBACK-COMPLETION-REQUIRED"};
            if (readbackCompletionVersion != 1u)
                return {backend == Backend::Null,
                    false,
                    false,
                    false,
                    "RHI-TYPED-NATIVE-READBACK-COMPLETION-VERSION"};
            // The explicit composed completion profile uses the same typed issuers.
        }
        if (readbackCompletionVersion
            && (readbackCompletionVersion != 1u || (version != 3u && version != 5u)))
            return {false, false, false, false, "RHI-TYPED-NATIVE-READBACK-COMPLETION-VERSION"};
        if (version < 1u || version > 8u)
            return {false, false, false, false, "RHI-TYPED-NATIVE-EXECUTION-VERSION"};
        // Graphics8 consumes exact Issuer8 through the original typed recorder.
        // The separate opted-in GeneralSubmission3 consumes the original queue
        // and scope receipts. Output copy/map and public execution stay closed.
        if (version == 8u) {
            switch (backend) {
            case Backend::Null:
                return {true,
                    false,
                    false,
                    false,
                    "RHI-ORIGINAL-GRAPHICS-EXECUTION-NULL-VALIDATOR-ONLY"};
            case Backend::Vulkan:
                return {false,
                    false,
                    false,
                    false,
                    "RHI-ORIGINAL-GRAPHICS-EXECUTION-VULKAN-JOIN-MISSING"};
            case Backend::Metal:
                return {false,
                    false,
                    false,
                    false,
                    "RHI-ORIGINAL-GRAPHICS-EXECUTION-METAL-JOIN-MISSING"};
            case Backend::WebGPU:
                return generalSubmissionVersion == 3u
                    ? Decision{false, true, true, true, nullptr}
                    : Decision{false,
                          true,
                          true,
                          false,
                          "RHI-ORIGINAL-GRAPHICS-EXECUTION-WEBGPU-SUBMISSION-MISSING"};
            case Backend::WebGL:
                break;
            }
            return {};
        }
        // Geometry4 consumes its exact original Cold4 chain; legacy and mixed
        // readback profiles do not authorize geometry execution. Public gates stay closed.
        switch (backend) {
        case Backend::Null:
            return {true, false, false, false, "RHI-TYPED-NATIVE-EXECUTION-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {
                false, false, false, false, "RHI-TYPED-NATIVE-EXECUTION-VULKAN-PAYLOAD-MISSING"};
        case Backend::Metal:
            return {false, false, false, false, "RHI-TYPED-NATIVE-EXECUTION-METAL-PAYLOAD-MISSING"};
        case Backend::WebGPU:
            // General6 defaults to record/cancel. Only its independently
            // opted-in, recording-frozen submission profile consumes the queue.
            if (version == 7u && !generalSubmissionVersion)
                return {false,
                    true,
                    true,
                    false,
                    "RHI-TYPED-NATIVE-FINITE-GENERAL-SUBMISSION-UNPROVEN"};
            if (version == 6u && !generalSubmissionVersion)
                return {false, true, true, false, "RHI-TYPED-NATIVE-GENERAL-SUBMISSION-UNPROVEN"};
            // Mixed original readback can record its frozen copies. Submission
            // remains closed until the same-root map/result drain is implemented.
            if (version == 3u && !readbackCompletionVersion)
                return {false,
                    true,
                    true,
                    false,
                    "RHI-TYPED-NATIVE-EXECUTION-READBACK-COMPLETION-MISSING"};
            return {false, true, true, true, nullptr};
        case Backend::WebGL:
            break;
        }
        return {};
    }
};

// endregion original_typed_native_execution
// endregion Command shader lowering

// region Command transfer lowering

// region original_command_upload_staging

class OriginalCommandUploadStagingLowering final {
public:
    enum class Disposition { ValidatorOnly, DiagnosticFailure };

    struct Decision {
        Disposition disposition = Disposition::DiagnosticFailure;
        bool initializationAvailable = false, recordingAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-UPLOAD-STAGING-BACKEND-UNKNOWN";
    };

    static constexpr Decision lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {Disposition::ValidatorOnly,
                false,
                false,
                "RHI-ORIGINAL-UPLOAD-STAGING-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {Disposition::DiagnosticFailure,
                false,
                false,
                "RHI-ORIGINAL-UPLOAD-STAGING-VULKAN-JOIN-MISSING"};
        case Backend::Metal:
            return {Disposition::DiagnosticFailure,
                false,
                false,
                "RHI-ORIGINAL-UPLOAD-STAGING-METAL-JOIN-MISSING"};
        case Backend::WebGPU:
            return {Disposition::DiagnosticFailure,
                false,
                false,
                "RHI-ORIGINAL-UPLOAD-STAGING-WEBGPU-JOIN-MISSING"};
        case Backend::WebGL:
            break;
        }
        return {};
    }
};

// endregion original_command_upload_staging

// region original_command_ordered_upload

class OriginalCommandOrderedUploadLowering final {
public:
    using Disposition = arp::rhi::semantics::OriginalCommandNativeClosureLowering::Disposition;

    struct Decision {
        std::uint32_t contractVersion = 1u;
        Disposition disposition = Disposition::DiagnosticFailure;
        bool nativeProjectionAvailable = false, recordingAvailable = false,
             executionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-ORDERED-UPLOAD-BACKEND-UNKNOWN";
    };

    static constexpr Decision lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {1u,
                Disposition::ValidatorOnly,
                false,
                false,
                false,
                "RHI-ORIGINAL-ORDERED-UPLOAD-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {1u,
                Disposition::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-ORDERED-UPLOAD-VULKAN-PAYLOAD-MISSING"};
        case Backend::Metal:
            return {1u,
                Disposition::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-ORDERED-UPLOAD-METAL-PAYLOAD-MISSING"};
        case Backend::WebGPU:
            return {1u,
                Disposition::Prototype,
                false,
                false,
                false,
                "RHI-ORIGINAL-ORDERED-UPLOAD-WEBGPU-JOIN-MISSING"};
        case Backend::WebGL:
            break;
        }
        return {};
    }
};

// endregion original_command_ordered_upload

// region original_command_readback_join

class OriginalCommandReadbackJoinLowering final {
public:
    enum class Disposition { ValidatorOnly, Prototype, DiagnosticFailure };

    struct Decision {
        Disposition disposition = Disposition::DiagnosticFailure;
        bool privateJoinAvailable = false;
        bool recordingAvailable = false, mapAvailable = false, publicationAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-READBACK-JOIN-BACKEND-UNKNOWN";
    };

    static constexpr Decision lowering(Backend backend, std::uint32_t version = 1u) noexcept
    {
        if (version == 4u) {
            switch (backend) {
            case Backend::Null:
                return {Disposition::ValidatorOnly,
                    false,
                    false,
                    false,
                    false,
                    "RHI-GENERAL-OUTPUT-READBACK-NULL-VALIDATOR-ONLY"};
            case Backend::Vulkan:
                return {Disposition::DiagnosticFailure,
                    false,
                    false,
                    false,
                    false,
                    "RHI-GENERAL-OUTPUT-READBACK-VULKAN-JOIN-MISSING"};
            case Backend::Metal:
                return {Disposition::DiagnosticFailure,
                    false,
                    false,
                    false,
                    false,
                    "RHI-GENERAL-OUTPUT-READBACK-METAL-JOIN-MISSING"};
            case Backend::WebGPU:
                return {Disposition::Prototype,
                    true,
                    false,
                    false,
                    false,
                    "RHI-GENERAL-OUTPUT-READBACK-WEBGPU-EXECUTION-MISSING"};
            case Backend::WebGL:
                break;
            }
            return {};
        }
        if (version == 3u) {
            switch (backend) {
            case Backend::Null:
                return {Disposition::ValidatorOnly,
                    false,
                    false,
                    false,
                    false,
                    "RHI-ORIGINAL-GEOMETRY-READBACK-PROGRAM-JOIN-NULL-VALIDATOR-ONLY"};
            case Backend::Vulkan:
            case Backend::Metal:
                return {Disposition::DiagnosticFailure,
                    false,
                    false,
                    false,
                    false,
                    "RHI-ORIGINAL-GEOMETRY-READBACK-PROGRAM-JOIN-MISSING"};
            case Backend::WebGPU:
                return {Disposition::Prototype,
                    true,
                    false,
                    false,
                    false,
                    "RHI-ORIGINAL-GEOMETRY-READBACK-PROGRAM-JOIN-EXECUTION-MISSING"};
            case Backend::WebGL:
                break;
            }
            return {};
        }
        if (version == 2u) {
            switch (backend) {
            case Backend::Null:
                return {Disposition::ValidatorOnly,
                    false,
                    false,
                    false,
                    false,
                    "RHI-ORIGINAL-READBACK-PROGRAM-JOIN-NULL-VALIDATOR-ONLY"};
            case Backend::Vulkan:
                return {Disposition::DiagnosticFailure,
                    false,
                    false,
                    false,
                    false,
                    "RHI-ORIGINAL-READBACK-PROGRAM-JOIN-VULKAN-PAYLOAD-MISSING"};
            case Backend::Metal:
                return {Disposition::DiagnosticFailure,
                    false,
                    false,
                    false,
                    false,
                    "RHI-ORIGINAL-READBACK-PROGRAM-JOIN-METAL-PAYLOAD-MISSING"};
            case Backend::WebGPU:
                return {Disposition::Prototype,
                    true,
                    false,
                    false,
                    false,
                    "RHI-ORIGINAL-READBACK-PROGRAM-JOIN-EXECUTION-MISSING"};
            case Backend::WebGL:
                break;
            }
            return {};
        }
        if (version != 1u)
            return {Disposition::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-ORIGINAL-READBACK-JOIN-LOWERING-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {Disposition::ValidatorOnly,
                false,
                false,
                false,
                false,
                "RHI-ORIGINAL-READBACK-JOIN-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {Disposition::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-ORIGINAL-READBACK-JOIN-VULKAN-PAYLOAD-MISSING"};
        case Backend::Metal:
            return {Disposition::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-ORIGINAL-READBACK-JOIN-METAL-PAYLOAD-MISSING"};
        case Backend::WebGPU:
            return {Disposition::Prototype,
                true,
                false,
                false,
                false,
                "RHI-ORIGINAL-READBACK-JOIN-ORDERED-EXECUTION-MISSING"};
        case Backend::WebGL:
            break;
        }
        return {};
    }
};

// endregion original_command_readback_join

// region original_general_output_readback_dependency
// Normative ordered-use mapping only; native issuers remain independently gated.
class OriginalGeneralOutputReadbackDependencyLowering final {
public:
    using Decision = TransferDependencyLowering::Decision;

    static Decision transition(Backend backend,
        std::uint32_t version,
        bool optIn,
        const ResourceBarrierDesc& desc,
        ResourceState current,
        const TextureSubresourceRange& wholeRange) noexcept
    {
        const auto failure = [](const char* id) noexcept -> Decision {
            return {CapabilityMode::DiagnosticFailure,
                TransferDependencyLowering::Operation::DiagnosticFailure,
                id};
        };
        if (version < 1u || version > 3u)
            return failure("RHI-GENERAL-OUTPUT-READBACK-DEPENDENCY-VERSION");
        if (!optIn)
            return failure("RHI-GENERAL-OUTPUT-READBACK-DEPENDENCY-OPT-IN");
        if (const auto* id = OriginalGeneralOutputReadbackDependencyContract::validate(
                desc, current, wholeRange, version))
            return failure(id);
        switch (backend) {
        case Backend::Null:
            return failure("RHI-GENERAL-OUTPUT-READBACK-DEPENDENCY-NULL-VALIDATOR-ONLY");
        case Backend::Vulkan:
            return failure("RHI-GENERAL-OUTPUT-READBACK-DEPENDENCY-VULKAN-PAYLOAD-MISSING");
        case Backend::Metal:
            return failure("RHI-GENERAL-OUTPUT-READBACK-DEPENDENCY-METAL-PAYLOAD-MISSING");
        case Backend::WebGPU:
            return {CapabilityMode::Equivalent,
                TransferDependencyLowering::Operation::ImplicitOrderedUsage,
                nullptr};
        case Backend::WebGL:
            break;
        }
        return failure("RHI-GENERAL-OUTPUT-READBACK-DEPENDENCY-BACKEND-UNKNOWN");
    }
};

// endregion original_general_output_readback_dependency

// region original_command_upload_join

class OriginalCommandUploadJoinLowering final {
public:
    struct Decision {
        bool privateJoinAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-UPLOAD-JOIN-BACKEND-UNKNOWN";
    };

    static constexpr Decision lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {false, "RHI-ORIGINAL-UPLOAD-JOIN-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {false, "RHI-ORIGINAL-UPLOAD-JOIN-VULKAN-PAYLOAD-MISSING"};
        case Backend::Metal:
            return {false, "RHI-ORIGINAL-UPLOAD-JOIN-METAL-PAYLOAD-MISSING"};
        case Backend::WebGPU:
            return {true, "RHI-ORIGINAL-UPLOAD-JOIN-ORDERED-EXECUTION-MISSING"};
        case Backend::WebGL:
            break;
        }
        return {};
    }
};

// endregion original_command_upload_join

// region original_command_readback_map_drain

class OriginalCommandReadbackMapDrainLowering final {
public:
    static constexpr auto lowering(Backend backend) noexcept
    {
        return arp::rhi::semantics::OriginalCommandReadbackMapLowering::lowering(backend);
    }
};

// endregion original_command_readback_map_drain

// region original_command_readback_snapshot_publication

class OriginalCommandReadbackSnapshotPublicationLowering final {
public:
    struct Lowering {
        arp::rhi::semantics::OriginalCommandNativeClosureLowering::Disposition disposition = arp::
            rhi::semantics::OriginalCommandNativeClosureLowering::Disposition::DiagnosticFailure;
        bool publicationAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-SNAPSHOT-PUBLICATION-BACKEND-UNKNOWN";
    };

    static constexpr Lowering lowering(Backend backend) noexcept
    {
        using D = arp::rhi::semantics::OriginalCommandNativeClosureLowering::Disposition;
        switch (backend) {
        case Backend::Null:
            return {
                D::ValidatorOnly, false, "RHI-ORIGINAL-SNAPSHOT-PUBLICATION-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {D::DiagnosticFailure,
                false,
                "RHI-ORIGINAL-SNAPSHOT-PUBLICATION-VULKAN-PAYLOAD-MISSING"};
        case Backend::Metal:
            return {D::DiagnosticFailure,
                false,
                "RHI-ORIGINAL-SNAPSHOT-PUBLICATION-METAL-PAYLOAD-MISSING"};
        case Backend::WebGPU:
            return {
                D::Prototype, true, "RHI-ORIGINAL-SNAPSHOT-PUBLICATION-WEBGPU-PROTOTYPE-OPT-IN"};
        case Backend::WebGL:
            break;
        }
        return {};
    }
};

// endregion original_command_readback_snapshot_publication

// region original_command_initialized_buffer_staging

class OriginalCommandInitializedBufferStagingLowering final {
public:
    enum class Disposition { ValidatorOnly, Prototype, DiagnosticFailure };

    struct Decision {
        Disposition disposition = Disposition::DiagnosticFailure;
        bool privateInitializationAvailable = false;
        const char* diagnostic = "RHI-INITIALIZED-CANONICAL-STAGING-BACKEND-UNKNOWN";
    };

    static constexpr Decision indexShaderReadLowering(
        Backend backend, std::uint32_t version = 1u) noexcept
    {
        if (version != 1u)
            return {
                Disposition::DiagnosticFailure, false, "RHI-INITIALIZED-INDEX-SHADER-READ-VERSION"};
        // Same initialization/callback/loss ownership; no shader execution gate.
        return lowering(backend, 2u);
    }

    static constexpr Decision lowering(Backend backend, std::uint32_t version = 1u) noexcept
    {
        if (version < 1u || version > 4u)
            return {
                Disposition::DiagnosticFailure, false, "RHI-INITIALIZED-CANONICAL-STAGING-VERSION"};
        if (version == 4u) {
            switch (backend) {
            case Backend::Null:
                return {Disposition::ValidatorOnly,
                    false,
                    "RHI-INITIALIZED-SHADER-NULL-VALIDATOR-ONLY"};
            case Backend::Vulkan:
                return {Disposition::DiagnosticFailure,
                    false,
                    "RHI-INITIALIZED-SHADER-VULKAN-PAYLOAD-MISSING"};
            case Backend::Metal:
                return {Disposition::DiagnosticFailure,
                    false,
                    "RHI-INITIALIZED-SHADER-METAL-PAYLOAD-MISSING"};
            case Backend::WebGPU:
                return {
                    Disposition::Prototype, true, "RHI-INITIALIZED-SHADER-WEBGPU-PRIVATE-PROFILE"};
            case Backend::WebGL:
                break;
            }
            return {};
        }
        if (version == 3u) {
            switch (backend) {
            case Backend::Null:
                return {Disposition::ValidatorOnly,
                    false,
                    "RHI-INITIALIZED-VERTEX-NULL-VALIDATOR-ONLY"};
            case Backend::Vulkan:
                return {Disposition::DiagnosticFailure,
                    false,
                    "RHI-INITIALIZED-VERTEX-VULKAN-PAYLOAD-MISSING"};
            case Backend::Metal:
                return {Disposition::DiagnosticFailure,
                    false,
                    "RHI-INITIALIZED-VERTEX-METAL-PAYLOAD-MISSING"};
            case Backend::WebGPU:
                return {
                    Disposition::Prototype, true, "RHI-INITIALIZED-VERTEX-WEBGPU-PRIVATE-PROFILE"};
            case Backend::WebGL:
                break;
            }
            return {};
        }
        if (version == 2u) {
            switch (backend) {
            case Backend::Null:
                return {
                    Disposition::ValidatorOnly, false, "RHI-INITIALIZED-INDEX-NULL-VALIDATOR-ONLY"};
            case Backend::Vulkan:
                return {Disposition::DiagnosticFailure,
                    false,
                    "RHI-INITIALIZED-INDEX-VULKAN-PAYLOAD-MISSING"};
            case Backend::Metal:
                return {Disposition::DiagnosticFailure,
                    false,
                    "RHI-INITIALIZED-INDEX-METAL-PAYLOAD-MISSING"};
            case Backend::WebGPU:
                return {
                    Disposition::Prototype, true, "RHI-INITIALIZED-INDEX-WEBGPU-PRIVATE-PROFILE"};
            case Backend::WebGL:
                break;
            }
            return {};
        }
        switch (backend) {
        case Backend::Null:
            return {Disposition::ValidatorOnly,
                false,
                "RHI-INITIALIZED-CANONICAL-STAGING-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {Disposition::DiagnosticFailure,
                false,
                "RHI-INITIALIZED-CANONICAL-STAGING-VULKAN-PAYLOAD-MISSING"};
        case Backend::Metal:
            return {Disposition::DiagnosticFailure,
                false,
                "RHI-INITIALIZED-CANONICAL-STAGING-METAL-PAYLOAD-MISSING"};
        case Backend::WebGPU:
            return {Disposition::Prototype,
                true,
                "RHI-INITIALIZED-CANONICAL-STAGING-WEBGPU-PRIVATE-PROFILE"};
        case Backend::WebGL:
            break;
        }
        return {};
    }
};

// endregion original_command_initialized_buffer_staging

// region original_command_copy_pass_lowering

class OriginalCommandCopyPassLowering final {
public:
    struct Decision {
        arp::rhi::semantics::OriginalCommandNativeClosureLowering::Disposition disposition = arp::
            rhi::semantics::OriginalCommandNativeClosureLowering::Disposition::DiagnosticFailure;
        bool nativeProjectionAvailable = false, recordingAvailable = false,
             executionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-COPY-PASS-LOWERING-BACKEND-UNKNOWN";
    };

    static constexpr Decision lowering(Backend backend) noexcept
    {
        using D = arp::rhi::semantics::OriginalCommandNativeClosureLowering::Disposition;
        switch (backend) {
        case Backend::Null:
            return {D::ValidatorOnly,
                false,
                false,
                false,
                "RHI-ORIGINAL-COPY-PASS-LOWERING-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {D::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-COPY-PASS-LOWERING-VULKAN-UNIMPLEMENTED"};
        case Backend::Metal:
            return {D::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-COPY-PASS-LOWERING-METAL-UNIMPLEMENTED"};
        case Backend::WebGPU:
            return {D::Prototype,
                true,
                false,
                false,
                "RHI-ORIGINAL-COPY-PASS-LOWERING-WEBGPU-RECORDING-UNPROVEN"};
        case Backend::WebGL:
            break;
        }
        return {};
    }

    static constexpr Decision lowering(Backend backend, std::uint32_t version) noexcept
    {
        if (version == 1u)
            return lowering(backend);
        if (version == 2u) {
            const auto d =
                arp::rhi::semantics::OriginalCommandOrderedCopyAttachmentLowering::lowering(
                    backend);
            return {d.disposition,
                d.nativeProjectionAvailable,
                d.recordingAvailable,
                d.executionAvailable,
                d.diagnostic};
        }
        if (version == 3u) {
            const auto d =
                arp::rhi::semantics::OriginalCommandOrderedUploadLowering::lowering(backend);
            return {d.disposition,
                backend == Backend::WebGPU,
                false,
                false,
                backend == Backend::WebGPU ? "RHI-ORIGINAL-UPLOAD-LOWERING-COLD-PROJECTION-ONLY"
                                           : d.diagnostic};
        }
        if (version == 4u || version == 5u) {
            const auto d =
                arp::rhi::semantics::OriginalCommandOrderedReadbackLowering::lowering(backend);
            return {d.disposition,
                d.nativeProjectionAvailable,
                false,
                false,
                version == 5u && backend == Backend::WebGPU
                    ? "RHI-ORIGINAL-COPY-PASS-LOWERING-MAP-COLD-PROJECTION-ONLY"
                    : d.diagnostic};
        }
        return {arp::rhi::semantics::OriginalCommandNativeClosureLowering::Disposition::
                    DiagnosticFailure,
            false,
            false,
            false,
            "RHI-ORIGINAL-COPY-PASS-LOWERING-VERSION"};
    }
};

// endregion original_command_copy_pass_lowering

// region original_command_initialized_buffer_upload_execution

class OriginalCommandInitializedBufferUploadExecutionLowering final {
public:
    using Disposition = arp::rhi::semantics::OriginalCommandNativeClosureLowering::Disposition;

    struct Decision {
        Disposition disposition = Disposition::DiagnosticFailure;
        bool recordingAvailable = false, executionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-INITIALIZED-BUFFER-UPLOAD-BACKEND-UNKNOWN";
    };

    static constexpr Decision lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {Disposition::ValidatorOnly,
                false,
                false,
                "RHI-ORIGINAL-INITIALIZED-BUFFER-UPLOAD-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {Disposition::DiagnosticFailure,
                false,
                false,
                "RHI-ORIGINAL-INITIALIZED-BUFFER-UPLOAD-VULKAN-PAYLOAD-MISSING"};
        case Backend::Metal:
            return {Disposition::DiagnosticFailure,
                false,
                false,
                "RHI-ORIGINAL-INITIALIZED-BUFFER-UPLOAD-METAL-PAYLOAD-MISSING"};
        case Backend::WebGPU:
            return {Disposition::Prototype,
                true,
                true,
                "RHI-ORIGINAL-INITIALIZED-BUFFER-UPLOAD-WEBGPU-PROTOTYPE"};
        case Backend::WebGL:
            break;
        }
        return {};
    }
};

// endregion original_command_initialized_buffer_upload_execution

// region original_command_initialized_texture_upload_execution

class OriginalCommandInitializedTextureUploadExecutionLowering final {
public:
    using Decision =
        arp::rhi::semantics::OriginalCommandInitializedBufferUploadExecutionLowering::Decision;
    using Disposition = arp::rhi::semantics::OriginalCommandNativeClosureLowering::Disposition;

    static constexpr Decision lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {Disposition::ValidatorOnly,
                false,
                false,
                "RHI-ORIGINAL-INITIALIZED-TEXTURE-UPLOAD-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {Disposition::DiagnosticFailure,
                false,
                false,
                "RHI-ORIGINAL-INITIALIZED-TEXTURE-UPLOAD-VULKAN-PAYLOAD-MISSING"};
        case Backend::Metal:
            return {Disposition::DiagnosticFailure,
                false,
                false,
                "RHI-ORIGINAL-INITIALIZED-TEXTURE-UPLOAD-METAL-PAYLOAD-MISSING"};
        case Backend::WebGPU:
            return {Disposition::Prototype,
                true,
                true,
                "RHI-ORIGINAL-INITIALIZED-TEXTURE-UPLOAD-WEBGPU-PROTOTYPE"};
        case Backend::WebGL:
            break;
        }
        return {};
    }
};

class OriginalCompletedShaderTextureInputLowering final {
public:
    // Private input validation includes authentic whole-view/parent association.
    // State transitions, command issuance and shader execution remain separate.
    using Disposition = OriginalCommandNativeClosureLowering::Disposition;

    struct Decision {
        Disposition disposition = Disposition::DiagnosticFailure;
        bool privateInputValidationAvailable = false;
        bool shaderExecutionAvailable = false;
        const char* diagnostic = "RHI-COMPLETED-SHADER-TEXTURE-BACKEND-UNKNOWN";
    };

    static constexpr Decision lowering(Backend backend, std::uint32_t version = 1u) noexcept
    {
        if (version != 1u)
            return {Disposition::DiagnosticFailure,
                false,
                false,
                "RHI-COMPLETED-SHADER-TEXTURE-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {Disposition::ValidatorOnly,
                false,
                false,
                "RHI-COMPLETED-SHADER-TEXTURE-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {Disposition::DiagnosticFailure,
                false,
                false,
                "RHI-COMPLETED-SHADER-TEXTURE-VULKAN-JOIN-MISSING"};
        case Backend::Metal:
            return {Disposition::DiagnosticFailure,
                false,
                false,
                "RHI-COMPLETED-SHADER-TEXTURE-METAL-JOIN-MISSING"};
        case Backend::WebGPU:
            return {Disposition::Prototype,
                true,
                false,
                "RHI-COMPLETED-SHADER-TEXTURE-WEBGPU-PRIVATE-INPUT"};
        case Backend::WebGL:
            break;
        }
        return {};
    }
};
} // namespace arp::rhi::semantics

// endregion original_command_initialized_texture_upload_execution
// endregion Command transfer lowering

// region original_general_output_snapshot
namespace arp::rhi::semantics {
class OriginalGeneralOutputSnapshotLowering final {
public:
    using Disposition = OriginalCommandNativeClosureLowering::Disposition;

    struct Decision {
        Disposition disposition = Disposition::DiagnosticFailure;
        bool privateAssociationAvailable = false;
        bool copyAvailable = false, mapAvailable = false, publicExecutionAvailable = false;
        const char* diagnostic = "RHI-GENERAL-OUTPUT-SNAPSHOT-BACKEND-UNKNOWN";
    };

    // Immutable association after independently authenticated completion.
    // This does not grant downstream copy/map or current Pending-state access.
    static constexpr Decision completedAssociationLowering(
        Backend backend, std::uint32_t version) noexcept
    {
        if (backend == Backend::WebGPU && version == 3u)
            return {Disposition::Prototype,
                true,
                false,
                false,
                false,
                "RHI-GRAPHICS-OUTPUT-SNAPSHOT-WEBGPU-PRIVATE-COMPLETED-ASSOCIATION"};
        return lowering(backend, version);
    }

    static constexpr Decision lowering(Backend backend, std::uint32_t version = 1u) noexcept
    {
        if (version == 3u) {
            using D = Disposition;
            switch (backend) {
            case Backend::Null:
                return {D::ValidatorOnly,
                    false,
                    false,
                    false,
                    false,
                    "RHI-GRAPHICS-OUTPUT-SNAPSHOT-NULL-VALIDATOR-ONLY"};
            case Backend::Vulkan:
                return {D::DiagnosticFailure,
                    false,
                    false,
                    false,
                    false,
                    "RHI-GRAPHICS-OUTPUT-SNAPSHOT-VULKAN-JOIN-MISSING"};
            case Backend::Metal:
                return {D::DiagnosticFailure,
                    false,
                    false,
                    false,
                    false,
                    "RHI-GRAPHICS-OUTPUT-SNAPSHOT-METAL-JOIN-MISSING"};
            case Backend::WebGPU:
                return {D::Prototype,
                    true,
                    false,
                    false,
                    false,
                    "RHI-GRAPHICS-OUTPUT-SNAPSHOT-WEBGPU-PRIVATE-PREPARED-ASSOCIATION"};
            case Backend::WebGL:
                break;
            }
            return {};
        }
        if (version != 1u && version != 2u)
            return {Disposition::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-GENERAL-OUTPUT-SNAPSHOT-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {Disposition::ValidatorOnly,
                false,
                false,
                false,
                false,
                "RHI-GENERAL-OUTPUT-SNAPSHOT-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {Disposition::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-GENERAL-OUTPUT-SNAPSHOT-VULKAN-JOIN-MISSING"};
        case Backend::Metal:
            return {Disposition::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-GENERAL-OUTPUT-SNAPSHOT-METAL-JOIN-MISSING"};
        case Backend::WebGPU:
            return {Disposition::Prototype,
                true,
                false,
                false,
                false,
                "RHI-GENERAL-OUTPUT-SNAPSHOT-WEBGPU-PRIVATE-ASSOCIATION"};
        case Backend::WebGL:
            break;
        }
        return {};
    }
};
} // namespace arp::rhi::semantics

// endregion original_general_output_snapshot

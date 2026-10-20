#pragma once
#include "rhi/gpu_capabilities.h"
#include "rhi/gpu_common.h"

namespace arp::rhi::semantics {
class OriginalArtifactCreationLowering final {
public:
    struct Facts {
        bool device = false, shader = false, pipeline = false, bindGroupLayout = false,
             pipelineLayout = false, pushScope = false, popScope = false;
    };

    struct Diagnostics {
        const char* backend;
        const char* nullValidator;
        const char* vulkan;
        const char* metal;
        const char* device;
        const char* methods;
    };

    struct Decision {
        CapabilityMode mode = CapabilityMode::DiagnosticFailure;
        bool validatorOnly = false, creationEligible = false, executionAvailable = false;
        const char* diagnostic = nullptr;
    };

    static constexpr Decision decide(Backend backend, Facts facts, Diagnostics ids) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {CapabilityMode::Equivalent, true, false, false, ids.nullValidator};
        case Backend::Vulkan:
            return {CapabilityMode::DiagnosticFailure, false, false, false, ids.vulkan};
        case Backend::Metal:
            return {CapabilityMode::DiagnosticFailure, false, false, false, ids.metal};
        case Backend::WebGPU:
            if (!facts.device)
                return {CapabilityMode::DiagnosticFailure, false, false, false, ids.device};
            if (!facts.shader || !facts.pipeline || !facts.bindGroupLayout || !facts.pipelineLayout
                || !facts.pushScope || !facts.popScope)
                return {CapabilityMode::DiagnosticFailure, false, false, false, ids.methods};
            return {CapabilityMode::Native, false, true, false, nullptr};
        case Backend::WebGL:
            break;
        }
        return {CapabilityMode::DiagnosticFailure, false, false, false, ids.backend};
    }
};
} // namespace arp::rhi::semantics

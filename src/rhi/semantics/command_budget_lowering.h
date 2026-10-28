#pragma once

#include "rhi/semantics/query_lowering.h"

namespace arp::rhi::semantics {

class CommandBudgetLowering final {
public:
    template <class Decision>
    static Decision profiling(Backend backend,
        bool requireNativeAllocationFootprint,
        const VulkanTimestampQueryFacts& facts,
        const char* runtimeDiagnostic) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {CapabilityMode::DiagnosticFailure,
                true,
                false,
                0.0,
                "RHI-COMMAND-PROFILING-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            if (runtimeDiagnostic)
                return {CapabilityMode::DiagnosticFailure, false, false, 0.0, runtimeDiagnostic};
            if (const auto* id = QueryLowering::vulkanTimestampDiagnostic(facts))
                return {CapabilityMode::Degraded, false, false, 0.0, id};
            if (requireNativeAllocationFootprint)
                return {CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    0.0,
                    "RHI-COMMAND-PROFILING-NATIVE-FOOTPRINT-UNAVAILABLE"};
            return {
                CapabilityMode::Native, false, false, facts.timestampPeriodNanoseconds, nullptr};
        case Backend::Metal:
        case Backend::WebGPU:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                0.0,
                "RHI-COMMAND-PROFILING-NATIVE-PAYLOAD-UNAVAILABLE"};
        case Backend::WebGL:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                0.0,
                "RHI-WEBGL1-QUERY-UNSUPPORTED"};
        }
        return {};
    }

    template <class Decision, class Disposition> static Decision recording(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {
                Disposition::ValidatorOnly, false, "RHI-COMMAND-RECORDING-BUDGET-NULL-VALIDATOR"};
        case Backend::Vulkan:
            return {Disposition::Prototype, true, nullptr};
        case Backend::Metal:
            return {Disposition::DiagnosticFailure,
                false,
                "RHI-COMMAND-RECORDING-BUDGET-METAL-PAYLOAD-MISSING"};
        case Backend::WebGPU:
            return {Disposition::DiagnosticFailure,
                false,
                "RHI-COMMAND-RECORDING-BUDGET-WEBGPU-PAYLOAD-MISSING"};
        case Backend::WebGL:
            return {Disposition::DiagnosticFailure,
                false,
                "RHI-COMMAND-RECORDING-BUDGET-WEBGL-UNSUPPORTED"};
        }
        return {};
    }

    template <class Decision> static Decision renderPassStorage(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {true, false, "RHI-RENDER-PASS-STORAGE-NULL-VALIDATOR"};
        case Backend::Vulkan:
            return {false, false, "RHI-RENDER-PASS-STORAGE-VULKAN-PAYLOAD-MISSING"};
        case Backend::Metal:
            return {false, false, "RHI-RENDER-PASS-STORAGE-METAL-PAYLOAD-MISSING"};
        case Backend::WebGPU:
            return {false, false, "RHI-RENDER-PASS-STORAGE-WEBGPU-PAYLOAD-MISSING"};
        case Backend::WebGL:
            return {false, false, "RHI-RENDER-PASS-STORAGE-WEBGL-UNSUPPORTED"};
        }
        return {};
    }

    template <class Decision>
    static constexpr Decision renderPassPreparation(Backend backend, bool recording) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {true, false, false, "RHI-RENDER-PASS-PREPARATION-NULL-VALIDATOR"};
        case Backend::Vulkan:
            return {false, true, recording, nullptr};
        case Backend::Metal:
            return {false, false, false, "RHI-RENDER-PASS-PREPARATION-METAL-PAYLOAD-MISSING"};
        case Backend::WebGPU:
            return {false, false, false, "RHI-RENDER-PASS-PREPARATION-WEBGPU-PAYLOAD-MISSING"};
        case Backend::WebGL:
            return {false, false, false, "RHI-RENDER-PASS-PREPARATION-WEBGL-UNSUPPORTED"};
        }
        return {};
    }

    template <class Decision>
    static constexpr Decision passTimingFailure(Backend backend, const char* diagnostic) noexcept
    {
        return {backend == Backend::Null, false, false, diagnostic};
    }
};

} // namespace arp::rhi::semantics

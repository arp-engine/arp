#pragma once

#include "rhi/gpu_common.h"

namespace arp::rhi::semantics {

// ConformanceEvidenceAdmission/1. These legacy entry points carry claims,
// not authenticated workload/output evidence. No identity grants execution.
// Real shared workloads and their evidence admission are a P4 deliverable.
class ConformanceEvidenceAdmission final {
public:
    static constexpr unsigned contractVersion = 1u;

    static constexpr const char* aggregateDiagnostic() noexcept
    {
        return "RHI-CONFORMANCE-AGGREGATE-EVIDENCE-UNAUTHENTICATED";
    }

    static constexpr const char* descriptorDiagnostic(Backend backend) noexcept
    {
        const auto* identity = identityDiagnostic(backend);
        return identity ? identity : "RHI-CONFORMANCE-DESCRIPTOR-EVIDENCE-UNAUTHENTICATED";
    }

    static constexpr const char* runtimeDiagnostic(Backend backend) noexcept
    {
        const auto* identity = identityDiagnostic(backend);
        return identity ? identity : "RHI-CONFORMANCE-RUNTIME-WORKLOAD-UNIMPLEMENTED";
    }

    static constexpr const char* platformDiagnostic(Backend backend) noexcept
    {
        const auto* identity = identityDiagnostic(backend);
        return identity ? identity : "RHI-CONFORMANCE-PLATFORM-EVIDENCE-UNAUTHENTICATED";
    }

private:
    static constexpr const char* identityDiagnostic(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return nullptr;
        case Backend::WebGL:
            return "RHI-WEBGL1-CONFORMANCE-UNAVAILABLE";
        }
        return "RHI-CONFORMANCE-BACKEND-UNKNOWN";
    }
};

} // namespace arp::rhi::semantics

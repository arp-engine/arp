#pragma once

#include "rhi/gpu_common.h"

#include <array>

namespace arp::rhi::semantics {

struct SemanticReportBackendDecision {
    bool knownIdentity = false;
    bool inspectStructuralContract = false;
    bool validatorOnly = false;
    bool executable = false;
    const char* diagnostic = "RHI-SEMANTIC-REPORT-BACKEND-UNKNOWN";
    const char* label = "Unknown";
};

// SemanticReportBackendAdmission/1 is CPU metadata admission only. A known
// backend may inspect its existing structural contract; it gains no capability,
// native payload, completion, conformance or execution authority from this table.
class SemanticReportBackendAdmission final {
public:
    static constexpr std::uint32_t contractVersion = 1u;
    inline static constexpr std::array backends{
        Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU, Backend::WebGL};

    static constexpr SemanticReportBackendDecision evaluate(
        Backend backend, std::uint32_t version = contractVersion) noexcept
    {
        if (version != contractVersion)
            return {false, false, false, false, "RHI-SEMANTIC-REPORT-CONTRACT-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {true, true, true, false, nullptr, "Null"};
        case Backend::Vulkan:
            return {true, true, false, false, nullptr, "Vulkan"};
        case Backend::Metal:
            return {true, true, false, false, nullptr, "Metal"};
        case Backend::WebGPU:
            return {true, true, false, false, nullptr, "WebGPU"};
        case Backend::WebGL:
            return {true, false, false, false, "RHI-WEBGL1-SEMANTIC-REPORT-UNAVAILABLE", "WebGL"};
        }
        return {};
    }
};

} // namespace arp::rhi::semantics

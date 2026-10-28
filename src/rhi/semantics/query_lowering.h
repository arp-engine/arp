#pragma once
#include "core/text/utf8.h"
#include "rhi/gpu_common.h"

#include <cmath>
#include <string_view>

namespace arp::rhi::semantics {
// Versioned value-only decisions. Template results preserve existing public decision types.
// Ordinary Vulkan host-reset lowering /1. These are queried native facts and
// actual device enablement, not caller intent or original query authority.
struct VulkanTimestampQueryFacts {
    bool propertiesQueried = false, apiCore12 = false, featuresQueried = false;
    bool hostResetSupported = false, enablementReported = false, hostResetEnabled = false;
    bool queueVerified = false, entrypointsAvailable = false;
    std::uint32_t timestampValidBits = 0u;
    double timestampPeriodNanoseconds = 0.0;
};

struct OriginalQueryCreationFacts {
    bool timestampFeature = false, create = false, pushScope = false, popScope = false;
    bool countAccess = false, typeAccess = false, destroy = false;
    VulkanTimestampQueryFacts vulkanTimestamp{};
    const char* vulkanRuntimeDiagnostic = "RHI-VULKAN-TIMESTAMP-RUNTIME-UNAVAILABLE";
};

class QueryLowering final {
public:
    // MixedPassTimingRecording/1 requires Preparation8 or independently opted-in
    // Preparation9 (timing plus opaque-depth subdomain), canonical Inputs11,
    // one ordinary Program/meter/journal, real same-device slots and completion.
    // This does not promote the pair-only projection or query-only recording.
    static constexpr const char* mixedPassTimingRecording(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Vulkan:
            return nullptr;
        case Backend::Null:
            return "RHI-MIXED-PASS-TIMING-VALIDATOR-ONLY";
        case Backend::Metal:
        case Backend::WebGPU:
            return "RHI-MIXED-PASS-TIMING-NATIVE-PAYLOAD-UNAVAILABLE";
        case Backend::WebGL:
            return "RHI-WEBGL1-QUERY-UNSUPPORTED";
        }
        return "RHI-MIXED-PASS-TIMING-BACKEND";
    }

    // OriginalPassTiming/1 authenticates pair placement only. Mixed native
    // recording must join the original Program and its sole journal first.
    template <class Result>
    static constexpr Result OriginalPassTiming_lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {true, true, false, "RHI-PASS-TIMING-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {false, true, false, "RHI-PASS-TIMING-NATIVE-JOIN-UNAVAILABLE"};
        case Backend::Metal:
        case Backend::WebGPU:
            return {false, true, false, "RHI-PASS-TIMING-NATIVE-PAYLOAD-UNAVAILABLE"};
        case Backend::WebGL:
            return {false, false, false, "RHI-WEBGL1-QUERY-UNSUPPORTED"};
        }
        return {};
    }

    struct OriginalRecording {
        bool validatorOnly = false, privateRecordingAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-QUERY-RECORDING-BACKEND";
    };

    static constexpr OriginalRecording originalRecording(
        Backend backend, std::uint32_t version, bool optIn) noexcept
    {
        if (version != 1u)
            return {false, false, "RHI-ORIGINAL-QUERY-RECORDING-VERSION"};
        if (!optIn)
            return {false, false, "RHI-ORIGINAL-QUERY-RECORDING-OPT-IN"};
        switch (backend) {
        case Backend::Null:
            return {true, false, "RHI-ORIGINAL-QUERY-RECORDING-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {false, true, nullptr};
        case Backend::Metal:
        case Backend::WebGPU:
            return {false, false, "RHI-ORIGINAL-QUERY-RECORDING-NATIVE-UNAVAILABLE"};
        case Backend::WebGL:
            return {false, false, "RHI-WEBGL1-QUERY-UNSUPPORTED"};
        }
        return {};
    }

    template <class Result>
    static constexpr Result OriginalQueryStorage_lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {true, false, false, "RHI-ORIGINAL-QUERY-STORAGE-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {false, true, false, "RHI-ORIGINAL-QUERY-STORAGE-EXECUTION-UNAVAILABLE"};
        case Backend::Metal:
        case Backend::WebGPU:
            return {false, false, false, "RHI-ORIGINAL-QUERY-STORAGE-NATIVE-UNAVAILABLE"};
        case Backend::WebGL:
            return {false, false, false, "RHI-WEBGL1-QUERY-UNSUPPORTED"};
        }
        return {};
    }

    struct TimestampDuration {
        std::uint64_t nanoseconds = 0u;
        const char* diagnostic = nullptr;
    };

    // A wrapped counter has no proven elapsed interval in this prerequisite.
    // Reject it rather than guessing a wrap count or publishing a zero duration.
    static TimestampDuration timestampDuration(
        std::uint64_t begin, std::uint64_t end, double periodNanoseconds) noexcept
    {
        if (!std::isfinite(periodNanoseconds) || periodNanoseconds <= 0.0)
            return {0u, "RHI-TIMESTAMP-DURATION-PERIOD"};
        if (end < begin)
            return {0u, "RHI-TIMESTAMP-DURATION-NONMONOTONIC"};
        const double duration = static_cast<double>(end - begin) * periodNanoseconds;
        // 2^64 is exactly representable; UINT64_MAX rounds up to it as a double.
        if (!std::isfinite(duration) || duration >= std::ldexp(1.0, 64))
            return {0u, "RHI-TIMESTAMP-DURATION-OVERFLOW"};
        return {static_cast<std::uint64_t>(duration), nullptr};
    }

    static const char* vulkanTimestampDiagnostic(const VulkanTimestampQueryFacts& f) noexcept
    {
        if (!f.propertiesQueried)
            return "RHI-VULKAN-TIMESTAMP-PROPERTIES-QUERY";
        if (!f.apiCore12)
            return "RHI-VULKAN-TIMESTAMP-CORE12-REQUIRED";
        if (!f.featuresQueried)
            return "RHI-VULKAN-TIMESTAMP-FEATURE-QUERY";
        if (!f.enablementReported)
            return "RHI-VULKAN-TIMESTAMP-ENABLEMENT-MISSING";
        if (!f.hostResetSupported)
            return "RHI-VULKAN-TIMESTAMP-HOST-RESET-UNSUPPORTED";
        if (!f.hostResetEnabled)
            return "RHI-VULKAN-TIMESTAMP-HOST-RESET-DISABLED";
        if (!f.queueVerified || !f.timestampValidBits || f.timestampValidBits > 64u)
            return "RHI-VULKAN-TIMESTAMP-QUEUE-UNAVAILABLE";
        if (!std::isfinite(f.timestampPeriodNanoseconds) || f.timestampPeriodNanoseconds <= 0.0)
            return "RHI-VULKAN-TIMESTAMP-PERIOD-INVALID";
        return f.entrypointsAvailable ? nullptr : "RHI-VULKAN-TIMESTAMP-ENTRYPOINTS";
    }

    template <class Result>
    static constexpr Result QueryCommandPoints_lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {true, true, "RHI-QUERY-POINTS-VALIDATOR-ONLY"};
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return {true, false, "RHI-QUERY-POINTS-NATIVE-UNPROVEN"};
        case Backend::WebGL:
            return {false, false, "RHI-WEBGL1-QUERY-UNSUPPORTED"};
        }
        return {};
    }

    template <class Result>
    static constexpr Result QueryPoolInputSnapshot_lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {true, true, "RHI-QUERY-ORIGINAL-OWNER-UNPROVEN"};
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return {true, false, "RHI-QUERY-ORIGINAL-OWNER-UNPROVEN"};
        case Backend::WebGL:
            return {false, false, "RHI-WEBGL1-QUERY-UNSUPPORTED"};
        }
        return {};
    }

    template <class Result>
    static constexpr Result QuerySlotDeclarationControl_lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {true, true, "RHI-QUERY-SLOT-VALIDATOR-ONLY"};
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return {false, true, "RHI-QUERY-SLOT-NATIVE-LIFECYCLE-UNIMPLEMENTED"};
        case Backend::WebGL:
            return {false, false, "RHI-WEBGL1-QUERY-UNSUPPORTED"};
        }
        return {};
    }

    struct CreationDecision {
        CapabilityMode mode;
        bool validatorOnly;
        const char* diagnostic;
    };

    static CreationDecision originalCreation(Backend backend,
        QueryType type,
        std::uint32_t count,
        std::string_view label,
        const OriginalQueryCreationFacts& facts,
        std::uint32_t maximumWebGpuQueryCount) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {CapabilityMode::DiagnosticFailure, true, "RHI-QUERY-CREATION-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            if (type != QueryType::Timestamp)
                return {CapabilityMode::Degraded,
                    false,
                    "RHI-VULKAN-ORIGINAL-QUERY-STATISTICS-UNSUPPORTED"};
            if (label.find('\0') != std::string_view::npos || core::Utf8Decoder::validate(label))
                return {CapabilityMode::DiagnosticFailure, false, "RHI-QUERY-CREATION-LABEL"};
            if (facts.vulkanRuntimeDiagnostic)
                return {CapabilityMode::DiagnosticFailure, false, facts.vulkanRuntimeDiagnostic};
            if (const auto* diagnostic = vulkanTimestampDiagnostic(facts.vulkanTimestamp))
                return {CapabilityMode::DiagnosticFailure, false, diagnostic};
            return {CapabilityMode::Native, false, nullptr};
        case Backend::Metal:
            return {CapabilityMode::DiagnosticFailure,
                false,
                "RHI-QUERY-CREATION-NATIVE-UNIMPLEMENTED"};
        case Backend::WebGL:
            return {CapabilityMode::DiagnosticFailure, false, "RHI-WEBGL1-QUERY-UNSUPPORTED"};
        case Backend::WebGPU:
            if (type != QueryType::Timestamp)
                return {CapabilityMode::Degraded,
                    false,
                    "RHI-WEBGPU-ORIGINAL-QUERY-STATISTICS-UNSUPPORTED"};
            if (label.find('\0') != std::string_view::npos || core::Utf8Decoder::validate(label))
                return {CapabilityMode::DiagnosticFailure, false, "RHI-QUERY-CREATION-LABEL"};
            if (count > maximumWebGpuQueryCount)
                return {
                    CapabilityMode::DiagnosticFailure, false, "RHI-WEBGPU-QUERY-CREATION-COUNT"};
            if (!facts.timestampFeature)
                return {CapabilityMode::Degraded, false, "RHI-WEBGPU-QUERY-CREATION-FEATURE"};
            if (!facts.create || !facts.pushScope || !facts.popScope || !facts.countAccess
                || !facts.typeAccess || !facts.destroy)
                return {CapabilityMode::Degraded, false, "RHI-WEBGPU-QUERY-CREATION-API"};
            return {CapabilityMode::Native, false, nullptr};
        }
        return {CapabilityMode::DiagnosticFailure, false, "RHI-QUERY-CREATION-BACKEND"};
    }
};
} // namespace arp::rhi::semantics

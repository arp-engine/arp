#pragma once

#include "rhi/conformance/original_output.h"

namespace arp::rhi {
class GpuDevice;
class GpuTexture;
class CommandEncoder;
class CommandBuffer;
class GpuFence;
struct CommandEncoderDesc;
struct QueueSubmitResult;

struct ConformanceClearExecutionRequest {
    OriginalConformanceRequest observation;
    std::uint64_t maxNativeReadbackBytes = 0;
    std::uint64_t completionTimeoutNanoseconds = 0;
};

enum class ConformanceSubmissionCompletion { ExternalFence, IssuerOwnedColdObservation };

class ConformanceClearExecutionContract final {
public:
    static const char* validate(const ConformanceClearExecutionRequest& request) noexcept
    {
        if (const auto* id = OriginalConformanceContract::validate(request.observation))
            return id;
        if (!request.maxNativeReadbackBytes || !request.completionTimeoutNanoseconds
            || request.completionTimeoutNanoseconds > INT64_MAX)
            return "RHI-CONFORMANCE-CLEAR-NATIVE-BUDGET";
        return nullptr;
    }

    static const char* loweringDiagnostic(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Vulkan:
        case Backend::WebGPU:
            return nullptr;
        case Backend::Null:
            return "RHI-CONFORMANCE-NULL-VALIDATOR-NO-OUTPUT";
        case Backend::Metal:
            return "RHI-CONFORMANCE-METAL-ORIGINAL-OUTPUT-JOIN-UNAVAILABLE";
        case Backend::WebGL:
            return "RHI-CONFORMANCE-CLEAR-ORDERED-ENTRY-REQUIRED";
        default:
            return "RHI-CONFORMANCE-ORIGINAL-BACKEND-UNKNOWN";
        }
    }
};

// Implementing this interface cannot create a sealed OriginalConformanceRun.
// Only original backend issuers can construct and mutate that read-only result.
class OriginalConformanceClearExecution {
public:
    virtual ~OriginalConformanceClearExecution() = default;
    virtual const char* diagnostic(const GpuDevice&) const noexcept = 0;

    virtual ConformanceSubmissionCompletion submissionCompletion() const noexcept
    {
        return ConformanceSubmissionCompletion::ExternalFence;
    }

    virtual core::UniquePtr<CommandEncoder> prepare(
        const CommandEncoderDesc&, GpuTexture&, const char*&) = 0;
    virtual QueueSubmitResult submit(const CommandBuffer&, GpuFence*) = 0;
    virtual const char* finalizePresented() noexcept = 0;
    virtual const OriginalConformanceRun* run() const noexcept = 0;
};
} // namespace arp::rhi

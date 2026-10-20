#pragma once

#include "core/cpu_allocator.h"
#include "rhi/gpu_common.h"

#include <cstdint>

namespace arp::rhi {

/// Tool-neutral L2 request for an offline GPU frame artifact. Native handles,
/// tool sessions, and platform objects remain backend-private.
enum class FrameCaptureTool : std::uint8_t {
    /// A backend-owned native offline trace artifact. The caller never names
    /// a backend, native trace document type, or capture session.
    NativeGpuTrace,
};

enum class FrameCaptureState : std::uint8_t {
    Unsupported,
    Ready,
    Capturing,
    Completed,
    DiagnosticFailure,
};

struct FrameCaptureRequest {
    FrameCaptureTool tool = FrameCaptureTool::NativeGpuTrace;
    /// An absolute, caller-owned output destination. The capture service never
    /// creates a fallback destination or silently redirects an artifact.
    std::pmr::string outputPath;
};

struct FrameCaptureCapability {
    Backend backend = Backend::Null;
    FrameCaptureTool tool = FrameCaptureTool::NativeGpuTrace;
    CapabilityMode mode = CapabilityMode::DiagnosticFailure;
    bool available = false;
    std::pmr::string diagnostics;
};

struct FrameCaptureResult {
    FrameCaptureState state = FrameCaptureState::DiagnosticFailure;
    std::pmr::string diagnostics;

    bool success() const noexcept
    {
        return state == FrameCaptureState::Ready || state == FrameCaptureState::Capturing
            || state == FrameCaptureState::Completed;
    }
};

inline FrameCaptureCapability unavailableFrameCaptureCapability(
    const Backend backend, const FrameCaptureTool tool, const char* diagnostics)
{
    FrameCaptureCapability capability;
    capability.backend = backend;
    capability.tool = tool;
    capability.mode = CapabilityMode::DiagnosticFailure;
    capability.diagnostics = diagnostics;
    return capability;
}

inline FrameCaptureResult unavailableFrameCaptureResult(const char* diagnostics)
{
    FrameCaptureResult result;
    result.state = FrameCaptureState::DiagnosticFailure;
    result.diagnostics = diagnostics;
    return result;
}

} // namespace arp::rhi

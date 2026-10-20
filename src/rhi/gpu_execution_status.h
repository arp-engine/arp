#pragma once

#include "core/cpu_allocator.h"

#include <cstdint>
#include <string>

namespace arp::rhi {

// These two enums are defined by gpu_capabilities.h.  They are forward
// declared here so the shared DeviceCapabilities model can be included from
// gpu_common.h without introducing a header cycle.
enum class EngineFeature;
enum class FallbackMode;
enum class QueryType : std::uint32_t;

/// Backend-neutral push-constant execution result.
enum class PushConstantLoweringOutcome {
    Native,
    EquivalentLowering,
    PrototypeLegacy,
    DiagnosticFailure
};

/// Owner that keeps a push update valid until its consuming command.
/// Unknown is never executable.
enum class PushConstantLifetimeOwner {
    Unknown,
    NativeCommandBuffer,
    NativeRenderEncoder,
    FrameUploadRing,
    ReferenceValidator
};

/// Explicit policy for a push-constant lowering. WebGPU's current executable
/// path is B2: one backend-owned uniform-ring bind group per pipeline and one
/// dynamic offset per atomic update. B1 is retained as a historical policy
/// value so reports can distinguish old artifacts from the current lowering.
enum class PushConstantLoweringPolicy {
    NotApplicable,
    B1PerPushBindGroupUniformRing,
    B2ExplicitLayoutDynamicOffset
};

inline constexpr std::uint32_t kNoPhysicalPushConstantBinding = 0xffffffffu;

/// Observable execution state selected by the shared fallback planner.
/// Native handles and backend vocabulary remain private to the backend.
struct EngineFeatureExecutionStatus {
    EngineFeature feature = static_cast<EngineFeature>(0);
    FallbackMode mode = static_cast<FallbackMode>(0);
    bool executable = false;
    bool upperLayerVisible = false;
    bool telemetryRequired = false;
    std::pmr::string diagnostics;
};

enum class QueryExecutionState : std::uint8_t {
    Unsupported,
    NotRecorded,
    Recorded,
    Pending,
    Available,
    DiagnosticFailure
};

/// Shared lifecycle/reporting surface for optional GPU query lowerings.
/// Native query objects remain backend-owned; this status only exposes the
/// L2 state machine and whether completion-gated asynchronous readback is
/// available.
struct QueryExecutionStatus {
    QueryType type = static_cast<QueryType>(0);
    QueryExecutionState state = QueryExecutionState::Unsupported;
    bool creationSupported = false;
    bool resetSupported = false;
    bool asynchronousReadbackSupported = false;
    bool completionGatedLifetimeRequired = false;
    std::uint32_t count = 0u;
    std::pmr::string diagnostics;
};

/// Bounded aggregate used by capability/frame reports. It contains counts
/// only; backend handles and backend-specific status vocabulary never cross
/// the RHI boundary.
struct ExecutionStatusTelemetrySummary {
    std::uint32_t nativeFeatureCount = 0u;
    std::uint32_t equivalentFeatureCount = 0u;
    std::uint32_t cpuReplayFeatureCount = 0u;
    std::uint32_t degradedFeatureCount = 0u;
    std::uint32_t diagnosticFailureFeatureCount = 0u;
    std::uint32_t unsupportedQueryCount = 0u;
    std::uint32_t notRecordedQueryCount = 0u;
    std::uint32_t recordedQueryCount = 0u;
    std::uint32_t pendingQueryCount = 0u;
    std::uint32_t availableQueryCount = 0u;
    std::uint32_t diagnosticFailureQueryCount = 0u;
};

} // namespace arp::rhi

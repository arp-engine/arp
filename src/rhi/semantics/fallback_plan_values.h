#pragma once

#include "core/memory/cpu_allocator.h"
#include "rhi/gpu_capabilities.h"

#include <cstdint>
#include <string>
#include <vector>

namespace arp::rhi {

constexpr std::uint32_t kMaxEngineFallbackDecisions = 1024;

enum class FeatureCorrectnessImpact { PreservesCorrectness, PerformanceOnly, ChangesCorrectness };

struct EngineFallbackPlannerDesc {
    Backend backend = Backend::Null;
    std::uint32_t logicalFrameSlots = 2;
    std::uint32_t maxFeatureDecisions = 0;
};

struct EngineFeatureDecisionInput {
    EngineFeature feature = EngineFeature::PushConstants;
    FallbackMode mode = FallbackMode::Native;
    FeatureCorrectnessImpact correctnessImpact = FeatureCorrectnessImpact::PreservesCorrectness;
    std::pmr::string reason;
};

struct EngineFeatureRequirement {
    EngineFeature feature = EngineFeature::PushConstants;
    FeatureCorrectnessImpact correctnessImpact = FeatureCorrectnessImpact::PreservesCorrectness;
    std::pmr::string reason;
};

struct EngineFeatureRequirementSet {
    std::uint32_t logicalFrameSlots = 2;
    std::uint32_t maxFeatureDecisions = 0;
    std::pmr::vector<EngineFeatureRequirement> features;
};

struct EngineFeaturePlanResult {
    bool success = false;
    EngineFeature feature = EngineFeature::PushConstants;
    FallbackMode mode = FallbackMode::Native;
    bool upperLayerVisible = false;
    bool telemetryRequired = false;
    /// ExtensionOptIn is never enabled by the base plan.  A caller must use
    /// the feature-specific extension contract to opt in explicitly.
    bool explicitOptInRequired = false;
    std::pmr::string diagnostics;
};

struct EngineFallbackPlan {
    bool success = false;
    Backend backend = Backend::Null;
    std::uint32_t logicalFrameSlots = 2;
    std::uint32_t nativeFeatureCount = 0;
    std::uint32_t emulatedFeatureCount = 0;
    std::uint32_t cpuReplayFeatureCount = 0;
    std::uint32_t degradedFeatureCount = 0;
    std::uint32_t diagnosticFailureCount = 0;
    bool upperLayerVisibleFallbacks = false;
    bool backendFallbacksHiddenFromUpperLayer = true;
    bool telemetryRequired = false;
    std::pmr::string diagnostics;
    std::pmr::vector<EngineFeaturePlanResult> decisions;
    /// Complete observable execution state, including diagnostic failures
    /// that are intentionally excluded from decisions.
    std::pmr::vector<EngineFeatureExecutionStatus> executionStatuses;

    /// Publish a structurally consistent plan. This legacy value API does not
    /// authenticate native query/payload provenance; device execution gates
    /// remain authoritative. Public renderer callers must use restrictions.
    inline void applyTo(DeviceCapabilities& caps) const { applyToImpl(caps, false); }

    /// Intersect the requested features with the existing capability snapshot.
    /// Cannot grant a base capability or a degraded-subpass execution path.
    inline void applyRestrictionsTo(DeviceCapabilities& caps) const { applyToImpl(caps, true); }

private:
    void applyToImpl(DeviceCapabilities& caps, bool restrictionsOnly) const;
};

const char* engineFeatureName(EngineFeature feature);
const char* fallbackModeName(FallbackMode mode);

} // namespace arp::rhi

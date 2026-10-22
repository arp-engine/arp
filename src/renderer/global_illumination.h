#pragma once

#include "core/memory/cpu_allocator.h"
#include "renderer/mobile_quality.h"
#include "rhi/gpu_common.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace arp::renderer {

enum class GiFallbackMode { AmbientCubeAndOcclusion, AmbientCubeOnly };

enum class DdgiTraceBackend { SoftwareBvh, HardwareRayQuery };

enum class DdgiCapabilityMode { Native, Equivalent, Degraded, DiagnosticFailure };

// Stable runtime labels used by mobile telemetry and platform validation.
// These are separate from DdgiCapabilityMode because a capable device can be
// paused by surface lifecycle or degraded by memory pressure.
enum class GiRuntimeStatus {
    Disabled,
    Enabled,
    Paused,
    DegradedCapability,
    DegradedMemory,
    DiagnosticFailure
};

enum class DdgiProbeClassification : std::uint32_t { Invalid, Active, Inactive };

struct ProbeGridDesc {
    uint32_t probeCountX = 0;
    uint32_t probeCountY = 0;
    uint32_t probeCountZ = 0;
    float spacingX = 8.0f;
    float spacingY = 8.0f;
    float spacingZ = 8.0f;

    uint32_t totalProbeCount() const { return probeCountX * probeCountY * probeCountZ; }
};

struct DdgiVolumeBounds {
    std::array<float, 3> minimum{};
    std::array<float, 3> maximum{};

    bool valid() const noexcept
    {
        for (std::size_t axis = 0u; axis < 3u; ++axis) {
            if (!std::isfinite(minimum[axis]) || !std::isfinite(maximum[axis])
                || maximum[axis] <= minimum[axis]) {
                return false;
            }
        }
        return true;
    }
};

struct DdgiVolumePlacement {
    std::array<float, 3> origin{};
    std::array<float, 3> spacing{};
    bool valid = false;

    bool contains(const std::array<float, 3>& point,
        const ProbeGridDesc& grid,
        float epsilon = 0.001f) const noexcept
    {
        if (!valid)
            return false;
        const std::array<std::uint32_t, 3> count{
            grid.probeCountX,
            grid.probeCountY,
            grid.probeCountZ,
        };
        for (std::size_t axis = 0u; axis < 3u; ++axis) {
            const auto maximum =
                origin[axis] + spacing[axis] * static_cast<float>(count[axis] - 1u);
            if (point[axis] < origin[axis] - epsilon || point[axis] > maximum + epsilon) {
                return false;
            }
        }
        return true;
    }
};

// Fits a fixed probe grid around finite scene bounds. Spacing may grow above
// the requested minimum, but never shrinks below it; any excess span is split
// symmetrically so both bounds remain inside the probe volume. Camera-following
// grids and open-world clipmaps use different placement policies.
inline DdgiVolumePlacement fitDdgiProbeGridToBounds(const ProbeGridDesc& grid,
    const DdgiVolumeBounds& bounds,
    const std::array<float, 3>& minimumSpacing) noexcept
{
    DdgiVolumePlacement placement;
    const std::array<std::uint32_t, 3> count{
        grid.probeCountX,
        grid.probeCountY,
        grid.probeCountZ,
    };
    if (!bounds.valid())
        return placement;
    for (std::size_t axis = 0u; axis < 3u; ++axis) {
        if (count[axis] < 2u || !std::isfinite(minimumSpacing[axis])
            || minimumSpacing[axis] <= 0.0f) {
            return placement;
        }
        const auto boundsSpan = bounds.maximum[axis] - bounds.minimum[axis];
        placement.spacing[axis] =
            std::max(minimumSpacing[axis], boundsSpan / static_cast<float>(count[axis] - 1u));
        const auto probeSpan = placement.spacing[axis] * static_cast<float>(count[axis] - 1u);
        const auto center = (bounds.minimum[axis] + bounds.maximum[axis]) * 0.5f;
        placement.origin[axis] = center - probeSpan * 0.5f;
    }
    placement.valid = true;
    return placement;
}

struct GlobalIlluminationDesc {
    bool enabled = false;
    QualityProfile profile = QualityProfile::Medium;
    ProbeGridDesc probeGrid;
    uint32_t maxProbeUpdatesPerFrame = 0;
    bool persistentHistoryEnabled = false;
    GiFallbackMode fallback = GiFallbackMode::AmbientCubeAndOcclusion;
    DdgiTraceBackend traceBackend = DdgiTraceBackend::SoftwareBvh;
    std::uint32_t raysPerProbe = 96;
    std::uint32_t irradianceInteriorTexels = 6;
    std::uint32_t distanceInteriorTexels = 14;
    float stableHysteresis = 0.97f;
    float changedHysteresis = 0.75f;
    float normalBiasMeters = 0.25f;
    float viewBiasMeters = 0.10f;
    float maxRayDistanceMeters = 120.0f;
    float multiBounceStrength = 0.35f;
    float gpuBudgetMilliseconds = 4.0f;
    bool relocationEnabled = true;
    bool classificationEnabled = true;
};

struct DdgiFrameInput {
    float cameraPosition[3]{};
    float currentViewProjection[16]{};
    float previousViewProjection[16]{};
    float directionalLightDirection[3]{0.0f, 1.0f, 0.0f};
    float directionalLightRadiance[3]{1.0f, 1.0f, 1.0f};
    float skyRadiance[3]{0.0f, 0.0f, 0.0f};
    std::uint64_t geometryRevision = 0;
    std::uint64_t lightingRevision = 0;
    std::uint32_t frameIndex = 0;
    bool historyValid = false;
};

struct DdgiSceneAcceleration {
    std::uint64_t staticGeometryRevision = 0;
    std::uint64_t dynamicGeometryRevision = 0;
    std::uint32_t staticBlasCount = 0;
    std::uint32_t dynamicInstanceCount = 0;
    std::uint32_t tlasNodeCount = 0;
    std::uint64_t residentBytes = 0;
};

struct DdgiTelemetry {
    DdgiCapabilityMode capabilityMode = DdgiCapabilityMode::DiagnosticFailure;
    std::uint32_t activeProbeCount = 0;
    std::uint32_t inactiveProbeCount = 0;
    std::uint32_t invalidProbeCount = 0;
    std::uint32_t updatedProbeCount = 0;
    std::uint32_t newlyExposedProbeCount = 0;
    std::uint32_t oldestProbeAge = 0;
    std::uint64_t tracedRayCount = 0;
    std::uint64_t atlasBytes = 0;
    float averageProbeAge = 0.0f;
    float accelerationBuildMilliseconds = 0.0f;
    float accelerationRefitMilliseconds = 0.0f;
    float accelerationUploadMilliseconds = 0.0f;
    std::uint64_t accelerationUploadBytes = 0u;
    std::uint32_t accelerationChangedNodeCount = 0u;
    std::uint32_t accelerationChangedLeafCount = 0u;
    std::uint32_t accelerationUploadRangeCount = 0u;
    bool accelerationRebuilt = false;
    bool accelerationRefitted = false;
    bool accelerationReused = false;
    float traceMilliseconds = 0.0f;
    float blendMilliseconds = 0.0f;
    float resolveMilliseconds = 0.0f;
    float measuredGpuMilliseconds = 0.0f;
    float gpuBudgetMilliseconds = 0.0f;
    std::uint32_t effectiveProbeBudget = 0u;
    bool gpuTimingAvailable = false;
    bool probeBudgetThrottled = false;
};

// Portable sky contract consumed by DDGI probe tracing and resolve. Values
// are authored by the scene/sky system; the DDGI shader does not own sky
// colors, sun color, halo tuning, or exposure defaults.
struct DdgiSkyEnvironmentDesc {
    // Fail dark until the host scene supplies its live environment. A default
    // daylight sky here hides missing frame wiring as plausible but incorrect
    // indirect light, especially in black-furnace and night scenes.
    float horizonRadiance[3]{0.0f, 0.0f, 0.0f};
    float zenithRadiance[3]{0.0f, 0.0f, 0.0f};
    float sunRadiance[3]{0.0f, 0.0f, 0.0f};
    float sunDirection[3]{0.0f, 1.0f, 0.0f};
    float glowPower = 6.0f;
    float glowStrength = 2.5f;
    float corePower = 45.0f;
    float coreStrength = 2.8f;
    float horizonPower = 0.6f;
    float preExposure = 1.0f;
};

// Bounded local-light contract consumed by DDGI probe tracing. The scene owns
// the source list; DDGI stores only the fixed-size GPU representation and a
// per-probe influence index list.
struct DdgiLocalLightDesc {
    float position[3]{0.0f, 0.0f, 0.0f};
    float range = 0.0f;
    float radiance[3]{1.0f, 1.0f, 1.0f};
    float intensity = 0.0f;
};

inline bool isValidDdgiLocalLight(const DdgiLocalLightDesc& light) noexcept
{
    const float rangeSquared = light.range * light.range;
    return std::isfinite(light.position[0]) && std::isfinite(light.position[1])
        && std::isfinite(light.position[2]) && std::isfinite(light.range) && light.range > 0.0f
        && std::isfinite(rangeSquared) && std::isfinite(light.radiance[0])
        && std::isfinite(light.radiance[1]) && std::isfinite(light.radiance[2])
        && light.radiance[0] >= 0.0f && light.radiance[1] >= 0.0f && light.radiance[2] >= 0.0f
        && std::isfinite(light.intensity) && light.intensity >= 0.0f;
}

inline bool isValidDdgiEmissiveRadiance(const float radiance[3]) noexcept
{
    return radiance != nullptr && std::isfinite(radiance[0]) && std::isfinite(radiance[1])
        && std::isfinite(radiance[2]) && radiance[0] >= 0.0f && radiance[1] >= 0.0f
        && radiance[2] >= 0.0f;
}

// Sky values cross the CPU/GPU boundary as a strict HDR contract. Reject
// malformed scene data before it can produce undefined normalization or NaN
// radiance in the probe tracing and resolve shaders.
inline bool isValidDdgiSkyEnvironment(const DdgiSkyEnvironmentDesc& environment) noexcept
{
    const auto finiteNonNegative = [](const float value) {
        return std::isfinite(value) && value >= 0.0f;
    };
    for (std::uint32_t channel = 0u; channel < 3u; ++channel) {
        if (!finiteNonNegative(environment.horizonRadiance[channel])
            || !finiteNonNegative(environment.zenithRadiance[channel])
            || !finiteNonNegative(environment.sunRadiance[channel])
            || !std::isfinite(environment.sunDirection[channel])) {
            return false;
        }
    }
    const float directionLengthSquared = environment.sunDirection[0] * environment.sunDirection[0]
        + environment.sunDirection[1] * environment.sunDirection[1]
        + environment.sunDirection[2] * environment.sunDirection[2];
    return std::isfinite(directionLengthSquared) && directionLengthSquared > 1.0e-8f
        && std::isfinite(environment.glowPower) && environment.glowPower > 0.0f
        && finiteNonNegative(environment.glowStrength) && std::isfinite(environment.corePower)
        && environment.corePower > 0.0f && finiteNonNegative(environment.coreStrength)
        && std::isfinite(environment.horizonPower) && environment.horizonPower > 0.0f
        && std::isfinite(environment.preExposure) && environment.preExposure > 0.0f;
}

struct GiFramePlan {
    bool enabled = false;
    std::pmr::vector<std::pmr::string> passNames;
    uint64_t probeStateBytes = 0;
    rhi::Extent2D irradianceExtent;
    GiFallbackMode fallback = GiFallbackMode::AmbientCubeAndOcclusion;
    std::uint32_t probeUpdates = 0;
    std::uint32_t raysPerProbe = 0;
    std::uint32_t irradiancePhysicalTexels = 0;
    std::uint32_t distancePhysicalTexels = 0;
    std::uint64_t tracedRayCount = 0;
};

struct GiMobileRuntimeContext {
    bool frameSubmissionAllowed = false;
    bool surfaceAvailable = false;
    uint64_t memoryBudgetBytes = 0;
    bool memoryPressure = false;
    DdgiCapabilityMode capabilityMode = DdgiCapabilityMode::Native;
    // Set only after a backend-owned hardware ray-query implementation has
    // been probed and its DDGI integration contract is executable. The
    // current renderer ships the software-BVH path, so the default is false.
    bool hardwareRayQueryBackendAvailable = false;
};

struct GiMobileRuntimePlan {
    bool giEnabled = false;
    bool historyUpdateAllowed = false;
    bool preserveHistory = false;
    bool releaseHistory = false;
    uint64_t estimatedProbeStateBytes = 0;
    GiFallbackMode fallback = GiFallbackMode::AmbientCubeAndOcclusion;
    DdgiCapabilityMode capabilityMode = DdgiCapabilityMode::Native;
    GiRuntimeStatus runtimeStatus = GiRuntimeStatus::Disabled;
    std::pmr::string diagnosticId;
    std::pmr::string status;
};

inline GlobalIlluminationDesc globalIlluminationDescForProfile(QualityProfile profile)
{
    GlobalIlluminationDesc desc;
    desc.profile = profile;
    switch (profile) {
    case QualityProfile::Low:
        return desc;
    case QualityProfile::Medium:
        desc.enabled = true;
        desc.probeGrid = {16, 8, 16, 10.0f, 10.0f, 10.0f};
        desc.maxProbeUpdatesPerFrame = 128;
        desc.raysPerProbe = 96;
        break;
    case QualityProfile::High:
        desc.enabled = true;
        desc.probeGrid = {24, 10, 24, 8.0f, 8.0f, 8.0f};
        desc.maxProbeUpdatesPerFrame = 256;
        desc.raysPerProbe = 144;
        desc.gpuBudgetMilliseconds = 3.5f;
        break;
    case QualityProfile::Desktop:
        desc.enabled = true;
        desc.probeGrid = {32, 12, 32, 6.0f, 6.0f, 6.0f};
        desc.maxProbeUpdatesPerFrame = 512;
        desc.raysPerProbe = 192;
        desc.gpuBudgetMilliseconds = 3.0f;
        break;
    }
    desc.persistentHistoryEnabled = desc.enabled;
    return desc;
}

inline GiFramePlan buildGiFramePlan(const GlobalIlluminationDesc& desc, rhi::Extent2D frameExtent)
{
    GiFramePlan plan;
    plan.enabled =
        desc.enabled && desc.probeGrid.totalProbeCount() > 0 && desc.maxProbeUpdatesPerFrame > 0;
    plan.fallback = desc.fallback;
    if (!plan.enabled) {
        return plan;
    }

    plan.passNames = {"GiProbeClassifyRelocate",
        "GiProbeTrace",
        "GiProbeBlendIrradiance",
        "GiProbeBlendDistance",
        "GiProbeBorderUpdate",
        "GiIrradianceResolve"};
    plan.probeStateBytes = static_cast<uint64_t>(desc.probeGrid.totalProbeCount()) * 64u;
    plan.probeUpdates = std::min(desc.maxProbeUpdatesPerFrame, desc.probeGrid.totalProbeCount());
    plan.raysPerProbe = desc.raysPerProbe;
    plan.irradiancePhysicalTexels = desc.irradianceInteriorTexels + 2u;
    plan.distancePhysicalTexels = desc.distanceInteriorTexels + 2u;
    plan.tracedRayCount = static_cast<std::uint64_t>(plan.probeUpdates) * plan.raysPerProbe;
    plan.irradianceExtent = {
        std::max<uint32_t>(frameExtent.width / 2u, 1u),
        std::max<uint32_t>(frameExtent.height / 2u, 1u),
    };
    return plan;
}

inline GiMobileRuntimePlan planGiMobileRuntime(
    const GlobalIlluminationDesc& desc, const GiMobileRuntimeContext& context)
{
    GiMobileRuntimePlan plan;
    plan.fallback = desc.fallback;
    plan.capabilityMode = context.capabilityMode;
    plan.estimatedProbeStateBytes = static_cast<uint64_t>(desc.probeGrid.totalProbeCount()) * 64u;

    if (!desc.enabled || desc.probeGrid.totalProbeCount() == 0
        || desc.maxProbeUpdatesPerFrame == 0) {
        plan.runtimeStatus = GiRuntimeStatus::Disabled;
        plan.status = "disabled";
        return plan;
    }

    // Hardware ray query is an opt-in backend, never an implicit alias for
    // the software BVH. Fail closed when the backend has not supplied an
    // executable implementation and invalidate persistent history so a
    // caller cannot present stale lighting as a valid result.
    if (desc.traceBackend == DdgiTraceBackend::HardwareRayQuery
        && !context.hardwareRayQueryBackendAvailable) {
        plan.capabilityMode = DdgiCapabilityMode::DiagnosticFailure;
        plan.runtimeStatus = GiRuntimeStatus::DiagnosticFailure;
        plan.releaseHistory = desc.persistentHistoryEnabled;
        plan.diagnosticId = "GI-HARDWARE-RAY-QUERY-UNAVAILABLE";
        plan.status = "diagnostic-failure";
        return plan;
    }

    // Degraded and diagnostic capability results are execution gates, not
    // hints. Do not submit a partial workload or treat stale history as valid.
    if (context.capabilityMode == DdgiCapabilityMode::DiagnosticFailure) {
        plan.runtimeStatus = GiRuntimeStatus::DiagnosticFailure;
        plan.releaseHistory = desc.persistentHistoryEnabled;
        plan.diagnosticId = "GI-CAPABILITY-DIAGNOSTIC-FAILURE";
        plan.status = "diagnostic-failure";
        return plan;
    }
    if (context.capabilityMode == DdgiCapabilityMode::Degraded) {
        plan.runtimeStatus = GiRuntimeStatus::DegradedCapability;
        plan.releaseHistory = desc.persistentHistoryEnabled;
        plan.diagnosticId = "GI-CAPABILITY-DEGRADED";
        plan.status = "degraded-capability";
        return plan;
    }

    const bool overBudget = context.memoryBudgetBytes < plan.estimatedProbeStateBytes;
    plan.releaseHistory = desc.persistentHistoryEnabled && (context.memoryPressure || overBudget);
    plan.preserveHistory = desc.persistentHistoryEnabled && !plan.releaseHistory;
    plan.historyUpdateAllowed =
        context.frameSubmissionAllowed && context.surfaceAvailable && !plan.releaseHistory;
    plan.giEnabled = plan.historyUpdateAllowed;

    if (plan.releaseHistory) {
        plan.runtimeStatus = GiRuntimeStatus::DegradedMemory;
        plan.diagnosticId =
            context.memoryPressure ? "GI-MEMORY-PRESSURE" : "GI-MEMORY-BUDGET-EXCEEDED";
        plan.status = "degraded-memory";
    } else if (!context.frameSubmissionAllowed || !context.surfaceAvailable) {
        plan.runtimeStatus = GiRuntimeStatus::Paused;
        plan.status = "paused";
    } else {
        plan.runtimeStatus = GiRuntimeStatus::Enabled;
        plan.status = "enabled";
    }
    return plan;
}

} // namespace arp::renderer

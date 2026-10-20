#include "core/cpu_allocator.h"
#include "renderer/gi/ddgi_renderer.h"
#include "renderer/global_illumination.h"

#include <cassert>
#include <string>

int main()
{
    using namespace arp::renderer;

    const auto low = globalIlluminationDescForProfile(QualityProfile::Low);
    assert(!low.enabled);
    assert(low.fallback == GiFallbackMode::AmbientCubeAndOcclusion);

    const DdgiCsmShadowDesc disabledCsm;
    assert(isValidDdgiCsmShadow(disabledCsm));
    auto invalidCsm = disabledCsm;
    invalidCsm.shadowParams[0] = 1.0f;
    assert(!isValidDdgiCsmShadow(invalidCsm));
    invalidCsm = disabledCsm;
    invalidCsm.cascadeSplits[1] = -1.0f;
    assert(!isValidDdgiCsmShadow(invalidCsm));

    const auto medium = globalIlluminationDescForProfile(QualityProfile::Medium);
    assert(medium.enabled);
    assert(medium.probeGrid.probeCountX == 16);
    assert(medium.probeGrid.probeCountY == 8);
    assert(medium.probeGrid.probeCountZ == 16);
    assert(medium.maxProbeUpdatesPerFrame == 128);
    assert(medium.raysPerProbe == 96);
    assert(medium.probeGrid.spacingX == 10.0f);
    assert(medium.probeGrid.spacingY == 10.0f);
    assert(medium.probeGrid.spacingZ == 10.0f);

    const ProbeGridDesc finiteWorldGrid{
        24u,
        6u,
        24u,
        12.0f,
        6.0f,
        12.0f,
    };
    const DdgiVolumeBounds finiteWorldBounds{
        {-302.0f, -10.0f, -302.0f},
        {302.0f, 58.0f, 302.0f},
    };
    const auto finiteWorldPlacement =
        fitDdgiProbeGridToBounds(finiteWorldGrid, finiteWorldBounds, {12.0f, 6.0f, 12.0f});
    assert(finiteWorldPlacement.valid);
    assert(finiteWorldPlacement.spacing[0] > 26.0f);
    assert(finiteWorldPlacement.spacing[2] > 26.0f);
    assert(finiteWorldPlacement.contains(finiteWorldBounds.minimum, finiteWorldGrid));
    assert(finiteWorldPlacement.contains(finiteWorldBounds.maximum, finiteWorldGrid));
    assert(!finiteWorldPlacement.contains({0.0f, 80.0f, 0.0f}, finiteWorldGrid));

    const auto desktop = globalIlluminationDescForProfile(QualityProfile::Desktop);
    assert(desktop.probeGrid.totalProbeCount() > medium.probeGrid.totalProbeCount());
    assert(desktop.maxProbeUpdatesPerFrame >= medium.maxProbeUpdatesPerFrame);
    assert(desktop.raysPerProbe == 192);

    const auto plan = buildGiFramePlan(medium, {1280, 720});
    assert(plan.enabled);
    assert(plan.passNames.size() == 6);
    assert(plan.passNames[0] == std::pmr::string("GiProbeClassifyRelocate"));
    assert(plan.passNames[5] == std::pmr::string("GiIrradianceResolve"));
    assert(plan.probeStateBytes > 0);
    assert(plan.irradianceExtent.width == 640);
    assert(plan.irradianceExtent.height == 360);
    assert(plan.probeUpdates == 128);
    assert(plan.raysPerProbe == 96);
    assert(plan.tracedRayCount == 12288);
    assert(plan.irradiancePhysicalTexels == 8);
    assert(plan.distancePhysicalTexels == 16);

    const auto activeMobile =
        planGiMobileRuntime(medium, GiMobileRuntimeContext{true, true, 4u * 1024u * 1024u, false});
    assert(activeMobile.giEnabled);
    assert(activeMobile.runtimeStatus == GiRuntimeStatus::Enabled);
    assert(activeMobile.capabilityMode == DdgiCapabilityMode::Native);
    assert(activeMobile.diagnosticId.empty());

    auto hardwareRayQuery = medium;
    hardwareRayQuery.traceBackend = DdgiTraceBackend::HardwareRayQuery;
    const auto unavailableHardwareRayQuery = planGiMobileRuntime(
        hardwareRayQuery, GiMobileRuntimeContext{true, true, 4u * 1024u * 1024u, false});
    assert(!unavailableHardwareRayQuery.giEnabled);
    assert(unavailableHardwareRayQuery.capabilityMode == DdgiCapabilityMode::DiagnosticFailure);
    assert(unavailableHardwareRayQuery.runtimeStatus == GiRuntimeStatus::DiagnosticFailure);
    assert(unavailableHardwareRayQuery.releaseHistory);
    assert(unavailableHardwareRayQuery.diagnosticId
        == std::pmr::string("GI-HARDWARE-RAY-QUERY-UNAVAILABLE"));

    const auto availableHardwareRayQuery = planGiMobileRuntime(hardwareRayQuery,
        GiMobileRuntimeContext{
            true, true, 4u * 1024u * 1024u, false, DdgiCapabilityMode::Native, true});
    assert(availableHardwareRayQuery.giEnabled);
    assert(availableHardwareRayQuery.runtimeStatus == GiRuntimeStatus::Enabled);

    const auto pausedMobile =
        planGiMobileRuntime(medium, GiMobileRuntimeContext{false, true, 4u * 1024u * 1024u, false});
    assert(!pausedMobile.giEnabled);
    assert(pausedMobile.runtimeStatus == GiRuntimeStatus::Paused);
    assert(pausedMobile.preserveHistory);

    const auto degradedMetal = planGiMobileRuntime(medium,
        GiMobileRuntimeContext{
            true, true, 4u * 1024u * 1024u, false, DdgiCapabilityMode::Degraded});
    assert(!degradedMetal.giEnabled);
    assert(degradedMetal.runtimeStatus == GiRuntimeStatus::DegradedCapability);
    assert(degradedMetal.releaseHistory);
    assert(degradedMetal.diagnosticId == std::pmr::string("GI-CAPABILITY-DEGRADED"));

    const auto diagnosticMetal = planGiMobileRuntime(medium,
        GiMobileRuntimeContext{
            true, true, 4u * 1024u * 1024u, false, DdgiCapabilityMode::DiagnosticFailure});
    assert(!diagnosticMetal.giEnabled);
    assert(diagnosticMetal.runtimeStatus == GiRuntimeStatus::DiagnosticFailure);
    assert(diagnosticMetal.diagnosticId == std::pmr::string("GI-CAPABILITY-DIAGNOSTIC-FAILURE"));

    DdgiFrameInput frameInput;
    frameInput.geometryRevision = 41u;
    frameInput.lightingRevision = 7u;
    assert(frameInput.geometryRevision != frameInput.lightingRevision);
    DdgiTelemetry telemetry;
    telemetry.updatedProbeCount = plan.probeUpdates;
    telemetry.tracedRayCount = plan.tracedRayCount;
    assert(telemetry.updatedProbeCount == 128u);
    assert(telemetry.tracedRayCount == 12288u);

    const auto disabledPlan = buildGiFramePlan(low, {1280, 720});
    assert(!disabledPlan.enabled);
    assert(disabledPlan.passNames.empty());

    return 0;
}

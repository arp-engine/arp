#include "core/algorithm/contiguous_diff.h"
#include "renderer/gi/ddgi_runtime.h"
#include "renderer/global_illumination.h"

#include <cassert>
#include <optional>
#include <string_view>

namespace {

void schedule_changes_only_on_acceptance()
{
    using namespace arp::renderer;
    DdgiProbeScheduler scheduler({2u, 2u, 2u}, 2u);
    DdgiProbeScheduleInput input{};
    const auto first = scheduler.prepare(input);
    const std::array<std::uint32_t, 2> firstIndices{
        (*first.logicalProbeIndices)[0], (*first.logicalProbeIndices)[1]};
    const auto retry = scheduler.prepare(input);
    assert(first.newlyExposedProbeCount == 8u && retry.newlyExposedProbeCount == 8u);
    assert(firstIndices[0] == (*retry.logicalProbeIndices)[0]);
    assert(firstIndices[1] == (*retry.logicalProbeIndices)[1]);
    assert(first.generation != retry.generation);
    assert(!scheduler.commit(first) && !scheduler.discard(first));
    assert(scheduler.commit(retry));
    assert(!scheduler.commit(retry) && !scheduler.discard(retry));
    assert(!scheduler.commit({}));
    const auto afterCommit = scheduler.prepare(input);
    assert(afterCommit.newlyExposedProbeCount == 0u);
    assert(firstIndices[0] != (*afterCommit.logicalProbeIndices)[0]);
    assert(scheduler.discard(afterCommit));
    assert(!scheduler.commit(afterCommit));

    input.frameIndex = 1u;
    input.originCell = {1, 0, 0};
    input.ringOffset = {1, 0, 0};
    input.lightingRevision = 2u;
    input.geometryRevision = 3u;
    const auto scroll = scheduler.prepare(input);
    const std::array<std::uint32_t, 2> scrollIndices{
        (*scroll.logicalProbeIndices)[0], (*scroll.logicalProbeIndices)[1]};
    assert(scroll.newlyExposedProbeCount == 4u);
    assert(scheduler.discard(scroll));
    const auto scrollRetry = scheduler.prepare(input);
    assert(scrollRetry.newlyExposedProbeCount == scroll.newlyExposedProbeCount);
    assert(scrollRetry.oldestProbeAge == scroll.oldestProbeAge);
    assert(scrollRetry.averageProbeAge == scroll.averageProbeAge);
    assert(scrollIndices[0] == (*scrollRetry.logicalProbeIndices)[0]);
    assert(scrollIndices[1] == (*scrollRetry.logicalProbeIndices)[1]);
    assert(scheduler.commit(scrollRetry));

    // Bounded budget changes and all transaction operations allocate nothing.
    const auto before = arp::core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i != 100; ++i) {
        input.frameIndex = i + 2u;
        scheduler.setUpdateBudget(i % 10u);
        const auto candidate = scheduler.prepare(input);
        if (i % 10u == 0u) {
            assert(candidate.logicalProbeIndices->empty());
            assert(!scheduler.commit(candidate));
        } else if (i % 2u == 0u) {
            assert(scheduler.commit(candidate));
        } else {
            assert(scheduler.discard(candidate));
        }
    }
    const auto after = arp::core::defaultCpuAllocator().stats();
    assert(after.allocationCount == before.allocationCount);
    assert(after.deallocationCount == before.deallocationCount);

    DdgiProbeScheduler exhausted({2u, 2u, 2u}, 2u, UINT64_MAX);
    const auto last = exhausted.prepare({});
    assert(last.generation == UINT64_MAX && exhausted.commit(last));
    const auto unavailable = exhausted.prepare({});
    assert(unavailable.generation == 0u && unavailable.logicalProbeIndices->empty());
    assert(!exhausted.hasRemainingGenerations() && !exhausted.commit(last));

    // Even same-address object recreation must not revive an old receipt.
    std::optional<DdgiProbeScheduler> reused;
    reused.emplace(std::array<std::uint32_t, 3>{2u, 2u, 2u}, 2u);
    const auto oldDomain = reused->prepare({});
    reused.reset();
    reused.emplace(std::array<std::uint32_t, 3>{2u, 2u, 2u}, 2u);
    const auto newDomain = reused->prepare({});
    assert(oldDomain.ownerId != newDomain.ownerId);
    assert(!reused->commit(oldDomain) && !reused->discard(oldDomain));
    assert(reused->commit(newDomain));
}

void spatial_history_changes_only_on_acceptance()
{
    using namespace arp::renderer;
    DdgiSpatialHistory history({4u, 2u, 4u});
    DdgiSpatialHistoryInput input{};
    input.spacing = {1.0f, 1.0f, 1.0f};
    input.lighting.fill(1.0f);

    auto initial = history.prepare(input);
    assert(initial.success && initial.candidate.has_value());
    assert(initial.candidate->gridScrolled);
    assert(initial.candidate->spacing == input.spacing);
    assert(initial.candidate->lightingRevision == 2u);
    assert(history.lightingRevision() == 1u);
    assert((history.ringOffset() == std::array<std::int32_t, 3>{0, 0, 0}));
    assert(history.commit(std::move(*initial.candidate)));
    assert(history.lightingRevision() == 2u);
    assert(history.origin() == input.origin);
    assert(history.lighting() == input.lighting);

    input.origin[0] = 1.0f;
    auto rejected = history.prepare(input);
    assert(rejected.success && rejected.candidate.has_value());
    assert(rejected.candidate->gridScrolled);
    assert(rejected.candidate->scroll.ringOffset[0] == 1);
    assert((history.ringOffset() == std::array<std::int32_t, 3>{0, 0, 0}));
    assert(history.origin()[0] == 0.0f);

    auto retry = history.prepare(input);
    assert(retry.success && retry.candidate.has_value());
    assert(retry.candidate->scroll.ringOffset[0] == 1);
    assert(history.commit(std::move(*retry.candidate)));
    assert(history.ringOffset()[0] == 1);
    assert(history.origin() == input.origin);
    assert(history.lighting() == input.lighting);

    auto stale = history.prepare(input);
    assert(stale.success && stale.candidate.has_value());
    history.reset();
    assert(!history.commit(std::move(*stale.candidate)));
    assert(history.lightingRevision() == 1u);
    assert((history.ringOffset() == std::array<std::int32_t, 3>{0, 0, 0}));

    input.origin[1] = std::numeric_limits<float>::quiet_NaN();
    const auto invalidGrid = history.prepare(input);
    assert(!invalidGrid.success);
    assert(std::string_view(invalidGrid.diagnostic) == "DDGI-SPATIAL-HISTORY-INVALID-GRID");
    input.origin[1] = 0.0f;
    input.lighting[3] = std::numeric_limits<float>::quiet_NaN();
    const auto invalidLighting = history.prepare(input);
    assert(!invalidLighting.success);
    assert(std::string_view(invalidLighting.diagnostic) == "DDGI-SPATIAL-HISTORY-INVALID-LIGHTING");
}

} // namespace

int main()
{
    schedule_changes_only_on_acceptance();
    spatial_history_changes_only_on_acceptance();
    using namespace arp::renderer;

    const DdgiSkyEnvironmentDesc defaultSky;
    assert(isValidDdgiSkyEnvironment(defaultSky));
    // Missing host wiring must fail dark. This is the CPU-side black-furnace
    // contract: with zero local lights/emissive and cleared history, the sky
    // cannot contribute any radiance to probe misses or hit shading.
    for (std::uint32_t channel = 0u; channel < 3u; ++channel) {
        assert(defaultSky.horizonRadiance[channel] == 0.0f);
        assert(defaultSky.zenithRadiance[channel] == 0.0f);
        assert(defaultSky.sunRadiance[channel] == 0.0f);
    }
    auto invalidSky = defaultSky;
    invalidSky.sunDirection[0] = 0.0f;
    invalidSky.sunDirection[1] = 0.0f;
    invalidSky.sunDirection[2] = 0.0f;
    assert(!isValidDdgiSkyEnvironment(invalidSky));
    invalidSky = defaultSky;
    invalidSky.preExposure = 0.0f;
    assert(!isValidDdgiSkyEnvironment(invalidSky));
    invalidSky = defaultSky;
    invalidSky.horizonRadiance[1] = -1.0f;
    assert(!isValidDdgiSkyEnvironment(invalidSky));
    invalidSky = defaultSky;
    invalidSky.horizonPower = 0.0f;
    assert(!isValidDdgiSkyEnvironment(invalidSky));

    const DdgiLocalLightDesc localLight{
        {0.0f, 1.0f, 0.0f},
        4.0f,
        {1.0f, 0.5f, 0.25f},
        3.0f,
    };
    assert(isValidDdgiLocalLight(localLight));
    auto invalidLocalLight = localLight;
    invalidLocalLight.range = 0.0f;
    assert(!isValidDdgiLocalLight(invalidLocalLight));
    invalidLocalLight = localLight;
    invalidLocalLight.radiance[2] = -0.1f;
    assert(!isValidDdgiLocalLight(invalidLocalLight));

    const auto hardwareTraceDesc = globalIlluminationDescForProfile(QualityProfile::Medium);
    assert(hardwareTraceDesc.traceBackend == DdgiTraceBackend::SoftwareBvh);

    const float emissiveRadiance[3]{2.0f, 0.5f, 0.0f};
    assert(isValidDdgiEmissiveRadiance(emissiveRadiance));
    const float invalidEmissiveRadiance[3]{1.0f, -0.1f, 0.0f};
    assert(!isValidDdgiEmissiveRadiance(invalidEmissiveRadiance));

    const auto noTiming = planDdgiProbeUpdateBudget({
        1536u,
        128u,
        0u,
        4.0f,
        0.0f,
        false,
    });
    assert(noTiming.probeUpdates == 128u);
    assert(!noTiming.measurementUsed);

    const auto overBudget = planDdgiProbeUpdateBudget({
        1536u,
        128u,
        100u,
        4.0f,
        8.0f,
        true,
    });
    assert(overBudget.measurementUsed);
    assert(overBudget.probeUpdates == 50u);
    assert(overBudget.throttled);

    const auto underBudget = planDdgiProbeUpdateBudget({
        1536u,
        128u,
        50u,
        4.0f,
        2.0f,
        true,
    });
    assert(underBudget.probeUpdates == 62u);
    assert(underBudget.increased);

    const auto capped = planDdgiProbeUpdateBudget({
        1536u,
        128u,
        120u,
        4.0f,
        1.0f,
        true,
    });
    assert(capped.probeUpdates == 128u);

    const auto invalidTiming = planDdgiProbeUpdateBudget({
        1536u,
        128u,
        62u,
        4.0f,
        -1.0f,
        true,
    });
    assert(invalidTiming.probeUpdates == 62u);
    assert(!invalidTiming.measurementUsed);

    const auto zeroCeiling = planDdgiProbeUpdateBudget({
        1536u,
        0u,
        62u,
        4.0f,
        1.0f,
        true,
    });
    assert(zeroCeiling.probeUpdates == 0u);

    const auto saturatingCeiling = planDdgiProbeUpdateBudget({
        0xffffffffu,
        0xffffffffu,
        0xfffffffeu,
        4.0f,
        1.0f,
        true,
    });
    assert(saturatingCeiling.probeUpdates == 0xffffffffu);

    const DdgiAtlasLayout irradiance{6u, 8u};
    assert(irradiance.valid());
    assert(irradiance.interiorTexel(0.0f) == 1.5f);
    assert(irradiance.interiorTexel(1.0f) == 6.5f);
    assert(irradiance.interiorTexel(-1.0f) == 1.5f);

    const DdgiAtlasLayout distance{14u, 16u};
    assert(distance.valid());
    assert(distance.interiorTexel(1.0f) == 14.5f);

    assert((irradiance.borderSource(0u, 0u) == std::array<std::uint32_t, 2>{6u, 6u}));
    assert((irradiance.borderSource(7u, 0u) == std::array<std::uint32_t, 2>{1u, 6u}));
    assert((irradiance.borderSource(0u, 7u) == std::array<std::uint32_t, 2>{6u, 1u}));
    assert((irradiance.borderSource(7u, 7u) == std::array<std::uint32_t, 2>{1u, 1u}));
    assert((irradiance.borderSource(2u, 0u) == std::array<std::uint32_t, 2>{5u, 1u}));
    assert((irradiance.borderSource(0u, 2u) == std::array<std::uint32_t, 2>{1u, 5u}));
    assert((irradiance.borderSource(3u, 4u) == std::array<std::uint32_t, 2>{3u, 4u}));
    assert((distance.borderSource(5u, 0u) == std::array<std::uint32_t, 2>{10u, 1u}));
    assert((distance.borderSource(15u, 9u) == std::array<std::uint32_t, 2>{14u, 6u}));

    DdgiScrollingVolume volume({16u, 6u, 16u});
    const auto xScroll = volume.scroll({1, 0, 0});
    assert(!xScroll.fullInvalidation);
    assert(xScroll.newlyExposedProbeCount == 6u * 16u);
    assert((volume.physicalCoordinate({0u, 0u, 0u}) == std::array<std::uint32_t, 3>{1u, 0u, 0u}));

    const auto reverse = volume.scroll({-1, 0, 0});
    assert(reverse.ringOffset[0] == 0);
    const auto teleport = volume.scroll({16, 0, 0});
    assert(teleport.fullInvalidation);
    assert(teleport.newlyExposedProbeCount == 16u * 6u * 16u);

    DdgiProbeScheduler scheduler({16u, 6u, 16u}, 128u);
    assert(scheduler.maximumAgeFrames() == 12u);
    std::array<std::uint32_t, 1536> lastScheduled{};
    lastScheduled.fill(0xffffffffu);
    for (std::uint32_t frame = 0u; frame < 12u; ++frame) {
        const auto schedule = scheduler.prepare({frame});
        assert(schedule.logicalProbeIndices != nullptr);
        assert(schedule.logicalProbeIndices->size() == 128u);
        for (const auto probe : *schedule.logicalProbeIndices) {
            assert(probe < lastScheduled.size());
            assert(lastScheduled[probe] != frame);
            lastScheduled[probe] = frame;
        }
        assert(scheduler.commit(schedule));
    }
    for (const auto frame : lastScheduled) {
        assert(frame != 0xffffffffu);
    }

    DdgiProbeScheduleInput scrolled;
    scrolled.frameIndex = 12u;
    scrolled.originCell = {1, 0, 0};
    scrolled.ringOffset = {1, 0, 0};
    const auto scrollSchedule = scheduler.prepare(scrolled);
    assert(scrollSchedule.newlyExposedProbeCount == 6u * 16u);
    assert(scrollSchedule.logicalProbeIndices->size() == 128u);
    assert(scheduler.commit(scrollSchedule));

    scrolled.frameIndex = 13u;
    scrolled.lightingRevision = 1u;
    const auto lightingSchedule = scheduler.prepare(scrolled);
    assert(lightingSchedule.logicalProbeIndices->size() == 128u);
    const auto first = lightingSchedule.logicalProbeIndices->front();
    const auto firstX = first % 16u;
    const auto firstY = (first / 16u) % 6u;
    const auto firstZ = first / (16u * 6u);
    const auto dx = static_cast<int>(firstX * 2u) - 15;
    const auto dy = static_cast<int>(firstY * 2u) - 5;
    const auto dz = static_cast<int>(firstZ * 2u) - 15;
    assert(dx * dx + dy * dy + dz * dz <= 16);
    assert(scheduler.commit(lightingSchedule));

    // Near-camera preference is bounded by the forced-starvation class. Over
    // two starvation windows every stable physical slot must be revisited.
    lastScheduled.fill(0xffffffffu);
    for (std::uint32_t frame = 14u; frame < 62u; ++frame) {
        scrolled.frameIndex = frame;
        const auto schedule = scheduler.prepare(scrolled);
        for (const auto probe : *schedule.logicalProbeIndices) {
            lastScheduled[probe] = frame;
        }
        assert(scheduler.commit(schedule));
    }
    for (const auto frame : lastScheduled) {
        assert(frame != 0xffffffffu);
    }

    DdgiProbeScheduler emptyScheduler({0u, 6u, 16u}, 128u);
    assert(emptyScheduler.maximumAgeFrames() == 0xffffffffu);
    const auto emptySchedule = emptyScheduler.prepare({});
    assert(emptySchedule.logicalProbeIndices != nullptr);
    assert(emptySchedule.logicalProbeIndices->empty());

    scheduler.setUpdateBudget(5000u);
    const auto clampedSchedule = scheduler.prepare({62u});
    assert(clampedSchedule.logicalProbeIndices->size() == lastScheduled.size());

    DdgiScrollingVolume ringBoundary({2u, 2u, 2u});
    const auto negativeScroll = ringBoundary.scroll({-1, -1, -1});
    assert(!negativeScroll.fullInvalidation);
    assert(negativeScroll.newlyExposedProbeCount == 7u);
    assert((
        ringBoundary.physicalCoordinate({0u, 0u, 0u}) == std::array<std::uint32_t, 3>{1u, 1u, 1u}));

    const DdgiProbeGridReference mapping{
        {4u, 3u, 2u},
        {-1, 2, 1},
    };
    assert(mapping.valid());
    assert((mapping.physicalCoordinate({0u, 0u, 0u}) == std::array<std::uint32_t, 3>{3u, 2u, 1u}));
    assert(mapping.linearIndex({0u, 0u, 0u}) == 23u);
    const auto mappingWeights = buildDdgiProbeWeightReference(
        {0.25f, 0.5f, 0.75f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, mapping);
    assert(mappingWeights.count == 8u);
    assert(std::abs(mappingWeights.totalWeight - 1.0f) < 1.0e-6f);
    const auto boundaryWeights = buildDdgiProbeWeightReference(
        {-0.25f, 0.5f, 0.5f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, mapping);
    assert(boundaryWeights.count == 4u);
    assert(std::abs(boundaryWeights.totalWeight - 0.75f) < 1.0e-6f);

    // The resolve uses this bounded trilinear sum as DDGI coverage. Across
    // either volume edge it must fade over exactly one probe cell instead of
    // switching between probe irradiance and an unrelated sky fallback.
    const std::array<float, 6> expectedCoverage{
        0.0f,
        0.25f,
        0.5f,
        0.75f,
        1.0f,
        1.0f,
    };
    for (std::size_t sample = 0u; sample < expectedCoverage.size(); ++sample) {
        const auto x = -1.0f + static_cast<float>(sample) * 0.25f;
        const auto weights = buildDdgiProbeWeightReference(
            {x, 0.5f, 0.5f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, mapping);
        assert(std::abs(weights.totalWeight - expectedCoverage[sample]) < 1.0e-6f);
    }
    for (std::size_t sample = 0u; sample < expectedCoverage.size(); ++sample) {
        const auto x = 4.0f - static_cast<float>(sample) * 0.25f;
        const auto weights = buildDdgiProbeWeightReference(
            {x, 0.5f, 0.5f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, mapping);
        assert(std::abs(weights.totalWeight - expectedCoverage[sample]) < 1.0e-6f);
    }
    assert(ddgiChebyshevWeightReference(2.0f, 4.0f, 1.0f) == 1.0f);
    assert(ddgiChebyshevWeightReference(2.0f, 5.0f, 3.0f) > 0.0f);
    assert(
        ddgiChebyshevWeightReference(2.0f, 5.0f, std::numeric_limits<float>::quiet_NaN()) == 0.0f);
    assert(ddgiVisibilityLeakReductionReference(0.0f) == 0.0f);
    assert(std::abs(ddgiVisibilityLeakReductionReference(0.1f) - 0.025f) < 1.0e-6f);
    assert(ddgiVisibilityLeakReductionReference(0.2f) == 0.2f);
    assert(ddgiVisibilityLeakReductionReference(0.5f) == 0.5f);
    assert(ddgiVisibilityLeakReductionReference(std::numeric_limits<float>::quiet_NaN()) == 0.0f);

    float previousReducedVisibility = 0.0f;
    for (std::uint32_t sample = 0u; sample <= 1000u; ++sample) {
        const auto visibility = static_cast<float>(sample) / 1000.0f;
        const auto reduced = ddgiVisibilityLeakReductionReference(visibility);
        assert(reduced >= previousReducedVisibility);
        assert(reduced >= 0.0f && reduced <= 1.0f);
        previousReducedVisibility = reduced;
    }
    const auto belowVisibilityThreshold = ddgiVisibilityLeakReductionReference(0.2f - 1.0e-5f);
    const auto aboveVisibilityThreshold = ddgiVisibilityLeakReductionReference(0.2f + 1.0e-5f);
    assert(std::abs(belowVisibilityThreshold - aboveVisibilityThreshold) < 1.0e-4f);

    std::array<DdgiResolveSampleReference, 8> resolveSamples{};
    for (std::size_t sample = 0u; sample < resolveSamples.size(); ++sample) {
        resolveSamples[sample] = {
            0.125f,
            sample % 2u == 0u ? 0.03125f : 0.875f,
            1.0f,
            2.0f,
            true,
        };
    }
    // Staggered confidence must change relative probe selection, not the
    // brightness of a spatially uniform irradiance field.
    assert(std::abs(ddgiResolveIrradianceReference(resolveSamples) - 2.0f) < 1.0e-6f);
    resolveSamples[0].active = false;
    resolveSamples[1].active = false;
    assert(std::abs(ddgiResolveIrradianceReference(resolveSamples) - 1.5f) < 1.0e-6f);

    const DdgiAccelerationUpdateInput accelerationBase{
        64u,
        64u,
        10u,
        20u,
        10u,
        20u,
        true,
    };
    assert(planDdgiAccelerationUpdate(accelerationBase) == DdgiAccelerationUpdateKind::Reuse);
    auto boundsChanged = accelerationBase;
    boundsChanged.dynamicGeometryRevision = 21u;
    assert(planDdgiAccelerationUpdate(boundsChanged) == DdgiAccelerationUpdateKind::Refit);
    auto staticTransformChangedAcceleration = accelerationBase;
    staticTransformChangedAcceleration.staticTransformRevision = 4u;
    assert(planDdgiAccelerationUpdate(staticTransformChangedAcceleration)
        == DdgiAccelerationUpdateKind::RefitStaticInstances);
    auto topologyChanged = boundsChanged;
    topologyChanged.staticGeometryRevision = 11u;
    assert(planDdgiAccelerationUpdate(topologyChanged) == DdgiAccelerationUpdateKind::Rebuild);
    auto leafCountChanged = boundsChanged;
    leafCountChanged.triangleCount = 65u;
    assert(planDdgiAccelerationUpdate(leafCountChanged) == DdgiAccelerationUpdateKind::Rebuild);
    auto cleared = accelerationBase;
    cleared.triangleCount = 0u;
    assert(planDdgiAccelerationUpdate(cleared) == DdgiAccelerationUpdateKind::Clear);

    const DdgiAccelerationSplitInput splitBase{
        12u,
        4u,
        12u,
        4u,
        7u,
        3u,
        9u,
        7u,
        3u,
        9u,
        true,
        true,
    };
    assert(planDdgiAccelerationSplit(splitBase) == DdgiAccelerationSplitAction::None);
    auto staticTopologyChanged = splitBase;
    staticTopologyChanged.staticTopologyRevision = 8u;
    assert(hasDdgiAccelerationSplitAction(planDdgiAccelerationSplit(staticTopologyChanged),
        DdgiAccelerationSplitAction::RebuildStaticBlas));
    auto staticTransformChanged = splitBase;
    staticTransformChanged.staticTransformRevision = 4u;
    const auto staticTransformPlan = planDdgiAccelerationSplit(staticTransformChanged);
    assert(hasDdgiAccelerationSplitAction(
        staticTransformPlan, DdgiAccelerationSplitAction::UpdateStaticInstances));
    assert(!hasDdgiAccelerationSplitAction(
        staticTransformPlan, DdgiAccelerationSplitAction::RebuildStaticBlas));
    auto dynamicBoundsChanged = splitBase;
    dynamicBoundsChanged.dynamicBoundsRevision = 10u;
    assert(hasDdgiAccelerationSplitAction(planDdgiAccelerationSplit(dynamicBoundsChanged),
        DdgiAccelerationSplitAction::RefitDynamicTlas));
    auto dynamicCountChanged = splitBase;
    dynamicCountChanged.dynamicProxyCount = 5u;
    const auto dynamicCountPlan = planDdgiAccelerationSplit(dynamicCountChanged);
    assert(hasDdgiAccelerationSplitAction(
        dynamicCountPlan, DdgiAccelerationSplitAction::RebuildDynamicTlas));
    assert(!hasDdgiAccelerationSplitAction(
        dynamicCountPlan, DdgiAccelerationSplitAction::RefitDynamicTlas));
    const auto splitCleared = planDdgiAccelerationSplit({});
    assert(hasDdgiAccelerationSplitAction(splitCleared, DdgiAccelerationSplitAction::Clear));

    const DdgiTerrainMinMaxHierarchyLayout terrainHierarchy;
    assert(terrainHierarchy.valid());
    assert(terrainHierarchy.levelResolution(0u) == 64u);
    assert(terrainHierarchy.levelResolution(6u) == 1u);
    assert(terrainHierarchy.levelOffset(0u) == 2u);
    assert(terrainHierarchy.levelOffset(1u) == 4098u);
    assert(terrainHierarchy.totalRecordCount() == 5463u);

    struct UploadElement {
        std::uint32_t words[2];
    };

    const std::array<UploadElement, 6> previous{{
        {{0u, 0u}},
        {{1u, 1u}},
        {{2u, 2u}},
        {{3u, 3u}},
        {{4u, 4u}},
        {{5u, 5u}},
    }};
    auto current = previous;
    current[1].words[0] = 10u;
    current[2].words[0] = 20u;
    current[5].words[0] = 50u;
    arp::core::pmr::Vector<arp::core::ContiguousDiffRange> ranges(
        &arp::core::defaultCpuMemoryResource());
    const auto diff = arp::core::buildContiguousDiffRanges(
        current.data(), current.size(), previous.data(), previous.size(), 8u, ranges);
    assert(diff.changedElementCount == 3u);
    assert(!diff.fullRangeFallback);
    assert(ranges.size() == 2u);
    assert(ranges[0].firstElement == 1u && ranges[0].elementCount == 2u);
    assert(ranges[1].firstElement == 5u && ranges[1].elementCount == 1u);
    const auto boundedDiff = arp::core::buildContiguousDiffRanges(
        current.data(), current.size(), previous.data(), previous.size(), 1u, ranges);
    assert(boundedDiff.changedElementCount == 3u);
    assert(!boundedDiff.fullRangeFallback);
    assert(boundedDiff.coalescedRanges);
    assert(ranges.size() == 1u);
    assert(ranges[0].firstElement == 1u && ranges[0].elementCount == 5u);
    const auto noHistoryDiff = arp::core::buildContiguousDiffRanges(
        current.data(), current.size(), static_cast<const UploadElement*>(nullptr), 0u, 4u, ranges);
    assert(noHistoryDiff.fullRangeFallback);
    assert(ranges.size() == 1u && ranges[0].elementCount == current.size());
    return 0;
}

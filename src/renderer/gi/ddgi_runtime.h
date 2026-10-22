#pragma once

#include "core/memory/cpu_allocator.h"
#include "core/ownership/monotonic_id_sequence.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace arp::renderer {

enum class DdgiAccelerationUpdateKind { Clear, Rebuild, Refit, RefitStaticInstances, Reuse };

// Contract for the production split acceleration path.  The existing flat
// BVH planner remains the compatibility path; this planner makes the
// static-BLAS/instance-table/dynamic-TLAS ownership boundaries explicit so a
// backend cannot accidentally rebuild static geometry for a transform-only or
// dynamic-bounds update.
enum class DdgiAccelerationSplitAction : std::uint32_t {
    None = 0u,
    Clear = 1u << 0u,
    RebuildStaticBlas = 1u << 1u,
    RebuildDynamicTlas = 1u << 2u,
    UpdateStaticInstances = 1u << 3u,
    RefitDynamicTlas = 1u << 4u,
};

constexpr DdgiAccelerationSplitAction operator|(
    DdgiAccelerationSplitAction lhs, DdgiAccelerationSplitAction rhs) noexcept
{
    return static_cast<DdgiAccelerationSplitAction>(
        static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
}

constexpr bool hasDdgiAccelerationSplitAction(
    DdgiAccelerationSplitAction actions, DdgiAccelerationSplitAction action) noexcept
{
    return (static_cast<std::uint32_t>(actions) & static_cast<std::uint32_t>(action)) != 0u;
}

struct DdgiAccelerationSplitInput {
    std::uint32_t staticPrimitiveCount = 0u;
    std::uint32_t dynamicProxyCount = 0u;
    std::uint32_t cachedStaticPrimitiveCount = 0u;
    std::uint32_t cachedDynamicProxyCount = 0u;
    std::uint32_t staticTopologyRevision = 0u;
    std::uint32_t staticTransformRevision = 0u;
    std::uint32_t dynamicBoundsRevision = 0u;
    std::uint32_t cachedStaticTopologyRevision = 0u;
    std::uint32_t cachedStaticTransformRevision = 0u;
    std::uint32_t cachedDynamicBoundsRevision = 0u;
    bool hasStaticBlas = false;
    bool hasDynamicTlas = false;
};

// Terrain min/max hierarchy layout shared by the CPU builder and DDGI Slang.
// The first two float4 records are metadata; subsequent records are level 0
// cells followed by progressively coarser 2:1 levels. Keeping the arithmetic
// here avoids duplicating offsets in renderer code and shader-side indexing.
struct DdgiTerrainMinMaxHierarchyLayout {
    std::uint32_t baseResolution = 64u;
    std::uint32_t levelCount = 7u; // 64, 32, 16, 8, 4, 2, 1

    constexpr bool valid() const noexcept
    {
        return baseResolution >= 1u && (baseResolution & (baseResolution - 1u)) == 0u
            && levelCount >= 1u && levelCount <= 16u;
    }

    constexpr std::uint32_t levelResolution(std::uint32_t level) const noexcept
    {
        if (level >= levelCount)
            return 0u;
        const auto shift = level >= 31u ? 31u : level;
        return std::max(1u, baseResolution >> shift);
    }

    constexpr std::uint32_t levelOffset(std::uint32_t level) const noexcept
    {
        std::uint32_t offset = 2u; // metadata records
        for (std::uint32_t current = 0u; current < level; ++current) {
            const auto resolution = levelResolution(current);
            offset += resolution * resolution;
        }
        return offset;
    }

    constexpr std::uint32_t totalRecordCount() const noexcept { return levelOffset(levelCount); }
};

inline DdgiAccelerationSplitAction planDdgiAccelerationSplit(
    const DdgiAccelerationSplitInput& input) noexcept
{
    if (input.staticPrimitiveCount == 0u && input.dynamicProxyCount == 0u) {
        return DdgiAccelerationSplitAction::Clear;
    }

    DdgiAccelerationSplitAction actions = DdgiAccelerationSplitAction::None;
    if (input.staticPrimitiveCount != 0u
        && (!input.hasStaticBlas || input.staticPrimitiveCount != input.cachedStaticPrimitiveCount
            || input.staticTopologyRevision != input.cachedStaticTopologyRevision)) {
        actions = actions | DdgiAccelerationSplitAction::RebuildStaticBlas;
    }
    if (input.dynamicProxyCount != 0u
        && (!input.hasDynamicTlas || input.dynamicProxyCount != input.cachedDynamicProxyCount)) {
        actions = actions | DdgiAccelerationSplitAction::RebuildDynamicTlas;
    }
    if (input.staticTransformRevision != input.cachedStaticTransformRevision) {
        actions = actions | DdgiAccelerationSplitAction::UpdateStaticInstances;
    }
    if (input.dynamicBoundsRevision != input.cachedDynamicBoundsRevision
        && input.dynamicProxyCount != 0u && input.dynamicProxyCount == input.cachedDynamicProxyCount
        && input.hasDynamicTlas) {
        actions = actions | DdgiAccelerationSplitAction::RefitDynamicTlas;
    }
    return actions;
}

struct DdgiAccelerationUpdateInput {
    std::uint32_t triangleCount = 0u;
    std::uint32_t cachedLeafCount = 0u;
    std::uint32_t staticGeometryRevision = 0u;
    std::uint32_t dynamicGeometryRevision = 0u;
    std::uint32_t cachedStaticGeometryRevision = 0u;
    std::uint32_t cachedDynamicGeometryRevision = 0u;
    bool hasBuiltTree = false;
    std::uint32_t staticTransformRevision = 0u;
    std::uint32_t cachedStaticTransformRevision = 0u;
};

inline DdgiAccelerationUpdateKind planDdgiAccelerationUpdate(
    const DdgiAccelerationUpdateInput& input) noexcept
{
    if (input.triangleCount == 0u) {
        return DdgiAccelerationUpdateKind::Clear;
    }
    const bool leafCountMatches =
        input.hasBuiltTree && input.cachedLeafCount == input.triangleCount;
    const bool hasRevision = input.staticGeometryRevision != 0u
        || input.dynamicGeometryRevision != 0u || input.staticTransformRevision != 0u;
    if (leafCountMatches && hasRevision
        && input.staticGeometryRevision == input.cachedStaticGeometryRevision
        && input.dynamicGeometryRevision == input.cachedDynamicGeometryRevision
        && input.staticTransformRevision == input.cachedStaticTransformRevision) {
        return DdgiAccelerationUpdateKind::Reuse;
    }
    if (leafCountMatches && input.staticGeometryRevision != 0u
        && input.staticGeometryRevision == input.cachedStaticGeometryRevision
        && input.staticTransformRevision != input.cachedStaticTransformRevision) {
        return DdgiAccelerationUpdateKind::RefitStaticInstances;
    }
    if (leafCountMatches && input.staticGeometryRevision != 0u
        && input.staticGeometryRevision == input.cachedStaticGeometryRevision) {
        return DdgiAccelerationUpdateKind::Refit;
    }
    return DdgiAccelerationUpdateKind::Rebuild;
}

struct DdgiAtlasLayout {
    std::uint32_t interiorTexels = 6u;
    std::uint32_t physicalTexels = 8u;

    bool valid() const noexcept
    {
        return interiorTexels >= 2u && physicalTexels == interiorTexels + 2u;
    }

    float interiorTexel(float octCoordinate) const noexcept
    {
        const float clamped = std::clamp(octCoordinate, 0.0f, 1.0f);
        return 1.5f + clamped * static_cast<float>(interiorTexels - 1u);
    }

    std::array<std::uint32_t, 2> borderSource(std::uint32_t x, std::uint32_t y) const noexcept
    {
        if (!valid() || x >= physicalTexels || y >= physicalTexels) {
            return {x, y};
        }
        const auto last = physicalTexels - 1u;
        if (x == 0u && y == 0u)
            return {interiorTexels, interiorTexels};
        if (x == last && y == 0u)
            return {1u, interiorTexels};
        if (x == 0u && y == last)
            return {interiorTexels, 1u};
        if (x == last && y == last)
            return {1u, 1u};
        if (y == 0u)
            return {physicalTexels - 1u - x, 1u};
        if (y == last)
            return {physicalTexels - 1u - x, interiorTexels};
        if (x == 0u)
            return {1u, physicalTexels - 1u - y};
        if (x == last)
            return {interiorTexels, physicalTexels - 1u - y};
        return {x, y};
    }
};

struct DdgiScrollResult {
    std::array<std::int32_t, 3> ringOffset{};
    std::uint32_t newlyExposedProbeCount = 0u;
    bool fullInvalidation = false;
};

class DdgiScrollingVolume final {
public:
    explicit DdgiScrollingVolume(std::array<std::uint32_t, 3> probeCount) : probeCount_(probeCount)
    {}

    DdgiScrollResult scroll(std::array<std::int32_t, 3> cellDelta) noexcept
    {
        DdgiScrollResult result;
        std::uint64_t overlap = 1u;
        std::uint64_t total = 1u;
        for (std::uint32_t axis = 0u; axis < 3u; ++axis) {
            const auto count = static_cast<std::int32_t>(probeCount_[axis]);
            if (count <= 0) {
                result.fullInvalidation = true;
                result.newlyExposedProbeCount = 0u;
                return result;
            }
            const auto magnitude =
                std::min<std::uint32_t>(static_cast<std::uint32_t>(cellDelta[axis] < 0
                                                ? -static_cast<std::int64_t>(cellDelta[axis])
                                                : cellDelta[axis]),
                    probeCount_[axis]);
            ringOffset_[axis] = positiveModulo(ringOffset_[axis] + cellDelta[axis], count);
            overlap *= probeCount_[axis] - magnitude;
            total *= probeCount_[axis];
            result.fullInvalidation = result.fullInvalidation || magnitude == probeCount_[axis];
        }
        result.ringOffset = ringOffset_;
        result.newlyExposedProbeCount = static_cast<std::uint32_t>(total - overlap);
        return result;
    }

    std::array<std::uint32_t, 3> physicalCoordinate(
        std::array<std::uint32_t, 3> logicalCoordinate) const noexcept
    {
        std::array<std::uint32_t, 3> physical{};
        for (std::uint32_t axis = 0u; axis < 3u; ++axis) {
            physical[axis] = static_cast<std::uint32_t>(positiveModulo(
                static_cast<std::int32_t>(logicalCoordinate[axis]) + ringOffset_[axis],
                static_cast<std::int32_t>(probeCount_[axis])));
        }
        return physical;
    }

    const std::array<std::int32_t, 3>& ringOffset() const noexcept { return ringOffset_; }

private:
    static std::int32_t positiveModulo(std::int32_t value, std::int32_t divisor) noexcept
    {
        return (value % divisor + divisor) % divisor;
    }

    std::array<std::uint32_t, 3> probeCount_{};
    std::array<std::int32_t, 3> ringOffset_{};
};

/// Candidate/accepted ownership for DDGI spatial inputs. A frame may prepare
/// an origin, ring remap and lighting revision before recording, but those
/// values become visible to the next frame only after the owner commits the
/// candidate for its exact accepted submission.
struct DdgiSpatialHistoryInput final {
    std::array<float, 3> origin{};
    std::array<float, 3> spacing{};
    std::array<float, 7> lighting{};
};

struct DdgiSpatialHistoryCandidate final {
    explicit DdgiSpatialHistoryCandidate(std::array<std::uint32_t, 3> probeCount)
        : volume(probeCount)
    {}

    std::uint64_t sourceGeneration = 0u;
    std::uint64_t generation = 0u;
    std::array<float, 3> origin{};
    std::array<float, 3> spacing{};
    std::array<float, 7> lighting{};
    DdgiScrollingVolume volume;
    std::uint32_t lightingRevision = 1u;
    bool gridScrolled = false;
    DdgiScrollResult scroll{};
};

struct DdgiSpatialHistoryPrepareResult final {
    bool success = false;
    const char* diagnostic = "DDGI-SPATIAL-HISTORY-INVALID";
    std::optional<DdgiSpatialHistoryCandidate> candidate;
};

class DdgiSpatialHistory final {
public:
    explicit DdgiSpatialHistory(std::array<std::uint32_t, 3> probeCount)
        : probeCount_(probeCount), volume_(probeCount)
    {
        reset();
    }

    DdgiSpatialHistoryPrepareResult prepare(const DdgiSpatialHistoryInput& input) const noexcept
    {
        DdgiSpatialHistoryPrepareResult result;
        for (std::uint32_t axis = 0u; axis < 3u; ++axis) {
            if (!std::isfinite(input.origin[axis]) || !std::isfinite(input.spacing[axis])
                || input.spacing[axis] <= 0.0f) {
                result.diagnostic = "DDGI-SPATIAL-HISTORY-INVALID-GRID";
                return result;
            }
        }
        for (const float component : input.lighting) {
            if (!std::isfinite(component)) {
                result.diagnostic = "DDGI-SPATIAL-HISTORY-INVALID-LIGHTING";
                return result;
            }
        }

        result.candidate.emplace(probeCount_);
        auto& candidate = *result.candidate;
        candidate.sourceGeneration = acceptedGeneration_;
        candidate.generation = nextCandidateGeneration_++;
        candidate.origin = input.origin;
        candidate.spacing = input.spacing;
        candidate.lighting = input.lighting;
        candidate.volume = volume_;
        candidate.lightingRevision = lightingRevision_;

        const bool hadPreviousOrigin =
            !std::isnan(origin_[0]) && !std::isnan(origin_[1]) && !std::isnan(origin_[2]);
        candidate.gridScrolled = !hadPreviousOrigin || candidate.origin[0] != origin_[0]
            || candidate.origin[1] != origin_[1] || candidate.origin[2] != origin_[2];
        if (hadPreviousOrigin && candidate.gridScrolled) {
            std::array<std::int32_t, 3> cellDelta{};
            for (std::uint32_t axis = 0u; axis < 3u; ++axis) {
                cellDelta[axis] = static_cast<std::int32_t>(
                    std::lround((candidate.origin[axis] - origin_[axis]) / input.spacing[axis]));
            }
            candidate.scroll = candidate.volume.scroll(cellDelta);
        } else {
            candidate.scroll.ringOffset = candidate.volume.ringOffset();
        }

        bool lightingChanged = false;
        for (std::uint32_t component = 0u; component < candidate.lighting.size(); ++component) {
            lightingChanged = lightingChanged || std::isnan(lighting_[component])
                || lighting_[component] != candidate.lighting[component];
        }
        if (lightingChanged && candidate.lightingRevision < 0x00fffffeu) {
            ++candidate.lightingRevision;
        }
        result.success = true;
        result.diagnostic = "";
        return result;
    }

    bool canCommit(const DdgiSpatialHistoryCandidate& candidate) const noexcept
    {
        return candidate.generation != 0u && candidate.sourceGeneration == acceptedGeneration_;
    }

    bool commit(DdgiSpatialHistoryCandidate&& candidate) noexcept
    {
        if (!canCommit(candidate))
            return false;
        origin_ = candidate.origin;
        lighting_ = candidate.lighting;
        volume_ = std::move(candidate.volume);
        lightingRevision_ = candidate.lightingRevision;
        acceptedGeneration_ = candidate.generation;
        if (nextCandidateGeneration_ <= acceptedGeneration_) {
            nextCandidateGeneration_ = acceptedGeneration_ + 1u;
        }
        return true;
    }

    void reset() noexcept
    {
        origin_.fill(std::numeric_limits<float>::quiet_NaN());
        lighting_.fill(std::numeric_limits<float>::quiet_NaN());
        volume_ = DdgiScrollingVolume(probeCount_);
        lightingRevision_ = 1u;
        acceptedGeneration_ = nextCandidateGeneration_++;
    }

    const std::array<std::int32_t, 3>& ringOffset() const noexcept { return volume_.ringOffset(); }

    // Observers may report only the state published by an accepted submission.
    // Candidates stay private to prepare()/commit() and cannot be mistaken for
    // GPU-visible history by a consumer or diagnostic path.
    const std::array<float, 3>& origin() const noexcept { return origin_; }

    const std::array<float, 7>& lighting() const noexcept { return lighting_; }

    std::uint32_t lightingRevision() const noexcept { return lightingRevision_; }

    std::uint64_t acceptedGeneration() const noexcept { return acceptedGeneration_; }

private:
    std::array<std::uint32_t, 3> probeCount_{};
    std::array<float, 3> origin_{};
    std::array<float, 7> lighting_{};
    DdgiScrollingVolume volume_;
    std::uint32_t lightingRevision_ = 1u;
    std::uint64_t acceptedGeneration_ = 0u;
    mutable std::uint64_t nextCandidateGeneration_ = 1u;
};

// Backend-neutral probe mapping reference shared by acceptance tests and
// future GPU readback comparison. This mirrors logicalToPhysicalProbe and
// probeGridToIndex in ddgi.slang without depending on a device or shader.
struct DdgiProbeGridReference {
    std::array<std::uint32_t, 3> probeCount{};
    std::array<std::int32_t, 3> ringOffset{};

    bool valid() const noexcept
    {
        return probeCount[0] != 0u && probeCount[1] != 0u && probeCount[2] != 0u;
    }

    std::array<std::uint32_t, 3> physicalCoordinate(
        const std::array<std::uint32_t, 3>& logical) const noexcept
    {
        std::array<std::uint32_t, 3> physical{};
        if (!valid())
            return physical;
        for (std::uint32_t axis = 0u; axis < 3u; ++axis) {
            const auto count = static_cast<std::int32_t>(probeCount[axis]);
            const auto value = static_cast<std::int32_t>(logical[axis]) + ringOffset[axis];
            physical[axis] = static_cast<std::uint32_t>((value % count + count) % count);
        }
        return physical;
    }

    std::uint32_t linearIndex(const std::array<std::uint32_t, 3>& logical) const noexcept
    {
        if (!valid())
            return 0u;
        const auto physical = physicalCoordinate(logical);
        return physical[0] + physical[1] * probeCount[0]
            + physical[2] * probeCount[0] * probeCount[1];
    }
};

struct DdgiProbeWeightReference {
    std::uint32_t linearIndex = 0u;
    float weight = 0.0f;
};

struct DdgiProbeWeightSetReference {
    std::array<DdgiProbeWeightReference, 8> entries{};
    std::uint32_t count = 0u;
    float totalWeight = 0.0f;
};

// Mirrors sampleProbeIrradiance's bounded trilinear neighborhood. The fixed
// eight-entry result makes this suitable for deterministic CPU/GPU readback
// comparisons without introducing hot-path allocation.
inline DdgiProbeWeightSetReference buildDdgiProbeWeightReference(
    const std::array<float, 3>& worldPosition,
    const std::array<float, 3>& gridOrigin,
    const std::array<float, 3>& gridSpacing,
    const DdgiProbeGridReference& grid) noexcept
{
    DdgiProbeWeightSetReference result;
    if (!grid.valid())
        return result;
    for (const auto spacing : gridSpacing) {
        if (!std::isfinite(spacing) || spacing <= 0.0f)
            return result;
    }
    std::array<std::int32_t, 3> base{};
    std::array<float, 3> fraction{};
    for (std::uint32_t axis = 0u; axis < 3u; ++axis) {
        const auto coordinate = (worldPosition[axis] - gridOrigin[axis]) / gridSpacing[axis];
        if (!std::isfinite(coordinate))
            return result;
        const auto floorCoordinate = std::floor(coordinate);
        base[axis] = static_cast<std::int32_t>(floorCoordinate);
        fraction[axis] = coordinate - floorCoordinate;
    }
    for (std::uint32_t z = 0u; z < 2u; ++z) {
        for (std::uint32_t y = 0u; y < 2u; ++y) {
            for (std::uint32_t x = 0u; x < 2u; ++x) {
                const std::array<std::int32_t, 3> coordinate{
                    base[0] + static_cast<std::int32_t>(x),
                    base[1] + static_cast<std::int32_t>(y),
                    base[2] + static_cast<std::int32_t>(z),
                };
                if (coordinate[0] < 0 || coordinate[1] < 0 || coordinate[2] < 0
                    || static_cast<std::uint32_t>(coordinate[0]) >= grid.probeCount[0]
                    || static_cast<std::uint32_t>(coordinate[1]) >= grid.probeCount[1]
                    || static_cast<std::uint32_t>(coordinate[2]) >= grid.probeCount[2]) {
                    continue;
                }
                const auto weight = (x == 0u ? 1.0f - fraction[0] : fraction[0])
                    * (y == 0u ? 1.0f - fraction[1] : fraction[1])
                    * (z == 0u ? 1.0f - fraction[2] : fraction[2]);
                if (weight <= 0.0f || result.count >= result.entries.size())
                    continue;
                const std::array<std::uint32_t, 3> logical{
                    static_cast<std::uint32_t>(coordinate[0]),
                    static_cast<std::uint32_t>(coordinate[1]),
                    static_cast<std::uint32_t>(coordinate[2]),
                };
                result.entries[result.count++] = {grid.linearIndex(logical), weight};
                result.totalWeight += weight;
            }
        }
    }
    return result;
}

inline float ddgiChebyshevWeightReference(
    float meanDepth, float meanDepthSquared, float fragmentDistance) noexcept
{
    if (!std::isfinite(meanDepth) || !std::isfinite(meanDepthSquared)
        || !std::isfinite(fragmentDistance))
        return 0.0f;
    if (fragmentDistance <= meanDepth)
        return 1.0f;
    const auto variance = std::max(meanDepthSquared - meanDepth * meanDepth, 0.0001f);
    const auto difference = fragmentDistance - meanDepth;
    return variance / (variance + difference * difference);
}

inline float ddgiVisibilityLeakReductionReference(float visibility) noexcept
{
    if (!std::isfinite(visibility))
        return 0.0f;
    constexpr auto threshold = 0.2f;
    if (visibility >= threshold)
        return visibility;
    const auto normalized = visibility / threshold;
    return visibility * normalized * normalized;
}

struct DdgiResolveSampleReference {
    float trilinearWeight = 0.0f;
    float confidence = 0.0f;
    float shadingWeight = 0.0f;
    float irradiance = 0.0f;
    bool active = false;
};

// Mirrors the resolve weighting contract for one scalar irradiance channel.
// Confidence affects which probe is trusted, while geometric coverage alone
// controls the finite-volume edge fade.
inline float ddgiResolveIrradianceReference(
    const std::array<DdgiResolveSampleReference, 8>& samples) noexcept
{
    float weightedIrradiance = 0.0f;
    float totalWeight = 0.0f;
    float geometricCoverage = 0.0f;
    for (const auto& sample : samples) {
        if (!sample.active || sample.confidence <= 0.0f)
            continue;
        const auto trilinearWeight = std::max(sample.trilinearWeight, 0.0f);
        const auto weight =
            trilinearWeight * sample.confidence * std::max(sample.shadingWeight, 0.0f);
        weightedIrradiance += sample.irradiance * weight;
        totalWeight += weight;
        geometricCoverage += trilinearWeight;
    }
    const auto normalized = totalWeight > 0.0001f ? weightedIrradiance / totalWeight : 0.0f;
    return normalized * std::clamp(geometricCoverage, 0.0f, 1.0f);
}

struct DdgiProbeScheduleInput {
    std::uint32_t frameIndex = 0u;
    std::array<std::int32_t, 3> originCell{};
    std::array<std::int32_t, 3> ringOffset{};
    std::uint32_t lightingRevision = 0u;
    std::uint64_t geometryRevision = 0u;
};

struct DdgiProbeScheduleResult {
    const core::pmr::Vector<std::uint32_t>* logicalProbeIndices = nullptr;
    std::uint32_t newlyExposedProbeCount = 0u;
    std::uint32_t oldestProbeAge = 0u;
    float averageProbeAge = 0.0f;
    std::uint64_t generation = 0u;
    std::uint64_t ownerId = 0u;
};

// The scheduler consumes asynchronous GPU timing from the most recently
// completed frame. Keeping the budget decision as a pure helper makes the
// safety bounds testable without a backend or a command encoder.
struct DdgiProbeBudgetInput {
    std::uint32_t totalProbeCount = 0u;
    std::uint32_t configuredMaxProbeUpdates = 0u;
    std::uint32_t previousProbeUpdates = 0u;
    float gpuBudgetMilliseconds = 0.0f;
    float measuredGpuMilliseconds = 0.0f;
    bool measurementAvailable = false;
};

struct DdgiProbeBudgetResult {
    std::uint32_t probeUpdates = 0u;
    bool measurementUsed = false;
    bool throttled = false;
    bool increased = false;
};

inline DdgiProbeBudgetResult planDdgiProbeUpdateBudget(const DdgiProbeBudgetInput& input) noexcept
{
    constexpr float kHeadroomRatio = 0.85f;
    constexpr std::uint32_t kRampDenominator = 4u;

    const auto ceiling = std::min(input.totalProbeCount, input.configuredMaxProbeUpdates);
    if (ceiling == 0u) {
        return {};
    }

    const auto previous =
        input.previousProbeUpdates == 0u ? ceiling : std::min(input.previousProbeUpdates, ceiling);
    DdgiProbeBudgetResult result{previous, false, false, false};
    const bool validMeasurement = input.measurementAvailable
        && std::isfinite(input.measuredGpuMilliseconds) && input.measuredGpuMilliseconds > 0.0f
        && std::isfinite(input.gpuBudgetMilliseconds) && input.gpuBudgetMilliseconds > 0.0f;
    if (!validMeasurement) {
        return result;
    }

    result.measurementUsed = true;
    if (input.measuredGpuMilliseconds > input.gpuBudgetMilliseconds) {
        const auto scaled = static_cast<std::uint32_t>(static_cast<float>(previous)
            * input.gpuBudgetMilliseconds / input.measuredGpuMilliseconds);
        result.probeUpdates = std::max(1u, std::min(previous, scaled));
        if (result.probeUpdates == previous && previous > 1u) {
            --result.probeUpdates;
        }
        result.throttled = result.probeUpdates < previous;
        return result;
    }

    if (input.measuredGpuMilliseconds < input.gpuBudgetMilliseconds * kHeadroomRatio) {
        const auto increment = std::max(1u, previous / kRampDenominator);
        const auto available = ceiling - previous;
        result.probeUpdates = previous + std::min(available, increment);
        result.increased = result.probeUpdates > previous;
    }
    return result;
}

class DdgiProbeScheduler final {
public:
    DdgiProbeScheduler(std::array<std::uint32_t, 3> probeCount,
        std::uint32_t updateBudget,
        std::uint64_t firstGeneration = 1u)
        : probeCount_(probeCount), totalProbeCount_(probeCount[0] * probeCount[1] * probeCount[2]),
          updateBudget_(std::min(totalProbeCount_, updateBudget)),
          states_(totalProbeCount_, State{}, &core::defaultCpuMemoryResource()),
          preparedStates_(totalProbeCount_, State{}, &core::defaultCpuMemoryResource()),
          candidates_(&core::defaultCpuMemoryResource()),
          scheduledLogicalIndices_(&core::defaultCpuMemoryResource()),
          generations_(firstGeneration), ownerId_(ownerIds_.tryAcquire())
    {
        candidates_.reserve(totalProbeCount_);
        scheduledLogicalIndices_.reserve(totalProbeCount_);
    }

    void setUpdateBudget(std::uint32_t updateBudget)
    {
        updateBudget_ = std::min(totalProbeCount_, updateBudget);
    }

    DdgiProbeScheduleResult prepare(const DdgiProbeScheduleInput& input)
    {
        preparedGeneration_ = 0u;
        candidates_.clear();
        scheduledLogicalIndices_.clear();
        if (totalProbeCount_ == 0u || updateBudget_ == 0u || ownerId_ == 0u) {
            return {&scheduledLogicalIndices_, 0u, 0u, 0.0f};
        }
        preparedGeneration_ = generations_.tryAcquire();
        if (preparedGeneration_ == 0u) {
            return {&scheduledLogicalIndices_, 0u, 0u, 0.0f};
        }
        std::copy(states_.begin(), states_.end(), preparedStates_.begin());

        std::uint32_t newlyExposedCount = 0u;
        std::uint64_t ageSum = 0u;
        std::uint32_t oldestAge = 0u;
        const auto starvationAge = maximumAgeFrames() * 2u;

        for (std::uint32_t z = 0u; z < probeCount_[2]; ++z) {
            for (std::uint32_t y = 0u; y < probeCount_[1]; ++y) {
                for (std::uint32_t x = 0u; x < probeCount_[0]; ++x) {
                    const std::array<std::uint32_t, 3> logical{x, y, z};
                    const auto physicalIndex = physicalLinearIndex(logical, input.ringOffset);
                    auto& state = preparedStates_[physicalIndex];
                    const std::array<std::int32_t, 3> logicalCell{
                        input.originCell[0] + static_cast<std::int32_t>(x),
                        input.originCell[1] + static_cast<std::int32_t>(y),
                        input.originCell[2] + static_cast<std::int32_t>(z),
                    };
                    const bool newlyExposed =
                        !state.initialized || state.logicalCell != logicalCell;
                    if (newlyExposed) {
                        state.initialized = true;
                        state.logicalCell = logicalCell;
                        state.pendingNewCell = true;
                        ++newlyExposedCount;
                    }

                    const auto age = state.hasUpdate ? input.frameIndex - state.lastUpdateFrame
                                                     : std::numeric_limits<std::uint32_t>::max();
                    const auto finiteAge = state.hasUpdate ? age : starvationAge;
                    ageSum += finiteAge;
                    oldestAge = std::max(oldestAge, finiteAge);

                    const bool revisionChanged = !state.hasUpdate
                        || state.lightingRevision != input.lightingRevision
                        || state.geometryRevision != input.geometryRevision;
                    const std::int32_t dx = static_cast<std::int32_t>(x * 2u)
                        - static_cast<std::int32_t>(probeCount_[0] - 1u);
                    const std::int32_t dy = static_cast<std::int32_t>(y * 2u)
                        - static_cast<std::int32_t>(probeCount_[1] - 1u);
                    const std::int32_t dz = static_cast<std::int32_t>(z * 2u)
                        - static_cast<std::int32_t>(probeCount_[2] - 1u);
                    const auto distanceSquared =
                        static_cast<std::uint32_t>(dx * dx + dy * dy + dz * dz);

                    std::uint32_t priority = 4u;
                    if (state.pendingNewCell) {
                        priority = 0u;
                    } else if (finiteAge >= starvationAge) {
                        priority = 1u;
                    } else if (distanceSquared <= nearDistanceSquared()) {
                        priority = 2u;
                    } else if (revisionChanged) {
                        priority = 3u;
                    }
                    candidates_.push_back({
                        logicalLinearIndex(logical),
                        physicalIndex,
                        priority,
                        distanceSquared,
                        finiteAge,
                    });
                }
            }
        }

        std::sort(
            candidates_.begin(), candidates_.end(), [](const Candidate& a, const Candidate& b) {
                if (a.priority != b.priority)
                    return a.priority < b.priority;
                if (a.priority == 4u && a.age != b.age)
                    return a.age > b.age;
                if (a.distanceSquared != b.distanceSquared) {
                    return a.distanceSquared < b.distanceSquared;
                }
                if (a.age != b.age)
                    return a.age > b.age;
                return a.logicalIndex < b.logicalIndex;
            });

        const auto count =
            std::min<std::uint32_t>(updateBudget_, static_cast<std::uint32_t>(candidates_.size()));
        for (std::uint32_t rank = 0u; rank < count; ++rank) {
            const auto& candidate = candidates_[rank];
            scheduledLogicalIndices_.push_back(candidate.logicalIndex);
            auto& state = preparedStates_[candidate.physicalIndex];
            state.hasUpdate = true;
            state.pendingNewCell = false;
            state.lastUpdateFrame = input.frameIndex;
            state.lightingRevision = input.lightingRevision;
            state.geometryRevision = input.geometryRevision;
        }

        return {
            &scheduledLogicalIndices_,
            newlyExposedCount,
            oldestAge,
            static_cast<float>(ageSum) / static_cast<float>(totalProbeCount_),
            preparedGeneration_,
            ownerId_,
        };
    }

    // The receipt is a borrowed-list result, but acceptance uses only its
    // scheduler identity and non-recycled generation, never its list pointer.
    bool commit(const DdgiProbeScheduleResult& receipt) noexcept
    {
        if (receipt.ownerId != ownerId_ || receipt.generation == 0u
            || receipt.generation != preparedGeneration_)
            return false;
        states_.swap(preparedStates_);
        preparedGeneration_ = 0u;
        return true;
    }

    bool discard(const DdgiProbeScheduleResult& receipt) noexcept
    {
        if (receipt.ownerId != ownerId_ || receipt.generation == 0u
            || receipt.generation != preparedGeneration_)
            return false;
        preparedGeneration_ = 0u;
        return true;
    }

    bool hasRemainingGenerations() const noexcept { return generations_.hasRemaining(); }

    std::uint32_t maximumAgeFrames() const noexcept
    {
        if (updateBudget_ == 0u)
            return 0xffffffffu;
        return (totalProbeCount_ + updateBudget_ - 1u) / updateBudget_;
    }

private:
    struct State {
        std::array<std::int32_t, 3> logicalCell{};
        std::uint32_t lastUpdateFrame = 0u;
        std::uint32_t lightingRevision = 0u;
        std::uint64_t geometryRevision = 0u;
        bool initialized = false;
        bool hasUpdate = false;
        bool pendingNewCell = false;
    };

    struct Candidate {
        std::uint32_t logicalIndex = 0u;
        std::uint32_t physicalIndex = 0u;
        std::uint32_t priority = 0u;
        std::uint32_t distanceSquared = 0u;
        std::uint32_t age = 0u;
    };

    std::uint32_t logicalLinearIndex(const std::array<std::uint32_t, 3>& logical) const noexcept
    {
        return logical[0] + probeCount_[0] * (logical[1] + probeCount_[1] * logical[2]);
    }

    std::uint32_t physicalLinearIndex(const std::array<std::uint32_t, 3>& logical,
        const std::array<std::int32_t, 3>& ringOffset) const noexcept
    {
        std::array<std::uint32_t, 3> physical{};
        for (std::uint32_t axis = 0u; axis < 3u; ++axis) {
            physical[axis] = static_cast<std::uint32_t>(
                positiveModulo(static_cast<std::int32_t>(logical[axis]) + ringOffset[axis],
                    static_cast<std::int32_t>(probeCount_[axis])));
        }
        return logicalLinearIndex(physical);
    }

    static std::int32_t positiveModulo(std::int32_t value, std::int32_t divisor) noexcept
    {
        return (value % divisor + divisor) % divisor;
    }

    std::uint32_t nearDistanceSquared() const noexcept
    {
        const auto radius = std::max<std::uint32_t>(
            2u, std::min({probeCount_[0], probeCount_[1], probeCount_[2]}) / 3u);
        return 4u * radius * radius;
    }

    std::array<std::uint32_t, 3> probeCount_{};
    std::uint32_t totalProbeCount_ = 0u;
    std::uint32_t updateBudget_ = 0u;
    core::pmr::Vector<State> states_;
    core::pmr::Vector<State> preparedStates_;
    core::pmr::Vector<Candidate> candidates_;
    core::pmr::Vector<std::uint32_t> scheduledLogicalIndices_;
    core::MonotonicIdSequence generations_;
    inline static core::MonotonicIdSequence ownerIds_;
    const std::uint64_t ownerId_;
    std::uint64_t preparedGeneration_ = 0u;
};

} // namespace arp::renderer

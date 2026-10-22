#pragma once
#include "core/math/unsigned_range.h"
#include "rhi/frontend/command_indirect_commands.h"
#include "rhi/frontend/visibility/instance64.h"
#include "rhi/gpu_resource_types.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <limits>
#include <type_traits>

namespace arp::rhi::visibility {
// Current production draw-group ABI, including bounds and the middle cascade's
// alternate mesh. Kept in the engine before renderer/WOCC publication migrates.
struct GpuVisibilityDrawGroup {
    std::uint32_t templateId, sectionIndex, pipelineVariantId;
    std::uint32_t globalFirstInstance, globalInstanceCount, padding[3];
    float baseColor[4], localBoundsCenterRadius[4];
    std::uint32_t materialIndex;
    float cullDistance;
    std::uint32_t useAlphaTest, doubleSided, useFullPbrFragment, singleSidedLighting;
    std::uint32_t castsShadow, firstIndex, indexCount;
    std::uint32_t shadowCascade1TemplateId, shadowCascade1FirstIndex, shadowCascade1IndexCount;
};

static_assert(
    std::is_standard_layout_v<GpuVisibilityDrawGroup> && sizeof(GpuVisibilityDrawGroup) == 112u);
static_assert(offsetof(GpuVisibilityDrawGroup, globalFirstInstance) == 12u
    && offsetof(GpuVisibilityDrawGroup, baseColor) == 32u
    && offsetof(GpuVisibilityDrawGroup, localBoundsCenterRadius) == 48u
    && offsetof(GpuVisibilityDrawGroup, materialIndex) == 64u
    && offsetof(GpuVisibilityDrawGroup, firstIndex) == 92u
    && offsetof(GpuVisibilityDrawGroup, shadowCascade1TemplateId) == 100u);

// Uniform448, shared by the engine's reset/cull/emit/finalize entry family.
// viewProjectionRows are explicit row vectors (not a matrix ABI declaration).
struct GpuVisibilityParameters {
    std::uint32_t counts[4];     // instances, sections, cascades, native firstInstance
    std::uint32_t capacities[4]; // instance/group/material capacity, ABI version
    float cameraAndEnvelope[4];
    float selection[4]; // shadow distance followed by reserved zero lanes
    float frustumPlanes[18][4];
    float viewProjectionRows[4][4];
    std::uint32_t depthInfo[4]; // extent, mip count, enabled
    std::uint32_t policy[4];    // Scene=0/Shadow=1, reserved zero, reserved zero, version
};

static_assert(
    std::is_standard_layout_v<GpuVisibilityParameters> && sizeof(GpuVisibilityParameters) == 448u);
static_assert(offsetof(GpuVisibilityParameters, capacities) == 16u
    && offsetof(GpuVisibilityParameters, cameraAndEnvelope) == 32u
    && offsetof(GpuVisibilityParameters, selection) == 48u
    && offsetof(GpuVisibilityParameters, frustumPlanes) == 64u
    && offsetof(GpuVisibilityParameters, viewProjectionRows) == 352u
    && offsetof(GpuVisibilityParameters, depthInfo) == 416u
    && offsetof(GpuVisibilityParameters, policy) == 432u);

struct GpuVisibilityOutputRanges {
    std::uint32_t drawWords = 0u, baseSlots = 0u, controlWords = 0u, contractVersion = 0u;
};

static_assert(sizeof(GpuVisibilityOutputRanges) == 16u
    && offsetof(GpuVisibilityOutputRanges, drawWords) == 0u
    && offsetof(GpuVisibilityOutputRanges, baseSlots) == 4u
    && offsetof(GpuVisibilityOutputRanges, controlWords) == 8u
    && offsetof(GpuVisibilityOutputRanges, contractVersion) == 12u);

// Cold validation of the exact authored instance-to-owner map. Identical ranges
// may be consumed by several material sections; partial overlaps cannot share a
// counter. This borrows scene data and produces sizes, never a copied owner graph
// or a CPU replacement for GPU culling/indirect output.
class GpuVisibilityLayoutContract final {
public:
    struct Input {
        const GpuVisibilityDrawGroup* groups = nullptr;
        const std::uint32_t* instanceToGroup = nullptr;
        const std::uint32_t* templateIndexCounts = nullptr;
        std::uint32_t groupCount = 0u, instanceCount = 0u, templateCount = 0u;
        std::uint32_t materialCapacity = 0u, instanceCapacity = 0u, groupCapacity = 0u;
        std::uint32_t cascades = 0u;
    };

    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool optIn = false;
        std::uint64_t maxGroupVisits = 0u, maxInstanceVisits = 0u, maxPairVisits = 0u;
    };

    struct Report {
        std::uint64_t inputInstanceBytes = 0u, drawGroupBytes = 0u, ownerMapBytes = 0u;
        std::uint64_t visibleInstanceBytes = 0u, countBytes = 0u, indirectBytes = 0u;
        std::uint64_t drawCountBytes = 0u, zeroBaseMappingBytes = 0u;
        std::uint64_t controlBytes = 0u, parameterBytes = 0u, ordinaryOutputRangeBytes = 0u;
        std::uint64_t groupVisits = 0u, instanceVisits = 0u, pairVisits = 0u;
    };

    static const char* inspect(const Input& input, const Budget& budget, Report& output) noexcept
    {
        output = {};
        if (budget.contractVersion != 1u)
            return "PCG-GPU-VISIBILITY-VERSION";
        if (!budget.optIn)
            return "PCG-GPU-VISIBILITY-OPT-IN";
        if (!input.groups || !input.instanceToGroup || !input.templateIndexCounts
            || !input.groupCount || !input.instanceCount || !input.templateCount
            || !input.materialCapacity || input.instanceCount > input.instanceCapacity
            || input.groupCount > input.groupCapacity)
            return "PCG-GPU-VISIBILITY-INPUT";
        if (input.cascades != 1u && input.cascades != 3u)
            return "PCG-GPU-VISIBILITY-CASCADES";
        // Shader indices are uint32, even on a backend with larger buffers.
        if (!core::canMultiplyUnsigned(input.instanceCapacity, input.cascades)
            || !core::canMultiplyUnsigned(input.groupCapacity, input.cascades)
            || std::uint64_t(input.groupCapacity) * input.cascades > UINT32_MAX / 5u)
            return "PCG-GPU-VISIBILITY-INDEX-OVERFLOW";
        Report result;
        if (!core::countUnorderedPairsUnsigned(std::uint64_t(input.groupCount), result.pairVisits))
            return "PCG-GPU-VISIBILITY-VISIT-OVERFLOW";
        // Count every group and map lookup, including indirect owner lookup
        // and the separately bounded pair traversal. These are independent
        // ceilings, rather than an estimate of the number of input records.
        result.groupVisits =
            3u * std::uint64_t(input.groupCount) + input.instanceCount + result.pairVisits;
        result.instanceVisits = 2u * std::uint64_t(input.instanceCount) + input.groupCount;
        if (result.groupVisits > budget.maxGroupVisits
            || result.instanceVisits > budget.maxInstanceVisits
            || result.pairVisits > budget.maxPairVisits)
            return "PCG-GPU-VISIBILITY-VISIT-CAPACITY";
        const auto validIndexRange = [&](std::uint32_t mesh,
                                         std::uint32_t first,
                                         std::uint32_t count) noexcept {
            return mesh < input.templateCount
                && core::containsUnsignedRange(0u, input.templateIndexCounts[mesh], first, count);
        };
        for (std::uint32_t i = 0u; i < input.groupCount; ++i) {
            const auto& group = input.groups[i];
            if (!group.globalInstanceCount || !group.indexCount
                || !core::containsUnsignedRange(
                    0u, input.instanceCount, group.globalFirstInstance, group.globalInstanceCount)
                || group.materialIndex >= input.materialCapacity
                || !validIndexRange(group.templateId, group.firstIndex, group.indexCount)
                || (group.shadowCascade1IndexCount
                    && !validIndexRange(group.shadowCascade1TemplateId,
                        group.shadowCascade1FirstIndex,
                        group.shadowCascade1IndexCount)))
                return "PCG-GPU-VISIBILITY-GROUP-RANGE";
            if (!std::isfinite(group.cullDistance) || group.cullDistance < 0.f
                || group.localBoundsCenterRadius[3] < 0.f)
                return "PCG-GPU-VISIBILITY-BOUNDS";
            for (const auto coordinate : group.localBoundsCenterRadius)
                if (!std::isfinite(coordinate))
                    return "PCG-GPU-VISIBILITY-BOUNDS";
            for (const auto channel : group.baseColor)
                if (!std::isfinite(channel))
                    return "PCG-GPU-VISIBILITY-MATERIAL";
            for (const auto flag : {group.useAlphaTest,
                     group.doubleSided,
                     group.useFullPbrFragment,
                     group.singleSidedLighting,
                     group.castsShadow})
                if (flag > 1u)
                    return "PCG-GPU-VISIBILITY-FLAG";
            for (std::uint32_t j = 0u; j < i; ++j) {
                const auto& prior = input.groups[j];
                if (core::overlapsUnsignedRanges(group.globalFirstInstance,
                        group.globalInstanceCount,
                        prior.globalFirstInstance,
                        prior.globalInstanceCount)
                    && !sameRange(group, prior))
                    return "PCG-GPU-VISIBILITY-PARTIAL-OVERLAP";
            }
        }
        for (std::uint32_t instance = 0u; instance < input.instanceCount; ++instance) {
            const auto owner = input.instanceToGroup[instance];
            if (owner >= input.groupCount)
                return "PCG-GPU-VISIBILITY-OWNER";
            const auto& group = input.groups[owner];
            if (!core::containsUnsignedRange(
                    group.globalFirstInstance, group.globalInstanceCount, instance, 1u))
                return "PCG-GPU-VISIBILITY-OWNER-RANGE";
            // All members of a span must map to its one explicitly authored
            // counter owner, including the range's first instance.
            if (input.instanceToGroup[group.globalFirstInstance] != owner)
                return "PCG-GPU-VISIBILITY-SPLIT-OWNER";
        }
        for (std::uint32_t i = 0u; i < input.groupCount; ++i) {
            const auto& group = input.groups[i];
            const auto& owner = input.groups[input.instanceToGroup[group.globalFirstInstance]];
            if (!sameRange(group, owner))
                return "PCG-GPU-VISIBILITY-SECTION-OWNER";
            // Cull distance/bounds/shadow selection come from the owner. A
            // section cannot silently inherit incompatible visibility semantics.
            if (group.cullDistance != owner.cullDistance || group.castsShadow != owner.castsShadow)
                return "PCG-GPU-VISIBILITY-SECTION-SEMANTICS";
            for (std::size_t lane = 0u; lane < 4u; ++lane)
                if (group.localBoundsCenterRadius[lane] != owner.localBoundsCenterRadius[lane])
                    return "PCG-GPU-VISIBILITY-SECTION-SEMANTICS";
        }
        const auto instances = std::uint64_t(input.instanceCapacity) * input.cascades;
        const auto groups = std::uint64_t(input.groupCapacity) * input.cascades;
        result.inputInstanceBytes = std::uint64_t(input.instanceCount) * sizeof(Instance64);
        result.drawGroupBytes = std::uint64_t(input.groupCount) * sizeof(GpuVisibilityDrawGroup);
        result.ownerMapBytes = std::uint64_t(input.instanceCount) * sizeof(std::uint32_t);
        result.visibleInstanceBytes = instances * sizeof(Instance64);
        result.countBytes = groups * sizeof(std::uint32_t);
        result.indirectBytes = groups * sizeof(rhi::DrawIndexedIndirectCommand);
        result.drawCountBytes = std::uint64_t(input.cascades) * sizeof(std::uint32_t);
        result.controlBytes = result.drawCountBytes + sizeof(std::uint32_t);
        result.parameterBytes = sizeof(GpuVisibilityParameters);
        result.ordinaryOutputRangeBytes = sizeof(GpuVisibilityOutputRanges);
        result.zeroBaseMappingBytes = groups * sizeof(rhi::IndirectInstanceBase);
        output = result;
        return nullptr;
    }

    // queriedNativeBase is a queried capability fact, not a request to emulate
    // it. The command closure must independently authenticate that same device.
    static const char* inspectParameters(const Input& input,
        const GpuVisibilityParameters& p,
        bool queriedNativeBase,
        const rhi::TextureDesc* pyramid = nullptr) noexcept
    {
        if (p.capacities[3] != 1u || p.policy[3] != 1u)
            return "PCG-GPU-VISIBILITY-PARAMETER-VERSION";
        if (p.counts[0] != input.instanceCount || p.counts[1] != input.groupCount
            || p.counts[2] != input.cascades || p.capacities[0] != input.instanceCapacity
            || p.capacities[1] != input.groupCapacity || p.capacities[2] != input.materialCapacity
            || !p.counts[0] || !p.counts[1] || p.counts[0] > p.capacities[0]
            || p.counts[1] > p.capacities[1] || !p.capacities[2]
            || (p.counts[2] != 1u && p.counts[2] != 3u)
            || !core::canMultiplyUnsigned(p.capacities[0], p.counts[2])
            || !core::canMultiplyUnsigned(p.capacities[1], p.counts[2])
            || std::uint64_t(p.capacities[1]) * p.counts[2] > UINT32_MAX / 5u)
            return "PCG-GPU-VISIBILITY-PARAMETER-CORRESPONDENCE";
        if (p.counts[3] > 1u || (p.counts[3] && !queriedNativeBase))
            return "PCG-GPU-VISIBILITY-FIRST-INSTANCE-CAPABILITY";
        if (p.policy[0] > 1u || p.policy[1] || p.policy[2] || p.selection[1] != 0.f
            || p.selection[2] != 0.f || p.selection[3] != 0.f
            || (p.policy[0] == 0u && p.counts[2] != 1u))
            return "PCG-GPU-VISIBILITY-PARAMETER-POLICY";
        for (const auto value : p.cameraAndEnvelope)
            if (!std::isfinite(value))
                return "PCG-GPU-VISIBILITY-PARAMETER-FINITE";
        if (p.cameraAndEnvelope[3] < 0.f || !std::isfinite(p.selection[0]) || p.selection[0] < 0.f)
            return "PCG-GPU-VISIBILITY-PARAMETER-ENVELOPE";
        for (std::uint32_t plane = 0u; plane < p.counts[2] * 6u; ++plane) {
            double lengthSquared = 0.;
            for (std::size_t lane = 0u; lane < 4u; ++lane) {
                const auto value = p.frustumPlanes[plane][lane];
                if (!std::isfinite(value))
                    return "PCG-GPU-VISIBILITY-PARAMETER-FINITE";
                if (lane < 3u)
                    lengthSquared += double(value) * value;
            }
            if (!(lengthSquared > 0.) || lengthSquared > std::numeric_limits<float>::max())
                return "PCG-GPU-VISIBILITY-PARAMETER-PLANE";
        }
        if (p.depthInfo[3] > 1u)
            return "PCG-GPU-VISIBILITY-PYRAMID-ENABLE";
        if (!p.depthInfo[3])
            return (p.depthInfo[0] || p.depthInfo[1] || p.depthInfo[2])
                ? "PCG-GPU-VISIBILITY-PYRAMID-DISABLED-SCHEMA"
                : nullptr;
        if (p.policy[0] || !pyramid || !p.depthInfo[0] || !p.depthInfo[1] || !p.depthInfo[2]
            || p.depthInfo[2] > 32u || p.depthInfo[0] != pyramid->extent.width
            || p.depthInfo[1] != pyramid->extent.height || p.depthInfo[2] != pyramid->mipLevelCount
            || pyramid->extent.depth != 1u || pyramid->arrayLayerCount != 1u
            || pyramid->sampleCount != 1u || pyramid->cubeCompatible
            || pyramid->format != rhi::TextureFormat::Rgba16Float
            || (pyramid->usage & rhi::TextureUsage::Sampled) == rhi::TextureUsage::None)
            return "PCG-GPU-VISIBILITY-PYRAMID-CORRESPONDENCE";
        for (const auto& row : p.viewProjectionRows)
            for (const auto value : row)
                if (!std::isfinite(value))
                    return "PCG-GPU-VISIBILITY-PARAMETER-FINITE";
        return nullptr;
    }

private:
    static bool sameRange(const GpuVisibilityDrawGroup& a, const GpuVisibilityDrawGroup& b) noexcept
    {
        return a.globalFirstInstance == b.globalFirstInstance
            && a.globalInstanceCount == b.globalInstanceCount;
    }
};

// Exact launch coverage for the four trusted VisibilityKernel/1 entries.
// Device limits are actual queries. Counts come from the authenticated layout;
// a larger or repeated launch is not another valid production dispatch.
class GpuVisibilityDispatchContract final {
public:
    static constexpr std::uint32_t localSizeX = 64u;
    enum class Phase : std::uint8_t { Reset, Cull, Emit, Finalize };

    struct Dispatch {
        std::uint32_t x = 0u, y = 0u, z = 0u;
        std::uint64_t logicalInvocations = 0u;
    };

    static const char* outputRanges(
        const GpuVisibilityLayoutContract::Input& input, GpuVisibilityOutputRanges& output) noexcept
    {
        output = {};
        const auto slots = std::uint64_t(input.groupCapacity) * input.cascades;
        if (!slots || slots > UINT32_MAX / 5u || !input.groupCount
            || input.groupCount > input.groupCapacity
            || (input.cascades != 1u && input.cascades != 3u))
            return "PCG-GPU-VISIBILITY-OUTPUT-RANGES";
        output = {static_cast<std::uint32_t>(slots * 5u),
            static_cast<std::uint32_t>(slots),
            1u + input.cascades,
            1u};
        return nullptr;
    }

    static const char* derive(const GpuVisibilityLayoutContract::Input& input,
        Phase phase,
        std::uint32_t queriedWorkgroupLimit,
        Dispatch& output) noexcept
    {
        output = {};
        if (!queriedWorkgroupLimit || !input.instanceCount || !input.groupCount
            || input.instanceCount > input.instanceCapacity
            || input.groupCount > input.groupCapacity
            || (input.cascades != 1u && input.cascades != 3u))
            return "PCG-GPU-VISIBILITY-DISPATCH-INPUT";
        const auto slots = std::uint64_t(input.groupCapacity) * input.cascades;
        if (slots > UINT32_MAX / 5u)
            return "PCG-GPU-VISIBILITY-DISPATCH-ADDRESS-OVERFLOW";
        std::uint64_t lanes = 0u;
        std::uint32_t rows = 1u;
        switch (phase) {
        case Phase::Reset:
            lanes = std::uint64_t(input.groupCount) * input.cascades;
            break;
        case Phase::Cull:
            lanes = input.instanceCount;
            rows = input.cascades;
            break;
        case Phase::Emit:
            lanes = input.groupCount;
            rows = input.cascades;
            break;
        case Phase::Finalize:
            // Finalize wipes the complete physical output capacity and all
            // physical draw counts, even when the parameter read has failed.
            lanes = std::max(slots, std::uint64_t(1u) + input.cascades);
            break;
        default:
            return "PCG-GPU-VISIBILITY-DISPATCH-PHASE";
        }
        const auto groups = lanes / localSizeX + bool(lanes % localSizeX);
        if (!groups || groups > queriedWorkgroupLimit || rows > queriedWorkgroupLimit)
            return "PCG-GPU-VISIBILITY-DISPATCH-DEVICE-LIMIT";
        output = {static_cast<std::uint32_t>(groups), rows, 1u, lanes * rows};
        return nullptr;
    }

    static const char* validate(const GpuVisibilityLayoutContract::Input& input,
        Phase phase,
        std::uint32_t queriedWorkgroupLimit,
        std::uint32_t x,
        std::uint32_t y,
        std::uint32_t z) noexcept
    {
        Dispatch required;
        if (const auto* id = derive(input, phase, queriedWorkgroupLimit, required))
            return id;
        return x == required.x && y == required.y && z == required.z
            ? nullptr
            : "PCG-GPU-VISIBILITY-DISPATCH-COVERAGE";
    }
};
} // namespace arp::rhi::visibility

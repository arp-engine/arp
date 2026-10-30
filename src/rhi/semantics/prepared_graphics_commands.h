#pragma once
#include "core/container/array_view.h"
#include "rhi/semantics/ordered_command_issue.h"

namespace arp::rhi::semantics {
// Keep the original public type identity and mangled signatures. New engine
// callers use the neutral alias; descriptor layout and diagnostic IDs are frozen.
struct WebGlPreparedCommandsRequest {
    std::uint32_t contractVersion = 0;
    bool prototypeOptIn = false;
    std::uint32_t maxDraws = 0;
    std::uint64_t maxUniformBytes = 0, maxAnalysisIndices = 0;
    OrderedCommandIssueBudget issue;
    std::uint32_t maxPipelines = 0;
    std::uint32_t maxTargets = 0, maxPasses = 0, maxTransitions = 0;
    // Version5 bounds heterogeneous pass-local layouts. Counts are capacities,
    // never descriptor-set or vertex-stream slot numbers.
    std::uint32_t maxVertexStreams = 0, maxDescriptorSets = 0, maxSampledTextures = 0;
};

using PreparedGraphicsCommandsRequest = WebGlPreparedCommandsRequest;

class PreparedGraphicsCommandsContract final {
public:
    static const char* validate(const PreparedGraphicsCommandsRequest& r) noexcept
    {
        if (r.contractVersion < 1 || r.contractVersion > 5)
            return "RHI-WEBGL1-PREPARED-COMMAND-VERSION";
        if ((r.contractVersion < 5
                && (r.maxVertexStreams || r.maxDescriptorSets || r.maxSampledTextures))
            || (r.contractVersion == 5
                && (!r.maxVertexStreams || !r.maxDescriptorSets || !r.maxSampledTextures)))
            return "RHI-ORDERED-GRAPHICS-CATALOG-BUDGET";
        if ((r.contractVersion == 1 && r.maxPipelines)
            || (r.contractVersion >= 2 && !r.maxPipelines))
            return "RHI-WEBGL1-PREPARED-PIPELINE-BUDGET";
        if ((r.contractVersion < 3 && (r.maxTargets || r.maxPasses || r.maxTransitions))
            || (r.contractVersion >= 3 && (!r.maxTargets || !r.maxPasses || !r.maxTransitions)))
            return "RHI-WEBGL1-PREPARED-PASS-BUDGET";
        if (!r.prototypeOptIn)
            return "RHI-WEBGL1-PREPARED-COMMAND-PROTOTYPE-OPT-IN";
        if (const auto* id = OrderedCommandIssueLowering::validate(r.issue))
            return id;
        const std::uint64_t operations =
            std::uint64_t{r.maxDraws} + 2 * std::uint64_t{r.contractVersion >= 3 ? r.maxPasses : 1};
        if (!r.maxDraws || operations >= UINT32_MAX || !r.maxUniformBytes || !r.maxAnalysisIndices
            || r.issue.maxOperations < operations || !r.issue.maxResourceOwners
            || !r.issue.maxOwnerReferences
            || r.issue.maxSnapshotBytes < operations * sizeof(std::uint32_t))
            return "RHI-WEBGL1-PREPARED-COMMAND-BUDGET";
        return nullptr;
    }

    // Version4 explicitly admits different immutable shader/layout/input ABIs
    // BETWEEN render passes. An active pass retains Version1-3 compatibility.
    static const char* pipelineSwitch(
        const PreparedGraphicsCommandsRequest& r, bool sameAbi, bool boundInPass) noexcept
    {
        if (sameAbi)
            return nullptr;
        if (r.contractVersion < 4)
            return "RHI-WEBGL1-PREPARED-PIPELINE-ABI-MISMATCH";
        return boundInPass ? "RHI-ORDERED-GRAPHICS-ABI-PASS-BOUNDARY-REQUIRED" : nullptr;
    }
};

struct PreparedGraphicsCatalogShape {
    std::size_t vertexStreams = 0, descriptorSets = 0, sampledTextures = 0;
    bool topologyMatchesFirst = true;
};

struct PreparedGraphicsCatalogDecision {
    std::size_t vertexStreams = 0, descriptorSets = 0, sampledTextures = 0;
    const char* diagnostic = nullptr;
};

// Original pipeline/layout owners supply these facts. This decision only
// sizes CPU custody; it grants no native execution or GPU completion.
class PreparedGraphicsCatalogContract final {
public:
    static PreparedGraphicsCatalogDecision assess(const PreparedGraphicsCommandsRequest& request,
        core::ArrayView<PreparedGraphicsCatalogShape> shapes) noexcept
    {
        PreparedGraphicsCatalogDecision result;
        if (const auto* id = PreparedGraphicsCommandsContract::validate(request))
            return {0, 0, 0, id};
        if (!shapes.data() || shapes.empty()
            || shapes.size() > (request.contractVersion == 1 ? 1 : request.maxPipelines))
            return {0, 0, 0, "RHI-WEBGL1-PREPARED-PIPELINE-CAPACITY"};
        for (const auto& shape : shapes) {
            if (request.contractVersion < 5 && !shape.topologyMatchesFirst)
                return {0, 0, 0, "RHI-ORDERED-GRAPHICS-CATALOG-TOPOLOGY"};
            if (shape.vertexStreams > result.vertexStreams)
                result.vertexStreams = shape.vertexStreams;
            if (shape.descriptorSets > result.descriptorSets)
                result.descriptorSets = shape.descriptorSets;
            if (shape.sampledTextures > result.sampledTextures)
                result.sampledTextures = shape.sampledTextures;
        }
        if (request.contractVersion == 5
            && (result.vertexStreams > request.maxVertexStreams
                || result.descriptorSets > request.maxDescriptorSets
                || result.sampledTextures > request.maxSampledTextures))
            return {0, 0, 0, "RHI-ORDERED-GRAPHICS-CATALOG-CAPACITY"};
        return result;
    }
};
} // namespace arp::rhi::semantics

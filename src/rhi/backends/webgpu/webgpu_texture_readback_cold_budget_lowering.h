#pragma once
#include "core/strided_index_range.h"
#include "rhi/backends/webgpu/webgpu_native_ordinary_recording.h"
#include "rhi/backends/webgpu/webgpu_native_ordinary_submission.h"
#include "rhi/backends/webgpu/webgpu_readback_results.h"
#include "rhi/frontend/transfer_readback_cold_budget.h"

namespace arp::rhi_webgpu {
class WebGpuTextureReadbackColdBudgetLowering final {
    using Plan = rhi::TextureReadbackColdBudgetPlan;

public:
    struct Outcome {
        bool privateBudgetMappingImplemented = false, validatorOnly = false;
        const char* diagnostic = "RHI-READBACK-COLD-BUDGET-NATIVE-LOWERING-UNIMPLEMENTED";
    };

    static constexpr Outcome lowering(rhi::Backend backend) noexcept
    {
        switch (backend) {
        case rhi::Backend::Null:
            return {false, true, "RHI-READBACK-COLD-BUDGET-VALIDATOR-ONLY"};
        case rhi::Backend::Vulkan:
        case rhi::Backend::Metal:
            return {};
        case rhi::Backend::WebGPU:
            return {true, false, "RHI-READBACK-COLD-BUDGET-PRIVATE-RECIPE-ONLY"};
        case rhi::Backend::WebGL:
            break;
        }
        return {};
    }

    struct Recipe {
        std::shared_ptr<const Plan> originalPlan;
        rhi::TextureReadbackInputSnapshotBudget input;
        rhi::TextureReadbackLayoutBudget layout;
        rhi::TextureReadbackRequestDeclarationBudget request;
        rhi::TextureReadbackSourceLeaseBudget sourceLease;
        WebGpuOriginalTextureReadbackSourceBudget source;
        WebGpuOriginalTextureReadbackDestinationBudget destination;
        WebGpuOriginalTextureReadbackPairBudget pair;
        WebGpuGenericCommandProjectionBudget projection;
        WebGpuGenericCommandUsageBudget usage;
        rhi::CommandVariableArgumentsBudget variables;
        WebGpuFrozenCommandScalarsBudget scalars;
        WebGpuOriginalCommandInputsBudget inputs;
        WebGpuOriginalResourceStatesBudget states;
        WebGpuOrdinaryCompletionBudget completion;
        rhi::DeviceLossRegistrationBudget deviceLoss;
        WebGpuOriginalTextureReadbackRecordingBudget recording;
        WebGpuOrdinarySubmissionBudget submission;
        WebGpuOriginalTextureReadbackMappingBudget mapping;
        WebGpuOriginalTextureReadbackPublicationBudget publication;
        rhi::TextureReadbackStatisticsPlanBudget statistics;
        WebGpuOriginalTextureReadbackNumericalBudget numerical;

        bool nativeExecutionProven() const noexcept { return false; }
    };

    static const char* lowerOriginal(
        const std::shared_ptr<const Plan>& plan, Recipe& output) noexcept
    {
        if (output.originalPlan)
            return "RHI-READBACK-COLD-BUDGET-OUTPUT-BUSY";
        if (const auto* id = Plan::validateOriginal(plan))
            return id;
        const auto& b = plan->budget();
        if (b.maxCreationCallbacks < WebGpuOriginalTextureReadbackDestination::callbackCount
            || b.maxRecordingCallbacks < WebGpuOrdinaryRecording::callbackCount
            || b.maxSubmissionCallbacks < WebGpuOrdinarySubmission::callbackCount
            || b.maxMappingCallbacks < WebGpuOriginalTextureReadbackMapping::callbackCount
                    + WebGpuOriginalTextureReadbackMapping::scopeCallbackCount)
            return "RHI-READBACK-COLD-BUDGET-CALLBACK-CAPACITY";
        std::uint32_t callbacks = 0u, mips = 0u;
        if (const auto* id = Plan::aggregatePublicationCounts(b, callbacks, mips))
            return id;
        constexpr auto one = Plan::singleRequestGraphCount;
        Recipe recipe;
        recipe.input = {1u,
            true,
            b.maxMipCopies,
            b.maxLabelBytesPerRequest,
            b.maxCpuRequestBytes,
            b.maxCpuRequestBytes};
        recipe.layout = {
            1u, true, b.maxMipCopies, b.maxResultBytesPerRequest, b.maxCpuRequestBytes};
        recipe.request = {1u,
            true,
            b.maxMipCopies,
            b.maxLabelBytesPerRequest,
            b.maxResultBytesPerRequest,
            b.maxCpuRequestBytes};
        recipe.sourceLease = {1u,
            true,
            b.maxResourceOwners,
            b.maxMipCopies,
            b.maxLabelBytesPerRequest,
            b.maxCpuRequestBytes};
        recipe.source = {1u,
            true,
            b.maxResourceOwners,
            b.maxMipCopies,
            b.maxSourceAllocationBytes,
            b.maxCpuRequestBytes};
        recipe.destination = {1u,
            true,
            b.maxResourceOwners,
            b.maxCreationCallbacks,
            b.maxSourceAllocationBytes,
            b.maxStagingBytesPerRequest,
            b.maxCpuRequestBytes,
            b.maxCpuResourceBytes,
            b.maxCpuResultBytes};
        const auto& g = b.graph;
        recipe.pair = {1u,
            true,
            one,
            g.maxCompletedSourceGraphs,
            g.maxProjectedOwners,
            b.maxMipCopies,
            g.maxAllocationRecords,
            b.maxNativeBytesPerRequest,
            b.maxContentBytesPerRequest,
            b.maxResultBytesPerRequest,
            g.maxCpuMetadataBytes};
        recipe.projection.contractVersion = 1u;
        recipe.projection.prototypeOptIn = true;
        recipe.projection.maxReferences = g.maxReferences;
        recipe.projection.maxAllocationReferences = g.maxAllocationRecords;
        recipe.projection
            .maxDomainReferences[static_cast<std::size_t>(WebGpuCommandReferenceDomain::Texture)] =
            g.maxTextureReferences;
        recipe.projection
            .maxDomainReferences[static_cast<std::size_t>(WebGpuCommandReferenceDomain::Readback)] =
            g.maxReadbackReferences;
        recipe.projection.maxCpuMetadataBytes = g.maxCpuMetadataBytes;
        recipe.projection.maxRetainedAllocationBytes = b.maxNativeBytesPerRequest;
        recipe.projection.maxOriginalReadbackPairs = one;
        recipe.projection.maxReadbackMipCopies = b.maxMipCopies;
        recipe.projection.maxReadbackRetainedContentBytes = b.maxContentBytesPerRequest;
        recipe.projection.maxReadbackUnpublishedResultBytes = b.maxResultBytesPerRequest;
        recipe.projection.maxReadbackStateReferences = g.maxStateReferences;
        recipe.usage = {
            1u, true, b.maxOriginalCommands, g.maxReferenceUsages, g.maxCpuMetadataBytes};
        recipe.variables = {
            1u, true, b.maxOriginalCommands, 0u, g.maxCommandTextBytes, 0u, g.maxCpuMetadataBytes};
        recipe.scalars = {1u, true, b.maxOriginalCommands, b.maxDebugDepth, g.maxCpuMetadataBytes};
        recipe.inputs = {1u, true, g.maxDescriptorRecords, g.maxCpuMetadataBytes};
        recipe.inputs.maxReadbackDescriptors = g.maxReadbackDescriptors;
        recipe.inputs.maxReadbackMipMappings = b.maxMipCopies;
        recipe.inputs.maxReadbackUnpublishedResultBytes = b.maxResultBytesPerRequest;
        recipe.states = {
            1u, true, g.maxSourceReferences, b.maxStateControls, g.maxCpuMetadataBytes};
        recipe.completion = {1u, true, b.maxPendingRequests, b.maxCpuDeviceBytes};
        recipe.deviceLoss = b.deviceLoss;
        recipe.recording.contractVersion = 1u;
        recipe.recording.prototypeOptIn = true;
        recipe.recording.native = {1u,
            true,
            b.maxOriginalCommands,
            b.maxNativeCommands,
            b.maxEncoders,
            b.maxDebugScopes,
            b.maxCommandBuffers,
            b.maxRecordingCallbacks,
            b.maxCpuRequestBytes};
        recipe.recording.transaction = {1u,
            true,
            one,
            b.maxMipCopies,
            one,
            b.maxResultBytesPerRequest,
            {1u,
                true,
                b.maxStateControls,
                b.maxStateTransitions,
                b.maxDebugDepth,
                b.maxCpuRequestBytes}};
        recipe.recording.copies = {1u,
            true,
            one,
            b.maxMipCopies,
            b.maxMipCopies,
            b.maxNativeBytesPerRequest,
            b.maxContentBytesPerRequest,
            b.maxResultBytesPerRequest,
            b.maxCpuRequestBytes,
            one};
        recipe.recording.maxMipCopies = b.maxMipCopies;
        recipe.submission = {1u, true, one, b.maxSubmissionCallbacks, b.maxCpuRequestBytes};
        recipe.mapping = {1u,
            true,
            one,
            b.maxMappingCallbacks - WebGpuOriginalTextureReadbackMapping::scopeCallbackCount,
            one,
            b.maxMipCopies,
            b.maxNativeBytesPerRequest,
            b.maxContentBytesPerRequest,
            b.maxResultBytesPerRequest,
            b.maxCpuRequestBytes,
            b.maxCpuRequestBytes,
            WebGpuOriginalTextureReadbackMapping::scopeCallbackCount};
        recipe.publication = {1u,
            true,
            b.maxPendingRequests,
            static_cast<std::uint32_t>(callbacks),
            static_cast<std::uint32_t>(mips),
            b.maxDeviceRetainedNativeBytes,
            b.maxDeviceRetainedContentBytes,
            b.maxDeviceRetainedResultBytes,
            b.maxDeviceRetainedLabelBytes,
            b.maxCpuDeviceBytes,
            b.maxCpuRequestBytes};
        recipe.statistics = {1u,
            true,
            b.maxStatisticsViews,
            b.maxAnalyzedPixelsPerRequest,
            b.maxResultBytesPerRequest,
            b.maxCpuRequestBytes};
        recipe.numerical = {1u,
            true,
            b.maxStatisticsViews,
            b.maxAnalyzedPixelsPerRequest,
            b.maxResultBytesPerRequest,
            b.maxCpuRequestBytes};
        // Consume every existing contract validator; recipes cannot widen native gates.
        if (const auto* id = rhi::TextureReadbackInputSnapshot::validateBudget(recipe.input))
            return id;
        if (const auto* id = rhi::TextureReadbackLayout::validateBudget(recipe.layout))
            return id;
        if (const auto* id = rhi::TextureReadbackRequestDeclaration::validateBudget(recipe.request))
            return id;
        if (const auto* id = rhi::TextureReadbackSourceLease::validateBudget(recipe.sourceLease))
            return id;
        if (const auto* id = WebGpuOriginalTextureReadbackSource::validateBudget(recipe.source))
            return id;
        if (const auto* id =
                WebGpuOriginalTextureReadbackDestination::validateBudget(recipe.destination))
            return id;
        if (const auto* id =
                WebGpuOriginalTextureReadbackPairProjection::validateBudget(recipe.pair))
            return id;
        if (const auto* id = WebGpuGenericCommandProjection::validateBudget(recipe.projection))
            return id;
        if (const auto* id = WebGpuGenericCommandUsage::validateBudget(recipe.usage))
            return id;
        if (const auto* id = rhi::CommandVariableArguments::validateBudget(recipe.variables))
            return id;
        if (const auto* id = WebGpuFrozenCommandScalars::validateBudget(recipe.scalars))
            return id;
        if (const auto* id = WebGpuOriginalCommandInputs::validateBudget(recipe.inputs))
            return id;
        if (const auto* id = WebGpuOriginalResourceStates::validateBudget(recipe.states))
            return id;
        if (const auto* id = WebGpuOrdinaryCompletionOwner::validateBudget(recipe.completion))
            return id;
        if (const auto* id = WebGpuOrdinaryRecording::validateBudget(recipe.recording))
            return id;
        if (const auto* id = WebGpuOrdinarySubmission::validateBudget(recipe.submission))
            return id;
        if (const auto* id = WebGpuOriginalTextureReadbackMapping::validateBudget(recipe.mapping))
            return id;
        if (const auto* id =
                WebGpuOriginalTextureReadbackPublication::validateBudget(recipe.publication))
            return id;
        if (const auto* id = rhi::TextureReadbackStatisticsPlan::validateBudget(recipe.statistics))
            return id;
        if (const auto* id =
                WebGpuOriginalTextureReadbackNumericalResult::validateBudget(recipe.numerical))
            return id;
        recipe.originalPlan = plan;
        output = std::move(recipe);
        return nullptr;
    }
};
} // namespace arp::rhi_webgpu

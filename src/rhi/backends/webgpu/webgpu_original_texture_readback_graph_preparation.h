#pragma once
#include "core/text/utf8.h"
#include "rhi/backends/webgpu/webgpu_texture_readback_cold_budget_lowering.h"

namespace arp::rhi_webgpu {
// OriginalReadbackCompleteGraphBudget/1's public-leased snapshot composition.
// Cold metadata only. All capacities and quotas come from the original plan;
// callers cannot widen a mutable recipe or substitute an unrelated graph.
class WebGpuOriginalTextureReadbackGraphPreparation final {
    using Plan = rhi::TextureReadbackColdBudgetPlan;
    using Destination = WebGpuOriginalTextureReadbackDestination;

public:
    static std::shared_ptr<const WebGpuOriginalResourceStates> prepareOriginal(
        const std::shared_ptr<const Plan>& plan,
        const std::shared_ptr<const Destination>& destination,
        const rhi::ResourceBarrierDesc& barrier,
        std::string_view passLabel,
        const char*& diagnostic)
    {
        WebGpuTextureReadbackColdBudgetLowering::Recipe recipe;
        diagnostic = WebGpuTextureReadbackColdBudgetLowering::lowerOriginal(plan, recipe);
        if (diagnostic)
            return {};
        if (!destination || !destination->hasQuotaBoundSharedOwner(destination)) {
            diagnostic = "RHI-READBACK-GRAPH-DESTINATION-OWNER";
            return {};
        }
        const auto& deviceBudget = destination->originalDeviceStagingBudget();
        if (!deviceBudget
            || !core::SharedOwnerProjection::sameOwner(deviceBudget->originalPlan(), plan)) {
            diagnostic = "RHI-READBACK-GRAPH-PLAN-MISMATCH";
            return {};
        }
        const auto& quota = plan->quotas()[static_cast<std::size_t>(Plan::CpuDomain::Request)];
        const auto& source = destination->source();
        const auto& lease = source->publicSourceLease();
        if (!lease
            || source->contentMode()
                != rhi::TextureReadbackContentIntent::Mode::QueueOrderedSnapshot) {
            diagnostic = "RHI-READBACK-GRAPH-PUBLIC-SNAPSHOT-REQUIRED";
            return {};
        }
        const auto& request = lease->originalRequest();
        const auto& layout = source->layout();
        const auto& input = layout->inputOwner();
        for (const auto& original : {destination->originalMetadataQuota(),
                 source->originalMetadataQuota(),
                 lease->originalMetadataQuota(),
                 request->originalMetadataQuota(),
                 layout->originalMetadataQuota(),
                 input->originalMetadataQuota(),
                 input->labelOwner()->originalMetadataQuota()})
            if (!core::SharedOwnerProjection::sameOwner(original, quota)) {
                diagnostic = "RHI-READBACK-GRAPH-REQUEST-QUOTA";
                return {};
            }
        if (passLabel.size() > plan->budget().graph.maxPassLabelBytes) {
            diagnostic = "RHI-READBACK-GRAPH-PASS-LABEL-CAPACITY";
            return {};
        }
        if (passLabel.find('\0') != std::string_view::npos
            || core::Utf8Decoder::validate(passLabel)) {
            diagnostic = "RHI-READBACK-GRAPH-PASS-LABEL-ENCODING";
            return {};
        }
        if (barrier.resource != source->resource().id
            || barrier.after != rhi::ResourceState::CopySrc) {
            diagnostic = "RHI-READBACK-GRAPH-SOURCE-BARRIER";
            return {};
        }
        auto pair = WebGpuOriginalTextureReadbackPairProjection::prepareOriginal(
            destination, recipe.pair, quota, diagnostic);
        if (!pair)
            return {};
        using Domain = WebGpuCommandReferenceDomain;
        using Source = WebGpuCommandReferenceSource;
        const std::array<WebGpuCommandReference,
            rhi::TextureReadbackCommandGraphContract::referenceCount>
            references{{{Domain::Texture, Source::OriginalTextureReadbackState, barrier.resource},
                {Domain::Texture, Source::OriginalTextureReadback, source->resource().id},
                {Domain::Readback, Source::OriginalTextureReadback, destination->resourceId()}}};
        WebGpuGenericCommandProjection::Sources sources;
        sources.readbacks = &pair;
        sources.readbackCount = 1u;
        auto projection = WebGpuGenericCommandProjection::prepareOriginalShared(std::move(sources),
            references.data(),
            references.size(),
            recipe.projection,
            quota,
            diagnostic);
        if (!projection)
            return {};
        using Contract = rhi::TextureReadbackCommandGraphContract;
        using K = rhi::CommandUsageKind;
        using Scalar = rhi::CommandScalarArguments;
        using Variables = rhi::CommandVariableArguments;
        const std::array<rhi::CommandUsage, Contract::commandCount> commands{
            {{Contract::commands[0], 0u, 1u},
                {Contract::commands[1], 1u, 0u},
                {Contract::commands[2], 1u, 2u},
                {Contract::commands[3], 3u, 0u},
                {Contract::commands[4], 3u, 0u}}};
        auto usage = WebGpuGenericCommandUsage::prepareOriginal(
            projection, commands.data(), commands.size(), recipe.usage, quota, diagnostic);
        if (!usage)
            return {};
        const std::array<Variables::Input, Contract::commandCount> raw{
            {{*Scalar::capture(K::TextureBarrier, Scalar::None{})},
                {*Scalar::capture(K::BeginTransferPass, Scalar::None{}), passLabel},
                {*Scalar::capture(K::TextureReadback, Scalar::None{}), input->label()},
                {*Scalar::capture(K::EndTransferPass, Scalar::None{})},
                {*Scalar::capture(K::Finish, Scalar::None{})}}};
        auto variables =
            Variables::prepareOriginal(raw.data(), raw.size(), recipe.variables, quota, diagnostic);
        if (!variables)
            return {};
        auto scalars = WebGpuFrozenCommandScalars::prepareOriginal(
            usage, variables, recipe.scalars, quota, diagnostic);
        if (!scalars)
            return {};
        const auto dependency =
            rhi::ResourceDependencyArguments::capture(K::TextureBarrier, barrier);
        const auto readback = rhi::TextureReadbackOriginalInputArguments::captureOriginal(input);
        if (!dependency || !readback) {
            diagnostic = "RHI-READBACK-GRAPH-INPUT-CONTRACT";
            return {};
        }
        const std::array<WebGpuOriginalCommandInputs::OriginalDescriptor, Contract::descriptorCount>
            descriptors{{{0u, *dependency}, {2u, *readback}}};
        auto inputs = WebGpuOriginalCommandInputs::prepareOriginal(
            scalars, {}, descriptors.data(), descriptors.size(), recipe.inputs, quota, diagnostic);
        if (!inputs)
            return {};
        auto states =
            WebGpuOriginalResourceStates::prepareOriginal(inputs, recipe.states, quota, diagnostic);
        if (!states)
            return {};
        if ((diagnostic = WebGpuOriginalTransferStateValidation::validate(states, 0u).diagnostic))
            return {};
        return states;
    }
};
} // namespace arp::rhi_webgpu

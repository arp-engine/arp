#include "renderer/frame/render_frame_director.h"

#include "renderer/pipeline/scene_runtime_pipeline_payload.h"
#include "renderer/scene_runtime_frame.h"
#include "renderer/scene_runtime_frame_rhi_planner.h"

#include <array>
#include <cstdint>
#include <utility>

namespace arp::renderer {

namespace {

using SectionArray = std::array<const CustomWorkloadSection*, 5u>;

SectionArray enabledSections(const CustomRenderFrameWorkload& workload)
{
    return {
        &workload.scene(),
        &workload.particles(),
        &workload.gui(),
        &workload.postprocess(),
        &workload.globalIllumination(),
    };
}

arp::core::pmr::String firstAdapterDiagnostic(const RenderFramePayloadAdapterResult& result)
{
    if (result.diagnostics.empty()) {
        return "RenderFrameDirector payload adapter failed without diagnostics.";
    }
    return result.diagnostics[0].message;
}

arp::core::pmr::String firstProviderDiagnostic(const RenderPassProviderResult& result)
{
    if (result.diagnostics.empty()) {
        return "RenderFrameDirector pass provider failed without diagnostics.";
    }
    return result.diagnostics[0].message;
}

bool isUnknownAdapterResult(const RenderFramePayloadAdapterResult& result)
{
    return !result.diagnostics.empty()
        && result.diagnostics[0].code == RenderFramePayloadAdapterDiagnosticCode::UnknownAdapter;
}

bool isBackbufferAttachment(const RenderFrameAttachmentUse& use)
{
    return use.attachmentId == "Backbuffer";
}

struct PreparedSubmitBuildResult {
    bool success = true;
    bool required = false;
    RenderFramePreparedSubmitRequirement requirement;
    arp::core::pmr::String diagnostics;
};

std::uint32_t sceneDrawCommandCount(const RendererRhiFramePlanDesc& framePlan)
{
    std::uint32_t count = 0;
    for (const auto& drawPlan : framePlan.drawPlans) {
        if (drawPlan.workKind == RendererRhiWorkKind::Scene) {
            count += drawPlan.drawCommandCount;
        }
    }
    return count;
}

const RendererSceneRuntimeFrame* sceneRuntimeFrameFromIntent(const RenderFrameIntent& intent)
{
    if (intent.sceneRequirements.empty()) {
        return nullptr;
    }
    return static_cast<const RendererSceneRuntimeFrame*>(
        intent.sceneRequirements.front().rendererFrame);
}

RendererSceneRuntimeFrameRhiPlannerDesc plannerDescFrom(
    const RenderFramePreparedRendererFrameConfig& config,
    const RendererRhiFramePlanDesc* preparedFramePlan)
{
    RendererSceneRuntimeFrameRhiPlannerDesc desc;
    desc.frameSlot = preparedFramePlan != nullptr ? preparedFramePlan->frameSlot : 0u;
    desc.descriptorSetCount = 2u;
    desc.descriptorLayoutNameCount = 2u;
    desc.maxMaterialResourceCount = config.maxMaterialResourceCount;
    desc.pushConstantSizeBytes = config.pushConstantSizeBytes;
    desc.pipelinePushConstantCapacityBytes = config.pipelinePushConstantCapacityBytes;
    desc.maxDrawCommandCount = config.maxDrawCommandsPerPass;
    return desc;
}

RendererSceneRuntimeFrameRhiPlanRequest plannerRequestFrom(
    const RenderFrameIntent& intent, const RendererSceneRuntimeFrame& frame)
{
    RendererSceneRuntimeFrameRhiPlanRequest request;
    request.frame = &frame;
    request.requiresMultiplePhysicalPasses = intent.requiresMultiplePhysicalPasses;
    request.hasExplicitAttachmentContract = intent.hasExplicitAttachmentContract;
    request.hasExplicitPostprocessContract = intent.hasExplicitPostprocessContract;
    request.hasExplicitOffscreenContract = intent.hasExplicitOffscreenContract;
    return request;
}

PreparedSubmitBuildResult buildPreparedSubmitRequirement(
    const RenderFrameIntent& intent, const RenderFramePreparedRendererFrameConfig& config)
{
    PreparedSubmitBuildResult result;
    if (intent.contractName != kSceneRuntimePipelinePayloadContract) {
        return result;
    }

    result.required = true;
    const auto* frame = sceneRuntimeFrameFromIntent(intent);
    if (frame == nullptr) {
        result.success = false;
        result.diagnostics =
            "RenderFrameDirector prepared submit requires a scene-runtime renderer frame.";
        return result;
    }

    const auto expectedDrawCount = static_cast<std::uint32_t>(frame->drawItemCount());
    RendererRhiFramePlanDesc framePlan;
    if (config.preparedFramePlan != nullptr) {
        framePlan = *config.preparedFramePlan;
    } else {
        RendererSceneRuntimeFrameRhiPlanner planner(
            plannerDescFrom(config, config.preparedFramePlan));
        const auto planned = planner.plan(plannerRequestFrom(intent, *frame));
        if (!planned.ok()) {
            result.success = false;
            result.diagnostics = "RenderFrameDirector prepared submit planning failed: "
                + planned.diagnostic().message;
            return result;
        }
        framePlan = planned.framePlan();
    }

    if (sceneDrawCommandCount(framePlan) != expectedDrawCount) {
        result.success = false;
        result.diagnostics =
            "RenderFrameDirector prepared submit rejected a payload whose draw contract differs from the prepared frame plan.";
        return result;
    }

    result.requirement.id = "SceneRuntimePreparedRendererFrame";
    result.requirement.required = true;
    result.requirement.plannerContractName = "renderer/SceneRuntimeRhiFramePlan";
    result.requirement.expectedDrawItemCount = expectedDrawCount;
    result.requirement.framePlan = std::move(framePlan);
    result.requirement.framePlanBridge = config.framePlanBridge;
    result.requirement.maxDrawCommandsPerPass = config.maxDrawCommandsPerPass;
    result.requirement.shaderArtifactRootPolicy = config.shaderArtifactRootPolicy.empty()
        ? "DefaultRendererRhiShaderArtifactRoot"
        : config.shaderArtifactRootPolicy;
    result.requirement.backendSpecificBranchingRequired = config.backendSpecificBranchingRequired;
    result.requirement.nativeHandleExposedToUpperLayer = config.nativeHandleExposedToUpperLayer;
    return result;
}

void recordPassDeclaration(render_graph::FrameGraph& graph,
    render_graph::TextureHandle backbuffer,
    const RenderPassDeclaration& pass)
{
    auto builder = graph.addPass(pass.id);
    for (const auto& attachmentUse : pass.attachmentUses) {
        if (!isBackbufferAttachment(attachmentUse)) {
            continue;
        }
        if (attachmentUse.access == RenderFrameAttachmentAccess::Read) {
            builder.read(backbuffer, attachmentUse.state);
        } else if (attachmentUse.state == rhi::ResourceState::ColorAttachment) {
            builder.writeAttachment(
                backbuffer, attachmentUse.state, rhi::LoadOp::Load, rhi::StoreOp::Store);
        } else {
            builder.write(backbuffer, attachmentUse.state);
        }
    }
    builder.execute([](render_graph::RenderContext&) {});
}

} // namespace

RenderFrameDirector::RenderFrameDirector(RenderFrameDirectorDesc desc) : desc_(desc)
{
    if (desc_.installDefaultAdaptersAndProviders) {
        payloadAdapters_.registerAdapter(sceneRuntimePayloadAdapter_);
        passProviders_.registerProvider(sceneRuntimeWorldUiPassProvider_);
    }
}

RenderFramePayloadAdapterRegistrationResult RenderFrameDirector::registerPayloadAdapter(
    const IRenderFramePayloadAdapter& adapter)
{
    return payloadAdapters_.registerAdapter(adapter);
}

RenderPassProviderRegistrationResult RenderFrameDirector::registerPassProvider(
    const IRenderPassProvider& provider)
{
    return passProviders_.registerProvider(provider);
}

RenderFeatureResult RenderFrameDirector::registerFeature(IExecutableRenderFeature& feature)
{
    return features_.registerFeature(feature);
}

RenderFeatureResult RenderFrameDirector::createFeatures(const RenderFeatureCreateContext& context)
{
    return features_.create(context);
}

RenderFeatureResult RenderFrameDirector::resizeFeatures(rhi::Extent2D extent)
{
    return features_.resize(extent);
}

RenderFeatureResult RenderFrameDirector::contributeFeatures(
    const RenderFeatureGraphContext& context)
{
    return features_.contributeGraph(context);
}

void RenderFrameDirector::releaseFeatures() noexcept
{
    features_.release();
}

RenderFrameDirectorResult RenderFrameDirector::failure(
    RenderFrameDirectorDiagnosticCode code, const arp::core::pmr::String& message) const
{
    RenderFrameDirectorResult result;
    result.success = false;
    result.decision.mode = RenderFrameExecutionMode::DiagnosticFailure;
    result.diagnostics.push_back({code, message});
    return result;
}

RenderFrameDirectorResult RenderFrameDirector::legacyFallback(
    const RenderFrameDirectorRequest& request, const arp::core::pmr::String& reason) const
{
    if (!request.workload.allowsPrototypeLegacyFallback()) {
        arp::core::pmr::String diagnostic =
            "RenderFrameDirector rejected legacy pipeline fallback for a workload without an explicit Prototype/Legacy fallback resource contract: ";
        diagnostic += reason;
        return failure(RenderFrameDirectorDiagnosticCode::LegacyPipelineFallbackUsed, diagnostic);
    }

    if (request.legacyPipeline == nullptr) {
        return failure(RenderFrameDirectorDiagnosticCode::MissingLegacyPipeline, reason);
    }

    auto compositionContext = request.compositionContext;
    if (request.workload.legacySceneFallback().enabled()) {
        const auto& fallbackPayload = request.workload.legacySceneFallback().payload();
        compositionContext.request.pipelinePayload = fallbackPayload.object;
        compositionContext.request.pipelinePayloadContractName = fallbackPayload.contractName;
        compositionContext.request.pipelinePayloadContractVersion = fallbackPayload.contractVersion;
    }

    auto frameFeatures = request.frameFeatures;
    const auto composed = request.legacyPipeline->composeGraph(
        request.graph, request.backbuffer, compositionContext, std::move(frameFeatures));
    if (!composed.success) {
        return failure(
            RenderFrameDirectorDiagnosticCode::LegacyPipelineFailed, composed.diagnostics);
    }

    RenderFrameDirectorResult result;
    result.success = true;
    result.decision.mode = RenderFrameExecutionMode::LegacyPipelineFallback;
    result.decision.statusLabel = "LegacyPipelineFallback";
    arp::core::pmr::String diagnostic = "Prototype/Legacy fallback: ";
    diagnostic += reason;
    result.diagnostics.push_back({
        RenderFrameDirectorDiagnosticCode::LegacyPipelineFallbackUsed,
        std::move(diagnostic),
    });
    return result;
}

RenderFrameDirectorResult RenderFrameDirector::compose(
    const RenderFrameDirectorRequest& request) const
{
    const auto workloadValidation = request.workload.validate();
    if (!workloadValidation.success) {
        return failure(RenderFrameDirectorDiagnosticCode::WorkloadValidationFailed,
            workloadValidation.diagnostics);
    }

    RenderFrameDirectorResult result;
    result.success = true;
    result.decision.mode = RenderFrameExecutionMode::ComposeGraph;
    result.decision.statusLabel = "Prototype Complete";

    bool sawEnabledPayload = false;
    bool emittedProviderPass = false;
    for (const auto* section : enabledSections(request.workload)) {
        if (section == nullptr || !section->enabled()) {
            continue;
        }
        sawEnabledPayload = true;

        const auto adapted = payloadAdapters_.adaptPayload(section->payload());
        if (!adapted.success) {
            if (isUnknownAdapterResult(adapted)) {
                return legacyFallback(request, firstAdapterDiagnostic(adapted));
            }
            return failure(RenderFrameDirectorDiagnosticCode::PayloadAdapterFailed,
                firstAdapterDiagnostic(adapted));
        }

        const auto provided = passProviders_.composePasses(adapted.intent);
        if (!provided.success) {
            return failure(RenderFrameDirectorDiagnosticCode::PassProviderFailed,
                firstProviderDiagnostic(provided));
        }

        for (const auto& pass : provided.passes) {
            recordPassDeclaration(request.graph, request.backbuffer, pass);
            result.decision.graphPasses.push_back({
                pass.id,
                pass.stage,
                pass.attachmentUses,
                false,
            });
            emittedProviderPass = true;
        }

        auto preparedSubmit =
            buildPreparedSubmitRequirement(adapted.intent, request.preparedRendererFrame);
        if (!preparedSubmit.success) {
            return failure(RenderFrameDirectorDiagnosticCode::PreparedSubmitRejected,
                preparedSubmit.diagnostics);
        }
        if (preparedSubmit.required) {
            result.decision.mode = RenderFrameExecutionMode::PreparedSubmit;
            result.decision.statusLabel = "Contract Complete";
            result.decision.preparedSubmitRequirements.push_back(
                std::move(preparedSubmit.requirement));
        }
    }

    if (sawEnabledPayload && !emittedProviderPass && request.legacyPipeline != nullptr) {
        return legacyFallback(
            request, "RenderFrameDirector found no provider coverage for enabled payloads.");
    }

    return result;
}

} // namespace arp::renderer

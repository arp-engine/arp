#include "renderer/rhi_draw_plan_bridge.h"

#include "core/cpu_allocator.h"
#include "core/diagnostic_builder.h"

namespace arp::renderer {

namespace {

void addRequirement(render_graph::RhiFramePlanBuilder& builder,
    rhi::EngineFeature feature,
    const std::pmr::string& reason,
    rhi::FeatureCorrectnessImpact impact = rhi::FeatureCorrectnessImpact::PreservesCorrectness)
{
    builder.requireFeature(feature, impact, reason);
}

struct RendererFeatureRequirement {
    rhi::EngineFeature feature = rhi::EngineFeature::PushConstants;
    rhi::FeatureCorrectnessImpact impact = rhi::FeatureCorrectnessImpact::PreservesCorrectness;
    const char* reason = "";
};

bool sameRequirement(const RendererFeatureRequirement& lhs, const RendererFeatureRequirement& rhs)
{
    return lhs.feature == rhs.feature && lhs.impact == rhs.impact;
}

render_graph::PassKind passKindFor(RendererRhiWorkKind workKind)
{
    switch (workKind) {
    case RendererRhiWorkKind::Scene:
    case RendererRhiWorkKind::Gui:
    case RendererRhiWorkKind::Postprocess:
    case RendererRhiWorkKind::Particle:
    case RendererRhiWorkKind::Shadow:
        return render_graph::PassKind::Graphics;
    case RendererRhiWorkKind::Compute:
        return render_graph::PassKind::Compute;
    case RendererRhiWorkKind::Transfer:
        return render_graph::PassKind::Copy;
    }

    return render_graph::PassKind::Graphics;
}

render_graph::QueueRole queueRoleFor(RendererRhiWorkKind workKind)
{
    switch (workKind) {
    case RendererRhiWorkKind::Scene:
    case RendererRhiWorkKind::Gui:
    case RendererRhiWorkKind::Postprocess:
    case RendererRhiWorkKind::Particle:
    case RendererRhiWorkKind::Shadow:
        return render_graph::QueueRole::Graphics;
    case RendererRhiWorkKind::Compute:
        return render_graph::QueueRole::Compute;
    case RendererRhiWorkKind::Transfer:
        return render_graph::QueueRole::Transfer;
    }

    return render_graph::QueueRole::Graphics;
}

std::uint32_t effectiveDispatchCount(const RendererRhiDrawPlanDesc& drawPlan)
{
    // Dispatch dimensions are executable metadata. A draw count or a feature
    // flag is not a legal substitute for the workgroup dimensions.
    return drawPlan.dispatchCount;
}

void validateWorkInputs(const RendererRhiDrawPlanDesc& drawPlan, std::pmr::string& diagnostics)
{
    switch (drawPlan.workKind) {
    case RendererRhiWorkKind::Scene:
    case RendererRhiWorkKind::Gui:
    case RendererRhiWorkKind::Postprocess:
    case RendererRhiWorkKind::Particle:
    case RendererRhiWorkKind::Shadow:
        if (drawPlan.drawCommandCount == 0) {
            arp::core::appendDiagnostic(diagnostics, "renderer RHI graphics draw plan is empty");
        }
        break;
    case RendererRhiWorkKind::Compute:
        if (effectiveDispatchCount(drawPlan) == 0) {
            arp::core::appendDiagnostic(diagnostics, "renderer RHI compute draw plan is empty");
        }
        break;
    case RendererRhiWorkKind::Transfer:
        if (drawPlan.transferCommandCount == 0) {
            arp::core::appendDiagnostic(diagnostics, "renderer RHI transfer draw plan is empty");
        }
        break;
    }
}

void validateDescriptorInputs(
    const RendererRhiDrawPlanDesc& drawPlan, std::pmr::string& diagnostics)
{
    if (drawPlan.descriptorLayoutNameCount > render_graph::kMaxRhiPassDescriptorLayoutNames) {
        arp::core::appendDiagnostic(
            diagnostics, "renderer RHI descriptor layout name capacity exhausted");
        return;
    }

    if (drawPlan.descriptorSetCount > 0
        && drawPlan.descriptorLayoutNameCount < drawPlan.descriptorSetCount) {
        arp::core::appendDiagnostic(
            diagnostics, "renderer RHI descriptor layout names are incomplete");
    }

    for (std::uint32_t index = 0; index < drawPlan.descriptorLayoutNameCount; ++index) {
        if (drawPlan.descriptorLayoutNames[index].empty()) {
            arp::core::appendDiagnostic(
                diagnostics, "renderer RHI descriptor layout name is empty");
            return;
        }
    }
}

void validateMaterialInputs(const RendererRhiDrawPlanDesc& drawPlan, std::pmr::string& diagnostics)
{
    if (drawPlan.materialResourceCount < drawPlan.requiredMaterialResourceCount) {
        arp::core::appendDiagnostic(
            diagnostics, "renderer RHI material resource bindings are incomplete");
    }
}

void validatePipelineInputs(const RendererRhiDrawPlanDesc& drawPlan, std::pmr::string& diagnostics)
{
    if (!drawPlan.expectedPipelineLayoutName.empty()
        && drawPlan.pipelineLayoutName != drawPlan.expectedPipelineLayoutName) {
        arp::core::appendDiagnostic(
            diagnostics, "renderer RHI pipeline layout does not match draw plan");
    }

    if (drawPlan.pipelinePushConstantCapacityBytes > 0
        && drawPlan.pushConstantSizeBytes > drawPlan.pipelinePushConstantCapacityBytes) {
        arp::core::appendDiagnostic(
            diagnostics, "renderer RHI push constant bytes exceed pipeline layout capacity");
    }
}

void validateUploadInputs(const RendererRhiDrawPlanDesc& drawPlan, std::pmr::string& diagnostics)
{
    if (drawPlan.requiresBackendOwnedVertexUpload && drawPlan.maxUploadBytes == 0) {
        arp::core::appendDiagnostic(diagnostics,
            "renderer RHI vertex upload path requires an explicit bounded upload size");
    }
}

void validateRhiInputContract(
    const RendererRhiDrawPlanDesc& drawPlan, std::pmr::string& diagnostics)
{
    if (!drawPlan.hasInputContract) {
        return;
    }

    const rhi::RhiFrameContractValidator validator({2});
    const auto result = validator.validateRendererInput(drawPlan.inputContract);
    arp::core::appendDiagnostic(diagnostics, result.diagnostics);
}

void validateDrawPlanInputs(const RendererRhiDrawPlanDesc& drawPlan, std::pmr::string& diagnostics)
{
    validateWorkInputs(drawPlan, diagnostics);
    validateDescriptorInputs(drawPlan, diagnostics);
    validateMaterialInputs(drawPlan, diagnostics);
    validatePipelineInputs(drawPlan, diagnostics);
    validateUploadInputs(drawPlan, diagnostics);
    validateRhiInputContract(drawPlan, diagnostics);
}

render_graph::RhiPassRecord makePassRecord(const RendererRhiDrawPlanDesc& drawPlan)
{
    render_graph::RhiPassRecord pass;
    pass.name = drawPlan.name;
    pass.kind = passKindFor(drawPlan.workKind);
    pass.queue = queueRoleFor(drawPlan.workKind);
    pass.drawCommandCount = drawPlan.drawCommandCount;
    pass.dispatchCount = effectiveDispatchCount(drawPlan);
    pass.transferCommandCount = drawPlan.transferCommandCount;
    pass.drawCommands = drawPlan.drawCommands;
    pass.dispatchCommands = drawPlan.dispatchCommands;
    pass.transferCommands = drawPlan.transferCommands;
    pass.pushConstantSizeBytes = drawPlan.pushConstantSizeBytes;
    pass.descriptorSetCount = drawPlan.descriptorSetCount;
    pass.descriptorTableCount = drawPlan.descriptorTableCount;
    pass.descriptorLayoutNameCount = drawPlan.descriptorLayoutNameCount;
    pass.descriptorLayoutNames = drawPlan.descriptorLayoutNames;
    pass.pipelineLayoutName = drawPlan.pipelineLayoutName;
    pass.pipelinePushConstantCapacityBytes = drawPlan.pipelinePushConstantCapacityBytes;
    pass.requiredMaterialResourceCount = drawPlan.requiredMaterialResourceCount;
    pass.materialResourceCount = drawPlan.materialResourceCount;
    pass.usesStorageTextureBinding = drawPlan.usesStorageTextureBinding;
    pass.hasDepthTarget = drawPlan.hasDepthTarget;
    pass.requiresBackendOwnedVertexUpload = drawPlan.requiresBackendOwnedVertexUpload;
    pass.maxUploadBytes = drawPlan.maxUploadBytes;
    return pass;
}

void appendUniqueRequirement(RendererFeatureRequirement* requirements,
    std::uint32_t& requirementCount,
    std::uint32_t maxRequirementCount,
    const RendererFeatureRequirement& requirement)
{
    for (std::uint32_t index = 0; index < requirementCount; ++index) {
        if (sameRequirement(requirements[index], requirement)) {
            return;
        }
    }

    if (requirementCount < maxRequirementCount) {
        requirements[requirementCount] = requirement;
        ++requirementCount;
    }
}

void collectRequirements(const RendererRhiDrawPlanDesc& drawPlan,
    RendererFeatureRequirement* requirements,
    std::uint32_t& requirementCount,
    std::uint32_t maxRequirementCount)
{
    if (drawPlan.pushConstantSizeBytes > 0) {
        appendUniqueRequirement(requirements,
            requirementCount,
            maxRequirementCount,
            {
                rhi::EngineFeature::PushConstants,
                rhi::FeatureCorrectnessImpact::PreservesCorrectness,
                "renderer frame draw plan push constants",
            });
    }
    // This flag permits an optional optimization. The renderer frame
    // executor currently records primary command encoders and has no generic
    // secondary/render-bundle execution path, so permission must not become a
    // required capability or a CPU-replay promotion.
    if (drawPlan.descriptorTableCount > 0) {
        appendUniqueRequirement(requirements,
            requirementCount,
            maxRequirementCount,
            {
                rhi::EngineFeature::DescriptorTable,
                rhi::FeatureCorrectnessImpact::PerformanceOnly,
                "renderer frame descriptor table",
            });
    }
    if (drawPlan.usesTransientTargets) {
        appendUniqueRequirement(requirements,
            requirementCount,
            maxRequirementCount,
            {
                rhi::EngineFeature::TransientAliasing,
                rhi::FeatureCorrectnessImpact::PerformanceOnly,
                "renderer frame transient render targets",
            });
    }
    if (drawPlan.workKind == RendererRhiWorkKind::Compute || drawPlan.usesComputeDispatch
        || drawPlan.dispatchCount > 0) {
        appendUniqueRequirement(requirements,
            requirementCount,
            maxRequirementCount,
            {
                rhi::EngineFeature::ComputeDispatch,
                rhi::FeatureCorrectnessImpact::PreservesCorrectness,
                "renderer frame compute dispatch",
            });
    }
    if (drawPlan.usesStorageTextureBinding) {
        appendUniqueRequirement(requirements,
            requirementCount,
            maxRequirementCount,
            {
                rhi::EngineFeature::StorageTextureBinding,
                rhi::FeatureCorrectnessImpact::PreservesCorrectness,
                "renderer frame storage texture binding",
            });
    }
}

} // namespace

RendererRhiDrawPlanBridge::RendererRhiDrawPlanBridge(const RendererRhiDrawPlanBridgeDesc& desc)
    : desc_(desc)
{}

RendererRhiFramePlanBuildResult RendererRhiDrawPlanBridge::buildFramePlan(
    const RendererRhiDrawPlanDesc& drawPlan) const
{
    return buildRendererRhiFramePlan(desc_, drawPlan);
}

RendererRhiFramePlanBuildResult buildRendererRhiFramePlan(
    const RendererRhiDrawPlanBridgeDesc& bridgeDesc, const RendererRhiDrawPlanDesc& drawPlan)
{
    RendererRhiFramePlanBuildResult result;
    result.backendSpecificBranchingRequired = false;
    result.nativeHandleExposedToUpperLayer = false;
    result.shaderPackages = nullptr;

    std::pmr::string diagnostics;
    if (drawPlan.name.empty()) {
        arp::core::appendDiagnostic(diagnostics, "renderer RHI draw plan requires a pass name");
    }
    validateDrawPlanInputs(drawPlan, diagnostics);

    render_graph::RhiFramePlanBuilder builder({
        bridgeDesc.logicalFrameSlots,
        bridgeDesc.maxFeatureRequirements,
        bridgeDesc.maxBarriers,
        bridgeDesc.maxPasses,
    });
    builder.setFrameSlot(drawPlan.frameSlot);

    if (drawPlan.pushConstantSizeBytes > 0) {
        addRequirement(
            builder, rhi::EngineFeature::PushConstants, "renderer draw plan push constants");
    }
    // Optional soft-secondary permission is intentionally not a frame-plan
    // requirement until the renderer can consume an executable secondary or
    // render-bundle lowering. Primary command recording remains the path used
    // by this bridge.
    if (drawPlan.descriptorTableCount > 0) {
        addRequirement(builder,
            rhi::EngineFeature::DescriptorTable,
            "renderer draw plan descriptor table",
            rhi::FeatureCorrectnessImpact::PerformanceOnly);
    }
    if (drawPlan.usesTransientTargets) {
        addRequirement(builder,
            rhi::EngineFeature::TransientAliasing,
            "renderer draw plan transient render targets",
            rhi::FeatureCorrectnessImpact::PerformanceOnly);
    }
    if (drawPlan.workKind == RendererRhiWorkKind::Compute || drawPlan.usesComputeDispatch
        || drawPlan.dispatchCount > 0) {
        addRequirement(
            builder, rhi::EngineFeature::ComputeDispatch, "renderer draw plan compute dispatch");
    }
    if (drawPlan.usesStorageTextureBinding) {
        addRequirement(builder,
            rhi::EngineFeature::StorageTextureBinding,
            "renderer draw plan storage texture binding");
    }

    builder.addPass(makePassRecord(drawPlan));
    const auto framePlanResult = builder.build();
    arp::core::appendDiagnostic(diagnostics, framePlanResult.diagnostics);

    result.success = diagnostics.empty() && framePlanResult.success;
    result.diagnostics = diagnostics;
    result.plan = framePlanResult.plan;
    return result;
}

RendererRhiFramePlanBridge::RendererRhiFramePlanBridge(const RendererRhiDrawPlanBridgeDesc& desc)
    : desc_(desc)
{}

RendererRhiFramePlanBuildResult RendererRhiFramePlanBridge::buildFramePlan(
    const RendererRhiFramePlanDesc& framePlan) const
{
    return buildRendererRhiFramePlan(desc_, framePlan);
}

RendererRhiFramePlanBuildResult buildRendererRhiFramePlan(
    const RendererRhiDrawPlanBridgeDesc& bridgeDesc, const RendererRhiFramePlanDesc& framePlan)
{
    RendererRhiFramePlanBuildResult result;
    result.backendSpecificBranchingRequired = false;
    result.nativeHandleExposedToUpperLayer = false;
    result.shaderPackages = framePlan.shaderPackages;

    std::pmr::string diagnostics;
    if (framePlan.drawPlans.empty()) {
        arp::core::appendDiagnostic(diagnostics, "renderer RHI frame plan is empty");
    }

    render_graph::RhiFramePlanBuilder builder({
        bridgeDesc.logicalFrameSlots,
        bridgeDesc.maxFeatureRequirements,
        bridgeDesc.maxBarriers,
        bridgeDesc.maxPasses,
    });
    builder.setFrameSlot(framePlan.frameSlot);

    RendererFeatureRequirement requirements[6]{};
    std::uint32_t requirementCount = 0;
    for (const auto& drawPlan : framePlan.drawPlans) {
        if (drawPlan.name.empty()) {
            arp::core::appendDiagnostic(diagnostics, "renderer RHI draw plan requires a pass name");
        }
        validateDrawPlanInputs(drawPlan, diagnostics);
        if (drawPlan.frameSlot != framePlan.frameSlot) {
            arp::core::appendDiagnostic(
                diagnostics, "renderer RHI draw plan frame slot does not match frame");
        }

        collectRequirements(drawPlan,
            requirements,
            requirementCount,
            static_cast<std::uint32_t>(sizeof(requirements) / sizeof(requirements[0])));
        builder.addPass(makePassRecord(drawPlan));
    }

    for (std::uint32_t index = 0; index < requirementCount; ++index) {
        addRequirement(builder,
            requirements[index].feature,
            requirements[index].reason,
            requirements[index].impact);
    }

    const auto framePlanResult = builder.build();
    arp::core::appendDiagnostic(diagnostics, framePlanResult.diagnostics);

    result.success = diagnostics.empty() && framePlanResult.success;
    result.diagnostics = diagnostics;
    result.plan = framePlanResult.plan;
    return result;
}

RendererRhiFramePlanBuilder::RendererRhiFramePlanBuilder(
    const RendererRhiFramePlanBuilderDesc& desc)
    : desc_(desc)
{
    framePlan_.drawPlans.reserve(desc_.maxDrawPlans);
    framePlan_.shaderPackages = desc_.shaderPackages;
    if (desc_.logicalFrameSlots != 2) {
        appendFailure("renderer RHI frame plan builder requires two logical frame slots");
    }
}

RendererRhiFramePlanBuilder& RendererRhiFramePlanBuilder::setFrameSlot(std::uint32_t frameSlot)
{
    framePlan_.frameSlot = frameSlot;
    if (frameSlot >= desc_.logicalFrameSlots) {
        appendFailure("renderer RHI frame plan frame slot is outside the double-buffer range");
    }
    return *this;
}

RendererRhiFramePlanBuilder& RendererRhiFramePlanBuilder::addDrawPlan(
    const RendererRhiDrawPlanDesc& drawPlan)
{
    if (framePlan_.drawPlans.size() >= desc_.maxDrawPlans) {
        appendFailure("renderer RHI frame plan draw plan capacity exhausted");
        return *this;
    }

    framePlan_.drawPlans.push_back(drawPlan);
    return *this;
}

RendererRhiFramePlanBuildResult RendererRhiFramePlanBuilder::build() const
{
    const RendererRhiDrawPlanBridgeDesc bridgeDesc{
        desc_.logicalFrameSlots,
        desc_.maxFeatureRequirements,
        desc_.maxBarriers,
        desc_.maxPasses,
    };
    auto result = buildRendererRhiFramePlan(bridgeDesc, framePlan_);
    arp::core::appendDiagnostic(result.diagnostics, diagnostics_);
    result.success = result.success && !failed_;
    result.backendSpecificBranchingRequired = false;
    result.nativeHandleExposedToUpperLayer = false;
    return result;
}

void RendererRhiFramePlanBuilder::appendFailure(const std::pmr::string& diagnostics)
{
    failed_ = true;
    arp::core::appendDiagnostic(diagnostics_, diagnostics);
}

} // namespace arp::renderer

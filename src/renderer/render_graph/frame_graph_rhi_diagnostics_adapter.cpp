#include "core/diagnostics/diagnostic_builder.h"
#include "core/memory/cpu_allocator.h"
#include "renderer/render_graph/compiled_frame_graph.h"
#include "renderer/render_graph/rhi_frame_plan.h"

namespace arp::render_graph {

namespace {

const char* resourceStateName(rhi::ResourceState state)
{
    switch (state) {
    case rhi::ResourceState::Undefined:
        return "Undefined";
    case rhi::ResourceState::CopySrc:
        return "CopySrc";
    case rhi::ResourceState::CopyDst:
        return "CopyDst";
    case rhi::ResourceState::ColorAttachment:
        return "ColorAttachment";
    case rhi::ResourceState::DepthAttachment:
        return "DepthAttachment";
    case rhi::ResourceState::ShaderRead:
        return "ShaderRead";
    case rhi::ResourceState::ShaderWrite:
        return "ShaderWrite";
    case rhi::ResourceState::Present:
        return "Present";
    }

    return "Unknown";
}

bool passNeedsComputeFeature(const QueuePlanRecord& pass)
{
    return pass.kind == PassKind::Compute || pass.queue == QueueRole::Compute
        || pass.asyncComputeEligible;
}

struct FrameGraphRhiFeatureRequirement {
    rhi::EngineFeature feature = rhi::EngineFeature::PushConstants;
    rhi::FeatureCorrectnessImpact impact = rhi::FeatureCorrectnessImpact::PreservesCorrectness;
    const char* reason = "";
};

void addUniqueRequirement(std::pmr::vector<FrameGraphRhiFeatureRequirement>& requirements,
    const FrameGraphRhiFeatureRequirement& requirement)
{
    for (const auto& entry : requirements) {
        if (entry.feature == requirement.feature) {
            return;
        }
    }
    requirements.push_back(requirement);
}

RhiPassRecord makePassRecord(const CompiledPass& pass)
{
    RhiPassRecord record;
    record.name = pass.name;
    record.kind = pass.kind;
    record.queue = pass.queue;
    record.drawCommands = pass.rhiDrawCommands;
    record.dispatchCommands = pass.rhiDispatchCommands;
    record.transferCommands = pass.rhiTransferCommands;
    record.drawCommandCount = static_cast<std::uint32_t>(record.drawCommands.size());
    record.dispatchCount = static_cast<std::uint32_t>(record.dispatchCommands.size());
    record.transferCommandCount = static_cast<std::uint32_t>(record.transferCommands.size());
    return record;
}

void collectPassRequirements(
    const RhiPassRecord& pass, std::pmr::vector<FrameGraphRhiFeatureRequirement>& requirements)
{
    if (pass.pushConstantSizeBytes > 0) {
        addUniqueRequirement(requirements,
            {
                rhi::EngineFeature::PushConstants,
                rhi::FeatureCorrectnessImpact::PreservesCorrectness,
                "Custom graph pass push constants",
            });
    }
    if (pass.descriptorSetCount > 0 || pass.descriptorTableCount > 0) {
        addUniqueRequirement(requirements,
            {
                rhi::EngineFeature::DescriptorTable,
                rhi::FeatureCorrectnessImpact::PreservesCorrectness,
                "Custom graph descriptor layout bindings",
            });
    }
    if (pass.kind == PassKind::Compute || pass.queue == QueueRole::Compute
        || pass.dispatchCount > 0) {
        addUniqueRequirement(requirements,
            {
                rhi::EngineFeature::ComputeDispatch,
                rhi::FeatureCorrectnessImpact::PreservesCorrectness,
                "Custom graph compute dispatch",
            });
    }
    if (pass.usesStorageTextureBinding) {
        addUniqueRequirement(requirements,
            {
                rhi::EngineFeature::StorageTextureBinding,
                rhi::FeatureCorrectnessImpact::PreservesCorrectness,
                "Custom graph storage texture binding",
            });
    }
}

} // namespace

FrameGraphRhiDiagnosticsAdapter::FrameGraphRhiDiagnosticsAdapter(
    const FrameGraphRhiDiagnosticsAdapterDesc& desc)
    : desc_(desc)
{}

FrameGraphRhiDiagnosticsReport FrameGraphRhiDiagnosticsAdapter::build(
    const CompiledFrameGraph& compiled, std::uint32_t frameSlot) const
{
    FrameGraphRhiDiagnosticsReport report;
    report.compiledPassCount = static_cast<std::uint32_t>(compiled.passCount());
    report.executablePassCount = static_cast<std::uint32_t>(compiled.executablePassCount());
    report.queuePlanCount = static_cast<std::uint32_t>(compiled.queuePlan().size());
    report.aliasCount = static_cast<std::uint32_t>(compiled.aliasPlan().size());
    report.loadStoreCount = static_cast<std::uint32_t>(compiled.loadStorePlan().size());
    report.externalResourceCount = static_cast<std::uint32_t>(compiled.externalResources().size());

    RhiFramePlanBuilder builder({
        desc_.logicalFrameSlots,
        desc_.maxFeatureRequirements,
        desc_.maxBarriers,
        desc_.maxPasses,
        desc_.maxTextureUploads,
        desc_.maxAliases,
        desc_.maxAttachments,
        desc_.maxQueueRecords,
        desc_.maxExternalResources,
    });
    builder.setFrameSlot(frameSlot);

    std::pmr::vector<FrameGraphRhiFeatureRequirement> featureRequirements;
    featureRequirements.reserve(desc_.maxFeatureRequirements);

    bool requiresComputeDispatch = false;
    for (const auto& pass : compiled.queuePlan()) {
        requiresComputeDispatch = requiresComputeDispatch || passNeedsComputeFeature(pass);
    }
    if (requiresComputeDispatch) {
        addUniqueRequirement(featureRequirements,
            {
                rhi::EngineFeature::ComputeDispatch,
                rhi::FeatureCorrectnessImpact::PreservesCorrectness,
                "FrameGraph diagnostics detected compute queue work",
            });
    }
    if (!compiled.aliasPlan().empty()) {
        addUniqueRequirement(featureRequirements,
            {
                rhi::EngineFeature::TransientAliasing,
                rhi::FeatureCorrectnessImpact::PreservesCorrectness,
                "FrameGraph diagnostics detected transient alias groups",
            });
    }

    for (const auto compiledPassIndex : compiled.executionOrder()) {
        if (compiledPassIndex >= compiled.passes().size()) {
            core::appendDiagnostic(report.diagnostics,
                "FrameGraph diagnostics execution order references an invalid pass");
            continue;
        }

        const auto& pass = compiled.passes()[compiledPassIndex];
        if (!pass.culled) {
            auto rhiPass = makePassRecord(pass);
            collectPassRequirements(rhiPass, featureRequirements);
            builder.addPass(rhiPass);
        }
    }

    for (const auto& requirement : featureRequirements) {
        builder.requireFeature(requirement.feature, requirement.impact, requirement.reason);
    }

    for (const auto& barrier : compiled.barrierPlan()) {
        builder.addBarrier({
            barrier.resourceName,
            barrier.passName,
            barrier.kind,
            barrier.before,
            barrier.after,
            barrier.srcStage,
            barrier.dstStage,
            barrier.srcAccess,
            barrier.dstAccess,
            barrier.aspect,
            barrier.handleId,
            true,
            barrier.after == rhi::ResourceState::Present,
            barrier.memoryOnly,
        });
    }

    for (const auto& alias : compiled.aliasPlan()) {
        builder.addAlias({
            alias.resourceName,
            ResourceKind::Texture,
            alias.handle.id,
            alias.aliasGroup,
            true,
        });
    }

    for (const auto& attachment : compiled.loadStorePlan()) {
        builder.addAttachment({
            attachment.passName,
            attachment.resourceName,
            attachment.state,
            attachment.loadOp,
            attachment.storeOp,
            attachment.tileHints,
            attachment.transient,
            attachment.imported,
            attachment.externalProtected,
            attachment.state == rhi::ResourceState::Present,
        });
    }

    for (const auto& queueRecord : compiled.queuePlan()) {
        builder.addQueueRecord({
            queueRecord.passName,
            queueRecord.kind,
            queueRecord.queue,
            queueRecord.asyncComputeEligible,
        });
    }

    for (const auto& resource : compiled.externalResources()) {
        builder.addExternalResource({
            resource.resourceName,
            resource.kind,
            resource.handleId,
            resource.initialState,
            resource.externalProtected,
            resource.kind == ResourceKind::Texture,
            resource.initialState == rhi::ResourceState::Present,
        });
    }

    const auto built = builder.build();
    report.plan = built.plan;
    core::appendDiagnostic(report.diagnostics, built.diagnostics);
    report.success = built.success && report.diagnostics.empty();
    return report;
}

} // namespace arp::render_graph

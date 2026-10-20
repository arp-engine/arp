#include "renderer/render_graph/rhi_frame_plan.h"

#include "core/cpu_allocator.h"
#include "core/diagnostic_builder.h"
#include "rhi/gpu_common.h"

namespace arp::render_graph {

namespace {

void validateDescriptorLayouts(const RhiPassRecord& pass, std::pmr::string& diagnostics)
{
    if (pass.descriptorLayoutNameCount > kMaxRhiPassDescriptorLayoutNames) {
        arp::core::appendDiagnostic(
            diagnostics, "RHI frame plan descriptor layout name capacity exhausted");
        return;
    }

    const auto requiredLayoutCount = pass.descriptorSetCount > pass.descriptorTableCount
        ? pass.descriptorSetCount
        : pass.descriptorTableCount;
    if (requiredLayoutCount > 0 && pass.descriptorLayoutNameCount < requiredLayoutCount) {
        arp::core::appendDiagnostic(
            diagnostics, "RHI frame plan descriptor layout names are incomplete");
        return;
    }

    for (std::uint32_t index = 0; index < pass.descriptorLayoutNameCount; ++index) {
        if (pass.descriptorLayoutNames[index].empty()) {
            arp::core::appendDiagnostic(
                diagnostics, "RHI frame plan descriptor layout name is empty");
            return;
        }
    }
}

void validatePushConstants(const RhiPassRecord& pass, std::pmr::string& diagnostics)
{
    if (pass.pushConstantSizeBytes == 0) {
        return;
    }

    if (pass.pushConstantSizeBytes > rhi::kL2AtomicPushConstantLimitBytes) {
        arp::core::appendDiagnostic(diagnostics,
            "RHI frame plan push constant bytes exceed the L2 AtomicFullBlockPush "
            "profile ceiling (256 bytes); native/lowering limits may be larger "
            "but must not silently expand the L2 contract");
        return;
    }

    if (pass.pipelinePushConstantCapacityBytes == 0
        || pass.pushConstantSizeBytes > pass.pipelinePushConstantCapacityBytes) {
        arp::core::appendDiagnostic(
            diagnostics, "RHI frame plan push constant bytes exceed pipeline layout capacity");
    }
}

void validatePushConstantsExecutablePayload(
    const RhiPassRecord& pass, const rhi::DeviceCapabilities& caps, std::pmr::string& diagnostics)
{
    if (pass.pushConstantSizeBytes == 0) {
        return;
    }
    if (!caps.pushConstantLoweringHasExecutablePayload && caps.backend != rhi::Backend::Null) {
        arp::core::appendDiagnostic(diagnostics,
            "RHI frame plan push constants require a backend-owned executable "
            "payload but the device lowering plan reports no executable payload "
            "(PrototypeLegacy or DiagnosticFailure outcome); push constants are "
            "disabled by default on this backend; explicit opt-in is required");
    }
}

void validateCommandMetadata(const RhiPassRecord& pass, std::pmr::string& diagnostics)
{
    if (pass.drawCommandCount != pass.drawCommands.size()) {
        arp::core::appendDiagnostic(
            diagnostics, "RHI frame plan draw command count does not match explicit draw metadata");
    }
    if (pass.dispatchCount != pass.dispatchCommands.size()) {
        arp::core::appendDiagnostic(
            diagnostics, "RHI frame plan dispatch count does not match explicit dispatch metadata");
    }
    if (pass.transferCommandCount != pass.transferCommands.size()) {
        arp::core::appendDiagnostic(diagnostics,
            "RHI frame plan transfer command count does not match explicit transfer metadata");
    }

    if (pass.kind == PassKind::Graphics || pass.kind == PassKind::Present) {
        if (!pass.dispatchCommands.empty()) {
            arp::core::appendDiagnostic(diagnostics,
                "RHI frame plan graphics/present pass contains compute command metadata");
        }
    }
    if (pass.kind == PassKind::Compute && !pass.drawCommands.empty()) {
        arp::core::appendDiagnostic(
            diagnostics, "RHI frame plan compute pass contains graphics command metadata");
    }
    if (pass.kind != PassKind::Copy && pass.kind != PassKind::Present
        && !pass.transferCommands.empty()) {
        arp::core::appendDiagnostic(
            diagnostics, "RHI frame plan non-transfer pass contains transfer command metadata");
    }

    for (const auto& command : pass.drawCommands) {
        if (command.indexCount == 0 || command.instanceCount == 0) {
            arp::core::appendDiagnostic(diagnostics,
                "RHI frame plan indexed draw requires non-zero index and instance counts");
            break;
        }
    }
    for (const auto& command : pass.dispatchCommands) {
        if (command.groupCountX == 0 || command.groupCountY == 0 || command.groupCountZ == 0) {
            arp::core::appendDiagnostic(
                diagnostics, "RHI frame plan dispatch requires non-zero group counts");
            break;
        }
    }
    for (const auto& command : pass.transferCommands) {
        if (command.sizeBytes == 0) {
            arp::core::appendDiagnostic(
                diagnostics, "RHI frame plan transfer requires a non-zero size");
            break;
        }
        if (command.stagingIntent != rhi::StagingIntent::Upload || !command.backendOwnedStaging) {
            arp::core::appendDiagnostic(diagnostics,
                "RHI frame plan transfer requires backend-owned upload staging intent");
            break;
        }
    }
}

std::pmr::string validatePassRecord(const RhiPassRecord& pass)
{
    std::pmr::string diagnostics;
    if (pass.name.empty()) {
        arp::core::appendDiagnostic(diagnostics, "RHI frame plan pass requires a name");
    }
    validateDescriptorLayouts(pass, diagnostics);
    validatePushConstants(pass, diagnostics);
    validateCommandMetadata(pass, diagnostics);
    if (pass.materialResourceCount < pass.requiredMaterialResourceCount) {
        arp::core::appendDiagnostic(
            diagnostics, "RHI frame plan material resource bindings are incomplete");
    }
    if (pass.requiresBackendOwnedVertexUpload && pass.maxUploadBytes == 0) {
        arp::core::appendDiagnostic(
            diagnostics, "RHI frame plan vertex upload requires an explicit bounded size");
    }
    if (pass.hasDepthTarget && pass.kind != PassKind::Graphics) {
        arp::core::appendDiagnostic(
            diagnostics, "RHI frame plan depth target requires a graphics pass");
    }
    return diagnostics;
}

} // namespace

RhiFramePlanBuilder::RhiFramePlanBuilder(const RhiFramePlanBuilderDesc& desc) : desc_(desc)
{
    plan_.featureRequirements.logicalFrameSlots = desc_.logicalFrameSlots;
    plan_.featureRequirements.maxFeatureDecisions = desc_.maxFeatureRequirements;
    plan_.featureRequirements.features.reserve(desc_.maxFeatureRequirements);
    plan_.barriers.reserve(desc_.maxBarriers);
    plan_.aliases.reserve(desc_.maxAliases);
    plan_.attachments.reserve(desc_.maxAttachments);
    plan_.queueRecords.reserve(desc_.maxQueueRecords);
    plan_.externalResources.reserve(desc_.maxExternalResources);
    plan_.passes.reserve(desc_.maxPasses);
    plan_.textureUploads.reserve(desc_.maxTextureUploads);

    if (desc_.logicalFrameSlots < 2) {
        appendFailure("RHI frame plan requires at least two logical frame slots");
    }
}

RhiFramePlanBuilder& RhiFramePlanBuilder::setFrameSlot(std::uint32_t frameSlot)
{
    plan_.frameSlot = frameSlot;
    if (frameSlot >= desc_.logicalFrameSlots) {
        appendFailure("RHI frame plan frame slot is outside the frame slot range");
    }
    return *this;
}

RhiFramePlanBuilder& RhiFramePlanBuilder::requireFeature(rhi::EngineFeature feature,
    rhi::FeatureCorrectnessImpact correctnessImpact,
    const std::pmr::string& reason)
{
    if (plan_.featureRequirements.features.size() >= desc_.maxFeatureRequirements) {
        appendFailure("RHI frame plan feature requirement capacity exhausted");
        return *this;
    }

    plan_.featureRequirements.features.push_back({feature, correctnessImpact, reason});
    return *this;
}

RhiFramePlanBuilder& RhiFramePlanBuilder::addBarrier(const RhiBarrierRecord& barrier)
{
    if (plan_.barriers.size() >= desc_.maxBarriers) {
        appendFailure("RHI frame plan barrier capacity exhausted");
        return *this;
    }
    if (barrier.resourceName.empty()) {
        appendFailure("RHI frame plan barrier requires a resource name");
        return *this;
    }
    if ((barrier.beforeState == barrier.afterState) != barrier.memoryOnly) {
        appendFailure("RHI frame plan barrier requires a state transition");
        return *this;
    }
    if (barrier.memoryOnly
        && (barrier.sourceAccess == rhi::ResourceAccess::None
            || barrier.destinationAccess == rhi::ResourceAccess::None)) {
        appendFailure("RHI-RG-MEMORY-BARRIER-ACCESS-MISSING");
        return *this;
    }
    if (barrier.sourceStages == rhi::PipelineStage::None
        || barrier.destinationStages == rhi::PipelineStage::None) {
        appendFailure("RHI frame plan barrier requires explicit pipeline stages");
        return *this;
    }
    if (!barrier.backendConsumable) {
        appendFailure("RHI frame plan barrier must be backend-consumable");
        return *this;
    }
    if (barrier.aspect == rhi::TextureAspect::Stencil) {
        appendFailure("RHI frame plan barrier does not support Stencil aspect");
        return *this;
    }

    plan_.barriers.push_back(barrier);
    return *this;
}

RhiFramePlanBuilder& RhiFramePlanBuilder::addAlias(const RhiAliasRecord& alias)
{
    if (plan_.aliases.size() >= desc_.maxAliases) {
        appendFailure("RHI frame plan alias capacity exhausted");
        return *this;
    }
    if (alias.resourceName.empty() || alias.handleId == 0 || alias.aliasGroup == 0) {
        appendFailure("RHI frame plan alias requires explicit resource identity");
        return *this;
    }
    if (!alias.backendOwnedTransient) {
        appendFailure("RHI frame plan aliasing must stay backend-owned");
        return *this;
    }
    plan_.aliases.push_back(alias);
    return *this;
}

RhiFramePlanBuilder& RhiFramePlanBuilder::addAttachment(const RhiAttachmentRecord& attachment)
{
    if (plan_.attachments.size() >= desc_.maxAttachments) {
        appendFailure("RHI frame plan attachment capacity exhausted");
        return *this;
    }
    if (attachment.passName.empty() || attachment.resourceName.empty()) {
        appendFailure("RHI frame plan attachment requires pass and resource names");
        return *this;
    }
    if (attachment.state == rhi::ResourceState::Undefined) {
        appendFailure("RHI frame plan attachment requires an explicit state");
        return *this;
    }
    plan_.attachments.push_back(attachment);
    return *this;
}

RhiFramePlanBuilder& RhiFramePlanBuilder::addQueueRecord(const RhiQueueRecord& queueRecord)
{
    if (plan_.queueRecords.size() >= desc_.maxQueueRecords) {
        appendFailure("RHI frame plan queue record capacity exhausted");
        return *this;
    }
    if (queueRecord.passName.empty()) {
        appendFailure("RHI frame plan queue record requires a pass name");
        return *this;
    }
    plan_.queueRecords.push_back(queueRecord);
    return *this;
}

RhiFramePlanBuilder& RhiFramePlanBuilder::addExternalResource(
    const RhiExternalResourceRecord& resource)
{
    if (plan_.externalResources.size() >= desc_.maxExternalResources) {
        appendFailure("RHI frame plan external resource capacity exhausted");
        return *this;
    }
    if (resource.resourceName.empty() || resource.handleId == 0) {
        appendFailure("RHI frame plan external resource requires explicit identity");
        return *this;
    }
    if (!resource.externalProtected) {
        appendFailure("RHI frame plan external resource must be protected");
        return *this;
    }
    plan_.externalResources.push_back(resource);
    return *this;
}

RhiFramePlanBuilder& RhiFramePlanBuilder::addPass(const RhiPassRecord& pass)
{
    if (plan_.passes.size() >= desc_.maxPasses) {
        appendFailure("RHI frame plan pass capacity exhausted");
        return *this;
    }

    const auto passDiagnostics = validatePassRecord(pass);
    if (!passDiagnostics.empty()) {
        appendFailure(passDiagnostics);
        return *this;
    }

    plan_.passes.push_back(pass);
    return *this;
}

RhiFramePlanBuilder& RhiFramePlanBuilder::addTextureUpload(const RhiTextureUploadRecord& upload)
{
    if (plan_.textureUploads.size() >= desc_.maxTextureUploads) {
        appendFailure("RHI frame plan texture upload capacity exhausted");
        return *this;
    }
    if (upload.extent.width == 0 || upload.extent.height == 0 || upload.extent.depth == 0) {
        appendFailure("RHI frame plan texture upload requires non-zero extent");
        return *this;
    }
    if (upload.bytesPerRow == 0 || upload.rowsPerImage == 0) {
        appendFailure("RHI frame plan texture upload requires explicit row and slice pitch");
        return *this;
    }
    if ((upload.bytesPerRow % 256u) != 0u) {
        appendFailure("RHI frame plan texture upload row pitch must be 256-byte aligned");
        return *this;
    }
    if (upload.stagingIntent != rhi::StagingIntent::Upload || !upload.backendOwnedStaging) {
        appendFailure("RHI frame plan texture upload requires backend-owned upload staging intent");
        return *this;
    }

    plan_.textureUploads.push_back(upload);
    return *this;
}

RhiFramePlanBuildResult RhiFramePlanBuilder::build() const
{
    RhiFramePlanBuildResult result;
    result.success = !failed_;
    result.diagnostics = diagnostics_;
    result.plan = plan_;
    return result;
}

void RhiFramePlanBuilder::appendFailure(const std::pmr::string& diagnostics)
{
    failed_ = true;
    arp::core::appendDiagnostic(diagnostics_, diagnostics);
}

} // namespace arp::render_graph

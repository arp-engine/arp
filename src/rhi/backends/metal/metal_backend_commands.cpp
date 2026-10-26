#include "core/memory/cpu_allocator.h"
#include "rhi/backends/metal/metal_backend_objects.h"
#include "rhi/backends/metal/metal_binding_validation.h"
#include "rhi/backends/metal/metal_bindless_validation.h"
#include "rhi/backends/metal/metal_device.h"
#include "rhi/backends/metal/metal_native_runtime.h"
#include "rhi/backends/metal/metal_native_secondary.h"
#include "rhi/backends/metal/metal_original_ordinary_command.h"
#include "rhi/backends/metal/metal_staging_upload_pool.h"
#include "rhi/frontend/bindless_graphics_binding.h"
#include "rhi/frontend/bindless_shader_package.h"
#include "rhi/frontend/bindless_snapshot.h"
#include "rhi/frontend/command_attachment_validation.h"
#include "rhi/frontend/command_encoder_base.h"
#include "rhi/frontend/command_indirect_commands.h"
#include "rhi/frontend/command_retention_validation.h"
#include "rhi/frontend/descriptor_bound_descriptor_slots.h"
#include "rhi/frontend/descriptor_resource_validation.h"
#include "rhi/frontend/descriptor_set_validation.h"
#include "rhi/frontend/memory_upload_staging_readiness.h"
#include "rhi/frontend/resource_memory_barrier.h"
#include "rhi/gpu_bindless.h"
#include "rhi/gpu_extensions.h"
#include "rhi/semantics/bindless_original_preparation_lowering.h"
#include "rhi/semantics/bindless_pipeline_interface.h"
#include "rhi/semantics/command_state_lowering.h"
#include "rhi/semantics/native_command_lowering.h"
#include "rhi/semantics/push_constant_validation.h"
#include "rhi/semantics/viewport_scissor_validation.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>
#include <utility>

namespace arp::rhi_metal {

namespace {

class MetalRhiPreparedComputeBinding final : public rhi::BindlessPreparedComputeBinding {
public:
    MetalRhiPreparedComputeBinding(std::shared_ptr<const rhi::BindlessComputeBindingOwners> owners,
        std::shared_ptr<const MetalRhiNativePreparedComputeBindings> payload)
        : BindlessPreparedComputeBinding(std::move(owners)), payload_(std::move(payload))
    {}

    rhi::ValidationResult validatePayload() const override
    {
        const auto live = owners()->validate();
        if (!live.success)
            return live;
        return payload_ && payload_->pipeline && payload_->descriptors
                && payload_->stage == rhi::ShaderStage::Compute
                && payload_->function == payload_->pipeline->computeFunction
                && payload_->pipeline == metalRhiNativePayloadFor(*owners()->desc().pipeline)
                && payload_->set == owners()->desc().snapshot->setIndex()
                && owners()->desc().plan.execution == rhi::BindlessExecution::Native
                && (owners()->desc().plan.executable
                    || rhi::semantics::OriginalBindlessPreparationLowering::hasPlan(
                        owners()->desc().plan, 6u)
                    || rhi::semantics::OriginalBindlessPreparationLowering::hasImageCellBindingPlan(
                        owners()->desc().plan))
            ? rhi::ValidationResult{true, {}}
            : rhi::ValidationResult{false, "RHI-METAL-BINDLESS-PREPARED-PAYLOAD-MISMATCH"};
    }

    std::uint32_t argumentBufferCount() const noexcept override
    {
        return payload_ ? static_cast<std::uint32_t>(payload_->arrays.size()) : 0u;
    }

    std::uint64_t argumentBufferBytes() const noexcept override
    {
        return payload_ ? payload_->argumentBytes : 0u;
    }

    const std::shared_ptr<const MetalRhiNativePreparedComputeBindings>& payload() const noexcept
    {
        return payload_;
    }

private:
    std::shared_ptr<const MetalRhiNativePreparedComputeBindings> payload_;
};

class MetalRhiPreparedGraphicsBinding final : public rhi::BindlessPreparedGraphicsBinding {
public:
    MetalRhiPreparedGraphicsBinding(
        std::shared_ptr<const rhi::BindlessGraphicsBindingOwners> owners,
        std::shared_ptr<const MetalRhiNativePreparedGraphicsBindings> payload)
        : BindlessPreparedGraphicsBinding(std::move(owners)), payload_(std::move(payload))
    {}

    rhi::ValidationResult validatePayload() const override
    {
        const auto live = validateOwners();
        if (!live.success)
            return live;
        const auto& desc = owners()->desc();
        const auto entries = metalRhiSelectedGraphicsEntriesFor(*desc.pipeline);
        const auto native = metalRhiNativePayloadFor(*desc.pipeline);
        const auto fail = [] {
            return rhi::ValidationResult{
                false, "RHI-METAL-BINDLESS-PREPARED-GRAPHICS-PAYLOAD-MISMATCH"};
        };
        if (!payload_ || !native || payload_->pipeline != native || !payload_->descriptors
            || native->preparedGraphicsStages != owners()->stages()
            || payload_->set != desc.snapshot->setIndex()
            || desc.plan.execution != rhi::BindlessExecution::Native || !desc.plan.executable)
            return fail();
        const bool fragment =
            owners()->stages() == (rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment);
        const std::array functions{native->vertexFunction, native->fragmentFunction};
        const std::array names{entries.vertex, entries.fragment};
        const std::array stages{rhi::ShaderStage::Vertex, rhi::ShaderStage::Fragment};
        for (unsigned i = 0u; i < 2u; ++i) {
            const auto& stage = payload_->stages[i];
            const auto& layout = payload_->layouts[i];
            if (i == 1u && !fragment) {
                if (stage || layout || functions[i] || !names[i].empty())
                    return fail();
                continue;
            }
            if (!stage || !layout || names[i].empty() || !functions[i] || stage->pipeline != native
                || stage->function != functions[i] || stage->stage != stages[i]
                || stage->descriptors != payload_->descriptors || stage->set != payload_->set
                || layout->stage != stages[i] || layout->entry != names[i])
                return fail();
        }
        return {true, {}};
    }

    std::uint32_t argumentBufferCount() const noexcept override
    {
        return payload_ ? payload_->argumentCount : 0u;
    }

    std::uint64_t argumentBufferBytes() const noexcept override
    {
        return payload_ ? payload_->argumentBytes : 0u;
    }

    const std::shared_ptr<const MetalRhiNativePreparedGraphicsBindings>& payload() const noexcept
    {
        return payload_;
    }

private:
    std::shared_ptr<const MetalRhiNativePreparedGraphicsBindings> payload_;
};

rhi::CommandValidationResult commandOk(std::uint32_t count)
{
    return {true, "", count, rhi::CommandValidationStatus::Success, ""};
}

rhi::CommandValidationResult commandFail(const std::pmr::string& diagnostics, std::uint32_t count)
{
    return {false, diagnostics, count};
}

rhi::CommandValidationResult commandStagingFail(
    const std::pmr::string& diagnostics, std::uint32_t count, std::string_view diagnosticId)
{
    const auto decision = rhi::semantics::classifyUploadStagingFailure(diagnosticId);
    return {
        false,
        diagnostics,
        count,
        decision.retryAfterCompletion() ? rhi::CommandValidationStatus::Deferred
                                        : rhi::CommandValidationStatus::DiagnosticFailure,
        std::pmr::string(diagnosticId.data(), diagnosticId.size()),
    };
}

bool metalCommandLoweringAllows(
    const rhi::semantics::NativeBackendCapabilityContract& capabilityContract,
    const rhi::semantics::CommandWorkloadDesc& workload,
    rhi::semantics::CommandRecordingIntent intent,
    std::pmr::string& diagnostics,
    rhi::semantics::NativeCommandLoweringDecision* acceptedDecision = nullptr)
{
    auto decision = rhi::semantics::lowerCommandWorkload(capabilityContract, workload, intent);
    if (!decision.success) {
        diagnostics = decision.diagnostics;
        return false;
    }
    const auto require = [&](rhi::semantics::NativeCommandSemantic semantic,
                             rhi::FallbackMode fallback,
                             std::string_view operation) {
        const auto result = rhi::semantics::validateNativeCommandLoweringUse(
            decision, semantic, fallback, operation);
        if (!result.success) {
            diagnostics = result.diagnostics;
            return false;
        }
        return true;
    };
    if (!require(rhi::semantics::NativeCommandSemantic::MetalCommandBuffer,
            decision.fallbackMode,
            "Metal command buffer")) {
        return false;
    }
    if (intent == rhi::semantics::CommandRecordingIntent::RenderBundle) {
        const auto semantic = workload.allowCpuReplay
            ? rhi::semantics::NativeCommandSemantic::MetalRenderBundleReplay
            : rhi::semantics::NativeCommandSemantic::MetalRenderBundleIndirectCommandBuffer;
        const auto fallback =
            workload.allowCpuReplay ? rhi::FallbackMode::CpuReplay : rhi::FallbackMode::Native;
        if (!require(semantic, fallback, "Metal render bundle")) {
            return false;
        }
    } else if (intent == rhi::semantics::CommandRecordingIntent::SoftSecondaryCommandBuffer) {
        const auto semantic = workload.allowCpuReplay
            ? rhi::semantics::NativeCommandSemantic::MetalSecondaryCommandReplay
            : rhi::semantics::NativeCommandSemantic::MetalIndirectCommandBuffer;
        const auto fallback =
            workload.allowCpuReplay ? rhi::FallbackMode::CpuReplay : rhi::FallbackMode::Native;
        if (!require(semantic, fallback, "Metal secondary command")) {
            return false;
        }
    }
    if (intent != rhi::semantics::CommandRecordingIntent::RenderBundle && workload.usesRenderPass
        && !require(rhi::semantics::NativeCommandSemantic::MetalRenderCommandEncoder,
            decision.fallbackMode,
            "Metal render pass")) {
        return false;
    }
    if (workload.usesComputeDispatch
        && !require(rhi::semantics::NativeCommandSemantic::MetalComputeCommandEncoder,
            decision.fallbackMode,
            "Metal compute pass")) {
        return false;
    }
    if (workload.usesPushConstants
        && !require(rhi::semantics::NativeCommandSemantic::MetalPushConstantUpload,
            decision.fallbackMode,
            "Metal push constants")) {
        return false;
    }
    if (workload.usesIndirectDraw
        && !require(rhi::semantics::NativeCommandSemantic::MetalIndirectDrawCommand,
            workload.usesGpuCountedIndirectDraw ? rhi::FallbackMode::Emulated
                                                : rhi::FallbackMode::Native,
            workload.usesGpuCountedIndirectDraw ? "Metal GPU-counted indirect draw"
                                                : "Metal indirect draw")) {
        return false;
    }
    if (workload.usesGpuCountedIndirectDraw
        && !require(rhi::semantics::NativeCommandSemantic::MetalGpuCountedIndirectMask,
            rhi::FallbackMode::Emulated,
            "Metal GPU-counted indirect mask")) {
        return false;
    }
    if (workload.usesDynamicViewportScissor) {
        if (!require(rhi::semantics::NativeCommandSemantic::MetalSetViewportCommand,
                decision.fallbackMode,
                "Metal viewport")) {
            return false;
        }
        if (!require(rhi::semantics::NativeCommandSemantic::MetalSetScissorCommand,
                decision.fallbackMode,
                "Metal scissor")) {
            return false;
        }
    }
    if (workload.usesBarriers || workload.usesSubresourceBarriers
        || workload.usesStageAccessBarriers) {
        if (!require(rhi::semantics::NativeCommandSemantic::MetalResourceBarrier,
                decision.fallbackMode,
                "Metal resource barrier")) {
            return false;
        }
    }
    if (workload.usesTransferCopies
        && !require(rhi::semantics::NativeCommandSemantic::MetalBlitCopyCommands,
            decision.fallbackMode,
            "Metal transfer copy")) {
        return false;
    }
    if ((workload.usesUploadStaging || workload.usesReadbackStaging)
        && !require(rhi::semantics::NativeCommandSemantic::MetalBackendOwnedStagingBuffer,
            decision.fallbackMode,
            "Metal backend-owned staging")) {
        return false;
    }
    if (workload.colorAttachmentCount > 1u || workload.usesDepthStencilAttachment) {
        if (!require(rhi::semantics::NativeCommandSemantic::MetalMrtDepthStencilRenderEncoder,
                decision.fallbackMode,
                "Metal MRT/depth-stencil render pass")) {
            return false;
        }
    }
    if (workload.usesResolveTargets
        && !require(rhi::semantics::NativeCommandSemantic::MetalResolveAttachment,
            decision.fallbackMode,
            "Metal resolve attachment")) {
        return false;
    }
    if (workload.usesTimestampQuery
        && !require(rhi::semantics::NativeCommandSemantic::MetalTimestampQuery,
            decision.fallbackMode,
            "Metal timestamp query")) {
        return false;
    }
    if (workload.usesPipelineStatisticsQuery
        && !require(rhi::semantics::NativeCommandSemantic::MetalPipelineStatisticsQuery,
            decision.fallbackMode,
            "Metal pipeline statistics query")) {
        return false;
    }
    if (acceptedDecision)
        *acceptedDecision = std::move(decision);
    return true;
}

bool hasTextureUsage(rhi::TextureUsage usage, rhi::TextureUsage required)
{
    return (usage & required) == required;
}

bool hasResourceUsage(rhi::ResourceUsage usage, rhi::ResourceUsage required)
{
    return (usage & required) == required;
}

bool metalTextureAspectMatchesFormat(rhi::TextureFormat format, rhi::TextureAspect aspect)
{
    if (format == rhi::TextureFormat::Depth24Stencil8) {
        return aspect == rhi::TextureAspect::Depth || aspect == rhi::TextureAspect::Stencil;
    }
    if (rhi::isDepthTextureFormat(format)) {
        return aspect == rhi::TextureAspect::Depth;
    }
    return aspect == rhi::TextureAspect::Color;
}

bool validMetalBarrierDesc(const rhi::GpuResourceId resource,
    const rhi::ResourceBarrierDesc& desc,
    const rhi::TextureDesc* textureDesc,
    std::pmr::string& diagnostics)
{
    if (!resource.isValid() || !desc.resource.isValid() || desc.resource != resource) {
        diagnostics = "RHI-METAL-BARRIER-RESOURCE-MISMATCH";
        return false;
    }
    if (desc.before == desc.after) {
        diagnostics = "RHI-METAL-BARRIER-NO-OP-TRANSITION";
        return false;
    }
    if (desc.destinationStages == rhi::PipelineStage::None) {
        diagnostics = "RHI-METAL-BARRIER-DESTINATION-STAGE-MISSING";
        return false;
    }
    if (desc.after != rhi::ResourceState::Undefined
        && desc.destinationAccess == rhi::ResourceAccess::None) {
        diagnostics = "RHI-METAL-BARRIER-DESTINATION-ACCESS-MISSING";
        return false;
    }
    if (textureDesc != nullptr
        && (desc.textureSubresources.mipLevelCount == 0u
            || desc.textureSubresources.arrayLayerCount == 0u
            || desc.textureSubresources.baseMipLevel >= textureDesc->mipLevelCount
            || desc.textureSubresources.baseArrayLayer >= textureDesc->arrayLayerCount
            || desc.textureSubresources.mipLevelCount
                > textureDesc->mipLevelCount - desc.textureSubresources.baseMipLevel
            || desc.textureSubresources.arrayLayerCount
                > textureDesc->arrayLayerCount - desc.textureSubresources.baseArrayLayer
            || !metalTextureAspectMatchesFormat(
                textureDesc->format, desc.textureSubresources.aspect))) {
        diagnostics = "RHI-METAL-BARRIER-TEXTURE-RANGE-INVALID";
        return false;
    }
    return true;
}

bool checkedMetalMultiply(std::uint64_t lhs, std::uint64_t rhs, std::uint64_t& result)
{
    if (lhs != 0u && rhs > std::numeric_limits<std::uint64_t>::max() / lhs) {
        return false;
    }
    result = lhs * rhs;
    return true;
}

bool checkedMetalAdd(std::uint64_t lhs, std::uint64_t rhs, std::uint64_t& result)
{
    if (rhs > std::numeric_limits<std::uint64_t>::max() - lhs) {
        return false;
    }
    result = lhs + rhs;
    return true;
}

bool descriptorSetMatchesPipeline(const rhi::PipelineLayout* pipelineLayout,
    std::uint32_t set,
    const rhi::DescriptorSet& descriptorSet)
{
    if (!pipelineLayout) {
        return false;
    }
    for (const auto& slot : pipelineLayout->desc().descriptorSets) {
        if (slot.set == set) {
            return slot.layoutName == descriptorSet.layout().name;
        }
    }
    return false;
}

bool declaresDescriptorTableSlot(const rhi::PipelineLayout* pipelineLayout,
    std::uint32_t set,
    const rhi::DescriptorTable& descriptorTable)
{
    if (!pipelineLayout || !descriptorTable.isCurrentAllocation()) {
        return false;
    }
    return rhi::validateDescriptorTableSlot(pipelineLayout->desc(), set, descriptorTable.desc())
        .success;
}

bool descriptorTableIsComplete(
    const rhi::DescriptorTable& descriptorTable, std::pmr::string& diagnostics)
{
    if (!descriptorTable.isCurrentAllocation()) {
        diagnostics = "RHI-METAL-DESCRIPTOR-TABLE-STALE-ALLOCATION";
        return false;
    }
    const auto validation = rhi::validateDescriptorResources(
        descriptorTable, rhi::DescriptorContentRequirement::CompleteLayout);
    if (!validation.success) {
        diagnostics = "RHI-METAL-DESCRIPTOR-TABLE-INCOMPLETE: ";
        diagnostics += validation.diagnostics;
        return false;
    }
    return true;
}

bool descriptorSetIsComplete(const rhi::DescriptorSet& descriptorSet, std::pmr::string& diagnostics)
{
    if (!descriptorSet.isCurrentAllocation()) {
        diagnostics = "RHI-METAL-DESCRIPTOR-SET-STALE-ALLOCATION";
        return false;
    }
    const auto validation = rhi::validateDescriptorResources(
        descriptorSet, rhi::DescriptorContentRequirement::CompleteLayout);
    if (!validation.success) {
        diagnostics = "RHI-METAL-DESCRIPTOR-SET-INCOMPLETE: ";
        diagnostics += validation.diagnostics;
        return false;
    }
    return true;
}

bool descriptorSetNeedsArgumentBuffers(const rhi::DescriptorSet& descriptorSet)
{
    for (const auto& binding : descriptorSet.layout().bindings) {
        if (binding.arrayCount > 1u)
            return true;
    }
    return false;
}

class MetalRhiCommandBuffer final : public rhi::RecordedStateCommandBuffer,
                                    public rhi::BindlessCommandBufferExtension {
public:
    struct ColdStorage {};

    explicit MetalRhiCommandBuffer(ColdStorage) noexcept
        : RecordedStateCommandBuffer(core::UniquePtr<core::MutationJournal>{})
    {}

    MetalRhiCommandBuffer(MetalRhiCommandBuffer&&) noexcept = default;

    MetalRhiCommandBuffer(std::uint32_t commandCount,
        std::shared_ptr<MetalRhiNativeCommandPayload> nativePayload,
        core::MutationJournal& journal) noexcept
        : RecordedStateCommandBuffer(
              std::shared_ptr<core::MutationJournal>(nativePayload, &journal)),
          commandCount_(commandCount), nativePayload_(std::move(nativePayload))
    {}

    MetalRhiCommandBuffer(std::uint32_t commandCount,
        std::shared_ptr<MetalRhiNativeCommandPayload> nativePayload,
        core::UniquePtr<core::MutationJournal> states,
        core::UniquePtr<rhi::RecordedStagingUploadJournal> stagingUploads)
        : RecordedStateCommandBuffer(std::move(states)), commandCount_(commandCount),
          nativePayload_(std::move(nativePayload))
    {
        if (nativePayload_)
            nativePayload_->adoptStagingUploads(std::move(stagingUploads));
    }

    ~MetalRhiCommandBuffer() override
    {
        if (nativePayload_)
            nativePayload_->abandonUnsubmitted();
    }

    MetalRhiCommandBuffer(const MetalRhiCommandBuffer&) = delete;
    MetalRhiCommandBuffer& operator=(const MetalRhiCommandBuffer&) = delete;

    std::uint32_t commandCount() const override { return commandCount_; }

    void* queryInterface(std::string_view extension) noexcept override
    {
        return extension == rhi::kExtensionBindlessCross
            ? static_cast<rhi::BindlessCommandBufferExtension*>(this)
            : nullptr;
    }

    rhi::BindlessFaultReport faultReport(std::uint64_t generation) const noexcept override
    {
        return nativePayload_
            ? nativePayload_->bindlessFaultReport(generation)
            : rhi::BindlessFaultReport{
                  generation, rhi::BindlessFaultValidationStatus::Unavailable, 0u};
    }

    std::shared_ptr<MetalRhiNativeCommandPayload> nativePayload() const { return nativePayload_; }

private:
    bool acceptBackendOwnedRecordedOwnership() const noexcept override
    {
        return nativePayload_ && nativePayload_->acceptOriginalOrdinaryOwnership();
    }

    std::uint32_t commandCount_ = 0;
    std::shared_ptr<MetalRhiNativeCommandPayload> nativePayload_;
};

class MetalRhiRenderPassEncoder final : public rhi::RenderPassEncoder,
                                        public rhi::BindlessPassExtension {
public:
    std::uint64_t recordingIdentity() const noexcept override { return recordingIdentity_; }

    void* queryInterface(std::string_view extension) noexcept override
    {
        return graphicsExecutable_ && extension == rhi::kExtensionBindlessCross
            ? static_cast<rhi::BindlessPassExtension*>(this)
            : nullptr;
    }

    rhi::CommandValidationResult bindSnapshot(std::shared_ptr<const rhi::BindlessSnapshot>,
        const rhi::BindlessLoweringPlan&,
        std::uint64_t,
        const rhi::BindlessAccessDeclarations&) override
    {
        if (nativePayload_ && nativePayload_->hasOriginalOrdinary())
            return failCommand("RHI-METAL-ORDINARY-COMMAND-PROFILE", commandCount_);

        return failCommand("RHI-METAL-BINDLESS-PREPARED-GRAPHICS-REQUIRED", commandCount_);
    }

    rhi::CommandValidationResult bindPreparedGraphicsBinding(
        std::shared_ptr<const rhi::BindlessPreparedGraphicsBinding> binding,
        const rhi::BindlessAccessDeclarations& accesses) override
    {
        if (nativePayload_ && nativePayload_->hasOriginalOrdinary())
            return failCommand("RHI-METAL-ORDINARY-COMMAND-PROFILE", commandCount_);

        if (!binding || !binding->sharedOwnerMatches(binding))
            return failCommand("RHI-BINDLESS-PREPARED-GRAPHICS-PAYLOAD-MISMATCH", commandCount_);
        if (!active_ || ended_ || !graphicsExecutable_ || !scope_ || binding != scope_)
            return failCommand("RHI-BINDLESS-GRAPHICS-PASS-SCOPE-MISMATCH", commandCount_);
        if (!activePipeline_ || scope_->owners()->desc().pipeline.get() != activePipeline_)
            return failCommand("RHI-BINDLESS-PREPARED-GRAPHICS-PIPELINE-MISMATCH", commandCount_);
        const auto valid = binding->validateForRecord(*activePipeline_, accesses);
        if (!valid.success)
            return failCommand(valid.diagnostics, commandCount_);
        const auto& snapshot = binding->owners()->desc().snapshot;
        const auto* descriptors = snapshot->descriptorSet();
        const auto capacity = boundDescriptors_.validateSetCapacity(snapshot->setIndex());
        if (!capacity.success)
            return failCommand(capacity.diagnostics, commandCount_);
        const auto compatible =
            rhi::validateDescriptorSetBinding(pipelineLayout_, snapshot->setIndex(), *descriptors);
        if (!compatible.success)
            return failCommand(compatible.diagnostics, commandCount_);
        core::pmr::String diagnostic;
        if (!nativePayload_->recordScopedGraphicsBinding(
                metalRhiPreparedGraphicsPayloadFor(*binding), accesses, diagnostic))
            return failCommand(diagnostic, commandCount_);
        const auto tracked =
            boundDescriptors_.bindSet(snapshot->setIndex(), *descriptors, nullptr, 0u);
        if (!tracked.success) {
            nativePayload_->poisonRecording(tracked.diagnostics);
            return failCommand(tracked.diagnostics, commandCount_);
        }
        return commandOk(++commandCount_);
    }

    MetalRhiRenderPassEncoder(std::uint32_t& commandCount,
        bool& active,
        std::shared_ptr<MetalRhiNativeCommandPayload> nativePayload,
        const rhi::semantics::NativeBackendCapabilityContract& capabilityContract,
        std::shared_ptr<MetalRhiResourceRegistry> resourceRegistry,
        std::uint32_t subpassCount,
        core::pmr::Vector<rhi::SubpassDesc> subpasses,
        const std::pmr::vector<rhi::RenderPassColorAttachmentDesc>& colorAttachments,
        rhi::TextureFormat depthStencilFormat,
        std::uint32_t sampleCount,
        bool graphicsExecutable,
        std::uint64_t recordingIdentity,
        std::shared_ptr<const rhi::BindlessPreparedGraphicsBinding> scope,
        rhi::semantics::NativeCommandLoweringDecision drawLowering)
        : commandCount_(commandCount), active_(active), nativePayload_(std::move(nativePayload)),
          capabilityContract_(capabilityContract), resourceRegistry_(std::move(resourceRegistry)),
          subpassCount_(subpassCount == 0u ? 1u : subpassCount), subpasses_(std::move(subpasses)),
          depthStencilFormat_(depthStencilFormat), sampleCount_(sampleCount),
          graphicsExecutable_(graphicsExecutable), recordingIdentity_(recordingIdentity),
          scope_(std::move(scope)),
          graphicsScope_(nativePayload_ && nativePayload_->hasGraphicsPassScope()),
          drawLowering_(std::move(drawLowering))
    {
        colorFormats_.reserve(colorAttachments.size());
        for (const auto& attachment : colorAttachments) {
            colorFormats_.push_back(attachment.format);
        }
    }

    rhi::CommandValidationResult setPipeline(const rhi::RenderPipeline& pipeline) override
    {
        if (nativePayload_ && nativePayload_->hasOriginalOrdinary()) {
            core::pmr::String diagnostic;
            if (!nativePayload_->recordOriginalOrdinaryPipeline(
                    commandCount_, pipeline, diagnostic))
                return failCommand(diagnostic, commandCount_);
            return commandOk(++commandCount_);
        }

        if (ended_ || !active_)
            return failCommand("RHI-METAL-GRAPHICS-PASS-NOT-OPEN", commandCount_);
        if (scope_) {
            if (scope_->owners()->desc().pipeline.get() != &pipeline)
                return failCommand(
                    "RHI-BINDLESS-GRAPHICS-PASS-SCOPE-PIPELINE-MISMATCH", commandCount_);
            const auto valid = scope_->validateOwners();
            if (!valid.success)
                return failCommand(valid.diagnostics, commandCount_);
        } else if (pipeline.bindlessAuthorization()
            || !pipeline.shaderReflection().bindlessContractVersion.empty()) {
            return failCommand("RHI-METAL-BINDLESS-PREPARED-GRAPHICS-REQUIRED", commandCount_);
        }
        if (pipeline.nativeHandleExposedToUpperLayer()) {
            return failCommand("Metal RHI render pipeline exposed native handle", commandCount_);
        }
        if (!nativePayload_) {
            return failCommand("RHI-METAL-RENDER-PIPELINE-NATIVE-PAYLOAD-MISSING", commandCount_);
        }
        if (nativePayload_) {
            std::pmr::string diagnostics;
            const auto pipelinePayload = metalRhiNativePayloadFor(pipeline);
            if (!pipelinePayload || pipelinePayload->subpassIndex != subpassIndex_) {
                return failCommand("RHI-METAL-PIPELINE-SUBPASS-INDEX-MISMATCH", commandCount_);
            }
            if (pipeline.validationReport().colorTargetCount != colorFormats_.size()
                || pipeline.validationReport().sampleCount != sampleCount_
                || pipeline.validationReport().depthStencilFormat != depthStencilFormat_
                || pipeline.validationReport().colorFormats.size() != colorFormats_.size()
                || sampleCount_ == 0u) {
                return failCommand(
                    "RHI-METAL-PIPELINE-RENDER-PASS-ATTACHMENT-MISMATCH", commandCount_);
            }
            for (std::size_t formatIndex = 0u; formatIndex < colorFormats_.size(); ++formatIndex) {
                if (pipeline.validationReport().colorFormats[formatIndex]
                    != colorFormats_[formatIndex]) {
                    return failCommand(
                        "RHI-METAL-PIPELINE-RENDER-PASS-COLOR-FORMAT-MISMATCH", commandCount_);
                }
            }
            if (!subpasses_.empty()) {
                if (!pipelinePayload->renderPassCompatibilityEnabled
                    || subpassIndex_ >= subpasses_.size()) {
                    return failCommand(
                        "RHI-METAL-PIPELINE-SUBPASS-COMPATIBILITY-MISSING", commandCount_);
                }
                const auto& subpass = subpasses_[subpassIndex_];
                if (pipeline.validationReport().colorTargetCount
                        != subpass.colorAttachmentIndices.size()
                    || pipelinePayload->usesDepthStencilAttachment
                        != subpass.useDepthStencilAttachment) {
                    return failCommand(
                        "RHI-METAL-PIPELINE-SUBPASS-ATTACHMENT-MISMATCH", commandCount_);
                }
                for (const auto& binding : pipeline.shaderReflection().descriptorBindings) {
                    if (binding.type != rhi::DescriptorType::InputAttachment) {
                        continue;
                    }
                    if (binding.depthTexture) {
                        if (!subpass.readDepthInputAttachment
                            || !subpass.useDepthStencilAttachment) {
                            return failCommand(
                                "RHI-METAL-PIPELINE-DEPTH-FETCH-NOT-IN-CURRENT-SUBPASS",
                                commandCount_);
                        }
                    } else if (std::find(subpass.inputAttachmentIndices.begin(),
                                   subpass.inputAttachmentIndices.end(),
                                   binding.subpassAttachmentIndex)
                        == subpass.inputAttachmentIndices.end()) {
                        return failCommand(
                            "RHI-METAL-PIPELINE-FETCH-NOT-IN-CURRENT-SUBPASS", commandCount_);
                    }
                }
            }
            if (!nativePayload_->recordGraphicsPipelineBind(pipelinePayload, diagnostics)) {
                return failCommand(diagnostics, commandCount_);
            }
            boundPipelinePayload_ = pipelinePayload;
        }
        pipelineLayout_ = pipeline.layout();
        shaderReflection_ = &pipeline.shaderReflection();
        pipelineBound_ = true;
        activePipeline_ = &pipeline;
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult setViewport(const rhi::ViewportState& viewport) override
    {
        if (nativePayload_ && nativePayload_->hasOriginalOrdinary()) {
            core::pmr::String diagnostic;
            if (!nativePayload_->recordOriginalOrdinaryScalar(commandCount_,
                    *rhi::CommandScalarArguments::capture(
                        rhi::CommandUsageKind::SetViewport, viewport),
                    diagnostic))
                return failCommand(diagnostic, commandCount_);
            return commandOk(++commandCount_);
        }

        if (ended_ || !active_)
            return failCommand("RHI-METAL-GRAPHICS-PASS-NOT-OPEN", commandCount_);
        const auto valueResult = rhi::semantics::validateViewportCommand(viewport);
        if (!valueResult.success) {
            return failCommand(valueResult.diagnostics, commandCount_);
        }
        if (!pipelineBound_) {
            return failCommand(rhi::semantics::ViewportScissorDiagCode::kNoPipeline
                    + std::pmr::string("Metal RHI setViewport requires a bound pipeline"),
                commandCount_);
        }
        if (!nativePayload_) {
            return failCommand(rhi::semantics::ViewportScissorDiagCode::kNoExecPayload
                    + std::pmr::string("Metal RHI setViewport requires a native command payload"),
                commandCount_);
        }
        std::pmr::string diagnostics;
        rhi::semantics::CommandWorkloadDesc workload;
        workload.commandCount = 1u;
        workload.usesRenderPass = true;
        workload.usesDynamicViewportScissor = true;
        if (!metalCommandLoweringAllows(capabilityContract_,
                workload,
                rhi::semantics::CommandRecordingIntent::PrimaryCommandBuffer,
                diagnostics)) {
            return failCommand(diagnostics, commandCount_);
        }
        if (!nativePayload_->recordSetViewport(viewport, diagnostics)) {
            return failCommand(diagnostics, commandCount_);
        }
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult setScissor(const rhi::ScissorState& scissor) override
    {
        if (nativePayload_ && nativePayload_->hasOriginalOrdinary()) {
            core::pmr::String diagnostic;
            if (!nativePayload_->recordOriginalOrdinaryScalar(commandCount_,
                    *rhi::CommandScalarArguments::capture(
                        rhi::CommandUsageKind::SetScissor, scissor),
                    diagnostic))
                return failCommand(diagnostic, commandCount_);
            return commandOk(++commandCount_);
        }

        if (ended_ || !active_)
            return failCommand("RHI-METAL-GRAPHICS-PASS-NOT-OPEN", commandCount_);
        const auto valueResult = rhi::semantics::validateScissorCommand(scissor);
        if (!valueResult.success) {
            return failCommand(valueResult.diagnostics, commandCount_);
        }
        if (!pipelineBound_) {
            return failCommand(rhi::semantics::ViewportScissorDiagCode::kNoPipeline
                    + std::pmr::string("Metal RHI setScissor requires a bound pipeline"),
                commandCount_);
        }
        if (!nativePayload_) {
            return failCommand(rhi::semantics::ViewportScissorDiagCode::kNoExecPayload
                    + std::pmr::string("Metal RHI setScissor requires a native command payload"),
                commandCount_);
        }
        std::pmr::string diagnostics;
        rhi::semantics::CommandWorkloadDesc workload;
        workload.commandCount = 1u;
        workload.usesRenderPass = true;
        workload.usesDynamicViewportScissor = true;
        if (!metalCommandLoweringAllows(capabilityContract_,
                workload,
                rhi::semantics::CommandRecordingIntent::PrimaryCommandBuffer,
                diagnostics)) {
            return failCommand(diagnostics, commandCount_);
        }
        if (!nativePayload_->recordSetScissor(scissor, diagnostics)) {
            return failCommand(diagnostics, commandCount_);
        }
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult bindVertexBuffer(
        std::uint32_t slot, const rhi::GpuBuffer& buffer, std::uint64_t offsetBytes) override
    {
        if (nativePayload_ && nativePayload_->hasOriginalOrdinary())
            return failCommand("RHI-METAL-ORDINARY-COMMAND-PROFILE", commandCount_);

        if ((buffer.desc().usage & rhi::ResourceUsage::VertexBuffer) == rhi::ResourceUsage::None) {
            return failCommand("RHI-METAL-VERTEX-BUFFER-USAGE-MISSING", commandCount_);
        }
        if (!nativePayload_) {
            return failCommand("RHI-METAL-VERTEX-BUFFER-NATIVE-PAYLOAD-MISSING", commandCount_);
        }
        if (nativePayload_) {
            const auto resource =
                resourceRegistry_ ? resourceRegistry_->find(buffer.id()) : std::nullopt;
            std::pmr::string diagnostics;
            if (!resource
                || !nativePayload_->recordVertexBufferBind(
                    slot, resource->buffer, offsetBytes, diagnostics)) {
                return failCommand(diagnostics.empty()
                        ? "Metal RHI vertex buffer native payload is missing"
                        : diagnostics,
                    commandCount_);
            }
        }
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult bindIndexBuffer(
        const rhi::GpuBuffer& buffer, rhi::IndexFormat format, std::uint64_t offsetBytes) override
    {
        if (nativePayload_ && nativePayload_->hasOriginalOrdinary())
            return failCommand("RHI-METAL-ORDINARY-COMMAND-PROFILE", commandCount_);

        if ((buffer.desc().usage & rhi::ResourceUsage::IndexBuffer) == rhi::ResourceUsage::None) {
            return failCommand("RHI-METAL-INDEX-BUFFER-USAGE-MISSING", commandCount_);
        }
        if (!nativePayload_) {
            return failCommand("RHI-METAL-INDEX-BUFFER-NATIVE-PAYLOAD-MISSING", commandCount_);
        }
        if (nativePayload_) {
            const auto resource =
                resourceRegistry_ ? resourceRegistry_->find(buffer.id()) : std::nullopt;
            std::pmr::string diagnostics;
            if (!resource
                || !nativePayload_->recordIndexBufferBind(
                    resource->buffer, format, offsetBytes, diagnostics)) {
                return failCommand(diagnostics.empty()
                        ? "Metal RHI index buffer native payload is missing"
                        : diagnostics,
                    commandCount_);
            }
        }
        indexBufferBound_ = true;
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult bindTessellationFactorBuffer(const rhi::GpuBuffer& buffer,
        std::uint64_t offsetBytes,
        std::uint32_t instanceStrideBytes) override
    {
        if (nativePayload_ && nativePayload_->hasOriginalOrdinary())
            return failCommand("RHI-METAL-ORDINARY-COMMAND-PROFILE", commandCount_);

        if (!hasResourceUsage(buffer.desc().usage, rhi::ResourceUsage::StorageBuffer)) {
            return failCommand("RHI-METAL-TESSELLATION-FACTOR-BUFFER-USAGE-MISSING", commandCount_);
        }
        if (instanceStrideBytes == 0u || (offsetBytes & 3u) != 0u
            || (instanceStrideBytes & 3u) != 0u || offsetBytes > buffer.desc().sizeBytes
            || buffer.desc().sizeBytes - offsetBytes < sizeof(std::uint32_t)) {
            return failCommand("RHI-METAL-TESSELLATION-FACTOR-BUFFER-RANGE-INVALID", commandCount_);
        }
        if (pipelineBound_
            && (!boundPipelinePayload_
                || boundPipelinePayload_->primitiveTopology != rhi::PrimitiveTopology::PatchList)) {
            return failCommand(
                "RHI-METAL-TESSELLATION-FACTOR-BUFFER-REQUIRES-PATCHLIST", commandCount_);
        }
        if (pipelineBound_ && boundPipelinePayload_
            && (boundPipelinePayload_->metalTessellationFactorInstanceStrideBytes
                    != instanceStrideBytes
                || !boundPipelinePayload_->metalTessellationAbiRecorded
                || boundPipelinePayload_->metalTessellationFactorBytesPerPatch == 0u
                || buffer.desc().sizeBytes - offsetBytes
                    < boundPipelinePayload_->metalTessellationFactorBytesPerPatch)) {
            return failCommand("RHI-METAL-TESSELLATION-FACTOR-BUFFER-ABI-MISMATCH", commandCount_);
        }
        if (!nativePayload_) {
            return failCommand(
                "RHI-METAL-TESSELLATION-FACTOR-BUFFER-NATIVE-PAYLOAD-MISSING", commandCount_);
        }
        const auto resource =
            resourceRegistry_ ? resourceRegistry_->find(buffer.id()) : std::nullopt;
        std::pmr::string diagnostics;
        if (!resource
            || !nativePayload_->recordTessellationFactorBufferBind(
                resource->buffer, offsetBytes, instanceStrideBytes, diagnostics)) {
            return failCommand(diagnostics.empty()
                    ? "RHI-METAL-TESSELLATION-FACTOR-BUFFER-NATIVE-PAYLOAD-MISSING"
                    : diagnostics,
                commandCount_);
        }
        tessellationFactorBufferBound_ = true;
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult bindDescriptorSet(std::uint32_t set,
        const rhi::DescriptorSet& descriptorSet,
        const std::uint32_t* dynamicOffsets,
        std::uint32_t dynamicOffsetCount) override
    {
        if (nativePayload_ && nativePayload_->hasOriginalOrdinary()) {
            core::pmr::String diagnostic;
            if (!nativePayload_->recordOriginalOrdinaryDescriptor(commandCount_,
                    set,
                    descriptorSet,
                    dynamicOffsets,
                    dynamicOffsetCount,
                    diagnostic))
                return failCommand(diagnostic, commandCount_);
            return commandOk(++commandCount_);
        }

        if (graphicsScope_)
            return failCommand("RHI-METAL-GRAPHICS-SCOPE-BINDING-MISMATCH", commandCount_);
        const auto trackingCapacity = boundDescriptors_.validateSetCapacity(set);
        if (!trackingCapacity.success)
            return failCommand(trackingCapacity.diagnostics, commandCount_);
        if (descriptorSetNeedsArgumentBuffers(descriptorSet)
            && !rhi::semantics::hasExecutableNativeFeature(
                capabilityContract_, rhi::semantics::NativeBackendFeature::ArgumentBuffers)) {
            return failCommand(
                "RHI-METAL-DESCRIPTOR-ARRAY-CAPABILITY-NOT-AUTHORIZED", commandCount_);
        }
        if (descriptorSet.stats().nativeHandleExposedToUpperLayer) {
            return failCommand("Metal RHI descriptor set exposed native handle", commandCount_);
        }
        const auto offsets = rhi::validateDynamicOffsetArguments(
            descriptorSet.layout(), dynamicOffsets, dynamicOffsetCount);
        if (!offsets.success) {
            return failCommand(offsets.diagnostics, commandCount_);
        }
        if (!descriptorSetMatchesPipeline(pipelineLayout_, set, descriptorSet)) {
            return failCommand(
                "Metal RHI descriptor set does not match the bound pipeline layout", commandCount_);
        }
        if (!nativePayload_) {
            return failCommand("RHI-METAL-DESCRIPTOR-NATIVE-PAYLOAD-MISSING", commandCount_);
        }
        if (nativePayload_) {
            std::pmr::string diagnostics;
            if (!descriptorSetIsComplete(descriptorSet, diagnostics)) {
                return failCommand(diagnostics, commandCount_);
            }
            const auto nativeDescriptorSet = metalRhiNativePayloadFor(descriptorSet);
            if (!nativeDescriptorSet
                || !nativePayload_->recordDescriptorSetBind(set,
                    nativeDescriptorSet,
                    rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment,
                    dynamicOffsets,
                    dynamicOffsetCount,
                    diagnostics)) {
                return failCommand(diagnostics, commandCount_);
            }
        }
        const auto tracked =
            boundDescriptors_.bindSet(set, descriptorSet, dynamicOffsets, dynamicOffsetCount);
        if (!tracked.success)
            return failCommand(tracked.diagnostics, commandCount_);
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult bindDescriptorTable(
        std::uint32_t set, const rhi::DescriptorTable& descriptorTable) override
    {
        if (nativePayload_ && nativePayload_->hasOriginalOrdinary())
            return failCommand("RHI-METAL-ORDINARY-COMMAND-PROFILE", commandCount_);

        if (graphicsScope_)
            return failCommand("RHI-METAL-GRAPHICS-SCOPE-BINDING-MISMATCH", commandCount_);
        const auto trackingCapacity = boundDescriptors_.validateTableCapacity(set);
        if (!trackingCapacity.success)
            return failCommand(trackingCapacity.diagnostics, commandCount_);
        if (!rhi::semantics::hasExecutableNativeFeature(
                capabilityContract_, rhi::semantics::NativeBackendFeature::ArgumentBuffers)) {
            return failCommand(
                "RHI-METAL-DESCRIPTOR-TABLE-CAPABILITY-NOT-AUTHORIZED", commandCount_);
        }
        if (!pipelineLayout_
            || !declaresDescriptorTableSlot(pipelineLayout_, set, descriptorTable)) {
            return failCommand(
                "Metal RHI descriptor table does not match the bound pipeline layout",
                commandCount_);
        }
        if (!nativePayload_) {
            return failCommand("RHI-METAL-DESCRIPTOR-TABLE-NATIVE-PAYLOAD-MISSING", commandCount_);
        }
        if (nativePayload_) {
            std::pmr::string diagnostics;
            if (!descriptorTableIsComplete(descriptorTable, diagnostics)) {
                return failCommand(diagnostics, commandCount_);
            }
            const auto nativeTable = metalRhiNativePayloadFor(descriptorTable);
            if (!nativeTable
                || !nativePayload_->recordDescriptorTableBind(set,
                    nativeTable,
                    rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment,
                    diagnostics)) {
                return failCommand(diagnostics.empty()
                        ? "Metal RHI descriptor table native payload is missing"
                        : diagnostics,
                    commandCount_);
            }
        }
        const auto tracked = boundDescriptors_.bindTable(set, descriptorTable);
        if (!tracked.success)
            return failCommand(tracked.diagnostics, commandCount_);
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult pushConstants(rhi::ShaderStage stages,
        std::uint32_t offsetBytes,
        std::uint32_t sizeBytes,
        const void* data) override
    {
        if (nativePayload_ && nativePayload_->hasOriginalOrdinary())
            return failCommand("RHI-METAL-ORDINARY-COMMAND-PROFILE", commandCount_);

        auto cmdResult =
            rhi::semantics::validatePushConstantCommand(stages, offsetBytes, sizeBytes, data);
        if (!cmdResult.success) {
            return failCommand(cmdResult.diagnostics, commandCount_);
        }
        auto bindResult = rhi::semantics::validatePushConstantBinding(pipelineLayout_,
            rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment,
            stages,
            offsetBytes,
            sizeBytes,
            shaderReflection_);
        if (!bindResult.success) {
            return failCommand(bindResult.diagnostics, commandCount_);
        }
        if (pushState_.hasUnconsumedUpdate()) {
            return failCommand(rhi::semantics::PushConstantDiagCode::kDuplicateUnconsumed
                    + std::pmr::string(
                        "push constants already set without intervening draw/dispatch"),
                commandCount_);
        }
        rhi::semantics::CommandWorkloadDesc workload;
        workload.commandCount = 1u;
        workload.usesPushConstants = true;
        std::pmr::string loweringDiagnostics;
        if (!metalCommandLoweringAllows(capabilityContract_,
                workload,
                rhi::semantics::CommandRecordingIntent::PrimaryCommandBuffer,
                loweringDiagnostics)) {
            return failCommand(loweringDiagnostics, commandCount_);
        }
        // P2 executable-payload gate.
        if (!nativePayload_) {
            return failCommand(rhi::semantics::PushConstantDiagCode::kNoExecPayload
                    + std::pmr::string("Metal push constants require a native command payload"),
                commandCount_);
        }
        {
            std::pmr::string diagnostics;
            if (!nativePayload_->recordPushConstants(
                    stages, offsetBytes, sizeBytes, data, diagnostics)) {
                return failCommand(diagnostics, commandCount_);
            }
        }
        pushState_.onPush();
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult executeSecondary(
        const rhi::SecondaryCommandBuffer& commandBuffer) override;

    rhi::CommandValidationResult executeRenderBundle(const rhi::RenderBundle& bundle) override;

    rhi::CommandValidationResult drawIndexed(std::uint32_t indexCount,
        std::uint32_t instanceCount,
        std::uint32_t firstIndex,
        std::int32_t vertexOffset,
        std::uint32_t firstInstance) override
    {
        if (nativePayload_ && nativePayload_->hasOriginalOrdinary())
            return failCommand("RHI-METAL-ORDINARY-COMMAND-PROFILE", commandCount_);

        if (ended_ || !active_)
            return failCommand("RHI-METAL-GRAPHICS-PASS-NOT-OPEN", commandCount_);
        if (scope_) {
            const auto valid = scope_->validateOwners();
            if (!valid.success)
                return failCommand(valid.diagnostics, commandCount_);
        }
        const auto descriptorUse = boundDescriptors_.validateLiveBindings();
        if (!descriptorUse.success)
            return failCommand(descriptorUse.diagnostics, commandCount_);
        if (indexCount == 0) {
            return failCommand("Metal RHI draw indexed requires indices", commandCount_);
        }
        if (!indexBufferBound_) {
            return failCommand("RHI-METAL-DRAW-INDEXED-REQUIRES-INDEX-BUFFER", commandCount_);
        }
        const auto lowering = rhi::semantics::validateNativeCommandLoweringUse(drawLowering_,
            rhi::semantics::NativeCommandSemantic::MetalRenderCommandEncoder,
            drawLowering_.fallbackMode,
            "Metal indexed draw");
        if (!lowering.success)
            return failCommand(lowering.diagnostics, commandCount_);
        if (boundPipelinePayload_
            && boundPipelinePayload_->primitiveTopology == rhi::PrimitiveTopology::PatchList
            && !tessellationFactorBufferBound_) {
            return failCommand(
                "RHI-METAL-PATCH-DRAW-REQUIRES-TESSELLATION-FACTOR-BUFFER", commandCount_);
        }
        if (!nativePayload_) {
            return failCommand("RHI-METAL-DRAW-INDEXED-NATIVE-PAYLOAD-MISSING", commandCount_);
        }
        if (nativePayload_) {
            std::pmr::string diagnostics;
            if (!nativePayload_->recordDrawIndexed(indexCount,
                    instanceCount,
                    firstIndex,
                    vertexOffset,
                    firstInstance,
                    diagnostics)) {
                return failCommand(diagnostics, commandCount_);
            }
        }
        pushState_.onConsume();
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult drawIndexedIndirect(const rhi::GpuBuffer& indirectBuffer,
        std::uint64_t indirectOffset,
        std::uint32_t drawCount,
        std::uint32_t stride) override
    {
        if (nativePayload_ && nativePayload_->hasOriginalOrdinary())
            return failCommand("RHI-METAL-ORDINARY-COMMAND-PROFILE", commandCount_);

        if (graphicsScope_)
            return failCommand("RHI-METAL-GRAPHICS-SCOPE-INDIRECT-UNSUPPORTED", commandCount_);
        const auto descriptorUse = boundDescriptors_.validateLiveBindings();
        if (!descriptorUse.success)
            return failCommand(descriptorUse.diagnostics, commandCount_);
        if (!indexBufferBound_) {
            return failCommand("RHI-METAL-INDIRECT-DRAW-REQUIRES-INDEX-BUFFER", commandCount_);
        }
        rhi::semantics::CommandWorkloadDesc workload;
        workload.commandCount = 1u;
        workload.usesRenderPass = true;
        workload.usesIndirectDraw = true;
        workload.usesPatchListIndirectDraw = pipelineBound_ && boundPipelinePayload_
            && boundPipelinePayload_->primitiveTopology == rhi::PrimitiveTopology::PatchList;
        const auto lowering = rhi::semantics::lowerCommandWorkload(capabilityContract_,
            workload,
            rhi::semantics::CommandRecordingIntent::PrimaryCommandBuffer);
        const auto loweringUse = rhi::semantics::validateNativeCommandLoweringUse(lowering,
            rhi::semantics::NativeCommandSemantic::MetalIndirectDrawCommand,
            rhi::FallbackMode::Native,
            "Metal indirect draw");
        if (!loweringUse.success) {
            return failCommand(loweringUse.diagnostics, commandCount_);
        }
        if (drawCount == 0 || stride < sizeof(rhi::DrawIndexedIndirectCommand)
            || (stride & 3u) != 0u || (indirectOffset & 3u) != 0u
            || static_cast<std::uint32_t>(
                   indirectBuffer.desc().usage & rhi::ResourceUsage::IndirectBuffer)
                == 0u
            || indirectOffset > indirectBuffer.desc().sizeBytes
            || indirectBuffer.desc().sizeBytes - indirectOffset
                < sizeof(rhi::DrawIndexedIndirectCommand)
            || drawCount - 1u > (indirectBuffer.desc().sizeBytes - indirectOffset
                                    - sizeof(rhi::DrawIndexedIndirectCommand))
                    / stride) {
            return failCommand("Metal RHI indirect draw arguments are invalid", commandCount_);
        }
        const auto resource =
            resourceRegistry_ ? resourceRegistry_->find(indirectBuffer.id()) : std::nullopt;
        std::pmr::string diagnostics;
        if (!nativePayload_ || !resource
            || !nativePayload_->recordDrawIndexedIndirect(
                resource->buffer, indirectOffset, drawCount, stride, diagnostics)) {
            return failCommand(diagnostics.empty()
                    ? "Metal RHI indirect draw native payload is missing"
                    : diagnostics,
                commandCount_);
        }
        pushState_.onConsume();
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult drawIndexedIndirectCount(const rhi::GpuBuffer& argsBuffer,
        std::uint64_t argsOffset,
        const rhi::GpuBuffer& countBuffer,
        std::uint64_t countOffset,
        std::uint32_t maxDrawCount,
        std::uint32_t stride) override
    {
        if (nativePayload_ && nativePayload_->hasOriginalOrdinary())
            return failCommand("RHI-METAL-ORDINARY-COMMAND-PROFILE", commandCount_);

        if (graphicsScope_)
            return failCommand("RHI-METAL-GRAPHICS-SCOPE-INDIRECT-UNSUPPORTED", commandCount_);
        const auto descriptorUse = boundDescriptors_.validateLiveBindings();
        if (!descriptorUse.success)
            return failCommand(descriptorUse.diagnostics, commandCount_);
        if (!pipelineBound_ || !boundPipelinePayload_) {
            return failCommand(
                "RHI-METAL-GPU-COUNTED-INDIRECT-REQUIRES-BOUND-PIPELINE", commandCount_);
        }
        if (!indexBufferBound_) {
            return failCommand(
                "RHI-METAL-GPU-COUNTED-INDIRECT-REQUIRES-INDEX-BUFFER", commandCount_);
        }
        rhi::semantics::CommandWorkloadDesc workload;
        workload.commandCount = 1u;
        workload.usesRenderPass = true;
        workload.usesIndirectDraw = true;
        workload.usesGpuCountedIndirectDraw = true;
        workload.usesPatchListIndirectDraw =
            boundPipelinePayload_->primitiveTopology == rhi::PrimitiveTopology::PatchList;
        workload.usesMetalGpuCountedIndirectMaskAbi =
            boundPipelinePayload_->metalGpuCountedIndirectContractVersion
                == rhi::kMetalGpuCountedIndirectContractVersion
            && boundPipelinePayload_->metalGpuCountedIndirectStatus == "Available"
            && boundPipelinePayload_->metalGpuCountedIndirectUsesDrawId
            && boundPipelinePayload_->metalGpuCountedIndirectMasksOutOfRange
            && boundPipelinePayload_->metalGpuCountedIndirectCountBufferIndex < 31u;
        const auto lowering = rhi::semantics::lowerCommandWorkload(capabilityContract_,
            workload,
            rhi::semantics::CommandRecordingIntent::PrimaryCommandBuffer);
        const auto indirectUse = rhi::semantics::validateNativeCommandLoweringUse(lowering,
            rhi::semantics::NativeCommandSemantic::MetalIndirectDrawCommand,
            rhi::FallbackMode::Emulated,
            "Metal GPU-counted indirect draw");
        if (!indirectUse.success) {
            return failCommand(indirectUse.diagnostics, commandCount_);
        }
        const auto maskUse = rhi::semantics::validateNativeCommandLoweringUse(lowering,
            rhi::semantics::NativeCommandSemantic::MetalGpuCountedIndirectMask,
            rhi::FallbackMode::Emulated,
            "Metal GPU-counted indirect mask");
        if (!maskUse.success) {
            return failCommand(maskUse.diagnostics, commandCount_);
        }
        const auto recordSize = sizeof(rhi::DrawIndexedIndirectCommand);
        if (maxDrawCount == 0u || stride < recordSize || (stride & 3u) != 0u
            || (argsOffset & 3u) != 0u || (countOffset & 3u) != 0u
            || (static_cast<std::uint32_t>(
                    argsBuffer.desc().usage & rhi::ResourceUsage::IndirectBuffer)
                == 0u)
            || (static_cast<std::uint32_t>(
                    countBuffer.desc().usage & rhi::ResourceUsage::StorageBuffer)
                == 0u)
            || argsOffset > argsBuffer.desc().sizeBytes
            || countOffset > countBuffer.desc().sizeBytes
            || argsBuffer.desc().sizeBytes - argsOffset < recordSize
            || countBuffer.desc().sizeBytes - countOffset < sizeof(std::uint32_t)
            || maxDrawCount - 1u > (std::numeric_limits<std::uint64_t>::max() - argsOffset) / stride
            || argsOffset + static_cast<std::uint64_t>(maxDrawCount - 1u) * stride
                > argsBuffer.desc().sizeBytes
            || argsBuffer.desc().sizeBytes
                    - (argsOffset + static_cast<std::uint64_t>(maxDrawCount - 1u) * stride)
                < recordSize) {
            return failCommand(
                "RHI-METAL-DRAW-INDEXED-INDIRECT-COUNT-ARGUMENTS-INVALID", commandCount_);
        }
        const auto argsResource =
            resourceRegistry_ ? resourceRegistry_->find(argsBuffer.id()) : std::nullopt;
        const auto countResource =
            resourceRegistry_ ? resourceRegistry_->find(countBuffer.id()) : std::nullopt;
        if (!argsResource || !countResource || !argsResource->buffer || !countResource->buffer) {
            return failCommand(
                "RHI-METAL-DRAW-INDEXED-INDIRECT-COUNT-RESOURCE-MISSING", commandCount_);
        }
        if (!nativePayload_) {
            return failCommand(
                "RHI-METAL-DRAW-INDEXED-INDIRECT-COUNT-NATIVE-PAYLOAD-MISSING", commandCount_);
        }
        std::pmr::string diagnostics;
        if (!nativePayload_->recordDrawIndexedIndirectCount(argsResource->buffer,
                argsOffset,
                countResource->buffer,
                countOffset,
                maxDrawCount,
                stride,
                diagnostics)) {
            return failCommand(diagnostics.empty()
                    ? "RHI-METAL-DRAW-INDEXED-INDIRECT-COUNT-NATIVE-PAYLOAD-MISSING"
                    : diagnostics,
                commandCount_);
        }
        pushState_.onConsume();
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult draw(std::uint32_t vertexCount,
        std::uint32_t instanceCount,
        std::uint32_t firstVertex,
        std::uint32_t firstInstance) override
    {
        if (nativePayload_ && nativePayload_->hasOriginalOrdinary()) {
            core::pmr::String diagnostic;
            if (!nativePayload_->recordOriginalOrdinaryScalar(commandCount_,
                    *rhi::CommandScalarArguments::capture(rhi::CommandUsageKind::Draw,
                        rhi::CommandScalarArguments::Draw{
                            vertexCount, instanceCount, firstVertex, firstInstance}),
                    diagnostic))
                return failCommand(diagnostic, commandCount_);
            return commandOk(++commandCount_);
        }

        if (ended_ || !active_)
            return failCommand("RHI-METAL-GRAPHICS-PASS-NOT-OPEN", commandCount_);
        if (scope_) {
            const auto valid = scope_->validateOwners();
            if (!valid.success)
                return failCommand(valid.diagnostics, commandCount_);
        }
        const auto descriptorUse = boundDescriptors_.validateLiveBindings();
        if (!descriptorUse.success)
            return failCommand(descriptorUse.diagnostics, commandCount_);
        if (vertexCount == 0) {
            return failCommand("Metal RHI draw requires vertices", commandCount_);
        }
        const auto lowering = rhi::semantics::validateNativeCommandLoweringUse(drawLowering_,
            rhi::semantics::NativeCommandSemantic::MetalRenderCommandEncoder,
            drawLowering_.fallbackMode,
            "Metal draw");
        if (!lowering.success)
            return failCommand(lowering.diagnostics, commandCount_);
        if (boundPipelinePayload_
            && boundPipelinePayload_->primitiveTopology == rhi::PrimitiveTopology::PatchList
            && !tessellationFactorBufferBound_) {
            return failCommand(
                "RHI-METAL-PATCH-DRAW-REQUIRES-TESSELLATION-FACTOR-BUFFER", commandCount_);
        }
        if (!nativePayload_) {
            return failCommand("RHI-METAL-DRAW-NATIVE-PAYLOAD-MISSING", commandCount_);
        }
        if (nativePayload_) {
            std::pmr::string diagnostics;
            if (!nativePayload_->recordDraw(
                    vertexCount, instanceCount, firstVertex, firstInstance, diagnostics)) {
                return failCommand(diagnostics, commandCount_);
            }
        }
        pushState_.onConsume();
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult writeTimestamp(
        const rhi::QueryPool& pool, std::uint32_t queryIndex) override
    {
        if (nativePayload_ && nativePayload_->hasOriginalOrdinary())
            return failCommand("RHI-METAL-ORDINARY-COMMAND-PROFILE", commandCount_);

        const auto* metalPool = asMetalRhiQueryPool(pool);
        if (!metalPool || metalPool->type() != rhi::QueryType::Timestamp || !metalPool->payload()
            || queryIndex >= metalPool->count()) {
            return failCommand("RHI-METAL-TIMESTAMP-QUERY-POOL-INVALID", commandCount_);
        }
        rhi::semantics::CommandWorkloadDesc workload;
        workload.commandCount = 1u;
        workload.usesRenderPass = true;
        workload.usesTimestampQuery = true;
        std::pmr::string loweringDiagnostics;
        if (!metalCommandLoweringAllows(capabilityContract_,
                workload,
                rhi::semantics::CommandRecordingIntent::PrimaryCommandBuffer,
                loweringDiagnostics)) {
            return failCommand(loweringDiagnostics, commandCount_);
        }
        std::pmr::string diagnostics;
        if (!nativePayload_
            || !nativePayload_->recordTimestamp(metalPool->payload(), queryIndex, diagnostics)) {
            return failCommand(diagnostics.empty()
                    ? "RHI-METAL-TIMESTAMP-QUERY-NATIVE-PAYLOAD-MISSING"
                    : diagnostics,
                commandCount_);
        }
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult writePipelineStatistics(
        const rhi::QueryPool& pool, std::uint32_t queryIndex) override
    {
        if (nativePayload_ && nativePayload_->hasOriginalOrdinary())
            return failCommand("RHI-METAL-ORDINARY-COMMAND-PROFILE", commandCount_);

        const auto* metalPool = asMetalRhiQueryPool(pool);
        if (!metalPool || metalPool->type() != rhi::QueryType::PipelineStatistics
            || !metalPool->payload() || queryIndex >= metalPool->count()) {
            return failCommand("RHI-METAL-PIPELINE-STATISTICS-QUERY-POOL-INVALID", commandCount_);
        }
        rhi::semantics::CommandWorkloadDesc workload;
        workload.commandCount = 1u;
        workload.usesRenderPass = true;
        workload.usesPipelineStatisticsQuery = true;
        std::pmr::string loweringDiagnostics;
        if (!metalCommandLoweringAllows(capabilityContract_,
                workload,
                rhi::semantics::CommandRecordingIntent::PrimaryCommandBuffer,
                loweringDiagnostics)) {
            return failCommand(loweringDiagnostics, commandCount_);
        }
        std::pmr::string diagnostics;
        if (!nativePayload_
            || !nativePayload_->recordPipelineStatistics(
                metalPool->payload(), queryIndex, diagnostics)) {
            return failCommand(diagnostics.empty()
                    ? "RHI-METAL-PIPELINE-STATISTICS-QUERY-NATIVE-PAYLOAD-MISSING"
                    : diagnostics,
                commandCount_);
        }
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult nextSubpass() override
    {
        if (nativePayload_ && nativePayload_->hasOriginalOrdinary())
            return failCommand("RHI-METAL-ORDINARY-COMMAND-PROFILE", commandCount_);

        if (graphicsScope_)
            return failCommand("RHI-METAL-GRAPHICS-SCOPE-SUBPASS-UNSUPPORTED", commandCount_);
        if (subpassCount_ <= 1u) {
            return commandOk(commandCount_);
        }
        if (subpassIndex_ + 1u >= subpassCount_) {
            return failCommand("RHI-METAL-NEXT-SUBPASS-EXCEEDS-SUBPASS-COUNT", commandCount_);
        }
        if (!nativePayload_) {
            return failCommand("RHI-METAL-NEXT-SUBPASS-NATIVE-PAYLOAD-MISSING", commandCount_);
        }
        std::pmr::string diagnostics;
        if (!nativePayload_->recordNextSubpass(diagnostics)) {
            return failCommand(diagnostics, commandCount_);
        }
        ++subpassIndex_;
        // Dynamic state and the pipeline are subpass-local in the RHI
        // contract. The next subpass must explicitly bind them again.
        pipelineBound_ = false;
        boundPipelinePayload_.reset();
        pipelineLayout_ = nullptr;
        shaderReflection_ = nullptr;
        tessellationFactorBufferBound_ = false;
        boundDescriptors_.reset();
        return commandOk(++commandCount_);
    }

    std::uint32_t currentSubpassIndex() const override { return subpassIndex_; }

    rhi::CommandValidationResult end() override
    {
        if (nativePayload_ && nativePayload_->hasOriginalOrdinary()) {
            core::pmr::String diagnostic;
            if (!nativePayload_->recordOriginalOrdinaryScope(
                    rhi::CommandUsageKind::EndRenderPass, commandCount_, nullptr, diagnostic))
                return failCommand(diagnostic, commandCount_);
            active_ = false;
            ended_ = true;
            return commandOk(++commandCount_);
        }

        if (ended_ || !active_)
            return failCommand("RHI-METAL-GRAPHICS-PASS-NOT-OPEN", commandCount_);
        if (!nativePayload_) {
            return failCommand("RHI-METAL-RENDER-PASS-NATIVE-PAYLOAD-MISSING", commandCount_);
        }
        if (nativePayload_) {
            std::pmr::string diagnostics;
            if (!nativePayload_->recordRenderPassEnd(diagnostics)) {
                return failCommand(diagnostics, commandCount_);
            }
        }
        active_ = false;
        ended_ = true;
        scope_.reset();
        pushState_.onPassEnd();
        boundDescriptors_.reset();
        return commandOk(++commandCount_);
    }

private:
    rhi::CommandValidationResult failCommand(
        const core::pmr::String& diagnostic, std::uint32_t count)
    {
        if (nativePayload_ && nativePayload_->hasOriginalOrdinary())
            nativePayload_->poisonRecording(diagnostic);
        return commandFail(diagnostic, count);
    }

    rhi::BoundDescriptorSlots boundDescriptors_;
    std::uint32_t& commandCount_;
    bool& active_;
    std::shared_ptr<MetalRhiNativeCommandPayload> nativePayload_;
    rhi::semantics::NativeBackendCapabilityContract capabilityContract_;
    std::shared_ptr<MetalRhiNativePipelinePayload> boundPipelinePayload_;
    const rhi::PipelineLayout* pipelineLayout_ = nullptr;
    const rhi::ShaderReflectionDesc* shaderReflection_ = nullptr;
    std::shared_ptr<MetalRhiResourceRegistry> resourceRegistry_;
    rhi::semantics::PushConstantConsumptionState pushState_;
    bool pipelineBound_ = false; // L2 gate for setViewport/setScissor
    bool indexBufferBound_ = false;
    bool tessellationFactorBufferBound_ = false;
    std::uint32_t subpassIndex_ = 0;
    std::uint32_t subpassCount_ = 1;
    core::pmr::Vector<rhi::SubpassDesc> subpasses_;
    core::pmr::Vector<rhi::TextureFormat> colorFormats_{&core::defaultCpuMemoryResource()};
    rhi::TextureFormat depthStencilFormat_ = rhi::TextureFormat::Unknown;
    std::uint32_t sampleCount_ = 0u;
    bool graphicsExecutable_ = false;
    bool ended_ = false;
    std::uint64_t recordingIdentity_ = 0u;
    const rhi::RenderPipeline* activePipeline_ = nullptr;
    std::shared_ptr<const rhi::BindlessPreparedGraphicsBinding> scope_;
    bool graphicsScope_ = false;
    rhi::semantics::NativeCommandLoweringDecision drawLowering_;
};

class MetalRhiComputePassEncoder final : public rhi::ComputePassEncoder,
                                         public rhi::BindlessPassExtension {
public:
    rhi::CommandValidationResult bindSnapshot(std::shared_ptr<const rhi::BindlessSnapshot>,
        const rhi::BindlessLoweringPlan&,
        std::uint64_t,
        const rhi::BindlessAccessDeclarations&) override
    {
        if (nativePayload_ && nativePayload_->hasOriginalOrdinary())
            return failCommand("RHI-METAL-ORDINARY-COMMAND-PROFILE", commandCount_);

        return failCommand("RHI-METAL-BINDLESS-PREPARED-COMPUTE-REQUIRED", commandCount_);
    }

    rhi::CommandValidationResult bindPreparedComputeBinding(
        std::shared_ptr<const rhi::BindlessPreparedComputeBinding> binding,
        const rhi::BindlessAccessDeclarations& accesses) override
    {
        if (nativePayload_ && nativePayload_->hasOriginalOrdinary())
            return failCommand("RHI-METAL-ORDINARY-COMMAND-PROFILE", commandCount_);

        if (!binding || !binding->sharedOwnerMatches(binding))
            return failCommand("RHI-BINDLESS-PREPARED-COMPUTE-PAYLOAD-MISMATCH", commandCount_);
        const auto* prepared = dynamic_cast<const MetalRhiPreparedComputeBinding*>(binding.get());
        if (!prepared || !activePipeline_ || !binding->owners()
            || !binding->owners()->authorization()
            || binding->owners()->desc().pipeline.get() != activePipeline_)
            return failCommand("RHI-BINDLESS-PREPARED-COMPUTE-PIPELINE-MISMATCH", commandCount_);
        if (const auto* id =
                rhi::semantics::BindlessPipelineInterfaceContract::standaloneSnapshotDiagnostic(
                    binding->owners()->authorization()->package().artifact()))
            return failCommand(id, commandCount_);
        const auto valid =
            binding->validateForRecord(*binding->owners()->desc().pipeline, accesses);
        if (!valid.success)
            return failCommand(valid.diagnostics, commandCount_);
        const auto& desc = binding->owners()->desc();
        preparingBinding_ = std::move(binding);
        const auto result =
            bindPreparedSnapshot(desc.snapshot, desc.plan, desc.generation, accesses);
        preparingBinding_.reset();
        return result;
    }

    MetalRhiComputePassEncoder(std::uint32_t& commandCount,
        bool& active,
        std::shared_ptr<MetalRhiNativeCommandPayload> nativePayload,
        const rhi::semantics::NativeBackendCapabilityContract& capabilityContract,
        std::shared_ptr<MetalRhiResourceRegistry> resourceRegistry,
        bool bindlessExecutable,
        std::uint64_t bindlessDeviceIdentity,
        rhi::semantics::NativeCommandLoweringDecision dispatchLowering)
        : commandCount_(commandCount), active_(active), nativePayload_(std::move(nativePayload)),
          capabilityContract_(capabilityContract), resourceRegistry_(std::move(resourceRegistry)),
          bindlessExecutable_(bindlessExecutable), bindlessDeviceIdentity_(bindlessDeviceIdentity),
          recordingIdentity_(rhi::allocateGpuResourceId().value),
          dispatchLowering_(std::move(dispatchLowering))
    {}

    std::uint64_t recordingIdentity() const noexcept override { return recordingIdentity_; }

    void* queryInterface(std::string_view extension) noexcept override
    {
        return bindlessExecutable_ && extension == rhi::kExtensionBindlessCross
            ? static_cast<rhi::BindlessPassExtension*>(this)
            : nullptr;
    }

    rhi::CommandValidationResult bindPreparedSnapshot(
        std::shared_ptr<const rhi::BindlessSnapshot> snapshot,
        const rhi::BindlessLoweringPlan& selected,
        std::uint64_t generation,
        const rhi::BindlessAccessDeclarations& accesses)
    {
        if (!active_ || !bindlessExecutable_ || !activeAuthorization_
            || !activeAuthorization_->current(activeAuthorization_)) {
            return failCommand("RHI-BINDLESS-RECORD-AUTHORIZATION-INVALID", commandCount_);
        }
        if (boundSnapshot_) {
            return failCommand("RHI-BINDLESS-RECORD-SNAPSHOT-ALREADY-BOUND", commandCount_);
        }
        if (!snapshot) {
            return failCommand("RHI-BINDLESS-RECORD-SNAPSHOT-MISSING", commandCount_);
        }
        if (!snapshot->sharedOwnerMatches(snapshot))
            return failCommand("RHI-BINDLESS-RECORD-SNAPSHOT-OWNER-MISMATCH", commandCount_);
        const auto& authorizedPlan = activeAuthorization_->plan();
        const auto& artifact = activeAuthorization_->package().artifact();
        if (!selected.success || !selected.executable
            || selected.execution != rhi::BindlessExecution::Native
            || selected.identity != authorizedPlan.identity
            || selected.deviceIdentity != authorizedPlan.deviceIdentity
            || selected.shaderPackageIdentity != artifact.shaderPackageIdentity
            || artifact.execution != rhi::BindlessExecution::Native
            || snapshot->plan().identity != authorizedPlan.identity) {
            return failCommand("RHI-BINDLESS-RECORD-PLAN-MISMATCH", commandCount_);
        }
        auto valid =
            snapshot->validateForRecord(selected, generation, accesses, rhi::ShaderStage::Compute);
        if (!valid.success)
            return failCommand(valid.diagnostics, commandCount_);
        valid = snapshot->validateForRecord(
            authorizedPlan, generation, accesses, rhi::ShaderStage::Compute);
        if (!valid.success)
            return failCommand(valid.diagnostics, commandCount_);
        if (!snapshot->faultLatchBuffer() || !snapshot->faultReadbackBuffer()) {
            return failCommand("RHI-BINDLESS-FAULT-READBACK-UNAVAILABLE", commandCount_);
        }
        const auto* descriptors = snapshot->descriptorSet();
        if (!descriptors) {
            return failCommand("RHI-BINDLESS-RECORD-SNAPSHOT-INVALID", commandCount_);
        }
        const auto capacity = boundDescriptors_.validateSetCapacity(snapshot->setIndex());
        if (!capacity.success)
            return failCommand(capacity.diagnostics, commandCount_);
        valid =
            rhi::validateDescriptorSetBinding(pipelineLayout_, snapshot->setIndex(), *descriptors);
        if (!valid.success)
            return failCommand(valid.diagnostics, commandCount_);
        const auto* prepared =
            dynamic_cast<const MetalRhiPreparedComputeBinding*>(preparingBinding_.get());
        if (!prepared || !prepared->payload()) {
            return failCommand("RHI-METAL-BINDLESS-DESCRIPTOR-PAYLOAD-MISSING", commandCount_);
        }
        std::pmr::string diagnostics;
        if (!nativePayload_->validatePreparedComputeBinding(
                prepared->payload(), preparingBinding_, diagnostics))
            return failCommand(diagnostics, commandCount_);
        if (!nativePayload_->recordBindlessFaultLatchClear(
                snapshot, snapshot->faultLatchPriorState(), diagnostics)
            || !nativePayload_->recordPreparedComputeBinding(
                prepared->payload(), preparingBinding_, diagnostics)) {
            nativePayload_->poisonRecording(diagnostics);
            return failCommand(diagnostics, commandCount_);
        }
        const auto tracked =
            boundDescriptors_.bindSet(snapshot->setIndex(), *descriptors, nullptr, 0u);
        if (!tracked.success) {
            nativePayload_->poisonRecording(tracked.diagnostics);
            return failCommand(tracked.diagnostics, commandCount_);
        }
        boundSnapshot_ = std::move(snapshot);
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult setPipeline(const rhi::ComputePipeline& pipeline) override
    {
        if (nativePayload_ && nativePayload_->hasOriginalOrdinary()) {
            core::pmr::String diagnostic;
            if (!nativePayload_->recordOriginalOrdinaryPipeline(
                    commandCount_, pipeline, diagnostic))
                return failCommand(diagnostic, commandCount_);
            return commandOk(++commandCount_);
        }

        if (pipeline.nativeHandleExposedToUpperLayer()) {
            return failCommand("Metal RHI compute pipeline exposed native handle", commandCount_);
        }
        if (!nativePayload_) {
            return failCommand("RHI-METAL-COMPUTE-PIPELINE-NATIVE-PAYLOAD-MISSING", commandCount_);
        }
        if (boundSnapshot_) {
            return failCommand("RHI-BINDLESS-PIPELINE-CHANGE-AFTER-SNAPSHOT", commandCount_);
        }
        auto authorization = pipeline.bindlessAuthorization();
        if (authorization
            && (authorization->plan().originalExecutionContractVersion == 6u
                || authorization->plan().originalExecutionContractVersion == 9u))
            return failCommand(
                "RHI-BINDLESS-ORIGINAL-EXECUTION-PUBLIC-ROOT-REQUIRED", commandCount_);
        if (authorization
            && rhi::semantics::OriginalBindlessPreparationLowering::hasPlan(
                authorization->plan(), 6u))
            return failCommand("RHI-BINDLESS-ORIGINAL-PREPARATION-NOT-EXECUTABLE", commandCount_);
        if (authorization
            && (!authorization->current(authorization) || !authorization->plan().executable
                || authorization->plan().execution != rhi::BindlessExecution::Native
                || authorization->plan().deviceIdentity != bindlessDeviceIdentity_
                || !bindlessExecutable_)) {
            return failCommand("RHI-METAL-BINDLESS-PIPELINE-AUTHORIZATION-INVALID", commandCount_);
        }
        if (!authorization && !pipeline.shaderReflection().bindlessContractVersion.empty()) {
            return failCommand("RHI-BINDLESS-RECORD-AUTHORIZATION-INVALID", commandCount_);
        }
        if (nativePayload_) {
            std::pmr::string diagnostics;
            const auto pipelinePayload = metalRhiNativePayloadFor(pipeline);
            if (authorization
                && !nativePayload_->retainIndexedPipeline(authorization, diagnostics)) {
                return failCommand(diagnostics, commandCount_);
            }
            if (!pipelinePayload
                || !nativePayload_->recordComputePipelineBind(pipelinePayload, diagnostics)) {
                return failCommand(diagnostics, commandCount_);
            }
        }
        pipelineLayout_ = pipeline.layout();
        shaderReflection_ = &pipeline.shaderReflection();
        boundDescriptors_.reset();
        activeAuthorization_ = std::move(authorization);
        activePipeline_ = &pipeline;
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult bindDescriptorSet(std::uint32_t set,
        const rhi::DescriptorSet& descriptorSet,
        const std::uint32_t* dynamicOffsets,
        std::uint32_t dynamicOffsetCount) override
    {
        if (nativePayload_ && nativePayload_->hasOriginalOrdinary()) {
            core::pmr::String diagnostic;
            if (!nativePayload_->recordOriginalOrdinaryDescriptor(commandCount_,
                    set,
                    descriptorSet,
                    dynamicOffsets,
                    dynamicOffsetCount,
                    diagnostic))
                return failCommand(diagnostic, commandCount_);
            return commandOk(++commandCount_);
        }

        if (activeAuthorization_) {
            return failCommand("RHI-BINDLESS-RECORD-SNAPSHOT-REQUIRED", commandCount_);
        }
        const auto trackingCapacity = boundDescriptors_.validateSetCapacity(set);
        if (!trackingCapacity.success)
            return failCommand(trackingCapacity.diagnostics, commandCount_);
        if (descriptorSetNeedsArgumentBuffers(descriptorSet)
            && !rhi::semantics::hasExecutableNativeFeature(
                capabilityContract_, rhi::semantics::NativeBackendFeature::ArgumentBuffers)) {
            return failCommand(
                "RHI-METAL-DESCRIPTOR-ARRAY-CAPABILITY-NOT-AUTHORIZED", commandCount_);
        }
        if (descriptorSet.stats().nativeHandleExposedToUpperLayer) {
            return failCommand("Metal RHI descriptor set exposed native handle", commandCount_);
        }
        const auto offsets = rhi::validateDynamicOffsetArguments(
            descriptorSet.layout(), dynamicOffsets, dynamicOffsetCount);
        if (!offsets.success) {
            return failCommand(offsets.diagnostics, commandCount_);
        }
        if (!descriptorSetMatchesPipeline(pipelineLayout_, set, descriptorSet)) {
            return failCommand(
                "Metal RHI descriptor set does not match the bound pipeline layout", commandCount_);
        }
        if (!nativePayload_) {
            return failCommand("RHI-METAL-DESCRIPTOR-NATIVE-PAYLOAD-MISSING", commandCount_);
        }
        if (nativePayload_) {
            std::pmr::string diagnostics;
            if (!descriptorSetIsComplete(descriptorSet, diagnostics)) {
                return failCommand(diagnostics, commandCount_);
            }
            const auto nativeDescriptorSet = metalRhiNativePayloadFor(descriptorSet);
            if (!nativeDescriptorSet
                || !nativePayload_->recordDescriptorSetBind(set,
                    nativeDescriptorSet,
                    rhi::ShaderStage::Compute,
                    dynamicOffsets,
                    dynamicOffsetCount,
                    diagnostics)) {
                return failCommand(diagnostics, commandCount_);
            }
        }
        const auto tracked =
            boundDescriptors_.bindSet(set, descriptorSet, dynamicOffsets, dynamicOffsetCount);
        if (!tracked.success)
            return failCommand(tracked.diagnostics, commandCount_);
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult bindDescriptorTable(
        std::uint32_t set, const rhi::DescriptorTable& descriptorTable) override
    {
        if (nativePayload_ && nativePayload_->hasOriginalOrdinary())
            return failCommand("RHI-METAL-ORDINARY-COMMAND-PROFILE", commandCount_);

        if (activeAuthorization_) {
            return failCommand("RHI-BINDLESS-RECORD-SNAPSHOT-REQUIRED", commandCount_);
        }
        const auto trackingCapacity = boundDescriptors_.validateTableCapacity(set);
        if (!trackingCapacity.success)
            return failCommand(trackingCapacity.diagnostics, commandCount_);
        if (!rhi::semantics::hasExecutableNativeFeature(
                capabilityContract_, rhi::semantics::NativeBackendFeature::ArgumentBuffers)) {
            return failCommand(
                "RHI-METAL-DESCRIPTOR-TABLE-CAPABILITY-NOT-AUTHORIZED", commandCount_);
        }
        if (!pipelineLayout_
            || !declaresDescriptorTableSlot(pipelineLayout_, set, descriptorTable)) {
            return failCommand(
                "Metal RHI descriptor table does not match the bound pipeline layout",
                commandCount_);
        }
        if (!nativePayload_) {
            return failCommand("RHI-METAL-DESCRIPTOR-TABLE-NATIVE-PAYLOAD-MISSING", commandCount_);
        }
        if (nativePayload_) {
            std::pmr::string diagnostics;
            if (!descriptorTableIsComplete(descriptorTable, diagnostics)) {
                return failCommand(diagnostics, commandCount_);
            }
            const auto nativeTable = metalRhiNativePayloadFor(descriptorTable);
            if (!nativeTable
                || !nativePayload_->recordDescriptorTableBind(
                    set, nativeTable, rhi::ShaderStage::Compute, diagnostics)) {
                return failCommand(diagnostics.empty()
                        ? "Metal RHI descriptor table native payload is missing"
                        : diagnostics,
                    commandCount_);
            }
        }
        const auto tracked = boundDescriptors_.bindTable(set, descriptorTable);
        if (!tracked.success)
            return failCommand(tracked.diagnostics, commandCount_);
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult pushConstants(rhi::ShaderStage stages,
        std::uint32_t offsetBytes,
        std::uint32_t sizeBytes,
        const void* data) override
    {
        if (nativePayload_ && nativePayload_->hasOriginalOrdinary())
            return failCommand("RHI-METAL-ORDINARY-COMMAND-PROFILE", commandCount_);

        auto cmdResult =
            rhi::semantics::validatePushConstantCommand(stages, offsetBytes, sizeBytes, data);
        if (!cmdResult.success) {
            return failCommand(cmdResult.diagnostics, commandCount_);
        }
        auto bindResult = rhi::semantics::validatePushConstantBinding(pipelineLayout_,
            rhi::ShaderStage::Compute,
            stages,
            offsetBytes,
            sizeBytes,
            shaderReflection_);
        if (!bindResult.success) {
            return failCommand(bindResult.diagnostics, commandCount_);
        }
        if (pushState_.hasUnconsumedUpdate()) {
            return failCommand(rhi::semantics::PushConstantDiagCode::kDuplicateUnconsumed
                    + std::pmr::string(
                        "push constants already set without intervening draw/dispatch"),
                commandCount_);
        }
        rhi::semantics::CommandWorkloadDesc workload;
        workload.commandCount = 1u;
        workload.usesPushConstants = true;
        workload.usesComputeDispatch = true;
        std::pmr::string loweringDiagnostics;
        if (!metalCommandLoweringAllows(capabilityContract_,
                workload,
                rhi::semantics::CommandRecordingIntent::PrimaryCommandBuffer,
                loweringDiagnostics)) {
            return failCommand(loweringDiagnostics, commandCount_);
        }
        // P2 executable-payload gate.
        if (!nativePayload_) {
            return failCommand(rhi::semantics::PushConstantDiagCode::kNoExecPayload
                    + std::pmr::string("Metal push constants require a native command payload"),
                commandCount_);
        }
        {
            std::pmr::string diagnostics;
            if (!nativePayload_->recordPushConstants(
                    stages, offsetBytes, sizeBytes, data, diagnostics)) {
                return failCommand(diagnostics, commandCount_);
            }
        }
        pushState_.onPush();
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult dispatchIndirect(
        const rhi::GpuBuffer& indirectBuffer, std::uint64_t indirectOffset) override
    {
        if (nativePayload_ && nativePayload_->hasOriginalOrdinary())
            return failCommand("RHI-METAL-ORDINARY-COMMAND-PROFILE", commandCount_);

        if (activeAuthorization_)
            return failCommand("RHI-BINDLESS-INDIRECT-DISPATCH-UNSUPPORTED", commandCount_);
        const auto descriptorUse = boundDescriptors_.validateLiveBindings();
        if (!descriptorUse.success)
            return failCommand(descriptorUse.diagnostics, commandCount_);
        constexpr std::uint64_t kDispatchArgumentsSize = sizeof(std::uint32_t) * 3u;
        rhi::semantics::CommandWorkloadDesc workload;
        workload.commandCount = 1u;
        workload.usesComputeDispatch = true;
        workload.usesIndirectDraw = false;
        std::pmr::string loweringDiagnostics;
        if (!metalCommandLoweringAllows(capabilityContract_,
                workload,
                rhi::semantics::CommandRecordingIntent::PrimaryCommandBuffer,
                loweringDiagnostics)) {
            return failCommand(loweringDiagnostics, commandCount_);
        }
        if ((indirectOffset & 3u) != 0u
            || static_cast<std::uint32_t>(
                   indirectBuffer.desc().usage & rhi::ResourceUsage::IndirectBuffer)
                == 0u
            || indirectOffset > indirectBuffer.desc().sizeBytes
            || kDispatchArgumentsSize > indirectBuffer.desc().sizeBytes - indirectOffset) {
            return failCommand("Metal RHI indirect dispatch arguments are invalid", commandCount_);
        }
        const auto resource =
            resourceRegistry_ ? resourceRegistry_->find(indirectBuffer.id()) : std::nullopt;
        std::pmr::string diagnostics;
        if (!nativePayload_ || !resource
            || !nativePayload_->recordDispatchIndirect(
                resource->buffer, indirectOffset, diagnostics)) {
            return failCommand(diagnostics.empty()
                    ? "Metal RHI indirect dispatch native payload is missing"
                    : diagnostics,
                commandCount_);
        }
        pushState_.onConsume();
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult dispatch(
        std::uint32_t x, std::uint32_t y, std::uint32_t z) override
    {
        if (nativePayload_ && nativePayload_->hasOriginalOrdinary()) {
            core::pmr::String diagnostic;
            if (!nativePayload_->recordOriginalOrdinaryScalar(commandCount_,
                    *rhi::CommandScalarArguments::capture(rhi::CommandUsageKind::Dispatch,
                        rhi::CommandScalarArguments::Dispatch{x, y, z}),
                    diagnostic))
                return failCommand(diagnostic, commandCount_);
            return commandOk(++commandCount_);
        }

        if (activeAuthorization_ && !boundSnapshot_) {
            return failCommand("RHI-BINDLESS-RECORD-SNAPSHOT-REQUIRED", commandCount_);
        }
        const auto descriptorUse = boundDescriptors_.validateLiveBindings();
        if (!descriptorUse.success)
            return failCommand(descriptorUse.diagnostics, commandCount_);
        if (x == 0 || y == 0 || z == 0) {
            return failCommand("Metal RHI dispatch requires non-zero groups", commandCount_);
        }
        const auto lowering = rhi::semantics::validateNativeCommandLoweringUse(dispatchLowering_,
            rhi::semantics::NativeCommandSemantic::MetalComputeCommandEncoder,
            rhi::FallbackMode::Native,
            "Metal compute dispatch");
        if (!lowering.success)
            return failCommand(lowering.diagnostics, commandCount_);
        if (!nativePayload_) {
            return failCommand("RHI-METAL-DISPATCH-NATIVE-PAYLOAD-MISSING", commandCount_);
        }
        if (nativePayload_) {
            std::pmr::string diagnostics;
            if (!nativePayload_->recordDispatch(x, y, z, diagnostics)) {
                return failCommand(diagnostics, commandCount_);
            }
        }
        pushState_.onConsume();
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult writeTimestamp(
        const rhi::QueryPool& pool, std::uint32_t queryIndex) override
    {
        if (nativePayload_ && nativePayload_->hasOriginalOrdinary())
            return failCommand("RHI-METAL-ORDINARY-COMMAND-PROFILE", commandCount_);

        const auto* metalPool = asMetalRhiQueryPool(pool);
        if (!metalPool || metalPool->type() != rhi::QueryType::Timestamp || !metalPool->payload()
            || queryIndex >= metalPool->count()) {
            return failCommand("RHI-METAL-TIMESTAMP-QUERY-POOL-INVALID", commandCount_);
        }
        rhi::semantics::CommandWorkloadDesc workload;
        workload.commandCount = 1u;
        workload.usesTimestampQuery = true;
        std::pmr::string loweringDiagnostics;
        if (!metalCommandLoweringAllows(capabilityContract_,
                workload,
                rhi::semantics::CommandRecordingIntent::PrimaryCommandBuffer,
                loweringDiagnostics)) {
            return failCommand(loweringDiagnostics, commandCount_);
        }
        std::pmr::string diagnostics;
        if (!nativePayload_
            || !nativePayload_->recordTimestamp(metalPool->payload(), queryIndex, diagnostics)) {
            return failCommand(diagnostics.empty()
                    ? "RHI-METAL-TIMESTAMP-QUERY-NATIVE-PAYLOAD-MISSING"
                    : diagnostics,
                commandCount_);
        }
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult writePipelineStatistics(
        const rhi::QueryPool& pool, std::uint32_t queryIndex) override
    {
        if (nativePayload_ && nativePayload_->hasOriginalOrdinary())
            return failCommand("RHI-METAL-ORDINARY-COMMAND-PROFILE", commandCount_);

        const auto* metalPool = asMetalRhiQueryPool(pool);
        if (!metalPool || metalPool->type() != rhi::QueryType::PipelineStatistics
            || !metalPool->payload() || queryIndex >= metalPool->count()) {
            return failCommand("RHI-METAL-PIPELINE-STATISTICS-QUERY-POOL-INVALID", commandCount_);
        }
        rhi::semantics::CommandWorkloadDesc workload;
        workload.commandCount = 1u;
        workload.usesPipelineStatisticsQuery = true;
        std::pmr::string loweringDiagnostics;
        if (!metalCommandLoweringAllows(capabilityContract_,
                workload,
                rhi::semantics::CommandRecordingIntent::PrimaryCommandBuffer,
                loweringDiagnostics)) {
            return failCommand(loweringDiagnostics, commandCount_);
        }
        std::pmr::string diagnostics;
        if (!nativePayload_
            || !nativePayload_->recordPipelineStatistics(
                metalPool->payload(), queryIndex, diagnostics)) {
            return failCommand(diagnostics.empty()
                    ? "RHI-METAL-PIPELINE-STATISTICS-QUERY-NATIVE-PAYLOAD-MISSING"
                    : diagnostics,
                commandCount_);
        }
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult end() override
    {
        if (nativePayload_ && nativePayload_->hasOriginalOrdinary()) {
            core::pmr::String diagnostic;
            if (!nativePayload_->recordOriginalOrdinaryScope(
                    rhi::CommandUsageKind::EndComputePass, commandCount_, nullptr, diagnostic))
                return failCommand(diagnostic, commandCount_);
            active_ = false;
            return commandOk(++commandCount_);
        }

        if (!nativePayload_) {
            return failCommand("RHI-METAL-COMPUTE-PASS-NATIVE-PAYLOAD-MISSING", commandCount_);
        }
        if (nativePayload_) {
            std::pmr::string diagnostics;
            if (!nativePayload_->recordComputePassEnd(diagnostics)) {
                return failCommand(diagnostics, commandCount_);
            }
            if (boundSnapshot_
                && !nativePayload_->scheduleBindlessFaultReadback(boundSnapshot_, diagnostics)) {
                nativePayload_->poisonRecording(diagnostics);
                return failCommand(diagnostics, commandCount_);
            }
        }
        active_ = false;
        pushState_.onPassEnd();
        boundDescriptors_.reset();
        return commandOk(++commandCount_);
    }

private:
    rhi::CommandValidationResult failCommand(
        const core::pmr::String& diagnostic, std::uint32_t count)
    {
        if (nativePayload_ && nativePayload_->hasOriginalOrdinary())
            nativePayload_->poisonRecording(diagnostic);
        return commandFail(diagnostic, count);
    }

    rhi::BoundDescriptorSlots boundDescriptors_;
    std::uint32_t& commandCount_;
    bool& active_;
    std::shared_ptr<MetalRhiNativeCommandPayload> nativePayload_;
    rhi::semantics::NativeBackendCapabilityContract capabilityContract_;
    const rhi::PipelineLayout* pipelineLayout_ = nullptr;
    const rhi::ShaderReflectionDesc* shaderReflection_ = nullptr;
    std::shared_ptr<MetalRhiResourceRegistry> resourceRegistry_;
    rhi::semantics::PushConstantConsumptionState pushState_;
    bool bindlessExecutable_ = false;
    std::uint64_t bindlessDeviceIdentity_ = 0u;
    std::uint64_t recordingIdentity_ = 0u;
    rhi::semantics::NativeCommandLoweringDecision dispatchLowering_;
    std::shared_ptr<const rhi::BindlessAuthorization> activeAuthorization_;
    std::shared_ptr<const rhi::BindlessSnapshot> boundSnapshot_;
    const rhi::ComputePipeline* activePipeline_ = nullptr;
    std::shared_ptr<const rhi::BindlessPreparedComputeBinding> preparingBinding_;
};

class MetalRhiTransferPassEncoder final : public rhi::TransferPassEncoder {
public:
    MetalRhiTransferPassEncoder(std::uint32_t& commandCount,
        bool& active,
        std::shared_ptr<MetalRhiNativeCommandPayload> nativePayload,
        const rhi::semantics::NativeBackendCapabilityContract& capabilityContract,
        MetalRhiStagingUploadPool* stagingUploadPool,
        rhi::RecordedStagingUploadJournal* stagingUploads,
        std::shared_ptr<MetalRhiResourceRegistry> registry,
        std::uint64_t uploadRetirementSerial)
        : commandCount_(commandCount), active_(active), nativePayload_(std::move(nativePayload)),
          capabilityContract_(capabilityContract), stagingUploadPool_(stagingUploadPool),
          stagingUploads_(stagingUploads), registry_(std::move(registry)),
          currentFrameSerial_(uploadRetirementSerial)
    {}

    rhi::CommandValidationResult copyBufferToBuffer(std::uint64_t sizeBytes) override
    {
        if (nativePayload_ && nativePayload_->hasOriginalOrdinary())
            return failCommand("RHI-METAL-ORDINARY-COMMAND-PROFILE", commandCount_);

        (void)sizeBytes;
        return failCommand(
            "Metal RHI buffer copy requires explicit source and destination resources",
            commandCount_);
    }

    rhi::CommandValidationResult copyBufferToBuffer(const rhi::BufferCopyDesc& desc) override
    {
        if (nativePayload_ && nativePayload_->hasOriginalOrdinary())
            return failCommand("RHI-METAL-ORDINARY-COMMAND-PROFILE", commandCount_);

        if (desc.sizeBytes == 0 || !desc.sourceBuffer.isValid()
            || !desc.destinationBuffer.isValid()) {
            return failCommand("Metal RHI buffer copy requires resources and bytes", commandCount_);
        }
        if (const auto destination =
                registry_ ? registry_->find(desc.destinationBuffer) : std::nullopt;
            destination && destination->gpuTransferWritesSealed)
            return failCommand("RHI-BUFFER-GPU-WRITES-SEALED", commandCount_);
        std::pmr::string diagnostics;
        if (!nativePayload_ || !nativePayload_->recordBlitCopy(desc, diagnostics)) {
            return failCommand(diagnostics.empty()
                    ? "Metal RHI native buffer copy payload is missing"
                    : diagnostics,
                commandCount_);
        }
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult copyBufferToTexture(
        const rhi::BufferToTextureCopyDesc& desc) override
    {
        if (nativePayload_ && nativePayload_->hasOriginalOrdinary())
            return failCommand("RHI-METAL-ORDINARY-COMMAND-PROFILE", commandCount_);

        if (const auto* id = metalRhiTextureWriteDiagnostic(registry_, desc.destinationTexture))
            return failCommand(id, commandCount_);
        if (!desc.sourceBuffer.isValid() || !desc.destinationTexture.isValid()
            || (desc.mipLayouts.empty()
                && (desc.extent.width == 0 || desc.extent.height == 0 || desc.extent.depth == 0))) {
            return failCommand(
                "Metal RHI buffer-to-texture copy requires non-zero extent", commandCount_);
        }
        if (desc.mipLayouts.empty() && (desc.bytesPerRow == 0 || desc.rowsPerImage == 0)) {
            return failCommand(
                "Metal RHI texture upload requires explicit row and slice pitch", commandCount_);
        }
        if (!desc.mipLayouts.empty()) {
            if (desc.mipLayouts.size() != desc.textureSubresources.mipLevelCount) {
                return failCommand("Metal RHI buffer-to-texture mip layout count does not match "
                                   "the subresource range",
                    commandCount_);
            }
            for (const auto& layout : desc.mipLayouts) {
                if (layout.bytesPerRow == 0u || layout.rowsPerImage == 0u
                    || (layout.bytesPerRow % 256u) != 0u || layout.extent.width == 0u
                    || layout.extent.height == 0u || layout.extent.depth == 0u) {
                    return failCommand(
                        "Metal RHI buffer-to-texture mip layout is invalid", commandCount_);
                }
            }
        } else if ((desc.bytesPerRow % 256u) != 0u) {
            return failCommand(
                "Metal RHI texture upload row pitch must be 256-byte aligned", commandCount_);
        }
        std::pmr::string diagnostics;
        if (!nativePayload_ || !nativePayload_->recordBlitBufferToTexture(desc, diagnostics)) {
            return failCommand(diagnostics.empty()
                    ? "Metal RHI native buffer-to-texture copy payload is missing"
                    : diagnostics,
                commandCount_);
        }
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult copyTextureToBuffer(
        const rhi::TextureToBufferCopyDesc& desc) override
    {
        if (nativePayload_ && nativePayload_->hasOriginalOrdinary()) {
            core::pmr::String diagnostic;
            if (!nativePayload_->recordOriginalOrdinaryCopy(commandCount_, desc, diagnostic))
                return failCommand(diagnostic, commandCount_);
            return commandOk(++commandCount_);
        }

        if (const auto* id = metalRhiOriginalTextureAccessDiagnostic(registry_, desc.sourceTexture))
            return failCommand(id, commandCount_);
        if (!desc.sourceTexture.isValid() || !desc.destinationBuffer.isValid()
            || (desc.mipLayouts.empty()
                && (desc.extent.width == 0 || desc.extent.height == 0 || desc.extent.depth == 0))) {
            return failCommand(
                "Metal RHI texture-to-buffer copy requires resources and extent", commandCount_);
        }
        if (const auto destination =
                registry_ ? registry_->find(desc.destinationBuffer) : std::nullopt;
            destination && destination->gpuTransferWritesSealed)
            return failCommand("RHI-BUFFER-GPU-WRITES-SEALED", commandCount_);
        if (desc.mipLayouts.empty()
            && (desc.bytesPerRow == 0u || desc.rowsPerImage == 0u
                || (desc.bytesPerRow % 256u) != 0u)) {
            return failCommand(
                "Metal RHI texture-to-buffer copy requires a 256-byte aligned row layout",
                commandCount_);
        }
        if (!desc.mipLayouts.empty()) {
            if (desc.mipLayouts.size() != desc.textureSubresources.mipLevelCount) {
                return failCommand("Metal RHI texture-to-buffer mip layout count does not match "
                                   "the subresource range",
                    commandCount_);
            }
            for (const auto& layout : desc.mipLayouts) {
                if (layout.bytesPerRow == 0u || layout.rowsPerImage == 0u
                    || (layout.bytesPerRow % 256u) != 0u || layout.extent.width == 0u
                    || layout.extent.height == 0u || layout.extent.depth == 0u) {
                    return failCommand(
                        "Metal RHI texture-to-buffer mip layout is invalid", commandCount_);
                }
            }
        }
        std::pmr::string diagnostics;
        if (!nativePayload_ || !nativePayload_->recordBlitTextureToBuffer(desc, diagnostics)) {
            return failCommand(diagnostics.empty()
                    ? "Metal RHI native texture-to-buffer copy payload is missing"
                    : diagnostics,
                commandCount_);
        }
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult copyTextureToTexture(
        const rhi::TextureToTextureCopyDesc& desc) override
    {
        if (nativePayload_ && nativePayload_->hasOriginalOrdinary())
            return failCommand("RHI-METAL-ORDINARY-COMMAND-PROFILE", commandCount_);

        if (const auto* id = metalRhiOriginalTextureAccessDiagnostic(registry_, desc.sourceTexture))
            return failCommand(id, commandCount_);
        if (const auto* id = metalRhiTextureWriteDiagnostic(registry_, desc.destinationTexture))
            return failCommand(id, commandCount_);
        if (!desc.sourceTexture.isValid() || !desc.destinationTexture.isValid()) {
            return failCommand("Metal RHI texture copy requires resources", commandCount_);
        }
        if ((desc.mipLayouts.empty()
                && (desc.extent.width == 0 || desc.extent.height == 0 || desc.extent.depth == 0))
            || (!desc.mipLayouts.empty()
                && desc.mipLayouts.size() != desc.sourceSubresources.mipLevelCount)) {
            return failCommand("Metal RHI texture copy requires non-zero extent", commandCount_);
        }
        if (!nativePayload_) {
            return failCommand("RHI-METAL-TEXTURE-COPY-NATIVE-PAYLOAD-MISSING", commandCount_);
        }
        if (nativePayload_) {
            std::pmr::string diagnostics;
            if (!nativePayload_->recordBlitTextureCopy(desc, diagnostics)) {
                return failCommand(diagnostics, commandCount_);
            }
        }
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult uploadBuffer(const rhi::UploadBufferDesc& desc) override
    {
        if (nativePayload_ && nativePayload_->hasOriginalOrdinary())
            return failCommand("RHI-METAL-ORDINARY-COMMAND-PROFILE", commandCount_);

        if (!desc.backendOwnedStaging || desc.stagingIntent != rhi::StagingIntent::Upload
            || desc.sizeBytes == 0 || !desc.sourceUploadBuffer.isValid()
            || !desc.destinationResource.isValid()) {
            return failCommand("Metal RHI upload requires backend-owned staging, source buffer, "
                               "destination buffer, and bytes",
                commandCount_);
        }
        if (const auto destination =
                registry_ ? registry_->find(desc.destinationResource) : std::nullopt;
            destination && destination->gpuTransferWritesSealed)
            return failCommand("RHI-BUFFER-GPU-WRITES-SEALED", commandCount_);
        if (!nativePayload_) {
            return failCommand("RHI-METAL-UPLOAD-BUFFER-NATIVE-PAYLOAD-MISSING", commandCount_);
        }
        if (nativePayload_) {
            std::pmr::string diagnostics;
            const rhi::BufferCopyDesc copy{
                desc.sourceUploadBuffer,
                desc.destinationResource,
                0,
                0,
                desc.sizeBytes,
            };
            if (!nativePayload_->recordBlitCopy(copy, diagnostics)) {
                return failCommand(diagnostics, commandCount_);
            }
        }
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult uploadTexture(const rhi::UploadTextureDesc& desc) override
    {
        if (nativePayload_ && nativePayload_->hasOriginalOrdinary())
            return failCommand("RHI-METAL-ORDINARY-COMMAND-PROFILE", commandCount_);

        if (const auto* id = metalRhiTextureWriteDiagnostic(registry_, desc.destinationTexture))
            return failCommand(id, commandCount_);
        if (!desc.backendOwnedStaging || desc.stagingIntent != rhi::StagingIntent::Upload
            || desc.pixelDataSize == 0 || desc.pixelData == nullptr) {
            return failCommand(
                "Metal RHI texture upload requires backend-owned staging and valid pixel data",
                commandCount_);
        }
        if (!stagingUploadPool_) {
            return failCommand(
                "Metal RHI texture upload requires a backend-owned staging pool", commandCount_);
        }
        if (!nativePayload_) {
            return failCommand("RHI-METAL-UPLOAD-TEXTURE-NATIVE-PAYLOAD-MISSING", commandCount_);
        }
        if (desc.format == rhi::TextureFormat::Unknown
            || (desc.mipLayouts.empty()
                && (desc.bytesPerRow == 0 || desc.rowsPerImage == 0 || desc.extent.width == 0
                    || desc.extent.height == 0 || desc.extent.depth == 0))
            || !desc.destinationTexture.isValid()) {
            return failCommand(
                "Metal RHI texture upload requires explicit destination, extent, format, and pitch",
                commandCount_);
        }
        const auto destination =
            registry_ ? registry_->find(desc.destinationTexture) : std::nullopt;
        if (!destination || destination->kind != rhi::DescriptorResourceKind::TextureView
            || !hasTextureUsage(destination->textureUsage, rhi::TextureUsage::CopyDst)
            || destination->textureFormat != desc.format) {
            return failCommand(
                "RHI-METAL-TEXTURE-UPLOAD-DESTINATION-FORMAT-OR-USAGE-MISMATCH", commandCount_);
        }
        auto effectiveDesc = desc;
        if (desc.mipLevel != 0u) {
            if (desc.subresources.baseMipLevel != 0u
                && desc.subresources.baseMipLevel != desc.mipLevel) {
                return failCommand("RHI-METAL-TEXTURE-UPLOAD-MIP-IDENTITY-MISMATCH", commandCount_);
            }
            effectiveDesc.subresources.baseMipLevel = desc.mipLevel;
        }
        if (desc.arrayLayer != 0u) {
            if (desc.subresources.baseArrayLayer != 0u
                && desc.subresources.baseArrayLayer != desc.arrayLayer) {
                return failCommand(
                    "RHI-METAL-TEXTURE-UPLOAD-LAYER-IDENTITY-MISMATCH", commandCount_);
            }
            effectiveDesc.subresources.baseArrayLayer = desc.arrayLayer;
        }
        if (effectiveDesc.subresources.mipLevelCount == 0u
            || effectiveDesc.subresources.arrayLayerCount == 0u
            || (effectiveDesc.mipLayouts.empty() ? effectiveDesc.subresources.mipLevelCount != 1u
                                                 : effectiveDesc.mipLayouts.size()
                        != effectiveDesc.subresources.mipLevelCount)) {
            return failCommand("RHI-METAL-TEXTURE-UPLOAD-MIP-LAYOUT-INVALID", commandCount_);
        }
        const auto frameSerial = currentFrameSerial_;
        auto staging =
            stagingUploadPool_->allocate({desc.label, desc.pixelDataSize, 256, frameSerial});
        if (!staging.success) {
            return commandStagingFail(staging.diagnostics, commandCount_, staging.diagnosticId);
        }
        if (!stagingUploads_) {
            if (!stagingUploadPool_->rollback(staging)) {
                return failCommand("RHI-METAL-STAGING-UPLOAD-ROLLBACK-FAILED", commandCount_);
            }
            return failCommand("RHI-STAGING-RECORDING-JOURNAL-MISSING", commandCount_);
        }
        if (!stagingUploadPool_->enlist(*stagingUploads_,
                {
                    staging.pageIndex,
                    staging.offsetBytes,
                    staging.sizeBytes,
                    staging.frameSerial,
                })) {
            (void)stagingUploadPool_->rollback(staging);
            return failCommand("RHI-METAL-STAGING-RESERVATION-IDENTITY-INVALID", commandCount_);
        }
        const auto failStaging = [&](const std::pmr::string& diagnostics) {
            if (!stagingUploads_->rollbackLast()) {
                return failCommand("RHI-METAL-STAGING-UPLOAD-ROLLBACK-FAILED", commandCount_);
            }
            return failCommand(diagnostics, commandCount_);
        };
        if (!staging.mappedData) {
            return failStaging("Metal RHI texture upload staging page is not CPU-mappable");
        }
        std::memcpy(staging.mappedData, desc.pixelData, desc.pixelDataSize);
        {
            std::pmr::string diagnostics;
            if (!prepareMetalRhiNativeStagingBufferForGpu(
                    staging.payload, staging.offsetBytes, desc.pixelDataSize, diagnostics)) {
                return failStaging(diagnostics);
            }
        }
        if (nativePayload_) {
            std::pmr::string diagnostics;
            if (!nativePayload_->recordUploadStagingTextureCopy(
                    staging.payload, staging.offsetBytes, effectiveDesc, diagnostics)) {
                return failStaging(diagnostics);
            }
            // A readiness credential belongs only to a staging copy that was
            // recorded successfully. A later page-capacity failure abandons
            // this encoder, so marking it at encoder construction would make
            // an unsubmitted reservation look like a completion dependency.
            nativePayload_->markStagingUpload(stagingUploadPool_->completedSubmissionEpoch());
        }
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult readbackTexture(const rhi::ReadbackTextureDesc& desc) override
    {
        if (nativePayload_ && nativePayload_->hasOriginalOrdinary())
            return failCommand("RHI-METAL-ORDINARY-COMMAND-PROFILE", commandCount_);

        if (!desc.backendOwnedStaging || desc.stagingIntent != rhi::StagingIntent::Readback
            || desc.sizeBytes == 0 || !desc.sourceResource.isValid()
            || !desc.destinationReadbackBuffer.isValid()) {
            return failCommand(
                "Metal RHI readback requires backend-owned staging, resources, and bytes",
                commandCount_);
        }
        if (!nativePayload_) {
            return failCommand(
                "Metal RHI readback requires a native command payload", commandCount_);
        }
        const auto source = registry_ ? registry_->find(desc.sourceResource) : std::nullopt;
        const auto destination =
            registry_ ? registry_->find(desc.destinationReadbackBuffer) : std::nullopt;
        if (!source || !destination || !destination->buffer || source->textureSampleCount != 1u
            || (rhi::bytesPerPixel(source->textureFormat) == 0
                && !rhi::isBlockCompressed(source->textureFormat))
            || !hasResourceUsage(destination->bufferUsage, rhi::ResourceUsage::CopyDst)
            || (destination->buffer->resource.storageMode != MetalRhiNativeStorageMode::Shared
                && destination->buffer->resource.storageMode
                    != MetalRhiNativeStorageMode::Managed)) {
            return failCommand("Metal RHI readback requires a tracked single-sample texture and "
                               "CPU-readable destination buffer",
                commandCount_);
        }
        const bool legacyWholeResource = desc.extent.width == 0u && desc.extent.height == 0u
            && desc.extent.depth == 0u && desc.mipLayouts.empty();
        if (!legacyWholeResource
            && (desc.extent.width == 0u || desc.extent.height == 0u || desc.extent.depth == 0u)) {
            return failCommand(
                "Metal RHI readback extent must be complete when explicit layout is used",
                commandCount_);
        }
        auto sourceRange = desc.sourceSubresources;
        sourceRange.aspect = rhi::isDepthTextureFormat(source->textureFormat)
            ? rhi::TextureAspect::Depth
            : rhi::TextureAspect::Color;
        if (legacyWholeResource) {
            sourceRange.baseMipLevel = 0u;
            sourceRange.mipLevelCount = 1u;
            sourceRange.baseArrayLayer = 0u;
            sourceRange.arrayLayerCount = 1u;
        }
        if (sourceRange.mipLevelCount == 0u || sourceRange.arrayLayerCount == 0u
            || sourceRange.baseMipLevel >= source->textureMipLevelCount
            || sourceRange.mipLevelCount > source->textureMipLevelCount - sourceRange.baseMipLevel
            || sourceRange.baseArrayLayer >= source->textureArrayLayerCount
            || sourceRange.arrayLayerCount
                > source->textureArrayLayerCount - sourceRange.baseArrayLayer
            || (!desc.mipLayouts.empty() && desc.mipLayouts.size() != sourceRange.mipLevelCount)
            || (desc.mipLayouts.empty() && sourceRange.mipLevelCount != 1u)) {
            return failCommand(
                "Metal RHI readback subresource range or mip layout is invalid", commandCount_);
        }
        const auto extentAt = [&](std::uint32_t mipIndex) {
            const auto shift = [mipIndex](std::uint32_t value) {
                const auto shifted = value >> mipIndex;
                return shifted == 0u ? 1u : shifted;
            };
            if (mipIndex >= 32u) {
                return rhi::Extent3D{1u, 1u, 1u};
            }
            return rhi::Extent3D{
                shift(source->textureExtent.width),
                shift(source->textureExtent.height),
                source->textureExtent.depth > 1u ? shift(source->textureExtent.depth) : 1u,
            };
        };
        const auto rowBytesFor = [&](const rhi::Extent3D& extent) {
            if (rhi::isBlockCompressed(source->textureFormat)) {
                return static_cast<std::uint64_t>(rhi::blocksForExtent(
                           extent.width, rhi::blockExtent(source->textureFormat)))
                    * rhi::bytesPerBlock(source->textureFormat);
            }
            return static_cast<std::uint64_t>(extent.width)
                * rhi::bytesPerPixel(source->textureFormat);
        };
        const auto rowCountFor = [&](const rhi::Extent3D& extent) {
            return rhi::isBlockCompressed(source->textureFormat)
                ? rhi::blocksForExtent(extent.height, rhi::blockExtent(source->textureFormat))
                : extent.height;
        };
        core::pmr::Vector<rhi::TextureToBufferCopyDesc> readbackCopies;
        readbackCopies.reserve(sourceRange.mipLevelCount);
        for (std::uint32_t mipIndex = 0u; mipIndex < sourceRange.mipLevelCount; ++mipIndex) {
            rhi::Extent3D extent = legacyWholeResource ? extentAt(0u) : desc.extent;
            std::uint32_t bytesPerRow = desc.bytesPerRow;
            std::uint32_t rowsPerImage = desc.rowsPerImage;
            std::uint64_t relativeOffset = 0u;
            std::uint32_t mipLevel = sourceRange.baseMipLevel;
            if (!desc.mipLayouts.empty()) {
                const auto& layout = desc.mipLayouts[mipIndex];
                mipLevel = layout.mipLevel;
                extent = layout.extent;
                bytesPerRow = layout.bytesPerRow;
                rowsPerImage = layout.rowsPerImage;
                relativeOffset = layout.bufferOffsetBytes;
                if (mipIndex > std::numeric_limits<std::uint32_t>::max() - sourceRange.baseMipLevel
                    || mipLevel != sourceRange.baseMipLevel + mipIndex) {
                    return failCommand(
                        "Metal RHI readback mip layouts are not in ascending order", commandCount_);
                }
            } else if (!legacyWholeResource && sourceRange.baseMipLevel != 0u) {
                extent = extentAt(sourceRange.baseMipLevel);
            }
            const auto requiredRowBytes = rowBytesFor(extent);
            const auto requiredRows = rowCountFor(extent);
            if (bytesPerRow == 0u) {
                std::uint64_t aligned = 0u;
                if (!checkedMetalAdd(requiredRowBytes, 255u, aligned)) {
                    return failCommand("Metal RHI readback row pitch overflows", commandCount_);
                }
                const auto alignedRowBytes = aligned & ~255ull;
                if (alignedRowBytes > std::numeric_limits<std::uint32_t>::max()) {
                    return failCommand(
                        "Metal RHI readback row pitch exceeds the RHI limit", commandCount_);
                }
                bytesPerRow = static_cast<std::uint32_t>(alignedRowBytes);
            }
            if (rowsPerImage == 0u) {
                rowsPerImage = requiredRows;
            }
            if (bytesPerRow < requiredRowBytes || rowsPerImage < requiredRows
                || (bytesPerRow % 256u) != 0u
                || bytesPerRow > std::numeric_limits<std::uint32_t>::max()) {
                return failCommand("Metal RHI readback row layout is invalid", commandCount_);
            }
            std::uint64_t bytesPerImage = 0u;
            std::uint64_t lastSlice = 0u;
            std::uint64_t requiredOneLayer = 0u;
            std::uint64_t requiredAllLayers = 0u;
            if (!checkedMetalMultiply(bytesPerRow, rowsPerImage, bytesPerImage)
                || !checkedMetalMultiply(bytesPerImage, extent.depth - 1u, lastSlice)
                || !checkedMetalMultiply(bytesPerRow, requiredRows, requiredOneLayer)
                || !checkedMetalAdd(lastSlice, requiredOneLayer, requiredOneLayer)
                || !checkedMetalMultiply(
                    bytesPerImage, sourceRange.arrayLayerCount - 1u, requiredAllLayers)
                || !checkedMetalAdd(requiredAllLayers, requiredOneLayer, requiredAllLayers)
                || requiredAllLayers == 0u) {
                return failCommand(
                    "Metal RHI readback destination is smaller than the packed texture copy",
                    commandCount_);
            }
            std::uint64_t destinationOffset = 0u;
            std::uint64_t destinationEnd = 0u;
            if (!checkedMetalAdd(desc.destinationOffsetBytes, relativeOffset, destinationOffset)
                || !checkedMetalAdd(destinationOffset, requiredAllLayers, destinationEnd)
                || destinationEnd > desc.sizeBytes) {
                return failCommand(
                    "Metal RHI readback destination is smaller than the packed texture copy",
                    commandCount_);
            }
            rhi::TextureToBufferCopyDesc copy;
            copy.sourceTexture = desc.sourceResource;
            copy.destinationBuffer = desc.destinationReadbackBuffer;
            copy.textureSubresources = sourceRange;
            copy.textureSubresources.baseMipLevel = mipLevel;
            copy.textureSubresources.mipLevelCount = 1u;
            copy.bufferOffsetBytes = destinationOffset;
            copy.bytesPerRow = bytesPerRow;
            copy.rowsPerImage = rowsPerImage;
            copy.extent = extent;
            readbackCopies.push_back(std::move(copy));
        }
        for (const auto& copy : readbackCopies) {
            std::pmr::string diagnostics;
            if (!nativePayload_->preflightBlitTextureToBuffer(copy, diagnostics)) {
                return failCommand(diagnostics, commandCount_);
            }
        }
        for (const auto& copy : readbackCopies) {
            std::pmr::string diagnostics;
            if (!nativePayload_->recordBlitTextureToBuffer(copy, diagnostics)) {
                return failCommand(diagnostics, commandCount_);
            }
        }
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult end() override
    {
        if (nativePayload_ && nativePayload_->hasOriginalOrdinary()) {
            core::pmr::String diagnostic;
            if (!nativePayload_->recordOriginalOrdinaryScope(
                    rhi::CommandUsageKind::EndTransferPass, commandCount_, nullptr, diagnostic))
                return failCommand(diagnostic, commandCount_);
            active_ = false;
            return commandOk(++commandCount_);
        }

        if (!nativePayload_) {
            return failCommand("RHI-METAL-BLIT-PASS-NATIVE-PAYLOAD-MISSING", commandCount_);
        }
        if (nativePayload_) {
            std::pmr::string diagnostics;
            if (!nativePayload_->recordBlitPassEnd(diagnostics)) {
                return failCommand(diagnostics, commandCount_);
            }
        }
        active_ = false;
        return commandOk(++commandCount_);
    }

private:
    rhi::CommandValidationResult failCommand(
        const core::pmr::String& diagnostic, std::uint32_t count)
    {
        if (nativePayload_ && nativePayload_->hasOriginalOrdinary())
            nativePayload_->poisonRecording(diagnostic);
        return commandFail(diagnostic, count);
    }

    std::uint32_t& commandCount_;
    bool& active_;
    std::shared_ptr<MetalRhiNativeCommandPayload> nativePayload_;
    rhi::semantics::NativeBackendCapabilityContract capabilityContract_;
    MetalRhiStagingUploadPool* stagingUploadPool_ = nullptr;
    rhi::RecordedStagingUploadJournal* stagingUploads_ = nullptr;
    std::shared_ptr<MetalRhiResourceRegistry> registry_;
    std::uint64_t currentFrameSerial_ = 1;
};

class MetalRhiCommandEncoder final : public rhi::CommandEncoderBase {
public:
    MetalRhiCommandEncoder(rhi::CommandEncoderDesc desc,
        const rhi::semantics::NativeBackendCapabilityContract& capabilityContract,
        MetalRhiStagingUploadPool* stagingUploadPool,
        std::shared_ptr<MetalRhiResourceRegistry> registry,
        const rhi::BindlessCapabilities* bindlessCapabilities,
        const rhi::BindlessCrossExtension* bindlessIssuer)
        : nativePayload_(createMetalRhiNativeCommandPayload(desc.label,
              registry,
              nullptr,
              desc.maxBindlessPipelines,
              desc.maxBindlessSnapshots,
              desc.maxBindlessGraphicsAccesses)),
          capabilityContract_(capabilityContract), stagingUploadPool_(stagingUploadPool),
          registry_(std::move(registry)),
          bindlessExecutable_(bindlessCapabilities
              && bindlessCapabilities->contractVersion == rhi::kBindlessContractVersion
              && bindlessCapabilities->execution == rhi::BindlessExecution::Native
              && bindlessCapabilities->limitsQueried && bindlessCapabilities->executablePayload
              && bindlessCapabilities->faultReadbackContractVersion == 1u
              && bindlessCapabilities->preparedComputeBindingContractVersion == 1u
              && desc.maxBindlessPipelines != 0u && desc.maxBindlessSnapshots != 0u),
          bindlessDeviceIdentity_(bindlessCapabilities ? bindlessCapabilities->deviceIdentity : 0u),
          graphicsExecutable_(bindlessCapabilities && bindlessIssuer
              && metalBindlessGraphicsExecutionAvailable(*bindlessCapabilities)
              && metalBindlessGraphicsExecutionAvailable(bindlessIssuer->capabilities())
              && bindlessCapabilities->deviceIdentity
                  == bindlessIssuer->capabilities().deviceIdentity
              && desc.maxBindlessPipelines && desc.maxBindlessSnapshots
              && desc.maxBindlessGraphicsAccesses),
          bindlessIssuer_(bindlessIssuer), recordingIdentity_(rhi::allocateGpuResourceId().value),
          stagingUploads_(arp::core::makeUnique<rhi::RecordedStagingUploadJournal>()),
          uploadRetirementSerial_(stagingUploadPool ? stagingUploadPool->beginSubmission() : 0u)
    {}

    bool prepareOriginalOrdinary(std::shared_ptr<const rhi::OriginalCommandStreamProgram> program,
        const rhi::GpuDevice& device,
        const rhi::OriginalRenderPassPreparationRequest& request,
        core::pmr::String& diagnostic)
    {
        if (!nativePayload_) {
            diagnostic = "RHI-METAL-ORDINARY-NATIVE-PAYLOAD";
            return false;
        }
        finishedCommand_ =
            core::makeUnique<MetalRhiCommandBuffer>(MetalRhiCommandBuffer::ColdStorage{});
        if (const auto* id = stagingUploads_->prepareBounded(0u)) {
            diagnostic = id;
            return false;
        }
        std::size_t compute = 0u, transfer = 0u;
        for (const auto& operation : program->operations()) {
            compute += operation.kind == rhi::CommandUsageKind::BeginComputePass;
            transfer += operation.kind == rhi::CommandUsageKind::BeginTransferPass;
        }
        coldComputePasses_.reserve(compute);
        coldTransferPasses_.reserve(transfer);
        coldRenderPasses_.reserve(program->counts().renderPasses);
        for (std::size_t i = 0u; i < compute; ++i)
            coldComputePasses_.push_back(core::makeUnique<MetalRhiComputePassEncoder>(commandCount_,
                activePass_,
                nativePayload_,
                capabilityContract_,
                registry_,
                false,
                0u,
                rhi::semantics::NativeCommandLoweringDecision{}));
        for (std::size_t i = 0u; i < transfer; ++i)
            coldTransferPasses_.push_back(
                core::makeUnique<MetalRhiTransferPassEncoder>(commandCount_,
                    activePass_,
                    nativePayload_,
                    capabilityContract_,
                    nullptr,
                    stagingUploads_.get(),
                    registry_,
                    0u));
        for (const auto& operation : program->operations()) {
            if (operation.kind != rhi::CommandUsageKind::BeginRenderPass)
                continue;
            const auto& args = std::get<std::shared_ptr<const rhi::RenderPassArguments>>(
                *program->originalInputs()->descriptorAt(operation.commandIndex));
            const auto& desc = args->desc();
            const auto first = desc.colorAttachments.empty() ? desc.depthStencilAttachment.target
                                                             : desc.colorAttachments.front().target;
            const auto resource = registry_ ? registry_->find(first) : std::nullopt;
            if (!resource || !resource->textureSampleCount) {
                diagnostic = "RHI-METAL-ORDINARY-PASS-SAMPLE-COUNT";
                return false;
            }
            coldRenderPasses_.push_back(core::makeUnique<MetalRhiRenderPassEncoder>(commandCount_,
                activePass_,
                nativePayload_,
                capabilityContract_,
                registry_,
                1u,
                core::pmr::Vector<rhi::SubpassDesc>{&core::defaultCpuMemoryResource()},
                desc.colorAttachments,
                desc.hasDepthStencilAttachment ? desc.depthStencilAttachment.format
                                               : rhi::TextureFormat::Unknown,
                resource->textureSampleCount,
                false,
                recordingIdentity_,
                std::shared_ptr<const rhi::BindlessPreparedGraphicsBinding>{},
                rhi::semantics::NativeCommandLoweringDecision{}));
        }
        return nativePayload_->prepareOriginalOrdinary(
            std::move(program), device, request, recordedStates_, diagnostic);
    }

    ~MetalRhiCommandEncoder() override
    {
        if (nativePayload_ && !submissionOwnershipTransferred_) {
            nativePayload_->abandonUnsubmitted();
            nativePayload_.reset();
        }
    }

    std::uint64_t recordingIdentity() const noexcept override { return recordingIdentity_; }

    rhi::CommandValidationResult prepareBindlessGraphicsPass(
        std::shared_ptr<const rhi::BindlessPreparedGraphicsBinding> binding,
        const rhi::BindlessAccessDeclarations& accesses) override
    {
        if (nativePayload_ && nativePayload_->hasOriginalOrdinary()) {
            markCommandFail("RHI-METAL-ORDINARY-COMMAND-PROFILE");
            return lastValidation_;
        }

        const auto fail = [&](std::string_view diagnostic) {
            markCommandFail(core::pmr::String(diagnostic));
            return lastValidation_;
        };
        if (!graphicsExecutable_)
            return fail("RHI-BINDLESS-GRAPHICS-PASS-SCOPE-UNAVAILABLE");
        if (!recordedStates_ || hasActivePass() || pendingGraphics_)
            return fail("RHI-BINDLESS-GRAPHICS-PASS-SCOPE-RECORDING-INVALID");
        if (!binding || !binding->sharedOwnerMatches(binding)
            || !metalRhiPreparedGraphicsPayloadFor(*binding) || !binding->owners())
            return fail("RHI-BINDLESS-PREPARED-GRAPHICS-PAYLOAD-MISMATCH");
        const auto& desc = binding->owners()->desc();
        const auto valid = binding->validateForRecord(*desc.pipeline, accesses);
        if (!valid.success)
            return fail(valid.diagnostics);
        const auto authorization = binding->authorization();
        if (!authorization || !authorization->issuedBy(bindlessIssuer_, authorization)
            || authorization->plan().deviceIdentity != bindlessDeviceIdentity_)
            return fail("RHI-BINDLESS-RECORD-AUTHORIZATION-INVALID");
        core::pmr::String diagnostic;
        if (!nativePayload_
            || !nativePayload_->prepareGraphicsPassScope(binding, accesses, diagnostic))
            return fail(
                diagnostic.empty() ? "RHI-METAL-RENDER-PASS-NATIVE-PAYLOAD-MISSING" : diagnostic);
        pendingGraphics_ = std::move(binding);
        markCommandOk();
        return lastValidation_;
    }

    std::shared_ptr<MetalRhiNativeCommandPayload> nativePayload() const { return nativePayload_; }

    rhi::ResourceBarrierResult transition(
        rhi::GpuBuffer& buffer, const rhi::ResourceBarrierDesc& desc) override
    {
        if (nativePayload_ && nativePayload_->hasOriginalOrdinary()) {
            core::pmr::String diagnostic;
            if (!nativePayload_->recordOriginalOrdinaryTransition(
                    commandCount_, buffer, desc, diagnostic)) {
                markCommandFail(diagnostic);
                return barrierFailure(buffer.id(), desc, false, diagnostic);
            }
            markCommandOk();
            auto result = barrierFailure(buffer.id(), desc, false, {});
            result.success = true;
            return result;
        }

        if (const auto* id =
                rhi::semantics::GeometryInputAccessLowering::geometryInputBarrierDiagnostic(
                    rhi::Backend::Metal, desc)) {
            markCommandFail(id);
            return {false, lastValidation_.diagnostics};
        }
        if (pendingGraphics_) {
            markCommandFail("RHI-METAL-GRAPHICS-SCOPE-NOT-CONSUMED");
            return barrierFailure(buffer.id(), desc, false, lastValidation_.diagnostics);
        }
        if (!recordedStates_) {
            markCommandFail("RHI-RESOURCE-STATE-RECORDING-FINISHED");
            return {false, lastValidation_.diagnostics};
        }
        std::pmr::string diagnostics;
        if (!validMetalBarrierDesc(buffer.id(), desc, nullptr, diagnostics)) {
            markCommandFail(diagnostics);
            return barrierFailure(buffer.id(), desc, false, diagnostics);
        }
        if (buffer.state() != desc.before) {
            diagnostics = "RHI-METAL-BARRIER-BEFORE-STATE-MISMATCH";
            markCommandFail(diagnostics);
            return barrierFailure(buffer.id(), desc, false, diagnostics);
        }
        if (!nativePayload_) {
            const auto diagnostics =
                std::pmr::string("RHI-METAL-BUFFER-BARRIER-NATIVE-PAYLOAD-MISSING");
            markCommandFail(diagnostics);
            return barrierFailure(desc.resource, desc, false, diagnostics);
        }
        rhi::semantics::CommandWorkloadDesc workload;
        workload.commandCount = 1u;
        workload.usesBarriers = true;
        workload.usesStageAccessBarriers = true;
        if (!metalCommandLoweringAllows(capabilityContract_,
                workload,
                rhi::semantics::CommandRecordingIntent::PrimaryCommandBuffer,
                diagnostics)) {
            markCommandFail(diagnostics);
            return barrierFailure(desc.resource, desc, false, diagnostics);
        }
        if (!recordedStates_ || !prepareMetalRhiRecordedState(buffer, *recordedStates_)) {
            diagnostics = "RHI-RESOURCE-STATE-RECORDING-CONFLICT";
            markCommandFail(diagnostics);
            return barrierFailure(desc.resource, desc, false, diagnostics);
        }
        if (nativePayload_) {
            std::pmr::string diagnostics;
            if (!nativePayload_->recordBufferBarrier(diagnostics)) {
                markCommandFail(diagnostics);
                return barrierFailure(desc.resource, desc, false, diagnostics);
            }
        }
        auto result = setMetalRhiBufferStateForBarrier(buffer, desc, recordedStates_.get());
        if (!result.success) {
            markCommandFail(result.diagnostics);
            return result;
        }
        markCommandOk();
        return result;
    }

    rhi::ResourceBarrierResult transition(
        rhi::GpuTexture& texture, const rhi::ResourceBarrierDesc& desc) override
    {
        if (nativePayload_ && nativePayload_->hasOriginalOrdinary()) {
            core::pmr::String diagnostic;
            if (!nativePayload_->recordOriginalOrdinaryTransition(
                    commandCount_, texture, desc, diagnostic)) {
                markCommandFail(diagnostic);
                return barrierFailure(texture.id(), desc, true, diagnostic);
            }
            markCommandOk();
            auto result = barrierFailure(texture.id(), desc, true, {});
            result.success = true;
            return result;
        }

        if (texture.originalContentExclusive()) {
            markCommandFail("RHI-TEXTURE-CONTENT-EXCLUSIVE-USE");
            return barrierFailure(texture.id(), desc, true, lastValidation_.diagnostics);
        }
        if (const auto* id =
                rhi::semantics::GeometryInputAccessLowering::geometryInputBarrierDiagnostic(
                    rhi::Backend::Metal, desc)) {
            markCommandFail(id);
            return {false, lastValidation_.diagnostics};
        }
        if (pendingGraphics_) {
            markCommandFail("RHI-METAL-GRAPHICS-SCOPE-NOT-CONSUMED");
            return barrierFailure(texture.id(), desc, true, lastValidation_.diagnostics);
        }
        if (!recordedStates_) {
            markCommandFail("RHI-RESOURCE-STATE-RECORDING-FINISHED");
            return {false, lastValidation_.diagnostics};
        }
        std::pmr::string diagnostics;
        if (!validMetalBarrierDesc(texture.id(), desc, &texture.desc(), diagnostics)) {
            markCommandFail(diagnostics);
            return barrierFailure(texture.id(), desc, true, diagnostics);
        }
        if (texture.subresourceState(desc.textureSubresources) != desc.before) {
            diagnostics = "RHI-METAL-BARRIER-BEFORE-SUBRESOURCE-STATE-MISMATCH";
            markCommandFail(diagnostics);
            return barrierFailure(texture.id(), desc, true, diagnostics);
        }
        if (!nativePayload_) {
            const auto diagnostics =
                std::pmr::string("RHI-METAL-TEXTURE-BARRIER-NATIVE-PAYLOAD-MISSING");
            markCommandFail(diagnostics);
            return barrierFailure(desc.resource, desc, true, diagnostics);
        }
        rhi::semantics::CommandWorkloadDesc workload;
        workload.commandCount = 1u;
        workload.usesBarriers = true;
        workload.usesSubresourceBarriers = true;
        workload.usesStageAccessBarriers = true;
        if (!metalCommandLoweringAllows(capabilityContract_,
                workload,
                rhi::semantics::CommandRecordingIntent::PrimaryCommandBuffer,
                diagnostics)) {
            markCommandFail(diagnostics);
            return barrierFailure(desc.resource, desc, true, diagnostics);
        }
        if (!recordedStates_ || !prepareMetalRhiRecordedState(texture, *recordedStates_)) {
            diagnostics = "RHI-RESOURCE-STATE-RECORDING-CONFLICT";
            markCommandFail(diagnostics);
            return barrierFailure(desc.resource, desc, true, diagnostics);
        }
        if (nativePayload_) {
            std::pmr::string diagnostics;
            if (!nativePayload_->recordTextureBarrier(diagnostics)) {
                markCommandFail(diagnostics);
                return barrierFailure(desc.resource, desc, true, diagnostics);
            }
        }
        auto result = setMetalRhiTextureStateForBarrier(texture, desc, recordedStates_.get());
        if (!result.success) {
            markCommandFail(result.diagnostics);
            return result;
        }
        markCommandOk();
        return result;
    }

    rhi::ResourceBarrierResult memoryBarrier(
        rhi::GpuBuffer& buffer, const rhi::ResourceMemoryBarrierDesc& desc) override
    {
        if (nativePayload_ && nativePayload_->hasOriginalOrdinary()) {
            markCommandFail("RHI-METAL-ORDINARY-COMMAND-PROFILE");
            return {false, lastValidation_.diagnostics};
        }

        if (const auto* id =
                rhi::semantics::GeometryInputAccessLowering::geometryInputBarrierDiagnostic(
                    rhi::Backend::Metal, desc)) {
            markCommandFail(id);
            return {false, lastValidation_.diagnostics};
        }
        if (pendingGraphics_) {
            markCommandFail("RHI-METAL-GRAPHICS-SCOPE-NOT-CONSUMED");
            return {false, lastValidation_.diagnostics};
        }
        rhi::ResourceBarrierResult result;
        result.resource = buffer.id();
        result.before = buffer.state();
        result.after = buffer.state();
        result.stageAccessAware = true;
        result.sourceStages = desc.sourceStages;
        result.destinationStages = desc.destinationStages;
        result.sourceAccess = desc.sourceAccess;
        result.destinationAccess = desc.destinationAccess;
        if (!recordedStates_) {
            result.diagnostics = "RHI-RESOURCE-MEMORY-BARRIER-RECORDING-FINISHED";
            markCommandFail(result.diagnostics);
            return result;
        }
        if (const auto* diagnostics =
                rhi::semantics::validateResourceMemoryBarrierDesc(desc, buffer.id())) {
            result.diagnostics = diagnostics;
            markCommandFail(result.diagnostics);
            return result;
        }
        if (!metalRhiNativePayloadFor(buffer)) {
            result.diagnostics = "RHI-RESOURCE-MEMORY-BARRIER-BACKEND-RESOURCE-MISSING";
            markCommandFail(result.diagnostics);
            return result;
        }
        if (!nativePayload_) {
            result.diagnostics = "RHI-METAL-MEMORY-BARRIER-NATIVE-PAYLOAD-MISSING";
            markCommandFail(result.diagnostics);
            return result;
        }
        rhi::semantics::CommandWorkloadDesc workload;
        workload.commandCount = 1u;
        workload.usesBarriers = true;
        workload.usesStageAccessBarriers = true;
        std::pmr::string diagnostics;
        if (!metalCommandLoweringAllows(capabilityContract_,
                workload,
                rhi::semantics::CommandRecordingIntent::PrimaryCommandBuffer,
                diagnostics)) {
            result.diagnostics = diagnostics;
            markCommandFail(result.diagnostics);
            return result;
        }
        if (!nativePayload_->recordBufferBarrier(diagnostics)) {
            result.diagnostics = diagnostics;
            markCommandFail(result.diagnostics);
            return result;
        }
        result.success = true;
        markCommandOk();
        return result;
    }

    rhi::ResourceBarrierResult memoryBarrier(
        rhi::GpuTexture& texture, const rhi::ResourceMemoryBarrierDesc& desc) override
    {
        if (nativePayload_ && nativePayload_->hasOriginalOrdinary()) {
            markCommandFail("RHI-METAL-ORDINARY-COMMAND-PROFILE");
            return {false, lastValidation_.diagnostics};
        }

        if (texture.originalContentExclusive()) {
            markCommandFail("RHI-TEXTURE-CONTENT-EXCLUSIVE-USE");
            return {false, lastValidation_.diagnostics};
        }
        if (const auto* id =
                rhi::semantics::GeometryInputAccessLowering::geometryInputBarrierDiagnostic(
                    rhi::Backend::Metal, desc)) {
            markCommandFail(id);
            return {false, lastValidation_.diagnostics};
        }
        if (pendingGraphics_) {
            markCommandFail("RHI-METAL-GRAPHICS-SCOPE-NOT-CONSUMED");
            return {false, lastValidation_.diagnostics};
        }
        rhi::ResourceBarrierResult result;
        result.resource = texture.id();
        result.before = texture.state();
        result.after = texture.state();
        result.stageAccessAware = true;
        result.sourceStages = desc.sourceStages;
        result.destinationStages = desc.destinationStages;
        result.sourceAccess = desc.sourceAccess;
        result.destinationAccess = desc.destinationAccess;
        if (!recordedStates_) {
            result.diagnostics = "RHI-RESOURCE-MEMORY-BARRIER-RECORDING-FINISHED";
            markCommandFail(result.diagnostics);
            return result;
        }
        if (hasActivePass()) {
            result.diagnostics = "RHI-RESOURCE-MEMORY-BARRIER-ACTIVE-PASS";
            markCommandFail(result.diagnostics);
            return result;
        }
        if (!registry_ || !registry_->find(texture.id())) {
            result.diagnostics = "RHI-RESOURCE-MEMORY-BARRIER-FOREIGN-RESOURCE";
            markCommandFail(result.diagnostics);
            return result;
        }
        if (const auto* diagnostics =
                rhi::semantics::validateResourceMemoryBarrierDesc(desc, texture.id())) {
            result.diagnostics = diagnostics;
            markCommandFail(result.diagnostics);
            return result;
        }
        if (!metalRhiNativePayloadFor(texture)) {
            result.diagnostics = "RHI-RESOURCE-MEMORY-BARRIER-BACKEND-RESOURCE-MISSING";
            markCommandFail(result.diagnostics);
            return result;
        }
        if (!nativePayload_) {
            result.diagnostics = "RHI-METAL-MEMORY-BARRIER-NATIVE-PAYLOAD-MISSING";
            markCommandFail(result.diagnostics);
            return result;
        }
        rhi::semantics::CommandWorkloadDesc workload;
        workload.commandCount = 1u;
        workload.usesBarriers = true;
        workload.usesStageAccessBarriers = true;
        std::pmr::string diagnostics;
        if (!metalCommandLoweringAllows(capabilityContract_,
                workload,
                rhi::semantics::CommandRecordingIntent::PrimaryCommandBuffer,
                diagnostics)) {
            result.diagnostics = diagnostics;
            markCommandFail(result.diagnostics);
            return result;
        }
        if (!nativePayload_->recordTextureBarrier(diagnostics)) {
            result.diagnostics = diagnostics;
            markCommandFail(result.diagnostics);
            return result;
        }
        result.success = true;
        markCommandOk();
        return result;
    }

    rhi::ResourceBarrierResult aliasBarrier(const rhi::AliasingBarrierDesc& desc) override
    {
        if (nativePayload_ && nativePayload_->hasOriginalOrdinary()) {
            markCommandFail("RHI-METAL-ORDINARY-COMMAND-PROFILE");
            return {false, lastValidation_.diagnostics};
        }

        if (pendingGraphics_) {
            markCommandFail("RHI-METAL-GRAPHICS-SCOPE-NOT-CONSUMED");
            return {false, lastValidation_.diagnostics};
        }
        rhi::ResourceBarrierResult result;
        result.resource = desc.afterResource;
        result.sourceStages = desc.sourceStages;
        result.destinationStages = desc.destinationStages;
        result.sourceAccess = desc.sourceAccess;
        result.destinationAccess = desc.destinationAccess;
        result.stageAccessAware = true;
        if (!registry_ || !nativePayload_) {
            result.diagnostics = "RHI-METAL-ALIAS-BARRIER-REGISTRY-MISSING";
            markCommandFail(result.diagnostics);
            return result;
        }
        if (!desc.beforeResource.isValid() || !desc.afterResource.isValid()
            || desc.beforeResource == desc.afterResource || desc.aliasGroup == 0
            || desc.ownershipHandoff || desc.sourceQueueRole != desc.destinationQueueRole
            || desc.sourceStages == rhi::PipelineStage::None
            || desc.destinationStages == rhi::PipelineStage::None) {
            result.diagnostics = "RHI-METAL-ALIAS-BARRIER-DESC-INVALID";
            markCommandFail(result.diagnostics);
            return result;
        }
        const auto before = registry_->find(desc.beforeResource);
        const auto after = registry_->find(desc.afterResource);
        if (!before || !after || !before->texture || !after->texture) {
            result.diagnostics = "RHI-METAL-ALIAS-BARRIER-RESOURCE-MISSING";
            markCommandFail(result.diagnostics);
            return result;
        }
        rhi::semantics::CommandWorkloadDesc workload;
        workload.commandCount = 1u;
        workload.usesBarriers = true;
        workload.usesStageAccessBarriers = true;
        std::pmr::string loweringDiagnostics;
        if (!metalCommandLoweringAllows(capabilityContract_,
                workload,
                rhi::semantics::CommandRecordingIntent::PrimaryCommandBuffer,
                loweringDiagnostics)) {
            result.diagnostics = loweringDiagnostics;
            markCommandFail(result.diagnostics);
            return result;
        }
        std::pmr::string diagnostics;
        if (!nativePayload_->recordAliasingBarrier(
                desc, before->texture, after->texture, diagnostics)) {
            result.diagnostics = diagnostics;
            markCommandFail(result.diagnostics);
            return result;
        }
        result.success = true;
        markCommandOk();
        return result;
    }

    arp::core::UniquePtr<rhi::RenderPassEncoder> beginRenderPass(
        const rhi::RenderPassDesc& desc) override
    {
        if (nativePayload_ && nativePayload_->hasOriginalOrdinary()) {
            if (isFinished() || hasActivePass() || nextRenderPass_ >= coldRenderPasses_.size()) {
                markCommandFail("RHI-METAL-ORDINARY-PASS-WRAPPER-CAPACITY");
                return {};
            }
            core::pmr::String diagnostic;
            if (!nativePayload_->recordOriginalOrdinaryScope(
                    rhi::CommandUsageKind::BeginRenderPass, commandCount_, &desc, diagnostic)) {
                markCommandFail(diagnostic);
                return {};
            }
            activePass_ = true;
            markCommandOk();
            return std::move(coldRenderPasses_[nextRenderPass_++]);
        }

        const auto operations = rhi::semantics::validateRenderPassAttachmentOperations(desc);
        if (!operations.success) {
            markCommandFail(operations.diagnostics);
            return {};
        }
        if (const auto* id = metalRhiRenderPassWriteDiagnostic(registry_, desc)) {
            markCommandFail(id);
            return {};
        }
        if (pendingGraphics_) {
            const auto valid = pendingGraphics_->validateOwners();
            if (!valid.success) {
                markCommandFail(valid.diagnostics);
                return nullptr;
            }
        }
        const bool depthOnlyRenderPass =
            desc.colorAttachments.empty() && desc.hasDepthStencilAttachment;
        if (hasActivePass() || (desc.colorAttachments.empty() && !depthOnlyRenderPass)) {
            markCommandFail("Metal RHI render pass requires color or depth attachment");
            return nullptr;
        }
        std::uint32_t sampleCount = 0u;
        auto observeAttachmentSampleCount = [&](rhi::GpuResourceId resourceId) {
            const auto resource = registry_ ? registry_->find(resourceId) : std::nullopt;
            if (!resource || resource->textureSampleCount == 0u) {
                return false;
            }
            if (sampleCount == 0u) {
                sampleCount = resource->textureSampleCount;
                return true;
            }
            return sampleCount == resource->textureSampleCount;
        };
        for (const auto& attachment : desc.colorAttachments) {
            if (!observeAttachmentSampleCount(attachment.target)) {
                markCommandFail("RHI-METAL-RENDER-PASS-ATTACHMENT-SAMPLE-COUNT-INVALID");
                return nullptr;
            }
        }
        if (desc.hasDepthStencilAttachment
            && !observeAttachmentSampleCount(desc.depthStencilAttachment.target)) {
            markCommandFail("RHI-METAL-RENDER-PASS-ATTACHMENT-SAMPLE-COUNT-INVALID");
            return nullptr;
        }
        rhi::semantics::CommandWorkloadDesc workload;
        workload.commandCount = 1u;
        workload.usesRenderPass = true;
        workload.colorAttachmentCount = static_cast<std::uint32_t>(desc.colorAttachments.size());
        workload.usesDepthStencilAttachment = desc.hasDepthStencilAttachment;
        for (const auto& attachment : desc.colorAttachments) {
            workload.usesResolveTargets =
                workload.usesResolveTargets || attachment.hasResolveTarget;
        }
        std::pmr::string loweringDiagnostics;
        rhi::semantics::NativeCommandLoweringDecision drawLowering;
        if (!metalCommandLoweringAllows(capabilityContract_,
                workload,
                rhi::semantics::CommandRecordingIntent::PrimaryCommandBuffer,
                loweringDiagnostics,
                &drawLowering)) {
            markCommandFail(loweringDiagnostics);
            return nullptr;
        }
        if (!beginPassGuard("Metal RHI command encoder has active pass")) {
            return nullptr;
        }
        if (!nativePayload_) {
            markCommandFail("RHI-METAL-RENDER-PASS-NATIVE-PAYLOAD-MISSING");
            rollbackPassBegin();
            return nullptr;
        }
        if (nativePayload_) {
            std::pmr::string diagnostics;
            if (!nativePayload_->recordRenderPassBegin(desc, diagnostics)) {
                markCommandFail(diagnostics);
                rollbackPassBegin();
                return nullptr;
            }
        }
        markCommandOk();
        return arp::core::makeUnique<MetalRhiRenderPassEncoder>(commandCount_,
            activePass_,
            nativePayload_,
            capabilityContract_,
            registry_,
            desc.subpasses.empty() ? 1u : static_cast<std::uint32_t>(desc.subpasses.size()),
            core::pmr::Vector<rhi::SubpassDesc>(desc.subpasses.begin(), desc.subpasses.end()),
            desc.colorAttachments,
            desc.hasDepthStencilAttachment ? desc.depthStencilAttachment.format
                                           : rhi::TextureFormat::Unknown,
            sampleCount,
            graphicsExecutable_,
            recordingIdentity_,
            std::move(pendingGraphics_),
            std::move(drawLowering));
    }

    arp::core::UniquePtr<rhi::ComputePassEncoder> beginComputePass(
        const rhi::ComputePassDesc& desc) override
    {
        if (nativePayload_ && nativePayload_->hasOriginalOrdinary()) {
            if (isFinished() || hasActivePass() || nextComputePass_ >= coldComputePasses_.size()) {
                markCommandFail("RHI-METAL-ORDINARY-PASS-WRAPPER-CAPACITY");
                return {};
            }
            core::pmr::String diagnostic;
            if (!nativePayload_->recordOriginalOrdinaryScope(
                    rhi::CommandUsageKind::BeginComputePass,
                    commandCount_,
                    nullptr,
                    diagnostic,
                    desc.label)) {
                markCommandFail(diagnostic);
                return {};
            }
            activePass_ = true;
            markCommandOk();
            return std::move(coldComputePasses_[nextComputePass_++]);
        }

        if (pendingGraphics_) {
            markCommandFail("RHI-METAL-GRAPHICS-SCOPE-NOT-CONSUMED");
            return nullptr;
        }
        rhi::semantics::CommandWorkloadDesc workload;
        workload.commandCount = 1u;
        workload.usesComputeDispatch = true;
        std::pmr::string loweringDiagnostics;
        rhi::semantics::NativeCommandLoweringDecision dispatchLowering;
        if (!metalCommandLoweringAllows(capabilityContract_,
                workload,
                rhi::semantics::CommandRecordingIntent::PrimaryCommandBuffer,
                loweringDiagnostics,
                &dispatchLowering)) {
            markCommandFail(loweringDiagnostics);
            return nullptr;
        }
        if (!beginPassGuard("Metal RHI command encoder has active pass")) {
            return nullptr;
        }
        if (!nativePayload_) {
            markCommandFail("RHI-METAL-COMPUTE-PASS-NATIVE-PAYLOAD-MISSING");
            rollbackPassBegin();
            return nullptr;
        }
        if (nativePayload_) {
            std::pmr::string diagnostics;
            if (!nativePayload_->recordComputePassBegin(diagnostics)) {
                markCommandFail(diagnostics);
                rollbackPassBegin();
                return nullptr;
            }
        }
        markCommandOk();
        return arp::core::makeUnique<MetalRhiComputePassEncoder>(commandCount_,
            activePass_,
            nativePayload_,
            capabilityContract_,
            registry_,
            bindlessExecutable_,
            bindlessDeviceIdentity_,
            std::move(dispatchLowering));
    }

    arp::core::UniquePtr<rhi::TransferPassEncoder> beginTransferPass(
        const rhi::TransferPassDesc& desc) override
    {
        if (nativePayload_ && nativePayload_->hasOriginalOrdinary()) {
            if (isFinished() || hasActivePass()
                || nextTransferPass_ >= coldTransferPasses_.size()) {
                markCommandFail("RHI-METAL-ORDINARY-PASS-WRAPPER-CAPACITY");
                return {};
            }
            core::pmr::String diagnostic;
            if (!nativePayload_->recordOriginalOrdinaryScope(
                    rhi::CommandUsageKind::BeginTransferPass,
                    commandCount_,
                    nullptr,
                    diagnostic,
                    desc.label)) {
                markCommandFail(diagnostic);
                return {};
            }
            activePass_ = true;
            markCommandOk();
            return std::move(coldTransferPasses_[nextTransferPass_++]);
        }

        if (pendingGraphics_) {
            markCommandFail("RHI-METAL-GRAPHICS-SCOPE-NOT-CONSUMED");
            return nullptr;
        }
        rhi::semantics::CommandWorkloadDesc workload;
        workload.commandCount = 1u;
        workload.usesTransferCopies = true;
        workload.usesUploadStaging = true;
        workload.usesReadbackStaging = true;
        std::pmr::string loweringDiagnostics;
        if (!metalCommandLoweringAllows(capabilityContract_,
                workload,
                rhi::semantics::CommandRecordingIntent::PrimaryCommandBuffer,
                loweringDiagnostics)) {
            markCommandFail(loweringDiagnostics);
            return nullptr;
        }
        if (!beginPassGuard("Metal RHI command encoder has active pass")) {
            return nullptr;
        }
        if (!nativePayload_) {
            markCommandFail("RHI-METAL-BLIT-PASS-NATIVE-PAYLOAD-MISSING");
            rollbackPassBegin();
            return nullptr;
        }
        if (nativePayload_) {
            std::pmr::string diagnostics;
            if (!nativePayload_->recordBlitPassBegin(diagnostics)) {
                markCommandFail(diagnostics);
                rollbackPassBegin();
                return nullptr;
            }
        }
        markCommandOk();
        return arp::core::makeUnique<MetalRhiTransferPassEncoder>(commandCount_,
            activePass_,
            nativePayload_,
            capabilityContract_,
            stagingUploadPool_,
            stagingUploads_.get(),
            registry_,
            uploadRetirementSerial_);
    }

    void pushDebugGroup(std::string_view label) override
    {
        if (nativePayload_ && nativePayload_->hasOriginalOrdinary()) {
            markCommandFail("RHI-METAL-ORDINARY-COMMAND-PROFILE");
            return;
        }

        if (!nativePayload_) {
            markCommandFail("RHI-METAL-DEBUG-GROUP-NATIVE-PAYLOAD-MISSING");
            return;
        }
        if (nativePayload_) {
            std::pmr::string diag;
            if (!nativePayload_->recordPushDebugGroup(label, diag)) {
                markCommandFail(diag);
            }
        }
    }

    void popDebugGroup() override
    {
        if (nativePayload_ && nativePayload_->hasOriginalOrdinary()) {
            markCommandFail("RHI-METAL-ORDINARY-COMMAND-PROFILE");
            return;
        }

        if (!nativePayload_) {
            markCommandFail("RHI-METAL-DEBUG-GROUP-NATIVE-PAYLOAD-MISSING");
            return;
        }
        if (nativePayload_) {
            std::pmr::string diag;
            if (!nativePayload_->recordPopDebugGroup(diag)) {
                markCommandFail(diag);
            }
        }
    }

    void insertDebugMarker(std::string_view label) override
    {
        if (nativePayload_ && nativePayload_->hasOriginalOrdinary()) {
            markCommandFail("RHI-METAL-ORDINARY-COMMAND-PROFILE");
            return;
        }

        if (!nativePayload_) {
            markCommandFail("RHI-METAL-DEBUG-MARKER-NATIVE-PAYLOAD-MISSING");
            return;
        }
        if (nativePayload_) {
            std::pmr::string diag;
            if (!nativePayload_->recordInsertDebugMarker(label, diag)) {
                markCommandFail(diag);
            }
        }
    }

    arp::core::UniquePtr<rhi::CommandBuffer> finish() override
    {
        if (nativePayload_ && nativePayload_->hasOriginalOrdinary()) {
            auto* journal = nativePayload_->originalOrdinaryJournal();
            if (isFinished() || hasActivePass() || !finishedCommand_ || !journal) {
                markCommandFail("RHI-METAL-ORDINARY-FINISH-STATE");
                return {};
            }
            core::pmr::String diagnostic;
            if (!nativePayload_->recordOriginalOrdinaryScope(
                    rhi::CommandUsageKind::Finish, commandCount_, nullptr, diagnostic)) {
                markCommandFail(diagnostic);
                return {};
            }
            markCommandOk();
            MetalRhiCommandBuffer finished(commandCount_, nativePayload_, *journal);
            core::replaceCpuObjectByMove(*finishedCommand_, finished);
            finished_ = true;
            submissionOwnershipTransferred_ = true;
            return std::move(finishedCommand_);
        }

        if (pendingGraphics_) {
            markCommandFail("RHI-METAL-GRAPHICS-SCOPE-NOT-CONSUMED");
            return nullptr;
        }
        if (!finishGuard("Metal RHI command encoder has unclosed pass")) {
            return nullptr;
        }
        if (!nativePayload_) {
            markCommandFail("RHI-METAL-COMMAND-BUFFER-NATIVE-PAYLOAD-MISSING");
            return nullptr;
        }
        if (nativePayload_) {
            std::pmr::string diagnostics;
            if (!nativePayload_->finish(diagnostics)) {
                markCommandFail(diagnostics);
                return nullptr;
            }
        }
        auto result = arp::core::makeUnique<MetalRhiCommandBuffer>(
            commandCount_, nativePayload_, std::move(recordedStates_), std::move(stagingUploads_));
        submissionOwnershipTransferred_ = true;
        return result;
    }

private:
    rhi::CommandValidationResult markCommandFail(core::pmr::String diagnostic) noexcept
    {
        if (nativePayload_ && nativePayload_->hasOriginalOrdinary())
            nativePayload_->poisonRecording(diagnostic);
        return CommandEncoderBase::markCommandFail(std::move(diagnostic));
    }

    core::UniquePtr<MetalRhiCommandBuffer> finishedCommand_;
    core::pmr::Vector<core::UniquePtr<MetalRhiRenderPassEncoder>> coldRenderPasses_{
        &core::defaultCpuMemoryResource()};
    core::pmr::Vector<core::UniquePtr<MetalRhiComputePassEncoder>> coldComputePasses_{
        &core::defaultCpuMemoryResource()};
    core::pmr::Vector<core::UniquePtr<MetalRhiTransferPassEncoder>> coldTransferPasses_{
        &core::defaultCpuMemoryResource()};
    std::size_t nextRenderPass_ = 0u, nextComputePass_ = 0u, nextTransferPass_ = 0u;

    static rhi::ResourceBarrierResult barrierFailure(rhi::GpuResourceId resource,
        const rhi::ResourceBarrierDesc& desc,
        bool subresourceAware,
        const std::pmr::string& diagnostics)
    {
        return {
            false,
            diagnostics,
            resource,
            desc.before,
            desc.after,
            subresourceAware,
            true,
            desc.sourceStages,
            desc.destinationStages,
            desc.sourceAccess,
            desc.destinationAccess,
        };
    }

    std::shared_ptr<MetalRhiNativeCommandPayload> nativePayload_;
    rhi::semantics::NativeBackendCapabilityContract capabilityContract_;
    MetalRhiStagingUploadPool* stagingUploadPool_ = nullptr;
    std::shared_ptr<MetalRhiResourceRegistry> registry_;
    bool bindlessExecutable_ = false;
    std::uint64_t bindlessDeviceIdentity_ = 0u;
    bool graphicsExecutable_ = false;
    const rhi::BindlessCrossExtension* bindlessIssuer_ = nullptr;
    std::uint64_t recordingIdentity_ = 0u;
    std::shared_ptr<const rhi::BindlessPreparedGraphicsBinding> pendingGraphics_;
    core::UniquePtr<rhi::RecordedStagingUploadJournal> stagingUploads_;
    std::uint64_t uploadRetirementSerial_ = 0u;
    bool submissionOwnershipTransferred_ = false;
};

bool recordMetalSecondaryFactorBinding(
    const std::shared_ptr<MetalRhiNativeSecondaryCommandPayload>& payload,
    const std::shared_ptr<MetalRhiNativeBufferPayload>& factorBuffer,
    std::uint64_t offsetBytes,
    std::uint32_t instanceStrideBytes,
    std::pmr::string& diagnostics)
{
    if (!payload || !factorBuffer) {
        diagnostics = "RHI-METAL-SECONDARY-TESSELLATION-FACTOR-BUFFER-PAYLOAD-MISSING";
        return false;
    }
    if (payload->cpuReplay) {
        payload->tessellationFactorBuffer = factorBuffer;
        payload->tessellationFactorBufferOffset = offsetBytes;
        payload->tessellationFactorInstanceStrideBytes = instanceStrideBytes;
        payload->tessellationFactorBufferBound = true;
        return true;
    }
    return recordMetalRhiNativeSecondaryTessellationFactorBufferBind(
        payload, factorBuffer, offsetBytes, instanceStrideBytes, diagnostics);
}

enum class MetalRhiSecondaryCommandKind : std::uint8_t {
    PushConstants,
    BindIndexBuffer,
    BindTessellationFactorBuffer,
    DrawIndexed,
    Draw,
};

struct MetalRhiSecondaryCommand {
    MetalRhiSecondaryCommandKind kind = MetalRhiSecondaryCommandKind::Draw;
    rhi::ShaderStage stages = rhi::ShaderStage::None;
    std::uint32_t offsetBytes = 0u;
    std::uint32_t sizeBytes = 0u;
    std::uint32_t pushDataOffset = 0u;
    std::uint32_t vertexOrIndexCount = 0u;
    std::uint32_t instanceCount = 0u;
    std::uint32_t firstElement = 0u;
    std::int32_t vertexOffset = 0;
    std::uint32_t firstInstance = 0u;
    std::uint32_t nativeCommandIndex = UINT32_MAX;
    std::shared_ptr<MetalRhiNativeBufferPayload> indexBuffer;
    rhi::IndexFormat indexFormat = rhi::IndexFormat::Uint32;
    std::uint64_t indexBufferOffset = 0u;
    std::shared_ptr<MetalRhiNativeBufferPayload> tessellationFactorBuffer;
    std::uint64_t tessellationFactorBufferOffset = 0u;
    std::uint32_t tessellationFactorInstanceStrideBytes = 0u;
};

class MetalRhiSecondaryCommandBuffer final : public rhi::SecondaryCommandBuffer {
public:
    MetalRhiSecondaryCommandBuffer(std::shared_ptr<MetalRhiNativeSecondaryCommandPayload> payload,
        std::uint32_t commandCount,
        core::pmr::Vector<MetalRhiSecondaryCommand> commands,
        core::pmr::Vector<std::uint8_t> pushData)
        : payload_(std::move(payload)), commandCount_(commandCount), commands_(std::move(commands)),
          pushData_(std::move(pushData))
    {}

    std::uint32_t commandCount() const override { return commandCount_; }

    bool softSecondary() const override { return payload_ && payload_->cpuReplay; }

    const std::shared_ptr<MetalRhiNativeSecondaryCommandPayload>& payload() const
    {
        return payload_;
    }

    const core::pmr::Vector<MetalRhiSecondaryCommand>& commands() const { return commands_; }

    const core::pmr::Vector<std::uint8_t>& pushData() const { return pushData_; }

private:
    std::shared_ptr<MetalRhiNativeSecondaryCommandPayload> payload_;
    std::uint32_t commandCount_ = 0u;
    core::pmr::Vector<MetalRhiSecondaryCommand> commands_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::uint8_t> pushData_{&core::defaultCpuMemoryResource()};
};

bool preflightMetalReplayCommands(const core::pmr::Vector<MetalRhiSecondaryCommand>& commands,
    const core::pmr::Vector<std::uint8_t>* pushData,
    const std::shared_ptr<MetalRhiNativeSecondaryCommandPayload>& payload,
    const rhi::PipelineLayout* pipelineLayout,
    const rhi::ShaderReflectionDesc* shaderReflection,
    const rhi::semantics::PushConstantConsumptionState& initialPushState,
    bool cpuReplay,
    std::pmr::string& diagnostics)
{
    if (!payload || payload->cpuReplay != cpuReplay
        || (!cpuReplay && !payload->nativeRuntimeBacked)) {
        diagnostics = "RHI-METAL-REPLAY-PAYLOAD-MODE-MISMATCH";
        return false;
    }
    if (payload->commandCount > payload->maxCommandCount) {
        diagnostics = "RHI-METAL-REPLAY-NATIVE-COMMAND-COUNT-INVALID";
        return false;
    }

    auto pushState = initialPushState;
    bool indexBufferBound = false;
    bool tessellationFactorBufferBound = false;
    bool drawRecorded = false;
    for (const auto& command : commands) {
        switch (command.kind) {
        case MetalRhiSecondaryCommandKind::PushConstants: {
            if (!cpuReplay || !pushData) {
                diagnostics = "[PC-E014] Metal native replay cannot execute push constants";
                return false;
            }
            if (command.pushDataOffset > pushData->size()
                || command.sizeBytes > pushData->size() - command.pushDataOffset) {
                diagnostics = rhi::semantics::PushConstantDiagCode::kSecondaryDataRange
                    + std::pmr::string("secondary push-constant payload range is invalid");
                return false;
            }
            const auto* data = pushData->data() + command.pushDataOffset;
            const auto commandValidation = rhi::semantics::validatePushConstantCommand(
                command.stages, command.offsetBytes, command.sizeBytes, data);
            if (!commandValidation.success) {
                diagnostics = commandValidation.diagnostics;
                return false;
            }
            const auto bindingValidation =
                rhi::semantics::validatePushConstantBinding(pipelineLayout,
                    rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment,
                    command.stages,
                    command.offsetBytes,
                    command.sizeBytes,
                    shaderReflection);
            if (!bindingValidation.success) {
                diagnostics = bindingValidation.diagnostics;
                return false;
            }
            if (pushState.hasUnconsumedUpdate()) {
                diagnostics = rhi::semantics::PushConstantDiagCode::kDuplicateUnconsumed
                    + std::pmr::string(
                        "secondary push constants already set without an intervening draw");
                return false;
            }
            pushState.onPush();
            break;
        }
        case MetalRhiSecondaryCommandKind::BindIndexBuffer: {
            if (!command.indexBuffer) {
                diagnostics = "RHI-METAL-REPLAY-INDEX-BUFFER-PAYLOAD-MISSING";
                return false;
            }
            const auto alignment = command.indexFormat == rhi::IndexFormat::Uint16 ? 2u : 4u;
            if ((command.indexBufferOffset % alignment) != 0u) {
                diagnostics = "RHI-METAL-REPLAY-INDEX-BUFFER-OFFSET-INVALID";
                return false;
            }
            indexBufferBound = true;
            break;
        }
        case MetalRhiSecondaryCommandKind::BindTessellationFactorBuffer:
            if (!command.tessellationFactorBuffer
                || payload->primitiveTopology != rhi::PrimitiveTopology::PatchList
                || payload->patchControlPointCount == 0u
                || command.tessellationFactorInstanceStrideBytes == 0u
                || (command.tessellationFactorBufferOffset & 3u) != 0u
                || (command.tessellationFactorInstanceStrideBytes & 3u) != 0u || drawRecorded
                || tessellationFactorBufferBound) {
                diagnostics = "RHI-METAL-REPLAY-TESSELLATION-STATE-INVALID";
                return false;
            }
            tessellationFactorBufferBound = true;
            break;
        case MetalRhiSecondaryCommandKind::DrawIndexed:
            if (!indexBufferBound || command.vertexOrIndexCount == 0u || command.instanceCount == 0u
                || (payload->primitiveTopology == rhi::PrimitiveTopology::PatchList
                    && (!tessellationFactorBufferBound || command.vertexOffset != 0
                        || command.vertexOrIndexCount % payload->patchControlPointCount != 0u))) {
                diagnostics = "RHI-METAL-REPLAY-INDEXED-DRAW-STATE-INVALID";
                return false;
            }
            if (cpuReplay) {
                if (command.nativeCommandIndex != UINT32_MAX) {
                    diagnostics = "RHI-METAL-CPU-REPLAY-NATIVE-INDEX-UNEXPECTED";
                    return false;
                }
            } else if (command.nativeCommandIndex == UINT32_MAX
                || command.nativeCommandIndex >= payload->commandCount) {
                diagnostics = "RHI-METAL-REPLAY-NATIVE-COMMAND-INDEX-INVALID";
                return false;
            }
            pushState.onConsume();
            drawRecorded = true;
            break;
        case MetalRhiSecondaryCommandKind::Draw:
            if (command.vertexOrIndexCount == 0u || command.instanceCount == 0u
                || (payload->primitiveTopology == rhi::PrimitiveTopology::PatchList
                    && (!tessellationFactorBufferBound
                        || command.vertexOrIndexCount % payload->patchControlPointCount != 0u
                        || command.firstElement % payload->patchControlPointCount != 0u))) {
                diagnostics = "RHI-METAL-REPLAY-DRAW-STATE-INVALID";
                return false;
            }
            if (cpuReplay) {
                if (command.nativeCommandIndex != UINT32_MAX) {
                    diagnostics = "RHI-METAL-CPU-REPLAY-NATIVE-INDEX-UNEXPECTED";
                    return false;
                }
            } else if (command.nativeCommandIndex == UINT32_MAX
                || command.nativeCommandIndex >= payload->commandCount) {
                diagnostics = "RHI-METAL-REPLAY-NATIVE-COMMAND-INDEX-INVALID";
                return false;
            }
            pushState.onConsume();
            drawRecorded = true;
            break;
        default:
            diagnostics = "RHI-METAL-REPLAY-COMMAND-KIND-INVALID";
            return false;
        }
    }
    return true;
}

class MetalRhiSecondaryCommandEncoder final : public rhi::SecondaryCommandEncoder {
public:
    explicit MetalRhiSecondaryCommandEncoder(const rhi::SecondaryCommandEncoderDesc& desc,
        const rhi::semantics::NativeBackendCapabilityContract& capabilityContract)
        : capabilityContract_(capabilityContract)
    {
        rhi::semantics::CommandWorkloadDesc workload;
        workload.commandCount = 1u;
        workload.allowCpuReplay = desc.allowCpuReplay;
        workload.usesRenderPass = true;
        const auto lowering = rhi::semantics::lowerCommandWorkload(capabilityContract_,
            workload,
            rhi::semantics::CommandRecordingIntent::SoftSecondaryCommandBuffer);
        if (!lowering.success) {
            return;
        }
        payload_ = createMetalRhiNativeSecondaryCommandPayload(desc.label,
            desc.primitiveTopology,
            desc.maxCommandCount,
            desc.allowCpuReplay,
            desc.patchControlPointCount);
    }

    bool executable() const
    {
        return payload_ && (payload_->nativeRuntimeBacked || payload_->cpuReplay) && !finished_;
    }

    rhi::CommandValidationResult pushConstants(rhi::ShaderStage stages,
        std::uint32_t offsetBytes,
        std::uint32_t sizeBytes,
        const void* data) override
    {
        if (finished_) {
            return commandFail("RHI-METAL-SECONDARY-ENCODER-ALREADY-FINISHED", commandCount_);
        }
        if (!(payload_ && (payload_->cpuReplay || payload_->nativeRuntimeBacked))) {
            return commandFail(rhi::semantics::PushConstantDiagCode::kSecondaryNotSupported
                    + std::pmr::string(
                        "Metal secondary push constants require an executable replay payload"),
                commandCount_);
        }
        rhi::semantics::CommandWorkloadDesc workload;
        workload.commandCount = 1u;
        workload.allowCpuReplay = payload_->cpuReplay;
        workload.usesRenderPass = true;
        workload.usesPushConstants = true;
        std::pmr::string loweringDiagnostics;
        if (!metalCommandLoweringAllows(capabilityContract_,
                workload,
                rhi::semantics::CommandRecordingIntent::SoftSecondaryCommandBuffer,
                loweringDiagnostics)) {
            return commandFail(loweringDiagnostics, commandCount_);
        }
        const auto validation =
            rhi::semantics::validatePushConstantCommand(stages, offsetBytes, sizeBytes, data);
        if (!validation.success) {
            return commandFail(validation.diagnostics, commandCount_);
        }
        if (pushState_.hasUnconsumedUpdate()) {
            return commandFail(rhi::semantics::PushConstantDiagCode::kSecondaryNotSupported
                    + std::pmr::string(
                        "Metal secondary push constants require an intervening draw"),
                commandCount_);
        }
        if (sizeBytes > std::numeric_limits<std::uint32_t>::max()
                - static_cast<std::uint32_t>(pushData_.size())) {
            return commandFail("RHI-METAL-SECONDARY-PUSH-CONSTANT-DATA-OVERFLOW", commandCount_);
        }
        const auto pushDataOffset = static_cast<std::uint32_t>(pushData_.size());
        pushData_.resize(pushData_.size() + sizeBytes);
        std::memcpy(pushData_.data() + pushDataOffset, data, sizeBytes);
        MetalRhiSecondaryCommand command;
        command.kind = MetalRhiSecondaryCommandKind::PushConstants;
        command.stages = stages;
        command.offsetBytes = offsetBytes;
        command.sizeBytes = sizeBytes;
        command.pushDataOffset = pushDataOffset;
        commands_.push_back(std::move(command));
        pushState_.onPush();
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult bindTessellationFactorBuffer(const rhi::GpuBuffer& buffer,
        std::uint64_t offsetBytes,
        std::uint32_t instanceStrideBytes) override
    {
        if (finished_) {
            return commandFail("RHI-METAL-SECONDARY-ENCODER-ALREADY-FINISHED", commandCount_);
        }
        if (!payload_ || payload_->primitiveTopology != rhi::PrimitiveTopology::PatchList
            || payload_->patchControlPointCount == 0u) {
            return commandFail(
                "RHI-METAL-SECONDARY-TESSELLATION-FACTOR-BUFFER-REQUIRES-PATCHLIST", commandCount_);
        }
        if (!hasResourceUsage(buffer.desc().usage, rhi::ResourceUsage::StorageBuffer)
            || instanceStrideBytes == 0u || (offsetBytes & 3u) != 0u
            || (instanceStrideBytes & 3u) != 0u || offsetBytes > buffer.desc().sizeBytes
            || buffer.desc().sizeBytes - offsetBytes < sizeof(std::uint32_t)) {
            return commandFail(
                "RHI-METAL-SECONDARY-TESSELLATION-FACTOR-BUFFER-RANGE-INVALID", commandCount_);
        }
        auto factorBuffer = metalRhiNativePayloadFor(buffer);
        if (!factorBuffer) {
            return commandFail(
                "RHI-METAL-SECONDARY-TESSELLATION-FACTOR-BUFFER-PAYLOAD-MISSING", commandCount_);
        }
        std::pmr::string diagnostics;
        if (!recordMetalSecondaryFactorBinding(
                payload_, factorBuffer, offsetBytes, instanceStrideBytes, diagnostics)) {
            return commandFail(diagnostics.empty()
                    ? "RHI-METAL-SECONDARY-TESSELLATION-FACTOR-BUFFER-PAYLOAD-MISSING"
                    : diagnostics,
                commandCount_);
        }
        MetalRhiSecondaryCommand command;
        command.kind = MetalRhiSecondaryCommandKind::BindTessellationFactorBuffer;
        command.tessellationFactorBuffer = factorBuffer;
        command.tessellationFactorBufferOffset = offsetBytes;
        command.tessellationFactorInstanceStrideBytes = instanceStrideBytes;
        commands_.push_back(std::move(command));
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult bindIndexBuffer(
        const rhi::GpuBuffer& buffer, rhi::IndexFormat format, std::uint64_t offsetBytes) override
    {
        if (finished_) {
            return commandFail("RHI-METAL-SECONDARY-ENCODER-ALREADY-FINISHED", commandCount_);
        }
        const auto alignment = format == rhi::IndexFormat::Uint16 ? 2u : 4u;
        if (static_cast<std::uint32_t>(buffer.desc().usage & rhi::ResourceUsage::IndexBuffer) == 0u
            || (offsetBytes % alignment) != 0u || offsetBytes >= buffer.desc().sizeBytes) {
            return commandFail("RHI-METAL-SECONDARY-INDEX-BUFFER-ARGUMENTS-INVALID", commandCount_);
        }
        indexBuffer_ = metalRhiNativePayloadFor(buffer);
        indexFormat_ = format;
        indexBufferOffset_ = offsetBytes;
        if (!indexBuffer_) {
            return commandFail("RHI-METAL-SECONDARY-INDEX-BUFFER-PAYLOAD-MISSING", commandCount_);
        }
        MetalRhiSecondaryCommand command;
        command.kind = MetalRhiSecondaryCommandKind::BindIndexBuffer;
        command.indexBuffer = indexBuffer_;
        command.indexFormat = indexFormat_;
        command.indexBufferOffset = indexBufferOffset_;
        commands_.push_back(std::move(command));
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult drawIndexed(std::uint32_t indexCount,
        std::uint32_t instanceCount,
        std::uint32_t firstIndex,
        std::int32_t vertexOffset,
        std::uint32_t firstInstance) override
    {
        if (finished_) {
            return commandFail("RHI-METAL-SECONDARY-ENCODER-ALREADY-FINISHED", commandCount_);
        }
        if (!payload_ || !indexBuffer_ || indexCount == 0u || instanceCount == 0u) {
            return commandFail("RHI-METAL-SECONDARY-INDEXED-DRAW-ARGUMENTS-INVALID", commandCount_);
        }
        if (payload_->primitiveTopology == rhi::PrimitiveTopology::PatchList
            && (payload_->patchControlPointCount == 0u || !payload_->tessellationFactorBufferBound
                || vertexOffset != 0 || indexCount % payload_->patchControlPointCount != 0u)) {
            return commandFail(
                "RHI-METAL-SECONDARY-PATCH-DRAW-REQUIRES-VALID-TESSELLATION-STATE", commandCount_);
        }
        if (payload_->primitiveTopology == rhi::PrimitiveTopology::PatchList) {
            payload_->maxTessellationInstanceCount =
                std::max(payload_->maxTessellationInstanceCount, instanceCount);
        }
        if (payload_->cpuReplay) {
            if (!reserveCpuReplayDraw()) {
                return commandFail("RHI-METAL-SECONDARY-ICB-CAPACITY-EXCEEDED", commandCount_);
            }
            MetalRhiSecondaryCommand command;
            command.kind = MetalRhiSecondaryCommandKind::DrawIndexed;
            command.vertexOrIndexCount = indexCount;
            command.instanceCount = instanceCount;
            command.firstElement = firstIndex;
            command.vertexOffset = vertexOffset;
            command.firstInstance = firstInstance;
            command.indexBuffer = indexBuffer_;
            command.indexFormat = indexFormat_;
            command.indexBufferOffset = indexBufferOffset_;
            commands_.push_back(std::move(command));
            pushState_.onConsume();
            return commandOk(++commandCount_);
        }
        const auto nativeCommandIndex = payload_->commandCount;
        std::pmr::string diagnostics;
        if (!recordMetalRhiNativeSecondaryDrawIndexed(payload_,
                indexCount,
                instanceCount,
                firstIndex,
                vertexOffset,
                firstInstance,
                indexBuffer_,
                indexFormat_,
                indexBufferOffset_,
                diagnostics)) {
            return commandFail(diagnostics.empty()
                    ? "RHI-METAL-SECONDARY-INDEXED-DRAW-PAYLOAD-MISSING"
                    : diagnostics,
                commandCount_);
        }
        MetalRhiSecondaryCommand command;
        command.kind = MetalRhiSecondaryCommandKind::DrawIndexed;
        command.vertexOrIndexCount = indexCount;
        command.instanceCount = instanceCount;
        command.firstElement = firstIndex;
        command.vertexOffset = vertexOffset;
        command.firstInstance = firstInstance;
        command.nativeCommandIndex = nativeCommandIndex;
        command.indexBuffer = indexBuffer_;
        command.indexFormat = indexFormat_;
        command.indexBufferOffset = indexBufferOffset_;
        commands_.push_back(std::move(command));
        ++commandCount_;
        return commandOk(commandCount_);
    }

    rhi::CommandValidationResult draw(std::uint32_t vertexCount,
        std::uint32_t instanceCount,
        std::uint32_t firstVertex,
        std::uint32_t firstInstance) override
    {
        if (finished_) {
            return commandFail("RHI-METAL-SECONDARY-ENCODER-ALREADY-FINISHED", commandCount_);
        }
        if (!payload_ || vertexCount == 0u || instanceCount == 0u) {
            return commandFail("RHI-METAL-SECONDARY-DRAW-ARGUMENTS-INVALID", commandCount_);
        }
        if (payload_->primitiveTopology == rhi::PrimitiveTopology::PatchList
            && (payload_->patchControlPointCount == 0u || !payload_->tessellationFactorBufferBound
                || vertexCount % payload_->patchControlPointCount != 0u
                || firstVertex % payload_->patchControlPointCount != 0u)) {
            return commandFail(
                "RHI-METAL-SECONDARY-PATCH-DRAW-REQUIRES-VALID-TESSELLATION-STATE", commandCount_);
        }
        if (payload_->primitiveTopology == rhi::PrimitiveTopology::PatchList) {
            payload_->maxTessellationInstanceCount =
                std::max(payload_->maxTessellationInstanceCount, instanceCount);
        }
        if (payload_->cpuReplay) {
            if (!reserveCpuReplayDraw()) {
                return commandFail("RHI-METAL-SECONDARY-ICB-CAPACITY-EXCEEDED", commandCount_);
            }
            MetalRhiSecondaryCommand command;
            command.kind = MetalRhiSecondaryCommandKind::Draw;
            command.vertexOrIndexCount = vertexCount;
            command.instanceCount = instanceCount;
            command.firstElement = firstVertex;
            command.firstInstance = firstInstance;
            commands_.push_back(std::move(command));
            pushState_.onConsume();
            return commandOk(++commandCount_);
        }
        const auto nativeCommandIndex = payload_->commandCount;
        std::pmr::string diagnostics;
        if (!recordMetalRhiNativeSecondaryDraw(
                payload_, vertexCount, instanceCount, firstVertex, firstInstance, diagnostics)) {
            return commandFail(
                diagnostics.empty() ? "RHI-METAL-SECONDARY-DRAW-PAYLOAD-MISSING" : diagnostics,
                commandCount_);
        }
        MetalRhiSecondaryCommand command;
        command.kind = MetalRhiSecondaryCommandKind::Draw;
        command.vertexOrIndexCount = vertexCount;
        command.instanceCount = instanceCount;
        command.firstElement = firstVertex;
        command.firstInstance = firstInstance;
        command.nativeCommandIndex = nativeCommandIndex;
        commands_.push_back(std::move(command));
        ++commandCount_;
        return commandOk(commandCount_);
    }

    arp::core::UniquePtr<rhi::SecondaryCommandBuffer> finish() override
    {
        if (finished_) {
            return nullptr;
        }
        finished_ = true;
        return payload_ && (payload_->nativeRuntimeBacked || payload_->cpuReplay)
            ? arp::core::makeUnique<MetalRhiSecondaryCommandBuffer>(
                  payload_, commandCount_, std::move(commands_), std::move(pushData_))
            : nullptr;
    }

private:
    bool reserveCpuReplayDraw()
    {
        if (!payload_ || !payload_->cpuReplay
            || executableCommandCount_ >= payload_->maxCommandCount) {
            return false;
        }
        ++executableCommandCount_;
        return true;
    }

    std::uint32_t commandCount_ = 0u;
    std::shared_ptr<MetalRhiNativeSecondaryCommandPayload> payload_;
    std::shared_ptr<MetalRhiNativeBufferPayload> indexBuffer_;
    rhi::IndexFormat indexFormat_ = rhi::IndexFormat::Uint32;
    std::uint64_t indexBufferOffset_ = 0u;
    bool finished_ = false;
    std::uint32_t executableCommandCount_ = 0u;
    core::pmr::Vector<MetalRhiSecondaryCommand> commands_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::uint8_t> pushData_{&core::defaultCpuMemoryResource()};
    rhi::semantics::NativeBackendCapabilityContract capabilityContract_;
    rhi::semantics::PushConstantConsumptionState pushState_;
};

class MetalRhiReplayTransaction final {
public:
    explicit MetalRhiReplayTransaction(const std::shared_ptr<MetalRhiNativeCommandPayload>& payload)
        : payload_(payload)
    {}

    ~MetalRhiReplayTransaction()
    {
        if (!committed_ && payload_) {
            payload_->poisonRecording("RHI-METAL-REPLAY-TRANSACTION-ABORTED");
        }
    }

    void commit() noexcept { committed_ = true; }

private:
    std::shared_ptr<MetalRhiNativeCommandPayload> payload_;
    bool committed_ = false;
};

rhi::CommandValidationResult MetalRhiRenderPassEncoder::executeSecondary(
    const rhi::SecondaryCommandBuffer& commandBuffer)
{
    if (graphicsScope_)
        return commandFail("RHI-METAL-GRAPHICS-SCOPE-SECONDARY-UNSUPPORTED", commandCount_);
    const auto descriptorUse = boundDescriptors_.validateLiveBindings();
    if (!descriptorUse.success)
        return commandFail(descriptorUse.diagnostics, commandCount_);
    const auto* secondary = dynamic_cast<const MetalRhiSecondaryCommandBuffer*>(&commandBuffer);
    if (!secondary || !secondary->payload() || !nativePayload_) {
        return commandFail("RHI-METAL-SECONDARY-ICB-PAYLOAD-INVALID", commandCount_);
    }
    if (!pipelineBound_) {
        return commandFail("RHI-METAL-SECONDARY-REQUIRES-BOUND-PIPELINE", commandCount_);
    }
    if (!secondary->softSecondary()
        && (!boundPipelinePayload_ || !boundPipelinePayload_->supportsIndirectCommands)) {
        return commandFail("RHI-METAL-SECONDARY-PIPELINE-BINDINGS-UNSUPPORTED", commandCount_);
    }
    if (!boundPipelinePayload_
        || secondary->payload()->primitiveTopology != boundPipelinePayload_->primitiveTopology
        || (boundPipelinePayload_->primitiveTopology == rhi::PrimitiveTopology::PatchList
            && secondary->payload()->patchControlPointCount
                != boundPipelinePayload_->patchControlPointCount)) {
        return commandFail("RHI-METAL-SECONDARY-PIPELINE-PATCH-CONTRACT-MISMATCH", commandCount_);
    }
    if (secondary->commandCount() != secondary->commands().size()) {
        return commandFail("RHI-METAL-SECONDARY-COMMAND-COUNT-MISMATCH", commandCount_);
    }
    if (secondary->commandCount() != 0u) {
        rhi::semantics::CommandWorkloadDesc workload;
        workload.commandCount = secondary->commandCount();
        workload.allowCpuReplay = secondary->softSecondary();
        workload.usesRenderPass = true;
        for (const auto& command : secondary->commands()) {
            workload.usesPushConstants = workload.usesPushConstants
                || command.kind == MetalRhiSecondaryCommandKind::PushConstants;
        }
        const auto lowering = rhi::semantics::lowerCommandWorkload(capabilityContract_,
            workload,
            rhi::semantics::CommandRecordingIntent::SoftSecondaryCommandBuffer);
        const auto loweringUse = rhi::semantics::validateNativeCommandLoweringUse(lowering,
            secondary->softSecondary()
                ? rhi::semantics::NativeCommandSemantic::MetalSecondaryCommandReplay
                : rhi::semantics::NativeCommandSemantic::MetalIndirectCommandBuffer,
            secondary->softSecondary() ? rhi::FallbackMode::CpuReplay : rhi::FallbackMode::Native,
            "Metal secondary command");
        if (!loweringUse.success) {
            return commandFail(loweringUse.diagnostics, commandCount_);
        }
    }
    if (!secondary->softSecondary()) {
        for (const auto& command : secondary->commands()) {
            if (command.kind == MetalRhiSecondaryCommandKind::PushConstants) {
                return commandFail("[PC-E014] Metal native secondary command buffers cannot "
                                   "execute push constants",
                    commandCount_);
            }
        }
    }
    {
        std::pmr::string diagnostics;
        if (!preflightMetalReplayCommands(secondary->commands(),
                &secondary->pushData(),
                secondary->payload(),
                pipelineLayout_,
                shaderReflection_,
                pushState_,
                secondary->softSecondary(),
                diagnostics)) {
            return commandFail(diagnostics, commandCount_);
        }
    }
    MetalRhiReplayTransaction replayTransaction(nativePayload_);
    bool secondaryIndexBufferBound = false;
    auto replayPushState = pushState_;
    if (secondary->softSecondary()) {
        for (const auto& command : secondary->commands()) {
            std::pmr::string diagnostics;
            switch (command.kind) {
            case MetalRhiSecondaryCommandKind::PushConstants: {
                if (command.pushDataOffset > secondary->pushData().size()
                    || command.sizeBytes > secondary->pushData().size() - command.pushDataOffset) {
                    return commandFail(rhi::semantics::PushConstantDiagCode::kSecondaryDataRange
                            + std::pmr::string("secondary push-constant payload range is invalid"),
                        commandCount_);
                }
                const auto* data = secondary->pushData().data() + command.pushDataOffset;
                const auto commandValidation = rhi::semantics::validatePushConstantCommand(
                    command.stages, command.offsetBytes, command.sizeBytes, data);
                if (!commandValidation.success) {
                    return commandFail(commandValidation.diagnostics, commandCount_);
                }
                const auto bindingValidation =
                    rhi::semantics::validatePushConstantBinding(pipelineLayout_,
                        rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment,
                        command.stages,
                        command.offsetBytes,
                        command.sizeBytes,
                        shaderReflection_);
                if (!bindingValidation.success) {
                    return commandFail(bindingValidation.diagnostics, commandCount_);
                }
                if (replayPushState.hasUnconsumedUpdate()) {
                    return commandFail(rhi::semantics::PushConstantDiagCode::kDuplicateUnconsumed
                            + std::pmr::string(
                                "secondary push constants already set without an intervening draw"),
                        commandCount_);
                }
                if (!nativePayload_->recordPushConstants(command.stages,
                        command.offsetBytes,
                        command.sizeBytes,
                        data,
                        diagnostics)) {
                    return commandFail(diagnostics, commandCount_);
                }
                replayPushState.onPush();
                break;
            }
            case MetalRhiSecondaryCommandKind::BindIndexBuffer:
                if (!command.indexBuffer
                    || !nativePayload_->recordIndexBufferBind(command.indexBuffer,
                        command.indexFormat,
                        command.indexBufferOffset,
                        diagnostics)) {
                    return commandFail(diagnostics.empty()
                            ? "RHI-METAL-SECONDARY-INDEX-BUFFER-PAYLOAD-MISSING"
                            : diagnostics,
                        commandCount_);
                }
                secondaryIndexBufferBound = true;
                break;
            case MetalRhiSecondaryCommandKind::BindTessellationFactorBuffer:
                if (!command.tessellationFactorBuffer
                    || !nativePayload_->recordTessellationFactorBufferBind(
                        command.tessellationFactorBuffer,
                        command.tessellationFactorBufferOffset,
                        command.tessellationFactorInstanceStrideBytes,
                        diagnostics)) {
                    return commandFail(diagnostics.empty()
                            ? "RHI-METAL-SECONDARY-TESSELLATION-FACTOR-BUFFER-PAYLOAD-MISSING"
                            : diagnostics,
                        commandCount_);
                }
                break;
            case MetalRhiSecondaryCommandKind::DrawIndexed:
                if (!secondaryIndexBufferBound) {
                    return commandFail(
                        "RHI-METAL-SECONDARY-INDEXED-DRAW-REQUIRES-INDEX-BUFFER", commandCount_);
                }
                if (!nativePayload_->recordDrawIndexed(command.vertexOrIndexCount,
                        command.instanceCount,
                        command.firstElement,
                        command.vertexOffset,
                        command.firstInstance,
                        diagnostics)) {
                    return commandFail(diagnostics, commandCount_);
                }
                replayPushState.onConsume();
                break;
            case MetalRhiSecondaryCommandKind::Draw:
                if (!nativePayload_->recordDraw(command.vertexOrIndexCount,
                        command.instanceCount,
                        command.firstElement,
                        command.firstInstance,
                        diagnostics)) {
                    return commandFail(diagnostics, commandCount_);
                }
                replayPushState.onConsume();
                break;
            }
        }
        pushState_ = replayPushState;
        if (secondaryIndexBufferBound) {
            indexBufferBound_ = true;
        }
        commandCount_ += secondary->commandCount();
        replayTransaction.commit();
        return commandOk(commandCount_);
    }
    for (const auto& command : secondary->commands()) {
        std::pmr::string diagnostics;
        switch (command.kind) {
        case MetalRhiSecondaryCommandKind::PushConstants: {
            if (command.pushDataOffset > secondary->pushData().size()
                || command.sizeBytes > secondary->pushData().size() - command.pushDataOffset) {
                return commandFail(rhi::semantics::PushConstantDiagCode::kSecondaryDataRange
                        + std::pmr::string("secondary push-constant payload range is invalid"),
                    commandCount_);
            }
            const auto* data = secondary->pushData().data() + command.pushDataOffset;
            const auto commandValidation = rhi::semantics::validatePushConstantCommand(
                command.stages, command.offsetBytes, command.sizeBytes, data);
            if (!commandValidation.success) {
                return commandFail(commandValidation.diagnostics, commandCount_);
            }
            const auto bindingValidation =
                rhi::semantics::validatePushConstantBinding(pipelineLayout_,
                    rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment,
                    command.stages,
                    command.offsetBytes,
                    command.sizeBytes,
                    shaderReflection_);
            if (!bindingValidation.success) {
                return commandFail(bindingValidation.diagnostics, commandCount_);
            }
            if (replayPushState.hasUnconsumedUpdate()) {
                return commandFail(rhi::semantics::PushConstantDiagCode::kDuplicateUnconsumed
                        + std::pmr::string(
                            "secondary push constants already set without an intervening draw"),
                    commandCount_);
            }
            if (!nativePayload_->recordPushConstants(
                    command.stages, command.offsetBytes, command.sizeBytes, data, diagnostics)) {
                return commandFail(diagnostics, commandCount_);
            }
            replayPushState.onPush();
            break;
        }
        case MetalRhiSecondaryCommandKind::BindIndexBuffer:
            if (!command.indexBuffer) {
                return commandFail(
                    "RHI-METAL-SECONDARY-INDEX-BUFFER-PAYLOAD-MISSING", commandCount_);
            }
            secondaryIndexBufferBound = true;
            break;
        case MetalRhiSecondaryCommandKind::BindTessellationFactorBuffer:
            if (!command.tessellationFactorBuffer
                || !nativePayload_->recordTessellationFactorBufferBind(
                    command.tessellationFactorBuffer,
                    command.tessellationFactorBufferOffset,
                    command.tessellationFactorInstanceStrideBytes,
                    diagnostics)) {
                return commandFail(diagnostics.empty()
                        ? "RHI-METAL-SECONDARY-TESSELLATION-FACTOR-BUFFER-PAYLOAD-MISSING"
                        : diagnostics,
                    commandCount_);
            }
            break;
        case MetalRhiSecondaryCommandKind::DrawIndexed:
            if (!secondaryIndexBufferBound) {
                return commandFail(
                    "RHI-METAL-SECONDARY-INDEXED-DRAW-REQUIRES-INDEX-BUFFER", commandCount_);
            }
            if (command.nativeCommandIndex == UINT32_MAX
                || !nativePayload_->executeSecondaryCommand(
                    secondary->payload(), command.nativeCommandIndex, diagnostics)) {
                return commandFail(diagnostics.empty()
                        ? "RHI-METAL-SECONDARY-ICB-COMMAND-PAYLOAD-MISSING"
                        : diagnostics,
                    commandCount_);
            }
            replayPushState.onConsume();
            break;
        case MetalRhiSecondaryCommandKind::Draw:
            if (command.nativeCommandIndex == UINT32_MAX
                || !nativePayload_->executeSecondaryCommand(
                    secondary->payload(), command.nativeCommandIndex, diagnostics)) {
                return commandFail(diagnostics.empty()
                        ? "RHI-METAL-SECONDARY-ICB-COMMAND-PAYLOAD-MISSING"
                        : diagnostics,
                    commandCount_);
            }
            replayPushState.onConsume();
            break;
        }
    }
    pushState_ = replayPushState;
    if (secondaryIndexBufferBound) {
        indexBufferBound_ = true;
    }
    commandCount_ += secondary->commandCount();
    replayTransaction.commit();
    return commandOk(commandCount_);
}

class MetalRhiRenderBundle final : public rhi::RenderBundle {
public:
    MetalRhiRenderBundle(std::shared_ptr<MetalRhiNativeSecondaryCommandPayload> payload,
        rhi::RenderBundleDesc desc,
        std::uint32_t commandCount,
        core::pmr::Vector<MetalRhiSecondaryCommand> commands)
        : payload_(std::move(payload)), desc_(std::move(desc)), commandCount_(commandCount),
          commands_(std::move(commands))
    {}

    std::uint32_t commandCount() const override { return commandCount_; }

    rhi::PrimitiveTopology primitiveTopology() const { return desc_.primitiveTopology; }

    std::uint32_t patchControlPointCount() const { return desc_.patchControlPointCount; }

    const std::pmr::vector<rhi::TextureFormat>& colorFormats() const { return desc_.colorFormats; }

    rhi::TextureFormat depthStencilFormat() const { return desc_.depthStencilFormat; }

    std::uint32_t sampleCount() const { return desc_.sampleCount; }

    const std::shared_ptr<MetalRhiNativeSecondaryCommandPayload>& payload() const
    {
        return payload_;
    }

    const core::pmr::Vector<MetalRhiSecondaryCommand>& commands() const { return commands_; }

private:
    std::shared_ptr<MetalRhiNativeSecondaryCommandPayload> payload_;
    rhi::RenderBundleDesc desc_;
    std::uint32_t commandCount_ = 0u;
    core::pmr::Vector<MetalRhiSecondaryCommand> commands_{&core::defaultCpuMemoryResource()};
};

class MetalRhiRenderBundleEncoder final : public rhi::RenderBundleEncoder {
public:
    MetalRhiRenderBundleEncoder(rhi::RenderBundleDesc desc,
        const rhi::semantics::NativeBackendCapabilityContract& capabilityContract)
        : desc_(std::move(desc)), capabilityContract_(capabilityContract)
    {
        rhi::semantics::CommandWorkloadDesc workload;
        workload.commandCount = 1u;
        workload.allowCpuReplay = desc_.allowCpuReplay;
        workload.usesRenderPass = true;
        workload.usesRenderBundle = true;
        const auto lowering = rhi::semantics::lowerCommandWorkload(
            capabilityContract_, workload, rhi::semantics::CommandRecordingIntent::RenderBundle);
        if (!lowering.success || !attachmentContractValid()) {
            return;
        }
        payload_ = createMetalRhiNativeSecondaryCommandPayload(desc_.label,
            desc_.primitiveTopology,
            desc_.maxCommandCount,
            desc_.allowCpuReplay,
            desc_.patchControlPointCount);
    }

    rhi::CommandValidationResult bindTessellationFactorBuffer(const rhi::GpuBuffer& buffer,
        std::uint64_t offsetBytes,
        std::uint32_t instanceStrideBytes) override
    {
        if (finished_) {
            return commandFail("RHI-METAL-RENDER-BUNDLE-ENCODER-ALREADY-FINISHED", commandCount_);
        }
        if (!payload_ || desc_.primitiveTopology != rhi::PrimitiveTopology::PatchList
            || desc_.patchControlPointCount == 0u) {
            return commandFail(
                "RHI-METAL-RENDER-BUNDLE-TESSELLATION-FACTOR-BUFFER-REQUIRES-PATCHLIST",
                commandCount_);
        }
        if (!hasResourceUsage(buffer.desc().usage, rhi::ResourceUsage::StorageBuffer)
            || instanceStrideBytes == 0u || (offsetBytes & 3u) != 0u
            || (instanceStrideBytes & 3u) != 0u || offsetBytes > buffer.desc().sizeBytes
            || buffer.desc().sizeBytes - offsetBytes < sizeof(std::uint32_t)) {
            return commandFail(
                "RHI-METAL-RENDER-BUNDLE-TESSELLATION-FACTOR-BUFFER-RANGE-INVALID", commandCount_);
        }
        auto factorBuffer = metalRhiNativePayloadFor(buffer);
        if (!factorBuffer) {
            return commandFail("RHI-METAL-RENDER-BUNDLE-TESSELLATION-FACTOR-BUFFER-PAYLOAD-MISSING",
                commandCount_);
        }
        std::pmr::string diagnostics;
        if (!recordMetalSecondaryFactorBinding(
                payload_, factorBuffer, offsetBytes, instanceStrideBytes, diagnostics)) {
            return commandFail(diagnostics.empty()
                    ? "RHI-METAL-RENDER-BUNDLE-TESSELLATION-FACTOR-BUFFER-PAYLOAD-MISSING"
                    : diagnostics,
                commandCount_);
        }
        MetalRhiSecondaryCommand command;
        command.kind = MetalRhiSecondaryCommandKind::BindTessellationFactorBuffer;
        command.tessellationFactorBuffer = factorBuffer;
        command.tessellationFactorBufferOffset = offsetBytes;
        command.tessellationFactorInstanceStrideBytes = instanceStrideBytes;
        commands_.push_back(std::move(command));
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult bindIndexBuffer(
        const rhi::GpuBuffer& buffer, rhi::IndexFormat format, std::uint64_t offsetBytes) override
    {
        if (finished_) {
            return commandFail("RHI-METAL-RENDER-BUNDLE-ENCODER-ALREADY-FINISHED", commandCount_);
        }
        const auto alignment = format == rhi::IndexFormat::Uint16 ? 2u : 4u;
        if ((buffer.desc().usage & rhi::ResourceUsage::IndexBuffer) == rhi::ResourceUsage::None
            || (offsetBytes % alignment) != 0u || offsetBytes >= buffer.desc().sizeBytes) {
            return commandFail(
                "RHI-METAL-RENDER-BUNDLE-INDEX-BUFFER-ARGUMENTS-INVALID", commandCount_);
        }
        indexBuffer_ = metalRhiNativePayloadFor(buffer);
        if (!indexBuffer_) {
            return commandFail(
                "RHI-METAL-RENDER-BUNDLE-INDEX-BUFFER-PAYLOAD-MISSING", commandCount_);
        }
        indexFormat_ = format;
        indexBufferOffset_ = offsetBytes;
        MetalRhiSecondaryCommand command;
        command.kind = MetalRhiSecondaryCommandKind::BindIndexBuffer;
        command.indexBuffer = indexBuffer_;
        command.indexFormat = indexFormat_;
        command.indexBufferOffset = indexBufferOffset_;
        commands_.push_back(std::move(command));
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult drawIndexed(std::uint32_t indexCount,
        std::uint32_t instanceCount,
        std::uint32_t firstIndex,
        std::int32_t vertexOffset,
        std::uint32_t firstInstance) override
    {
        if (finished_) {
            return commandFail("RHI-METAL-RENDER-BUNDLE-ENCODER-ALREADY-FINISHED", commandCount_);
        }
        if (!executable() || !indexBuffer_ || indexCount == 0u || instanceCount == 0u) {
            return commandFail(
                "RHI-METAL-RENDER-BUNDLE-INDEXED-DRAW-ARGUMENTS-INVALID", commandCount_);
        }
        if (desc_.primitiveTopology == rhi::PrimitiveTopology::PatchList
            && (desc_.patchControlPointCount == 0u || !payload_->tessellationFactorBufferBound
                || vertexOffset != 0 || indexCount % desc_.patchControlPointCount != 0u)) {
            return commandFail(
                "RHI-METAL-RENDER-BUNDLE-PATCH-DRAW-REQUIRES-VALID-TESSELLATION-STATE",
                commandCount_);
        }
        if (desc_.primitiveTopology == rhi::PrimitiveTopology::PatchList) {
            payload_->maxTessellationInstanceCount =
                std::max(payload_->maxTessellationInstanceCount, instanceCount);
        }
        MetalRhiSecondaryCommand command;
        command.kind = MetalRhiSecondaryCommandKind::DrawIndexed;
        command.vertexOrIndexCount = indexCount;
        command.instanceCount = instanceCount;
        command.firstElement = firstIndex;
        command.vertexOffset = vertexOffset;
        command.firstInstance = firstInstance;
        command.indexBuffer = indexBuffer_;
        command.indexFormat = indexFormat_;
        command.indexBufferOffset = indexBufferOffset_;
        if (!payload_->cpuReplay) {
            const auto nativeCommandIndex = payload_->commandCount;
            std::pmr::string diagnostics;
            if (!recordMetalRhiNativeSecondaryDrawIndexed(payload_,
                    indexCount,
                    instanceCount,
                    firstIndex,
                    vertexOffset,
                    firstInstance,
                    indexBuffer_,
                    indexFormat_,
                    indexBufferOffset_,
                    diagnostics)) {
                return commandFail(diagnostics.empty()
                        ? "RHI-METAL-RENDER-BUNDLE-INDEXED-DRAW-PAYLOAD-MISSING"
                        : diagnostics,
                    commandCount_);
            }
            command.nativeCommandIndex = nativeCommandIndex;
        } else if (!reserveCpuReplayDraw()) {
            return commandFail("RHI-METAL-RENDER-BUNDLE-ICB-CAPACITY-EXCEEDED", commandCount_);
        }
        commands_.push_back(std::move(command));
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult draw(std::uint32_t vertexCount,
        std::uint32_t instanceCount,
        std::uint32_t firstVertex,
        std::uint32_t firstInstance) override
    {
        if (finished_) {
            return commandFail("RHI-METAL-RENDER-BUNDLE-ENCODER-ALREADY-FINISHED", commandCount_);
        }
        if (!executable() || vertexCount == 0u || instanceCount == 0u) {
            return commandFail("RHI-METAL-RENDER-BUNDLE-DRAW-ARGUMENTS-INVALID", commandCount_);
        }
        if (desc_.primitiveTopology == rhi::PrimitiveTopology::PatchList
            && (desc_.patchControlPointCount == 0u || !payload_->tessellationFactorBufferBound
                || vertexCount % desc_.patchControlPointCount != 0u
                || firstVertex % desc_.patchControlPointCount != 0u)) {
            return commandFail(
                "RHI-METAL-RENDER-BUNDLE-PATCH-DRAW-REQUIRES-VALID-TESSELLATION-STATE",
                commandCount_);
        }
        if (desc_.primitiveTopology == rhi::PrimitiveTopology::PatchList) {
            payload_->maxTessellationInstanceCount =
                std::max(payload_->maxTessellationInstanceCount, instanceCount);
        }
        MetalRhiSecondaryCommand command;
        command.kind = MetalRhiSecondaryCommandKind::Draw;
        command.vertexOrIndexCount = vertexCount;
        command.instanceCount = instanceCount;
        command.firstElement = firstVertex;
        command.firstInstance = firstInstance;
        if (!payload_->cpuReplay) {
            const auto nativeCommandIndex = payload_->commandCount;
            std::pmr::string diagnostics;
            if (!recordMetalRhiNativeSecondaryDraw(payload_,
                    vertexCount,
                    instanceCount,
                    firstVertex,
                    firstInstance,
                    diagnostics)) {
                return commandFail(diagnostics.empty()
                        ? "RHI-METAL-RENDER-BUNDLE-DRAW-PAYLOAD-MISSING"
                        : diagnostics,
                    commandCount_);
            }
            command.nativeCommandIndex = nativeCommandIndex;
        } else if (!reserveCpuReplayDraw()) {
            return commandFail("RHI-METAL-RENDER-BUNDLE-ICB-CAPACITY-EXCEEDED", commandCount_);
        }
        commands_.push_back(std::move(command));
        return commandOk(++commandCount_);
    }

    arp::core::UniquePtr<rhi::RenderBundle> finish() override
    {
        if (finished_ || !executable() || !attachmentContractValid()) {
            return nullptr;
        }
        finished_ = true;
        return arp::core::makeUnique<MetalRhiRenderBundle>(
            payload_, std::move(desc_), commandCount_, std::move(commands_));
    }

    bool executable() const
    {
        return payload_ && (payload_->cpuReplay || payload_->nativeRuntimeBacked)
            && attachmentContractValid();
    }

private:
    bool attachmentContractValid() const
    {
        if (desc_.sampleCount == 0u
            || (desc_.colorFormats.empty()
                && desc_.depthStencilFormat == rhi::TextureFormat::Unknown)) {
            return false;
        }
        for (const auto format : desc_.colorFormats) {
            if (format == rhi::TextureFormat::Unknown || rhi::isDepthTextureFormat(format)) {
                return false;
            }
        }
        return desc_.depthStencilFormat == rhi::TextureFormat::Unknown
            || rhi::isDepthTextureFormat(desc_.depthStencilFormat);
    }

    bool reserveCpuReplayDraw()
    {
        if (!payload_ || !payload_->cpuReplay
            || executableCommandCount_ >= payload_->maxCommandCount) {
            return false;
        }
        ++executableCommandCount_;
        return true;
    }

    rhi::RenderBundleDesc desc_;
    rhi::semantics::NativeBackendCapabilityContract capabilityContract_;
    std::shared_ptr<MetalRhiNativeSecondaryCommandPayload> payload_;
    std::uint32_t commandCount_ = 0u;
    bool finished_ = false;
    std::shared_ptr<MetalRhiNativeBufferPayload> indexBuffer_;
    rhi::IndexFormat indexFormat_ = rhi::IndexFormat::Uint32;
    std::uint64_t indexBufferOffset_ = 0u;
    std::uint32_t executableCommandCount_ = 0u;
    core::pmr::Vector<MetalRhiSecondaryCommand> commands_{&core::defaultCpuMemoryResource()};
};

rhi::CommandValidationResult MetalRhiRenderPassEncoder::executeRenderBundle(
    const rhi::RenderBundle& bundle)
{
    if (graphicsScope_)
        return commandFail("RHI-METAL-GRAPHICS-SCOPE-SECONDARY-UNSUPPORTED", commandCount_);
    const auto descriptorUse = boundDescriptors_.validateLiveBindings();
    if (!descriptorUse.success)
        return commandFail(descriptorUse.diagnostics, commandCount_);
    const auto* metalBundle = dynamic_cast<const MetalRhiRenderBundle*>(&bundle);
    if (!metalBundle || !nativePayload_) {
        return commandFail("RHI-METAL-RENDER-BUNDLE-CPU-REPLAY-PAYLOAD-INVALID", commandCount_);
    }
    if (!pipelineBound_) {
        return commandFail("RHI-METAL-RENDER-BUNDLE-REQUIRES-BOUND-PIPELINE", commandCount_);
    }
    if (metalBundle->payload() && !metalBundle->payload()->cpuReplay
        && (!boundPipelinePayload_ || !boundPipelinePayload_->supportsIndirectCommands)) {
        return commandFail("RHI-METAL-SECONDARY-PIPELINE-BINDINGS-UNSUPPORTED", commandCount_);
    }
    if (!boundPipelinePayload_
        || boundPipelinePayload_->primitiveTopology != metalBundle->primitiveTopology()) {
        return commandFail("RHI-METAL-RENDER-BUNDLE-PIPELINE-TOPOLOGY-MISMATCH", commandCount_);
    }
    if (metalBundle->primitiveTopology() == rhi::PrimitiveTopology::PatchList
        && metalBundle->patchControlPointCount() != boundPipelinePayload_->patchControlPointCount) {
        return commandFail(
            "RHI-METAL-RENDER-BUNDLE-PIPELINE-PATCH-CONTRACT-MISMATCH", commandCount_);
    }
    if (metalBundle->colorFormats().empty()
        && metalBundle->depthStencilFormat() == rhi::TextureFormat::Unknown) {
        return commandFail("RHI-METAL-RENDER-BUNDLE-ATTACHMENT-CONTRACT-MISSING", commandCount_);
    }
    if (metalBundle->colorFormats().size() != colorFormats_.size()) {
        return commandFail(
            "RHI-METAL-RENDER-BUNDLE-COLOR-ATTACHMENT-COUNT-MISMATCH", commandCount_);
    }
    for (std::size_t index = 0u; index < colorFormats_.size(); ++index) {
        if (metalBundle->colorFormats()[index] != colorFormats_[index]) {
            return commandFail("RHI-METAL-RENDER-BUNDLE-COLOR-FORMAT-MISMATCH", commandCount_);
        }
    }
    if (metalBundle->depthStencilFormat() != depthStencilFormat_
        || metalBundle->sampleCount() != sampleCount_ || sampleCount_ == 0u) {
        return commandFail(
            "RHI-METAL-RENDER-BUNDLE-ATTACHMENT-SAMPLE-CONTRACT-MISMATCH", commandCount_);
    }
    if (metalBundle->commandCount() != metalBundle->commands().size()) {
        return commandFail("RHI-METAL-RENDER-BUNDLE-COMMAND-COUNT-MISMATCH", commandCount_);
    }
    if (metalBundle->commandCount() != 0u) {
        rhi::semantics::CommandWorkloadDesc workload;
        workload.commandCount = metalBundle->commandCount();
        workload.allowCpuReplay = metalBundle->payload() && metalBundle->payload()->cpuReplay;
        workload.usesRenderPass = true;
        workload.usesRenderBundle = true;
        const auto lowering = rhi::semantics::lowerCommandWorkload(
            capabilityContract_, workload, rhi::semantics::CommandRecordingIntent::RenderBundle);
        const auto loweringUse = rhi::semantics::validateNativeCommandLoweringUse(lowering,
            metalBundle->payload() && metalBundle->payload()->cpuReplay
                ? rhi::semantics::NativeCommandSemantic::MetalRenderBundleReplay
                : rhi::semantics::NativeCommandSemantic::MetalRenderBundleIndirectCommandBuffer,
            metalBundle->payload() && metalBundle->payload()->cpuReplay
                ? rhi::FallbackMode::CpuReplay
                : rhi::FallbackMode::Native,
            "Metal render bundle");
        if (!loweringUse.success) {
            return commandFail(loweringUse.diagnostics, commandCount_);
        }
    }
    {
        std::pmr::string diagnostics;
        if (!preflightMetalReplayCommands(metalBundle->commands(),
                nullptr,
                metalBundle->payload(),
                pipelineLayout_,
                shaderReflection_,
                pushState_,
                metalBundle->payload() && metalBundle->payload()->cpuReplay,
                diagnostics)) {
            return commandFail(diagnostics, commandCount_);
        }
    }
    MetalRhiReplayTransaction replayTransaction(nativePayload_);
    auto replayPushState = pushState_;
    if (metalBundle->payload() && metalBundle->payload()->nativeRuntimeBacked) {
        if (metalBundle->payload()->commandCount == 0u) {
            commandCount_ += metalBundle->commandCount();
            replayTransaction.commit();
            return commandOk(commandCount_);
        }
        std::pmr::string diagnostics;
        if (metalBundle->payload()->tessellationFactorBufferBound
            && !nativePayload_->recordTessellationFactorBufferBind(
                metalBundle->payload()->tessellationFactorBuffer,
                metalBundle->payload()->tessellationFactorBufferOffset,
                metalBundle->payload()->tessellationFactorInstanceStrideBytes,
                diagnostics)) {
            return commandFail(diagnostics.empty()
                    ? "RHI-METAL-RENDER-BUNDLE-TESSELLATION-FACTOR-BUFFER-PAYLOAD-MISSING"
                    : diagnostics,
                commandCount_);
        }
        if (!nativePayload_->executeSecondary(metalBundle->payload(), diagnostics)) {
            return commandFail(
                diagnostics.empty() ? "RHI-METAL-RENDER-BUNDLE-ICB-EXECUTION-FAILED" : diagnostics,
                commandCount_);
        }
        for (const auto& command : metalBundle->commands()) {
            if (command.kind == MetalRhiSecondaryCommandKind::Draw
                || command.kind == MetalRhiSecondaryCommandKind::DrawIndexed) {
                replayPushState.onConsume();
            }
        }
        pushState_ = replayPushState;
        commandCount_ += metalBundle->commandCount();
        replayTransaction.commit();
        return commandOk(commandCount_);
    }
    bool bundleIndexBufferBound = false;
    for (const auto& command : metalBundle->commands()) {
        std::pmr::string diagnostics;
        switch (command.kind) {
        case MetalRhiSecondaryCommandKind::BindIndexBuffer:
            if (!command.indexBuffer
                || !nativePayload_->recordIndexBufferBind(command.indexBuffer,
                    command.indexFormat,
                    command.indexBufferOffset,
                    diagnostics)) {
                return commandFail(diagnostics.empty()
                        ? "RHI-METAL-RENDER-BUNDLE-INDEX-BUFFER-PAYLOAD-MISSING"
                        : diagnostics,
                    commandCount_);
            }
            bundleIndexBufferBound = true;
            break;
        case MetalRhiSecondaryCommandKind::BindTessellationFactorBuffer:
            if (!command.tessellationFactorBuffer
                || !nativePayload_->recordTessellationFactorBufferBind(
                    command.tessellationFactorBuffer,
                    command.tessellationFactorBufferOffset,
                    command.tessellationFactorInstanceStrideBytes,
                    diagnostics)) {
                return commandFail(diagnostics.empty()
                        ? "RHI-METAL-RENDER-BUNDLE-TESSELLATION-FACTOR-BUFFER-PAYLOAD-MISSING"
                        : diagnostics,
                    commandCount_);
            }
            break;
        case MetalRhiSecondaryCommandKind::DrawIndexed:
            if (!bundleIndexBufferBound) {
                return commandFail(
                    "RHI-METAL-RENDER-BUNDLE-INDEXED-DRAW-REQUIRES-INDEX-BUFFER", commandCount_);
            }
            if (!nativePayload_->recordDrawIndexed(command.vertexOrIndexCount,
                    command.instanceCount,
                    command.firstElement,
                    command.vertexOffset,
                    command.firstInstance,
                    diagnostics)) {
                return commandFail(diagnostics, commandCount_);
            }
            replayPushState.onConsume();
            break;
        case MetalRhiSecondaryCommandKind::Draw:
            if (!nativePayload_->recordDraw(command.vertexOrIndexCount,
                    command.instanceCount,
                    command.firstElement,
                    command.firstInstance,
                    diagnostics)) {
                return commandFail(diagnostics, commandCount_);
            }
            replayPushState.onConsume();
            break;
        default:
            return commandFail("RHI-METAL-RENDER-BUNDLE-COMMAND-KIND-INVALID", commandCount_);
        }
    }
    pushState_ = replayPushState;
    if (bundleIndexBufferBound) {
        indexBufferBound_ = true;
    }
    commandCount_ += metalBundle->commandCount();
    replayTransaction.commit();
    return commandOk(commandCount_);
}

} // namespace

const char* MetalOriginalOrdinaryCommand::inspect(const std::shared_ptr<const Program>& program,
    const rhi::GpuDevice& device,
    const rhi::OriginalRenderPassPreparationRequest& request) noexcept
{
    if (const auto* id = metalRhiOriginalSubmissionOwnerDiagnostic(device))
        return id;
    if (!metalRhiOriginalSubmissionDomain(device))
        return "RHI-SUBMISSION-COMPLETION-UNPREPARED";
    if (const auto* id = rhi::OriginalRenderPassPreparationContract::validate(request))
        return id;
    if (request.contractVersion != 6u || device.backend() != rhi::Backend::Metal)
        return "RHI-METAL-ORDINARY-PREPARATION-PROFILE";
    if (const auto* id = Program::validateOriginal(program))
        return id;
    if (!program->recordingBudget())
        return "RHI-METAL-ORDINARY-RECORDING-BUDGET";
    const auto& sources =
        program->originalInputs()->originalScalars()->originalUsage()->originalSources();
    if (const auto* id =
            rhi::OriginalCommandResourceSources::validateRegisteredLifetime(sources, device))
        return id;
    const rhi::OriginalComputeDispatchRecordingInput admission{device, request.dispatchDomain};
    const auto checked = rhi::OriginalRenderPassRecordingContract::inspect(program,
        request.storage,
        *request.content,
        *request.recording,
        request.dispatchDomain ? &admission : nullptr);
    if (checked.diagnostic)
        return checked.diagnostic;
    const auto passes = program->counts().renderPasses;
    if (const auto* id =
            rhi::OriginalRenderPassPreparationContract::inspectWrapperCapacity(request, passes))
        return id;
    // One command buffer, one reusable sequential encoder slot, one completion
    // storage object and one immutable native descriptor per literal render pass.
    if (request.maxNativeObjects < 3u || passes > request.maxNativeObjects - 3u)
        return "RHI-METAL-ORDINARY-NATIVE-OBJECT-CAPACITY";
    for (const auto& operation : program->operations())
        if (operation.kind == K::Dispatch && !request.dispatchDomain)
            return "RHI-DISPATCH-RECORDING-ADMISSION-INPUT";
    return nullptr;
}

rhi::CommandRecordingUsage MetalOriginalOrdinaryCommand::increment(
    std::size_t command) const noexcept
{
    const auto& operation = program_->operations()[command];
    const auto& args = *program_->originalInputs()->originalScalars()->originalArguments();
    const auto& record = args.records()[command];
    rhi::CommandRecordingUsage value;
    (void)value.appendArguments({record.scalar,
        args.text(command),
        args.offsets(command),
        static_cast<std::uint32_t>(record.offsetCount),
        args.bytes(command),
        static_cast<std::uint32_t>(record.byteCount)});
    value.resourceReferences = operation.referenceCount;
    if (operation.kind == K::BindRenderDescriptor || operation.kind == K::BindComputeDescriptor)
        if (const auto* source =
                rhi::OriginalReadOnlyGraphicsPassProjection::descriptor(*program_, command))
            value.descriptorElements = source->frozenElements().size();
    return value;
}

const rhi::OriginalCommandTextureViewSource& MetalOriginalOrdinaryCommand::attachmentSource(
    const Attachment& attachment) const
{
    const auto& operation = program_->operations()[attachment.command];
    const auto& usage = *program_->originalInputs()->originalScalars()->originalUsage();
    const auto index =
        usage.references()[operation.firstReference + attachment.operand].sourceIndex;
    return *std::get<std::shared_ptr<const rhi::OriginalCommandTextureViewSource>>(
        usage.originalSources()->sources()[index]);
}

const char* MetalOriginalOrdinaryCommand::validate() const noexcept
{
    if (owner_ != std::this_thread::get_id())
        return "RHI-SUBMISSION-COMPLETION-THREAD";
    if (failure_)
        return failure_;
    const auto check = [&]() noexcept -> const char* {
        if (!originalMetadataQuota()
            || originalMetadataQuota()->limitBytes()
                != program_->originalBudget().maxCpuMetadataBytes)
            return "RHI-METAL-ORDINARY-METADATA-QUOTA";
        const auto& sources =
            program_->originalInputs()->originalScalars()->originalUsage()->originalSources();
        if (const auto* id = rhi::OriginalCommandResourceSources::validateOriginal(sources))
            return id;
        for (const auto& dispatch : dispatches_)
            if (const auto* id = dispatch.frozen.diagnostic())
                return id;
        for (const auto& pass : passes_)
            if (const auto* id = validateMetalRhiNativeRenderPassResources(
                    pass.native, registry_, nativeDevice_))
                return id;
        return nullptr;
    };
    failure_ = check();
    return failure_;
}

const char* MetalOriginalOrdinaryCommand::preflight(K kind, std::uint32_t command) const noexcept
{
    if (owner_ != std::this_thread::get_id())
        return "RHI-SUBMISSION-COMPLETION-THREAD";
    if (terminal_ || sealed_ || !journal_ || !command_ || command_->recordingPoisoned_
        || command_->abandoned_ || command_->readbackWasSubmitted()
        || command != meter_.commandCount() || command >= program_->operations().size()
        || program_->operations()[command].kind != kind)
        return "RHI-METAL-ORDINARY-RECORDING-ORDINAL";
    if (const auto* id = validate())
        return id;
    return meter_.preflight(increment(command));
}

void MetalOriginalOrdinaryCommand::consume() noexcept
{
    (void)meter_.consume(increment(static_cast<std::size_t>(meter_.commandCount())));
}

const char* MetalOriginalOrdinaryCommand::claim() noexcept
{
    if (owner_ != std::this_thread::get_id())
        return "RHI-SUBMISSION-COMPLETION-THREAD";
    if (claimed_)
        return validatePending();
    if (const auto* id = validate())
        return id;
    if (const auto* id = visitBuffers([&](const auto& object) noexcept -> const char* {
            const auto native = metalRhiNativePayloadFor(object);
            if (!native || !native->originalHostDefinedBytesMatch(object))
                return "RHI-METAL-ORDINARY-BUFFER-INITIALIZATION";
            return rhi::OriginalCommandStateTransaction::preflightOrdinaryInitializedBuffer(
                object, *journal_);
        }))
        return id;
    for (const auto& attachment : attachments_) {
        const auto& object = *attachmentSource(attachment).originalParentLease()->originalObject();
        if (const auto* id = object.validateOriginalCommittedState(attachment.initialState))
            return id;
        if (const auto* id = object.validateOriginalContent(attachment.initialContent))
            return id;
        if (attachment.claimOwner)
            if (const auto* id = rhi::OriginalCommandStateTransaction::preflightOrdinaryAttachment(
                    object, attachment.initialContent, *journal_))
                return id;
    }
    if (const auto* id = visitBuffers([&](const auto& object) noexcept {
            return rhi::OriginalCommandStateTransaction::claimOrdinaryInitializedBuffer(
                object, *journal_);
        }))
        return id;
    for (auto& attachment : attachments_)
        if (attachment.claimOwner)
            if (const auto* id = rhi::OriginalCommandStateTransaction::proposeOrdinaryAttachment(
                    *attachmentSource(attachment).originalParentLease()->originalObject(),
                    attachment.initialContent,
                    attachment.finalStored,
                    *journal_,
                    attachment.proposed,
                    attachment.pending))
                return id;
    claimed_ = true;
    return validatePending();
}

const char* MetalOriginalOrdinaryCommand::validatePending() const noexcept
{
    if (const auto* id = validate())
        return id;
    if (!claimed_ || !journal_)
        return "RHI-METAL-ORDINARY-CONTENT-UNCLAIMED";
    if (const auto* id = visitBuffers([&](const auto& object) noexcept -> const char* {
            return rhi::OriginalCommandStateTransaction::ordinaryInitializedBufferPending(
                       object, *journal_)
                ? nullptr
                : "RHI-METAL-ORDINARY-BUFFER-PENDING";
        }))
        return id;
    for (const auto& attachment : attachments_)
        if (attachment.claimOwner) {
            if (!attachment.pending)
                return "RHI-METAL-ORDINARY-CONTENT-PENDING";
            if (const auto* id =
                    rhi::OriginalCommandStateTransaction::validateOrdinaryAttachmentPending(
                        *attachmentSource(attachment).originalParentLease()->originalObject(),
                        attachment.initialContent,
                        *journal_,
                        attachment.proposed,
                        *attachment.pending))
                return id;
        }
    return journal_->canInvalidate() ? nullptr : "RHI-METAL-ORDINARY-INVALIDATION-UNAVAILABLE";
}

bool MetalOriginalOrdinaryCommand::beforeSubmit(
    const MetalRhiNativeCommandPayload& native, core::pmr::String& diagnostic) noexcept
{
    if (owner_ != std::this_thread::get_id()) {
        diagnostic = "RHI-SUBMISSION-COMPLETION-THREAD";
        return false;
    }
    if (&native != command_ || terminal_ || !sealed_
        || meter_.commandCount() != program_->operations().size()) {
        diagnostic = "RHI-METAL-ORDINARY-RECORDING-INCOMPLETE";
        return false;
    }
    if (const auto* id = validatePending()) {
        diagnostic = id;
        return false;
    }
    return true;
}

const char* MetalOriginalOrdinaryCommand::beforeNativeCommit() noexcept
{
    if (owner_ != std::this_thread::get_id())
        return "RHI-SUBMISSION-COMPLETION-THREAD";
    return terminal_ || !sealed_ ? "RHI-METAL-ORDINARY-RECORDING-INCOMPLETE" : validatePending();
}

bool MetalOriginalOrdinaryCommand::nativeCompleted(bool success) noexcept
{
    if (terminal_ || !journal_)
        return false;
    if (success)
        success = sealed_ && !validatePending();
    if (success)
        for (const auto& attachment : attachments_)
            if (attachment.claimOwner)
                if (rhi::OriginalCommandStateTransaction::completeOrdinaryAttachment(
                        *attachmentSource(attachment).originalParentLease()->originalObject(),
                        attachment.initialContent,
                        *journal_,
                        attachment.proposed,
                        *attachment.pending)) {
                    success = false;
                    break;
                }
    terminal_ = true;
    if (success)
        journal_->commit();
    else if (!journal_->invalidate())
        journal_->discard();
    return success;
}

void MetalOriginalOrdinaryCommand::abandonUnsubmitted() noexcept
{
    if (owner_ != std::this_thread::get_id())
        std::terminate();
    if (terminal_)
        return;
    terminal_ = true;
    if (encoderSlot_)
        encoderSlot_->reset();
    if (journal_)
        journal_->rollback();
}

core::MutationJournal* MetalRhiNativeCommandPayload::originalOrdinaryJournal() const noexcept
{
    return originalOrdinary_ ? originalOrdinary_->journal_.get() : nullptr;
}

bool MetalRhiNativeCommandPayload::acceptOriginalOrdinaryOwnership() const noexcept
{
    return originalOrdinary_ && originalOrdinary_->sealed_ && readbackWasSubmitted()
        && !submissionFailed_.load(std::memory_order_acquire) && publishOriginalQueueAcceptance();
}

const char* MetalOriginalQueueAcceptance::validateOriginal(
    const std::shared_ptr<const MetalOriginalQueueAcceptance>& observation) noexcept
{
    if (!observation
        || !core::SharedOwnerProjection::sameOwner(observation, observation->self_.lock()))
        return "RHI-QUEUE-ACCEPTANCE-OWNER";
    return observation->diagnostic();
}

const char* MetalOriginalQueueAcceptance::diagnostic() const noexcept
{
    if (!published_.load(std::memory_order_acquire) || !nativeAccepted_ || !ordinal_)
        return "RHI-QUEUE-ACCEPTANCE-NOT-ACCEPTED";
    if (const auto* id = device_.diagnostic())
        return id;
    if (!queue_ || registry_.expired() || !completion_)
        return "RHI-QUEUE-ACCEPTANCE-OWNER";
    return completion_->outcome() == MetalRhiCompletionState::Outcome::Failed
        ? "RHI-QUEUE-ACCEPTANCE-COMPLETION-FAILED"
        : nullptr;
}

const char* MetalOriginalQueueAcceptance::precedes(
    const MetalOriginalQueueAcceptance& successor) const noexcept
{
    if (const auto* id = diagnostic())
        return id;
    if (const auto* id = successor.diagnostic())
        return id;
    if (!core::SharedOwnerProjection::sameOwner(queue_, successor.queue_)
        || !core::SharedOwnerProjection::sameOwner(registry_.lock(), successor.registry_.lock()))
        return "RHI-QUEUE-ACCEPTANCE-FOREIGN-QUEUE";
    return ordinal_ < successor.ordinal_ ? nullptr : "RHI-QUEUE-ACCEPTANCE-ORDER";
}

bool MetalOriginalQueueAcceptance::matchesProgram(
    const std::shared_ptr<const rhi::OriginalCommandStreamProgram>& program) const noexcept
{
    const auto original = program_.lock();
    return !diagnostic() && original && core::SharedOwnerProjection::sameOwner(original, program);
}

bool MetalRhiNativeCommandPayload::rejectOriginalOrdinary(
    std::string_view message, core::pmr::String& diagnostic)
{
    diagnostic = message;
    if (originalOrdinary_ && originalOrdinary_->owner_ != std::this_thread::get_id())
        return false;
    poisonRecording(diagnostic);
    return false;
}

bool MetalRhiNativeCommandPayload::recordOriginalOrdinaryScope(rhi::CommandUsageKind kind,
    std::uint32_t command,
    const rhi::RenderPassDesc* incoming,
    core::pmr::String& diagnostic,
    std::string_view label)
{
    using K = rhi::CommandUsageKind;
    if (!originalOrdinary_)
        return rejectOriginalOrdinary("RHI-METAL-ORDINARY-OWNER-MISSING", diagnostic);
    auto& owner = *originalOrdinary_;
    if (const auto* id = owner.preflight(kind, command))
        return rejectOriginalOrdinary(id, diagnostic);
    if ((kind == K::BeginComputePass || kind == K::BeginTransferPass)
        && owner.program_->originalInputs()->originalScalars()->originalArguments()->text(command)
            != label)
        return rejectOriginalOrdinary("RHI-METAL-ORDINARY-PASS-LABEL", diagnostic);
    if (const auto* id = owner.claim())
        return rejectOriginalOrdinary(id, diagnostic);
    bool recorded = false;
    if (kind == K::BeginRenderPass) {
        const auto& args = std::get<std::shared_ptr<const rhi::RenderPassArguments>>(
            *owner.program_->originalInputs()->descriptorAt(command));
        if (!incoming || !args->matches(*incoming))
            return rejectOriginalOrdinary("RHI-METAL-ORDINARY-PASS-INPUT", diagnostic);
        const auto pass =
            std::find_if(owner.passes_.begin(), owner.passes_.end(), [&](const auto& value) {
                return value.command == command;
            });
        if (pass == owner.passes_.end())
            return rejectOriginalOrdinary("RHI-METAL-ORDINARY-PASS-STORAGE", diagnostic);
        for (const auto& attachment : owner.attachments_)
            if (attachment.command == command) {
                const auto required = attachment.operand < incoming->colorAttachments.size()
                    ? rhi::ResourceState::ColorAttachment
                    : rhi::ResourceState::DepthAttachment;
                if (!rhi::OriginalCommandStateTransaction::ordinaryAttachmentState(
                        *owner.attachmentSource(attachment).originalParentLease()->originalObject(),
                        attachment.initialContent.range(),
                        required,
                        *owner.journal_))
                    return rejectOriginalOrdinary("RHI-METAL-ORDINARY-PASS-STATE", diagnostic);
            }
        recorded = encodeOriginalOrdinary([&] {
            return beginPreparedNativeRenderPass(pass->native, owner.encoderSlot_, diagnostic);
        });
    } else if (incoming) {
        return rejectOriginalOrdinary("RHI-METAL-ORDINARY-PASS-INPUT", diagnostic);
    } else {
        recorded = encodeOriginalOrdinary([&] {
            switch (kind) {
            case K::EndRenderPass:
                return recordRenderPassEnd(diagnostic);
            case K::BeginComputePass:
                return beginPreparedNativeComputePass(owner.encoderSlot_, diagnostic);
            case K::EndComputePass:
                return recordComputePassEnd(diagnostic);
            case K::BeginTransferPass:
                return beginPreparedNativeBlitPass(owner.encoderSlot_, diagnostic);
            case K::EndTransferPass:
                return recordBlitPassEnd(diagnostic);
            case K::Finish:
                if (command + 1u != owner.program_->operations().size())
                    return false;
                return finish(diagnostic);
            default:
                return false;
            }
        });
    }
    if (!recorded)
        return rejectOriginalOrdinary(
            diagnostic.empty() ? "RHI-METAL-ORDINARY-NATIVE-SCOPE" : std::string_view(diagnostic),
            diagnostic);
    if (kind == K::EndRenderPass || kind == K::EndComputePass || kind == K::EndTransferPass)
        owner.encoderSlot_->reset();
    owner.computePipeline_ = nullptr;
    owner.graphicsPipeline_ = nullptr;
    owner.descriptor_ = nullptr;
    owner.viewport_ = owner.scissor_ = false;
    owner.consume();
    if (kind == K::Finish) {
        owner.meter_.seal();
        owner.sealed_ = true;
    }
    return true;
}

bool MetalRhiNativeCommandPayload::recordOriginalOrdinaryPipeline(
    std::uint32_t command, const rhi::ComputePipeline& pipeline, core::pmr::String& diagnostic)
{
    constexpr auto kind = rhi::CommandUsageKind::BindComputePipeline;
    if (!originalOrdinary_)
        return rejectOriginalOrdinary("RHI-METAL-ORDINARY-OWNER-MISSING", diagnostic);
    auto& owner = *originalOrdinary_;
    if (const auto* id = owner.preflight(kind, command))
        return rejectOriginalOrdinary(id, diagnostic);
    const auto& binding = std::get<rhi::OriginalCommandPipelineBinding>(
        *owner.program_->originalInputs()->descriptorAt(command));
    const auto& source = std::get<std::shared_ptr<const rhi::OriginalCommandComputePipelineSource>>(
        owner.program_->originalInputs()
            ->originalScalars()
            ->originalUsage()
            ->originalSources()
            ->sources()[binding.sourceIndex]);
    if (source->originalObject() != &pipeline)
        return rejectOriginalOrdinary("RHI-METAL-ORDINARY-PIPELINE-INPUT", diagnostic);
    const auto native = metalRhiNativePayloadFor(pipeline);
    if (!native || !encodeOriginalOrdinary([&] {
            return recordComputePipelineBind(native, diagnostic);
        }))
        return rejectOriginalOrdinary(diagnostic.empty() ? "RHI-METAL-ORDINARY-NATIVE-PIPELINE"
                                                         : std::string_view(diagnostic),
            diagnostic);
    owner.computePipeline_ = &pipeline;
    owner.descriptor_ = nullptr;
    owner.consume();
    return true;
}

bool MetalRhiNativeCommandPayload::recordOriginalOrdinaryPipeline(
    std::uint32_t command, const rhi::RenderPipeline& pipeline, core::pmr::String& diagnostic)
{
    constexpr auto kind = rhi::CommandUsageKind::BindGraphicsPipeline;
    if (!originalOrdinary_)
        return rejectOriginalOrdinary("RHI-METAL-ORDINARY-OWNER-MISSING", diagnostic);
    auto& owner = *originalOrdinary_;
    if (const auto* id = owner.preflight(kind, command))
        return rejectOriginalOrdinary(id, diagnostic);
    const auto& binding = std::get<rhi::OriginalCommandPipelineBinding>(
        *owner.program_->originalInputs()->descriptorAt(command));
    const auto& source =
        std::get<std::shared_ptr<const rhi::OriginalCommandGraphicsPipelineSource>>(
            owner.program_->originalInputs()
                ->originalScalars()
                ->originalUsage()
                ->originalSources()
                ->sources()[binding.sourceIndex]);
    if (source->originalObject() != &pipeline)
        return rejectOriginalOrdinary("RHI-METAL-ORDINARY-PIPELINE-INPUT", diagnostic);
    const auto native = metalRhiNativePayloadFor(pipeline);
    if (!native || !encodeOriginalOrdinary([&] {
            return recordGraphicsPipelineBind(native, diagnostic);
        }))
        return rejectOriginalOrdinary(diagnostic.empty() ? "RHI-METAL-ORDINARY-NATIVE-PIPELINE"
                                                         : std::string_view(diagnostic),
            diagnostic);
    owner.graphicsPipeline_ = &pipeline;
    owner.descriptor_ = nullptr;
    owner.viewport_ = owner.scissor_ = false;
    owner.consume();
    return true;
}

bool MetalRhiNativeCommandPayload::recordOriginalOrdinaryDescriptor(std::uint32_t command,
    std::uint32_t set,
    const rhi::DescriptorSet& descriptor,
    const std::uint32_t* offsets,
    std::uint32_t offsetCount,
    core::pmr::String& diagnostic)
{
    if (!originalOrdinary_)
        return rejectOriginalOrdinary("RHI-METAL-ORDINARY-OWNER-MISSING", diagnostic);
    auto& owner = *originalOrdinary_;
    const bool compute = bool(nativeComputeEncoder_);
    const auto kind = compute ? rhi::CommandUsageKind::BindComputeDescriptor
                              : rhi::CommandUsageKind::BindRenderDescriptor;
    if (const auto* id = owner.preflight(kind, command))
        return rejectOriginalOrdinary(id, diagnostic);
    const auto& binding = std::get<rhi::OriginalCommandDescriptorBinding>(
        *owner.program_->originalInputs()->descriptorAt(command));
    const auto& sources = owner.program_->originalInputs()
                              ->originalScalars()
                              ->originalUsage()
                              ->originalSources()
                              ->sources();
    const auto& source = std::get<std::shared_ptr<const rhi::OriginalCommandDescriptorSetSource>>(
        sources[binding.sourceIndex]);
    const auto& scalar = owner.program_->originalInputs()
                             ->originalScalars()
                             ->originalArguments()
                             ->records()[command]
                             .scalar;
    if (offsets || offsetCount || source->originalObject() != &descriptor
        || set != source->frozenLogicalSet()
        || scalar.get<rhi::CommandScalarArguments::DescriptorBinding>()->set != set)
        return rejectOriginalOrdinary("RHI-METAL-ORDINARY-DESCRIPTOR-INPUT", diagnostic);
    if (compute ? std::get<std::shared_ptr<const rhi::OriginalCommandComputePipelineSource>>(
                      sources[binding.pipelineSourceIndex])
                      ->originalObject()
                != owner.computePipeline_
                : std::get<std::shared_ptr<const rhi::OriginalCommandGraphicsPipelineSource>>(
                      sources[binding.pipelineSourceIndex])
                      ->originalObject()
                != owner.graphicsPipeline_)
        return rejectOriginalOrdinary("RHI-METAL-ORDINARY-DESCRIPTOR-PIPELINE", diagnostic);
    const auto native = metalRhiOriginalSynchronousPayloadFor(source);
    if (!native || !encodeOriginalOrdinary([&] {
            return recordDescriptorSetBindContents(set,
                native,
                compute ? rhi::ShaderStage::Compute
                        : rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment,
                nullptr,
                0u,
                diagnostic,
                true);
        }))
        return rejectOriginalOrdinary(diagnostic.empty() ? "RHI-METAL-ORDINARY-NATIVE-DESCRIPTOR"
                                                         : std::string_view(diagnostic),
            diagnostic);
    owner.descriptor_ = &descriptor;
    owner.consume();
    return true;
}

bool MetalRhiNativeCommandPayload::recordOriginalOrdinaryScalar(
    std::uint32_t command, const rhi::CommandScalarArguments& scalar, core::pmr::String& diagnostic)
{
    using K = rhi::CommandUsageKind;
    if (!originalOrdinary_)
        return rejectOriginalOrdinary("RHI-METAL-ORDINARY-OWNER-MISSING", diagnostic);
    auto& owner = *originalOrdinary_;
    if (const auto* id = owner.preflight(scalar.kind(), command))
        return rejectOriginalOrdinary(id, diagnostic);
    const auto& expected = owner.program_->originalInputs()
                               ->originalScalars()
                               ->originalArguments()
                               ->records()[command];
    if (!expected.scalar.matches(scalar) || expected.offsetCount || expected.byteCount)
        return rejectOriginalOrdinary("RHI-METAL-ORDINARY-SCALAR-INPUT", diagnostic);
    if (const auto* id = owner.validatePending())
        return rejectOriginalOrdinary(id, diagnostic);
    const bool recorded = encodeOriginalOrdinary([&] {
        switch (scalar.kind()) {
        case K::SetViewport:
            return owner.graphicsPipeline_
                && recordSetViewport(*scalar.get<rhi::ViewportState>(), diagnostic);
        case K::SetScissor:
            return owner.graphicsPipeline_
                && recordSetScissor(*scalar.get<rhi::ScissorState>(), diagnostic);
        case K::Draw: {
            const auto& draw = *scalar.get<rhi::CommandScalarArguments::Draw>();
            return owner.graphicsPipeline_ && owner.descriptor_ && owner.viewport_ && owner.scissor_
                && recordDraw(draw.vertices,
                    draw.instances,
                    draw.firstVertex,
                    draw.firstInstance,
                    diagnostic);
        }
        case K::Dispatch: {
            const auto& groups = *scalar.get<rhi::CommandScalarArguments::Dispatch>();
            const auto found = std::find_if(
                owner.dispatches_.begin(), owner.dispatches_.end(), [&](const auto& value) {
                    return value.command == command;
                });
            if (!owner.computePipeline_ || !owner.descriptor_ || found == owner.dispatches_.end())
                return false;
            const auto& source =
                std::get<std::shared_ptr<const rhi::OriginalCommandComputePipelineSource>>(
                    owner.program_->originalInputs()
                        ->originalScalars()
                        ->originalUsage()
                        ->originalSources()
                        ->sources()[found->pipelineSource]);
            if (source->originalObject() != owner.computePipeline_
                || found->frozen.diagnostic(source, {groups.x, groups.y, groups.z}))
                return false;
            const auto& local =
                source->originalPipeline()->originalComputeCreation()->definition().computeShader;
            if (!computePipeline_
                || computePipeline_->computeWorkgroupSizeX != local.computeWorkgroupSizeX
                || computePipeline_->computeWorkgroupSizeY != local.computeWorkgroupSizeY
                || computePipeline_->computeWorkgroupSizeZ != local.computeWorkgroupSizeZ)
                return false;
            return recordDispatch(groups.x, groups.y, groups.z, diagnostic);
        }
        default:
            return false;
        }
    });
    if (!recorded)
        return rejectOriginalOrdinary(
            diagnostic.empty() ? "RHI-METAL-ORDINARY-NATIVE-SCALAR" : std::string_view(diagnostic),
            diagnostic);
    if (scalar.kind() == K::SetViewport)
        owner.viewport_ = true;
    if (scalar.kind() == K::SetScissor)
        owner.scissor_ = true;
    owner.consume();
    return true;
}

bool MetalRhiNativeCommandPayload::recordOriginalOrdinaryTransition(std::uint32_t command,
    rhi::GpuBuffer& object,
    const rhi::ResourceBarrierDesc& desc,
    core::pmr::String& diagnostic)
{
    constexpr auto kind = rhi::CommandUsageKind::BufferBarrier;
    if (!originalOrdinary_)
        return rejectOriginalOrdinary("RHI-METAL-ORDINARY-OWNER-MISSING", diagnostic);
    auto& owner = *originalOrdinary_;
    if (const auto* id = owner.preflight(kind, command))
        return rejectOriginalOrdinary(id, diagnostic);
    const auto& operation = owner.program_->operations()[command];
    const auto* input = owner.program_->originalInputs()->descriptorAt(command);
    const auto* expected = input ? std::get_if<rhi::ResourceDependencyArguments>(input) : nullptr;
    const auto& usage = *owner.program_->originalInputs()->originalScalars()->originalUsage();
    if (!expected || !expected->matches(kind, desc) || operation.referenceCount != 1u
        || std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(
               usage.originalSources()
                   ->sources()[usage.references()[operation.firstReference].sourceIndex])
                ->originalObject()
            != &object)
        return rejectOriginalOrdinary("RHI-METAL-ORDINARY-BARRIER-INPUT", diagnostic);
    if (const auto* id = owner.claim())
        return rejectOriginalOrdinary(id, diagnostic);
    if (!rhi::OriginalCommandStateTransaction::ordinaryBufferState(
            object, desc.before, *owner.journal_))
        return rejectOriginalOrdinary("RHI-METAL-ORDINARY-BARRIER-STATE", diagnostic);
    if (!encodeOriginalOrdinary([&] {
            return recordBufferBarrier(diagnostic);
        }))
        return rejectOriginalOrdinary(diagnostic, diagnostic);
    const auto changed = setMetalRhiBufferStateForBarrier(object, desc, owner.journal_.get());
    if (!changed.success)
        return rejectOriginalOrdinary(changed.diagnostics, diagnostic);
    owner.consume();
    return true;
}

bool MetalRhiNativeCommandPayload::recordOriginalOrdinaryTransition(std::uint32_t command,
    rhi::GpuTexture& object,
    const rhi::ResourceBarrierDesc& desc,
    core::pmr::String& diagnostic)
{
    constexpr auto kind = rhi::CommandUsageKind::TextureBarrier;
    if (!originalOrdinary_)
        return rejectOriginalOrdinary("RHI-METAL-ORDINARY-OWNER-MISSING", diagnostic);
    auto& owner = *originalOrdinary_;
    if (const auto* id = owner.preflight(kind, command))
        return rejectOriginalOrdinary(id, diagnostic);
    const auto& operation = owner.program_->operations()[command];
    const auto* input = owner.program_->originalInputs()->descriptorAt(command);
    const auto* expected = input ? std::get_if<rhi::ResourceDependencyArguments>(input) : nullptr;
    const auto& usage = *owner.program_->originalInputs()->originalScalars()->originalUsage();
    if (!expected || !expected->matches(kind, desc) || operation.referenceCount != 1u
        || std::get<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(
               usage.originalSources()
                   ->sources()[usage.references()[operation.firstReference].sourceIndex])
                ->originalObject()
            != &object)
        return rejectOriginalOrdinary("RHI-METAL-ORDINARY-BARRIER-INPUT", diagnostic);
    if (const auto* id = owner.claim())
        return rejectOriginalOrdinary(id, diagnostic);
    if (!rhi::OriginalCommandStateTransaction::ordinaryAttachmentState(
            object, desc.textureSubresources, desc.before, *owner.journal_))
        return rejectOriginalOrdinary("RHI-METAL-ORDINARY-BARRIER-STATE", diagnostic);
    if (!encodeOriginalOrdinary([&] {
            return recordTextureBarrier(diagnostic);
        }))
        return rejectOriginalOrdinary(
            diagnostic.empty() ? "RHI-METAL-ORDINARY-BARRIER-CLAIM" : std::string_view(diagnostic),
            diagnostic);
    if (const auto* id = rhi::OriginalCommandStateTransaction::transitionOrdinaryAttachment(
            object, desc, *owner.journal_))
        return rejectOriginalOrdinary(id, diagnostic);
    owner.consume();
    return true;
}

bool MetalRhiNativeCommandPayload::recordOriginalOrdinaryCopy(
    std::uint32_t command, const rhi::TextureToBufferCopyDesc& desc, core::pmr::String& diagnostic)
{
    constexpr auto kind = rhi::CommandUsageKind::TextureToBufferCopy;
    if (!originalOrdinary_)
        return rejectOriginalOrdinary("RHI-METAL-ORDINARY-OWNER-MISSING", diagnostic);
    auto& owner = *originalOrdinary_;
    if (const auto* id = owner.preflight(kind, command))
        return rejectOriginalOrdinary(id, diagnostic);
    const auto* input = owner.program_->originalInputs()->descriptorAt(command);
    const auto* expected = input ? std::get_if<rhi::TransferCopyArguments>(input) : nullptr;
    if (!expected || !expected->matches(desc))
        return rejectOriginalOrdinary("RHI-METAL-ORDINARY-COPY-INPUT", diagnostic);
    const auto& operation = owner.program_->operations()[command];
    const auto& usage = *owner.program_->originalInputs()->originalScalars()->originalUsage();
    const auto& sources = usage.originalSources()->sources();
    const auto& source = std::get<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(
        sources[usage.references()[operation.firstReference].sourceIndex]);
    const auto& destination = std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(
        sources[usage.references()[operation.firstReference + 1u].sourceIndex]);
    if (destination->originalObject()->gpuTransferWritesSealed()
        || !rhi::OriginalCommandStateTransaction::ordinaryAttachmentState(*source->originalObject(),
            desc.textureSubresources,
            rhi::ResourceState::CopySrc,
            *owner.journal_)
        || !rhi::OriginalCommandStateTransaction::ordinaryBufferState(
            *destination->originalObject(), rhi::ResourceState::CopyDst, *owner.journal_))
        return rejectOriginalOrdinary("RHI-METAL-ORDINARY-COPY-STATE", diagnostic);
    if (!encodeOriginalOrdinary([&] {
            return recordBlitTextureToBufferContents(desc, diagnostic);
        }))
        return rejectOriginalOrdinary(diagnostic, diagnostic);
    owner.consume();
    return true;
}

std::shared_ptr<const rhi::BindlessPreparedComputeBinding> prepareMetalRhiComputeBinding(
    std::shared_ptr<const rhi::BindlessComputeBindingOwners> owners,
    const rhi::BindlessComputeBindingBudget& budget,
    core::pmr::String& diagnostic)
{
    if (!owners) {
        diagnostic = "RHI-BINDLESS-PREPARED-COMPUTE-OWNERS-MISSING";
        return {};
    }
    if (!owners->sharedOwnerMatches(owners)) {
        diagnostic = "RHI-BINDLESS-PREPARED-COMPUTE-OWNERS-MISMATCH";
        return {};
    }
    const auto live = owners->validate();
    if (!live.success) {
        diagnostic = live.diagnostics;
        return {};
    }
    const auto& desc = owners->desc();
    const auto* set = desc.snapshot->descriptorSet();
    if (!set) {
        diagnostic = "RHI-BINDLESS-RECORD-SNAPSHOT-INVALID";
        return {};
    }
    auto payload = prepareMetalRhiNativeComputeBindings(metalRhiNativePayloadFor(*desc.pipeline),
        (desc.plan.originalExecutionContractVersion == 6u
            || rhi::semantics::OriginalBindlessPreparationLowering::hasPlan(desc.plan, 6u))
            ? prepareMetalRhiOriginalCompletedDepthDescriptors(*desc.snapshot)
            : rhi::semantics::OriginalBindlessPreparationLowering::hasImageCellBindingPlan(
                  desc.plan)
            ? prepareMetalOriginalImageCellDescriptors(*desc.snapshot)
            : desc.snapshot->plan().originalExecutionContractVersion == 8u
            ? prepareMetalRhiOriginalCompletedImageDescriptors(
                  *desc.snapshot, rhi::OriginalBindlessCompletedImageScope::ComputeColor8)
            : metalRhiNativePayloadFor(*set),
        desc.snapshot->setIndex(),
        budget,
        diagnostic);
    if (!payload)
        return {};
    auto result =
        core::makeShared<MetalRhiPreparedComputeBinding>(std::move(owners), std::move(payload));
    const auto valid = result->validatePayload();
    if (!valid.success) {
        diagnostic = valid.diagnostics;
        return {};
    }
    return result;
}

std::shared_ptr<const MetalRhiNativePreparedComputeBindings> metalRhiPreparedComputePayloadFor(
    const rhi::BindlessPreparedComputeBinding& binding) noexcept
{
    const auto* prepared = dynamic_cast<const MetalRhiPreparedComputeBinding*>(&binding);
    return prepared ? prepared->payload() : nullptr;
}

std::shared_ptr<const rhi::BindlessPreparedGraphicsBinding> prepareMetalRhiGraphicsBinding(
    std::shared_ptr<const rhi::BindlessGraphicsBindingOwners> owners,
    const rhi::BindlessGraphicsBindingBudget& budget,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    if (!owners) {
        diagnostic = "RHI-BINDLESS-PREPARED-GRAPHICS-OWNERS-MISSING";
        return {};
    }
    if (!owners->sharedOwnerMatches(owners)) {
        diagnostic = "RHI-BINDLESS-PREPARED-GRAPHICS-OWNERS-MISMATCH";
        return {};
    }
    const auto live = owners->validate();
    if (!live.success) {
        diagnostic = live.diagnostics;
        return {};
    }
    const auto& desc = owners->desc();
    const auto pipeline = metalRhiNativePayloadFor(*desc.pipeline);
    const auto* set = desc.snapshot->descriptorSet();
    const auto descriptors = set
        ? (std::any_of(set->layout().bindings.begin(),
               set->layout().bindings.end(),
               [](const auto& b) {
                   return b.type == rhi::DescriptorType::SampledTexture;
               })
                  ? prepareMetalRhiOriginalCompletedImageDescriptors(
                        *desc.snapshot, rhi::OriginalBindlessCompletedImageScope::SampledGraphics1)
                  : metalRhiNativePayloadFor(*set))
        : nullptr;
    if (desc.plan.execution != rhi::BindlessExecution::Native || !desc.plan.executable || !pipeline
        || !descriptors || !pipeline->nativeRuntimeBacked || !descriptors->nativeRuntimeBacked
        || pipeline->preparedGraphicsStages != owners->stages()) {
        diagnostic = "RHI-METAL-BINDLESS-PREPARED-GRAPHICS-PAYLOAD-MISMATCH";
        return {};
    }
    const auto& package = owners->authorization()->package();
    // The validated device-issued authorization covers the complete package,
    // including ordinary image domains. The snapshot descriptor group owns only
    // the indexed set, so its completed images cannot determine that authority.
    // Ordinary Sources are authenticated separately by the original Root closure.
    const auto admitted = validateMetalBindlessGraphicsPackage(package, true);
    if (!admitted.success) {
        diagnostic = admitted.diagnostics;
        return {};
    }
    const auto selected = metalRhiSelectedGraphicsEntriesFor(*desc.pipeline);
    const std::array names{selected.vertex, selected.fragment};
    const std::array stages{rhi::ShaderStage::Vertex, rhi::ShaderStage::Fragment};
    std::array<std::shared_ptr<const MetalRhiPreparedGraphicsStageLayout>, 2> layouts;
    const bool fragment =
        owners->stages() == (rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment);
    for (unsigned i = 0u; i < (fragment ? 2u : 1u); ++i) {
        auto layout = core::makeShared<MetalRhiPreparedGraphicsStageLayout>();
        const auto lowered = desc.pipeline->shaderReflection().pushConstantByteCount
            ? lowerMetalRhiPreparedGraphicsStage(
                  owners->authorization()->originalPackageOwner(), names[i], stages[i], *layout)
            : lowerMetalRhiPreparedGraphicsStage(package, names[i], stages[i], *layout);
        if (!lowered.success) {
            diagnostic = lowered.diagnostics;
            return {};
        }
        layouts[i] = std::move(layout);
    }
    auto payload = prepareMetalRhiNativeGraphicsBindings(
        pipeline, descriptors, desc.snapshot->setIndex(), layouts, budget, diagnostic);
    if (!payload)
        return {};
    auto result =
        core::makeShared<MetalRhiPreparedGraphicsBinding>(std::move(owners), std::move(payload));
    const auto valid = result->validatePayload();
    if (!valid.success) {
        diagnostic = valid.diagnostics;
        return {};
    }
    return result;
}

std::shared_ptr<const MetalRhiNativePreparedGraphicsBindings> metalRhiPreparedGraphicsPayloadFor(
    const rhi::BindlessPreparedGraphicsBinding& binding) noexcept
{
    const auto* prepared = dynamic_cast<const MetalRhiPreparedGraphicsBinding*>(&binding);
    return prepared ? prepared->payload() : nullptr;
}

core::UniquePtr<rhi::CommandEncoder> createMetalRhiOriginalOrdinaryCommandEncoder(
    std::shared_ptr<const rhi::OriginalCommandStreamProgram> program,
    const rhi::GpuDevice& device,
    const rhi::OriginalRenderPassPreparationRequest& request,
    std::shared_ptr<MetalRhiResourceRegistry> registry,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    if (const auto* id = MetalOriginalOrdinaryCommand::inspect(program, device, request)) {
        diagnostic = id;
        return {};
    }
    try {
        core::CpuMemoryQuotaScope scope(program->originalMetadataQuota());
        auto encoder = core::makeUnique<MetalRhiCommandEncoder>(rhi::CommandEncoderDesc{},
            rhi::semantics::nativeBackendCapabilityContract(rhi::Backend::Metal),
            nullptr,
            std::move(registry),
            nullptr,
            nullptr);
        if (!encoder->prepareOriginalOrdinary(std::move(program), device, request, diagnostic))
            return {};
        return encoder;
    } catch (const std::bad_alloc&) {
        core::CpuMemoryQuotaScope diagnosticScope(nullptr);
        diagnostic = "RHI-METAL-ORDINARY-COLD-ALLOCATION";
        return {};
    } catch (const std::length_error&) {
        core::CpuMemoryQuotaScope diagnosticScope(nullptr);
        diagnostic = "RHI-METAL-ORDINARY-COLD-ALLOCATION";
        return {};
    }
}

arp::core::UniquePtr<rhi::CommandEncoder> createMetalRhiCommandEncoder(
    const rhi::CommandEncoderDesc& desc,
    MetalRhiStagingUploadPool* stagingUploadPool,
    std::shared_ptr<MetalRhiResourceRegistry> registry)
{
    return createMetalRhiCommandEncoder(desc,
        rhi::semantics::nativeBackendCapabilityContract(rhi::Backend::Metal),
        stagingUploadPool,
        std::move(registry),
        nullptr);
}

arp::core::UniquePtr<rhi::CommandEncoder> createMetalRhiCommandEncoder(
    const rhi::CommandEncoderDesc& desc,
    const rhi::semantics::NativeBackendCapabilityContract& capabilityContract,
    MetalRhiStagingUploadPool* stagingUploadPool,
    std::shared_ptr<MetalRhiResourceRegistry> registry,
    const rhi::BindlessCapabilities* bindlessCapabilities,
    const rhi::BindlessCrossExtension* bindlessIssuer)
{
    if (!rhi::semantics::commandRetentionRequestAllowed(
            desc, rhi::CommandRetentionCapabilities{}, rhi::Backend::Metal))
        return {};
    auto encoder = arp::core::makeUnique<MetalRhiCommandEncoder>(desc,
        capabilityContract,
        stagingUploadPool,
        std::move(registry),
        bindlessCapabilities,
        bindlessIssuer);
    const auto runtime = describeMetalRhiNativeRuntimeBoundary();
    const auto* metalEncoder = dynamic_cast<MetalRhiCommandEncoder*>(encoder.get());
    const auto nativePayload = metalEncoder ? metalEncoder->nativePayload() : nullptr;
    if (runtime.runtimeAvailableOnThisPlatform
        && (!nativePayload || !nativePayload->stats()
            || !nativePayload->stats()->nativeRuntimeBacked)) {
        return nullptr;
    }
    return encoder;
}

arp::core::UniquePtr<rhi::SecondaryCommandEncoder> createMetalRhiSecondaryCommandEncoder(
    const rhi::SecondaryCommandEncoderDesc& desc,
    const rhi::semantics::NativeBackendCapabilityContract& capabilityContract)
{
    auto encoder = arp::core::makeUnique<MetalRhiSecondaryCommandEncoder>(desc, capabilityContract);
    const auto* metalEncoder = dynamic_cast<MetalRhiSecondaryCommandEncoder*>(encoder.get());
    return !metalEncoder || !metalEncoder->executable() ? nullptr : std::move(encoder);
}

arp::core::UniquePtr<rhi::RenderBundleEncoder> createMetalRhiRenderBundleEncoder(
    const rhi::RenderBundleDesc& desc,
    const rhi::semantics::NativeBackendCapabilityContract& capabilityContract)
{
    auto encoder = arp::core::makeUnique<MetalRhiRenderBundleEncoder>(desc, capabilityContract);
    return encoder->executable() ? std::move(encoder) : nullptr;
}

std::shared_ptr<MetalRhiNativeCommandPayload> metalRhiNativePayloadFor(
    const rhi::CommandBuffer& commandBuffer)
{
    const auto* metalCommandBuffer = dynamic_cast<const MetalRhiCommandBuffer*>(&commandBuffer);
    return metalCommandBuffer ? metalCommandBuffer->nativePayload() : nullptr;
}

std::shared_ptr<MetalRhiNativeCommandPayload> metalRhiNativePayloadFor(
    const rhi::CommandEncoder& commandEncoder)
{
    const auto* encoder = dynamic_cast<const MetalRhiCommandEncoder*>(&commandEncoder);
    return encoder ? encoder->nativePayload() : nullptr;
}

MetalRhiNativeCommandStats inspectMetalRhiNativeCommandStats(
    const rhi::CommandBuffer& commandBuffer)
{
    const auto nativePayload = metalRhiNativePayloadFor(commandBuffer);
    return nativePayload && nativePayload->stats() ? *nativePayload->stats()
                                                   : MetalRhiNativeCommandStats{};
}

} // namespace arp::rhi_metal

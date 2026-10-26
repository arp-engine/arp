#include "rhi/backends/vulkan/vulkan_graphics_pass.h"

#include "core/memory/cpu_allocator.h"
#include "rhi/backends/vulkan/vulkan_bindless.h"
#include "rhi/frontend/bindless_graphics_binding.h"
#include "rhi/frontend/bindless_shader_package.h"
#include "rhi/frontend/bindless_snapshot.h"
#include "rhi/frontend/command_arguments.h"
#include "rhi/frontend/descriptor_bound_descriptor_slots.h"
#include "rhi/frontend/descriptor_resource_validation.h"
#include "rhi/frontend/descriptor_set_validation.h"
#include "rhi/gpu_bindless.h"
#include "rhi/gpu_extensions.h"
#include "rhi/semantics/bindless_pipeline_interface.h"
#include "rhi/semantics/native_command_lowering.h"
#include "rhi/semantics/push_constant_validation.h"
#include "rhi/semantics/viewport_scissor_validation.h"

#include <utility>

namespace arp::rhi_vulkan {

namespace {

rhi::CommandValidationResult commandOk(std::uint32_t count)
{
    return {true, "", count};
}

rhi::CommandValidationResult commandFail(const std::pmr::string& diagnostics, std::uint32_t count)
{
    return {false, diagnostics, count};
}

class VulkanRhiRenderPassEncoder final : public rhi::RenderPassEncoder,
                                         public rhi::BindlessPassExtension {
public:
    VulkanRhiRenderPassEncoder(std::uint32_t& commandCount,
        bool& active,
        std::uint64_t recordingIdentity,
        bool indirectFirstInstanceEnabled,
        rhi::BindlessIndirectDeclarations& indirectAccesses,
        const rhi::RenderPassDesc& passDesc,
        std::shared_ptr<VulkanRhiNativeCommandPayload> nativePayload,
        std::shared_ptr<VulkanRhiResourceRegistry> resourceRegistry,
        const rhi::semantics::NativeBackendCapabilityContract* capabilityContract,
        bool bindlessExecutable,
        std::shared_ptr<const rhi::RenderPassArguments> originalPass = {})
        : commandCount_(commandCount), active_(active), recordingIdentity_(recordingIdentity),
          indirectFirstInstanceEnabled_(indirectFirstInstanceEnabled),
          indirectAccesses_(indirectAccesses),
          passDesc_(originalPass ? rhi::RenderPassDesc{} : passDesc),
          originalPass_(std::move(originalPass)), nativePayload_(std::move(nativePayload)),
          resourceRegistry_(std::move(resourceRegistry)), capabilityContract_(capabilityContract),
          bindlessExecutable_(bindlessExecutable)
    {}

    std::uint64_t recordingIdentity() const noexcept override { return recordingIdentity_; }

    const char* activate(std::shared_ptr<VulkanRhiNativeCommandPayload> payload,
        VulkanRhiColdFailure failure) noexcept
    {
        if (!originalPass_ || nativePayload_ || !payload || !payload->hasOrdinaryAttachments())
            return "RHI-ORDINARY-ATTACHMENT-RECORDING-WRAPPER";
        nativePayload_ = std::move(payload);
        failure_ = failure;
        return nullptr;
    }

    VulkanRhiColdRenderPassObservation coldObservation() const noexcept
    {
        return originalPass_ && !nativePayload_
            ? VulkanRhiColdRenderPassObservation{this, originalPass_.get(), &passDescriptor()}
            : VulkanRhiColdRenderPassObservation{};
    }

    void* queryInterface(std::string_view extension) noexcept override
    {
        return bindlessExecutable_ && extension == rhi::kExtensionBindlessCross
            ? static_cast<rhi::BindlessPassExtension*>(this)
            : nullptr;
    }

    rhi::CommandValidationResult setPipeline(const rhi::RenderPipeline& pipeline) override
    {
        if (originalPass_)
            return originalCommand(rhi::CommandUsageKind::BindGraphicsPipeline,
                rhi::CommandScalarArguments::None{},
                &pipeline);
        if (boundSnapshot_) {
            return commandFail("RHI-BINDLESS-PIPELINE-CHANGE-AFTER-SNAPSHOT", commandCount_);
        }
        indirectAccesses_.clear();
        if (pipeline.nativeHandleExposedToUpperLayer()) {
            return commandFail("Vulkan RHI render pipeline exposed native handle", 0);
        }
        if (!nativePayload_) {
            return commandFail("RHI-VULKAN-NATIVE-RENDER-PASS-PAYLOAD-MISSING", 0);
        }
        const auto authorization = pipeline.bindlessAuthorization();
        if (authorization && !authorization->current(authorization))
            return commandFail("RHI-BINDLESS-PIPELINE-AUTHORIZATION-STALE", 0);
        if (authorization
            && (!bindlessExecutable_
                || authorization->plan().execution != rhi::BindlessExecution::Native
                || !authorization->plan().executable)) {
            return commandFail("RHI-VULKAN-BINDLESS-PIPELINE-AUTHORIZATION-INVALID", commandCount_);
        }
        if (!authorization && !pipeline.shaderReflection().bindlessContractVersion.empty()) {
            return commandFail("RHI-BINDLESS-RECORD-AUTHORIZATION-INVALID", commandCount_);
        }
        if (authorization) {
            std::pmr::string retentionDiagnostic;
            if (!nativePayload_->retainIndexedPipeline(authorization, retentionDiagnostic))
                return commandFail(retentionDiagnostic, 0);
        }
        pipelineLayout_ = pipeline.layout();
        shaderReflection_ = &pipeline.shaderReflection();
        nativeRenderPipeline_ = vulkanRhiNativePayload(pipeline);
        nativePayload_->retainResource(nativeRenderPipeline_);
        std::pmr::string diagnostics;
        if (!nativeRenderPipeline_
            || !nativePayload_->recordGraphicsPipelineBind(*nativeRenderPipeline_, diagnostics)) {
            return commandFail(diagnostics.empty()
                    ? "Vulkan RHI native graphics bind requires a native pipeline"
                    : diagnostics,
                0);
        }
        activeAuthorization_ = authorization;
        activePipeline_ = &pipeline;
        pipelineBound_ = true;
        boundDescriptors_.reset();
        activePipelineReport_ = pipeline.validationReport();
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult bindPreparedGraphicsBinding(
        std::shared_ptr<const rhi::BindlessPreparedGraphicsBinding> binding,
        const rhi::BindlessAccessDeclarations& accesses) override
    {
        if (originalPass_)
            return rejectOriginal();
        if (!active_)
            return commandFail("command pass is not active", commandCount_);
        if (!binding || !binding->originalFactoryOwnerMatches(binding)
            || !isVulkanPreparedGraphicsBinding(*binding))
            return commandFail("RHI-BINDLESS-PREPARED-GRAPHICS-PAYLOAD-MISMATCH", commandCount_);
        if (!activePipeline_ || binding->owners()->desc().pipeline.get() != activePipeline_)
            return commandFail("RHI-BINDLESS-PREPARED-GRAPHICS-PIPELINE-MISMATCH", commandCount_);
        const auto valid = binding->validateForRecord(*activePipeline_, accesses);
        if (!valid.success)
            return commandFail(valid.diagnostics, commandCount_);
        if (!nativePayload_ || !nativePayload_->canRetainPreparedBinding(binding))
            return commandFail("RHI-BINDLESS-PREPARED-GRAPHICS-COMMAND-BUDGET", commandCount_);
        const auto& desc = binding->owners()->desc();
        const auto result = bindSnapshot(desc.snapshot, desc.plan, desc.generation, accesses);
        if (!result.success)
            return result;
        core::pmr::String diagnostic;
        if (!nativePayload_->retainPreparedBinding(binding, diagnostic))
            return commandFail(diagnostic, commandCount_);
        return result;
    }

    rhi::CommandValidationResult bindSnapshot(std::shared_ptr<const rhi::BindlessSnapshot> snapshot,
        const rhi::BindlessLoweringPlan& selected,
        std::uint64_t generation,
        const rhi::BindlessAccessDeclarations& accesses) override
    {
        if (originalPass_)
            return rejectOriginal();
        if (!active_ || !bindlessExecutable_ || !activeAuthorization_
            || !activeAuthorization_->current(activeAuthorization_)) {
            return commandFail("RHI-BINDLESS-RECORD-AUTHORIZATION-INVALID", commandCount_);
        }
        if (boundSnapshot_) {
            return commandFail("RHI-BINDLESS-RECORD-SNAPSHOT-ALREADY-BOUND", commandCount_);
        }
        if (!snapshot) {
            return commandFail("RHI-BINDLESS-RECORD-SNAPSHOT-MISSING", commandCount_);
        }
        if (!snapshot->sharedOwnerMatches(snapshot))
            return commandFail("RHI-BINDLESS-RECORD-SNAPSHOT-OWNER-MISMATCH", commandCount_);
        const auto& authorizedPlan = activeAuthorization_->plan();
        const auto& artifact = activeAuthorization_->package().artifact();
        if (const auto* id =
                rhi::semantics::BindlessPipelineInterfaceContract::standaloneSnapshotDiagnostic(
                    artifact))
            return commandFail(id, commandCount_);
        if (!selected.success || !selected.executable
            || selected.execution != rhi::BindlessExecution::Native
            || selected.identity != authorizedPlan.identity
            || selected.deviceIdentity != authorizedPlan.deviceIdentity
            || selected.shaderPackageIdentity != artifact.shaderPackageIdentity
            || artifact.execution != rhi::BindlessExecution::Native
            || snapshot->plan().identity != authorizedPlan.identity) {
            return commandFail("RHI-BINDLESS-RECORD-PLAN-MISMATCH", commandCount_);
        }
        const auto activeStages = activePipeline_->bindlessShaderStages();
        if (activeStages == rhi::ShaderStage::None)
            return commandFail("RHI-BINDLESS-ACTIVE-STAGES-INVALID", commandCount_);
        auto valid = snapshot->validateForRecord(selected, generation, accesses, activeStages);
        if (!valid.success)
            return commandFail(valid.diagnostics, commandCount_);
        valid = snapshot->validateForRecord(authorizedPlan, generation, accesses, activeStages);
        if (!valid.success)
            return commandFail(valid.diagnostics, commandCount_);
        valid = indirectAccesses_.capture(accesses);
        if (!valid.success)
            return commandFail(valid.diagnostics, commandCount_);
        if (!snapshot->faultLatchBuffer() || !snapshot->faultReadbackBuffer()) {
            return commandFail("RHI-BINDLESS-FAULT-READBACK-UNAVAILABLE", commandCount_);
        }

        valid = rhi::validateBindlessRenderPass(activePipelineReport_, passDescriptor());
        if (!valid.success)
            return commandFail(valid.diagnostics, commandCount_);

        const auto* descriptors = snapshot->descriptorSet();
        if (!descriptors) {
            return commandFail("RHI-BINDLESS-RECORD-SNAPSHOT-INVALID", commandCount_);
        }
        const auto capacity = boundDescriptors_.validateSetCapacity(snapshot->setIndex());
        if (!capacity.success)
            return commandFail(capacity.diagnostics, commandCount_);
        valid =
            rhi::validateDescriptorSetBinding(pipelineLayout_, snapshot->setIndex(), *descriptors);
        if (!valid.success)
            return commandFail(valid.diagnostics, commandCount_);
        const auto nativeSet = vulkanRhiNativePayload(*descriptors);
        if (!nativeSet) {
            return commandFail("RHI-VULKAN-NATIVE-DESCRIPTOR-PAYLOAD-MISSING", commandCount_);
        }
        std::pmr::string diagnostics;
        if (!nativePayload_->retainIndexedSnapshot(snapshot, diagnostics)
            || !nativePayload_->recordBindlessFaultLatchClear(
                snapshot, snapshot->faultLatchPriorState(), diagnostics)
            || !nativePayload_->recordDescriptorSetBind(
                *nativeSet, snapshot->setIndex(), nullptr, 0u, diagnostics)) {
            return commandFail(diagnostics, commandCount_);
        }
        // The bounded indexedSnapshots owner already retains the immutable set
        // and its native payload through completion. Do not duplicate it in
        // the ordinary recording resource vector (which can allocate here).
        const auto tracked =
            boundDescriptors_.bindSet(snapshot->setIndex(), *descriptors, nullptr, 0u);
        if (!tracked.success)
            return commandFail(tracked.diagnostics, commandCount_);
        boundSnapshot_ = std::move(snapshot);
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult setViewport(const rhi::ViewportState& viewport) override
    {
        if (originalPass_)
            return originalCommand(rhi::CommandUsageKind::SetViewport, viewport);
        // L1 value validation - every backend calls this before lowering.
        const auto valueResult = rhi::semantics::validateViewportCommand(viewport);
        if (!valueResult.success) {
            return commandFail(valueResult.diagnostics, 0);
        }
        // L2 invariant: pipeline must be bound before dynamic viewport state
        // (AGENTS.md RHI Rule 4: no implicit state). The stable code is the
        // same one the Null backend emits so contract tests assert uniformly.
        if (!pipelineBound_) {
            return commandFail(rhi::semantics::ViewportScissorDiagCode::kNoPipeline
                    + std::pmr::string("Vulkan RHI setViewport requires a bound pipeline"),
                0);
        }
        // P2 executable-payload gate - matches pushConstants' pattern.
        if (!nativePayload_) {
            return commandFail(rhi::semantics::ViewportScissorDiagCode::kNoExecPayload
                    + std::pmr::string("Vulkan RHI setViewport requires a native command payload"),
                0);
        }
        std::pmr::string diagnostics;
        if (!nativePayload_->recordSetViewport(viewport, diagnostics)) {
            return commandFail(diagnostics, 0);
        }
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult setScissor(const rhi::ScissorState& scissor) override
    {
        if (originalPass_)
            return originalCommand(rhi::CommandUsageKind::SetScissor, scissor);
        const auto valueResult = rhi::semantics::validateScissorCommand(scissor);
        if (!valueResult.success) {
            return commandFail(valueResult.diagnostics, 0);
        }
        if (!pipelineBound_) {
            return commandFail(rhi::semantics::ViewportScissorDiagCode::kNoPipeline
                    + std::pmr::string("Vulkan RHI setScissor requires a bound pipeline"),
                0);
        }
        if (!nativePayload_) {
            return commandFail(rhi::semantics::ViewportScissorDiagCode::kNoExecPayload
                    + std::pmr::string("Vulkan RHI setScissor requires a native command payload"),
                0);
        }
        std::pmr::string diagnostics;
        if (!nativePayload_->recordSetScissor(scissor, diagnostics)) {
            return commandFail(diagnostics, 0);
        }
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult bindVertexBuffer(
        std::uint32_t slot, const rhi::GpuBuffer& buffer, std::uint64_t offsetBytes) override
    {
        if (originalPass_)
            return rejectOriginal();
        if (!nativePayload_) {
            return commandFail("RHI-VULKAN-NATIVE-RENDER-PASS-PAYLOAD-MISSING", 0);
        }
        const auto nativeBuffer =
            resourceRegistry_ ? resourceRegistry_->payload(buffer.id()) : nullptr;
        nativePayload_->retainResource(nativeBuffer);
        std::pmr::string diagnostics;
        if (!nativeBuffer
            || !nativePayload_->recordVertexBufferBind(
                slot, *nativeBuffer, offsetBytes, diagnostics)) {
            return commandFail(diagnostics.empty()
                    ? "Vulkan RHI native vertex buffer bind requires a tracked buffer"
                    : diagnostics,
                0);
        }
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult bindIndexBuffer(
        const rhi::GpuBuffer& buffer, rhi::IndexFormat format, std::uint64_t offsetBytes) override
    {
        if (originalPass_)
            return rejectOriginal();
        if (!nativePayload_) {
            return commandFail("RHI-VULKAN-NATIVE-RENDER-PASS-PAYLOAD-MISSING", 0);
        }
        const auto nativeBuffer =
            resourceRegistry_ ? resourceRegistry_->payload(buffer.id()) : nullptr;
        nativePayload_->retainResource(nativeBuffer);
        std::pmr::string diagnostics;
        if (!nativeBuffer
            || !nativePayload_->recordIndexBufferBind(
                *nativeBuffer, format, offsetBytes, diagnostics)) {
            return commandFail(diagnostics.empty()
                    ? "Vulkan RHI native index buffer bind requires a tracked buffer"
                    : diagnostics,
                0);
        }
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult bindDescriptorSet(std::uint32_t setIndex,
        const rhi::DescriptorSet& set,
        const std::uint32_t* dynamicOffsets,
        std::uint32_t dynamicOffsetCount) override
    {
        if (originalPass_) {
            if (dynamicOffsetCount || dynamicOffsets)
                return rejectOriginal();
            return originalCommand(rhi::CommandUsageKind::BindRenderDescriptor,
                rhi::CommandScalarArguments::DescriptorBinding{setIndex},
                nullptr,
                &set);
        }
        if (activeAuthorization_) {
            return commandFail("RHI-BINDLESS-RECORD-SNAPSHOT-REQUIRED", commandCount_);
        }
        const auto trackingCapacity = boundDescriptors_.validateSetCapacity(setIndex);
        if (!trackingCapacity.success)
            return commandFail(trackingCapacity.diagnostics, commandCount_);
        if (set.stats().nativeHandleExposedToUpperLayer) {
            return commandFail("Vulkan RHI descriptor set exposed native handle", 0);
        }
        const auto resources = rhi::validateDescriptorResources(
            set, rhi::DescriptorContentRequirement::WrittenElements);
        if (!resources.success)
            return commandFail(resources.diagnostics, commandCount_);
        if (!nativePayload_) {
            return commandFail("RHI-VULKAN-NATIVE-RENDER-PASS-PAYLOAD-MISSING", 0);
        }
        const auto dynamicValidation =
            rhi::validateDynamicOffsetArguments(set.layout(), dynamicOffsets, dynamicOffsetCount);
        if (!dynamicValidation.success) {
            return commandFail(dynamicValidation.diagnostics, 0);
        }
        auto descriptorSet = vulkanRhiNativePayload(set);
        nativePayload_->retainResource(descriptorSet);
        std::pmr::string diagnostics;
        if (!descriptorSet
            || !nativePayload_->recordDescriptorSetBind(
                *descriptorSet, setIndex, dynamicOffsets, dynamicOffsetCount, diagnostics)) {
            return commandFail(diagnostics.empty()
                    ? "Vulkan RHI native descriptor bind requires a native descriptor set"
                    : diagnostics,
                0);
        }
        const auto tracked =
            boundDescriptors_.bindSet(setIndex, set, dynamicOffsets, dynamicOffsetCount);
        if (!tracked.success)
            return commandFail(tracked.diagnostics, commandCount_);
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult bindDescriptorTable(
        std::uint32_t, const rhi::DescriptorTable& table) override
    {
        if (originalPass_)
            return rejectOriginal();
        if (activeAuthorization_) {
            return commandFail("RHI-BINDLESS-RECORD-SNAPSHOT-REQUIRED", commandCount_);
        }
        (void)table;
        return commandFail("RHI-VULKAN-DESCRIPTOR-TABLE-NATIVE-LOWERING-UNAVAILABLE", 0);
    }

    rhi::CommandValidationResult pushConstants(rhi::ShaderStage stages,
        std::uint32_t offsetBytes,
        std::uint32_t sizeBytes,
        const void* data) override
    {
        if (originalPass_)
            return rejectOriginal();
        auto cmdResult =
            rhi::semantics::validatePushConstantCommand(stages, offsetBytes, sizeBytes, data);
        if (!cmdResult.success) {
            return commandFail(cmdResult.diagnostics, 0);
        }
        auto bindResult = rhi::semantics::validatePushConstantBinding(pipelineLayout_,
            rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment,
            stages,
            offsetBytes,
            sizeBytes,
            shaderReflection_);
        if (!bindResult.success) {
            return commandFail(bindResult.diagnostics, 0);
        }
        // P1-C1: duplicate-unconsumed-update detection.
        if (pushState_.hasUnconsumedUpdate()) {
            return commandFail(rhi::semantics::PushConstantDiagCode::kDuplicateUnconsumed
                    + std::pmr::string(
                        "push constants already set without intervening draw/dispatch"),
                0);
        }
        // P2 executable-payload gate.
        if (!nativePayload_) {
            return commandFail(rhi::semantics::PushConstantDiagCode::kNoExecPayload
                    + std::pmr::string(
                        "Vulkan graphics push constants require a native command payload"),
                0);
        }
        std::pmr::string diagnostics;
        if (!nativePayload_->recordPushConstants(
                stages, offsetBytes, sizeBytes, data, diagnostics)) {
            return commandFail(diagnostics, 0);
        }
        pushState_.onPush();
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult executeSecondary(
        const rhi::SecondaryCommandBuffer& commandBuffer) override
    {
        if (originalPass_)
            return rejectOriginal();
        (void)commandBuffer;
        return commandFail("RHI-VULKAN-SECONDARY-COMMAND-NATIVE-LOWERING-UNAVAILABLE", 0);
    }

    rhi::CommandValidationResult executeRenderBundle(const rhi::RenderBundle&) override
    {
        if (originalPass_)
            return rejectOriginal();
        return commandFail("Vulkan RHI render bundle has no executable native payload", 0);
    }

    rhi::CommandValidationResult drawIndexed(std::uint32_t indexCount,
        std::uint32_t instanceCount,
        std::uint32_t firstIndex,
        std::int32_t vertexOffset,
        std::uint32_t firstInstance) override
    {
        if (originalPass_)
            return rejectOriginal();
        if (activeAuthorization_ && !boundSnapshot_) {
            return commandFail("RHI-BINDLESS-RECORD-SNAPSHOT-REQUIRED", commandCount_);
        }
        const auto descriptorUse = boundDescriptors_.validateLiveBindings();
        if (!descriptorUse.success)
            return commandFail(descriptorUse.diagnostics, commandCount_);
        if (indexCount == 0) {
            return commandFail("Vulkan RHI draw indexed requires indices", 0);
        }
        if (!nativePayload_) {
            return commandFail("RHI-VULKAN-NATIVE-RENDER-PASS-PAYLOAD-MISSING", 0);
        }
        std::pmr::string diagnostics;
        if (!nativePayload_->recordDrawIndexed(
                indexCount, instanceCount, firstIndex, vertexOffset, firstInstance, diagnostics)) {
            return commandFail(diagnostics, 0);
        }
        pushState_.onConsume();
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult drawIndexedIndirect(const rhi::GpuBuffer& indirectBuffer,
        std::uint64_t indirectOffset,
        std::uint32_t drawCount,
        std::uint32_t stride) override
    {
        if (originalPass_)
            return rejectOriginal();
        if (activeAuthorization_) {
            if (!boundSnapshot_)
                return commandFail("RHI-BINDLESS-RECORD-SNAPSHOT-REQUIRED", commandCount_);
            if (!indirectFirstInstanceEnabled_)
                return commandFail(
                    "RHI-BINDLESS-INDIRECT-FIRST-INSTANCE-UNAVAILABLE", commandCount_);
            if (drawCount != 1u)
                return commandFail("RHI-BINDLESS-INDIRECT-MULTIDRAW-UNAVAILABLE", commandCount_);
            const auto valid = rhi::validateBindlessIndirectDrawAccess(
                indirectBuffer, indirectOffset, drawCount, stride, indirectAccesses_);
            if (!valid.success)
                return commandFail(valid.diagnostics, commandCount_);
        }
        const auto descriptorUse = boundDescriptors_.validateLiveBindings();
        if (!descriptorUse.success)
            return commandFail(descriptorUse.diagnostics, commandCount_);
        if (!nativePayload_) {
            return commandFail("RHI-VULKAN-NATIVE-RENDER-PASS-PAYLOAD-MISSING", 0);
        }
        const auto nativeBuffer =
            resourceRegistry_ ? resourceRegistry_->payload(indirectBuffer.id()) : nullptr;
        nativePayload_->retainResource(nativeBuffer);
        std::pmr::string diagnostics;
        if (!nativeBuffer
            || !nativePayload_->recordDrawIndexedIndirect(
                *nativeBuffer, indirectOffset, drawCount, stride, diagnostics)) {
            return commandFail(diagnostics.empty()
                    ? "Vulkan RHI indirect draw requires a tracked args buffer"
                    : diagnostics,
                0);
        }
        pushState_.onConsume();
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult drawIndexedIndirectSequence(const rhi::GpuBuffer& buffer,
        std::uint64_t offset,
        std::uint32_t count,
        std::uint32_t stride) override
    {
        if (originalPass_)
            return rejectOriginal();
        const rhi::IndexedIndirectSequenceRange range(buffer.desc(), offset, count, stride);
        if (!range.valid())
            return commandFail("RHI-INDIRECT-SEQUENCE-RANGE-INVALID", commandCount_);
        if (count == 1u)
            return drawIndexedIndirect(buffer, offset, 1u, stride);
        if (activeAuthorization_)
            return commandFail("RHI-BINDLESS-INDIRECT-MULTIDRAW-UNAVAILABLE", commandCount_);
        const auto bindings = boundDescriptors_.validateLiveBindings();
        if (!bindings.success)
            return commandFail(bindings.diagnostics, commandCount_);
        if (!nativePayload_)
            return commandFail("RHI-VULKAN-NATIVE-RENDER-PASS-PAYLOAD-MISSING", commandCount_);
        const auto nativeBuffer =
            resourceRegistry_ ? resourceRegistry_->payload(buffer.id()) : nullptr;
        if (!nativeBuffer)
            return commandFail("RHI-VULKAN-INDIRECT-SEQUENCE-ARGS-PAYLOAD-MISSING", commandCount_);
        nativePayload_->retainResource(nativeBuffer);
        std::pmr::string diagnostic;
        for (std::uint32_t ordinal = 0; ordinal < count; ++ordinal) {
            std::uint64_t address = 0;
            range.offset(ordinal, address);
            if (!nativePayload_->recordDrawIndexedIndirect(
                    *nativeBuffer, address, 1u, stride, diagnostic))
                return commandFail(diagnostic, commandCount_);
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
        if (originalPass_)
            return rejectOriginal();
        if (activeAuthorization_) {
            return commandFail("RHI-BINDLESS-INDIRECT-DRAW-UNAVAILABLE", commandCount_);
        }
        const auto descriptorUse = boundDescriptors_.validateLiveBindings();
        if (!descriptorUse.success)
            return commandFail(descriptorUse.diagnostics, commandCount_);
        if (!nativePayload_) {
            return commandFail("RHI-VULKAN-NATIVE-RENDER-PASS-PAYLOAD-MISSING", 0);
        }
        const auto nativeArgs =
            resourceRegistry_ ? resourceRegistry_->payload(argsBuffer.id()) : nullptr;
        const auto nativeCount =
            resourceRegistry_ ? resourceRegistry_->payload(countBuffer.id()) : nullptr;
        nativePayload_->retainResource(nativeArgs);
        nativePayload_->retainResource(nativeCount);
        std::pmr::string diagnostics;
        if (!nativeArgs || !nativeCount
            || !nativePayload_->recordDrawIndexedIndirectCount(*nativeArgs,
                argsOffset,
                *nativeCount,
                countOffset,
                maxDrawCount,
                stride,
                diagnostics)) {
            return commandFail(diagnostics.empty()
                    ? "Vulkan RHI indirect draw count requires tracked args + count buffers"
                    : diagnostics,
                0);
        }
        pushState_.onConsume();
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult draw(std::uint32_t vertexCount,
        std::uint32_t instanceCount,
        std::uint32_t firstVertex,
        std::uint32_t firstInstance) override
    {
        if (originalPass_)
            return originalCommand(rhi::CommandUsageKind::Draw,
                rhi::CommandScalarArguments::Draw{
                    vertexCount, instanceCount, firstVertex, firstInstance});
        if (activeAuthorization_ && !boundSnapshot_) {
            return commandFail("RHI-BINDLESS-RECORD-SNAPSHOT-REQUIRED", commandCount_);
        }
        const auto descriptorUse = boundDescriptors_.validateLiveBindings();
        if (!descriptorUse.success)
            return commandFail(descriptorUse.diagnostics, commandCount_);
        if (vertexCount == 0) {
            return commandFail("Vulkan RHI draw requires vertices", 0);
        }
        if (!nativePayload_) {
            return commandFail("RHI-VULKAN-NATIVE-RENDER-PASS-PAYLOAD-MISSING", 0);
        }
        std::pmr::string diagnostics;
        if (!nativePayload_->recordDraw(
                vertexCount, instanceCount, firstVertex, firstInstance, diagnostics)) {
            return commandFail(diagnostics, 0);
        }
        pushState_.onConsume();
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult writeTimestamp(
        const rhi::QueryPool& pool, std::uint32_t queryIndex) override
    {
        if (originalPass_)
            return rejectOriginal();
        if (!capabilityContract_) {
            return commandFail("Vulkan RHI timestamp write has no capability contract", 0);
        }
        rhi::semantics::CommandWorkloadDesc workload;
        workload.label = "vulkan-render-timestamp";
        workload.commandCount = 1u;
        workload.usesRenderPass = true;
        workload.usesTimestampQuery = true;
        const auto decision = rhi::semantics::lowerCommandWorkload(*capabilityContract_,
            workload,
            rhi::semantics::CommandRecordingIntent::PrimaryCommandBuffer);
        if (!decision.success) {
            return commandFail(decision.diagnostics, 0);
        }
        const auto loweringUse = rhi::semantics::validateNativeCommandLoweringUse(decision,
            rhi::semantics::NativeCommandSemantic::VulkanTimestampQuery,
            rhi::FallbackMode::Native,
            "Vulkan timestamp query");
        if (!loweringUse.success) {
            return commandFail(loweringUse.diagnostics, 0);
        }
        if (!nativePayload_) {
            return commandFail("Vulkan RHI timestamp write requires a native command payload", 0);
        }
        std::pmr::string diagnostics;
        if (!nativePayload_->recordTimestamp(pool, queryIndex, diagnostics)) {
            return commandFail(diagnostics, 0);
        }
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult nextSubpass() override
    {
        if (originalPass_)
            return rejectOriginal();
        if (activeAuthorization_) {
            return commandFail("RHI-BINDLESS-RENDER-PASS-ABI-UNAVAILABLE", commandCount_);
        }
        if (!nativePayload_) {
            return commandFail("RHI-VULKAN-NATIVE-RENDER-PASS-PAYLOAD-MISSING", 0);
        }
        std::pmr::string diagnostics;
        if (!nativePayload_->recordNextSubpass(diagnostics)) {
            return commandFail(diagnostics, 0);
        }
        ++subpassIndex_;
        boundDescriptors_.reset();
        indirectAccesses_.clear();
        return commandOk(++commandCount_);
    }

    std::uint32_t currentSubpassIndex() const override { return subpassIndex_; }

    rhi::CommandValidationResult end() override
    {
        if (originalPass_) {
            if (!nativePayload_ || !active_)
                return rejectOriginal();
            if (const auto* id = nativePayload_->recordOrdinaryRenderPassEnd(commandCount_)) {
                publishOriginalFailure(id);
                return {false, "", commandCount_};
            }
            active_ = false;
            return commandOk(++commandCount_);
        }
        if (!nativePayload_) {
            return commandFail("RHI-VULKAN-NATIVE-RENDER-PASS-PAYLOAD-MISSING", 0);
        }
        std::pmr::string diagnostics;
        if (!nativePayload_->recordRenderPassEnd(diagnostics)) {
            return commandFail(diagnostics, 0);
        }
        if (activeAuthorization_ && !boundSnapshot_) {
            return commandFail("RHI-BINDLESS-RECORD-SNAPSHOT-REQUIRED", commandCount_);
        }
        if (!activeAuthorization_ && boundSnapshot_) {
            return commandFail("RHI-BINDLESS-RECORD-AUTHORIZATION-INVALID", commandCount_);
        }
        active_ = false;
        pushState_.onPassEnd();
        boundDescriptors_.reset();
        indirectAccesses_.clear();
        if (boundSnapshot_) {
            std::pmr::string diagnostics;
            if (!nativePayload_->scheduleBindlessFaultReadback(boundSnapshot_, diagnostics)) {
                return commandFail(diagnostics, commandCount_);
            }
            boundSnapshot_.reset();
            activeAuthorization_.reset();
        }
        return commandOk(++commandCount_);
    }

private:
    template <class Value>
    rhi::CommandValidationResult originalCommand(rhi::CommandUsageKind kind,
        const Value& value,
        const rhi::RenderPipeline* pipeline = nullptr,
        const rhi::DescriptorSet* set = nullptr) noexcept
    {
        const auto scalar = rhi::CommandScalarArguments::capture(kind, value);
        const auto* id = !active_ || !nativePayload_ || !scalar
            ? "RHI-ORDINARY-GRAPHICS-WRAPPER"
            : nativePayload_->recordOrdinaryGraphicsCommand(commandCount_, *scalar, pipeline, set);
        if (id) {
            publishOriginalFailure(id);
            return {false, "", commandCount_};
        }
        return commandOk(++commandCount_);
    }

    void publishOriginalFailure(const char* id) noexcept
    {
        if (failure_.publish)
            failure_.publish(failure_.context, id);
        else if (nativePayload_)
            nativePayload_->rejectRecordingStorage(id);
    }

    rhi::CommandValidationResult rejectOriginal() noexcept
    {
        publishOriginalFailure("RHI-ORDINARY-ATTACHMENT-RECORDING-COMMAND-PROFILE");
        return {false, "", commandCount_};
    }

    const rhi::RenderPassDesc& passDescriptor() const noexcept
    {
        return originalPass_ ? originalPass_->desc() : passDesc_;
    }

    rhi::BoundDescriptorSlots boundDescriptors_;
    std::uint32_t& commandCount_;
    bool& active_;
    std::uint64_t recordingIdentity_ = 0u;
    bool indirectFirstInstanceEnabled_ = false;
    rhi::BindlessIndirectDeclarations& indirectAccesses_;
    rhi::RenderPassDesc passDesc_;
    std::shared_ptr<const rhi::RenderPassArguments> originalPass_;
    VulkanRhiColdFailure failure_;
    std::shared_ptr<VulkanRhiNativeCommandPayload> nativePayload_;
    std::shared_ptr<VulkanRhiResourceRegistry> resourceRegistry_;
    const rhi::semantics::NativeBackendCapabilityContract* capabilityContract_ = nullptr;
    std::shared_ptr<const rhi::BindlessAuthorization> activeAuthorization_;
    std::shared_ptr<const rhi::BindlessSnapshot> boundSnapshot_;
    const rhi::RenderPipeline* activePipeline_ = nullptr;
    rhi::PipelineValidationReport activePipelineReport_;
    bool bindlessExecutable_ = false;
    std::shared_ptr<VulkanRhiNativeResourcePayload> nativeRenderPipeline_;
    const rhi::PipelineLayout* pipelineLayout_ = nullptr;
    const rhi::ShaderReflectionDesc* shaderReflection_ = nullptr;
    rhi::semantics::PushConstantConsumptionState pushState_;
    bool pipelineBound_ = false; // L2 gate for setViewport/setScissor
    std::uint32_t subpassIndex_ = 0;
};

} // namespace

core::UniquePtr<rhi::RenderPassEncoder> createVulkanRhiColdRenderPassEncoder(
    std::uint32_t& commandCount,
    bool& active,
    std::uint64_t recordingIdentity,
    rhi::BindlessIndirectDeclarations& indirectAccesses,
    std::shared_ptr<const rhi::RenderPassArguments> original)
{
    if (!original || !original->hasQuotaBoundSharedOwner(original))
        return {};
    return core::makeUnique<VulkanRhiRenderPassEncoder>(commandCount,
        active,
        recordingIdentity,
        false,
        indirectAccesses,
        original->desc(),
        nullptr,
        nullptr,
        nullptr,
        false,
        original);
}

VulkanRhiColdRenderPassObservation inspectVulkanRhiColdRenderPass(
    const rhi::RenderPassEncoder& pass) noexcept
{
    const auto* original = dynamic_cast<const VulkanRhiRenderPassEncoder*>(&pass);
    return original ? original->coldObservation() : VulkanRhiColdRenderPassObservation{};
}

const char* activateVulkanRhiColdRenderPass(rhi::RenderPassEncoder& pass,
    std::shared_ptr<VulkanRhiNativeCommandPayload> payload,
    VulkanRhiColdFailure failure) noexcept
{
    auto* original = dynamic_cast<VulkanRhiRenderPassEncoder*>(&pass);
    return original ? original->activate(std::move(payload), failure)
                    : "RHI-ORDINARY-ATTACHMENT-RECORDING-WRAPPER";
}

arp::core::UniquePtr<rhi::RenderPassEncoder> createVulkanRhiRenderPassEncoder(
    std::uint32_t& commandCount,
    bool& active,
    std::uint64_t recordingIdentity,
    bool bindlessExecutable,
    bool indirectFirstInstanceEnabled,
    rhi::BindlessIndirectDeclarations& indirectAccesses,
    const rhi::RenderPassDesc& desc,
    std::shared_ptr<VulkanRhiNativeCommandPayload> nativePayload,
    std::shared_ptr<VulkanRhiResourceRegistry> resourceRegistry,
    const rhi::semantics::NativeBackendCapabilityContract* capabilityContract,
    rhi::CommandValidationResult& validation)
{
    if (!nativePayload) {
        active = false;
        validation = commandFail("RHI-VULKAN-NATIVE-RENDER-PASS-PAYLOAD-MISSING", commandCount);
        return nullptr;
    }
    if (nativePayload) {
        // Resolve and retain every RHI color attachment. MRT lowering must not
        // silently collapse RenderPassDesc::colorAttachments to slot zero.
        std::pmr::vector<std::shared_ptr<VulkanRhiNativeResourcePayload>> colorTargets;
        colorTargets.reserve(desc.colorAttachments.size());
        if (resourceRegistry) {
            for (const auto& attachment : desc.colorAttachments) {
                auto target = resourceRegistry->payload(attachment.target);
                if (!target) {
                    validation = commandFail(
                        "Vulkan RHI native render pass requires tracked color attachments",
                        commandCount);
                    active = false;
                    return nullptr;
                }
                nativePayload->retainResource(target);
                colorTargets.push_back(std::move(target));
            }
        }
        const auto depthTarget = desc.hasDepthStencilAttachment && resourceRegistry
            ? resourceRegistry->payload(desc.depthStencilAttachment.target)
            : nullptr;
        nativePayload->retainResource(depthTarget);
        std::pmr::string diagnostics;
        if ((colorTargets.empty() && !depthTarget)
            || !nativePayload->recordRenderPassBegin(
                desc, colorTargets, depthTarget.get(), diagnostics)) {
            validation = commandFail(diagnostics.empty()
                    ? "Vulkan RHI native render pass requires a tracked color or depth target"
                    : diagnostics,
                commandCount);
            active = false;
            return nullptr;
        }
    }
    validation = commandOk(++commandCount);
    return arp::core::makeUnique<VulkanRhiRenderPassEncoder>(commandCount,
        active,
        recordingIdentity,
        indirectFirstInstanceEnabled,
        indirectAccesses,
        desc,
        std::move(nativePayload),
        std::move(resourceRegistry),
        capabilityContract,
        bindlessExecutable);
}

} // namespace arp::rhi_vulkan

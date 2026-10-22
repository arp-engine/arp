#include "core/memory/cpu_allocator.h"
#include "rhi/backends/webgpu/webgpu_backend_objects.h"
#include "rhi/backends/webgpu/webgpu_native_indirect_processor.h"
#include "rhi/backends/webgpu/webgpu_native_payloads.h"
#include "rhi/backends/webgpu/webgpu_staging_upload_pool.h"
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
#include "rhi/frontend/transfer_readback_borrowed_retirement.h"
#include "rhi/frontend/transfer_validation.h"
#include "rhi/gpu_bindless.h"
#include "rhi/gpu_extensions.h"
#include "rhi/semantics/command_state_lowering.h"
#include "rhi/semantics/native_command_lowering.h"
#include "rhi/semantics/push_constant_validation.h"
#include "rhi/semantics/viewport_scissor_validation.h"

#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <utility>

namespace arp::rhi_webgpu {

namespace {

rhi::CommandValidationResult commandOk(std::uint32_t count)
{
    return {true, "", count, rhi::CommandValidationStatus::Success, ""};
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

rhi::CommandValidationResult commandFail(const std::pmr::string& diagnostics, std::uint32_t count)
{
    return {false, diagnostics, count};
}

std::optional<WebGpuRhiRegisteredResource> findResource(
    const std::shared_ptr<WebGpuRhiResourceRegistry>& registry, rhi::GpuResourceId id)
{
    return registry ? registry->find(id) : std::nullopt;
}

bool hasResourceUsage(rhi::ResourceUsage usage, rhi::ResourceUsage flag)
{
    return static_cast<std::uint32_t>(usage & flag) != 0u;
}

bool hasTextureUsage(rhi::TextureUsage usage, rhi::TextureUsage flag)
{
    return static_cast<std::uint32_t>(usage & flag) != 0u;
}

class WebGpuRhiCommandBuffer final : public rhi::RecordedStateCommandBuffer,
                                     public rhi::BindlessCommandBufferExtension {
public:
    ~WebGpuRhiCommandBuffer() override
    {
        if (!submitted_ && nativePayload_)
            nativePayload_->discardPrototypeBatch();
    }

    WebGpuRhiCommandBuffer(std::uint32_t commandCount,
        std::shared_ptr<WebGpuNativeCommandPayload> nativePayload,
        std::shared_ptr<WebGpuRhiStagingUploadPool> stagingUploadPool,
        std::uint64_t stagingSubmissionSerial,
        core::UniquePtr<core::MutationJournal> states,
        core::UniquePtr<rhi::RecordedStagingUploadJournal> stagingUploads)
        : RecordedStateCommandBuffer(std::move(states), std::move(stagingUploads)),
          commandCount_(commandCount), nativePayload_(std::move(nativePayload)),
          stagingUploadPool_(stagingUploadPool), stagingSubmissionSerial_(stagingSubmissionSerial)
    {}

    std::uint32_t commandCount() const override { return commandCount_; }

    std::shared_ptr<rhi::GpuFence> ownedSubmissionFence() const override
    {
        return nativePayload_ ? nativePayload_->prototypeBatchFence() : nullptr;
    }

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

    rhi::BindlessFaultReport processorFaultReport(std::uint64_t identity) const noexcept override
    {
        return nativePayload_
            ? nativePayload_->processorFaultReport(identity)
            : rhi::BindlessFaultReport{0u, rhi::BindlessFaultValidationStatus::Unavailable, 0u};
    }

    std::shared_ptr<WebGpuNativeCommandPayload> nativePayload() const { return nativePayload_; }

    void markSubmitted() const { submitted_ = true; }

private:
    std::uint32_t commandCount_ = 0;
    std::shared_ptr<WebGpuNativeCommandPayload> nativePayload_;
    std::shared_ptr<WebGpuRhiStagingUploadPool> stagingUploadPool_;
    std::uint64_t stagingSubmissionSerial_ = 0u;
    mutable bool submitted_ = false;
};

class WebGpuRhiRenderBundle;

enum class WebGpuSecondaryCommandKind : std::uint8_t {
    PushConstants,
    DrawIndexed,
    Draw,
};

struct WebGpuSecondaryCommand {
    WebGpuSecondaryCommandKind kind = WebGpuSecondaryCommandKind::Draw;
    rhi::ShaderStage stages = rhi::ShaderStage::None;
    std::uint32_t offsetBytes = 0u;
    std::uint32_t sizeBytes = 0u;
    std::uint32_t pushDataOffset = 0u;
    std::uint32_t vertexOrIndexCount = 0u;
    std::uint32_t instanceCount = 0u;
    std::uint32_t firstElement = 0u;
    std::int32_t vertexOffset = 0;
    std::uint32_t firstInstance = 0u;
};

class WebGpuRhiSecondaryCommandBuffer final : public rhi::SecondaryCommandBuffer {
public:
    WebGpuRhiSecondaryCommandBuffer(core::pmr::Vector<WebGpuSecondaryCommand> commands,
        core::pmr::Vector<std::uint8_t> pushData)
        : commands_(std::move(commands)), pushData_(std::move(pushData))
    {}

    std::uint32_t commandCount() const override
    {
        return static_cast<std::uint32_t>(commands_.size());
    }

    bool softSecondary() const override { return true; }

    const core::pmr::Vector<WebGpuSecondaryCommand>& commands() const { return commands_; }

    const core::pmr::Vector<std::uint8_t>& pushData() const { return pushData_; }

private:
    core::pmr::Vector<WebGpuSecondaryCommand> commands_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::uint8_t> pushData_{&core::defaultCpuMemoryResource()};
};

class WebGpuRhiRenderPassEncoder final : public rhi::RenderPassEncoder,
                                         public rhi::BindlessPassExtension {
public:
    WebGpuRhiRenderPassEncoder(std::uint32_t& commandCount,
        bool& active,
        std::uint64_t recordingIdentity,
        bool indirectFirstInstanceEnabled,
        rhi::BindlessIndirectDeclarations& indirectAccesses,
        const rhi::RenderPassDesc& passDesc,
        std::shared_ptr<WebGpuNativeCommandPayload> nativePayload,
        const rhi::semantics::NativeBackendCapabilityContract* capabilityContract,
        bool bindlessExecutable)
        : commandCount_(commandCount), active_(active), recordingIdentity_(recordingIdentity),
          indirectFirstInstanceEnabled_(indirectFirstInstanceEnabled),
          indirectAccesses_(indirectAccesses), passDesc_(passDesc),
          nativePayload_(std::move(nativePayload)), capabilityContract_(capabilityContract),
          bindlessExecutable_(bindlessExecutable)
    {}

    std::uint64_t recordingIdentity() const noexcept override { return recordingIdentity_; }

    void* queryInterface(std::string_view extension) noexcept override
    {
        return bindlessExecutable_ && extension == rhi::kExtensionBindlessCross
            ? static_cast<rhi::BindlessPassExtension*>(this)
            : nullptr;
    }

    rhi::CommandValidationResult setPipeline(const rhi::RenderPipeline& pipeline) override
    {
        if (boundSnapshot_)
            return commandFail("RHI-BINDLESS-PIPELINE-CHANGE-AFTER-SNAPSHOT", commandCount_);
        indirectAccesses_.clear();
        if (pipeline.nativeHandleExposedToUpperLayer()) {
            return commandFail("WebGPU RHI render pipeline exposed native handle", commandCount_);
        }
        if (!nativePayload_) {
            return commandFail(
                "WebGPU RHI render pipeline bind requires a native command payload", commandCount_);
        }
        const auto authorization = pipeline.bindlessAuthorization();
        if (authorization
            && (!bindlessExecutable_ || !authorization->current(authorization)
                || authorization->plan().execution != rhi::BindlessExecution::Emulated
                || !authorization->plan().executable))
            return commandFail("RHI-WEBGPU-BINDLESS-PIPELINE-AUTHORIZATION-INVALID", commandCount_);
        if (!authorization && !pipeline.shaderReflection().bindlessContractVersion.empty())
            return commandFail("RHI-BINDLESS-RECORD-AUTHORIZATION-INVALID", commandCount_);
        pipelineLayout_ = pipeline.layout();
        shaderReflection_ = &pipeline.shaderReflection();
        pipelineTopology_ = pipeline.primitiveTopology();
        std::pmr::string diagnostics;
        auto nativePipeline = webGpuNativePayloadFor(pipeline);
        if (authorization
            && (!nativePipeline
                || !nativePayload_->retainIndexedPipeline(
                    nativePipeline, authorization, diagnostics)))
            return commandFail(diagnostics, commandCount_);
        if (!nativePipeline
            || !nativePayload_->recordGraphicsPipelineBind(nativePipeline, diagnostics)) {
            return commandFail(diagnostics, commandCount_);
        }
        activeAuthorization_ = authorization;
        activePipelineReport_ = pipeline.validationReport();
        boundDescriptors_.reset();
        pipelineBound_ = true;
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult bindSnapshot(std::shared_ptr<const rhi::BindlessSnapshot> snapshot,
        const rhi::BindlessLoweringPlan& selected,
        std::uint64_t generation,
        const rhi::BindlessAccessDeclarations& accesses) override
    {
        if (!active_ || !bindlessExecutable_ || !activeAuthorization_
            || !activeAuthorization_->current(activeAuthorization_))
            return commandFail("RHI-BINDLESS-RECORD-AUTHORIZATION-INVALID", commandCount_);
        if (boundSnapshot_)
            return commandFail("RHI-BINDLESS-RECORD-SNAPSHOT-ALREADY-BOUND", commandCount_);
        if (!snapshot)
            return commandFail("RHI-BINDLESS-RECORD-SNAPSHOT-MISSING", commandCount_);
        if (!snapshot->sharedOwnerMatches(snapshot))
            return commandFail("RHI-BINDLESS-RECORD-SNAPSHOT-OWNER-MISMATCH", commandCount_);

        const auto& authorizedPlan = activeAuthorization_->plan();
        const auto& artifact = activeAuthorization_->package().artifact();
        if (!selected.success || !selected.executable
            || selected.execution != rhi::BindlessExecution::Emulated
            || selected.bindingModel != rhi::BindlessBindingModel::ScalarExpansion
            || selected.identity != authorizedPlan.identity
            || selected.deviceIdentity != authorizedPlan.deviceIdentity
            || selected.shaderPackageIdentity != artifact.shaderPackageIdentity
            || artifact.execution != rhi::BindlessExecution::Emulated
            || snapshot->plan().identity != authorizedPlan.identity)
            return commandFail("RHI-BINDLESS-RECORD-PLAN-MISMATCH", commandCount_);

        const auto* descriptors = snapshot->descriptorSet();
        if (!descriptors)
            return commandFail("RHI-BINDLESS-RECORD-SNAPSHOT-INVALID", commandCount_);
        auto activeStages = rhi::ShaderStage::None;
        for (const auto& binding : descriptors->layout().bindings)
            activeStages = activeStages
                | (binding.stages & (rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment));
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
        if (!snapshot->faultLatchBuffer() || !snapshot->faultReadbackBuffer())
            return commandFail("RHI-BINDLESS-FAULT-READBACK-UNAVAILABLE", commandCount_);

        valid = rhi::validateBindlessRenderPass(activePipelineReport_, passDesc_);
        if (!valid.success)
            return commandFail(valid.diagnostics, commandCount_);

        const auto capacity = boundDescriptors_.validateSetCapacity(snapshot->setIndex());
        if (!capacity.success)
            return commandFail(capacity.diagnostics, commandCount_);
        valid =
            rhi::validateDescriptorSetBinding(pipelineLayout_, snapshot->setIndex(), *descriptors);
        if (!valid.success)
            return commandFail(valid.diagnostics, commandCount_);
        const auto nativeSet = webGpuNativePayloadFor(*descriptors);
        if (!nativeSet)
            return commandFail("RHI-WEBGPU-BINDLESS-DESCRIPTOR-PAYLOAD-MISSING", commandCount_);

        std::pmr::string diagnostics;
        if (!nativePayload_->retainIndexedSnapshot(snapshot, diagnostics)
            || !nativePayload_->recordBindlessFaultLatchClear(snapshot, diagnostics)
            || !nativePayload_->recordRenderDescriptorSetBind(
                snapshot->setIndex(), nativeSet, nullptr, 0u, diagnostics))
            return commandFail(diagnostics, commandCount_);
        const auto tracked =
            boundDescriptors_.bindSet(snapshot->setIndex(), *descriptors, nullptr, 0u);
        if (!tracked.success)
            return commandFail(tracked.diagnostics, commandCount_);
        boundSnapshot_ = std::move(snapshot);
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult setViewport(const rhi::ViewportState& viewport) override
    {
        const auto valueResult = rhi::semantics::validateViewportCommand(viewport);
        if (!valueResult.success) {
            return commandFail(valueResult.diagnostics, commandCount_);
        }
        if (!pipelineBound_) {
            return commandFail(rhi::semantics::ViewportScissorDiagCode::kNoPipeline
                    + std::pmr::string("WebGPU RHI setViewport requires a bound pipeline"),
                commandCount_);
        }
        if (!nativePayload_) {
            return commandFail(rhi::semantics::ViewportScissorDiagCode::kNoExecPayload
                    + std::pmr::string("WebGPU RHI setViewport requires a native command payload"),
                commandCount_);
        }
        std::pmr::string diagnostics;
        if (!nativePayload_->recordSetViewport(viewport, diagnostics)) {
            return commandFail(diagnostics, commandCount_);
        }
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult setScissor(const rhi::ScissorState& scissor) override
    {
        const auto valueResult = rhi::semantics::validateScissorCommand(scissor);
        if (!valueResult.success) {
            return commandFail(valueResult.diagnostics, commandCount_);
        }
        if (!pipelineBound_) {
            return commandFail(rhi::semantics::ViewportScissorDiagCode::kNoPipeline
                    + std::pmr::string("WebGPU RHI setScissor requires a bound pipeline"),
                commandCount_);
        }
        if (!nativePayload_) {
            return commandFail(rhi::semantics::ViewportScissorDiagCode::kNoExecPayload
                    + std::pmr::string("WebGPU RHI setScissor requires a native command payload"),
                commandCount_);
        }
        std::pmr::string diagnostics;
        if (!nativePayload_->recordSetScissor(scissor, diagnostics)) {
            return commandFail(diagnostics, commandCount_);
        }
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult bindVertexBuffer(
        std::uint32_t slot, const rhi::GpuBuffer& buffer, std::uint64_t offsetBytes) override
    {
        if (!hasResourceUsage(buffer.desc().usage, rhi::ResourceUsage::VertexBuffer)
            || (offsetBytes & 3u) != 0u || offsetBytes >= buffer.desc().sizeBytes) {
            return commandFail(
                "WebGPU RHI vertex bind requires VertexBuffer usage and a valid aligned offset",
                commandCount_);
        }
        if (!nativePayload_) {
            return commandFail(
                "WebGPU RHI bindVertexBuffer requires a native command payload", commandCount_);
        }
        const auto nativeBuffer = webGpuNativePayloadFor(buffer);
        std::pmr::string diagnostics;
        if (!nativeBuffer
            || !nativePayload_->recordVertexBufferBind(
                slot, *nativeBuffer, offsetBytes, diagnostics)) {
            return commandFail(diagnostics, commandCount_);
        }
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult bindIndexBuffer(
        const rhi::GpuBuffer& buffer, rhi::IndexFormat format, std::uint64_t offsetBytes) override
    {
        const auto indexByteCount = format == rhi::IndexFormat::Uint16 ? 2u : 4u;
        if (!hasResourceUsage(buffer.desc().usage, rhi::ResourceUsage::IndexBuffer)
            || (offsetBytes & 3u) != 0u || offsetBytes > buffer.desc().sizeBytes
            || buffer.desc().sizeBytes - offsetBytes < indexByteCount) {
            return commandFail(
                "WebGPU RHI index bind requires IndexBuffer usage and a valid aligned range",
                commandCount_);
        }
        if (!nativePayload_) {
            return commandFail(
                "WebGPU RHI bindIndexBuffer requires a native command payload", commandCount_);
        }
        const auto nativeBuffer = webGpuNativePayloadFor(buffer);
        std::pmr::string diagnostics;
        if (!nativeBuffer
            || !nativePayload_->recordIndexBufferBind(
                *nativeBuffer, format, offsetBytes, diagnostics)) {
            return commandFail(diagnostics, commandCount_);
        }
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult bindDescriptorSet(std::uint32_t set,
        const rhi::DescriptorSet& descriptorSet,
        const std::uint32_t* dynamicOffsets,
        std::uint32_t dynamicOffsetCount) override
    {
        if (activeAuthorization_)
            return commandFail("RHI-BINDLESS-RECORD-SNAPSHOT-REQUIRED", commandCount_);
        const auto trackingCapacity = boundDescriptors_.validateSetCapacity(set);
        if (!trackingCapacity.success)
            return commandFail(trackingCapacity.diagnostics, commandCount_);
        if (descriptorSet.stats().nativeHandleExposedToUpperLayer) {
            return commandFail("WebGPU RHI descriptor set exposed native handle", commandCount_);
        }
        const auto resources = rhi::validateDescriptorResources(
            descriptorSet, rhi::DescriptorContentRequirement::WrittenElements);
        if (!resources.success)
            return commandFail(resources.diagnostics, commandCount_);
        const auto dynamicValidation = rhi::validateDynamicOffsetArguments(
            descriptorSet.layout(), dynamicOffsets, dynamicOffsetCount);
        if (!dynamicValidation.success) {
            return commandFail(dynamicValidation.diagnostics, commandCount_);
        }
        if (!nativePayload_) {
            return commandFail(
                "WebGPU RHI render descriptor bind requires a native command payload",
                commandCount_);
        }
        std::pmr::string diagnostics;
        auto nativeDescriptorSet = webGpuNativePayloadFor(descriptorSet);
        if (!nativeDescriptorSet) {
            diagnostics = "RHI-WEBGPU-DESCRIPTOR-SET-NATIVE-PAYLOAD-MISSING";
        }
        if (!nativeDescriptorSet
            || !nativePayload_->recordRenderDescriptorSetBind(
                set, nativeDescriptorSet, dynamicOffsets, dynamicOffsetCount, diagnostics)) {
            return commandFail(diagnostics, commandCount_);
        }
        const auto tracked =
            boundDescriptors_.bindSet(set, descriptorSet, dynamicOffsets, dynamicOffsetCount);
        if (!tracked.success)
            return commandFail(tracked.diagnostics, commandCount_);
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult bindDescriptorTable(
        std::uint32_t set, const rhi::DescriptorTable& table) override
    {
        if (activeAuthorization_)
            return commandFail("RHI-BINDLESS-RECORD-SNAPSHOT-REQUIRED", commandCount_);
        const auto trackingCapacity = boundDescriptors_.validateTableCapacity(set);
        if (!trackingCapacity.success)
            return commandFail(trackingCapacity.diagnostics, commandCount_);
        if (!pipelineLayout_)
            return commandFail("pipeline must be bound before descriptor table", commandCount_);
        const auto slot =
            rhi::validateDescriptorTableSlot(pipelineLayout_->desc(), set, table.desc());
        if (!slot.success)
            return commandFail(slot.diagnostics, commandCount_);
        if (table.stats().nativeHandleExposedToUpperLayer) {
            return commandFail("WebGPU RHI descriptor table exposed native handle", commandCount_);
        }
        const auto resources = rhi::validateDescriptorResources(
            table, rhi::DescriptorContentRequirement::CompleteLayout);
        if (!resources.success)
            return commandFail(resources.diagnostics, commandCount_);
        if (!nativePayload_) {
            return commandFail("WebGPU RHI descriptor table bind requires a native command payload",
                commandCount_);
        }
        std::pmr::string diagnostics;
        const auto nativeTable = webGpuNativePayloadFor(table);
        if (!nativeTable
            || !nativePayload_->recordRenderDescriptorSetBind(
                set, nativeTable, nullptr, 0u, diagnostics)) {
            return commandFail(diagnostics, commandCount_);
        }
        const auto tracked = boundDescriptors_.bindTable(set, table);
        if (!tracked.success)
            return commandFail(tracked.diagnostics, commandCount_);
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult pushConstants(rhi::ShaderStage stages,
        std::uint32_t offsetBytes,
        std::uint32_t sizeBytes,
        const void* data) override
    {
        auto cmdResult =
            rhi::semantics::validatePushConstantCommand(stages, offsetBytes, sizeBytes, data);
        if (!cmdResult.success) {
            return commandFail(cmdResult.diagnostics, commandCount_);
        }
        auto bindResult = rhi::semantics::validatePushConstantBinding(pipelineLayout_,
            rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment,
            stages,
            offsetBytes,
            sizeBytes,
            shaderReflection_);
        if (!bindResult.success) {
            return commandFail(bindResult.diagnostics, commandCount_);
        }
        if (pushState_.hasUnconsumedUpdate()) {
            return commandFail(rhi::semantics::PushConstantDiagCode::kDuplicateUnconsumed
                    + std::pmr::string(
                        "push constants already set without intervening draw/dispatch"),
                commandCount_);
        }
        // P2 executable-payload gate: production encoder must fail without
        // native payload.  Only Null/explicit diagnostic fixture may be
        // recording-only.
        if (!nativePayload_) {
            return commandFail(rhi::semantics::PushConstantDiagCode::kNoExecPayload
                    + std::pmr::string("WebGPU push constants require a native command payload"),
                commandCount_);
        }
        std::pmr::string diagnostics;
        if (!nativePayload_->recordPushConstants(
                stages, offsetBytes, sizeBytes, data, diagnostics)) {
            return commandFail(diagnostics, commandCount_);
        }
        pushState_.onPush();
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult executeSecondary(
        const rhi::SecondaryCommandBuffer& commandBuffer) override
    {
        if (activeAuthorization_)
            return commandFail("RHI-BINDLESS-SECONDARY-UNAVAILABLE", commandCount_);
        const auto descriptorUse = boundDescriptors_.validateLiveBindings();
        if (!descriptorUse.success)
            return commandFail(descriptorUse.diagnostics, commandCount_);
        const auto* secondary =
            dynamic_cast<const WebGpuRhiSecondaryCommandBuffer*>(&commandBuffer);
        if (!secondary || !nativePayload_) {
            return commandFail(
                "WebGPU RHI secondary command requires an executable CPU-replay payload",
                commandCount_);
        }
        if (!capabilityContract_) {
            return commandFail(
                "RHI-WEBGPU-SECONDARY-LOWERING-CAPABILITY-CONTRACT-MISSING", commandCount_);
        }
        rhi::semantics::CommandWorkloadDesc workload;
        workload.commandCount = secondary->commandCount();
        workload.allowCpuReplay = secondary->softSecondary();
        workload.usesRenderPass = true;
        for (const auto& command : secondary->commands()) {
            workload.usesPushConstants = workload.usesPushConstants
                || command.kind == WebGpuSecondaryCommandKind::PushConstants;
        }
        const auto decision = rhi::semantics::lowerCommandWorkload(*capabilityContract_,
            workload,
            rhi::semantics::CommandRecordingIntent::SoftSecondaryCommandBuffer);
        const auto loweringUse = rhi::semantics::validateNativeCommandLoweringUse(decision,
            rhi::semantics::NativeCommandSemantic::WebGpuSecondaryCommandReplay,
            rhi::FallbackMode::CpuReplay,
            "WebGPU secondary command replay");
        if (!loweringUse.success) {
            return commandFail(loweringUse.diagnostics, commandCount_);
        }
        for (const auto& command : secondary->commands()) {
            std::pmr::string diagnostics;
            switch (command.kind) {
            case WebGpuSecondaryCommandKind::PushConstants: {
                if (command.pushDataOffset > secondary->pushData().size()
                    || command.sizeBytes > secondary->pushData().size() - command.pushDataOffset) {
                    return commandFail(rhi::semantics::PushConstantDiagCode::kSecondaryDataRange
                            + std::pmr::string("secondary push-constant payload range is invalid"),
                        commandCount_);
                }
                const auto* data = secondary->pushData().data() + command.pushDataOffset;
                auto cmdResult = rhi::semantics::validatePushConstantCommand(
                    command.stages, command.offsetBytes, command.sizeBytes, data);
                if (!cmdResult.success) {
                    return commandFail(cmdResult.diagnostics, commandCount_);
                }
                auto bindResult = rhi::semantics::validatePushConstantBinding(pipelineLayout_,
                    rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment,
                    command.stages,
                    command.offsetBytes,
                    command.sizeBytes,
                    shaderReflection_);
                if (!bindResult.success) {
                    return commandFail(bindResult.diagnostics, commandCount_);
                }
                if (pushState_.hasUnconsumedUpdate()) {
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
                pushState_.onPush();
                break;
            }
            case WebGpuSecondaryCommandKind::DrawIndexed:
                if (!nativePayload_->recordDrawIndexed(command.vertexOrIndexCount,
                        command.instanceCount,
                        command.firstElement,
                        command.vertexOffset,
                        command.firstInstance,
                        diagnostics)) {
                    return commandFail(diagnostics, commandCount_);
                }
                pushState_.onConsume();
                break;
            case WebGpuSecondaryCommandKind::Draw:
                if (!nativePayload_->recordDraw(command.vertexOrIndexCount,
                        command.instanceCount,
                        command.firstElement,
                        command.firstInstance,
                        diagnostics)) {
                    return commandFail(diagnostics, commandCount_);
                }
                pushState_.onConsume();
                break;
            }
        }
        commandCount_ += secondary->commandCount();
        return commandOk(commandCount_);
    }

    rhi::CommandValidationResult executeRenderBundle(const rhi::RenderBundle& bundle) override;

    rhi::CommandValidationResult drawIndexed(std::uint32_t indexCount,
        std::uint32_t instanceCount,
        std::uint32_t firstIndex,
        std::int32_t vertexOffset,
        std::uint32_t firstInstance) override
    {
        if (activeAuthorization_ && !boundSnapshot_)
            return commandFail("RHI-BINDLESS-RECORD-SNAPSHOT-REQUIRED", commandCount_);
        const auto descriptorUse = boundDescriptors_.validateLiveBindings();
        if (!descriptorUse.success)
            return commandFail(descriptorUse.diagnostics, commandCount_);
        if (indexCount == 0) {
            return commandFail("WebGPU RHI draw indexed requires indices", commandCount_);
        }
        if (!nativePayload_) {
            return commandFail(
                "WebGPU RHI indexed draw requires a native command payload", commandCount_);
        }
        std::pmr::string diagnostics;
        if (!nativePayload_->recordDrawIndexed(
                indexCount, instanceCount, firstIndex, vertexOffset, firstInstance, diagnostics)) {
            return commandFail(diagnostics, commandCount_);
        }
        pushState_.onConsume();
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult drawIndexedIndirect(const rhi::GpuBuffer& indirectBuffer,
        std::uint64_t indirectOffset,
        std::uint32_t drawCount,
        std::uint32_t stride) override
    {
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
        if (drawCount == 0u || stride < sizeof(rhi::DrawIndexedIndirectCommand)
            || (stride & 3u) != 0u || (indirectOffset & 3u) != 0u
            || !hasResourceUsage(indirectBuffer.desc().usage, rhi::ResourceUsage::IndirectBuffer)
            || indirectOffset > indirectBuffer.desc().sizeBytes
            || indirectBuffer.desc().sizeBytes - indirectOffset
                < sizeof(rhi::DrawIndexedIndirectCommand)
            || drawCount - 1u > (indirectBuffer.desc().sizeBytes - indirectOffset
                                    - sizeof(rhi::DrawIndexedIndirectCommand))
                    / stride) {
            return commandFail(
                "WebGPU RHI indirect draw has invalid args range or stride", commandCount_);
        }
        if (!nativePayload_) {
            return commandFail(
                "WebGPU RHI indirect draw requires a native command payload", commandCount_);
        }
        const auto nativeBuffer = webGpuNativePayloadFor(indirectBuffer);
        std::pmr::string diagnostics;
        if (!nativeBuffer
            || !nativePayload_->recordDrawIndexedIndirect(
                *nativeBuffer, indirectOffset, drawCount, stride, diagnostics)) {
            return commandFail(diagnostics.empty()
                    ? "WebGPU RHI indirect draw requires a tracked args buffer"
                    : diagnostics,
                commandCount_);
        }
        pushState_.onConsume();
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult drawIndirectPacket(
        const rhi::BindlessIndirectProcessor& processor) override
    {
        if (!active_ || !activeAuthorization_ || !boundSnapshot_ || !nativePayload_)
            return commandFail("RHI-BINDLESS-RECORD-SNAPSHOT-REQUIRED", commandCount_);
        const auto* native = dynamic_cast<const WebGpuNativeIndirectProcessor*>(&processor);
        if (!native)
            return commandFail("RHI-WEBGPU-INDIRECT-PROCESSOR-PAYLOAD-MISSING", commandCount_);
        const auto valid = boundDescriptors_.validateLiveBindings();
        if (!valid.success)
            return commandFail(valid.diagnostics, commandCount_);
        core::pmr::String diagnostic;
        if (!nativePayload_->recordIndirectProcessorDraw(
                *native, *boundSnapshot_, *activeAuthorization_, indirectAccesses_, diagnostic))
            return commandFail(diagnostic, commandCount_);
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
        if (activeAuthorization_)
            return commandFail("RHI-BINDLESS-INDIRECT-DRAW-UNAVAILABLE", commandCount_);
        const auto descriptorUse = boundDescriptors_.validateLiveBindings();
        if (!descriptorUse.success)
            return commandFail(descriptorUse.diagnostics, commandCount_);
        if (maxDrawCount == 0u || stride < sizeof(rhi::DrawIndexedIndirectCommand)
            || (stride & 3u) != 0u || (argsOffset & 3u) != 0u
            || !hasResourceUsage(argsBuffer.desc().usage, rhi::ResourceUsage::IndirectBuffer)
            || !hasResourceUsage(countBuffer.desc().usage, rhi::ResourceUsage::IndirectBuffer)
            || argsOffset > argsBuffer.desc().sizeBytes
            || argsBuffer.desc().sizeBytes - argsOffset < sizeof(rhi::DrawIndexedIndirectCommand)
            || maxDrawCount - 1u > (argsBuffer.desc().sizeBytes - argsOffset
                                       - sizeof(rhi::DrawIndexedIndirectCommand))
                    / stride
            || (countOffset & 3u) != 0u || countOffset > countBuffer.desc().sizeBytes
            || countBuffer.desc().sizeBytes - countOffset < sizeof(std::uint32_t)) {
            return commandFail(
                "WebGPU RHI indirect draw count has invalid args/count range or stride",
                commandCount_);
        }
        if (!nativePayload_) {
            return commandFail(
                "WebGPU RHI indirect draw count requires a native command payload", commandCount_);
        }
        const auto nativeArgs = webGpuNativePayloadFor(argsBuffer);
        const auto nativeCount = webGpuNativePayloadFor(countBuffer);
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
                    ? "WebGPU RHI indirect draw count requires tracked args and count buffers"
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
        if (activeAuthorization_ && !boundSnapshot_)
            return commandFail("RHI-BINDLESS-RECORD-SNAPSHOT-REQUIRED", commandCount_);
        const auto descriptorUse = boundDescriptors_.validateLiveBindings();
        if (!descriptorUse.success)
            return commandFail(descriptorUse.diagnostics, commandCount_);
        if (vertexCount == 0) {
            return commandFail("WebGPU RHI draw requires vertices", commandCount_);
        }
        if (!nativePayload_) {
            return commandFail("WebGPU RHI draw requires a native command payload", commandCount_);
        }
        std::pmr::string diagnostics;
        if (!nativePayload_->recordDraw(
                vertexCount, instanceCount, firstVertex, firstInstance, diagnostics)) {
            return commandFail(diagnostics, commandCount_);
        }
        pushState_.onConsume();
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult writeTimestamp(
        const rhi::QueryPool& queryPool, std::uint32_t queryIndex) override
    {
        if (!capabilityContract_) {
            return commandFail(
                "WebGPU RHI timestamp write has no capability contract", commandCount_);
        }
        rhi::semantics::CommandWorkloadDesc workload;
        workload.label = "webgpu-render-timestamp";
        workload.commandCount = 1u;
        workload.usesRenderPass = true;
        workload.usesTimestampQuery = true;
        const auto decision = rhi::semantics::lowerCommandWorkload(*capabilityContract_,
            workload,
            rhi::semantics::CommandRecordingIntent::PrimaryCommandBuffer);
        if (!decision.success) {
            return commandFail(decision.diagnostics, commandCount_);
        }
        const auto loweringUse = rhi::semantics::validateNativeCommandLoweringUse(decision,
            rhi::semantics::NativeCommandSemantic::WebGpuTimestampQuery,
            rhi::FallbackMode::Native,
            "WebGPU timestamp query");
        if (!loweringUse.success) {
            return commandFail(loweringUse.diagnostics, commandCount_);
        }
        if (!nativePayload_) {
            return commandFail(
                "WebGPU RHI timestamp write requires a native command payload", commandCount_);
        }
        std::pmr::string diagnostics;
        if (!nativePayload_->recordTimestamp(queryPool, queryIndex, diagnostics)) {
            return commandFail(diagnostics, commandCount_);
        }
        return commandOk(++commandCount_);
    }

    /// WebGPU does not support subpasses. The FrameGraph compiler guarantees
    /// this is never called on WebGPU by auto-lowering SubpassGraph to
    /// separate passes. If called directly, it is a programming error.
    rhi::CommandValidationResult nextSubpass() override
    {
        return commandFail("WebGPU RHI does not support subpasses", commandCount_);
    }

    std::uint32_t currentSubpassIndex() const override { return 0; }

    rhi::CommandValidationResult end() override
    {
        if (activeAuthorization_ && !boundSnapshot_)
            return commandFail("RHI-BINDLESS-RECORD-SNAPSHOT-REQUIRED", commandCount_);
        if (!activeAuthorization_ && boundSnapshot_)
            return commandFail("RHI-BINDLESS-RECORD-AUTHORIZATION-INVALID", commandCount_);
        if (!nativePayload_) {
            return commandFail(
                "WebGPU RHI render pass end requires a native command payload", commandCount_);
        }
        std::pmr::string diagnostics;
        if (!nativePayload_->recordRenderPassEnd(diagnostics)) {
            return commandFail(diagnostics, commandCount_);
        }
        active_ = false;
        pushState_.onPassEnd();
        boundDescriptors_.reset();
        indirectAccesses_.clear();
        if (boundSnapshot_) {
            const auto readback =
                nativePayload_->scheduleBindlessFaultReadback(boundSnapshot_, diagnostics);
            if (!readback)
                return commandFail(diagnostics, commandCount_);
            boundSnapshot_.reset();
            activeAuthorization_.reset();
        }
        return commandOk(++commandCount_);
    }

private:
    rhi::BoundDescriptorSlots boundDescriptors_;
    std::uint32_t& commandCount_;
    bool& active_;
    std::uint64_t recordingIdentity_ = 0u;
    bool indirectFirstInstanceEnabled_ = false;
    rhi::BindlessIndirectDeclarations& indirectAccesses_;
    rhi::RenderPassDesc passDesc_;
    std::shared_ptr<WebGpuNativeCommandPayload> nativePayload_;
    const rhi::semantics::NativeBackendCapabilityContract* capabilityContract_ = nullptr;
    const rhi::PipelineLayout* pipelineLayout_ = nullptr;
    const rhi::ShaderReflectionDesc* shaderReflection_ = nullptr;
    rhi::PrimitiveTopology pipelineTopology_ = rhi::PrimitiveTopology::TriangleList;
    rhi::PipelineValidationReport activePipelineReport_;
    rhi::semantics::PushConstantConsumptionState pushState_;
    bool pipelineBound_ = false; // L2 gate for setViewport/setScissor
    bool bindlessExecutable_ = false;
    std::shared_ptr<const rhi::BindlessAuthorization> activeAuthorization_;
    std::shared_ptr<const rhi::BindlessSnapshot> boundSnapshot_;
};

class WebGpuRhiComputePassEncoder final : public rhi::ComputePassEncoder,
                                          public rhi::BindlessPassExtension {
public:
    WebGpuRhiComputePassEncoder(std::uint32_t& commandCount,
        bool& active,
        std::shared_ptr<WebGpuNativeCommandPayload> nativePayload,
        const rhi::semantics::NativeBackendCapabilityContract* capabilityContract,
        bool bindlessExecutable)
        : commandCount_(commandCount), active_(active), nativePayload_(std::move(nativePayload)),
          capabilityContract_(capabilityContract), bindlessExecutable_(bindlessExecutable),
          recordingIdentity_(rhi::allocateGpuResourceId().value)
    {}

    std::uint64_t recordingIdentity() const noexcept override { return recordingIdentity_; }

    void* queryInterface(std::string_view extension) noexcept override
    {
        return bindlessExecutable_ && extension == rhi::kExtensionBindlessCross
            ? static_cast<rhi::BindlessPassExtension*>(this)
            : nullptr;
    }

    rhi::CommandValidationResult bindSnapshot(std::shared_ptr<const rhi::BindlessSnapshot> snapshot,
        const rhi::BindlessLoweringPlan& selected,
        std::uint64_t generation,
        const rhi::BindlessAccessDeclarations& accesses) override
    {
        if (!active_)
            return commandFail("RHI-WEBGPU-BINDLESS-PASS-NOT-ACTIVE", commandCount_);
        if (!bindlessExecutable_ || !activeAuthorization_
            || !activeAuthorization_->current(activeAuthorization_))
            return commandFail("RHI-BINDLESS-RECORD-AUTHORIZATION-INVALID", commandCount_);
        if (boundSnapshot_)
            return commandFail("RHI-BINDLESS-RECORD-SNAPSHOT-ALREADY-BOUND", commandCount_);
        if (!snapshot)
            return commandFail("RHI-BINDLESS-RECORD-SNAPSHOT-MISSING", commandCount_);
        if (!snapshot->sharedOwnerMatches(snapshot))
            return commandFail("RHI-BINDLESS-RECORD-SNAPSHOT-OWNER-MISMATCH", commandCount_);

        const auto& authorizedPlan = activeAuthorization_->plan();
        const auto& artifact = activeAuthorization_->package().artifact();
        if (!selected.success || !selected.executable
            || selected.execution != rhi::BindlessExecution::Emulated
            || selected.bindingModel != rhi::BindlessBindingModel::ScalarExpansion
            || selected.identity != authorizedPlan.identity
            || selected.deviceIdentity != authorizedPlan.deviceIdentity
            || selected.shaderPackageIdentity != artifact.shaderPackageIdentity
            || artifact.execution != rhi::BindlessExecution::Emulated
            || snapshot->plan().identity != authorizedPlan.identity)
            return commandFail("RHI-BINDLESS-RECORD-PLAN-MISMATCH", commandCount_);

        auto valid =
            snapshot->validateForRecord(selected, generation, accesses, rhi::ShaderStage::Compute);
        if (!valid.success)
            return commandFail(valid.diagnostics, commandCount_);
        valid = snapshot->validateForRecord(
            authorizedPlan, generation, accesses, rhi::ShaderStage::Compute);
        if (!valid.success)
            return commandFail(valid.diagnostics, commandCount_);
        if (!snapshot->faultLatchBuffer() || !snapshot->faultReadbackBuffer())
            return commandFail("RHI-BINDLESS-FAULT-READBACK-UNAVAILABLE", commandCount_);

        const auto* descriptors = snapshot->descriptorSet();
        if (!descriptors)
            return commandFail("RHI-BINDLESS-RECORD-SNAPSHOT-INVALID", commandCount_);
        const auto capacity = boundDescriptors_.validateSetCapacity(snapshot->setIndex());
        if (!capacity.success)
            return commandFail(capacity.diagnostics, commandCount_);
        valid =
            rhi::validateDescriptorSetBinding(pipelineLayout_, snapshot->setIndex(), *descriptors);
        if (!valid.success)
            return commandFail(valid.diagnostics, commandCount_);
        const auto nativeSet = webGpuNativePayloadFor(*descriptors);
        if (!nativeSet)
            return commandFail("RHI-WEBGPU-BINDLESS-DESCRIPTOR-PAYLOAD-MISSING", commandCount_);

        std::pmr::string diagnostics;
        if (!nativePayload_->retainIndexedSnapshot(snapshot, diagnostics)
            || !nativePayload_->recordBindlessFaultLatchClear(snapshot, diagnostics)
            || !nativePayload_->recordDescriptorSetBind(
                snapshot->setIndex(), nativeSet, nullptr, 0u, diagnostics))
            return commandFail(diagnostics, commandCount_);
        const auto tracked =
            boundDescriptors_.bindSet(snapshot->setIndex(), *descriptors, nullptr, 0u);
        if (!tracked.success)
            return commandFail(tracked.diagnostics, commandCount_);
        boundSnapshot_ = std::move(snapshot);
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult setPipeline(const rhi::ComputePipeline& pipeline) override
    {
        if (pipeline.nativeHandleExposedToUpperLayer()) {
            return commandFail("WebGPU RHI compute pipeline exposed native handle", commandCount_);
        }
        if (!nativePayload_) {
            return commandFail("WebGPU RHI compute pipeline bind requires a native command payload",
                commandCount_);
        }
        if (boundSnapshot_)
            return commandFail("RHI-BINDLESS-PIPELINE-CHANGE-AFTER-SNAPSHOT", commandCount_);
        auto authorization = pipeline.bindlessAuthorization();
        if (authorization
            && (!authorization->current(authorization)
                || authorization->plan().execution != rhi::BindlessExecution::Emulated
                || !authorization->plan().executable || !bindlessExecutable_))
            return commandFail("RHI-WEBGPU-BINDLESS-PIPELINE-AUTHORIZATION-INVALID", commandCount_);
        if (!authorization && !pipeline.shaderReflection().bindlessContractVersion.empty())
            return commandFail("RHI-BINDLESS-RECORD-AUTHORIZATION-INVALID", commandCount_);
        pipelineLayout_ = pipeline.layout();
        shaderReflection_ = &pipeline.shaderReflection();
        std::pmr::string diagnostics;
        auto nativePipeline = webGpuNativePayloadFor(pipeline);
        if (authorization) {
            if (!nativePipeline
                || !nativePayload_->retainIndexedPipeline(
                    nativePipeline, authorization, diagnostics))
                return commandFail(diagnostics, commandCount_);
        }
        if (!nativePipeline
            || !nativePayload_->recordComputePipelineBind(nativePipeline, diagnostics))
            return commandFail(diagnostics, commandCount_);
        activeAuthorization_ = std::move(authorization);
        boundDescriptors_.reset();
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult bindDescriptorSet(std::uint32_t set,
        const rhi::DescriptorSet& descriptorSet,
        const std::uint32_t* dynamicOffsets,
        std::uint32_t dynamicOffsetCount) override
    {
        if (activeAuthorization_)
            return commandFail("RHI-BINDLESS-RECORD-SNAPSHOT-REQUIRED", commandCount_);
        const auto trackingCapacity = boundDescriptors_.validateSetCapacity(set);
        if (!trackingCapacity.success)
            return commandFail(trackingCapacity.diagnostics, commandCount_);
        if (descriptorSet.stats().nativeHandleExposedToUpperLayer) {
            return commandFail("WebGPU RHI descriptor set exposed native handle", commandCount_);
        }
        const auto resources = rhi::validateDescriptorResources(
            descriptorSet, rhi::DescriptorContentRequirement::WrittenElements);
        if (!resources.success)
            return commandFail(resources.diagnostics, commandCount_);
        const auto dynamicValidation = rhi::validateDynamicOffsetArguments(
            descriptorSet.layout(), dynamicOffsets, dynamicOffsetCount);
        if (!dynamicValidation.success) {
            return commandFail(dynamicValidation.diagnostics, commandCount_);
        }
        if (!nativePayload_) {
            return commandFail(
                "WebGPU RHI compute descriptor bind requires a native command payload",
                commandCount_);
        }
        std::pmr::string diagnostics;
        auto nativeDescriptorSet = webGpuNativePayloadFor(descriptorSet);
        if (!nativeDescriptorSet
            || !nativePayload_->recordDescriptorSetBind(
                set, nativeDescriptorSet, dynamicOffsets, dynamicOffsetCount, diagnostics)) {
            return commandFail(diagnostics, commandCount_);
        }
        const auto tracked =
            boundDescriptors_.bindSet(set, descriptorSet, dynamicOffsets, dynamicOffsetCount);
        if (!tracked.success)
            return commandFail(tracked.diagnostics, commandCount_);
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult bindDescriptorTable(
        std::uint32_t set, const rhi::DescriptorTable& table) override
    {
        if (activeAuthorization_)
            return commandFail("RHI-BINDLESS-RECORD-SNAPSHOT-REQUIRED", commandCount_);
        const auto trackingCapacity = boundDescriptors_.validateTableCapacity(set);
        if (!trackingCapacity.success)
            return commandFail(trackingCapacity.diagnostics, commandCount_);
        if (!pipelineLayout_)
            return commandFail("pipeline must be bound before descriptor table", commandCount_);
        const auto slot =
            rhi::validateDescriptorTableSlot(pipelineLayout_->desc(), set, table.desc());
        if (!slot.success)
            return commandFail(slot.diagnostics, commandCount_);
        if (table.stats().nativeHandleExposedToUpperLayer) {
            return commandFail("WebGPU RHI descriptor table exposed native handle", commandCount_);
        }
        const auto resources = rhi::validateDescriptorResources(
            table, rhi::DescriptorContentRequirement::CompleteLayout);
        if (!resources.success)
            return commandFail(resources.diagnostics, commandCount_);
        if (!nativePayload_) {
            return commandFail("WebGPU RHI descriptor table bind requires a native command payload",
                commandCount_);
        }
        std::pmr::string diagnostics;
        const auto nativeTable = webGpuNativePayloadFor(table);
        if (!nativeTable
            || !nativePayload_->recordDescriptorSetBind(
                set, nativeTable, nullptr, 0u, diagnostics)) {
            return commandFail(diagnostics, commandCount_);
        }
        const auto tracked = boundDescriptors_.bindTable(set, table);
        if (!tracked.success)
            return commandFail(tracked.diagnostics, commandCount_);
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult pushConstants(rhi::ShaderStage stages,
        std::uint32_t offsetBytes,
        std::uint32_t sizeBytes,
        const void* data) override
    {
        auto cmdResult =
            rhi::semantics::validatePushConstantCommand(stages, offsetBytes, sizeBytes, data);
        if (!cmdResult.success) {
            return commandFail(cmdResult.diagnostics, commandCount_);
        }
        auto bindResult = rhi::semantics::validatePushConstantBinding(pipelineLayout_,
            rhi::ShaderStage::Compute,
            stages,
            offsetBytes,
            sizeBytes,
            shaderReflection_);
        if (!bindResult.success) {
            return commandFail(bindResult.diagnostics, commandCount_);
        }
        if (pushState_.hasUnconsumedUpdate()) {
            return commandFail(rhi::semantics::PushConstantDiagCode::kDuplicateUnconsumed
                    + std::pmr::string("push constants already set without intervening dispatch"),
                commandCount_);
        }
        // P2 executable-payload gate.
        if (!nativePayload_) {
            return commandFail(rhi::semantics::PushConstantDiagCode::kNoExecPayload
                    + std::pmr::string(
                        "WebGPU compute push constants require a native command payload"),
                commandCount_);
        }
        std::pmr::string diagnostics;
        if (!nativePayload_->recordPushConstants(
                stages, offsetBytes, sizeBytes, data, diagnostics)) {
            return commandFail(diagnostics, commandCount_);
        }
        pushState_.onPush();
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult dispatchIndirect(
        const rhi::GpuBuffer& indirectBuffer, std::uint64_t indirectOffset) override
    {
        if (activeAuthorization_)
            return commandFail("RHI-BINDLESS-INDIRECT-DISPATCH-UNSUPPORTED", commandCount_);
        const auto descriptorUse = boundDescriptors_.validateLiveBindings();
        if (!descriptorUse.success)
            return commandFail(descriptorUse.diagnostics, commandCount_);
        if ((indirectOffset & 3u) != 0u
            || !hasResourceUsage(indirectBuffer.desc().usage, rhi::ResourceUsage::IndirectBuffer)
            || indirectOffset > indirectBuffer.desc().sizeBytes
            || indirectBuffer.desc().sizeBytes - indirectOffset < 12u) {
            return commandFail(
                "WebGPU RHI indirect dispatch has invalid args range or alignment", commandCount_);
        }
        if (!nativePayload_) {
            return commandFail(
                "WebGPU RHI indirect dispatch requires a native command payload", commandCount_);
        }
        const auto nativeBuffer = webGpuNativePayloadFor(indirectBuffer);
        std::pmr::string diagnostics;
        if (!nativeBuffer
            || !nativePayload_->recordDispatchIndirect(
                *nativeBuffer, indirectOffset, diagnostics)) {
            return commandFail(diagnostics.empty()
                    ? "WebGPU RHI indirect dispatch requires a tracked args buffer"
                    : diagnostics,
                commandCount_);
        }
        pushState_.onConsume();
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult dispatch(
        std::uint32_t x, std::uint32_t y, std::uint32_t z) override
    {
        const auto descriptorUse = boundDescriptors_.validateLiveBindings();
        if (!descriptorUse.success)
            return commandFail(descriptorUse.diagnostics, commandCount_);
        if (activeAuthorization_ && !boundSnapshot_)
            return commandFail("RHI-BINDLESS-RECORD-SNAPSHOT-REQUIRED", commandCount_);
        if (x == 0 || y == 0 || z == 0) {
            return commandFail("WebGPU RHI dispatch requires non-zero groups", commandCount_);
        }
        if (!nativePayload_) {
            return commandFail(
                "WebGPU RHI dispatch requires a native command payload", commandCount_);
        }
        std::pmr::string diagnostics;
        if (!nativePayload_->recordDispatch(x, y, z, diagnostics)) {
            return commandFail(diagnostics, commandCount_);
        }
        pushState_.onConsume();
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult writeTimestamp(
        const rhi::QueryPool& queryPool, std::uint32_t queryIndex) override
    {
        if (!capabilityContract_) {
            return commandFail(
                "WebGPU RHI timestamp write has no capability contract", commandCount_);
        }
        rhi::semantics::CommandWorkloadDesc workload;
        workload.label = "webgpu-compute-timestamp";
        workload.commandCount = 1u;
        workload.usesTimestampQuery = true;
        const auto decision = rhi::semantics::lowerCommandWorkload(*capabilityContract_,
            workload,
            rhi::semantics::CommandRecordingIntent::PrimaryCommandBuffer);
        if (!decision.success) {
            return commandFail(decision.diagnostics, commandCount_);
        }
        const auto loweringUse = rhi::semantics::validateNativeCommandLoweringUse(decision,
            rhi::semantics::NativeCommandSemantic::WebGpuTimestampQuery,
            rhi::FallbackMode::Native,
            "WebGPU timestamp query");
        if (!loweringUse.success) {
            return commandFail(loweringUse.diagnostics, commandCount_);
        }
        if (!nativePayload_) {
            return commandFail(
                "WebGPU RHI timestamp write requires a native command payload", commandCount_);
        }
        std::pmr::string diagnostics;
        if (!nativePayload_->recordTimestamp(queryPool, queryIndex, diagnostics)) {
            return commandFail(diagnostics, commandCount_);
        }
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult end() override
    {
        if (!nativePayload_) {
            return commandFail(
                "WebGPU RHI compute pass end requires a native command payload", commandCount_);
        }
        std::pmr::string diagnostics;
        if (!nativePayload_->recordComputePassEnd(diagnostics)) {
            return commandFail(diagnostics, commandCount_);
        }
        if (activeAuthorization_) {
            if (!boundSnapshot_)
                return commandFail("RHI-BINDLESS-RECORD-SNAPSHOT-REQUIRED", commandCount_);
            if (!nativePayload_->scheduleBindlessFaultReadback(boundSnapshot_, diagnostics))
                return commandFail(diagnostics, commandCount_);
        }
        active_ = false;
        pushState_.onPassEnd();
        boundDescriptors_.reset();
        boundSnapshot_.reset();
        activeAuthorization_.reset();
        return commandOk(++commandCount_);
    }

private:
    rhi::BoundDescriptorSlots boundDescriptors_;
    std::uint64_t recordingIdentity_ = 0u;
    std::uint32_t& commandCount_;
    bool& active_;
    std::shared_ptr<WebGpuNativeCommandPayload> nativePayload_;
    const rhi::semantics::NativeBackendCapabilityContract* capabilityContract_ = nullptr;
    const rhi::PipelineLayout* pipelineLayout_ = nullptr;
    const rhi::ShaderReflectionDesc* shaderReflection_ = nullptr;
    rhi::semantics::PushConstantConsumptionState pushState_;
    std::shared_ptr<const rhi::BindlessAuthorization> activeAuthorization_;
    std::shared_ptr<const rhi::BindlessSnapshot> boundSnapshot_;
    bool bindlessExecutable_ = false;
};

class WebGpuRhiTransferPassEncoder final : public rhi::TransferPassEncoder {
public:
    WebGpuRhiTransferPassEncoder(std::uint32_t& commandCount,
        bool& active,
        std::shared_ptr<WebGpuRhiResourceRegistry> registry,
        std::shared_ptr<WebGpuNativeCommandPayload> nativePayload,
        std::shared_ptr<WebGpuRhiStagingUploadPool> stagingUploadPool,
        const rhi::semantics::NativeBackendCapabilityContract* capabilityContract)
        : commandCount_(commandCount), active_(active), registry_(std::move(registry)),
          nativePayload_(std::move(nativePayload)), capabilityContract_(capabilityContract),
          stagingUploadPool_(std::move(stagingUploadPool))
    {}

    rhi::CommandValidationResult copyBufferToBuffer(std::uint64_t sizeBytes) override
    {
        if (sizeBytes == 0u) {
            return commandFail("WebGPU RHI copy requires non-zero bytes", commandCount_);
        }
        // This legacy overload carries no source or destination resource ID,
        // offsets, or usage contract. It can never lower to
        // wgpuCommandEncoderCopyBufferToBuffer; use BufferCopyDesc instead.
        return commandFail(
            "WebGPU RHI buffer copy requires explicit source and destination resources",
            commandCount_);
    }

    rhi::CommandValidationResult copyBufferToBuffer(const rhi::BufferCopyDesc& desc) override
    {
        if (!desc.sourceBuffer.isValid() || !desc.destinationBuffer.isValid()
            || desc.sizeBytes == 0u || (desc.sourceOffsetBytes & 3u) != 0u
            || (desc.destinationOffsetBytes & 3u) != 0u || (desc.sizeBytes & 3u) != 0u) {
            return commandFail(
                "WebGPU RHI buffer copy requires valid resources and 4-byte-aligned ranges",
                commandCount_);
        }
        const auto source = findResource(registry_, desc.sourceBuffer);
        const auto destination = findResource(registry_, desc.destinationBuffer);
        if (destination && destination->gpuTransferWritesSealed)
            return commandFail("RHI-BUFFER-GPU-WRITES-SEALED", commandCount_);
        if (!source || !destination || source->kind != rhi::DescriptorResourceKind::Buffer
            || destination->kind != rhi::DescriptorResourceKind::Buffer
            || !hasResourceUsage(source->bufferUsage, rhi::ResourceUsage::CopySrc)
            || !hasResourceUsage(destination->bufferUsage, rhi::ResourceUsage::CopyDst)
            || desc.sourceOffsetBytes > source->bufferDesc.sizeBytes
            || desc.sizeBytes > source->bufferDesc.sizeBytes - desc.sourceOffsetBytes
            || desc.destinationOffsetBytes > destination->bufferDesc.sizeBytes
            || desc.sizeBytes > destination->bufferDesc.sizeBytes - desc.destinationOffsetBytes) {
            return commandFail(
                "WebGPU RHI buffer copy resource usage or range is invalid", commandCount_);
        }
        if (!nativePayload_) {
            return commandFail(
                "WebGPU RHI buffer copy requires a native command payload", commandCount_);
        }
        std::pmr::string diagnostics;
        if (!source->buffer || !destination->buffer
            || !nativePayload_->recordBufferCopy(
                *source->buffer, *destination->buffer, desc, diagnostics)) {
            return commandFail(diagnostics, commandCount_);
        }
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult copyBufferToTexture(
        const rhi::BufferToTextureCopyDesc& desc) override
    {
        if (!desc.mipLayouts.empty()) {
            return commandFail(
                "RHI-WEBGPU-BUFFER-TEXTURE-PER-MIP-LAYOUT-NOT-LOWERED", commandCount_);
        }
        if (!desc.sourceBuffer.isValid() || !desc.destinationTexture.isValid()) {
            return commandFail(
                "WebGPU RHI buffer-to-texture copy requires resources", commandCount_);
        }
        const auto source = findResource(registry_, desc.sourceBuffer);
        const auto destination = findResource(registry_, desc.destinationTexture);
        if (!source || !destination || source->kind != rhi::DescriptorResourceKind::Buffer
            || destination->kind != rhi::DescriptorResourceKind::TextureView
            || !hasResourceUsage(source->bufferUsage, rhi::ResourceUsage::CopySrc)
            || !hasTextureUsage(destination->textureUsage, rhi::TextureUsage::CopyDst)
            || !rhi::semantics::TextureTransferValidation::copyLayoutValid(destination->textureDesc,
                desc.textureSubresources,
                desc.extent,
                desc.bytesPerRow,
                desc.rowsPerImage,
                desc.bufferOffsetBytes,
                source->bufferDesc.sizeBytes)) {
            return commandFail(
                "WebGPU RHI buffer-to-texture copy resource, range, format or layout is invalid",
                commandCount_);
        }
        if (!nativePayload_) {
            return commandFail(
                "WebGPU RHI buffer-to-texture copy requires a native command payload",
                commandCount_);
        }
        std::pmr::string diagnostics;
        if (!source->buffer || !destination->texture
            || !nativePayload_->recordBufferToTextureCopy(
                *source->buffer, *destination->texture, desc, diagnostics)) {
            return commandFail(diagnostics, commandCount_);
        }
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult copyTextureToBuffer(
        const rhi::TextureToBufferCopyDesc& desc) override
    {
        if (!desc.mipLayouts.empty()) {
            return commandFail(
                "RHI-WEBGPU-TEXTURE-BUFFER-PER-MIP-LAYOUT-NOT-LOWERED", commandCount_);
        }
        if (!desc.sourceTexture.isValid() || !desc.destinationBuffer.isValid()) {
            return commandFail(
                "WebGPU RHI texture-to-buffer copy requires resources", commandCount_);
        }
        const auto source = findResource(registry_, desc.sourceTexture);
        const auto destination = findResource(registry_, desc.destinationBuffer);
        if (destination && destination->gpuTransferWritesSealed)
            return commandFail("RHI-BUFFER-GPU-WRITES-SEALED", commandCount_);
        if (!source || !destination || source->kind != rhi::DescriptorResourceKind::TextureView
            || destination->kind != rhi::DescriptorResourceKind::Buffer
            || !hasTextureUsage(source->textureUsage, rhi::TextureUsage::CopySrc)
            || !hasResourceUsage(destination->bufferUsage, rhi::ResourceUsage::CopyDst)
            || !rhi::semantics::TextureTransferValidation::copyLayoutValid(source->textureDesc,
                desc.textureSubresources,
                desc.extent,
                desc.bytesPerRow,
                desc.rowsPerImage,
                desc.bufferOffsetBytes,
                destination->bufferDesc.sizeBytes)) {
            return commandFail(
                "WebGPU RHI texture-to-buffer copy resource, range, format or layout is invalid",
                commandCount_);
        }
        if (!nativePayload_) {
            return commandFail(
                "WebGPU RHI texture-to-buffer copy requires a native command payload",
                commandCount_);
        }
        std::pmr::string diagnostics;
        if (!source->texture || !destination->buffer
            || !nativePayload_->recordTextureToBufferCopy(
                *source->texture, *destination->buffer, desc, diagnostics)) {
            return commandFail(diagnostics, commandCount_);
        }
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult copyTextureToTexture(
        const rhi::TextureToTextureCopyDesc& desc) override
    {
        if (!desc.mipLayouts.empty()) {
            return commandFail("RHI-WEBGPU-TEXTURE-COPY-PER-MIP-LAYOUT-NOT-LOWERED", commandCount_);
        }
        if (!desc.sourceTexture.isValid() || !desc.destinationTexture.isValid()) {
            return commandFail("WebGPU RHI texture copy requires resources", commandCount_);
        }
        const auto source = findResource(registry_, desc.sourceTexture);
        const auto destination = findResource(registry_, desc.destinationTexture);
        if (!source || !destination || source->kind != rhi::DescriptorResourceKind::TextureView
            || destination->kind != rhi::DescriptorResourceKind::TextureView
            || !hasTextureUsage(source->textureUsage, rhi::TextureUsage::CopySrc)
            || !hasTextureUsage(destination->textureUsage, rhi::TextureUsage::CopyDst)
            || source->textureDesc.format != destination->textureDesc.format
            || desc.sourceSubresources.arrayLayerCount
                != desc.destinationSubresources.arrayLayerCount
            || !rhi::semantics::TextureTransferValidation::copyExtentValid(
                source->textureDesc, desc.sourceSubresources, desc.extent)
            || !rhi::semantics::TextureTransferValidation::copyExtentValid(
                destination->textureDesc, desc.destinationSubresources, desc.extent)) {
            return commandFail(
                "WebGPU RHI texture copy resource, range, format or extent is invalid",
                commandCount_);
        }
        if (!nativePayload_) {
            return commandFail(
                "WebGPU RHI texture-to-texture copy requires a native command payload",
                commandCount_);
        }
        std::pmr::string diagnostics;
        if (!source->texture || !destination->texture
            || !nativePayload_->recordTextureToTextureCopy(
                *source->texture, *destination->texture, desc, diagnostics)) {
            return commandFail(diagnostics, commandCount_);
        }
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult uploadBuffer(const rhi::UploadBufferDesc& desc) override
    {
        if (!desc.backendOwnedStaging || desc.stagingIntent != rhi::StagingIntent::Upload
            || desc.sizeBytes == 0 || !desc.sourceUploadBuffer.isValid()
            || !desc.destinationResource.isValid()) {
            return commandFail("WebGPU RHI upload requires backend-owned staging, source buffer, "
                               "destination buffer, and bytes",
                commandCount_);
        }
        if (!stagingUploadPool_) {
            return commandFail(
                "WebGPU RHI upload requires a backend-owned staging pool", commandCount_);
        }
        const auto source = findResource(registry_, desc.sourceUploadBuffer);
        const auto destination = findResource(registry_, desc.destinationResource);
        if (destination && destination->gpuTransferWritesSealed)
            return commandFail("RHI-BUFFER-GPU-WRITES-SEALED", commandCount_);
        if (!source || !destination || source->kind != rhi::DescriptorResourceKind::Buffer
            || destination->kind != rhi::DescriptorResourceKind::Buffer
            || !hasResourceUsage(source->bufferUsage, rhi::ResourceUsage::CopySrc)
            || !hasResourceUsage(destination->bufferUsage, rhi::ResourceUsage::CopyDst)
            || desc.sizeBytes > source->bufferDesc.sizeBytes
            || desc.sizeBytes > destination->bufferDesc.sizeBytes) {
            return commandFail(
                "WebGPU RHI upload source or destination buffer usage/range is invalid",
                commandCount_);
        }
        if (!nativePayload_) {
            return commandFail(
                "WebGPU RHI upload requires a native command payload", commandCount_);
        }
        // `sourceUploadBuffer` is already the explicit backend-owned upload
        // resource. WebGPU copies it directly in the native command encoder;
        // reserving another staging-ring slice here only accounts phantom
        // memory and can exhaust the ring while the real source buffer remains
        // valid. Texture uploads still reserve their actual caller-byte
        // footprint because queue.writeTexture copies from host memory.
        const auto copy = rhi::BufferCopyDesc{
            desc.sourceUploadBuffer,
            desc.destinationResource,
            0,
            0,
            desc.sizeBytes,
        };
        const auto copied = copyBufferToBuffer(copy);
        return copied;
    }

    rhi::CommandValidationResult uploadTexture(const rhi::UploadTextureDesc& desc) override
    {
        if (!desc.backendOwnedStaging || desc.stagingIntent != rhi::StagingIntent::Upload
            || desc.pixelDataSize == 0 || desc.pixelData == nullptr) {
            return commandFail(
                "WebGPU RHI texture upload requires backend-owned staging and valid pixel data",
                commandCount_);
        }
        if (!desc.mipLayouts.empty() || desc.subresources.mipLevelCount != 1u) {
            return commandFail(
                "RHI-WEBGPU-TEXTURE-UPLOAD-PER-MIP-LAYOUT-NOT-LOWERED", commandCount_);
        }
        if (!stagingUploadPool_) {
            return commandFail(
                "WebGPU RHI texture upload requires a backend-owned staging pool", commandCount_);
        }
        if (!nativePayload_) {
            return commandFail(
                "WebGPU RHI texture upload requires a native command payload", commandCount_);
        }
        // Only the original immediate queue-write payload is implemented.
        // Refuse persistent staging before resource lookup or native mutation.
        if (const auto* id = nativePayload_->preparedRecordingDiagnostic())
            return commandFail(id, commandCount_);
        if (!stagingUploadPool_->prefersQueueWrite()) {
            return commandFail("RHI-WEBGPU-STAGING-COPY-PATH-UNAVAILABLE", commandCount_);
        }
        const auto destination = findResource(registry_, desc.destinationTexture);
        if (!destination || destination->kind != rhi::DescriptorResourceKind::TextureView
            || !hasTextureUsage(destination->textureUsage, rhi::TextureUsage::CopyDst)
            || desc.format == rhi::TextureFormat::Unknown
            || desc.format != destination->textureDesc.format
            || (desc.mipLevel != 0u && desc.mipLevel != desc.subresources.baseMipLevel)
            || (desc.arrayLayer != 0u && desc.arrayLayer != desc.subresources.baseArrayLayer)
            || !rhi::semantics::TextureTransferValidation::textureUploadRegionValid(
                destination->textureDesc, desc.subresources, desc.extent, desc.destinationOffset)
            || !rhi::semantics::TextureTransferValidation::copyLayoutValid(destination->textureDesc,
                desc.subresources,
                desc.extent,
                desc.bytesPerRow,
                desc.rowsPerImage,
                0u,
                desc.pixelDataSize)) {
            return commandFail(
                "WebGPU RHI texture upload destination, range, format or layout is invalid",
                commandCount_);
        }
        const auto sliceCount = rhi::semantics::TextureTransferValidation::copySliceCount(
            destination->textureDesc, desc.subresources, desc.extent);
        const auto rowsPerImage = static_cast<std::uint64_t>(desc.rowsPerImage);
        const auto sliceCount64 = static_cast<std::uint64_t>(sliceCount);
        if (rowsPerImage == 0u || sliceCount64 == 0u
            || rowsPerImage > std::numeric_limits<std::uint64_t>::max() / sliceCount64) {
            return commandFail(
                "WebGPU RHI texture upload staging size overflows the native copy contract",
                commandCount_);
        }
        // rowsPerImage is expressed in texel rows by the RHI/WebGPU copy
        // contract. Compressed payloads contain one physical row per block
        // row, so the final source footprint must use rhi::semantics::TextureTransferValidation::copyRowCount() for the
        // copied extent rather than multiplying the declared texel stride by
        // the whole slice count. The last slice does not require trailing
        // padding after its copied rows.
        const auto sourceRows =
            static_cast<std::uint64_t>(rhi::semantics::TextureTransferValidation::copyRowCount(
                destination->textureDesc.format, desc.extent.height));
        if (sourceRows == 0u
            || (sliceCount64 > 1u
                && rowsPerImage > std::numeric_limits<std::uint64_t>::max() / (sliceCount64 - 1u))
            || (sliceCount64 > 1u
                && rowsPerImage * (sliceCount64 - 1u)
                    > std::numeric_limits<std::uint64_t>::max() - sourceRows)) {
            return commandFail(
                "WebGPU RHI texture upload source row/slice footprint overflows", commandCount_);
        }
        const auto sourceRowCount = rowsPerImage * (sliceCount64 - 1u) + sourceRows;
        if (static_cast<std::uint64_t>(desc.bytesPerRow)
                > std::numeric_limits<std::uint64_t>::max() / sourceRowCount
            || static_cast<std::uint64_t>(desc.bytesPerRow) * sourceRowCount > desc.pixelDataSize) {
            return commandFail(
                "WebGPU RHI texture upload pixel data does not cover the declared row/slice pitch",
                commandCount_);
        }
        // The bridge copies exactly pixelDataSize bytes. This reservation has
        // no GPU submission serial and retires when the immediate API returns.
        const auto immediateQueueWriteReservation = stagingUploadPool_->reserveImmediateQueueWrite({
            desc.label,
            desc.pixelDataSize,
            256u,
            0u,
        });
        if (!immediateQueueWriteReservation.success) {
            return commandStagingFail(immediateQueueWriteReservation.diagnostics,
                commandCount_,
                immediateQueueWriteReservation.diagnosticId);
        }
        std::pmr::string diagnostics;
        const auto recorded =
            nativePayload_->recordTextureUpload(*destination->texture, desc, diagnostics);
        stagingUploadPool_->releaseImmediateQueueWrite(immediateQueueWriteReservation);
        if (!recorded) {
            return commandFail(diagnostics, commandCount_);
        }
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult readbackTexture(const rhi::ReadbackTextureDesc& desc) override
    {
        if (!desc.mipLayouts.empty() || desc.extent.width != 0u || desc.extent.height != 0u
            || desc.extent.depth != 0u || desc.destinationOffsetBytes != 0u
            || desc.bytesPerRow != 0u || desc.rowsPerImage != 0u
            || desc.sourceSubresources.mipLevelCount != 0u
            || desc.sourceSubresources.arrayLayerCount != 0u) {
            return commandFail("RHI-WEBGPU-EXPLICIT-READBACK-LAYOUT-NOT-LOWERED", commandCount_);
        }
        if (!desc.backendOwnedStaging || desc.stagingIntent != rhi::StagingIntent::Readback
            || desc.sizeBytes == 0 || !desc.sourceResource.isValid()
            || !desc.destinationReadbackBuffer.isValid()) {
            return commandFail(
                "WebGPU RHI readback requires backend-owned staging, resources, and bytes",
                commandCount_);
        }
        if (!nativePayload_) {
            return commandFail(
                "WebGPU RHI readback requires a native command payload", commandCount_);
        }
        const auto source = findResource(registry_, desc.sourceResource);
        const auto destination = findResource(registry_, desc.destinationReadbackBuffer);
        if (!source || !destination || source->kind != rhi::DescriptorResourceKind::TextureView
            || destination->kind != rhi::DescriptorResourceKind::Buffer) {
            return commandFail(
                "WebGPU RHI readback requires a tracked source texture and destination buffer",
                commandCount_);
        }
        const auto& texture = source->textureDesc;
        if (texture.extent.width == 0 || texture.extent.height == 0 || texture.extent.depth == 0
            || texture.mipLevelCount != 1 || texture.arrayLayerCount != 1
            || texture.sampleCount != 1) {
            return commandFail("WebGPU RHI readback currently requires a single-sample, "
                               "single-mip, single-layer texture",
                commandCount_);
        }
        const auto bytesPerPixel = rhi::bytesPerPixel(texture.format);
        if (bytesPerPixel == 0) {
            return commandFail(
                "WebGPU RHI readback requires a supported uncompressed texture format",
                commandCount_);
        }
        const auto rowBytes = static_cast<std::uint64_t>(texture.extent.width) * bytesPerPixel;
        const auto alignedRowBytes = (rowBytes + 255u) & ~255ull;
        const auto requiredBytes = alignedRowBytes * texture.extent.height * texture.extent.depth;
        if (alignedRowBytes > std::numeric_limits<std::uint32_t>::max() || requiredBytes == 0
            || requiredBytes > desc.sizeBytes
            || requiredBytes > destination->bufferDesc.sizeBytes) {
            return commandFail(
                "WebGPU RHI readback destination is smaller than the padded texture copy",
                commandCount_);
        }
        const rhi::TextureToBufferCopyDesc copy{
            desc.sourceResource,
            desc.destinationReadbackBuffer,
            rhi::defaultTextureSubresourceRange(texture),
            0,
            static_cast<std::uint32_t>(alignedRowBytes),
            texture.extent.height,
            texture.extent,
        };
        return copyTextureToBuffer(copy);
    }

    rhi::CommandValidationResult end() override
    {
        if (!nativePayload_) {
            return commandFail(
                "WebGPU RHI transfer pass end requires a native command payload", commandCount_);
        }
        active_ = false;
        return commandOk(++commandCount_);
    }

private:
    std::uint32_t& commandCount_;
    bool& active_;
    std::shared_ptr<WebGpuRhiResourceRegistry> registry_;
    std::shared_ptr<WebGpuNativeCommandPayload> nativePayload_;
    const rhi::semantics::NativeBackendCapabilityContract* capabilityContract_ = nullptr;
    std::shared_ptr<WebGpuRhiStagingUploadPool> stagingUploadPool_;
};

class WebGpuRhiCommandEncoder final : public rhi::CommandEncoderBase,
                                      public WebGpuTextureReadbackRecorder,
                                      public rhi::BindlessCommandEncoderExtension {
public:
    WebGpuRhiCommandEncoder(std::shared_ptr<WebGpuRhiResourceRegistry> registry,
        std::shared_ptr<WebGpuNativeCommandPayload> nativePayload,
        const rhi::semantics::NativeBackendCapabilityContract* capabilityContract,
        std::shared_ptr<WebGpuRhiStagingUploadPool> stagingUploadPool,
        std::uint64_t stagingSubmissionSerial,
        std::shared_ptr<std::atomic<std::uint64_t>> completedSubmissionSerial,
        bool bindlessExecutable,
        std::uint32_t maxBindlessIndirectAccesses,
        bool indirectFirstInstanceEnabled)
        : registry_(std::move(registry)), nativePayload_(std::move(nativePayload)),
          capabilityContract_(capabilityContract), stagingUploadPool_(stagingUploadPool),
          stagingSubmissionSerial_(stagingSubmissionSerial),
          completedSubmissionSerial_(std::move(completedSubmissionSerial)),
          indirectAccesses_(maxBindlessIndirectAccesses),
          indirectFirstInstanceEnabled_(indirectFirstInstanceEnabled),
          stagingUploads_(arp::core::makeUnique<rhi::RecordedStagingUploadJournal>()),
          bindlessExecutable_(bindlessExecutable)
    {}

    std::shared_ptr<WebGpuNativeCommandPayload> nativePayload() const { return nativePayload_; }

    std::uint64_t recordingIdentity() const noexcept override
    {
        return nativePayload_ ? nativePayload_->recordingIdentity() : 0u;
    }

    void* queryInterface(std::string_view extension) noexcept override
    {
        return bindlessExecutable_ && extension == rhi::kExtensionBindlessCross
            ? static_cast<rhi::BindlessCommandEncoderExtension*>(this)
            : nullptr;
    }

    rhi::CommandValidationResult recordIndirectProcessor(
        std::shared_ptr<const rhi::BindlessIndirectProcessor> processor,
        const rhi::BindlessAccessDeclarations& producerAccess,
        const rhi::BindlessAccessDeclarations& consumerAccess) override
    {
        if (!nativePayload_ || activePass_ || finished_)
            return commandFail("RHI-WEBGPU-INDIRECT-PROCESSOR-NOT-RECORDING", commandCount_);
        const auto native =
            std::dynamic_pointer_cast<const WebGpuNativeIndirectProcessor>(processor);
        if (!native)
            return commandFail("RHI-WEBGPU-INDIRECT-PROCESSOR-PAYLOAD-MISSING", commandCount_);
        const auto live = native->validate();
        if (!live.success)
            return commandFail(live.diagnostics, commandCount_);
        const auto& intent = native->intent().desc();
        const rhi::GpuBuffer* resources[]{intent.logicalSource.get(),
            intent.packet->physicalArgs(),
            intent.packet->instanceMappings(),
            intent.packet->producerParameters()};
        for (const auto* resource : resources) {
            const auto facts =
                registry_ ? registry_->findDescriptorFacts(resource->id()) : std::nullopt;
            if (!facts || facts->kind != rhi::DescriptorResourceKind::Buffer)
                return commandFail("RHI-WEBGPU-INDIRECT-PROCESSOR-DEVICE-MISMATCH", commandCount_);
        }
        core::pmr::String diagnostic;
        if (!nativePayload_->recordPreparedIndirectProcessor(
                native, producerAccess, consumerAccess, diagnostic, true))
            return commandFail(diagnostic, commandCount_);
        if (!nativePayload_->scheduleIndirectProcessorFaultReadback(*native, diagnostic)) {
            markCommandFail(diagnostic);
            return lastValidation_;
        }
        commandCount_ += 6u;
        return commandOk(commandCount_);
    }

    rhi::ResourceBarrierResult transition(
        rhi::GpuBuffer& buffer, const rhi::ResourceBarrierDesc& desc) override
    {
        if (const auto* id = nativePayload_
                ? nativePayload_->sampledGeometryBarrierDiagnostic(desc)
                : rhi::semantics::GeometryInputAccessLowering::geometryInputBarrierDiagnostic(
                      rhi::Backend::WebGPU, desc)) {
            markCommandFail(id);
            return {false, lastValidation_.diagnostics};
        }
        if (!recordedStates_) {
            markCommandFail("RHI-RESOURCE-STATE-RECORDING-FINISHED");
            return {false, lastValidation_.diagnostics};
        }
        if (!nativePayload_) {
            constexpr auto id = "RHI-WEBGPU-BARRIER-NATIVE-PAYLOAD-MISSING";
            markCommandFail(id);
            return {false,
                core::pmr::String(id),
                buffer.id(),
                desc.before,
                desc.after,
                false,
                true,
                desc.sourceStages,
                desc.destinationStages,
                desc.sourceAccess,
                desc.destinationAccess};
        }
        if (const auto* id = nativePayload_->transitionRecordingDiagnostic()) {
            markCommandFail(id);
            return {false,
                core::pmr::String(id),
                buffer.id(),
                desc.before,
                desc.after,
                false,
                true,
                desc.sourceStages,
                desc.destinationStages,
                desc.sourceAccess,
                desc.destinationAccess};
        }
        auto result = transitionWebGpuRhiBuffer(buffer, desc, recordedStates_.get());
        if (result.success) {
            std::pmr::string diagnostics;
            auto nativeBuffer = webGpuNativePayloadFor(buffer);
            if (!nativeBuffer
                || !nativePayload_->recordBufferBarrier(*nativeBuffer, desc, diagnostics)) {
                result.success = false;
                result.diagnostics = diagnostics;
                return result;
            }
        }
        if (result.success) {
            markCommandOk();
        }
        return result;
    }

    rhi::ResourceBarrierResult transition(
        rhi::GpuTexture& texture, const rhi::ResourceBarrierDesc& desc) override
    {
        if (const auto* id =
                rhi::semantics::GeometryInputAccessLowering::geometryInputBarrierDiagnostic(
                    rhi::Backend::WebGPU, desc)) {
            markCommandFail(id);
            return {false, lastValidation_.diagnostics};
        }
        if (!recordedStates_) {
            markCommandFail("RHI-RESOURCE-STATE-RECORDING-FINISHED");
            return {false, lastValidation_.diagnostics};
        }
        if (!nativePayload_) {
            constexpr auto id = "RHI-WEBGPU-BARRIER-NATIVE-PAYLOAD-MISSING";
            markCommandFail(id);
            return {false,
                core::pmr::String(id),
                texture.id(),
                desc.before,
                desc.after,
                true,
                true,
                desc.sourceStages,
                desc.destinationStages,
                desc.sourceAccess,
                desc.destinationAccess};
        }
        if (const auto* id = nativePayload_->transitionRecordingDiagnostic()) {
            markCommandFail(id);
            return {false,
                core::pmr::String(id),
                texture.id(),
                desc.before,
                desc.after,
                true,
                true,
                desc.sourceStages,
                desc.destinationStages,
                desc.sourceAccess,
                desc.destinationAccess};
        }
        auto result = transitionWebGpuRhiTexture(texture, desc, recordedStates_.get());
        if (result.success) {
            std::pmr::string diagnostics;
            auto nativeTexture = webGpuNativePayloadFor(texture);
            if (nativeTexture && !nativePayload_->nonTransferRecordingDiagnostic())
                nativePayload_->retainTextureForSubmission(nativeTexture);
            if (!nativeTexture
                || !nativePayload_->recordTextureBarrier(*nativeTexture, desc, diagnostics)) {
                result.success = false;
                result.diagnostics = diagnostics;
                return result;
            }
        }
        if (result.success) {
            markCommandOk();
        }
        return result;
    }

    rhi::ResourceBarrierResult memoryBarrier(
        rhi::GpuBuffer& buffer, const rhi::ResourceMemoryBarrierDesc& desc) override
    {
        if (const auto* id = nativePayload_
                ? nativePayload_->sampledGeometryBarrierDiagnostic(desc)
                : rhi::semantics::GeometryInputAccessLowering::geometryInputBarrierDiagnostic(
                      rhi::Backend::WebGPU, desc)) {
            markCommandFail(id);
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
        if (!nativePayload_) {
            result.diagnostics = "RHI-WEBGPU-MEMORY-BARRIER-NATIVE-PAYLOAD-MISSING";
            markCommandFail(result.diagnostics);
            return result;
        }
        if (const auto* id = nativePayload_->transitionRecordingDiagnostic()) {
            result.diagnostics = id;
            markCommandFail(result.diagnostics);
            return result;
        }
        auto nativeBuffer = webGpuNativePayloadFor(buffer);
        if (!nativeBuffer) {
            result.diagnostics = "RHI-RESOURCE-MEMORY-BARRIER-BACKEND-RESOURCE-MISSING";
            markCommandFail(result.diagnostics);
            return result;
        }
        std::pmr::string diagnostics;
        if (!nativePayload_->recordBufferMemoryBarrier(*nativeBuffer, desc, diagnostics)) {
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
        if (const auto* id =
                rhi::semantics::GeometryInputAccessLowering::geometryInputBarrierDiagnostic(
                    rhi::Backend::WebGPU, desc)) {
            markCommandFail(id);
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
        if (!findResource(registry_, texture.id())) {
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
        if (!nativePayload_) {
            result.diagnostics = "RHI-WEBGPU-MEMORY-BARRIER-NATIVE-PAYLOAD-MISSING";
            markCommandFail(result.diagnostics);
            return result;
        }
        if (const auto* id = nativePayload_->memoryBarrierRecordingDiagnostic()) {
            result.diagnostics = id;
            markCommandFail(result.diagnostics);
            return result;
        }
        auto nativeTexture = webGpuNativePayloadFor(texture);
        if (!nativeTexture) {
            result.diagnostics = "RHI-RESOURCE-MEMORY-BARRIER-BACKEND-RESOURCE-MISSING";
            markCommandFail(result.diagnostics);
            return result;
        }
        std::pmr::string diagnostics;
        if (!nativePayload_->recordTextureMemoryBarrier(*nativeTexture, desc, diagnostics)) {
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
        const auto operations = rhi::semantics::validateRenderPassAttachmentOperations(desc);
        if (!operations.success) {
            markCommandFail(operations.diagnostics);
            return nullptr;
        }
        if (!nativePayload_) {
            markCommandFail("RHI-WEBGPU-PASS-NATIVE-PAYLOAD-MISSING");
            return nullptr;
        }
        if (const auto* id = nativePayload_->nonTransferRecordingDiagnostic()) {
            markCommandFail(id);
            return nullptr;
        }
        if (desc.layeredRendering) {
            markCommandFail("RHI-WEBGPU-LAYERED-RENDERING-NOT-LOWERED");
            return nullptr;
        }
        const bool depthOnlyRenderPass =
            desc.colorAttachments.empty() && desc.hasDepthStencilAttachment;
        if (hasActivePass() || isFinished()
            || (desc.colorAttachments.empty() && !depthOnlyRenderPass)) {
            markCommandFail("WebGPU RHI render pass requires an inactive encoder and color or "
                            "depth attachment");
            return nullptr;
        }
        for (const auto& attachment : desc.colorAttachments) {
            if (attachment.hasResolveTarget != attachment.resolveTarget.isValid()) {
                markCommandFail(
                    "WebGPU RHI render pass resolve target declaration is inconsistent");
                return nullptr;
            }
            const auto target = findResource(registry_, attachment.target);
            if (!target || target->kind != rhi::DescriptorResourceKind::TextureView
                || !hasTextureUsage(target->textureUsage, rhi::TextureUsage::RenderAttachment)
                || (attachment.format != rhi::TextureFormat::Unknown
                    && attachment.format != target->textureDesc.format)) {
                markCommandFail(
                    "WebGPU RHI render pass color attachment resource or format is invalid");
                return nullptr;
            }
            if (attachment.hasResolveTarget) {
                const auto resolve = findResource(registry_, attachment.resolveTarget);
                if (!resolve || resolve->kind != rhi::DescriptorResourceKind::TextureView
                    || !hasTextureUsage(resolve->textureUsage, rhi::TextureUsage::RenderAttachment)
                    || resolve->textureDesc.sampleCount != 1u
                    || target->textureDesc.sampleCount <= 1u
                    || resolve->textureDesc.format != target->textureDesc.format
                    || resolve->textureDesc.extent.width != target->textureDesc.extent.width
                    || resolve->textureDesc.extent.height != target->textureDesc.extent.height
                    || resolve->textureDesc.extent.depth != target->textureDesc.extent.depth) {
                    markCommandFail("WebGPU RHI render pass resolve target is incompatible with "
                                    "its color target");
                    return nullptr;
                }
            }
        }
        if (desc.hasDepthStencilAttachment) {
            const auto depth = findResource(registry_, desc.depthStencilAttachment.target);
            if (!depth || depth->kind != rhi::DescriptorResourceKind::TextureView
                || !hasTextureUsage(depth->textureUsage, rhi::TextureUsage::RenderAttachment)
                || !rhi::isDepthTextureFormat(depth->textureDesc.format)
                || (desc.depthStencilAttachment.format != rhi::TextureFormat::Unknown
                    && desc.depthStencilAttachment.format != depth->textureDesc.format)) {
                markCommandFail(
                    "WebGPU RHI render pass depth attachment resource or format is invalid");
                return nullptr;
            }
        }
        core::pmr::Vector<const WebGpuNativeTexturePayload*> colorTargets;
        core::pmr::Vector<const WebGpuNativeTexturePayload*> resolveTargets;
        colorTargets.reserve(desc.colorAttachments.size());
        resolveTargets.reserve(desc.colorAttachments.size());
        for (const auto& attachment : desc.colorAttachments) {
            const auto target = findResource(registry_, attachment.target);
            if (!target || !target->texture) {
                markCommandFail("WebGPU RHI render pass color attachment target is unavailable");
                return nullptr;
            }
            nativePayload_->retainTextureForSubmission(target->texture);
            colorTargets.push_back(target->texture.get());
            if (attachment.hasResolveTarget) {
                const auto resolve = findResource(registry_, attachment.resolveTarget);
                if (!resolve || !resolve->texture) {
                    markCommandFail("WebGPU RHI render pass resolve target is unavailable");
                    return nullptr;
                }
                nativePayload_->retainTextureForSubmission(resolve->texture);
                resolveTargets.push_back(resolve->texture.get());
            } else {
                resolveTargets.push_back(nullptr);
            }
        }
        const auto depthTarget = desc.hasDepthStencilAttachment
            ? findResource(registry_, desc.depthStencilAttachment.target)
            : std::optional<WebGpuRhiRegisteredResource>{};
        if (depthTarget && depthTarget->texture) {
            nativePayload_->retainTextureForSubmission(depthTarget->texture);
        }
        std::pmr::string diagnostics;
        if ((desc.hasDepthStencilAttachment && (!depthTarget || !depthTarget->texture))
            || !nativePayload_->recordRenderPassBegin(desc,
                colorTargets,
                resolveTargets,
                desc.hasDepthStencilAttachment ? depthTarget->texture.get() : nullptr,
                diagnostics)) {
            markCommandFail(diagnostics);
            return nullptr;
        }
        activePass_ = true;
        markCommandOk();
        return arp::core::makeUnique<WebGpuRhiRenderPassEncoder>(commandCount_,
            activePass_,
            recordingIdentity(),
            indirectFirstInstanceEnabled_,
            indirectAccesses_,
            desc,
            nativePayload_,
            capabilityContract_,
            bindlessExecutable_);
    }

    arp::core::UniquePtr<rhi::ComputePassEncoder> beginComputePass(
        const rhi::ComputePassDesc& desc) override
    {
        if (!nativePayload_) {
            markCommandFail("RHI-WEBGPU-PASS-NATIVE-PAYLOAD-MISSING");
            return nullptr;
        }
        if (const auto* id = nativePayload_->nonTransferRecordingDiagnostic()) {
            markCommandFail(id);
            return nullptr;
        }
        if (!beginPassGuard("WebGPU RHI command encoder already has an active pass")) {
            return nullptr;
        }
        std::pmr::string diagnostics;
        if (!nativePayload_->recordComputePassBegin(desc, diagnostics)) {
            markCommandFail(diagnostics);
            rollbackPassBegin();
            return nullptr;
        }
        markCommandOk();
        return arp::core::makeUnique<WebGpuRhiComputePassEncoder>(
            commandCount_, activePass_, nativePayload_, capabilityContract_, bindlessExecutable_);
    }

    arp::core::UniquePtr<rhi::TransferPassEncoder> beginTransferPass(
        const rhi::TransferPassDesc&) override
    {
        if (!nativePayload_) {
            markCommandFail("RHI-WEBGPU-PASS-NATIVE-PAYLOAD-MISSING");
            return nullptr;
        }
        if (const auto* id = nativePayload_->preparedRecordingDiagnostic()) {
            markCommandFail(id);
            return nullptr;
        }
        if (!beginPassGuard("WebGPU RHI command encoder already has an active pass")) {
            return nullptr;
        }
        markCommandOk();
        return arp::core::makeUnique<WebGpuRhiTransferPassEncoder>(commandCount_,
            activePass_,
            registry_,
            nativePayload_,
            stagingUploadPool_,
            capabilityContract_);
    }

    bool scheduleTextureReadback(const rhi::GpuTexture& texture,
        std::string_view label,
        std::pmr::string& diagnostics) override
    {
        (void)texture;
        (void)label;
        diagnostics = rhi::BorrowedTextureReadbackRetirementContract::lowering(rhi::Backend::WebGPU)
                          .diagnostic;
        return false;
    }

    void pushDebugGroup(std::string_view label) override
    {
        if (nativePayload_) {
            std::pmr::string diag;
            if (!nativePayload_->recordPushDebugGroup(label, diag))
                markCommandFail(std::move(diag));
        } else {
            markCommandFail(webGpuGenericRecordingUsageDiagnostic);
        }
    }

    void popDebugGroup() override
    {
        if (nativePayload_) {
            std::pmr::string diag;
            if (!nativePayload_->recordPopDebugGroup(diag))
                markCommandFail(std::move(diag));
        } else {
            markCommandFail(webGpuGenericRecordingUsageDiagnostic);
        }
    }

    void insertDebugMarker(std::string_view label) override
    {
        if (nativePayload_) {
            std::pmr::string diag;
            if (!nativePayload_->recordInsertDebugMarker(label, diag))
                markCommandFail(std::move(diag));
        } else {
            markCommandFail(webGpuGenericRecordingUsageDiagnostic);
        }
    }

    arp::core::UniquePtr<rhi::CommandBuffer> finish() override
    {
        if (!finishGuard("WebGPU RHI command encoder has an unclosed pass")) {
            return nullptr;
        }
        if (!nativePayload_) {
            markCommandFail("WebGPU RHI command encoder requires a native command payload");
            return nullptr;
        }
        std::pmr::string diagnostics;
        if (!nativePayload_->finish(diagnostics)) {
            markCommandFail(diagnostics);
            return nullptr;
        }
        finished_ = true;
        return arp::core::makeUnique<WebGpuRhiCommandBuffer>(commandCount_,
            nativePayload_,
            stagingUploadPool_,
            stagingSubmissionSerial_,
            std::move(recordedStates_),
            std::move(stagingUploads_));
    }

private:
    std::shared_ptr<WebGpuRhiResourceRegistry> registry_;
    std::shared_ptr<WebGpuNativeCommandPayload> nativePayload_;
    const rhi::semantics::NativeBackendCapabilityContract* capabilityContract_ = nullptr;
    std::shared_ptr<WebGpuRhiStagingUploadPool> stagingUploadPool_;
    std::uint64_t stagingSubmissionSerial_ = 0;
    std::shared_ptr<std::atomic<std::uint64_t>> completedSubmissionSerial_;
    rhi::BindlessIndirectDeclarations indirectAccesses_;
    bool indirectFirstInstanceEnabled_ = false;
    core::UniquePtr<rhi::RecordedStagingUploadJournal> stagingUploads_;
    bool bindlessExecutable_ = false;
    bool finished_ = false;
};

class WebGpuRhiSecondaryCommandEncoder final : public rhi::SecondaryCommandEncoder {
public:
    rhi::CommandValidationResult bindIndexBuffer(
        const rhi::GpuBuffer&, rhi::IndexFormat, std::uint64_t) override
    {
        return commandFail(
            "WebGPU RHI secondary index-buffer state is not part of the soft replay contract",
            commandCount_);
    }

    rhi::CommandValidationResult pushConstants(rhi::ShaderStage stages,
        std::uint32_t offsetBytes,
        std::uint32_t sizeBytes,
        const void* data) override
    {
        const auto validation =
            rhi::semantics::validatePushConstantCommand(stages, offsetBytes, sizeBytes, data);
        if (!validation.success) {
            return commandFail(validation.diagnostics, commandCount_);
        }
        if (pushState_.hasUnconsumedUpdate()) {
            return commandFail(rhi::semantics::PushConstantDiagCode::kDuplicateUnconsumed
                    + std::pmr::string(
                        "secondary push constants already set without an intervening draw"),
                commandCount_);
        }
        const auto pushDataOffset = static_cast<std::uint32_t>(pushData_.size());
        pushData_.resize(pushData_.size() + sizeBytes);
        std::memcpy(pushData_.data() + pushDataOffset, data, sizeBytes);
        commands_.push_back({
            WebGpuSecondaryCommandKind::PushConstants,
            stages,
            offsetBytes,
            sizeBytes,
            pushDataOffset,
        });
        pushState_.onPush();
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult drawIndexed(std::uint32_t indexCount,
        std::uint32_t instanceCount,
        std::uint32_t firstIndex,
        std::int32_t vertexOffset,
        std::uint32_t firstInstance) override
    {
        if (indexCount == 0u) {
            return commandFail("WebGPU RHI secondary draw requires indices", commandCount_);
        }
        commands_.push_back({
            WebGpuSecondaryCommandKind::DrawIndexed,
            rhi::ShaderStage::None,
            0u,
            0u,
            0u,
            indexCount,
            instanceCount,
            firstIndex,
            vertexOffset,
            firstInstance,
        });
        pushState_.onConsume();
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult draw(std::uint32_t vertexCount,
        std::uint32_t instanceCount,
        std::uint32_t firstVertex,
        std::uint32_t firstInstance) override
    {
        if (vertexCount == 0u) {
            return commandFail("WebGPU RHI secondary draw requires vertices", commandCount_);
        }
        commands_.push_back({
            WebGpuSecondaryCommandKind::Draw,
            rhi::ShaderStage::None,
            0u,
            0u,
            0u,
            vertexCount,
            instanceCount,
            firstVertex,
            0,
            firstInstance,
        });
        pushState_.onConsume();
        return commandOk(++commandCount_);
    }

    arp::core::UniquePtr<rhi::SecondaryCommandBuffer> finish() override
    {
        return arp::core::makeUnique<WebGpuRhiSecondaryCommandBuffer>(
            std::move(commands_), std::move(pushData_));
    }

private:
    std::uint32_t commandCount_ = 0;
    core::pmr::Vector<WebGpuSecondaryCommand> commands_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::uint8_t> pushData_{&core::defaultCpuMemoryResource()};
    rhi::semantics::PushConstantConsumptionState pushState_;
};

class WebGpuRhiRenderBundle final : public rhi::RenderBundle {
public:
    WebGpuRhiRenderBundle(std::shared_ptr<WebGpuNativeRenderBundlePayload> payload,
        rhi::PrimitiveTopology topology,
        std::uint32_t commandCount)
        : payload_(std::move(payload)), topology_(topology), commandCount_(commandCount)
    {}

    std::uint32_t commandCount() const override { return commandCount_; }

    rhi::PrimitiveTopology primitiveTopology() const { return topology_; }

    const std::shared_ptr<WebGpuNativeRenderBundlePayload>& payload() const { return payload_; }

private:
    std::shared_ptr<WebGpuNativeRenderBundlePayload> payload_;
    rhi::PrimitiveTopology topology_ = rhi::PrimitiveTopology::TriangleList;
    std::uint32_t commandCount_ = 0u;
};

class WebGpuRhiRenderBundleEncoder final : public rhi::RenderBundleEncoder {
public:
    WebGpuRhiRenderBundleEncoder(rhi::RenderBundleDesc desc,
        const rhi::semantics::NativeBackendCapabilityContract& capabilityContract)
        : desc_(std::move(desc)), diagnostics_()
    {
        rhi::semantics::CommandWorkloadDesc workload;
        workload.commandCount = 1u;
        workload.usesRenderPass = true;
        workload.usesRenderBundle = true;
        const auto decision = rhi::semantics::lowerCommandWorkload(
            capabilityContract, workload, rhi::semantics::CommandRecordingIntent::RenderBundle);
        const auto loweringUse = rhi::semantics::validateNativeCommandLoweringUse(decision,
            rhi::semantics::NativeCommandSemantic::WebGpuRenderBundle,
            rhi::FallbackMode::Native,
            "WebGPU render bundle");
        if (!loweringUse.success) {
            diagnostics_ = loweringUse.diagnostics;
            return;
        }
        payload_ = createWebGpuNativeRenderBundlePayload(desc_, diagnostics_);
    }

    rhi::CommandValidationResult setPipeline(const rhi::RenderPipeline& pipeline) override
    {
        if (finished_) {
            return commandFail("RHI-WEBGPU-RENDER-BUNDLE-ENCODER-ALREADY-FINISHED", commandCount_);
        }
        if (pipeline.primitiveTopology() != desc_.primitiveTopology) {
            return commandFail(
                "RHI-WEBGPU-RENDER-BUNDLE-PIPELINE-TOPOLOGY-MISMATCH", commandCount_);
        }
        auto nativePipeline = webGpuNativePayloadFor(pipeline);
        if (!nativePipeline) {
            return commandFail("RHI-WEBGPU-RENDER-BUNDLE-PIPELINE-PAYLOAD-MISSING", commandCount_);
        }
        std::pmr::string diagnostics;
        if (!recordWebGpuNativeRenderBundlePipeline(payload_, *nativePipeline, diagnostics)) {
            return commandFail(diagnostics.empty()
                    ? "RHI-WEBGPU-RENDER-BUNDLE-PIPELINE-LOWERING-FAILED"
                    : diagnostics,
                commandCount_);
        }
        pipelinePayload_ = std::move(nativePipeline);
        payload_->retainedPipeline = pipelinePayload_;
        payload_->requiredDescriptorSets = pipelinePayload_->descriptorSetGroups;
        payload_->boundDescriptorSets.clear();
        pipelineBound_ = true;
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult bindVertexBuffer(
        std::uint32_t slot, const rhi::GpuBuffer& buffer, std::uint64_t offsetBytes) override
    {
        if (finished_ || !pipelineBound_) {
            return commandFail("RHI-WEBGPU-RENDER-BUNDLE-VERTEX-BIND-STATE-INVALID", commandCount_);
        }
        if (!hasResourceUsage(buffer.desc().usage, rhi::ResourceUsage::VertexBuffer)
            || (offsetBytes & 3u) != 0u || offsetBytes >= buffer.desc().sizeBytes) {
            return commandFail(
                "RHI-WEBGPU-RENDER-BUNDLE-VERTEX-BIND-ARGUMENTS-INVALID", commandCount_);
        }
        auto nativeBuffer = webGpuNativePayloadFor(buffer);
        std::pmr::string diagnostics;
        if (!nativeBuffer
            || !recordWebGpuNativeRenderBundleVertexBuffer(
                payload_, slot, nativeBuffer, offsetBytes, diagnostics)) {
            return commandFail(diagnostics.empty()
                    ? "RHI-WEBGPU-RENDER-BUNDLE-VERTEX-PAYLOAD-MISSING"
                    : diagnostics,
                commandCount_);
        }
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult bindIndexBuffer(
        const rhi::GpuBuffer& buffer, rhi::IndexFormat format, std::uint64_t offsetBytes) override
    {
        if (finished_ || !pipelineBound_) {
            return commandFail("RHI-WEBGPU-RENDER-BUNDLE-INDEX-BIND-STATE-INVALID", commandCount_);
        }
        const auto alignment = format == rhi::IndexFormat::Uint16 ? 2u : 4u;
        if (!hasResourceUsage(buffer.desc().usage, rhi::ResourceUsage::IndexBuffer)
            || (offsetBytes % alignment) != 0u || offsetBytes >= buffer.desc().sizeBytes) {
            return commandFail(
                "RHI-WEBGPU-RENDER-BUNDLE-INDEX-BIND-ARGUMENTS-INVALID", commandCount_);
        }
        auto nativeBuffer = webGpuNativePayloadFor(buffer);
        std::pmr::string diagnostics;
        if (!nativeBuffer
            || !recordWebGpuNativeRenderBundleIndexBuffer(
                payload_, nativeBuffer, format, offsetBytes, diagnostics)) {
            return commandFail(diagnostics.empty()
                    ? "RHI-WEBGPU-RENDER-BUNDLE-INDEX-PAYLOAD-MISSING"
                    : diagnostics,
                commandCount_);
        }
        indexBufferBound_ = true;
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult bindDescriptorSet(std::uint32_t set,
        const rhi::DescriptorSet& descriptorSet,
        const std::uint32_t* dynamicOffsets,
        std::uint32_t dynamicOffsetCount) override
    {
        const auto trackingCapacity = boundDescriptors_.validateSetCapacity(set);
        if (!trackingCapacity.success)
            return commandFail(trackingCapacity.diagnostics, commandCount_);
        if (finished_ || !pipelineBound_ || !pipelinePayload_) {
            return commandFail(
                "RHI-WEBGPU-RENDER-BUNDLE-DESCRIPTOR-BIND-STATE-INVALID", commandCount_);
        }
        if (descriptorSet.stats().nativeHandleExposedToUpperLayer) {
            return commandFail("RHI-WEBGPU-RENDER-BUNDLE-DESCRIPTOR-HANDLE-EXPOSED", commandCount_);
        }
        const auto resources = rhi::validateDescriptorResources(
            descriptorSet, rhi::DescriptorContentRequirement::WrittenElements);
        if (!resources.success)
            return commandFail(resources.diagnostics, commandCount_);
        const auto dynamicValidation = rhi::validateDynamicOffsetArguments(
            descriptorSet.layout(), dynamicOffsets, dynamicOffsetCount);
        if (!dynamicValidation.success) {
            return commandFail(dynamicValidation.diagnostics, commandCount_);
        }
        auto nativeDescriptorSet = webGpuNativePayloadFor(descriptorSet);
        std::pmr::string diagnostics;
        if (!nativeDescriptorSet
            || !recordWebGpuNativeRenderBundleDescriptorSet(payload_,
                set,
                *pipelinePayload_,
                nativeDescriptorSet,
                dynamicOffsets,
                dynamicOffsetCount,
                diagnostics)) {
            return commandFail(diagnostics.empty()
                    ? "RHI-WEBGPU-RENDER-BUNDLE-DESCRIPTOR-LOWERING-FAILED"
                    : diagnostics,
                commandCount_);
        }
        const auto tracked =
            boundDescriptors_.bindSet(set, descriptorSet, dynamicOffsets, dynamicOffsetCount);
        if (!tracked.success)
            return commandFail(tracked.diagnostics, commandCount_);
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult drawIndexed(std::uint32_t indexCount,
        std::uint32_t instanceCount,
        std::uint32_t firstIndex,
        std::int32_t vertexOffset,
        std::uint32_t firstInstance) override
    {
        const auto descriptorUse = boundDescriptors_.validateLiveBindings();
        if (!descriptorUse.success)
            return commandFail(descriptorUse.diagnostics, commandCount_);
        if (finished_ || commandCount_ >= desc_.maxCommandCount || !indexBufferBound_) {
            return commandFail(
                "RHI-WEBGPU-RENDER-BUNDLE-INDEXED-DRAW-STATE-INVALID", commandCount_);
        }
        std::pmr::string diagnostics;
        if (!recordWebGpuNativeRenderBundleDrawIndexed(payload_,
                indexCount,
                instanceCount,
                firstIndex,
                vertexOffset,
                firstInstance,
                diagnostics)) {
            return commandFail(
                diagnostics.empty() ? "RHI-WEBGPU-RENDER-BUNDLE-INDEXED-DRAW-FAILED" : diagnostics,
                commandCount_);
        }
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult draw(std::uint32_t vertexCount,
        std::uint32_t instanceCount,
        std::uint32_t firstVertex,
        std::uint32_t firstInstance) override
    {
        const auto descriptorUse = boundDescriptors_.validateLiveBindings();
        if (!descriptorUse.success)
            return commandFail(descriptorUse.diagnostics, commandCount_);
        if (finished_ || commandCount_ >= desc_.maxCommandCount) {
            return commandFail("RHI-WEBGPU-RENDER-BUNDLE-DRAW-STATE-INVALID", commandCount_);
        }
        std::pmr::string diagnostics;
        if (!recordWebGpuNativeRenderBundleDraw(
                payload_, vertexCount, instanceCount, firstVertex, firstInstance, diagnostics)) {
            return commandFail(
                diagnostics.empty() ? "RHI-WEBGPU-RENDER-BUNDLE-DRAW-FAILED" : diagnostics,
                commandCount_);
        }
        return commandOk(++commandCount_);
    }

    arp::core::UniquePtr<rhi::RenderBundle> finish() override
    {
        if (finished_ || commandCount_ == 0u || !payload_) {
            return nullptr;
        }
        std::pmr::string diagnostics;
        if (!finishWebGpuNativeRenderBundle(payload_, diagnostics)) {
            diagnostics_ = std::move(diagnostics);
            return nullptr;
        }
        finished_ = true;
        return arp::core::makeUnique<WebGpuRhiRenderBundle>(
            std::move(payload_), desc_.primitiveTopology, commandCount_);
    }

    bool executable() const { return payload_ && payload_->nativeRuntimeBacked; }

private:
    rhi::BoundDescriptorSlots boundDescriptors_;
    rhi::RenderBundleDesc desc_;
    std::pmr::string diagnostics_;
    std::shared_ptr<WebGpuNativeRenderBundlePayload> payload_;
    std::shared_ptr<WebGpuNativeRenderPipelinePayload> pipelinePayload_;
    std::uint32_t commandCount_ = 0u;
    bool pipelineBound_ = false;
    bool indexBufferBound_ = false;
    bool finished_ = false;
};

rhi::CommandValidationResult WebGpuRhiRenderPassEncoder::executeRenderBundle(
    const rhi::RenderBundle& bundle)
{
    if (activeAuthorization_ || boundSnapshot_)
        return commandFail("RHI-BINDLESS-RENDER-BUNDLE-UNAVAILABLE", commandCount_);
    const auto* webGpuBundle = dynamic_cast<const WebGpuRhiRenderBundle*>(&bundle);
    if (!webGpuBundle || !nativePayload_) {
        return commandFail("RHI-WEBGPU-RENDER-BUNDLE-PAYLOAD-INVALID", commandCount_);
    }
    if (!pipelineBound_) {
        return commandFail("RHI-WEBGPU-RENDER-BUNDLE-REQUIRES-BOUND-PASS-PIPELINE", commandCount_);
    }
    if (webGpuBundle->primitiveTopology() != pipelineTopology_) {
        return commandFail("RHI-WEBGPU-RENDER-BUNDLE-PIPELINE-TOPOLOGY-MISMATCH", commandCount_);
    }
    if (!capabilityContract_) {
        return commandFail(
            "RHI-WEBGPU-RENDER-BUNDLE-LOWERING-CAPABILITY-CONTRACT-MISSING", commandCount_);
    }
    rhi::semantics::CommandWorkloadDesc workload;
    workload.commandCount = std::max(1u, webGpuBundle->commandCount());
    workload.usesRenderPass = true;
    workload.usesRenderBundle = true;
    const auto decision = rhi::semantics::lowerCommandWorkload(
        *capabilityContract_, workload, rhi::semantics::CommandRecordingIntent::RenderBundle);
    const auto loweringUse = rhi::semantics::validateNativeCommandLoweringUse(decision,
        rhi::semantics::NativeCommandSemantic::WebGpuRenderBundle,
        rhi::FallbackMode::Native,
        "WebGPU render bundle");
    if (!loweringUse.success) {
        return commandFail(loweringUse.diagnostics, commandCount_);
    }
    std::pmr::string diagnostics;
    if (!executeWebGpuNativeRenderBundle(nativePayload_, webGpuBundle->payload(), diagnostics)) {
        return commandFail(
            diagnostics.empty() ? "RHI-WEBGPU-RENDER-BUNDLE-EXECUTE-FAILED" : diagnostics,
            commandCount_);
    }
    commandCount_ += webGpuBundle->commandCount();
    return commandOk(commandCount_);
}

} // namespace

arp::core::UniquePtr<rhi::CommandEncoder> createWebGpuRhiCommandEncoder(
    const rhi::CommandEncoderDesc& desc,
    std::shared_ptr<WebGpuRhiResourceRegistry> registry,
    WebGpuRhiNativePayloadPolicy nativePayloadPolicy,
    std::shared_ptr<WebGpuNativeCommandStats> commandStats,
    const rhi::semantics::NativeBackendCapabilityContract& capabilityContract,
    std::uint32_t pushConstantUploadRingByteCapacity,
    std::uint32_t pushConstantMaxBindingSize,
    std::uint32_t pushConstantOffsetAlignment,
    bool bindlessExecutable,
    std::shared_ptr<WebGpuRhiStagingUploadPool> stagingUploadPool,
    std::shared_ptr<WebGpuProducerCompletionOwner> processorCompletion)
{
    rhi::PrototypeCommandBatchContract::Decision prototypeDecision;
    if (desc.prototypeBatch) {
        prototypeDecision = rhi::PrototypeCommandBatchContract::lowering(
            rhi::Backend::WebGPU, *desc.prototypeBatch, webGpuNativePrototypeBatchCapabilities());
        if (desc.retentionBudget || !nativePayloadPolicy.enabled)
            prototypeDecision = {};
        if (!prototypeDecision.executable) {
            rhi::RhiErrorHandler::get().report(
                rhi::RhiSeverity::Error, prototypeDecision.diagnostic);
            return {};
        }
    } else if (!rhi::semantics::commandRetentionRequestAllowed(
                   desc, rhi::CommandRetentionCapabilities{}))
        return {};
    const auto stagingSubmissionSerial =
        stagingUploadPool ? stagingUploadPool->beginSubmission() : 0u;
    const auto completedSubmissionSerial = stagingUploadPool
        ? stagingUploadPool->completedSubmissionSerial()
        : std::shared_ptr<std::atomic<std::uint64_t>>{};
    std::shared_ptr<WebGpuNativeCommandPayload> nativePayload;
    if (nativePayloadPolicy.enabled) {
        if (desc.maxBindlessIndirectProcessors > UINT32_MAX - desc.maxBindlessSnapshots)
            return {};
        std::pmr::string diagnostics;
        nativePayload = createWebGpuNativeCommandPayload(desc.label,
            pushConstantUploadRingByteCapacity,
            pushConstantMaxBindingSize,
            pushConstantOffsetAlignment,
            stagingSubmissionSerial,
            std::move(commandStats),
            rhi::semantics::hasExecutableOptionalNativeFeature(
                capabilityContract, rhi::semantics::NativeBackendFeature::TimestampQueries)
                && !desc.prototypeBatch,
            diagnostics,
            desc.maxBindlessSnapshots + desc.maxBindlessIndirectProcessors,
            desc.maxBindlessPipelines,
            desc.maxBindlessIndirectProcessors,
            std::move(processorCompletion),
            desc.maxBindlessSnapshots);
        if (nativePayloadPolicy.required && !nativePayload) {
            return nullptr;
        }
        if (desc.prototypeBatch
            && (!nativePayload
                || !nativePayload->preparePrototypeBatch(
                    prototypeDecision, registry, diagnostics, nativePayload))) {
            rhi::RhiErrorHandler::get().report(rhi::RhiSeverity::Error, diagnostics.c_str());
            return {};
        }
    }
    return arp::core::makeUnique<WebGpuRhiCommandEncoder>(std::move(registry),
        std::move(nativePayload),
        &capabilityContract,
        stagingUploadPool,
        stagingSubmissionSerial,
        completedSubmissionSerial,
        bindlessExecutable,
        desc.maxBindlessIndirectAccesses,
        webGpuNativeDeviceSupportsIndirectFirstInstance());
}

arp::core::UniquePtr<rhi::SecondaryCommandEncoder> createWebGpuRhiSecondaryCommandEncoder(
    const rhi::SecondaryCommandEncoderDesc& desc)
{
    if (desc.primitiveTopology == rhi::PrimitiveTopology::PatchList || !desc.allowCpuReplay) {
        return nullptr;
    }
    return arp::core::makeUnique<WebGpuRhiSecondaryCommandEncoder>();
}

arp::core::UniquePtr<rhi::RenderBundleEncoder> createWebGpuRhiRenderBundleEncoder(
    const rhi::RenderBundleDesc& desc,
    WebGpuRhiNativePayloadPolicy nativePayloadPolicy,
    const rhi::semantics::NativeBackendCapabilityContract& capabilityContract)
{
    if (desc.primitiveTopology == rhi::PrimitiveTopology::PatchList
        || !nativePayloadPolicy.enabled) {
        return nullptr;
    }
    auto encoder = arp::core::makeUnique<WebGpuRhiRenderBundleEncoder>(desc, capabilityContract);
    return encoder->executable() ? std::move(encoder) : nullptr;
}

std::shared_ptr<WebGpuNativeCommandPayload> webGpuNativePayloadFor(
    const rhi::CommandBuffer& commandBuffer)
{
    const auto* webGpuCommandBuffer = dynamic_cast<const WebGpuRhiCommandBuffer*>(&commandBuffer);
    return webGpuCommandBuffer ? webGpuCommandBuffer->nativePayload() : nullptr;
}

std::shared_ptr<WebGpuNativeCommandPayload> webGpuNativePayloadFor(
    const rhi::CommandEncoder& encoder)
{
    const auto* original = dynamic_cast<const WebGpuRhiCommandEncoder*>(&encoder);
    return original ? original->nativePayload() : nullptr;
}

void markWebGpuRhiCommandBufferSubmitted(const rhi::CommandBuffer& commandBuffer)
{
    const auto* webGpuCommandBuffer = dynamic_cast<const WebGpuRhiCommandBuffer*>(&commandBuffer);
    if (webGpuCommandBuffer) {
        webGpuCommandBuffer->markSubmitted();
    }
}

} // namespace arp::rhi_webgpu

#include "core/memory/cpu_allocator.h"
#include "core/ownership/monotonic_id_sequence.h"
#include "rhi/backends/vulkan/vulkan_backend_objects.h"
#include "rhi/backends/vulkan/vulkan_bindless.h"
#include "rhi/backends/vulkan/vulkan_graphics_pass.h"
#include "rhi/backends/vulkan/vulkan_native_commands.h"
#include "rhi/backends/vulkan/vulkan_recording_storage.h"
#include "rhi/backends/vulkan/vulkan_render_pass_storage.h"
#include "rhi/backends/vulkan/vulkan_staging_upload_pool.h"
#include "rhi/frontend/bindless_compute_binding.h"
#include "rhi/frontend/bindless_shader_package.h"
#include "rhi/frontend/bindless_snapshot.h"
#include "rhi/frontend/command_attachment_validation.h"
#include "rhi/frontend/command_encoder_base.h"
#include "rhi/frontend/command_retention_validation.h"
#include "rhi/frontend/command_stream_program.h"
#include "rhi/frontend/command_transfer_validation.h"
#include "rhi/frontend/descriptor_bound_descriptor_slots.h"
#include "rhi/frontend/descriptor_resource_validation.h"
#include "rhi/frontend/descriptor_set_validation.h"
#include "rhi/frontend/memory_upload_staging_readiness.h"
#include "rhi/frontend/resource_access_usage.h"
#include "rhi/frontend/resource_memory_barrier.h"
#include "rhi/frontend/transfer_readback_borrowed_retirement.h"
#include "rhi/gpu_bindless.h"
#include "rhi/gpu_extensions.h"
#include "rhi/semantics/bindless_pipeline_interface.h"
#include "rhi/semantics/native_command_lowering.h"
#include "rhi/semantics/push_constant_validation.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

namespace arp::rhi_vulkan {

namespace {

// Reservation identity only. Completion is owned by each native command's exact
// staging journal, never by the magnitude of this or a caller frame serial.
core::MonotonicIdSequence gNextUploadRetirementSerial;

constexpr std::string_view kColdComputePassCapacity = "RHI-VULKAN-COLD-COMPUTE-PASS-CAPACITY";
constexpr std::string_view kColdTransferPassCapacity = "RHI-VULKAN-COLD-TRANSFER-PASS-CAPACITY";
constexpr std::string_view kColdRenderPassUnavailable =
    "RHI-VULKAN-COLD-RENDER-PASS-STORAGE-UNAVAILABLE";

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

struct NativeResourceBinding {
    rhi::GpuResourceId id;
    std::shared_ptr<VulkanRhiNativeResourcePayload> payload;
};

void rememberNativeResource(std::pmr::vector<NativeResourceBinding>& bindings,
    rhi::GpuResourceId id,
    std::shared_ptr<VulkanRhiNativeResourcePayload> payload)
{
    if (!payload) {
        return;
    }
    for (auto& binding : bindings) {
        if (binding.id == id) {
            binding.payload = std::move(payload);
            return;
        }
    }
    bindings.push_back({id, std::move(payload)});
}

std::shared_ptr<VulkanRhiNativeResourcePayload> findNativeResource(
    const std::pmr::vector<NativeResourceBinding>& bindings, rhi::GpuResourceId id)
{
    for (const auto& binding : bindings) {
        if (binding.id == id) {
            return binding.payload;
        }
    }
    return {};
}

class VulkanRhiCommandBuffer final : public rhi::RecordedStateCommandBuffer,
                                     public rhi::BindlessCommandBufferExtension {
public:
    // Storage only. This form stays private to the encoder until successful
    // finish replaces it with the original journal and genuine native payload.
    struct ColdStorage {};

    explicit VulkanRhiCommandBuffer(ColdStorage) noexcept
        : RecordedStateCommandBuffer(core::UniquePtr<core::MutationJournal>{})
    {}

    VulkanRhiCommandBuffer(VulkanRhiCommandBuffer&&) noexcept = default;

    ~VulkanRhiCommandBuffer() override
    {
        if (nativePayload_)
            nativePayload_->abandonOrdinaryRecording();
    }

    VulkanRhiCommandBuffer(std::uint32_t commandCount,
        std::shared_ptr<VulkanRhiNativeCommandPayload> nativePayload,
        core::UniquePtr<core::MutationJournal> states,
        core::UniquePtr<rhi::RecordedStagingUploadJournal> stagingUploads) noexcept
        : RecordedStateCommandBuffer(std::move(states), std::move(stagingUploads)),
          commandCount_(commandCount), nativePayload_(std::move(nativePayload))
    {}

    VulkanRhiCommandBuffer(std::uint32_t commandCount,
        std::shared_ptr<VulkanRhiNativeCommandPayload> nativePayload,
        core::MutationJournal& journal) noexcept
        : RecordedStateCommandBuffer(
              std::shared_ptr<core::MutationJournal>(nativePayload, &journal)),
          commandCount_(commandCount), nativePayload_(std::move(nativePayload))
    {}

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

    const std::shared_ptr<VulkanRhiNativeCommandPayload>& nativePayload() const
    {
        return nativePayload_;
    }

private:
    bool acceptBackendOwnedRecordedOwnership() const noexcept override
    {
        return nativePayload_ && nativePayload_->acceptOrdinaryRecordedOwnership();
    }

    std::uint32_t commandCount_ = 0;
    std::shared_ptr<VulkanRhiNativeCommandPayload> nativePayload_;
};

static_assert(std::is_nothrow_move_constructible_v<VulkanRhiCommandBuffer>);

class VulkanRhiRenderBundle final : public rhi::RenderBundle {
public:
    explicit VulkanRhiRenderBundle(std::uint32_t commandCount) : commandCount_(commandCount) {}

    std::uint32_t commandCount() const override { return commandCount_; }

private:
    std::uint32_t commandCount_ = 0;
};

class VulkanRhiSecondaryCommandBuffer final : public rhi::SecondaryCommandBuffer {
public:
    explicit VulkanRhiSecondaryCommandBuffer(std::uint32_t commandCount)
        : commandCount_(commandCount)
    {}

    std::uint32_t commandCount() const override { return commandCount_; }

    bool softSecondary() const override { return false; }

private:
    std::uint32_t commandCount_ = 0;
};

} // namespace

class VulkanRhiPassState {
public:
    VulkanRhiPassState(std::uint32_t& commandCount,
        std::shared_ptr<VulkanRhiNativeCommandPayload> nativePayload,
        VulkanRhiColdFailure failure = {})
        : commandCount_(commandCount), nativePayload_(std::move(nativePayload)), failure_(failure)
    {}

    bool preflightResources(std::size_t count = 0u)
    {
        if (!nativePayload_)
            return true;
        if (nativePayload_->hasOrdinaryAttachments()) {
            rejectStorage("RHI-ORDINARY-MIXED-COMMAND-PROFILE");
            return false;
        }
        if (const auto* id = nativePayload_->resourceRetentionStorageDiagnostic(count)) {
            rejectStorage(id);
            return false;
        }
        return true;
    }

    bool retainResources(
        const std::shared_ptr<VulkanRhiNativeResourcePayload>* resources, std::size_t count)
    {
        if (const auto* id = nativePayload_->retainResources(resources, count)) {
            rejectStorage(id);
            return false;
        }
        return true;
    }

    rhi::CommandValidationResult rejectStorage(const char* id)
    {
        nativePayload_->rejectRecordingStorage(id);
        if (failure_.publish) {
            failure_.publish(failure_.context, id);
            return rejected();
        }
        return commandFail(id, commandCount_);
    }

    rhi::CommandValidationResult rejected() const { return {false, {}, commandCount_}; }

    rhi::CommandValidationResult record()
    {
        if (!preflightResources())
            return rejected();
        std::pmr::string diagnostics;
        if (!recordVulkanRhiNativeCommand(nativePayload_, diagnostics)) {
            return commandFail(diagnostics, commandCount_);
        }
        ++commandCount_;
        return commandOk(commandCount_);
    }

    std::uint32_t commandCount() const noexcept { return commandCount_; }

    rhi::CommandValidationResult acceptOriginalCommand() noexcept
    {
        ++commandCount_;
        return commandOk(commandCount_);
    }

    rhi::CommandValidationResult acceptOriginalTimestamp() noexcept
    {
        ++commandCount_;
        return commandOk(commandCount_);
    }

private:
    std::uint32_t& commandCount_;
    std::shared_ptr<VulkanRhiNativeCommandPayload> nativePayload_;
    VulkanRhiColdFailure failure_;
};

class VulkanRhiComputePassEncoder final : public rhi::ComputePassEncoder,
                                          public rhi::BindlessPassExtension {
public:
    VulkanRhiComputePassEncoder(std::uint32_t& commandCount,
        bool& active,
        std::uint64_t recordingIdentity,
        std::shared_ptr<VulkanRhiNativeCommandPayload> nativePayload,
        std::shared_ptr<VulkanRhiResourceRegistry> resourceRegistry,
        const rhi::semantics::NativeBackendCapabilityContract* capabilityContract,
        bool bindlessExecutable,
        VulkanRhiColdFailure failure = {})
        : pass_(commandCount, nativePayload, failure), recordingIdentity_(recordingIdentity),
          nativePayload_(std::move(nativePayload)), resourceRegistry_(std::move(resourceRegistry)),
          capabilityContract_(capabilityContract), active_(active),
          bindlessExecutable_(bindlessExecutable)
    {}

    std::uint64_t recordingIdentity() const noexcept override { return recordingIdentity_; }

    void* queryInterface(std::string_view extension) noexcept override
    {
        return bindlessExecutable_ && extension == rhi::kExtensionBindlessCross
            ? static_cast<rhi::BindlessPassExtension*>(this)
            : nullptr;
    }

    rhi::CommandValidationResult bindPreparedComputeBinding(
        std::shared_ptr<const rhi::BindlessPreparedComputeBinding> binding,
        const rhi::BindlessAccessDeclarations& accesses) override
    {
        if (!pass_.preflightResources(0u))
            return pass_.rejected();
        if (!active_)
            return commandFail("command pass is not active", pass_.commandCount());
        if (!binding || !binding->originalFactoryOwnerMatches(binding)
            || !isVulkanPreparedComputeBinding(*binding))
            return commandFail(
                "RHI-BINDLESS-PREPARED-COMPUTE-PAYLOAD-MISMATCH", pass_.commandCount());
        if (!activePipeline_ || binding->owners()->desc().pipeline.get() != activePipeline_)
            return commandFail(
                "RHI-BINDLESS-PREPARED-COMPUTE-PIPELINE-MISMATCH", pass_.commandCount());
        const auto valid = binding->validateForRecord(*activePipeline_, accesses);
        if (!valid.success)
            return commandFail(valid.diagnostics, pass_.commandCount());
        if (!nativePayload_ || !nativePayload_->canRetainPreparedBinding(binding))
            return commandFail(
                "RHI-BINDLESS-PREPARED-COMPUTE-COMMAND-BUDGET", pass_.commandCount());
        const auto& desc = binding->owners()->desc();
        const auto result = bindSnapshot(desc.snapshot, desc.plan, desc.generation, accesses);
        if (!result.success)
            return result;
        core::pmr::String diagnostic;
        if (!nativePayload_->retainPreparedBinding(binding, diagnostic))
            return commandFail(diagnostic, pass_.commandCount());
        return result;
    }

    rhi::CommandValidationResult bindSnapshot(std::shared_ptr<const rhi::BindlessSnapshot> snapshot,
        const rhi::BindlessLoweringPlan& selected,
        std::uint64_t generation,
        const rhi::BindlessAccessDeclarations& accesses) override
    {
        if (!pass_.preflightResources(0u))
            return pass_.rejected();
        if (!active_)
            return commandFail("command pass is not active", pass_.commandCount());
        if (!activeAuthorization_ || !activeAuthorization_->current(activeAuthorization_))
            return commandFail("RHI-BINDLESS-RECORD-AUTHORIZATION-INVALID", pass_.commandCount());
        if (boundSnapshot_)
            return commandFail("RHI-BINDLESS-RECORD-SNAPSHOT-ALREADY-BOUND", pass_.commandCount());
        if (!snapshot)
            return commandFail("RHI-BINDLESS-RECORD-SNAPSHOT-MISSING", pass_.commandCount());
        if (!snapshot->sharedOwnerMatches(snapshot))
            return commandFail("RHI-BINDLESS-RECORD-SNAPSHOT-OWNER-MISMATCH", pass_.commandCount());

        const auto& authorizedPlan = activeAuthorization_->plan();
        const auto& artifact = activeAuthorization_->package().artifact();
        if (const auto* id =
                rhi::semantics::BindlessPipelineInterfaceContract::standaloneSnapshotDiagnostic(
                    artifact))
            return commandFail(id, pass_.commandCount());
        if (!selected.success || !selected.executable
            || selected.execution != rhi::BindlessExecution::Native
            || selected.identity != authorizedPlan.identity
            || selected.deviceIdentity != authorizedPlan.deviceIdentity
            || selected.shaderPackageIdentity != artifact.shaderPackageIdentity
            || artifact.execution != rhi::BindlessExecution::Native
            || snapshot->plan().identity != authorizedPlan.identity)
            return commandFail("RHI-BINDLESS-RECORD-PLAN-MISMATCH", pass_.commandCount());

        auto valid =
            snapshot->validateForRecord(selected, generation, accesses, rhi::ShaderStage::Compute);
        if (!valid.success)
            return commandFail(valid.diagnostics, pass_.commandCount());
        valid = snapshot->validateForRecord(
            authorizedPlan, generation, accesses, rhi::ShaderStage::Compute);
        if (!valid.success)
            return commandFail(valid.diagnostics, pass_.commandCount());
        if (!snapshot->faultLatchBuffer() || !snapshot->faultReadbackBuffer())
            return commandFail("RHI-BINDLESS-FAULT-READBACK-UNAVAILABLE", pass_.commandCount());

        const auto* descriptors = snapshot->descriptorSet();
        if (!descriptors)
            return commandFail("RHI-BINDLESS-RECORD-SNAPSHOT-INVALID", pass_.commandCount());
        const auto capacity = boundDescriptors_.validateSetCapacity(snapshot->setIndex());
        if (!capacity.success)
            return commandFail(capacity.diagnostics, pass_.commandCount());
        valid =
            rhi::validateDescriptorSetBinding(pipelineLayout_, snapshot->setIndex(), *descriptors);
        if (!valid.success)
            return commandFail(valid.diagnostics, pass_.commandCount());
        const auto nativeSet = vulkanRhiNativePayload(*descriptors);
        if (!nativeSet)
            return commandFail(
                "RHI-VULKAN-NATIVE-DESCRIPTOR-PAYLOAD-MISSING", pass_.commandCount());

        std::pmr::string diagnostics;
        if (!nativePayload_->retainIndexedSnapshot(snapshot, diagnostics))
            return commandFail(diagnostics, pass_.commandCount());
        if (!nativePayload_->recordBindlessFaultLatchClear(
                snapshot, snapshot->faultLatchPriorState(), diagnostics))
            return commandFail(diagnostics, pass_.commandCount());
        if (!nativePayload_->recordDescriptorSetBind(
                *nativeSet, snapshot->setIndex(), nullptr, 0u, diagnostics))
            return commandFail(diagnostics, pass_.commandCount());
        // The bounded indexedSnapshots owner retains this immutable set and
        // its native payload through completion; no second resource owner is
        // needed in the ordinary recording vector.
        const auto tracked =
            boundDescriptors_.bindSet(snapshot->setIndex(), *descriptors, nullptr, 0u);
        if (!tracked.success)
            return commandFail(tracked.diagnostics, pass_.commandCount());
        boundSnapshot_ = std::move(snapshot);
        return pass_.record();
    }

    rhi::CommandValidationResult setPipeline(const rhi::ComputePipeline& pipeline) override
    {
        if (nativePayload_ && nativePayload_->hasOrdinaryAttachments())
            return originalCommand(rhi::CommandUsageKind::BindComputePipeline,
                rhi::CommandScalarArguments::None{},
                &pipeline);
        if (!pass_.preflightResources(1u))
            return pass_.rejected();
        if (pipeline.nativeHandleExposedToUpperLayer()) {
            return commandFail("Vulkan RHI compute pipeline exposed native handle", 0);
        }
        if (!nativePayload_) {
            return commandFail("RHI-VULKAN-NATIVE-COMPUTE-PASS-PAYLOAD-MISSING", 0);
        }
        if (boundSnapshot_)
            return commandFail("RHI-BINDLESS-PIPELINE-CHANGE-AFTER-SNAPSHOT", pass_.commandCount());
        const auto authorization = pipeline.bindlessAuthorization();
        if (authorization && !authorization->current(authorization))
            return commandFail("RHI-BINDLESS-PIPELINE-AUTHORIZATION-STALE", 0);
        if (authorization && !authorization->plan().executable)
            return commandFail(
                "RHI-BINDLESS-ORIGINAL-PREPARATION-NOT-EXECUTABLE", pass_.commandCount());
        if (authorization
            && (authorization->plan().originalExecutionContractVersion == 6u
                || authorization->plan().originalExecutionContractVersion == 8u
                || authorization->plan().originalExecutionContractVersion == 9u))
            return commandFail(
                "RHI-BINDLESS-ORIGINAL-EXECUTION-PUBLIC-ROOT-REQUIRED", pass_.commandCount());
        if (!authorization && !pipeline.shaderReflection().bindlessContractVersion.empty())
            return commandFail("RHI-BINDLESS-RECORD-AUTHORIZATION-INVALID", pass_.commandCount());
        if (authorization) {
            std::pmr::string retentionDiagnostic;
            if (!nativePayload_->retainIndexedPipeline(authorization, retentionDiagnostic))
                return commandFail(retentionDiagnostic, 0);
        }
        pipelineLayout_ = pipeline.layout();
        shaderReflection_ = &pipeline.shaderReflection();
        nativeComputePipeline_ = vulkanRhiNativePayload(pipeline);
        if (!pass_.retainResources(&nativeComputePipeline_, 1u))
            return pass_.rejected();
        std::pmr::string diagnostics;
        if (!nativeComputePipeline_
            || !nativePayload_->recordComputePipelineBind(*nativeComputePipeline_, diagnostics)) {
            return commandFail(diagnostics.empty()
                    ? "Vulkan RHI native compute bind requires a native pipeline"
                    : diagnostics,
                0);
        }
        activeAuthorization_ = authorization;
        activePipeline_ = &pipeline;
        boundDescriptors_.reset();
        return pass_.record();
    }

    rhi::CommandValidationResult bindDescriptorSet(std::uint32_t setIndex,
        const rhi::DescriptorSet& set,
        const std::uint32_t* dynamicOffsets,
        std::uint32_t dynamicOffsetCount) override
    {
        if (nativePayload_ && nativePayload_->hasOrdinaryAttachments()) {
            if (dynamicOffsetCount || dynamicOffsets)
                return pass_.rejectStorage("RHI-ORDINARY-MIXED-DYNAMIC-OFFSETS");
            return originalCommand(rhi::CommandUsageKind::BindComputeDescriptor,
                rhi::CommandScalarArguments::DescriptorBinding{setIndex},
                nullptr,
                &set);
        }
        if (!pass_.preflightResources(1u))
            return pass_.rejected();
        if (activeAuthorization_)
            return commandFail("RHI-BINDLESS-RECORD-SNAPSHOT-REQUIRED", pass_.commandCount());
        const auto trackingCapacity = boundDescriptors_.validateSetCapacity(setIndex);
        if (!trackingCapacity.success)
            return commandFail(trackingCapacity.diagnostics, pass_.commandCount());
        if (set.stats().nativeHandleExposedToUpperLayer) {
            return commandFail("Vulkan RHI descriptor set exposed native handle", 0);
        }
        const auto resources = rhi::validateDescriptorResources(
            set, rhi::DescriptorContentRequirement::WrittenElements);
        if (!resources.success)
            return commandFail(resources.diagnostics, pass_.commandCount());
        const auto dynamicValidation =
            rhi::validateDynamicOffsetArguments(set.layout(), dynamicOffsets, dynamicOffsetCount);
        if (!dynamicValidation.success) {
            return commandFail(dynamicValidation.diagnostics, 0);
        }
        if (nativePayload_) {
            auto descriptorSet = vulkanRhiNativePayload(set);
            if (!pass_.retainResources(&descriptorSet, 1u))
                return pass_.rejected();
            std::pmr::string diagnostics;
            if (!descriptorSet
                || !nativePayload_->recordDescriptorSetBind(
                    *descriptorSet, setIndex, dynamicOffsets, dynamicOffsetCount, diagnostics)) {
                return commandFail(diagnostics.empty()
                        ? "Vulkan RHI native descriptor bind requires a native descriptor set"
                        : diagnostics,
                    0);
            }
        }
        const auto tracked =
            boundDescriptors_.bindSet(setIndex, set, dynamicOffsets, dynamicOffsetCount);
        if (!tracked.success)
            return commandFail(tracked.diagnostics, pass_.commandCount());
        return pass_.record();
    }

    rhi::CommandValidationResult bindDescriptorTable(
        std::uint32_t, const rhi::DescriptorTable& table) override
    {
        if (!pass_.preflightResources(0u))
            return pass_.rejected();
        (void)table;
        if (activeAuthorization_)
            return commandFail("RHI-BINDLESS-RECORD-SNAPSHOT-REQUIRED", pass_.commandCount());
        return commandFail("RHI-VULKAN-DESCRIPTOR-TABLE-NATIVE-LOWERING-UNAVAILABLE", 0);
    }

    rhi::CommandValidationResult pushConstants(rhi::ShaderStage stages,
        std::uint32_t offsetBytes,
        std::uint32_t sizeBytes,
        const void* data) override
    {
        if (!pass_.preflightResources(0u))
            return pass_.rejected();
        auto cmdResult =
            rhi::semantics::validatePushConstantCommand(stages, offsetBytes, sizeBytes, data);
        if (!cmdResult.success) {
            return commandFail(cmdResult.diagnostics, 0);
        }
        auto bindResult = rhi::semantics::validatePushConstantBinding(pipelineLayout_,
            rhi::ShaderStage::Compute,
            stages,
            offsetBytes,
            sizeBytes,
            shaderReflection_);
        if (!bindResult.success) {
            return commandFail(bindResult.diagnostics, 0);
        }
        if (pushState_.hasUnconsumedUpdate()) {
            return commandFail(rhi::semantics::PushConstantDiagCode::kDuplicateUnconsumed
                    + std::pmr::string("push constants already set without intervening dispatch"),
                0);
        }
        // P2 executable-payload gate. Compute pipelines use their own
        // VkPipelineLayout and therefore need a compute bind-point lowering;
        // recording only the semantic command would be a false success.
        if (!nativePayload_) {
            return commandFail(rhi::semantics::PushConstantDiagCode::kNoExecPayload
                    + std::pmr::string(
                        "Vulkan compute push constants require a native command payload"),
                0);
        }
        std::pmr::string diagnostics;
        if (!nativePayload_->recordComputePushConstants(
                offsetBytes, sizeBytes, data, diagnostics)) {
            return commandFail(diagnostics, 0);
        }
        auto result = pass_.record();
        if (result.success) {
            pushState_.onPush();
        }
        return result;
    }

    rhi::CommandValidationResult dispatch(
        std::uint32_t groupCountX, std::uint32_t groupCountY, std::uint32_t groupCountZ) override
    {
        if (nativePayload_ && nativePayload_->hasOrdinaryAttachments())
            return originalCommand(rhi::CommandUsageKind::Dispatch,
                rhi::CommandScalarArguments::Dispatch{groupCountX, groupCountY, groupCountZ});
        if (!pass_.preflightResources(0u))
            return pass_.rejected();
        const auto descriptorUse = boundDescriptors_.validateLiveBindings();
        if (!descriptorUse.success)
            return commandFail(descriptorUse.diagnostics, pass_.commandCount());
        if (activeAuthorization_ && !boundSnapshot_)
            return commandFail("RHI-BINDLESS-RECORD-SNAPSHOT-REQUIRED", pass_.commandCount());
        if (groupCountX == 0 || groupCountY == 0 || groupCountZ == 0) {
            return commandFail("Vulkan RHI dispatch requires non-zero groups", 0);
        }
        if (!nativePayload_) {
            return commandFail("RHI-VULKAN-NATIVE-COMPUTE-PAYLOAD-MISSING", 0);
        }
        std::pmr::string diagnostics;
        if (!nativePayload_->recordDispatch(groupCountX, groupCountY, groupCountZ, diagnostics)) {
            return commandFail(diagnostics, 0);
        }
        auto result = pass_.record();
        if (result.success) {
            pushState_.onConsume();
        }
        return result;
    }

    rhi::CommandValidationResult dispatchIndirect(
        const rhi::GpuBuffer& indirectBuffer, std::uint64_t indirectOffset) override
    {
        if (!pass_.preflightResources(1u))
            return pass_.rejected();
        if (activeAuthorization_)
            return commandFail("RHI-BINDLESS-INDIRECT-DISPATCH-UNSUPPORTED", pass_.commandCount());
        const auto descriptorUse = boundDescriptors_.validateLiveBindings();
        if (!descriptorUse.success)
            return commandFail(descriptorUse.diagnostics, pass_.commandCount());
        constexpr std::uint64_t kDispatchArgumentsSize = sizeof(std::uint32_t) * 3u;
        if ((indirectOffset & 3u) != 0u
            || (indirectBuffer.desc().usage & rhi::ResourceUsage::IndirectBuffer)
                == rhi::ResourceUsage::None
            || indirectOffset > indirectBuffer.desc().sizeBytes
            || kDispatchArgumentsSize > indirectBuffer.desc().sizeBytes - indirectOffset) {
            return commandFail("Vulkan RHI indirect dispatch arguments are invalid", 0);
        }
        if (!nativePayload_) {
            return commandFail("Vulkan RHI indirect dispatch requires a native command payload", 0);
        }
        const auto nativeBuffer =
            resourceRegistry_ ? resourceRegistry_->payload(indirectBuffer.id()) : nullptr;
        if (!pass_.retainResources(&nativeBuffer, 1u))
            return pass_.rejected();
        std::pmr::string diagnostics;
        if (!nativeBuffer
            || !nativePayload_->recordDispatchIndirect(
                *nativeBuffer, indirectOffset, diagnostics)) {
            return commandFail(diagnostics.empty()
                    ? "Vulkan RHI indirect dispatch requires a tracked args buffer"
                    : diagnostics,
                0);
        }
        auto result = pass_.record();
        if (result.success) {
            pushState_.onConsume();
        }
        return result;
    }

    rhi::CommandValidationResult writeTimestamp(
        const rhi::QueryPool& pool, std::uint32_t queryIndex) override
    {
        if (nativePayload_ && nativePayload_->recordingStorageBound()) {
            if (const auto* id = nativePayload_->recordOriginalTimestamp(
                    pool, queryIndex, pass_.commandCount(), recordingIdentity_))
                return pass_.rejectStorage(id);
            return pass_.acceptOriginalTimestamp();
        }
        if (!pass_.preflightResources(0u))
            return pass_.rejected();
        if (!capabilityContract_) {
            return commandFail("Vulkan RHI timestamp write has no capability contract", 0);
        }
        rhi::semantics::CommandWorkloadDesc workload;
        workload.label = "vulkan-compute-timestamp";
        workload.commandCount = 1u;
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
        return pass_.record();
    }

    rhi::CommandValidationResult end() override
    {
        if (nativePayload_ && nativePayload_->hasOrdinaryAttachments()) {
            const auto result = originalCommand(
                rhi::CommandUsageKind::EndComputePass, rhi::CommandScalarArguments::None{});
            if (result.success)
                active_ = false;
            return result;
        }
        if (!pass_.preflightResources(0u))
            return pass_.rejected();
        if (activeAuthorization_ && !boundSnapshot_)
            return commandFail("RHI-BINDLESS-RECORD-SNAPSHOT-REQUIRED", pass_.commandCount());
        if (boundSnapshot_) {
            std::pmr::string diagnostics;
            if (!nativePayload_->scheduleBindlessFaultReadback(boundSnapshot_, diagnostics))
                return commandFail(diagnostics, pass_.commandCount());
        }
        core::pmr::String diagnostic;
        if (nativePayload_ && !nativePayload_->conformanceComputePass(false, diagnostic))
            return commandFail(diagnostic, pass_.commandCount());
        auto result = pass_.record();
        if (result.success) {
            boundDescriptors_.reset();
            boundSnapshot_.reset();
            activeAuthorization_.reset();
            active_ = false;
            pushState_.onPassEnd();
        }
        return result;
    }

private:
    template <class Value>
    rhi::CommandValidationResult originalCommand(rhi::CommandUsageKind kind,
        Value value,
        const rhi::ComputePipeline* pipeline = nullptr,
        const rhi::DescriptorSet* set = nullptr)
    {
        const auto scalar = rhi::CommandScalarArguments::capture(kind, value);
        if (!active_ || !scalar)
            return pass_.rejectStorage("RHI-ORDINARY-MIXED-COMPUTE-WRAPPER");
        if (const auto* id = nativePayload_->recordOrdinaryMixedCommand(
                pass_.commandCount(), *scalar, pipeline, set))
            return pass_.rejectStorage(id);
        return pass_.acceptOriginalCommand();
    }

    rhi::BoundDescriptorSlots boundDescriptors_;
    VulkanRhiPassState pass_;
    std::uint64_t recordingIdentity_ = 0u;
    std::shared_ptr<VulkanRhiNativeCommandPayload> nativePayload_;
    std::shared_ptr<VulkanRhiResourceRegistry> resourceRegistry_;
    std::shared_ptr<const rhi::BindlessAuthorization> activeAuthorization_;
    std::shared_ptr<const rhi::BindlessSnapshot> boundSnapshot_;
    const rhi::ComputePipeline* activePipeline_ = nullptr;
    const rhi::semantics::NativeBackendCapabilityContract* capabilityContract_ = nullptr;
    bool bindlessExecutable_ = false;
    std::shared_ptr<VulkanRhiNativeResourcePayload> nativeComputePipeline_;
    const rhi::PipelineLayout* pipelineLayout_ = nullptr;
    const rhi::ShaderReflectionDesc* shaderReflection_ = nullptr;
    rhi::semantics::PushConstantConsumptionState pushState_;
    bool& active_;
};

class VulkanRhiTransferPassEncoder final : public rhi::TransferPassEncoder {
public:
    VulkanRhiTransferPassEncoder(std::uint32_t& commandCount,
        bool& active,
        std::shared_ptr<VulkanRhiNativeCommandPayload> nativePayload,
        std::shared_ptr<VulkanRhiResourceRegistry> resourceRegistry,
        VulkanRhiStagingUploadPool* stagingUploadPool,
        rhi::RecordedStagingUploadJournal* stagingUploads,
        std::uint64_t uploadRetirementSerial,
        VulkanRhiColdFailure failure = {})
        : pass_(commandCount, nativePayload, failure), nativePayload_(std::move(nativePayload)),
          resourceRegistry_(std::move(resourceRegistry)), stagingUploadPool_(stagingUploadPool),
          stagingUploads_(stagingUploads), currentFrameSerial_(uploadRetirementSerial),
          active_(active)
    {}

    rhi::CommandValidationResult copyBufferToBuffer(std::uint64_t sizeBytes) override
    {
        if (!pass_.preflightResources(0u))
            return pass_.rejected();
        return sizeBytes == 0 ? commandFail("Vulkan RHI copy requires non-zero bytes", 0)
                              : pass_.record();
    }

    rhi::CommandValidationResult copyBufferToBuffer(const rhi::BufferCopyDesc& desc) override
    {
        if (!pass_.preflightResources(2u))
            return pass_.rejected();
        if (desc.sizeBytes == 0 || !desc.sourceBuffer.isValid()
            || !desc.destinationBuffer.isValid()) {
            return commandFail("Vulkan RHI buffer copy requires resources and bytes", 0);
        }
        if (resourceRegistry_
            && resourceRegistry_->bufferGpuTransferWritesSealed(desc.destinationBuffer))
            return commandFail("RHI-BUFFER-GPU-WRITES-SEALED", 0);
        if (nativePayload_) {
            const auto source =
                resourceRegistry_ ? resourceRegistry_->payload(desc.sourceBuffer) : nullptr;
            const auto destination =
                resourceRegistry_ ? resourceRegistry_->payload(desc.destinationBuffer) : nullptr;
            const std::shared_ptr<VulkanRhiNativeResourcePayload> owners[]{source, destination};
            if (!pass_.retainResources(owners, 2u))
                return pass_.rejected();
            std::pmr::string diagnostics;
            if (!source || !destination
                || !nativePayload_->recordBufferCopy(*source, *destination, desc, diagnostics)) {
                return commandFail(diagnostics.empty()
                        ? "Vulkan RHI native buffer copy requires tracked native resources"
                        : diagnostics,
                    0);
            }
        }
        return pass_.record();
    }

    rhi::CommandValidationResult copyBufferToTexture(
        const rhi::BufferToTextureCopyDesc& desc) override
    {
        if (!pass_.preflightResources(2u))
            return pass_.rejected();
        if (!desc.mipLayouts.empty()) {
            return commandFail("RHI-VULKAN-BUFFER-TEXTURE-PER-MIP-LAYOUT-NOT-LOWERED", 0);
        }
        if (!desc.sourceBuffer.isValid() || !desc.destinationTexture.isValid()) {
            return commandFail("Vulkan RHI buffer-to-texture copy requires resources", 0);
        }
        if (desc.extent.width == 0 || desc.extent.height == 0 || desc.extent.depth == 0) {
            return commandFail("Vulkan RHI buffer-to-texture copy requires non-zero extent", 0);
        }
        if (desc.bytesPerRow == 0 || desc.rowsPerImage == 0) {
            return commandFail(
                "Vulkan RHI texture upload requires explicit row and slice pitch", 0);
        }
        if ((desc.bytesPerRow % 256u) != 0u) {
            return commandFail("Vulkan RHI texture upload row pitch must be 256-byte aligned", 0);
        }
        if (nativePayload_) {
            const auto source =
                resourceRegistry_ ? resourceRegistry_->payload(desc.sourceBuffer) : nullptr;
            const auto destination =
                resourceRegistry_ ? resourceRegistry_->payload(desc.destinationTexture) : nullptr;
            const std::shared_ptr<VulkanRhiNativeResourcePayload> owners[]{source, destination};
            if (!pass_.retainResources(owners, 2u))
                return pass_.rejected();
            std::pmr::string diagnostics;
            if (!source || !destination
                || !nativePayload_->recordBufferToImageCopy(
                    *source, *destination, desc, diagnostics)) {
                return commandFail(diagnostics.empty() ? "Vulkan RHI native buffer-to-texture copy "
                                                         "requires tracked native resources"
                                                       : diagnostics,
                    0);
            }
        }
        return pass_.record();
    }

    rhi::CommandValidationResult copyTextureToBuffer(
        const rhi::TextureToBufferCopyDesc& desc) override
    {
        if (nativePayload_ && nativePayload_->hasOrdinaryAttachments())
            return originalCommand(rhi::CommandUsageKind::TextureToBufferCopy, &desc);
        if (!pass_.preflightResources(2u))
            return pass_.rejected();
        if (!desc.mipLayouts.empty()) {
            return commandFail("RHI-VULKAN-TEXTURE-BUFFER-PER-MIP-LAYOUT-NOT-LOWERED", 0);
        }
        if (!desc.sourceTexture.isValid() || !desc.destinationBuffer.isValid()) {
            return commandFail("Vulkan RHI texture-to-buffer copy requires resources", 0);
        }
        if (resourceRegistry_
            && resourceRegistry_->bufferGpuTransferWritesSealed(desc.destinationBuffer))
            return commandFail("RHI-BUFFER-GPU-WRITES-SEALED", 0);
        if (nativePayload_) {
            const auto source =
                resourceRegistry_ ? resourceRegistry_->payload(desc.sourceTexture) : nullptr;
            const auto destination =
                resourceRegistry_ ? resourceRegistry_->payload(desc.destinationBuffer) : nullptr;
            const std::shared_ptr<VulkanRhiNativeResourcePayload> owners[]{source, destination};
            if (!pass_.retainResources(owners, 2u))
                return pass_.rejected();
            std::pmr::string diagnostics;
            if (!source || !destination
                || !nativePayload_->recordImageToBufferCopy(
                    *source, *destination, desc, diagnostics)) {
                return commandFail(diagnostics.empty() ? "Vulkan RHI native texture-to-buffer copy "
                                                         "requires tracked native resources"
                                                       : diagnostics,
                    0);
            }
        }
        return pass_.record();
    }

    rhi::CommandValidationResult copyTextureToTexture(
        const rhi::TextureToTextureCopyDesc& desc) override
    {
        if (!pass_.preflightResources(2u))
            return pass_.rejected();
        if (!desc.mipLayouts.empty()) {
            return commandFail("RHI-VULKAN-TEXTURE-COPY-PER-MIP-LAYOUT-NOT-LOWERED", 0);
        }
        if (!desc.sourceTexture.isValid() || !desc.destinationTexture.isValid()) {
            return commandFail("Vulkan RHI texture copy requires resources", 0);
        }
        if (desc.extent.width == 0 || desc.extent.height == 0 || desc.extent.depth == 0) {
            return commandFail("Vulkan RHI texture copy requires non-zero extent", 0);
        }
        if (nativePayload_) {
            const auto source =
                resourceRegistry_ ? resourceRegistry_->payload(desc.sourceTexture) : nullptr;
            const auto destination =
                resourceRegistry_ ? resourceRegistry_->payload(desc.destinationTexture) : nullptr;
            const std::shared_ptr<VulkanRhiNativeResourcePayload> owners[]{source, destination};
            if (!pass_.retainResources(owners, 2u))
                return pass_.rejected();
            std::pmr::string diagnostics;
            if (!source || !destination
                || !nativePayload_->recordImageToImageCopy(
                    *source, *destination, desc, diagnostics)) {
                return commandFail(diagnostics.empty() ? "Vulkan RHI native texture-to-texture "
                                                         "copy requires tracked native resources"
                                                       : diagnostics,
                    0);
            }
        }
        return pass_.record();
    }

    rhi::CommandValidationResult uploadBuffer(const rhi::UploadBufferDesc& desc) override
    {
        if (!pass_.preflightResources(2u))
            return pass_.rejected();
        if (const auto* id = preflightStagingJournal())
            return pass_.rejectStorage(id);
        if (!desc.backendOwnedStaging || desc.stagingIntent != rhi::StagingIntent::Upload
            || desc.sizeBytes == 0 || !desc.sourceUploadBuffer.isValid()
            || !desc.destinationResource.isValid()) {
            return commandFail("Vulkan RHI upload requires backend-owned staging, source buffer, "
                               "destination buffer, and bytes",
                0);
        }
        if (resourceRegistry_
            && resourceRegistry_->bufferGpuTransferWritesSealed(desc.destinationResource))
            return commandFail("RHI-BUFFER-GPU-WRITES-SEALED", 0);
        if (!stagingUploadPool_) {
            return commandFail("Vulkan RHI upload requires a backend-owned staging pool", 0);
        }
        const auto stagingRetirementSerial = currentFrameSerial_;
        if (nativePayload_)
            retireCompletedVulkanRhiSubmittedCommands(*nativePayload_->runtime());
        auto staging = stagingUploadPool_->allocate({
            desc.label,
            desc.sizeBytes,
            256,
            stagingRetirementSerial,
        });
        if (!staging.success) {
            return commandStagingFail(staging.diagnostics, 0, staging.diagnosticId);
        }
        if (!stagingUploads_) {
            (void)stagingUploadPool_->cancel(staging);
            return commandFail("RHI-STAGING-RECORDING-JOURNAL-MISSING", 0);
        }
        if (!stagingUploadPool_->enlist(*stagingUploads_,
                {
                    staging.pageIndex,
                    staging.offsetBytes,
                    staging.sizeBytes,
                    staging.frameSerial,
                })) {
            (void)stagingUploadPool_->cancel(staging);
            return commandFail("RHI-STAGING-RECORDING-LEASE-FAILED", 0);
        }
        if (nativePayload_) {
            const auto source =
                resourceRegistry_ ? resourceRegistry_->payload(desc.sourceUploadBuffer) : nullptr;
            const auto destination =
                resourceRegistry_ ? resourceRegistry_->payload(desc.destinationResource) : nullptr;
            const std::shared_ptr<VulkanRhiNativeResourcePayload> owners[]{source, destination};
            if (!pass_.retainResources(owners, 2u))
                return pass_.rejected();
            std::pmr::string diagnostics;
            const rhi::BufferCopyDesc copy{
                desc.sourceUploadBuffer,
                desc.destinationResource,
                0,
                0,
                desc.sizeBytes,
            };
            if (!source || !destination
                || !nativePayload_->recordBufferCopy(*source, *destination, copy, diagnostics)) {
                (void)stagingUploads_->rollbackLast();
                return commandFail(diagnostics.empty()
                        ? "Vulkan RHI native upload requires tracked source and destination buffers"
                        : diagnostics,
                    0);
            }
        }
        return pass_.record();
    }

    rhi::CommandValidationResult uploadTexture(const rhi::UploadTextureDesc& desc) override
    {
        if (!pass_.preflightResources(1u))
            return pass_.rejected();
        if (nativePayload_ && nativePayload_->hasOrdinaryTextureUploads()) {
            if (const auto* id =
                    nativePayload_->recordOrdinaryTextureUpload(pass_.commandCount(), desc))
                return pass_.rejectStorage(id);
            return pass_.record();
        }
        if (const auto* id = preflightStagingJournal())
            return pass_.rejectStorage(id);
        if (!desc.backendOwnedStaging || desc.stagingIntent != rhi::StagingIntent::Upload
            || desc.pixelDataSize == 0 || desc.pixelData == nullptr) {
            return commandFail(
                "Vulkan RHI texture upload requires backend-owned staging and valid pixel data", 0);
        }
        if (!desc.mipLayouts.empty() || desc.subresources.mipLevelCount != 1u) {
            return commandFail("RHI-VULKAN-TEXTURE-UPLOAD-PER-MIP-LAYOUT-NOT-LOWERED", 0);
        }
        if (desc.bytesPerRow == 0 || desc.rowsPerImage == 0) {
            return commandFail(
                "Vulkan RHI texture upload requires explicit row and slice pitch", 0);
        }
        if (!stagingUploadPool_) {
            return commandFail(
                "Vulkan RHI texture upload requires a backend-owned staging pool", 0);
        }
        const auto stagingRetirementSerial = currentFrameSerial_;
        if (nativePayload_)
            retireCompletedVulkanRhiSubmittedCommands(*nativePayload_->runtime());
        auto staging = stagingUploadPool_->allocate({
            desc.label,
            desc.pixelDataSize,
            256,
            stagingRetirementSerial,
        });
        if (!staging.success) {
            return commandStagingFail(staging.diagnostics, 0, staging.diagnosticId);
        }
        if (!stagingUploads_) {
            (void)stagingUploadPool_->cancel(staging);
            return commandFail("RHI-STAGING-RECORDING-JOURNAL-MISSING", 0);
        }
        if (!stagingUploadPool_->enlist(*stagingUploads_,
                {
                    staging.pageIndex,
                    staging.offsetBytes,
                    staging.sizeBytes,
                    staging.frameSerial,
                })) {
            (void)stagingUploadPool_->cancel(staging);
            return commandFail("RHI-STAGING-RECORDING-LEASE-FAILED", 0);
        }
        if (staging.mappedData && desc.pixelData) {
            std::memcpy(static_cast<std::uint8_t*>(staging.mappedData) + staging.offsetBytes,
                desc.pixelData,
                desc.pixelDataSize);
            std::pmr::string flushDiagnostics;
            if (!staging.writeFlusher
                || !staging.writeFlusher->flushMappedWrite(
                    staging.offsetBytes, desc.pixelDataSize, flushDiagnostics)) {
                (void)stagingUploads_->rollbackLast();
                return commandFail(flushDiagnostics.empty()
                        ? "Vulkan RHI staging texture upload flush failed"
                        : flushDiagnostics,
                    0);
            }
            if (!staging.nativeHandleExposedToUpperLayer) {
                staging.nativeHandleExposedToUpperLayer = false;
            }
        }
        if (nativePayload_) {
            const auto destination =
                resourceRegistry_ ? resourceRegistry_->payload(desc.destinationTexture) : nullptr;
            if (!pass_.retainResources(&destination, 1u))
                return pass_.rejected();
            std::pmr::string diagnostics;
            if (!destination
                || !nativePayload_->recordUploadStagingTextureCopy(
                    staging.buffer, staging.offsetBytes, *destination, desc, diagnostics)) {
                (void)stagingUploads_->rollbackLast();
                return commandFail(diagnostics.empty()
                        ? "Vulkan RHI native texture upload requires a tracked destination texture"
                        : diagnostics,
                    0);
            }
        }
        return pass_.record();
    }

    rhi::CommandValidationResult readbackTexture(const rhi::ReadbackTextureDesc& desc) override
    {
        if (!pass_.preflightResources(2u))
            return pass_.rejected();
        if (!desc.mipLayouts.empty() || desc.extent.width != 0u || desc.extent.height != 0u
            || desc.extent.depth != 0u || desc.destinationOffsetBytes != 0u
            || desc.bytesPerRow != 0u || desc.rowsPerImage != 0u
            || desc.sourceSubresources.mipLevelCount != 0u
            || desc.sourceSubresources.arrayLayerCount != 0u) {
            return commandFail("RHI-VULKAN-EXPLICIT-READBACK-LAYOUT-NOT-LOWERED", 0);
        }
        if (!desc.backendOwnedStaging || desc.sizeBytes == 0) {
            return commandFail("Vulkan RHI readback requires backend-owned staging", 0);
        }
        if (nativePayload_) {
            const auto source =
                resourceRegistry_ ? resourceRegistry_->payload(desc.sourceResource) : nullptr;
            const auto destination = resourceRegistry_
                ? resourceRegistry_->payload(desc.destinationReadbackBuffer)
                : nullptr;
            const std::shared_ptr<VulkanRhiNativeResourcePayload> owners[]{source, destination};
            if (!pass_.retainResources(owners, 2u))
                return pass_.rejected();
            std::pmr::string diagnostics;
            rhi::TextureToBufferCopyDesc copyDesc;
            copyDesc.sourceTexture = desc.sourceResource;
            copyDesc.destinationBuffer = desc.destinationReadbackBuffer;
            copyDesc.extent = {1, 1, 1};
            if (!source || !destination
                || !nativePayload_->recordImageToBufferCopy(
                    *source, *destination, copyDesc, diagnostics)) {
                return commandFail(diagnostics.empty()
                        ? "Vulkan RHI native readback requires tracked native resources"
                        : diagnostics,
                    0);
            }
        }
        return pass_.record();
    }

    rhi::CommandValidationResult end() override
    {
        if (nativePayload_ && nativePayload_->hasOrdinaryAttachments()) {
            const auto result = originalCommand(rhi::CommandUsageKind::EndTransferPass);
            if (result.success)
                active_ = false;
            return result;
        }
        if (!pass_.preflightResources(0u))
            return pass_.rejected();
        active_ = false;
        return pass_.record();
    }

private:
    rhi::CommandValidationResult originalCommand(
        rhi::CommandUsageKind kind, const rhi::TextureToBufferCopyDesc* copy = nullptr)
    {
        const auto scalar =
            rhi::CommandScalarArguments::capture(kind, rhi::CommandScalarArguments::None{});
        if (!active_ || !scalar)
            return pass_.rejectStorage("RHI-ORDINARY-MIXED-TRANSFER-WRAPPER");
        if (const auto* id = nativePayload_->recordOrdinaryMixedCommand(
                pass_.commandCount(), *scalar, nullptr, nullptr, copy))
            return pass_.rejectStorage(id);
        return pass_.acceptOriginalCommand();
    }

    const char* preflightStagingJournal() const noexcept
    {
        if (!stagingUploads_ || !stagingUploads_->bounded())
            return nullptr;
        if (const auto* id = stagingUploads_->preflightEnlist())
            return id;
        // Reserving journal entries does not make the existing pool's page
        // creation and slice metadata bounded or prove initialized byte input.
        return VulkanRhiRecordingStorageLimits::unsupportedStagingPoolDiagnostic();
    }

    VulkanRhiPassState pass_;
    std::shared_ptr<VulkanRhiNativeCommandPayload> nativePayload_;
    std::shared_ptr<VulkanRhiResourceRegistry> resourceRegistry_;
    VulkanRhiStagingUploadPool* stagingUploadPool_ = nullptr;
    rhi::RecordedStagingUploadJournal* stagingUploads_ = nullptr;
    std::uint64_t currentFrameSerial_ = 1;
    bool& active_;
};

static_assert(std::is_nothrow_move_constructible_v<VulkanRhiComputePassEncoder>);
static_assert(std::is_nothrow_move_constructible_v<VulkanRhiTransferPassEncoder>);

class VulkanRhiRenderBundleEncoder final : public rhi::RenderBundleEncoder {
public:
    rhi::CommandValidationResult bindIndexBuffer(
        const rhi::GpuBuffer&, rhi::IndexFormat, std::uint64_t) override
    {
        return commandOk(commandCount_);
    }

    rhi::CommandValidationResult drawIndexed(std::uint32_t indexCount,
        std::uint32_t,
        std::uint32_t,
        std::int32_t,
        std::uint32_t) override
    {
        if (indexCount == 0) {
            return commandFail("Vulkan RHI bundle draw requires indices", commandCount_);
        }
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult draw(
        std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) override
    {
        return commandOk(++commandCount_);
    }

    arp::core::UniquePtr<rhi::RenderBundle> finish() override
    {
        return arp::core::makeUnique<VulkanRhiRenderBundle>(commandCount_);
    }

private:
    std::uint32_t commandCount_ = 0;
};

class VulkanRhiSecondaryCommandEncoder final : public rhi::SecondaryCommandEncoder {
public:
    rhi::CommandValidationResult pushConstants(
        rhi::ShaderStage, std::uint32_t, std::uint32_t, const void*) override
    {
        // A-04: secondary push-constant recording is not supported in the
        // AtomicFullBlockPush profile.  Secondary command buffers do not
        // carry push-constant metadata (required stage, size, pipeline-layout
        // requirement) to the consuming primary encoder.  Until secondary
        // command IR/metadata is defined, push recording is rejected.
        return commandFail(rhi::semantics::PushConstantDiagCode::kSecondaryNotSupported
                + std::pmr::string("secondary push-constant recording is not supported"),
            commandCount_);
    }

    rhi::CommandValidationResult bindIndexBuffer(
        const rhi::GpuBuffer&, rhi::IndexFormat, std::uint64_t) override
    {
        return commandFail(
            "Vulkan RHI secondary index-buffer state has no executable native payload",
            commandCount_);
    }

    rhi::CommandValidationResult drawIndexed(std::uint32_t indexCount,
        std::uint32_t,
        std::uint32_t,
        std::int32_t,
        std::uint32_t) override
    {
        if (indexCount == 0) {
            return commandFail("Vulkan RHI secondary draw requires indices", commandCount_);
        }
        return commandOk(++commandCount_);
    }

    rhi::CommandValidationResult draw(
        std::uint32_t vertexCount, std::uint32_t, std::uint32_t, std::uint32_t) override
    {
        if (vertexCount == 0) {
            return commandFail("Vulkan RHI secondary draw requires vertices", commandCount_);
        }
        return commandOk(++commandCount_);
    }

    arp::core::UniquePtr<rhi::SecondaryCommandBuffer> finish() override
    {
        return arp::core::makeUnique<VulkanRhiSecondaryCommandBuffer>(commandCount_);
    }

private:
    std::uint32_t commandCount_ = 0;
};

class VulkanRhiCommandEncoder final : public rhi::CommandEncoderBase,
                                      public VulkanTextureReadbackRecorder {
public:
    VulkanRhiCommandEncoder(VulkanRhiNativeResourceAllocator nativeResources,
        std::shared_ptr<VulkanRhiResourceRegistry> resourceRegistry,
        VulkanRhiStagingUploadPool* stagingUploadPool,
        const rhi::semantics::NativeBackendCapabilityContract& capabilityContract,
        bool bindlessExecutable,
        bool requireNativeCommandEncoding,
        const rhi::CommandEncoderDesc& retention,
        std::shared_ptr<const rhi::OriginalCommandStreamProgram> coldPassProgram = {},
        const VulkanRhiRecordingStorageLimits* storage = nullptr,
        const rhi::semantics::NativeCommandLoweringDecision* queryLowering = nullptr,
        const rhi::OriginalRenderPassPreparationRequest* renderPreparation = nullptr)
        : requireNativeCommandEncoding_(requireNativeCommandEncoding),
          bindlessExecutable_(bindlessExecutable),
          indirectFirstInstanceEnabled_(nativeResources.runtime()
              && nativeResources.runtime()->indirectFirstInstanceEnabled()),
          recordingIdentity_(rhi::allocateGpuResourceId().value),
          indirectAccesses_(retention.maxBindlessIndirectAccesses),
          nativeAllocator_(nativeResources), resourceRegistry_(std::move(resourceRegistry)),
          stagingUploadPool_(stagingUploadPool), capabilityContract_(&capabilityContract),
          uploadRetirementSerial_(gNextUploadRetirementSerial.tryAcquire()),
          stagingUploads_(arp::core::makeUnique<rhi::RecordedStagingUploadJournal>()),
          finishedCommand_(
              core::makeUnique<VulkanRhiCommandBuffer>(VulkanRhiCommandBuffer::ColdStorage{})),
          coldPassProgram_(std::move(coldPassProgram)), coldStorage_(storage != nullptr)
    {
        if (!uploadRetirementSerial_) {
            markCommandFail("RHI-VULKAN-STAGING-RESERVATION-IDENTITY-EXHAUSTED");
            return;
        }
        if (coldStorage_) {
            coldPassFailure_.reserve(std::max({kColdComputePassCapacity.size(),
                kColdTransferPassCapacity.size(),
                kColdRenderPassUnavailable.size(),
                VulkanRhiRecordingStorageLimits::diagnosticCapacity()}));
            nativeResources_.reserve(storage->resourceReferences);
            std::size_t compute = 0u, transfer = 0u;
            const auto count = [&](const auto& operations) {
                for (const auto& operation : operations) {
                    compute += operation.kind == rhi::CommandUsageKind::BeginComputePass;
                    transfer += operation.kind == rhi::CommandUsageKind::BeginTransferPass;
                }
            };
            if (coldPassProgram_)
                count(coldPassProgram_->operations());
            else
                count(storage->originalQueries->inputs->originalScalars()
                        ->originalUsage()
                        ->commands());
            std::uint64_t uploads = 0u;
            if (coldPassProgram_) {
                const auto& budget = *coldPassProgram_->recordingBudget();
                const auto buffers =
                    budget.limits
                        .commands[static_cast<std::size_t>(rhi::CommandUsageKind::BufferUpload)];
                const auto textures =
                    budget.limits
                        .commands[static_cast<std::size_t>(rhi::CommandUsageKind::TextureUpload)];
                if (!core::canAddUnsigned(buffers, textures)) {
                    markCommandFail("RHI-STAGING-JOURNAL-ENTRY-RANGE");
                    return;
                }
                // The original Program was validated above, including its
                // exact upload counts. An empty journal needs no untyped
                // stream reconstruction, which discards a typed state root.
                // Nonempty staging retains its existing admission contract.
                if (coldPassProgram_->counts().bufferUploads
                    || coldPassProgram_->counts().textureUploadMips) {
                    if (const auto* id =
                            rhi::OriginalCommandUploadStagingContract::inspectJournalEntries(
                                coldPassProgram_->originalInputs(),
                                coldPassProgram_->originalBudget(),
                                buffers + textures,
                                uploads)) {
                        markCommandFail(id);
                        return;
                    }
                }
            }
            // The original Query-only preparation has no upload admission.
            // Its zero-entry journal therefore refuses uploads before mutation.
            if (uploads > SIZE_MAX) {
                markCommandFail("RHI-STAGING-JOURNAL-ENTRY-RANGE");
                return;
            }
            if (const auto* id =
                    stagingUploads_->prepareBounded(static_cast<std::size_t>(uploads))) {
                markCommandFail(id);
                return;
            }
            coldComputePasses_.reserve(compute);
            coldTransferPasses_.reserve(transfer);
            for (std::size_t i = 0u; i < compute; ++i)
                coldComputePasses_.push_back(
                    core::makeUnique<VulkanRhiComputePassEncoder>(commandCount_,
                        activePass_,
                        recordingIdentity_,
                        nullptr,
                        nullptr,
                        nullptr,
                        false));
            for (std::size_t i = 0u; i < transfer; ++i)
                coldTransferPasses_.push_back(core::makeUnique<VulkanRhiTransferPassEncoder>(
                    commandCount_, activePass_, nullptr, nullptr, nullptr, nullptr, 0u));
            if (renderPreparation && renderPreparation->contractVersion >= 2u) {
                // Complete original/profile/count admission already succeeded.
                // Traverse that same Program directly so allocation exceptions
                // propagate to the cold factory, never through a noexcept visitor.
                coldRenderPasses_.reserve(coldPassProgram_->counts().renderPasses);
                for (const auto& operation : coldPassProgram_->operations()) {
                    if (operation.kind != rhi::CommandUsageKind::BeginRenderPass)
                        continue;
                    const auto& original =
                        std::get<std::shared_ptr<const rhi::RenderPassArguments>>(
                            *coldPassProgram_->originalInputs()->descriptorAt(
                                operation.commandIndex));
                    auto wrapper = createVulkanRhiColdRenderPassEncoder(commandCount_,
                        activePass_,
                        recordingIdentity_,
                        indirectAccesses_,
                        original);
                    if (!wrapper) {
                        markCommandFail("RHI-VULKAN-COLD-RENDER-PASS-ORIGINAL-OWNER");
                        return;
                    }
                    coldRenderPasses_.push_back(std::move(wrapper));
                }
            }
        }
        std::pmr::string diagnostics;
        nativePayload_ =
            createVulkanRhiNativeCommandPayload(nativeResources, diagnostics, retention, storage);
        if (!nativePayload_ && requireNativeCommandEncoding_) {
            lastValidation_ = {false, std::move(diagnostics), commandCount_};
        }
        if (nativePayload_ && coldPassProgram_) {
            originalRecordingStates_ = recordedStates_.get();
            if (!nativePayload_->adoptOriginalStreamJournal(
                    recordedStates_, diagnostics, recordingIdentity_)) {
                originalRecordingStates_ = nullptr;
                nativePayload_.reset();
                lastValidation_ = {false, std::move(diagnostics), commandCount_};
            }
        }
        if (nativePayload_ && renderPreparation
            && rhi::OriginalRenderPassPreparationContract::hasPassTiming(*renderPreparation))
            nativePayload_->observeFrameSerial(renderPreparation->passTimingFrameSerial);
        if (nativePayload_ && !coldPassProgram_ && storage && storage->originalQueries
            && storage->originalQueries->recordingContractVersion) {
            originalRecordingStates_ = recordedStates_.get();
            if (!queryLowering
                || !nativePayload_->adoptOriginalQueryJournal(
                    recordedStates_, recordingIdentity_, *queryLowering, diagnostics)) {
                originalRecordingStates_ = nullptr;
                nativePayload_.reset();
                lastValidation_ = {false, std::move(diagnostics), commandCount_};
            }
        }
    }

    ~VulkanRhiCommandEncoder() override
    {
        // A published finished wrapper owns cancellation from this point on.
        // Failed/unpublished finish still leaves this cold wrapper in place.
        if (finishedCommand_ && nativePayload_)
            nativePayload_->abandonOrdinaryRecording();
    }

    const auto& nativePayload() const noexcept { return nativePayload_; }

    VulkanRhiColdRenderPassObservation coldRenderPassObservation(std::size_t index) const noexcept
    {
        return index < coldRenderPasses_.size() && coldRenderPasses_[index]
            ? inspectVulkanRhiColdRenderPass(*coldRenderPasses_[index])
            : VulkanRhiColdRenderPassObservation{};
    }

    bool prepareOrdinaryRenderPasses(const rhi::GpuDevice& device,
        const rhi::OriginalRenderPassPreparationRequest& request,
        core::pmr::String& diagnostic)
    {
        return nativePayload_
            && nativePayload_->prepareOrdinaryRenderPasses(device, request, diagnostic);
    }

    bool prepareOrdinaryTextureUploads(rhi::GpuDevice& device,
        const rhi::OriginalOrdinaryTextureUploadContract::Budget& budget,
        const rhi::OriginalOrdinaryTextureUploadContract::Binding* bindings,
        std::size_t count,
        core::pmr::String& diagnostic,
        const rhi::OriginalTextureUploadPreparationRequest* publicRequest,
        const std::shared_ptr<core::CpuMemoryQuota>& publicationQuota,
        core::pmr::Vector<std::shared_ptr<const rhi::OriginalTextureUploadPublication>>*
            publications)
    {
        return nativePayload_ && stagingUploadPool_ && stagingUploads_
            && nativePayload_->prepareOrdinaryTextureUploads(device,
                *stagingUploadPool_,
                *stagingUploads_,
                uploadRetirementSerial_,
                budget,
                bindings,
                count,
                diagnostic,
                publicRequest,
                publicationQuota,
                publications);
    }

    std::uint64_t recordingIdentity() const noexcept override { return recordingIdentity_; }

    bool ready() const
    {
        return uploadRetirementSerial_
            && (!requireNativeCommandEncoding_ || nativePayload_ != nullptr);
    }

    rhi::ResourceBarrierResult transition(
        rhi::GpuBuffer& buffer, const rhi::ResourceBarrierDesc& desc) override
    {
        if (!preflightColdBarrier(rhi::CommandUsageKind::BufferBarrier, 1u))
            return {};
        if (const auto* id = rhi::semantics::ResourceAccessUsageValidation::validateGeometryBarrier(
                buffer.desc(), desc, desc.before, desc.after)) {
            markCommandFail(id);
            return {false, lastValidation_.diagnostics};
        }
        if (!statesForRecording()) {
            markCommandFail("RHI-RESOURCE-STATE-RECORDING-FINISHED");
            return {false, lastValidation_.diagnostics};
        }
        if (nativePayload_ && nativePayload_->hasOrdinaryAttachments()) {
            if (const auto* id = nativePayload_->preflightOrdinaryBufferTransition(
                    buffer, desc, commandCount_)) {
                failColdPass(id);
                return {false, {}};
            }
            if (const auto* id = nativePayload_->claimOrdinaryAttachments()) {
                failColdPass(id);
                return {false, {}};
            }
        }
        auto result = transitionVulkanRhiBuffer(buffer, desc, statesForRecording());
        if (!result.success) {
            markCommandFail(result.diagnostics);
            return result;
        }
        auto payload = vulkanRhiNativePayload(buffer);
        rememberNativeResource(nativeResources_, buffer.id(), payload);
        if (nativePayload_) {
            nativePayload_->retainResource(payload);
        }
        if (nativePayload_) {
            std::pmr::string diagnostics;
            if (!payload || !nativePayload_->recordBufferBarrier(*payload, desc, diagnostics)) {
                const auto message = diagnostics.empty()
                    ? std::pmr::string("Vulkan RHI native buffer barrier requires native payload")
                    : diagnostics;
                markCommandFail(message);
                result.success = false;
                result.diagnostics = lastValidation_.diagnostics;
                return result;
            }
        }
        if (nativePayload_ && nativePayload_->hasOrdinaryAttachments())
            nativePayload_->consumeOrdinaryAttachmentOperation();
        record();
        return result;
    }

    rhi::ResourceBarrierResult transition(
        rhi::GpuTexture& texture, const rhi::ResourceBarrierDesc& desc) override
    {
        if (!preflightColdBarrier(rhi::CommandUsageKind::TextureBarrier, 1u))
            return {};
        if (const auto* id = rhi::semantics::ResourceAccessUsageValidation::validateGeometryBarrier(
                texture.desc(), desc, desc.before, desc.after)) {
            markCommandFail(id);
            return {false, lastValidation_.diagnostics};
        }
        if (!statesForRecording()) {
            markCommandFail("RHI-RESOURCE-STATE-RECORDING-FINISHED");
            return {false, lastValidation_.diagnostics};
        }
        if (std::getenv("ARP_VULKAN_TRACE_IMAGE_BARRIERS") != nullptr) {
            std::cerr << "[vulkan-barrier] label=" << texture.desc().label.c_str()
                      << " logical=" << static_cast<int>(texture.state())
                      << " before=" << static_cast<int>(desc.before)
                      << " after=" << static_cast<int>(desc.after) << '\n';
        }
        auto payload = vulkanRhiNativePayload(texture);
        if (nativePayload_) {
            std::pmr::string diagnostics;
            if (!payload
                || !nativePayload_->bindSwapchainFrameSync(*payload, desc.after, diagnostics)) {
                rhi::ResourceBarrierResult failed;
                failed.resource = texture.id();
                failed.diagnostics = diagnostics.empty()
                    ? std::pmr::string("Vulkan RHI native image barrier requires native payload")
                    : diagnostics;
                markCommandFail(failed.diagnostics);
                return failed;
            }
        }
        if (nativePayload_) {
            if (const auto* id = nativePayload_->preflightOrdinaryTextureTransition(
                    texture, desc, commandCount_)) {
                failColdPass(id);
                return {false, {}};
            }
            if (const auto* id = nativePayload_->claimOrdinaryAttachments()) {
                failColdPass(id);
                return {false, {}};
            }
        }
        const bool originalUpload = texture.originalContentExclusive() && nativePayload_
            && (nativePayload_->hasOrdinaryTextureUploads()
                || nativePayload_->hasOrdinaryAttachments());
        auto result = transitionVulkanRhiTexture(
            texture, desc, statesForRecording(), originalUpload ? nativePayload_.get() : nullptr);
        if (std::getenv("ARP_VULKAN_TRACE_IMAGE_BARRIERS") != nullptr) {
            const auto payload = vulkanRhiNativePayload(texture);
            std::cerr << "[vulkan-barrier-result] label=" << texture.desc().label.c_str()
                      << " success=" << (result.success ? 1 : 0) << " native=" << (payload ? 1 : 0)
                      << " image="
                      << (payload ? payload->backendVulkanImageHandle() : VK_NULL_HANDLE);
            if (!result.diagnostics.empty()) {
                std::cerr << " diag=" << result.diagnostics.c_str();
            }
            std::cerr << '\n';
        }
        if (!result.success) {
            markCommandFail(result.diagnostics);
            return result;
        }
        rememberNativeResource(nativeResources_, texture.id(), payload);
        if (nativePayload_) {
            nativePayload_->retainResource(payload);
        }
        if (nativePayload_) {
            std::pmr::string diagnostics;
            if (!payload
                || !(originalUpload
                        ? nativePayload_->recordValidatedOriginalImageBarrier(
                              *payload, desc, diagnostics)
                        : nativePayload_->recordImageBarrier(*payload, desc, diagnostics))) {
                const auto message = diagnostics.empty()
                    ? std::pmr::string("Vulkan RHI native image barrier requires native payload")
                    : diagnostics;
                markCommandFail(message);
                result.success = false;
                result.diagnostics = lastValidation_.diagnostics;
                return result;
            }
        }
        if (nativePayload_ && nativePayload_->hasOrdinaryAttachments())
            nativePayload_->consumeOrdinaryAttachmentOperation();
        record();
        return result;
    }

    rhi::ResourceBarrierResult memoryBarrier(
        rhi::GpuBuffer& buffer, const rhi::ResourceMemoryBarrierDesc& desc) override
    {
        if (!preflightColdBarrier(rhi::CommandUsageKind::BufferMemoryBarrier, 1u))
            return {};
        if (const auto* id = rhi::semantics::ResourceAccessUsageValidation::validateGeometryBarrier(
                buffer.desc(), desc, buffer.state(), buffer.state())) {
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
        if (!statesForRecording()) {
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
        auto payload = vulkanRhiNativePayload(buffer);
        if (!payload) {
            result.diagnostics = "RHI-RESOURCE-MEMORY-BARRIER-BACKEND-RESOURCE-MISSING";
            markCommandFail(result.diagnostics);
            return result;
        }
        rememberNativeResource(nativeResources_, buffer.id(), payload);
        if (nativePayload_) {
            nativePayload_->retainResource(payload);
            const rhi::ResourceBarrierDesc nativeBarrier{
                buffer.id(),
                {},
                buffer.state(),
                buffer.state(),
                desc.sourceStages,
                desc.destinationStages,
                desc.sourceAccess,
                desc.destinationAccess,
            };
            std::pmr::string diagnostics;
            if (!nativePayload_->recordBufferBarrier(*payload, nativeBarrier, diagnostics)) {
                result.diagnostics = diagnostics.empty()
                    ? std::pmr::string("RHI-VULKAN-MEMORY-BARRIER-NATIVE-PAYLOAD-MISSING")
                    : diagnostics;
                markCommandFail(result.diagnostics);
                return result;
            }
        }
        result.success = true;
        record();
        if (!lastValidation_.success) {
            result.success = false;
            result.diagnostics = lastValidation_.diagnostics;
        }
        return result;
    }

    rhi::ResourceBarrierResult memoryBarrier(
        rhi::GpuTexture& texture, const rhi::ResourceMemoryBarrierDesc& desc) override
    {
        if (!preflightColdBarrier(rhi::CommandUsageKind::TextureMemoryBarrier, 1u))
            return {};
        if (const auto* id = rhi::semantics::ResourceAccessUsageValidation::validateGeometryBarrier(
                texture.desc(), desc, texture.state(), texture.state())) {
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
        if (!statesForRecording()) {
            result.diagnostics = "RHI-RESOURCE-MEMORY-BARRIER-RECORDING-FINISHED";
            markCommandFail(result.diagnostics);
            return result;
        }
        if (!nativePayload_) {
            result.diagnostics = "RHI-VULKAN-MEMORY-BARRIER-NATIVE-PAYLOAD-MISSING";
            markCommandFail(result.diagnostics);
            return result;
        }
        if (hasActivePass()) {
            result.diagnostics = "RHI-RESOURCE-MEMORY-BARRIER-ACTIVE-PASS";
            markCommandFail(result.diagnostics);
            return result;
        }
        if (!resourceRegistry_ || !resourceRegistry_->payload(texture.id())) {
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
        auto payload = vulkanRhiNativePayload(texture);
        if (!payload) {
            result.diagnostics = "RHI-RESOURCE-MEMORY-BARRIER-BACKEND-RESOURCE-MISSING";
            markCommandFail(result.diagnostics);
            return result;
        }
        rememberNativeResource(nativeResources_, texture.id(), payload);
        if (nativePayload_) {
            nativePayload_->retainResource(payload);
            std::pmr::string diagnostics;
            if (!nativePayload_->recordResourceMemoryBarrier(desc, diagnostics)) {
                result.diagnostics = diagnostics.empty()
                    ? std::pmr::string("RHI-VULKAN-MEMORY-BARRIER-NATIVE-PAYLOAD-MISSING")
                    : diagnostics;
                markCommandFail(result.diagnostics);
                return result;
            }
        }
        result.success = true;
        record();
        if (!lastValidation_.success) {
            result.success = false;
            result.diagnostics = lastValidation_.diagnostics;
        }
        return result;
    }

    rhi::ResourceBarrierResult aliasBarrier(const rhi::AliasingBarrierDesc& desc) override
    {
        if (!preflightColdBarrier(rhi::CommandUsageKind::AliasBarrier, 0u))
            return {};
        rhi::ResourceBarrierResult result;
        result.resource = desc.afterResource;
        result.sourceStages = desc.sourceStages;
        result.destinationStages = desc.destinationStages;
        result.sourceAccess = desc.sourceAccess;
        result.destinationAccess = desc.destinationAccess;
        result.stageAccessAware = true;
        if (!nativePayload_) {
            result.diagnostics = "Vulkan alias barrier requires native command payload";
            markCommandFail(result.diagnostics);
            return result;
        }
        std::pmr::string diagnostics;
        if (!nativePayload_->recordAliasingBarrier(desc, diagnostics)) {
            result.diagnostics = diagnostics;
            markCommandFail(diagnostics);
            return result;
        }
        record();
        result.success = true;
        return result;
    }

    arp::core::UniquePtr<rhi::RenderPassEncoder> beginRenderPass(
        const rhi::RenderPassDesc& desc) override
    {
        if (coldStorage_) {
            if (!preflightColdRecording())
                return {};
            if (!nativePayload_->hasOrdinaryAttachments()) {
                failColdPass(kColdRenderPassUnavailable);
                return {};
            }
            if (activePass_ || finished_ || nextColdRenderPass_ >= coldRenderPasses_.size()) {
                failColdPass("RHI-ORDINARY-ATTACHMENT-RECORDING-PASS-ORDER");
                return {};
            }
            auto& pass = coldRenderPasses_[nextColdRenderPass_];
            if (const auto* id =
                    activateVulkanRhiColdRenderPass(*pass, nativePayload_, coldFailure())) {
                failColdPass(id);
                return {};
            }
            if (const auto* id =
                    nativePayload_->recordOrdinaryRenderPassBegin(commandCount_, desc)) {
                failColdPass(id);
                return {};
            }
            activePass_ = true;
            ++nextColdRenderPass_;
            markCommandOk();
            return std::move(pass);
        }
        const auto operations = rhi::semantics::validateRenderPassAttachmentOperations(desc);
        if (!operations.success) {
            lastValidation_ = commandFail(operations.diagnostics, commandCount_);
            return nullptr;
        }
        if (desc.layeredRendering) {
            lastValidation_ =
                commandFail("RHI-VULKAN-LAYERED-RENDERING-NOT-LOWERED", commandCount_);
            return nullptr;
        }
        if (!beginPassGuard("Vulkan RHI command encoder already has an active pass")) {
            return nullptr;
        }
        if (nativePayload_) {
            std::pmr::string diagnostics;
            if (!nativePayload_->flushPendingBarriers(diagnostics)) {
                markCommandFail(diagnostics);
                rollbackPassBegin();
                return nullptr;
            }
        }
        return createVulkanRhiRenderPassEncoder(commandCount_,
            activePass_,
            recordingIdentity_,
            bindlessExecutable_,
            indirectFirstInstanceEnabled_,
            indirectAccesses_,
            desc,
            nativePayload_,
            resourceRegistry_,
            capabilityContract_,
            lastValidation_);
    }

    arp::core::UniquePtr<rhi::ComputePassEncoder> beginComputePass(
        const rhi::ComputePassDesc& desc) override
    {
        if (!preflightColdRecording())
            return {};
        const bool originalMixed = nativePayload_ && nativePayload_->hasOrdinaryMixedPasses();
        if (nativePayload_ && nativePayload_->hasOrdinaryAttachments() && !originalMixed) {
            failColdPass("RHI-ORDINARY-ATTACHMENT-RECORDING-COMMAND-PROFILE");
            return {};
        }
        if (coldStorage_ && nextColdComputePass_ == coldComputePasses_.size()) {
            failColdPass(kColdComputePassCapacity);
            return {};
        }
        if (originalMixed) {
            if (!desc.label.empty()) {
                failColdPass("RHI-ORDINARY-MIXED-PASS-LABEL");
                return {};
            }
            const auto scalar = rhi::CommandScalarArguments::capture(
                rhi::CommandUsageKind::BeginComputePass, rhi::CommandScalarArguments::None{});
            if (const auto* id =
                    nativePayload_->recordOrdinaryMixedCommand(commandCount_, *scalar)) {
                failColdPass(id);
                return {};
            }
        }
        core::pmr::String diagnostic;
        if (nativePayload_ && !nativePayload_->conformanceComputePass(true, diagnostic)) {
            markCommandFail(diagnostic);
            return {};
        }
        if (!beginPassGuard("Vulkan RHI command encoder already has an active pass")) {
            return nullptr;
        }
        if (nativePayload_) {
            std::pmr::string diagnostics;
            if (!nativePayload_->flushPendingBarriers(diagnostics)) {
                markCommandFail(diagnostics);
                rollbackPassBegin();
                return nullptr;
            }
        }
        if (originalMixed)
            markCommandOk();
        else
            record();
        if (!lastValidation_.success) {
            rollbackPassBegin();
            return nullptr;
        }
        if (coldStorage_) {
            VulkanRhiComputePassEncoder pass(commandCount_,
                activePass_,
                recordingIdentity_,
                nativePayload_,
                resourceRegistry_,
                capabilityContract_,
                bindlessExecutable_,
                coldFailure());
            auto& slot = coldComputePasses_[nextColdComputePass_++];
            core::replaceCpuObjectByMove(*slot, pass);
            return std::move(slot);
        }
        return arp::core::makeUnique<VulkanRhiComputePassEncoder>(commandCount_,
            activePass_,
            recordingIdentity_,
            nativePayload_,
            resourceRegistry_,
            capabilityContract_,
            bindlessExecutable_);
    }

    arp::core::UniquePtr<rhi::TransferPassEncoder> beginTransferPass(
        const rhi::TransferPassDesc& desc) override
    {
        if (!preflightColdRecording())
            return {};
        const bool originalMixed = nativePayload_ && nativePayload_->hasOrdinaryMixedPasses();
        if (nativePayload_ && nativePayload_->hasOrdinaryAttachments() && !originalMixed) {
            failColdPass("RHI-ORDINARY-ATTACHMENT-RECORDING-COMMAND-PROFILE");
            return {};
        }
        if (coldStorage_ && nextColdTransferPass_ == coldTransferPasses_.size()) {
            failColdPass(kColdTransferPassCapacity);
            return {};
        }
        if (originalMixed) {
            if (!desc.label.empty()) {
                failColdPass("RHI-ORDINARY-MIXED-PASS-LABEL");
                return {};
            }
            const auto scalar = rhi::CommandScalarArguments::capture(
                rhi::CommandUsageKind::BeginTransferPass, rhi::CommandScalarArguments::None{});
            if (const auto* id =
                    nativePayload_->recordOrdinaryMixedCommand(commandCount_, *scalar)) {
                failColdPass(id);
                return {};
            }
        }
        core::pmr::String diagnostic;
        if (nativePayload_ && !nativePayload_->rejectConformanceClearWork(diagnostic)) {
            markCommandFail(diagnostic);
            return {};
        }
        if (!beginPassGuard("Vulkan RHI command encoder already has an active pass")) {
            return nullptr;
        }
        if (nativePayload_) {
            std::pmr::string diagnostics;
            if (!nativePayload_->flushPendingBarriers(diagnostics)) {
                markCommandFail(diagnostics);
                rollbackPassBegin();
                return nullptr;
            }
        }
        if (originalMixed)
            markCommandOk();
        else
            record();
        if (!lastValidation_.success) {
            rollbackPassBegin();
            return nullptr;
        }
        if (coldStorage_) {
            VulkanRhiTransferPassEncoder pass(commandCount_,
                activePass_,
                nativePayload_,
                resourceRegistry_,
                stagingUploadPool_,
                stagingUploads_.get(),
                uploadRetirementSerial_,
                coldFailure());
            auto& slot = coldTransferPasses_[nextColdTransferPass_++];
            core::replaceCpuObjectByMove(*slot, pass);
            return std::move(slot);
        }
        return arp::core::makeUnique<VulkanRhiTransferPassEncoder>(commandCount_,
            activePass_,
            nativePayload_,
            resourceRegistry_,
            stagingUploadPool_,
            stagingUploads_.get(),
            uploadRetirementSerial_);
    }

    bool scheduleTextureReadback(const rhi::GpuTexture& texture,
        std::string_view label,
        std::pmr::string& diagnostics) override
    {
        (void)texture;
        (void)label;
        diagnostics = rhi::BorrowedTextureReadbackRetirementContract::lowering(rhi::Backend::Vulkan)
                          .diagnostic;
        return false;
    }

    void pushDebugGroup(std::string_view label) override
    {
        if (!preflightDebugLabel())
            return;
        if (nativePayload_) {
            std::pmr::string diag;
            nativePayload_->recordPushDebugGroup(label, diag);
        }
    }

    void popDebugGroup() override
    {
        if (!preflightDebugLabel())
            return;
        if (nativePayload_) {
            std::pmr::string diag;
            nativePayload_->recordPopDebugGroup(diag);
        }
    }

    void insertDebugMarker(std::string_view label) override
    {
        if (!preflightDebugLabel())
            return;
        if (nativePayload_) {
            std::pmr::string diag;
            nativePayload_->recordInsertDebugMarker(label, diag);
        }
    }

    arp::core::UniquePtr<rhi::CommandBuffer> finish() override
    {
        if (!preflightColdRecording())
            return {};
        if (nativePayload_)
            if (const auto* id = nativePayload_->sealOrdinaryAttachments(commandCount_)) {
                failColdPass(id);
                return {};
            }
        if (!finishGuard("Vulkan RHI command encoder has an unclosed pass")) {
            return nullptr;
        }
        if (nativePayload_) {
            std::pmr::string diagnostics;
            if (!nativePayload_->flushPendingBarriers(diagnostics)) {
                markCommandFail(diagnostics);
                return nullptr;
            }
            if (const auto* id = nativePayload_->sealOriginalQueries(commandCount_)) {
                failColdPass(id);
                return {};
            }
            if (!nativePayload_->end(diagnostics)) {
                if (const auto* id = nativePayload_->recordingStorageDiagnostic()) {
                    failColdPass(id);
                    return {};
                }
                markCommandFail(diagnostics);
                return nullptr;
            }
            if (!nativePayload_->retainRecordedStagingUploads(stagingUploads_, diagnostics)) {
                markCommandFail(diagnostics);
                return nullptr;
            }
        }
        if (originalRecordingStates_) {
            VulkanRhiCommandBuffer finished(
                commandCount_, nativePayload_, *originalRecordingStates_);
            core::replaceCpuObjectByMove(*finishedCommand_, finished);
        } else {
            VulkanRhiCommandBuffer finished(commandCount_,
                nativePayload_,
                std::move(recordedStates_),
                std::move(stagingUploads_));
            core::replaceCpuObjectByMove(*finishedCommand_, finished);
        }
        return std::move(finishedCommand_);
    }

private:
    VulkanRhiColdFailure coldFailure() noexcept
    {
        return {this, [](void* owner, std::string_view id) noexcept {
                    static_cast<VulkanRhiCommandEncoder*>(owner)->failColdPass(id);
                }};
    }

    core::MutationJournal* statesForRecording() const noexcept
    {
        return finished_      ? nullptr
            : recordedStates_ ? recordedStates_.get()
                              : originalRecordingStates_;
    }

    bool preflightColdRecording() noexcept
    {
        if (coldPassFailed_)
            return false;
        if (coldStorage_ && nativePayload_) {
            if (const auto* id = nativePayload_->recordingStorageDiagnostic()) {
                failColdPass(id);
                return false;
            }
        }
        return true;
    }

    bool preflightDebugLabel() noexcept
    {
        if (!preflightColdRecording())
            return false;
        if (!coldStorage_)
            return true;
        failColdPass(VulkanRhiRecordingStorageLimits::unsupportedDebugLabelDiagnostic());
        return false;
    }

    bool preflightColdBarrier(rhi::CommandUsageKind kind, std::size_t references)
    {
        if (!preflightColdRecording())
            return false;
        if (!coldStorage_)
            return true;
        if (nativePayload_->hasOrdinaryAttachments())
            if (const auto* id =
                    nativePayload_->preflightOrdinaryAttachmentOperation(kind, commandCount_)) {
                failColdPass(id);
                return false;
            }
        const char* id = nativePayload_->recordingBarrierStorageDiagnostic(kind);
        if (!id)
            id = nativePayload_->resourceRetentionStorageDiagnostic(references);
        if (!id)
            return true;
        nativePayload_->rejectRecordingStorage(id);
        failColdPass(id);
        return false;
    }

    void failColdPass(std::string_view diagnostic) noexcept
    {
        if (coldPassFailed_)
            return;
        // Every caller supplies a static diagnostic ID. Existing pass wrappers
        // observe the same terminal native latch before touching any resource.
        if (nativePayload_)
            nativePayload_->rejectRecordingStorage(diagnostic.data());
        coldPassFailure_.assign(diagnostic.data(), diagnostic.size());
        lastValidation_.diagnostics = std::move(coldPassFailure_);
        lastValidation_.success = false;
        lastValidation_.status = rhi::CommandValidationStatus::DiagnosticFailure;
        lastValidation_.recordedCommandCount = commandCount_;
        lastValidation_.diagnosticId.clear();
        coldPassFailed_ = true;
    }

    void record()
    {
        std::pmr::string diagnostics;
        if (!recordVulkanRhiNativeCommand(nativePayload_, diagnostics)) {
            markCommandFail(diagnostics);
            return;
        }
        markCommandOk();
    }

    bool requireNativeCommandEncoding_ = false;
    bool bindlessExecutable_ = false;
    bool indirectFirstInstanceEnabled_ = false;
    std::uint64_t recordingIdentity_ = 0u;
    rhi::BindlessIndirectDeclarations indirectAccesses_;
    VulkanRhiNativeResourceAllocator nativeAllocator_;
    std::shared_ptr<VulkanRhiNativeCommandPayload> nativePayload_;
    std::shared_ptr<VulkanRhiResourceRegistry> resourceRegistry_;
    VulkanRhiStagingUploadPool* stagingUploadPool_ = nullptr;
    const rhi::semantics::NativeBackendCapabilityContract* capabilityContract_ = nullptr;
    std::uint64_t uploadRetirementSerial_ = 1u;
    core::UniquePtr<rhi::RecordedStagingUploadJournal> stagingUploads_;
    core::UniquePtr<VulkanRhiCommandBuffer> finishedCommand_;
    std::shared_ptr<const rhi::OriginalCommandStreamProgram> coldPassProgram_;
    bool coldStorage_ = false;
    core::MutationJournal* originalRecordingStates_ = nullptr;
    core::pmr::Vector<core::UniquePtr<VulkanRhiComputePassEncoder>> coldComputePasses_;
    core::pmr::Vector<core::UniquePtr<VulkanRhiTransferPassEncoder>> coldTransferPasses_;
    core::pmr::Vector<core::UniquePtr<rhi::RenderPassEncoder>> coldRenderPasses_;
    std::size_t nextColdComputePass_ = 0u, nextColdTransferPass_ = 0u, nextColdRenderPass_ = 0u;
    core::pmr::String coldPassFailure_;
    bool coldPassFailed_ = false;
    std::pmr::vector<NativeResourceBinding> nativeResources_;
};

std::shared_ptr<VulkanRhiNativeCommandPayload> vulkanRhiCommandPayload(rhi::CommandEncoder& encoder)
{
    auto* original = dynamic_cast<VulkanRhiCommandEncoder*>(&encoder);
    return original ? original->nativePayload() : nullptr;
}

VulkanRhiColdRenderPassObservation inspectVulkanRhiColdRenderPass(
    const rhi::CommandEncoder& encoder, std::size_t literalPassIndex) noexcept
{
    const auto* original = dynamic_cast<const VulkanRhiCommandEncoder*>(&encoder);
    return original ? original->coldRenderPassObservation(literalPassIndex)
                    : VulkanRhiColdRenderPassObservation{};
}

namespace {
core::UniquePtr<rhi::CommandEncoder> createColdPassEncoder(
    VulkanRhiNativeResourceAllocator nativeResources,
    std::shared_ptr<VulkanRhiResourceRegistry> resourceRegistry,
    VulkanRhiStagingUploadPool* stagingUploadPool,
    const rhi::semantics::NativeBackendCapabilityContract& capabilityContract,
    std::shared_ptr<const rhi::OriginalCommandStreamProgram> original,
    core::pmr::String& diagnostic,
    const rhi::GpuDevice* device,
    const rhi::OriginalRenderPassPreparationRequest* preparation)
{
    diagnostic.clear();
    if (const auto* id = rhi::OriginalCommandStreamProgram::validateOriginal(original)) {
        diagnostic = id;
        return {};
    }
    if (!original->recordingBudget()) {
        diagnostic = "RHI-VULKAN-COLD-PASS-RECORDING-BUDGET-REQUIRED";
        return {};
    }
    const char* projectionDiagnostic = nullptr;
    auto storage = VulkanRhiRecordingStorageLimits::project(
        *original->recordingBudget(), projectionDiagnostic);
    if (!storage) {
        diagnostic = projectionDiagnostic;
        return {};
    }
    storage->originalProgram = original;
    if (preparation) {
        if (!device || !nativeResources.runtime()
            || capabilityContract.backend != rhi::Backend::Vulkan) {
            diagnostic = "RHI-VULKAN-COLD-RENDER-PASS-DEVICE";
            return {};
        }
        const auto result = VulkanRhiRenderPassStorageProjection::inspectPreparation(
            original, *preparation, *device, *nativeResources.runtime());
        if (result.diagnostic) {
            diagnostic = result.diagnostic;
            return {};
        }
        if (rhi::OriginalRenderPassPreparationContract::hasPassTiming(*preparation)) {
            VulkanRhiRecordingStorageLimits::OriginalQueries queries;
            queries.inputs = original->originalInputs();
            queries.budget = *preparation->passTimingRetention;
            queries.recordingQuota = preparation->passTimingRecordingQuota;
            queries.recordingContractVersion = 2u;
            queries.recordingOptIn = true;
            storage->originalQueries = std::move(queries);
            if (const auto* id = storage->diagnostic()) {
                diagnostic = id;
                return {};
            }
        }
    } else if (original->counts().renderPasses) {
        diagnostic = "RHI-VULKAN-COLD-RENDER-PASS-STORAGE-UNAVAILABLE";
        return {};
    }
    if (nativeResources.runtime() && nativeResources.runtime()->syntheticRuntime()) {
        diagnostic = "RHI-VULKAN-COLD-PASS-SYNTHETIC-RUNTIME";
        return {};
    }
    try {
        core::UniquePtr<VulkanRhiCommandEncoder> encoder;
        {
            core::CpuMemoryQuotaScope scope(original->originalMetadataQuota());
            encoder = core::makeUnique<VulkanRhiCommandEncoder>(std::move(nativeResources),
                std::move(resourceRegistry),
                stagingUploadPool,
                capabilityContract,
                false,
                true,
                rhi::CommandEncoderDesc{},
                original,
                &*storage,
                nullptr,
                preparation);
            if (encoder->ready() && preparation
                && !encoder->prepareOrdinaryRenderPasses(*device, *preparation, diagnostic))
                return {};
        }
        if (!encoder->ready()) {
            diagnostic = encoder->lastValidation().diagnostics;
            return {};
        }
        return encoder;
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-VULKAN-COLD-PASS-STORAGE-ALLOCATION";
        return {};
    } catch (const std::length_error&) {
        diagnostic = "RHI-VULKAN-COLD-PASS-STORAGE-ALLOCATION";
        return {};
    }
}

} // namespace

core::UniquePtr<rhi::CommandEncoder> createVulkanRhiCommandEncoderWithColdPassStorage(
    VulkanRhiNativeResourceAllocator resources,
    std::shared_ptr<VulkanRhiResourceRegistry> registry,
    VulkanRhiStagingUploadPool* pool,
    const rhi::semantics::NativeBackendCapabilityContract& capabilities,
    std::shared_ptr<const rhi::OriginalCommandStreamProgram> original,
    core::pmr::String& diagnostic)
{
    return createColdPassEncoder(std::move(resources),
        std::move(registry),
        pool,
        capabilities,
        std::move(original),
        diagnostic,
        nullptr,
        nullptr);
}

core::UniquePtr<rhi::CommandEncoder> createVulkanRhiCommandEncoderWithColdRenderPasses(
    VulkanRhiNativeResourceAllocator resources,
    std::shared_ptr<VulkanRhiResourceRegistry> registry,
    VulkanRhiStagingUploadPool* pool,
    const rhi::semantics::NativeBackendCapabilityContract& capabilities,
    std::shared_ptr<const rhi::OriginalCommandStreamProgram> original,
    const rhi::GpuDevice& device,
    const rhi::OriginalRenderPassPreparationRequest& request,
    core::pmr::String& diagnostic)
{
    return createColdPassEncoder(std::move(resources),
        std::move(registry),
        pool,
        capabilities,
        std::move(original),
        diagnostic,
        &device,
        &request);
}

core::UniquePtr<rhi::CommandEncoder> createVulkanRhiCommandEncoderWithColdPassStorage(
    VulkanRhiNativeResourceAllocator resources,
    std::shared_ptr<VulkanRhiResourceRegistry> registry,
    VulkanRhiStagingUploadPool* staging,
    const rhi::semantics::NativeBackendCapabilityContract& capabilities,
    const rhi::GpuDevice& device,
    const VulkanRhiRecordingStorageLimits& original,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    if (!original.originalQueries) {
        diagnostic = "RHI-VULKAN-ORIGINAL-QUERY-INPUTS";
        return {};
    }
    const auto& query = *original.originalQueries;
    if (const auto* id = original.diagnostic()) {
        diagnostic = id;
        return {};
    }
    const auto lowering = rhi::semantics::QueryLowering::originalRecording(
        device.backend(), query.recordingContractVersion, query.recordingOptIn);
    if (!lowering.privateRecordingAvailable || lowering.diagnostic) {
        diagnostic = lowering.diagnostic;
        return {};
    }
    const auto& sources = query.inputs->originalScalars()->originalUsage()->originalSources();
    if (const auto* id =
            rhi::OriginalCommandResourceSources::validateRegisteredLifetime(sources, device)) {
        diagnostic = id;
        return {};
    }
    if (const auto* id = rhi::OriginalCommandStreamProgram::validateOriginalRecordingBudget(
            query.inputs, query.commandBudget)) {
        diagnostic = id;
        return {};
    }
    rhi::semantics::CommandWorkloadDesc workload;
    workload.label = "original-query-journal";
    workload.commandCount = 1u;
    workload.usesTimestampQuery = true;
    const auto native = rhi::semantics::lowerCommandWorkload(
        capabilities, workload, rhi::semantics::CommandRecordingIntent::PrimaryCommandBuffer);
    if (capabilities.backend != rhi::Backend::Vulkan || !native.success) {
        diagnostic = native.success ? "RHI-VULKAN-ORIGINAL-QUERY-CAPABILITY" : native.diagnostics;
        return {};
    }
    const char* id = nullptr;
    auto storage = VulkanRhiRecordingStorageLimits::project(query.commandBudget, id);
    if (!storage) {
        diagnostic = id;
        return {};
    }
    storage->originalQueries = query;
    for (const auto& command : query.inputs->originalScalars()->originalUsage()->commands()) {
        if (command.kind == rhi::CommandUsageKind::BeginRenderPass) {
            diagnostic = kColdRenderPassUnavailable;
            return {};
        }
    }
    try {
        core::UniquePtr<VulkanRhiCommandEncoder> encoder;
        {
            core::CpuMemoryQuotaScope scope(query.recordingQuota);
            encoder = core::makeUnique<VulkanRhiCommandEncoder>(std::move(resources),
                std::move(registry),
                staging,
                capabilities,
                false,
                true,
                rhi::CommandEncoderDesc{},
                nullptr,
                &*storage,
                &native);
        }
        if (!encoder->ready()) {
            diagnostic = encoder->lastValidation().diagnostics;
            return {};
        }
        return encoder;
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-VULKAN-COLD-PASS-STORAGE-ALLOCATION";
    } catch (const std::length_error&) {
        diagnostic = "RHI-VULKAN-COLD-PASS-STORAGE-ALLOCATION";
    }
    return {};
}

core::UniquePtr<rhi::CommandEncoder> createVulkanRhiCommandEncoderWithColdTextureUploads(
    VulkanRhiNativeResourceAllocator resources,
    std::shared_ptr<VulkanRhiResourceRegistry> registry,
    VulkanRhiStagingUploadPool* pool,
    const rhi::semantics::NativeBackendCapabilityContract& capabilities,
    std::shared_ptr<const rhi::OriginalCommandStreamProgram> program,
    rhi::GpuDevice& device,
    const rhi::OriginalOrdinaryTextureUploadContract::Budget& budget,
    const rhi::OriginalOrdinaryTextureUploadContract::Binding* bindings,
    std::size_t count,
    core::pmr::String& diagnostic,
    const rhi::OriginalTextureUploadPreparationRequest* publicRequest,
    const std::shared_ptr<core::CpuMemoryQuota>& publicationQuota,
    core::pmr::Vector<std::shared_ptr<const rhi::OriginalTextureUploadPublication>>* publications)
{
    using Contract = rhi::OriginalOrdinaryTextureUploadContract;
    diagnostic.clear();
    const auto decision = Contract::lowering(device.backend(), budget);
    if (decision.diagnostic) {
        diagnostic = decision.diagnostic;
        return {};
    }
    if (const auto* id = Contract::inspect(program, budget, bindings, count)) {
        diagnostic = id;
        return {};
    }
    if (bool(publicRequest) != bool(publications)
        || bool(publicRequest) != bool(publicationQuota)) {
        diagnostic = "RHI-TEXTURE-UPLOAD-PUBLICATION-REQUEST";
        return {};
    }
    if (publicRequest) {
        if (const auto* id = rhi::OriginalTextureUploadPreparationContract::inspect(
                program, *publicRequest, bindings, count, publicationQuota)) {
            diagnostic = id;
            return {};
        }
        if (!publications->empty() || publications->capacity() < count) {
            diagnostic = "RHI-TEXTURE-UPLOAD-PUBLICATION-STORAGE";
            return {};
        }
    }
    if (capabilities.backend != rhi::Backend::Vulkan) {
        diagnostic = "RHI-ORDINARY-TEXTURE-UPLOAD-CAPABILITY";
        return {};
    }
    if (const auto* id = rhi::OriginalCommandResourceSources::validateRegisteredLifetime(
            program->originalInputs()->originalScalars()->originalUsage()->originalSources(),
            device)) {
        diagnostic = id;
        return {};
    }
    if (!pool || !pool->coldStoragePrepared()) {
        diagnostic = "RHI-ORDINARY-TEXTURE-UPLOAD-COLD-POOL";
        return {};
    }
    auto result = createVulkanRhiCommandEncoderWithColdPassStorage(
        std::move(resources), std::move(registry), pool, capabilities, program, diagnostic);
    if (!result)
        return {};
    try {
        core::CpuMemoryQuotaScope scope(program->originalMetadataQuota());
        if (!static_cast<VulkanRhiCommandEncoder&>(*result).prepareOrdinaryTextureUploads(device,
                budget,
                bindings,
                count,
                diagnostic,
                publicRequest,
                publicationQuota,
                publications))
            return {};
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-ORDINARY-TEXTURE-UPLOAD-ALLOCATION";
        return {};
    } catch (const std::length_error&) {
        diagnostic = "RHI-ORDINARY-TEXTURE-UPLOAD-ALLOCATION";
        return {};
    }
    return result;
}

arp::core::UniquePtr<rhi::CommandEncoder> createVulkanRhiCommandEncoder(
    VulkanRhiNativeResourceAllocator nativeResources,
    std::shared_ptr<VulkanRhiResourceRegistry> resourceRegistry,
    VulkanRhiStagingUploadPool* stagingUploadPool,
    const rhi::semantics::NativeBackendCapabilityContract& capabilityContract,
    bool bindlessExecutable,
    bool requireNativeCommandEncoding,
    const rhi::CommandEncoderDesc& retention)
{
    if (!rhi::semantics::commandRetentionRequestAllowed(
            retention, rhi::CommandRetentionCapabilities{}, rhi::Backend::Vulkan))
        return {};
    auto encoder = arp::core::makeUnique<VulkanRhiCommandEncoder>(std::move(nativeResources),
        std::move(resourceRegistry),
        stagingUploadPool,
        capabilityContract,
        bindlessExecutable,
        requireNativeCommandEncoding,
        retention);
    if (!encoder->ready()) {
        return nullptr;
    }
    return encoder;
}

arp::core::UniquePtr<rhi::SecondaryCommandEncoder> createVulkanRhiSecondaryCommandEncoder(
    const rhi::SecondaryCommandEncoderDesc& desc)
{
    (void)desc;
    return nullptr;
}

arp::core::UniquePtr<rhi::RenderBundleEncoder> createVulkanRhiRenderBundleEncoder(
    const rhi::RenderBundleDesc& desc)
{
    (void)desc;
    return nullptr;
}

rhi::QueueSubmitResult submitVulkanRhiCommandBuffer(const rhi::CommandBuffer& commandBuffer)
{
    return submitVulkanRhiCommandBuffer(commandBuffer, {});
}

rhi::QueueSubmitResult submitVulkanRhiCommandBuffer(
    const rhi::CommandBuffer& commandBuffer, const VulkanRhiTimelineSubmitInfo& timeline)
{
    const auto* vulkanCommandBuffer = dynamic_cast<const VulkanRhiCommandBuffer*>(&commandBuffer);
    if (!vulkanCommandBuffer || !vulkanCommandBuffer->nativePayload()) {
        return {false, "RHI-VULKAN-NATIVE-COMMAND-PAYLOAD-MISSING", 0};
    }
    std::pmr::string diagnostics;
    bool accepted = false;
    const auto success =
        vulkanCommandBuffer->nativePayload()->submit(timeline, diagnostics, &accepted);
    if (accepted)
        vulkanCommandBuffer->acceptRecordedOwnership();
    return {
        success, std::move(diagnostics), accepted ? commandBuffer.commandCount() : 0u, accepted};
}

std::shared_ptr<VulkanRhiNativeCommandPayload> vulkanRhiCommandPayload(
    const rhi::CommandBuffer& commandBuffer)
{
    const auto* vulkanCommandBuffer = dynamic_cast<const VulkanRhiCommandBuffer*>(&commandBuffer);
    return vulkanCommandBuffer ? vulkanCommandBuffer->nativePayload() : nullptr;
}

} // namespace arp::rhi_vulkan

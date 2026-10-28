#include "core/memory/cpu_allocator.h"
#include "null_backend_internal.h"
#include "null_bindless.h"
#include "rhi/backends/null/dummy_sync.h"
#include "rhi/frontend/bindless_indirect_packet.h"
#include "rhi/frontend/command_recorded_state.h"
#include "rhi/frontend/device_validation.h"
#include "rhi/frontend/heap_placement_contract.h"
#include "rhi/gpu_bindless.h"
#include "rhi/gpu_descriptors.h"
#include "rhi/gpu_extensions.h"
#include "rhi/gpu_pipeline.h"
#include "rhi/semantics/bindless_original_execution_lowering.h"
#include "rhi/semantics/fallback_plan.h"
#include "rhi/semantics/native_backend_capabilities.h"
#include "rhi/semantics/native_engine_fallback_bridge.h"
#include "rhi/semantics/native_pipeline_state_lowering.h"

namespace arp::rhi {

namespace {

class NullQueue final : public GpuQueue {

public:
    explicit NullQueue(
        GpuQueueRole role = GpuQueueRole::Graphics, const BindlessCrossExtension* issuer = nullptr)
        : role_(role), issuer_(issuer)
    {}

    GpuQueueRole role() const override { return role_; }

    QueueSubmitResult submit(const CommandBuffer& commandBuffer) override
    {
        return submit(commandBuffer, nullptr);
    }

    QueueSubmitResult submit(const CommandBuffer& commandBuffer, GpuFence* signalFence) override
    {
        const auto bindless = null_backend::validateBindlessSubmission(commandBuffer, issuer_);
        if (!bindless.success)
            return {false, bindless.diagnostics, 0u};
        if (!acceptRecordedCommandOwnership(commandBuffer))
            return {false, "RHI-NULL-COMMAND-STATE-OWNER-MISSING", 0u};
        ++submitCount_;
        if (signalFence) {
            signalFence->signal();
        }
        return {true, "", commandBuffer.commandCount()};
    }

    QueueSubmitResult submit(const CommandBuffer& commandBuffer,
        GpuTimelineSemaphore* waitSemaphore,
        uint64_t waitValue,
        GpuTimelineSemaphore* signalSemaphore,
        uint64_t signalValue,
        GpuFence* signalFence) override
    {
        if (waitSemaphore)
            waitSemaphore->hostWait(waitValue, UINT64_MAX);
        auto result = submit(commandBuffer);
        if (result.success) {
            if (signalSemaphore)
                signalSemaphore->signal(signalValue);
            if (signalFence)
                signalFence->signal();
        }
        return result;
    }

    std::uint32_t submitCount() const override { return submitCount_; }

private:
    GpuQueueRole role_ = GpuQueueRole::Graphics;
    const BindlessCrossExtension* issuer_ = nullptr;
    std::uint32_t submitCount_ = 0;
};

class NullFence final : public GpuFence {
public:
    void signal() override { signaled_ = true; }

    bool isSignaled() const override { return signaled_; }

    void wait() override { /* No-op: Null backend has no GPU work to wait for. */ }

    void reset() override { signaled_ = false; }

private:
    bool signaled_ = false;
};

class NullMemoryHeap final : public GpuMemoryHeap {
public:
    NullMemoryHeap(
        std::uint64_t sizeBytes, std::uint64_t alignmentBytes, null_backend::MemoryLedgerPtr ledger)
        : sizeBytes_(sizeBytes), alignmentBytes_(alignmentBytes),
          lease_(core::makeShared<null_backend::MemoryLedgerLease>(std::move(ledger), sizeBytes))
    {
        bindOriginalAllocationLifetime(*lease_);
    }

    GpuResourceId id() const override { return lease_->id(); }

    std::uint64_t sizeBytes() const override { return sizeBytes_; }

    std::uint64_t alignmentBytes() const override { return alignmentBytes_; }

    /// Validator resources share this original accounting lease. There is
    /// no native allocation, aliasing or GPU completion proof.
    bool supportsAliasing() const override { return false; }

    std::shared_ptr<const null_backend::MemoryLedgerLease> lease() const { return lease_; }

private:
    std::uint64_t sizeBytes_;
    std::uint64_t alignmentBytes_;
    std::shared_ptr<null_backend::MemoryLedgerLease> lease_;
};

class NullSemaphore final : public GpuSemaphore {
public:
    void signal() override { signaled_ = true; }

    void wait() override { /* No-op: Null backend has no GPU work to order. */ }

    void reset() override { signaled_ = false; }

private:
    bool signaled_ = false;
};

// BindlessCross/1 validator ceilings, not native GPU limits. All snapshots
// still require explicit opt-in, concrete package mappings and bounded budgets.
class NullBindlessExtension final : public BindlessCrossExtension {
public:
    explicit NullBindlessExtension(null_backend::ResourceRegistryPtr registry)
        : registry_(std::move(registry))
    {
        caps_.deviceIdentity = allocateGpuResourceId().value;
        caps_.execution = BindlessExecution::Validator;
        caps_.indirectInstanceConsumerContractVersion = 1u;
        caps_.indirectPacketStorageContractVersion = 1u;
        caps_.preparedComputeBindingContractVersion = 1u;
        caps_.preparedGraphicsBindingContractVersion = 1u;
        caps_.preparedGraphicsPassFaultScopeContractVersion = 1u;
        caps_.limitsQueried = true;
        caps_.maxBindGroups = 4;
        caps_.maxBindingsPerGroup = 4096;
        caps_.maxUniformBufferBindingSize = 64u * 1024u;
        caps_.maxStorageBufferBindingSize = 128u * 1024u * 1024u;
        // Validator profile, not a physical-device query or native capability.
        caps_.uniformBufferOffsetAlignment = 16u;
        caps_.storageBufferOffsetAlignment = 16u;
        for (std::size_t stage = 0; stage < kBindlessShaderStages; ++stage) {
            caps_.stageLimits[stage] = {4096, 4096, 4096, 4096, 4096};
            const auto read = BindlessOperation::Load | BindlessOperation::Query;
            caps_.support[stage][0] = {read | BindlessOperation::SampleLevel
                    | BindlessOperation::SampleCompare
                    | (stage == 1 ? BindlessOperation::SampleGrad : BindlessOperation::None),
                true};
            caps_.support[stage][1] = {BindlessOperation::None, true};
            caps_.support[stage][2] = {read | BindlessOperation::Store, true};
            caps_.support[stage][3] = {BindlessOperation::Load, true};
            caps_.support[stage][4] = {read, true};
            caps_.support[stage][5] = {
                read | BindlessOperation::Store | BindlessOperation::Atomic, true};
        }
    }

    const BindlessCapabilities& capabilities() const noexcept override { return caps_; }

    core::UniquePtr<ComputePipeline> createComputePipeline(
        std::shared_ptr<const BindlessAuthorization> authorization,
        core::StringView entry,
        core::pmr::String& diagnostic) const override
    {
        return null_backend::createBindlessComputePipeline(
            *this, std::move(authorization), entry, diagnostic);
    }

    core::UniquePtr<RenderPipeline> createRenderPipeline(
        std::shared_ptr<const BindlessAuthorization> authorization,
        const RenderPipelineDesc& state,
        core::StringView vertex,
        core::StringView fragment,
        core::pmr::String& diagnostic) const override
    {
        return null_backend::createBindlessRenderPipeline(
            *this, std::move(authorization), state, vertex, fragment, diagnostic);
    }

    core::UniquePtr<RenderPipeline> createRenderPipeline(
        std::shared_ptr<const BindlessAuthorization> authorization,
        const RenderPipelineDesc& state,
        core::StringView vertex,
        core::StringView fragment,
        const BindlessGraphicsPushRequest& request,
        core::pmr::String& diagnostic) const override
    {
        return null_backend::createBindlessRenderPipeline(
            *this, std::move(authorization), state, vertex, fragment, diagnostic, &request);
    }

private:
    std::shared_ptr<const BindlessPreparedComputeBinding> prepareComputeBinding(
        std::shared_ptr<const BindlessComputeBindingOwners> owners,
        const BindlessComputeBindingBudget&,
        core::pmr::String& diagnostic) const override
    {
        return null_backend::prepareComputeBinding(std::move(owners), diagnostic);
    }

    std::shared_ptr<const BindlessPreparedGraphicsBinding> prepareGraphicsBinding(
        std::shared_ptr<const BindlessGraphicsBindingOwners> owners,
        const BindlessGraphicsBindingBudget&,
        core::pmr::String& diagnostic) const override
    {
        return null_backend::prepareGraphicsBinding(std::move(owners), diagnostic);
    }

    ValidationResult validateIndirectProcessorSource(const GpuBuffer& source) const override
    {
        return registry_->ownsResource(source.id(), null_backend::RegisteredResourceKind::Buffer)
            ? ValidationResult{true, {}}
            : ValidationResult{false, "RHI-BINDLESS-INDIRECT-PROCESSOR-SOURCE-DEVICE-MISMATCH"};
    }

    ValidationResult validateIndirectPacketPayload(
        const BindlessIndirectPacket& packet) const override
    {
        const auto plan = validateIndirectPacketCreation(packet.consumerPlan());
        return plan.success ? packet.validate() : plan;
    }

    ValidationResult validateIndirectPacketCreation(const BindlessLoweringPlan& plan) const override
    {
        return plan.execution == BindlessExecution::Validator && !plan.executable
                && plan.deviceIdentity == caps_.deviceIdentity
            ? ValidationResult{true, {}}
            : ValidationResult{false, "RHI-BINDLESS-INDIRECT-PACKET-STORAGE-UNAVAILABLE"};
    }

    ValidationResult validateAuthorization(
        const BindlessShaderPackage&, const BindlessSnapshot& snapshot) const override
    {
        const auto& plan = snapshot.plan();
        return caps_.execution == BindlessExecution::Validator && !caps_.executablePayload
                && plan.execution == BindlessExecution::Validator && !plan.executable
                && plan.deviceIdentity == caps_.deviceIdentity
            ? ValidationResult{true, {}}
            : ValidationResult{false, "RHI-BINDLESS-AUTHORIZATION-EXECUTION-UNAVAILABLE"};
    }

    ValidationResult validateSnapshotCreation(const BindlessLoweringPlan& plan) const override
    {
        if (plan.originalExecutionContractVersion)
            return {false,
                semantics::OriginalBindlessExecutionLowering::lowering(
                    Backend::Null, plan.originalExecutionContractVersion)
                    .diagnostic};
        return plan.execution == BindlessExecution::Validator && !plan.executable
                && plan.deviceIdentity == caps_.deviceIdentity
            ? ValidationResult{true, {}}
            : ValidationResult{false, "RHI-BINDLESS-SNAPSHOT-EXECUTION-UNAVAILABLE"};
    }

    BindlessCapabilities caps_;
    null_backend::ResourceRegistryPtr registry_;
};

class NullDevice final : public GpuDevice {
public:
    Backend backend() const override { return Backend::Null; }

    void* queryInterface(std::string_view extensionId) noexcept override
    {
        return extensionId == kExtensionBindlessCross
            ? static_cast<BindlessCrossExtension*>(&bindless_)
            : nullptr;
    }

    semantics::ShutdownDrainReport drainForShutdown(
        const semantics::ShutdownDrainRequest& request) override
    {
        const auto started = std::chrono::steady_clock::now();
        if (!request.validAt(started)) {
            return semantics::rejectedShutdownDrainReport(backend(), started, request, started);
        }
        semantics::ShutdownDrainReport report;
        report.backend = backend();
        report.outcome = semantics::ShutdownDrainOutcome::Equivalent;
        report.state = semantics::ShutdownDrainState::Completed;
        const auto finished = std::chrono::steady_clock::now();
        semantics::finalizeShutdownDrainTiming(report, started, request, finished);
        return report;
    }

    DeviceCapabilities capabilities() const override
    {
        auto caps =
            makeSharedDeviceCapabilities(Backend::Null, "RHI Null Device", std::pmr::string{});
        // NullValidatorProfile/2: explicit validator facts, never native proof.
        caps.core = {true, true};
        caps.explicitFeatures.explicitBarriers = true;
        caps.swapchain = rhi::SwapchainCapabilities{};
        caps.memory.deviceLocal = true;
        caps.memory.hostVisible = true;
        caps.memory.hostVisibleDeviceLocalBuffers =
            false; // Validator cannot prove native placement.
        caps.explicitFeatures.pushConstants = true;
        caps.explicitFeatures.secondaryCommandBuffers = true;
        caps.memory.transientAliasing = false;
        caps.features.pushConstants = caps.explicitFeatures.pushConstants;
        caps.features.secondaryCommandBuffers = caps.explicitFeatures.secondaryCommandBuffers;
        caps.features.descriptorTable = true;
        caps.features.transientAliasing = caps.memory.transientAliasing;
        caps.features.timelineSync = true;
        caps.features.storageTextureBinding = true;
        caps.features.r32FloatSampledUpload = true; // R32SampledUpload/1 validator only.
        caps.features.nativeSync = true;
        caps.features.timestampQueries = false;
        caps.features.subpassInputAttachment = true;
        // Versioned Null validation profile. These are validator ceilings,
        // not claims about a native GPU, hence Equivalent rather than Native.
        caps.computeResources.mode = CapabilityMode::Equivalent;
        caps.computeResources.sampledDepthTexture = true;
        caps.computeResources.maxComputeWorkgroupInvocations = 256u;
        caps.computeResources.maxComputeWorkgroupSizeX = 256u;
        caps.computeResources.maxComputeWorkgroupSizeY = 256u;
        caps.computeResources.maxComputeWorkgroupMemoryBytes = 32768u;
        caps.computeResources.maxStorageBufferBindingsPerStage = 8u;
        caps.computeResources.maxStorageBufferBindingSizeBytes = 128u * 1024u * 1024u;
        caps.computeResources.maxComputeWorkgroupSizeZ = 64u;
        caps.computeResources.maxComputeWorkgroupCountX = 65535u;
        caps.computeResources.maxComputeWorkgroupCountY = 65535u;
        caps.computeResources.maxComputeWorkgroupCountZ = 65535u;
        caps.computeResources.maxUniformBufferBindingsPerStage = 12u;
        caps.computeResources.maxUniformBufferBindingSizeBytes = 64u * 1024u;
        caps.computeResources.maxSampledTextureBindingsPerStage = 16u;
        caps.computeResources.maxTexture2DDimension = 8192u;
        {
            auto contract = rhi::semantics::nativeBackendCapabilityContract(rhi::Backend::Null);
            rhi::EngineFeatureRequirementSet requirements;
            requirements.logicalFrameSlots = 2;
            requirements.features = {
                {rhi::EngineFeature::PushConstants, {}},
                {rhi::EngineFeature::SecondaryCommandBuffer, {}},
                {rhi::EngineFeature::DescriptorTable, {}},
                {rhi::EngineFeature::TransientAliasing, {}},
                {rhi::EngineFeature::TimelineSync, {}},
                {rhi::EngineFeature::ComputeDispatch, {}},
                {rhi::EngineFeature::StorageTextureBinding, {}},
                {rhi::EngineFeature::NativeSync, {}},
                {rhi::EngineFeature::SubpassDepthInputAttachment, {}},
                {rhi::EngineFeature::Rg16FloatStorageTexture, {}},
                {rhi::EngineFeature::Rgba16FloatStorageTexture, {}},
                {rhi::EngineFeature::StorageBufferBinding, {}},
                {rhi::EngineFeature::SceneGeometryRayQuery, {}},
                {rhi::EngineFeature::IndirectDraw, {}},
                {rhi::EngineFeature::MultiDrawIndirectCount, {}},
            };
            auto plan = rhi::semantics::buildNativeEngineFallbackPlan(contract, requirements);
            plan.applyTo(caps);

            // Track A P2: push-constant lowering plan.
            {
                auto pcPlan =
                    rhi::semantics::buildPushConstantLoweringPlan(rhi::Backend::Null, contract);
                rhi::semantics::applyPushConstantLoweringPlan(caps, pcPlan);
            }
        }
        // Null validates alias contracts but never claims shared native memory.
        caps.memory.transientAliasing = false;
        caps.features.transientAliasing = false;
        rhi::synchronizeExecutionStatuses(caps);
        rhi::synchronizeQueryExecutionStatuses(caps);
        return caps;
    }

    MemoryStats memoryStats() const override { return memoryLedger_->stats(); }

    semantics::PhysicalBudgetLedgerReport physicalBudgetLedger() const override
    {
        semantics::PhysicalBudgetLedgerReport report;
        report.backend = backend();
        if (descriptorCache_) {
            const auto descriptors = descriptorCache_->budgetStats();
            if (descriptors.configured) {
                report.category(semantics::PhysicalBudgetCategory::Descriptor) =
                    semantics::observedPhysicalBudgetObservation(
                        semantics::PhysicalBudgetUnit::Descriptors,
                        descriptors.capacity,
                        descriptors.current,
                        descriptors.peak,
                        descriptors.rejectionCount,
                        descriptorCache_->entryCount());
            }
        }
        if (pipelineCache_) {
            const auto pipelines = pipelineCache_->budgetStats();
            if (pipelines.configured) {
                report.category(semantics::PhysicalBudgetCategory::PipelineCache) =
                    semantics::observedPhysicalBudgetObservation(
                        semantics::PhysicalBudgetUnit::PipelineEntries,
                        pipelines.capacity,
                        pipelines.current,
                        pipelines.peak,
                        pipelines.rejectionCount,
                        pipelineCache_->entryCount());
            }
        }
        return report;
    }

    GpuTimingSample latestGpuTiming() const override
    {
        GpuTimingSample sample;
        sample.availability = GpuTimingAvailability::Unsupported;
        sample.diagnostic = "Null backend has no GPU execution timeline";
        return sample;
    }

    semantics::UploadStagingReadiness uploadStagingReadiness() const override
    {
        // Null submits complete synchronously as an explicit validator
        // equivalent. The value is a completion epoch, not a queue handle.
        return {
            semantics::UploadStagingReadinessStatus::Equivalent,
            queue_.submitCount(),
            nullptr,
        };
    }

    GpuQueue& queue() override { return queue_; }

    void waitIdle() override {}

    /// Async compute queue. The Null backend exposes a real compute queue so
    /// the multi-queue async-compute path (timeline semaphore submit) can be
    /// exercised in tests without a native deadlock-capable backend. The queue
    /// is emulated (DummyTimelineSemaphore hostWait returns immediately), so
    /// semaphore parameter bugs do not hang — but the submit wait/signal
    /// values are observable via FrameGraphRhiCommandExecutionReport.
    GpuQueue* computeQueue() override { return &computeQueue_; }

    arp::core::UniquePtr<GpuBuffer> createBuffer(const BufferDesc& desc) override
    {
        return completeOriginalResourceCreation(
            null_backend::createBuffer(desc, memoryLedger_, resourceRegistry_));
    }

    CommandValidationResult writeBuffer(GpuBuffer& buffer,
        const void* data,
        std::uint64_t sizeBytes,
        std::uint64_t offsetBytes = 0) override
    {
        if (buffer.hostWritesSealed())
            return {false, "RHI-BUFFER-HOST-WRITES-SEALED", 0};
        if (!data || sizeBytes == 0u || offsetBytes + sizeBytes > buffer.desc().sizeBytes) {
            return {false, "Null RHI buffer write desc is invalid", 0};
        }
        return {true, "", 1};
    }

    arp::core::UniquePtr<GpuBufferView> createBufferView(
        const GpuBuffer& buffer, const BufferViewDesc& desc) override
    {
        return null_backend::createBufferView(buffer, desc, memoryLedger_, resourceRegistry_);
    }

    arp::core::UniquePtr<GpuTexture> createTexture(const TextureDesc& desc) override
    {
        return completeOriginalResourceCreation(
            null_backend::createTexture(desc, memoryLedger_, resourceRegistry_));
    }

    ResourceBarrierResult prepareTextureForPoolReuse(GpuTexture& texture) override
    {
        auto* stateful = dynamic_cast<null_backend::StatefulTextureResource*>(&texture);
        if (!stateful) {
            ResourceBarrierResult result;
            result.resource = texture.id();
            result.before = texture.state();
            result.after = ResourceState::Undefined;
            result.diagnostics = "RHI-POOL-REUSE-TEXTURE-BACKEND-MISMATCH";
            return result;
        }
        if (texture.state() == ResourceState::Undefined) {
            return {
                true, "", texture.id(), ResourceState::Undefined, ResourceState::Undefined, true};
        }
        return stateful->transitionState(texture.state(), ResourceState::Undefined);
    }

    ResourceBarrierResult prepareBufferForPoolReuse(GpuBuffer& buffer) override
    {
        auto* stateful = dynamic_cast<null_backend::StatefulResource*>(&buffer);
        if (!stateful) {
            ResourceBarrierResult result;
            result.resource = buffer.id();
            result.before = buffer.state();
            result.after = ResourceState::Undefined;
            result.diagnostics = "RHI-POOL-REUSE-BUFFER-BACKEND-MISMATCH";
            return result;
        }
        if (buffer.state() == ResourceState::Undefined) {
            return {true, "", buffer.id(), ResourceState::Undefined, ResourceState::Undefined};
        }
        return stateful->transitionState(buffer.state(), ResourceState::Undefined);
    }

    arp::core::UniquePtr<GpuMemoryHeap> createMemoryHeap(
        std::uint64_t sizeBytes, std::uint64_t alignmentBytes, MemoryUsage usage) override
    {
        lastHeapDiagnostic_ = HeapPlacementContract::heap(sizeBytes, alignmentBytes, usage);
        if (lastHeapDiagnostic_) {
            return nullptr;
        }
        // Account for the heap on the memory ledger.
        if (!memoryLedger_->canAllocate(sizeBytes)) {
            lastHeapDiagnostic_ = "RHI-NULL-HEAP-BUDGET-REJECTED";
            return nullptr;
        }
        auto heap = createOriginalMemoryHeap([&]() -> core::UniquePtr<GpuMemoryHeap> {
            try {
                return arp::core::makeUnique<NullMemoryHeap>(
                    sizeBytes, alignmentBytes, memoryLedger_);
            } catch (const std::bad_alloc&) {
                lastHeapDiagnostic_ = "RHI-NULL-HEAP-HOST-ALLOCATION-FAILED";
                return nullptr;
            }
        });
        if (!heap && !lastHeapDiagnostic_)
            lastHeapDiagnostic_ = lastOriginalResourceRegistrationDiagnostic().data();
        return heap;
    }

    std::string_view lastMemoryHeapCreationDiagnostic() const override
    {
        return lastHeapDiagnostic_ ? lastHeapDiagnostic_ : "";
    }

    TexturePlacementRequirements queryTexturePlacementRequirements(const TextureDesc& desc) override
    {
        return HeapPlacementContract::validatorRequirements(desc);
    }

    // Validator-only placement: check original ledger/range and retain its
    // accounting lease. No native aliasing or completion is implied.
    arp::core::UniquePtr<GpuTexture> createPlacedTexture(
        const TextureDesc& desc, const GpuMemoryHeap& heap, std::uint64_t offsetBytes) override
    {
        if ((lastHeapDiagnostic_ = originalMemoryHeapPlacementDiagnostic(heap)))
            return nullptr;
        const auto* validatorHeap = dynamic_cast<const NullMemoryHeap*>(&heap);
        if (!validatorHeap || !validatorHeap->lease()->belongsTo(memoryLedger_))
            return nullptr;
        const auto requirements = HeapPlacementContract::validatorRequirements(desc);
        if (HeapPlacementContract::placement(
                heap.sizeBytes(), heap.alignmentBytes(), offsetBytes, requirements))
            return nullptr;
        try {
            return completeOriginalResourceCreation(null_backend::createTexture(
                desc, memoryLedger_, resourceRegistry_, validatorHeap->lease()));
        } catch (const std::bad_alloc&) {
            return nullptr;
        }
    }

    arp::core::UniquePtr<GpuTextureView> createTextureView(
        const GpuTexture& texture, const TextureViewDesc& desc) override
    {
        return completeOriginalResourceCreation(
            null_backend::createTextureView(texture, desc, memoryLedger_, resourceRegistry_),
            &texture);
    }

    arp::core::UniquePtr<GpuSampler> createSampler(const SamplerDesc& desc) override
    {
        return completeOriginalResourceCreation(
            null_backend::createSampler(desc, memoryLedger_, resourceRegistry_));
    }

    arp::core::UniquePtr<PipelineLayout> createPipelineLayout(
        const PipelineLayoutDesc& desc) override
    {
        return null_backend::createPipelineLayout(desc);
    }

    PipelineValidationReport validateRenderPipeline(const RenderPipelineDesc& desc) override
    {
        return null_backend::validateRenderPipeline(desc);
    }

    PipelineValidationReport validateComputePipeline(const ComputePipelineDesc& desc) override
    {
        return null_backend::validateComputePipeline(desc);
    }

    arp::core::UniquePtr<RenderPipeline> createRenderPipeline(
        const RenderPipelineDesc& desc) override
    {
        // Capability lowering gate: Null is the reference validator, but it
        // still runs the same L2 lowering decision as the real backends so
        // parity tests and production code share one fail-closed path. The
        // contract is rebuilt on demand (Null has no per-device capability
        // state), matching the pattern used in capabilities().
        const auto contract = rhi::semantics::nativeBackendCapabilityContract(rhi::Backend::Null);
        const auto decision = rhi::semantics::lowerRenderPipelineState(contract, desc);
        if (!decision.success) {
            rhi::RhiErrorHandler::get().report(
                rhi::RhiSeverity::Error, decision.diagnostics.c_str());
            return nullptr;
        }
        return null_backend::createRenderPipeline(desc);
    }

    arp::core::UniquePtr<ComputePipeline> createComputePipeline(
        const ComputePipelineDesc& desc) override
    {
        // Capability lowering gate: Null is the reference validator, but it
        // still runs the same L2 compute lowering decision as the real
        // backends so parity tests and production code share one fail-closed
        // path. The contract is rebuilt on demand (Null has no per-device
        // capability state), matching the pattern used in capabilities().
        const auto contract = rhi::semantics::nativeBackendCapabilityContract(rhi::Backend::Null);
        const auto decision = rhi::semantics::lowerComputePipelineState(contract, desc);
        if (!decision.success) {
            rhi::RhiErrorHandler::get().report(
                rhi::RhiSeverity::Error, decision.diagnostics.c_str());
            return nullptr;
        }
        return null_backend::createComputePipeline(desc);
    }

    arp::core::UniquePtr<DescriptorArena> createDescriptorArena(
        const DescriptorArenaDesc& desc) override
    {
        return null_backend::createDescriptorArena(desc, resourceRegistry_);
    }

    arp::core::UniquePtr<CommandEncoder> createCommandEncoder(
        const CommandEncoderDesc& desc) override
    {
        return null_backend::createCommandEncoder(desc, &bindless_, resourceRegistry_);
    }

    arp::core::UniquePtr<SecondaryCommandEncoder> createSecondaryCommandEncoder(
        const SecondaryCommandEncoderDesc& desc) override
    {
        return null_backend::createSecondaryCommandEncoder(desc);
    }

    arp::core::UniquePtr<RenderBundleEncoder> createRenderBundleEncoder(
        const RenderBundleDesc& desc) override
    {
        return null_backend::createRenderBundleEncoder(desc);
    }

    arp::core::UniquePtr<Swapchain> createSwapchain(const SwapchainDesc& desc) override
    {
        return null_backend::createSwapchain(desc);
    }

    arp::core::UniquePtr<GpuFence> createFence() override
    {
        return arp::core::makeUnique<NullFence>();
    }

    arp::core::UniquePtr<GpuSemaphore> createSemaphore() override
    {
        return arp::core::makeUnique<NullSemaphore>();
    }

    arp::core::UniquePtr<GpuTimelineSemaphore> createTimelineSemaphore(
        uint64_t initialValue = 0) override
    {
        return arp::core::makeUnique<DummyTimelineSemaphore>(initialValue);
    }

    arp::core::UniquePtr<rhi::QueryPool> createQueryPool(const rhi::QueryPoolDesc&) override
    {
        return nullptr;
    }

    PipelineCache& pipelineCache() override
    {
        if (!pipelineCache_) {
            pipelineCache_ = arp::core::makeUnique<PipelineCache>(*this);
        }
        return *pipelineCache_;
    }

    DescriptorCache& descriptorCache() override
    {
        if (!descriptorCache_) {
            descriptorCache_ = arp::core::makeUnique<DescriptorCache>(*this);
        }
        return *descriptorCache_;
    }

private:
    const char* lastHeapDiagnostic_ = nullptr;
    null_backend::MemoryLedgerPtr memoryLedger_ =
        arp::core::makeShared<null_backend::MemoryLedger>();
    null_backend::ResourceRegistryPtr resourceRegistry_ =
        arp::core::makeShared<null_backend::ResourceRegistry>();
    NullBindlessExtension bindless_{resourceRegistry_};
    NullQueue queue_{GpuQueueRole::Graphics, &bindless_};
    NullQueue computeQueue_{GpuQueueRole::Compute, &bindless_};
    arp::core::UniquePtr<PipelineCache> pipelineCache_;
    arp::core::UniquePtr<DescriptorCache> descriptorCache_;
};

} // namespace

arp::core::UniquePtr<GpuDevice> createNullDevice()
{
    return arp::core::makeUnique<NullDevice>();
}

} // namespace arp::rhi

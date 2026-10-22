#include "rhi/backends/metal/metal_device.h"

#include "core/memory/cpu_allocator.h"
#include "rhi/backends/metal/metal_allocator.h"
#include "rhi/backends/metal/metal_backend_objects.h"
#include "rhi/backends/metal/metal_bindless_validation.h"
#include "rhi/backends/metal/metal_native_features.h"
#include "rhi/backends/metal/metal_native_queries.h"
#include "rhi/backends/metal/metal_native_runtime.h"
#include "rhi/backends/metal/metal_native_secondary.h"
#include "rhi/backends/metal/metal_native_sync.h"
#include "rhi/backends/metal/metal_original_bindless_command.h"
#include "rhi/backends/metal/metal_original_device_native_loss.h"
#include "rhi/backends/metal/metal_original_geometry_content.h"
#include "rhi/backends/metal/metal_staging_upload_pool.h"
#include "rhi/backends/metal/metal_texture_readback.h"
#include "rhi/backends/null/dummy_sync.h"
#include "rhi/frontend/bindless_shader_package.h"
#include "rhi/frontend/bindless_snapshot.h"
#include "rhi/frontend/command_recorded_state.h"
#include "rhi/frontend/device_validation.h"
#include "rhi/frontend/heap_placement_contract.h"
#include "rhi/gpu_bindless.h"
#include "rhi/gpu_descriptors.h"
#include "rhi/gpu_extensions.h"
#include "rhi/gpu_pipeline.h"
#include "rhi/semantics/bindless_original_execution_lowering.h"
#include "rhi/semantics/bindless_original_preparation_lowering.h"
#include "rhi/semantics/fallback_plan.h"
#include "rhi/semantics/native_backend_capabilities.h"
#include "rhi/semantics/native_command_lowering.h"
#include "rhi/semantics/native_engine_fallback_bridge.h"
#include "rhi/semantics/native_pipeline_state_lowering.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace arp::rhi_metal {

namespace {

bool metalHasTextureUsage(rhi::TextureUsage usage, rhi::TextureUsage required)
{
    return (usage & required) == required;
}

bool metalIsPowerOfTwo(std::uint64_t value)
{
    return value != 0u && (value & (value - 1u)) == 0u;
}

bool metalCapabilityContractMatchesProbe(
    const rhi::semantics::NativeBackendCapabilityContract& contract,
    const MetalRhiNativeFeatureProbe& probe,
    std::pmr::string& diagnostics)
{
    if (contract.backend != rhi::Backend::Metal) {
        diagnostics = "RHI-METAL-CAPABILITY-CONTRACT-BACKEND-MISMATCH";
        return false;
    }
    if (!contract.runtimeFeaturesQueried && !contract.runtimeFeatures.empty()) {
        diagnostics = "RHI-METAL-CAPABILITY-RUNTIME-QUERY-FLAG-MISSING";
        return false;
    }
    for (std::size_t index = 0; index < contract.features.size(); ++index) {
        for (std::size_t other = index + 1; other < contract.features.size(); ++other) {
            if (contract.features[index] == contract.features[other]) {
                diagnostics = "RHI-METAL-CAPABILITY-CONTRACT-DUPLICATE-FEATURE";
                return false;
            }
        }
    }
    for (const auto runtimeFeature : contract.runtimeFeatures) {
        bool declared = false;
        for (const auto declaredFeature : contract.features) {
            if (runtimeFeature == declaredFeature) {
                declared = true;
                break;
            }
        }
        if (!declared) {
            for (const auto optionalFeature : contract.optionalFeatures) {
                if (runtimeFeature == optionalFeature) {
                    declared = true;
                    break;
                }
            }
        }
        if (!declared) {
            diagnostics = "RHI-METAL-CAPABILITY-RUNTIME-FEATURE-NOT-DECLARED";
            return false;
        }
    }
    for (std::size_t index = 0; index < contract.runtimeFeatures.size(); ++index) {
        for (std::size_t other = index + 1; other < contract.runtimeFeatures.size(); ++other) {
            if (contract.runtimeFeatures[index] == contract.runtimeFeatures[other]) {
                diagnostics = "RHI-METAL-CAPABILITY-RUNTIME-DUPLICATE-FEATURE";
                return false;
            }
        }
    }
    for (std::size_t index = 0; index < contract.optionalFeatures.size(); ++index) {
        for (std::size_t other = index + 1; other < contract.optionalFeatures.size(); ++other) {
            if (contract.optionalFeatures[index] == contract.optionalFeatures[other]) {
                diagnostics = "RHI-METAL-CAPABILITY-OPTIONAL-DUPLICATE-FEATURE";
                return false;
            }
        }
        for (const auto requiredFeature : contract.features) {
            if (contract.optionalFeatures[index] == requiredFeature) {
                diagnostics = "RHI-METAL-CAPABILITY-REQUIRED-OPTIONAL-FEATURE-COLLISION";
                return false;
            }
        }
    }
    const auto requiresFeature = [&](rhi::semantics::NativeBackendFeature feature, bool available) {
        if (rhi::semantics::hasNativeFeature(contract, feature) && !available) {
            diagnostics = "RHI-METAL-CAPABILITY-CONTRACT-EXCEEDS-NATIVE-PROBE";
            return false;
        }
        return true;
    };
    return requiresFeature(
               rhi::semantics::NativeBackendFeature::ArgumentBuffers, probe.argumentBuffers)
        && requiresFeature(
            rhi::semantics::NativeBackendFeature::ExplicitMemoryControl, probe.placementAliasing)
        && requiresFeature(rhi::semantics::NativeBackendFeature::SecondaryCommandBuffers,
            probe.secondaryCommandBuffers)
        && requiresFeature(rhi::semantics::NativeBackendFeature::IndirectCommandBuffers,
            probe.indirectCommandBuffers)
        && requiresFeature(rhi::semantics::NativeBackendFeature::TimelineSynchronization,
            probe.timelineSynchronization)
        && requiresFeature(rhi::semantics::NativeBackendFeature::NativeSync, probe.nativeSync)
        && requiresFeature(rhi::semantics::NativeBackendFeature::TileMemoryHints,
            probe.memorylessTransientTextures)
        && requiresFeature(
            rhi::semantics::NativeBackendFeature::TextureCompressionBc, probe.textureCompressionBc)
        && requiresFeature(
            rhi::semantics::NativeBackendFeature::TimestampQueries, probe.timestampQueries)
        && requiresFeature(rhi::semantics::NativeBackendFeature::PipelineStatisticsQueries,
            probe.pipelineStatisticsQueries)
        && requiresFeature(
            rhi::semantics::NativeBackendFeature::FramebufferFetch, probe.framebufferFetch)
        && requiresFeature(rhi::semantics::NativeBackendFeature::FramebufferFetchDepth,
            probe.framebufferFetchDepth)
        && requiresFeature(rhi::semantics::NativeBackendFeature::Rg16FloatStorageTexture,
            probe.rg16FloatStorageTexture)
        && requiresFeature(rhi::semantics::NativeBackendFeature::Rgba16FloatStorageTexture,
            probe.rgba16FloatStorageTexture)
        && requiresFeature(rhi::semantics::NativeBackendFeature::SceneGeometryRayQuery,
            probe.sceneGeometryRayQuery);
}

bool metalTransientAliasingContractAvailable(
    const rhi::semantics::NativeBackendCapabilityContract& contract)
{
    return rhi::semantics::hasExecutableNativeFeature(
               contract, rhi::semantics::NativeBackendFeature::ExplicitMemoryControl)
        && metalRhiNativePlacementAliasingAvailable()
        && metalRhiNativeCompletionAndAliasingCommandsAvailable();
}

bool metalRuntimeFeatureAvailable(
    rhi::semantics::NativeBackendFeature feature, const MetalRhiNativeFeatureProbe& probe)
{
    switch (feature) {
    case rhi::semantics::NativeBackendFeature::ArgumentBuffers:
        return probe.argumentBuffers;
    case rhi::semantics::NativeBackendFeature::ExplicitMemoryControl:
        return probe.placementAliasing;
    case rhi::semantics::NativeBackendFeature::SecondaryCommandBuffers:
        return probe.secondaryCommandBuffers;
    case rhi::semantics::NativeBackendFeature::IndirectCommandBuffers:
        return probe.indirectCommandBuffers;
    case rhi::semantics::NativeBackendFeature::TimelineSynchronization:
        return probe.timelineSynchronization;
    case rhi::semantics::NativeBackendFeature::NativeSync:
        return probe.nativeSync;
    case rhi::semantics::NativeBackendFeature::TileMemoryHints:
        return probe.memorylessTransientTextures;
    case rhi::semantics::NativeBackendFeature::TextureCompressionBc:
        return probe.textureCompressionBc;
    case rhi::semantics::NativeBackendFeature::TimestampQueries:
        return probe.timestampQueries;
    case rhi::semantics::NativeBackendFeature::PipelineStatisticsQueries:
        return probe.pipelineStatisticsQueries;
    case rhi::semantics::NativeBackendFeature::FramebufferFetch:
        return probe.framebufferFetch;
    case rhi::semantics::NativeBackendFeature::FramebufferFetchDepth:
        return probe.framebufferFetchDepth;
    case rhi::semantics::NativeBackendFeature::Rg16FloatStorageTexture:
        return probe.rg16FloatStorageTexture;
    case rhi::semantics::NativeBackendFeature::Rgba16FloatStorageTexture:
        return probe.rgba16FloatStorageTexture;
    case rhi::semantics::NativeBackendFeature::SceneGeometryRayQuery:
        return probe.sceneGeometryRayQuery;
    default:
        return true;
    }
}

bool metalTextureCapabilityAllows(const rhi::TextureDesc& desc,
    const rhi::semantics::NativeBackendCapabilityContract& contract,
    const MetalRhiNativeFeatureProbe& probe)
{
    if (desc.format == rhi::TextureFormat::R32Float
        && metalHasTextureUsage(desc.usage, rhi::TextureUsage::Sampled)
        && metalHasTextureUsage(desc.usage, rhi::TextureUsage::CopyDst)
        && !probe.r32FloatSampledUpload) {
        return false;
    }
    if (rhi::isBlockCompressed(desc.format)
        && !rhi::semantics::hasExecutableNativeFeature(
            contract, rhi::semantics::NativeBackendFeature::TextureCompressionBc)
        && !rhi::semantics::hasExecutableOptionalNativeFeature(
            contract, rhi::semantics::NativeBackendFeature::TextureCompressionBc)) {
        return false;
    }
    if (rhi::isBlockCompressed(desc.format) && !probe.textureCompressionBc) {
        return false;
    }
    if (rhi::isBlockCompressed(desc.format)
        && (metalHasTextureUsage(desc.usage, rhi::TextureUsage::Storage)
            || metalHasTextureUsage(desc.usage, rhi::TextureUsage::RenderAttachment))) {
        return false;
    }
    if (desc.format == rhi::TextureFormat::Depth24Stencil8
        && metalHasTextureUsage(desc.usage, rhi::TextureUsage::Storage)) {
        return false;
    }
    if (!metalHasTextureUsage(desc.usage, rhi::TextureUsage::Storage)) {
        return true;
    }
    if (!rhi::semantics::hasExecutableNativeFeature(
            contract, rhi::semantics::NativeBackendFeature::StorageTextureBinding)) {
        return false;
    }
    if (desc.format == rhi::TextureFormat::Rg16Float) {
        return rhi::semantics::hasExecutableNativeFeature(
                   contract, rhi::semantics::NativeBackendFeature::Rg16FloatStorageTexture)
            && probe.rg16FloatStorageTexture;
    }
    if (desc.format == rhi::TextureFormat::Rgba16Float) {
        return rhi::semantics::hasExecutableNativeFeature(
                   contract, rhi::semantics::NativeBackendFeature::Rgba16FloatStorageTexture)
            && probe.rgba16FloatStorageTexture;
    }
    return true;
}

class MetalCompletionFence final : public rhi::GpuFence {
public:
    void signal() override
    {
        throw std::logic_error("RHI-METAL-FENCE-HOST-SIGNAL-UNAUTHENTICATED");
    }

    bool isSignaled() const override { return state_ && state_->isComplete(); }

    void wait() override
    {
        if (!state_)
            throw std::logic_error("RHI-METAL-FENCE-NOT-SUBMITTED");
        state_->wait();
        if (state_->outcome() == MetalRhiCompletionState::Outcome::Failed)
            throw std::runtime_error("RHI-METAL-FENCE-SUBMISSION-FAILED");
    }

    void reset() override
    {
        if (state_ && !state_->isComplete())
            throw std::logic_error("RHI-METAL-FENCE-RESET-PENDING");
        state_.reset();
    }

    bool canBind() const noexcept { return !state_; }

    void bind(std::shared_ptr<MetalRhiCompletionState> state) noexcept
    {
        state_ = std::move(state);
    }

private:
    std::shared_ptr<MetalRhiCompletionState> state_;
};

class MetalRhiBinarySemaphore final : public rhi::GpuSemaphore {
public:
    explicit MetalRhiBinarySemaphore(std::shared_ptr<MetalRhiNativeSharedEventPayload> payload)
        : payload_(std::move(payload))
    {}

    void signal() override
    {
        if (payload_)
            signalMetalRhiNativeSharedEvent(payload_, 1u);
    }

    void wait() override
    {
        if (payload_)
            waitMetalRhiNativeSharedEvent(payload_, 1u);
    }

    void reset() override
    {
        // Shared-event values are monotonic; reset starts a new private epoch.
        payload_ = createMetalRhiNativeSharedEventPayload("MetalBinarySemaphore");
    }

    const std::shared_ptr<MetalRhiNativeSharedEventPayload>& payload() const { return payload_; }

private:
    std::shared_ptr<MetalRhiNativeSharedEventPayload> payload_;
};

class MetalRhiTimelineSemaphore final : public rhi::GpuTimelineSemaphore {
public:
    MetalRhiTimelineSemaphore(
        std::shared_ptr<MetalRhiNativeSharedEventPayload> payload, std::uint64_t initialValue)
        : payload_(std::move(payload)), maximumSubmittedValue_(initialValue)
    {
        if (payload_ && initialValue != 0u) {
            signalMetalRhiNativeSharedEvent(payload_, initialValue);
        }
    }

    void signal(std::uint64_t value) override
    {
        if (payload_ && reserveSignalValue(value)) {
            signalMetalRhiNativeSharedEvent(payload_, value);
        }
    }

    void wait(std::uint64_t value) override
    {
        if (payload_)
            waitMetalRhiNativeSharedEvent(payload_, value);
    }

    std::uint64_t getValue() const override
    {
        return payload_ && payload_->state ? payload_->state->value() : 0u;
    }

    void hostWait(std::uint64_t value, std::uint64_t timeoutNs) override
    {
        if (payload_) {
            (void)waitForMetalRhiNativeSharedEvent(payload_, value, timeoutNs);
        }
    }

    const std::shared_ptr<MetalRhiNativeSharedEventPayload>& payload() const { return payload_; }

    bool reserveSignalValue(std::uint64_t value, std::uint64_t* previousValue = nullptr) const
    {
        auto current = maximumSubmittedValue_.load(std::memory_order_acquire);
        for (;;) {
            if (previousValue) {
                *previousValue = current;
            }
            if (value < current || value == current) {
                return value == current;
            }
            if (maximumSubmittedValue_.compare_exchange_weak(
                    current, value, std::memory_order_acq_rel, std::memory_order_acquire)) {
                return true;
            }
        }
    }

    void rollbackSignalReservation(std::uint64_t value, std::uint64_t previousValue) const
    {
        if (value == previousValue) {
            return;
        }
        auto expected = value;
        (void)maximumSubmittedValue_.compare_exchange_strong(
            expected, previousValue, std::memory_order_acq_rel, std::memory_order_acquire);
    }

private:
    std::shared_ptr<MetalRhiNativeSharedEventPayload> payload_;
    mutable std::atomic<std::uint64_t> maximumSubmittedValue_ = 0;
};

class MetalRhiQueue final : public rhi::GpuQueue {

public:
    rhi::GpuQueueRole role() const override { return rhi::GpuQueueRole::Graphics; }

    explicit MetalRhiQueue(std::shared_ptr<MetalRhiNativeQueueStats> nativeStats)
        : nativeStats_(std::move(nativeStats))
    {}

    rhi::QueueSubmitResult submit(const rhi::CommandBuffer& commandBuffer) override
    {
        return submit(commandBuffer, nullptr);
    }

    rhi::QueueSubmitResult submit(
        const rhi::CommandBuffer& commandBuffer, rhi::GpuFence* signalFence) override
    {
        auto* completionFence =
            signalFence ? dynamic_cast<MetalCompletionFence*>(signalFence) : nullptr;
        if (signalFence && !completionFence) {
            return {false, "RHI-METAL-COMPLETION-FENCE-BACKEND-MISMATCH", 0};
        }
        const auto nativePayload = metalRhiNativePayloadFor(commandBuffer);
        if (!nativePayload) {
            return {false, "RHI-METAL-COMPLETION-PAYLOAD-MISSING", 0};
        }
        if (completionFence && !completionFence->canBind())
            return {false, "RHI-METAL-FENCE-ALREADY-BOUND", 0};
        auto completionState = nativePayload->preparedCompletionState();
        std::pmr::string diagnostics;
        if (!nativePayload->submit(
                *nativeStats_, completionState, nullptr, 0u, nullptr, 0u, diagnostics)) {
            return {false, diagnostics, commandBuffer.commandCount()};
        }
        if (completionFence)
            completionFence->bind(completionState);
        ++submitCount_;
        if (!rhi::acceptRecordedCommandOwnership(commandBuffer))
            return {false, "RHI-COMMAND-STATE-OWNER-MISSING", commandBuffer.commandCount(), true};
        return {true, "", commandBuffer.commandCount()};
    }

    rhi::QueueSubmitResult submit(const rhi::CommandBuffer& commandBuffer,
        rhi::GpuTimelineSemaphore* waitSemaphore,
        uint64_t waitValue,
        rhi::GpuTimelineSemaphore* signalSemaphore,
        uint64_t signalValue,
        rhi::GpuFence* signalFence) override
    {
        const auto* wait =
            waitSemaphore ? dynamic_cast<const MetalRhiTimelineSemaphore*>(waitSemaphore) : nullptr;
        const auto* signal = signalSemaphore
            ? dynamic_cast<const MetalRhiTimelineSemaphore*>(signalSemaphore)
            : nullptr;
        if ((waitSemaphore && !wait) || (signalSemaphore && !signal)) {
            return {false, "RHI-METAL-TIMELINE-SEMAPHORE-BACKEND-MISMATCH", 0};
        }
        const auto nativePayload = metalRhiNativePayloadFor(commandBuffer);
        if (!nativePayload) {
            return {false, "RHI-METAL-COMPLETION-PAYLOAD-MISSING", 0};
        }
        auto* completionFence =
            signalFence ? dynamic_cast<MetalCompletionFence*>(signalFence) : nullptr;
        if (signalFence && !completionFence) {
            return {false, "RHI-METAL-COMPLETION-FENCE-BACKEND-MISMATCH", 0};
        }
        if (completionFence && !completionFence->canBind())
            return {false, "RHI-METAL-FENCE-ALREADY-BOUND", 0};
        std::uint64_t previousSignalValue = 0;
        if (signal && !signal->reserveSignalValue(signalValue, &previousSignalValue)) {
            return {false, "RHI-METAL-TIMELINE-SEMAPHORE-SIGNAL-MUST-BE-MONOTONIC", 0};
        }
        auto completionState = nativePayload->preparedCompletionState();
        std::pmr::string diagnostics;
        if (!nativePayload->submit(*nativeStats_,
                completionState,
                wait ? wait->payload() : nullptr,
                waitValue,
                signal ? signal->payload() : nullptr,
                signalValue,
                diagnostics)) {
            if (signal) {
                signal->rollbackSignalReservation(signalValue, previousSignalValue);
            }
            return {false, diagnostics, commandBuffer.commandCount()};
        }
        if (completionFence)
            completionFence->bind(completionState);
        ++submitCount_;
        if (!rhi::acceptRecordedCommandOwnership(commandBuffer))
            return {false, "RHI-COMMAND-STATE-OWNER-MISSING", commandBuffer.commandCount(), true};
        return {true, "", commandBuffer.commandCount()};
    }

    std::uint32_t submitCount() const override { return submitCount_; }

    std::shared_ptr<MetalRhiNativeQueueStats> nativeStats() const { return nativeStats_; }

private:
    std::shared_ptr<MetalRhiNativeQueueStats> nativeStats_;
    std::uint32_t submitCount_ = 0;
};

/// Metal memory heap backed by an MTLHeap. Supports aliasing: multiple
/// placed textures share the same native heap allocation.
class MetalMemoryHeap final : public rhi::GpuMemoryHeap {
public:
    explicit MetalMemoryHeap(std::shared_ptr<MetalRhiNativeHeapPayload> payload,
        std::shared_ptr<void> allocationReleaseToken)
        : id_(rhi::allocateGpuResourceId()), payload_(std::move(payload)),
          allocationReleaseToken_(std::move(allocationReleaseToken))
    {
        payload_->resource.allocationReleaseToken = allocationReleaseToken_;
    }

    rhi::GpuResourceId id() const override { return id_; }

    std::uint64_t sizeBytes() const override { return payload_->sizeBytes; }

    std::uint64_t alignmentBytes() const override { return payload_->alignmentBytes; }

    bool supportsAliasing() const override { return payload_->resource.nativeRuntimeBacked; }

    const MetalRhiNativeHeapPayload& payload() const { return *payload_; }

private:
    rhi::GpuResourceId id_;
    std::shared_ptr<MetalRhiNativeHeapPayload> payload_;
    std::shared_ptr<void> allocationReleaseToken_;
};

class MetalBindlessCrossExtension final : public rhi::BindlessCrossExtension {
public:
    MetalBindlessCrossExtension(
        const rhi::semantics::NativeBackendCapabilityContract& capabilityContract,
        std::shared_ptr<rhi::semantics::DirectObjectTelemetryTracker> telemetry,
        std::shared_ptr<rhi::semantics::DirectObjectBudgetTracker> directObjectBudget,
        std::shared_ptr<MetalRhiResourceRegistry> registry,
        bool optedIn,
        bool imagesOptedIn,
        bool graphicsOptedIn,
        bool graphicsImagesOptedIn)
        : capabilityContract_(capabilityContract), telemetry_(std::move(telemetry)),
          directObjectBudget_(std::move(directObjectBudget)), registry_(std::move(registry))
    {
        const auto nativeFeatures = probeMetalRhiNativeFeatures();
        caps_ = metalBindlessComputeCapabilities(nativeFeatures,
            optedIn,
            describeMetalRhiNativeRuntimeBoundary().runtimeAvailableOnThisPlatform,
            rhi::allocateGpuResourceId().value,
            imagesOptedIn,
            graphicsOptedIn,
            graphicsImagesOptedIn,
            metalRhiNativeDeviceRemovalResultsAvailable());
        if (metalBindlessGraphicsExecutionAvailable(caps_)
            && metalRhiNativeDeviceRemovalResultsAvailable()) {
            caps_.originalGraphicsExecutionContractVersion = 1u;
            if (metalBindlessGraphicsDepthExecutionAvailable(caps_, nativeFeatures))
                caps_.originalGraphicsDepthExecutionContractVersion = 2u;
            const auto push = rhi::semantics::buildPushConstantLoweringPlan(
                rhi::Backend::Metal, capabilityContract_);
            if (push.hasExecutablePayload
                && push.outcome == rhi::PushConstantLoweringOutcome::Native
                && capabilityContract_.maxPushConstantSizeBytes)
                caps_.originalGraphicsPushContractVersion = 1u;
        }
    }

    const rhi::BindlessCapabilities& capabilities() const noexcept override { return caps_; }

    std::shared_ptr<const rhi::OriginalInitializedGeometrySource>
    prepareOriginalInitializedBufferPayload(rhi::GpuDevice& device,
        const rhi::OriginalInitializedBufferRequest& request,
        const char*& diagnostic) const override
    {
        return MetalOriginalGeometryContent::prepareOriginal(device,
            request.desc,
            request.bytes,
            request.byteCount,
            request.budget,
            request.quotas,
            diagnostic);
    }

    std::shared_ptr<const rhi::OriginalBindlessGraphicsExecution>
    prepareOriginalGraphicsExecutionPayload(rhi::GpuDevice& device,
        const rhi::OriginalBindlessGraphicsExecutionInputs& inputs,
        const rhi::OriginalBindlessGraphicsExecutionBudget& budget,
        core::pmr::String& diagnostic) const override
    {
        return prepareMetalRhiOriginalBindlessGraphicsExecution(device, inputs, budget, diagnostic);
    }

    std::shared_ptr<const rhi::OriginalBindlessRootExecution> prepareOriginalRootExecutionPayload(
        rhi::GpuDevice& device,
        const rhi::OriginalBindlessRootFenceExecutionInputs& inputs,
        const rhi::OriginalBindlessRootFenceExecutionBudget& budget,
        core::pmr::String& diagnostic) const override
    {
        return prepareMetalRhiOriginalBindlessRootExecution(device, inputs, budget, diagnostic);
    }

    core::UniquePtr<rhi::ComputePipeline> createComputePipeline(
        std::shared_ptr<const rhi::BindlessAuthorization> authorization,
        std::string_view entry,
        core::pmr::String& diagnostic) const override
    {
        diagnostic.clear();
        if (caps_.preparedComputeBindingContractVersion != 1u
            || caps_.execution != rhi::BindlessExecution::Native || !caps_.executablePayload
            || !authorization || !authorization->issuedBy(this, authorization)) {
            diagnostic = "RHI-METAL-BINDLESS-EXECUTION-UNAVAILABLE";
            return {};
        }
        auto reservation = directObjectBudget_
            ? directObjectBudget_->reserve(rhi::semantics::DirectObjectBudgetKind::ComputePipeline)
            : rhi::semantics::DirectObjectBudgetReservation{true, {}};
        if (!reservation.success) {
            diagnostic = "RHI-METAL-BINDLESS-PIPELINE-BUDGET-EXHAUSTED";
            return {};
        }
        auto pipeline = createMetalRhiAuthorizedBindlessComputePipeline(*this,
            std::move(authorization),
            entry,
            capabilityContract_,
            telemetry_,
            std::move(reservation.lease),
            &diagnostic);
        if (!pipeline && diagnostic.empty())
            diagnostic = "RHI-METAL-BINDLESS-PIPELINE-CREATION-FAILED";
        return pipeline;
    }

    core::UniquePtr<rhi::RenderPipeline> createRenderPipeline(
        std::shared_ptr<const rhi::BindlessAuthorization> authorization,
        const rhi::RenderPipelineDesc& state,
        std::string_view vertexEntry,
        std::string_view fragmentEntry,
        core::pmr::String& diagnostic) const override
    {
        return createRenderPipelineImpl(
            std::move(authorization), state, vertexEntry, fragmentEntry, diagnostic, nullptr);
    }

    core::UniquePtr<rhi::RenderPipeline> createRenderPipeline(
        std::shared_ptr<const rhi::BindlessAuthorization> authorization,
        const rhi::RenderPipelineDesc& state,
        std::string_view vertexEntry,
        std::string_view fragmentEntry,
        const rhi::BindlessGraphicsPushRequest& push,
        core::pmr::String& diagnostic) const override
    {
        if (const auto* id = push.diagnostic()) {
            diagnostic = id;
            return {};
        }
        if (caps_.originalGraphicsPushContractVersion != 1u) {
            diagnostic = "RHI-METAL-BINDLESS-GRAPHICS-PUSH-UNAVAILABLE";
            return {};
        }
        return createRenderPipelineImpl(
            std::move(authorization), state, vertexEntry, fragmentEntry, diagnostic, &push);
    }

private:
    core::UniquePtr<rhi::RenderPipeline> createRenderPipelineImpl(
        std::shared_ptr<const rhi::BindlessAuthorization> authorization,
        const rhi::RenderPipelineDesc& state,
        std::string_view vertexEntry,
        std::string_view fragmentEntry,
        core::pmr::String& diagnostic,
        const rhi::BindlessGraphicsPushRequest* push) const
    {
        if (!metalBindlessGraphicsExecutionAvailable(caps_)) {
            diagnostic = "RHI-METAL-BINDLESS-GRAPHICS-EXECUTION-UNAVAILABLE";
            return {};
        }
        if (!authorization || !authorization->issuedBy(this, authorization)) {
            diagnostic = "RHI-METAL-BINDLESS-PIPELINE-AUTHORIZATION-INVALID";
            return {};
        }
        auto reservation = directObjectBudget_
            ? directObjectBudget_->reserve(rhi::semantics::DirectObjectBudgetKind::RenderPipeline)
            : rhi::semantics::DirectObjectBudgetReservation{true, {}};
        if (!reservation.success) {
            diagnostic = "RHI-METAL-BINDLESS-PIPELINE-BUDGET-EXHAUSTED";
            return {};
        }
        return createMetalRhiAuthorizedBindlessRenderPipeline(*this,
            std::move(authorization),
            state,
            vertexEntry,
            fragmentEntry,
            capabilityContract_,
            telemetry_,
            std::move(reservation.lease),
            diagnostic,
            push);
    }

protected:
    std::uint64_t queryOriginalSnapshotBufferAllocation(
        const rhi::BufferDesc& desc, const char*& diagnostic) const override
    {
        diagnostic = "RHI-METAL-BINDLESS-SNAPSHOT-ALLOCATION-RUNTIME";
        if (!caps_.limitsQueried || !registry_
            || !describeMetalRhiNativeRuntimeBoundary().runtimeAvailableOnThisPlatform)
            return 0u;
        // Cold, bounded probe of the same storage policy used by createBuffer.
        // allocatedSize can exceed length. Release this unregistered probe
        // before reserving/creating any published original buffer.
        const auto probe =
            createMetalRhiNativeBufferPayload(desc, metalRhiMemoryPolicyFor(desc.memory));
        if (!probe) {
            diagnostic = "RHI-METAL-BINDLESS-SNAPSHOT-ALLOCATION-PROBE";
            return 0u;
        }
        return observeMetalRhiNativeBufferAllocation(probe->resource, desc.sizeBytes, diagnostic);
    }

    std::shared_ptr<const rhi::BindlessPreparedComputeBinding> prepareComputeBinding(
        std::shared_ptr<const rhi::BindlessComputeBindingOwners> owners,
        const rhi::BindlessComputeBindingBudget& budget,
        core::pmr::String& diagnostic) const override
    {
        if (caps_.preparedComputeBindingContractVersion != 1u
            || caps_.execution != rhi::BindlessExecution::Native || !caps_.executablePayload) {
            diagnostic = "RHI-METAL-BINDLESS-PREPARED-COMPUTE-UNAVAILABLE";
            return {};
        }
        return prepareMetalRhiComputeBinding(std::move(owners), budget, diagnostic);
    }

    std::shared_ptr<const rhi::BindlessPreparedGraphicsBinding> prepareGraphicsBinding(
        std::shared_ptr<const rhi::BindlessGraphicsBindingOwners> owners,
        const rhi::BindlessGraphicsBindingBudget& budget,
        core::pmr::String& diagnostic) const override
    {
        if (!metalBindlessGraphicsExecutionAvailable(caps_)) {
            diagnostic = "RHI-METAL-BINDLESS-PREPARED-GRAPHICS-UNAVAILABLE";
            return {};
        }
        return prepareMetalRhiGraphicsBinding(std::move(owners), budget, diagnostic);
    }

    rhi::ValidationResult validateSnapshotCreation(
        const rhi::BindlessLoweringPlan& plan) const override
    {
        if (plan.originalExecutionContractVersion) {
            const auto execution = rhi::semantics::OriginalBindlessExecutionLowering::lowering(
                rhi::Backend::Metal, plan.originalExecutionContractVersion);
            if (!execution.executionAvailable || !execution.completionAvailable
                || execution.diagnostic)
                return {false, execution.diagnostic};
            if (!rhi::semantics::OriginalBindlessExecutionLowering::capabilityAvailable(
                    caps_, plan.originalExecutionContractVersion))
                return {false, "RHI-METAL-ROOT-EXECUTION-CAPABILITY"};
        }
        if (caps_.execution != rhi::BindlessExecution::Native || !caps_.limitsQueried
            || !caps_.executablePayload || caps_.faultReadbackContractVersion != 1u
            || (caps_.preparedComputeBindingContractVersion != 1u
                && !metalBindlessGraphicsExecutionAvailable(caps_))
            || !registry_ || !plan.success
            || (!plan.executable
                && !(rhi::semantics::OriginalBindlessPreparationLowering::hasPlan(plan, 7u)
                    && rhi::semantics::OriginalBindlessPreparationLowering::capabilityAvailable(
                        caps_, 7u))
                && !(rhi::semantics::OriginalBindlessPreparationLowering::hasPlan(plan, 6u)
                    && rhi::semantics::OriginalBindlessPreparationLowering::capabilityAvailable(
                        caps_, 6u))
                && !(rhi::semantics::OriginalBindlessPreparationLowering::hasPlan(plan, 8u)
                    && rhi::semantics::OriginalBindlessPreparationLowering::capabilityAvailable(
                        caps_, 8u))
                && !(
                    (rhi::semantics::OriginalBindlessPreparationLowering::hasMixedImageSnapshotPlan(
                         plan)
                        || rhi::semantics::OriginalBindlessPreparationLowering::
                            hasMixedImageResourcePlan(plan))
                    && rhi::semantics::OriginalBindlessPreparationLowering::capabilityAvailable(
                        caps_, plan.originalPreparationContractVersion)))
            || plan.execution != rhi::BindlessExecution::Native
            || plan.deviceIdentity != caps_.deviceIdentity)
            return {false, "RHI-METAL-BINDLESS-SNAPSHOT-EXECUTION-UNAVAILABLE"};
        return {true, {}};
    }

    rhi::ValidationResult validateAuthorization(const rhi::BindlessShaderPackage& package,
        const rhi::BindlessSnapshot& snapshot) const override
    {
        if (snapshot.plan().originalPreparationContractVersion == 7u)
            return {false, "RHI-BINDLESS-ORIGINAL-STORAGE-PREPARATION-SNAPSHOT-ONLY"};
        if (rhi::semantics::OriginalBindlessPreparationLowering::hasMixedImageSnapshotPlan(
                snapshot.plan()))
            return {false, "RHI-BINDLESS-ORIGINAL-MIXED-IMAGE-SNAPSHOT-ONLY"};
        const auto gate = validateSnapshotCreation(snapshot.plan());
        if (!gate.success)
            return {false, "RHI-METAL-BINDLESS-AUTHORIZATION-UNAVAILABLE"};
        const auto stages = package.artifact().snapshotMetadata.stages;
        const bool compute = stages == rhi::ShaderStage::Compute;
        const bool images =
            compute && caps_.support[2u][0u].operations != rhi::BindlessOperation::None;
        if ((compute && caps_.preparedComputeBindingContractVersion != 1u)
            || (!compute && !metalBindlessGraphicsExecutionAvailable(caps_)))
            return {false, "RHI-METAL-BINDLESS-AUTHORIZATION-UNAVAILABLE"};
        const bool originalColor = snapshot.plan().originalExecutionContractVersion == 8u;
        const bool originalDepth = snapshot.plan().originalExecutionContractVersion == 6u
            || rhi::semantics::OriginalBindlessPreparationLowering::hasPlan(snapshot.plan(), 6u);
        const bool storageRoot =
            rhi::semantics::OriginalBindlessPreparationLowering::hasStoragePlan(snapshot.plan());
        const bool mixedImages =
            rhi::semantics::OriginalBindlessPreparationLowering::hasMixedImageResourcePlan(
                snapshot.plan());
        const bool sampledGraphics = !compute && caps_.support[1u][0u].nonUniform;
        const auto abi = compute ? validateMetalBindlessComputeArtifact(package.artifact(),
                                       images,
                                       originalColor,
                                       storageRoot,
                                       originalDepth,
                                       mixedImages)
                                 : validateMetalBindlessGraphicsPackage(package, sampledGraphics);
        if (!abi.success)
            return abi;
        if (!compute) {
            const auto depthDeclaration = validateMetalBindlessOrdinaryDepthPipelinePrerequisites(
                package.artifact(), probeMetalRhiNativeFeatures());
            if (!depthDeclaration.success)
                return depthDeclaration;
        }
        const auto slots =
            validateMetalBindlessBufferSlotBudget(package.artifact(), caps_.maxBindingsPerGroup);
        if (!slots.success)
            return slots;
        for (const auto& module : package.modules())
            if (module.reflection.reflection.entryPoints.size() != 1u
                || (compute ? module.reflection.reflection.entryPoints.front().stage
                            != shader::ShaderStage::Compute
                            : module.reflection.reflection.entryPoints.front().stage
                                != shader::ShaderStage::Vertex
                            && module.reflection.reflection.entryPoints.front().stage
                                != shader::ShaderStage::Fragment))
                return {false,
                    compute ? "RHI-METAL-BINDLESS-COMPUTE-STAGE-UNSUPPORTED"
                            : "RHI-METAL-BINDLESS-GRAPHICS-STAGE-UNSUPPORTED"};
        if (!snapshot.descriptorSet()
            || !metalRhiDescriptorRegistryMatches(*snapshot.descriptorSet(), registry_))
            return {false, "RHI-METAL-BINDLESS-AUTHORIZATION-DEVICE-MISMATCH"};
        const bool completedImages = originalColor
            || (sampledGraphics
                && std::any_of(package.artifact().physicalLayout.bindings.begin(),
                    package.artifact().physicalLayout.bindings.end(),
                    [](const auto& b) {
                        return b.type == rhi::DescriptorType::SampledTexture;
                    }));
        const auto descriptors = originalDepth
            ? prepareMetalRhiOriginalCompletedDepthDescriptors(snapshot)
            : (storageRoot || mixedImages
                  || rhi::semantics::OriginalBindlessPreparationLowering::hasProducedImageReadPlan(
                      snapshot.plan()))
            ? prepareMetalOriginalImageCellDescriptors(snapshot)
            : completedImages
            ? prepareMetalRhiOriginalCompletedImageDescriptors(snapshot,
                  compute ? rhi::OriginalBindlessCompletedImageScope::ComputeColor8
                          : rhi::OriginalBindlessCompletedImageScope::SampledGraphics1)
            : metalRhiNativePayloadFor(*snapshot.descriptorSet());
        if (!descriptors || !descriptors->nativeRuntimeBacked)
            return {false, "RHI-METAL-BINDLESS-AUTHORIZATION-NATIVE-PAYLOAD-MISSING"};
        for (const auto& binding : descriptors->bindings) {
            if (binding.type == rhi::DescriptorType::SampledTexture
                || binding.type == rhi::DescriptorType::StorageTexture) {
                const auto registered = registry_->find(binding.resourceId);
                if (!registered || !registered->parentTextureLive || !registered->textureView
                    || registered->textureView != binding.textureView
                    || !binding.textureView->resource.nativeRuntimeBacked)
                    return {false, "RHI-METAL-BINDLESS-AUTHORIZATION-NATIVE-PAYLOAD-MISSING"};
                const auto& view = *binding.textureView;
                const auto valid = (originalDepth || (mixedImages && binding.depthTexture))
                    ? rhi::ValidationResult{binding.type == rhi::DescriptorType::SampledTexture
                              && view.format == rhi::TextureFormat::D32Float
                              && view.sampleCount == 1u
                              && registered->textureViewDimension
                                  == rhi::TextureViewDimension::Texture2D
                              && registered->textureViewAspect == rhi::TextureAspect::Depth,
                          "RHI-METAL-ROOT-COMPLETED-DEPTH-IMAGE"}
                    : (storageRoot || mixedImages)
                    ? rhi::ValidationResult{view.format == rhi::TextureFormat::Rgba16Float
                              && registered->textureViewDimension
                                  == rhi::TextureViewDimension::Texture2D
                              && registered->textureViewAspect == rhi::TextureAspect::Color
                              && view.sampleCount == 1u,
                          "RHI-METAL-STORAGE-ROOT-IMAGE-SHAPE"}
                    : completedImages
                    ? rhi::ValidationResult{binding.type == rhi::DescriptorType::SampledTexture
                              && rhi::OriginalBindlessCompletedImageDomain::acceptsTexture(
                                  view.format,
                                  registered->textureViewDimension,
                                  registered->textureViewAspect,
                                  view.sampleCount),
                          "RHI-METAL-ROOT-COMPLETED-COLOR-IMAGE"}
                    : MetalRhiBindlessComputeImageContract::validateTexture(binding.type,
                          view.format,
                          registered->textureViewDimension,
                          registered->textureViewAspect,
                          view.sampleCount,
                          view.arrayLayerCount);
                if (!valid.success)
                    return valid;
            } else if (binding.type == rhi::DescriptorType::Sampler) {
                const auto registered = registry_->find(binding.resourceId);
                if (!registered || !binding.sampler || registered->sampler != binding.sampler
                    || !binding.sampler->resource.nativeRuntimeBacked)
                    return {false, "RHI-METAL-BINDLESS-AUTHORIZATION-NATIVE-PAYLOAD-MISSING"};
                const auto valid = MetalRhiBindlessComputeImageContract::validateSampler(
                    binding.sampler->comparisonEnabled, binding.sampler->argumentBufferCompatible);
                if (!valid.success)
                    return valid;
            }
        }
        for (const auto* buffer : {snapshot.metadataBuffer(),
                 snapshot.faultLatchBuffer(),
                 snapshot.faultReadbackBuffer()}) {
            const auto native = buffer ? metalRhiNativePayloadFor(*buffer) : nullptr;
            const auto registered = buffer ? registry_->find(buffer->id()) : std::nullopt;
            if (!native || !registered || registered->buffer != native
                || !native->resource.nativeRuntimeBacked || !native->resource.nativeObject)
                return {false, "RHI-METAL-BINDLESS-AUTHORIZATION-NATIVE-PAYLOAD-MISSING"};
        }
        const auto readback = metalRhiNativePayloadFor(*snapshot.faultReadbackBuffer());
        if (readback->resource.storageMode != MetalRhiNativeStorageMode::Shared
            && readback->resource.storageMode != MetalRhiNativeStorageMode::Managed)
            return {false, "RHI-METAL-BINDLESS-AUTHORIZATION-READBACK-UNAVAILABLE"};
        return {true, {}};
    }

private:
    const rhi::semantics::NativeBackendCapabilityContract& capabilityContract_;
    std::shared_ptr<rhi::semantics::DirectObjectTelemetryTracker> telemetry_;
    std::shared_ptr<rhi::semantics::DirectObjectBudgetTracker> directObjectBudget_;
    std::shared_ptr<MetalRhiResourceRegistry> registry_;
    rhi::BindlessCapabilities caps_;
};

class MetalRhiDevice final : public rhi::GpuDevice {
public:
    void* queryInterface(std::string_view id) noexcept override
    {
        if (id == rhi::kExtensionBindlessCross) {
            return static_cast<rhi::BindlessCrossExtension*>(&bindless_);
        }
        if (id != rhi::kExtensionTextureReadback
            || !describeMetalRhiNativeRuntimeBoundary().runtimeAvailableOnThisPlatform)
            return nullptr;
        return &textureReadback_;
    }

    const std::shared_ptr<MetalRhiResourceRegistry>& resourceRegistry() const noexcept
    {
        return registry_;
    }

    explicit MetalRhiDevice(MetalRhiDeviceDesc desc, bool hostDiagnostic = false)
        : desc_(std::move(desc)), hostDiagnostic_(hostDiagnostic),
          allocator_({
              desc_.label,
              desc_.memoryBudgetBytes,
              desc_.enableMemorylessTransientTextures,
              desc_.allowNativeHandleExposureToUpperLayers,
          }),
          stagingUploadPool_({
              desc_.label.empty() ? "MetalRhiUploadStagingPool" : desc_.label + "UploadStagingPool",
              desc_.uploadStagingPageSizeBytes,
              desc_.uploadStagingMaxPageCount,
              256,
              true,
          }),
          bindless_(desc_.capabilityContract,
              directObjectTelemetry_,
              directObjectBudget_,
              registry_,
              desc_.enableBindlessCompute && desc_.enableArgumentBuffers,
              desc_.enableBindlessComputeImages,
              desc_.enableBindlessGraphics && desc_.enableArgumentBuffers,
              desc_.enableBindlessGraphicsImages)
    {
        const auto runtime = describeMetalRhiNativeRuntimeBoundary();
        const auto probe = probeMetalRhiNativeFeatures();
        desc_.capabilityContract.runtimeFeaturesQueried = true;
        desc_.capabilityContract.runtimeFeatures.clear();
        if (runtime.runtimeAvailableOnThisPlatform) {
            for (const auto feature : desc_.capabilityContract.features) {
                if (metalRuntimeFeatureAvailable(feature, probe)) {
                    desc_.capabilityContract.runtimeFeatures.push_back(feature);
                }
            }
            for (const auto feature : desc_.capabilityContract.optionalFeatures) {
                if (metalRuntimeFeatureAvailable(feature, probe)) {
                    desc_.capabilityContract.runtimeFeatures.push_back(feature);
                }
            }
        }
        if (!desc_.enableArgumentBuffers) {
            desc_.capabilityContract.runtimeFeatures.erase(
                std::remove(desc_.capabilityContract.runtimeFeatures.begin(),
                    desc_.capabilityContract.runtimeFeatures.end(),
                    rhi::semantics::NativeBackendFeature::ArgumentBuffers),
                desc_.capabilityContract.runtimeFeatures.end());
        }
    }

    rhi::Backend backend() const override { return rhi::Backend::Metal; }

    rhi::DeviceCapabilities capabilities() const override
    {
        auto caps =
            rhi::makeSharedDeviceCapabilities(rhi::Backend::Metal, "RHI Metal", desc_.label);
        const auto runtime = describeMetalRhiNativeRuntimeBoundary();
        const auto nativeFeatures = probeMetalRhiNativeFeatures();
        // The boundary queries the actual shared native device, not the OS name.
        const bool nativeAvailable = runtime.runtimeAvailableOnThisPlatform;
        caps.core = {nativeAvailable, nativeAvailable};
        caps.memory.deviceLocal = nativeAvailable; // MTLStorageModePrivate intent.
        caps.memory.hostVisible = nativeAvailable; // MTLStorageModeShared intent.
        if (nativeAvailable)
            caps.swapchain = rhi::SwapchainCapabilities{};
        caps.explicitFeatures.pushConstants = false;
        caps.explicitFeatures.explicitBarriers = runtime.runtimeAvailableOnThisPlatform;
        caps.explicitFeatures.secondaryCommandBuffers =
            rhi::semantics::hasExecutableNativeFeature(desc_.capabilityContract,
                rhi::semantics::NativeBackendFeature::SecondaryCommandBuffers);
        rhi::EngineFeatureRequirementSet requirements;
        requirements.logicalFrameSlots = 2;
        requirements.features = {
            {rhi::EngineFeature::PushConstants, {}},
            {rhi::EngineFeature::SecondaryCommandBuffer, {}},
            {rhi::EngineFeature::DescriptorTable, {}},
            {rhi::EngineFeature::TransientAliasing, {}},
            {rhi::EngineFeature::TimelineSync, {}},
            {rhi::EngineFeature::ComputeDispatch, {}},
            {rhi::EngineFeature::IndirectDraw, {}},
            {rhi::EngineFeature::StorageTextureBinding, {}},
            {rhi::EngineFeature::NativeSync, {}},
            {rhi::EngineFeature::SubpassInputAttachment, {}},
            {rhi::EngineFeature::SubpassDepthInputAttachment, {}},
            {rhi::EngineFeature::Rg16FloatStorageTexture, {}},
            {rhi::EngineFeature::Rgba16FloatStorageTexture, {}},
            {rhi::EngineFeature::StorageBufferBinding, {}},
            {rhi::EngineFeature::SceneGeometryRayQuery, {}},
        };
        auto plan =
            rhi::semantics::buildNativeEngineFallbackPlan(desc_.capabilityContract, requirements);
        plan.applyTo(caps);
        auto& compute = caps.computeResources;
        compute.sampledDepthTexture = nativeFeatures.sampledDepthTexture;
        compute.maxComputeWorkgroupInvocations = nativeFeatures.maxComputeWorkgroupInvocations;
        compute.maxComputeWorkgroupSizeX = nativeFeatures.maxComputeWorkgroupSizeX;
        compute.maxComputeWorkgroupSizeY = nativeFeatures.maxComputeWorkgroupSizeY;
        compute.maxComputeWorkgroupSizeZ = nativeFeatures.maxComputeWorkgroupSizeZ;
        // ComputeResourceLimits/2: no established Metal dispatch-count fact.
        // Keep X/Y/Z counts zero; consumers must reject instead of borrowing
        // a limit from Vulkan/WebGPU or inferring one from MTLSize's type.
        compute.maxUniformBufferBindingsPerStage = nativeFeatures.maxUniformBufferBindingsPerStage;
        compute.maxUniformBufferBindingSizeBytes = nativeFeatures.maxUniformBufferBindingSizeBytes;
        compute.maxSampledTextureBindingsPerStage =
            nativeFeatures.maxSampledTextureBindingsPerStage;
        compute.maxTexture2DDimension = nativeFeatures.maxTexture2DDimension;
        compute.maxComputeWorkgroupMemoryBytes = nativeFeatures.maxComputeWorkgroupMemoryBytes;
        compute.maxStorageBufferBindingsPerStage = nativeFeatures.maxStorageBufferBindingsPerStage;
        compute.maxStorageBufferBindingSizeBytes = nativeFeatures.maxStorageBufferBindingSizeBytes;
        const bool complete = runtime.runtimeAvailableOnThisPlatform && compute.sampledDepthTexture
            && caps.features.computeDispatch && caps.features.storageBufferBinding
            && compute.maxComputeWorkgroupInvocations != 0u
            && compute.maxComputeWorkgroupSizeX != 0u && compute.maxComputeWorkgroupSizeY != 0u
            && compute.maxComputeWorkgroupMemoryBytes != 0u
            && compute.maxStorageBufferBindingsPerStage != 0u
            && compute.maxStorageBufferBindingSizeBytes != 0u;
        compute.mode =
            complete ? rhi::CapabilityMode::Native : rhi::CapabilityMode::DiagnosticFailure;
        const bool nativeAliasing =
            metalTransientAliasingContractAvailable(desc_.capabilityContract);
        caps.memory.transientAliasing = nativeAliasing;
        caps.features.transientAliasing = nativeAliasing;
        caps.features.timestampQueries = runtime.runtimeAvailableOnThisPlatform
            && nativeFeatures.timestampQueries
            && rhi::semantics::hasExecutableOptionalNativeFeature(
                desc_.capabilityContract, rhi::semantics::NativeBackendFeature::TimestampQueries);
        caps.features.pipelineStatisticsQueries = runtime.runtimeAvailableOnThisPlatform
            && nativeFeatures.pipelineStatisticsQueries
            && rhi::semantics::hasExecutableOptionalNativeFeature(desc_.capabilityContract,
                rhi::semantics::NativeBackendFeature::PipelineStatisticsQueries);
        const bool nativeSync = metalRhiNativeTimelineSynchronizationAvailable();
        const bool nativeSecondary = metalRhiNativeSecondaryCommandBuffersAvailable();
        if (!runtime.runtimeAvailableOnThisPlatform) {
            // The non-Apple translation-unit stubs are diagnostic-only. Do
            // not publish native execution capabilities for them.
            caps.features.computeDispatch = false;
            caps.features.storageTextureBinding = false;
            caps.features.storageBufferBinding = false;
            caps.features.indirectDraw = false;
            caps.features.pushConstants = false;
        }
        caps.features.secondaryCommandBuffers =
            caps.features.secondaryCommandBuffers && nativeSecondary;
        caps.explicitFeatures.secondaryCommandBuffers =
            caps.explicitFeatures.secondaryCommandBuffers && nativeSecondary;
        caps.features.descriptorTable = caps.features.descriptorTable && desc_.enableArgumentBuffers
            && nativeFeatures.argumentBuffers;
        caps.features.subpassInputAttachment =
            caps.features.subpassInputAttachment && nativeFeatures.framebufferFetch;
        caps.features.subpassDepthInputAttachment =
            caps.features.subpassDepthInputAttachment && nativeFeatures.framebufferFetchDepth;
        caps.features.rg16FloatStorageTexture =
            caps.features.rg16FloatStorageTexture && nativeFeatures.rg16FloatStorageTexture;
        caps.features.rgba16FloatStorageTexture =
            caps.features.rgba16FloatStorageTexture && nativeFeatures.rgba16FloatStorageTexture;
        caps.features.r32FloatSampledUpload = nativeFeatures.r32FloatSampledUpload;
        caps.features.sceneGeometryRayQuery =
            caps.features.sceneGeometryRayQuery && nativeFeatures.sceneGeometryRayQuery;
        caps.features.textureCompressionBc =
            (rhi::semantics::hasExecutableNativeFeature(desc_.capabilityContract,
                 rhi::semantics::NativeBackendFeature::TextureCompressionBc)
                || rhi::semantics::hasExecutableOptionalNativeFeature(desc_.capabilityContract,
                    rhi::semantics::NativeBackendFeature::TextureCompressionBc))
            && nativeFeatures.textureCompressionBc;
        // Metal lowers GPU-counted indirect through a bounded native indirect
        // replay plus the compiler-owned MetalGpuCountedIndirect/1 vertex ABI.
        // The device capability authorizes that substrate; pipeline creation
        // still rejects shaders without the reflected draw-id/count mask ABI.
        caps.features.multiDrawIndirectCount = caps.features.indirectDraw;
        caps.features.timelineSync = caps.features.timelineSync && nativeSync;
        caps.features.nativeSync = caps.features.nativeSync && nativeSync;

        {
            rhi::semantics::CommandWorkloadDesc workload;
            workload.label = "metal-backend-validation";
            workload.commandCount = 1;
            workload.usesRenderPass = true;
            workload.usesPushConstants = caps.features.pushConstants;
            workload.usesBarriers = caps.explicitFeatures.explicitBarriers;
            workload.colorAttachmentCount = 1;
            workload.usesDepthStencilAttachment = true;

            auto decision = rhi::semantics::lowerCommandWorkload(desc_.capabilityContract,
                workload,
                rhi::semantics::CommandRecordingIntent::PrimaryCommandBuffer);

            if (!decision.success) {
                rhi::RhiErrorHandler::get().report(
                    rhi::RhiSeverity::Error, decision.diagnostics.c_str());
            }
        }

        // Track A P2: push-constant lowering plan.
        {
            auto pcPlan = rhi::semantics::buildPushConstantLoweringPlan(
                rhi::Backend::Metal, desc_.capabilityContract);
            rhi::semantics::applyPushConstantLoweringPlan(caps, pcPlan);
            // Expose push constants only when the selected device contract
            // has the executable setBytes payload and this process actually
            // has an Apple Metal runtime. The non-Apple translation-unit
            // stubs are diagnostic-only and must not re-promote the feature.
            caps.features.pushConstants =
                pcPlan.hasExecutablePayload && runtime.runtimeAvailableOnThisPlatform;
        }

        if (!nativeAvailable)
            rhi::revokeUnavailableNativeDeviceCapabilities(caps);
        rhi::synchronizeExecutionStatuses(caps);
        rhi::synchronizeQueryExecutionStatuses(caps);

        return caps;
    }

    rhi::MemoryStats memoryStats() const override
    {
        auto stats = allocator_.memoryStats();
        stats.backendFallbacksHiddenFromUpperLayer = true;
        return stats;
    }

    rhi::semantics::PhysicalBudgetLedgerReport physicalBudgetLedger() const override
    {
        rhi::semantics::PhysicalBudgetLedgerReport report;
        report.backend = backend();

        const auto memory = memoryStats();
        if (memory.budgetCapacityBytes != 0u) {
            report.category(rhi::semantics::PhysicalBudgetCategory::ResourceAllocation) =
                rhi::semantics::observedPhysicalBudgetObservation(
                    rhi::semantics::PhysicalBudgetUnit::Bytes,
                    memory.budgetCapacityBytes,
                    memory.allocatedBytes,
                    memory.peakAllocatedBytes,
                    memory.budgetRejectionCount,
                    memory.liveResourceCount);
        }

        const auto pageSize = desc_.uploadStagingPageSizeBytes;
        const auto maxPageCount = static_cast<std::uint64_t>(desc_.uploadStagingMaxPageCount);
        if (pageSize != 0u && maxPageCount != 0u
            && pageSize <= std::numeric_limits<std::uint64_t>::max() / maxPageCount) {
            const auto staging = stagingUploadPool_.stats();
            if (staging.boundedCapacity) {
                report.category(rhi::semantics::PhysicalBudgetCategory::UploadStaging) =
                    rhi::semantics::observedPhysicalBudgetObservation(
                        rhi::semantics::PhysicalBudgetUnit::Bytes,
                        pageSize * maxPageCount,
                        staging.allocatedBytes,
                        staging.peakAllocatedBytes,
                        staging.failedAllocationCount,
                        staging.liveSliceCount,
                        staging.retiredSliceCount);
            }
        }

        const auto readback = textureReadback_.budgetStats();
        if (readback.configured) {
            report.category(rhi::semantics::PhysicalBudgetCategory::TextureReadback) =
                rhi::semantics::observedPhysicalBudgetObservation(
                    rhi::semantics::PhysicalBudgetUnit::Bytes,
                    readback.capacityBytes,
                    readback.chargedBytes,
                    readback.peakChargedBytes,
                    readback.budgetRejectionCount,
                    readback.pendingRequestCount,
                    0u,
                    rhi::semantics::PhysicalBudgetChargeRelation::OverlapsResourceAllocation);
        }

        // CommandCapacityReservation/1 is enforced by the Metal-owned shared
        // queue lease. Publish only its scalar state once the real native
        // completion/aliasing path is available; the non-Apple stub remains
        // Unknown rather than pretending that a queue has zero capacity.
        if (metalRhiNativeCompletionAndAliasingCommandsAvailable()) {
            const auto commands = inspectSharedMetalRhiCommandCapacity();
            if (commands.capacity != 0u) {
                report.category(rhi::semantics::PhysicalBudgetCategory::Command) =
                    rhi::semantics::observedPhysicalBudgetObservation(
                        rhi::semantics::PhysicalBudgetUnit::CommandBuffers,
                        commands.capacity,
                        commands.charged,
                        commands.peakCharged,
                        commands.rejectionCount,
                        commands.charged);
            }
        }
        if (descriptorCache_) {
            const auto descriptors = descriptorCache_->budgetStats();
            if (descriptors.configured) {
                report.category(rhi::semantics::PhysicalBudgetCategory::Descriptor) =
                    rhi::semantics::observedPhysicalBudgetObservation(
                        rhi::semantics::PhysicalBudgetUnit::Descriptors,
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
                report.category(rhi::semantics::PhysicalBudgetCategory::PipelineCache) =
                    rhi::semantics::observedPhysicalBudgetObservation(
                        rhi::semantics::PhysicalBudgetUnit::PipelineEntries,
                        pipelines.capacity,
                        pipelines.current,
                        pipelines.peak,
                        pipelines.rejectionCount,
                        pipelineCache_->entryCount());
            }
        }
        const auto directBudget = directObjectBudget_->stats();
        if (directBudget.configured) {
            const auto directTelemetry = directObjectTelemetry();
            report.category(rhi::semantics::PhysicalBudgetCategory::DirectPipelineObjects) =
                rhi::semantics::observedPhysicalBudgetObservation(
                    rhi::semantics::PhysicalBudgetUnit::PipelineEntries,
                    directBudget.pipelineObjectCapacity,
                    directBudget.currentPipelineObjects,
                    directBudget.peakPipelineObjects,
                    directBudget.pipelineRejectionCount,
                    directTelemetry.pipelineLayouts.liveObjects
                        + directTelemetry.renderPipelines.liveObjects
                        + directTelemetry.computePipelines.liveObjects);
            report.category(rhi::semantics::PhysicalBudgetCategory::DirectDescriptorArenas) =
                rhi::semantics::observedPhysicalBudgetObservation(
                    rhi::semantics::PhysicalBudgetUnit::Descriptors,
                    directBudget.descriptorCapacity,
                    directBudget.currentDescriptors,
                    directBudget.peakDescriptors,
                    directBudget.descriptorRejectionCount,
                    directTelemetry.descriptorArenas.liveObjects);
        }
        return report;
    }

    rhi::semantics::DirectObjectTelemetryReport directObjectTelemetry() const override
    {
        return directObjectTelemetry_->report(backend());
    }

    rhi::GpuTimingSample latestGpuTiming() const override
    {
        return latestMetalRhiNativeTimingSample();
    }

    rhi::semantics::UploadStagingReadiness uploadStagingReadiness() const override
    {
        const auto completed = stagingUploadPool_.completedSubmissionEpoch();
        return {
            rhi::semantics::UploadStagingReadinessStatus::Available,
            completed ? completed->load(std::memory_order_acquire) : 0u,
            nullptr,
        };
    }

    rhi::FrameCaptureCapability frameCaptureCapability(
        const rhi::FrameCaptureTool tool) const override
    {
        return metalRhiFrameCaptureCapability(tool);
    }

    rhi::FrameCaptureResult beginFrameCapture(const rhi::FrameCaptureRequest& request) override
    {
        if (frameCaptureActive_) {
            return rhi::unavailableFrameCaptureResult(
                "ARP-RHI-METAL-FRAME-CAPTURE-DEVICE-ALREADY-ACTIVE");
        }
        auto result = beginMetalRhiFrameCapture(request);
        frameCaptureActive_ = result.state == rhi::FrameCaptureState::Capturing;
        return result;
    }

    rhi::FrameCaptureResult endFrameCapture() override
    {
        if (!frameCaptureActive_) {
            return rhi::unavailableFrameCaptureResult(
                "ARP-RHI-METAL-FRAME-CAPTURE-DEVICE-NOT-ACTIVE");
        }
        auto result = endMetalRhiFrameCapture();
        if (result.success()) {
            frameCaptureActive_ = false;
        }
        return result;
    }

    rhi::GpuPipelineStatisticsSample latestPipelineStatistics() const override
    {
        if (!capabilities().features.pipelineStatisticsQueries) {
            rhi::GpuPipelineStatisticsSample sample;
            sample.availability = rhi::PipelineStatisticsAvailability::FeatureNotEnabled;
            sample.diagnostic =
                "Metal pipeline-statistics capability is not enabled for this device";
            return sample;
        }
        return latestMetalRhiNativePipelineStatistics();
    }

    rhi::GpuPipelineStatisticsSample readPipelineStatistics(
        const rhi::QueryPool& queryPool, std::uint32_t queryIndex) const override
    {
        rhi::GpuPipelineStatisticsSample sample;
        sample.queryIndex = queryIndex;
        const auto* metalPool = asMetalRhiQueryPool(queryPool);
        if (!metalPool || queryPool.type() != rhi::QueryType::PipelineStatistics
            || queryIndex >= queryPool.count()) {
            sample.availability = rhi::PipelineStatisticsAvailability::DiagnosticFailure;
            sample.diagnostic = "RHI-METAL-PIPELINE-STATISTICS-QUERY-IDENTITY-INVALID";
            return sample;
        }
        if (!capabilities().features.pipelineStatisticsQueries) {
            sample.availability = rhi::PipelineStatisticsAvailability::FeatureNotEnabled;
            sample.diagnostic =
                "Metal pipeline-statistics capability is not enabled for this device";
            return sample;
        }
        return readMetalRhiNativePipelineStatistics(metalPool->payload(), queryIndex);
    }

    void waitIdle() override
    {
        const auto runtime = describeMetalRhiNativeRuntimeBoundary();
        if (!runtime.runtimeAvailableOnThisPlatform) {
            return;
        }
        std::pmr::string diagnostics;
        if (waitMetalRhiNativeQueueIdle(diagnostics)) {
            if (const auto stats = queue_.nativeStats()) {
                stats->blockingWaitUsed = true;
            }
        } else if (!diagnostics.empty()) {
            rhi::RhiErrorHandler::get().report(rhi::RhiSeverity::Error, diagnostics.c_str());
        }
    }

    rhi::semantics::ShutdownDrainReport drainForShutdown(
        const rhi::semantics::ShutdownDrainRequest& request) override
    {
        const auto started = std::chrono::steady_clock::now();
        if (!request.validAt(started)) {
            return rhi::semantics::rejectedShutdownDrainReport(
                backend(), started, request, started);
        }

        rhi::semantics::ShutdownDrainReport report;
        report.backend = backend();
        const auto runtime = describeMetalRhiNativeRuntimeBoundary();
        if (!runtime.runtimeAvailableOnThisPlatform) {
            report.diagnosticId = "ARP-RHI-METAL-SHUTDOWN-DRAIN-NATIVE-RUNTIME-UNAVAILABLE";
            rhi::semantics::finalizeShutdownDrainTiming(
                report, started, request, std::chrono::steady_clock::now());
            return report;
        }

        std::pmr::string diagnostics;
        const auto state = drainMetalRhiNativeQueueForShutdown(request, diagnostics);
        const auto finished = std::chrono::steady_clock::now();
        switch (state) {
        case MetalRhiNativeQueueDrainState::Completed:
            report.outcome = rhi::semantics::ShutdownDrainOutcome::Native;
            report.state = rhi::semantics::ShutdownDrainState::Completed;
            if (const auto stats = queue_.nativeStats())
                stats->blockingWaitUsed = true;
            break;
        case MetalRhiNativeQueueDrainState::DeadlineExpired:
            report.state = rhi::semantics::ShutdownDrainState::DeadlineExpired;
            report.diagnosticId = rhi::semantics::kShutdownDrainDeadlineExpiredDiagnostic;
            break;
        case MetalRhiNativeQueueDrainState::DiagnosticFailure:
            report.diagnosticId =
                diagnostics.empty() ? "ARP-RHI-METAL-SHUTDOWN-DRAIN-FAILED" : diagnostics;
            break;
        }
        rhi::semantics::finalizeShutdownDrainTiming(report, started, request, finished);
        return report;
    }

    rhi::GpuQueue& queue() override { return queue_; }

    rhi::TexturePlacementRequirements queryTexturePlacementRequirements(
        const rhi::TextureDesc& textureDesc) override
    {
        std::uint64_t logicalBytes = 0u;
        const char* diagnostic = rhi::HeapPlacementContract::texture(textureDesc, logicalBytes);
        if (!diagnostic && !metalTransientAliasingContractAvailable(desc_.capabilityContract))
            diagnostic = "RHI-METAL-MEMORY-HEAP-ALIASING-CAPABILITY-UNAVAILABLE";
        rhi::TexturePlacementRequirements requirements;
        if (!diagnostic
            && queryMetalRhiNativeTexturePlacementRequirements(textureDesc, requirements)
            && requirements.sizeBytes && core::isPowerOfTwoUnsigned(requirements.alignmentBytes))
            return requirements;
        rhi::RhiErrorHandler::get().report(rhi::RhiSeverity::Error,
            diagnostic ? diagnostic : "RHI-METAL-HEAP-PLACEMENT-QUERY-FAILED");
        return {};
    }

    arp::core::UniquePtr<rhi::GpuBuffer> createBuffer(const rhi::BufferDesc& bufferDesc) override
    {
        if (!creationRuntimeAdmitted())
            return nullptr;

        const auto runtime = describeMetalRhiNativeRuntimeBoundary();
        std::shared_ptr<MetalRhiNativeBufferPayload> native;
        auto allocationBytes = bufferDesc.sizeBytes;
        if (runtime.runtimeAvailableOnThisPlatform) {
            native = createMetalRhiNativeBufferPayload(
                bufferDesc, metalRhiMemoryPolicyFor(bufferDesc.memory));
            if (!native)
                return {};
            const char* id = nullptr;
            allocationBytes =
                observeMetalRhiNativeBufferAllocation(native->resource, bufferDesc.sizeBytes, id);
            if (id || !allocationBytes)
                return {};
        }
        const auto allocation = allocator_.allocate({
            bufferDesc.label,
            allocationBytes,
            bufferDesc.memory,
        });
        if (!allocation.success) {
            return nullptr;
        }
        auto buffer = createMetalRhiBuffer(nextResourceId(),
            bufferDesc,
            metalRhiResourceAllocation(allocation),
            allocation.releaseToken,
            registry_,
            std::move(native));
        if (runtime.runtimeAvailableOnThisPlatform
            && (!buffer || !inspectMetalRhiNativePayload(*buffer).nativeRuntimeBacked)) {
            return nullptr;
        }
        if (buffer) {
            registry_->registerBuffer(*buffer);
        }
        return completeOriginalResourceCreation(std::move(buffer));
    }

    rhi::CommandValidationResult writeBuffer(rhi::GpuBuffer& buffer,
        const void* data,
        std::uint64_t sizeBytes,
        std::uint64_t offsetBytes = 0) override
    {
        if (buffer.hostWritesSealed())
            return {false, "RHI-BUFFER-HOST-WRITES-SEALED", 0};
        if (!data || sizeBytes == 0u || offsetBytes > buffer.desc().sizeBytes
            || sizeBytes > buffer.desc().sizeBytes - offsetBytes) {
            return {false, "Metal RHI buffer write desc is invalid", 0};
        }
        const auto nativeBuffer = metalRhiNativePayloadFor(buffer);
        std::pmr::string diagnostics;
        if (!writeMetalRhiNativeBuffer(
                nativeBuffer, data, sizeBytes, offsetBytes, buffer.desc().sizeBytes, diagnostics)) {
            return {false, diagnostics, 0};
        }
        return {true, "", 1};
    }

    arp::core::UniquePtr<rhi::GpuBufferView> createBufferView(
        const rhi::GpuBuffer& buffer, const rhi::BufferViewDesc& viewDesc) override
    {
        if (!creationRuntimeAdmitted())
            return nullptr;

        return createMetalRhiBufferView(nextResourceId(), buffer, viewDesc);
    }

    std::uint64_t queryOriginalContentTextureAllocationPayload(
        const rhi::TextureDesc& desc, const char*& diagnostic) override
    {
        if (!metalTextureCapabilityAllows(
                desc, desc_.capabilityContract, probeMetalRhiNativeFeatures())) {
            diagnostic = "RHI-METAL-CONTENT-CREATION-CAPABILITY";
            return 0u;
        }
        return queryMetalRhiOriginalContentAllocation(desc, diagnostic);
    }

    core::UniquePtr<rhi::GpuTexture> createOriginalContentTexturePayload(
        const rhi::TextureDesc& desc,
        const rhi::OriginalTextureContentCreationContract::Decision& decision,
        const rhi::OriginalTextureContentCreationBudget& budget,
        const char*& diagnostic) override
    {
        diagnostic = "RHI-METAL-CONTENT-CREATION-PROFILE";
        if (!rhi::OriginalTextureContentCreationContract::hasNativeAllocationBudget(
                decision.contractVersion)
            || !decision.exclusiveCreationAvailable
            || budget.contractVersion != decision.contractVersion)
            return {};
        const auto nativeBytes = queryOriginalContentTextureAllocationPayload(desc, diagnostic);
        if (!nativeBytes)
            return {};
        if (nativeBytes > budget.maxNativeAllocationBytes) {
            diagnostic = "RHI-METAL-CONTENT-CREATION-NATIVE-BUDGET";
            return {};
        }
        const bool observe = bool(budget.nativeAllocationObservation);
        if (observe) {
            const auto& intent = *budget.nativeAllocationObservation;
            if ((diagnostic = rhi::OriginalNativeAllocationObservationContract::validateIntent(
                     intent, budget.contractVersion, budget.maxNativeAllocationBytes)))
                return {};
            if (!rhi::OriginalNativeAllocationObservationContract::lowering(
                    rhi::Backend::Metal, intent.contractVersion)
                    .reserveThenObserve) {
                diagnostic = "RHI-NATIVE-ALLOCATION-OBSERVATION-METAL-PROFILE";
                return {};
            }
        }
        const auto reservedBytes = observe ? budget.maxNativeAllocationBytes : nativeBytes;
        auto allocation = allocator_.allocate({desc.label, reservedBytes, desc.memory});
        if (!allocation.success) {
            diagnostic = "RHI-METAL-CONTENT-CREATION-DEVICE-BUDGET";
            return {};
        }
        auto payload = createMetalRhiOriginalContentTexturePayload(desc);
        if (!payload) {
            diagnostic = "RHI-METAL-CONTENT-CREATION-NATIVE-PAYLOAD";
            return {};
        }
        if ((diagnostic = validateMetalRhiOriginalContentAllocation(
                 payload->resource, desc, reservedBytes)))
            return {};
        if (observe) {
            const auto actual = observeMetalRhiOriginalContentAllocation(
                payload->resource, desc, reservedBytes, diagnostic);
            if (!actual)
                return {};
            if (!allocator_.shrinkReservation(allocation, actual)) {
                diagnostic = "RHI-NATIVE-ALLOCATION-OBSERVATION-RESERVATION-OWNER";
                return {};
            }
        }
        payload->resource.allocationReleaseToken = allocation.releaseToken;
        auto texture = createMetalRhiTexture(nextResourceId(),
            desc,
            metalRhiResourceAllocation(allocation),
            std::move(payload),
            registry_);
        if (!texture) {
            diagnostic = "RHI-METAL-CONTENT-CREATION-WRAPPER";
            return {};
        }
        registry_->registerTexture(*texture);
        auto result = completeOriginalResourceCreation(std::move(texture));
        diagnostic = result ? nullptr : lastOriginalResourceRegistrationDiagnostic().data();
        return result;
    }

    arp::core::UniquePtr<rhi::GpuTexture> createTexture(
        const rhi::TextureDesc& textureDesc) override
    {
        if (!creationRuntimeAdmitted())
            return nullptr;

        const auto nativeFeatures = probeMetalRhiNativeFeatures();
        if (!metalTextureCapabilityAllows(textureDesc, desc_.capabilityContract, nativeFeatures)) {
            return nullptr;
        }
        const auto allocation = allocator_.allocate({
            textureDesc.label,
            estimateMetalRhiTextureBytes(textureDesc),
            textureDesc.memory,
        });
        if (!allocation.success) {
            return nullptr;
        }
        auto texture = createMetalRhiTexture(nextResourceId(),
            textureDesc,
            metalRhiResourceAllocation(allocation),
            allocation.releaseToken,
            registry_);
        const auto runtime = describeMetalRhiNativeRuntimeBoundary();
        if (runtime.runtimeAvailableOnThisPlatform
            && (!texture || !inspectMetalRhiNativePayload(*texture).nativeRuntimeBacked)) {
            return nullptr;
        }
        if (texture) {
            registry_->registerTexture(*texture);
        }
        return completeOriginalResourceCreation(std::move(texture));
    }

    arp::core::UniquePtr<rhi::GpuMemoryHeap> createMemoryHeap(
        std::uint64_t sizeBytes, std::uint64_t alignmentBytes, rhi::MemoryUsage usage) override
    {
        lastMemoryHeapCreationDiagnostic_.clear();
        if (const auto* diagnostic = originalMemoryHeapCreationDiagnostic()) {
            lastMemoryHeapCreationDiagnostic_ = diagnostic;
            return nullptr;
        }
        if (sizeBytes == 0) {
            lastMemoryHeapCreationDiagnostic_ = "RHI-METAL-MEMORY-HEAP-SIZE-ZERO";
            return nullptr;
        }
        if (!metalIsPowerOfTwo(alignmentBytes)) {
            lastMemoryHeapCreationDiagnostic_ = "RHI-METAL-MEMORY-HEAP-ALIGNMENT-INVALID";
            return nullptr;
        }
        if (const auto* diagnostic =
                rhi::HeapPlacementContract::heap(sizeBytes, alignmentBytes, usage)) {
            lastMemoryHeapCreationDiagnostic_ = diagnostic;
            return nullptr;
        }
        if (!metalTransientAliasingContractAvailable(desc_.capabilityContract)) {
            lastMemoryHeapCreationDiagnostic_ =
                "RHI-METAL-MEMORY-HEAP-ALIASING-CAPABILITY-UNAVAILABLE";
            return nullptr;
        }

        auto payload =
            createMetalRhiNativeHeap(sizeBytes, alignmentBytes, &lastMemoryHeapCreationDiagnostic_);
        if (!payload || !payload->resource.nativeRuntimeBacked) {
            // A Metal heap is an L2 aliasing contract.  Do not substitute a
            // size-only heap when native creation fails: callers must take the
            // advertised non-aliasing path instead.
            return nullptr;
        }

        const auto allocation = allocator_.allocate({
            std::pmr::string("MetalHeap"),
            payload->allocationSizeBytes,
            rhi::MemoryUsage::GpuOnly,
        });
        if (!allocation.success) {
            lastMemoryHeapCreationDiagnostic_ = "RHI-METAL-MEMORY-HEAP-BUDGET-REJECTED";
            return nullptr;
        }

        return arp::core::makeUnique<MetalMemoryHeap>(std::move(payload), allocation.releaseToken);
    }

    std::string_view lastMemoryHeapCreationDiagnostic() const override
    {
        return lastMemoryHeapCreationDiagnostic_;
    }

    /// Metal placed texture: creates a native texture from the placement heap at the
    /// given offset. The texture shares the heap's GPU memory with other
    /// transient textures in the same alias group.
    arp::core::UniquePtr<rhi::GpuTexture> createPlacedTexture(const rhi::TextureDesc& desc,
        const rhi::GpuMemoryHeap& heap,
        std::uint64_t offsetBytes) override
    {
        if (const auto* diagnostic = originalMemoryHeapPlacementDiagnostic(heap)) {
            rhi::RhiErrorHandler::get().report(rhi::RhiSeverity::Error, diagnostic);
            return nullptr;
        }
        std::uint64_t logicalBytes = 0u;
        if (const auto* diagnostic = rhi::HeapPlacementContract::texture(desc, logicalBytes)) {
            rhi::RhiErrorHandler::get().report(rhi::RhiSeverity::Error, diagnostic);
            return nullptr;
        }
        if (!metalTransientAliasingContractAvailable(desc_.capabilityContract)) {
            return nullptr;
        }
        const auto nativeFeatures = probeMetalRhiNativeFeatures();
        if (!metalTextureCapabilityAllows(desc, desc_.capabilityContract, nativeFeatures)) {
            return nullptr;
        }
        const auto* metalHeap = dynamic_cast<const MetalMemoryHeap*>(&heap);
        if (!metalHeap || !metalHeap->supportsAliasing()) {
            return nullptr;
        }

        rhi::TexturePlacementRequirements requirements;
        if (!queryMetalRhiNativeTexturePlacementRequirements(desc, requirements)
            || rhi::HeapPlacementContract::placement(
                metalHeap->sizeBytes(), metalHeap->alignmentBytes(), offsetBytes, requirements)) {
            rhi::RhiErrorHandler::get().report(
                rhi::RhiSeverity::Error, "RHI-METAL-HEAP-PLACEMENT-REJECTED");
            return nullptr;
        }

        auto payload =
            createPlacedMetalRhiNativeTexturePayload(metalHeap->payload(), desc, offsetBytes);
        if (!payload || !payload->resource.nativeRuntimeBacked) {
            return nullptr;
        }

        const rhi::ResourceAllocation allocation{
            rhi::ResourceMemoryClass::DeviceLocal,
            estimateMetalRhiTextureBytes(desc),
            false,
            true,
        };

        auto texture = createMetalRhiTexture(
            nextResourceId(), desc, allocation, std::move(payload), registry_);
        if (texture) {
            registry_->registerTexture(*texture);
        }
        return completeOriginalResourceCreation(std::move(texture));
    }

    arp::core::UniquePtr<rhi::GpuTextureView> createTextureView(
        const rhi::GpuTexture& texture, const rhi::TextureViewDesc& viewDesc) override
    {
        if (!creationRuntimeAdmitted())
            return nullptr;

        auto view = createMetalRhiTextureView(nextResourceId(), texture, viewDesc, registry_);
        const auto runtime = describeMetalRhiNativeRuntimeBoundary();
        if (runtime.runtimeAvailableOnThisPlatform
            && (!view || !inspectMetalRhiNativePayload(*view).nativeRuntimeBacked)) {
            return nullptr;
        }
        if (view) {
            registry_->registerTextureView(*view);
        }
        return completeOriginalResourceCreation(std::move(view), &texture);
    }

    arp::core::UniquePtr<rhi::GpuSampler> createSampler(
        const rhi::SamplerDesc& samplerDesc) override
    {
        if (!creationRuntimeAdmitted())
            return nullptr;

        const bool argumentBufferCompatible = desc_.enableArgumentBuffers
            && rhi::semantics::hasExecutableNativeFeature(
                desc_.capabilityContract, rhi::semantics::NativeBackendFeature::ArgumentBuffers);
        auto sampler = createMetalRhiSampler(
            nextResourceId(), samplerDesc, registry_, argumentBufferCompatible);
        const auto runtime = describeMetalRhiNativeRuntimeBoundary();
        if (runtime.runtimeAvailableOnThisPlatform
            && (!sampler || !inspectMetalRhiNativePayload(*sampler).nativeRuntimeBacked)) {
            return nullptr;
        }
        if (sampler) {
            registry_->registerSampler(*sampler);
        }
        return completeOriginalResourceCreation(std::move(sampler));
    }

    arp::core::UniquePtr<rhi::PipelineLayout> createPipelineLayout(
        const rhi::PipelineLayoutDesc& layoutDesc) override
    {
        if (!creationRuntimeAdmitted())
            return nullptr;

        auto reservation =
            directObjectBudget_->reserve(rhi::semantics::DirectObjectBudgetKind::PipelineLayout);
        if (!reservation.success)
            return nullptr;
        return createMetalRhiPipelineLayout(
            layoutDesc, directObjectTelemetry_, std::move(reservation.lease));
    }

    rhi::PipelineValidationReport validateRenderPipeline(
        const rhi::RenderPipelineDesc& pipelineDesc) override
    {
        return validateMetalRhiRenderPipeline(pipelineDesc, desc_.capabilityContract);
    }

    rhi::PipelineValidationReport validateComputePipeline(
        const rhi::ComputePipelineDesc& pipelineDesc) override
    {
        return validateMetalRhiComputePipeline(pipelineDesc, desc_.capabilityContract);
    }

    arp::core::UniquePtr<rhi::RenderPipeline> createRenderPipeline(
        const rhi::RenderPipelineDesc& pipelineDesc) override
    {
        if (!creationRuntimeAdmitted())
            return nullptr;

        // Capability lowering gate: run the L2 lowering decision (which
        // includes validateRenderPipelineState plus per-backend capability
        // checks such as the Vulkan depthBiasClamp feature) before delegating
        // to the backend pipeline builder. A descriptor the device cannot
        // lower must fail closed instead of producing a payload that silently
        // drops the unsupported state.
        const auto decision =
            rhi::semantics::lowerRenderPipelineState(desc_.capabilityContract, pipelineDesc);
        if (!decision.success) {
            rhi::RhiErrorHandler::get().report(
                rhi::RhiSeverity::Error, decision.diagnostics.c_str());
            return nullptr;
        }
        auto reservation =
            directObjectBudget_->reserve(rhi::semantics::DirectObjectBudgetKind::RenderPipeline);
        if (!reservation.success)
            return nullptr;
        auto pipeline = createMetalRhiRenderPipeline(pipelineDesc,
            desc_.capabilityContract,
            directObjectTelemetry_,
            std::move(reservation.lease));
        const auto runtime = describeMetalRhiNativeRuntimeBoundary();
        if (runtime.runtimeAvailableOnThisPlatform
            && (!pipeline || !inspectMetalRhiPipelinePayload(*pipeline).nativeRuntimeBacked)) {
            return nullptr;
        }
        return pipeline;
    }

    arp::core::UniquePtr<rhi::ComputePipeline> createComputePipeline(
        const rhi::ComputePipelineDesc& pipelineDesc) override
    {
        if (!creationRuntimeAdmitted())
            return nullptr;

        // Capability lowering gate: run the L2 compute lowering decision
        // (which includes validateComputePipelineState) before delegating to
        // the backend pipeline builder.
        const auto decision =
            rhi::semantics::lowerComputePipelineState(desc_.capabilityContract, pipelineDesc);
        if (!decision.success) {
            rhi::RhiErrorHandler::get().report(
                rhi::RhiSeverity::Error, decision.diagnostics.c_str());
            return nullptr;
        }
        auto reservation =
            directObjectBudget_->reserve(rhi::semantics::DirectObjectBudgetKind::ComputePipeline);
        if (!reservation.success)
            return nullptr;
        auto pipeline = createMetalRhiComputePipeline(pipelineDesc,
            desc_.capabilityContract,
            directObjectTelemetry_,
            std::move(reservation.lease));
        const auto runtime = describeMetalRhiNativeRuntimeBoundary();
        if (runtime.runtimeAvailableOnThisPlatform
            && (!pipeline || !inspectMetalRhiPipelinePayload(*pipeline).nativeRuntimeBacked)) {
            return nullptr;
        }
        return pipeline;
    }

    arp::core::UniquePtr<rhi::DescriptorArena> createDescriptorArena(
        const rhi::DescriptorArenaDesc& arenaDesc) override
    {
        if (!creationRuntimeAdmitted())
            return nullptr;

        auto reservation = directObjectBudget_->reserve(
            rhi::semantics::DirectObjectBudgetKind::DescriptorArena, arenaDesc.maxDescriptors);
        if (!reservation.success)
            return nullptr;
        return createMetalRhiDescriptorArena(
            arenaDesc, registry_, directObjectTelemetry_, std::move(reservation.lease));
    }

    arp::core::UniquePtr<rhi::CommandEncoder> createCommandEncoder(
        const rhi::CommandEncoderDesc& commandDesc) override
    {
        if (!creationRuntimeAdmitted())
            return nullptr;

        return createMetalRhiCommandEncoder(commandDesc,
            desc_.capabilityContract,
            &stagingUploadPool_,
            registry_,
            &bindless_.capabilities(),
            &bindless_);
    }

    arp::core::UniquePtr<rhi::SecondaryCommandEncoder> createSecondaryCommandEncoder(
        const rhi::SecondaryCommandEncoderDesc& desc) override
    {
        if (!creationRuntimeAdmitted())
            return nullptr;

        return createMetalRhiSecondaryCommandEncoder(desc, desc_.capabilityContract);
    }

    arp::core::UniquePtr<rhi::RenderBundleEncoder> createRenderBundleEncoder(
        const rhi::RenderBundleDesc& desc) override
    {
        if (!creationRuntimeAdmitted())
            return nullptr;

        return createMetalRhiRenderBundleEncoder(desc, desc_.capabilityContract);
    }

    arp::core::UniquePtr<rhi::Swapchain> createSwapchain(
        const rhi::SwapchainDesc& swapchainDesc) override
    {
        if (!creationRuntimeAdmitted())
            return nullptr;

        return createMetalRhiSwapchain(swapchainDesc, swapchainStats_, registry_);
    }

    arp::core::UniquePtr<rhi::GpuFence> createFence() override
    {
        if (!creationRuntimeAdmitted())
            return nullptr;

        return arp::core::makeUnique<MetalCompletionFence>();
    }

    rhi::ResourceBarrierResult prepareTextureForPoolReuse(rhi::GpuTexture& texture) override
    {
        return prepareMetalRhiTextureForPoolReuse(texture);
    }

    rhi::ResourceBarrierResult prepareBufferForPoolReuse(rhi::GpuBuffer& buffer) override
    {
        return prepareMetalRhiBufferForPoolReuse(buffer);
    }

    arp::core::UniquePtr<rhi::GpuSemaphore> createSemaphore() override
    {
        if (!creationRuntimeAdmitted())
            return nullptr;

        if (!rhi::semantics::hasExecutableNativeFeature(desc_.capabilityContract,
                rhi::semantics::NativeBackendFeature::TimelineSynchronization)
            || !rhi::semantics::hasExecutableNativeFeature(
                desc_.capabilityContract, rhi::semantics::NativeBackendFeature::NativeSync)
            || !metalRhiNativeTimelineSynchronizationAvailable()) {
            return nullptr;
        }
        auto payload = createMetalRhiNativeSharedEventPayload("MetalBinarySemaphore");
        return payload && payload->nativeRuntimeBacked
            ? arp::core::makeUnique<MetalRhiBinarySemaphore>(std::move(payload))
            : nullptr;
    }

    arp::core::UniquePtr<rhi::GpuTimelineSemaphore> createTimelineSemaphore(
        uint64_t initialValue = 0) override
    {
        if (!creationRuntimeAdmitted())
            return nullptr;

        if (!rhi::semantics::hasExecutableNativeFeature(desc_.capabilityContract,
                rhi::semantics::NativeBackendFeature::TimelineSynchronization)
            || !rhi::semantics::hasExecutableNativeFeature(
                desc_.capabilityContract, rhi::semantics::NativeBackendFeature::NativeSync)
            || !metalRhiNativeTimelineSynchronizationAvailable()) {
            return nullptr;
        }
        auto payload = createMetalRhiNativeSharedEventPayload("MetalTimelineSemaphore");
        return payload && payload->nativeRuntimeBacked
            ? arp::core::makeUnique<MetalRhiTimelineSemaphore>(std::move(payload), initialValue)
            : nullptr;
    }

    arp::core::UniquePtr<rhi::QueryPool> createQueryPool(const rhi::QueryPoolDesc& desc) override
    {
        if (!creationRuntimeAdmitted())
            return nullptr;

        return createMetalRhiQueryPool(desc, desc_.capabilityContract);
    }

    rhi::PipelineCache& pipelineCache() override
    {
        if (!pipelineCache_) {
            pipelineCache_ = arp::core::makeUnique<rhi::PipelineCache>(*this);
        }
        return *pipelineCache_;
    }

    rhi::DescriptorCache& descriptorCache() override
    {
        if (!descriptorCache_) {
            descriptorCache_ = arp::core::makeUnique<rhi::DescriptorCache>(*this);
        }
        return *descriptorCache_;
    }

private:
    template <class Resource, class Payload>
    const char* validateResultLossResource(
        const Resource& object, const std::shared_ptr<Payload>& payload) const noexcept
    {
        const auto nativeDevice = metalRhiNativeDeviceIdentity();
        const auto registered = registry_ ? registry_->find(object.id()) : std::nullopt;
        return nativeDevice && payload && registered && payload->resource.nativeRuntimeBacked
                && !payload->resource.nativeRuntimeDiagnosticOnly && payload->resource.nativeObject
                && core::SharedOwnerProjection::sameOwner(
                    payload->resource.nativeDevice, nativeDevice)
                && core::SharedOwnerProjection::sameOwner(payload, registered->payloadOwner())
            ? nullptr
            : "RHI-METAL-RESULT-LOSS-RESOURCE-SOURCE";
    }

    const char* validateOriginalNativeLossResource(
        const rhi::GpuBuffer& object) const noexcept override
    {
        return validateResultLossResource(object, metalRhiNativePayloadFor(object));
    }

    const char* validateOriginalNativeLossResource(
        const rhi::GpuTexture& object) const noexcept override
    {
        return validateResultLossResource(object, metalRhiNativePayloadFor(object));
    }

    const char* validateOriginalNativeLossResource(
        const rhi::GpuSampler& object) const noexcept override
    {
        return validateResultLossResource(object, metalRhiNativePayloadFor(object));
    }

    const char* validateOriginalNativeLossResource(
        const rhi::GpuTextureView& object) const noexcept override
    {
        if (const auto* id = validateResultLossResource(object, metalRhiNativePayloadFor(object)))
            return id;
        const auto view = registry_->find(object.id());
        const auto parent = registry_->find(object.textureId());
        return view && view->parentTextureLive && view->parentTexture == object.textureId()
                && parent && parent->texture && parent->textureWrapper.get()
                && !validateResultLossResource(*parent->textureWrapper.get(), parent->texture)
            ? nullptr
            : "RHI-METAL-RESULT-LOSS-VIEW-PARENT";
    }

    const char* validateOriginalNativeLossDescriptor(
        const rhi::DescriptorArena& arena) const noexcept override
    {
        return metalRhiNativeDeviceIdentity() && metalRhiDescriptorRegistryMatches(arena, registry_)
            ? nullptr
            : "RHI-METAL-RESULT-LOSS-ARENA-SOURCE";
    }

    const char* validateOriginalNativeLossDescriptor(
        const rhi::DescriptorSet& set) const noexcept override
    {
        return metalRhiNativeDeviceIdentity() && metalRhiDescriptorRegistryMatches(set, registry_)
            ? nullptr
            : "RHI-METAL-RESULT-LOSS-SET-SOURCE";
    }

    std::shared_ptr<rhi::OriginalDeviceNativeLossBinding> bindOriginalDeviceNativeResultLoss(
        const rhi::OriginalDeviceLifetime::NativeLossReceiver& receiver,
        const rhi::OriginalDeviceNativeResultLossBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic) override
    {
        return MetalOriginalDeviceNativeResultLoss::prepare(
            sharedMetalRhiOriginalResultLossSource(),
            receiver,
            budget,
            std::move(quota),
            diagnostic);
    }

    rhi::GpuResourceId nextResourceId() { return rhi::allocateGpuResourceId(); }

    bool creationRuntimeAdmitted() const
    {
        if (hostDiagnostic_
            != describeMetalRhiNativeRuntimeBoundary().runtimeAvailableOnThisPlatform)
            return true;
        rhi::RhiErrorHandler::get().report(
            rhi::RhiSeverity::Error, "RHI-METAL-NATIVE-DEVICE-UNAVAILABLE");
        return false;
    }

    MetalRhiDeviceDesc desc_;
    const bool hostDiagnostic_;
    std::pmr::string lastMemoryHeapCreationDiagnostic_;
    MetalRhiAllocator allocator_;
    MetalRhiStagingUploadPool stagingUploadPool_;
    std::shared_ptr<MetalRhiNativeQueueStats> queueStats_ = createMetalRhiNativeQueueStats();
    MetalRhiQueue queue_{queueStats_};
    std::shared_ptr<MetalRhiNativeSwapchainStats> swapchainStats_ =
        createMetalRhiNativeSwapchainStats();
    std::shared_ptr<MetalRhiResourceRegistry> registry_ =
        arp::core::makeShared<MetalRhiResourceRegistry>();
    std::shared_ptr<rhi::semantics::DirectObjectTelemetryTracker> directObjectTelemetry_ =
        arp::core::makeShared<rhi::semantics::DirectObjectTelemetryTracker>();
    std::shared_ptr<rhi::semantics::DirectObjectBudgetTracker> directObjectBudget_ =
        arp::core::makeShared<rhi::semantics::DirectObjectBudgetTracker>(desc_.directObjectBudget);
    arp::core::UniquePtr<rhi::PipelineCache> pipelineCache_;
    arp::core::UniquePtr<rhi::DescriptorCache> descriptorCache_;
    MetalTextureReadback textureReadback_{*this, registry_};
    MetalBindlessCrossExtension bindless_;
    bool frameCaptureActive_ = false;

public:
    MetalRhiNativeSwapchainStats swapchainStats() const
    {
        return swapchainStats_ ? *swapchainStats_ : MetalRhiNativeSwapchainStats{};
    }
};

} // namespace

std::shared_ptr<MetalRhiResourceRegistry> metalRhiIssuingResourceRegistry(
    const rhi::GpuDevice& device) noexcept
{
    const auto* concrete = dynamic_cast<const MetalRhiDevice*>(&device);
    return concrete ? concrete->resourceRegistry() : nullptr;
}

MetalRhiDeviceReport describeMetalRhiDevice(const MetalRhiDeviceDesc& desc)
{
    MetalRhiDeviceReport report;
    report.backend = rhi::Backend::Metal;
    if (desc.enableBindlessComputeImages
        && (!desc.enableBindlessCompute || !desc.enableArgumentBuffers)) {
        report.diagnostics = "RHI-METAL-BINDLESS-COMPUTE-IMAGES-BASE-OPT-IN-REQUIRED";
        return report;
    }
    const auto nativeFeatures = probeMetalRhiNativeFeatures();
    // Descriptor sets retain the direct-binding model; descriptor tables use
    // the separate MetalArgumentBuffer/1 lowering. Report the native
    // substrate truthfully rather than conflating the two models.
    report.argumentBuffersIntegrated = desc.enableArgumentBuffers && nativeFeatures.argumentBuffers;
    report.argumentBindingsHiddenFromPublicApi = true;
    // Metal lowers the L2 atomic push-constant block through set*Bytes. It
    // does not allocate an upload buffer, so this telemetry must not claim
    // backend upload-buffer emulation.
    report.pushConstantUploadBufferEmulated = false;
    report.memoryIntentMapped = desc.enableMemorylessTransientTextures;
    report.memoryBudgetTelemetrySanitized = desc.memoryBudgetBytes > 0;
    report.drawableLifecycleGuarded = desc.enableDrawableLifecycleGuards;
    report.boundedDrawableSynchronization = !desc.allowBlockingDrawableAcquire;
    report.queueSubmitSupported = true;
    report.swapchainDrawableLifecycleSupported = desc.enableDrawableLifecycleGuards;
    report.nullDelegateUsed = false;
    report.backendOwnedDevice = true;
    report.backendOwnedResourceObjects = true;
    report.backendOwnedDescriptorObjects = true;
    report.backendOwnedPipelineObjects = true;
    report.backendOwnedCommandObjects = true;
    report.backendOwnedQueueSubmit = true;
    report.backendOwnedSwapchain = true;
    report.backendOwnedUploadStagingSupported =
        describeMetalRhiNativeRuntimeBoundary().runtimeAvailableOnThisPlatform;
    report.uploadStagingNativeHandlesHidden = true;
    report.uploadStagingBounded =
        desc.uploadStagingPageSizeBytes != 0u && desc.uploadStagingMaxPageCount != 0u;
    report.nativeResourcePayloadsPrivate = true;
    report.nativeBufferCreationPathPresent = true;
    report.nativeTextureCreationPathPresent = true;
    report.nativeTextureViewCreationPathPresent = true;
    report.nativeSamplerCreationPathPresent = true;
    report.privateStorageIntentMapped =
        metalRhiNativeStorageMode(MetalRhiMemoryPolicy::PrivateStorage)
        == MetalRhiNativeStorageMode::Private;
    report.sharedStorageIntentMapped = metalRhiNativeStorageMode(MetalRhiMemoryPolicy::SharedUpload)
        == MetalRhiNativeStorageMode::Shared;
    // GpuToCpu is a logical readback intent. On iOS-family devices Metal
    // maps it to Shared because Managed storage is unavailable; that is still
    // an executable mapping of the RHI intent and must not be reported as a
    // missing policy.
    const auto managedReadbackMode =
        metalRhiNativeStorageMode(MetalRhiMemoryPolicy::ManagedReadback);
    report.managedStorageIntentMapped = managedReadbackMode == MetalRhiNativeStorageMode::Managed
        || managedReadbackMode == MetalRhiNativeStorageMode::Shared;
    report.memorylessStorageIntentMapped =
        metalRhiNativeStorageMode(MetalRhiMemoryPolicy::MemorylessTransient)
        == MetalRhiNativeStorageMode::Memoryless;
    const auto nativeRuntime = describeMetalRhiNativeRuntimeBoundary();
    report.nonAppleNativeResourceRuntimeDiagnosticOnly =
        !nativeRuntime.runtimeAvailableOnThisPlatform;
    report.nativeArgumentBindingPayloadsPrivate = true;
    report.nativeArgumentBufferCreationPathPresent =
        desc.enableArgumentBuffers && nativeFeatures.argumentBuffers;
    report.uniformBufferArgumentBindingSupported = true;
    report.storageBufferArgumentBindingSupported = true;
    report.sampledTextureArgumentBindingSupported = true;
    report.storageTextureArgumentBindingSupported = true;
    report.samplerArgumentBindingSupported = true;
    report.nonAppleNativeDescriptorRuntimeDiagnosticOnly =
        !nativeRuntime.runtimeAvailableOnThisPlatform;
    report.nativeShaderModuleCreationPathPresent = true;
    report.nativeRenderPipelineCreationPathPresent = true;
    report.nativeComputePipelineCreationPathPresent = true;
    report.nonAppleNativePipelineRuntimeDiagnosticOnly =
        !nativeRuntime.runtimeAvailableOnThisPlatform;
    report.nativeCommandBufferCreationPathPresent = true;
    report.nativeRenderEncoderPathPresent = true;
    report.nativeComputeEncoderPathPresent = true;
    report.nativeBlitEncoderPathPresent = true;
    report.nativeDescriptorArgumentBindingRecordPathPresent = true;
    report.nativePushConstantUploadBufferPathPresent = false;
    report.nativePushConstantSetBytesPathPresent = desc.enablePushConstantSetBytes;
    report.nativeQueueSubmitPathPresent = true;
    report.boundedCompletionTelemetryPresent = true;
    report.nonAppleNativeCommandRuntimeDiagnosticOnly =
        !nativeRuntime.runtimeAvailableOnThisPlatform;
    report.nativeDrawableLayerCreationPathPresent = true;
    report.nativeDrawableAcquirePathPresent = true;
    report.nativeDrawablePresentPathPresent = true;
    report.nativeDrawableResizePathPresent = true;
    report.nativeDrawableLayerLossRecreatePathPresent = true;
    report.nativeDrawableBackgroundForegroundPathPresent = true;
    report.nativeDrawableMemoryWarningPathPresent = true;
    report.nativeDrawableTwoFrameSlotsPresent = true;
    report.nonAppleNativeSwapchainRuntimeDiagnosticOnly =
        !nativeRuntime.runtimeAvailableOnThisPlatform;
#if defined(__MINGW32__) || defined(__MINGW64__)
    report.windowsMingwNativeRuntimeDiagnosticOnly = !nativeRuntime.runtimeAvailableOnThisPlatform;
    report.windowsMingwNativeExecutionSuppressed = true;
    report.windowsMingwSourceGuardrailsEnforced = true;
    report.windowsMingwNoNullDelegateGuardrailEnforced = !report.nullDelegateUsed;
#endif
    report.nativeHandleExposedToUpperLayer = desc.allowNativeHandleExposureToUpperLayers;
    report.perDrawPipelineCreationAllowed = desc.allowPerDrawPipelineCreation;
    report.perFrameShaderCompilationAllowed = desc.allowPerFrameShaderCompilation;

    if (!desc.capabilityContract.queriedFromNativeDevice
        || !desc.capabilityContract.queriedExtendedFeatures) {
        report.diagnostics = "Metal RHI device requires a queried native capability contract";
        return report;
    }
    if (nativeRuntime.runtimeAvailableOnThisPlatform) {
        std::pmr::string capabilityDiagnostics;
        if (!metalCapabilityContractMatchesProbe(
                desc.capabilityContract, nativeFeatures, capabilityDiagnostics)) {
            report.diagnostics = capabilityDiagnostics;
            return report;
        }
    }
    if (!desc.enablePushConstantSetBytes) {
        report.diagnostics =
            "Metal RHI device requires the executable setBytes push-constant lowering";
        return report;
    }
    if (!rhi::semantics::hasExecutableNativeFeature(
            desc.capabilityContract, rhi::semantics::NativeBackendFeature::TileMemoryHints)
        || !desc.enableMemorylessTransientTextures) {
        report.diagnostics = "Metal RHI device requires memoryless transient texture support";
        return report;
    }
    if (!desc.enableDrawableLifecycleGuards) {
        report.diagnostics = "Metal RHI device requires drawable lifecycle guards";
        return report;
    }
    if (desc.allowBlockingDrawableAcquire) {
        report.diagnostics = "Metal RHI device must use bounded drawable synchronization";
        return report;
    }
    if (desc.uploadStagingPageSizeBytes == 0u || desc.uploadStagingMaxPageCount == 0u) {
        report.diagnostics = "Metal RHI device requires a bounded upload staging pool budget";
        return report;
    }
    if (desc.directObjectBudget.configured() && !desc.directObjectBudget.valid()) {
        report.diagnostics = "RHI-DIRECT-OBJECT-BUDGET-INVALID";
        return report;
    }

    const auto invariants = rhi::validateSharedDeviceInvariants(rhi::Backend::Metal,
        "Metal",
        desc.capabilityContract.backend,
        desc.memoryBudgetBytes,
        desc.allowNativeHandleExposureToUpperLayers,
        desc.allowPerDrawPipelineCreation,
        desc.allowPerFrameShaderCompilation);
    if (!invariants.success) {
        report.diagnostics = invariants.diagnostics;
        return report;
    }
    report.nativeHandleExposedToUpperLayer = invariants.nativeHandleExposedToUpperLayer;
    report.perDrawPipelineCreationAllowed = invariants.perDrawPipelineCreationAllowed;
    report.perFrameShaderCompilationAllowed = invariants.perFrameShaderCompilationAllowed;

    report.success = true;
    return report;
}

MetalRhiDrawableLifecycleReport describeMetalRhiDrawableLifecycle(
    const MetalRhiDeviceDesc& desc, MetalRhiDrawableLifecycleEvent event)
{
    MetalRhiDrawableLifecycleReport report;
    report.backend = rhi::Backend::Metal;
    const auto deviceReport = describeMetalRhiDevice(desc);
    if (!deviceReport.success) {
        report.diagnostics = deviceReport.diagnostics;
        return report;
    }

    switch (event) {
    case MetalRhiDrawableLifecycleEvent::Resize:
        report.resizeHandled = true;
        report.acquireAllowed = true;
        report.presentAllowed = true;
        report.swapchainRebuildRequired = true;
        report.releaseDrawableResources = true;
        break;
    case MetalRhiDrawableLifecycleEvent::LayerLost:
        report.layerLossHandled = true;
        report.acquireAllowed = false;
        report.presentAllowed = false;
        report.swapchainRebuildRequired = true;
        report.releaseDrawableResources = true;
        break;
    case MetalRhiDrawableLifecycleEvent::LayerRestored:
        report.layerRecreateHandled = true;
        report.acquireAllowed = true;
        report.presentAllowed = true;
        report.swapchainRebuildRequired = true;
        report.releaseDrawableResources = false;
        break;
    case MetalRhiDrawableLifecycleEvent::BackgroundPause:
        report.backgroundPauseHandled = true;
        report.acquireAllowed = false;
        report.presentAllowed = false;
        report.swapchainRebuildRequired = false;
        report.releaseDrawableResources = true;
        break;
    case MetalRhiDrawableLifecycleEvent::ForegroundResume:
        report.foregroundResumeHandled = true;
        report.acquireAllowed = true;
        report.presentAllowed = true;
        report.swapchainRebuildRequired = true;
        report.releaseDrawableResources = false;
        break;
    case MetalRhiDrawableLifecycleEvent::MemoryWarning:
        report.memoryWarningHandled = true;
        report.acquireAllowed = true;
        report.presentAllowed = true;
        report.swapchainRebuildRequired = false;
        report.releaseDrawableResources = true;
        break;
    }

    report.success = true;
    return report;
}

arp::core::UniquePtr<rhi::GpuDevice> createMetalRhiDevice(const MetalRhiDeviceDesc& desc)
{
    if (!describeMetalRhiNativeRuntimeBoundary().runtimeAvailableOnThisPlatform) {
        rhi::RhiErrorHandler::get().report(
            rhi::RhiSeverity::Error, "RHI-METAL-NATIVE-DEVICE-UNAVAILABLE");
        return nullptr;
    }
    if (!describeMetalRhiDevice(desc).success) {
        return nullptr;
    }
    return arp::core::makeUnique<MetalRhiDevice>(desc);
}

arp::core::UniquePtr<rhi::GpuDevice> createMetalRhiHostDiagnosticDevice(
    const MetalRhiDeviceDesc& desc, std::uint32_t contractVersion)
{
    if (contractVersion != 1u) {
        rhi::RhiErrorHandler::get().report(
            rhi::RhiSeverity::Error, "RHI-HOST-DIAGNOSTIC-DEVICE-VERSION");
        return nullptr;
    }
    if (describeMetalRhiNativeRuntimeBoundary().runtimeAvailableOnThisPlatform) {
        rhi::RhiErrorHandler::get().report(
            rhi::RhiSeverity::Error, "RHI-HOST-DIAGNOSTIC-DEVICE-NATIVE-RUNTIME");
        return nullptr;
    }
    if (!describeMetalRhiDevice(desc).success)
        return nullptr;
    return arp::core::makeUnique<MetalRhiDevice>(desc, true);
}

MetalRhiNativeQueueStats inspectMetalRhiNativeQueueStats(const rhi::GpuQueue& queue)
{
    const auto* metalQueue = dynamic_cast<const MetalRhiQueue*>(&queue);
    return metalQueue && metalQueue->nativeStats() ? *metalQueue->nativeStats()
                                                   : MetalRhiNativeQueueStats{};
}

MetalRhiNativeSwapchainStats inspectMetalRhiNativeSwapchainStats(const rhi::GpuDevice& device)
{
    const auto* metalDevice = dynamic_cast<const MetalRhiDevice*>(&device);
    return metalDevice ? metalDevice->swapchainStats() : MetalRhiNativeSwapchainStats{};
}

} // namespace arp::rhi_metal

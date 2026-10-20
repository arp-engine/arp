#pragma once

#include "core/api.h"
#include "core/cpu_allocator.h"
#include "renderer/render_graph/render_graph_types.h"
#include "renderer/render_graph/transient_resource_report.h"
#include "rhi/gpu_commands.h"
#include "rhi/semantics/fallback_plan.h"
#include "rhi/semantics/gpu_timing_lowering.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace arp::rhi {
class GpuDevice;
class GpuBuffer;
class GpuTexture;
class GpuTextureView;
class DescriptorSet;
class DescriptorTable;
struct ShaderPackageDesc;
struct PipelineLayoutDesc;
} // namespace arp::rhi

namespace arp::render_graph {

class CompiledFrameGraph;
class FrameGraphUniformRingBuffer;

constexpr std::uint32_t kMaxRhiPassDescriptorLayoutNames = 8;

struct RhiBarrierRecord {
    std::pmr::string resourceName;
    std::pmr::string passName;
    ResourceKind resourceKind = ResourceKind::Texture;
    rhi::ResourceState beforeState = rhi::ResourceState::Undefined;
    rhi::ResourceState afterState = rhi::ResourceState::Undefined;
    rhi::PipelineStage sourceStages = rhi::PipelineStage::None;
    rhi::PipelineStage destinationStages = rhi::PipelineStage::None;
    rhi::ResourceAccess sourceAccess = rhi::ResourceAccess::None;
    rhi::ResourceAccess destinationAccess = rhi::ResourceAccess::None;
    rhi::TextureAspect aspect = rhi::TextureAspect::Color;
    std::uint32_t handleId = 0;
    bool backendConsumable = false;
    bool finalPresentState = false;
    bool memoryOnly = false;
};

struct RhiAliasRecord {
    std::pmr::string resourceName;
    ResourceKind resourceKind = ResourceKind::Texture;
    std::uint32_t handleId = 0;
    std::uint32_t aliasGroup = 0;
    bool backendOwnedTransient = true;
};

struct RhiAttachmentRecord {
    std::pmr::string passName;
    std::pmr::string resourceName;
    rhi::ResourceState state = rhi::ResourceState::Undefined;
    rhi::LoadOp loadOp = rhi::LoadOp::Load;
    rhi::StoreOp storeOp = rhi::StoreOp::Store;
    MobileTileHints tileHints;
    bool transient = false;
    bool imported = false;
    bool externalProtected = false;
    bool finalPresentState = false;
};

struct RhiQueueRecord {
    std::pmr::string passName;
    PassKind kind = PassKind::Graphics;
    QueueRole queue = QueueRole::Graphics;
    bool asyncComputeEligible = false;
};

struct RhiExternalResourceRecord {
    std::pmr::string resourceName;
    ResourceKind kind = ResourceKind::Texture;
    std::uint32_t handleId = 0;
    rhi::ResourceState initialState = rhi::ResourceState::Undefined;
    bool externalProtected = false;
    bool importedBackbuffer = false;
    bool finalPresentStateRequired = false;
};

struct RhiPassRecord {
    std::pmr::string name;
    PassKind kind = PassKind::Graphics;
    QueueRole queue = QueueRole::Graphics;
    std::uint32_t drawCommandCount = 0;
    std::uint32_t dispatchCount = 0;
    std::uint32_t transferCommandCount = 0;
    std::pmr::vector<RhiDrawIndexedCommand> drawCommands;
    std::pmr::vector<RhiDispatchCommand> dispatchCommands;
    std::pmr::vector<RhiTransferCommand> transferCommands;
    std::uint32_t pushConstantSizeBytes = 0;
    std::uint32_t descriptorSetCount = 0;
    std::uint32_t descriptorTableCount = 0;
    std::uint32_t descriptorLayoutNameCount = 0;
    std::array<std::pmr::string, kMaxRhiPassDescriptorLayoutNames> descriptorLayoutNames{};
    std::pmr::string pipelineLayoutName;
    std::uint32_t pipelinePushConstantCapacityBytes = 0;
    std::uint32_t requiredMaterialResourceCount = 0;
    std::uint32_t materialResourceCount = 0;
    bool usesStorageTextureBinding = false;
    bool hasDepthTarget = false;
    bool requiresBackendOwnedVertexUpload = false;
    std::uint32_t maxUploadBytes = 0;
};

struct RhiTextureUploadRecord {
    std::pmr::string resourceName;
    std::uint32_t bytesPerRow = 0;
    std::uint32_t rowsPerImage = 0;
    rhi::Extent3D extent;
    rhi::StagingIntent stagingIntent = rhi::StagingIntent::Upload;
    bool backendOwnedStaging = true;
};

struct RhiFramePlan {
    std::uint32_t frameSlot = 0;
    rhi::EngineFeatureRequirementSet featureRequirements;
    /// Device-selected lowering outcomes are copied into the frame handoff
    /// without exposing native handles or backend vocabulary. Diagnostic
    /// failures remain observable but never become executable plans.
    std::pmr::vector<rhi::EngineFeatureExecutionStatus> executionStatuses;
    /// Query lifecycle capability snapshots travel with the frame plan;
    /// per-pool execution may refine them to Recorded/Pending/Available.
    std::pmr::vector<rhi::QueryExecutionStatus> queryExecutionStatuses;
    rhi::ExecutionStatusTelemetrySummary statusTelemetry;
    std::pmr::vector<RhiBarrierRecord> barriers;
    std::pmr::vector<RhiAliasRecord> aliases;
    std::pmr::vector<RhiAttachmentRecord> attachments;
    std::pmr::vector<RhiQueueRecord> queueRecords;
    std::pmr::vector<RhiExternalResourceRecord> externalResources;
    std::pmr::vector<RhiPassRecord> passes;
    std::pmr::vector<RhiTextureUploadRecord> textureUploads;
};

struct RhiFramePlanBuilderDesc {
    std::uint32_t logicalFrameSlots = 2;
    std::uint32_t maxFeatureRequirements = 0;
    std::uint32_t maxBarriers = 0;
    std::uint32_t maxPasses = 0;
    std::uint32_t maxTextureUploads = 0;
    std::uint32_t maxAliases = 0;
    std::uint32_t maxAttachments = 0;
    std::uint32_t maxQueueRecords = 0;
    std::uint32_t maxExternalResources = 0;
};

struct RhiFramePlanBuildResult {
    bool success = false;
    std::pmr::string diagnostics;
    RhiFramePlan plan;
};

class ARP_API RhiFramePlanBuilder {
public:
    explicit RhiFramePlanBuilder(const RhiFramePlanBuilderDesc& desc);

    RhiFramePlanBuilder& setFrameSlot(std::uint32_t frameSlot);
    RhiFramePlanBuilder& requireFeature(rhi::EngineFeature feature,
        rhi::FeatureCorrectnessImpact correctnessImpact,
        const std::pmr::string& reason);
    RhiFramePlanBuilder& addBarrier(const RhiBarrierRecord& barrier);
    RhiFramePlanBuilder& addAlias(const RhiAliasRecord& alias);
    RhiFramePlanBuilder& addAttachment(const RhiAttachmentRecord& attachment);
    RhiFramePlanBuilder& addQueueRecord(const RhiQueueRecord& queueRecord);
    RhiFramePlanBuilder& addExternalResource(const RhiExternalResourceRecord& resource);
    RhiFramePlanBuilder& addPass(const RhiPassRecord& pass);
    RhiFramePlanBuilder& addTextureUpload(const RhiTextureUploadRecord& upload);

    RhiFramePlanBuildResult build() const;

private:
    void appendFailure(const std::pmr::string& diagnostics);

    RhiFramePlanBuilderDesc desc_;
    RhiFramePlan plan_;
    bool failed_ = false;
    std::pmr::string diagnostics_;
};

struct FrameGraphRhiDiagnosticsAdapterDesc {
    std::uint32_t logicalFrameSlots = 2;
    std::uint32_t maxFeatureRequirements = 0;
    std::uint32_t maxBarriers = 0;
    std::uint32_t maxPasses = 0;
    std::uint32_t maxTextureUploads = 8;
    std::uint32_t maxAliases = 64;
    std::uint32_t maxAttachments = 64;
    std::uint32_t maxQueueRecords = 64;
    std::uint32_t maxExternalResources = 8;
};

/// Shader packages are renderer-owned ABI artifacts. The FrameGraph executor
/// may consume them, but it must never synthesize a package from pass counts
/// or descriptor names. The binding lifetime must cover the execute call.
struct FrameGraphRhiDescriptorSetBinding {
    const rhi::DescriptorSet* descriptorSet = nullptr;
    const std::uint32_t* dynamicOffsets = nullptr;
    std::uint32_t dynamicOffsetCount = 0;
};

struct FrameGraphRhiResourceBinding {
    std::uint32_t handleId = 0;
    rhi::GpuTexture* texture = nullptr;
    rhi::GpuBuffer* buffer = nullptr;
};

struct FrameGraphRhiShaderPackageBinding {
    std::pmr::string passName;
    const rhi::ShaderPackageDesc* package = nullptr;
    const rhi::PipelineLayoutDesc* pipelineLayout = nullptr;
    rhi::GpuTexture* colorTarget = nullptr;
    rhi::GpuBuffer* transferSource = nullptr;
    rhi::GpuBuffer* transferDestination = nullptr;
    const rhi::DescriptorTable* descriptorTable = nullptr;
    std::pmr::vector<FrameGraphRhiDescriptorSetBinding> descriptorSets;
    const void* pushConstantData = nullptr;
    std::uint32_t pushConstantDataSize = 0;
};

struct FrameGraphRhiShaderPackageCatalog {
    std::pmr::vector<FrameGraphRhiShaderPackageBinding> bindings;
    std::pmr::vector<FrameGraphRhiResourceBinding> resources;

    const rhi::ShaderPackageDesc* find(const std::pmr::string& passName) const
    {
        for (const auto& binding : bindings) {
            if (binding.passName == passName) {
                return binding.package;
            }
        }
        return nullptr;
    }

    const FrameGraphRhiShaderPackageBinding* findBinding(const std::pmr::string& passName) const
    {
        for (const auto& binding : bindings) {
            if (binding.passName == passName) {
                return &binding;
            }
        }
        return nullptr;
    }

    rhi::GpuTexture* findTexture(std::uint32_t handleId) const
    {
        for (const auto& resource : resources) {
            if (resource.handleId == handleId) {
                return resource.texture;
            }
        }
        return nullptr;
    }

    rhi::GpuBuffer* findBuffer(std::uint32_t handleId) const
    {
        for (const auto& resource : resources) {
            if (resource.handleId == handleId) {
                return resource.buffer;
            }
        }
        return nullptr;
    }
};

struct FrameGraphRhiDiagnosticsReport {
    bool success = false;
    std::pmr::string diagnostics;
    bool diagnosticsOnly = true;
    bool activeExecutionPath = false;
    bool backendFallbacksHiddenFromUpperLayer = true;
    bool nativeHandleExposedToUpperLayer = false;
    std::uint32_t compiledPassCount = 0;
    std::uint32_t executablePassCount = 0;
    std::uint32_t queuePlanCount = 0;
    std::uint32_t aliasCount = 0;
    std::uint32_t loadStoreCount = 0;
    std::uint32_t externalResourceCount = 0;
    RhiFramePlan plan;
};

class FrameGraphRhiDiagnosticsAdapter final {
public:
    explicit FrameGraphRhiDiagnosticsAdapter(const FrameGraphRhiDiagnosticsAdapterDesc& desc);

    FrameGraphRhiDiagnosticsReport build(
        const CompiledFrameGraph& compiled, std::uint32_t frameSlot) const;

private:
    FrameGraphRhiDiagnosticsAdapterDesc desc_;
};

struct FrameGraphRhiExecutionGate {
    bool enabled = false;
    bool runNullHiddenFrameSmoke = true;
    std::pmr::string smokeSurfaceName = "FrameGraphRhiActiveExecution";
    rhi::Extent2D smokeExtent{320, 180};
    rhi::TextureFormat smokeFormat = rhi::TextureFormat::Bgra8Unorm;
    std::uint32_t smokeFrameCount = 2;
};

struct FrameGraphRhiActiveFrameReport {
    std::uint32_t frameSlot = 0;
    std::uint64_t frameSerial = 0;
    std::uint32_t submittedCommandCount = 0;
    bool presentSucceeded = false;
    std::uint32_t releasedResourceCount = 0;
    std::uint64_t releasedBytes = 0;
    std::pmr::string diagnostics;
};

struct FrameGraphRhiNullSmokeSummary {
    bool executed = false;
    bool success = false;
    std::pmr::string diagnostics;
    std::uint32_t swapchainImageCount = 0;
    std::uint32_t maxFramesInFlight = 0;
    std::uint32_t submittedFrameCount = 0;
    std::uint32_t totalSubmittedCommandCount = 0;
    std::uint32_t logicalFrameSlots = 0;
    bool descriptorCompletenessValidated = false;
    std::uint32_t transientResourceCount = 0;
    std::uint32_t pendingDestroyCount = 0;
    std::pmr::vector<FrameGraphRhiActiveFrameReport> frames;
};

struct FrameGraphRhiCommandExecutorDesc {
    std::uint32_t logicalFrameSlots = 2;
    std::uint32_t maxFeatureRequirements = 0;
    std::uint32_t maxBarriers = 0;
    std::uint32_t maxPasses = 0;
    const rhi::semantics::GpuTimingLoweringPlan* gpuTimingPlan = nullptr;
    const FrameGraphRhiShaderPackageCatalog* shaderPackages = nullptr;
};

struct FrameGraphRhiRecordedPassReport {
    std::pmr::string name;
    PassKind kind = PassKind::Graphics;
    QueueRole queue = QueueRole::Graphics;
    std::uint32_t drawCommandCount = 0;
    std::uint32_t dispatchCommandCount = 0;
    std::uint32_t transferCommandCount = 0;
    std::uint32_t pushConstantCommandCount = 0;
    bool recordedThroughCommandEncoder = false;
};

struct FrameGraphRhiCommandExecutionReport {
    bool success = false;
    std::pmr::string diagnostics;
    bool recordedThroughRhiCommandEncoder = false;
    bool passRecordsConsumed = false;
    bool backendOwnedPassResources = false;
    bool backendConsumableBarrierPlan = false;
    bool backendFallbacksHiddenFromUpperLayer = true;
    bool nativeHandleExposedToUpperLayer = false;
    std::uint32_t frameSlot = 0;
    std::uint32_t executedPassCount = 0;
    std::uint32_t renderPassCount = 0;
    std::uint32_t computePassCount = 0;
    std::uint32_t transferPassCount = 0;
    std::uint32_t drawCommandCount = 0;
    std::uint32_t dispatchCommandCount = 0;
    std::uint32_t transferCommandCount = 0;
    std::uint32_t pushConstantCommandCount = 0;
    std::uint32_t barrierCount = 0;
    std::uint32_t barrierEmissionCount = 0;
    std::uint32_t loadStorePlanCount = 0;
    std::uint32_t externalProtectedResourceCount = 0;
    std::uint32_t pipelineCacheHitCount = 0;
    std::uint32_t pipelineCacheMissCount = 0;
    std::uint32_t pipelineCacheEntryCount = 0;
    std::uint32_t uniformRingBufferAllocationCount = 0;
    std::uint32_t uniformRingBufferCreationCount = 0;
    std::uint32_t gpuTimingScopeCount = 0;
    std::uint32_t gpuTimingWriteCount = 0;
    bool asyncComputeSplitExecuted = false;
    bool multiQueueExecuted = false;
    /// Timeline semaphore submit observability for the async-compute
    /// multi-queue path. Records the wait/signal values actually handed to
    /// GpuQueue::submit so contract tests can assert the compute-signal →
    /// graphics-wait ordering without a native (deadlock-capable) backend.
    /// computeSignalValue: value the compute submission signalled (0 = none).
    /// graphicsWaitValue: value the graphics submission waited on (0 = none).
    bool asyncComputeTimelineUsed = false;
    std::uint64_t asyncComputeSignalValue = 0;
    std::uint64_t asyncComputeWaitValue = 0;
    std::uint32_t asyncComputeDegradationCount = 0;
    std::uint32_t asyncComputePassCount = 0;
    /// Phase 7 [P1-B]: submit segment observability. The multi-queue path
    /// splits execution into contiguous same-queue segments. submitSegmentCount
    /// is the number of segments; submitSegmentQueueRoles is one char per
    /// segment ('G' = graphics, 'C' = compute) so contract tests can assert
    /// the exact queue interleaving (e.g. "GCG" for g→c→g).
    std::uint32_t submitSegmentCount = 0;
    std::pmr::string submitSegmentQueueRoles;
    std::uint32_t commandBufferCount = 0;
    bool mobileTileHintsLowered = false;
    bool memorylessAttachmentIntentLowered = false;
    std::uint32_t submittedCommandCount = 0;
    std::pmr::vector<rhi::EngineFeatureExecutionStatus> executionStatuses;
    std::pmr::vector<rhi::QueryExecutionStatus> queryExecutionStatuses;
    rhi::ExecutionStatusTelemetrySummary statusTelemetry;
    FrameGraphRhiDiagnosticsReport diagnosticsReport;
    std::pmr::vector<FrameGraphRhiRecordedPassReport> recordedPasses;
};

struct FrameGraphRhiActiveExecutionReport {
    bool success = false;
    std::pmr::string diagnostics;
    bool diagnosticsOnly = true;
    bool activeExecutionPath = false;
    bool explicitGateEnabled = false;
    bool realRhiCommandSubmission = false;
    bool transientAllocationExecuted = false;
    bool transientAllocationSucceeded = false;
    bool graphPassExecutionRan = false;
    bool v1ExecutionUnaffected = true;
    bool backendFallbacksHiddenFromUpperLayer = true;
    bool nativeHandleExposedToUpperLayer = false;
    FrameGraphRhiDiagnosticsReport diagnosticsReport;
    FrameGraphTransientAllocationReport transientAllocation;
    FrameGraphRhiNullSmokeSummary nullSmoke;
    FrameGraphRhiCommandExecutionReport graphPassExecution;
};

class FrameGraphRhiCommandExecutor final {
public:
    explicit FrameGraphRhiCommandExecutor(const FrameGraphRhiCommandExecutorDesc& desc);

    FrameGraphRhiCommandExecutionReport execute(
        const CompiledFrameGraph& compiled, std::uint32_t frameSlot, rhi::GpuDevice& device) const;

    /// Legacy ring overload: fails before recording/submission until a genuine
    /// completion/ordered-reuse issuer and command custody can be supplied.
    FrameGraphRhiCommandExecutionReport execute(const CompiledFrameGraph& compiled,
        std::uint32_t frameSlot,
        rhi::GpuDevice& device,
        FrameGraphUniformRingBuffer& uniformRingBuffer) const;

private:
    FrameGraphRhiCommandExecutorDesc desc_;
};

struct FrameGraphRhiActiveExecutorDesc {
    FrameGraphRhiDiagnosticsAdapterDesc diagnostics;
    const FrameGraphRhiShaderPackageCatalog* shaderPackages = nullptr;
};

class FrameGraphRhiActiveExecutor final {
public:
    explicit FrameGraphRhiActiveExecutor(const FrameGraphRhiActiveExecutorDesc& desc);

    FrameGraphRhiActiveExecutionReport execute(const CompiledFrameGraph& compiled,
        std::uint32_t frameSlot,
        rhi::GpuDevice& device,
        const FrameGraphRhiExecutionGate& gate) const;

private:
    FrameGraphRhiActiveExecutorDesc desc_;
};

} // namespace arp::render_graph

#pragma once

#include "core/api.h"
#include "core/memory/cpu_allocator.h"
#include "renderer/render_graph/render_graph_types.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace arp::render_graph {

struct CompiledPass {
    size_t originalPassIndex = 0;
    std::pmr::string name;
    PassKind kind = PassKind::Graphics;
    QueueRole queue = QueueRole::Graphics;
    bool asyncComputeEligible = false;
    bool culled = false;
    std::pmr::vector<RhiDrawIndexedCommand> rhiDrawCommands;
    std::pmr::vector<RhiDispatchCommand> rhiDispatchCommands;
    std::pmr::vector<RhiTransferCommand> rhiTransferCommands;
};

struct ResourceLifetimeRecord {
    std::pmr::string resourceName;
    ResourceKind kind = ResourceKind::Texture;
    uint32_t handleId = 0;
    size_t firstUseOrder = 0;
    size_t lastUseOrder = 0;
    std::pmr::string firstUsePassName;
    std::pmr::string lastUsePassName;
    std::pmr::string producerPassName;
    std::pmr::vector<std::pmr::string> consumerPassNames;
};

struct QueuePlanRecord {
    std::pmr::string passName;
    PassKind kind = PassKind::Graphics;
    QueueRole queue = QueueRole::Graphics;
    bool asyncComputeEligible = false;
};

struct ExternalResourceRecord {
    std::pmr::string resourceName;
    ResourceKind kind = ResourceKind::Texture;
    uint32_t handleId = 0;
    rhi::ResourceState initialState = rhi::ResourceState::Undefined;
    rhi::ResourceState requiredFinalState = rhi::ResourceState::Undefined;
    bool externalProtected = false;
    bool presentable = false;
};

struct PassBarrierBatch {
    std::uint32_t passOriginalIndex = 0;
    std::pmr::vector<BarrierTraceRecord> prePassBarriers;
};

struct CompiledResourceUse {
    std::pmr::string resourceName;
    ResourceKind kind = ResourceKind::Texture;
    ResourceAccess access = ResourceAccess::Read;
    rhi::ResourceState state = rhi::ResourceState::Undefined;
    std::uint32_t handleId = 0;
    rhi::PipelineStage stages = rhi::PipelineStage::None;
    rhi::ResourceAccess resourceAccess = rhi::ResourceAccess::None;
    rhi::TextureSubresourceRange textureRange;
    std::uint64_t bufferOffset = 0;
    std::uint64_t bufferSize = 0;
};

struct CompiledDependencyRecord {
    size_t fromPass = 0;
    size_t toPass = 0;
    std::pmr::string resourceName;
    ResourceKind resourceKind = ResourceKind::Texture;
    TopologyDependencyKind dependency = TopologyDependencyKind::Raw;
    bool renderTargetDependency = false;
};

struct CompiledRenderTargetFlow {
    size_t writerPass = 0;
    std::pmr::string targetName;
    AttachmentRole role = AttachmentRole::ColorAttachment;
    rhi::LoadOp loadOp = rhi::LoadOp::Load;
    rhi::StoreOp storeOp = rhi::StoreOp::Store;
    std::pmr::vector<size_t> consumers;
};

struct CompiledSubpassTopology {
    struct Entry {
        std::pmr::string name;
        std::pmr::vector<std::pmr::string> writes;
        std::pmr::vector<std::pmr::string> inputs;
    };

    std::pmr::string groupName;
    size_t parentPass = 0;
    std::pmr::vector<Entry> entries;
};

struct AliasHandoffRecord {
    TextureHandle before;
    TextureHandle after;
    std::uint32_t aliasGroup = 0;
    size_t producerPass = 0;
    size_t consumerPass = 0;
    QueueRole queue = QueueRole::Graphics;
    rhi::PipelineStage sourceStages = rhi::PipelineStage::None;
    rhi::PipelineStage destinationStages = rhi::PipelineStage::None;
    rhi::ResourceAccess sourceAccess = rhi::ResourceAccess::None;
    rhi::ResourceAccess destinationAccess = rhi::ResourceAccess::None;
};

/// Phase 6: Compiled subgraph metadata. Records the linked position of a
/// subgraph within the parent CompiledFrameGraph — the range of global pass
/// indices it occupies, its pin bindings, alias scope, and cull policy.
struct ARP_API CompiledSubgraph {
    std::pmr::string name;
    /// The global pass index range [first, last) occupied by this subgraph.
    size_t globalPassBegin = 0;
    size_t globalPassEnd = 0;
    /// Maps pin names to the parent graph's resource handles.
    std::pmr::vector<std::pair<std::pmr::string, TextureHandle>> pinBindings;
    /// Unique alias scope identifier for this subgraph's transient resources.
    std::uint32_t aliasScopeId = 0;
    /// Whether this subgraph is culled as a single unit. If all output pins
    /// have no consumer, the entire subgraph is culled together.
    bool culledAsUnit = false;
    AliasScopePolicy aliasScopePolicy = AliasScopePolicy::Isolated;
};

class ARP_API CompiledFrameGraph {
public:
    CompiledFrameGraph() = default;
    CompiledFrameGraph(const CompiledFrameGraph&) = default;
    CompiledFrameGraph(CompiledFrameGraph&&) noexcept = default;
    CompiledFrameGraph& operator=(const CompiledFrameGraph& other);
    CompiledFrameGraph& operator=(CompiledFrameGraph&& other) noexcept;

    // CPU provenance only; native payload and current graph membership still
    // need their original validation. Manual/modified compiled objects reject.
    const char* retirementSourceDiagnostic() const noexcept;
    void addPass(CompiledPass pass);

    void setCommandEncoderDesc(rhi::CommandEncoderDesc desc)
    {
        invalidateRetirementSource();
        commandEncoderDesc_ = std::move(desc);
    }

    const rhi::CommandEncoderDesc& commandEncoderDesc() const noexcept
    {
        return commandEncoderDesc_;
    }

    void addExecutablePass(size_t compiledPassIndex);
    void addResourceLifetime(ResourceLifetimeRecord lifetime);
    void addBarrier(BarrierTraceRecord barrier);
    void addAlias(AliasTraceRecord alias);
    void addLoadStore(AttachmentTraceRecord attachment);
    void addQueuePlan(QueuePlanRecord queuePlan);
    void addExternalResource(ExternalResourceRecord resource);
    void addPassBarrierBatch(PassBarrierBatch batch);
    void addPassResourceUse(size_t passIndex, CompiledResourceUse use);
    void addDependency(CompiledDependencyRecord dependency);
    void addRenderTargetFlow(CompiledRenderTargetFlow flow);
    void addSubpassTopology(CompiledSubpassTopology topology);
    void addAliasHandoff(AliasHandoffRecord handoff);
    void setTransientResourceBudget(
        std::uint64_t peakBytes, std::uint32_t resourceCount, std::uint64_t physicalPeakBytes = 0);
    void addSubgraph(CompiledSubgraph subgraph);
    size_t passCount() const;
    size_t executablePassCount() const;
    const std::pmr::vector<CompiledPass>& passes() const;
    const std::pmr::vector<size_t>& executionOrder() const;
    const std::pmr::vector<ResourceLifetimeRecord>& resourceLifetimes() const;
    const std::pmr::vector<BarrierTraceRecord>& barrierPlan() const;
    const std::pmr::vector<AliasTraceRecord>& aliasPlan() const;
    const std::pmr::vector<AttachmentTraceRecord>& loadStorePlan() const;
    const std::pmr::vector<QueuePlanRecord>& queuePlan() const;
    const std::pmr::vector<ExternalResourceRecord>& externalResources() const;
    const std::pmr::vector<PassBarrierBatch>& passBarrierBatches() const;
    const std::pmr::vector<std::pmr::vector<CompiledResourceUse>>& passResourceUses() const;
    const std::pmr::vector<CompiledDependencyRecord>& dependencies() const;
    const std::pmr::vector<CompiledRenderTargetFlow>& renderTargetFlows() const;
    const std::pmr::vector<CompiledSubpassTopology>& subpassTopologies() const;
    const std::pmr::vector<AliasHandoffRecord>& aliasHandoffs() const;

    std::uint64_t transientPeakBytes() const { return transientPeakBytes_; }

    /// Alias placement requirements after alignment padding, zero without
    /// aliases. Null supplies validator requirements. This is neither a
    /// resident VRAM bound nor a total for ordinary textures and buffers.
    std::uint64_t transientPhysicalPeakBytes() const { return transientPhysicalPeakBytes_; }

    std::uint32_t transientResourceCount() const { return transientResourceCount_; }

    const std::pmr::vector<CompiledSubgraph>& subgraphs() const;

private:
    friend class FrameGraph;
    struct RetirementSource;
    std::shared_ptr<const RetirementSource> retirementSource_;

    void invalidateRetirementSource() noexcept { retirementSource_.reset(); }

    rhi::CommandEncoderDesc commandEncoderDesc_{"FrameGraph"};
    std::pmr::vector<CompiledPass> passes_;
    std::pmr::vector<size_t> executionOrder_;
    std::pmr::vector<ResourceLifetimeRecord> resourceLifetimes_;
    std::pmr::vector<BarrierTraceRecord> barrierPlan_;
    std::pmr::vector<AliasTraceRecord> aliasPlan_;
    std::pmr::vector<AttachmentTraceRecord> loadStorePlan_;
    std::pmr::vector<QueuePlanRecord> queuePlan_;
    std::pmr::vector<ExternalResourceRecord> externalResources_;
    std::pmr::vector<PassBarrierBatch> passBarrierBatches_;
    std::pmr::vector<std::pmr::vector<CompiledResourceUse>> passResourceUses_;
    std::pmr::vector<CompiledDependencyRecord> dependencies_;
    std::pmr::vector<CompiledRenderTargetFlow> renderTargetFlows_;
    std::pmr::vector<CompiledSubpassTopology> subpassTopologies_;
    std::pmr::vector<AliasHandoffRecord> aliasHandoffs_;
    std::uint64_t transientPeakBytes_ = 0;
    std::uint64_t transientPhysicalPeakBytes_ = 0;
    std::uint32_t transientResourceCount_ = 0;
    std::pmr::vector<CompiledSubgraph> subgraphs_;
};

} // namespace arp::render_graph

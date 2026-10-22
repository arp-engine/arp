#include "core/memory/atomic_capacity.h"
#include "rhi/frontend/bindless_shader_package.h"
#include "rhi/frontend/bindless_snapshot.h"
#pragma once

#include "core/api.h"
#include "core/memory/cpu_allocator.h"
#include "renderer/render_graph/frame_graph_resource_pool.h"
#include "renderer/render_graph/render_graph_types.h"
#include "rhi/frontend/ordered_graphics_execution.h"
#include "rhi/frontend/resource_access_scope.h"
#include "rhi/gpu_bindless.h"
#include "rhi/gpu_capabilities.h"
#include "rhi/gpu_device.h"
#include "rhi/gpu_pipeline.h"
#include "rhi/semantics/subpass_input_lowering.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace arp::render_graph {

using SubpassInputDescriptorRoute = rhi::semantics::SubpassInputDescriptorRoute;

class FrameGraph;
class FrameGraphExecutor;
struct OrderedGraphicsMeasurements;
struct CompiledResourceUse;

class ARP_API RenderContext {
public:
    RenderContext(rhi::GpuDevice& device, rhi::CommandEncoder& commands);

    rhi::GpuDevice& device();
    rhi::CommandEncoder& commands();
    const rhi::QueryPool* gpuTimingPool() const;
    std::uint32_t gpuTimingBeginQuery() const;
    std::uint32_t gpuTimingEndQuery() const;
    void setGpuTimingScope(
        const rhi::QueryPool* pool, std::uint32_t beginQuery, std::uint32_t endQuery);
    rhi::CommandValidationResult bindSnapshot(rhi::BindlessPassExtension& pass,
        std::shared_ptr<const rhi::BindlessSnapshot>,
        const rhi::BindlessLoweringPlan&,
        std::uint64_t generation) const;

private:
    friend class FrameGraphExecutor;
    FrameGraph* graph_ = nullptr;
    const core::pmr::Vector<CompiledResourceUse>* passUses_ = nullptr;
    rhi::GpuDevice& device_;
    rhi::CommandEncoder& commands_;
    const rhi::QueryPool* gpuTimingPool_ = nullptr;
    std::uint32_t gpuTimingBeginQuery_ = 0u;
    std::uint32_t gpuTimingEndQuery_ = 0u;
};

class ARP_API RenderPassContext {
public:
    RenderPassContext(RenderContext& context,
        rhi::RenderPassEncoder& pass,
        const std::pmr::vector<SubpassInputDescriptorRoute>* subpassInputBindings = nullptr);

    rhi::GpuDevice& device();
    rhi::CommandEncoder& commands();
    rhi::RenderPassEncoder& renderPass();
    rhi::CommandValidationResult bindSnapshot(std::shared_ptr<const rhi::BindlessSnapshot>,
        const rhi::BindlessLoweringPlan&,
        std::uint64_t generation) const;
    rhi::ValidationResult validateSubpassShaderPackage(
        const rhi::ShaderPackageDesc& shaderPackage) const;
    /// Materializes the validated route as logical RHI descriptor writes.
    /// For a separate-pass lowering the returned write keeps logical type
    /// InputAttachment and carries SampledTexture as its explicit physical
    /// lowering. The caller must provide descriptor sets indexed by logical
    /// descriptor set; this method writes them and validates every resource
    /// route before returning.
    rhi::ValidationResult writeSubpassInputDescriptors(const rhi::ShaderPackageDesc& shaderPackage,
        rhi::DescriptorSet* const* descriptorSets,
        std::uint32_t descriptorSetCount) const;

private:
    RenderContext& context_;
    rhi::RenderPassEncoder& pass_;
    const std::pmr::vector<SubpassInputDescriptorRoute>* subpassInputBindings_ = nullptr;
};

/// Phase 6: RAII wrapper for a scoped debug marker on a CommandEncoder.
/// Constructor pushes a debug group; destructor pops it. Scope-balanced by
/// construction. Used to mark subgraph boundaries during execution so the
/// subgraph's passes are visible as a labeled region in RenderDoc / GPU
/// profilers.
class ARP_API CommandMarkerRecorder {
public:
    explicit CommandMarkerRecorder(rhi::CommandEncoder& commands, std::string_view label)
        : commands_(commands)
    {
        commands_.pushDebugGroup(label);
    }

    ~CommandMarkerRecorder() { commands_.popDebugGroup(); }

    CommandMarkerRecorder(const CommandMarkerRecorder&) = delete;
    CommandMarkerRecorder& operator=(const CommandMarkerRecorder&) = delete;

private:
    rhi::CommandEncoder& commands_;
};

class PassBuilder;
class SubgraphBuilder;
class SubpassGraph;
class SubpassBuilder;
class CompiledFrameGraph;
class CompiledSubgraph;
class FrameGraphCompiler;
class FrameGraphExecutor;

/// Non-owning, per-pass CPU effect at queue acceptance, not GPU completion.
/// See FrameGraphSubmissionObserver/3. The observer must outlive execution;
/// its callback must not allocate, block, reenter or mutate the graph.
struct PassSubmissionToken {
    std::uint64_t ownerId = 0u;
    std::uint64_t generation = 0u;
};

class PassSubmissionObserver {
public:
    virtual ~PassSubmissionObserver() = default;

    virtual const char* validateSubmission(PassSubmissionToken token) const noexcept
    {
        return token.ownerId == 0u && token.generation == 0u ? nullptr
                                                             : "RG-SUBMISSION-TOKEN-UNSUPPORTED";
    }

    virtual void onSubmitted(PassSubmissionToken token,
        const std::shared_ptr<const rhi::GpuFence>& completion) noexcept = 0;
};

/// Forward declaration for the topology dump result (defined in
/// render_graph_topology_dump.h). Avoids a circular include here.
struct TopologyDumpResult;

/// Phase 6: Subgraph recipe. Describes a reusable subgraph with input/output
/// pins and local passes. The recipe is built by SubgraphBuilder and
/// registered with the parent FrameGraph via addSubgraph.
struct ARP_API SubgraphRecipe {
    std::pmr::string name;
    std::pmr::vector<SubgraphPinDecl> inputPins;
    std::pmr::vector<SubgraphPinDecl> outputPins;
    /// Transient alias scope for this subgraph's internal resources.
    AliasScopePolicy aliasScope = AliasScopePolicy::Isolated;
};

class ARP_API FrameGraph {
public:
    using ExecuteCallback = std::function<void(RenderContext&)>;
    using RenderPassExecuteCallback = std::function<void(RenderPassContext&)>;

    /// Compiler-owned mapping emitted for every logical input attachment.
    /// Native and separate-pass backends consume the same logical route;
    /// physical descriptor lowering happens below the FrameGraph boundary.
    struct SubpassInputLoweringRecord {
        std::pmr::string groupName;
        std::uint32_t subpassIndex = 0;
        std::uint32_t logicalAttachmentIndex = 0;
        TextureHandle texture;
        rhi::semantics::SubpassInputPhysicalBinding physicalBinding =
            rhi::semantics::SubpassInputPhysicalBinding::Unsupported;
        rhi::semantics::SubpassInputLoadSemantics loadSemantics =
            rhi::semantics::SubpassInputLoadSemantics::CurrentFragment;
        bool requiresSampler = false;
        rhi::semantics::SubpassInputBindingReference shaderBinding;
        rhi::GpuResourceId sourceTexture;
        rhi::GpuResourceId textureView;
    };

    /// Optional application-owned GPU timing scope for one physical FrameGraph
    /// pass. The graph preserves the opaque id; the RHI publishes it with the
    /// asynchronous timestamp result after the submission fence completes.
    struct GpuTimingPassScope {
        std::pmr::string passName;
        std::uint64_t scopeId = 0u;
        bool callbackOwnsTimestamps = false;
    };

    /// The caller may provide a device-owned transient pool.  FrameGraph only
    /// borrows that pool for one graph lifetime; this keeps cache ownership
    /// independent from per-frame graph construction.
    explicit FrameGraph(
        rhi::GpuDevice& device, std::shared_ptr<FrameGraphResourcePool> resourcePool = nullptr);
    ~FrameGraph();

    // Explicit cold CPU provenance prototype, not native frame admission.
    const char* prepareCompiledRetirementSource(
        const rhi::semantics::FrameGraphCompiledRetirementSourceBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota);
    const char* validateCompiledRetirementSource(const CompiledFrameGraph& compiled) const noexcept;

    TextureHandle createTexture(const rhi::TextureDesc& desc);
    TextureHandle createTransientTexture(const rhi::TextureDesc& desc);
    TextureHandle importTexture(arp::core::UniquePtr<rhi::GpuTexture> texture);
    /// Imports an acquired swapchain image with its L2 acquire/present
    /// contract. The reference must describe the same resource, extent and
    /// format as the texture wrapper; mismatches fail deterministically.
    TextureHandle importSwapchainTexture(
        arp::core::UniquePtr<rhi::GpuTexture> texture, const rhi::SwapchainImageRef& image);
    /// Import a texture by shared-ownership borrow. The FrameGraph holds a
    /// copy of the shared_ptr, extending the texture's lifetime for as long
    /// as any FrameGraph reference to it remains alive - even if the original
    /// owner (e.g. a render pipeline) releases its shared_ptr first.
    ///
    /// This is intended for persistent textures (e.g. shadow maps, atlas
    /// textures) that are created once and reused across frames, avoiding
    /// per-frame allocation/deallocation while remaining safe against
    /// pipeline rebuild / swapchain-out-of-date teardown races: if the
    /// pipeline destroys its shared_ptr while an in-flight graph frame still
    /// holds a reference, the GPU resource stays alive until the graph
    /// releases it.
    ///
    /// Deterministic diagnostic: passing a null shared_ptr throws
    /// std::runtime_error instead of recording a dangling reference.
    /// Repeated imports of the same object return the existing handle and
    /// share its hazard/state chain; equal descriptors do not imply identity.
    TextureHandle importTextureRef(std::shared_ptr<rhi::GpuTexture> texture);
    /// Declares that the first graph write defines the imported texture's
    /// contents for this frame. Reads authored before that write consume the
    /// new value and are ordered after the writer instead of preserving the
    /// external value through a WAR dependency.
    void discardImportedContentsBeforeFirstWrite(TextureHandle handle);
    /// Declares the externally visible release state for an imported texture.
    /// This is independent from its live acquire state: a Vulkan swapchain
    /// image may first arrive Undefined, while it must leave in Present.
    void setExternalFinalState(TextureHandle handle, rhi::ResourceState state);
    BufferHandle createBuffer(const rhi::BufferDesc& desc);
    /// GraphBufferImport/1: shared persistent buffer, one identity/hazard chain
    /// per object. Retained across reset, never transferred to the graph pool.
    BufferHandle importBufferRef(std::shared_ptr<rhi::GpuBuffer> buffer);
    /// Read-only metadata import retaining the entire sealed snapshot/leases.
    /// Pass access must still be explicitly declared.
    BufferHandle importSnapshotMetadata(std::shared_ptr<const rhi::BindlessSnapshot> snapshot);
    /// Writable fault latch import retaining the sealed snapshot and its budget.
    /// Each pass must declare the shader read-write use and stage explicitly.
    BufferHandle importSnapshotFaultLatch(std::shared_ptr<const rhi::BindlessSnapshot> snapshot);
    /// Fault readback import retaining the sealed snapshot and its budget.
    /// The copy destination write must be declared in the pass access set.
    BufferHandle importSnapshotFaultReadback(std::shared_ptr<const rhi::BindlessSnapshot> snapshot);

    /// Frozen into the compiled graph; zero indexed capacities remain disabled.
    void setCommandEncoderDesc(rhi::CommandEncoderDesc desc)
    {
        invalidateCompiledRetirementSource();
        commandEncoderDesc_ = std::move(desc);
    }

    /// GraphBufferLifetime/1: graph-local lifetime is independent of memory
    /// intent. Preserve desc unchanged through the completion-gated pool.
    /// Null validates the same descriptor; Vulkan, Metal and WebGPU consume
    /// their existing RHI memory lowering (or reject unsupported intents).
    /// This does not request physical buffer aliasing or memoryless storage.
    BufferHandle createTransientBuffer(const rhi::BufferDesc& desc);
    rhi::GpuTexture& texture(TextureHandle handle);
    rhi::GpuBuffer& buffer(BufferHandle handle);

    /// Access the stored TextureDesc for a handle without requiring the
    /// GpuTexture to be allocated. This is safe to call during graph build
    /// (before compile/execute) for transient textures that use deferred
    /// allocation. Use this instead of texture(handle).desc() when only the
    /// descriptor is needed.
    const rhi::TextureDesc& textureDesc(TextureHandle handle) const;

    /// Access the current resolved ResourceState for a handle without
    /// requiring the GpuTexture to be allocated. For deferred transient
    /// textures, this returns desc.initialState.
    rhi::ResourceState textureState(TextureHandle handle) const;

    bool isTransient(TextureHandle handle) const;
    bool isTransient(BufferHandle handle) const;
    void setPassCullingEnabled(bool enabled);
    PassBuilder addPass(std::pmr::string name);
    /// Phase 6: Add a pre-built subgraph recipe to the frame graph. Returns
    /// a reference to the stored SubgraphBuilder. Passes and transient
    /// textures declared on the builder's localGraph() are compiled in the
    /// subgraph's local space and linked into the parent during compile().
    SubgraphBuilder& addSubgraph(const SubgraphRecipe& recipe);
    /// Subpass: Add a subpass group to the frame graph. A subpass group
    /// declares a set of attachments shared across multiple sub-passes that
    /// execute inside a single render pass with tile-local data sharing via
    /// input attachments. A backend that explicitly reports a degraded
    /// lowering may convert the group to separate passes with cross-pass
    /// texture sampling. DiagnosticFailure backends reject it at compile.
    /// Returns a reference to the stored SubpassGraph.
    SubpassGraph& addSubpassGraph(std::pmr::string name);
    /// Compile the frame graph. Parent passes are compiled first, then
    /// registered subgraphs are compiled locally and linked into the
    /// result with handle remapping and boundary barriers.
    CompiledFrameGraph compile();
    /// Requests timestamp measurements for named graphics passes.
    /// Unsupported backends simply omit the samples; recording remains valid.
    void setGpuTimingPassScopes(std::pmr::vector<GpuTimingPassScope> scopes);
    void execute();
    /// Execute a graph that has already been compiled by this FrameGraph.
    /// This avoids compiling the same topology twice when callers need the
    /// compiled pass metadata before submission.
    void execute(const CompiledFrameGraph& compiled);
    void executeConformance(rhi::OriginalConformanceClearExecution&, TextureHandle);
    /// Explicit original-issuer Prototype path. Compiles this actual graph;
    /// copied/mutated compiled metadata never grants execution authority.
    void executeOrdered(const std::shared_ptr<rhi::OriginalOrderedGraphicsExecution>&,
        core::ArrayView<std::shared_ptr<rhi::RenderPipeline>>,
        const rhi::semantics::PreparedGraphicsCommandsRequest&,
        const std::shared_ptr<core::CpuMemoryQuota>& commandQuota);
    void executeOrdered(const std::shared_ptr<rhi::OriginalOrderedGraphicsExecution>&,
        core::ArrayView<std::shared_ptr<rhi::RenderPipeline>>,
        const rhi::semantics::PreparedGraphicsCommandsRequest&,
        const std::shared_ptr<core::CpuMemoryQuota>& commandQuota,
        OrderedGraphicsMeasurements*);
    core::UniquePtr<rhi::GpuTexture> takeOrderedSwapchainTexture(TextureHandle);
    TextureHandle importOrderedSwapchainTexture(rhi::Swapchain&,
        const rhi::AcquiredFrame&,
        const std::shared_ptr<rhi::OriginalOrderedGraphicsExecution>&);

    bool lastExecutionSubmitted() const noexcept { return lastExecutionSubmitted_; }

    void reset();

    /// Transient resource pool access (for diagnostics and testing).
    /// The pool caches GpuTexture objects across frames, keyed by TextureDesc
    /// (excluding label), so that transient textures are reused rather than
    /// destroyed and recreated every frame.
    FrameGraphResourcePool& resourcePool() { return *resourcePool_; }

    const FrameGraphResourcePool& resourcePool() const { return *resourcePool_; }

    const std::pmr::vector<std::pmr::string>& executionTrace() const;
    const std::pmr::vector<ResourceTraceRecord>& resourceTrace() const;
    const std::pmr::vector<PassTraceRecord>& passTrace() const;
    const std::pmr::vector<AttachmentTraceRecord>& attachmentTrace() const;
    const std::pmr::vector<AliasTraceRecord>& aliasTrace() const;
    const std::pmr::vector<BarrierTraceRecord>& barrierTrace() const;
    const std::pmr::vector<SubpassInputLoweringRecord>& subpassInputLowerings() const;
    /// Phase 6: debug marker push/pop trace. Each entry is "push:<name>" or
    /// "pop:<name>". A balanced subgraph execution produces matching
    /// push/pop pairs.
    const std::pmr::vector<std::pmr::string>& markerTrace() const;

private:
    void executeNative(
        const CompiledFrameGraph&, rhi::OriginalConformanceClearExecution*, rhi::GpuTexture*);

    friend class CompiledFrameGraph;
    friend class FrameGraphResourcePool;
    struct RetirementSourceState;
    std::shared_ptr<RetirementSourceState> retirementSourceState_;
    void invalidateCompiledRetirementSource() const noexcept;
    void publishCompiledRetirementSource(CompiledFrameGraph& compiled);
    const char* sharedImportSourceDiagnostic() const noexcept;
    const char* ownedResourceSourceDiagnostic() const noexcept;
    bool ownedResourcesPublished() const noexcept;
    bool mixedResourceOwnersEnabled() const noexcept;
    bool transientResourceOwnersEnabled() const noexcept;
    void rejectUnjoinedGraphImport(bool shared = false) const;
    const char* compiledRetirementExecutionDiagnostic(
        const CompiledFrameGraph& compiled) const noexcept;

    friend class PassBuilder;
    friend class SubgraphBuilder;
    friend class FrameGraphCompiler;
    friend class FrameGraphExecutor;

    struct TextureRecord {
        TextureHandle handle;
        rhi::TextureDesc desc;
        arp::core::UniquePtr<rhi::GpuTexture> texture;
        /// Shared-ownership borrow (set by importTextureRef). When non-null,
        /// this takes precedence over `texture`. The FrameGraph shares
        /// ownership with the original creator - the GPU resource is kept
        /// alive until both the original owner and all FrameGraph references
        /// release their shared_ptr. This prevents use-after-free when the
        /// pipeline rebuilds and destroys its texture while an in-flight
        /// graph frame still references it.
        std::shared_ptr<rhi::GpuTexture> textureRef;
        bool transient = false;
        bool imported = false;
        bool placed = false;
        bool presentableExternal = false;
        bool preserveInitialContents = true;
        /// Optional release-state contract for imported resources.
        rhi::ResourceState externalFinalState = rhi::ResourceState::Undefined;
        /// Phase 6: alias scope id. 0 = global pool. Non-zero = isolated
        /// subgraph scope; transient textures with a non-zero scopeId only
        /// alias within the same scope, never with the global pool. Set by
        /// SubgraphBuilder::migratePassesAndTextures for Isolated-scope
        /// subgraphs.
        std::uint32_t aliasScopeId = 0;
        // Negative guard captured while import's object is alive. Positive
        // admission still revalidates canonical owner and registration.
        bool canonicalSharedOwnerAtImport = false;
        // Non-allocating alias of the source owning the original FrameBundle.
        // Separate from imported references: it never changes hazard/reset intent.
        std::shared_ptr<rhi::GpuTexture> bundleTexture;
        bool orderedSwapchainImage = false;

        /// Resolve the underlying GpuTexture (owned or borrowed).
        rhi::GpuTexture* resolveTexture() const noexcept
        {
            return textureRef   ? textureRef.get()
                : bundleTexture ? bundleTexture.get()
                                : texture.get();
        }

        /// Resolve the current resource state without requiring the GpuTexture
        /// to be allocated. For deferred (not-yet-allocated) transient
        /// textures, this returns the desc's initialState. For allocated or
        /// imported textures, this returns the live GpuTexture's state.
        rhi::ResourceState resolvedState() const noexcept
        {
            if (const auto* resource = resolveTexture())
                return resource->state();
            return desc.initialState;
        }

        /// Resolve the texture descriptor. Always available because the desc
        /// is stored in the record regardless of allocation status.
        const rhi::TextureDesc& resolvedDesc() const noexcept { return desc; }
    };

    struct BufferRecord {
        BufferHandle handle;
        arp::core::UniquePtr<rhi::GpuBuffer> buffer;
        bool transient = false;
        std::shared_ptr<rhi::GpuBuffer> bufferRef;
        bool readOnly = false;
        bool canonicalSharedOwnerAtImport = false;
        std::shared_ptr<rhi::GpuBuffer> bundleBuffer;

        rhi::GpuBuffer* resolveBuffer() const noexcept
        {
            return bufferRef ? bufferRef.get() : bundleBuffer ? bundleBuffer.get() : buffer.get();
        }

        bool imported() const noexcept { return bool(bufferRef); }
    };

    struct ResourceUse {
        TextureHandle handle;
        rhi::ResourceState state;
        rhi::PipelineStage stages = rhi::PipelineStage::None;
        rhi::ResourceAccess access = rhi::ResourceAccess::None;
        rhi::TextureSubresourceRange range;
    };

    struct AttachmentUse {
        TextureHandle handle;
        rhi::ResourceState state = rhi::ResourceState::Undefined;
        rhi::LoadOp loadOp = rhi::LoadOp::Load;
        rhi::StoreOp storeOp = rhi::StoreOp::Store;
        MobileTileHints tileHints;
        rhi::Color clearColor;
    };

    struct BufferUse {
        BufferHandle handle;
        rhi::ResourceState state;
        rhi::PipelineStage stages = rhi::PipelineStage::None;
        rhi::ResourceAccess access = rhi::ResourceAccess::None;
        std::uint64_t offset = 0;
        std::uint64_t size = 0;
    };

    /// Subpass metadata for a PassRecord that originated from a SubpassGraph
    /// (native multi-subpass path only). When non-null, this PassRecord
    /// represents a multi-subpass render pass: the executor calls each
    /// subpassCallback in order, calling nextSubpass() between them.
    struct SubpassGroupInfo {
        std::pmr::string groupName;

        /// One entry per subpass. Each entry's callback is invoked at the
        /// corresponding subpass index within the render pass.
        struct SubpassEntry {
            std::pmr::string name;
            RenderPassExecuteCallback callback;
            /// Attachment indices (into attachmentWrites) written by this subpass.
            std::pmr::vector<std::uint32_t> writtenAttachmentIndices;
            /// Attachment indices read as input attachments by this subpass.
            std::pmr::vector<std::uint32_t> inputAttachmentIndices;
            /// Explicit logical shader routes consumed by this subpass.
            std::pmr::vector<SubpassInputDescriptorRoute> subpassInputBindings;
        };

        std::pmr::vector<SubpassEntry> subpasses;
        /// RHI subpass descriptions for the RenderPassDesc.
        std::pmr::vector<rhi::SubpassDesc> subpassDescs;
        std::pmr::vector<rhi::SubpassDependency> subpassDependencies;
    };

    struct PassRecord {
        std::pmr::string name;
        PassKind kind = PassKind::Graphics;
        QueueRole queue = QueueRole::Graphics;
        bool asyncComputeEligible = false;
        // The pass mutates an explicitly managed RHI resource outside graph
        // handle tracking (for example persistent GPU-driven indirect buffers).
        // It must remain executable even when no graph resource consumes it.
        bool externalSideEffects = false;
        std::pmr::vector<ResourceUse> reads;
        std::pmr::vector<ResourceUse> writes;
        std::pmr::vector<AttachmentUse> attachmentWrites;
        std::pmr::vector<BufferUse> bufferReads;
        std::pmr::vector<BufferUse> bufferWrites;
        ExecuteCallback callback;
        RenderPassExecuteCallback renderPassCallback;
        PassSubmissionObserver* submissionObserver = nullptr;
        PassSubmissionToken submissionToken{};
        /// Non-null when this PassRecord originated from a SubpassGraph native
        /// lowering. Null for regular passes and degraded subpass groups.
        /// Shared ptr to keep PassRecord copyable (subgraph migration copies
        /// PassRecords; those never have subpassGroup, so the copy is null).
        std::shared_ptr<SubpassGroupInfo> subpassGroup;
        /// Explicit logical shader routes for a degraded subpass pass.
        std::pmr::vector<SubpassInputDescriptorRoute> subpassInputBindings;
        std::pmr::vector<RhiDrawIndexedCommand> rhiDrawCommands;
        std::pmr::vector<RhiDispatchCommand> rhiDispatchCommands;
        std::pmr::vector<RhiTransferCommand> rhiTransferCommands;
    };

    PassRecord& pass(uint32_t id);
    TextureHandle allocateTextureHandle() const;
    BufferHandle allocateBufferHandle() const;
    const TextureRecord& textureRecord(TextureHandle handle) const;
    TextureRecord& textureRecord(TextureHandle handle);
    const BufferRecord& bufferRecord(BufferHandle handle) const;
    bool shouldCullPass(size_t passIndex, const std::pmr::vector<bool>& culled) const;
    bool isTextureReadAfter(
        TextureHandle handle, size_t passIndex, const std::pmr::vector<bool>& culled) const;
    bool isBufferReadAfter(
        BufferHandle handle, size_t passIndex, const std::pmr::vector<bool>& culled) const;
    rhi::RenderPassDesc renderPassDescFor(const PassRecord& passRecord) const;
    void buildAliasTrace(const CompiledFrameGraph& compiled);
    void replayPassBarriers(rhi::CommandEncoder& commands,
        const CompiledFrameGraph& compiled,
        std::uint32_t passOriginalIndex);
    void recordAttachmentTrace(const PassRecord& pass);
    void allocateTransientTextures(const CompiledFrameGraph& compiled);
    void prepareSubpassInputViews();
    void validateSubmissionObservers(const CompiledFrameGraph& compiled) const;
    /// Subpass: lower a SubpassGraph into PassRecords. Called by compile()
    /// before FrameGraphCompiler::compile. Native path (Vulkan/Metal) creates
    /// a single multi-subpass PassRecord; degraded path (WebGPU) creates
    /// separate PassRecords with cross-pass sampling.
    void lowerSubpassGraph(SubpassGraph& group);

    rhi::GpuDevice& device_;
    std::shared_ptr<FrameGraphResourcePool> resourcePool_;
    core::AtomicCapacityLease graphPoolLease_;
    // Device capability snapshots are stable for a graph lifetime. Caching
    // the hot-path timestamp bit avoids rebuilding backend fallback plans on
    // every compiled-graph execution while preserving compile-time queries.
    bool timestampQueriesSupported_ = false;
    /// Completion signal for the command buffer submitted by this graph.
    /// Transient resources remain fence-gated in resourcePool_ until this
    /// signal is published by the backend queue.
    std::shared_ptr<rhi::GpuFence> submissionFence_;
    std::shared_ptr<rhi::OriginalOrderedGraphicsExecution> orderedExecution_;
    bool lastExecutionSubmitted_ = false;
    bool hasExecutedCompiledGraph_ = false;
    const CompiledFrameGraph* executedCompiledGraph_ = nullptr;
    bool reuseCompiledTrace_ = false;
    std::size_t resourceTraceCursor_ = 0u;
    std::size_t attachmentTraceCursor_ = 0u;
    std::size_t barrierTraceCursor_ = 0u;
    /// Transient memory heaps created from the transient allocation plan.
    /// Declared before textures_ so heaps outlive placed textures during
    /// destruction (C++ reverse-declaration-order destruction).
    std::pmr::vector<arp::core::UniquePtr<rhi::GpuMemoryHeap>> transientHeaps_;
    FrameGraphAliasedResourceLayout transientAliasedLayout_;
    std::pmr::vector<std::uint32_t> transientAliasedTextureHandles_;
    std::pmr::vector<TextureRecord> textures_;
    std::pmr::vector<arp::core::UniquePtr<rhi::GpuTextureView>> subpassInputViews_;
    std::pmr::vector<BufferRecord> buffers_;
    rhi::CommandEncoderDesc commandEncoderDesc_{"FrameGraph"};
    std::pmr::vector<PassRecord> passes_;
    std::pmr::vector<std::pmr::string> executionTrace_;
    std::pmr::vector<ResourceTraceRecord> resourceTrace_;
    std::pmr::vector<PassTraceRecord> passTrace_;
    std::pmr::vector<AttachmentTraceRecord> attachmentTrace_;
    std::pmr::vector<AliasTraceRecord> aliasTrace_;
    std::pmr::vector<BarrierTraceRecord> barrierTrace_;
    std::pmr::vector<SubpassInputLoweringRecord> subpassInputLowerings_;
    std::pmr::vector<GpuTimingPassScope> gpuTimingPassScopes_;
    std::pmr::vector<std::pmr::string> markerTrace_;
    std::pmr::vector<core::UniquePtr<SubgraphBuilder>> subgraphs_;
    std::pmr::vector<core::UniquePtr<SubpassGraph>> subpassGraphs_;
    bool passCullingEnabled_ = false;
};

/// Phase 6: Subgraph builder. Constructs a subgraph from a recipe, binding
/// pins to parent FrameGraph resources. The subgraph owns a private FrameGraph
/// for local pass declarations. On parent compile(), the local graph is
/// compiled independently and linked into the parent CompiledFrameGraph with
/// handle remapping, pass offset, and boundary barriers.
class ARP_API SubgraphBuilder {
public:
    SubgraphBuilder(rhi::GpuDevice& device, FrameGraph& parent, const SubgraphRecipe& recipe);

    /// Bind a subgraph pin to a parent graph texture handle. The handle must
    /// be a valid resource in the parent graph at compile time. Unbound
    /// input pins throw during compile.
    SubgraphBuilder& bindPin(std::pmr::string pinName, TextureHandle handle);

    /// Access the subgraph's local FrameGraph for pass authoring. Passes
    /// and transient textures declared here are compiled in the subgraph's
    /// local space and linked into the parent during compile().
    FrameGraph& localGraph();

    /// Mark a local texture handle as imported (input pin). This bypasses
    /// the read-before-write validation during local compilation, since the
    /// texture's data comes from the parent graph through a pin binding.
    void markPinImported(TextureHandle handle);

    /// Set the alias scope identifier for this subgraph instance.
    SubgraphBuilder& aliasScopeId(std::uint32_t scopeId);

    /// Phase 6: whole-unit cull decision. Returns true if every output pin's
    /// migrated parent handle has no consumer among non-culled passes outside
    /// this subgraph's pass range. The compiler uses this to cull an entire
    /// subgraph as a single unit (monotonic: once culled, never resurrected).
    /// A subgraph with no output pins is never whole-culled here; per-pass
    /// culling handles its dead transient writes.
    bool shouldCullAsUnit(const std::pmr::vector<bool>& culled) const;

    /// Phase 6: mark every pass in passRange_ as culled. Returns true if any
    /// pass was newly culled (so the compiler can keep iterating). Does not
    /// modify SubgraphBuilder state — only the caller-owned culled vector.
    bool markCulledAsUnit(std::pmr::vector<bool>& culled) const;

private:
    friend class FrameGraph;
    friend class FrameGraphCompiler;

    /// Phase 6: resolve every output pin name to its migrated parent handle.
    /// Output pins with no migrated handle are skipped (compileAndLink throws
    /// on those separately). Order matches recipe_.outputPins.
    std::pmr::vector<TextureHandle> outputPinParentHandles() const;

    /// Phase 6: returns true if any non-culled pass outside passRange_ reads
    /// any of the output pin parent handles (sampled/storage reads). Shared
    /// by shouldCullAsUnit and the cull/read consistency check in
    /// compileAndLink. Reads only — attachment loadOp::Load is not counted,
    /// matching the existing per-pass culling surface (isTextureReadAfter).
    bool hasExternalOutputConsumer(const std::pmr::vector<bool>& culled) const;

    /// Compile the local graph and link results into the parent compiled
    /// output. Called by FrameGraph::compile() after parent passes are
    /// compiled. Throws on pin binding / output producer diagnostics.
    void compileAndLink(CompiledFrameGraph& parentCompiled, const FrameGraph& parentGraph) const;

    /// Phase 7 [P0-B]: migrate local pass records and transient textures
    /// into the parent FrameGraph (passes_ / textures_) with handle remap,
    /// so the subsequent FrameGraphCompiler::compile produces a compiled graph
    /// that includes the subgraph's passes and alias plan. Called before
    /// FrameGraphCompiler::compile. Internal transient textures are allocated
    /// via the parent's createTransientTexture (real handles, not the
    /// 10000+aliasScopeId_*1000 placeholder); input-pin textures reuse the
    /// bound parent handle. Fills localToParentTexture_ and passRange_ for
    /// compileAndLink to consume.
    void migratePassesAndTextures();

    rhi::GpuDevice& device_;
    FrameGraph& parentGraph_;
    SubgraphRecipe recipe_;
    core::UniquePtr<FrameGraph> localGraph_;
    std::pmr::vector<std::pair<std::pmr::string, TextureHandle>> pinBindings_;
    std::uint32_t aliasScopeId_ = 0;
    /// Phase 7 [P0-B]: local texture handle.id -> parent handle, filled by
    /// migratePassesAndTextures, consumed by compileAndLink for output-pin
    /// exit barriers.
    std::pmr::map<std::uint32_t, TextureHandle> localToParentTexture_;
    /// Phase 7 [P0-B]: [begin, end) range of migrated pass records in the
    /// parent passes_ vector, for CompiledSubgraph metadata.
    std::pair<std::uint32_t, std::uint32_t> passRange_{0, 0};
    /// Idempotency guard: migratePassesAndTextures() must run at most once per
    /// SubgraphBuilder. FrameGraph::compile() may be called multiple times
    /// (diagnostic pre-compile, compile()+execute(), repeated trace) — without
    /// this guard each call would re-create parent transient textures and
    /// re-append the local passes, turning the read-only compile() into a
    /// state-mutating accumulation (duplicate resources + passes). Once true,
    /// the local passes/textures already live in the parent and a re-compile
    /// must reuse them as-is.
    bool migrated_ = false;
};

// ===========================================================================
// SubpassGraph: multi-subpass render pass with tile-local data sharing.
// ===========================================================================

/// A subpass group: multiple sub-passes executed inside a single render pass
/// with tile-local data sharing via input attachments. On backends without
/// subpass support (WebGPU), the compiler auto-lowers to separate passes with
/// cross-pass texture sampling.
class ARP_API SubpassGraph {
public:
    SubpassGraph(FrameGraph& parent, std::pmr::string name);

    /// Declare an attachment for this subpass group. The texture must be a
    /// parent transient or imported texture. Returns a SubpassAttachment handle
    /// used to reference this attachment when authoring subpasses.
    SubpassAttachment attachment(TextureHandle handle,
        rhi::ResourceState state,
        AttachmentRole role,
        MobileTileHints tileHints = {});

    /// Declare a subpass. Subpasses execute in declaration order within one
    /// render pass. Returns a SubpassBuilder for pass authoring.
    SubpassBuilder& subpass(std::pmr::string name);

    /// Number of subpasses declared.
    std::uint32_t subpassCount() const;

    /// Name of this subpass group (for diagnostics / debug markers).
    const std::pmr::string& name() const { return name_; }

private:
    friend class FrameGraph;
    friend class FrameGraphCompiler;
    friend class FrameGraphExecutor;

    FrameGraph& parent_;
    std::pmr::string name_;
    std::pmr::vector<SubpassAttachmentDecl> attachments_;
    std::pmr::vector<core::UniquePtr<SubpassBuilder>> subpasses_;
    /// Pass slot reserved at authoring time. Lowering is deferred until all
    /// subpasses are authored, but the resulting pass record(s) must retain
    /// this position relative to ordinary FrameGraph passes.
    std::uint32_t placeholderPassIndex_ = UINT32_MAX;
    /// Idempotency guard: lowering happens at most once per compile().
    bool lowered_ = false;
};

/// Builder for a single subpass within a SubpassGraph. Each subpass declares
/// which attachments it writes and which previous-subpass attachments it reads
/// as input attachments (tile-local).
class ARP_API SubpassBuilder {
public:
    SubpassBuilder(SubpassGraph& group, std::pmr::string name);

    /// Write to an attachment in this subpass. The attachment must have been
    /// declared on the SubpassGraph. loadOp/storeOp follow the same semantics
    /// as PassBuilder::writeAttachment.
    SubpassBuilder& writeAttachment(SubpassAttachment att,
        rhi::LoadOp loadOp,
        rhi::StoreOp storeOp,
        rhi::Color clearColor = {});

    /// Read a previous subpass's attachment as an input attachment (tile-local
    /// on TBDR GPUs). A backend with an explicit degraded lowering may convert
    /// this to a cross-pass sampled texture read.
    /// Diagnostic compatibility overload. The missing shader binding is
    /// rejected during FrameGraph compilation.
    SubpassBuilder& readInputAttachment(SubpassAttachment att);

    /// Read an input attachment through an explicit logical shader binding.
    SubpassBuilder& readInputAttachment(
        SubpassAttachment att, std::uint32_t shaderSet, std::uint32_t shaderBinding);

    /// Read an external texture (normal sampled texture, not tile-local).
    SubpassBuilder& read(TextureHandle handle, rhi::ResourceState state);

    /// Author the subpass's draw commands. The callback receives a
    /// RenderPassContext whose renderPass() encoder is at this subpass's
    /// index. The executor calls nextSubpass() between subpass callbacks.
    void executeRenderPass(FrameGraph::RenderPassExecuteCallback callback);

private:
    friend class SubpassGraph;
    friend class FrameGraph;
    friend class FrameGraphCompiler;
    friend class FrameGraphExecutor;

    struct AttachmentWrite {
        std::uint32_t attachmentIndex = 0;
        rhi::LoadOp loadOp = rhi::LoadOp::Load;
        rhi::StoreOp storeOp = rhi::StoreOp::Store;
        rhi::Color clearColor;
    };

    struct InputAttachmentRead {
        std::uint32_t attachmentIndex = 0;
        rhi::semantics::SubpassInputBindingReference shaderBinding;
    };

    struct ExternalRead {
        TextureHandle handle;
        rhi::ResourceState state = rhi::ResourceState::Undefined;
    };

    SubpassGraph& group_;
    std::pmr::string name_;
    std::pmr::vector<AttachmentWrite> attachmentWrites_;
    std::pmr::vector<InputAttachmentRead> inputReads_;
    std::pmr::vector<ExternalRead> externalReads_;
    FrameGraph::RenderPassExecuteCallback callback_;
};

class ARP_API PassBuilder {
public:
    using RenderPassExecuteCallback = FrameGraph::RenderPassExecuteCallback;

    PassBuilder(FrameGraph& graph, uint32_t passId);

    PassBuilder& read(TextureHandle handle, rhi::ResourceState state);
    PassBuilder& write(TextureHandle handle, rhi::ResourceState state);
    PassBuilder& writeAttachment(TextureHandle handle,
        rhi::ResourceState state,
        rhi::LoadOp loadOp,
        rhi::StoreOp storeOp,
        MobileTileHints tileHints = {},
        rhi::Color clearColor = {});
    PassBuilder& read(BufferHandle handle, rhi::ResourceState state);
    PassBuilder& write(BufferHandle handle, rhi::ResourceState state);
    PassBuilder& access(TextureHandle,
        const rhi::semantics::ResourceAccessScope&,
        const rhi::TextureSubresourceRange&);
    PassBuilder& access(BufferHandle,
        const rhi::semantics::ResourceAccessScope&,
        std::uint64_t offset,
        std::uint64_t size);
    PassBuilder& kind(PassKind kind);
    PassBuilder& queue(QueueRole queue);
    PassBuilder& asyncComputeEligible(bool eligible);
    PassBuilder& externalSideEffects(bool enabled = true);
    PassBuilder& onSubmitted(PassSubmissionObserver& observer, PassSubmissionToken token = {});
    PassBuilder& addRhiDrawIndexedCommand(const RhiDrawIndexedCommand& command);
    PassBuilder& addRhiDispatchCommand(const RhiDispatchCommand& command);
    PassBuilder& addRhiTransferCommand(const RhiTransferCommand& command);
    void execute(FrameGraph::ExecuteCallback callback);
    void executeRenderPass(RenderPassExecuteCallback callback);

private:
    FrameGraph& graph_;
    uint32_t passId_ = 0;
};

} // namespace arp::render_graph

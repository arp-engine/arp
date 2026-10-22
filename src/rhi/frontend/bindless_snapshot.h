#pragma once

#include "core/memory/atomic_capacity.h"
#include "core/ownership/lifetime_epoch.h"
#include "rhi/frontend/bindless_fault_domain.h"
#include "rhi/frontend/resource_leases.h"
#include "rhi/gpu_bindless.h"
#include "rhi/gpu_device.h"

#include <array>
#include <atomic>

namespace arp::rhi {

inline PipelineStage bindlessPipelineStagesForShader(ShaderStage stages) noexcept
{
    auto result = PipelineStage::None;
    if ((stages & ShaderStage::Vertex) != ShaderStage::None)
        result = result | PipelineStage::VertexShader;
    if ((stages & ShaderStage::Fragment) != ShaderStage::None)
        result = result | PipelineStage::FragmentShader;
    if ((stages & ShaderStage::Compute) != ShaderStage::None)
        result = result | PipelineStage::ComputeShader;
    return result;
}

enum class BindlessAccessRetention { Indirect, GraphicsPass };

// Reserved before recording; copies transient declarations without growing.
// A failed capture leaves no declaration authorized.
class ARP_API BindlessRetainedDeclarations : public BindlessAccessDeclarations {
public:
    BindlessRetainedDeclarations(std::uint32_t capacity, BindlessAccessRetention);
    ValidationResult capture(const BindlessAccessDeclarations& source);
    bool matches(const BindlessAccessDeclarations& source) const;

    void clear() noexcept { accesses_.clear(); }

    bool valid() const noexcept override { return true; }

    std::size_t size() const noexcept override { return accesses_.size(); }

    bool read(std::size_t index, BindlessResourceAccess& out) const override;

private:
    std::uint32_t capacity_ = 0;
    BindlessAccessRetention selection_;
    core::pmr::Vector<BindlessResourceAccess> accesses_{&core::defaultCpuMemoryResource()};
};

class ARP_API BindlessIndirectDeclarations final : public BindlessRetainedDeclarations {
public:
    explicit BindlessIndirectDeclarations(std::uint32_t capacity);
};

// Validates the command-side indirect read in addition to snapshot descriptor
// accesses. Passing this check does not authorize a backend to execute indirect.
ARP_API ValidationResult validateBindlessIndirectDrawAccess(const GpuBuffer& buffer,
    std::uint64_t offset,
    std::uint32_t drawCount,
    std::uint32_t stride,
    const BindlessAccessDeclarations& declared);

// The required buffer range must be covered by one declaration with all of
// its state, stage and access intent. Validation does not authorize execution.
ARP_API ValidationResult validateBindlessBufferAccess(
    const BindlessResourceAccess& required, const BindlessAccessDeclarations& declared);

// A prepared, immutable descriptor domain. Preparation is externally serialized
// and occurs outside recording. Native command storage retention remains owned
// by each backend; this wrapper must never interpret a CPU serial as completion.
struct BindlessSnapshotOriginalDescriptorRequest {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    // /2 cold ownership only; never grants mapping or result publication.
    bool retainFaultReadback = false;
};

class ARP_API BindlessSnapshot final : public BindlessFaultDomain {
public:
    BindlessSnapshot() = default;
    BindlessSnapshot(const BindlessSnapshot&) = delete;
    BindlessSnapshot& operator=(const BindlessSnapshot&) = delete;
    BindlessResourceHandle handle(std::uint32_t tableId, std::uint32_t index) const noexcept;
    ValidationResult write(BindlessResourceHandle, const DescriptorBindingWrite&);
    // Typed writes capture the exact buffer extent/view-parent range needed by
    // the graph. Raw descriptor writes alone never authorize recording.
    ValidationResult writeBuffer(BindlessResourceHandle, const GpuBuffer&);
    ValidationResult writeOriginalBufferRange(BindlessResourceHandle,
        GpuDevice&,
        const std::shared_ptr<const OriginalCommandBufferLease>&,
        const DescriptorBufferBindingRange&,
        const OriginalBufferRangeDescriptorRequest&);
    ValidationResult writeTexture(BindlessResourceHandle, const GpuTextureView&, ResourceState);
    ValidationResult writeOriginalCompletedTexture(BindlessResourceHandle,
        const GpuDevice&,
        const std::shared_ptr<const OriginalCommandTextureViewSource>&,
        const GpuTexture::ContentExpectation&,
        const OriginalCompletedTextureDescriptorRequest&);
    ValidationResult writeOriginalCompletedTexture(BindlessResourceHandle,
        const GpuDevice&,
        const std::shared_ptr<const OriginalCompletedDepthTextureSource>&,
        const OriginalCompletedTextureDescriptorRequest&);
    ValidationResult writeSampler(BindlessResourceHandle, const GpuSampler&);
    ValidationResult writeOriginalTextureCell(BindlessResourceHandle,
        const GpuDevice&,
        const std::shared_ptr<const OriginalCommandTextureViewSource>&,
        const GpuTexture::ContentRangeExpectation&,
        const OriginalTextureCellDescriptorRequest&);
    ValidationResult writeOrdinary(const DescriptorBindingWrite&);
    ValidationResult writeOrdinaryBuffer(std::uint32_t binding, const GpuBuffer&);
    // Explicitly alias inactive physical slots to a live active entry from
    // this table. No handle or shader index may address these padding slots.
    ValidationResult initializeInactiveSlots(BindlessResourceHandle source);
    ValidationResult initializeInactiveSlots(BindlessResourceHandle source, const GpuDevice&);
    ValidationResult initializeInactiveSlots(BindlessResourceHandle source, GpuDevice&);
    std::uint32_t activeCount(std::uint32_t tableId) const noexcept;
    // Validated bound-view element count, zero for unwritten/non-buffer slots.
    std::uint32_t bufferElementCount(BindlessResourceHandle) const noexcept;
    ValidationResult seal(GpuDevice& device);
    ValidationResult validate() const override;
    ValidationResult validatePackage(
        const BindlessCrossExtension&, const BindlessShaderArtifact&) const;

    bool sealed() const noexcept { return sealed_; }

    std::uint64_t generation() const noexcept override { return generation_; }

    const BindlessLoweringPlan& plan() const noexcept { return plan_; }

    std::uint64_t reportedInternalBufferBytes() const noexcept
    {
        return reportedInternalBufferBytes_;
    }

    std::shared_ptr<const core::AtomicCapacityLease> originalCountLease() const noexcept
    {
        return countLease_;
    }

    std::shared_ptr<const core::AtomicCapacityLease> originalByteLease() const noexcept
    {
        return byteLease_;
    }

    std::uint32_t setIndex() const noexcept { return setIndex_; }

    // Only sealed and currently live snapshots expose a const descriptor set.
    // No caller can mutate its descriptor contents after publication.
    const DescriptorSet* descriptorSet() const;
    enum class OriginalBufferRole : std::size_t { Metadata, FaultLatch, FaultReadback, Count };
    // Cold, externally serialized ownership bridge into the canonical Source
    // inventory. All original registrations and lease allocations are preflighted
    // before transferring any child. This does not authorize command execution.
    static const char* prepareOriginalBufferSources(const std::shared_ptr<BindlessSnapshot>&,
        const GpuDevice&,
        const OriginalCommandResourceLeaseBudget&,
        std::shared_ptr<core::CpuMemoryQuota>) noexcept;
    const std::shared_ptr<const OriginalCommandBufferLease>& originalBufferSource(
        OriginalBufferRole role) const noexcept;
    // Proves the sealed scalar Vertex reader names this exact mapping extent.
    ValidationResult validateIndirectInstanceMapping(
        const GpuBuffer& mapping, std::uint32_t instanceCapacity) const;

    const core::pmr::Vector<std::uint32_t>& metadataWords() const noexcept
    {
        return metadataWords_;
    }

    BindlessResourceAccess metadataAccess() const noexcept;
    BindlessResourceAccess faultLatchAccess() const noexcept;
    BindlessResourceAccess faultReadbackAccess() const noexcept;

    // Borrowed resources remain owned by the snapshot. The importing graph
    // must retain it, including its budget leases; use is constrained by the
    // matching access declaration and sealed descriptor contract.
    GpuBuffer* metadataBuffer() const noexcept
    {
        return sealed_ ? internalBuffer(OriginalBufferRole::Metadata) : nullptr;
    }

    GpuBuffer* faultLatchBuffer() const noexcept override
    {
        return sealed_ ? internalBuffer(OriginalBufferRole::FaultLatch) : nullptr;
    }

    GpuBuffer* faultReadbackBuffer() const noexcept override
    {
        return sealed_ ? internalBuffer(OriginalBufferRole::FaultReadback) : nullptr;
    }

    ValidationResult validateForRecord(const BindlessLoweringPlan& selected,
        std::uint64_t resourceGeneration,
        const BindlessResourceAccess* declared,
        std::size_t declaredCount) const;
    ValidationResult validateForRecord(const BindlessLoweringPlan& selected,
        std::uint64_t resourceGeneration,
        const BindlessAccessDeclarations&) const;
    ValidationResult validateForRecord(const BindlessLoweringPlan& selected,
        std::uint64_t resourceGeneration,
        const BindlessAccessDeclarations&,
        ShaderStage activeStages) const;

private:
    friend class BindlessSnapshotPool;
    friend class OriginalCommandBindlessRootSource;
    static core::UniquePtr<BindlessSnapshot> create(GpuDevice& device,
        const BindlessRequirements& request,
        const BindlessShaderArtifact& artifact,
        const BindlessLoweringPlan& selected,
        core::pmr::String& diagnostic,
        bool originalDescriptors,
        bool retainFaultReadback);

    struct Table {
        BindlessTableMapping mapping;
        DescriptorType type = DescriptorType::SampledTexture;
        std::uint32_t resourceCount = 0;
        ShaderStage stages = ShaderStage::None;
        std::uint64_t maxBufferBindingSize = 0;
    };

    bool mappedBinding(std::uint32_t binding) const noexcept;
    GpuBuffer* internalBuffer(OriginalBufferRole) const noexcept;
    BindlessResourceAccess metadataAccessUnchecked() const noexcept;
    BindlessResourceAccess faultLatchAccessUnchecked() const noexcept;
    BindlessResourceAccess faultReadbackAccessUnchecked() const noexcept;
    const Table* findTable(BindlessResourceHandle) const noexcept;
    std::uint32_t physicalElement(const Table&, std::uint32_t index) const noexcept;
    ValidationResult writeImpl(BindlessResourceHandle,
        const DescriptorBindingWrite&,
        const GpuDevice*,
        const std::shared_ptr<const OriginalCommandTextureViewSource>&,
        const GpuTexture::ContentExpectation*,
        const OriginalCompletedTextureDescriptorRequest*,
        const std::shared_ptr<const OriginalCommandBufferLease>& buffer = {},
        const OriginalBufferRangeDescriptorRequest* rangeRequest = nullptr,
        GpuDevice* rangeDevice = nullptr,
        const std::shared_ptr<const OriginalCompletedDepthTextureSource>& depth = {},
        const GpuTexture::ContentRangeExpectation* cellIdentity = nullptr,
        const OriginalTextureCellDescriptorRequest* cellRequest = nullptr);
    void captureTextureAccess(const Table&,
        BindlessResourceHandle,
        const GpuTextureView&,
        ResourceState,
        ResourceAccess) noexcept;
    ValidationResult initializeInactiveSlotsImpl(
        BindlessResourceHandle, const GpuDevice*, GpuDevice* rangeDevice = nullptr);

    struct CompletedTexture {
        std::shared_ptr<const OriginalCommandTextureViewSource> source;
        std::optional<GpuTexture::ContentExpectation> content;
        std::shared_ptr<const OriginalCompletedDepthTextureSource> depth;
        OriginalCompletedTextureDescriptorRequest request;
        std::optional<GpuTexture::ContentRangeExpectation> cellIdentity;
        OriginalTextureCellDescriptorRequest cellRequest;
    };

    core::pmr::Vector<CompletedTexture> completedTextures_{&core::defaultCpuMemoryResource()};

    struct BufferRange {
        std::shared_ptr<const OriginalCommandBufferLease> source;
        OriginalBufferRangeDescriptorRequest request;
    };

    core::pmr::Vector<BufferRange> originalBufferRanges_{&core::defaultCpuMemoryResource()};
    // Destroy descriptor storage before releasing accounting. Native factories
    // remain closed until the backend retains these charges through completion.
    std::shared_ptr<core::AtomicCapacityLease> countLease_;
    std::shared_ptr<core::AtomicCapacityLease> byteLease_;
    BindlessSnapshotMetadataLayout metadataLayout_;
    BindlessFaultLatchLayout faultLatchLayout_;
    BindlessIndirectInstanceConsumerLayout indirectInstanceConsumer_;
    core::pmr::Vector<std::uint32_t> metadataWords_{&core::defaultCpuMemoryResource()};
    core::UniquePtr<GpuBuffer> metadataBuffer_;
    core::UniquePtr<GpuBuffer> faultLatchBuffer_;
    core::UniquePtr<GpuBuffer> faultReadbackBuffer_;
    std::array<std::shared_ptr<const OriginalCommandBufferLease>,
        static_cast<std::size_t>(OriginalBufferRole::Count)>
        originalBufferSources_{};
    std::shared_ptr<DescriptorArena> arena_;
    core::UniquePtr<DescriptorSet> set_;
    core::pmr::Vector<Table> tables_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<BindlessResourceAccess> accesses_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<DescriptorBindingWrite> writes_{&core::defaultCpuMemoryResource()};
    BindlessLoweringPlan plan_;
    BindlessRequirements requirements_;
    std::uint64_t reportedInternalBufferBytes_ = 0;
    std::uint64_t maxUniformBufferBindingSize_ = 0;
    std::uint64_t maxStorageBufferBindingSize_ = 0;
    std::uint64_t generation_ = 0;
    std::uint32_t setIndex_ = 0;
    bool sealed_ = false;
    std::uint32_t updates_ = 0;
    std::uint32_t maxUpdates_ = 0;
};

// One workload's bounded candidate/live snapshot domain. Old snapshots retain
// their leases even if the pool is destroyed. Preparation never waits/grows.
// This first implementation validates Null lifetimes only.
class ARP_API BindlessSnapshotPool final {
public:
    BindlessSnapshotPool(GpuDevice& device, BindlessBudget budget);
    BindlessSnapshotPool(const BindlessSnapshotPool&) = delete;
    BindlessSnapshotPool& operator=(const BindlessSnapshotPool&) = delete;
    core::UniquePtr<BindlessSnapshot> prepare(
        const BindlessRequirements&, const BindlessShaderArtifact&, core::pmr::String& diagnostic);

    core::UniquePtr<BindlessSnapshot> prepareOriginalDescriptors(const BindlessRequirements&,
        const BindlessShaderArtifact&,
        BindlessSnapshotOriginalDescriptorRequest,
        core::pmr::String& diagnostic);

    std::shared_ptr<BindlessSnapshot> prepareShared(const BindlessRequirements& request,
        const BindlessShaderArtifact& artifact,
        core::pmr::String& diagnostic)
    {
        return core::shareUnique(prepare(request, artifact, diagnostic));
    }

    std::uint64_t liveSnapshots() const noexcept { return counts_->used(); }

    std::uint64_t chargedBytes() const noexcept { return bytes_->used(); }

private:
    core::UniquePtr<BindlessSnapshot> prepareImpl(const BindlessRequirements&,
        const BindlessShaderArtifact&,
        core::pmr::String&,
        bool originalDescriptors,
        bool retainFaultReadback = false);
    GpuDevice& device_;
    BindlessBudget budget_;
    std::shared_ptr<core::AtomicCapacityBudget> counts_;
    std::shared_ptr<core::AtomicCapacityBudget> bytes_;
};

} // namespace arp::rhi

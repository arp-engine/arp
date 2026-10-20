#pragma once

#include "core/cpu_allocator.h"
#include "core/mutation_journal.h"
#include "null_backend.h"
#include "rhi/gpu_bindless.h"

#include <memory>
#include <mutex>
#include <vector>

namespace arp::rhi::null_backend {

class MemoryLedger final {
public:
    bool canAllocate(std::uint64_t sizeBytes) const;
    GpuResourceId allocate(std::uint64_t sizeBytes);
    void release(std::uint64_t sizeBytes);
    MemoryStats stats() const;

private:
    MemoryStats stats_;
};

enum class RegisteredResourceKind { Buffer, BufferView, Texture, TextureView, Sampler };

struct RegisteredResourceInfo {
    GpuResourceId id;
    RegisteredResourceKind kind = RegisteredResourceKind::Buffer;
    ResourceUsage bufferUsage = ResourceUsage::None;
    TextureUsage textureUsage = TextureUsage::None;
    GpuResourceId parentBuffer;
    GpuResourceId parentTexture;
    TextureViewDimension textureViewDimension = TextureViewDimension::Texture2D;
    TextureFormat textureViewFormat = TextureFormat::Unknown;
    TextureAspect textureViewAspect = TextureAspect::Color;
    std::weak_ptr<const GpuBufferWriteSealState> gpuWriteSeal;
};

class ResourceRegistry final {
public:
    /// Construct with an explicit CPU memory resource. When omitted, the
    /// process-wide default resource is used. Threaded through to the pmr
    /// resources vector so a backend can bind a per-instance intent/budget
    /// instead of silently falling back to the global default.
    explicit ResourceRegistry(
        std::pmr::memory_resource* memoryResource = &core::defaultCpuMemoryResource())
        : resources_(memoryResource)
    {}

    void registerBuffer(GpuResourceId id,
        ResourceUsage usage,
        std::weak_ptr<const GpuBufferWriteSealState> gpuWriteSeal);
    void registerBufferView(GpuResourceId id, GpuResourceId parentBuffer, ResourceUsage usage);
    void registerTexture(GpuResourceId id, TextureUsage usage);
    void registerTextureView(GpuResourceId id,
        GpuResourceId parentTexture,
        TextureUsage textureUsage,
        TextureViewDimension textureViewDimension,
        TextureFormat textureViewFormat,
        TextureAspect textureViewAspect);
    void registerSampler(GpuResourceId id);
    void unregisterResource(GpuResourceId id);
    bool ownsResource(GpuResourceId id, RegisteredResourceKind kind) const;
    bool bufferGpuWritesSealed(GpuResourceId id) const;
    bool bufferGpuTransferWritesSealed(GpuResourceId id) const;
    ValidationResult validateDescriptorResource(DescriptorType type,
        DescriptorResourceKind resourceKind,
        GpuResourceId id,
        bool textureViewDimensionConstrained = false,
        TextureViewDimension textureViewDimension = TextureViewDimension::Texture2D,
        bool depthTexture = false) const;

private:
    mutable std::mutex mutex_;
    std::pmr::vector<RegisteredResourceInfo> resources_;
};

class StatefulResource {
public:
    virtual ~StatefulResource() = default;
    virtual GpuResourceId resourceId() const = 0;
    virtual ResourceState currentState() const = 0;
    virtual bool beginStateRecording(core::MutationJournal& journal) = 0;
    virtual ResourceBarrierResult transitionState(
        ResourceState before, ResourceState after, core::MutationJournal* journal = nullptr) = 0;
};

class StatefulTextureResource : public StatefulResource {
public:
    virtual ResourceBarrierResult transitionSubresourceState(const TextureSubresourceRange& range,
        ResourceState before,
        ResourceState after,
        core::MutationJournal* journal = nullptr) = 0;
};

using MemoryLedgerPtr = std::shared_ptr<MemoryLedger>;
using ResourceRegistryPtr = std::shared_ptr<ResourceRegistry>;

// Validator accounting, independent of public wrappers. Construction before
// registry publication makes a throwing publication release its charge too.
class MemoryLedgerLease final : public OriginalAllocationLifetime {
public:
    MemoryLedgerLease(MemoryLedgerPtr ledger, std::uint64_t bytes)
        : ledger_(std::move(ledger)), bytes_(bytes), id_(ledger_->allocate(bytes_))
    {}

    ~MemoryLedgerLease()
    {
        if (id_.isValid())
            ledger_->release(bytes_);
    }

    MemoryLedgerLease(const MemoryLedgerLease&) = delete;
    MemoryLedgerLease& operator=(const MemoryLedgerLease&) = delete;

    GpuResourceId id() const noexcept { return id_; }

    bool belongsTo(const MemoryLedgerPtr& ledger) const noexcept { return ledger_ == ledger; }

private:
    MemoryLedgerPtr ledger_;
    std::uint64_t bytes_;
    GpuResourceId id_;
};

arp::core::UniquePtr<GpuBuffer> createBuffer(
    const BufferDesc& desc, const MemoryLedgerPtr& ledger, const ResourceRegistryPtr& registry);
arp::core::UniquePtr<GpuBufferView> createBufferView(const GpuBuffer& buffer,
    const BufferViewDesc& desc,
    const MemoryLedgerPtr& ledger,
    const ResourceRegistryPtr& registry);
arp::core::UniquePtr<GpuTexture> createTexture(const TextureDesc& desc,
    const MemoryLedgerPtr& ledger,
    const ResourceRegistryPtr& registry,
    std::shared_ptr<const MemoryLedgerLease> placementLease = {});
arp::core::UniquePtr<GpuTextureView> createTextureView(const GpuTexture& texture,
    const TextureViewDesc& desc,
    const MemoryLedgerPtr& ledger,
    const ResourceRegistryPtr& registry);
arp::core::UniquePtr<GpuSampler> createSampler(
    const SamplerDesc& desc, const MemoryLedgerPtr& ledger, const ResourceRegistryPtr& registry);

arp::core::UniquePtr<DescriptorArena> createDescriptorArena(
    const DescriptorArenaDesc& desc, const ResourceRegistryPtr& registry);

arp::core::UniquePtr<PipelineLayout> createPipelineLayout(const PipelineLayoutDesc& desc);
PipelineValidationReport validateRenderPipeline(const RenderPipelineDesc& desc);
PipelineValidationReport validateComputePipeline(const ComputePipelineDesc& desc);
arp::core::UniquePtr<RenderPipeline> createRenderPipeline(const RenderPipelineDesc& desc);
void applyPipelineStateValidation(PipelineValidationReport&, const PipelineStateValidationResult&);
arp::core::UniquePtr<ComputePipeline> createComputePipeline(const ComputePipelineDesc& desc);

arp::core::UniquePtr<CommandEncoder> createCommandEncoder(const CommandEncoderDesc& desc,
    const BindlessCrossExtension* issuer = nullptr,
    ResourceRegistryPtr registry = {});
arp::core::UniquePtr<SecondaryCommandEncoder> createSecondaryCommandEncoder(
    const SecondaryCommandEncoderDesc& desc);
arp::core::UniquePtr<RenderBundleEncoder> createRenderBundleEncoder(const RenderBundleDesc& desc);

arp::core::UniquePtr<Swapchain> createSwapchain(const SwapchainDesc& desc);

} // namespace arp::rhi::null_backend

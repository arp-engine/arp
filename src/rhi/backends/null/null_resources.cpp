#include "core/cpu_allocator.h"
#include "null_backend_internal.h"
#include "rhi/frontend/descriptor_resource_validation.h"
#include "rhi/frontend/heap_placement_contract.h"

#include <array>
#include <limits>
#include <utility>

namespace arp::rhi::null_backend {

namespace {

bool hasUsage(ResourceUsage usage)
{
    return static_cast<std::uint32_t>(usage) != 0;
}

bool hasUsage(TextureUsage usage)
{
    return static_cast<std::uint32_t>(usage) != 0;
}

bool hasResourceUsage(ResourceUsage usage, ResourceUsage required)
{
    return (usage & required) == required;
}

bool hasTextureUsage(TextureUsage usage, TextureUsage required)
{
    return (usage & required) == required;
}

bool addWouldOverflow(std::uint64_t lhs, std::uint64_t rhs)
{
    return rhs > std::numeric_limits<std::uint64_t>::max() - lhs;
}

ResourceMemoryClass memoryClassFor(MemoryUsage usage)
{
    switch (usage) {
    case MemoryUsage::GpuOnly:
        return ResourceMemoryClass::DeviceLocal;
    case MemoryUsage::CpuToGpu:
        return ResourceMemoryClass::Upload;
    case MemoryUsage::GpuToCpu:
        return ResourceMemoryClass::Readback;
    case MemoryUsage::Transient:
        return ResourceMemoryClass::Transient;
    }

    return ResourceMemoryClass::DeviceLocal;
}

[[maybe_unused]] bool multiplySize(std::uint64_t lhs, std::uint64_t rhs, std::uint64_t& result)
{
    if (lhs != 0 && rhs > std::numeric_limits<std::uint64_t>::max() / lhs) {
        return false;
    }
    result = lhs * rhs;
    return true;
}

std::uint64_t estimateTextureSize(const TextureDesc& desc)
{
    // Block-compressed formats (BC1/BC3/BC7) must size by 4x4 block, not
    // per-texel; bytesPerPixel returns 0 for them. The shared helper handles
    // both uncompressed and block-compressed sizing.
    if (desc.extent.width == 0 || desc.extent.height == 0 || desc.extent.depth == 0) {
        return 0;
    }
    return rhi::bytesForTextureMip(
        desc.format, desc.extent.width, desc.extent.height, desc.extent.depth);
}

bool isValidTextureExtent(const Extent3D& extent)
{
    return extent.width > 0 && extent.height > 0 && extent.depth > 0;
}

bool isDepthFormat(TextureFormat format)
{
    return format == TextureFormat::D32Float || format == TextureFormat::Depth24Stencil8;
}

bool aspectMatchesFormat(TextureFormat format, TextureAspect aspect)
{
    if (format == TextureFormat::Depth24Stencil8) {
        return aspect == TextureAspect::Depth || aspect == TextureAspect::Stencil;
    }
    if (isDepthFormat(format)) {
        return aspect == TextureAspect::Depth;
    }
    return aspect == TextureAspect::Color;
}

bool rangeWithinTexture(const TextureDesc& desc, const TextureSubresourceRange& range)
{
    if (range.mipLevelCount == 0 || range.arrayLayerCount == 0) {
        return false;
    }
    if (range.baseMipLevel >= desc.mipLevelCount || range.baseArrayLayer >= desc.arrayLayerCount) {
        return false;
    }
    if (range.mipLevelCount > desc.mipLevelCount - range.baseMipLevel) {
        return false;
    }
    if (range.arrayLayerCount > desc.arrayLayerCount - range.baseArrayLayer) {
        return false;
    }
    return aspectMatchesFormat(desc.format, range.aspect);
}

TextureSubresourceRange rangeFromViewDesc(const TextureViewDesc& desc)
{
    return {
        desc.baseMipLevel,
        desc.mipLevelCount,
        desc.baseArrayLayer,
        desc.arrayLayerCount,
        desc.aspect,
    };
}

bool textureViewDimensionMatches(const TextureDesc& texture, const TextureViewDesc& view)
{
    if (texture.extent.depth > 1) {
        return view.dimension == TextureViewDimension::Texture3D && view.baseArrayLayer == 0
            && view.arrayLayerCount == 1;
    }
    if (view.dimension == TextureViewDimension::Texture3D) {
        return false;
    }
    if (view.dimension == TextureViewDimension::TextureCube) {
        return texture.cubeCompatible && view.arrayLayerCount == 6
            && view.baseArrayLayer + view.arrayLayerCount <= texture.arrayLayerCount;
    }
    if (view.arrayLayerCount > 1) {
        return view.dimension == TextureViewDimension::Texture2DArray;
    }
    return true;
}

class NullBuffer final : public GpuBuffer, public StatefulResource {
public:
    NullBuffer(BufferDesc desc,
        ResourceAllocation allocation,
        MemoryLedgerPtr ledger,
        ResourceRegistryPtr registry)
        : desc_(std::move(desc)), allocation_(allocation), ledger_(std::move(ledger)),
          registry_(std::move(registry)), charge_(ledger_, allocation_.estimatedSizeBytes),
          id_(charge_.id()), state_(desc_.initialState)
    {
        bindOriginalRecordedState(state_);
        bindOriginalAllocationLifetime(charge_);
        registry_->registerBuffer(id_, desc_.usage, gpuWriteSealToken());
    }

    ~NullBuffer() override { registry_->unregisterResource(id_); }

    GpuResourceId id() const override { return id_; }

    const BufferDesc& desc() const override { return desc_; }

    const ResourceAllocation& allocation() const override { return allocation_; }

    ResourceState state() const override { return state_.state(); }

    GpuResourceId resourceId() const override { return id_; }

    ResourceState currentState() const override { return state_.state(); }

    bool beginStateRecording(core::MutationJournal& journal) override
    {
        return state_.prepare(journal);
    }

    ResourceBarrierResult transitionState(
        ResourceState before, ResourceState after, core::MutationJournal* journal) override
    {
        if (before != state_.state())
            return {false, "resource state transition rejected state mismatch", id_, before, after};
        if (before == after)
            return {false, "resource state transition rejected no-op state", id_, before, after};
        if (const auto* diagnostic = state_.writeBuffer(after, journal))
            return {false, diagnostic, id_, before, after};
        return {true, "", id_, before, after};
    }

private:
    BufferDesc desc_;
    ResourceAllocation allocation_;
    MemoryLedgerPtr ledger_;
    ResourceRegistryPtr registry_;
    MemoryLedgerLease charge_;
    GpuResourceId id_;
    common::RecordedResourceStateControl state_;
};

class NullBufferView final : public GpuBufferView {
public:
    NullBufferView(GpuResourceId bufferId,
        BufferViewDesc desc,
        MemoryLedgerPtr ledger,
        ResourceRegistryPtr registry)
        : bufferId_(bufferId), desc_(std::move(desc)), ledger_(std::move(ledger)),
          registry_(std::move(registry)), charge_(ledger_, 0u), id_(charge_.id())
    {
        registry_->registerBufferView(id_, bufferId_, desc_.usage);
    }

    ~NullBufferView() override { registry_->unregisterResource(id_); }

    GpuResourceId id() const override { return id_; }

    GpuResourceId bufferId() const override { return bufferId_; }

    const BufferViewDesc& desc() const override { return desc_; }

private:
    GpuResourceId bufferId_;
    BufferViewDesc desc_;
    MemoryLedgerPtr ledger_;
    ResourceRegistryPtr registry_;
    MemoryLedgerLease charge_;
    GpuResourceId id_;
};

class NullTexture final : public GpuTexture, public StatefulTextureResource {
public:
    NullTexture(TextureDesc desc,
        ResourceAllocation allocation,
        MemoryLedgerPtr ledger,
        ResourceRegistryPtr registry,
        std::shared_ptr<const MemoryLedgerLease> placementLease)
        : desc_(std::move(desc)), allocation_(allocation), ledger_(std::move(ledger)),
          registry_(std::move(registry)), state_(desc_, desc_.initialState),
          charge_(ledger_, allocation_.estimatedSizeBytes), id_(charge_.id()),
          placementLease_(std::move(placementLease))
    {
        bindOriginalRecordedState(state_);
        if (!placementLease_)
            bindOriginalAllocationLifetime(charge_);
        registry_->registerTexture(id_, desc_.usage);
    }

    ~NullTexture() override { registry_->unregisterResource(id_); }

    GpuResourceId id() const override { return id_; }

    const TextureDesc& desc() const override { return desc_; }

    const ResourceAllocation& allocation() const override { return allocation_; }

    ResourceState state() const override
    {
        return state_.uniformState(defaultTextureSubresourceRange(desc_))
            .value_or(ResourceState::Undefined);
    }

    ResourceState subresourceState(const TextureSubresourceRange& range) const override
    {
        return state_.uniformState(range).value_or(ResourceState::Undefined);
    }

    GpuResourceId resourceId() const override { return id_; }

    ResourceState currentState() const override { return state(); }

    bool beginStateRecording(core::MutationJournal& journal) override
    {
        return state_.prepare(journal);
    }

    ResourceBarrierResult transitionState(
        ResourceState before, ResourceState after, core::MutationJournal* journal) override
    {
        return transitionSubresourceState(
            defaultTextureSubresourceRange(desc_), before, after, journal);
    }

    ResourceBarrierResult transitionSubresourceState(const TextureSubresourceRange& range,
        ResourceState before,
        ResourceState after,
        core::MutationJournal* journal) override
    {
        ResourceBarrierResult result{false, "", id_, before, after};
        result.subresourceAware = true;
        if (before == after) {
            result.diagnostics = "resource state transition rejected no-op state";
            return result;
        }
        if (const auto* diagnostic = state_.write(range, before, after, journal)) {
            result.diagnostics = diagnostic;
            return result;
        }
        result.success = true;
        return result;
    }

private:
    TextureDesc desc_;
    ResourceAllocation allocation_;
    MemoryLedgerPtr ledger_;
    ResourceRegistryPtr registry_;
    common::RecordedTextureStateControl state_;
    MemoryLedgerLease charge_;
    GpuResourceId id_;
    std::shared_ptr<const MemoryLedgerLease> placementLease_;
};

class NullTextureView final : public GpuTextureView {
public:
    NullTextureView(GpuResourceId textureId,
        TextureUsage textureUsage,
        TextureViewDesc desc,
        MemoryLedgerPtr ledger,
        ResourceRegistryPtr registry)
        : textureId_(textureId), desc_(std::move(desc)), ledger_(std::move(ledger)),
          registry_(std::move(registry)), charge_(ledger_, 0u), id_(charge_.id())
    {
        registry_->registerTextureView(
            id_, textureId_, textureUsage, desc_.dimension, desc_.format, desc_.aspect);
    }

    ~NullTextureView() override { registry_->unregisterResource(id_); }

    GpuResourceId id() const override { return id_; }

    GpuResourceId textureId() const override { return textureId_; }

    const TextureViewDesc& desc() const override { return desc_; }

private:
    GpuResourceId textureId_;
    TextureViewDesc desc_;
    MemoryLedgerPtr ledger_;
    ResourceRegistryPtr registry_;
    MemoryLedgerLease charge_;
    GpuResourceId id_;
};

class NullSampler final : public GpuSampler {
public:
    NullSampler(SamplerDesc desc, MemoryLedgerPtr ledger, ResourceRegistryPtr registry)
        : desc_(std::move(desc)), ledger_(std::move(ledger)), registry_(std::move(registry)),
          charge_(ledger_, 0u), id_(charge_.id())
    {
        registry_->registerSampler(id_);
    }

    ~NullSampler() override { registry_->unregisterResource(id_); }

    GpuResourceId id() const override { return id_; }

    const SamplerDesc& desc() const override { return desc_; }

private:
    SamplerDesc desc_;
    MemoryLedgerPtr ledger_;
    ResourceRegistryPtr registry_;
    MemoryLedgerLease charge_;
    GpuResourceId id_;
};

} // namespace

bool MemoryLedger::canAllocate(std::uint64_t sizeBytes) const
{
    return sizeBytes <= std::numeric_limits<std::uint64_t>::max() - stats_.allocatedBytes;
}

GpuResourceId MemoryLedger::allocate(std::uint64_t sizeBytes)
{
    if (!canAllocate(sizeBytes)) {
        return {};
    }
    const auto id = allocateGpuResourceId();
    stats_.allocatedBytes += sizeBytes;
    ++stats_.liveResourceCount;
    return id;
}

void MemoryLedger::release(std::uint64_t sizeBytes)
{
    if (stats_.allocatedBytes >= sizeBytes) {
        stats_.allocatedBytes -= sizeBytes;
    } else {
        stats_.allocatedBytes = 0;
    }
    if (stats_.liveResourceCount > 0) {
        --stats_.liveResourceCount;
    }
}

MemoryStats MemoryLedger::stats() const
{
    return stats_;
}

bool ResourceRegistry::ownsResource(GpuResourceId id, RegisteredResourceKind kind) const
{
    std::scoped_lock lock(mutex_);
    for (const auto& resource : resources_)
        if (resource.id == id && resource.kind == kind)
            return true;
    return false;
}

void ResourceRegistry::registerBuffer(GpuResourceId id,
    ResourceUsage usage,
    std::weak_ptr<const GpuBufferWriteSealState> gpuWriteSeal)
{
    std::scoped_lock lock(mutex_);
    resources_.push_back({id, RegisteredResourceKind::Buffer, usage, TextureUsage::None, {}, {}});
    resources_.back().gpuWriteSeal = std::move(gpuWriteSeal);
}

bool ResourceRegistry::bufferGpuWritesSealed(GpuResourceId id) const
{
    std::scoped_lock lock(mutex_);
    for (const auto& resource : resources_)
        if (resource.id == id && resource.kind == RegisteredResourceKind::Buffer) {
            const auto seal = resource.gpuWriteSeal.lock();
            return seal && seal->writableDescriptorSealed();
        }
    return false;
}

bool ResourceRegistry::bufferGpuTransferWritesSealed(GpuResourceId id) const
{
    std::scoped_lock lock(mutex_);
    for (const auto& resource : resources_)
        if (resource.id == id && resource.kind == RegisteredResourceKind::Buffer) {
            const auto seal = resource.gpuWriteSeal.lock();
            return seal && seal->transferWritesSealed();
        }
    return false;
}

void ResourceRegistry::registerBufferView(
    GpuResourceId id, GpuResourceId parentBuffer, ResourceUsage usage)
{
    std::scoped_lock lock(mutex_);
    resources_.push_back(
        {id, RegisteredResourceKind::BufferView, usage, TextureUsage::None, parentBuffer, {}});
}

void ResourceRegistry::registerTexture(GpuResourceId id, TextureUsage usage)
{
    std::scoped_lock lock(mutex_);
    resources_.push_back({id, RegisteredResourceKind::Texture, ResourceUsage::None, usage, {}, {}});
}

void ResourceRegistry::registerTextureView(GpuResourceId id,
    GpuResourceId parentTexture,
    TextureUsage textureUsage,
    TextureViewDimension textureViewDimension,
    TextureFormat textureViewFormat,
    TextureAspect textureViewAspect)
{
    std::scoped_lock lock(mutex_);
    resources_.push_back({id,
        RegisteredResourceKind::TextureView,
        ResourceUsage::None,
        textureUsage,
        {},
        parentTexture});
    resources_.back().textureViewDimension = textureViewDimension;
    resources_.back().textureViewFormat = textureViewFormat;
    resources_.back().textureViewAspect = textureViewAspect;
}

void ResourceRegistry::registerSampler(GpuResourceId id)
{
    std::scoped_lock lock(mutex_);
    resources_.push_back(
        {id, RegisteredResourceKind::Sampler, ResourceUsage::None, TextureUsage::None, {}, {}});
}

void ResourceRegistry::unregisterResource(GpuResourceId id)
{
    std::scoped_lock lock(mutex_);
    for (auto it = resources_.begin(); it != resources_.end(); ++it) {
        if (it->id == id) {
            resources_.erase(it);
            return;
        }
    }
}

bool parentTextureIsRegistered(
    const std::pmr::vector<RegisteredResourceInfo>& resources, GpuResourceId parentTexture)
{
    for (const auto& resource : resources) {
        if (resource.id == parentTexture && resource.kind == RegisteredResourceKind::Texture) {
            return true;
        }
    }
    return false;
}

ValidationResult ResourceRegistry::validateDescriptorResource(DescriptorType type,
    DescriptorResourceKind resourceKind,
    GpuResourceId id,
    bool textureViewDimensionConstrained,
    TextureViewDimension textureViewDimension,
    bool depthTexture) const
{
    std::scoped_lock lock(mutex_);
    for (const auto& resource : resources_) {
        if (resource.id != id) {
            continue;
        }

        switch (type) {
        case DescriptorType::UniformBuffer:
            if (resource.kind != RegisteredResourceKind::Buffer
                || resourceKind != DescriptorResourceKind::Buffer) {
                return {false, "descriptor resource kind does not match registered buffer"};
            }
            if (!hasResourceUsage(resource.bufferUsage, ResourceUsage::UniformBuffer)) {
                return {false, "descriptor buffer resource usage does not include uniform buffer"};
            }
            return {true, ""};
        case DescriptorType::StorageBuffer:
        case DescriptorType::ReadOnlyStorageBuffer:
            if (resource.kind != RegisteredResourceKind::Buffer
                || resourceKind != DescriptorResourceKind::Buffer) {
                return {false, "descriptor resource kind does not match registered buffer"};
            }
            if (!hasResourceUsage(resource.bufferUsage, ResourceUsage::StorageBuffer)) {
                return {false, "descriptor buffer resource usage does not include storage buffer"};
            }
            if (type == DescriptorType::StorageBuffer) {
                const auto seal = resource.gpuWriteSeal.lock();
                if (seal && seal->writableDescriptorSealed())
                    return {false, "RHI-BUFFER-GPU-WRITES-SEALED"};
            }
            return {true, ""};
        case DescriptorType::SampledTexture:
            if (resource.kind != RegisteredResourceKind::TextureView
                || resourceKind != DescriptorResourceKind::TextureView) {
                return {false, "descriptor resource kind does not match registered texture view"};
            }
            if (!hasTextureUsage(resource.textureUsage, TextureUsage::Sampled)) {
                return {
                    false, "descriptor texture resource usage does not include sampled texture"};
            }
            if (!parentTextureIsRegistered(resources_, resource.parentTexture)) {
                return validateDescriptorTextureParent(false);
            }
            if (textureViewDimensionConstrained
                && resource.textureViewDimension != textureViewDimension) {
                return {false, "descriptor texture view dimension does not match layout contract"};
            }
            return validateSampledDepthView(
                depthTexture, resource.textureViewFormat, resource.textureViewAspect);
        case DescriptorType::StorageTexture:
            if (resource.kind != RegisteredResourceKind::TextureView
                || resourceKind != DescriptorResourceKind::TextureView) {
                return {false, "descriptor resource kind does not match registered texture view"};
            }
            if (!hasTextureUsage(resource.textureUsage, TextureUsage::Storage)) {
                return {
                    false, "descriptor texture resource usage does not include storage texture"};
            }
            if (!parentTextureIsRegistered(resources_, resource.parentTexture)) {
                return validateDescriptorTextureParent(false);
            }
            if (textureViewDimensionConstrained
                && resource.textureViewDimension != textureViewDimension) {
                return {false, "descriptor texture view dimension does not match layout contract"};
            }
            return {true, ""};
        case DescriptorType::Sampler:
            if (resource.kind != RegisteredResourceKind::Sampler
                || resourceKind != DescriptorResourceKind::Sampler) {
                return {false, "descriptor resource kind does not match registered sampler"};
            }
            return {true, ""};
        case DescriptorType::InputAttachment:
            // Input attachment is a tile-local attachment read within a render
            // pass. On the Null backend (testing), validate it as a texture
            // view with render-attachment usage.
            if (resource.kind != RegisteredResourceKind::TextureView
                || resourceKind != DescriptorResourceKind::TextureView) {
                return {false, "descriptor resource kind does not match registered texture view"};
            }
            if (!hasTextureUsage(resource.textureUsage, TextureUsage::RenderAttachment)) {
                return {
                    false, "descriptor texture resource usage does not include render attachment"};
            }
            if (!parentTextureIsRegistered(resources_, resource.parentTexture)) {
                return validateDescriptorTextureParent(false);
            }
            return {true, ""};
        }
    }

    return {false, "RHI-DESCRIPTOR-RESOURCE-NOT-LIVE: descriptor resource id is not registered"};
}

arp::core::UniquePtr<GpuBuffer> createBuffer(
    const BufferDesc& desc, const MemoryLedgerPtr& ledger, const ResourceRegistryPtr& registry)
{
    if (desc.sizeBytes == 0 || !hasUsage(desc.usage) || !ledger->canAllocate(desc.sizeBytes)) {
        return nullptr;
    }

    ResourceAllocation allocation;
    allocation.memoryClass = memoryClassFor(desc.memory);
    allocation.estimatedSizeBytes = desc.sizeBytes;
    allocation.nativeHandleExposed = false;
    allocation.backendFallbackHiddenFromUpperLayer = true;
    return arp::core::makeUnique<NullBuffer>(desc, allocation, ledger, registry);
}

arp::core::UniquePtr<GpuBufferView> createBufferView(const GpuBuffer& buffer,
    const BufferViewDesc& desc,
    const MemoryLedgerPtr& ledger,
    const ResourceRegistryPtr& registry)
{
    if (!buffer.id().isValid()) {
        return nullptr;
    }

    BufferViewDesc resolved = desc;
    if (resolved.usage == ResourceUsage::None) {
        resolved.usage = buffer.desc().usage;
    }
    if (resolved.sizeBytes == 0 || resolved.strideBytes == 0 || !hasUsage(resolved.usage)
        || !hasResourceUsage(buffer.desc().usage, resolved.usage)
        || addWouldOverflow(resolved.offsetBytes, resolved.sizeBytes)
        || resolved.offsetBytes + resolved.sizeBytes > buffer.desc().sizeBytes
        || resolved.strideBytes > resolved.sizeBytes) {
        return nullptr;
    }

    return arp::core::makeUnique<NullBufferView>(buffer.id(), resolved, ledger, registry);
}

arp::core::UniquePtr<GpuTexture> createTexture(const TextureDesc& desc,
    const MemoryLedgerPtr& ledger,
    const ResourceRegistryPtr& registry,
    std::shared_ptr<const MemoryLedgerLease> placementLease)
{
    std::uint64_t estimatedSize = 0u;
    if (placementLease) {
        if (!placementLease->belongsTo(ledger)
            || HeapPlacementContract::texture(desc, estimatedSize))
            return nullptr;
    } else {
        estimatedSize = estimateTextureSize(desc);
    }
    if (!isValidTextureExtent(desc.extent) || desc.format == TextureFormat::Unknown
        || desc.mipLevelCount == 0 || desc.arrayLayerCount == 0 || !hasUsage(desc.usage)
        || estimatedSize == 0
        || (!placementLease && desc.memory == MemoryUsage::Transient
            && !hasTextureUsage(desc.usage, TextureUsage::RenderAttachment))
        || !ledger->canAllocate(estimatedSize)) {
        return nullptr;
    }

    ResourceAllocation allocation;
    allocation.memoryClass = memoryClassFor(desc.memory);
    allocation.estimatedSizeBytes = estimatedSize;
    allocation.nativeHandleExposed = false;
    allocation.backendFallbackHiddenFromUpperLayer = true;
    return arp::core::makeUnique<NullTexture>(
        desc, allocation, ledger, registry, std::move(placementLease));
}

arp::core::UniquePtr<GpuTextureView> createTextureView(const GpuTexture& texture,
    const TextureViewDesc& desc,
    const MemoryLedgerPtr& ledger,
    const ResourceRegistryPtr& registry)
{
    if (!texture.id().isValid()) {
        return nullptr;
    }
    TextureViewDesc resolved = desc;
    if (resolved.format == TextureFormat::Unknown) {
        resolved.format = texture.desc().format;
    }
    if (resolved.usage == TextureUsage::None) {
        resolved.usage = texture.desc().usage;
    }
    if (resolved.format == TextureFormat::Unknown || resolved.format != texture.desc().format
        || !hasUsage(resolved.usage) || !hasTextureUsage(texture.desc().usage, resolved.usage)
        || !rangeWithinTexture(texture.desc(), rangeFromViewDesc(resolved))
        || !textureViewDimensionMatches(texture.desc(), resolved)) {
        return nullptr;
    }
    return arp::core::makeUnique<NullTextureView>(
        texture.id(), resolved.usage, resolved, ledger, registry);
}

arp::core::UniquePtr<GpuSampler> createSampler(
    const SamplerDesc& desc, const MemoryLedgerPtr& ledger, const ResourceRegistryPtr& registry)
{
    return arp::core::makeUnique<NullSampler>(desc, ledger, registry);
}

} // namespace arp::rhi::null_backend

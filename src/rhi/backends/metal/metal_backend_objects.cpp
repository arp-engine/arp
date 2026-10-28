#include "rhi/backends/metal/metal_backend_objects.h"

#include "rhi/frontend/buffer_placement.h"
#include "rhi/frontend/pipeline_state_copy.h"
#include "rhi/gpu_common.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <utility>

namespace arp::rhi_metal {

namespace {

bool hasTextureUsage(rhi::TextureUsage usage, rhi::TextureUsage required)
{
    return (usage & required) == required;
}

bool hasOnlyKnownResourceUsage(rhi::ResourceUsage usage)
{
    constexpr std::uint32_t known = static_cast<std::uint32_t>(rhi::ResourceUsage::VertexBuffer)
        | static_cast<std::uint32_t>(rhi::ResourceUsage::IndexBuffer)
        | static_cast<std::uint32_t>(rhi::ResourceUsage::UniformBuffer)
        | static_cast<std::uint32_t>(rhi::ResourceUsage::StorageBuffer)
        | static_cast<std::uint32_t>(rhi::ResourceUsage::CopySrc)
        | static_cast<std::uint32_t>(rhi::ResourceUsage::CopyDst)
        | static_cast<std::uint32_t>(rhi::ResourceUsage::IndirectBuffer);
    return static_cast<std::uint32_t>(usage) != 0u
        && (static_cast<std::uint32_t>(usage) & ~known) == 0u;
}

bool hasOnlyKnownTextureUsage(rhi::TextureUsage usage)
{
    constexpr std::uint32_t known = static_cast<std::uint32_t>(rhi::TextureUsage::Sampled)
        | static_cast<std::uint32_t>(rhi::TextureUsage::Storage)
        | static_cast<std::uint32_t>(rhi::TextureUsage::RenderAttachment)
        | static_cast<std::uint32_t>(rhi::TextureUsage::CopySrc)
        | static_cast<std::uint32_t>(rhi::TextureUsage::CopyDst);
    return static_cast<std::uint32_t>(usage) != 0u
        && (static_cast<std::uint32_t>(usage) & ~known) == 0u;
}

bool validMetalBufferDesc(const rhi::BufferDesc& desc)
{
    return desc.sizeBytes != 0u && hasOnlyKnownResourceUsage(desc.usage)
        && desc.memory != rhi::MemoryUsage::Transient;
}

bool validMetalTextureDesc(const rhi::TextureDesc& desc)
{
    if (desc.extent.width == 0u || desc.extent.height == 0u || desc.extent.depth == 0u
        || desc.format == rhi::TextureFormat::Unknown || !hasOnlyKnownTextureUsage(desc.usage)
        || desc.mipLevelCount == 0u || desc.arrayLayerCount == 0u || desc.sampleCount == 0u) {
        return false;
    }

    const bool is3D = desc.extent.depth > 1u;
    if ((desc.cubeCompatible
            && (desc.extent.width != desc.extent.height || desc.extent.depth != 1u
                || desc.sampleCount != 1u || desc.arrayLayerCount % 6u != 0u))
        || (is3D && (desc.cubeCompatible || desc.arrayLayerCount != 1u))
        || (desc.sampleCount > 1u && (is3D || desc.mipLevelCount != 1u))) {
        return false;
    }
    return true;
}

bool validMetalTransientTextureUsage(const rhi::TextureDesc& desc)
{
    return desc.memory != rhi::MemoryUsage::Transient
        || (hasTextureUsage(desc.usage, rhi::TextureUsage::RenderAttachment)
            && !hasTextureUsage(desc.usage, rhi::TextureUsage::Sampled)
            && !hasTextureUsage(desc.usage, rhi::TextureUsage::Storage)
            && !hasTextureUsage(desc.usage, rhi::TextureUsage::CopySrc)
            && !hasTextureUsage(desc.usage, rhi::TextureUsage::CopyDst));
}

bool textureAspectMatchesFormat(rhi::TextureFormat format, rhi::TextureAspect aspect)
{
    if (format == rhi::TextureFormat::Depth24Stencil8) {
        return aspect == rhi::TextureAspect::Depth || aspect == rhi::TextureAspect::Stencil;
    }
    if (rhi::isDepthTextureFormat(format)) {
        return aspect == rhi::TextureAspect::Depth;
    }
    return aspect == rhi::TextureAspect::Color;
}

bool textureRangeWithin(const rhi::TextureDesc& desc, const rhi::TextureSubresourceRange& range)
{
    return range.mipLevelCount != 0u && range.arrayLayerCount != 0u
        && range.baseMipLevel < desc.mipLevelCount && range.baseArrayLayer < desc.arrayLayerCount
        && range.mipLevelCount <= desc.mipLevelCount - range.baseMipLevel
        && range.arrayLayerCount <= desc.arrayLayerCount - range.baseArrayLayer
        && textureAspectMatchesFormat(desc.format, range.aspect);
}

bool textureViewCompatible(const rhi::TextureDesc& texture, const rhi::TextureViewDesc& view)
{
    if (view.mipLevelCount == 0u || view.arrayLayerCount == 0u
        || view.baseMipLevel >= texture.mipLevelCount
        || view.baseArrayLayer >= texture.arrayLayerCount
        || view.mipLevelCount > texture.mipLevelCount - view.baseMipLevel
        || view.arrayLayerCount > texture.arrayLayerCount - view.baseArrayLayer
        || !textureAspectMatchesFormat(texture.format, view.aspect)) {
        return false;
    }
    if (texture.extent.depth > 1u) {
        return view.dimension == rhi::TextureViewDimension::Texture3D && view.baseArrayLayer == 0u
            && view.arrayLayerCount == 1u;
    }
    if (view.dimension == rhi::TextureViewDimension::Texture3D) {
        return false;
    }
    if (view.dimension == rhi::TextureViewDimension::TextureCube) {
        return texture.cubeCompatible && view.arrayLayerCount == 6u
            && (view.baseArrayLayer % 6u) == 0u;
    }
    if (view.arrayLayerCount > 1u) {
        return view.dimension == rhi::TextureViewDimension::Texture2DArray;
    }
    return true;
}

rhi::ResourceMemoryClass memoryClassFor(MetalRhiMemoryPolicy policy)
{
    switch (policy) {
    case MetalRhiMemoryPolicy::PrivateStorage:
        return rhi::ResourceMemoryClass::DeviceLocal;
    case MetalRhiMemoryPolicy::MemorylessTransient:
        return rhi::ResourceMemoryClass::Transient;
    case MetalRhiMemoryPolicy::SharedUpload:
        return rhi::ResourceMemoryClass::Upload;
    case MetalRhiMemoryPolicy::ManagedReadback:
        return rhi::ResourceMemoryClass::Readback;
    }
    return rhi::ResourceMemoryClass::DeviceLocal;
}

class MetalRhiBuffer final : public rhi::GpuBuffer {
public:
    MetalRhiBuffer(rhi::GpuResourceId id,
        rhi::BufferDesc desc,
        rhi::ResourceAllocation allocation,
        std::shared_ptr<void> allocationReleaseToken,
        std::shared_ptr<MetalRhiNativeBufferPayload> nativePayload,
        std::weak_ptr<MetalRhiResourceRegistry> registry)
        : id_(id), desc_(std::move(desc)), allocation_(allocation),
          allocationReleaseToken_(std::move(allocationReleaseToken)),
          nativePayload_(std::move(nativePayload)), registry_(std::move(registry)),
          state_(desc_.initialState)
    {
        bindOriginalRecordedState(state_);
        if (nativePayload_ && allocationReleaseToken_)
            nativePayload_->resource.allocationReleaseToken = allocationReleaseToken_;
    }

    ~MetalRhiBuffer() override
    {
        if (auto registry = registry_.lock()) {
            registry->unregisterResource(id_);
        }
    }

    rhi::GpuResourceId id() const override { return id_; }

    const rhi::BufferDesc& desc() const override { return desc_; }

    const rhi::ResourceAllocation& allocation() const override { return allocation_; }

    rhi::ResourceState state() const override { return state_.state(); }

    const char* recordState(rhi::ResourceState state, core::MutationJournal* journal) noexcept
    {
        return state_.writeBuffer(state, journal);
    }

    bool canPrepareRecordedState(const core::MutationJournal& journal) const noexcept
    {
        return state_.canPrepare(journal);
    }

    bool prepareRecordedState(core::MutationJournal* journal)
    {
        return journal ? state_.prepare(*journal) : state_.active() && !state_.pending();
    }

    std::shared_ptr<MetalRhiNativeBufferPayload> nativePayload() const { return nativePayload_; }

private:
    rhi::GpuResourceId id_;
    rhi::BufferDesc desc_;
    rhi::ResourceAllocation allocation_;
    std::shared_ptr<void> allocationReleaseToken_;
    std::shared_ptr<MetalRhiNativeBufferPayload> nativePayload_;
    std::weak_ptr<MetalRhiResourceRegistry> registry_;
    rhi::common::RecordedResourceStateControl state_;
};

class MetalRhiTexture final : public rhi::GpuTexture {
public:
    auto lifetimeObserver() const noexcept
    {
        return core::LifetimeObserver<const rhi::GpuTexture>(this, lifetime_.ticket());
    }

    MetalRhiTexture(rhi::GpuResourceId id,
        rhi::TextureDesc desc,
        rhi::ResourceAllocation allocation,
        std::shared_ptr<void> allocationReleaseToken,
        std::shared_ptr<MetalRhiNativeTexturePayload> nativePayload,
        std::weak_ptr<MetalRhiResourceRegistry> registry)
        : id_(id), desc_(std::move(desc)), allocation_(allocation),
          allocationReleaseToken_(std::move(allocationReleaseToken)),
          nativePayload_(std::move(nativePayload)), registry_(std::move(registry)),
          state_(desc_, desc_.initialState)
    {
        bindOriginalRecordedState(state_);
        if (nativePayload_ && allocationReleaseToken_)
            nativePayload_->resource.allocationReleaseToken = allocationReleaseToken_;
        if (nativePayload_ && nativePayload_->originalContentExclusive())
            originalNativeIdentity_ = MetalRhiNativeResourceObservation(nativePayload_->resource);
    }

    ~MetalRhiTexture() override
    {
        if (auto registry = registry_.lock()) {
            registry->unregisterResource(id_);
        }
    }

    rhi::GpuResourceId id() const override { return id_; }

    const rhi::TextureDesc& desc() const override { return desc_; }

    const rhi::ResourceAllocation& allocation() const override { return allocation_; }

    rhi::ResourceState state() const override
    {
        return state_.uniformState(rhi::defaultTextureSubresourceRange(desc_))
            .value_or(rhi::ResourceState::Undefined);
    }

    rhi::ResourceState subresourceState(const rhi::TextureSubresourceRange& range) const override
    {
        return state_.uniformState(range).value_or(rhi::ResourceState::Undefined);
    }

    auto uniformState(const rhi::TextureSubresourceRange& range) const noexcept
    {
        return state_.uniformState(range);
    }

    bool prepareRecordedState(core::MutationJournal* journal)
    {
        return journal ? state_.prepare(*journal) : state_.active() && !state_.pending();
    }

    const char* recordSubresourceState(const rhi::TextureSubresourceRange& range,
        rhi::ResourceState before,
        rhi::ResourceState after,
        core::MutationJournal* journal) noexcept
    {
        return state_.write(range, before, after, journal);
    }

    std::shared_ptr<MetalRhiNativeTexturePayload> nativePayload() const
    {
        // A completed content stamp belongs to the original allocation/object.
        // Freezing only at descriptor publication would accept replacement of
        // the image after upload completion and before its first publication.
        if (nativePayload_ && nativePayload_->originalContentExclusive()
            && !originalNativeIdentity_.matches(nativePayload_->resource))
            return {};
        return nativePayload_;
    }

private:
    core::LifetimeEpoch lifetime_;
    rhi::GpuResourceId id_;
    rhi::TextureDesc desc_;
    rhi::ResourceAllocation allocation_;
    std::shared_ptr<void> allocationReleaseToken_;
    std::shared_ptr<MetalRhiNativeTexturePayload> nativePayload_;
    MetalRhiNativeResourceObservation originalNativeIdentity_;
    std::weak_ptr<MetalRhiResourceRegistry> registry_;
    rhi::common::RecordedTextureStateControl state_;
};

class MetalRhiBufferView final : public rhi::GpuBufferView {
public:
    MetalRhiBufferView(rhi::GpuResourceId id, rhi::GpuResourceId bufferId, rhi::BufferViewDesc desc)
        : id_(id), bufferId_(bufferId), desc_(std::move(desc))
    {}

    rhi::GpuResourceId id() const override { return id_; }

    rhi::GpuResourceId bufferId() const override { return bufferId_; }

    const rhi::BufferViewDesc& desc() const override { return desc_; }

private:
    rhi::GpuResourceId id_;
    rhi::GpuResourceId bufferId_;
    rhi::BufferViewDesc desc_;
};

class MetalRhiTextureView final : public rhi::GpuTextureView {
public:
    auto lifetimeObserver() const noexcept
    {
        return core::LifetimeObserver<const rhi::GpuTextureView>(this, lifetime_.ticket());
    }

    MetalRhiTextureView(rhi::GpuResourceId id,
        rhi::GpuResourceId textureId,
        rhi::TextureViewDesc desc,
        std::shared_ptr<MetalRhiNativeTextureViewPayload> nativePayload,
        std::weak_ptr<MetalRhiResourceRegistry> registry)
        : id_(id), textureId_(textureId), desc_(std::move(desc)),
          nativePayload_(std::move(nativePayload)), registry_(std::move(registry))
    {
        if (nativePayload_ && nativePayload_->originalContentExclusive())
            originalNativeIdentity_ = MetalRhiNativeResourceObservation(nativePayload_->resource);
    }

    ~MetalRhiTextureView() override
    {
        if (auto registry = registry_.lock()) {
            registry->unregisterResource(id_);
        }
    }

    rhi::GpuResourceId id() const override { return id_; }

    rhi::GpuResourceId textureId() const override { return textureId_; }

    const rhi::TextureViewDesc& desc() const override { return desc_; }

    std::shared_ptr<MetalRhiNativeTextureViewPayload> nativePayload() const
    {
        if (nativePayload_ && nativePayload_->originalContentExclusive()
            && !originalNativeIdentity_.matches(nativePayload_->resource))
            return {};
        return nativePayload_;
    }

private:
    core::LifetimeEpoch lifetime_;
    rhi::GpuResourceId id_;
    rhi::GpuResourceId textureId_;
    rhi::TextureViewDesc desc_;
    std::shared_ptr<MetalRhiNativeTextureViewPayload> nativePayload_;
    MetalRhiNativeResourceObservation originalNativeIdentity_;
    std::weak_ptr<MetalRhiResourceRegistry> registry_;
};

class MetalRhiSampler final : public rhi::GpuSampler {
public:
    MetalRhiSampler(rhi::GpuResourceId id,
        rhi::SamplerDesc desc,
        std::shared_ptr<MetalRhiNativeSamplerPayload> nativePayload,
        std::weak_ptr<MetalRhiResourceRegistry> registry)
        : id_(id), desc_(std::move(desc)), nativePayload_(std::move(nativePayload)),
          registry_(std::move(registry))
    {}

    ~MetalRhiSampler() override
    {
        if (auto registry = registry_.lock()) {
            registry->unregisterResource(id_);
        }
    }

    rhi::GpuResourceId id() const override { return id_; }

    const rhi::SamplerDesc& desc() const override { return desc_; }

    std::shared_ptr<MetalRhiNativeSamplerPayload> nativePayload() const { return nativePayload_; }

private:
    rhi::GpuResourceId id_;
    rhi::SamplerDesc desc_;
    std::shared_ptr<MetalRhiNativeSamplerPayload> nativePayload_;
    std::weak_ptr<MetalRhiResourceRegistry> registry_;
};

class MetalRhiPipelineLayout final : public rhi::PipelineLayout {
public:
    MetalRhiPipelineLayout(rhi::PipelineLayoutDesc desc,
        rhi::semantics::DirectObjectTelemetryLease telemetryLease,
        rhi::semantics::DirectObjectBudgetLease budgetLease)
        : desc_(std::move(desc)), telemetryLease_(std::move(telemetryLease)),
          budgetLease_(std::move(budgetLease))
    {}

    const rhi::PipelineLayoutDesc& desc() const override { return desc_; }

    std::uint32_t descriptorSetCount() const override
    {
        return static_cast<std::uint32_t>(desc_.descriptorSets.size());
    }

    std::uint32_t descriptorTableCount() const override
    {
        return static_cast<std::uint32_t>(desc_.descriptorTables.size());
    }

    std::uint32_t pushConstantByteCount() const override
    {
        std::uint32_t byteCount = 0;
        for (const auto& range : desc_.pushConstantRanges) {
            byteCount = std::max(byteCount, range.offsetBytes + range.sizeBytes);
        }
        return byteCount;
    }

    bool backendFallbacksHiddenFromUpperLayer() const override { return true; }

    bool nativeHandleExposedToUpperLayer() const override { return false; }

private:
    rhi::PipelineLayoutDesc desc_;
    rhi::semantics::DirectObjectTelemetryLease telemetryLease_;
    rhi::semantics::DirectObjectBudgetLease budgetLease_;
};

MetalRhiNativeResourceInspection inspectPayload(const MetalRhiNativeResourcePayload* payload)
{
    MetalRhiNativeResourceInspection inspection;
    if (!payload) {
        return inspection;
    }
    inspection.backendPayloadPresent = true;
    inspection.nativeRuntimeBacked = payload->nativeRuntimeBacked;
    inspection.nativeRuntimeDiagnosticOnly = payload->nativeRuntimeDiagnosticOnly;
    inspection.nativeHandleExposedToUpperLayer = payload->nativeHandleExposedToUpperLayer;
    inspection.storageMode = payload->storageMode;
    return inspection;
}

} // namespace

std::uint64_t estimateMetalRhiTextureBytes(const rhi::TextureDesc& desc)
{
    // Block-compressed formats (BC1/BC3/BC7) size by 4x4 block, not per-texel;
    // bytesPerPixel returns 0 for them. The shared helper handles both.
    return rhi::estimateTexturePlacementRequirements(desc).sizeBytes;
}

rhi::ResourceAllocation metalRhiResourceAllocation(const MetalRhiAllocation& allocation)
{
    return {
        memoryClassFor(allocation.policy),
        allocation.sizeBytes,
        allocation.nativeAllocationExposedToUpperLayer,
        true,
    };
}

arp::core::UniquePtr<rhi::GpuBuffer> createMetalRhiBuffer(rhi::GpuResourceId id,
    const rhi::BufferDesc& desc,
    const rhi::ResourceAllocation& allocation,
    std::shared_ptr<void> allocationReleaseToken,
    std::shared_ptr<MetalRhiResourceRegistry> registry,
    std::shared_ptr<MetalRhiNativeBufferPayload> preparedPayload)
{
    if (const auto* id =
            rhi::HostVisibleDeviceLocalBufferContract::validate(desc, rhi::Backend::Metal, false)) {
        rhi::RhiErrorHandler::get().report(rhi::RhiSeverity::Error, id);
        return {};
    }
    if (!validMetalBufferDesc(desc)) {
        // Metal memoryless storage is a texture-only attachment concept. A
        // transient buffer must not be silently created with private/shared
        // storage because that changes the lifetime and aliasing contract.
        return nullptr;
    }
    auto nativePayload = preparedPayload
        ? std::move(preparedPayload)
        : createMetalRhiNativeBufferPayload(desc, metalRhiMemoryPolicyFor(desc.memory));
    if (!nativePayload) {
        return nullptr;
    }
    return arp::core::makeUnique<MetalRhiBuffer>(id,
        desc,
        allocation,
        std::move(allocationReleaseToken),
        std::move(nativePayload),
        std::move(registry));
}

arp::core::UniquePtr<rhi::GpuBufferView> createMetalRhiBufferView(
    rhi::GpuResourceId id, const rhi::GpuBuffer& buffer, const rhi::BufferViewDesc& desc)
{
    if (!buffer.id().isValid()) {
        return nullptr;
    }
    auto resolved = desc;
    if (resolved.usage == rhi::ResourceUsage::None) {
        resolved.usage = buffer.desc().usage;
    }
    if (resolved.sizeBytes == 0u || resolved.strideBytes == 0u
        || !hasOnlyKnownResourceUsage(resolved.usage)
        || (buffer.desc().usage & resolved.usage) != resolved.usage
        || resolved.offsetBytes > std::numeric_limits<std::uint64_t>::max() - resolved.sizeBytes
        || resolved.offsetBytes + resolved.sizeBytes > buffer.desc().sizeBytes
        || resolved.strideBytes > resolved.sizeBytes) {
        return nullptr;
    }
    return arp::core::makeUnique<MetalRhiBufferView>(id, buffer.id(), std::move(resolved));
}

arp::core::UniquePtr<rhi::GpuTexture> createMetalRhiTexture(rhi::GpuResourceId id,
    const rhi::TextureDesc& desc,
    const rhi::ResourceAllocation& allocation,
    std::shared_ptr<void> allocationReleaseToken,
    std::shared_ptr<MetalRhiResourceRegistry> registry)
{
    if (!validMetalTextureDesc(desc) || !validMetalTransientTextureUsage(desc)) {
        return nullptr;
    }
    auto nativePayload =
        createMetalRhiNativeTexturePayload(desc, metalRhiMemoryPolicyFor(desc.memory));
    if (!nativePayload) {
        return nullptr;
    }
    return arp::core::makeUnique<MetalRhiTexture>(id,
        desc,
        allocation,
        std::move(allocationReleaseToken),
        std::move(nativePayload),
        std::move(registry));
}

arp::core::UniquePtr<rhi::GpuTexture> createMetalRhiTexture(rhi::GpuResourceId id,
    const rhi::TextureDesc& desc,
    const rhi::ResourceAllocation& allocation,
    std::shared_ptr<MetalRhiNativeTexturePayload> nativePayload,
    std::shared_ptr<MetalRhiResourceRegistry> registry)
{
    if (!validMetalTextureDesc(desc) || !nativePayload
        || (desc.memory == rhi::MemoryUsage::Transient
            && (!nativePayload->aliasHeap || !nativePayload->resource.nativeRuntimeBacked
                || !nativePayload->resource.nativeObject
                || nativePayload->resource.storageMode != MetalRhiNativeStorageMode::Private))) {
        return nullptr;
    }
    return arp::core::makeUnique<MetalRhiTexture>(id,
        desc,
        allocation,
        std::shared_ptr<void>{},
        std::move(nativePayload),
        std::move(registry));
}

arp::core::UniquePtr<rhi::GpuTextureView> createMetalRhiTextureView(rhi::GpuResourceId id,
    const rhi::GpuTexture& texture,
    const rhi::TextureViewDesc& desc,
    std::shared_ptr<MetalRhiResourceRegistry> registry)
{
    const auto* metalTexture = dynamic_cast<const MetalRhiTexture*>(&texture);
    const auto texturePayload = metalTexture ? metalTexture->nativePayload() : nullptr;
    if (!metalTexture || !texturePayload) {
        return nullptr;
    }
    auto resolved = desc;
    if (resolved.format == rhi::TextureFormat::Unknown) {
        resolved.format = texture.desc().format;
    }
    if (resolved.usage == rhi::TextureUsage::None) {
        resolved.usage = texture.desc().usage;
    }
    if (resolved.format != texture.desc().format || resolved.usage == rhi::TextureUsage::None
        || !hasTextureUsage(texture.desc().usage, resolved.usage)
        || !textureViewCompatible(texture.desc(), resolved)) {
        return nullptr;
    }
    auto nativePayload = createMetalRhiNativeTextureViewPayload(texturePayload, resolved);
    if (!nativePayload) {
        return nullptr;
    }
    return arp::core::makeUnique<MetalRhiTextureView>(
        id, texture.id(), resolved, std::move(nativePayload), std::move(registry));
}

arp::core::UniquePtr<rhi::GpuSampler> createMetalRhiSampler(rhi::GpuResourceId id,
    const rhi::SamplerDesc& desc,
    std::shared_ptr<MetalRhiResourceRegistry> registry,
    bool argumentBufferCompatible)
{
    auto nativePayload = createMetalRhiNativeSamplerPayload(desc, argumentBufferCompatible);
    if (!nativePayload) {
        return nullptr;
    }
    return arp::core::makeUnique<MetalRhiSampler>(
        id, desc, std::move(nativePayload), std::move(registry));
}

MetalRhiNativeResourceInspection inspectMetalRhiNativePayload(const rhi::GpuBuffer& buffer)
{
    const auto* metalBuffer = dynamic_cast<const MetalRhiBuffer*>(&buffer);
    return inspectPayload(metalBuffer && metalBuffer->nativePayload()
            ? &metalBuffer->nativePayload()->resource
            : nullptr);
}

MetalRhiNativeResourceInspection inspectMetalRhiNativePayload(const rhi::GpuTexture& texture)
{
    const auto* metalTexture = dynamic_cast<const MetalRhiTexture*>(&texture);
    return inspectPayload(metalTexture && metalTexture->nativePayload()
            ? &metalTexture->nativePayload()->resource
            : nullptr);
}

MetalRhiNativeResourceInspection inspectMetalRhiNativePayload(const rhi::GpuTextureView& view)
{
    const auto* metalView = dynamic_cast<const MetalRhiTextureView*>(&view);
    return inspectPayload(
        metalView && metalView->nativePayload() ? &metalView->nativePayload()->resource : nullptr);
}

MetalRhiNativeResourceInspection inspectMetalRhiNativePayload(const rhi::GpuSampler& sampler)
{
    const auto* metalSampler = dynamic_cast<const MetalRhiSampler*>(&sampler);
    return inspectPayload(metalSampler && metalSampler->nativePayload()
            ? &metalSampler->nativePayload()->resource
            : nullptr);
}

std::shared_ptr<MetalRhiNativeBufferPayload> metalRhiNativePayloadFor(const rhi::GpuBuffer& buffer)
{
    const auto* metalBuffer = dynamic_cast<const MetalRhiBuffer*>(&buffer);
    return metalBuffer ? metalBuffer->nativePayload() : nullptr;
}

std::shared_ptr<MetalRhiNativeTexturePayload> metalRhiNativePayloadFor(
    const rhi::GpuTexture& texture)
{
    const auto* metalTexture = dynamic_cast<const MetalRhiTexture*>(&texture);
    return metalTexture ? metalTexture->nativePayload() : nullptr;
}

std::shared_ptr<MetalRhiNativeTextureViewPayload> metalRhiNativePayloadFor(
    const rhi::GpuTextureView& textureView)
{
    const auto* metalTextureView = dynamic_cast<const MetalRhiTextureView*>(&textureView);
    return metalTextureView ? metalTextureView->nativePayload() : nullptr;
}

std::shared_ptr<MetalRhiNativeSamplerPayload> metalRhiNativePayloadFor(
    const rhi::GpuSampler& sampler)
{
    const auto* metalSampler = dynamic_cast<const MetalRhiSampler*>(&sampler);
    return metalSampler ? metalSampler->nativePayload() : nullptr;
}

namespace {

template <typename Resource, typename MetalResource>
rhi::ResourceBarrierResult setMetalState(Resource& resource,
    const rhi::ResourceBarrierDesc& desc,
    bool subresourceAware,
    core::MutationJournal* journal)
{
    rhi::ResourceBarrierResult result;
    result.resource = resource.id();
    result.before = desc.before;
    result.after = desc.after;
    result.subresourceAware = subresourceAware;
    result.stageAccessAware = true;
    result.sourceStages = desc.sourceStages;
    result.destinationStages = desc.destinationStages;
    result.sourceAccess = desc.sourceAccess;
    result.destinationAccess = desc.destinationAccess;
    auto* metalResource = dynamic_cast<MetalResource*>(&resource);
    if (!metalResource || desc.resource != resource.id()) {
        result.diagnostics = "RHI-METAL-BARRIER-RESOURCE-MISMATCH";
        return result;
    }
    if (resource.state() != desc.before) {
        result.diagnostics = "RHI-METAL-BARRIER-BEFORE-STATE-MISMATCH";
        return result;
    }
    if (const auto* diagnostic = metalResource->recordState(desc.after, journal)) {
        result.diagnostics = diagnostic;
        return result;
    }
    result.success = true;
    return result;
}

} // namespace

rhi::ResourceBarrierResult setMetalRhiBufferStateForBarrier(
    rhi::GpuBuffer& buffer, const rhi::ResourceBarrierDesc& desc, core::MutationJournal* journal)
{
    auto* resource = dynamic_cast<MetalRhiBuffer*>(&buffer);
    if (resource && !resource->prepareRecordedState(journal))
        return {false, "RHI-RESOURCE-STATE-RECORDING-CONFLICT"};
    return setMetalState<rhi::GpuBuffer, MetalRhiBuffer>(buffer, desc, false, journal);
}

rhi::ResourceBarrierResult setMetalRhiTextureStateForBarrier(
    rhi::GpuTexture& texture, const rhi::ResourceBarrierDesc& desc, core::MutationJournal* journal)
{
    rhi::ResourceBarrierResult result;
    result.resource = texture.id();
    result.before = desc.before;
    result.after = desc.after;
    result.subresourceAware = true;
    result.stageAccessAware = true;
    result.sourceStages = desc.sourceStages;
    result.destinationStages = desc.destinationStages;
    result.sourceAccess = desc.sourceAccess;
    result.destinationAccess = desc.destinationAccess;
    if (texture.originalContentExclusive()) {
        result.diagnostics = "RHI-TEXTURE-CONTENT-EXCLUSIVE-USE";
        return result;
    }
    auto* metalTexture = dynamic_cast<MetalRhiTexture*>(&texture);
    if (!metalTexture || desc.resource != texture.id()
        || !textureRangeWithin(texture.desc(), desc.textureSubresources)) {
        result.diagnostics = "RHI-METAL-BARRIER-TEXTURE-RANGE-INVALID";
        return result;
    }
    if (const auto* diagnostic = metalTexture->recordSubresourceState(
            desc.textureSubresources, desc.before, desc.after, journal)) {
        result.diagnostics = diagnostic;
        return result;
    }
    result.success = true;
    return result;
}

bool canPrepareMetalRhiRecordedState(
    const rhi::GpuBuffer& buffer, const core::MutationJournal& journal) noexcept
{
    const auto* resource = dynamic_cast<const MetalRhiBuffer*>(&buffer);
    return resource && resource->canPrepareRecordedState(journal);
}

bool prepareMetalRhiRecordedState(rhi::GpuBuffer& buffer, core::MutationJournal& journal)
{
    auto* resource = dynamic_cast<MetalRhiBuffer*>(&buffer);
    return resource && resource->prepareRecordedState(&journal);
}

bool prepareMetalRhiRecordedState(rhi::GpuTexture& texture, core::MutationJournal& journal)
{
    auto* resource = dynamic_cast<MetalRhiTexture*>(&texture);
    return resource && resource->prepareRecordedState(&journal);
}

rhi::ResourceBarrierResult prepareMetalRhiBufferForPoolReuse(rhi::GpuBuffer& buffer)
{
    if (buffer.state() == rhi::ResourceState::Undefined) {
        return {
            true, "", buffer.id(), rhi::ResourceState::Undefined, rhi::ResourceState::Undefined};
    }
    return setMetalRhiBufferStateForBarrier(
        buffer, {buffer.id(), {}, buffer.state(), rhi::ResourceState::Undefined});
}

rhi::ResourceBarrierResult prepareMetalRhiTextureForPoolReuse(rhi::GpuTexture& texture)
{
    const auto* original = dynamic_cast<const MetalRhiTexture*>(&texture);
    const auto range = rhi::defaultTextureSubresourceRange(texture.desc());
    const auto state =
        original ? original->uniformState(range) : std::optional<rhi::ResourceState>{};
    if (!state)
        return {false, "RHI-ORIGINAL-TEXTURE-STATE-NONUNIFORM", texture.id()};
    if (*state == rhi::ResourceState::Undefined) {
        return {true,
            "",
            texture.id(),
            rhi::ResourceState::Undefined,
            rhi::ResourceState::Undefined,
            true};
    }
    return setMetalRhiTextureStateForBarrier(texture,
        {texture.id(),
            rhi::defaultTextureSubresourceRange(texture.desc()),
            *state,
            rhi::ResourceState::Undefined});
}

void MetalRhiResourceRegistry::registerBuffer(const rhi::GpuBuffer& buffer)
{
    std::scoped_lock lock(mutex_);
    auto& resource = resources_[buffer.id().value];
    resource.kind = rhi::DescriptorResourceKind::Buffer;
    resource.bufferUsage = buffer.desc().usage;
    resource.bufferSizeBytes = buffer.desc().sizeBytes;
    resource.gpuWriteSeal = buffer.gpuWriteSealToken();
    resource.buffer = metalRhiNativePayloadFor(buffer);
}

void MetalRhiResourceRegistry::registerTexture(const rhi::GpuTexture& texture)
{
    std::scoped_lock lock(mutex_);
    auto& resource = resources_[texture.id().value];
    resource.kind = rhi::DescriptorResourceKind::TextureView;
    resource.textureUsage = texture.desc().usage;
    resource.texture = metalRhiNativePayloadFor(texture);
    if (const auto* wrapper = dynamic_cast<const MetalRhiTexture*>(&texture))
        resource.textureWrapper = wrapper->lifetimeObserver();
    if (resource.texture.lock()) {
        const auto payload = resource.texture.lock();
        resource.textureFormat = payload->format;
        resource.textureExtent = payload->extent;
        resource.textureMipLevelCount = payload->mipLevelCount;
        resource.textureArrayLayerCount = payload->arrayLayerCount;
        resource.textureSampleCount = payload->sampleCount;
    }
}

void MetalRhiResourceRegistry::registerTextureView(const rhi::GpuTextureView& textureView)
{
    std::scoped_lock lock(mutex_);
    auto& resource = resources_[textureView.id().value];
    resource.kind = rhi::DescriptorResourceKind::TextureView;
    resource.textureUsage = textureView.desc().usage;
    resource.textureView = metalRhiNativePayloadFor(textureView);
    if (const auto* wrapper = dynamic_cast<const MetalRhiTextureView*>(&textureView))
        resource.textureViewWrapper = wrapper->lifetimeObserver();
    resource.parentTexture = textureView.textureId();
    resource.textureViewDimension = textureView.desc().dimension;
    resource.textureViewAspect = textureView.desc().aspect;
    if (resource.textureView.lock()) {
        const auto payload = resource.textureView.lock();
        resource.textureFormat = payload->format;
        resource.textureExtent = payload->extent;
        resource.textureMipLevelCount = payload->mipLevelCount;
        resource.textureArrayLayerCount = payload->arrayLayerCount;
        resource.textureSampleCount = payload->sampleCount;
    }
}

void MetalRhiResourceRegistry::registerSampler(const rhi::GpuSampler& sampler)
{
    std::scoped_lock lock(mutex_);
    auto payload = metalRhiNativePayloadFor(sampler);
    if (payload && payload->resource.nativeRuntimeBacked
        && !payload->resource.nativeRuntimeDiagnosticOnly && payload->resource.nativeObject
        && payload->resource.nativeDevice) {
        // Metal may return the same immutable sampler object from two genuine
        // native creations. Reuse that object's existing owner at cold registry
        // admission, before any descriptor/prepared identity is captured. The
        // bounded registry is the only inventory; no second cache is created.
        for (const auto& entry : resources_) {
            const auto existing = entry.second.sampler.lock();
            const auto original = entry.second.samplerNativeObject.lock();
            if (!existing || existing == payload || !existing->resource.nativeRuntimeBacked
                || existing->resource.nativeRuntimeDiagnosticOnly || !original
                || existing->argumentBufferCompatible != payload->argumentBufferCompatible
                || !core::SharedOwnerProjection::sameOwner(
                    original, existing->resource.nativeObject)
                || original.get() != payload->resource.nativeObject.get()
                || !core::SharedOwnerProjection::sameOwner(
                    existing->resource.nativeDevice, payload->resource.nativeDevice))
                continue;
            payload->resource.nativeObject = original;
            break;
        }
    }
    auto& resource = resources_[sampler.id().value];
    resource.kind = rhi::DescriptorResourceKind::Sampler;
    resource.samplerNativeObject =
        payload ? payload->resource.nativeObject : std::shared_ptr<void>{};
    resource.sampler = std::move(payload);
}

void MetalRhiResourceRegistry::unregisterResource(rhi::GpuResourceId id)
{
    std::scoped_lock lock(mutex_);
    resources_.erase(id.value);
}

std::optional<MetalRhiRegisteredResource> MetalRhiResourceRegistry::find(
    rhi::GpuResourceId id) const
{
    std::scoped_lock lock(mutex_);
    const auto found = resources_.find(id.value);
    if (found == resources_.end()) {
        return std::nullopt;
    }
    MetalRhiRegisteredResource resource;
    resource.kind = found->second.kind;
    resource.bufferUsage = found->second.bufferUsage;
    resource.bufferSizeBytes = found->second.bufferSizeBytes;
    if (const auto seal = found->second.gpuWriteSeal.lock()) {
        resource.gpuWritesSealed = seal->writableDescriptorSealed();
        resource.gpuTransferWritesSealed = seal->transferWritesSealed();
    }
    resource.textureUsage = found->second.textureUsage;
    resource.buffer = found->second.buffer.lock();
    resource.texture = found->second.texture.lock();
    resource.textureView = found->second.textureView.lock();
    if (resource.texture)
        resource.immutableTextureRevision = resource.texture->immutableRevision();
    else if (resource.textureView)
        resource.immutableTextureRevision = resource.textureView->immutableRevision();
    resource.sampler = found->second.sampler.lock();
    resource.textureFormat = found->second.textureFormat;
    resource.textureExtent = found->second.textureExtent;
    resource.textureMipLevelCount = found->second.textureMipLevelCount;
    resource.textureArrayLayerCount = found->second.textureArrayLayerCount;
    resource.textureSampleCount = found->second.textureSampleCount;
    resource.textureViewDimension = found->second.textureViewDimension;
    resource.textureViewAspect = found->second.textureViewAspect;
    resource.parentTexture = found->second.parentTexture;
    resource.textureWrapper = found->second.textureWrapper;
    resource.textureViewWrapper = found->second.textureViewWrapper;
    const auto parent = resources_.find(found->second.parentTexture.value);
    resource.parentTextureLive = parent != resources_.end() && !parent->second.texture.expired()
        && parent->second.textureWrapper.get();
    const bool payloadAlive =
        resource.buffer || resource.texture || resource.textureView || resource.sampler;
    return payloadAlive ? std::optional<MetalRhiRegisteredResource>{std::move(resource)}
                        : std::nullopt;
}

const char* metalRhiOriginalTextureAccessDiagnostic(
    const MetalRhiRegisteredResource& resource) noexcept
{
    return (resource.texture && resource.texture->originalContentExclusive())
            || (resource.textureView && resource.textureView->originalContentExclusive())
        ? "RHI-TEXTURE-CONTENT-EXCLUSIVE-USE"
        : nullptr;
}

const char* metalRhiOriginalTextureAccessDiagnostic(
    const std::shared_ptr<MetalRhiResourceRegistry>& registry, rhi::GpuResourceId id) noexcept
{
    const auto resource = registry ? registry->find(id) : std::nullopt;
    return resource ? metalRhiOriginalTextureAccessDiagnostic(*resource) : nullptr;
}

const char* metalRhiTextureWriteDiagnostic(
    const std::shared_ptr<MetalRhiResourceRegistry>& registry, rhi::GpuResourceId id) noexcept
{
    const auto resource = registry ? registry->find(id) : std::nullopt;
    if (resource)
        if (const auto* diagnostic = metalRhiOriginalTextureAccessDiagnostic(*resource))
            return diagnostic;
    return resource && resource->immutableTextureRevision
        ? "RHI-METAL-IMMUTABLE-TEXTURE-WRITE-FORBIDDEN"
        : nullptr;
}

const char* metalRhiRenderPassWriteDiagnostic(
    const std::shared_ptr<MetalRhiResourceRegistry>& registry,
    const rhi::RenderPassDesc& desc) noexcept
{
    for (const auto& attachment : desc.colorAttachments) {
        if (const auto* id = metalRhiTextureWriteDiagnostic(registry, attachment.target))
            return id;
        if (attachment.hasResolveTarget)
            if (const auto* id = metalRhiTextureWriteDiagnostic(registry, attachment.resolveTarget))
                return id;
    }
    if (desc.hasDepthStencilAttachment)
        return metalRhiTextureWriteDiagnostic(registry, desc.depthStencilAttachment.target);
    return nullptr;
}

arp::core::UniquePtr<rhi::PipelineLayout> createMetalRhiPipelineLayout(
    const rhi::PipelineLayoutDesc& desc,
    std::shared_ptr<rhi::semantics::DirectObjectTelemetryTracker> telemetry,
    rhi::semantics::DirectObjectBudgetLease budgetLease)
{
    if (!rhi::validatePipelineLayout(desc).success) {
        return nullptr;
    }
    auto layout =
        arp::core::makeUnique<MetalRhiPipelineLayout>(arp::rhi::PipelineLayoutStateCopy::copy(desc),
            telemetry ? telemetry->acquire(rhi::semantics::DirectObjectKind::PipelineLayout)
                      : rhi::semantics::DirectObjectTelemetryLease{},
            std::move(budgetLease));
    return layout->pushConstantByteCount() > rhi::kL2AtomicPushConstantLimitBytes
        ? nullptr
        : std::move(layout);
}

} // namespace arp::rhi_metal

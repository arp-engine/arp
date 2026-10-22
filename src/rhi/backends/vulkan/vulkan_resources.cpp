#include "rhi/backends/vulkan/vulkan_backend_objects.h"
#include "rhi/backends/vulkan/vulkan_native_resources.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <utility>

namespace arp::rhi_vulkan {

namespace {

bool registryOwnerMatches(const std::weak_ptr<VulkanRhiResourceRegistry>& source,
    const std::shared_ptr<VulkanRhiResourceRegistry>& expected) noexcept
{
    const auto issuing = source.lock();
    return expected && issuing == expected
        && core::SharedOwnerProjection::sameOwner(issuing, expected);
}

rhi::ResourceMemoryClass memoryClassFor(rhi::MemoryUsage usage)
{
    switch (usage) {
    case rhi::MemoryUsage::GpuOnly:
        return rhi::ResourceMemoryClass::DeviceLocal;
    case rhi::MemoryUsage::CpuToGpu:
        return rhi::ResourceMemoryClass::Upload;
    case rhi::MemoryUsage::GpuToCpu:
        return rhi::ResourceMemoryClass::Readback;
    case rhi::MemoryUsage::Transient:
        return rhi::ResourceMemoryClass::Transient;
    }
    return rhi::ResourceMemoryClass::DeviceLocal;
}

rhi::ResourceAllocation allocationFrom(
    const VulkanRhiAllocation& allocation, rhi::MemoryUsage memory)
{
    rhi::ResourceAllocation result;
    result.memoryClass = memoryClassFor(memory);
    result.estimatedSizeBytes = allocation.sizeBytes;
    result.nativeHandleExposed = allocation.nativeAllocationExposedToUpperLayer;
    result.backendFallbackHiddenFromUpperLayer = !allocation.vmaAllocationVisibleToUpperLayer;
    return result;
}

rhi::ResourceBarrierResult barrierResultFrom(
    rhi::GpuResourceId id, const rhi::ResourceBarrierDesc& desc)
{
    rhi::ResourceBarrierResult result;
    result.resource = id;
    result.before = desc.before;
    result.after = desc.after;
    result.sourceStages = desc.sourceStages;
    result.destinationStages = desc.destinationStages;
    result.sourceAccess = desc.sourceAccess;
    result.destinationAccess = desc.destinationAccess;
    result.stageAccessAware = desc.sourceStages != rhi::PipelineStage::None
        || desc.destinationStages != rhi::PipelineStage::None;
    return result;
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

class VulkanRhiBuffer final : public rhi::GpuBuffer {
public:
    VulkanRhiBuffer(rhi::GpuResourceId id,
        rhi::BufferDesc desc,
        rhi::ResourceAllocation allocation,
        std::shared_ptr<void> allocationReleaseToken,
        const std::shared_ptr<rhi::OriginalAllocationLifetime>& originalLifetime,
        std::shared_ptr<VulkanRhiNativeResourcePayload> nativePayload,
        std::weak_ptr<VulkanRhiResourceRegistry> registry)
        : id_(id), desc_(std::move(desc)), allocation_(allocation),
          allocationReleaseToken_(std::move(allocationReleaseToken)),
          nativePayload_(std::move(nativePayload)), registry_(std::move(registry))
          // Buffers have no Vulkan layout, but the logical state is still used
          // by the RHI barrier tracker. Honor the caller's declared state so a
          // first-use ShaderRead -> ShaderWrite barrier is valid.
          ,
          state_(desc_.initialState)
    {
        bindOriginalRecordedState(state_);
        if (nativePayload_ && allocationReleaseToken_)
            nativePayload_->allocationReleaseToken = allocationReleaseToken_;
        if (nativePayload_ && originalLifetime
            && core::SharedOwnerProjection::sameOwner(allocationReleaseToken_, originalLifetime)
            && nativePayload_->backendVulkanDeviceHandle()
            && nativePayload_->backendVulkanBufferHandle())
            bindOriginalAllocationLifetime(*originalLifetime);
    }

    ~VulkanRhiBuffer() override
    {
        if (auto registry = registry_.lock())
            registry->unregisterResource(id_);
    }

    rhi::GpuResourceId id() const override { return id_; }

    const rhi::BufferDesc& desc() const override { return desc_; }

    const rhi::ResourceAllocation& allocation() const override { return allocation_; }

    rhi::ResourceState state() const override { return state_.state(); }

    const char* recordState(rhi::ResourceState state, core::MutationJournal* journal) noexcept
    {
        return state_.writeBuffer(state, journal);
    }

    bool prepareRecordedState(core::MutationJournal* journal)
    {
        return journal ? state_.prepare(*journal) : state_.active() && !state_.pending();
    }

    bool stateCommitted() const noexcept { return state_.active() && !state_.pending(); }

    bool stateAvailable(const core::MutationJournal* journal) const noexcept
    {
        return state_.active()
            && (!state_.pending() || (journal && state_.observePending(*journal).has_value()));
    }

    bool canTransition(const core::MutationJournal& journal) const noexcept
    {
        return state_.canWriteBatch(journal, 1u);
    }

    std::shared_ptr<VulkanRhiNativeResourcePayload> nativePayload() const { return nativePayload_; }

    bool registryMatches(const std::shared_ptr<VulkanRhiResourceRegistry>& registry) const noexcept
    {
        return registryOwnerMatches(registry_, registry);
    }

private:
    rhi::GpuResourceId id_;
    rhi::BufferDesc desc_;
    rhi::ResourceAllocation allocation_;
    std::shared_ptr<void> allocationReleaseToken_;
    std::shared_ptr<VulkanRhiNativeResourcePayload> nativePayload_;
    std::weak_ptr<VulkanRhiResourceRegistry> registry_;
    rhi::common::RecordedResourceStateControl state_;
};

class VulkanRhiTexture final : public rhi::GpuTexture {
public:
    VulkanRhiTexture(rhi::GpuResourceId id,
        rhi::TextureDesc desc,
        rhi::ResourceAllocation allocation,
        std::shared_ptr<void> allocationReleaseToken,
        const std::shared_ptr<rhi::OriginalAllocationLifetime>& originalLifetime,
        std::shared_ptr<VulkanRhiNativeResourcePayload> nativePayload,
        std::weak_ptr<VulkanRhiResourceRegistry> registry)
        : id_(id), desc_(std::move(desc)), allocation_(allocation),
          allocationReleaseToken_(std::move(allocationReleaseToken)),
          nativePayload_(std::move(nativePayload)), registry_(std::move(registry))
          // Newly allocated VkImages begin Undefined. Acquired swapchain images
          // are external resources, however, and expose their native-acquire
          // state through the backend-private payload.
          ,
          state_(desc_,
              nativePayload_ && nativePayload_->backendVulkanSwapchainFrameSyncAvailable()
                  ? nativePayload_->backendVulkanSwapchainAcquiredState()
                  : rhi::ResourceState::Undefined)
    {
        bindOriginalRecordedState(state_);
        if (nativePayload_ && allocationReleaseToken_)
            nativePayload_->allocationReleaseToken = allocationReleaseToken_;
        if (nativePayload_ && originalLifetime
            && core::SharedOwnerProjection::sameOwner(allocationReleaseToken_, originalLifetime)
            && nativePayload_->backendVulkanDeviceHandle()
            && nativePayload_->backendVulkanImageHandle())
            bindOriginalAllocationLifetime(*originalLifetime);
    }

    ~VulkanRhiTexture() override
    {
        if (auto registry = registry_.lock())
            registry->unregisterResource(id_);
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

    bool prepareRecordedState(core::MutationJournal* journal)
    {
        return journal ? state_.prepare(*journal) : state_.active() && !state_.pending();
    }

    bool stateCommitted() const noexcept { return state_.active() && !state_.pending(); }

    const char* recordSubresourceState(const rhi::TextureSubresourceRange& range,
        rhi::ResourceState before,
        rhi::ResourceState after,
        core::MutationJournal* journal) noexcept
    {
        return state_.write(range, before, after, journal);
    }

    bool stateAvailable(const rhi::TextureSubresourceRange& range,
        const core::MutationJournal* journal) const noexcept
    {
        return state_.active() && state_.uniformState(range).has_value()
            && (!state_.pending()
                || (journal && state_.observePending(range, *journal).has_value()));
    }

    bool canTransition(const core::MutationJournal& journal) const noexcept
    {
        return state_.canWriteBatch(journal, 1u);
    }

    std::shared_ptr<VulkanRhiNativeResourcePayload> nativePayload() const { return nativePayload_; }

    bool registryMatches(const std::shared_ptr<VulkanRhiResourceRegistry>& registry) const noexcept
    {
        return registryOwnerMatches(registry_, registry);
    }

private:
    rhi::GpuResourceId id_;
    rhi::TextureDesc desc_;
    rhi::ResourceAllocation allocation_;
    std::shared_ptr<void> allocationReleaseToken_;
    std::shared_ptr<VulkanRhiNativeResourcePayload> nativePayload_;
    std::weak_ptr<VulkanRhiResourceRegistry> registry_;
    rhi::common::RecordedTextureStateControl state_;
};

class VulkanRhiBufferView final : public rhi::GpuBufferView {
public:
    VulkanRhiBufferView(
        rhi::GpuResourceId id, rhi::GpuResourceId bufferId, rhi::BufferViewDesc desc)
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

class VulkanRhiTextureView final : public rhi::GpuTextureView {
public:
    VulkanRhiTextureView(rhi::GpuResourceId id,
        rhi::GpuResourceId textureId,
        rhi::TextureViewDesc desc,
        std::shared_ptr<VulkanRhiNativeResourcePayload> nativePayload,
        std::weak_ptr<VulkanRhiResourceRegistry> registry)
        : id_(id), textureId_(textureId), desc_(std::move(desc)),
          nativePayload_(std::move(nativePayload)), registry_(std::move(registry))
    {}

    ~VulkanRhiTextureView() override
    {
        if (auto registry = registry_.lock())
            registry->unregisterResource(id_);
    }

    rhi::GpuResourceId id() const override { return id_; }

    rhi::GpuResourceId textureId() const override { return textureId_; }

    const rhi::TextureViewDesc& desc() const override { return desc_; }

    std::shared_ptr<VulkanRhiNativeResourcePayload> nativePayload() const { return nativePayload_; }

    bool registryMatches(const std::shared_ptr<VulkanRhiResourceRegistry>& registry) const noexcept
    {
        return registryOwnerMatches(registry_, registry);
    }

private:
    rhi::GpuResourceId id_;
    rhi::GpuResourceId textureId_;
    rhi::TextureViewDesc desc_;
    std::shared_ptr<VulkanRhiNativeResourcePayload> nativePayload_;
    std::weak_ptr<VulkanRhiResourceRegistry> registry_;
};

class VulkanRhiSampler final : public rhi::GpuSampler {
public:
    VulkanRhiSampler(rhi::GpuResourceId id,
        rhi::SamplerDesc desc,
        std::shared_ptr<VulkanRhiNativeResourcePayload> nativePayload,
        std::weak_ptr<VulkanRhiResourceRegistry> registry)
        : id_(id), desc_(std::move(desc)), nativePayload_(std::move(nativePayload)),
          registry_(std::move(registry))
    {}

    ~VulkanRhiSampler() override
    {
        if (auto registry = registry_.lock())
            registry->unregisterResource(id_);
    }

    rhi::GpuResourceId id() const override { return id_; }

    const rhi::SamplerDesc& desc() const override { return desc_; }

    std::shared_ptr<VulkanRhiNativeResourcePayload> nativePayload() const { return nativePayload_; }

private:
    rhi::GpuResourceId id_;
    rhi::SamplerDesc desc_;
    std::shared_ptr<VulkanRhiNativeResourcePayload> nativePayload_;
    std::weak_ptr<VulkanRhiResourceRegistry> registry_;
};

} // namespace

std::uint64_t estimateVulkanRhiTextureBytes(const rhi::TextureDesc& desc)
{
    if (desc.extent.width == 0 || desc.extent.height == 0 || desc.extent.depth == 0) {
        return 0;
    }
    // Block-compressed formats (BC1/BC3/BC7) size by 4x4 block, not per-texel.
    // Route through the shared block-aware helper; for uncompressed formats
    // it reduces to width*height*depth*bytesPerPixel as before.
    const auto bytes = rhi::bytesForTextureMip(
        desc.format, desc.extent.width, desc.extent.height, desc.extent.depth);
    if (bytes == 0) {
        return 0;
    }
    return bytes;
}

arp::core::UniquePtr<rhi::GpuBuffer> createVulkanRhiBuffer(rhi::GpuResourceId id,
    const rhi::BufferDesc& desc,
    const VulkanRhiAllocation& allocation,
    std::shared_ptr<VulkanRhiNativeResourcePayload> nativePayload,
    std::shared_ptr<VulkanRhiResourceRegistry> registry)
{
    return arp::core::makeUnique<VulkanRhiBuffer>(id,
        desc,
        allocationFrom(allocation, desc.memory),
        allocation.releaseToken,
        allocation.originalLifetime.lock(),
        std::move(nativePayload),
        std::move(registry));
}

arp::core::UniquePtr<rhi::GpuTexture> createVulkanRhiTexture(rhi::GpuResourceId id,
    const rhi::TextureDesc& desc,
    const VulkanRhiAllocation& allocation,
    std::shared_ptr<VulkanRhiNativeResourcePayload> nativePayload,
    std::shared_ptr<VulkanRhiResourceRegistry> registry)
{
    auto texture = arp::core::makeUnique<VulkanRhiTexture>(id,
        desc,
        allocationFrom(allocation, desc.memory),
        allocation.releaseToken,
        allocation.originalLifetime.lock(),
        std::move(nativePayload),
        registry);
    if (registry)
        registry->registerTextureObject(id, *texture);
    return texture;
}

arp::core::UniquePtr<rhi::GpuBufferView> createVulkanRhiBufferView(
    rhi::GpuResourceId id, rhi::GpuResourceId bufferId, const rhi::BufferViewDesc& desc)
{
    return arp::core::makeUnique<VulkanRhiBufferView>(id, bufferId, desc);
}

arp::core::UniquePtr<rhi::GpuTextureView> createVulkanRhiTextureView(rhi::GpuResourceId id,
    rhi::GpuResourceId textureId,
    const rhi::TextureViewDesc& desc,
    std::shared_ptr<VulkanRhiNativeResourcePayload> nativePayload,
    std::shared_ptr<VulkanRhiResourceRegistry> registry)
{
    return arp::core::makeUnique<VulkanRhiTextureView>(
        id, textureId, desc, std::move(nativePayload), std::move(registry));
}

arp::core::UniquePtr<rhi::GpuSampler> createVulkanRhiSampler(rhi::GpuResourceId id,
    const rhi::SamplerDesc& desc,
    std::shared_ptr<VulkanRhiNativeResourcePayload> nativePayload,
    std::shared_ptr<VulkanRhiResourceRegistry> registry)
{
    return arp::core::makeUnique<VulkanRhiSampler>(
        id, desc, std::move(nativePayload), std::move(registry));
}

rhi::ResourceBarrierResult transitionVulkanRhiBuffer(
    rhi::GpuBuffer& buffer, const rhi::ResourceBarrierDesc& desc, core::MutationJournal* journal)
{
    auto result = barrierResultFrom(buffer.id(), desc);
    auto* vulkanBuffer = dynamic_cast<VulkanRhiBuffer*>(&buffer);
    if (!vulkanBuffer) {
        result.diagnostics = "Vulkan RHI transition requires a backend-owned buffer";
        return result;
    }
    if (desc.resource != buffer.id() || buffer.state() != desc.before) {
        result.diagnostics = "Vulkan RHI buffer transition state mismatch";
        return result;
    }
    if (!vulkanBuffer->prepareRecordedState(journal)) {
        result.diagnostics = "RHI-RESOURCE-STATE-RECORDING-CONFLICT";
        return result;
    }
    if (const auto* diagnostic = vulkanBuffer->recordState(desc.after, journal)) {
        result.diagnostics = diagnostic;
        return result;
    }
    result.success = true;
    return result;
}

rhi::ResourceBarrierResult transitionVulkanRhiTexture(
    rhi::GpuTexture& texture, const rhi::ResourceBarrierDesc& desc, core::MutationJournal* journal)
{
    auto result = barrierResultFrom(texture.id(), desc);
    result.subresourceAware = true;
    if (texture.originalContentExclusive()) {
        result.diagnostics = "RHI-TEXTURE-CONTENT-EXCLUSIVE-USE";
        return result;
    }
    auto* vulkanTexture = dynamic_cast<VulkanRhiTexture*>(&texture);
    if (!vulkanTexture) {
        result.diagnostics = "Vulkan RHI transition requires a backend-owned texture";
        return result;
    }
    if (desc.resource != texture.id()
        || !textureRangeWithin(texture.desc(), desc.textureSubresources)) {
        result.diagnostics = "RHI-VULKAN-BARRIER-TEXTURE-RANGE-INVALID";
        return result;
    }
    if (const auto* diagnostic = vulkanTexture->recordSubresourceState(
            desc.textureSubresources, desc.before, desc.after, journal)) {
        result.diagnostics = diagnostic;
        return result;
    }
    result.success = true;
    return result;
}

std::shared_ptr<VulkanRhiNativeResourcePayload> vulkanRhiNativePayload(const rhi::GpuBuffer& buffer)
{
    const auto* vulkanBuffer = dynamic_cast<const VulkanRhiBuffer*>(&buffer);
    return vulkanBuffer ? vulkanBuffer->nativePayload() : nullptr;
}

bool vulkanRhiBufferRegistryMatches(const rhi::GpuBuffer& buffer,
    const std::shared_ptr<VulkanRhiResourceRegistry>& registry) noexcept
{
    const auto* vulkanBuffer = dynamic_cast<const VulkanRhiBuffer*>(&buffer);
    return vulkanBuffer && vulkanBuffer->registryMatches(registry);
}

bool vulkanRhiBufferStateCommitted(const rhi::GpuBuffer& buffer) noexcept
{
    const auto* original = dynamic_cast<const VulkanRhiBuffer*>(&buffer);
    return original && original->stateCommitted();
}

bool vulkanRhiBufferStateAvailable(
    const rhi::GpuBuffer& buffer, const core::MutationJournal* journal) noexcept
{
    const auto* original = dynamic_cast<const VulkanRhiBuffer*>(&buffer);
    return original && original->stateAvailable(journal);
}

bool vulkanRhiBufferCanTransition(
    const rhi::GpuBuffer& buffer, const core::MutationJournal& journal) noexcept
{
    const auto* original = dynamic_cast<const VulkanRhiBuffer*>(&buffer);
    return original && original->canTransition(journal);
}

bool vulkanRhiTextureStateCommitted(const rhi::GpuTexture& texture) noexcept
{
    const auto* original = dynamic_cast<const VulkanRhiTexture*>(&texture);
    return original && original->stateCommitted();
}

bool vulkanRhiTextureStateAvailable(const rhi::GpuTexture& texture,
    const rhi::TextureSubresourceRange& range,
    const core::MutationJournal* journal) noexcept
{
    const auto* original = dynamic_cast<const VulkanRhiTexture*>(&texture);
    return original && original->stateAvailable(range, journal);
}

bool vulkanRhiTextureCanTransition(
    const rhi::GpuTexture& texture, const core::MutationJournal& journal) noexcept
{
    const auto* original = dynamic_cast<const VulkanRhiTexture*>(&texture);
    return original && original->canTransition(journal);
}

bool vulkanRhiTextureRegistryMatches(const rhi::GpuTexture& texture,
    const std::shared_ptr<VulkanRhiResourceRegistry>& registry) noexcept
{
    const auto* native = dynamic_cast<const VulkanRhiTexture*>(&texture);
    return native && native->registryMatches(registry);
}

bool vulkanRhiTextureViewRegistryMatches(const rhi::GpuTextureView& view,
    const std::shared_ptr<VulkanRhiResourceRegistry>& registry) noexcept
{
    const auto* native = dynamic_cast<const VulkanRhiTextureView*>(&view);
    return native && native->registryMatches(registry);
}

std::shared_ptr<VulkanRhiNativeResourcePayload> vulkanRhiNativePayload(
    const rhi::GpuTexture& texture)
{
    const auto* vulkanTexture = dynamic_cast<const VulkanRhiTexture*>(&texture);
    return vulkanTexture ? vulkanTexture->nativePayload() : nullptr;
}

std::shared_ptr<VulkanRhiNativeResourcePayload> vulkanRhiNativePayload(
    const rhi::GpuTextureView& textureView)
{
    const auto* vulkanTextureView = dynamic_cast<const VulkanRhiTextureView*>(&textureView);
    return vulkanTextureView ? vulkanTextureView->nativePayload() : nullptr;
}

std::shared_ptr<VulkanRhiNativeResourcePayload> vulkanRhiNativePayload(
    const rhi::GpuSampler& sampler)
{
    const auto* vulkanSampler = dynamic_cast<const VulkanRhiSampler*>(&sampler);
    return vulkanSampler ? vulkanSampler->nativePayload() : nullptr;
}

} // namespace arp::rhi_vulkan

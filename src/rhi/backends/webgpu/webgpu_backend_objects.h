#pragma once
#include "core/cpu_allocator.h"
#include "core/mutation_journal.h"
#include "rhi/backends/webgpu/webgpu_allocator.h"
#include "rhi/backends/webgpu/webgpu_native_runtime.h"
#include "rhi/gpu_device.h"
#include "rhi/semantics/native_backend_capabilities.h"

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>

namespace arp::rhi_webgpu {

class WebGpuRhiStagingUploadPool;
class WebGpuTransferResourceObservation;
class WebGpuOrdinaryResourceObservation;
class WebGpuRhiResourceRegistry;

// Nonblocking wrapper-owned registry correspondence; no descriptor or command
// authority is issued by these resource observations.
const char* observeWebGpuDescriptorResource(
    const rhi::DescriptorSet&, rhi::GpuResourceId, WebGpuOrdinaryResourceObservation&) noexcept;

class WebGpuTextureReadbackRecorder {
public:
    virtual ~WebGpuTextureReadbackRecorder() = default;
    virtual bool scheduleTextureReadback(
        const rhi::GpuTexture& texture, std::string_view label, std::pmr::string& diagnostics) = 0;
};

struct WebGpuRhiNativePayloadPolicy {
    bool enabled = false;
    bool required = false;
};

const char* validateWebGpuOriginalDescriptorLossSource(
    const rhi::DescriptorArena&, const std::shared_ptr<WebGpuRhiResourceRegistry>&) noexcept;
const char* validateWebGpuOriginalDescriptorLossSource(
    const rhi::DescriptorSet&, const std::shared_ptr<WebGpuRhiResourceRegistry>&) noexcept;

struct WebGpuRhiDescriptorResourceFacts {
    rhi::DescriptorResourceKind kind = rhi::DescriptorResourceKind::Buffer;
    rhi::ResourceUsage bufferUsage = rhi::ResourceUsage::None;
    rhi::TextureUsage textureUsage = rhi::TextureUsage::None;
    rhi::TextureViewDimension textureViewDimension = rhi::TextureViewDimension::Texture2D;
    rhi::TextureFormat textureFormat = rhi::TextureFormat::Unknown;
    rhi::TextureAspect textureViewAspect = rhi::TextureAspect::Color;
    bool parentTextureLive = false;
    bool gpuWritesSealed = false;
    bool textureContentExclusive = false;
};

struct WebGpuRhiRegisteredResource {
    rhi::DescriptorResourceKind kind = rhi::DescriptorResourceKind::Buffer;
    rhi::ResourceUsage bufferUsage = rhi::ResourceUsage::None;
    rhi::TextureUsage textureUsage = rhi::TextureUsage::None;
    std::shared_ptr<WebGpuNativeBufferPayload> buffer;
    std::shared_ptr<WebGpuNativeTexturePayload> texture;
    std::shared_ptr<WebGpuNativeTextureViewPayload> textureView;
    std::shared_ptr<WebGpuNativeSamplerPayload> sampler;
    rhi::TextureViewDimension textureViewDimension = rhi::TextureViewDimension::Texture2D;
    // Keep the public descriptions beside the private payloads so transfer
    // commands can validate byte ranges without exposing native handles.
    rhi::BufferDesc bufferDesc;
    rhi::TextureDesc textureDesc;
    rhi::SamplerDesc samplerDesc;
    rhi::TextureAspect textureViewAspect = rhi::TextureAspect::Color;
    rhi::GpuResourceId parentTexture;
    bool textureObject = false;
    bool parentTextureLive = false;
    std::weak_ptr<const rhi::GpuBufferWriteSealState> gpuWriteSeal;
    bool gpuWritesSealed = false;
    bool gpuTransferWritesSealed = false;
    std::uint64_t allocationBytes = 0u;
    std::shared_ptr<const void> allocationOwner;
    std::uintptr_t registeredNativeObject = 0u;
    std::uintptr_t registeredNativeDevice = 0u;
    rhi::TextureViewDesc textureViewDesc;
    std::shared_ptr<WebGpuNativeTexturePayload> parentTextureOwner;
    WebGpuRhiDescriptorResourceFacts descriptorFacts() const noexcept;
};

class WebGpuRhiResourceRegistry : public core::SharedObjectIdentity {
public:
    /// Construct with an explicit CPU memory resource. When omitted, the
    /// process-wide default resource is used. Threaded through to the pmr
    /// resources map so a backend can bind a per-instance intent/budget
    /// instead of silently falling back to the global default.
    explicit WebGpuRhiResourceRegistry(
        std::pmr::memory_resource* memoryResource = &core::defaultCpuMemoryResource())
        : resources_(memoryResource)
    {}

    void registerBuffer(const rhi::GpuBuffer& buffer);
    void registerTexture(const rhi::GpuTexture& texture);
    void registerTextureView(const rhi::GpuTextureView& textureView);
    void registerSampler(const rhi::GpuSampler& sampler);
    void unregisterResource(rhi::GpuResourceId id);
    void unregisterBufferIfOwner(
        rhi::GpuResourceId, const std::shared_ptr<WebGpuNativeBufferPayload>&) noexcept;
    void unregisterTextureIfOwner(
        rhi::GpuResourceId, const std::shared_ptr<WebGpuNativeTexturePayload>&) noexcept;
    std::optional<WebGpuRhiRegisteredResource> find(rhi::GpuResourceId id) const;
    // Live scalar facts only: recording validation must not copy descriptor labels.
    std::optional<WebGpuRhiDescriptorResourceFacts> findDescriptorFacts(
        rhi::GpuResourceId id) const;
    // Cold destination slots; no label copy, allocation or blocking wait.
    const char* observeTransferResources(const rhi::GpuResourceId* ids,
        std::size_t count,
        WebGpuTransferResourceObservation* observations) const noexcept;
    const char* validateTransferResources(
        const WebGpuTransferResourceObservation*, std::size_t count) const noexcept;
    const char* observeOrdinaryResources(const rhi::GpuResourceId* ids,
        std::size_t count,
        WebGpuOrdinaryResourceObservation* observations) const noexcept;
    const char* validateOrdinaryResources(
        const WebGpuOrdinaryResourceObservation*, std::size_t count) const noexcept;

private:
    friend class WebGpuOriginalTextureUploadDestination;
    friend class WebGpuOriginalBufferUploadSource;
    bool tryRegisterFreshBuffer(const rhi::GpuBuffer&);
    static WebGpuRhiRegisteredResource bufferRegistration(const rhi::GpuBuffer&);
    bool tryRegisterFreshTexture(const rhi::GpuTexture&);
    static WebGpuRhiRegisteredResource textureRegistration(const rhi::GpuTexture&);
    mutable std::mutex mutex_;
    // Node ownership ends at unregister; no bucket capacity retains a resource quota.
    core::pmr::Map<std::uint64_t, WebGpuRhiRegisteredResource> resources_;
};

// Exact wrapper-owned registry observations. Cold capture and hot validation
// reuse nonblocking registry adapters; no registry or native handle leaks to L2.
const char* observeOriginalWebGpuResource(
    const rhi::GpuBuffer&, WebGpuTransferResourceObservation&) noexcept;
const char* observeOriginalWebGpuResource(
    const rhi::GpuTexture&, WebGpuTransferResourceObservation&) noexcept;
const char* observeOriginalWebGpuResource(
    const rhi::GpuTextureView&, WebGpuOrdinaryResourceObservation&) noexcept;
const char* observeOriginalWebGpuResource(
    const rhi::GpuSampler&, WebGpuOrdinaryResourceObservation&) noexcept;
const char* validateOriginalWebGpuResource(
    const rhi::GpuBuffer&, const WebGpuTransferResourceObservation&) noexcept;
const char* validateOriginalWebGpuResource(
    const rhi::GpuTexture&, const WebGpuTransferResourceObservation&) noexcept;
const char* validateOriginalWebGpuResource(
    const rhi::GpuTextureView&, const WebGpuOrdinaryResourceObservation&) noexcept;
const char* validateOriginalWebGpuResource(
    const rhi::GpuSampler&, const WebGpuOrdinaryResourceObservation&) noexcept;

std::uint64_t estimateWebGpuRhiTextureBytes(const rhi::TextureDesc& desc);

arp::core::UniquePtr<rhi::GpuBuffer> createWebGpuRhiBuffer(rhi::GpuResourceId id,
    const rhi::BufferDesc& desc,
    const WebGpuRhiAllocation& allocation,
    WebGpuRhiNativePayloadPolicy nativePayloadPolicy,
    std::shared_ptr<WebGpuRhiResourceRegistry> registry,
    const WebGpuNativeBufferInitializer& initializer = {});

arp::core::UniquePtr<rhi::GpuTexture> createWebGpuRhiTexture(rhi::GpuResourceId id,
    const rhi::TextureDesc& desc,
    const WebGpuRhiAllocation& allocation,
    WebGpuRhiNativePayloadPolicy nativePayloadPolicy,
    std::shared_ptr<WebGpuRhiResourceRegistry> registry);

/// Wrap an existing native texture payload (e.g. a canvas texture from the
/// swapchain) in a GpuTexture without allocating a new WGPUTexture.
arp::core::UniquePtr<rhi::GpuTexture> createWebGpuRhiTexture(rhi::GpuResourceId id,
    const rhi::TextureDesc& desc,
    const WebGpuRhiAllocation& allocation,
    std::shared_ptr<WebGpuNativeTexturePayload> nativePayload,
    std::shared_ptr<WebGpuRhiResourceRegistry> registry);

arp::core::UniquePtr<rhi::GpuBufferView> createWebGpuRhiBufferView(
    rhi::GpuResourceId id, rhi::GpuResourceId bufferId, const rhi::BufferViewDesc& desc);

arp::core::UniquePtr<rhi::GpuTextureView> createWebGpuRhiTextureView(rhi::GpuResourceId id,
    rhi::GpuResourceId textureId,
    const rhi::GpuTexture& texture,
    const rhi::TextureViewDesc& desc,
    WebGpuRhiNativePayloadPolicy nativePayloadPolicy,
    std::shared_ptr<WebGpuRhiResourceRegistry> registry);

arp::core::UniquePtr<rhi::GpuSampler> createWebGpuRhiSampler(rhi::GpuResourceId id,
    const rhi::SamplerDesc& desc,
    WebGpuRhiNativePayloadPolicy nativePayloadPolicy,
    std::shared_ptr<WebGpuRhiResourceRegistry> registry);

std::shared_ptr<WebGpuNativeBufferPayload> webGpuNativePayloadFor(const rhi::GpuBuffer& buffer);

std::shared_ptr<WebGpuNativeTexturePayload> webGpuNativePayloadFor(const rhi::GpuTexture& texture);

std::shared_ptr<WebGpuNativeTextureViewPayload> webGpuNativePayloadFor(
    const rhi::GpuTextureView& textureView);

std::shared_ptr<WebGpuNativeSamplerPayload> webGpuNativePayloadFor(const rhi::GpuSampler& sampler);

std::shared_ptr<WebGpuNativeDescriptorGroupPayload> prepareWebGpuOriginalImageCellDescriptors(
    const rhi::BindlessSnapshot&);
std::shared_ptr<WebGpuNativeDescriptorGroupPayload> webGpuNativePayloadFor(
    const rhi::DescriptorSet& descriptorSet);

std::shared_ptr<WebGpuNativeDescriptorGroupPayload> webGpuNativePayloadFor(
    const rhi::DescriptorTable& descriptorTable);

std::shared_ptr<WebGpuNativeRenderPipelinePayload> webGpuNativePayloadFor(
    const rhi::RenderPipeline& pipeline);

std::shared_ptr<WebGpuNativeComputePipelinePayload> webGpuNativePayloadFor(
    const rhi::ComputePipeline& pipeline);

std::shared_ptr<WebGpuNativeCommandPayload> webGpuNativePayloadFor(
    const rhi::CommandBuffer& commandBuffer);

std::shared_ptr<WebGpuNativeCommandPayload> webGpuNativePayloadFor(const rhi::CommandEncoder&);

void markWebGpuRhiCommandBufferSubmitted(const rhi::CommandBuffer& commandBuffer);

rhi::ResourceBarrierResult transitionWebGpuRhiBuffer(rhi::GpuBuffer& buffer,
    const rhi::ResourceBarrierDesc& desc,
    core::MutationJournal* journal = nullptr);

// CPU metadata only; the uniform state slot rejects every non-full range.
// This helper cannot establish native barrier or ordered recording authority.
rhi::ResourceBarrierResult transitionWebGpuRhiTexture(rhi::GpuTexture& texture,
    const rhi::ResourceBarrierDesc& desc,
    core::MutationJournal* journal = nullptr);

arp::core::UniquePtr<rhi::PipelineLayout> createWebGpuRhiPipelineLayout(
    const rhi::PipelineLayoutDesc& desc);

rhi::PipelineValidationReport validateWebGpuRhiRenderPipeline(const rhi::RenderPipelineDesc& desc);

rhi::PipelineValidationReport validateOriginalWebGpuRhiRenderPipeline(
    const rhi::RenderPipelineDesc&,
    const std::shared_ptr<const rhi::OriginalGraphicsPipelineInput>&);

rhi::PipelineValidationReport validateWebGpuRhiComputePipeline(
    const rhi::ComputePipelineDesc& desc);
rhi::PipelineValidationReport validateOriginalWebGpuRhiComputePipeline(
    const rhi::ComputePipelineDesc&,
    const std::shared_ptr<const rhi::OriginalSingleEntryPipelineInput>&);

arp::core::UniquePtr<rhi::RenderPipeline> createWebGpuRhiRenderPipeline(
    const rhi::RenderPipelineDesc& desc, WebGpuRhiNativePayloadPolicy nativePayloadPolicy);

arp::core::UniquePtr<rhi::RenderPipeline> createWebGpuRhiAuthorizedBindlessRenderPipeline(
    std::shared_ptr<const rhi::BindlessAuthorization> authorization,
    const rhi::RenderPipelineDesc& state,
    std::string_view vertexEntry,
    std::string_view fragmentEntry,
    core::pmr::String& diagnostic,
    const rhi::BindlessGraphicsPushRequest* push = nullptr);

arp::core::UniquePtr<rhi::ComputePipeline> createWebGpuRhiComputePipeline(
    const rhi::ComputePipelineDesc& desc, WebGpuRhiNativePayloadPolicy nativePayloadPolicy);

arp::core::UniquePtr<rhi::ComputePipeline> createWebGpuRhiAuthorizedBindlessComputePipeline(
    std::shared_ptr<const rhi::BindlessAuthorization> authorization,
    std::string_view entry,
    core::pmr::String& diagnostic);

arp::core::UniquePtr<rhi::DescriptorArena> createWebGpuRhiDescriptorArena(
    const rhi::DescriptorArenaDesc& desc,
    std::shared_ptr<WebGpuRhiResourceRegistry> registry,
    WebGpuRhiNativePayloadPolicy nativePayloadPolicy);

arp::core::UniquePtr<rhi::CommandEncoder> createWebGpuRhiCommandEncoder(
    const rhi::CommandEncoderDesc& desc,
    std::shared_ptr<WebGpuRhiResourceRegistry> registry,
    WebGpuRhiNativePayloadPolicy nativePayloadPolicy,
    std::shared_ptr<WebGpuNativeCommandStats> commandStats,
    const rhi::semantics::NativeBackendCapabilityContract& capabilityContract,
    std::uint32_t pushConstantUploadRingByteCapacity,
    std::uint32_t pushConstantMaxBindingSize,
    std::uint32_t pushConstantOffsetAlignment,
    bool bindlessExecutable,
    std::shared_ptr<WebGpuRhiStagingUploadPool> stagingUploadPool = nullptr,
    std::shared_ptr<WebGpuProducerCompletionOwner> processorCompletion = {});

arp::core::UniquePtr<rhi::SecondaryCommandEncoder> createWebGpuRhiSecondaryCommandEncoder(
    const rhi::SecondaryCommandEncoderDesc& desc);

arp::core::UniquePtr<rhi::RenderBundleEncoder> createWebGpuRhiRenderBundleEncoder(
    const rhi::RenderBundleDesc& desc,
    WebGpuRhiNativePayloadPolicy nativePayloadPolicy,
    const rhi::semantics::NativeBackendCapabilityContract& capabilityContract);

arp::core::UniquePtr<rhi::Swapchain> createWebGpuRhiSwapchain(const rhi::SwapchainDesc& desc,
    WebGpuRhiNativePayloadPolicy nativePayloadPolicy,
    std::shared_ptr<WebGpuNativeSwapchainStats> swapchainStats,
    std::shared_ptr<WebGpuRhiResourceRegistry> registry);

} // namespace arp::rhi_webgpu

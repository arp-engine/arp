#pragma once
#include "core/memory/cpu_allocator.h"
#include "core/ownership/lifetime_epoch.h"
#include "core/ownership/mutation_journal.h"
#include "rhi/backends/metal/metal_allocator.h"
#include "rhi/backends/metal/metal_backend_queries.h"
#include "rhi/backends/metal/metal_native_commands.h"
#include "rhi/backends/metal/metal_native_descriptors.h"
#include "rhi/backends/metal/metal_native_pipelines.h"
#include "rhi/backends/metal/metal_native_resources.h"
#include "rhi/backends/metal/metal_native_swapchain.h"
#include "rhi/frontend/bindless_graphics_pass_preparation.h"
#include "rhi/frontend/memory_direct_object_budget.h"
#include "rhi/gpu_device.h"
#include "rhi/semantics/bindless_completed_image_lowering.h"
#include "rhi/semantics/native_backend_capabilities.h"

#include <map>
#include <memory>
#include <mutex>
#include <optional>

namespace arp::rhi {
class BindlessCrossExtension;
class OriginalCommandBindlessRootSource;
template <class Descriptor> class OriginalCommandDescriptorSource;
using OriginalCommandDescriptorSetSource = OriginalCommandDescriptorSource<DescriptorSet>;
struct BindlessCapabilities;
} // namespace arp::rhi

namespace arp::shader {
struct ShaderReflection;
}

namespace arp::rhi_metal {
std::shared_ptr<MetalRhiNativeDescriptorGroupPayload> prepareMetalOriginalImageCellDescriptors(
    const rhi::BindlessSnapshot&);
std::shared_ptr<const rhi::BindlessPreparedComputeBinding> prepareMetalRhiComputeBinding(
    std::shared_ptr<const rhi::BindlessComputeBindingOwners>,
    const rhi::BindlessComputeBindingBudget&,
    core::pmr::String&);
std::shared_ptr<const MetalRhiNativePreparedComputeBindings> metalRhiPreparedComputePayloadFor(
    const rhi::BindlessPreparedComputeBinding&) noexcept;
std::shared_ptr<const rhi::BindlessPreparedGraphicsBinding> prepareMetalRhiGraphicsBinding(
    std::shared_ptr<const rhi::BindlessGraphicsBindingOwners>,
    const rhi::BindlessGraphicsBindingBudget&,
    core::pmr::String&);
std::shared_ptr<const MetalRhiNativePreparedGraphicsBindings> metalRhiPreparedGraphicsPayloadFor(
    const rhi::BindlessPreparedGraphicsBinding&) noexcept;

struct MetalRhiSelectedGraphicsEntries {
    core::StringView vertex, fragment;
};

// Borrowed only from the concrete pipeline's owned descriptor, never caller metadata.
MetalRhiSelectedGraphicsEntries metalRhiSelectedGraphicsEntriesFor(
    const rhi::RenderPipeline&) noexcept;
std::optional<rhi::OriginalBindlessGraphicsPassContract::PipelineState>
metalRhiOriginalGraphicsPassPipelineState(const rhi::RenderPipeline&) noexcept;

class MetalRhiStagingUploadPool;

struct MetalRhiRegisteredResource {
    rhi::DescriptorResourceKind kind = rhi::DescriptorResourceKind::Buffer;
    rhi::ResourceUsage bufferUsage = rhi::ResourceUsage::None;
    std::uint64_t bufferSizeBytes = 0u;
    bool gpuWritesSealed = false;
    bool gpuTransferWritesSealed = false;
    std::shared_ptr<const MetalRhiImmutableTextureRevision> immutableTextureRevision;
    rhi::TextureUsage textureUsage = rhi::TextureUsage::None;
    std::shared_ptr<MetalRhiNativeBufferPayload> buffer;
    std::shared_ptr<MetalRhiNativeTexturePayload> texture;
    std::shared_ptr<MetalRhiNativeTextureViewPayload> textureView;
    std::shared_ptr<MetalRhiNativeSamplerPayload> sampler;
    rhi::TextureFormat textureFormat = rhi::TextureFormat::Unknown;
    rhi::Extent3D textureExtent;
    std::uint32_t textureMipLevelCount = 0;
    std::uint32_t textureArrayLayerCount = 0;
    std::uint32_t textureSampleCount = 0;
    rhi::TextureViewDimension textureViewDimension = rhi::TextureViewDimension::Texture2D;
    rhi::TextureAspect textureViewAspect = rhi::TextureAspect::Color;
    bool parentTextureLive = false;
    rhi::GpuResourceId parentTexture;
    // Weak concrete-wrapper tickets; strong native payloads cannot revive them.
    core::LifetimeObserver<const rhi::GpuTexture> textureWrapper;
    core::LifetimeObserver<const rhi::GpuTextureView> textureViewWrapper;

    // Exact issuing registry payload, never a fallback or a guessed parent.
    std::shared_ptr<const void> payloadOwner() const noexcept
    {
        return metalRhiCanonicalResourceOwner(kind, buffer, texture, textureView, sampler);
    }

    const MetalRhiNativeResourcePayload* nativeResource() const noexcept
    {
        return metalRhiCanonicalNativeResource(kind, buffer, texture, textureView, sampler);
    }
};

class MetalRhiResourceRegistry {
public:
    /// Construct with an explicit CPU memory resource. When omitted, the
    /// process-wide default resource is used. Threaded through to the pmr
    /// resources map so a backend can bind a per-instance intent/budget
    /// instead of silently falling back to the global default.
    explicit MetalRhiResourceRegistry(
        std::pmr::memory_resource* memoryResource = &core::defaultCpuMemoryResource())
        : resources_(memoryResource)
    {}

    void registerBuffer(const rhi::GpuBuffer& buffer);
    void registerTexture(const rhi::GpuTexture& texture);
    void registerTextureView(const rhi::GpuTextureView& textureView);
    void registerSampler(const rhi::GpuSampler& sampler);
    void unregisterResource(rhi::GpuResourceId id);
    std::optional<MetalRhiRegisteredResource> find(rhi::GpuResourceId id) const;

private:
    struct RegisteredResourceEntry {
        rhi::DescriptorResourceKind kind = rhi::DescriptorResourceKind::Buffer;
        rhi::ResourceUsage bufferUsage = rhi::ResourceUsage::None;
        std::uint64_t bufferSizeBytes = 0u;
        std::weak_ptr<const rhi::GpuBufferWriteSealState> gpuWriteSeal;
        rhi::TextureUsage textureUsage = rhi::TextureUsage::None;
        std::weak_ptr<MetalRhiNativeBufferPayload> buffer;
        std::weak_ptr<MetalRhiNativeTexturePayload> texture;
        std::weak_ptr<MetalRhiNativeTextureViewPayload> textureView;
        std::weak_ptr<MetalRhiNativeSamplerPayload> sampler;
        // Cold sampler interning authenticates the original native owner,
        // rather than trusting a subsequently mutable payload observation.
        std::weak_ptr<void> samplerNativeObject;
        rhi::TextureFormat textureFormat = rhi::TextureFormat::Unknown;
        rhi::Extent3D textureExtent;
        std::uint32_t textureMipLevelCount = 0;
        std::uint32_t textureArrayLayerCount = 0;
        std::uint32_t textureSampleCount = 0;
        rhi::TextureViewDimension textureViewDimension = rhi::TextureViewDimension::Texture2D;
        rhi::TextureAspect textureViewAspect = rhi::TextureAspect::Color;
        rhi::GpuResourceId parentTexture;
        core::LifetimeObserver<const rhi::GpuTexture> textureWrapper;
        core::LifetimeObserver<const rhi::GpuTextureView> textureViewWrapper;
    };

    mutable std::mutex mutex_;
    // Node ownership ends at unregister; no bucket capacity retains a resource quota.
    core::pmr::Map<std::uint64_t, RegisteredResourceEntry> resources_;
};

std::uint64_t estimateMetalRhiTextureBytes(const rhi::TextureDesc& desc);
// Observes original parent/view payload marker. Does not issue a revision.
const char* metalRhiTextureWriteDiagnostic(
    const std::shared_ptr<MetalRhiResourceRegistry>&, rhi::GpuResourceId) noexcept;
const char* metalRhiOriginalTextureAccessDiagnostic(const MetalRhiRegisteredResource&) noexcept;
const char* metalRhiOriginalTextureAccessDiagnostic(
    const std::shared_ptr<MetalRhiResourceRegistry>&, rhi::GpuResourceId) noexcept;
const char* metalRhiRenderPassWriteDiagnostic(
    const std::shared_ptr<MetalRhiResourceRegistry>&, const rhi::RenderPassDesc&) noexcept;

rhi::ResourceAllocation metalRhiResourceAllocation(const MetalRhiAllocation& allocation);

arp::core::UniquePtr<rhi::GpuBuffer> createMetalRhiBuffer(rhi::GpuResourceId id,
    const rhi::BufferDesc& desc,
    const rhi::ResourceAllocation& allocation,
    std::shared_ptr<void> allocationReleaseToken,
    std::shared_ptr<MetalRhiResourceRegistry> registry,
    std::shared_ptr<MetalRhiNativeBufferPayload> preparedPayload = {});

arp::core::UniquePtr<rhi::GpuBufferView> createMetalRhiBufferView(
    rhi::GpuResourceId id, const rhi::GpuBuffer& buffer, const rhi::BufferViewDesc& desc);

arp::core::UniquePtr<rhi::GpuTexture> createMetalRhiTexture(rhi::GpuResourceId id,
    const rhi::TextureDesc& desc,
    const rhi::ResourceAllocation& allocation,
    std::shared_ptr<void> allocationReleaseToken,
    std::shared_ptr<MetalRhiResourceRegistry> registry);

/// Placed-texture overload: accepts a pre-created native payload (from
/// createPlacedMetalRhiNativeTexturePayload) instead of creating one
/// internally. The payload owns the native texture; memory is owned by the heap.
/// Transient descriptors require real private heap residency; ordinary
/// pre-created resources (including swapchain drawables) need no alias heap.
arp::core::UniquePtr<rhi::GpuTexture> createMetalRhiTexture(rhi::GpuResourceId id,
    const rhi::TextureDesc& desc,
    const rhi::ResourceAllocation& allocation,
    std::shared_ptr<MetalRhiNativeTexturePayload> nativePayload,
    std::shared_ptr<MetalRhiResourceRegistry> registry);

arp::core::UniquePtr<rhi::GpuTextureView> createMetalRhiTextureView(rhi::GpuResourceId id,
    const rhi::GpuTexture& texture,
    const rhi::TextureViewDesc& desc,
    std::shared_ptr<MetalRhiResourceRegistry> registry);

arp::core::UniquePtr<rhi::GpuSampler> createMetalRhiSampler(rhi::GpuResourceId id,
    const rhi::SamplerDesc& desc,
    std::shared_ptr<MetalRhiResourceRegistry> registry,
    bool argumentBufferCompatible = false);

MetalRhiNativeResourceInspection inspectMetalRhiNativePayload(const rhi::GpuBuffer& buffer);

MetalRhiNativeResourceInspection inspectMetalRhiNativePayload(const rhi::GpuTexture& texture);

MetalRhiNativeResourceInspection inspectMetalRhiNativePayload(const rhi::GpuTextureView& view);

MetalRhiNativeResourceInspection inspectMetalRhiNativePayload(const rhi::GpuSampler& sampler);

std::shared_ptr<MetalRhiNativeBufferPayload> metalRhiNativePayloadFor(const rhi::GpuBuffer& buffer);

std::shared_ptr<MetalRhiNativeTexturePayload> metalRhiNativePayloadFor(
    const rhi::GpuTexture& texture);

std::shared_ptr<MetalRhiNativeTextureViewPayload> metalRhiNativePayloadFor(
    const rhi::GpuTextureView& textureView);

std::shared_ptr<MetalRhiNativeSamplerPayload> metalRhiNativePayloadFor(
    const rhi::GpuSampler& sampler);

rhi::ResourceBarrierResult setMetalRhiBufferStateForBarrier(rhi::GpuBuffer& buffer,
    const rhi::ResourceBarrierDesc& desc,
    core::MutationJournal* journal = nullptr);

rhi::ResourceBarrierResult setMetalRhiTextureStateForBarrier(rhi::GpuTexture& texture,
    const rhi::ResourceBarrierDesc& desc,
    core::MutationJournal* journal = nullptr);

// Non-mutating query of the existing embedded snapshot on the recording owner.
bool canPrepareMetalRhiRecordedState(const rhi::GpuBuffer&, const core::MutationJournal&) noexcept;
bool prepareMetalRhiRecordedState(rhi::GpuBuffer& buffer, core::MutationJournal& journal);
bool prepareMetalRhiRecordedState(rhi::GpuTexture& texture, core::MutationJournal& journal);

rhi::ResourceBarrierResult prepareMetalRhiBufferForPoolReuse(rhi::GpuBuffer& buffer);

rhi::ResourceBarrierResult prepareMetalRhiTextureForPoolReuse(rhi::GpuTexture& texture);

std::shared_ptr<MetalRhiNativeDescriptorGroupPayload> metalRhiNativePayloadFor(
    const rhi::DescriptorSet& descriptorSet);
std::shared_ptr<const MetalRhiNativeDescriptorGroupPayload> metalRhiOriginalSynchronousPayloadFor(
    const std::shared_ptr<const rhi::OriginalCommandDescriptorSetSource>&) noexcept;

std::shared_ptr<MetalRhiNativeDescriptorGroupPayload>
prepareMetalRhiOriginalCompletedImageDescriptors(
    const rhi::BindlessSnapshot& snapshot, rhi::OriginalBindlessCompletedImageScope scope);
std::shared_ptr<MetalRhiNativeDescriptorGroupPayload>
prepareMetalRhiOriginalCompletedDepthDescriptors(const rhi::BindlessSnapshot& snapshot);
std::shared_ptr<MetalRhiNativeDescriptorGroupPayload>
prepareMetalRhiOriginalOrdinaryColorDescriptors(
    const std::shared_ptr<const rhi::OriginalCommandBindlessRootSource>&, std::uint32_t set);
std::shared_ptr<MetalRhiNativeDescriptorGroupPayload>
prepareMetalRhiOriginalOrdinaryDepthDescriptors(
    const std::shared_ptr<const rhi::OriginalCommandBindlessRootSource>&, std::uint32_t set);
bool metalRhiDescriptorRegistryMatches(
    const rhi::DescriptorArena&, const std::shared_ptr<MetalRhiResourceRegistry>&) noexcept;
bool metalRhiDescriptorRegistryMatches(
    const rhi::DescriptorSet&, const std::shared_ptr<MetalRhiResourceRegistry>&) noexcept;

std::shared_ptr<MetalRhiNativeDescriptorTablePayload> metalRhiNativePayloadFor(
    const rhi::DescriptorTable& descriptorTable);

MetalRhiDescriptorPayloadInspection inspectMetalRhiDescriptorPayload(
    const rhi::DescriptorSet& descriptorSet);

std::shared_ptr<MetalRhiNativePipelinePayload> metalRhiNativePayloadFor(
    const rhi::RenderPipeline& pipeline);

std::shared_ptr<MetalRhiNativePipelinePayload> metalRhiNativePayloadFor(
    const rhi::ComputePipeline& pipeline);

MetalRhiPipelinePayloadInspection inspectMetalRhiPipelinePayload(
    const rhi::RenderPipeline& pipeline);

MetalRhiPipelinePayloadInspection inspectMetalRhiPipelinePayload(
    const rhi::ComputePipeline& pipeline);

std::shared_ptr<MetalRhiNativeCommandPayload> metalRhiNativePayloadFor(
    const rhi::CommandBuffer& commandBuffer);
std::shared_ptr<MetalRhiNativeCommandPayload> metalRhiNativePayloadFor(
    const rhi::CommandEncoder& commandEncoder);

MetalRhiNativeCommandStats inspectMetalRhiNativeCommandStats(
    const rhi::CommandBuffer& commandBuffer);

MetalRhiNativeQueueStats inspectMetalRhiNativeQueueStats(const rhi::GpuQueue& queue);

arp::core::UniquePtr<rhi::PipelineLayout> createMetalRhiPipelineLayout(
    const rhi::PipelineLayoutDesc& desc,
    std::shared_ptr<rhi::semantics::DirectObjectTelemetryTracker> telemetry = nullptr,
    rhi::semantics::DirectObjectBudgetLease budgetLease = {});

rhi::PipelineValidationReport validateMetalRhiRenderPipeline(const rhi::RenderPipelineDesc& desc,
    const rhi::semantics::NativeBackendCapabilityContract& capabilityContract);

rhi::PipelineValidationReport validateMetalRhiComputePipeline(const rhi::ComputePipelineDesc& desc,
    const rhi::semantics::NativeBackendCapabilityContract& capabilityContract);

arp::core::UniquePtr<rhi::RenderPipeline> createMetalRhiRenderPipeline(
    const rhi::RenderPipelineDesc& desc,
    const rhi::semantics::NativeBackendCapabilityContract& capabilityContract,
    std::shared_ptr<rhi::semantics::DirectObjectTelemetryTracker> telemetry = nullptr,
    rhi::semantics::DirectObjectBudgetLease budgetLease = {});

arp::core::UniquePtr<rhi::ComputePipeline> createMetalRhiComputePipeline(
    const rhi::ComputePipelineDesc& desc,
    const rhi::semantics::NativeBackendCapabilityContract& capabilityContract,
    std::shared_ptr<rhi::semantics::DirectObjectTelemetryTracker> telemetry = nullptr,
    rhi::semantics::DirectObjectBudgetLease budgetLease = {});

arp::core::UniquePtr<rhi::ComputePipeline> createMetalRhiAuthorizedBindlessComputePipeline(
    const rhi::BindlessCrossExtension& issuer,
    std::shared_ptr<const rhi::BindlessAuthorization> authorization,
    std::string_view entry,
    const rhi::semantics::NativeBackendCapabilityContract& capabilityContract,
    std::shared_ptr<rhi::semantics::DirectObjectTelemetryTracker> telemetry = nullptr,
    rhi::semantics::DirectObjectBudgetLease budgetLease = {},
    core::pmr::String* failureDiagnostic = nullptr);

// Owned source/layout prerequisite. This creates no BindlessAuthorization.
core::UniquePtr<rhi::RenderPipeline> createMetalRhiRetainedGraphicsPipeline(
    std::shared_ptr<const rhi::BindlessShaderPackage>,
    const rhi::RenderPipelineDesc& state,
    std::string_view vertexEntry,
    std::string_view fragmentEntry,
    const rhi::semantics::NativeBackendCapabilityContract&,
    core::pmr::String& diagnostic);
core::UniquePtr<rhi::RenderPipeline> createMetalRhiAuthorizedBindlessRenderPipeline(
    const rhi::BindlessCrossExtension&,
    std::shared_ptr<const rhi::BindlessAuthorization>,
    const rhi::RenderPipelineDesc& state,
    std::string_view vertexEntry,
    std::string_view fragmentEntry,
    const rhi::semantics::NativeBackendCapabilityContract&,
    std::shared_ptr<rhi::semantics::DirectObjectTelemetryTracker>,
    rhi::semantics::DirectObjectBudgetLease,
    core::pmr::String& diagnostic,
    const rhi::BindlessGraphicsPushRequest* push = nullptr);

arp::core::UniquePtr<rhi::ComputePipeline> createMetalRhiAuthorizedBindlessComputePipeline(
    const rhi::ComputePipelineDesc& desc,
    const rhi::semantics::NativeBackendCapabilityContract& capabilityContract,
    const shader::ShaderReflection& verifiedPackageReflection,
    std::string_view verifiedMslSource,
    std::shared_ptr<const rhi::BindlessAuthorization> authorization,
    std::shared_ptr<rhi::semantics::DirectObjectTelemetryTracker> telemetry = nullptr,
    rhi::semantics::DirectObjectBudgetLease budgetLease = {});

arp::core::UniquePtr<rhi::DescriptorArena> createMetalRhiDescriptorArena(
    const rhi::DescriptorArenaDesc& desc,
    std::shared_ptr<MetalRhiResourceRegistry> registry,
    std::shared_ptr<rhi::semantics::DirectObjectTelemetryTracker> telemetry = nullptr,
    rhi::semantics::DirectObjectBudgetLease budgetLease = {});

arp::core::UniquePtr<rhi::CommandEncoder> createMetalRhiCommandEncoder(
    const rhi::CommandEncoderDesc& desc,
    MetalRhiStagingUploadPool* stagingUploadPool = nullptr,
    std::shared_ptr<MetalRhiResourceRegistry> registry = nullptr);

// Backend-private cold storage; public preparation/lowering admission remains
// the device extension's responsibility.
core::UniquePtr<rhi::CommandEncoder> createMetalRhiOriginalOrdinaryCommandEncoder(
    std::shared_ptr<const rhi::OriginalCommandStreamProgram>,
    const rhi::GpuDevice&,
    const rhi::OriginalRenderPassPreparationRequest&,
    std::shared_ptr<MetalRhiResourceRegistry>,
    core::pmr::String&);

arp::core::UniquePtr<rhi::CommandEncoder> createMetalRhiCommandEncoder(
    const rhi::CommandEncoderDesc& desc,
    const rhi::semantics::NativeBackendCapabilityContract& capabilityContract,
    MetalRhiStagingUploadPool* stagingUploadPool = nullptr,
    std::shared_ptr<MetalRhiResourceRegistry> registry = nullptr,
    const rhi::BindlessCapabilities* bindlessCapabilities = nullptr,
    const rhi::BindlessCrossExtension* bindlessIssuer = nullptr);

arp::core::UniquePtr<rhi::SecondaryCommandEncoder> createMetalRhiSecondaryCommandEncoder(
    const rhi::SecondaryCommandEncoderDesc& desc,
    const rhi::semantics::NativeBackendCapabilityContract& capabilityContract);

arp::core::UniquePtr<rhi::RenderBundleEncoder> createMetalRhiRenderBundleEncoder(
    const rhi::RenderBundleDesc& desc,
    const rhi::semantics::NativeBackendCapabilityContract& capabilityContract);

arp::core::UniquePtr<rhi::Swapchain> createMetalRhiSwapchain(const rhi::SwapchainDesc& desc,
    std::shared_ptr<MetalRhiNativeSwapchainStats> swapchainStats,
    std::shared_ptr<MetalRhiResourceRegistry> registry);

} // namespace arp::rhi_metal

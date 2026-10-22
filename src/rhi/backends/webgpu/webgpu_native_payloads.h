#pragma once

#include "core/memory/atomic_capacity.h"
#include "rhi/backends/webgpu/webgpu_native_runtime.h"
#include "rhi/backends/webgpu/webgpu_original_canvas.h"
#include "rhi/frontend/resource_state_control.h"
#include "rhi/frontend/resource_texture_content_access.h"
#include "rhi/frontend/resource_texture_validation.h"
#include "rhi/frontend/resource_whole_state_view.h"

#include <algorithm>
#include <array>

#if defined(__EMSCRIPTEN__) && defined(__has_include)
#if __has_include(<webgpu/webgpu.h>)
#define ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE 1
#include <webgpu/webgpu.h>
#endif
#endif

#include "rhi/backends/webgpu/webgpu_pipeline_ownership.h"
#include "rhi/frontend/descriptor_validation.h"

namespace arp::rhi {
class GpuBuffer;
class GpuTexture;
class GpuDevice;
class DescriptorSet;
class DescriptorArena;
class ComputePipeline;
class RenderPipeline;
template <class Resource> class OriginalCommandResourceLease;
struct OriginalFiniteDescriptorWriteRequest;
struct OriginalFiniteDescriptorBufferWrite;
struct OriginalCommandDescriptorSetFrozenElement;
class OriginalCommandResourceSources;
} // namespace arp::rhi

namespace arp::rhi {
struct OriginalWholeDescriptorWriteRequest;
struct OriginalWholeDescriptorBufferWrite;
} // namespace arp::rhi

namespace arp::rhi_webgpu {
class WebGpuRhiDescriptorSet;
template <class Actor> class WebGpuArtifactCreationScopeCallbacks;

struct WebGpuNativeBufferPayload : core::SharedObjectIdentity {
    // Default construction is reserved for negative fixtures. Real native
    // factories always pass their explicit L2 creation state.
    WebGpuNativeBufferPayload() : WebGpuNativeBufferPayload(rhi::ResourceState::Undefined) {}

    explicit WebGpuNativeBufferPayload(rhi::ResourceState initial) noexcept
        : creationState_(initial), state_(initial)
    {}

    rhi::ResourceState creationState() const noexcept { return creationState_; }

    const auto& recordedState() const noexcept { return state_; }

    void invalidateRecordedState() noexcept { state_.invalidate(); }

    const char* recordState(
        rhi::ResourceState after, core::MutationJournal* journal = nullptr) noexcept
    {
        return state_.writeBuffer(after, journal);
    }

private:
    friend class rhi::GpuBuffer;

    auto& originalRecordedStateForBinding() noexcept { return state_; }

    const rhi::ResourceState creationState_;
    rhi::common::RecordedResourceStateControl state_;

public:
    // Released after the native destroy/release destructor body. Copies of
    // the native payload retain one shared allocation charge.
    std::shared_ptr<void> allocationReleaseToken;
    // Retain the RHI-owned size beside the opaque native handle so descriptor
    // dynamic-offset validation can reject an out-of-range binding before
    // WebGPU receives the bind-group command.
    std::uint64_t sizeBytes = 0u;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    WGPUBuffer buffer = nullptr;
    WGPUDevice ownerDevice = nullptr;
#endif
    ~WebGpuNativeBufferPayload()
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        if (buffer) {
            // Release only drops the emdawn wrapper. Explicit destruction is
            // required to retire the browser GPU allocation without waiting
            // for JavaScript GC.
            if (!webGpuNativeDeviceLost()) {
                wgpuBufferDestroy(buffer);
            }
            wgpuBufferRelease(buffer);
        }
#endif
    }
};

struct WebGpuNativeTextureViewPayload;

struct WebGpuNativeTextureViewCacheKey {
    // Native enum values are used so attachment views created by command
    // lowering share the same cache entries as L2-created views.
    std::uint32_t nativeFormat = 0u;
    rhi::TextureUsage usage = rhi::TextureUsage::None;
    std::uint32_t nativeDimension = 0u;
    std::uint32_t baseMipLevel = 0u;
    std::uint32_t mipLevelCount = 1u;
    std::uint32_t baseArrayLayer = 0u;
    std::uint32_t arrayLayerCount = 1u;
    std::uint32_t nativeAspect = 0u;

    bool operator==(const WebGpuNativeTextureViewCacheKey& other) const noexcept
    {
        return nativeFormat == other.nativeFormat && usage == other.usage
            && nativeDimension == other.nativeDimension && baseMipLevel == other.baseMipLevel
            && mipLevelCount == other.mipLevelCount && baseArrayLayer == other.baseArrayLayer
            && arrayLayerCount == other.arrayLayerCount && nativeAspect == other.nativeAspect;
    }
};

struct WebGpuNativeTextureViewCacheEntry {
    WebGpuNativeTextureViewCacheKey key;
    // A live view retains its parent texture. The reverse cache edge must be
    // weak or both native storage and its budget token survive forever.
    std::weak_ptr<WebGpuNativeTextureViewPayload> payload;
};

struct WebGpuNativeTexturePayload : core::SharedObjectIdentity {
    class OriginalUploadKey {
        friend class WebGpuNativeTextureFactory;
        OriginalUploadKey() = default;

    public:
        OriginalUploadKey(const OriginalUploadKey&) = default;
    };

    class OriginalAttachmentKey {
        friend class WebGpuNativeTextureFactory;
        OriginalAttachmentKey() = default;

    public:
        OriginalAttachmentKey(const OriginalAttachmentKey&) = default;
    };

    // Empty payloads exist only for negative fixtures; they carry no control.
    WebGpuNativeTexturePayload() = default;

    explicit WebGpuNativeTexturePayload(const rhi::TextureDesc& desc)
        : creationState_(desc.initialState), state_(std::in_place, desc, desc.initialState)
    {}

    WebGpuNativeTexturePayload(OriginalUploadKey, const rhi::TextureDesc& desc)
        : creationState_(desc.initialState), state_(std::in_place, desc, desc.initialState),
          accessPolicy_(rhi::semantics::TextureContentAccessPolicy::Mode::OriginalUploadExclusive)
    {}

    WebGpuNativeTexturePayload(OriginalAttachmentKey, const rhi::TextureDesc& desc)
        : creationState_(desc.initialState), state_(std::in_place, desc, desc.initialState),
          accessPolicy_(
              rhi::semantics::TextureContentAccessPolicy::Mode::OriginalAttachmentExclusive)
    {}

    const char* ordinaryContentAccessDiagnostic() const noexcept
    {
        if (originalCanvas_ && originalCanvas_->claimed_)
            return "RHI-WEBGPU-ORIGINAL-CANVAS-EXCLUSIVE-USE";
        return accessPolicy_.ordinaryAccessDiagnostic();
    }

    const auto& contentAccessPolicy() const noexcept { return accessPolicy_; }

    rhi::ResourceState creationState() const noexcept { return creationState_; }

    auto recordedState() const noexcept
    {
        return state_ ? rhi::common::RecordedWholeResourceStateView{*state_}
                      : rhi::common::RecordedWholeResourceStateView{};
    }

    const auto& normalizedRecordedState() const { return state_.value(); }

    bool hasNormalizedRecordedState() const noexcept { return state_.has_value(); }

    void invalidateRecordedState() noexcept
    {
        if (state_)
            state_->invalidate();
    }

    const char* recordState(
        rhi::ResourceState after, core::MutationJournal* journal = nullptr) noexcept
    {
        return state_ ? state_->writeWhole(after, journal) : "RHI-TEXTURE-STATE-SOURCE-MISSING";
    }

    const char* recordSubresourceState(const rhi::TextureSubresourceRange& range,
        rhi::ResourceState before,
        rhi::ResourceState after,
        core::MutationJournal* journal) noexcept
    {
        return state_ ? state_->write(range, before, after, journal)
                      : "RHI-TEXTURE-STATE-SOURCE-MISSING";
    }

private:
    friend class rhi::GpuTexture;
    friend class WebGpuRhiSwapchain;
    friend class WebGpuConformanceClearExecution;
    friend class WebGpuConformanceClearFrame;
    std::shared_ptr<class WebGpuOriginalCanvas> originalCanvas_;

    auto& originalRecordedStateForBinding() noexcept { return *state_; }

    const rhi::ResourceState creationState_ = rhi::ResourceState::Undefined;
    std::optional<rhi::common::RecordedTextureStateControl> state_;
    const rhi::semantics::TextureContentAccessPolicy accessPolicy_{
        rhi::semantics::TextureContentAccessPolicy::Mode::Ordinary};

public:
    std::shared_ptr<void> allocationReleaseToken;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    WGPUTexture texture = nullptr;
    WGPUDevice ownerDevice = nullptr;
    // Surface current textures are owned by the browser compositor. Calling
    // wgpuTextureDestroy on them before the next animation-frame tick drops
    // the canvas frame instead of presenting it.
    bool browserSurfaceTexture = false;
    std::uint32_t acquiredExecutionTurn = 0;
    bool acquiredImageValid = false;
    WGPUTextureFormat format = WGPUTextureFormat_Undefined;
    WGPUTextureDimension dimension = WGPUTextureDimension_2D;
    rhi::TextureFormat rhiFormat = rhi::TextureFormat::Unknown;
    rhi::Extent3D extent;
    std::uint32_t mipLevelCount = 1u;
    std::uint32_t arrayLayerCount = 1u;
    std::uint32_t sampleCount = 1u;
#endif
    // Entries are created cold and live only as long as their views. Per-view
    // nodes release their quota on retirement; a vector's spare capacity would
    // retain temporary view charges until the long-lived parent is destroyed.
    mutable core::pmr::List<WebGpuNativeTextureViewCacheEntry> viewCache;

    ~WebGpuNativeTexturePayload()
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        if (texture) {
            if (!browserSurfaceTexture && !webGpuNativeDeviceLost()) {
                wgpuTextureDestroy(texture);
            }
            // Host reference retirement is required even after physical loss.
            // Destroy keeps its independent GPU/surface policy above.
            wgpuTextureRelease(texture);
        }
#endif
    }
};

class WebGpuNativeTextureFactory final {
    friend class WebGpuOriginalTextureUploadDestination;

public:
    static std::shared_ptr<WebGpuNativeTexturePayload> create(const rhi::TextureDesc&);
    static std::shared_ptr<WebGpuNativeTexturePayload> createCanonicalContent(
        const rhi::TextureDesc&, const rhi::OriginalTextureContentCreationContract::Decision&);
    static const char* validateOriginalUpload(
        const WebGpuNativeTexturePayload&, const rhi::TextureDesc&) noexcept;
    static const char* validateOriginalAttachment(
        const WebGpuNativeTexturePayload&, const rhi::TextureDesc&) noexcept;

private:
    static const char* validateOriginalContent(
        const WebGpuNativeTexturePayload&, const rhi::TextureDesc&, bool attachment) noexcept;
    static std::shared_ptr<WebGpuNativeTexturePayload> createOriginalUpload(
        const rhi::TextureDesc&);
    static std::shared_ptr<WebGpuNativeTexturePayload> create(
        const rhi::TextureDesc&, bool originalUpload, bool originalAttachment = false);
};

struct WebGpuNativeTextureViewPayload : core::SharedObjectIdentity {
    // A WebGPU texture view does not keep the parent texture's explicit
    // destroy lifetime alive. Command payloads retain views for submitted
    // attachment work, so the view must retain its parent payload as well.
    std::shared_ptr<WebGpuNativeTexturePayload> parentTexture;
    WebGpuNativeTextureViewCacheKey creationKey;
    bool matchesDescription(const rhi::TextureViewDesc&) const noexcept;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    WGPUTextureView view = nullptr;
    WGPUTextureFormat format = WGPUTextureFormat_Undefined;
#endif
    ~WebGpuNativeTextureViewPayload()
    {
        // The cache does not own a view. Drop expired weak edges while the
        // retained parent is still alive, so the child's allocation quota is
        // released now rather than at the parent's eventual destruction.
        if (parentTexture) {
            auto& cache = parentTexture->viewCache;
            cache.remove_if([](const auto& entry) {
                return entry.payload.expired();
            });
        }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        if (view) {
            wgpuTextureViewRelease(view);
        }
#endif
    }
};

struct WebGpuNativeSamplerPayload : core::SharedObjectIdentity {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    WGPUSampler sampler = nullptr;
    WGPUDevice ownerDevice = nullptr;
#endif
    ~WebGpuNativeSamplerPayload()
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        if (sampler) {
            wgpuSamplerRelease(sampler);
        }
#endif
    }
};

struct WebGpuNativeDescriptorGroupPayload : core::SharedObjectIdentity {
private:
    friend class WebGpuRhiDescriptorSet;
    friend class WebGpuRhiDescriptorTable;
    friend class WebGpuCanonicalNativeClosure;
    void* originalScopedNativeDevice() const noexcept;
    std::shared_ptr<const void> originalScopedQuotaOwner(std::size_t) const noexcept;
    const char* diagnoseOriginalClosurePipeline(
        const std::shared_ptr<const rhi::ComputePipeline>&, std::uint32_t) const noexcept;
    const char* diagnoseOriginalClosurePipeline(
        const std::shared_ptr<const rhi::RenderPipeline>&, std::uint32_t) const noexcept;
    const char* diagnoseOriginalClosureBinding(std::size_t,
        std::size_t,
        const rhi::DescriptorBindingWrite&,
        const std::shared_ptr<const rhi::OriginalCommandResourceLease<rhi::GpuBuffer>>&,
        std::uint32_t) const noexcept;
    const char* diagnoseOriginalGeneralClosureBinding(std::size_t,
        std::size_t,
        const rhi::OriginalCommandDescriptorSetElement&,
        const rhi::OriginalCommandResourceSources&) const noexcept;
    template <class Actor> friend class WebGpuArtifactCreationScopeCallbacks;
    struct OriginalFiniteWriteState;
    core::UniquePtr<OriginalFiniteWriteState> originalFiniteWrite_;
    void observeCreationScope(std::uint32_t participant, bool successful) noexcept;
    bool finiteScopeObserved(std::uint32_t participant) const noexcept;
    bool finiteScopesDrained() const noexcept;
    const char* finiteCandidateDiagnostic() const noexcept;
    core::pmr::Vector<rhi::DescriptorBindingWrite>& finiteCandidateWrites() noexcept;
    void commitOriginalFinite() noexcept;
    struct OriginalDepthWriteState;
    struct OriginalGeneralWriteState;
    static std::shared_ptr<WebGpuNativeDescriptorGroupPayload> prepareOriginalGeneral(
        const rhi::GpuDevice&,
        const rhi::DescriptorSet&,
        const std::shared_ptr<const rhi::DescriptorArena>&,
        const std::shared_ptr<rhi::ComputePipeline>&,
        const std::shared_ptr<const rhi::OriginalCommandResourceSources>&,
        std::uint32_t,
        const rhi::OriginalCommandDescriptorSetElement*,
        std::size_t,
        const rhi::OriginalGeneralDescriptorWriteRequest&,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>&,
        const char*&);
    static std::shared_ptr<WebGpuNativeDescriptorGroupPayload> prepareOriginalGeneral(
        const rhi::GpuDevice&,
        const rhi::DescriptorSet&,
        const std::shared_ptr<const rhi::DescriptorArena>&,
        const std::shared_ptr<rhi::RenderPipeline>&,
        const std::shared_ptr<const rhi::OriginalCommandResourceSources>&,
        std::uint32_t,
        const rhi::OriginalCommandDescriptorSetElement*,
        std::size_t,
        const rhi::OriginalGeneralDescriptorWriteRequest&,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>&,
        const char*&);
    const char* generalContentDiagnostic() const noexcept;
    std::uint32_t generalContentContractVersion() const noexcept;
    template <class Pipeline>
    const char* generalSourceDiagnostic(const std::shared_ptr<const Pipeline>&,
        const std::shared_ptr<const rhi::OriginalCommandResourceSources>&,
        std::uint32_t,
        const rhi::OriginalCommandDescriptorSetFrozenElement*,
        std::size_t,
        const std::shared_ptr<core::CpuMemoryQuota>&) const noexcept;
    const char* diagnoseOriginalGeneralSource(const std::shared_ptr<const rhi::ComputePipeline>&,
        const std::shared_ptr<const rhi::OriginalCommandResourceSources>&,
        std::uint32_t,
        const rhi::OriginalCommandDescriptorSetFrozenElement*,
        std::size_t,
        const std::shared_ptr<core::CpuMemoryQuota>&) const noexcept;
    const char* diagnoseOriginalGeneralSource(const std::shared_ptr<const rhi::RenderPipeline>&,
        const std::shared_ptr<const rhi::OriginalCommandResourceSources>&,
        std::uint32_t,
        const rhi::OriginalCommandDescriptorSetFrozenElement*,
        std::size_t,
        const std::shared_ptr<core::CpuMemoryQuota>&) const noexcept;
    template <class Pipeline>
    static std::shared_ptr<WebGpuNativeDescriptorGroupPayload> prepareOriginalDepthTable(
        const rhi::GpuDevice&,
        const rhi::DescriptorTable&,
        const std::shared_ptr<const rhi::DescriptorArena>&,
        const std::shared_ptr<Pipeline>&,
        const rhi::OriginalDepthTablePublicationWrite*,
        std::size_t,
        const rhi::OriginalDepthTablePublicationRequest&,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>&,
        const char*&);
    const core::pmr::Vector<rhi::OriginalDepthTablePublicationWrite>&
    depthCandidateWrites() const noexcept;
    core::pmr::Vector<std::uint32_t>& depthCandidateIndices() noexcept;
    const char* depthContentDiagnostic() const noexcept;
    const char* depthPipelineDiagnostic(const rhi::ComputePipeline&) const noexcept;
    const char* depthPipelineDiagnostic(const rhi::RenderPipeline&) const noexcept;
    const char* finiteNativeIdentityDiagnostic() const noexcept;
    const char* wholeContentDiagnostic() const noexcept;
    template <class Pipeline>
    const char* finiteSourceDiagnostic(const std::shared_ptr<const Pipeline>&,
        std::uint32_t,
        const rhi::OriginalFiniteDescriptorBufferWrite*,
        std::size_t,
        const std::shared_ptr<core::CpuMemoryQuota>&,
        bool targetMemory = false) const noexcept;
    const char* diagnoseOriginalFiniteSource(const std::shared_ptr<const rhi::ComputePipeline>&,
        std::uint32_t,
        const rhi::OriginalFiniteDescriptorBufferWrite*,
        std::size_t,
        const std::shared_ptr<core::CpuMemoryQuota>&) const noexcept;
    const char* diagnoseOriginalFiniteSource(const std::shared_ptr<const rhi::RenderPipeline>&,
        std::uint32_t,
        const rhi::OriginalFiniteDescriptorBufferWrite*,
        std::size_t,
        const std::shared_ptr<core::CpuMemoryQuota>&) const noexcept;
    const char* diagnoseOriginalFiniteTargetMemorySource(
        const std::shared_ptr<const rhi::ComputePipeline>&,
        std::uint32_t,
        const rhi::OriginalFiniteDescriptorBufferWrite*,
        std::size_t,
        const std::shared_ptr<core::CpuMemoryQuota>&) const noexcept;
    const char* diagnoseOriginalFiniteTargetMemorySource(
        const std::shared_ptr<const rhi::RenderPipeline>&,
        std::uint32_t,
        const rhi::OriginalFiniteDescriptorBufferWrite*,
        std::size_t,
        const std::shared_ptr<core::CpuMemoryQuota>&) const noexcept;
    template <class Pipeline>
    static std::shared_ptr<WebGpuNativeDescriptorGroupPayload> prepareOriginalFiniteImpl(
        const rhi::GpuDevice&,
        const rhi::DescriptorSet&,
        const std::shared_ptr<const rhi::DescriptorArena>&,
        const std::shared_ptr<Pipeline>&,
        std::uint32_t,
        const rhi::OriginalFiniteDescriptorBufferWrite*,
        std::size_t,
        const rhi::OriginalFiniteDescriptorWriteRequest&,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>&,
        const char*&);
    static std::shared_ptr<WebGpuNativeDescriptorGroupPayload> prepareOriginalFinite(
        const rhi::GpuDevice&,
        const rhi::DescriptorSet&,
        const std::shared_ptr<const rhi::DescriptorArena>&,
        const std::shared_ptr<rhi::ComputePipeline>&,
        std::uint32_t,
        const rhi::OriginalFiniteDescriptorBufferWrite*,
        std::size_t,
        const rhi::OriginalFiniteDescriptorWriteRequest&,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>&,
        const char*&);
    static std::shared_ptr<WebGpuNativeDescriptorGroupPayload> prepareOriginalFinite(
        const rhi::GpuDevice&,
        const rhi::DescriptorSet&,
        const std::shared_ptr<const rhi::DescriptorArena>&,
        const std::shared_ptr<rhi::RenderPipeline>&,
        std::uint32_t,
        const rhi::OriginalFiniteDescriptorBufferWrite*,
        std::size_t,
        const rhi::OriginalFiniteDescriptorWriteRequest&,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>&,
        const char*&);
    static std::shared_ptr<WebGpuNativeDescriptorGroupPayload> prepareOriginalFinite(
        const rhi::GpuDevice&,
        const rhi::DescriptorSet&,
        const std::shared_ptr<const rhi::DescriptorArena>&,
        const std::shared_ptr<rhi::ComputePipeline>&,
        std::uint32_t,
        const rhi::OriginalFiniteDescriptorBufferWrite*,
        std::size_t,
        const rhi::OriginalFiniteTargetMemoryWriteRequest&,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>&,
        const char*&);
    static std::shared_ptr<WebGpuNativeDescriptorGroupPayload> prepareOriginalFinite(
        const rhi::GpuDevice&,
        const rhi::DescriptorSet&,
        const std::shared_ptr<const rhi::DescriptorArena>&,
        const std::shared_ptr<rhi::RenderPipeline>&,
        std::uint32_t,
        const rhi::OriginalFiniteDescriptorBufferWrite*,
        std::size_t,
        const rhi::OriginalFiniteTargetMemoryWriteRequest&,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>&,
        const char*&);
    template <class Pipeline, class Request, class Write>
    static std::shared_ptr<WebGpuNativeDescriptorGroupPayload> prepareOriginalScopedImpl(
        const rhi::GpuDevice&,
        const rhi::DescriptorSet&,
        const std::shared_ptr<const rhi::DescriptorArena>&,
        const std::shared_ptr<Pipeline>&,
        std::uint32_t,
        const Write*,
        std::size_t,
        const Request&,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>&,
        const char*&,
        const std::shared_ptr<const rhi::OriginalCommandResourceSources>& = {});
    static std::shared_ptr<WebGpuNativeDescriptorGroupPayload> prepareOriginalWhole(
        const rhi::GpuDevice&,
        const rhi::DescriptorSet&,
        const std::shared_ptr<const rhi::DescriptorArena>&,
        const std::shared_ptr<rhi::ComputePipeline>&,
        std::uint32_t,
        const rhi::OriginalWholeDescriptorBufferWrite*,
        std::size_t,
        const rhi::OriginalWholeDescriptorWriteRequest&,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>&,
        const char*&);
    static std::shared_ptr<WebGpuNativeDescriptorGroupPayload> prepareOriginalWhole(
        const rhi::GpuDevice&,
        const rhi::DescriptorSet&,
        const std::shared_ptr<const rhi::DescriptorArena>&,
        const std::shared_ptr<rhi::RenderPipeline>&,
        std::uint32_t,
        const rhi::OriginalWholeDescriptorBufferWrite*,
        std::size_t,
        const rhi::OriginalWholeDescriptorWriteRequest&,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>&,
        const char*&);

public:
    bool containsOriginalDepthTableWrite() const noexcept;
    bool containsOriginalFiniteWrite() const noexcept;
    bool containsOriginalWholeWrite() const noexcept;
    bool containsOriginalGeneralWrite() const noexcept;
    bool containsOriginalFiniteTargetMemoryWrite() const noexcept;

    bool containsOriginalScopedWrite() const noexcept { return bool(originalFiniteWrite_); }

    const char* diagnoseOriginalNativeIdentity(
        const rhi::OriginalDescriptorNativeIdentityRequest&) const noexcept;
    const char* diagnoseOriginalFiniteGroup(
        std::uint32_t& group, const std::uint32_t*& offsets, std::uint32_t& count) const noexcept;
    /// Reserved for a descriptor payload produced by a future lowering-aware
    /// path. The descriptor helper sets this only after consuming an explicit
    /// physical lowering ticket; it never infers the conversion from layout
    /// names or resource kinds.
    bool containsLoweredInputAttachment = false;
    /// Non-zero only for a descriptor group containing an explicit physical
    /// InputAttachment lowering. The command path compares this with the
    /// active pipeline's shader-package identity before binding.
    std::uint64_t shaderPackageHash = 0;
    /// Number of buffer bindings declared with dynamicOffset=true. WebGPU
    /// requires the exact count on every setBindGroup call.
    std::uint32_t dynamicOffsetCount = 0u;
    /// Binding numbers corresponding to the dynamic offset array, retained
    /// as explicit backend metadata for the canonical ordering contract.
    core::pmr::Vector<std::uint32_t> dynamicOffsetBindings{&core::defaultCpuMemoryResource()};
    // Retain the backend-owned resource payloads so a versioned compact
    // lowering can assemble one physical bind group from multiple logical
    // descriptor sets.  The opaque WGPUBindGroup alone cannot be merged.
    core::pmr::Vector<WebGpuNativeDescriptorBindingPayload> bindings{
        &core::defaultCpuMemoryResource()};
    core::pmr::Vector<WebGpuCompletedTexturePublication> completedTexturePublications{
        &core::defaultCpuMemoryResource()};

    const char* ordinaryTextureAccessDiagnostic() const noexcept
    {
        if (containsOriginalGeneralWrite())
            return "RHI-ORIGINAL-GENERAL-WRITE-EMISSION-CLOSED";
        if (containsOriginalDepthTableWrite())
            return "RHI-ORIGINAL-TABLE-PUBLICATION-ORDINARY-CLOSED";
        if (containsOriginalFiniteWrite())
            return "RHI-ORIGINAL-FINITE-WRITE-EMISSION-CLOSED";
        if (containsOriginalWholeWrite())
            return "RHI-ORIGINAL-WHOLE-WRITE-EMISSION-CLOSED";
        for (const auto& binding : bindings)
            if (binding.textureView) {
                if (!binding.textureView->parentTexture)
                    return "RHI-WEBGPU-DESCRIPTOR-TEXTURE-PARENT-MISSING";
                if (const auto* id = binding.textureView->parentTexture->contentAccessPolicy()
                        .ordinaryAccessDiagnostic())
                    return id;
            }
        return nullptr;
    }

    /// True when this payload came from DescriptorTable rather than a logical
    /// descriptor set. Its synthetic binding number is remapped by the active
    /// pipeline's explicit table lowering before native bind-group creation.
    bool tableBacked = false;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    WGPUBindGroupLayout layout = nullptr;
    WGPUBindGroup group = nullptr;
#endif
    ~WebGpuNativeDescriptorGroupPayload()
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        if (group) {
            wgpuBindGroupRelease(group);
        }
        if (layout) {
            wgpuBindGroupLayoutRelease(layout);
        }
#endif
    }
};

class WebGpuNativeShaderModulePayload final {
public:
    class ConstructionKey {
        friend class WebGpuOriginalArtifactComputeFactory;
        friend class WebGpuOriginalArtifactGraphicsFactory;
        friend std::shared_ptr<WebGpuNativeShaderModulePayload>
        createWebGpuNativeShaderModulePayload(const rhi::ShaderPackageDesc&);
        friend std::shared_ptr<WebGpuNativeShaderModulePayload>
        createWebGpuNativeBindlessShaderModulePayload(const rhi::BindlessShaderPackage&,
            std::string_view,
            rhi::ShaderStage,
            core::pmr::String&,
            bool);
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    WebGpuNativeShaderModulePayload(ConstructionKey, core::StringView source)
        : sourceBytes_(source.size())
    {
        const auto digest = core::Sha256::digest(source);
        std::copy(digest.begin(), digest.end(), sourceSha256_.begin());
    }

    const auto& sourceSha256() const noexcept { return sourceSha256_; }

    std::uint64_t sourceBytes() const noexcept { return sourceBytes_; }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    WGPUShaderModule module = nullptr;
#endif
    ~WebGpuNativeShaderModulePayload()
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        if (module) {
            wgpuShaderModuleRelease(module);
        }
#endif
    }

private:
    std::array<std::uint8_t, core::Sha256::digestByteCount> sourceSha256_{};
    const std::uint64_t sourceBytes_;
};

struct WebGpuNativeRenderPipelinePayload : WebGpuNativePipelineOwnership {
    bool hasViewport = false;
    rhi::ViewportState viewport;
    bool hasScissor = false;
    rhi::ScissorState scissor;
    bool usesPushConstantGroup = false;
    bool hasValidatedInputAttachmentLowering = false;
    std::uint16_t sourceContractVersion = 1u;
    std::uint64_t shaderPackageHash = 0;
    std::uint32_t pushConstantGroup = UINT32_MAX;
    std::uint32_t pushConstantBinding = UINT32_MAX;
    std::uint32_t bindGroupCount = 0u;
    rhi::PrimitiveTopology primitiveTopology = rhi::PrimitiveTopology::TriangleList;
    std::uint32_t vertexBufferCount = 0u;
    core::pmr::Vector<rhi::TextureFormat> colorFormats{&core::defaultCpuMemoryResource()};
    rhi::TextureFormat depthStencilFormat = rhi::TextureFormat::Unknown;
    std::uint32_t sampleCount = 1u;
    core::pmr::Vector<WebGpuNativeDescriptorSetGroupLowering> descriptorSetGroups{
        &core::defaultCpuMemoryResource()};
    core::pmr::Vector<WebGpuNativeDescriptorBindingLowering> descriptorBindings{
        &core::defaultCpuMemoryResource()};
    core::pmr::Vector<WebGpuNativeDescriptorTableLowering> descriptorTables{
        &core::defaultCpuMemoryResource()};
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    WGPURenderPipeline pipeline = nullptr;
    WGPUPipelineLayout layout = nullptr;
#endif
    ~WebGpuNativeRenderPipelinePayload()
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        if (pipeline) {
            wgpuRenderPipelineRelease(pipeline);
        }
        if (layout) {
            wgpuPipelineLayoutRelease(layout);
        }
#endif
    }
};

struct WebGpuNativeComputePipelinePayload : WebGpuNativePipelineOwnership {
    bool usesPushConstantGroup = false;
    bool hasValidatedInputAttachmentLowering = false;
    std::uint16_t sourceContractVersion = 1u;
    std::uint64_t shaderPackageHash = 0;
    std::uint32_t pushConstantGroup = UINT32_MAX;
    std::uint32_t pushConstantBinding = UINT32_MAX;
    std::uint32_t bindGroupCount = 0u;
    core::pmr::Vector<WebGpuNativeDescriptorSetGroupLowering> descriptorSetGroups{
        &core::defaultCpuMemoryResource()};
    core::pmr::Vector<WebGpuNativeDescriptorBindingLowering> descriptorBindings{
        &core::defaultCpuMemoryResource()};
    core::pmr::Vector<WebGpuNativeDescriptorTableLowering> descriptorTables{
        &core::defaultCpuMemoryResource()};
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    WGPUComputePipeline pipeline = nullptr;
    WGPUPipelineLayout layout = nullptr;
#endif
    ~WebGpuNativeComputePipelinePayload()
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        if (pipeline) {
            wgpuComputePipelineRelease(pipeline);
        }
        if (layout) {
            wgpuPipelineLayoutRelease(layout);
        }
#endif
    }
};

class WebGpuNativeZeroBaseIndirectProducer final {
    class ConstructionKey {
        friend std::shared_ptr<WebGpuNativeZeroBaseIndirectProducer>
        prepareWebGpuNativeZeroBaseIndirectProducer(const rhi::BindlessShaderPackage&,
            const std::shared_ptr<WebGpuNativeDescriptorGroupPayload>&,
            core::pmr::String&);

        ConstructionKey() {}

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

    friend std::shared_ptr<WebGpuNativeZeroBaseIndirectProducer>
    prepareWebGpuNativeZeroBaseIndirectProducer(const rhi::BindlessShaderPackage&,
        const std::shared_ptr<WebGpuNativeDescriptorGroupPayload>&,
        core::pmr::String&);
    friend struct WebGpuNativeCommandPayload;
    friend struct WebGpuRetainedZeroBaseProducer;
    friend class WebGpuNativeIndirectProcessorPool;

public:
    explicit WebGpuNativeZeroBaseIndirectProducer(ConstructionKey) {}

    ~WebGpuNativeZeroBaseIndirectProducer();
    WebGpuNativeZeroBaseIndirectProducer(const WebGpuNativeZeroBaseIndirectProducer&) = delete;

private:
    std::shared_ptr<WebGpuNativeComputePipelinePayload> pipeline_;
    std::array<std::shared_ptr<WebGpuNativeBufferPayload>, 6> resources_;
    std::shared_ptr<WebGpuNativeBufferPayload> faultLatch_;
    mutable core::AtomicCapacityBudget recordings_{1u};
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    WGPUDevice device_ = nullptr;
    WGPUBindGroupLayout layout_ = nullptr;
    WGPUBindGroup group_ = nullptr;
#endif
};

/// A native WebGPU render bundle owns the encoder-produced bundle object and
/// retains every backend payload referenced while recording. The RHI bundle
/// object never exposes these handles; the consuming render-pass payload is
/// the only owner allowed to execute the finished bundle.
struct WebGpuNativeRenderBundlePayload {
    bool nativeRuntimeBacked = false;
    bool finished = false;
    bool pipelineBound = false;
    bool indexBufferBound = false;
    std::uint32_t commandCount = 0u;
    rhi::PrimitiveTopology primitiveTopology = rhi::PrimitiveTopology::TriangleList;
    core::pmr::Vector<rhi::TextureFormat> colorFormats{&core::defaultCpuMemoryResource()};
    rhi::TextureFormat depthStencilFormat = rhi::TextureFormat::Unknown;
    std::uint32_t sampleCount = 1u;
    std::shared_ptr<WebGpuNativeRenderPipelinePayload> retainedPipeline;
    core::pmr::Vector<WebGpuNativeDescriptorSetGroupLowering> requiredDescriptorSets{
        &core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::shared_ptr<WebGpuNativeBufferPayload>> retainedBuffers{
        &core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::shared_ptr<WebGpuNativeDescriptorGroupPayload>> retainedDescriptorGroups{
        &core::defaultCpuMemoryResource()};

    struct BoundDescriptorSet {
        std::uint32_t logicalSet = 0u;
        std::shared_ptr<WebGpuNativeDescriptorGroupPayload> payload;
        core::pmr::Vector<std::uint32_t> dynamicOffsets{&core::defaultCpuMemoryResource()};
    };

    core::pmr::Vector<BoundDescriptorSet> boundDescriptorSets{&core::defaultCpuMemoryResource()};
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    core::pmr::Vector<WGPUBindGroup> retainedNativeDescriptorGroups{
        &core::defaultCpuMemoryResource()};
    WGPURenderBundleEncoder encoder = nullptr;
    WGPURenderBundle bundle = nullptr;
#endif
    ~WebGpuNativeRenderBundlePayload()
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        if (encoder) {
            wgpuRenderBundleEncoderRelease(encoder);
        }
        if (bundle) {
            wgpuRenderBundleRelease(bundle);
        }
        for (auto group : retainedNativeDescriptorGroups) {
            if (group) {
                wgpuBindGroupRelease(group);
            }
        }
#endif
    }
};

#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
// Maps an rhi::TextureFormat to its WGPUTextureFormat equivalent. Returns
// WGPUTextureFormat_Undefined for Unknown / Depth24Stencil8 (WebGPU does not
// expose Depth24Stencil8 in this build) / unrecognized formats. Previously
// duplicated verbatim across webgpu_native_resources.cpp,
// webgpu_native_runtime.cpp and webgpu_native_swapchain.cpp; now defined once
// in webgpu_native_resources.cpp and shared by the other two, satisfying the
// "no duplicated utility functions in backend paths" constraint.
WGPUTextureFormat nativeTextureFormat(rhi::TextureFormat format) noexcept;
#endif

} // namespace arp::rhi_webgpu

#pragma once
#include "core/ownership/shared_identity_projection.h"
#include "rhi/backends/webgpu/webgpu_native_payloads.h"
#include "rhi/backends/webgpu/webgpu_original_geometry_content.h"
#include "rhi/frontend/bindless_root_initialized_shader.h"
#include "rhi/frontend/command_sources.h"
#include "rhi/frontend/command_stream_program.h"

namespace arp::rhi_webgpu {
// Authentic Root, same canonical inventory; no replacement graph or journal.
// PreparedCompute/1 or explicit Graphics preparation/2 freezes the native
// pipeline and physical group. This same closure freezes every original
// buffer, including readback. Graphics preparation grants no execution.
class WebGpuBindlessRootNativeClosure final : public core::QuotaBoundSharedObjectIdentity {
    friend class WebGpuNativeCommandPreparation;
    friend class WebGpuOrdinaryRecording;

    class ConstructionKey {
        friend class WebGpuBindlessRootNativeClosure;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

    using Root = rhi::OriginalCommandBindlessRootSource;
    using Sources = rhi::OriginalCommandResourceSources;
    using Budget = rhi::OriginalBindlessRootNativeClosureBudget;
    using GeometryContent = WebGpuOriginalGeometryContent;

    struct Entry {
        std::size_t sourceIndex = SIZE_MAX;
        rhi::GpuResourceId resource;
        std::uint64_t generation = 0u, bytes = 0u;
        std::shared_ptr<WebGpuNativeBufferPayload> owner;
        std::shared_ptr<WebGpuNativeTextureViewPayload> viewOwner;
        std::shared_ptr<WebGpuNativeTexturePayload> parentOwner;
        std::shared_ptr<WebGpuNativeSamplerPayload> samplerOwner;
        rhi::OriginalCompletedDepthReadInputContract::Input depthInput;
        std::shared_ptr<const GeometryContent> initializedGeometry;
        std::optional<rhi::GpuBuffer::CommittedStateObservation> initializedGeometryState;
        std::shared_ptr<const GeometryContent> initializedShader;
        std::optional<rhi::GpuBuffer::CommittedStateObservation> initializedShaderState;

        const auto& initializedActor() const noexcept
        {
            return initializedGeometry ? initializedGeometry : initializedShader;
        }

        std::size_t parentSourceIndex = SIZE_MAX;
        rhi::GpuResourceId parentResource;
        std::uint64_t parentGeneration = 0u;
        rhi::PipelineStage sampledStages = rhi::PipelineStage::None;
        rhi::TextureSubresourceRange sampledRange;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        WGPUBuffer handle = nullptr;
        WGPUTextureView view = nullptr;
        WGPUTexture texture = nullptr;
        WGPUSampler sampler = nullptr;
        WGPUDevice device = nullptr;
#endif
    };

    struct Attachment {
        std::size_t sourceIndex = SIZE_MAX, parentSourceIndex = SIZE_MAX;
        rhi::GpuResourceId resource, parentResource;
        std::uint64_t generation = 0u, parentGeneration = 0u;
        std::shared_ptr<WebGpuNativeTextureViewPayload> owner;
        std::shared_ptr<WebGpuNativeTexturePayload> parent;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        WGPUTextureView handle = nullptr;
        WGPUTexture texture = nullptr;
        WGPUDevice device = nullptr;
#endif
    };

    struct OrdinaryGroup {
        std::uint32_t logicalSet = UINT32_MAX, groupIndex = UINT32_MAX;
        std::shared_ptr<WebGpuNativeDescriptorGroupPayload> descriptors, physical;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        WGPUBindGroup group = nullptr;
        WGPUBindGroupLayout layout = nullptr;
#endif
    };

public:
    struct GeometryContents {
        const std::shared_ptr<const GeometryContent>* data = nullptr;
        std::size_t count = 0u;
        const std::shared_ptr<const GeometryContent>* shaderData = nullptr;
        std::size_t shaderCount = 0u;
    };

    WebGpuBindlessRootNativeClosure(ConstructionKey,
        std::shared_ptr<const Root>,
        std::shared_ptr<const Sources>,
        Budget,
        std::size_t);
    static std::shared_ptr<const WebGpuBindlessRootNativeClosure> prepareOriginal(
        std::shared_ptr<const Root>,
        std::shared_ptr<const Sources>,
        rhi::GpuDevice&,
        const Budget&,
        std::shared_ptr<core::CpuMemoryQuota>,
        const char*&);
    static std::shared_ptr<const WebGpuBindlessRootNativeClosure> prepareOriginal(
        std::shared_ptr<const Root>,
        std::shared_ptr<const Sources>,
        rhi::GpuDevice&,
        const Budget&,
        std::shared_ptr<core::CpuMemoryQuota>,
        const char*&,
        GeometryContents);
    static const char* validateOriginal(
        const std::shared_ptr<const WebGpuBindlessRootNativeClosure>&) noexcept;
    const char* validate() const noexcept;
    static const char* validateOriginalPending(
        const std::shared_ptr<const WebGpuBindlessRootNativeClosure>&,
        const std::shared_ptr<const rhi::OriginalCommandRecordingSeal>&) noexcept;
    const std::shared_ptr<const GeometryContent>& initializedGeometry(std::size_t) const noexcept;

    const auto& originalRoot() const noexcept { return root_; }

    const auto& originalInventory() const noexcept { return inventory_; }

    std::shared_ptr<WebGpuNativePipelineOwnership> originalPipelineOwnership() const noexcept;

    std::size_t resourceEntryCount() const noexcept
    {
        return entries_.size() + attachments_.size();
    }

    std::size_t attachmentViewCount() const noexcept { return attachments_.size(); }

    std::uint32_t nativeOwnerCount() const noexcept { return nativeOwners_; }

    const char* combinedNativeOwnerCount(const WebGpuBindlessRootNativeClosure&,
        std::uint32_t,
        std::uint32_t&,
        const WebGpuBindlessRootNativeClosure* final = nullptr) const noexcept;

    std::uint32_t retainedOwnerCount() const noexcept { return retainedOwners_; }

    // Read-only custody projection; identity/generation are authenticated by
    // validateOriginal/validateOriginalPending on this same closed object.
    bool retainsOriginalBufferSource(std::size_t source) const noexcept
    {
        for (const auto& entry : entries_)
            if (entry.sourceIndex == source)
                return true;
        return false;
    }

    bool hasInitializedGeometry() const noexcept
    {
        return rhi::OriginalBindlessRootNativeClosureContract::hasInitializedGeometry(budget_);
    }

    std::uint32_t initializedGeometryOwnerCount() const noexcept;
    const char* validateGeometryFetch(
        const std::shared_ptr<const rhi::OriginalCommandStreamProgram>&,
        const rhi::OriginalIndexedDrawFetchContract::Budget&) const noexcept;

    const std::shared_ptr<const GeometryContent>& initializedShader(
        std::size_t source) const noexcept
    {
        for (const auto& entry : entries_)
            if (entry.sourceIndex == source && entry.initializedShader)
                return entry.initializedShader;
        static const std::shared_ptr<const GeometryContent> empty;
        return empty;
    }

    bool nativeExecutionProven() const noexcept { return false; }

private:
    const char* validatePhase(
        const std::shared_ptr<const rhi::OriginalCommandRecordingSeal>&) const noexcept;
    const char* captureGeometryEntry(std::size_t, Entry&) const noexcept;
    const char* inspectInitializedGeometry(
        const std::shared_ptr<const rhi::OriginalCommandRecordingSeal>&) const noexcept;
    const char* captureEntry(std::size_t, Entry&) const noexcept;
    const char* captureOrdinaryImage(const rhi::OriginalCommandDescriptorSetFrozenElement&,
        const OrdinaryGroup&,
        Entry&) const noexcept;
    const char* captureAttachment(std::size_t, Attachment&, bool cold = false) const noexcept;
    const char* inspectOwners(std::uint32_t&, std::uint32_t&) const noexcept;
    const char* inspectGeometryOwners(std::uint32_t&) const noexcept;
    std::size_t nativeOwnerReferences() const noexcept;
    core::SharedIdentityProjection::Record nativeOwnerAt(std::size_t) const noexcept;
    std::size_t expectedEntryCount() const noexcept;
    const char* validateOrdinaryGroups() const noexcept;
    const std::shared_ptr<const Root> root_;
    const std::shared_ptr<const Sources> inventory_;
    const Budget budget_;
    core::pmr::Vector<Entry> entries_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<Attachment> attachments_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<OrdinaryGroup> ordinaryGroups_{&core::defaultCpuMemoryResource()};
    std::uint32_t nativeOwners_ = 0u, retainedOwners_ = 0u;
};
} // namespace arp::rhi_webgpu

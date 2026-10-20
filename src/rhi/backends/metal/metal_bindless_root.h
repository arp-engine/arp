#pragma once
#include "rhi/backends/metal/metal_native_descriptors.h"
#include "rhi/backends/metal/metal_original_geometry_content.h"
#include "rhi/frontend/bindless_completed_image_domain.h"
#include "rhi/frontend/bindless_root_initialized_shader.h"
#include "rhi/frontend/command_sources.h"

namespace arp::rhi_metal {
// Backend-private correspondence over the same original inventory. No new
// resource graph, journal, command, result store or completion authority.
class MetalRhiBindlessRootNativeClosure final : public core::QuotaBoundSharedObjectIdentity {
    friend class MetalOriginalBindlessCommand;
    friend class MetalOriginalBindlessGraphicsPass;

    class ConstructionKey {
        friend class MetalRhiBindlessRootNativeClosure;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

    using Root = rhi::OriginalCommandBindlessRootSource;
    using Sources = rhi::OriginalCommandResourceSources;
    using Budget = rhi::OriginalBindlessRootNativeClosureBudget;
    using GeometryContent = MetalOriginalGeometryContent;

    struct Group {
        std::uint32_t set = 0u;
        std::shared_ptr<const MetalRhiNativePreparedComputeBindings> prepared;
        std::shared_ptr<const MetalRhiNativePreparedGraphicsBindings> graphics;
        std::shared_ptr<const MetalRhiNativeDescriptorGroupPayload> frozenDescriptors;
        std::array<std::shared_ptr<const MetalRhiNativePreparedFunctionBindings>, 2u> frozenStages;
        std::array<std::shared_ptr<const MetalRhiPreparedGraphicsStageLayout>, 2u> frozenLayouts;

        const auto& descriptors() const noexcept { return frozenDescriptors; }

        const MetalRhiNativePreparedFunctionBindings* function(std::size_t index) const noexcept
        {
            return prepared ? (index == 0u ? prepared.get() : nullptr) : frozenStages[index].get();
        }
    };

    struct Entry {
        std::size_t sourceIndex = SIZE_MAX;
        std::size_t groupIndex = 0u;
        rhi::GpuResourceId resource;
        std::uint64_t generation = 0u;
        rhi::DescriptorResourceKind kind = rhi::DescriptorResourceKind::Buffer;
        std::shared_ptr<MetalRhiNativeBufferPayload> buffer;
        std::shared_ptr<MetalRhiNativeTexturePayload> texture;
        std::shared_ptr<void> heap;
        std::shared_ptr<MetalRhiNativeTextureViewPayload> view;
        std::shared_ptr<MetalRhiNativeSamplerPayload> sampler;
        std::shared_ptr<const rhi::OriginalCompletedDepthTextureSource> completedDepth;
        std::shared_ptr<const GeometryContent> initializedGeometry;
        std::optional<rhi::GpuBuffer::CommittedStateObservation> initializedGeometryState;
        std::shared_ptr<const GeometryContent> initializedShader;
        std::optional<rhi::GpuBuffer::CommittedStateObservation> initializedShaderState;

        const auto& initializedActor() const noexcept
        {
            return initializedGeometry ? initializedGeometry : initializedShader;
        }

        std::uint64_t parentGeneration = 0u;
        MetalRhiNativeResourceObservation identity;
        MetalRhiNativeResourceObservation parentIdentity;

        const MetalRhiNativeResourcePayload* nativeResource() const noexcept
        {
            return metalRhiCanonicalNativeResource(kind, buffer, {}, view, sampler);
        }
    };

public:
    struct GeometryContents {
        const std::shared_ptr<const GeometryContent>* data = nullptr;
        std::size_t count = 0u;
        const std::shared_ptr<const GeometryContent>* shaderData = nullptr;
        std::size_t shaderCount = 0u;
    };

    MetalRhiBindlessRootNativeClosure(ConstructionKey,
        std::shared_ptr<const Root>,
        std::shared_ptr<const Sources>,
        Budget,
        std::size_t entries);
    static std::shared_ptr<const MetalRhiBindlessRootNativeClosure> prepareOriginal(
        std::shared_ptr<const Root>,
        std::shared_ptr<const Sources>,
        rhi::GpuDevice&,
        const Budget&,
        std::shared_ptr<core::CpuMemoryQuota>,
        const char*&);
    static std::shared_ptr<const MetalRhiBindlessRootNativeClosure> prepareOriginal(
        std::shared_ptr<const Root>,
        std::shared_ptr<const Sources>,
        rhi::GpuDevice&,
        const Budget&,
        std::shared_ptr<core::CpuMemoryQuota>,
        const char*&,
        GeometryContents);
    static const char* validateOriginal(
        const std::shared_ptr<const MetalRhiBindlessRootNativeClosure>&) noexcept;
    const char* validate() const noexcept;
    static const char* validateOriginalPending(
        const std::shared_ptr<const MetalRhiBindlessRootNativeClosure>&,
        const std::shared_ptr<const rhi::OriginalCommandRecordingSeal>&) noexcept;
    const std::shared_ptr<const GeometryContent>& initializedGeometry(std::size_t) const noexcept;

    const auto& originalRoot() const noexcept { return root_; }

    const auto& originalInventory() const noexcept { return inventory_; }

    std::size_t resourceEntryCount() const noexcept { return entries_.size(); }

    std::uint32_t nativeOwnerCount() const noexcept { return nativeOwners_; }

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

    std::uint32_t closureContractVersion() const noexcept { return budget_.contractVersion; }

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

    const char* bufferExecutionDiagnostic() const noexcept;
    const char* storageExecutionDiagnostic() const noexcept;
    const char* completedDepthExecutionDiagnostic() const noexcept;
    const char* ordinaryColorGraphicsExecutionDiagnostic() const noexcept;
    const char* ordinaryDepthGraphicsExecutionDiagnostic() const noexcept;
    const char* completedColorExecutionDiagnostic(
        rhi::OriginalBindlessCompletedImageScope scope =
            rhi::OriginalBindlessCompletedImageScope::ComputeColor8) const noexcept;

private:
    const char* validatePhase(
        const std::shared_ptr<const rhi::OriginalCommandRecordingSeal>&) const noexcept;
    const char* captureGeometryEntry(std::size_t, Entry&) const noexcept;
    const char* inspectInitializedGeometry(
        const std::shared_ptr<const rhi::OriginalCommandRecordingSeal>&) const noexcept;
    const char* captureEntry(std::size_t, Entry&) const noexcept;
    const char* inspectOwners(std::uint32_t&, std::uint32_t&) const noexcept;
    const std::shared_ptr<const Root> root_;
    const std::shared_ptr<const Sources> inventory_;
    const Budget budget_;
    std::shared_ptr<MetalRhiNativePipelinePayload> pipeline_;
    MetalRhiNativeResourceObservation pipelineIdentity_;
    std::array<MetalRhiNativeResourceObservation, 3u> functionIdentities_;
    std::array<bool, 3u> functionPresent_{};
    core::pmr::Vector<Group> groups_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<Entry> entries_{&core::defaultCpuMemoryResource()};
    std::uint32_t nativeOwners_ = 0u, retainedOwners_ = 0u;
};
} // namespace arp::rhi_metal

#pragma once

#include "rhi/backends/vulkan/vulkan_native_resources.h"
#include "rhi/backends/vulkan/vulkan_original_geometry_content.h"
#include "rhi/frontend/bindless_root_initialized_shader.h"
#include "rhi/frontend/command_sources.h"

namespace arp::rhi_vulkan {

// Cold payload correspondence for Root /1 and scalar-buffer Root /2. Only this factory can issue the
// immutable object. It retains the same canonical inventory, not a second
// resource graph. Native handles stay backend-private; no execution authority.
class VulkanBindlessRootNativeClosure final : public core::QuotaBoundSharedObjectIdentity {
    friend class VulkanOriginalBindlessCommand;
    friend class VulkanOriginalBindlessGraphicsPass;

    class ConstructionKey {
        friend class VulkanBindlessRootNativeClosure;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

    using Root = rhi::OriginalCommandBindlessRootSource;
    using Sources = rhi::OriginalCommandResourceSources;
    using Budget = rhi::OriginalBindlessRootNativeClosureBudget;
    using GeometryContent = VulkanOriginalGeometryContent;

    struct Observation {
        std::shared_ptr<VulkanRhiNativeResourcePayload> payload;
        VkDevice device = VK_NULL_HANDLE;
        VkBuffer buffer = VK_NULL_HANDLE;
        VkImage image = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_UNDEFINED;
        rhi::Extent3D extent;
        static Observation capture(std::shared_ptr<VulkanRhiNativeResourcePayload>);
        bool matches(const std::shared_ptr<VulkanRhiNativeResourcePayload>&) const noexcept;
    };

    struct Entry {
        std::size_t sourceIndex = SIZE_MAX;
        std::uint64_t registrationGeneration = 0u;
        rhi::GpuResourceId resource;
        Observation resourcePayload, parentPayload;
        VkImageView descriptorImageView = VK_NULL_HANDLE;
        std::shared_ptr<const GeometryContent> initializedGeometry;
        std::optional<rhi::GpuBuffer::CommittedStateObservation> initializedGeometryState;
        std::shared_ptr<const GeometryContent> initializedShader;
        std::optional<rhi::GpuBuffer::CommittedStateObservation> initializedShaderState;

        const auto& initializedActor() const noexcept
        {
            return initializedGeometry ? initializedGeometry : initializedShader;
        }

        std::shared_ptr<const rhi::OriginalCompletedDepthTextureSource> completedDepth;
    };

    struct OrdinaryDescriptor {
        std::uint32_t set = 0u;
        std::shared_ptr<VulkanRhiNativeResourcePayload> payload;
        VkDescriptorSet handle = VK_NULL_HANDLE;
        VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    };

    // Physical descriptor ordinal includes samplers and the optional fault
    // readback slot. It is deliberately distinct from shader-access ordinals.
    struct DescriptorEntry {
        const rhi::OriginalCommandDescriptorSetElement* declaration = nullptr;
        const rhi::OriginalCommandDescriptorDependencies::Owner* owner = nullptr;
        const rhi::DescriptorLayoutDesc* layout = nullptr;
        std::shared_ptr<VulkanRhiNativeResourcePayload> descriptors;
        std::uint32_t set = 0u;
        bool ordinary = false;
    };

public:
    struct GeometryContents {
        const std::shared_ptr<const GeometryContent>* data = nullptr;
        std::size_t count = 0u;
        const std::shared_ptr<const GeometryContent>* shaderData = nullptr;
        std::size_t shaderCount = 0u;
    };

    VulkanBindlessRootNativeClosure(ConstructionKey,
        std::shared_ptr<const Root>,
        std::shared_ptr<const Sources>,
        std::shared_ptr<VulkanRhiRuntime>,
        Budget,
        std::size_t);

    static std::shared_ptr<const VulkanBindlessRootNativeClosure> prepareOriginal(
        std::shared_ptr<const Root>,
        std::shared_ptr<const Sources>,
        rhi::GpuDevice&,
        const Budget&,
        std::shared_ptr<core::CpuMemoryQuota>,
        const char*&);
    static std::shared_ptr<const VulkanBindlessRootNativeClosure> prepareOriginal(
        std::shared_ptr<const Root>,
        std::shared_ptr<const Sources>,
        rhi::GpuDevice&,
        const Budget&,
        std::shared_ptr<core::CpuMemoryQuota>,
        const char*&,
        GeometryContents);
    static const char* validateOriginal(
        const std::shared_ptr<const VulkanBindlessRootNativeClosure>&) noexcept;
    const char* validate() const noexcept;

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

    bool nativeExecutionProven() const noexcept { return false; }

    std::uint32_t closureContractVersion() const noexcept { return budget_.contractVersion; }

    const std::shared_ptr<const GeometryContent>& initializedGeometry(
        std::size_t sourceIndex) const noexcept;

    const std::shared_ptr<const GeometryContent>& initializedShader(
        std::size_t source) const noexcept
    {
        for (const auto& entry : entries_)
            if (entry.sourceIndex == source && entry.initializedShader)
                return entry.initializedShader;
        static const std::shared_ptr<const GeometryContent> empty;
        return empty;
    }

    std::uint32_t initializedGeometryCount() const noexcept { return initializedGeometryCount_; }

    std::uint64_t initializedGeometryBytes() const noexcept { return initializedGeometryBytes_; }

private:
    static const char* validateOriginalPending(
        const std::shared_ptr<const VulkanBindlessRootNativeClosure>&,
        const std::shared_ptr<const rhi::OriginalCommandRecordingSeal>&) noexcept;
    const char* validatePhase(
        const std::shared_ptr<const rhi::OriginalCommandRecordingSeal>&) const noexcept;
    const char* captureEntry(std::size_t, Entry&) const noexcept;
    const char* resolveDescriptorEntry(std::size_t, DescriptorEntry&) const noexcept;
    std::size_t descriptorEntryCount() const noexcept;
    const char* inspectCompletedDepth(const Entry&, std::size_t) const noexcept;
    static std::shared_ptr<VulkanRhiNativeResourcePayload> pipelinePayload(const Root&) noexcept;
    static bool isExtraGeometrySource(const Root&, const Sources&, std::size_t) noexcept;
    static const char* captureGeometryEntry(const Sources&, std::size_t, VkDevice, Entry&) noexcept;
    const char* inspectOwners(std::uint32_t&, std::uint32_t&) const noexcept;
    const char* inspectInitializedGeometry(std::uint32_t&,
        std::uint64_t&,
        const std::shared_ptr<const rhi::OriginalCommandRecordingSeal>& = {}) const noexcept;

    const std::shared_ptr<const Root> root_;
    const std::shared_ptr<const Sources> inventory_;
    const std::shared_ptr<VulkanRhiRuntime> runtime_;
    const Budget budget_;
    core::pmr::Vector<Entry> entries_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<OrdinaryDescriptor> ordinaryDescriptors_{&core::defaultCpuMemoryResource()};
    std::shared_ptr<VulkanRhiNativeResourcePayload> pipeline_, descriptors_;
    VkPipeline pipelineHandle_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorLayout_ = VK_NULL_HANDLE;
    std::uint32_t nativeOwners_ = 0u, retainedOwners_ = 0u;
    std::uint32_t initializedGeometryCount_ = 0u;
    std::uint64_t initializedGeometryBytes_ = 0u;
};

} // namespace arp::rhi_vulkan

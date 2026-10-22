#include "core/memory/cpu_allocator.h"
#include "core/ownership/lifetime_epoch.h"
#include "core/ownership/mutation_journal.h"
#include "rhi/backends/vulkan/vulkan_backend_objects.h"
#include "rhi/backends/vulkan/vulkan_native_resources.h"
#include "rhi/backends/vulkan/vulkan_runtime.h"
#include "rhi/frontend/bindless_snapshot.h"
#include "rhi/frontend/completed_depth_texture_source.h"
#include "rhi/frontend/descriptor_resource_validation.h"
#include "rhi/frontend/descriptor_set_validation.h"
#include "rhi/frontend/descriptor_state_copy.h"
#include "rhi/frontend/resource_leases.h"
#include "rhi/semantics/bindless_original_preparation_lowering.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>

namespace arp::rhi_vulkan {

namespace {

template <typename Fn> Fn functionFrom(PFN_vkVoidFunction value)
{
    Fn result = nullptr;
    static_assert(sizeof(result) == sizeof(value), "unexpected Vulkan function pointer size");
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

VkDescriptorType descriptorTypeFor(rhi::DescriptorType type, bool dynamicOffset = false)
{
    switch (type) {
    case rhi::DescriptorType::SampledTexture:
        return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    case rhi::DescriptorType::StorageTexture:
        return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    case rhi::DescriptorType::Sampler:
        return VK_DESCRIPTOR_TYPE_SAMPLER;
    case rhi::DescriptorType::UniformBuffer:
        return dynamicOffset ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
                             : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    case rhi::DescriptorType::StorageBuffer:
    case rhi::DescriptorType::ReadOnlyStorageBuffer:
        return dynamicOffset ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC
                             : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    case rhi::DescriptorType::InputAttachment:
        return VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    }
    return VK_DESCRIPTOR_TYPE_MAX_ENUM;
}

VkImageViewType imageViewTypeFor(rhi::TextureViewDimension dimension)
{
    switch (dimension) {
    case rhi::TextureViewDimension::Texture2D:
        return VK_IMAGE_VIEW_TYPE_2D;
    case rhi::TextureViewDimension::Texture2DArray:
        return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    case rhi::TextureViewDimension::Texture3D:
        return VK_IMAGE_VIEW_TYPE_3D;
    case rhi::TextureViewDimension::TextureCube:
        return VK_IMAGE_VIEW_TYPE_CUBE;
    }
    return VK_IMAGE_VIEW_TYPE_2D;
}

VkImageAspectFlags imageAspectFor(rhi::TextureAspect aspect)
{
    switch (aspect) {
    case rhi::TextureAspect::Color:
        return VK_IMAGE_ASPECT_COLOR_BIT;
    case rhi::TextureAspect::Depth:
        return VK_IMAGE_ASPECT_DEPTH_BIT;
    case rhi::TextureAspect::Stencil:
        return VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    return VK_IMAGE_ASPECT_COLOR_BIT;
}

const rhi::DescriptorBindingDesc* findBinding(
    const rhi::DescriptorLayoutDesc& layout, std::uint32_t binding)
{
    for (const auto& candidate : layout.bindings) {
        if (candidate.binding == binding) {
            return &candidate;
        }
    }
    return nullptr;
}

rhi::ValidationResult validateRegisteredDescriptorViewShape(
    const std::shared_ptr<VulkanRhiResourceRegistry>& registry,
    rhi::GpuResourceId id,
    rhi::DescriptorResourceKind kind,
    rhi::DescriptorType type,
    bool dimensionConstrained,
    rhi::TextureViewDimension dimension,
    bool depthTexture)
{
    const auto resource = registry ? registry->payload(id) : nullptr;
    if (!resource)
        return {false, "RHI-DESCRIPTOR-RESOURCE-NOT-LIVE: Vulkan resource is unknown"};
    if (type == rhi::DescriptorType::StorageBuffer && registry->bufferGpuWritesSealed(id))
        return {false, "RHI-BUFFER-GPU-WRITES-SEALED"};
    if (type != rhi::DescriptorType::SampledTexture && type != rhi::DescriptorType::StorageTexture
        && type != rhi::DescriptorType::InputAttachment)
        return {true, {}};
    if (kind != rhi::DescriptorResourceKind::TextureView
        || !resource->backendVulkanTextureViewDescAvailable())
        return {false, "Vulkan RHI descriptor requires a texture view payload"};
    const auto parent = rhi::validateDescriptorTextureParent(registry->textureViewParentIsLive(id));
    if (!parent.success)
        return parent;
    const auto* originalView = resource->backendVulkanOriginalTextureViewDesc();
    if (!originalView)
        return {false, "Vulkan RHI descriptor requires original texture view metadata"};
    const auto& view = *originalView;
    const auto usage = type == rhi::DescriptorType::StorageTexture ? rhi::TextureUsage::Storage
        : type == rhi::DescriptorType::InputAttachment ? rhi::TextureUsage::RenderAttachment
                                                       : rhi::TextureUsage::Sampled;
    if ((view.usage & usage) != usage)
        return {false, "Vulkan RHI descriptor texture usage mismatch"};
    if (dimensionConstrained && view.dimension != dimension)
        return {false, "Vulkan RHI descriptor texture view dimension mismatch"};
    if (type == rhi::DescriptorType::InputAttachment
        && rhi::isDepthTextureFormat(view.format) != depthTexture)
        return {false, "Vulkan RHI input attachment depth/color identity mismatch"};
    return rhi::validateSampledDepthView(depthTexture, view.format, view.aspect);
}

rhi::ValidationResult validateRegisteredDescriptorView(
    const std::shared_ptr<VulkanRhiResourceRegistry>& registry,
    rhi::GpuResourceId id,
    rhi::DescriptorResourceKind kind,
    rhi::DescriptorType type,
    bool dimensionConstrained,
    rhi::TextureViewDimension dimension,
    bool depthTexture)
{
    const auto resource = registry ? registry->payload(id) : nullptr;
    if (resource && resource->backendVulkanOriginalContentExclusive())
        return {false, "RHI-TEXTURE-CONTENT-EXCLUSIVE-USE"};
    return validateRegisteredDescriptorViewShape(
        registry, id, kind, type, dimensionConstrained, dimension, depthTexture);
}

struct VulkanCompletedTexturePublication {
    std::shared_ptr<const rhi::OriginalCommandTextureViewSource> source;
    std::optional<rhi::GpuTexture::ContentExpectation> content;
    std::shared_ptr<const rhi::OriginalCompletedDepthTextureSource> depthContent;
    std::optional<rhi::GpuTexture::ContentRangeExpectation> cellIdentity;
    rhi::OriginalTextureCellDescriptorRequest cellRequest;
    std::uint32_t colorPublicationVersion = 1u;
    VkFormatFeatureFlags colorFormatFeatures = 0u;
};

// One physical descriptor version and everything needed to execute it. Native
// command retention owns this object independently of the public set wrapper.
class VulkanDescriptorStorage final : public VulkanRhiNativeResourcePayload {
public:
    VulkanDescriptorStorage(std::shared_ptr<VulkanRhiNativeResourcePayload> nativeSet,
        std::shared_ptr<VulkanRhiRuntime> runtime,
        std::size_t bindingCount)
        : runtime_(std::move(runtime)), nativeSet_(std::move(nativeSet)),
          resources(bindingCount, &core::defaultCpuMemoryResource()),
          imageViews(bindingCount, VK_NULL_HANDLE, &core::defaultCpuMemoryResource()),
          completedTextures(bindingCount, &core::defaultCpuMemoryResource()),
          bufferRanges(bindingCount, {UINT64_MAX, 0}, &core::defaultCpuMemoryResource())
    {}

    ~VulkanDescriptorStorage() override
    {
        // Release the pool/set before its auxiliary views, then selected
        // resources. runtime_ remains alive throughout all native destruction.
        nativeSet_.reset();
        if (!runtime_ || runtime_->syntheticRuntime())
            return;
        auto* destroyImageView =
            functionFrom<PFN_vkDestroyImageView>(runtime_->getDeviceProcAddr("vkDestroyImageView"));
        if (destroyImageView) {
            for (const auto view : imageViews)
                if (view != VK_NULL_HANDLE)
                    destroyImageView(runtime_->logicalDevice(), view, nullptr);
        }
    }

    bool hasNativeSet() const noexcept { return nativeSet_ != nullptr; }

    std::pair<std::uint64_t, std::uint64_t> backendVulkanDescriptorElementBufferRange(
        std::uint32_t index) const noexcept override
    {
        return index < bufferRanges.size() ? bufferRanges[index]
                                           : std::pair<std::uint64_t, std::uint64_t>{UINT64_MAX, 0};
    }

    std::shared_ptr<VulkanRhiNativeResourcePayload> backendVulkanDescriptorElementResource(
        std::uint32_t index) const override
    {
        return index < resources.size() ? resources[index] : nullptr;
    }

    VkImageView backendVulkanDescriptorElementImageView(std::uint32_t index) const override
    {
        return index < imageViews.size() ? imageViews[index] : VK_NULL_HANDLE;
    }

    std::shared_ptr<const rhi::OriginalCompletedDepthTextureSource>
    backendVulkanDescriptorElementDepthContent(std::uint32_t index) const override
    {
        return index < completedTextures.size() ? completedTextures[index].depthContent : nullptr;
    }

    VkDevice backendVulkanDeviceHandle() const override
    {
        return nativeSet_ ? nativeSet_->backendVulkanDeviceHandle() : VK_NULL_HANDLE;
    }

    VkDescriptorSet backendVulkanDescriptorSetHandle() const override
    {
        return nativeSet_ ? nativeSet_->backendVulkanDescriptorSetHandle() : VK_NULL_HANDLE;
    }

    VkDescriptorSetLayout backendVulkanDescriptorSetLayoutHandle() const override
    {
        return nativeSet_ ? nativeSet_->backendVulkanDescriptorSetLayoutHandle() : VK_NULL_HANDLE;
    }

private:
    std::shared_ptr<VulkanRhiRuntime> runtime_;
    std::shared_ptr<VulkanRhiNativeResourcePayload> nativeSet_;

public:
    core::pmr::Vector<std::shared_ptr<VulkanRhiNativeResourcePayload>> resources;
    core::pmr::Vector<VkImageView> imageViews;
    core::pmr::Vector<VulkanCompletedTexturePublication> completedTextures;
    core::pmr::Vector<std::pair<std::uint64_t, std::uint64_t>> bufferRanges;
};

class VulkanRhiDescriptorSet final : public rhi::DescriptorSet {
public:
    VulkanRhiDescriptorSet(rhi::DescriptorSetAllocation allocation,
        rhi::DescriptorLayoutDesc layout,
        const core::LifetimeEpoch& arenaGeneration,
        std::shared_ptr<VulkanRhiNativeResourcePayload> nativePayload,
        VulkanRhiNativeResourceAllocator nativeResources,
        std::shared_ptr<VulkanRhiResourceRegistry> resourceRegistry,
        std::shared_ptr<rhi::semantics::DirectObjectBudgetLease> generationBudget)
        : allocation_(std::move(allocation)), layout_(std::move(layout)),
          arenaGeneration_(arenaGeneration.ticket()),
          nativePayload_(core::makeShared<VulkanDescriptorStorage>(
              std::move(nativePayload), nativeResources.runtime(), allocation_.descriptorCapacity)),
          nativeResources_(std::move(nativeResources)),
          resourceRegistry_(std::move(resourceRegistry)),
          written_(allocation_.descriptorCapacity, false, &core::defaultCpuMemoryResource()),
          lastWrites_(allocation_.descriptorCapacity, &core::defaultCpuMemoryResource())
    {
        nativePayload_->directObjectBudgetReleaseToken = std::move(generationBudget);
    }

    const rhi::DescriptorSetAllocation& allocation() const override { return allocation_; }

    const rhi::DescriptorLayoutDesc& layout() const override { return layout_; }

    std::shared_ptr<const rhi::OriginalCompletedDepthTextureSource>
    originalCompletedDepthPublication(
        std::uint32_t binding, std::uint32_t element) const noexcept override
    {
        const auto index = rhi::descriptorElementIndex(layout_, binding, element);
        return isCurrentAllocation() && nativePayload_->hasNativeSet()
            ? nativePayload_->backendVulkanDescriptorElementDepthContent(index)
            : nullptr;
    }

    std::shared_ptr<VulkanRhiNativeResourcePayload> nativePayload() const
    {
        // Cell descriptors have no native command consumer yet. Existing
        // completed-image/ordinary exporters cannot acquire that authority.
        for (const auto& publication : nativePayload_->completedTextures)
            if (publication.cellIdentity)
                return nullptr;
        return isCurrentAllocation() && nativePayload_->hasNativeSet() ? nativePayload_ : nullptr;
    }

    std::shared_ptr<VulkanRhiNativeResourcePayload> imageCellPayload(
        const rhi::BindlessSnapshot& snapshot) const
    {
        if (snapshot.descriptorSet() != this
            || !rhi::semantics::OriginalBindlessPreparationLowering::hasImageCellBindingPlan(
                snapshot.plan())
            || !isCurrentAllocation() || !nativePayload_->hasNativeSet())
            return {};
        bool storage = false;
        const bool produced =
            rhi::semantics::OriginalBindlessPreparationLowering::hasProducedImageReadPlan(
                snapshot.plan());
        for (const auto& binding : layout_.bindings)
            for (std::uint32_t i = 0u; i < binding.arrayCount; ++i) {
                if (binding.stages != rhi::ShaderStage::Compute
                    || !validateBindingElement(binding.binding, i).success)
                    return {};
                const auto index = rhi::descriptorElementIndex(layout_, binding.binding, i);
                if (binding.type == rhi::DescriptorType::SampledTexture
                    || binding.type == rhi::DescriptorType::StorageTexture) {
                    const auto& proof = nativePayload_->completedTextures[index];
                    if (rhi::semantics::OriginalBindlessPreparationLowering::imageProofDiagnostic(
                            snapshot.plan(),
                            binding,
                            bool(proof.depthContent),
                            bool(proof.cellIdentity)))
                        return {};
                    if (produced
                        && !rhi::OriginalTextureCellDescriptorContract::isProducedImageRead(
                            proof.cellRequest.contractVersion))
                        return {};
                    storage =
                        storage || produced || binding.type == rhi::DescriptorType::StorageTexture;
                }
            }
        return storage ? nativePayload_ : nullptr;
    }

    bool contextMatches(const std::shared_ptr<VulkanRhiRuntime>& runtime,
        const std::shared_ptr<VulkanRhiResourceRegistry>& registry) const noexcept
    {
        return runtime && runtime->valid() && !runtime->syntheticRuntime()
            && core::SharedOwnerProjection::sameOwner(runtime, nativeResources_.runtime())
            && core::SharedOwnerProjection::sameOwner(registry, resourceRegistry_)
            && isCurrentAllocation()
            && nativePayload_->backendVulkanDeviceHandle() == runtime->logicalDevice()
            && nativePayload_->backendVulkanDescriptorSetHandle()
            && nativePayload_->backendVulkanDescriptorSetLayoutHandle();
    }

    const char* validateOriginalLossSource(const std::shared_ptr<VulkanRhiRuntime>& runtime,
        const std::shared_ptr<VulkanRhiResourceRegistry>& registry) const noexcept
    {
        return contextMatches(runtime, registry) ? nullptr
                                                 : "RHI-VULKAN-DESCRIPTOR-LOSS-SET-SOURCE";
    }

    rhi::ValidationResult writeImpl(const rhi::DescriptorBindingWrite& write) override
    {
        const auto originalIndex =
            rhi::descriptorElementIndex(layout_, write.binding, write.arrayElement);
        if (originalIndex < nativePayload_->completedTextures.size()
            && nativePayload_->completedTextures[originalIndex].cellIdentity)
            return {false, "RHI-TEXTURE-CELL-DESCRIPTOR-ORIGINAL-PUBLICATION-REQUIRED"};
        if (!isCurrentAllocation()) {
            return {false,
                "RHI-DESCRIPTOR-ALLOCATION-STALE: Vulkan RHI descriptor set allocation is stale"};
        }
        const auto* binding = findBinding(layout_, write.binding);
        if (!binding) {
            return {false, "Vulkan RHI descriptor binding does not exist"};
        }
        if (binding->type != write.type) {
            return {false, "Vulkan RHI descriptor binding type mismatch"};
        }
        if (!write.resource.isValid()) {
            return {false, "Vulkan RHI descriptor write requires a resource id"};
        }
        if (write.physicalLowering.enabled) {
            return {false,
                "Vulkan RHI descriptor write cannot consume a degraded input-attachment lowering"};
        }
        const auto index = rhi::descriptorElementIndex(layout_, write.binding, write.arrayElement);
        if (index == UINT32_MAX || index >= written_.size())
            return {false,
                "RHI-DESCRIPTOR-ELEMENT-RANGE: Vulkan descriptor array element is outside layout"};
        const auto viewValidation = validateTextureViewContract(*binding, write);
        if (!viewValidation.success) {
            return viewValidation;
        }
        // The resource and selected element were revalidated above. Identical
        // array writes also preserve the recorded physical descriptor version.
        if (written_[index] && sameWrite(lastWrites_[index], write)) {
            return {true, ""};
        }
        // VkDescriptorSet updates can invalidate recorded buffers or race GPU
        // use. There is no implicit copy-on-write lowering here: preserve the
        // old physical version until all recording/retention owners release it.
        if (nativePayload_.use_count() != 1u) {
            return {false,
                "RHI-DESCRIPTOR-RECORDED-STORAGE-IN-USE: Vulkan set rewrite requires retirement"};
        }
        return publishValidatedWrite(write, index, {});
    }

    rhi::ValidationResult writeOriginalBufferRangeImpl(const rhi::DescriptorBindingWrite& write,
        const std::shared_ptr<const rhi::OriginalCommandBufferLease>& source) override
    {
        const auto runtime = nativeResources_.runtime();
        const auto original = vulkanRhiNativePayload(*source->originalObject());
        const auto registered =
            resourceRegistry_ ? resourceRegistry_->payload(write.resource) : nullptr;
        if (!runtime || !runtime->valid() || runtime->syntheticRuntime() || !original
            || !core::SharedOwnerProjection::sameOwner(original, registered)
            || original->backendVulkanDeviceHandle() != runtime->logicalDevice()
            || !original->backendVulkanBufferHandle())
            return {false, "RHI-BUFFER-RANGE-DESCRIPTOR-NATIVE-IDENTITY"};
        const auto& limits = runtime->descriptorIndexing().limits;
        const bool uniform = write.type == rhi::DescriptorType::UniformBuffer;
        const auto alignment = uniform ? limits.minUniformBufferOffsetAlignment
                                       : limits.minStorageBufferOffsetAlignment;
        const auto maximum = uniform ? limits.maxUniformBufferRange : limits.maxStorageBufferRange;
        if (!alignment || !maximum || write.bufferBindingRange.offsetBytes % alignment
            || write.bufferBindingRange.sizeBytes > maximum)
            return {false, "RHI-BUFFER-RANGE-DESCRIPTOR-NATIVE-LIMITS"};
        return writeImpl(write);
    }

    rhi::ValidationResult writeOriginalTextureCellImpl(const rhi::DescriptorBindingWrite& write,
        const std::shared_ptr<const rhi::OriginalCommandTextureViewSource>& source,
        const rhi::GpuTexture::ContentRangeExpectation& identity,
        const rhi::OriginalTextureCellDescriptorRequest& request) override
    {
        const auto* binding = findBinding(layout_, write.binding);
        const auto index = rhi::descriptorElementIndex(layout_, write.binding, write.arrayElement);
        if (!binding || index == UINT32_MAX || index >= written_.size())
            return {false, "RHI-TEXTURE-CELL-DESCRIPTOR-ELEMENT"};
        const VulkanCompletedTexturePublication publication{source, {}, {}, identity, request};
        const auto validation = validateCompletedTexture(*binding, write, publication);
        if (!validation.success)
            return validation;
        const auto& prior = nativePayload_->completedTextures[index];
        if (prior.source
            && (!prior.cellIdentity
                || !core::SharedOwnerProjection::sameOwner(prior.source, source)))
            return {false, "RHI-TEXTURE-CELL-DESCRIPTOR-SOURCE-REPLACEMENT"};
        if (nativePayload_.use_count() != 1u)
            return {false, "RHI-DESCRIPTOR-RECORDED-STORAGE-IN-USE"};
        return publishValidatedWrite(write, index, publication);
    }

    rhi::ValidationResult writeOriginalCompletedTextureImpl(
        const rhi::DescriptorBindingWrite& write,
        const std::shared_ptr<const rhi::OriginalCompletedDepthTextureSource>& source) override
    {
        const auto* binding = findBinding(layout_, write.binding);
        const auto index = rhi::descriptorElementIndex(layout_, write.binding, write.arrayElement);
        if (!binding || binding->type != rhi::DescriptorType::SampledTexture || index == UINT32_MAX
            || index >= written_.size() || !source)
            return {false, "RHI-COMPLETED-TEXTURE-DESCRIPTOR-ELEMENT"};
        const VulkanCompletedTexturePublication publication{
            source->originalViewSource(), {}, source};
        if (nativePayload_->completedTextures[index].cellIdentity)
            return {false, "RHI-TEXTURE-CELL-DESCRIPTOR-ORIGINAL-PUBLICATION-REQUIRED"};
        const auto validation = validateCompletedTexture(*binding, write, publication);
        if (!validation.success)
            return validation;
        if (written_[index] && sameWrite(lastWrites_[index], write)) {
            const auto& prior = nativePayload_->completedTextures[index];
            if (!core::SharedOwnerProjection::sameOwner(prior.source, publication.source))
                return {false, "RHI-COMPLETED-TEXTURE-DESCRIPTOR-SOURCE-REPLACEMENT"};
            if (core::SharedOwnerProjection::sameOwner(prior.depthContent, source))
                return {true, {}};
        }
        if (nativePayload_.use_count() != 1u)
            return {false,
                "RHI-DESCRIPTOR-RECORDED-STORAGE-IN-USE: Vulkan set rewrite requires retirement"};
        return publishValidatedWrite(write, index, publication);
    }

    rhi::ValidationResult writeOriginalCompletedTextureImpl(
        const rhi::DescriptorBindingWrite& write,
        const std::shared_ptr<const rhi::OriginalCommandTextureViewSource>& source,
        const rhi::GpuTexture::ContentExpectation& content) override
    {
        return writeOriginalCompletedTextureImpl(write,
            source,
            content,
            {1u, true, 1u, 1u, rhi::OriginalCommandTextureViewSource::completeRetainedOwnerCount});
    }

    rhi::ValidationResult writeOriginalCompletedTextureImpl(
        const rhi::DescriptorBindingWrite& write,
        const std::shared_ptr<const rhi::OriginalCommandTextureViewSource>& source,
        const rhi::GpuTexture::ContentExpectation& content,
        const rhi::OriginalCompletedTextureDescriptorRequest& request) override
    {
        const auto* binding = findBinding(layout_, write.binding);
        const auto index = rhi::descriptorElementIndex(layout_, write.binding, write.arrayElement);
        if (!binding || binding->type != rhi::DescriptorType::SampledTexture || index == UINT32_MAX
            || index >= written_.size())
            return {false, "RHI-COMPLETED-TEXTURE-DESCRIPTOR-ELEMENT"};
        VulkanCompletedTexturePublication publication{source, content};
        publication.colorPublicationVersion = request.contractVersion;
        if (request.contractVersion == 5u) {
            const auto runtime = nativeResources_.runtime();
            if (!runtime || !runtime->valid() || runtime->syntheticRuntime())
                return {false, "RHI-VULKAN-COMPLETED-HDR-NATIVE-RUNTIME"};
            const auto format = source->originalObject()->desc().format;
            if (format != rhi::TextureFormat::Rgba8Unorm
                && format != rhi::TextureFormat::Rgba16Float)
                return {false, "RHI-VULKAN-COMPLETED-HDR-FORMAT"};
            VkFormatProperties properties{};
            vkGetPhysicalDeviceFormatProperties(runtime->physicalDevice(),
                format == rhi::TextureFormat::Rgba16Float ? VK_FORMAT_R16G16B16A16_SFLOAT
                                                          : VK_FORMAT_R8G8B8A8_UNORM,
                &properties);
            publication.colorFormatFeatures = properties.optimalTilingFeatures;
        }

        if (nativePayload_->completedTextures[index].cellIdentity)
            return {false, "RHI-TEXTURE-CELL-DESCRIPTOR-ORIGINAL-PUBLICATION-REQUIRED"};
        const auto validation = validateCompletedTexture(*binding, write, publication);
        if (!validation.success)
            return validation;
        if (written_[index] && sameWrite(lastWrites_[index], write)) {
            const auto& prior = nativePayload_->completedTextures[index];
            if (!core::SharedOwnerProjection::sameOwner(prior.source, source)
                || prior.colorPublicationVersion != publication.colorPublicationVersion)
                return {false, "RHI-COMPLETED-TEXTURE-DESCRIPTOR-SOURCE-REPLACEMENT"};
            return {true, {}};
        }
        if (nativePayload_.use_count() != 1u)
            return {false,
                "RHI-DESCRIPTOR-RECORDED-STORAGE-IN-USE: Vulkan set rewrite requires retirement"};
        return publishValidatedWrite(write, index, publication);
    }

    bool isCurrentAllocation() const override
    {
        return allocation_.success && arenaGeneration_.isCurrent();
    }

    bool hasWrittenBinding(std::uint32_t binding) const override
    {
        if (!isCurrentAllocation())
            return false;
        const auto* bindingDesc = findBinding(layout_, binding);
        if (!bindingDesc) {
            return false;
        }
        const auto offset = rhi::descriptorElementIndex(layout_, binding, 0u);
        for (std::uint32_t element = 0u; element < bindingDesc->arrayCount; ++element)
            if (written_[offset + element])
                return true;
        return false;
    }

    bool hasWrittenBindingElement(std::uint32_t binding, std::uint32_t element) const override
    {
        if (!isCurrentAllocation())
            return false;
        const auto index = rhi::descriptorElementIndex(layout_, binding, element);
        return index != UINT32_MAX && index < written_.size() && written_[index];
    }

    rhi::ValidationResult validateBinding(std::uint32_t binding) const override
    {
        if (!isCurrentAllocation()) {
            return {false,
                "RHI-DESCRIPTOR-ALLOCATION-STALE: Vulkan RHI descriptor set allocation is stale"};
        }
        if (!hasWrittenBinding(binding)) {
            return {false, "Vulkan RHI descriptor binding is not written"};
        }
        const auto* desc = findBinding(layout_, binding);
        for (std::uint32_t element = 0u; element < desc->arrayCount; ++element) {
            if (!hasWrittenBindingElement(binding, element))
                continue;
            const auto validation = validateBindingElement(binding, element);
            if (!validation.success)
                return validation;
        }
        return {true, {}};
    }

    rhi::ValidationResult validateBindingElement(
        std::uint32_t binding, std::uint32_t element) const override
    {
        if (!isCurrentAllocation())
            return {false,
                "RHI-DESCRIPTOR-ALLOCATION-STALE: Vulkan descriptor array validation after arena "
                "reset"};
        if (!hasWrittenBindingElement(binding, element))
            return {false, "Vulkan descriptor array element is not written"};
        const auto index = rhi::descriptorElementIndex(layout_, binding, element);
        const auto& publication = nativePayload_->completedTextures[index];
        if (publication.source)
            return validateCompletedTexture(
                *findBinding(layout_, binding), lastWrites_[index], publication);
        return validateTextureViewContract(*findBinding(layout_, binding), lastWrites_[index]);
    }

    rhi::DescriptorSetStats stats() const override
    {
        rhi::DescriptorSetStats stats;
        stats.bindingCount = static_cast<std::uint32_t>(layout_.bindings.size());
        stats.writtenBindings = isCurrentAllocation()
            ? static_cast<std::uint32_t>(std::count(written_.begin(), written_.end(), true))
            : 0u;
        stats.backendFallbacksHiddenFromUpperLayer = true;
        stats.nativeHandleExposedToUpperLayer = false;
        return stats;
    }

    core::LifetimeEpoch::Ticket allocationEpochTicket() const noexcept override
    {
        return arenaGeneration_;
    }

    std::optional<rhi::DescriptorBindingWrite> successfulBindingWrite(
        std::uint32_t binding, std::uint32_t element) const noexcept override
    {
        const auto index = rhi::descriptorElementIndex(layout_, binding, element);
        return isCurrentAllocation() && index != UINT32_MAX && index < written_.size()
                && written_[index]
            ? std::optional<rhi::DescriptorBindingWrite>(lastWrites_[index])
            : std::nullopt;
    }

private:
    static bool sameWrite(
        const rhi::DescriptorBindingWrite& lhs, const rhi::DescriptorBindingWrite& rhs) noexcept
    {
        return rhi::descriptorBindingWriteMatches(lhs, rhs);
    }

    rhi::ValidationResult validateTextureViewContract(
        const rhi::DescriptorBindingDesc& binding, const rhi::DescriptorBindingWrite& write) const
    {
        return validateRegisteredDescriptorView(resourceRegistry_,
            write.resource,
            write.resourceKind,
            binding.type,
            binding.textureViewDimensionConstrained,
            binding.textureViewDimension,
            binding.depthTexture);
    }

    rhi::ValidationResult validateCompletedTexture(const rhi::DescriptorBindingDesc& binding,
        const rhi::DescriptorBindingWrite& write,
        const VulkanCompletedTexturePublication& publication) const
    {
        const bool cell = bool(publication.cellIdentity);
        if (!publication.source
            || unsigned(bool(publication.content)) + unsigned(bool(publication.depthContent))
                    + unsigned(cell)
                != 1u)
            return {false, "RHI-COMPLETED-TEXTURE-DESCRIPTOR-PROVENANCE"};
        if (cell) {
            if (const auto* id =
                    rhi::OriginalTextureCellDescriptorContract::validateSource(publication.source,
                        *publication.cellIdentity,
                        publication.cellRequest.contractVersion))
                return {false, id};
            if (const auto* id = rhi::OriginalTextureCellDescriptorContract::validateBinding(
                    binding, write, publication.cellRequest.contractVersion))
                return {false, id};
        } else if (publication.depthContent) {
            if (const auto* id = rhi::OriginalCompletedTextureDescriptorContract::validateSource(
                    publication.depthContent, 4u))
                return {false, id};
            const auto& original = publication.depthContent->originalViewSource();
            if (original.get() != publication.source.get()
                || !core::SharedOwnerProjection::sameOwner(original, publication.source))
                return {false, "RHI-COMPLETED-TEXTURE-DESCRIPTOR-PROVENANCE"};
        } else if (const auto* id = rhi::OriginalCompletedTextureDescriptorContract::validateSource(
                       publication.source,
                       *publication.content,
                       publication.colorPublicationVersion))
            return {false, id};
        const auto runtime = nativeResources_.runtime();
        const auto view = vulkanRhiNativePayload(*publication.source->originalObject());
        const auto parent =
            vulkanRhiNativePayload(*publication.source->originalParentLease()->originalObject());
        const auto registered =
            resourceRegistry_ ? resourceRegistry_->payload(write.resource) : nullptr;
        if (!runtime || !runtime->valid() || runtime->syntheticRuntime()
            || (!cell && write.type != rhi::DescriptorType::SampledTexture)
            || binding.type != write.type
            || write.resourceKind != rhi::DescriptorResourceKind::TextureView
            || write.resourceState
                != (cell && write.type == rhi::DescriptorType::StorageTexture
                        ? rhi::ResourceState::ShaderWrite
                        : rhi::ResourceState::ShaderRead)
            || binding.depthTexture != bool(publication.depthContent)
            || write.resource != publication.source->resource() || !view || !parent
            || !core::SharedOwnerProjection::sameOwner(view, registered)
            || !core::SharedOwnerProjection::sameOwner(
                parent, view->backendVulkanTextureViewParentPayload())
            || !view->backendVulkanOriginalContentExclusive()
            || view->backendVulkanDeviceHandle() != runtime->logicalDevice()
            || parent->backendVulkanDeviceHandle() != runtime->logicalDevice()
            || !view->backendVulkanImageHandle()
            || view->backendVulkanImageHandle() != parent->backendVulkanImageHandle())
            return {false, "RHI-COMPLETED-TEXTURE-DESCRIPTOR-NATIVE-IDENTITY"};
        if (publication.colorPublicationVersion == 5u) {
            const auto format = publication.source->originalObject()->desc().format;
            if (format != rhi::TextureFormat::Rgba8Unorm
                && format != rhi::TextureFormat::Rgba16Float)
                return {false, "RHI-VULKAN-COMPLETED-HDR-FORMAT"};
            VkFormatFeatureFlags required =
                VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
            if (binding.sampledTextureFilterable)
                required |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
            if ((publication.colorFormatFeatures & required) != required)
                return {false, "RHI-VULKAN-COMPLETED-HDR-FORMAT-FEATURES"};
        }
        return validateRegisteredDescriptorViewShape(resourceRegistry_,
            write.resource,
            write.resourceKind,
            binding.type,
            binding.textureViewDimensionConstrained,
            binding.textureViewDimension,
            binding.depthTexture);
    }

    rhi::ValidationResult publishValidatedWrite(const rhi::DescriptorBindingWrite& write,
        std::uint32_t index,
        const VulkanCompletedTexturePublication& publication)
    {
        const auto nativeWrite = writeNativeDescriptor(write);
        if (!nativeWrite.success)
            return nativeWrite;
        nativePayload_->resources[index] = resourceRegistry_->payload(write.resource);
        auto& retained = nativePayload_->completedTextures[index];
        retained.source = publication.source;
        retained.depthContent = publication.depthContent;
        retained.content.reset();
        if (publication.content)
            retained.content.emplace(*publication.content);
        retained.cellIdentity.reset();
        if (publication.cellIdentity)
            retained.cellIdentity.emplace(*publication.cellIdentity);
        retained.cellRequest = publication.cellRequest;
        retained.colorPublicationVersion = publication.colorPublicationVersion;
        retained.colorFormatFeatures = publication.colorFormatFeatures;
        written_[index] = true;
        lastWrites_[index] = write;
        return {true, {}};
    }

    rhi::ValidationResult writeNativeDescriptor(const rhi::DescriptorBindingWrite& write)
    {
        if (!nativePayload_->hasNativeSet() || !nativeResources_.connected()) {
            return {true, ""};
        }
        const auto device = nativeResources_.runtime();
        if (device && device->syntheticRuntime()) {
            if (auto stats = nativeResources_.sharedStats()) {
                ++stats->nativeDescriptorWriteCount;
            }
            return {true, ""};
        }
        const auto descriptorSet = nativePayload_->backendVulkanDescriptorSetHandle();
        const auto resource =
            resourceRegistry_ ? resourceRegistry_->payload(write.resource) : nullptr;
        if (!device || !descriptorSet || !resource) {
            return {false, "Vulkan RHI native descriptor write requires tracked native payloads"};
        }
        const auto* binding = findBinding(layout_, write.binding);
        if (!binding) {
            return {false, "Vulkan RHI native descriptor binding does not exist"};
        }
        const auto bindingIndex =
            rhi::descriptorElementIndex(layout_, write.binding, write.arrayElement);
        auto* updateDescriptorSets = functionFrom<PFN_vkUpdateDescriptorSets>(
            device->getDeviceProcAddr("vkUpdateDescriptorSets"));
        if (!updateDescriptorSets) {
            return {false, "Vulkan RHI native descriptor write missing vkUpdateDescriptorSets"};
        }

        VkDescriptorBufferInfo bufferInfo{};
        VkDescriptorImageInfo imageInfo{};
        VkWriteDescriptorSet nativeWrite{};
        nativeWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        nativeWrite.dstSet = descriptorSet;
        nativeWrite.dstBinding = write.binding;
        nativeWrite.dstArrayElement = write.arrayElement;
        nativeWrite.descriptorCount = 1;
        nativeWrite.descriptorType = descriptorTypeFor(write.type, binding->dynamicOffset);
        if (write.resourceKind == rhi::DescriptorResourceKind::Buffer) {
            const auto buffer = resource->backendVulkanBufferHandle();
            if (!buffer) {
                return {false, "Vulkan RHI native descriptor write requires a buffer payload"};
            }
            bufferInfo.buffer = buffer;
            bufferInfo.offset = write.bufferBindingRange.contractVersion == 2u
                ? write.bufferBindingRange.offsetBytes
                : 0u;
            bufferInfo.range = write.bufferBindingRange.contractVersion == 2u
                ? write.bufferBindingRange.sizeBytes
                : VK_WHOLE_SIZE;
            nativeWrite.pBufferInfo = &bufferInfo;
            nativePayload_->bufferRanges[bindingIndex] = {bufferInfo.offset, bufferInfo.range};
            if (std::getenv("ARP_FORWARD_PLUS_DESCRIPTOR_TRACE") != nullptr && write.binding >= 6u
                && write.binding <= 10u) {
                std::cerr << "[vulkan-descriptor-trace] set=" << descriptorSet
                          << " layout=" << nativePayload_->backendVulkanDescriptorSetLayoutHandle()
                          << " binding=" << write.binding << " resource=" << write.resource.value
                          << " vkBuffer=" << buffer << '\n';
            }
        } else if (write.resourceKind == rhi::DescriptorResourceKind::TextureView) {
            const auto image = resource->backendVulkanImageHandle();
            if (!image) {
                return {false, "Vulkan RHI native descriptor write requires an image payload"};
            }
            if (!resource->backendVulkanTextureViewDescAvailable()) {
                return {
                    false, "Vulkan RHI native descriptor write requires a texture view payload"};
            }
            const auto* originalView = resource->backendVulkanOriginalTextureViewDesc();
            if (!originalView)
                return {false,
                    "Vulkan RHI native descriptor write requires original texture view metadata"};
            const auto& viewDesc = *originalView;
            auto* createImageView =
                functionFrom<PFN_vkCreateImageView>(device->getDeviceProcAddr("vkCreateImageView"));
            auto* destroyImageView = functionFrom<PFN_vkDestroyImageView>(
                device->getDeviceProcAddr("vkDestroyImageView"));
            if (!createImageView || !destroyImageView) {
                return {false,
                    "Vulkan RHI native descriptor write requires image view create/destroy entry "
                    "points"};
            }
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = image;
            viewInfo.viewType = imageViewTypeFor(viewDesc.dimension);
            viewInfo.format = resource->backendVulkanImageFormat();
            if (viewInfo.format == VK_FORMAT_UNDEFINED) {
                return {false, "Vulkan RHI native descriptor write requires an image format"};
            }
            viewInfo.subresourceRange.aspectMask = imageAspectFor(viewDesc.aspect);
            viewInfo.subresourceRange.baseMipLevel = viewDesc.baseMipLevel;
            viewInfo.subresourceRange.levelCount = viewDesc.mipLevelCount;
            viewInfo.subresourceRange.baseArrayLayer = viewDesc.baseArrayLayer;
            viewInfo.subresourceRange.layerCount = viewDesc.arrayLayerCount;
            VkImageView imageView = VK_NULL_HANDLE;
            const auto created =
                createImageView(device->logicalDevice(), &viewInfo, nullptr, &imageView);
            if (created != VK_SUCCESS) {
                return {false, "Vulkan RHI native descriptor image view creation failed"};
            }
            const auto previousImageView = nativePayload_->imageViews[bindingIndex];
            imageInfo.imageView = imageView;
            if (write.type == rhi::DescriptorType::StorageTexture
                || write.resourceState == rhi::ResourceState::ShaderWrite) {
                imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            } else if (write.type == rhi::DescriptorType::InputAttachment) {
                imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            } else {
                imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }
            nativeWrite.pImageInfo = &imageInfo;

            updateDescriptorSets(device->logicalDevice(), 1, &nativeWrite, 0, nullptr);
            nativePayload_->imageViews[bindingIndex] = imageView;
            // The exclusive-storage gate proves that no recorded work can
            // reference the replaced view. Keep one view per element, bounded
            // by the layout, rather than accumulating every historical write.
            if (previousImageView != VK_NULL_HANDLE) {
                destroyImageView(device->logicalDevice(), previousImageView, nullptr);
            }
            if (auto stats = nativeResources_.sharedStats()) {
                ++stats->nativeDescriptorWriteCount;
            }
            return {true, ""};
        } else if (write.resourceKind == rhi::DescriptorResourceKind::Sampler) {
            const auto sampler = resource->backendVulkanSamplerHandle();
            if (!sampler) {
                return {false, "Vulkan RHI native descriptor write requires a sampler payload"};
            }
            imageInfo.sampler = sampler;
            nativeWrite.pImageInfo = &imageInfo;
        } else {
            return {false, "Vulkan RHI native descriptor resource kind is unsupported"};
        }

        updateDescriptorSets(device->logicalDevice(), 1, &nativeWrite, 0, nullptr);
        if (auto stats = nativeResources_.sharedStats()) {
            ++stats->nativeDescriptorWriteCount;
        }
        return {true, ""};
    }

    rhi::DescriptorSetAllocation allocation_;
    rhi::DescriptorLayoutDesc layout_;
    core::LifetimeEpoch::Ticket arenaGeneration_;
    std::shared_ptr<VulkanDescriptorStorage> nativePayload_;
    VulkanRhiNativeResourceAllocator nativeResources_;
    std::shared_ptr<VulkanRhiResourceRegistry> resourceRegistry_;
    core::pmr::Vector<bool> written_;
    core::pmr::Vector<rhi::DescriptorBindingWrite> lastWrites_;
};

class VulkanRhiDescriptorTable final : public rhi::DescriptorTable {
public:
    VulkanRhiDescriptorTable(rhi::DescriptorTableAllocation allocation,
        rhi::DescriptorTableDesc desc,
        const core::LifetimeEpoch& arenaGeneration,
        std::shared_ptr<VulkanRhiResourceRegistry> resourceRegistry,
        std::shared_ptr<rhi::semantics::DirectObjectBudgetLease> generationBudget)
        : allocation_(std::move(allocation)), desc_(std::move(desc)),
          arenaGeneration_(arenaGeneration.ticket()),
          written_(desc_.capacity, false, &core::defaultCpuMemoryResource()),
          resourceRegistry_(std::move(resourceRegistry)),
          writes_(desc_.capacity, &core::defaultCpuMemoryResource()),
          generationBudget_(std::move(generationBudget))
    {}

    const rhi::DescriptorTableAllocation& allocation() const override { return allocation_; }

    const rhi::DescriptorTableDesc& desc() const override { return desc_; }

    rhi::ValidationResult writeImpl(const rhi::DescriptorTableWrite& write) override
    {
        if (!isCurrentAllocation()) {
            return {false,
                "RHI-DESCRIPTOR-ALLOCATION-STALE: Vulkan RHI descriptor table allocation is stale"};
        }
        if (write.index >= desc_.capacity) {
            return {false, "Vulkan RHI descriptor table index is out of range"};
        }
        if (write.type != desc_.type) {
            return {false, "Vulkan RHI descriptor table type mismatch"};
        }
        if (!write.resource.isValid()) {
            return {false, "Vulkan RHI descriptor table write requires a resource id"};
        }
        if (!resourceRegistry_ || !resourceRegistry_->payload(write.resource)) {
            return {false, "RHI-DESCRIPTOR-RESOURCE-NOT-LIVE: Vulkan table resource is unknown"};
        }
        const auto parent = validateView(write.resource, write.resourceKind);
        if (!parent.success)
            return parent;
        writes_[write.index] = write;
        written_[write.index] = true;
        return {true, ""};
    }

    bool isCurrentAllocation() const override
    {
        return allocation_.success && arenaGeneration_.isCurrent();
    }

    bool hasWrittenDescriptor(std::uint32_t index) const override
    {
        return isCurrentAllocation() && index < written_.size() && written_[index];
    }

    rhi::ValidationResult validateDescriptor(std::uint32_t index) const override
    {
        if (!isCurrentAllocation()) {
            return {false,
                "RHI-DESCRIPTOR-ALLOCATION-STALE: Vulkan RHI descriptor table allocation is stale"};
        }
        if (!hasWrittenDescriptor(index)) {
            return {false, "Vulkan RHI descriptor table slot is not written"};
        }
        if (!resourceRegistry_ || !resourceRegistry_->payload(writes_[index].resource)) {
            return {false, "RHI-DESCRIPTOR-RESOURCE-NOT-LIVE: Vulkan table resource is unknown"};
        }
        return validateView(writes_[index].resource, writes_[index].resourceKind);
    }

    rhi::DescriptorTableStats stats() const override
    {
        rhi::DescriptorTableStats stats;
        stats.capacity = desc_.capacity;
        stats.writtenDescriptors = isCurrentAllocation()
            ? static_cast<std::uint32_t>(std::count(written_.begin(), written_.end(), true))
            : 0u;
        stats.backendFallbacksHiddenFromUpperLayer = true;
        stats.nativeHandleExposedToUpperLayer = false;
        return stats;
    }

    core::LifetimeEpoch::Ticket allocationEpochTicket() const noexcept override
    {
        return arenaGeneration_;
    }

    std::optional<rhi::DescriptorTableWrite> successfulDescriptorWrite(
        std::uint32_t index) const noexcept override
    {
        return hasWrittenDescriptor(index)
            ? std::optional<rhi::DescriptorTableWrite>(writes_[index])
            : std::nullopt;
    }

private:
    rhi::DescriptorTableAllocation allocation_;

    rhi::ValidationResult validateView(
        rhi::GpuResourceId id, rhi::DescriptorResourceKind kind) const
    {
        return validateRegisteredDescriptorView(resourceRegistry_,
            id,
            kind,
            desc_.type,
            desc_.textureViewDimensionConstrained,
            desc_.textureViewDimension,
            desc_.depthTexture);
    }

    rhi::DescriptorTableDesc desc_;
    core::LifetimeEpoch::Ticket arenaGeneration_;
    core::pmr::Vector<bool> written_;
    std::shared_ptr<VulkanRhiResourceRegistry> resourceRegistry_;
    core::pmr::Vector<rhi::DescriptorTableWrite> writes_;
    std::shared_ptr<rhi::semantics::DirectObjectBudgetLease> generationBudget_;
};

class VulkanRhiDescriptorArena final : public rhi::DescriptorArena {
public:
    const char* validateOriginalLossSource(const std::shared_ptr<VulkanRhiRuntime>& runtime,
        const std::shared_ptr<VulkanRhiResourceRegistry>& registry) const noexcept
    {
        return runtime && runtime->valid() && !runtime->syntheticRuntime()
                && core::SharedOwnerProjection::sameOwner(runtime, nativeResources_.runtime())
                && core::SharedOwnerProjection::sameOwner(registry, resourceRegistry_)
                && requireNativeDescriptorAllocation_ && generation_.isActive()
            ? nullptr
            : "RHI-VULKAN-DESCRIPTOR-LOSS-ARENA-SOURCE";
    }

    VulkanRhiDescriptorArena(const rhi::DescriptorArenaDesc& desc,
        VulkanRhiNativeResourceAllocator nativeResources,
        std::shared_ptr<VulkanRhiResourceRegistry> resourceRegistry,
        bool requireNativeDescriptorAllocation,
        std::shared_ptr<rhi::semantics::DirectObjectBudgetTracker> directObjectBudget,
        std::shared_ptr<rhi::semantics::DirectObjectBudgetLease> generationBudget)
        : nativeResources_(std::move(nativeResources)),
          resourceRegistry_(std::move(resourceRegistry)),
          requireNativeDescriptorAllocation_(requireNativeDescriptorAllocation),
          directObjectBudget_(std::move(directObjectBudget)),
          generationBudget_(std::move(generationBudget))
    {
        stats_.maxSets = desc.maxSets;
        stats_.maxDescriptors = desc.maxDescriptors;
    }

    rhi::DescriptorSetAllocation allocateSet(const rhi::DescriptorLayoutDesc& layout) override
    {
        if (directObjectBudget_ && !generationBudget_)
            return {false, "RHI-DIRECT-DESCRIPTOR-GENERATION-BUDGET", 0u, 0u};
        if (!generation_.isActive()) {
            return {false, "RHI-DESCRIPTOR-GENERATION-EXHAUSTED", 0u, 0u};
        }
        rhi::DescriptorSetAllocation allocation;
        const auto validation = rhi::validateDescriptorLayout(layout);
        if (!validation.success) {
            allocation.diagnostics = validation.diagnostics;
            return allocation;
        }
        std::uint32_t descriptorCount = 0u;
        const auto count = rhi::countDescriptorElements(layout, descriptorCount);
        if (!count.success)
            return {false, count.diagnostics, 0u, 0u};
        if (stats_.allocatedSets >= stats_.maxSets
            || stats_.allocatedDescriptors > stats_.maxDescriptors
            || descriptorCount > stats_.maxDescriptors - stats_.allocatedDescriptors) {
            allocation.diagnostics = "Vulkan RHI descriptor arena capacity exceeded";
            return allocation;
        }
        allocation.success = true;
        allocation.setIndex = stats_.allocatedSets++;
        allocation.descriptorCapacity = descriptorCount;
        stats_.allocatedDescriptors += descriptorCount;
        return allocation;
    }

    arp::core::UniquePtr<rhi::DescriptorSet> allocateDescriptorSet(
        const rhi::DescriptorSetDesc& desc) override
    {
        auto copied = arp::rhi::DescriptorStateCopy::copy(desc);
        return core::withMutationRollback(
            [&]() -> core::UniquePtr<rhi::DescriptorSet> {
                auto allocation = allocateSet(copied.layout);
                if (!allocation.success) {
                    std::cerr << "[wocc] allocateSet FAIL: " << allocation.diagnostics.c_str()
                              << "\n";
                    return nullptr;
                }
                auto nativeDescriptor = nativeResources_.createDescriptorSet(copied.layout);
                if (requireNativeDescriptorAllocation_
                    && (!nativeDescriptor.success || !nativeDescriptor.payload)) {
                    std::cerr << "[wocc] native createDescriptorSet FAIL: "
                              << nativeDescriptor.diagnostics.c_str() << "\n";
                    return nullptr;
                }
                return arp::core::makeUnique<VulkanRhiDescriptorSet>(std::move(allocation),
                    std::move(copied.layout),
                    generation_,
                    nativeDescriptor.payload,
                    nativeResources_,
                    resourceRegistry_,
                    generationBudget_);
            },
            stats_);
    }

    rhi::DescriptorTableAllocation allocateTable(const rhi::DescriptorTableDesc& desc) override
    {
        if (directObjectBudget_ && !generationBudget_)
            return {false, "RHI-DIRECT-DESCRIPTOR-GENERATION-BUDGET", 0u, 0u};
        if (!generation_.isActive()) {
            return {false, "RHI-DESCRIPTOR-GENERATION-EXHAUSTED", 0u, 0u};
        }
        rhi::DescriptorTableAllocation allocation;
        if (desc.label.empty() || desc.capacity == 0 || desc.stages == rhi::ShaderStage::None) {
            allocation.diagnostics = "Vulkan RHI descriptor table desc is invalid";
            return allocation;
        }
        const auto shape = rhi::validateDescriptorTextureRequirement(
            desc.type, desc.textureViewDimensionConstrained, desc.depthTexture);
        if (!shape.success)
            return {false, shape.diagnostics, 0u, 0u};
        if (stats_.allocatedTables >= stats_.maxSets
            || stats_.allocatedDescriptors > stats_.maxDescriptors
            || desc.capacity > stats_.maxDescriptors - stats_.allocatedDescriptors) {
            allocation.diagnostics = "Vulkan RHI descriptor arena table capacity exceeded";
            return allocation;
        }
        allocation.success = true;
        allocation.tableIndex = stats_.allocatedTables++;
        allocation.descriptorCapacity = desc.capacity;
        stats_.allocatedDescriptors += desc.capacity;
        return allocation;
    }

    arp::core::UniquePtr<rhi::DescriptorTable> allocateDescriptorTable(
        const rhi::DescriptorTableDesc& desc) override
    {
        auto copied = arp::rhi::DescriptorStateCopy::copy(desc);
        return core::withMutationRollback(
            [&]() -> core::UniquePtr<rhi::DescriptorTable> {
                auto allocation = allocateTable(copied);
                if (!allocation.success) {
                    return nullptr;
                }
                return arp::core::makeUnique<VulkanRhiDescriptorTable>(std::move(allocation),
                    std::move(copied),
                    generation_,
                    resourceRegistry_,
                    generationBudget_);
            },
            stats_);
    }

    rhi::DescriptorArenaStats stats() const override { return stats_; }

    void reset() override
    {
        generation_.advance();
        stats_.allocatedSets = 0;
        stats_.allocatedTables = 0;
        stats_.allocatedDescriptors = 0;
        generationBudget_.reset();
        if (directObjectBudget_ && generation_.isActive()) {
            auto reservation = directObjectBudget_->reserve(
                rhi::semantics::DirectObjectBudgetKind::DescriptorArena, stats_.maxDescriptors);
            if (reservation.success)
                generationBudget_ = core::makeShared<rhi::semantics::DirectObjectBudgetLease>(
                    std::move(reservation.lease));
            else
                rhi::RhiErrorHandler::get().report(
                    rhi::RhiSeverity::Error, "RHI-DIRECT-DESCRIPTOR-GENERATION-BUDGET");
        }
    }

    core::LifetimeEpoch::Ticket allocationEpochTicket() const noexcept override
    {
        return generation_.ticket();
    }

private:
    rhi::DescriptorArenaStats stats_;
    core::LifetimeEpoch generation_;
    VulkanRhiNativeResourceAllocator nativeResources_;
    std::shared_ptr<VulkanRhiResourceRegistry> resourceRegistry_;
    bool requireNativeDescriptorAllocation_ = false;
    std::shared_ptr<rhi::semantics::DirectObjectBudgetTracker> directObjectBudget_;
    std::shared_ptr<rhi::semantics::DirectObjectBudgetLease> generationBudget_;
};

} // namespace

arp::core::UniquePtr<rhi::DescriptorArena> createVulkanRhiDescriptorArena(
    const rhi::DescriptorArenaDesc& desc,
    VulkanRhiNativeResourceAllocator nativeResources,
    std::shared_ptr<VulkanRhiResourceRegistry> resourceRegistry,
    bool requireNativeDescriptorAllocation,
    std::shared_ptr<rhi::semantics::DirectObjectBudgetTracker> directObjectBudget)
{
    if (desc.maxSets == 0 || desc.maxDescriptors == 0) {
        return nullptr;
    }
    std::shared_ptr<rhi::semantics::DirectObjectBudgetLease> generationBudget;
    if (directObjectBudget) {
        auto reservation = directObjectBudget->reserve(
            rhi::semantics::DirectObjectBudgetKind::DescriptorArena, desc.maxDescriptors);
        if (!reservation.success) {
            rhi::RhiErrorHandler::get().report(
                rhi::RhiSeverity::Error, "RHI-DIRECT-DESCRIPTOR-BUDGET");
            return nullptr;
        }
        generationBudget =
            core::makeShared<rhi::semantics::DirectObjectBudgetLease>(std::move(reservation.lease));
    }
    return arp::core::makeUnique<VulkanRhiDescriptorArena>(
        arp::rhi::DescriptorStateCopy::copy(desc),
        std::move(nativeResources),
        std::move(resourceRegistry),
        requireNativeDescriptorAllocation,
        std::move(directObjectBudget),
        std::move(generationBudget));
}

std::shared_ptr<VulkanRhiNativeResourcePayload> prepareVulkanOriginalImageCellDescriptors(
    const rhi::BindlessSnapshot& snapshot)
{
    const auto* set = dynamic_cast<const VulkanRhiDescriptorSet*>(snapshot.descriptorSet());
    return set ? set->imageCellPayload(snapshot) : nullptr;
}

std::shared_ptr<VulkanRhiNativeResourcePayload> vulkanRhiNativePayload(
    const rhi::DescriptorSet& descriptorSet)
{
    const auto* vulkanDescriptorSet = dynamic_cast<const VulkanRhiDescriptorSet*>(&descriptorSet);
    return vulkanDescriptorSet ? vulkanDescriptorSet->nativePayload() : nullptr;
}

const char* validateVulkanOriginalDescriptorLossSource(const rhi::DescriptorArena& arena,
    const std::shared_ptr<VulkanRhiRuntime>& runtime,
    const std::shared_ptr<VulkanRhiResourceRegistry>& registry) noexcept
{
    const auto* original = dynamic_cast<const VulkanRhiDescriptorArena*>(&arena);
    return original ? original->validateOriginalLossSource(runtime, registry)
                    : "RHI-VULKAN-DESCRIPTOR-LOSS-ARENA-SOURCE";
}

bool vulkanRhiDescriptorSetContextMatches(const rhi::DescriptorSet& set,
    const std::shared_ptr<VulkanRhiRuntime>& runtime,
    const std::shared_ptr<VulkanRhiResourceRegistry>& registry) noexcept
{
    const auto* original = dynamic_cast<const VulkanRhiDescriptorSet*>(&set);
    return original && original->contextMatches(runtime, registry);
}

const char* validateVulkanOriginalDescriptorLossSource(const rhi::DescriptorSet& set,
    const std::shared_ptr<VulkanRhiRuntime>& runtime,
    const std::shared_ptr<VulkanRhiResourceRegistry>& registry) noexcept
{
    const auto* original = dynamic_cast<const VulkanRhiDescriptorSet*>(&set);
    return original ? original->validateOriginalLossSource(runtime, registry)
                    : "RHI-VULKAN-DESCRIPTOR-LOSS-SET-SOURCE";
}

} // namespace arp::rhi_vulkan

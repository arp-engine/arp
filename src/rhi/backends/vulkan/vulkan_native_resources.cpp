#include "rhi/backends/vulkan/vulkan_native_resources.h"

#include "core/cpu_allocator.h"
#include "rhi/backends/vulkan/vulkan_allocator.h"
#include "rhi/backends/vulkan/vulkan_runtime.h"
#include "rhi/backends/vulkan/vulkan_texture_descriptor.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
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

VkBufferUsageFlags bufferUsageFor(rhi::ResourceUsage usage)
{
    VkBufferUsageFlags flags = 0;
    if ((usage & rhi::ResourceUsage::VertexBuffer) != rhi::ResourceUsage::None) {
        flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    }
    if ((usage & rhi::ResourceUsage::IndexBuffer) != rhi::ResourceUsage::None) {
        flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    }
    if ((usage & rhi::ResourceUsage::UniformBuffer) != rhi::ResourceUsage::None) {
        flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    }
    if ((usage & rhi::ResourceUsage::StorageBuffer) != rhi::ResourceUsage::None) {
        flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    }
    if ((usage & rhi::ResourceUsage::CopySrc) != rhi::ResourceUsage::None) {
        flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    }
    if ((usage & rhi::ResourceUsage::CopyDst) != rhi::ResourceUsage::None) {
        flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }
    if ((usage & rhi::ResourceUsage::IndirectBuffer) != rhi::ResourceUsage::None) {
        flags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    }
    return flags;
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

VkFilter filterFor(rhi::SamplerFilter filter)
{
    switch (filter) {
    case rhi::SamplerFilter::Nearest:
        return VK_FILTER_NEAREST;
    case rhi::SamplerFilter::Linear:
        return VK_FILTER_LINEAR;
    }
    return VK_FILTER_LINEAR;
}

VkSamplerAddressMode addressModeFor(rhi::SamplerAddressMode mode)
{
    switch (mode) {
    case rhi::SamplerAddressMode::ClampToEdge:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case rhi::SamplerAddressMode::Repeat:
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
}

VkCompareOp compareOpFor(rhi::CompareOp compare)
{
    switch (compare) {
    case rhi::CompareOp::Never:
        return VK_COMPARE_OP_NEVER;
    case rhi::CompareOp::Less:
        return VK_COMPARE_OP_LESS;
    case rhi::CompareOp::LessEqual:
        return VK_COMPARE_OP_LESS_OR_EQUAL;
    case rhi::CompareOp::Equal:
        return VK_COMPARE_OP_EQUAL;
    case rhi::CompareOp::Greater:
        return VK_COMPARE_OP_GREATER;
    case rhi::CompareOp::GreaterEqual:
        return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case rhi::CompareOp::NotEqual:
        return VK_COMPARE_OP_NOT_EQUAL;
    case rhi::CompareOp::Always:
        return VK_COMPARE_OP_ALWAYS;
    }
    // Defensive default for an out-of-range enum value. Unified across backends
    // to Never (most conservative: reject all comparisons) to avoid divergent
    // fallback semantics — previously Vulkan/WebGPU returned Always here while
    // Metal returned Never, an inconsistent silent-fallback that violates the
    // "no guesswork fallback" constraint.
    return VK_COMPARE_OP_NEVER;
}

VkShaderStageFlags shaderStagesFor(rhi::ShaderStage stages)
{
    VkShaderStageFlags flags = 0;
    if ((stages & rhi::ShaderStage::Vertex) != rhi::ShaderStage::None) {
        flags |= VK_SHADER_STAGE_VERTEX_BIT;
    }
    if ((stages & rhi::ShaderStage::Fragment) != rhi::ShaderStage::None) {
        flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    if ((stages & rhi::ShaderStage::Compute) != rhi::ShaderStage::None) {
        flags |= VK_SHADER_STAGE_COMPUTE_BIT;
    }
    return flags;
}

class NativeBufferPayload final : public VulkanRhiNativeResourcePayload {
public:
    NativeBufferPayload(VkDevice device,
        VkBuffer buffer,
        void* mappedData,
        std::shared_ptr<void> allocationPayload,
        std::shared_ptr<VulkanRhiMappedBufferWriteFlusher> writeFlusher,
        std::shared_ptr<VulkanRhiMappedBufferReadback> readback)
        : device_(device), buffer_(buffer), mappedData_(mappedData),
          allocationPayload_(std::move(allocationPayload)), writeFlusher_(std::move(writeFlusher)),
          readback_(std::move(readback))
    {}

    VkDevice backendVulkanDeviceHandle() const override { return device_; }

    VkBuffer backendVulkanBufferHandle() const override { return buffer_; }

    void* backendMappedData() const override { return mappedData_; }

    bool backendFlushMappedWrite(const std::uint64_t offsetBytes,
        const std::uint64_t sizeBytes,
        std::pmr::string& diagnostics) const override
    {
        if (!writeFlusher_) {
            diagnostics = "Vulkan RHI native mapped buffer write requires a flush contract";
            return false;
        }
        return writeFlusher_->flushMappedWrite(offsetBytes, sizeBytes, diagnostics);
    }

    bool backendInvalidateMappedRead(const std::uint64_t offsetBytes,
        const std::uint64_t sizeBytes,
        std::pmr::string& diagnostics) override
    {
        if (!readback_) {
            diagnostics = "Vulkan RHI native mapped buffer read requires an invalidation contract";
            return false;
        }
        return readback_->invalidateMappedRead(offsetBytes, sizeBytes, diagnostics);
    }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    void* mappedData_ = nullptr;
    std::shared_ptr<void> allocationPayload_;
    std::shared_ptr<VulkanRhiMappedBufferWriteFlusher> writeFlusher_;
    std::shared_ptr<VulkanRhiMappedBufferReadback> readback_;
};

class NativeImagePayload final : public VulkanRhiNativeResourcePayload {
public:
    NativeImagePayload(VkDevice device,
        VkImage image,
        VkFormat format,
        rhi::Extent3D extent,
        std::shared_ptr<void> allocationPayload,
        bool originalContentExclusive)
        : device_(device), image_(image), format_(format), extent_(extent),
          allocationPayload_(std::move(allocationPayload)),
          originalContentExclusive_(originalContentExclusive)
    {}

    VkDevice backendVulkanDeviceHandle() const override { return device_; }

    VkImage backendVulkanImageHandle() const override { return image_; }

    bool backendVulkanOriginalContentExclusive() const noexcept override
    {
        return originalContentExclusive_;
    }

    VkFormat backendVulkanImageFormat() const override { return format_; }

    rhi::Extent3D backendVulkanImageExtent() const override { return extent_; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkImage image_ = VK_NULL_HANDLE;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    rhi::Extent3D extent_;
    std::shared_ptr<void> allocationPayload_;
    const bool originalContentExclusive_;
};

class NativeDescriptorSetPayload final : public VulkanRhiNativeResourcePayload {
public:
    NativeDescriptorSetPayload(std::shared_ptr<VulkanRhiRuntime> device,
        VkDescriptorSetLayout layout,
        VkDescriptorPool pool,
        VkDescriptorSet set)
        : device_(std::move(device)), layout_(layout), pool_(pool), set_(set)
    {}

    ~NativeDescriptorSetPayload() override
    {
        auto* destroyDescriptorPool = functionFrom<PFN_vkDestroyDescriptorPool>(
            device_->getDeviceProcAddr("vkDestroyDescriptorPool"));
        auto* destroyDescriptorSetLayout = functionFrom<PFN_vkDestroyDescriptorSetLayout>(
            device_->getDeviceProcAddr("vkDestroyDescriptorSetLayout"));
        if (destroyDescriptorPool && pool_) {
            destroyDescriptorPool(device_->logicalDevice(), pool_, nullptr);
        }
        if (destroyDescriptorSetLayout && layout_) {
            destroyDescriptorSetLayout(device_->logicalDevice(), layout_, nullptr);
        }
    }

    VkDevice backendVulkanDeviceHandle() const override { return device_->logicalDevice(); }

    VkDescriptorSet backendVulkanDescriptorSetHandle() const override { return set_; }

    VkDescriptorSetLayout backendVulkanDescriptorSetLayoutHandle() const override
    {
        return layout_;
    }

private:
    std::shared_ptr<VulkanRhiRuntime> device_;
    VkDescriptorSetLayout layout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkDescriptorSet set_ = VK_NULL_HANDLE;
};

class NativeSamplerPayload final : public VulkanRhiNativeResourcePayload {
public:
    NativeSamplerPayload(std::shared_ptr<VulkanRhiRuntime> device, VkSampler sampler)
        : device_(std::move(device)), sampler_(sampler)
    {}

    ~NativeSamplerPayload() override
    {
        auto* destroySampler =
            functionFrom<PFN_vkDestroySampler>(device_->getDeviceProcAddr("vkDestroySampler"));
        if (destroySampler && sampler_) {
            destroySampler(device_->logicalDevice(), sampler_, nullptr);
        }
    }

    VkDevice backendVulkanDeviceHandle() const override { return device_->logicalDevice(); }

    VkSampler backendVulkanSamplerHandle() const override { return sampler_; }

private:
    std::shared_ptr<VulkanRhiRuntime> device_;
    VkSampler sampler_ = VK_NULL_HANDLE;
};

std::pmr::string vkFailure(const char* operation, VkResult result)
{
    std::ostringstream message;
    message << operation << " failed: " << static_cast<int>(result);
    return arp::core::toPmrString(message.str());
}

} // namespace

VulkanRhiNativeResourceAllocator::VulkanRhiNativeResourceAllocator(
    std::shared_ptr<VulkanRhiRuntime> runtime, VulkanRhiAllocator* allocator)
    : runtime_(std::move(runtime)), allocator_(allocator),
      stats_(arp::core::makeShared<VulkanRhiNativeResourceStats>())
{
    stats_->runtimeConnected = runtime_ != nullptr && runtime_->valid();
    stats_->nativeHandlesHiddenFromPublicApi = true;
}

bool VulkanRhiNativeResourceAllocator::connected() const
{
    return stats_->runtimeConnected;
}

VulkanRhiNativeResourceStats VulkanRhiNativeResourceAllocator::stats() const
{
    return *stats_;
}

std::shared_ptr<VulkanRhiRuntime> VulkanRhiNativeResourceAllocator::runtime() const
{
    return runtime_;
}

VulkanRhiAllocator* VulkanRhiNativeResourceAllocator::allocator() const
{
    return allocator_;
}

std::shared_ptr<VulkanRhiNativeResourceStats> VulkanRhiNativeResourceAllocator::sharedStats() const
{
    return stats_;
}

VulkanRhiNativeResourceResult VulkanRhiNativeResourceAllocator::createBuffer(
    const rhi::BufferDesc& desc)
{
    VulkanRhiNativeResourceResult result;
    if (!connected()) {
        result.diagnostics = "Vulkan RHI native buffer allocation requires a native device";
        return result;
    }
    if (desc.sizeBytes == 0 || bufferUsageFor(desc.usage) == 0) {
        result.diagnostics = "Vulkan RHI native buffer allocation desc is invalid";
        return result;
    }
    if (!allocator_) {
        result.diagnostics = "Vulkan RHI native buffer allocation requires a VMA allocator";
        return result;
    }
    if (runtime_->syntheticRuntime()) {
        result.allocation = allocator_->allocate({desc.label, desc.sizeBytes, desc.memory});
        if (!result.allocation.success) {
            result.diagnostics = result.allocation.diagnostics;
            return result;
        }
        ++stats_->nativeBufferCreateCount;
        ++stats_->nativeMemoryAllocationCount;
        result.payload = arp::core::makeShared<VulkanRhiNativeResourcePayload>();
        result.success = true;
        return result;
    }

    const auto allocated = allocator_->createBuffer(desc, bufferUsageFor(desc.usage));
    if (!allocated.success) {
        result.diagnostics = allocated.diagnostics;
        return result;
    }

    ++stats_->nativeBufferCreateCount;
    ++stats_->nativeMemoryAllocationCount;
    result.allocation = allocated.allocation;
    result.payload = arp::core::makeShared<NativeBufferPayload>(runtime_->logicalDevice(),
        allocated.buffer,
        allocated.mappedData,
        allocated.payload,
        allocated.writeFlusher,
        allocated.readback);
    result.readback = allocated.readback;
    result.success = true;
    return result;
}

std::uint64_t VulkanRhiNativeResourceAllocator::queryBufferAllocationBytes(
    const rhi::BufferDesc& desc, const char*& diagnostic) const
{
    diagnostic = "RHI-VULKAN-GEOMETRY-CONTENT-AUTHENTIC-RUNTIME";
    if (!runtime_ || !runtime_->valid() || runtime_->syntheticRuntime())
        return 0u;
    diagnostic = "RHI-VULKAN-GEOMETRY-CONTENT-ALLOCATION-QUERY";
    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = desc.sizeBytes;
    info.usage = bufferUsageFor(desc.usage);
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (!info.size || !info.usage)
        return 0u;
    VkBuffer buffer = VK_NULL_HANDLE;
    const auto created = vkCreateBuffer(runtime_->logicalDevice(), &info, nullptr, &buffer);
    if (created != VK_SUCCESS) {
        runtime_->captureOriginalDeviceLoss(created);
        return 0u;
    }
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(runtime_->logicalDevice(), buffer, &requirements);
    vkDestroyBuffer(runtime_->logicalDevice(), buffer, nullptr);
    if (!requirements.size)
        return 0u;
    diagnostic = nullptr;
    return requirements.size;
}

std::uint64_t VulkanRhiNativeResourceAllocator::queryTextureAllocationBytes(
    const rhi::TextureDesc& desc, const char*& diagnostic) const
{
    diagnostic = "RHI-VULKAN-CONTENT-CREATION-AUTHENTIC-RUNTIME";
    if (!runtime_ || !runtime_->valid() || runtime_->syntheticRuntime())
        return 0u;
    const auto info = VulkanTextureDescriptor::imageInfo(desc, 0u, VK_SAMPLE_COUNT_1_BIT);
    diagnostic = "RHI-VULKAN-CONTENT-CREATION-ALLOCATION-QUERY";
    if (info.format == VK_FORMAT_UNDEFINED || !info.usage)
        return 0u;
    VkImage image = VK_NULL_HANDLE;
    const auto created = vkCreateImage(runtime_->logicalDevice(), &info, nullptr, &image);
    if (created != VK_SUCCESS) {
        runtime_->captureOriginalDeviceLoss(created);
        return 0u;
    }
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(runtime_->logicalDevice(), image, &requirements);
    vkDestroyImage(runtime_->logicalDevice(), image, nullptr);
    if (!requirements.size)
        return 0u;
    diagnostic = nullptr;
    return requirements.size;
}

VulkanRhiNativeResourceResult VulkanRhiNativeResourceAllocator::createTexture(
    const rhi::TextureDesc& desc, std::uint64_t maxNativeAllocationBytes)
{
    VulkanRhiNativeResourceResult result;
    if (!connected()) {
        result.diagnostics = "Vulkan RHI native image allocation requires a native device";
        return result;
    }
    const auto format = VulkanTextureDescriptor::format(desc.format);
    const auto usage = VulkanTextureDescriptor::usage(desc.usage, desc.format);
    if (format == VK_FORMAT_UNDEFINED || usage == 0 || desc.extent.width == 0
        || desc.extent.height == 0 || desc.extent.depth == 0) {
        result.diagnostics = "Vulkan RHI native image allocation desc is invalid";
        return result;
    }
    if (!allocator_) {
        result.diagnostics = "Vulkan RHI native image allocation requires a VMA allocator";
        return result;
    }

    const auto imageInfo = VulkanTextureDescriptor::imageInfo(desc, 0u, VK_SAMPLE_COUNT_1_BIT);

    if (runtime_->syntheticRuntime()) {
        if (maxNativeAllocationBytes) {
            result.diagnostics = "RHI-VULKAN-CONTENT-CREATION-AUTHENTIC-RUNTIME";
            return result;
        }
        result.allocation = allocator_->createImage(desc, imageInfo).allocation;
        if (!result.allocation.success) {
            result.diagnostics = result.allocation.diagnostics;
            return result;
        }
        ++stats_->nativeImageCreateCount;
        ++stats_->nativeMemoryAllocationCount;
        result.payload = arp::core::makeShared<VulkanRhiNativeResourcePayload>();
        result.success = true;
        return result;
    }

    const auto allocated = allocator_->createImage(desc, imageInfo, maxNativeAllocationBytes);
    if (!allocated.success) {
        result.diagnostics = allocated.diagnostics;
        return result;
    }

    ++stats_->nativeImageCreateCount;
    ++stats_->nativeMemoryAllocationCount;
    result.allocation = allocated.allocation;
    if (std::getenv("ARP_VULKAN_TRACE_IMAGE_CREATES") != nullptr) {
        std::cerr << "[vulkan-image-create] label=" << desc.label.c_str() << " image=0x" << std::hex
                  << handleToUint(allocated.image) << std::dec
                  << " initial=Undefined format=" << static_cast<unsigned>(desc.format)
                  << " extent=" << desc.extent.width << 'x' << desc.extent.height << '\n';
    }
    result.payload = arp::core::makeShared<NativeImagePayload>(runtime_->logicalDevice(),
        allocated.image,
        allocated.format,
        allocated.extent,
        allocated.payload,
        maxNativeAllocationBytes != 0u);
    result.success = true;
    return result;
}

VulkanRhiNativeResourceResult VulkanRhiNativeResourceAllocator::createDescriptorSet(
    const rhi::DescriptorLayoutDesc& layout)
{
    VulkanRhiNativeResourceResult result;
    if (!connected()) {
        result.diagnostics = "Vulkan RHI native descriptor allocation requires a native device";
        return result;
    }
    const auto validation = rhi::validateDescriptorLayout(layout);
    if (!validation.success) {
        result.diagnostics = validation.diagnostics;
        return result;
    }
    if (runtime_->syntheticRuntime()) {
        ++stats_->nativeDescriptorSetLayoutCreateCount;
        ++stats_->nativeDescriptorPoolCreateCount;
        ++stats_->nativeDescriptorSetAllocateCount;
        result.payload = arp::core::makeShared<VulkanRhiNativeResourcePayload>();
        result.success = true;
        return result;
    }

    auto* createDescriptorSetLayout = functionFrom<PFN_vkCreateDescriptorSetLayout>(
        runtime_->getDeviceProcAddr("vkCreateDescriptorSetLayout"));
    auto* destroyDescriptorSetLayout = functionFrom<PFN_vkDestroyDescriptorSetLayout>(
        runtime_->getDeviceProcAddr("vkDestroyDescriptorSetLayout"));
    auto* createDescriptorPool = functionFrom<PFN_vkCreateDescriptorPool>(
        runtime_->getDeviceProcAddr("vkCreateDescriptorPool"));
    auto* destroyDescriptorPool = functionFrom<PFN_vkDestroyDescriptorPool>(
        runtime_->getDeviceProcAddr("vkDestroyDescriptorPool"));
    auto* allocateDescriptorSets = functionFrom<PFN_vkAllocateDescriptorSets>(
        runtime_->getDeviceProcAddr("vkAllocateDescriptorSets"));
    if (!createDescriptorSetLayout || !destroyDescriptorSetLayout || !createDescriptorPool
        || !destroyDescriptorPool || !allocateDescriptorSets) {
        result.diagnostics = "Vulkan RHI native descriptor allocation missing Vulkan entrypoints";
        return result;
    }

    std::pmr::vector<VkDescriptorSetLayoutBinding> nativeBindings;
    std::pmr::vector<VkDescriptorPoolSize> poolSizes;
    nativeBindings.reserve(layout.bindings.size());
    poolSizes.reserve(layout.bindings.size());
    for (const auto& binding : layout.bindings) {
        const auto nativeType = descriptorTypeFor(binding.type, binding.dynamicOffset);
        const auto nativeStages = shaderStagesFor(binding.stages);
        if (nativeType == VK_DESCRIPTOR_TYPE_MAX_ENUM || nativeStages == 0
            || binding.arrayCount == 0) {
            std::cerr << "[wocc] binding invalid: type=" << static_cast<int>(binding.type)
                      << " nativeType=" << nativeType
                      << " stages=" << static_cast<unsigned>(binding.stages)
                      << " nativeStages=" << nativeStages << " arrayCount=" << binding.arrayCount
                      << "\n";
            result.diagnostics = "Vulkan RHI native descriptor binding is invalid";
            return result;
        }
        nativeBindings.push_back(VkDescriptorSetLayoutBinding{
            binding.binding,
            nativeType,
            binding.arrayCount,
            nativeStages,
            nullptr,
        });
        if (std::getenv("ARP_FORWARD_PLUS_LAYOUT_TRACE") != nullptr) {
            std::cerr << "[vulkan-layout-trace] descriptor-set name=" << layout.name.c_str()
                      << " binding=" << binding.binding
                      << " type=" << static_cast<unsigned>(nativeType)
                      << " count=" << binding.arrayCount << " stages=" << nativeStages << '\n';
        }
        auto poolSize = std::find_if(poolSizes.begin(),
            poolSizes.end(),
            [nativeType](const VkDescriptorPoolSize& candidate) {
                return candidate.type == nativeType;
            });
        if (poolSize == poolSizes.end()) {
            poolSizes.push_back(VkDescriptorPoolSize{
                nativeType,
                binding.arrayCount,
            });
        } else {
            poolSize->descriptorCount += binding.arrayCount;
        }
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<std::uint32_t>(nativeBindings.size());
    layoutInfo.pBindings = nativeBindings.data();

    VkDescriptorSetLayout nativeLayout = VK_NULL_HANDLE;
    const auto layoutCreated =
        createDescriptorSetLayout(runtime_->logicalDevice(), &layoutInfo, nullptr, &nativeLayout);
    if (layoutCreated != VK_SUCCESS) {
        result.diagnostics = vkFailure("vkCreateDescriptorSetLayout", layoutCreated);
        return result;
    }

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = static_cast<std::uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();

    VkDescriptorPool nativePool = VK_NULL_HANDLE;
    const auto poolCreated =
        createDescriptorPool(runtime_->logicalDevice(), &poolInfo, nullptr, &nativePool);
    if (poolCreated != VK_SUCCESS) {
        destroyDescriptorSetLayout(runtime_->logicalDevice(), nativeLayout, nullptr);
        result.diagnostics = vkFailure("vkCreateDescriptorPool", poolCreated);
        return result;
    }

    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = nativePool;
    allocateInfo.descriptorSetCount = 1;
    allocateInfo.pSetLayouts = &nativeLayout;

    VkDescriptorSet nativeSet = VK_NULL_HANDLE;
    const auto setAllocated =
        allocateDescriptorSets(runtime_->logicalDevice(), &allocateInfo, &nativeSet);
    if (setAllocated != VK_SUCCESS) {
        destroyDescriptorPool(runtime_->logicalDevice(), nativePool, nullptr);
        destroyDescriptorSetLayout(runtime_->logicalDevice(), nativeLayout, nullptr);
        result.diagnostics = vkFailure("vkAllocateDescriptorSets", setAllocated);
        return result;
    }

    ++stats_->nativeDescriptorSetLayoutCreateCount;
    ++stats_->nativeDescriptorPoolCreateCount;
    ++stats_->nativeDescriptorSetAllocateCount;
    result.payload = arp::core::makeShared<NativeDescriptorSetPayload>(
        runtime_, nativeLayout, nativePool, nativeSet);
    result.success = true;
    return result;
}

VulkanRhiNativeResourceResult VulkanRhiNativeResourceAllocator::createSampler(
    const rhi::SamplerDesc& desc)
{
    VulkanRhiNativeResourceResult result;
    if (!connected()) {
        result.diagnostics = "Vulkan RHI native sampler allocation requires a native device";
        return result;
    }
    if (runtime_->syntheticRuntime()) {
        result.payload = arp::core::makeShared<VulkanRhiNativeResourcePayload>();
        result.success = true;
        return result;
    }

    auto* createSampler =
        functionFrom<PFN_vkCreateSampler>(runtime_->getDeviceProcAddr("vkCreateSampler"));
    if (!createSampler) {
        result.diagnostics = "Vulkan RHI native sampler allocation missing vkCreateSampler";
        return result;
    }

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = filterFor(desc.magFilter);
    samplerInfo.minFilter = filterFor(desc.minFilter);
    // Mipmap filter mode is driven by SamplerDesc::mipFilter. Linear blends
    // between adjacent levels (trilinear); Nearest selects a single level;
    // None pins sampling to level 0 by clamping the LOD range to [0, 0].
    switch (desc.mipFilter) {
    case rhi::SamplerMipFilter::Nearest:
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        break;
    case rhi::SamplerMipFilter::None:
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 0.0f;
        break;
    default:
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        break;
    }
    samplerInfo.addressModeU = addressModeFor(desc.addressU);
    samplerInfo.addressModeV = addressModeFor(desc.addressV);
    samplerInfo.addressModeW = addressModeFor(desc.addressW);
    if (desc.mipFilter != rhi::SamplerMipFilter::None) {
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
    }
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;

    // Anisotropic filtering. Requires the samplerAnisotropy device feature to
    // be enabled (see render_system_vulkan.cpp device creation). The requested
    // level is clamped to the device's limits.maxSamplerAnisotropy so
    // vkCreateSampler never returns VK_ERROR_FEATURE_NOT_PRESENT.
    if (desc.maxAnisotropy > 1.0f) {
        samplerInfo.anisotropyEnable = VK_TRUE;
        samplerInfo.maxAnisotropy = std::min(desc.maxAnisotropy, runtime_->maxSamplerAnisotropy());
    }

    // Hardware depth comparison for shadow map sampling. When compareOp is
    // not Never, the sampler performs a compare op between the sampled depth
    // and the shader-provided reference value, enabling hardware PCF.
    if (desc.compareOp != rhi::CompareOp::Never) {
        samplerInfo.compareEnable = VK_TRUE;
        samplerInfo.compareOp = compareOpFor(desc.compareOp);
    } else {
        samplerInfo.compareEnable = VK_FALSE;
    }

    VkSampler sampler = VK_NULL_HANDLE;
    const auto created = createSampler(runtime_->logicalDevice(), &samplerInfo, nullptr, &sampler);
    if (created != VK_SUCCESS) {
        result.diagnostics = vkFailure("vkCreateSampler", created);
        return result;
    }

    result.payload = arp::core::makeShared<NativeSamplerPayload>(runtime_, sampler);
    result.success = true;
    return result;
}

} // namespace arp::rhi_vulkan

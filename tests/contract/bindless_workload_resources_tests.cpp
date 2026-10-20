#include "rhi/backends/webgpu/webgpu_bindless_device_limit_plan.h"
#include "rhi/semantics/bindless_workload_resources.h"

#include <array>
#include <cassert>
#include <charconv>
#include <cstdio>
#include <limits>
#include <string_view>

#ifdef NDEBUG
#error Workload resource tests require assertions
#endif

using namespace arp::rhi;
using namespace arp::rhi::semantics;
using namespace arp::rhi_webgpu;

namespace {
class Workload final {
public:
    BindlessShaderArtifact artifact;
    std::array<BufferDesc, 4> buffers;
    std::array<BindlessWorkloadBufferBinding, 3> bindings;
    std::array<TextureDesc, 32> textures;

    Workload()
    {
        artifact.execution = BindlessExecution::Emulated;
        artifact.pipelineInterfaceContractVersion = 1u;
        artifact.physicalLayout.name = "terrain27-ibl-depth-storage-metadata-fault";
        for (std::uint32_t i = 0; i < 32u; ++i) {
            auto& texture = textures[i];
            texture.extent = {2u, 2u, 1u};
            texture.format = TextureFormat::Rgba8Unorm;
            texture.mipLevelCount = 2u;
            texture.usage = TextureUsage::Sampled | TextureUsage::CopyDst;
            if (i == 24u || i == 25u)
                texture.arrayLayerCount = 2u;
            if (i == 27u || i == 28u) {
                texture.extent = {64u, 64u, 1u};
                texture.arrayLayerCount = 6u;
                texture.mipLevelCount = 7u;
                texture.cubeCompatible = true;
                texture.format = TextureFormat::Rgba16Float;
            }
            if (i == 29u) {
                texture.extent = {128u, 128u, 1u};
                texture.mipLevelCount = 1u;
                texture.format = TextureFormat::Rgba16Float;
            }
            if (i == 30u) {
                texture.format = TextureFormat::D32Float;
                texture.usage = TextureUsage::Sampled | TextureUsage::RenderAttachment;
            }
            if (i == 31u) {
                texture.extent = {8u, 4u, 2u};
                texture.format = TextureFormat::Rgba16Float;
                texture.usage = TextureUsage::Storage | TextureUsage::Sampled;
            }
            DescriptorBindingDesc binding;
            binding.binding = i;
            binding.type =
                i == 31u ? DescriptorType::StorageTexture : DescriptorType::SampledTexture;
            binding.stages = i == 31u ? ShaderStage::Compute : ShaderStage::Fragment;
            binding.textureViewDimensionConstrained = true;
            binding.textureViewDimension = texture.cubeCompatible
                ? TextureViewDimension::TextureCube
                : texture.extent.depth > 1u    ? TextureViewDimension::Texture3D
                : texture.arrayLayerCount > 1u ? TextureViewDimension::Texture2DArray
                                               : TextureViewDimension::Texture2D;
            binding.depthTexture = i == 30u;
            if (i == 31u) {
                binding.storageTextureFormat = texture.format;
                binding.storageTextureAccess = StorageTextureAccess::WriteOnly;
            }
            artifact.physicalLayout.bindings.push_back(binding);
        }
        const auto append = [&](DescriptorType type, ShaderStage stages) {
            DescriptorBindingDesc binding;
            binding.binding = static_cast<std::uint32_t>(artifact.physicalLayout.bindings.size());
            binding.type = type;
            binding.stages = stages;
            artifact.physicalLayout.bindings.push_back(binding);
        };
        append(DescriptorType::Sampler, ShaderStage::Fragment);
        append(DescriptorType::UniformBuffer, ShaderStage::Vertex | ShaderStage::Fragment);
        append(DescriptorType::ReadOnlyStorageBuffer, ShaderStage::Fragment);
        append(DescriptorType::StorageBuffer, ShaderStage::Fragment | ShaderStage::Compute);
        buffers[0].sizeBytes = 131072u;
        buffers[0].usage = ResourceUsage::UniformBuffer | ResourceUsage::CopyDst;
        buffers[1].sizeBytes = 524288u;
        buffers[1].usage = ResourceUsage::StorageBuffer | ResourceUsage::CopyDst;
        buffers[2].sizeBytes = 1048576u;
        buffers[2].usage = ResourceUsage::IndirectBuffer | ResourceUsage::CopyDst;
        buffers[3].sizeBytes = 4u;
        buffers[3].usage = ResourceUsage::StorageBuffer | ResourceUsage::CopySrc;
        bindings = {{{0u, DescriptorType::UniformBuffer, {2u, 65536u, 65536u}},
            {1u, DescriptorType::ReadOnlyStorageBuffer, {2u, 256u, 262144u}},
            {3u, DescriptorType::StorageBuffer, {1u, 0u, 0u}}}};
    }

    BindlessWorkloadResources resources() const noexcept
    {
        return {1u,
            buffers.data(),
            buffers.size(),
            bindings.data(),
            bindings.size(),
            textures.data(),
            textures.size()};
    }
};

bool empty(const BindlessWorkloadResourceDemand& demand)
{
    return !demand.maxBufferBytes && !demand.maxUniformBindingBytes
        && !demand.maxStorageBindingBytes && !demand.texture2DDimension
        && !demand.texture3DDimension && !demand.textureArrayLayers && !demand.uniformOffsetDivisor
        && !demand.storageOffsetDivisor;
}
} // namespace

int main(int argc, char** argv)
{
    Workload workload;
    BindlessWorkloadResourceDemand demand;
    auto resources = workload.resources();
    const auto allocations = arp::core::defaultCpuAllocator().stats().allocationCount;
    assert(!BindlessWorkloadResourceContract::inspect(resources, demand));
    assert(demand.maxBufferBytes == 1048576u && demand.maxUniformBindingBytes == 65536u
        && demand.maxStorageBindingBytes == 262144u && demand.uniformOffsetDivisor == 65536u
        && demand.storageOffsetDivisor == 256u && demand.texture2DDimension == 128u
        && demand.texture3DDimension == 8u && demand.textureArrayLayers == 6u);
    for (const auto backend : {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
        const auto decision = BindlessWorkloadResourceContract::lowering(backend);
        assert(decision.planningAvailable && !decision.executionAvailable && !decision.diagnostic);
        assert(decision.validatorOnly == (backend == Backend::Null));
    }
    for (const auto backend : {Backend::WebGL, static_cast<Backend>(255u)}) {
        const auto decision = BindlessWorkloadResourceContract::lowering(backend);
        assert(!decision.planningAvailable && !decision.executionAvailable && decision.diagnostic);
    }
    const auto reject = [&](const char* expected) {
        const auto* id = BindlessWorkloadResourceContract::inspect(resources, demand);
        assert(id && std::string_view(id) == expected && empty(demand));
    };
    resources.contractVersion = 2u;
    reject("RHI-BINDLESS-WORKLOAD-RESOURCES-VERSION");
    resources.contractVersion = 1u;
    resources.buffers = nullptr;
    reject("RHI-BINDLESS-WORKLOAD-RESOURCES-INPUT");
    resources = workload.resources();
    workload.bindings[0].bufferIndex = 4u;
    reject("RHI-BINDLESS-WORKLOAD-BUFFER-INDEX");
    workload.bindings[0].bufferIndex = 0u;
    workload.bindings[0].range.sizeBytes++;
    reject("RHI-DESCRIPTOR-BUFFER-RANGE-BOUNDS");
    workload.bindings[0].range.sizeBytes--;
    workload.buffers[0].usage = ResourceUsage::StorageBuffer;
    reject("RHI-BINDLESS-WORKLOAD-BUFFER-USAGE");
    workload.buffers[0].usage = ResourceUsage::UniformBuffer | ResourceUsage::CopyDst;
    workload.textures[27].extent.height = 63u;
    reject("RHI-BINDLESS-WORKLOAD-TEXTURE-SHAPE");
    workload.textures[27].extent.height = 64u;
    workload.textures[31].arrayLayerCount = 2u;
    reject("RHI-BINDLESS-WORKLOAD-TEXTURE-SHAPE");
    workload.textures[31].arrayLayerCount = 1u;
    workload.textures[0].mipLevelCount = 3u;
    reject("RHI-TEXTURE-DESTINATION-MIPS");
    workload.textures[0].mipLevelCount = 2u;
    workload.buffers[2].sizeBytes = 0u;
    reject("RHI-BINDLESS-WORKLOAD-BUFFER-SHAPE");
    workload.buffers[2].sizeBytes = 1048576u;
    workload.bindings[1].type = DescriptorType::Sampler;
    reject("RHI-BINDLESS-WORKLOAD-BUFFER-TYPE");
    workload.bindings[1].type = DescriptorType::ReadOnlyStorageBuffer;
    workload.textures[0].format = TextureFormat::Unknown;
    reject("RHI-TEXTURE-DESTINATION-LAYOUT");
    workload.textures[0].format = TextureFormat::Rgba8Unorm;
    const auto volumeExtent = workload.textures[31].extent;
    workload.textures[31].extent = {UINT32_MAX, UINT32_MAX, UINT32_MAX};
    reject("RHI-TEXTURE-DESTINATION-BYTE-OVERFLOW");
    workload.textures[31].extent = volumeExtent;
    // Repeated binding references affect the offset divisor, not allocation size.
    auto extraBindings = std::array<BindlessWorkloadBufferBinding, 4>{workload.bindings[0],
        workload.bindings[1],
        workload.bindings[2],
        {1u, DescriptorType::StorageBuffer, {2u, 384u, 4u}}};
    resources.bufferBindings = extraBindings.data();
    resources.bufferBindingCount = extraBindings.size();
    assert(!BindlessWorkloadResourceContract::inspect(resources, demand));
    assert(demand.storageOffsetDivisor == 128u && demand.maxBufferBytes == 1048576u);
    // Independently inspected buffer-binding and texture batches must compose
    // into the same whole workload, including the nonzero offset gcd.
    auto first = resources;
    first.bufferBindingCount = 2u;
    first.textures = nullptr;
    first.textureCount = 0u;
    auto second = resources;
    second.bufferBindings += 2u;
    second.bufferBindingCount -= 2u;
    BindlessWorkloadResourceDemand a, b;
    assert(!BindlessWorkloadResourceContract::inspect(first, a));
    assert(!BindlessWorkloadResourceContract::inspect(second, b));
    const auto combined = BindlessWorkloadResourceContract::merge(a, b);
    const auto reverse = BindlessWorkloadResourceContract::merge(b, a);
    for (const auto& merged : {combined, reverse})
        assert(merged.maxBufferBytes == 1048576u && merged.maxUniformBindingBytes == 65536u
            && merged.maxStorageBindingBytes == 262144u && merged.uniformOffsetDivisor == 65536u
            && merged.storageOffsetDivisor == 128u && merged.texture2DDimension == 128u
            && merged.texture3DDimension == 8u && merged.textureArrayLayers == 6u);
    resources = workload.resources();

    WebGpuBindlessWorkloadAdapterCapacity adapter;
    adapter.contractVersion = 2u;
    adapter.bindings = {1u, true, 1u, 36u, {31u, 1u, 1u, 1u, 2u}};
    adapter.resources = {1u, 1048576u, 65536u, 262144u, 256u, 256u, 128u, 8u, 6u};
    // Size preflight must not invent a device-global alignment for Metal.
    assert(!BindlessWorkloadResourceContract::inspect(resources, demand));
    auto sizes = adapter.resources;
    sizes.uniformOffsetAlignment = sizes.storageOffsetAlignment = 0u;
    assert(!BindlessWorkloadResourceContract::validateSizeLimits(demand, sizes));
    assert(std::string_view(BindlessWorkloadResourceContract::validateLimits(demand, sizes))
        == "RHI-BINDLESS-WORKLOAD-LIMITS-UNAVAILABLE");
    const auto rejectSize = [&](const char* expected) {
        const auto* id = BindlessWorkloadResourceContract::validateSizeLimits(demand, sizes);
        assert(id && std::string_view(id) == expected);
    };
    for (auto* value :
        {&sizes.maxBufferBytes, &sizes.maxUniformBindingBytes, &sizes.maxStorageBindingBytes}) {
        const auto exact = *value;
        --*value;
        rejectSize("RHI-BINDLESS-WORKLOAD-RESOURCE-LIMIT");
        *value = 0u;
        rejectSize("RHI-BINDLESS-WORKLOAD-LIMITS-UNAVAILABLE");
        *value = exact;
    }
    for (auto* value :
        {&sizes.texture2DDimension, &sizes.texture3DDimension, &sizes.textureArrayLayers}) {
        const auto exact = *value;
        --*value;
        rejectSize("RHI-BINDLESS-WORKLOAD-RESOURCE-LIMIT");
        *value = 0u;
        rejectSize("RHI-BINDLESS-WORKLOAD-LIMITS-UNAVAILABLE");
        *value = exact;
    }
    for (auto version : {0u, 2u, UINT32_MAX}) {
        sizes.contractVersion = version;
        rejectSize("RHI-BINDLESS-WORKLOAD-LIMITS-VERSION");
    }
    sizes.contractVersion = 1u;
    assert(!BindlessWorkloadResourceContract::validateSizeLimits(demand, sizes));
    assert(!BindlessWorkloadResourceContract::validateSizeLimits({}, {1u}));
    for (auto backend : {Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
        const auto facts = BindlessWorkloadResourceContract::devicePreflightLowering(backend);
        assert(facts.available && !facts.diagnostic);
        assert(facts.globalAlignmentRequired == (backend != Backend::Metal));
    }
    for (auto backend : {Backend::Null, Backend::WebGL, static_cast<Backend>(255u)}) {
        const auto facts = BindlessWorkloadResourceContract::devicePreflightLowering(backend);
        assert(!facts.available && facts.diagnostic);
    }
    WebGpuBindlessWorkloadDeviceLimitPlan plan;
    assert(!WebGpuBindlessDeviceLimitPlanner::planWorkload(
        workload.artifact, resources, adapter, plan));
    assert(plan.contractVersion == 2u && plan.count == 15u);
    const auto rejectPlan = [&](const char* expected) {
        const auto* id = WebGpuBindlessDeviceLimitPlanner::planWorkload(
            workload.artifact, resources, adapter, plan);
        assert(id && std::string_view(id) == expected && !plan.contractVersion && !plan.count
            && empty(plan.resources) && !plan.bindings.descriptorCount);
        for (const auto& entry : plan.requiredLimits)
            assert(!entry.name && !entry.value);
    };
    for (auto* limit : {&adapter.resources.maxBufferBytes,
             &adapter.resources.maxUniformBindingBytes,
             &adapter.resources.maxStorageBindingBytes}) {
        --*limit;
        rejectPlan("RHI-BINDLESS-WORKLOAD-RESOURCE-LIMIT");
        ++*limit;
    }
    for (auto* limit : {&adapter.resources.texture2DDimension,
             &adapter.resources.texture3DDimension,
             &adapter.resources.textureArrayLayers}) {
        --*limit;
        rejectPlan("RHI-BINDLESS-WORKLOAD-RESOURCE-LIMIT");
        ++*limit;
    }
    adapter.resources.storageOffsetAlignment = 512u;
    rejectPlan("RHI-BINDLESS-WORKLOAD-BUFFER-ALIGNMENT");
    adapter.resources.storageOffsetAlignment = 128u;
    assert(!WebGpuBindlessDeviceLimitPlanner::planWorkload(
        workload.artifact, resources, adapter, plan));
    assert(plan.requiredLimits.back().value == 128u);
    adapter.resources.storageOffsetAlignment = 0u;
    rejectPlan("RHI-BINDLESS-WORKLOAD-LIMITS-UNAVAILABLE");
    adapter.resources.storageOffsetAlignment = 256u;
    adapter.contractVersion = 1u;
    rejectPlan("RHI-WEBGPU-BINDLESS-WORKLOAD-DEVICE-LIMIT-PLAN-VERSION");
    adapter.contractVersion = 2u;
    adapter.bindings.limitsQueried = false;
    rejectPlan("RHI-WEBGPU-BINDLESS-ADAPTER-LIMITS-UNAVAILABLE");
    adapter.bindings.limitsQueried = true;
    adapter.resources.contractVersion = 0u;
    rejectPlan("RHI-BINDLESS-WORKLOAD-LIMITS-VERSION");
    adapter.resources.contractVersion = 1u;
    workload.buffers[2].sizeBytes = std::uint64_t{1} << 53u;
    adapter.resources.maxBufferBytes = workload.buffers[2].sizeBytes;
    rejectPlan("RHI-WEBGPU-BINDLESS-WORKLOAD-LIMIT-INTEGER");
    workload.buffers[2].sizeBytes = 1048576u;
    adapter.resources.maxBufferBytes = 1048576u;
    const auto savedBindings = workload.bindings;
    for (auto& binding : workload.bindings)
        binding.range = {2u, 0u, 4u};
    assert(!WebGpuBindlessDeviceLimitPlanner::planWorkload(
        workload.artifact, resources, adapter, plan));
    assert(plan.count == 13u);
    for (const auto& entry : plan.requiredLimits)
        assert(!entry.name || std::string_view(entry.name).substr(0u, 3u) != "min");
    workload.bindings = savedBindings;
    BindlessWorkloadResources none;
    none.contractVersion = 1u;
    assert(!BindlessWorkloadResourceContract::inspect(none, demand) && empty(demand));
    assert(arp::core::defaultCpuAllocator().stats().allocationCount == allocations);
    if (argc != 1) {
        assert(argc == 17 && std::string_view(argv[1]) == "--adapter");
        const std::array<std::uint32_t*, 7> descriptors{&adapter.bindings.bindGroups,
            &adapter.bindings.bindingsPerGroup,
            &adapter.bindings.perStage.sampledTextures,
            &adapter.bindings.perStage.samplers,
            &adapter.bindings.perStage.storageTextures,
            &adapter.bindings.perStage.uniformBuffers,
            &adapter.bindings.perStage.storageBuffers};
        const auto parse = [&](std::size_t index, auto& value) {
            const std::string_view text(argv[index]);
            const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
            assert(result.ec == std::errc{} && result.ptr == text.data() + text.size());
        };
        for (std::size_t i = 0; i < descriptors.size(); ++i)
            parse(i + 2u, *descriptors[i]);
        parse(9u, adapter.resources.maxBufferBytes);
        parse(10u, adapter.resources.maxUniformBindingBytes);
        parse(11u, adapter.resources.maxStorageBindingBytes);
        parse(12u, adapter.resources.uniformOffsetAlignment);
        parse(13u, adapter.resources.storageOffsetAlignment);
        parse(14u, adapter.resources.texture2DDimension);
        parse(15u, adapter.resources.texture3DDimension);
        parse(16u, adapter.resources.textureArrayLayers);
        const auto* id = WebGpuBindlessDeviceLimitPlanner::planWorkload(
            workload.artifact, resources, adapter, plan);
        if (id) {
            std::printf("BINDLESS_WORKLOAD_DEVICE_PLAN_REFUSED %s\n", id);
            return 2;
        }
        std::printf(
            "BINDLESS_WORKLOAD_DEVICE_PLAN_JSON {\"contractVersion\":2,\"requiredLimits\":{");
        for (std::uint32_t i = 0; i < plan.count; ++i)
            std::printf("%s\"%s\":%llu",
                i ? "," : "",
                plan.requiredLimits[i].name,
                static_cast<unsigned long long>(plan.requiredLimits[i].value));
        std::puts("}}");
    }
    std::puts(
        "BINDLESS_WORKLOAD_RESOURCES_OK exact one-less shapes ranges alignment hot0 no-execution");
}

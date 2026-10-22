#include "renderer/shader_package_reflection.h"
#include "renderer/volumetric_cloud/cloud_gpu_contract.h"
#include "rhi/backends/vulkan/vulkan_backend_objects.h"
#include "rhi/backends/vulkan/vulkan_native_commands.h"
#include "tests/fixtures/vulkan_heap_owner_fixture.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace {
using namespace arp;
using namespace rhi;
using namespace rhi::semantics;

PhysicalBudgetObservation observation(const GpuDevice& device, PhysicalBudgetCategory category)
{
    const auto ledger = device.physicalBudgetLedger();
    assert(ledger.valid() && ledger.category(category).observed());
    return ledger.category(category);
}

void contract()
{
    rhi_vulkan::VulkanRhiDeviceDesc desc;
    desc.memoryBudgetBytes = 1024u * 1024u;
    desc.capabilityContract = nativeBackendCapabilityContract(Backend::Vulkan);
    desc.directObjectBudget = {kDirectObjectBudgetContractVersion, "DirectBudget.Contract/1", 2, 4};
    for (unsigned field = 0; field < 4; ++field) {
        auto invalid = desc;
        if (field == 0)
            invalid.directObjectBudget.contractVersion = 999;
        if (field == 1)
            invalid.directObjectBudget.policyId = "";
        if (field == 2)
            invalid.directObjectBudget.pipelineObjectCapacity = 0;
        if (field == 3)
            invalid.directObjectBudget.descriptorCapacity = 0;
        const auto report = rhi_vulkan::describeVulkanRhiDevice(invalid);
        assert(!report.success && report.diagnostics == "RHI-DIRECT-OBJECT-BUDGET-INVALID");
        assert(!rhi_vulkan::createVulkanRhiDevice(invalid));
    }
    auto device = rhi_vulkan::createVulkanRhiDevice(desc);
    assert(device);
    // Vulkan staging pages are allocated by this same resource allocator.
    // Their category must not be added to ResourceAllocation a second time.
    assert(observation(*device, PhysicalBudgetCategory::UploadStaging).chargeRelation
        == PhysicalBudgetChargeRelation::OverlapsResourceAllocation);
    auto first = device->createPipelineLayout({});
    auto second = device->createPipelineLayout({});
    assert(first && second && !device->createPipelineLayout({}));
    auto pipelines = observation(*device, PhysicalBudgetCategory::DirectPipelineObjects);
    assert(pipelines.current == 2 && pipelines.peak == 2 && pipelines.rejectionCount == 1);
    first.reset();
    second.reset();
    PipelineLayoutDesc invalidLayout;
    invalidLayout.pushConstantRanges.push_back({ShaderStage::None, 0, 4});
    assert(!device->createPipelineLayout(invalidLayout));
    assert(!observation(*device, PhysicalBudgetCategory::DirectPipelineObjects).current);

    // Metadata-only host contract: no native payload or completion claim.
    auto arena = device->createDescriptorArena({"generation A", 2, 2});
    auto other = device->createDescriptorArena({"generation B", 2, 2});
    assert(arena && other && !device->createDescriptorArena({"exhausted", 1, 1}));
    DescriptorTableDesc tableDesc{
        "retained host table", DescriptorType::UniformBuffer, ShaderStage::Compute, 1};
    auto table = arena->allocateDescriptorTable(tableDesc);
    assert(table && table->isCurrentAllocation());
    arena->reset();
    assert(!table->isCurrentAllocation());
    const auto refused = arena->allocateTable(tableDesc);
    assert(!refused.success && refused.diagnostics == "RHI-DIRECT-DESCRIPTOR-GENERATION-BUDGET");
    assert(observation(*device, PhysicalBudgetCategory::DirectDescriptorArenas).current == 4);
    table.reset();
    assert(observation(*device, PhysicalBudgetCategory::DirectDescriptorArenas).current == 2);
    // Releasing old owners never implicitly reopens a failed generation.
    assert(!arena->allocateTable(tableDesc).success);
    arena->reset();
    assert(arena->allocateTable(tableDesc).success);
    arena.reset();
    other.reset();
    assert(!observation(*device, PhysicalBudgetCategory::DirectDescriptorArenas).current);
    // Explicit slack admits overlapping generations, each charged once.
    arena = device->createDescriptorArena({"overlapping generations", 1, 2});
    auto old = arena->allocateDescriptorTable(tableDesc);
    assert(old);
    arena->reset();
    auto next = arena->allocateDescriptorTable(tableDesc);
    auto generations = observation(*device, PhysicalBudgetCategory::DirectDescriptorArenas);
    assert(next && !old->isCurrentAllocation() && generations.current == 4
        && generations.liveCount == 2);
    old.reset();
    generations = observation(*device, PhysicalBudgetCategory::DirectDescriptorArenas);
    assert(generations.current == 2 && generations.liveCount == 1);
    arena.reset();
    assert(!next->isCurrentAllocation());
    assert(observation(*device, PhysicalBudgetCategory::DirectDescriptorArenas).current == 2);
    next.reset();
    assert(!observation(*device, PhysicalBudgetCategory::DirectDescriptorArenas).current);
    auto unconfigured = desc;
    unconfigured.directObjectBudget = {};
    auto legacy = rhi_vulkan::createVulkanRhiDevice(unconfigured);
    assert(legacy
        && !legacy->physicalBudgetLedger()
            .category(PhysicalBudgetCategory::DirectPipelineObjects)
            .observed());
    std::puts("DirectObjectBudgetRetention/1 host contract PASS; native unproven");
}

bool failPipeline = false;
bool failDescriptors = false;

VKAPI_ATTR VkResult VKAPI_CALL createCompute(VkDevice device,
    VkPipelineCache cache,
    std::uint32_t count,
    const VkComputePipelineCreateInfo* infos,
    const VkAllocationCallbacks* allocator,
    VkPipeline* output)
{
    if (failPipeline) {
        failPipeline = false;
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    return vkCreateComputePipelines(device, cache, count, infos, allocator, output);
}

VKAPI_ATTR VkResult VKAPI_CALL createPool(VkDevice device,
    const VkDescriptorPoolCreateInfo* info,
    const VkAllocationCallbacks* allocator,
    VkDescriptorPool* output)
{
    if (failDescriptors) {
        failDescriptors = false;
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    return vkCreateDescriptorPool(device, info, allocator, output);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL resolve(VkDevice device, const char* name)
{
    if (std::strcmp(name, "vkCreateComputePipelines") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(createCompute);
    if (std::strcmp(name, "vkCreateDescriptorPool") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(createPool);
    return vkGetDeviceProcAddr(device, name);
}

int native()
{
    tests::VulkanHeadlessRuntime runtime(false, resolve);
    if (!runtime.runtime() || !runtime.runtime()->valid())
        return 77;
    // Backend-private entry must not silently discard an unavailable L2
    // recording budget either. Refusal precedes command/pool allocation.
    {
        rhi_vulkan::VulkanRhiNativeResourceAllocator resources(runtime.runtime());
        CommandEncoderDesc bounded;
        bounded.retentionBudget = CommandRetentionBudget{1u, true, 2u, 0u, 0u, 0u, 4096u};
        core::pmr::String diagnostic;
        assert(!rhi_vulkan::createVulkanRhiNativeCommandPayload(resources, diagnostic, bounded));
        assert(diagnostic == "RHI-COMMAND-RETENTION-UNSUPPORTED");
        assert(resources.stats().nativeCommandPoolCreateCount == 0u
            && resources.stats().nativeCommandBufferAllocateCount == 0u);
    }
    const DirectObjectBudget budget{
        kDirectObjectBudgetContractVersion, "DirectBudget.Native/1", 2, 4};
    auto device = tests::VulkanHeapOwnerFixture::makeDevice(
        runtime.runtime(), false, 16u * 1024u * 1024u, &budget);
    assert(device);
    DescriptorLayoutDesc bindings;
    bindings.name = "cloud_temporal";
    const std::array types{DescriptorType::UniformBuffer,
        DescriptorType::ReadOnlyStorageBuffer,
        DescriptorType::ReadOnlyStorageBuffer,
        DescriptorType::StorageBuffer};
    for (std::uint32_t i = 0; i < types.size(); ++i)
        bindings.bindings.push_back({i, types[i], ShaderStage::Compute});
    PipelineLayoutDesc layoutDesc;
    layoutDesc.descriptorLayouts.push_back(bindings);
    layoutDesc.descriptorSets.push_back({0, bindings.name});
    auto layout = device->createPipelineLayout(layoutDesc);
    assert(layout);
    ShaderPackageDesc package;
    package.label = "cloud_temporal";
    package.slangModulePath = ARP_CLOUD_SHADER_DIR "/cloud_temporal.slang";
    package.entrypoints.push_back({package.slangModulePath,
        "computeMain",
        ShaderStage::Compute,
        ShaderSourceLanguage::Slang});
    package.artifacts.spirvPath = ARP_CLOUD_SHADER_DIR "/cloud_temporal.spv";
    package.artifacts.mslPath = ARP_CLOUD_SHADER_DIR "/cloud_temporal.metal";
    package.artifacts.wgslPath = ARP_CLOUD_SHADER_DIR "/cloud_temporal.wgsl";
    package.artifacts.reflectionPath = ARP_CLOUD_SHADER_DIR "/cloud_temporal.reflection.yaml";
    assert(renderer::hydrateShaderPackageReflectionFromArtifact(package));
    for (const auto& binding : bindings.bindings)
        package.reflection.descriptorBindings.push_back(makeShaderReflectionBinding(0, binding));
    ComputePipelineDesc definition;
    definition.layout = layout.get();
    definition.shaderPackage = &package;
    definition.computeShader = package.entrypoints.front();
    failPipeline = true;
    assert(!device->createComputePipeline(definition) && !failPipeline);
    assert(observation(*device, PhysicalBudgetCategory::DirectPipelineObjects).current == 1);
    auto pipeline = device->createComputePipeline(definition);
    assert(pipeline
        && rhi_vulkan::vulkanRhiNativePayload(*pipeline)->backendVulkanComputePipelineHandle());
    const auto before = rhi_vulkan::inspectVulkanRhiNativeResourceStats(*device);
    assert(!device->createComputePipeline(definition));
    const auto after = rhi_vulkan::inspectVulkanRhiNativeResourceStats(*device);
    assert(after.nativeShaderModuleCreateCount == before.nativeShaderModuleCreateCount
        && after.nativeComputePipelineCreateCount == before.nativeComputePipelineCreateCount);
    auto arena = device->createDescriptorArena({"retained native generation", 1, 4});
    assert(arena);
    failDescriptors = true;
    assert(!arena->allocateDescriptorSet({bindings.name, bindings}) && !failDescriptors);
    assert(!arena->stats().allocatedSets && !arena->stats().allocatedDescriptors);
    assert(observation(*device, PhysicalBudgetCategory::DirectDescriptorArenas).current == 4);
    auto set = arena->allocateDescriptorSet({bindings.name, bindings});
    assert(set && rhi_vulkan::vulkanRhiNativePayload(*set)->backendVulkanDescriptorSetHandle());
    std::array<core::UniquePtr<GpuBuffer>, 4> buffers;
    for (std::uint32_t i = 0; i < buffers.size(); ++i) {
        buffers[i] = device->createBuffer({"retention binding",
            sizeof(renderer::cloud::CloudGpuConstants),
            i == 0 ? ResourceUsage::UniformBuffer : ResourceUsage::StorageBuffer,
            MemoryUsage::CpuToGpu,
            i == 3 ? ResourceState::ShaderWrite : ResourceState::ShaderRead});
        assert(buffers[i]
            && set->write({i, types[i], buffers[i]->id(), DescriptorResourceKind::Buffer}).success);
    }
    auto encoder = device->createCommandEncoder({"NativeRetainedDirectObjects"});
    auto pass = encoder->beginComputePass({"record native bindings"});
    assert(pass && pass->setPipeline(*pipeline).success && pass->bindDescriptorSet(0, *set).success
        && pass->end().success);
    pass.reset();
    auto command = encoder->finish();
    auto gate = device->createTimelineSemaphore(0);
    auto fence = device->createFence();
    assert(command && gate && fence);
    const auto submitted = device->queue().submit(*command, gate.get(), 1, nullptr, 0, fence.get());
    assert(submitted.success && !fence->isSignaled());
    encoder.reset();
    command.reset();
    pipeline.reset();
    arena->reset();
    assert(!set->isCurrentAllocation());
    set.reset();
    const bool retained =
        observation(*device, PhysicalBudgetCategory::DirectPipelineObjects).current == 2
        && observation(*device, PhysicalBudgetCategory::DirectDescriptorArenas).current == 4;
    const bool rejected = !device->createComputePipeline(definition)
        && !device->createDescriptorArena({"in flight budget", 1, 4});
    const bool generationClosed = !arena->allocateSet(bindings).success;
    // Always release the real native gate before asserting pending evidence.
    gate->signal(1);
    fence->wait();
    device->waitIdle();
    assert(retained && rejected && generationClosed);
    assert(observation(*device, PhysicalBudgetCategory::DirectPipelineObjects).current == 1);
    assert(!observation(*device, PhysicalBudgetCategory::DirectDescriptorArenas).current);
    arena->reset();
    auto recovered = arena->allocateDescriptorSet({bindings.name, bindings});
    assert(recovered);
    recovered.reset();
    arena.reset();
    assert(device->createComputePipeline(definition));
    layout.reset();
    assert(!observation(*device, PhysicalBudgetCategory::DirectPipelineObjects).current);
    assert(!observation(*device, PhysicalBudgetCategory::DirectDescriptorArenas).current);
    // Last native owners can outlive the device wrapper. Their charge tokens
    // must survive too, then disappear with native payload destruction.
    layout = device->createPipelineLayout(layoutDesc);
    definition.layout = layout.get();
    pipeline = device->createComputePipeline(definition);
    arena = device->createDescriptorArena({"beyond device wrapper", 1, 4});
    set = arena->allocateDescriptorSet({bindings.name, bindings});
    assert(pipeline && set);
    auto pipelinePayload = rhi_vulkan::vulkanRhiNativePayload(*pipeline);
    auto descriptorPayload = rhi_vulkan::vulkanRhiNativePayload(*set);
    const std::weak_ptr<void> pipelineCharge = pipelinePayload->directObjectBudgetReleaseToken;
    const std::weak_ptr<void> descriptorCharge = descriptorPayload->directObjectBudgetReleaseToken;
    pipeline.reset();
    set.reset();
    layout.reset();
    arena.reset();
    assert(observation(*device, PhysicalBudgetCategory::DirectPipelineObjects).current == 1);
    assert(observation(*device, PhysicalBudgetCategory::DirectDescriptorArenas).current == 4);
    device.reset();
    assert(!pipelineCharge.expired() && !descriptorCharge.expired());
    pipelinePayload.reset();
    descriptorPayload.reset();
    assert(pipelineCharge.expired() && descriptorCharge.expired());
    std::puts(
        "DirectObjectBudgetRetention/1 Vulkan PASS: native failure rollback, pre-create refusal, gated command retention, reset/recovery");
    return 0;
}
} // namespace

int main(int argc, char** argv)
{
    contract();
    return argc == 2 && std::string_view(argv[1]) == "--contract" ? 0 : native();
}

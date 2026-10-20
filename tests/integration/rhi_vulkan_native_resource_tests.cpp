#include "core/cpu_allocator.h"
#include "rhi/backends/vulkan/vulkan_backend_objects.h"
#include "rhi/backends/vulkan/vulkan_device.h"
#include "rhi/backends/vulkan/vulkan_native_commands.h"
#include "rhi/backends/vulkan/vulkan_runtime.h"
#include "rhi/semantics/vulkan_device_capability_query.h"
#include "tests/fixtures/descriptor_liveness_contract.h"
#include "tests/fixtures/recorded_resource_state_checks.h"
#include "tests/fixtures/resource_identity_contract.h"
#include "tests/fixtures/sampled_depth_descriptor_contract.h"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>

namespace {

void verifyRecordedDescriptorStorage(arp::rhi::GpuDevice& device)
{
    using namespace arp::rhi;
    using namespace arp::rhi_vulkan;
    const auto baseline = device.memoryStats();
    DescriptorLayoutDesc layout;
    layout.name = "RecordedDescriptorStorage";
    layout.bindings.push_back({0u, DescriptorType::UniformBuffer, ShaderStage::Compute, 2u});
    layout.bindings.push_back({1u, DescriptorType::SampledTexture, ShaderStage::Compute});
    layout.bindings.push_back({2u, DescriptorType::Sampler, ShaderStage::Compute});
    auto arena = device.createDescriptorArena({"RecordedDescriptorStorage", 1u, 4u});
    auto set = arena->allocateDescriptorSet({"RecordedDescriptorStorage", layout});
    auto buffer = device.createBuffer({"RecordedDescriptorBuffer",
        256u,
        ResourceUsage::UniformBuffer,
        MemoryUsage::CpuToGpu,
        ResourceState::ShaderRead});
    auto arrayBuffer = device.createBuffer({"RecordedArrayBuffer",
        256u,
        ResourceUsage::UniformBuffer,
        MemoryUsage::CpuToGpu,
        ResourceState::ShaderRead});
    assert(set && buffer && arrayBuffer);
    std::weak_ptr<VulkanRhiNativeResourcePayload> observedArray =
        vulkanRhiNativePayload(*arrayBuffer);
    auto texture = device.createTexture({"RecordedDescriptorTexture",
        {8u, 8u, 1u},
        TextureFormat::Rgba8Unorm,
        TextureUsage::Sampled,
        MemoryUsage::GpuOnly,
        ResourceState::ShaderRead});
    assert(texture);
    auto view = device.createTextureView(
        *texture, {"RecordedDescriptorView", TextureFormat::Rgba8Unorm, TextureUsage::Sampled});
    auto sampler = device.createSampler({"RecordedDescriptorSampler"});
    assert(view && sampler);
    std::weak_ptr<VulkanRhiNativeResourcePayload> observed = vulkanRhiNativePayload(*buffer);
    std::weak_ptr<VulkanRhiNativeResourcePayload> observedView = vulkanRhiNativePayload(*view);
    std::weak_ptr<VulkanRhiNativeResourcePayload> observedTexture =
        vulkanRhiNativePayload(*texture);
    std::weak_ptr<VulkanRhiNativeResourcePayload> observedSampler =
        vulkanRhiNativePayload(*sampler);
    assert(!observed.expired());
    assert(
        set->write(
               {0u, DescriptorType::UniformBuffer, buffer->id(), DescriptorResourceKind::Buffer})
            .success);
    DescriptorBindingWrite arrayWrite{
        0u, DescriptorType::UniformBuffer, arrayBuffer->id(), DescriptorResourceKind::Buffer};
    arrayWrite.arrayElement = 1u;
    assert(set->write(arrayWrite).success);
    assert(
        set->write({1u,
                       DescriptorType::SampledTexture,
                       view->id(),
                       DescriptorResourceKind::TextureView})
            .success);
    assert(set->write({2u, DescriptorType::Sampler, sampler->id(), DescriptorResourceKind::Sampler})
            .success);
    auto encoder = device.createCommandEncoder({"RecordedDescriptorStorage"});
    auto pass = encoder->beginComputePass({"RecordedDescriptorStorage"});
    assert(pass && pass->bindDescriptorSet(0u, *set).success);
    assert(pass->dispatch(1u, 1u, 1u).success); // explicitly synthetic native payload
    assert(pass->end().success);
    auto commands = encoder->finish();
    assert(commands);
    buffer.reset();
    arrayBuffer.reset();
    view.reset();
    texture.reset();
    sampler.reset();
    set.reset();
    arena.reset();
    assert(!observed.expired());
    assert(!observedArray.expired());
    assert(!observedView.expired() && !observedTexture.expired() && !observedSampler.expired());
    assert(device.memoryStats().allocatedBytes == baseline.allocatedBytes + 512u + 8u * 8u * 4u);
    pass.reset();
    encoder.reset();
    assert(!observed.expired());
    commands.reset(); // discard without submission; no GPU runtime claim
    assert(observed.expired());
    assert(observedArray.expired());
    assert(observedView.expired() && observedTexture.expired() && observedSampler.expired());
    assert(device.memoryStats().allocatedBytes == baseline.allocatedBytes);
    assert(device.memoryStats().liveResourceCount == baseline.liveResourceCount);

    // A changed native write must not mutate a recorded descriptor version.
    // Releasing a discarded command restores the exclusive rewrite permission.
    layout.bindings.resize(1u);
    layout.bindings[0].arrayCount = 1u;
    arena = device.createDescriptorArena({"RecordedRewrite", 1u, 1u});
    set = arena->allocateDescriptorSet({"RecordedRewrite", layout});
    auto first = device.createBuffer({"RecordedFirst",
        256u,
        ResourceUsage::UniformBuffer,
        MemoryUsage::CpuToGpu,
        ResourceState::ShaderRead});
    auto second = device.createBuffer({"RecordedSecond",
        256u,
        ResourceUsage::UniformBuffer,
        MemoryUsage::CpuToGpu,
        ResourceState::ShaderRead});
    assert(set && first && second);
    assert(
        set->write({0u, DescriptorType::UniformBuffer, first->id(), DescriptorResourceKind::Buffer})
            .success);
    encoder = device.createCommandEncoder({"RecordedRewrite"});
    pass = encoder->beginComputePass({"RecordedRewrite"});
    const auto bound = pass->bindDescriptorSet(0u, *set);
    assert(bound.success);
    const auto revision = set->contentRevision();
    for (unsigned retry = 0; retry < 3u; ++retry) {
        const auto rejected = set->write(
            {0u, DescriptorType::UniformBuffer, second->id(), DescriptorResourceKind::Buffer});
        assert(!rejected.success
            && rejected.diagnostics.find("RHI-DESCRIPTOR-RECORDED-STORAGE-IN-USE") == 0u);
        assert(set->contentRevision() == revision && set->validateBinding(0u).success);
    }
    const auto recorded = pass->dispatch(1u, 1u, 1u);
    assert(recorded.success && recorded.recordedCommandCount == bound.recordedCommandCount + 1u);
    assert(pass->end().success);
    commands = encoder->finish();
    observed = vulkanRhiNativePayload(*first);
    first.reset();
    pass.reset();
    encoder.reset();
    assert(!observed.expired());
    commands.reset();
    assert(
        set->write(
               {0u, DescriptorType::UniformBuffer, second->id(), DescriptorResourceKind::Buffer})
            .success);
    assert(set->contentRevision() > revision && observed.expired());
    second.reset();
    set.reset();
    arena.reset();
    assert(device.memoryStats().allocatedBytes == baseline.allocatedBytes);
    assert(device.memoryStats().liveResourceCount == baseline.liveResourceCount);
    std::puts("DescriptorRecordedStorage/1 Vulkan synthetic ownership/rewrite/discard: PASS");
}

std::pmr::string readText(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    std::ostringstream stream;
    stream << file.rdbuf();
    return arp::core::toPmrString(stream.str());
}

std::filesystem::path projectRoot()
{
#if defined(ARP_SOURCE_DIR)
    return std::filesystem::path(ARP_SOURCE_DIR);
#else
    auto current = std::filesystem::current_path();
    while (!current.empty()) {
        if (std::filesystem::exists(current / "CMakeLists.txt")
            && std::filesystem::exists(current / "src" / "rhi")) {
            return current;
        }
        const auto parent = current.parent_path();
        if (parent == current) {
            break;
        }
        current = parent;
    }
    return std::filesystem::current_path();
#endif
}

void assertNativeResourcesUseVma()
{
    const auto root = projectRoot();
    const auto commandContracts = readText(root / "src" / "rhi" / "gpu_commands.h");
    const auto resourceContracts = readText(root / "src" / "rhi" / "gpu_resources.h");
    const auto pipelineContracts = readText(root / "src" / "rhi" / "gpu_pipeline.h");
    const auto source =
        readText(root / "src" / "rhi" / "backends" / "vulkan" / "vulkan_native_resources.cpp");
    const auto graphicsCommandSource = readText(
        root / "src" / "rhi" / "backends" / "vulkan" / "vulkan_native_graphics_commands.cpp");
    const auto graphicsPassSource =
        readText(root / "src" / "rhi" / "backends" / "vulkan" / "vulkan_graphics_pass.cpp");
    const auto nativeCommandSource =
        readText(root / "src" / "rhi" / "backends" / "vulkan" / "vulkan_native_commands.cpp");
    const auto pipelineSource =
        readText(root / "src" / "rhi" / "backends" / "vulkan" / "vulkan_native_pipelines.cpp");
    assert(commandContracts.find("bindVertexBuffer") != std::pmr::string::npos);
    assert(commandContracts.find("bindIndexBuffer") != std::pmr::string::npos);
    assert(commandContracts.find("PushConstantsDesc") != std::pmr::string::npos);
    assert(commandContracts.find("const void* data") != std::pmr::string::npos);
    assert(resourceContracts.find("cubeCompatible") != std::pmr::string::npos);
    assert(resourceContracts.find("TextureCube") != std::pmr::string::npos);
    assert(pipelineContracts.find("VertexAttributeDesc") != std::pmr::string::npos);
    assert(pipelineContracts.find("vertexBuffers") != std::pmr::string::npos);
    assert(source.find("allocator_->createBuffer") != std::pmr::string::npos);
    assert(source.find("allocator_->createImage") != std::pmr::string::npos);
    assert(graphicsCommandSource.find("allocator->createBuffer") != std::pmr::string::npos);
    assert(source.find("vkAllocateMemory") == std::pmr::string::npos);
    assert(source.find("vkBindBufferMemory") == std::pmr::string::npos);
    assert(source.find("vkBindImageMemory") == std::pmr::string::npos);
    assert(source.find("vkGetBufferMemoryRequirements") == std::pmr::string::npos);
    assert(source.find("vkGetImageMemoryRequirements") == std::pmr::string::npos);
    assert(source.find("vkFreeMemory") == std::pmr::string::npos);
    assert(graphicsCommandSource.find("vkAllocateMemory") == std::pmr::string::npos);
    assert(graphicsCommandSource.find("vkBindBufferMemory") == std::pmr::string::npos);
    assert(graphicsCommandSource.find("vkGetBufferMemoryRequirements") == std::pmr::string::npos);
    assert(graphicsCommandSource.find("vkMapMemory") == std::pmr::string::npos);
    assert(graphicsCommandSource.find("vkUnmapMemory") == std::pmr::string::npos);
    assert(graphicsCommandSource.find("vkFreeMemory") == std::pmr::string::npos);
    assert(graphicsCommandSource.find("kScratchIndexCapacityBytes") != std::pmr::string::npos);
    assert(graphicsCommandSource.find("4u * 1024u * 1024u") != std::pmr::string::npos);
    assert(graphicsCommandSource.find("kScratchIndexCapacity = 4096") == std::pmr::string::npos);
    assert(graphicsCommandSource.find("vkCmdBindVertexBuffers") != std::pmr::string::npos);
    assert(graphicsCommandSource.find("vkCmdBindIndexBuffer") != std::pmr::string::npos);
    assert(graphicsCommandSource.find("vkCmdPushConstants") != std::pmr::string::npos);
    assert(graphicsCommandSource.find("boundGraphicsPipelineLayout") != std::pmr::string::npos);
    assert(graphicsPassSource.find("std::uint32_t setIndex") != std::pmr::string::npos);
    assert(graphicsPassSource.find(
               "recordDescriptorSetBind(\n                *descriptorSet, setIndex")
        != std::pmr::string::npos);
    assert(
        nativeCommandSource.find(
            "recordDescriptorSetBind(\n    const VulkanRhiNativeResourcePayload& descriptorSet,\n    std::uint32_t setIndex")
        != std::pmr::string::npos);
    assert(nativeCommandSource.find("cmdBindDescriptorSets(\n        impl_->commandBuffer")
        != std::pmr::string::npos);
    assert(nativeCommandSource.find("setIndex,\n        1") != std::pmr::string::npos);
    assert(nativeCommandSource.find("bytesPerPixelForVulkanCopy") != std::pmr::string::npos);
    assert(nativeCommandSource.find("desc.bytesPerRow / texelBytes") != std::pmr::string::npos);
    assert(pipelineSource.find("VkVertexInputBindingDescription") != std::pmr::string::npos);
    assert(pipelineSource.find("VkVertexInputAttributeDescription") != std::pmr::string::npos);
    assert(pipelineSource.find("maxDescriptorSetIndex") != std::pmr::string::npos);
    assert(pipelineSource.find("for (std::uint32_t setIndex = 0") != std::pmr::string::npos);
    assert(pipelineSource.find("setSlot.set == setIndex") != std::pmr::string::npos);
}

void assertNativeDescriptorWritesTrackTextureViewsAndSamplers()
{
    const auto root = projectRoot();
    const auto nativeResources =
        readText(root / "src" / "rhi" / "backends" / "vulkan" / "vulkan_native_resources.h");
    const auto descriptorSource =
        readText(root / "src" / "rhi" / "backends" / "vulkan" / "vulkan_descriptors.cpp");
    const auto deviceSource =
        readText(root / "src" / "rhi" / "backends" / "vulkan" / "vulkan_device.cpp");

    assert(nativeResources.find("backendVulkanSamplerHandle") != std::pmr::string::npos);
    assert(nativeResources.find("backendVulkanTextureViewDesc") != std::pmr::string::npos);
    assert(descriptorSource.find("DescriptorResourceKind::Sampler") != std::pmr::string::npos);
    assert(descriptorSource.find("backendVulkanSamplerHandle") != std::pmr::string::npos);
    assert(descriptorSource.find("VK_IMAGE_VIEW_TYPE_CUBE") != std::pmr::string::npos);
    assert(descriptorSource.find("backendVulkanTextureViewDesc") != std::pmr::string::npos);
    assert(descriptorSource.find("viewDesc.baseMipLevel") != std::pmr::string::npos);
    assert(descriptorSource.find("viewDesc.arrayLayerCount") != std::pmr::string::npos);
    assert(descriptorSource.find("VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL")
        != std::pmr::string::npos);
    assert(descriptorSource.find("native descriptor sampler writes are not implemented")
        == std::pmr::string::npos);
    assert(deviceSource.find("resourceRegistry_->payload(texture.id())") != std::pmr::string::npos);
    assert(deviceSource.find("NativeTextureViewPayload") != std::pmr::string::npos);
    assert(deviceSource.find("resourceRegistry_->registerResource(sampler->id()")
        != std::pmr::string::npos);
}

arp::rhi::semantics::vulkan::VulkanDeviceCapabilitySnapshot
makeBackendLocalVulkanCapabilitySnapshot()
{
    arp::rhi::semantics::vulkan::VulkanDeviceCapabilitySnapshot snapshot;
    snapshot.queriedFromNativeDevice = true;
    snapshot.queriedExtendedFeatures = true;
    snapshot.maxPushConstantSizeBytes = 256;
    snapshot.supportsDescriptorIndexing = true;
    snapshot.supportsTimelineSynchronization = true;
    snapshot.supportsSecondaryCommandBuffers = true;
    snapshot.queueFamilyCount = 2;
    snapshot.hasDeviceLocalMemory = true;
    snapshot.hasHostVisibleMemory = true;
    return snapshot;
}

arp::core::UniquePtr<arp::rhi::GpuDevice> createSyntheticVulkanDevice(
    const std::pmr::string& label, std::uint64_t memoryBudgetBytes)
{
    const auto snapshot = makeBackendLocalVulkanCapabilitySnapshot();
    arp::rhi_vulkan::VulkanRhiRuntimeDesc runtimeDesc;
    runtimeDesc.label = label + "Runtime";
    runtimeDesc.enableSyntheticRuntime = true;
    runtimeDesc.nativeHandlesPrivate = true;
    runtimeDesc.queueFamilyCount = snapshot.queueFamilyCount;
    runtimeDesc.hasDeviceLocalMemory = snapshot.hasDeviceLocalMemory;
    runtimeDesc.hasHostVisibleMemory = snapshot.hasHostVisibleMemory;
    const auto runtime = arp::rhi_vulkan::createVulkanRhiRuntime(runtimeDesc);
    assert(runtime);

    arp::rhi_vulkan::VulkanRhiDeviceDesc desc;
    desc.label = label;
    desc.capabilityContract =
        arp::rhi::semantics::vulkan::buildVulkanNativeCapabilityContract(snapshot);
    desc.memoryBudgetBytes = memoryBudgetBytes;
    desc.enableVmaAllocator = true;
    desc.runtime = runtime;
    desc.requireNativeResourceAllocation = true;
    // This synthetic fixture uploads 700 KiB at a time and verifies reuse of
    // one 1 MiB page. Declare its budget rather than relying on device defaults.
    desc.uploadStagingPageSizeBytes = 1024u * 1024u;
    desc.uploadStagingMaxPageCount = 1u;

    const auto report = arp::rhi_vulkan::describeVulkanRhiDevice(desc);
    assert(report.success);
    return arp::rhi_vulkan::createVulkanRhiDevice(desc);
}

void assertSyntheticVulkanResourceDestructionReleasesBudget()
{
    auto device = createSyntheticVulkanDevice("RHIVulkanResourceLifetime", 512u * 1024u);
    assert(device);

    for (int frame = 0; frame < 4; ++frame) {
        {
            auto transientTexture = device->createTexture({
                "FrameTransientColor",
                {256, 256, 1},
                arp::rhi::TextureFormat::Rgba8Unorm,
                arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled,
                arp::rhi::MemoryUsage::Transient,
                arp::rhi::ResourceState::Undefined,
            });
            auto transientBuffer = device->createBuffer({
                "FrameTransientStorage",
                16u * 1024u,
                arp::rhi::ResourceUsage::StorageBuffer,
                arp::rhi::MemoryUsage::Transient,
                arp::rhi::ResourceState::Undefined,
            });
            assert(transientTexture);
            assert(transientBuffer);

            const auto liveStats = device->memoryStats();
            assert(liveStats.allocatedBytes >= ((256u * 256u * 4u) + (16u * 1024u)));
            assert(liveStats.liveResourceCount == 2u);
        }

        const auto releasedStats = device->memoryStats();
        assert(releasedStats.allocatedBytes == 0u);
        assert(releasedStats.liveResourceCount == 0u);
    }
}

void assertVulkanUploadStagingRetiresCompletedFrameSerials()
{
    auto device =
        createSyntheticVulkanDevice("RHIVulkanUploadStagingFrameSerials", 64u * 1024u * 1024u);
    assert(device);

    auto upload = device->createBuffer({
        "FrameUploadSource",
        700u * 1024u,
        arp::rhi::ResourceUsage::CopySrc,
        arp::rhi::MemoryUsage::CpuToGpu,
        arp::rhi::ResourceState::CopySrc,
    });
    auto destination = device->createBuffer({
        "FrameUploadDestination",
        700u * 1024u,
        arp::rhi::ResourceUsage::CopyDst,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::CopyDst,
    });
    assert(upload);
    assert(destination);

    for (std::uint64_t frameSerial = 1; frameSerial <= 9; ++frameSerial) {
        auto encoder = device->createCommandEncoder({"FrameSerialUpload"});
        assert(encoder);
        auto transfer = encoder->beginTransferPass({"FrameSerialUpload"});
        assert(transfer);
        const auto uploaded = transfer->uploadBuffer({
            "FrameSerialUpload",
            upload->id(),
            destination->id(),
            700u * 1024u,
            arp::rhi::StagingIntent::Upload,
            true,
            frameSerial,
        });
        assert(uploaded.success);
        assert(transfer->end().success);
        auto commands = encoder->finish();
        assert(commands);
        assert(device->queue().submit(*commands).success);
    }

    const auto nativeStats = arp::rhi_vulkan::inspectVulkanRhiNativeResourceStats(*device);
    assert(nativeStats.nativeUploadStagingPoolPageCount == 1u);
    assert(nativeStats.nativeUploadStagingPeakBytes <= 1024u * 1024u);
}

void assertVulkanCommandBufferRetainsStylizedRenderPassBudget()
{
    auto device =
        createSyntheticVulkanDevice("RHIVulkanStylizedRenderPassBudget", 64u * 1024u * 1024u);
    assert(device);

    auto color = device->createTexture({
        "StylizedGraphColor",
        {320u, 180u, 1u},
        arp::rhi::TextureFormat::Rgba8Unorm,
        arp::rhi::TextureUsage::RenderAttachment,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::ColorAttachment,
    });
    assert(color);

    auto encoder = device->createCommandEncoder({"StylizedGraphRenderPassBudget"});
    assert(encoder);

    constexpr std::uint32_t stylizedVisiblePassBudget = 12u;
    for (std::uint32_t index = 0; index < stylizedVisiblePassBudget; ++index) {
        arp::rhi::RenderPassDesc passDesc;
        passDesc.label = "StylizedGraphPass";
        passDesc.colorAttachments.push_back({
            "StylizedGraphColor",
            color->id(),
            color->desc().format,
            arp::rhi::LoadOp::Load,
            arp::rhi::StoreOp::Store,
        });
        auto pass = encoder->beginRenderPass(passDesc);
        assert(pass);
        const auto ended = pass->end();
        assert(ended.success);
    }

    auto commands = encoder->finish();
    assert(commands);
    assert(commands->commandCount() >= stylizedVisiblePassBudget * 2u);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--recorded-state-synthetic") {
        auto device = createSyntheticVulkanDevice("RecordedStateSynthetic", 1024u * 1024u);
        assert(device);
        arp::tests::checkRecordedResourceStates(*device);
        // Synthetic waitIdle has no native queue. Explicitly drain its already
        // complete metadata submissions before checking retained allocations.
        arp::rhi_vulkan::retireCompletedVulkanRhiSubmittedCommands();
        assert(device->memoryStats().allocatedBytes == 0u);
        return 0; // Synthetic backend metadata only; no native GPU claim.
    }
    if (argc == 2
        && (std::string_view(argv[1]) == "--resource-identity"
            || std::string_view(argv[1]) == "--descriptor-liveness")) {
        auto local = createSyntheticVulkanDevice("IdentityLocal", 1024u * 1024u);
        auto foreign = createSyntheticVulkanDevice("IdentityForeign", 1024u * 1024u);
        assert(local && foreign);
        arp::tests::verifyForeignDescriptorResources(*local, *foreign);
        if (std::string_view(argv[1]) == "--descriptor-liveness") {
            arp::tests::verifySampledDepthDescriptors(*local);
            verifyRecordedDescriptorStorage(*local);
            arp::tests::verifyDescriptorWrapperLiveness(*local);
            using namespace arp::rhi;
            DescriptorLayoutDesc layout;
            layout.name = "VulkanStaleBind";
            layout.bindings.push_back({0u, DescriptorType::UniformBuffer, ShaderStage::Compute});
            auto arena = local->createDescriptorArena({"VulkanStaleBind", 1u, 1u});
            auto set = arena->allocateDescriptorSet({"VulkanStaleBind", layout});
            auto buffer = local->createBuffer({"VulkanStaleBind",
                256u,
                ResourceUsage::UniformBuffer,
                MemoryUsage::CpuToGpu,
                ResourceState::ShaderRead});
            assert(set && buffer);
            assert(
                set->write({0u,
                               DescriptorType::UniformBuffer,
                               buffer->id(),
                               DescriptorResourceKind::Buffer})
                    .success);
            auto encoder = local->createCommandEncoder({"VulkanStaleBind"});
            auto pass = encoder->beginComputePass({"VulkanStaleBind"});
            assert(pass);
            const auto bound = pass->bindDescriptorSet(0u, *set);
            assert(bound.success);
            assert(
                set->write({0u,
                               DescriptorType::UniformBuffer,
                               buffer->id(),
                               DescriptorResourceKind::Buffer})
                    .success);
            const auto changedDispatch = pass->dispatch(1u, 1u, 1u);
            assert(!changedDispatch.success
                && changedDispatch.recordedCommandCount == bound.recordedCommandCount);
            assert(changedDispatch.diagnostics.find("RHI-DESCRIPTOR-CONTENT-CHANGED") == 0u);
            const auto rebound = pass->bindDescriptorSet(0u, *set);
            assert(rebound.success);
            const auto count = rebound.recordedCommandCount;
            buffer.reset();
            // Actual backend entry, explicitly synthetic command payload. This
            // negative case must reject before native binding/pipeline access.
            const auto rejected = pass->bindDescriptorSet(0u, *set);
            assert(!rejected.success && rejected.recordedCommandCount == count);
            assert(rejected.diagnostics.find("RHI-DESCRIPTOR-RESOURCE-NOT-LIVE") == 0u);
            const auto staleDispatch = pass->dispatch(1u, 1u, 1u);
            assert(!staleDispatch.success && staleDispatch.recordedCommandCount == count);
            assert(staleDispatch.diagnostics.find("RHI-DESCRIPTOR-RESOURCE-NOT-LIVE") == 0u);
            set.reset();
            const auto destroyedSetDispatch = pass->dispatch(1u, 1u, 1u);
            assert(!destroyedSetDispatch.success
                && destroyedSetDispatch.recordedCommandCount == count);
            assert(destroyedSetDispatch.diagnostics.find("RHI-DESCRIPTOR-OBJECT-NOT-LIVE") == 0u);
            assert(pass->end().success);
            assert(encoder->finish());
            std::puts("DescriptorResourceLiveness/1 Vulkan synthetic metadata: PASS");
        }
        std::puts("ResourceIdentity/1 Vulkan synthetic metadata (no native GPU): PASS");
        return 0;
    }
    assertNativeResourcesUseVma();
    assertNativeDescriptorWritesTrackTextureViewsAndSamplers();
    assertSyntheticVulkanResourceDestructionReleasesBudget();
    assertVulkanUploadStagingRetiresCompletedFrameSerials();
    assertVulkanCommandBufferRetainsStylizedRenderPassBudget();

    // Synthetic native metadata is a registry contract check, not GPU evidence.
    {
        auto identityLocal = createSyntheticVulkanDevice("IdentityLocal", 1024u * 1024u);
        auto identityForeign = createSyntheticVulkanDevice("IdentityForeign", 1024u * 1024u);
        assert(identityLocal && identityForeign);
        arp::tests::verifyForeignDescriptorResources(*identityLocal, *identityForeign);
    }

    const auto snapshot = makeBackendLocalVulkanCapabilitySnapshot();
    arp::rhi_vulkan::VulkanRhiRuntimeDesc runtimeDesc;
    runtimeDesc.label = "RHIVulkanNativeResourcesRuntime";
    runtimeDesc.enableSyntheticRuntime = true;
    runtimeDesc.nativeHandlesPrivate = true;
    runtimeDesc.queueFamilyCount = snapshot.queueFamilyCount;
    runtimeDesc.hasDeviceLocalMemory = snapshot.hasDeviceLocalMemory;
    runtimeDesc.hasHostVisibleMemory = snapshot.hasHostVisibleMemory;
    const auto runtime = arp::rhi_vulkan::createVulkanRhiRuntime(runtimeDesc);
    assert(runtime);

    arp::rhi_vulkan::VulkanRhiDeviceDesc desc;
    desc.label = "RHIVulkanNativeResources";
    desc.capabilityContract =
        arp::rhi::semantics::vulkan::buildVulkanNativeCapabilityContract(snapshot);
    desc.memoryBudgetBytes = 64u * 1024u * 1024u;
    desc.enableVmaAllocator = true;
    desc.runtime = runtime;
    desc.requireNativeResourceAllocation = true;

    const auto report = arp::rhi_vulkan::describeVulkanRhiDevice(desc);
    assert(report.success);
    assert(report.nativeResourceAllocationAvailable);
    assert(!report.nativeHandleExposedToUpperLayer);

    auto device = arp::rhi_vulkan::createVulkanRhiDevice(desc);
    assert(device);

    auto buffer = device->createBuffer({
        "NativeVertexUpload",
        4096,
        arp::rhi::ResourceUsage::VertexBuffer | arp::rhi::ResourceUsage::CopySrc
            | arp::rhi::ResourceUsage::CopyDst,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
    });
    auto uploadBuffer = device->createBuffer({
        "NativeTransferUpload",
        4096,
        arp::rhi::ResourceUsage::CopySrc | arp::rhi::ResourceUsage::CopyDst,
        arp::rhi::MemoryUsage::CpuToGpu,
        arp::rhi::ResourceState::Undefined,
    });
    auto readbackBuffer = device->createBuffer({
        "NativeTransferReadback",
        4096,
        arp::rhi::ResourceUsage::CopySrc | arp::rhi::ResourceUsage::CopyDst,
        arp::rhi::MemoryUsage::GpuToCpu,
        arp::rhi::ResourceState::Undefined,
    });
    auto texture = device->createTexture({
        "NativeColorTarget",
        {64, 64, 1},
        arp::rhi::TextureFormat::Rgba8Unorm,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Storage
            | arp::rhi::TextureUsage::CopySrc | arp::rhi::TextureUsage::CopyDst,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
    });
    assert(buffer);
    assert(uploadBuffer);
    assert(readbackBuffer);
    assert(texture);
    assert(!buffer->allocation().nativeHandleExposed);
    assert(!uploadBuffer->allocation().nativeHandleExposed);
    assert(!readbackBuffer->allocation().nativeHandleExposed);
    assert(!texture->allocation().nativeHandleExposed);
    assert(buffer->allocation().backendFallbackHiddenFromUpperLayer);
    assert(uploadBuffer->allocation().backendFallbackHiddenFromUpperLayer);
    assert(readbackBuffer->allocation().backendFallbackHiddenFromUpperLayer);
    assert(texture->allocation().backendFallbackHiddenFromUpperLayer);

    auto arena = device->createDescriptorArena({
        "NativeDescriptorArena",
        2,
        8,
    });
    assert(arena);
    arp::rhi::DescriptorLayoutDesc descriptorLayout;
    descriptorLayout.name = "NativeResourceSet";
    descriptorLayout.bindings.push_back({
        0,
        arp::rhi::DescriptorType::UniformBuffer,
        arp::rhi::ShaderStage::Vertex,
        1,
        false,
        false,
    });
    auto descriptorSet = arena->allocateDescriptorSet({
        "NativeResourceSet0",
        descriptorLayout,
    });
    assert(descriptorSet);
    assert(descriptorSet->allocation().success);
    assert(!descriptorSet->stats().nativeHandleExposedToUpperLayer);

    auto renderPipelineLayout = device->createPipelineLayout({});
    assert(renderPipelineLayout);
    arp::rhi::ShaderPackageDesc clearShader;
    clearShader.label = "NativeClear";
    clearShader.slangModulePath = "shaders/samples/clear.slang";
    clearShader.artifacts.spirvPath = "shaders/clear.spv";
    clearShader.artifacts.mslPath = "shaders/clear.metal";
    clearShader.artifacts.reflectionPath = "shaders/clear.reflection.yaml";
    clearShader.entrypoints.push_back({
        clearShader.slangModulePath,
        "vertexMain",
        arp::rhi::ShaderStage::Vertex,
        arp::rhi::ShaderSourceLanguage::Slang,
    });
    clearShader.entrypoints.push_back({
        clearShader.slangModulePath,
        "fragmentMain",
        arp::rhi::ShaderStage::Fragment,
        arp::rhi::ShaderSourceLanguage::Slang,
    });
    arp::rhi::RenderPipelineDesc renderPipelineDesc;
    renderPipelineDesc.label = "NativeClearPipeline";
    renderPipelineDesc.layout = renderPipelineLayout.get();
    renderPipelineDesc.shaderPackage = &clearShader;
    renderPipelineDesc.vertexShader = clearShader.entrypoints[0];
    renderPipelineDesc.fragmentShader = clearShader.entrypoints[1];
    renderPipelineDesc.colorFormat = arp::rhi::TextureFormat::Rgba8Unorm;
    renderPipelineDesc.hasViewport = true;
    renderPipelineDesc.viewport = {0.0f, 0.0f, 320.0f, 180.0f, 0.0f, 1.0f};
    renderPipelineDesc.hasScissor = true;
    renderPipelineDesc.scissor = {0, 0, 320, 180};
    auto renderPipeline = device->createRenderPipeline(renderPipelineDesc);
    assert(renderPipeline);
    assert(!renderPipeline->nativeHandleExposedToUpperLayer());

    arp::rhi::DescriptorLayoutDesc computeDescriptorLayout;
    computeDescriptorLayout.name = "GiProbeSet";
    computeDescriptorLayout.bindings.push_back({
        0,
        arp::rhi::DescriptorType::StorageBuffer,
        arp::rhi::ShaderStage::Compute,
        1,
        false,
        false,
    });
    computeDescriptorLayout.bindings.push_back({
        1,
        arp::rhi::DescriptorType::StorageTexture,
        arp::rhi::ShaderStage::Compute,
        1,
        false,
        false,
    });
    arp::rhi::PipelineLayoutDesc computePipelineLayoutDesc;
    computePipelineLayoutDesc.descriptorLayouts.push_back(computeDescriptorLayout);
    computePipelineLayoutDesc.descriptorSets.push_back({0, "GiProbeSet"});
    auto computePipelineLayout = device->createPipelineLayout(computePipelineLayoutDesc);
    assert(computePipelineLayout);
    arp::rhi::ShaderPackageDesc computeShader;
    computeShader.label = "NativeGiProbeUpdate";
    computeShader.slangModulePath = "shaders/stylized/global_illumination.slang";
    computeShader.artifacts.spirvPath = "shaders/global_illumination.spv";
    computeShader.artifacts.mslPath = "shaders/global_illumination.metal";
    computeShader.artifacts.reflectionPath = "shaders/global_illumination.reflection.yaml";
    computeShader.entrypoints.push_back({
        computeShader.slangModulePath,
        "GiProbeUpdate",
        arp::rhi::ShaderStage::Compute,
        arp::rhi::ShaderSourceLanguage::Slang,
    });
    computeShader.reflection.descriptorBindings.push_back({
        0,
        0,
        arp::rhi::DescriptorType::StorageBuffer,
        arp::rhi::ShaderStage::Compute,
        1,
    });
    computeShader.reflection.descriptorBindings.push_back({
        0,
        1,
        arp::rhi::DescriptorType::StorageTexture,
        arp::rhi::ShaderStage::Compute,
        1,
    });
    arp::rhi::ComputePipelineDesc computePipelineDesc;
    computePipelineDesc.label = "NativeGiProbeUpdatePipeline";
    computePipelineDesc.layout = computePipelineLayout.get();
    computePipelineDesc.shaderPackage = &computeShader;
    computePipelineDesc.computeShader = computeShader.entrypoints[0];
    auto computePipeline = device->createComputePipeline(computePipelineDesc);
    assert(computePipeline);
    assert(!computePipeline->nativeHandleExposedToUpperLayer());
    auto computeDescriptorSet = arena->allocateDescriptorSet({
        "NativeGiProbeSet0",
        computeDescriptorLayout,
    });
    assert(computeDescriptorSet);
    assert(computeDescriptorSet
            ->write({
                0,
                arp::rhi::DescriptorType::StorageBuffer,
                buffer->id(),
                arp::rhi::DescriptorResourceKind::Buffer,
            })
            .success);
    assert(computeDescriptorSet
            ->write({
                1,
                arp::rhi::DescriptorType::StorageTexture,
                texture->id(),
                arp::rhi::DescriptorResourceKind::TextureView,
            })
            .success);

    auto encoder = device->createCommandEncoder({"NativeCommandFrame"});
    assert(encoder);
    auto bufferBarrier = encoder->transition(
        *buffer, arp::rhi::ResourceState::Undefined, arp::rhi::ResourceState::CopySrc);
    assert(bufferBarrier.success);
    auto uploadBarrier = encoder->transition(
        *uploadBuffer, arp::rhi::ResourceState::Undefined, arp::rhi::ResourceState::CopySrc);
    assert(uploadBarrier.success);
    auto readbackBarrier = encoder->transition(
        *readbackBuffer, arp::rhi::ResourceState::Undefined, arp::rhi::ResourceState::CopyDst);
    assert(readbackBarrier.success);
    auto textureBarrier = encoder->transition(
        *texture, arp::rhi::ResourceState::Undefined, arp::rhi::ResourceState::CopyDst);
    assert(textureBarrier.success);
    auto transferPass = encoder->beginTransferPass({"NativeTransferPass"});
    assert(transferPass);
    assert(transferPass
            ->copyBufferToBuffer({
                uploadBuffer->id(),
                readbackBuffer->id(),
                0,
                128,
                128,
            })
            .success);
    assert(!transferPass
            ->copyBufferToTexture({
                uploadBuffer->id(),
                texture->id(),
                0,
                128,
                64,
                {},
                {64, 64, 1},
            })
            .success);
    assert(transferPass
            ->copyBufferToTexture({
                uploadBuffer->id(),
                texture->id(),
                0,
                256,
                64,
                {},
                {64, 64, 1},
            })
            .success);
    assert(transferPass
            ->copyTextureToBuffer({
                texture->id(),
                readbackBuffer->id(),
                {},
                0,
                256,
                64,
                {64, 64, 1},
            })
            .success);
    assert(transferPass
            ->uploadBuffer({
                "NativeUpload",
                uploadBuffer->id(),
                buffer->id(),
                256,
                arp::rhi::StagingIntent::Upload,
                true,
            })
            .success);
    assert(transferPass
            ->readbackTexture({
                "NativeReadback",
                texture->id(),
                readbackBuffer->id(),
                256,
                arp::rhi::StagingIntent::Readback,
                true,
            })
            .success);
    assert(transferPass->end().success);
    textureBarrier = encoder->transition(
        *texture, arp::rhi::ResourceState::CopyDst, arp::rhi::ResourceState::ColorAttachment);
    assert(textureBarrier.success);
    auto renderPass = encoder->beginRenderPass({
        "NativeColorTarget",
        "NativeRenderPass",
        {
            {
                "NativeColorAttachment",
                texture->id(),
                arp::rhi::TextureFormat::Rgba8Unorm,
                arp::rhi::LoadOp::Clear,
                arp::rhi::StoreOp::Store,
            },
        },
    });
    assert(renderPass);
    assert(renderPass->setPipeline(*renderPipeline).success);
    assert(renderPass->drawIndexed(3, 1, 0, 0, 0).success);
    assert(renderPass->end().success);
    auto computePass = encoder->beginComputePass({"NativeComputePass"});
    assert(computePass);
    assert(computePass->setPipeline(*computePipeline).success);
    assert(computePass->bindDescriptorSet(0, *computeDescriptorSet).success);
    assert(computePass->dispatch(1, 1, 1).success);
    assert(computePass->end().success);
    auto commandBuffer = encoder->finish();
    assert(commandBuffer);
    assert(commandBuffer->commandCount() >= 16u);
    const auto submitted = device->queue().submit(*commandBuffer);
    assert(submitted.success);
    assert(submitted.submittedCommandCount == commandBuffer->commandCount());

    auto swapchain = device->createSwapchain({
        "NativeHiddenWindowSurface",
        {320, 180},
        arp::rhi::TextureFormat::Bgra8Unorm,
        2,
    });
    assert(swapchain);
    const auto frame = swapchain->acquireNextFrame();
    assert(frame.success);
    const auto presented = swapchain->present(frame);
    assert(presented.success);
    const auto resized = swapchain->resize({400, 240});
    assert(resized.success);
    const auto resizedFrame = swapchain->acquireNextFrame();
    assert(resizedFrame.success);
    const auto resizedPresented = swapchain->present(resizedFrame);
    assert(resizedPresented.success);

    const auto nativeStats = arp::rhi_vulkan::inspectVulkanRhiNativeResourceStats(*device);
    assert(nativeStats.runtimeConnected);
    assert(nativeStats.nativeBufferCreateCount == 3u);
    assert(nativeStats.nativeImageCreateCount == 1u);
    assert(nativeStats.nativeMemoryAllocationCount >= 2u);
    assert(nativeStats.nativeDescriptorSetLayoutCreateCount >= 1u);
    assert(nativeStats.nativeDescriptorPoolCreateCount == 2u);
    assert(nativeStats.nativeDescriptorSetAllocateCount == 2u);
    assert(nativeStats.nativeDescriptorWriteCount >= 2u);
    assert(nativeStats.nativeDescriptorBindRecordCount == 1u);
    assert(nativeStats.nativeShaderModuleCreateCount >= 2u);
    assert(nativeStats.nativePipelineLayoutCreateCount >= 2u);
    assert(nativeStats.nativeRenderPipelineCreateCount == 1u);
    assert(nativeStats.nativeComputePipelineCreateCount == 1u);
    assert(nativeStats.nativeCommandPoolCreateCount >= 1u);
    assert(nativeStats.nativeCommandBufferAllocateCount >= 1u);
    assert(nativeStats.nativeCommandBufferBeginCount >= 1u);
    assert(nativeStats.nativeCommandBufferRecordCount >= commandBuffer->commandCount());
    assert(nativeStats.nativeCommandBufferEndCount >= 1u);
    assert(nativeStats.nativeQueueSubmitCount == 1u);
    assert(nativeStats.nativeBufferBarrierRecordCount >= 3u);
    assert(nativeStats.nativeImageBarrierRecordCount >= 2u);
    assert(nativeStats.nativeBufferCopyRecordCount >= 2u);
    assert(nativeStats.nativeUploadStagingSliceCount >= 1u);
    assert(nativeStats.nativeUploadStagingPoolPageCount >= 1u);
    assert(nativeStats.nativeUploadStagingPeakBytes >= 256u);
    assert(nativeStats.nativeUploadStagingPoolBounded);
    assert(nativeStats.nativeUploadStagingHandlesHidden);
    assert(nativeStats.nativeBufferToImageCopyRecordCount == 1u);
    assert(nativeStats.nativeImageToBufferCopyRecordCount >= 2u);
    assert(nativeStats.nativeRenderAttachmentViewCreateCount == 1u);
    assert(nativeStats.nativeFramebufferCreateCount == 1u);
    assert(nativeStats.nativeRenderPassBeginRecordCount == 1u);
    assert(nativeStats.nativeGraphicsPipelineBindRecordCount == 1u);
    assert(nativeStats.nativeIndexedDrawRecordCount == 1u);
    assert(nativeStats.nativeRenderPassEndRecordCount == 1u);
    assert(nativeStats.nativeComputePipelineBindRecordCount == 1u);
    assert(nativeStats.backendDispatchRecordCount == 1u);
    assert(nativeStats.nativeExecutableDispatchRecordCount == 1u);
    assert(nativeStats.nativeSwapchainCreateCount >= 2u);
    assert(nativeStats.nativeSwapchainImageCount >= 2u);
    assert(nativeStats.nativeSwapchainAcquireCount == 2u);
    assert(nativeStats.nativeSwapchainPresentCount == 2u);
    assert(nativeStats.nativeSwapchainResizeCount == 1u);
    assert(nativeStats.backendSwapchainAcquireCount == 2u);
    assert(nativeStats.backendSwapchainPresentCount == 2u);
    assert(!nativeStats.hiddenWindowRenderOutputHashCaptured);
    assert(std::string_view(nativeStats.renderOutputEvidenceDiagnostic)
        == "RHI-OUTPUT-ORIGINAL-OBSERVATION-UNAVAILABLE");
    assert(nativeStats.renderOutputHashCaptureCount == 0u);
    assert(nativeStats.deterministicRenderOutputHash == 0u);
    assert(!nativeStats.renderOutputHashIncludesNativePresent);
    assert(nativeStats.nativeHandlesHiddenFromPublicApi);

    return 0;
}

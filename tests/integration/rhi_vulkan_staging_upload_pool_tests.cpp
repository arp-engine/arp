#include "core/memory/cpu_allocator.h"
#include "rhi/backends/vulkan/vulkan_allocator.h"
#include "rhi/backends/vulkan/vulkan_device.h"
#include "rhi/backends/vulkan/vulkan_native_commands.h"
#include "rhi/backends/vulkan/vulkan_runtime.h"
#include "rhi/backends/vulkan/vulkan_staging_upload_pool.h"
#include "rhi/semantics/vulkan_device_capability_query.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

namespace {

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

arp::core::UniquePtr<arp::rhi::GpuDevice> makeSyntheticVulkanDevice()
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

    arp::rhi_vulkan::VulkanRhiRuntimeDesc runtimeDesc;
    runtimeDesc.label = "SyntheticUploadRetirementRuntime";
    runtimeDesc.enableSyntheticRuntime = true;
    runtimeDesc.nativeHandlesPrivate = true;
    runtimeDesc.queueFamilyCount = snapshot.queueFamilyCount;
    runtimeDesc.hasDeviceLocalMemory = snapshot.hasDeviceLocalMemory;
    runtimeDesc.hasHostVisibleMemory = snapshot.hasHostVisibleMemory;
    const auto runtime = arp::rhi_vulkan::createVulkanRhiRuntime(runtimeDesc);
    assert(runtime);

    arp::rhi_vulkan::VulkanRhiDeviceDesc desc;
    desc.label = "SyntheticUploadRetirementDevice";
    desc.capabilityContract =
        arp::rhi::semantics::vulkan::buildVulkanNativeCapabilityContract(snapshot);
    desc.memoryBudgetBytes = 64u * 1024u * 1024u;
    desc.runtime = runtime;
    desc.requireNativeResourceAllocation = true;
    desc.allowNativeHandleExposureToUpperLayers = false;
    desc.allowBlockingFrameWaits = false;
    desc.allowPerDrawPipelineCreation = false;
    desc.allowPerFrameShaderCompilation = false;
    return arp::rhi_vulkan::createVulkanRhiDevice(desc);
}

void assertPoolIsBackendPrivateAndVmaBacked()
{
    const auto root = projectRoot();
    const auto header =
        readText(root / "src" / "rhi" / "backends" / "vulkan" / "vulkan_staging_upload_pool.h");
    const auto source =
        readText(root / "src" / "rhi" / "backends" / "vulkan" / "vulkan_staging_upload_pool.cpp");
    const auto publicCommands = readText(root / "src" / "rhi" / "gpu_commands.h");

    assert(header.find("VmaAllocator") == std::pmr::string::npos);
    assert(header.find("VmaAllocation") == std::pmr::string::npos);
    assert(publicCommands.find("VulkanRhiStagingUploadPool") == std::pmr::string::npos);
    assert(source.find("createBuffer") != std::pmr::string::npos);
    assert(source.find("MemoryUsage::CpuToGpu") != std::pmr::string::npos);
    assert(source.find("ResourceUsage::CopySrc") != std::pmr::string::npos);
    assert(source.find("vkMapMemory") == std::pmr::string::npos);
    assert(source.find("vkUnmapMemory") == std::pmr::string::npos);
    assert(source.find("vkAllocateMemory") == std::pmr::string::npos);
}

void assertValidatorPoolSuballocatesAndRetiresExactSlices()
{
    using namespace arp::rhi;
    using namespace arp::rhi_vulkan;

    VulkanRhiAllocatorDesc allocatorDesc;
    allocatorDesc.label = "SyntheticVulkanAllocator";
    allocatorDesc.memoryBudgetBytes = 64u * 1024u;
    allocatorDesc.enableVmaAllocator = true;
    allocatorDesc.exposeNativeAllocationsToUpperLayer = false;

    VulkanRhiRuntimeDesc runtimeDesc;
    runtimeDesc.label = "SyntheticUploadPoolRuntime";
    runtimeDesc.enableSyntheticRuntime = true;
    runtimeDesc.nativeHandlesPrivate = true;
    const auto runtime = createVulkanRhiRuntime(runtimeDesc);
    assert(runtime);

    VulkanRhiAllocator allocator(allocatorDesc, runtime);

    VulkanRhiStagingUploadPoolDesc poolDesc;
    poolDesc.label = "UploadPool";
    poolDesc.pageSizeBytes = 4096;
    poolDesc.maxPageCount = 2;
    poolDesc.alignmentBytes = 256;

    VulkanRhiStagingUploadPool pool(poolDesc, allocator);

    auto first = pool.allocate({"first", 96, 256, 1});
    assert(first.success);
    assert(first.offsetBytes == 0);
    assert(first.sizeBytes == 96);
    assert(!first.nativeHandleExposedToUpperLayer);

    auto second = pool.allocate({"second", 256, 256, 1});
    assert(second.success);
    assert(second.offsetBytes == 256);

    const auto beforeRetire = pool.stats();
    assert(beforeRetire.liveSliceCount == 2);
    assert(beforeRetire.allocatedPageCount == 1);
    assert(beforeRetire.peakAllocatedBytes >= 512);
    assert(beforeRetire.boundedCapacity);
    assert(beforeRetire.nativeHandlesHiddenFromUpperLayer);

    RecordedStagingUploadJournal completion;
    assert(pool.enlist(
        completion, {first.pageIndex, first.offsetBytes, first.sizeBytes, first.frameSerial}));
    assert(pool.enlist(
        completion, {second.pageIndex, second.offsetBytes, second.sizeBytes, second.frameSerial}));
    assert(completion.retireCompleted());
    const auto afterRetire = pool.stats();
    assert(afterRetire.liveSliceCount == 0);
    assert(afterRetire.retiredSliceCount == 2);

    auto oversized = pool.allocate({"oversized", 8192, 256, 2});
    assert(!oversized.success);
    assert(oversized.diagnostics.find("capacity") != std::pmr::string::npos);
}

void assertPoolCancelsExactUnacceptedSlice()
{
    using namespace arp::rhi_vulkan;

    VulkanRhiRuntimeDesc runtimeDesc;
    runtimeDesc.label = "ExactCancelRuntime";
    runtimeDesc.enableSyntheticRuntime = true;
    runtimeDesc.nativeHandlesPrivate = true;
    const auto runtime = createVulkanRhiRuntime(runtimeDesc);
    assert(runtime);

    VulkanRhiAllocatorDesc allocatorDesc;
    allocatorDesc.label = "ExactCancelAllocator";
    allocatorDesc.memoryBudgetBytes = 64u * 1024u;
    allocatorDesc.enableVmaAllocator = true;
    VulkanRhiAllocator allocator(allocatorDesc, runtime);
    VulkanRhiStagingUploadPool pool({"VulkanExactCancel", 1024u, 1u, 256u}, allocator);

    const auto first = pool.allocate({"first", 96u, 256u, 1u});
    const auto second = pool.allocate({"second", 256u, 256u, 2u});
    assert(first.success && first.offsetBytes == 0u);
    assert(second.success && second.offsetBytes == 256u);
    assert(pool.cancel(first));
    assert(pool.stats().liveSliceCount == 1u);

    const auto third = pool.allocate({"third", 256u, 256u, 3u});
    assert(third.success && third.offsetBytes == 512u);
    assert(pool.cancel(third));
    assert(pool.cancel(second));
    assert(pool.stats().liveSliceCount == 0u);
}

void assertIncompleteCommandDoesNotReleaseAnotherCommandsSlice()
{
    using namespace arp::rhi;
    using namespace arp::rhi_vulkan;

    VulkanRhiAllocatorDesc allocatorDesc;
    allocatorDesc.label = "DistinctCommandRetirementAllocator";
    allocatorDesc.memoryBudgetBytes = 64u * 1024u;
    allocatorDesc.enableVmaAllocator = true;

    VulkanRhiRuntimeDesc runtimeDesc;
    runtimeDesc.label = "DistinctCommandRetirementRuntime";
    runtimeDesc.enableSyntheticRuntime = true;
    runtimeDesc.nativeHandlesPrivate = true;
    const auto runtime = createVulkanRhiRuntime(runtimeDesc);
    assert(runtime);

    VulkanRhiAllocator allocator(allocatorDesc, runtime);
    VulkanRhiStagingUploadPool pool({"DistinctCommandRetirementPool", 1024u, 1u, 256u}, allocator);

    const auto firstCommand = pool.allocate({"first-command", 256u, 256u, 101u});
    const auto secondCommand = pool.allocate({"second-command", 256u, 256u, 102u});
    assert(firstCommand.success && firstCommand.offsetBytes == 0u);
    assert(secondCommand.success && secondCommand.offsetBytes == 256u);

    RecordedStagingUploadJournal completion;
    assert(pool.enlist(completion,
        {firstCommand.pageIndex,
            firstCommand.offsetBytes,
            firstCommand.sizeBytes,
            firstCommand.frameSerial}));
    assert(completion.retireCompleted());
    const auto afterFirstCompletion = pool.stats();
    assert(afterFirstCompletion.liveSliceCount == 1u);

    const auto thirdCommand = pool.allocate({"third-command", 256u, 256u, 103u});
    assert(thirdCommand.success && thirdCommand.offsetBytes == 512u);
}

void assertCommandEncoderOwnsStagingRetirementSerial()
{
    const auto root = projectRoot();
    const auto commandObjects =
        readText(root / "src" / "rhi" / "backends" / "vulkan" / "vulkan_command_objects.cpp");

    assert(commandObjects.find("const auto stagingRetirementSerial = currentFrameSerial_")
        != std::pmr::string::npos);
    assert(commandObjects.find("retainRecordedStagingUploads(stagingUploads_")
        != std::pmr::string::npos);
    assert(
        commandObjects.find("completedVulkanRhiSubmittedFrameSerial()") == std::pmr::string::npos);
    assert(commandObjects.find("desc.frameSerial != 0") == std::pmr::string::npos);
}

void assertNativeSwapchainSlotsRetireAfterSubmittedFrameCompletion()
{
    const auto root = projectRoot();
    const auto swapchainSource =
        readText(root / "src" / "rhi" / "backends" / "vulkan" / "vulkan_swapchain.cpp");
    const auto nativeResourceHeader =
        readText(root / "src" / "rhi" / "backends" / "vulkan" / "vulkan_native_resources.h");
    const auto nativeGraphicsSource = readText(
        root / "src" / "rhi" / "backends" / "vulkan" / "vulkan_native_graphics_commands.cpp");

    assert(swapchainSource.find("#include \"rhi/backends/vulkan/vulkan_native_commands.h\"")
        != std::pmr::string::npos);
    assert(swapchainSource.find("retireCompletedNativeFrameSlots()") != std::pmr::string::npos);
    assert(
        swapchainSource.find("completedVulkanRhiSubmittedFrameSerial()") == std::pmr::string::npos);
    assert(swapchainSource.find("pollVulkanLegacyPresentRetirement") != std::pmr::string::npos);
    assert(
        swapchainSource.find("markFrameSlotPresented(frame.frameIndex)") != std::pmr::string::npos);
    assert(swapchainSource.find("backendVulkanSwapchainFrameSerial() const override")
        != std::pmr::string::npos);
    assert(nativeResourceHeader.find("backendVulkanSwapchainFrameSerial() const")
        != std::pmr::string::npos);
    assert(nativeGraphicsSource.find("colorTarget.backendVulkanSwapchainFrameSerial()")
        != std::pmr::string::npos);
}

void assertValidatorUploadsDoNotPublishNativeFrameSerial()
{
    using namespace arp::rhi;

    auto device = makeSyntheticVulkanDevice();
    assert(device);
    auto upload = device->createBuffer({
        "SyntheticUploadSource",
        256u,
        ResourceUsage::CopySrc,
        MemoryUsage::CpuToGpu,
        ResourceState::CopySrc,
    });
    auto destination = device->createBuffer({
        "SyntheticUploadDestination",
        256u,
        ResourceUsage::CopyDst,
        MemoryUsage::GpuOnly,
        ResourceState::CopyDst,
    });
    assert(upload && destination);

    auto encoder = device->createCommandEncoder({"SyntheticUploadRetirement"});
    assert(encoder);
    auto transfer = encoder->beginTransferPass({"SyntheticUploadRetirement"});
    assert(transfer);
    const auto uploaded = transfer->uploadBuffer({
        "SyntheticUploadRetirement",
        upload->id(),
        destination->id(),
        256u,
        StagingIntent::Upload,
        true,
        77u,
    });
    assert(uploaded.success);
    assert(transfer->end().success);
    auto commands = encoder->finish();
    assert(commands);
    const auto submitted = device->queue().submit(*commands);
    assert(submitted.success);
    arp::rhi_vulkan::retireCompletedVulkanRhiSubmittedCommands();
    assert(arp::rhi_vulkan::completedVulkanRhiSubmittedFrameSerial() == 0u);
    assert(device->uploadStagingReadiness().completionEpoch > 0u);
}

} // namespace

int main()
{
    assertPoolIsBackendPrivateAndVmaBacked();
    assertValidatorPoolSuballocatesAndRetiresExactSlices();
    assertPoolCancelsExactUnacceptedSlice();
    assertIncompleteCommandDoesNotReleaseAnotherCommandsSlice();
    assertCommandEncoderOwnsStagingRetirementSerial();
    assertNativeSwapchainSlotsRetireAfterSubmittedFrameCompletion();
    assertValidatorUploadsDoNotPublishNativeFrameSerial();
    return 0;
}

#include "core/memory/cpu_allocator.h"
#include "rhi/backends/vulkan/vulkan_allocator.h"
#include "rhi/backends/vulkan/vulkan_device.h"
#include "rhi/backends/vulkan/vulkan_native_commands.h"
#include "rhi/backends/vulkan/vulkan_runtime.h"
#include "rhi/backends/vulkan/vulkan_staging_upload_pool.h"
#include "rhi/semantics/vulkan_device_capability_query.h"
#include "tests/fixtures/vulkan_heap_owner_fixture.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>

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

void assertExplicitRequestLimitAndAlignmentFloor()
{
    using namespace arp::rhi_vulkan;
    VulkanRhiRuntimeDesc runtimeDesc;
    runtimeDesc.enableSyntheticRuntime = true;
    const auto runtime = createVulkanRhiRuntime(runtimeDesc);
    assert(runtime);
    VulkanRhiAllocator allocator({"request policy", 65536u, true, false}, runtime);
    VulkanRhiStagingUploadPoolDesc desc{"request policy", 2048u, 1u, 512u};
    desc.maxRequestBytes = 512u;
    VulkanRhiStagingUploadPool pool(desc, allocator);
    const auto refused = pool.allocate({"one over", 513u, 1u, 1u});
    assert(!refused.success && refused.diagnosticId == "ARP-RHI-STAGING-REQUEST-LIMIT");
    assert(pool.stats().allocatedPageCount == 0u && pool.stats().liveSliceCount == 0u
        && pool.stats().failedAllocationCount == 1u);
    const auto invalid = pool.allocate({"invalid alignment", 1u, 3u, 1u});
    assert(!invalid.success && invalid.diagnosticId == "ARP-RHI-STAGING-ALIGNMENT-INVALID");
    assert(pool.stats().allocatedPageCount == 0u);
    const auto first = pool.allocate({"small", 1u, 1u, 1u});
    const auto second = pool.allocate({"exact", 512u, 1u, 1u});
    assert(first.success && first.offsetBytes == 0u);
    assert(second.success && second.offsetBytes == 512u);
    assert(pool.cancel(first) && pool.cancel(second));
}

void assertDeviceConsumesValidatedPolicy()
{
    using namespace arp::rhi_vulkan;
    using namespace arp::rhi::semantics;
    VulkanRhiDeviceDesc desc;
    desc.memoryBudgetBytes = 65536u;
    desc.capabilityContract = nativeBackendCapabilityContract(arp::rhi::Backend::Vulkan);
    const UploadStagingBudgetV1 policy{1u, 512u, 2048u, 2u, 4096u, 512u};
    for (unsigned field = 0; field < 7; ++field) {
        auto bad = policy;
        if (field == 0)
            bad.version = 999u;
        if (field == 1)
            bad.maxRequestBytes = 0u;
        if (field == 2)
            bad.pageSizeBytes = 511u;
        if (field == 3)
            bad.maxPageCount = 0u;
        if (field == 4)
            bad.totalCapacityBytes = 4095u;
        if (field == 5)
            bad.alignmentBytes = 3u;
        if (field == 6) {
            bad.pageSizeBytes = std::numeric_limits<std::uint64_t>::max();
            bad.totalCapacityBytes = 0u;
        }
        desc.uploadStagingBudget = bad;
        const auto expected = validateUploadStagingBudget(bad);
        const auto report = describeVulkanRhiDevice(desc);
        assert(!report.success && report.diagnostics == expected.diagnosticId);
        assert(!createVulkanRhiDevice(desc));
    }
    desc.uploadStagingBudget = policy;
    // Supplied policy overrides legacy backend defaults, even invalid legacy values.
    desc.uploadStagingPageSizeBytes = 0u;
    desc.uploadStagingMaxPageCount = 0u;
    assert(describeVulkanRhiDevice(desc).success);
    auto device = createVulkanRhiDevice(desc);
    assert(device);
    const auto observed =
        device->physicalBudgetLedger().category(PhysicalBudgetCategory::UploadStaging);
    assert(
        observed.observed() && observed.capacity == policy.totalCapacityBytes && !observed.current);
    desc.uploadStagingBudget.reset();
    desc.uploadStagingPageSizeBytes = std::numeric_limits<std::uint64_t>::max();
    desc.uploadStagingMaxPageCount = 2u;
    assert(!describeVulkanRhiDevice(desc).success && !createVulkanRhiDevice(desc));
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

int nativePolicy()
{
    using namespace arp;
    using namespace rhi;
    using namespace rhi::semantics;
    tests::VulkanHeadlessRuntime native;
    if (!native.runtime() || !native.runtime()->valid())
        return 77;
    // Two explicitly packed 256-byte rows fill the sole page exactly.
    constexpr std::uint32_t row = kTextureUploadRowPitchAlignment;
    constexpr std::uint32_t bytes = row * 2u;
    const UploadStagingBudgetV1 policy{1u, bytes, bytes, 1u, bytes, row};
    auto device = tests::VulkanHeapOwnerFixture::makeDevice(
        native.runtime(), false, 16u * 1024u * 1024u, nullptr, &policy);
    assert(device);
    const auto staging = [&]() {
        const auto report = device->physicalBudgetLedger();
        assert(report.valid());
        const auto result = report.category(PhysicalBudgetCategory::UploadStaging);
        assert(result.observed() && result.capacity == bytes
            && result.chargeRelation == PhysicalBudgetChargeRelation::OverlapsResourceAllocation);
        return result;
    };
    assert(!staging().current);
    TextureDesc image;
    image.label = "native policy upload";
    image.extent = {2u, 2u, 1u};
    image.format = TextureFormat::Rgba8Unorm;
    image.usage = TextureUsage::CopyDst | TextureUsage::CopySrc;
    auto texture = device->createTexture(image);
    auto second = device->createTexture(image);
    auto readback = device->createBuffer({"policy readback",
        bytes,
        ResourceUsage::CopyDst,
        MemoryUsage::GpuToCpu,
        ResourceState::CopyDst});
    assert(texture && second && readback);
    std::array<std::uint8_t, bytes + 1u> pixels{};
    for (std::uint32_t y = 0; y < 2u; ++y)
        for (std::uint32_t x = 0; x < 8u; ++x)
            pixels[y * row + x] = static_cast<std::uint8_t>(17u + y * 8u + x);
    UploadTextureDesc upload;
    upload.label = "native staging policy";
    upload.destinationTexture = texture->id();
    upload.extent = image.extent;
    upload.format = image.format;
    upload.bytesPerRow = row;
    upload.rowsPerImage = 2u;
    upload.pixelData = pixels.data();
    upload.pixelDataSize = bytes;
    {
        auto rejected = device->createCommandEncoder({"request one over"});
        assert(rejected);
        auto pass = rejected->beginTransferPass({"request one over"});
        auto over = upload;
        ++over.pixelDataSize;
        const auto denied = pass->uploadTexture(over);
        assert(!denied.success && denied.diagnosticId == "ARP-RHI-STAGING-REQUEST-LIMIT");
        assert(!staging().current && !staging().liveCount && staging().rejectionCount == 1u);
        assert(pass->end().success);
    }
    auto encoder = device->createCommandEncoder({"native policy copy"});
    assert(encoder
        && encoder->transition(*texture, ResourceState::Undefined, ResourceState::CopyDst).success);
    auto pass = encoder->beginTransferPass({"native policy copy"});
    assert(pass && pass->uploadTexture(upload).success && pass->end().success);
    pass.reset();
    assert(encoder->transition(*texture, ResourceState::CopyDst, ResourceState::CopySrc).success);
    pass = encoder->beginTransferPass({"native policy readback"});
    TextureToBufferCopyDesc copy;
    copy.sourceTexture = texture->id();
    copy.destinationBuffer = readback->id();
    copy.extent = image.extent;
    copy.bytesPerRow = row;
    copy.rowsPerImage = 2u;
    assert(pass && pass->copyTextureToBuffer(copy).success && pass->end().success);
    pass.reset();
    auto commands = encoder->finish();
    auto gate = device->createTimelineSemaphore(0u);
    auto fence = device->createFence();
    assert(commands && gate && fence);
    const auto epoch = device->uploadStagingReadiness().completionEpoch;
    assert(device->queue().submit(*commands, gate.get(), 1u, nullptr, 0u, fence.get()).success);
    const bool pending = !fence->isSignaled() && staging().liveCount == 1u;
    commands.reset();
    encoder.reset();
    auto blocked = device->createCommandEncoder({"in flight staging refusal"});
    assert(blocked);
    pass = blocked->beginTransferPass({"in flight staging refusal"});
    upload.destinationTexture = second->id();
    const auto denied = pass->uploadTexture(upload);
    const bool exhausted = !denied.success
        && denied.diagnosticId == "ARP-RHI-STAGING-PAGE-COUNT-EXCEEDED" && staging().liveCount == 1u
        && staging().current == bytes && device->uploadStagingReadiness().completionEpoch == epoch;
    assert(pass->end().success);
    pass.reset();
    blocked.reset();
    // Unblock before asserting in-flight observations, including the red path.
    gate->signal(1u);
    fence->wait();
    device->waitIdle();
    assert(pending && exhausted);
    assert(!staging().liveCount && staging().current == bytes && staging().retiredCount == 1u
        && device->uploadStagingReadiness().completionEpoch > epoch);
    core::pmr::String diagnostic;
    auto payload = rhi_vulkan::vulkanRhiNativePayload(*readback);
    assert(payload && payload->backendMappedData()
        && payload->backendInvalidateMappedRead(0u, bytes, diagnostic));
    const auto* actual = static_cast<const std::uint8_t*>(payload->backendMappedData());
    for (std::uint32_t y = 0; y < 2u; ++y)
        assert(std::memcmp(actual + y * row, pixels.data() + y * row, 8u) == 0);
    auto recovered = device->createCommandEncoder({"recovered staging"});
    assert(recovered
        && recovered->transition(*second, ResourceState::Undefined, ResourceState::CopyDst)
            .success);
    pass = recovered->beginTransferPass({"recovered staging"});
    assert(pass && pass->uploadTexture(upload).success && pass->end().success);
    assert(staging().liveCount == 1u && staging().peak == bytes);
    pass.reset();
    recovered.reset();
    assert(!staging().liveCount && staging().current == bytes);
    std::puts(
        "VulkanUploadStagingPolicy/1 native PASS: request refusal, exact upload/readback, pending-page refusal, exact retirement and recovery");
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--native-policy")
        return nativePolicy();
    assertPoolIsBackendPrivateAndVmaBacked();
    assertValidatorPoolSuballocatesAndRetiresExactSlices();
    assertPoolCancelsExactUnacceptedSlice();
    assertExplicitRequestLimitAndAlignmentFloor();
    assertDeviceConsumesValidatedPolicy();
    assertIncompleteCommandDoesNotReleaseAnotherCommandsSlice();
    assertCommandEncoderOwnsStagingRetirementSerial();
    assertNativeSwapchainSlotsRetireAfterSubmittedFrameCompletion();
    assertValidatorUploadsDoNotPublishNativeFrameSerial();
    return 0;
}

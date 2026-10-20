#include "../fixtures/cpp_source_evidence.h"
#include "../fixtures/webgpu_original_texture_upload_source_fixture.h"
#include "core/cpu_allocator.h"
#include "rhi/backends/webgpu/webgpu_device.h"
#include "rhi/backends/webgpu/webgpu_staging_upload_pool.h"
#include "rhi/frontend/memory_upload_staging_readiness.h"
#include "rhi/semantics/native_backend_capabilities.h"
#include "rhi/semantics/webgpu_runtime_capability_bridge.h"
#include "rhi/semantics/webgpu_runtime_profile.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

arp::tests::CppSourceEvidence readText(const std::filesystem::path& path)
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

void assertWebGpuUploadPoolIsBackendPrivate()
{
    const auto root = projectRoot();
    const auto headerPath =
        root / "src" / "rhi" / "backends" / "webgpu" / "webgpu_staging_upload_pool.h";
    const auto sourcePath =
        root / "src" / "rhi" / "backends" / "webgpu" / "webgpu_staging_upload_pool.cpp";
    assert(std::filesystem::exists(headerPath));
    assert(std::filesystem::exists(sourcePath));

    const auto publicCommands = readText(root / "src" / "rhi" / "gpu_commands.h");
    assert(publicCommands.find("WebGpuRhiStagingUploadPool") == std::pmr::string::npos);
    assert(publicCommands.find("WGPUBuffer") == std::pmr::string::npos);

    const auto header = readText(headerPath);
    const auto source = readText(sourcePath);
    assert(header.find("WGPUBuffer") == std::pmr::string::npos);
    assert(header.find("nativeHandleExposedToUpperLayer") != std::pmr::string::npos);
    assert(source.find("wgpuQueueWriteBuffer") != std::pmr::string::npos
        || source.find("queue.writeBuffer") != std::pmr::string::npos
        || source.find("backend-owned WebGPU upload ring") != std::pmr::string::npos);
}

void assertWebGpuPoolSuballocatesAndRetiresByCompletionSerial()
{
    using namespace arp::rhi_webgpu;

    WebGpuRhiStagingUploadPoolDesc poolDesc;
    poolDesc.label = "WebGpuUploadPool";
    poolDesc.byteCapacity = 1024;
    poolDesc.alignmentBytes = 256;
    poolDesc.preferQueueWrite = true;

    WebGpuRhiStagingUploadPool pool(poolDesc);

    auto first = pool.allocate({"first", 96, 256, 1});
    assert(first.success);
    assert(first.offsetBytes == 0);
    assert(first.sizeBytes == 96);
    assert(!first.nativeHandleExposedToUpperLayer);

    auto second = pool.allocate({"second", 256, 256, 1});
    assert(second.success);
    assert(second.offsetBytes == 256);

    const auto beforeRetire = pool.stats();
    assert(beforeRetire.writeCount == 2u);
    assert(beforeRetire.liveSliceCount == 2u);
    assert(beforeRetire.liveBytes >= 512u);
    assert(beforeRetire.peakBytes >= 512u);
    assert(beforeRetire.boundedCapacity);
    assert(beforeRetire.nativeHandlesHiddenFromUpperLayer);
    assert(beforeRetire.queueWritePath);

    auto immediate = pool.reserveImmediateQueueWrite({
        "immediate-over-capacity",
        513,
        256,
        1,
    });
    assert(!immediate.success);
    assert(immediate.diagnostics.find("bounded capacity") != std::pmr::string::npos);
    assert(immediate.diagnosticId == arp::rhi::semantics::kUploadStagingInFlightCapacityDiagnostic);

    pool.retireCompletedFrames(1);
    const auto afterRetire = pool.stats();
    assert(afterRetire.liveSliceCount == 0u);
    assert(afterRetire.retiredSliceCount == 2u);

    immediate = pool.reserveImmediateQueueWrite({
        "immediate",
        512,
        256,
        2,
    });
    assert(immediate.success);
    assert(immediate.immediateQueueWrite);
    assert(pool.stats().liveImmediateQueueWriteBytes >= 512u);
    assert(pool.stats().immediateQueueWriteCount == 1u);
    pool.releaseImmediateQueueWrite(immediate);
    assert(pool.stats().liveImmediateQueueWriteBytes == 0u);
    assert(pool.stats().liveBytes == 0u);

    const auto firstTicket = pool.beginSubmission();
    auto ticketed = pool.allocate({"ticketed", 256, 256, firstTicket});
    assert(ticketed.success);
    pool.completedSubmissionSerial()->store(firstTicket, std::memory_order_release);
    const auto secondTicket = pool.beginSubmission();
    auto recycled = pool.allocate({"recycled", 256, 256, secondTicket});
    assert(recycled.success);
    assert(pool.stats().retiredSliceCount >= 3u);

    auto oversized = pool.allocate({"oversized", 2048, 256, 2});
    assert(!oversized.success);
    assert(oversized.diagnostics.find("capacity") != std::pmr::string::npos);
    assert(oversized.diagnosticId == arp::core::kFrameRetiredPageSliceDiagnosticRequestExceedsPage);
}

void assertWebGpuPersistentPoolCancelsAlignedReservation()
{
    using namespace arp::rhi_webgpu;

    WebGpuRhiStagingUploadPool pool({"WebGpuExactCancel", 1024u, 256u, false});
    const auto first = pool.allocate({"first", 96u, 256u, 1u});
    const auto second = pool.allocate({"second", 256u, 256u, 2u});
    assert(first.success && first.sizeBytes == 96u && first.accountedBytes == 256u
        && first.offsetBytes == 0u);
    assert(second.success && second.offsetBytes == 256u);

    assert(pool.cancel(first));
    assert(pool.stats().liveSliceCount == 1u);
    assert(pool.stats().liveBytes == 256u);
    const auto third = pool.allocate({"third", 1u, 256u, 3u});
    assert(third.success && third.accountedBytes == 256u && third.offsetBytes == 512u);
    assert(pool.cancel(third));
    assert(pool.cancel(second));
    assert(pool.stats().liveSliceCount == 0u);

    WebGpuRhiStagingUploadPool immediatePool({"WebGpuImmediateCancel", 1024u, 256u, true});
    const auto immediate = immediatePool.reserveImmediateQueueWrite({"immediate", 96u, 256u, 1u});
    assert(immediate.success && immediate.immediateQueueWrite);
    assert(!immediatePool.cancel(immediate));
    immediatePool.releaseImmediateQueueWrite(immediate);
    assert(immediatePool.stats().liveBytes == 0u);
}

void assertWebGpuUploadRetirementPolicyIsExplicit()
{
    const auto root = projectRoot();
    const auto commandSource =
        readText(root / "src" / "rhi" / "backends" / "webgpu" / "webgpu_backend_commands.cpp");
    assert(commandSource.find("stagingUploadPool_->retireCompletedFrames(frameSerial - 1u)")
        == std::pmr::string::npos);
    assert(commandSource.find("beginSubmission()") != std::pmr::string::npos);
    assert(commandSource.find("completedSubmissionSerial()") != std::pmr::string::npos);
    assert(commandSource.find("markStagingUpload") == std::pmr::string::npos);
    const auto nativeRuntime =
        readText(root / "src" / "rhi" / "backends" / "webgpu" / "webgpu_native_runtime.cpp");
    assert(nativeRuntime.find("webGpuNativeTrackStagingUploadUntilCompletion")
        == std::pmr::string::npos);
    assert(nativeRuntime.find("RetainedUploadBuffers") == std::pmr::string::npos);
    assert(nativeRuntime.find("webGpuNativeTrackQueueSubmission") == std::pmr::string::npos);
    assert(nativeRuntime.find("kWebGpuMaxPendingSubmissions") == std::pmr::string::npos);
    assert(nativeRuntime.find("g_webGpuBackpressureEnabled") == std::pmr::string::npos);
    const auto uploadBegin =
        commandSource.find("uploadTexture(const rhi::UploadTextureDesc& desc)");
    const auto unavailable =
        commandSource.find("RHI-WEBGPU-STAGING-COPY-PATH-UNAVAILABLE", uploadBegin);
    const auto reserve = commandSource.find("reserveImmediateQueueWrite({", uploadBegin);
    const auto write = commandSource.find("nativePayload_->recordTextureUpload(", uploadBegin);
    const auto retire = commandSource.find("releaseImmediateQueueWrite(", uploadBegin);
    assert(uploadBegin < unavailable && unavailable < reserve && reserve < write && write < retire);
    const auto uploadEnd = commandSource.find("readbackTexture(", retire);
    const auto uploadSource = commandSource.substr(uploadBegin, uploadEnd - uploadBegin);
    assert(uploadSource.find("stagingSubmissionSerial_") == std::pmr::string::npos);
    assert(uploadSource.find("stagingUploads_") == std::pmr::string::npos);
    assert(uploadSource.find("desc.label, desc.pixelDataSize, 256u, 0u") != std::pmr::string::npos);
}

void assertWebGpuDeviceReportsUploadStaging()
{
    arp::rhi_webgpu::WebGpuRhiDeviceDesc desc;
    desc.label = "WebGpuUploadStagingContract";
    desc.memoryBudgetBytes = 16u * 1024u * 1024u;
    arp::rhi::WebGpuRuntimeProbe probe;
    probe.browserRuntime = true;
    probe.canvasSurface = true;
    probe.nativeDevice = true;
    probe.computeDispatch = true;
    probe.storageTextureBinding = true;
    probe.storageBuffer = true;
    probe.maxTexture2DSize = 4096;
    probe.maxColorAttachments = 4;
    probe.maxDescriptorBindings = 16;
    probe.maxSampledTexturesPerShaderStage = 16;
    probe.uniformBufferOffsetAlignment = 256;
    probe.storageBufferOffsetAlignment = 256;
    probe.maxUniformBufferBindingSize = 65536;
    probe.maxStorageBufferBindingSize = 134217728;
    desc.runtimeProfile = arp::rhi::buildWebGpuRuntimeProfile(probe);
    auto bridge = arp::rhi::semantics::buildWebGpuRuntimeCapabilityContract(desc.runtimeProfile);
    assert(bridge.success);
    desc.capabilityContract = bridge.contract;
    desc.pushConstantUploadRingByteCapacity = 4096;
    desc.uploadStagingByteCapacity = 8u * 1024u * 1024u;
    desc.requireComputeStorageRuntimePromotion = true;
    const auto report = arp::rhi_webgpu::describeWebGpuRhiDevice(desc);
    assert(report.success);
    assert(report.backendOwnedUploadStagingSupported);
    assert(report.uploadStagingNativeHandlesHidden);
    assert(report.uploadStagingBounded);
    assert(report.pushConstantUploadRingByteCapacity == 4096u);
    assert(report.uploadStagingByteCapacity == 8u * 1024u * 1024u);
}

void assertWebGpuTextureUploadMetadataIsExplicit()
{
    const auto root = projectRoot();
    const auto backendCommands =
        readText(root / "src" / "rhi" / "backends" / "webgpu" / "webgpu_backend_commands.cpp");
    const auto nativeTransfer = readText(
        root / "src" / "rhi" / "backends" / "webgpu" / "webgpu_native_transfer_commands.cpp");
    const auto nativeCopies = readText(
        root / "src" / "rhi" / "backends" / "webgpu" / "webgpu_native_transfer_copy_lowering.h");

    assert(backendCommands.find("copyLayoutValid(") != std::pmr::string::npos);
    assert(backendCommands.find("texture upload destination, range, format or layout is invalid")
        != std::pmr::string::npos);
    assert(nativeCopies.find("explicit row and slice pitch") != std::pmr::string::npos);
    assert(nativeCopies.find("row pitch must be 256-byte aligned") != std::pmr::string::npos);
    assert(nativeTransfer.find("wgpuQueueWriteTexture") != std::pmr::string::npos);
}

void assertWebGpuTextureUploadUsesEffectiveSubresourceBase()
{
    const auto root = projectRoot();
    const auto backendCommands =
        readText(root / "src" / "rhi" / "backends" / "webgpu" / "webgpu_backend_commands.cpp");
    const auto nativeTransfer = readText(
        root / "src" / "rhi" / "backends" / "webgpu" / "webgpu_native_transfer_commands.cpp");

    assert(backendCommands.find("stagingSubmissionSerial_") != std::pmr::string::npos);
    assert(backendCommands.find("markStagingUpload") == std::pmr::string::npos);
    assert(backendCommands.find("`sourceUploadBuffer` is already the explicit backend-owned upload")
        != std::pmr::string::npos);
    assert(backendCommands.find("reserveImmediateQueueWrite({") != std::pmr::string::npos);
    assert(backendCommands.find("releaseImmediateQueueWrite(") != std::pmr::string::npos);
    const auto deviceSource =
        readText(root / "src" / "rhi" / "backends" / "webgpu" / "webgpu_device.cpp");
    assert(deviceSource.find("uploadStagingByteCapacity") != std::pmr::string::npos);
    assert(deviceSource.find("pushConstantUploadRingByteCapacity") != std::pmr::string::npos);
    assert(nativeTransfer.find("const auto effectiveMipLevel = desc.mipLevel != 0")
        != std::pmr::string::npos);
    assert(nativeTransfer.find("const auto effectiveArrayLayer = desc.arrayLayer != 0")
        != std::pmr::string::npos);
    assert(nativeTransfer.find("destinationInfo.mipLevel = effectiveMipLevel")
        != std::pmr::string::npos);
    assert(nativeTransfer.find("desc.destinationOffset.depth") != std::pmr::string::npos);
    assert(nativeTransfer.find("effectiveArrayLayer") != std::pmr::string::npos);
}

} // namespace

int main()
{
    arp::test::WebGpuOriginalTextureUploadSourceFixture::verifyContract();
    arp::test::WebGpuOriginalTextureUploadDestinationFixture::verifyContract();
    assertWebGpuUploadPoolIsBackendPrivate();
    assertWebGpuPoolSuballocatesAndRetiresByCompletionSerial();
    assertWebGpuPersistentPoolCancelsAlignedReservation();
    assertWebGpuUploadRetirementPolicyIsExplicit();
    assertWebGpuDeviceReportsUploadStaging();
    assertWebGpuTextureUploadMetadataIsExplicit();
    assertWebGpuTextureUploadUsesEffectiveSubresourceBase();
    return 0;
}

#include "core/memory/cpu_allocator.h"
#include "rhi/backends/metal/metal_backend_objects.h"
#include "rhi/backends/metal/metal_device.h"
#include "rhi/backends/metal/metal_native_runtime.h"
#include "rhi/backends/metal/metal_staging_upload_pool.h"
#include "rhi/semantics/native_backend_capabilities.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

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

void assertMetalUploadPoolIsBackendPrivate()
{
    const auto root = projectRoot();
    const auto headerPath =
        root / "src" / "rhi" / "backends" / "metal" / "metal_staging_upload_pool.h";
    const auto appleSourcePath =
        root / "src" / "rhi" / "backends" / "metal" / "metal_staging_upload_pool.mm";
    const auto fallbackSourcePath =
        root / "src" / "rhi" / "backends" / "metal" / "metal_staging_upload_pool.cpp";
    assert(std::filesystem::exists(headerPath));
    assert(std::filesystem::exists(appleSourcePath));
    assert(std::filesystem::exists(fallbackSourcePath));

    const auto publicCommands = readText(root / "src" / "rhi" / "gpu_commands.h");
    assert(publicCommands.find("MetalRhiStagingUploadPool") == std::pmr::string::npos);
    assert(publicCommands.find("MTLBuffer") == std::pmr::string::npos);

    const auto header = readText(headerPath);
    const auto appleSource = readText(appleSourcePath);
    assert(header.find("id<MTLBuffer>") == std::pmr::string::npos);
    assert(header.find("MTLBuffer") == std::pmr::string::npos);
    assert(header.find("nativeHandleExposedToUpperLayer") != std::pmr::string::npos);
    assert(appleSource.find("MTLBuffer") != std::pmr::string::npos);
    assert(appleSource.find("newBufferWithLength") != std::pmr::string::npos);
}

void assertMetalPoolSuballocatesAndRetiresExactCommand()
{
    using namespace arp::rhi_metal;

    MetalRhiStagingUploadPoolDesc poolDesc;
    poolDesc.label = "MetalUploadPool";
    poolDesc.pageSizeBytes = 4096;
    poolDesc.maxPageCount = 2;
    poolDesc.alignmentBytes = 256;
    poolDesc.preferSharedMemory = true;

    MetalRhiStagingUploadPool pool(poolDesc);

    auto first = pool.allocate({"first", 96, 256, 1});
    assert(first.success);
    assert(first.offsetBytes == 0);
    assert(first.sizeBytes == 96);
    assert(!first.nativeHandleExposedToUpperLayer);

    auto second = pool.allocate({"second", 256, 256, 1});
    assert(second.success);
    assert(second.offsetBytes == 256);

    const auto beforeRetire = pool.stats();
    assert(beforeRetire.allocatedPageCount == 1u);
    assert(beforeRetire.liveSliceCount == 2u);
    assert(beforeRetire.peakAllocatedBytes >= 512u);
    assert(beforeRetire.boundedCapacity);
    assert(beforeRetire.nativeHandlesHiddenFromUpperLayer);
    assert(beforeRetire.sharedOrManagedUploadMemory);

    arp::rhi::RecordedStagingUploadJournal completed;
    assert(pool.enlist(
        completed, {first.pageIndex, first.offsetBytes, first.sizeBytes, first.frameSerial}));
    assert(pool.enlist(
        completed, {second.pageIndex, second.offsetBytes, second.sizeBytes, second.frameSerial}));
    assert(completed.retireCompleted());
    const auto afterRetire = pool.stats();
    assert(afterRetire.liveSliceCount == 0u);
    assert(afterRetire.retiredSliceCount == 2u);

    auto oversized = pool.allocate({"oversized-texture", 8192, 256, 2});
    assert(!oversized.success);
    assert(oversized.diagnostics.find("capacity") != std::pmr::string::npos);
    assert(oversized.diagnosticId == arp::core::kFrameRetiredPageSliceDiagnosticRequestExceedsPage);
    assert(oversized.diagnostics.find("uploadLabel=oversized-texture") != std::pmr::string::npos);
    assert(oversized.diagnostics.find("requestBytes=8192") != std::pmr::string::npos);
    assert(oversized.diagnostics.find("pageSizeBytes=4096") != std::pmr::string::npos);
    assert(oversized.diagnostics.find("allocatedBytes=4096") != std::pmr::string::npos);
    assert(oversized.diagnostics.find("frameSerial=") != std::pmr::string::npos);
}

void assertMetalPoolCancelsExactUnacceptedSlice()
{
    using namespace arp::rhi_metal;

    MetalRhiStagingUploadPool pool({"MetalExactCancel", 1024u, 1u, 256u, true});
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

    const auto reclaimed = pool.allocate({"reclaimed", 256u, 256u, 4u});
    assert(reclaimed.success && reclaimed.offsetBytes == 0u);
    assert(pool.cancel(reclaimed));
}

void assertMetalUploadRetirementPolicyIsExplicit()
{
    const auto root = projectRoot();
    const auto commandSource =
        readText(root / "src" / "rhi" / "backends" / "metal" / "metal_backend_commands.cpp");
    assert(commandSource.find("retireCompletedFrames(frameSerial - 1u)") == std::pmr::string::npos);
    assert(commandSource.find("beginSubmission()") != std::pmr::string::npos);
    assert(commandSource.find("completedSubmissionSerial()") == std::pmr::string::npos);
    assert(commandSource.find("completedSubmissionEpoch()") != std::pmr::string::npos);
    assert(commandSource.find("markStagingUpload") != std::pmr::string::npos);

    arp::rhi_metal::MetalRhiStagingUploadPool pool(
        {"MetalCompletionTicket", 4096u, 1u, 256u, true});
    const auto firstTicket = pool.beginSubmission();
    const auto first = pool.allocate({"first-ticket", 4096u, 256u, firstTicket});
    assert(first.success);
    const auto deferred = pool.allocate({"deferred-ticket", 4096u, 256u, pool.beginSubmission()});
    assert(!deferred.success);
    assert(deferred.diagnosticId == arp::core::kFrameRetiredPageSliceDiagnosticPageCount);
    pool.completedSubmissionSerial()->store(firstTicket, std::memory_order_release);
    assert(!pool.allocate({"forged-counter", 4096u, 256u, pool.beginSubmission()}).success);
    arp::rhi::RecordedStagingUploadJournal journal;
    assert(pool.enlist(
        journal, {first.pageIndex, first.offsetBytes, first.sizeBytes, first.frameSerial}));
    assert(journal.retireCompleted());
    const auto recovered = pool.allocate({"recovered-ticket", 4096u, 256u, pool.beginSubmission()});
    assert(recovered.success);
    // Readiness is not a submission serial. A discarded encoder may reserve
    // a later ticket without preventing an actual completion from publishing
    // a new retry credential.
    assert(pool.completedSubmissionEpoch()->load(std::memory_order_acquire) == 0u);
    pool.completedSubmissionEpoch()->fetch_add(1u, std::memory_order_release);
    assert(pool.completedSubmissionEpoch()->load(std::memory_order_acquire) == 1u);
}

void assertUnprovenCompletionCannotRetireEarlierRecording()
{
    using namespace arp::rhi_metal;
    MetalRhiStagingUploadPool pool({"UnorderedRecording", 512u, 1u, 256u, true});
    const auto earlier = pool.allocate({"unsubmitted", 256u, 256u, pool.beginSubmission()});
    const auto later = pool.allocate({"later", 256u, 256u, pool.beginSubmission()});
    assert(earlier.success && later.success);
    // A public serial/epoch is not the original command's completion proof.
    pool.completedSubmissionSerial()->store(later.frameSerial, std::memory_order_release);
    pool.completedSubmissionEpoch()->fetch_add(1u, std::memory_order_release);
    bool rejected = false;
    try {
        pool.retireCompletedFrames(later.frameSerial);
    } catch (const std::logic_error& e) {
        rejected =
            std::string_view(e.what()) == "RHI-METAL-STAGING-SERIAL-COMPLETION-UNAUTHENTICATED";
    }
    assert(rejected);
    const auto overlap = pool.allocate({"must-remain-full", 256u, 256u, pool.beginSubmission()});
    assert(!overlap.success);
    assert(pool.stats().liveSliceCount == 2u);
    assert(pool.cancel(later) && pool.cancel(earlier));
}

void assertExactCompletionAndFacadeLifetime()
{
    using namespace arp;
    using namespace rhi_metal;
    rhi::RecordedStagingUploadJournal firstJournal, secondJournal;
    auto pool = core::makeUnique<MetalRhiStagingUploadPool>(
        MetalRhiStagingUploadPoolDesc{"ExactCompletion", 512u, 1u, 256u, true});
    const auto first = pool->allocate({"unsubmitted", 256u, 256u, 1u});
    const auto second = pool->allocate({"submitted-first", 256u, 256u, 1u});
    assert(first.success && second.success && first.frameSerial != second.frameSerial);
    assert(pool->enlist(
        firstJournal, {first.pageIndex, first.offsetBytes, first.sizeBytes, first.frameSerial}));
    assert(pool->enlist(secondJournal,
        {second.pageIndex, second.offsetBytes, second.sizeBytes, second.frameSerial}));
    assert(!pool->enlist(secondJournal,
        {first.pageIndex, first.offsetBytes, first.sizeBytes + 1u, first.frameSerial}));
    std::thread completion([&] {
        assert(secondJournal.retireCompleted());
    });
    completion.join();
    assert(pool->stats().liveSliceCount == 1u && pool->stats().retiredSliceCount == 1u);
    const auto reused = pool->allocate({"reuse-tail-only", 256u, 256u, 1u});
    assert(
        reused.success && reused.offsetBytes == 256u && reused.frameSerial != second.frameSerial);
    assert(!pool->cancel(second));
    assert(!pool->allocate({"earlier-still-live", 256u, 256u, 1u}).success);
    assert(pool->cancel(reused));
    pool.reset();
    // No borrowed wrapper or allocator is dereferenced after facade destruction.
    assert(firstJournal.rollback());
}

void assertMetalDeviceReportsUploadStaging()
{
    arp::rhi_metal::MetalRhiDeviceDesc desc;
    desc.label = "MetalUploadStagingContract";
    desc.memoryBudgetBytes = 16u * 1024u * 1024u;
    desc.uploadStagingPageSizeBytes = 4096u;
    desc.uploadStagingMaxPageCount = 2u;
    desc.capabilityContract =
        arp::rhi::semantics::nativeBackendCapabilityContract(arp::rhi::Backend::Metal);
    desc.capabilityContract.queriedFromNativeDevice = true;
    desc.capabilityContract.queriedExtendedFeatures = true;
    desc.capabilityContract.runtimeFeaturesQueried = true;
    desc.capabilityContract.runtimeFeatures = desc.capabilityContract.features;
    desc.enablePushConstantSetBytes = true;
    desc.enableMemorylessTransientTextures = true;
    desc.enableDrawableLifecycleGuards = true;
    desc.allowNativeHandleExposureToUpperLayers = false;
    desc.allowBlockingDrawableAcquire = false;
    desc.allowPerDrawPipelineCreation = false;
    desc.allowPerFrameShaderCompilation = false;
    desc.uploadStagingPageSizeBytes = 4096u;
    desc.uploadStagingMaxPageCount = 2u;
    const auto report = arp::rhi_metal::describeMetalRhiDevice(desc);
    assert(report.success);
    const auto runtime = arp::rhi_metal::describeMetalRhiNativeRuntimeBoundary();
    assert(report.backendOwnedUploadStagingSupported == runtime.runtimeAvailableOnThisPlatform);
    assert(report.uploadStagingNativeHandlesHidden);
    assert(report.uploadStagingBounded);
}

void assertMetalTextureUploadsRejectInvalidLayout()
{
    using namespace arp::rhi;
    using namespace arp::rhi_metal;

    if (!describeMetalRhiNativeRuntimeBoundary().runtimeAvailableOnThisPlatform) {
        return;
    }

    auto encoder = createMetalRhiCommandEncoder({"MetalTextureUploadLayout"});
    assert(encoder);
    auto transfer = encoder->beginTransferPass({"MetalTextureUploadLayout"});
    assert(transfer);

    assert(!transfer
            ->copyBufferToTexture({
                {1},
                {2},
                0,
                0,
                4,
                {0, 1, 0, 1, TextureAspect::Color},
                {4, 4, 1},
            })
            .success);
    assert(!transfer
            ->copyBufferToTexture({
                {1},
                {2},
                0,
                128,
                4,
                {0, 1, 0, 1, TextureAspect::Color},
                {4, 4, 1},
            })
            .success);
    const auto validLayoutWithoutResources = transfer->copyBufferToTexture({
        {1},
        {2},
        0,
        256,
        4,
        {0, 1, 0, 1, TextureAspect::Color},
        {4, 4, 1},
    });
    // Resource lookup/native payload validation is a separate contract. This
    // fixture intentionally has no registered native resources, so a layout
    // that passes the pitch checks must still fail closed at that boundary.
    assert(!validLayoutWithoutResources.success);
}

void assertMetalMemoryBarrierPreservesState()
{
    using namespace arp::rhi;
    using namespace arp::rhi_metal;

    if (!describeMetalRhiNativeRuntimeBoundary().runtimeAvailableOnThisPlatform) {
        return;
    }

    MetalRhiDeviceDesc desc;
    desc.label = "MetalMemoryBarrier";
    desc.memoryBudgetBytes = 1024u * 1024u;
    desc.capabilityContract = semantics::nativeBackendCapabilityContract(Backend::Metal);
    desc.capabilityContract.queriedFromNativeDevice = true;
    desc.capabilityContract.queriedExtendedFeatures = true;
    desc.uploadStagingPageSizeBytes = 4096u;
    desc.uploadStagingMaxPageCount = 1u;
    auto device = createMetalRhiDevice(desc);
    assert(device);

    auto buffer = device->createBuffer({
        "MetalMemoryBarrierBuffer",
        256u,
        ResourceUsage::StorageBuffer,
        MemoryUsage::GpuOnly,
        ResourceState::ShaderWrite,
    });
    assert(buffer);
    auto encoder = device->createCommandEncoder({"MetalMemoryBarrier"});
    assert(encoder);
    const auto originalState = buffer->state();
    const auto result = encoder->memoryBarrier(*buffer,
        {
            buffer->id(),
            PipelineStage::ComputeShader,
            PipelineStage::ComputeShader,
            ResourceAccess::ShaderWrite,
            ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite,
        });
    assert(result.success);
    assert(result.stageAccessAware);
    assert(result.before == originalState && result.after == originalState);
    assert(buffer->state() == originalState);

    const auto commands = encoder->finish();
    assert(commands);
    assert(device->queue().submit(*commands).success);
    device->waitIdle();
}

} // namespace

int main()
{
    assertExactCompletionAndFacadeLifetime();
    assertUnprovenCompletionCannotRetireEarlierRecording();
    assertMetalUploadPoolIsBackendPrivate();
    assertMetalPoolSuballocatesAndRetiresExactCommand();
    assertMetalPoolCancelsExactUnacceptedSlice();
    assertMetalUploadRetirementPolicyIsExplicit();
    assertMetalDeviceReportsUploadStaging();
    assertMetalTextureUploadsRejectInvalidLayout();
    assertMetalMemoryBarrierPreservesState();
    return 0;
}

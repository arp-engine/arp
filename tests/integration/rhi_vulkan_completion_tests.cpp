#include "core/atomic_capacity.h"
#include "rhi/backends/vulkan/vulkan_backend_objects.h"
#include "rhi/backends/vulkan/vulkan_device.h"
#include "rhi/backends/vulkan/vulkan_native_commands.h"
#include "rhi/backends/vulkan/vulkan_staging_upload_pool.h"
#include "rhi/gpu_bindless.h"
#include "rhi/gpu_extensions.h"
#include "rhi/semantics/native_backend_capabilities.h"
#include "rhi/semantics/vulkan_device_capability_query.h"
#include "tests/fixtures/vulkan_headless_runtime.h"
#include "tests/fixtures/vulkan_heap_owner_fixture.h"
#include "tests/fixtures/vulkan_native_factory_admission_fixture.h"
#include "tests/fixtures/vulkan_resource_owner_fixture.h"
#include "tests/fixtures/vulkan_runtime_owner_fixture.h"
#include "tests/fixtures/vulkan_scoped_present_retirement_fixture.h"
#include "tests/fixtures/vulkan_timeline_owner_fixture.h"

#include <array>
#include <cassert>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <thread>

namespace {
class VulkanStagingIsolationFixture final {
public:
    static int runPoolRelease()
    {
        using namespace arp;
        using namespace arp::rhi_vulkan;
        tests::VulkanHeadlessRuntime native;
        if (!native.runtime() || !native.runtime()->valid())
            return 77;
        VulkanRhiAllocator allocator({"pool release", 64u * 1024u * 1024u}, native.runtime());
        VulkanRhiNativeResourceAllocator resources(native.runtime(), &allocator);
        auto pool = core::makeUnique<VulkanRhiStagingUploadPool>(
            VulkanRhiStagingUploadPoolDesc{"pool release", 1024u, 1u, 256u}, allocator);
        auto slice = pool->allocate({"pending page", sizeof(std::uint32_t), 256u, 1u});
        assert(slice.success && slice.mappedData && slice.writeFlusher);
        const std::uint32_t expected = 0x31a72bd9u;
        std::memcpy(static_cast<std::uint8_t*>(slice.mappedData) + slice.offsetBytes,
            &expected,
            sizeof(expected));
        core::pmr::String diagnostic;
        assert(
            slice.writeFlusher->flushMappedWrite(slice.offsetBytes, sizeof(expected), diagnostic));
        auto destination = resources.createBuffer({"page readback",
            sizeof(expected),
            rhi::ResourceUsage::CopyDst,
            rhi::MemoryUsage::GpuToCpu,
            rhi::ResourceState::CopyDst});
        assert(
            destination.success && destination.payload && destination.payload->backendMappedData());
        auto command = createVulkanRhiNativeCommandPayload(resources, diagnostic, {"page lease"});
        assert(command);
        auto journal = core::makeUnique<rhi::RecordedStagingUploadJournal>();
        assert(pool->enlist(
            *journal, {slice.pageIndex, slice.offsetBytes, slice.sizeBytes, slice.frameSerial}));
        assert(command->retainRecordedStagingUploads(journal, diagnostic) && !journal);
        command->retainResource(destination.payload);
        assert(command->recordUploadStagingBufferCopy(
            slice.buffer, slice.offsetBytes, *destination.payload, sizeof(expected), diagnostic));
        auto timelineOwner =
            VulkanRhiTimelineSemaphorePayload::create(native.runtime(), 0u, diagnostic);
        assert(timelineOwner);
        const VkSemaphore timeline = timelineOwner->nativeHandle();
        VulkanRhiTimelineSubmitInfo wait;
        wait.waitSemaphore = timelineOwner;
        wait.waitValue = 1u;
        bool accepted = false;
        assert(
            command->end(diagnostic) && command->submit(wait, diagnostic, &accepted) && accepted);
        assert(!command->submittedFenceCompleted());
        std::weak_ptr<void> observedPage = slice.payload;
        pool.reset();
        slice = {};
        const bool retainedPage = !observedPage.expired();
        VkSemaphoreSignalInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO};
        signal.semaphore = timeline;
        signal.value = 1u;
        assert(vkSignalSemaphore(native.device(), &signal) == VK_SUCCESS);
        assert(command->waitSubmittedFenceUntil(
            std::chrono::steady_clock::now() + std::chrono::seconds(5), diagnostic));
        assert(destination.payload->backendInvalidateMappedRead(0u, sizeof(expected), diagnostic));
        std::uint32_t actual = 0u;
        std::memcpy(&actual, destination.payload->backendMappedData(), sizeof(actual));
        const bool releasedPage = observedPage.expired();
        retireCompletedVulkanRhiSubmittedCommands();
        command.reset();
        wait = {};
        timelineOwner.reset();
        drainVulkanRhiSubmittedCommandsForShutdown();
        std::printf("staging-pool-release: retained=%d released=%d expected=%u actual=%u\n",
            retainedPage,
            releasedPage,
            expected,
            actual);
        std::fflush(stdout);
        assert(retainedPage && releasedPage && actual == expected);
        return 0;
    }

    static int run(bool differentRuntime, bool pendingFirst)
    {
        using namespace arp;
        using namespace arp::rhi_vulkan;
        tests::VulkanHeadlessRuntime firstNative;
        tests::VulkanHeadlessRuntime secondNative;
        if (!firstNative.runtime() || !firstNative.runtime()->valid() || !secondNative.runtime()
            || !secondNative.runtime()->valid())
            return 77;
        VulkanRhiAllocator firstAllocator(
            {"first staging", 64u * 1024u * 1024u}, firstNative.runtime());
        VulkanRhiAllocator secondAllocator({"second staging", 64u * 1024u * 1024u},
            differentRuntime ? secondNative.runtime() : firstNative.runtime());
        VulkanRhiStagingUploadPool firstPool({"first pool", 1024u, 1u, 256u}, firstAllocator);
        VulkanRhiStagingUploadPool secondPool({"second pool", 1024u, 1u, 256u}, secondAllocator);
        VulkanRhiNativeResourceAllocator firstResources(firstNative.runtime(), &firstAllocator);
        VulkanRhiNativeResourceAllocator secondResources(
            differentRuntime ? secondNative.runtime() : firstNative.runtime(), &secondAllocator);
        auto firstRegistry = core::makeShared<VulkanRhiResourceRegistry>();
        auto secondRegistry = core::makeShared<VulkanRhiResourceRegistry>();
        const auto capabilities =
            rhi::semantics::nativeBackendCapabilityContract(rhi::Backend::Vulkan);
        auto record = [&](VulkanRhiNativeResourceAllocator& resources,
                          const std::shared_ptr<VulkanRhiResourceRegistry>& registry,
                          VulkanRhiStagingUploadPool& pool) {
            const auto source = resources.createBuffer({"upload source",
                256u,
                rhi::ResourceUsage::CopySrc,
                rhi::MemoryUsage::CpuToGpu,
                rhi::ResourceState::CopySrc});
            const auto destination = resources.createBuffer({"upload destination",
                256u,
                rhi::ResourceUsage::CopyDst,
                rhi::MemoryUsage::GpuOnly,
                rhi::ResourceState::CopyDst});
            assert(source.success && destination.success);
            const auto sourceId = rhi::allocateGpuResourceId();
            const auto destinationId = rhi::allocateGpuResourceId();
            registry->registerResource(sourceId, source.payload);
            registry->registerResource(destinationId, destination.payload);
            auto encoder = createVulkanRhiCommandEncoder(
                resources, registry, &pool, capabilities, false, true, {"isolation"});
            assert(encoder);
            auto transfer = encoder->beginTransferPass({"isolation"});
            assert(transfer);
            assert(transfer
                    ->uploadBuffer({"isolation",
                        sourceId,
                        destinationId,
                        256u,
                        rhi::StagingIntent::Upload,
                        true,
                        1u})
                    .success);
            assert(transfer->end().success);
            auto result = encoder->finish();
            assert(result);
            return result;
        };
        auto first = record(firstResources, firstRegistry, firstPool);
        VkSemaphore timeline = VK_NULL_HANDLE;
        std::shared_ptr<VulkanRhiTimelineSemaphorePayload> timelineOwner;
        core::pmr::String diagnostic;
        if (pendingFirst) {
            assert(differentRuntime);
            timelineOwner =
                VulkanRhiTimelineSemaphorePayload::create(firstNative.runtime(), 0u, diagnostic);
            assert(timelineOwner);
            timeline = timelineOwner->nativeHandle();
            VulkanRhiTimelineSubmitInfo wait;
            wait.waitSemaphore = timelineOwner;
            wait.waitValue = 1u;
            assert(submitVulkanRhiCommandBuffer(*first, wait).workAccepted);
            assert(!vulkanRhiCommandPayload(*first)->submittedFenceCompleted());
        }
        auto second = record(differentRuntime ? secondResources : firstResources,
            differentRuntime ? secondRegistry : firstRegistry,
            differentRuntime ? secondPool : firstPool);
        auto secondPayload = vulkanRhiCommandPayload(*second);
        assert(submitVulkanRhiCommandBuffer(*second).workAccepted);
        second.reset();
        assert(secondPayload->waitSubmittedFenceUntil(
            std::chrono::steady_clock::now() + std::chrono::seconds(5), diagnostic));
        retireCompletedVulkanRhiSubmittedCommands();
        auto third = record(firstResources, firstRegistry, firstPool);
        const auto liveAfterUnrelatedCompletion = firstPool.stats().liveSliceCount;
        const auto epochBeforeCancellation = firstPool.completedSubmissionEpoch();
        third.reset();
        const auto liveAfterCancellation = firstPool.stats().liveSliceCount;
        const bool cancellationDidNotComplete =
            firstPool.completedSubmissionEpoch() == epochBeforeCancellation;
        if (pendingFirst) {
            VkSemaphoreSignalInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO};
            signal.semaphore = timeline;
            signal.value = 1u;
            assert(vkSignalSemaphore(firstNative.device(), &signal) == VK_SUCCESS);
            assert(vulkanRhiCommandPayload(*first)->waitSubmittedFenceUntil(
                std::chrono::steady_clock::now() + std::chrono::seconds(5), diagnostic));
            retireCompletedVulkanRhiSubmittedCommands();
            timelineOwner.reset();
        }
        first.reset();
        const auto liveAfterRelease = firstPool.stats().liveSliceCount;
        secondPayload.reset();
        drainVulkanRhiSubmittedCommandsForShutdown();
        std::printf(
            "staging-isolation: different-runtime=%d pending=%d live=%u cancelled=%u released=%u epoch-stable=%d\n",
            differentRuntime,
            pendingFirst,
            liveAfterUnrelatedCompletion,
            liveAfterCancellation,
            liveAfterRelease,
            cancellationDidNotComplete);
        std::fflush(stdout);
        assert(liveAfterUnrelatedCompletion == 2u && liveAfterCancellation == 1u
            && liveAfterRelease == 0u && cancellationDidNotComplete);
        return 0;
    }
};

class VulkanShutdownCompletionFixture final {
public:
    static int run()
    {
        using namespace arp;
        using namespace arp::rhi_vulkan;
        tests::VulkanHeadlessRuntime native;
        if (!native.runtime() || !native.runtime()->valid()) {
            std::puts("SKIP: real Vulkan 1.2 timeline device unavailable");
            return 77;
        }
        VulkanRhiAllocator allocator(
            {"shutdown completion fixture", 64u * 1024u * 1024u}, native.runtime());
        VulkanRhiNativeResourceAllocator resources(native.runtime(), &allocator);
        core::pmr::String diagnostic;
        auto command = createVulkanRhiNativeCommandPayload(
            resources, diagnostic, {"pending shutdown", 1u, 1u});
        assert(command);
        command->observeFrameSerial(1u);
        auto timelineOwner =
            VulkanRhiTimelineSemaphorePayload::create(native.runtime(), 0u, diagnostic);
        assert(timelineOwner);
        const VkSemaphore timeline = timelineOwner->nativeHandle();
        VulkanRhiTimelineSubmitInfo wait;
        wait.waitSemaphore = timelineOwner;
        wait.waitValue = 1u;
        bool accepted = false;
        assert(
            command->end(diagnostic) && command->submit(wait, diagnostic, &accepted) && accepted);
        assert(!command->submittedFenceCompleted() && command.use_count() >= 2);
        const auto completedBefore = completedVulkanRhiSubmittedFrameSerial();
        assert(completedBefore == 0u);
        drainVulkanRhiSubmittedCommandsForShutdown();
        const auto completedAfter = completedVulkanRhiSubmittedFrameSerial();
        const bool retainedByBackend = command.use_count() >= 2;
        const bool stillPending = !command->submittedFenceCompleted();
        // Keep the caller owner until actual completion, even on the red path.
        VkSemaphoreSignalInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO};
        signal.semaphore = timeline;
        signal.value = 1u;
        assert(vkSignalSemaphore(native.device(), &signal) == VK_SUCCESS);
        assert(command->waitSubmittedFenceUntil(
            std::chrono::steady_clock::now() + std::chrono::seconds(5), diagnostic));
        retireCompletedVulkanRhiSubmittedCommands();
        command.reset();
        drainVulkanRhiSubmittedCommandsForShutdown();
        wait = {};
        timelineOwner.reset();
        std::printf("shutdown-pending: before=%llu after=%llu retained=%d pending=%d\n",
            static_cast<unsigned long long>(completedBefore),
            static_cast<unsigned long long>(completedAfter),
            retainedByBackend,
            stillPending);
        std::fflush(stdout);
        assert(completedAfter == completedBefore && retainedByBackend && stillPending);
        return 0;
    }
};
} // namespace

void verifyOriginalPublicFence(const std::shared_ptr<arp::rhi_vulkan::VulkanRhiRuntime>& runtime)
{
    using namespace arp;
    auto device = tests::VulkanHeapOwnerFixture::makeDevice(runtime, false);
    assert(device);
    auto fence = device->createFence();
    auto timeline = device->createTimelineSemaphore(0u);
    assert(fence && timeline && !fence->isSignaled());
    bool hostRejected = false;
    try {
        fence->signal();
    } catch (const std::logic_error& e) {
        hostRejected = std::string_view(e.what()) == "RHI-VULKAN-FENCE-HOST-SIGNAL-UNAUTHENTICATED";
    }
    assert(hostRejected && !fence->isSignaled());
    auto encoder = device->createCommandEncoder({"PublicFencePending"});
    assert(encoder);
    auto commands = encoder->finish();
    assert(commands);
    assert(device->queue().submit(*commands, timeline.get(), 1u, nullptr, 0u, fence.get()).success);
    const bool pending = !fence->isSignaled();
    bool resetRejected = false;
    try {
        fence->reset();
    } catch (const std::logic_error& e) {
        resetRejected = std::string_view(e.what()) == "RHI-VULKAN-FENCE-RESET-PENDING";
    }
    auto nextEncoder = device->createCommandEncoder({"MustNotSubmitWithBoundFence"});
    assert(nextEncoder);
    auto next = nextEncoder->finish();
    assert(next);
    const auto before = device->queue().submitCount();
    const auto rejected = device->queue().submit(*next, fence.get());
    const bool untouched = device->queue().submitCount() == before && !rejected.hasAcceptedWork();
    // Safe cleanup precedes assertions of negative behavior.
    timeline->signal(1u);
    fence->wait();
    const bool terminal = fence->isSignaled();
    fence->reset();
    assert(hostRejected && pending && resetRejected && untouched && terminal);
    assert(rejected.diagnostics == "RHI-VULKAN-FENCE-ALREADY-BOUND");
    assert(!fence->isSignaled());
    assert(device->queue().submit(*next, fence.get()).success);
    fence->wait();
    assert(fence->isSignaled());
    fence->reset();
}

int main(int argc, char** argv)
{
    if (argc == 2 && std::strcmp(argv[1], "native-factory-admission") == 0)
        return arp::tests::VulkanNativeFactoryAdmissionFixture::run();
    using HeapOwner = arp::tests::VulkanHeapOwnerFixture;
    if (argc == 2 && std::strcmp(argv[1], "heap-owner-lease") == 0)
        return HeapOwner::run(HeapOwner::Mode::Lease);
    if (argc == 2 && std::strcmp(argv[1], "heap-owner-placed") == 0)
        return HeapOwner::run(HeapOwner::Mode::Placed);
    using TimelineOwner = arp::tests::VulkanTimelineOwnerFixture;
    if (argc == 2 && std::strcmp(argv[1], "timeline-owner-admission") == 0)
        return TimelineOwner::run(TimelineOwner::Mode::Admission);
    if (argc == 2 && std::strcmp(argv[1], "timeline-owner-independent") == 0)
        return TimelineOwner::run(TimelineOwner::Mode::Independent);
    if (argc == 2 && std::strcmp(argv[1], "timeline-owner-pending") == 0)
        return TimelineOwner::run(TimelineOwner::Mode::Pending);
    if (argc == 2 && std::strcmp(argv[1], "timeline-owner-wrong-device") == 0)
        return TimelineOwner::run(TimelineOwner::Mode::WrongDevice);
    if (argc == 2 && std::strcmp(argv[1], "timeline-owner-unproven") == 0)
        return TimelineOwner::run(TimelineOwner::Mode::Unproven);
    using ResourceOwner = arp::tests::VulkanResourceOwnerFixture;
    if (argc == 2 && std::strcmp(argv[1], "resource-owner-allocation-rollback") == 0)
        return ResourceOwner::runAllocationRollback();
    if (argc == 2 && std::strcmp(argv[1], "resource-owner-registered-buffer") == 0)
        return ResourceOwner::runRegistered(ResourceOwner::Resource::Buffer);
    if (argc == 2 && std::strcmp(argv[1], "resource-owner-registered-image") == 0)
        return ResourceOwner::runRegistered(ResourceOwner::Resource::Image);
    if (argc == 2 && std::strcmp(argv[1], "resource-owner-buffer") == 0)
        return ResourceOwner::run(ResourceOwner::Resource::Buffer);
    if (argc == 2 && std::strcmp(argv[1], "resource-owner-image") == 0)
        return ResourceOwner::run(ResourceOwner::Resource::Image);
    if (argc == 2 && std::strcmp(argv[1], "resource-owner-descriptor") == 0)
        return ResourceOwner::run(ResourceOwner::Resource::Descriptor);
    if (argc == 2 && std::strcmp(argv[1], "resource-owner-sampler") == 0)
        return ResourceOwner::run(ResourceOwner::Resource::Sampler);
    if (argc == 2 && std::strcmp(argv[1], "runtime-owner") == 0)
        return arp::tests::VulkanRuntimeOwnerFixture::run();
    if (argc == 2 && std::strcmp(argv[1], "scoped-present") == 0)
        return arp::tests::VulkanScopedPresentRetirementFixture::run();
    if (argc == 2 && std::strcmp(argv[1], "staging-pool-release") == 0)
        return VulkanStagingIsolationFixture::runPoolRelease();
    if (argc == 2 && std::strcmp(argv[1], "staging-same-runtime") == 0)
        return VulkanStagingIsolationFixture::run(false, false);
    if (argc == 2 && std::strcmp(argv[1], "staging-other-runtime") == 0)
        return VulkanStagingIsolationFixture::run(true, false);
    if (argc == 2 && std::strcmp(argv[1], "staging-pending") == 0)
        return VulkanStagingIsolationFixture::run(true, true);
    if (argc == 2 && std::strcmp(argv[1], "shutdown-pending") == 0)
        return VulkanShutdownCompletionFixture::run();
    assert(argc == 1);
    using namespace arp;
    using namespace arp::rhi_vulkan;
    using Clock = std::chrono::steady_clock;
    tests::VulkanHeadlessRuntime native;
    if (!native.runtime() || !native.runtime()->valid()) {
        std::puts("SKIP: real Vulkan 1.2 timeline device unavailable");
        return 77;
    }
    verifyOriginalPublicFence(native.runtime());
    VulkanRhiAllocator allocator({"completion fixture", 64u * 1024u * 1024u}, native.runtime());
    VulkanRhiNativeResourceAllocator resources(native.runtime(), &allocator);
    core::pmr::String diagnostic;
    diagnostic.reserve(192);
    // Failed preparation must not allocate a Vulkan pool or retain CPU bytes.
    auto cpuQuota = core::makeShared<core::CpuMemoryQuota>(1u);
    const auto poolsBefore = resources.stats().nativeCommandPoolCreateCount;
    {
        core::CpuMemoryQuotaScope limited(cpuQuota);
        assert(
            !createVulkanRhiNativeCommandPayload(resources, diagnostic, {"bounded failure", 1, 1}));
    }
    assert(diagnostic == "RHI-VULKAN-BINDLESS-RETENTION-ALLOCATION-FAILED");
    assert(!cpuQuota->liveBytes() && resources.stats().nativeCommandPoolCreateCount == poolsBefore);
    const rhi::CommandEncoderDesc retention{"indexed completion", 1, 1};
    auto command = createVulkanRhiNativeCommandPayload(resources, diagnostic, retention);
    assert(command);
    std::weak_ptr<VulkanRhiNativeCommandPayload> observedPreambleCommand = command;
    assert(!command->submittedFenceCompleted());
    assert(!command->waitSubmittedFenceUntil(Clock::now() + std::chrono::seconds(1), diagnostic));
    assert(diagnostic == "RHI-VULKAN-COMPLETION-NOT-SUBMITTED");

    auto timelineOwner =
        VulkanRhiTimelineSemaphorePayload::create(native.runtime(), 0u, diagnostic);
    assert(timelineOwner);
    const VkSemaphore timeline = timelineOwner->nativeHandle();

    // A real pending submission must retain both native storage and its charge.
    auto budget = core::makeShared<core::AtomicCapacityBudget>(1u);
    auto lease = core::makeShared<core::AtomicCapacityLease>(budget, 1u);
    assert(lease->charged());
    std::weak_ptr<core::AtomicCapacityLease> observedLease = lease;
    assert(!command->retainIndexedPipeline({}, diagnostic));
    assert(diagnostic == "RHI-VULKAN-BINDLESS-RETENTION-PAYLOAD-MISSING");
    assert(command->retainIndexedPipeline(lease, diagnostic));
    const auto overflow = core::makeShared<int>(1);
    assert(!command->retainIndexedPipeline(overflow, diagnostic));
    assert(diagnostic == "RHI-BINDLESS-COMMAND-PIPELINE-BUDGET");
    lease.reset();
    // A real descriptor array owns source storage even after all public
    // preparation wrappers are destroyed. GPU copy consumes that same storage.
    const std::array<std::uint32_t, 4> expected{{17, 31, 43, 59}};
    auto source = resources.createBuffer({"descriptor source",
        sizeof(expected),
        rhi::ResourceUsage::StorageBuffer | rhi::ResourceUsage::CopySrc,
        rhi::MemoryUsage::CpuToGpu,
        rhi::ResourceState::CopySrc});
    auto destination = resources.createBuffer({"copy readback",
        sizeof(expected),
        rhi::ResourceUsage::StorageBuffer | rhi::ResourceUsage::CopyDst,
        rhi::MemoryUsage::GpuToCpu,
        rhi::ResourceState::CopyDst});
    assert(source.success && destination.success && source.payload && destination.payload);
    assert(source.payload->backendVulkanBufferHandle()
        && destination.payload->backendVulkanBufferHandle());
    assert(source.payload->backendMappedData() && destination.payload->backendMappedData());
    std::memcpy(source.payload->backendMappedData(), expected.data(), sizeof(expected));
    assert(source.payload->backendFlushMappedWrite(0, sizeof(expected), diagnostic));
    const auto sourceId = rhi::allocateGpuResourceId();
    const auto destinationId = rhi::allocateGpuResourceId();
    auto registry = core::makeShared<VulkanRhiResourceRegistry>();
    registry->registerResource(sourceId, source.payload);
    registry->registerResource(destinationId, destination.payload);
    auto arena =
        createVulkanRhiDescriptorArena({"completion array", 1, 2}, resources, registry, true);
    rhi::DescriptorLayoutDesc layout;
    layout.name = "completion array";
    layout.bindings.push_back(
        {0, rhi::DescriptorType::StorageBuffer, rhi::ShaderStage::Compute, 2});
    auto descriptors = arena->allocateDescriptorSet({"completion array", layout});
    assert(descriptors);
    rhi::DescriptorBindingWrite write;
    write.type = rhi::DescriptorType::StorageBuffer;
    write.resourceKind = rhi::DescriptorResourceKind::Buffer;
    write.resource = sourceId;
    write.arrayElement = 0;
    write.resourceState = rhi::ResourceState::ShaderRead;
    assert(descriptors->write(write).success);
    write.resource = destinationId;
    write.arrayElement = 1;
    write.resourceState = rhi::ResourceState::ShaderWrite;
    assert(descriptors->write(write).success);
    auto retained = vulkanRhiNativePayload(*descriptors);
    assert(retained && retained->backendVulkanDescriptorSetHandle());
    std::weak_ptr<VulkanRhiNativeResourcePayload> observedResource = source.payload;
    std::weak_ptr<VulkanRhiNativeResourcePayload> observedDescriptors = retained;
    assert(command->retainIndexedSnapshot(retained, diagnostic));
    assert(!command->retainIndexedSnapshot(overflow, diagnostic));
    assert(diagnostic == "RHI-BINDLESS-COMMAND-SNAPSHOT-BUDGET");
    // Duplicate ownership is idempotent and cannot allocate on the hot path.
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    for (unsigned i = 0; i < 128; ++i) {
        assert(command->retainIndexedSnapshot(retained, diagnostic));
        assert(command->retainIndexedPipeline(observedLease.lock(), diagnostic));
    }
    assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
    write.resource = sourceId;
    assert(descriptors->write(write).diagnostics.find("RHI-DESCRIPTOR-RECORDED-STORAGE-IN-USE")
        != core::pmr::String::npos);
    assert(command->recordBufferCopy(*source.payload,
        *destination.payload,
        {sourceId, destinationId, 0, 0, sizeof(expected)},
        diagnostic));
    const std::uint32_t faultWord = 1u;
    rhi::BufferDesc latchDesc{"completion fault latch",
        sizeof(faultWord),
        rhi::ResourceUsage::StorageBuffer | rhi::ResourceUsage::CopySrc
            | rhi::ResourceUsage::CopyDst,
        rhi::MemoryUsage::CpuToGpu,
        rhi::ResourceState::ShaderWrite};
    rhi::BufferDesc faultReadbackDesc{"completion fault readback",
        sizeof(faultWord),
        rhi::ResourceUsage::CopyDst,
        rhi::MemoryUsage::GpuToCpu,
        rhi::ResourceState::CopyDst};
    auto latchNative = resources.createBuffer(latchDesc);
    auto faultReadbackNative = resources.createBuffer(faultReadbackDesc);
    assert(latchNative.success && faultReadbackNative.success);
    std::memcpy(latchNative.payload->backendMappedData(), &faultWord, sizeof(faultWord));
    assert(latchNative.payload->backendFlushMappedWrite(0, sizeof(faultWord), diagnostic));
    const auto latchId = rhi::allocateGpuResourceId();
    const auto faultReadbackId = rhi::allocateGpuResourceId();
    registry->registerResource(latchId, latchNative.payload);
    registry->registerResource(faultReadbackId, faultReadbackNative.payload);
    auto latchBuffer = createVulkanRhiBuffer(
        latchId, latchDesc, latchNative.allocation, latchNative.payload, registry);
    auto faultReadbackBuffer = createVulkanRhiBuffer(faultReadbackId,
        faultReadbackDesc,
        faultReadbackNative.allocation,
        faultReadbackNative.payload,
        registry);
    assert(command->recordBindlessFaultLatchClear(
        *latchBuffer, rhi::ResourceState::Undefined, diagnostic));
    auto faultValidation =
        command->scheduleBindlessFaultReadback(*latchBuffer, *faultReadbackBuffer, diagnostic);
    assert(faultValidation && !faultValidation->validate().success);
    latchBuffer.reset();
    faultReadbackBuffer.reset();
    latchNative = {};
    faultReadbackNative = {};
    retained.reset();
    descriptors.reset();
    arena.reset();
    source = {};
    assert(!observedResource.expired() && !observedDescriptors.expired());
    assert(command->end(diagnostic));
    assert(!command->retainIndexedSnapshot(overflow, diagnostic));
    assert(diagnostic == "RHI-VULKAN-BINDLESS-RETENTION-NOT-RECORDING");
    VulkanRhiTimelineSubmitInfo wait;
    wait.waitSemaphore = timelineOwner;
    wait.waitValue = 1u;
    bool accepted = false;
    assert(command->submit(wait, diagnostic, &accepted) && accepted);
    assert(!command->submittedFenceCompleted());
    assert(!command->waitSubmittedFenceUntil({}, diagnostic));
    assert(diagnostic == "RHI-VULKAN-COMPLETION-DEADLINE-INVALID");
    const auto deadline = Clock::now() + std::chrono::milliseconds(10);
    assert(!command->waitSubmittedFenceUntil(deadline, diagnostic));
    assert(diagnostic == "RHI-VULKAN-COMPLETION-DEADLINE-EXPIRED");
    assert(!drainVulkanRhiSubmittedCommandsUntil(
        Clock::now() + std::chrono::milliseconds(10), diagnostic));
    assert(diagnostic == "RHI-VULKAN-COMPLETION-DEADLINE-EXPIRED");
    assert(!command->waitSubmittedFenceUntil(deadline, diagnostic));
    assert(faultValidation->status() == rhi::BindlessFaultValidationStatus::Pending);
    assert(!observedResource.expired() && !observedDescriptors.expired() && !observedLease.expired()
        && budget->used() == 1u);
    command.reset();
    assert(!observedPreambleCommand.expired());
    retireCompletedVulkanRhiSubmittedCommands();
    assert(!observedResource.expired() && !observedLease.expired());

    VkSemaphoreSignalInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO};
    signal.semaphore = timeline;
    signal.value = 1u;
    assert(vkSignalSemaphore(native.device(), &signal) == VK_SUCCESS);
    assert(
        drainVulkanRhiSubmittedCommandsUntil(Clock::now() + std::chrono::seconds(5), diagnostic));
    assert(diagnostic.empty() && observedPreambleCommand.expired());
    assert(faultValidation->status() == rhi::BindlessFaultValidationStatus::Clean);
    assert(faultValidation->faultBits() == 0u);
    assert(faultValidation->validate().success);
    assert(observedResource.expired() && observedDescriptors.expired() && observedLease.expired()
        && budget->used() == 0u);
    assert(destination.payload->backendInvalidateMappedRead(0, sizeof(expected), diagnostic));
    assert(std::memcmp(destination.payload->backendMappedData(), expected.data(), sizeof(expected))
        == 0);
    retireCompletedVulkanRhiSubmittedCommands();

    // Polling must release storage independently of CPU wrapper lifetime.
    command = createVulkanRhiNativeCommandPayload(resources, diagnostic, retention);
    assert(command);
    lease = core::makeShared<core::AtomicCapacityLease>(budget, 1u);
    observedLease = lease;
    assert(command->retainIndexedSnapshot(lease, diagnostic));
    lease.reset();
    wait.waitValue = 2u;
    assert(command->end(diagnostic) && command->submit(wait, diagnostic, &accepted));
    assert(!command->submittedFenceCompleted() && !observedLease.expired());
    signal.value = 2u;
    assert(vkSignalSemaphore(native.device(), &signal) == VK_SUCCESS);
    const auto pollDeadline = Clock::now() + std::chrono::seconds(5);
    while (!command->submittedFenceCompleted() && Clock::now() < pollDeadline)
        std::this_thread::yield();
    assert(command->submittedFenceCompleted() && observedLease.expired() && !budget->used());
    command.reset();
    retireCompletedVulkanRhiSubmittedCommands();

    // The global submitted owner also preserves indexed storage after the
    // caller releases its command wrapper while a real timeline is pending.
    command = createVulkanRhiNativeCommandPayload(resources, diagnostic, retention);
    lease = core::makeShared<core::AtomicCapacityLease>(budget, 1u);
    observedLease = lease;
    assert(command->retainIndexedSnapshot(lease, diagnostic));
    lease.reset();
    wait.waitValue = 3u;
    assert(command->end(diagnostic) && command->submit(wait, diagnostic, &accepted));
    std::weak_ptr<VulkanRhiNativeCommandPayload> observedCommand = command;
    command.reset();
    retireCompletedVulkanRhiSubmittedCommands();
    assert(!observedCommand.expired() && !observedLease.expired() && budget->used() == 1u);
    signal.value = 3u;
    assert(vkSignalSemaphore(native.device(), &signal) == VK_SUCCESS);
    const auto retireDeadline = Clock::now() + std::chrono::seconds(5);
    while (!observedLease.expired() && Clock::now() < retireDeadline) {
        retireCompletedVulkanRhiSubmittedCommands();
        std::this_thread::yield();
    }
    assert(observedCommand.expired() && observedLease.expired() && !budget->used());

    // Discard is legal, but polling unsubmitted work cannot report completion.
    command = createVulkanRhiNativeCommandPayload(resources, diagnostic, retention);
    assert(command);
    lease = core::makeShared<core::AtomicCapacityLease>(budget, 1u);
    observedLease = lease;
    assert(command->retainIndexedSnapshot(lease, diagnostic));
    lease.reset();
    assert(!command->submittedFenceCompleted() && !observedLease.expired());
    command.reset();
    assert(observedLease.expired() && !budget->used());
    command = createVulkanRhiNativeCommandPayload(resources, diagnostic);
    assert(command && !command->retainIndexedSnapshot(overflow, diagnostic));
    assert(diagnostic == "RHI-BINDLESS-COMMAND-SNAPSHOT-BUDGET");
    assert(!command->retainIndexedPipeline(overflow, diagnostic));
    assert(diagnostic == "RHI-BINDLESS-COMMAND-PIPELINE-BUDGET");
    command.reset();

    // Fill the real retirement queue behind one unsignaled timeline. The next
    // submission must fail before vkQueueSubmit, preserving a retryable command.
    auto pendingBudget =
        core::makeShared<core::AtomicCapacityBudget>(kVulkanMaxAsyncSubmittedCommands);
    std::shared_ptr<VulkanRhiNativeCommandPayload> lastPending;
    wait.waitValue = 4u;
    for (std::size_t i = 0; i < kVulkanMaxAsyncSubmittedCommands; ++i) {
        auto pending = createVulkanRhiNativeCommandPayload(resources, diagnostic, retention);
        auto charge = core::makeShared<core::AtomicCapacityLease>(pendingBudget, 1u);
        assert(charge->charged() && pending->retainIndexedSnapshot(charge, diagnostic));
        assert(
            pending->end(diagnostic) && pending->submit(wait, diagnostic, &accepted) && accepted);
        lastPending = std::move(pending);
    }
    command = createVulkanRhiNativeCommandPayload(resources, diagnostic, retention);
    assert(command->end(diagnostic));
    const auto submits = resources.stats().nativeQueueSubmitCount;
    accepted = true;
    assert(!command->submit(wait, diagnostic, &accepted) && !accepted);
    assert(diagnostic == "RHI-VULKAN-SUBMISSION-RETIREMENT-CAPACITY");
    assert(resources.stats().nativeQueueSubmitCount == submits);
    assert(pendingBudget->used() == kVulkanMaxAsyncSubmittedCommands);
    signal.value = 4u;
    assert(vkSignalSemaphore(native.device(), &signal) == VK_SUCCESS);
    assert(
        lastPending->waitSubmittedFenceUntil(Clock::now() + std::chrono::seconds(5), diagnostic));
    retireCompletedVulkanRhiSubmittedCommands();
    assert(!pendingBudget->used());
    assert(command->submit(wait, diagnostic, &accepted) && accepted);
    assert(command->waitSubmittedFenceUntil(Clock::now() + std::chrono::seconds(5), diagnostic));
    command.reset();
    lastPending.reset();
    retireCompletedVulkanRhiSubmittedCommands();
    wait = {};
    timelineOwner.reset();
    auto drainTimelineOwner =
        VulkanRhiTimelineSemaphorePayload::create(native.runtime(), 0u, diagnostic);
    assert(drainTimelineOwner);
    const VkSemaphore drainTimeline = drainTimelineOwner->nativeHandle();
    auto firstDrainCommand = createVulkanRhiNativeCommandPayload(resources, diagnostic, retention);
    auto secondDrainCommand = createVulkanRhiNativeCommandPayload(resources, diagnostic, retention);
    auto firstDrainLease = core::makeShared<int>(1);
    auto secondDrainLease = core::makeShared<int>(2);
    std::weak_ptr<int> firstDrainObserved = firstDrainLease;
    std::weak_ptr<int> secondDrainObserved = secondDrainLease;
    assert(firstDrainCommand->retainIndexedSnapshot(firstDrainLease, diagnostic));
    assert(secondDrainCommand->retainIndexedSnapshot(secondDrainLease, diagnostic));
    firstDrainLease.reset();
    secondDrainLease.reset();
    wait.waitSemaphore = drainTimelineOwner;
    wait.waitValue = 1u;
    assert(firstDrainCommand->end(diagnostic)
        && firstDrainCommand->submit(wait, diagnostic, &accepted));
    wait.waitValue = 2u;
    assert(secondDrainCommand->end(diagnostic)
        && secondDrainCommand->submit(wait, diagnostic, &accepted));
    std::weak_ptr<VulkanRhiNativeCommandPayload> firstDrainOwned = firstDrainCommand;
    std::weak_ptr<VulkanRhiNativeCommandPayload> secondDrainOwned = secondDrainCommand;
    signal.semaphore = drainTimeline;
    signal.value = 1u;
    assert(vkSignalSemaphore(native.device(), &signal) == VK_SUCCESS);
    assert(firstDrainCommand->waitSubmittedFenceUntil(
        Clock::now() + std::chrono::seconds(5), diagnostic));
    firstDrainCommand.reset();
    secondDrainCommand.reset();
    assert(!drainVulkanRhiSubmittedCommandsUntil(
        Clock::now() + std::chrono::milliseconds(10), diagnostic));
    assert(diagnostic == "RHI-VULKAN-COMPLETION-DEADLINE-EXPIRED");
    assert(firstDrainObserved.expired() && firstDrainOwned.expired());
    assert(!secondDrainObserved.expired() && !secondDrainOwned.expired());
    signal.value = 2u;
    assert(vkSignalSemaphore(native.device(), &signal) == VK_SUCCESS);
    assert(
        drainVulkanRhiSubmittedCommandsUntil(Clock::now() + std::chrono::seconds(5), diagnostic));
    assert(secondDrainObserved.expired() && secondDrainOwned.expired());
    wait = {};
    drainTimelineOwner.reset();
    VulkanRhiDeviceDesc unqueriedDesc;
    unqueriedDesc.label = "unqueried timeline gate";
    unqueriedDesc.runtime = native.runtime();
    unqueriedDesc.capabilityContract =
        rhi::semantics::nativeBackendCapabilityContract(rhi::Backend::Vulkan);
    unqueriedDesc.memoryBudgetBytes = 64u * 1024u * 1024u;
    auto unqueriedDevice = createVulkanRhiDevice(unqueriedDesc);
    assert(unqueriedDevice);
    assert(!unqueriedDevice->capabilities().features.timelineSync);
    assert(!unqueriedDevice->createTimelineSemaphore(0u));
    unqueriedDevice.reset();
    VulkanRhiDeviceDesc headlessDesc;
    headlessDesc.label = "headless shutdown drain";
    headlessDesc.runtime = native.runtime();
    rhi::semantics::vulkan::VulkanDeviceCapabilitySnapshot queriedFeatures;
    queriedFeatures.queriedFromNativeDevice = true;
    queriedFeatures.queriedExtendedFeatures = true;
    queriedFeatures.supportsTimelineSynchronization = native.runtime()->timelineSemaphoreEnabled();
    headlessDesc.capabilityContract =
        rhi::semantics::vulkan::buildVulkanNativeCapabilityContract(queriedFeatures);
    headlessDesc.memoryBudgetBytes = 64u * 1024u * 1024u;
    auto headlessDevice = createVulkanRhiDevice(headlessDesc);
    assert(headlessDevice);
    assert(headlessDevice->capabilities().features.timelineSync);
    auto publicTimeline = headlessDevice->createTimelineSemaphore(0u);
    assert(publicTimeline && publicTimeline->getValue() == 0u);
    publicTimeline->signal(2u);
    publicTimeline->hostWait(2u, 1'000'000'000u);
    assert(publicTimeline->getValue() == 2u);
    auto* bindless = static_cast<rhi::BindlessCrossExtension*>(
        headlessDevice->queryInterface(rhi::kExtensionBindlessCross));
    assert(bindless);
    const auto& nativeLimits = native.runtime()->descriptorIndexing();
    const auto& bindlessCaps = bindless->capabilities();
    assert(nativeLimits.enablementVerified() && bindlessCaps.limitsQueried);
    assert(bindlessCaps.execution == rhi::BindlessExecution::Native);
    assert(bindlessCaps.executablePayload && bindlessCaps.faultReadbackContractVersion == 1u);
    assert(bindlessCaps.support[1][0].operations == rhi::BindlessOperation::None);
    assert(bindlessCaps.support[2][0].operations
        == (rhi::BindlessOperation::Load | rhi::BindlessOperation::Query
            | rhi::BindlessOperation::SampleLevel));
    assert(bindlessCaps.maxBindGroups == nativeLimits.limits.maxBoundDescriptorSets);
    assert(bindlessCaps.maxUniformBufferBindingSize == nativeLimits.limits.maxUniformBufferRange);
    assert(bindlessCaps.maxStorageBufferBindingSize == nativeLimits.limits.maxStorageBufferRange);
    for (const auto& stage : bindlessCaps.stageLimits) {
        assert(stage.sampledTextures == nativeLimits.limits.maxPerStageDescriptorSampledImages);
        assert(stage.samplers == nativeLimits.limits.maxPerStageDescriptorSamplers);
        assert(stage.storageTextures == nativeLimits.limits.maxPerStageDescriptorStorageImages);
        assert(stage.uniformBuffers == nativeLimits.limits.maxPerStageDescriptorUniformBuffers);
        assert(stage.storageBuffers == nativeLimits.limits.maxPerStageDescriptorStorageBuffers);
    }
    rhi::semantics::ShutdownDrainRequest drainRequest;
    drainRequest.deadline = Clock::now() + std::chrono::seconds(5);
    const auto drainReport = headlessDevice->drainForShutdown(drainRequest);
    assert(drainReport.completed());
    assert(drainReport.outcome == rhi::semantics::ShutdownDrainOutcome::Native);
    assert(drainReport.remainingNanoseconds > 0u);
    headlessDevice.reset();
    drainVulkanRhiSubmittedCommandsForShutdown(); // all test work proven complete
    std::puts("Native Vulkan completion deadline, retention and budget release: PASS");
}

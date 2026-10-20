#include "core/cpu_allocator.h"
#include "rhi/backends/metal/metal_native_commands.h"
#include "rhi/backends/metal/metal_native_runtime.h"
#include "rhi/backends/metal/metal_native_swapchain.h"
#include "rhi/backends/metal/metal_native_sync.h"

#include <Metal/Metal.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <dispatch/dispatch.h>
#include <memory>
#include <objc/runtime.h>
#include <thread>

namespace {

// Test-process-only injection at the actual native allocation call. Other
// queues retain their original implementation; no production hook or fake
// successful payload is introduced. All queue API calls here are serialized.
id rejectedAllocationQueue = nil;
IMP originalCommandBufferImplementation = nullptr;
std::atomic<unsigned> rejectedAllocationCalls = 0u;

id rejectCommandBufferAllocation(id receiver, SEL selector)
{
    if (receiver == rejectedAllocationQueue) {
        rejectedAllocationCalls.fetch_add(1u, std::memory_order_relaxed);
        return nil;
    }
    return reinterpret_cast<id (*)(id, SEL)>(originalCommandBufferImplementation)(
        receiver, selector);
}

class ScopedCommandBufferAllocationFailure {
public:
    explicit ScopedCommandBufferAllocationFailure(id<MTLCommandQueue> queue)
    {
        method_ = class_getInstanceMethod(object_getClass(queue), @selector(commandBuffer));
        if (!queue || !method_ || rejectedAllocationQueue)
            return;
        rejectedAllocationQueue = queue;
        rejectedAllocationCalls.store(0u, std::memory_order_relaxed);
        originalCommandBufferImplementation = method_getImplementation(method_);
        method_setImplementation(method_, reinterpret_cast<IMP>(&rejectCommandBufferAllocation));
        installed_ = true;
    }

    ~ScopedCommandBufferAllocationFailure()
    {
        if (!installed_)
            return;
        method_setImplementation(method_, originalCommandBufferImplementation);
        rejectedAllocationQueue = nil;
    }

    ScopedCommandBufferAllocationFailure(const ScopedCommandBufferAllocationFailure&) = delete;
    ScopedCommandBufferAllocationFailure& operator=(
        const ScopedCommandBufferAllocationFailure&) = delete;

    bool installed() const { return installed_; }

    unsigned calls() const { return rejectedAllocationCalls.load(std::memory_order_relaxed); }

private:
    Method method_ = nullptr;
    bool installed_ = false;
};

bool allCommandCapacityAvailable()
{
    std::pmr::string diagnostics;
    const auto capacity = arp::rhi_metal::reserveSharedMetalRhiCommandCapacity(
        arp::rhi_metal::kMetalNativeCommandBufferCapacity, diagnostics);
    return capacity && capacity->charged();
}

bool verifyNativeCreationFailures()
{
    using namespace arp::rhi_metal;
    const auto queueRef = sharedMetalRhiNativeCommandQueue();
    auto queue = reinterpret_cast<id<MTLCommandQueue>>(queueRef.get());
    if (!queue || !allCommandCapacityAvailable())
        return false;
    std::pmr::string diagnostics;
    bool primary = false, drain = false, present = false, prologue = false;
    {
        ScopedCommandBufferAllocationFailure failure(queue);
        if (!failure.installed())
            return false;
        auto payload = createMetalRhiNativeCommandPayload(
            "InjectedPrimaryCreateFailure", nullptr, &diagnostics);
        primary = !payload && failure.calls() == 1u
            && diagnostics == "RHI-METAL-COMMAND-BUFFER-CREATE-FAILED";
    }
    primary = allCommandCapacityAvailable() && primary;
    {
        ScopedCommandBufferAllocationFailure failure(queue);
        if (!failure.installed())
            return false;
        drain = !waitMetalRhiNativeQueueIdle(diagnostics) && failure.calls() == 1u
            && diagnostics == "RHI-METAL-WAIT-IDLE-COMMAND-BUFFER-MISSING";
    }
    drain = allCommandCapacityAvailable() && drain;

    @autoreleasepool {
        arp::rhi::SwapchainDesc desc;
        desc.extent = {8u, 8u};
        desc.format = arp::rhi::TextureFormat::Bgra8Unorm;
        auto stats = createMetalRhiNativeSwapchainStats();
        // The detached native layer is a failure-path fixture, not evidence
        // of a window/surface or visible presentation. No present is committed.
        auto swapchain = createMetalRhiNativeSwapchainPayload(desc, stats);
        if (!swapchain || !swapchain->acquireDrawable(0u, diagnostics)) {
            std::printf("present failure fixture acquire: %s\n", diagnostics.c_str());
            return false;
        }
        {
            ScopedCommandBufferAllocationFailure failure(queue);
            if (!failure.installed())
                return false;
            present = !swapchain->presentDrawable(0u,
                          arp::core::makeShared<MetalRhiNativeFrameSlotCompletionState>(),
                          diagnostics)
                && failure.calls() == 1u
                && diagnostics == "Metal RHI native drawable present requires a command buffer"
                && stats->nativeDrawablePresentCount == 0u
                && stats->nativeDrawablePresentPendingCount == 0u;
        }
        present = allCommandCapacityAvailable()
            && swapchain->acquiredDrawableTexture(0u, diagnostics)
            && swapchain->discardDrawable(0u, diagnostics) && present;
    }

    @autoreleasepool {
        id<MTLCommandBuffer> command = [queue commandBuffer];
        id<MTLBuffer> output = [queue.device newBufferWithLength:256
                                                         options:MTLResourceStorageModeShared];
        if (!command || !output) {
            [output release];
            return false;
        }
        id<MTLBlitCommandEncoder> blit = [command blitCommandEncoder];
        if (!blit) {
            [output release];
            return false;
        }
        [blit fillBuffer:output range:NSMakeRange(0, 256) value:0x5a];
        [blit endEncoding];
        const auto lifetime = arp::core::makeShared<unsigned>(0u);
        const std::shared_ptr<void> commandRef(lifetime, command);
        auto observed = createMetalRhiNativeSharedEventPayload("UncommittedWorkObserved");
        auto gate = createMetalRhiNativeSharedEventPayload("InjectedPrologueGate");
        if (!observed || !gate
            || !encodeMetalRhiNativeSharedEventSignal(commandRef, observed, 1u, diagnostics)) {
            [output release];
            return false;
        }
        auto stats = arp::core::makeShared<MetalRhiNativeCommandStats>();
        stats->nativeCommandBufferCreated = true;
        stats->nativeRuntimeBacked = true;
        auto reservation =
            reserveSharedMetalRhiCommandCapacity(kMetalPrimaryCommandReservation, diagnostics);
        auto payload = arp::core::makeShared<MetalRhiNativeCommandPayload>(
            stats, queueRef, commandRef, nullptr, std::move(reservation));
        auto completion = arp::core::makeShared<MetalRhiCompletionState>();
        MetalRhiNativeQueueStats queueStats;
        if (!payload->finish(diagnostics)) {
            [output release];
            return false;
        }
        {
            ScopedCommandBufferAllocationFailure failure(queue);
            if (!failure.installed()) {
                [output release];
                return false;
            }
            prologue = !payload->submit(queueStats, completion, gate, 1u, nullptr, 0u, diagnostics)
                && failure.calls() == 1u
                && diagnostics == "RHI-METAL-SUBMIT-WAIT-COMMAND-BUFFER-CREATE-FAILED"
                && command.status == MTLCommandBufferStatusNotEnqueued
                && queueStats.nativeQueueSubmitCount == 0u && completion->isComplete()
                && payload->readbackCompletionStatus() == MetalReadbackCompletion::Failed;
        }
        prologue = !payload->submit(queueStats, completion, gate, 1u, nullptr, 0u, diagnostics)
            && diagnostics == "RHI-METAL-COMMAND-BUFFER-ALREADY-SUBMITTED"
            && command.status == MTLCommandBufferStatusNotEnqueued
            && !waitForMetalRhiNativeSharedEvent(observed, 1u, 0u) && prologue;
        payload.reset();
        [output release];
    }
    const bool recovered = allCommandCapacityAvailable();
    std::printf(
        "CommandCapacityReservation/1 native allocation failure primary=%d drain=%d present-uncommitted=%d prologue-uncommitted-terminal=%d discard-recovery=%d\n",
        primary,
        drain,
        present,
        prologue,
        recovered);
    return primary && drain && present && prologue && recovered;
}

bool waitForCompletion(const std::shared_ptr<arp::rhi_metal::MetalRhiCompletionState>& completion)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!completion->isComplete() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return completion->isComplete();
}

bool verifyStartOrder(
    bool alreadySatisfied, std::uint64_t waitValue, bool useWait = true, bool dropPayload = false)
{
    using namespace arp::rhi_metal;
    id<MTLDevice> device = reinterpret_cast<id<MTLDevice>>(metalRhiNativeDeviceHandle());
    if (!device || !metalRhiNativeTimelineSynchronizationAvailable()) {
        std::puts("native device/shared event unavailable");
        return false;
    }
    id<MTLCommandQueue> queue = [device newCommandQueue];
    id<MTLCommandBuffer> command = [queue commandBuffer];
    id<MTLBuffer> output = [device newBufferWithLength:256 options:MTLResourceStorageModeShared];
    if (!queue || !command || !output) {
        [output release];
        [queue release];
        return false;
    }
    auto gate = createMetalRhiNativeSharedEventPayload("SubmitStartGate");
    auto observed = createMetalRhiNativeSharedEventPayload("ProtectedWorkObserved");
    if (!gate || !gate->nativeRuntimeBacked || !observed || !observed->nativeRuntimeBacked) {
        [output release];
        [queue release];
        return false;
    }
    // The +1 owners above and autoreleasepool outlive submit/completion.
    const auto lifetime = arp::core::makeShared<unsigned>(0u);
    const std::shared_ptr<void> queueRef(lifetime, queue);
    const std::shared_ptr<void> commandRef(lifetime, command);
    id<MTLBlitCommandEncoder> blit = [command blitCommandEncoder];
    [blit fillBuffer:output range:NSMakeRange(0, 256) value:0x5a];
    [blit endEncoding];
    std::pmr::string diagnostics;
    if (!encodeMetalRhiNativeSharedEventSignal(commandRef, observed, 1u, diagnostics)) {
        [output release];
        [queue release];
        return false;
    }
    auto stats = arp::core::makeShared<MetalRhiNativeCommandStats>();
    stats->nativeCommandBufferCreated = command != nil;
    stats->nativeRuntimeBacked = command != nil;
    auto reservation =
        reserveSharedMetalRhiCommandCapacity(kMetalPrimaryCommandReservation, diagnostics);
    auto payload = arp::core::makeShared<MetalRhiNativeCommandPayload>(
        stats, queueRef, commandRef, nullptr, reservation);
    auto completion = arp::core::makeShared<MetalRhiCompletionState>();
    MetalRhiNativeQueueStats queueStats;
    bool gates = !payload->submit(queueStats, completion, gate, waitValue, nullptr, 0u, diagnostics)
        && diagnostics == "RHI-METAL-COMMAND-BUFFER-MUST-BE-FINISHED-BEFORE-SUBMIT"
        && command.status == MTLCommandBufferStatusNotEnqueued;
    if (!payload->finish(diagnostics)) {
        std::printf("finish failed: %s\n", diagnostics.c_str());
        [output release];
        [queue release];
        return false;
    }
    // Failure injection uses the same real native object but deliberately omits
    // the private reservation. It must remain uncommitted and fail terminally.
    auto missingReservation =
        arp::core::makeShared<MetalRhiNativeCommandPayload>(stats, queueRef, commandRef, nullptr);
    auto rejectedCompletion = arp::core::makeShared<MetalRhiCompletionState>();
    gates = gates
        && !missingReservation->submit(
            queueStats, rejectedCompletion, gate, waitValue, nullptr, 0u, diagnostics)
        && diagnostics == "RHI-METAL-COMMAND-CAPACITY-RESERVATION-MISSING"
        && rejectedCompletion->isComplete()
        && missingReservation->readbackCompletionStatus() == MetalReadbackCompletion::Failed
        && command.status == MTLCommandBufferStatusNotEnqueued
        && queueStats.nativeQueueSubmitCount == 0u;
    missingReservation.reset();
    auto malformedGate = arp::core::makeShared<MetalRhiNativeSharedEventPayload>(*gate);
    malformedGate->nativeEvent.reset();
    gates = gates
        && !payload->submit(
            queueStats, completion, malformedGate, waitValue, nullptr, 0u, diagnostics)
        && diagnostics == "RHI-METAL-SHARED-EVENT-PAYLOAD-MISSING"
        && queueStats.nativeQueueSubmitCount == 0u
        && command.status == MTLCommandBufferStatusNotEnqueued;
    if (alreadySatisfied)
        signalMetalRhiNativeSharedEvent(gate, waitValue);
    dispatch_semaphore_t completed = dispatch_semaphore_create(0);
    [command addCompletedHandler:^(id<MTLCommandBuffer>) {
        dispatch_semaphore_signal(completed);
    }];
    diagnostics.clear();
    const bool submitted = payload->submit(
        queueStats, completion, useWait ? gate : nullptr, waitValue, nullptr, 0u, diagnostics);
    if (submitted) {
        gates = gates
            && !payload->submit(queueStats,
                completion,
                useWait ? gate : nullptr,
                waitValue,
                nullptr,
                0u,
                diagnostics)
            && diagnostics == "RHI-METAL-COMMAND-BUFFER-ALREADY-SUBMITTED"
            && queueStats.nativeQueueSubmitCount == 1u;
    }
    if (dropPayload)
        payload.reset();
    const bool early = submitted && useWait && !alreadySatisfied
        && waitForMetalRhiNativeSharedEvent(observed, 1u, 200000000u);
    if (useWait && !alreadySatisfied)
        gates = gates && !completion->isComplete();
    // Always release the gate, even when the ordering check has failed.
    signalMetalRhiNativeSharedEvent(gate, waitValue);
    const bool finished = submitted
        && dispatch_semaphore_wait(completed, dispatch_time(DISPATCH_TIME_NOW, 5000000000ll)) == 0;
    gates = gates && (!finished || waitForCompletion(completion));
    bool pixels = finished && command.status == MTLCommandBufferStatusCompleted;
    if (pixels) {
        const auto* bytes = static_cast<const unsigned char*>(output.contents);
        for (unsigned i = 0; i < 256; ++i)
            pixels = pixels && bytes[i] == 0x5a;
    }
    if (finished && payload) {
        gates = gates && payload->readbackCompletionStatus() == MetalReadbackCompletion::Completed;
        gates = gates
            && !payload->submit(queueStats,
                completion,
                useWait ? gate : nullptr,
                waitValue,
                nullptr,
                0u,
                diagnostics)
            && diagnostics == "RHI-METAL-COMMAND-BUFFER-ALREADY-SUBMITTED"
            && queueStats.nativeQueueSubmitCount == 1u;
    }
    std::printf(
        "QueueSubmitStartOrder/1 wait=%d value=%llu satisfied=%d submitted=%d early-work=%d completed=%d bytes=%d gates=%d dropped-payload=%d diagnostic=%s\n",
        useWait,
        static_cast<unsigned long long>(waitValue),
        alreadySatisfied,
        submitted,
        early,
        finished,
        pixels,
        gates,
        dropPayload,
        diagnostics.c_str());
    // Keep the callback semaphore alive if a broken driver misses the deadline.
    if (finished || !submitted)
        dispatch_release(completed);
    [output release];
    [queue release];
    return submitted && finished && pixels && gates && !early;
}

bool verifyCapacityReservation()
{
    using namespace arp::rhi_metal;
    const auto initialCapacity = inspectSharedMetalRhiCommandCapacity();
    constexpr auto count = kMetalNativeCommandBufferCapacity / kMetalPrimaryCommandReservation;
    std::array<std::shared_ptr<MetalRhiNativeCommandPayload>, count> recorded;
    std::pmr::string diagnostics;
    for (auto& payload : recorded) {
        payload = createMetalRhiNativeCommandPayload("ReservedRecording", nullptr, &diagnostics);
        if (!payload || !payload->finish(diagnostics))
            return false;
    }
    bool valid = !createMetalRhiNativeCommandPayload("ExhaustedRecording", nullptr, &diagnostics)
        && diagnostics == "RHI-METAL-COMMAND-CAPACITY-EXHAUSTED";
    valid = valid && !reserveSharedMetalRhiCommandCapacity(1u, diagnostics)
        && diagnostics == "RHI-METAL-COMMAND-CAPACITY-EXHAUSTED";
    valid = valid && !reserveSharedMetalRhiCommandCapacity(UINT64_MAX, diagnostics)
        && diagnostics == "RHI-METAL-COMMAND-CAPACITY-EXHAUSTED";
    valid = valid && !waitMetalRhiNativeQueueIdle(diagnostics)
        && diagnostics == "RHI-METAL-COMMAND-CAPACITY-EXHAUSTED";
    recorded.back().reset();
    recorded.back() = createMetalRhiNativeCommandPayload("AfterDiscard", nullptr, &diagnostics);
    valid = valid && recorded.back() && recorded.back()->finish(diagnostics);

    auto gate = createMetalRhiNativeSharedEventPayload("ReservedSubmitGate");
    auto completion = arp::core::makeShared<MetalRhiCompletionState>();
    MetalRhiNativeQueueStats stats;
    // All count slots are occupied, but the prologue's native slot was already
    // reserved at recording creation. This submit must return without waiting.
    const bool submitted =
        recorded.front()->submit(stats, completion, gate, 1u, nullptr, 0u, diagnostics);
    valid = valid && submitted
        && !createMetalRhiNativeCommandPayload("StillInFlight", nullptr, &diagnostics);
    signalMetalRhiNativeSharedEvent(gate, 1u);
    valid = valid && submitted && waitForCompletion(completion);
    // Keep the completed wrapper alive: completion, not wrapper destruction,
    // returns the two native slots for the next recording.
    auto afterCompletion =
        createMetalRhiNativeCommandPayload("AfterCompletion", nullptr, &diagnostics);
    valid = valid && afterCompletion && afterCompletion->finish(diagnostics);
    for (auto& payload : recorded)
        payload.reset();
    afterCompletion.reset();
    auto allCapacity =
        reserveSharedMetalRhiCommandCapacity(kMetalNativeCommandBufferCapacity, diagnostics);
    valid = valid && allCapacity && allCapacity->charged();
    const auto saturatedCapacity = inspectSharedMetalRhiCommandCapacity();
    valid = valid && saturatedCapacity.capacity == kMetalNativeCommandBufferCapacity
        && saturatedCapacity.charged == kMetalNativeCommandBufferCapacity
        && saturatedCapacity.peakCharged == kMetalNativeCommandBufferCapacity
        && saturatedCapacity.rejectionCount >= initialCapacity.rejectionCount + 2u;
    allCapacity.reset();
    const auto recoveredCapacity = inspectSharedMetalRhiCommandCapacity();
    valid = valid && recoveredCapacity.charged == 0u;
    std::printf(
        "CommandCapacityReservation/1 exact=%llu exhaustion/discard/in-flight/completion/drain=%d\n",
        static_cast<unsigned long long>(count),
        valid);
    return valid;
}

bool verifyRepeatedDiscardCapacity()
{
    using namespace arp::rhi_metal;
    std::pmr::string diagnostics;
    bool valid = true;
    constexpr auto attempts = kMetalNativeCommandBufferCapacity * 2u;
    for (std::uint64_t attempt = 0u; attempt < attempts; ++attempt) {
        auto payload =
            createMetalRhiNativeCommandPayload("RepeatedDiscardRecording", nullptr, &diagnostics);
        valid = valid && payload && payload->finish(diagnostics);
        if (!valid)
            break;
        payload->abandonUnsubmitted();
        payload.reset();
        valid = valid && allCommandCapacityAvailable();
        if (!valid)
            break;
    }
    std::printf(
        "CommandCapacityReservation/1 repeated-discard attempts=%llu recovered=%d diagnostic=%s\n",
        static_cast<unsigned long long>(attempts),
        valid,
        diagnostics.c_str());
    return valid;
}

bool verifyBindlessRetentionCapacity()
{
    using namespace arp::rhi_metal;
    std::pmr::string diagnostics;
    auto payload = createMetalRhiNativeCommandPayload(
        "BindlessRetentionCapacity", nullptr, &diagnostics, 1u, 1u);
    if (!payload)
        return false;

    auto pipelineOwner = std::make_shared<std::uint32_t>(1u);
    std::weak_ptr<std::uint32_t> weakPipeline = pipelineOwner;
    std::shared_ptr<const void> pipeline = pipelineOwner;
    bool valid = payload->retainIndexedPipeline(pipeline, diagnostics)
        && payload->retainIndexedPipeline(pipeline, diagnostics);
    pipelineOwner.reset();
    pipeline.reset();
    auto excessPipelineOwner = std::make_shared<std::uint32_t>(2u);
    std::shared_ptr<const void> excessPipeline = excessPipelineOwner;
    valid = valid && !payload->retainIndexedPipeline(excessPipeline, diagnostics)
        && diagnostics == "RHI-BINDLESS-PIPELINE-RETENTION-CAPACITY" && !weakPipeline.expired();

    auto snapshotOwner = std::make_shared<std::uint32_t>(3u);
    std::weak_ptr<std::uint32_t> weakSnapshot = snapshotOwner;
    std::shared_ptr<const void> snapshot = snapshotOwner;
    valid = valid && payload->retainIndexedSnapshot(snapshot, diagnostics)
        && payload->retainIndexedSnapshot(snapshot, diagnostics);
    snapshotOwner.reset();
    snapshot.reset();
    auto excessSnapshotOwner = std::make_shared<std::uint32_t>(4u);
    std::shared_ptr<const void> excessSnapshot = excessSnapshotOwner;
    valid = valid && !payload->retainIndexedSnapshot(excessSnapshot, diagnostics)
        && diagnostics == "RHI-BINDLESS-SNAPSHOT-RETENTION-CAPACITY" && !weakSnapshot.expired();

    payload->abandonUnsubmitted();
    valid = valid && weakPipeline.expired() && weakSnapshot.expired();
    std::printf("BindlessCommandRetention/1 bounded/deduplicated/abandon=%d\n", valid);
    return valid;
}

} // namespace

int main()
{
    @autoreleasepool {
        const bool creationFailures = verifyNativeCreationFailures();
        const bool noWait = verifyStartOrder(true, 0u, false);
        const bool zero = verifyStartOrder(true, 0u);
        const bool satisfied = verifyStartOrder(true, 1u);
        const bool protectedWork = verifyStartOrder(false, 1u);
        const bool maximum = verifyStartOrder(false, UINT64_MAX, true, true);
        const bool capacity = verifyCapacityReservation();
        const bool repeatedDiscard = verifyRepeatedDiscardCapacity();
        const bool bindlessRetention = verifyBindlessRetentionCapacity();
        return creationFailures && noWait && zero && satisfied && protectedWork && maximum
                && capacity && repeatedDiscard && bindlessRetention
            ? 0
            : 1;
    }
}

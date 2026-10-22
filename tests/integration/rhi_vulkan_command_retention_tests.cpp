#include "rhi/backends/vulkan/vulkan_backend_objects.h"
#include "rhi/backends/vulkan/vulkan_retained_transfer_command.h"
#include "tests/fixtures/renderdoc_capture.h"
#include "tests/fixtures/vulkan_headless_runtime.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string_view>
#include <type_traits>

namespace {
using namespace arp;
using namespace rhi;
using namespace rhi_vulkan;
using Storage = common::CommandRetentionStorage;
static_assert(!std::is_copy_constructible_v<VulkanRhiRetainedTransferCommand>
    && !std::is_move_constructible_v<VulkanRhiRetainedTransferCommand>);

constexpr std::uint64_t kMetadataBytes = 64u * 1024u;

core::UniquePtr<Storage> storage(
    const std::shared_ptr<core::CpuMemoryQuota>& quota, std::uint32_t owners = 2u)
{
    const char* diagnostic = nullptr;
    auto result =
        Storage::prepare({1u, true, owners, 0u, 0u, 0u, kMetadataBytes}, quota, diagnostic);
    assert(result && !diagnostic);
    return result;
}

void contract()
{
    auto quota = core::makeShared<core::CpuMemoryQuota>(kMetadataBytes);
    auto registry = core::makeShared<VulkanRhiResourceRegistry>();
    VulkanRhiNativeResourceAllocator resources;
    core::pmr::String diagnostic;
    diagnostic.reserve(512u);
    auto prepared = storage(quota);
    assert(!VulkanRhiRetainedTransferCommand::prepare(
        resources, registry, std::move(prepared), diagnostic));
    assert(diagnostic == "RHI-VULKAN-RETENTION-NATIVE-RUNTIME-REQUIRED" && !quota->liveBytes());
    prepared = storage(quota);
    assert(prepared->sealForSubmission());
    assert(!VulkanRhiRetainedTransferCommand::prepare(
        resources, registry, std::move(prepared), diagnostic));
    assert(diagnostic == "RHI-VULKAN-RETENTION-PREPARATION-STATE" && !quota->liveBytes());
    prepared = storage(quota);
    quota->setLimitBytes(kMetadataBytes + 1u);
    assert(!VulkanRhiRetainedTransferCommand::prepare(
        resources, registry, std::move(prepared), diagnostic));
    assert(diagnostic == "RHI-COMMAND-RETENTION-CPU-QUOTA-MISMATCH" && !quota->liveBytes());
    CommandEncoderDesc request;
    request.retentionBudget = {1u, true, 2u, 0u, 0u, 0u, kMetadataBytes};
    assert(!createVulkanRhiNativeCommandPayload(resources, diagnostic, request));
    assert(diagnostic == "RHI-COMMAND-RETENTION-UNSUPPORTED"
        && !resources.stats().nativeCommandPoolCreateCount);
    std::puts(
        "retention contract: explicit public admission remains closed; invalid private preparation rejected");
}

bool failSubmit = false;
bool failFenceObservation = false;
unsigned clearCreationFailure = 0u;
bool missingClearRecord = false;
std::array<unsigned, 3u> clearNativeLive{};

VKAPI_ATTR VkResult VKAPI_CALL clearCreatePass(VkDevice device,
    const VkRenderPassCreateInfo* info,
    const VkAllocationCallbacks* allocation,
    VkRenderPass* pass)
{
    if (clearCreationFailure == 1u) {
        clearCreationFailure = 0u;
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    const auto result = vkCreateRenderPass(device, info, allocation, pass);
    if (result == VK_SUCCESS)
        ++clearNativeLive[0];
    return result;
}

VKAPI_ATTR void VKAPI_CALL clearDestroyPass(
    VkDevice device, VkRenderPass pass, const VkAllocationCallbacks* allocation)
{
    assert(clearNativeLive[0]);
    --clearNativeLive[0];
    vkDestroyRenderPass(device, pass, allocation);
}

VKAPI_ATTR VkResult VKAPI_CALL clearCreateView(VkDevice device,
    const VkImageViewCreateInfo* info,
    const VkAllocationCallbacks* allocation,
    VkImageView* view)
{
    if (clearCreationFailure == 2u) {
        clearCreationFailure = 0u;
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    const auto result = vkCreateImageView(device, info, allocation, view);
    if (result == VK_SUCCESS)
        ++clearNativeLive[1];
    return result;
}

VKAPI_ATTR void VKAPI_CALL clearDestroyView(
    VkDevice device, VkImageView view, const VkAllocationCallbacks* allocation)
{
    assert(clearNativeLive[1]);
    --clearNativeLive[1];
    vkDestroyImageView(device, view, allocation);
}

VKAPI_ATTR VkResult VKAPI_CALL clearCreateFrame(VkDevice device,
    const VkFramebufferCreateInfo* info,
    const VkAllocationCallbacks* allocation,
    VkFramebuffer* frame)
{
    if (clearCreationFailure == 3u) {
        clearCreationFailure = 0u;
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    const auto result = vkCreateFramebuffer(device, info, allocation, frame);
    if (result == VK_SUCCESS)
        ++clearNativeLive[2];
    return result;
}

VKAPI_ATTR void VKAPI_CALL clearDestroyFrame(
    VkDevice device, VkFramebuffer frame, const VkAllocationCallbacks* allocation)
{
    assert(clearNativeLive[2]);
    --clearNativeLive[2];
    vkDestroyFramebuffer(device, frame, allocation);
}

VKAPI_ATTR VkResult VKAPI_CALL queueSubmit(
    VkQueue queue, std::uint32_t count, const VkSubmitInfo* info, VkFence fence)
{
    if (failSubmit) {
        failSubmit = false;
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    return vkQueueSubmit(queue, count, info, fence);
}

VKAPI_ATTR VkResult VKAPI_CALL fenceStatus(VkDevice device, VkFence fence)
{
    return failFenceObservation ? VK_ERROR_OUT_OF_HOST_MEMORY : vkGetFenceStatus(device, fence);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL resolve(VkDevice device, const char* name)
{
    if (missingClearRecord
        && (std::strcmp(name, "vkCmdBeginRenderPass") == 0
            || std::strcmp(name, "vkCmdEndRenderPass") == 0))
        return nullptr;
    if (std::strcmp(name, "vkQueueSubmit") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(queueSubmit);
    if (std::strcmp(name, "vkGetFenceStatus") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(fenceStatus);
    if (std::strcmp(name, "vkCreateRenderPass") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(clearCreatePass);
    if (std::strcmp(name, "vkDestroyRenderPass") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(clearDestroyPass);
    if (std::strcmp(name, "vkCreateImageView") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(clearCreateView);
    if (std::strcmp(name, "vkDestroyImageView") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(clearDestroyView);
    if (std::strcmp(name, "vkCreateFramebuffer") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(clearCreateFrame);
    if (std::strcmp(name, "vkDestroyFramebuffer") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(clearDestroyFrame);
    return vkGetDeviceProcAddr(device, name);
}

class CopyFixture final {
public:
    explicit CopyFixture(std::shared_ptr<VulkanRhiRuntime> runtime)
        : runtime(std::move(runtime)),
          allocator({"retained transfer", 16u * 1024u * 1024u}, this->runtime),
          resources(this->runtime, &allocator),
          registry(core::makeShared<VulkanRhiResourceRegistry>()),
          quota(core::makeShared<core::CpuMemoryQuota>(kMetadataBytes))
    {
        diagnostic.reserve(512u);
        source = buffer(ResourceUsage::CopySrc, MemoryUsage::CpuToGpu, ResourceState::CopySrc);
        destination = buffer(ResourceUsage::CopyDst, MemoryUsage::GpuToCpu, ResourceState::CopyDst);
        for (std::uint32_t i = 0; i < expected.size(); ++i)
            expected[i] = 0x3ab90000u + i;
        const auto src = vulkanRhiNativePayload(*source);
        const auto dst = vulkanRhiNativePayload(*destination);
        assert(src->backendMappedData() && dst->backendMappedData());
        std::memcpy(src->backendMappedData(), expected.data(), sizeof(expected));
        std::memset(dst->backendMappedData(), 0, sizeof(expected));
        assert(src->backendFlushMappedWrite(0u, sizeof(expected), diagnostic));
        assert(dst->backendFlushMappedWrite(0u, sizeof(expected), diagnostic));
        copy = {source->id(), destination->id(), 0u, 0u, sizeof(expected)};
    }

    core::UniquePtr<GpuBuffer> buffer(
        ResourceUsage usage, MemoryUsage memory, ResourceState state, std::uint64_t bytes = 256u)
    {
        BufferDesc desc{"retained buffer", bytes, usage, memory, state};
        auto native = resources.createBuffer(desc);
        assert(native.success && native.payload && native.allocation.success);
        const GpuResourceId id{nextId++};
        auto object = createVulkanRhiBuffer(id, desc, native.allocation, native.payload, registry);
        assert(object);
        registry->registerResource(id, native.payload);
        registry->registerBufferObject(id, *object);
        return object;
    }

    core::UniquePtr<VulkanRhiRetainedTransferCommand> command(std::uint32_t owners = 2u)
    {
        auto result = VulkanRhiRetainedTransferCommand::prepare(
            resources, registry, storage(quota, owners), diagnostic);
        if (!result)
            std::fprintf(stderr, "retained preparation: %s\n", diagnostic.c_str());
        assert(result);
        return result;
    }

    void verify(bool copied = true)
    {
        const auto dst = vulkanRhiNativePayload(*destination);
        assert(dst->backendInvalidateMappedRead(0u, sizeof(expected), diagnostic));
        std::array<std::uint32_t, 64> actual{};
        std::memcpy(actual.data(), dst->backendMappedData(), sizeof(actual));
        if (copied)
            assert(actual == expected);
        else
            for (const auto value : actual)
                assert(!value);
    }

    std::shared_ptr<VulkanRhiRuntime> runtime;
    VulkanRhiAllocator allocator;
    VulkanRhiNativeResourceAllocator resources;
    std::shared_ptr<VulkanRhiResourceRegistry> registry;
    std::shared_ptr<core::CpuMemoryQuota> quota;
    core::pmr::String diagnostic;
    std::array<std::uint32_t, 64> expected{};
    core::UniquePtr<GpuBuffer> source, destination;
    BufferCopyDesc copy;
    std::uint64_t nextId = 1u;
};

void rejection(const std::shared_ptr<VulkanRhiRuntime>& runtime)
{
    CopyFixture f(runtime);
    for (const std::uint32_t capacity : {0u, 1u}) {
        auto command = f.command(capacity);
        const auto before = f.resources.stats().nativeBufferCopyRecordCount;
        assert(!command->copy(*f.source, *f.destination, f.copy, f.diagnostic));
        assert(f.diagnostic == "RHI-COMMAND-RETENTION-OWNER-CAPACITY"
            && !command->retentionStorage()->resourceOwnerCount()
            && f.resources.stats().nativeBufferCopyRecordCount == before);
        if (capacity == 1u) {
            const ResourceMemoryBarrierDesc barrier{f.destination->id(),
                PipelineStage::Transfer,
                PipelineStage::Transfer,
                ResourceAccess::TransferWrite,
                ResourceAccess::TransferWrite};
            assert(command->dependency(*f.destination, barrier, f.diagnostic));
            assert(command->retentionStorage()->resourceOwnerCount() == 1u);
            assert(!command->copy(*f.source, *f.destination, f.copy, f.diagnostic));
            assert(command->retentionStorage()->resourceOwnerCount() == 1u
                && f.diagnostic == "RHI-COMMAND-RETENTION-OWNER-CAPACITY"
                && f.resources.stats().nativeBufferCopyRecordCount == before);
        }
        assert(command->finish(f.diagnostic) && command->submit({}, f.diagnostic));
        assert(command->waitUntil(
            std::chrono::steady_clock::now() + std::chrono::seconds(5), f.diagnostic));
        f.verify(false);
        retireCompletedVulkanRhiSubmittedCommands();
        command.reset();
        assert(!f.quota->liveBytes());
    }
    auto command = f.command();
    const auto before = f.resources.stats().nativeBufferCopyRecordCount;
    for (unsigned invalid = 0u; invalid < 7u; ++invalid) {
        auto desc = f.copy;
        if (invalid == 0u)
            desc.sizeBytes = 0u;
        if (invalid == 1u)
            desc.sizeBytes = sizeof(f.expected) + 4u;
        if (invalid == 2u)
            desc.sourceOffsetBytes = 1u;
        if (invalid == 3u)
            desc.destinationOffsetBytes = std::numeric_limits<std::uint64_t>::max() - 3u;
        if (invalid == 4u)
            desc.sourceBuffer = {};
        if (invalid == 5u)
            desc.sizeBytes = 3u;
        if (invalid == 6u)
            desc.destinationBuffer = desc.sourceBuffer;
        assert(!command->copy(*f.source, *f.destination, desc, f.diagnostic));
        assert(f.diagnostic == "RHI-VULKAN-RETENTION-BUFFER-COPY-DESCRIPTOR"
            && !command->retentionStorage()->resourceOwnerCount());
    }
    auto wrongUsage =
        f.buffer(ResourceUsage::UniformBuffer, MemoryUsage::CpuToGpu, ResourceState::CopySrc);
    auto desc = f.copy;
    desc.sourceBuffer = wrongUsage->id();
    assert(!command->copy(*wrongUsage, *f.destination, desc, f.diagnostic));
    auto wrongState =
        f.buffer(ResourceUsage::CopySrc, MemoryUsage::CpuToGpu, ResourceState::ShaderRead);
    desc.sourceBuffer = wrongState->id();
    assert(!command->copy(*wrongState, *f.destination, desc, f.diagnostic));
    {
        core::MutationJournal pending;
        ResourceBarrierDesc state;
        state.resource = f.source->id();
        state.before = state.after = ResourceState::CopySrc;
        assert(transitionVulkanRhiBuffer(*f.source, state, &pending).success);
        assert(!command->copy(*f.source, *f.destination, f.copy, f.diagnostic)
            && f.diagnostic == "RHI-VULKAN-RETENTION-BUFFER-STATE-UNCOMMITTED"
            && !command->retentionStorage()->resourceOwnerCount());
    }
    assert(f.source->state() == ResourceState::CopySrc);
    const auto original = vulkanRhiNativePayload(*f.source);
    auto otherRegistry = core::makeShared<VulkanRhiResourceRegistry>();
    otherRegistry->registerResource(f.source->id(), original);
    auto foreign = VulkanRhiRetainedTransferCommand::prepare(
        f.resources, otherRegistry, storage(f.quota), f.diagnostic);
    assert(foreign && !foreign->copy(*f.source, *f.destination, f.copy, f.diagnostic)
        && f.diagnostic == "RHI-VULKAN-RETENTION-BUFFER-REGISTRATION");
    auto unrelated = core::makeShared<int>(0);
    f.registry->registerResource(f.source->id(), {unrelated, original.get()});
    assert(!command->copy(*f.source, *f.destination, f.copy, f.diagnostic)
        && f.diagnostic == "RHI-VULKAN-RETENTION-BUFFER-REGISTRATION");
    f.registry->registerResource(f.source->id(), original);
    f.destination->sealWrites();
    assert(!command->copy(*f.source, *f.destination, f.copy, f.diagnostic)
        && f.diagnostic == "RHI-BUFFER-GPU-WRITES-SEALED");
    assert(!command->retentionStorage()->resourceOwnerCount()
        && f.resources.stats().nativeBufferCopyRecordCount == before);
    std::puts(
        "native rejection: atomic 0/1 owner ceilings, descriptor/usage/state/registry/control-block/write-seal gates PASS");
}

void completion(const std::shared_ptr<VulkanRhiRuntime>& runtime, bool dropWrapper)
{
    CopyFixture f(runtime);
    auto command = f.command();
    f.quota->setLimitBytes(kMetadataBytes + 1u);
    assert(!command->copy(*f.source, *f.destination, f.copy, f.diagnostic)
        && f.diagnostic == "RHI-COMMAND-RETENTION-CPU-QUOTA-MISMATCH");
    f.quota->setLimitBytes(kMetadataBytes);
    ResourceMemoryBarrierDesc barrier{f.destination->id(),
        PipelineStage::Transfer,
        PipelineStage::Transfer,
        ResourceAccess::TransferWrite,
        ResourceAccess::TransferWrite};
    auto invalidBarrier = barrier;
    invalidBarrier.sourceStages = PipelineStage::ComputeShader;
    assert(!command->dependency(*f.destination, invalidBarrier, f.diagnostic)
        && !command->retentionStorage()->resourceOwnerCount());
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    for (unsigned i = 0u; i < 32u; ++i) {
        assert(command->copy(*f.source, *f.destination, f.copy, f.diagnostic));
        assert(command->dependency(*f.destination, barrier, f.diagnostic));
        assert(command->retentionStorage()->resourceOwnerCount() == 2u);
    }
    assert(command->finish(f.diagnostic));
    assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
    assert(!command->copy(*f.source, *f.destination, f.copy, f.diagnostic));
    const std::weak_ptr<void> source = vulkanRhiNativePayload(*f.source);
    f.source.reset();
    auto gate = VulkanRhiTimelineSemaphorePayload::create(runtime, 0u, f.diagnostic);
    assert(gate);
    VulkanRhiTimelineSubmitInfo wait{gate, 1u};
    f.quota->setLimitBytes(kMetadataBytes + 1u);
    assert(!command->submit(wait, f.diagnostic)
        && f.diagnostic == "RHI-COMMAND-RETENTION-CPU-QUOTA-MISMATCH");
    f.quota->setLimitBytes(kMetadataBytes);
    const auto beforeSubmit = core::defaultCpuAllocator().stats().allocationCount;
    assert(command->submit(wait, f.diagnostic));
    assert(core::defaultCpuAllocator().stats().allocationCount == beforeSubmit);
    assert(!command->completed() && !source.expired());
    assert(!command->waitUntil(std::chrono::steady_clock::now(), f.diagnostic));
    failFenceObservation = true;
    assert(!command->completed() && !source.expired());
    failFenceObservation = false;
    if (dropWrapper)
        command.reset();
    retireCompletedVulkanRhiSubmittedCommands();
    assert(!source.expired() && f.quota->liveBytes());
    assert(gate->hostSignal(1u) == VK_SUCCESS);
    if (command) {
        assert(command->waitUntil(
            std::chrono::steady_clock::now() + std::chrono::seconds(5), f.diagnostic));
        assert(command->completed() && !command->retentionStorage());
        assert(f.quota->liveBytes());
    } else {
        assert(!waitIdleAndRetireVulkanRhiSubmittedCommands(runtime));
    }
    retireCompletedVulkanRhiSubmittedCommands();
    assert(source.expired());
    f.verify();
    command.reset();
    assert(!f.quota->liveBytes());
    std::printf(
        "native completion: 32 copies, no core record/finish/submit allocations, exact readback, delayed retirement, wrapper-dropped=%d PASS\n",
        dropWrapper);
}

void cancellationAndFailures(const std::shared_ptr<VulkanRhiRuntime>& runtime)
{
    CopyFixture f(runtime);
    auto prepared = storage(f.quota);
    const auto remaining = kMetadataBytes - f.quota->liveBytes();
    assert(f.quota->tryReserve(remaining));
    assert(!VulkanRhiRetainedTransferCommand::prepare(
        f.resources, f.registry, std::move(prepared), f.diagnostic));
    assert(f.diagnostic == "RHI-COMMAND-RETENTION-PREPARATION-ALLOCATION-FAILED"
        && f.quota->liveBytes() == remaining);
    f.quota->release(remaining);
    assert(!f.quota->liveBytes());
    auto command = f.command();
    assert(command->copy(*f.source, *f.destination, f.copy, f.diagnostic));
    const std::weak_ptr<void> source = vulkanRhiNativePayload(*f.source);
    f.source.reset();
    assert(!source.expired());
    command.reset();
    assert(source.expired() && !f.quota->liveBytes());
    f.verify(false);

    f.source = f.buffer(ResourceUsage::CopySrc, MemoryUsage::CpuToGpu, ResourceState::CopySrc);
    f.copy.sourceBuffer = f.source->id();
    command = f.command();
    assert(command->copy(*f.source, *f.destination, f.copy, f.diagnostic));
    const std::weak_ptr<void> uncertain = vulkanRhiNativePayload(*f.source);
    f.source.reset();
    assert(command->finish(f.diagnostic));
    failSubmit = true;
    assert(!command->submit({}, f.diagnostic) && !failSubmit);
    assert(f.diagnostic == "RHI-VULKAN-RETENTION-SUBMIT-UNPROVEN"
        && command->retentionStorage()->state() == Storage::State::Submitted);
    assert(!command->completed() && !uncertain.expired());
    command.reset();
    retireCompletedVulkanRhiSubmittedCommands();
    assert(!uncertain.expired() && f.quota->liveBytes());
    assert(!waitIdleAndRetireVulkanRhiSubmittedCommands(runtime));
    assert(uncertain.expired() && !f.quota->liveBytes());
    f.verify(false);
    std::puts(
        "native lifetime: preparation exhaustion, abandonment, failed queue quarantine and real idle retirement PASS");
}

void stateTransactions(const std::shared_ptr<VulkanRhiRuntime>& runtime, bool unproven)
{
    CopyFixture f(runtime);
    f.source = f.buffer(ResourceUsage::StorageBuffer | ResourceUsage::CopySrc,
        MemoryUsage::CpuToGpu,
        ResourceState::ShaderWrite);
    f.copy.sourceBuffer = f.source->id();
    {
        const auto native = vulkanRhiNativePayload(*f.source);
        std::memcpy(native->backendMappedData(), f.expected.data(), sizeof(f.expected));
        assert(native->backendFlushMappedWrite(0u, sizeof(f.expected), f.diagnostic));
    }
    ResourceBarrierDesc barrier;
    barrier.resource = f.source->id();
    barrier.before = ResourceState::ShaderWrite;
    barrier.after = ResourceState::CopySrc;
    barrier.sourceStages = PipelineStage::ComputeShader;
    barrier.destinationStages = PipelineStage::Transfer;
    barrier.sourceAccess = ResourceAccess::ShaderWrite;
    barrier.destinationAccess = ResourceAccess::TransferRead;
    {
        auto zero = f.command(0u);
        const auto before = f.resources.stats().nativeBufferBarrierRecordCount;
        assert(!zero->transition(*f.source, barrier, f.diagnostic)
            && f.diagnostic == "RHI-COMMAND-RETENTION-OWNER-CAPACITY"
            && !zero->retentionStorage()->resourceOwnerCount()
            && f.source->state() == ResourceState::ShaderWrite
            && vulkanRhiBufferStateCommitted(*f.source)
            && f.resources.stats().nativeBufferBarrierRecordCount == before);
    }
    for (const bool sealed : {false, true}) {
        auto cancelled = f.command();
        const auto before = f.resources.stats().nativeBufferBarrierRecordCount;
        for (unsigned i = 0u; i < 5u; ++i) {
            auto invalid = barrier;
            if (i == 0u)
                invalid.sourceStages = PipelineStage::None;
            if (i == 1u)
                invalid.destinationAccess = ResourceAccess::None;
            if (i == 2u)
                invalid.before = ResourceState::CopyDst;
            if (i == 3u)
                invalid.destinationStages = PipelineStage::ComputeShader;
            if (i == 4u)
                invalid.resource = {};
            assert(!cancelled->transition(*f.source, invalid, f.diagnostic)
                && !cancelled->retentionStorage()->resourceOwnerCount()
                && vulkanRhiBufferStateCommitted(*f.source)
                && f.resources.stats().nativeBufferBarrierRecordCount == before);
        }
        {
            core::MutationJournal other;
            auto same = barrier;
            same.after = same.before;
            assert(transitionVulkanRhiBuffer(*f.source, same, &other).success);
            assert(!cancelled->transition(*f.source, barrier, f.diagnostic)
                && !cancelled->retentionStorage()->resourceOwnerCount());
        }
        assert(cancelled->transition(*f.source, barrier, f.diagnostic));
        assert(!vulkanRhiBufferStateCommitted(*f.source)
            && f.source->state() == ResourceState::CopySrc);
        auto competing = f.command();
        assert(!competing->copy(*f.source, *f.destination, f.copy, f.diagnostic)
            && f.diagnostic == "RHI-VULKAN-RETENTION-BUFFER-STATE-UNCOMMITTED");
        if (sealed)
            assert(cancelled->finish(f.diagnostic));
        cancelled.reset();
        assert(vulkanRhiBufferStateCommitted(*f.source)
            && f.source->state() == ResourceState::ShaderWrite);
    }
    {
        const auto payload = vulkanRhiNativePayload(*f.source);
        {
            auto alias =
                createVulkanRhiBuffer(f.source->id(), f.source->desc(), {}, payload, f.registry);
            auto rejected = f.command();
            assert(!rejected->transition(*alias, barrier, f.diagnostic)
                && f.diagnostic == "RHI-VULKAN-RETENTION-BUFFER-REGISTRATION"
                && !rejected->retentionStorage()->resourceOwnerCount());
        }
        f.registry->registerResource(f.source->id(), payload);
        f.registry->registerBufferObject(f.source->id(), *f.source);
    }
    auto command = f.command();
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    assert(command->transition(*f.source, barrier, f.diagnostic)
        && command->copy(*f.source, *f.destination, f.copy, f.diagnostic)
        && command->finish(f.diagnostic));
    assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
    f.quota->setLimitBytes(kMetadataBytes + 1u);
    assert(!command->submit({}, f.diagnostic) && !vulkanRhiBufferStateCommitted(*f.source));
    f.quota->setLimitBytes(kMetadataBytes);
    if (unproven) {
        failSubmit = true;
        assert(!command->submit({}, f.diagnostic) && !failSubmit
            && f.diagnostic == "RHI-VULKAN-RETENTION-SUBMIT-UNPROVEN");
        assert(!vulkanRhiBufferStateCommitted(*f.source));
        command.reset();
        assert(!waitIdleAndRetireVulkanRhiSubmittedCommands(runtime));
        auto retry = barrier;
        retry.before = retry.after = ResourceState::CopySrc;
        assert(!vulkanRhiBufferStateCommitted(*f.source)
            && !transitionVulkanRhiBuffer(*f.source, retry).success);
        f.verify(false);
    } else {
        auto gate = VulkanRhiTimelineSemaphorePayload::create(runtime, 0u, f.diagnostic);
        const auto beforeSubmit = core::defaultCpuAllocator().stats().allocationCount;
        assert(gate && command->submit({gate, 1u}, f.diagnostic));
        assert(core::defaultCpuAllocator().stats().allocationCount == beforeSubmit
            && vulkanRhiBufferStateCommitted(*f.source)
            && f.source->state() == ResourceState::CopySrc && !command->completed());
        const std::weak_ptr<void> source = vulkanRhiNativePayload(*f.source);
        f.source.reset();
        command.reset();
        assert(!source.expired() && gate->hostSignal(1u) == VK_SUCCESS);
        assert(!waitIdleAndRetireVulkanRhiSubmittedCommands(runtime));
        assert(source.expired());
        f.verify();
    }
    assert(!f.quota->liveBytes());
    std::printf(
        "native state transactions: atomic gates, exact journal, cancellation, queue commit/terminal invalidation, no hot allocations, unproven=%d PASS\n",
        unproven);
}

// The ordinary allocating clear is cold fixture preparation. Only the retained
// barriers/copy/finish/submit below establish the bounded private texture slice.
class TextureFixture final {
public:
    TextureFixture(const std::shared_ptr<VulkanRhiRuntime>& runtime, TextureFormat format)
        : f(runtime), depth(format == TextureFormat::D32Float)
    {
        TextureDesc desc;
        desc.label = "Retained.Texture.KnownTexels";
        desc.extent = {5u, 3u, 1u};
        desc.format = format;
        desc.mipLevelCount = 2u;
        desc.arrayLayerCount = 2u;
        desc.usage = TextureUsage::RenderAttachment | TextureUsage::Sampled | TextureUsage::CopySrc;
        texture = makeTexture(desc);
        range.aspect = depth ? TextureAspect::Depth : TextureAspect::Color;
        untouched = range;
        untouched.baseMipLevel = untouched.baseArrayLayer = 1u;
        seed();
        f.destination =
            f.buffer(ResourceUsage::CopyDst, MemoryUsage::GpuToCpu, ResourceState::CopyDst, 1024u);
        const auto staging = vulkanRhiNativePayload(*f.destination);
        std::memset(staging->backendMappedData(), 0xa5, 1024u);
        assert(staging->backendFlushMappedWrite(0u, 1024u, f.diagnostic));
        copy.sourceTexture = texture->id();
        copy.destinationBuffer = f.destination->id();
        copy.textureSubresources = range;
        copy.bufferOffsetBytes = 256u;
        copy.bytesPerRow = 256u;
        copy.rowsPerImage = 4u;
        copy.extent = desc.extent;
        read.resource = texture->id();
        read.textureSubresources = range;
        read.before = ResourceState::ShaderRead;
        read.after = ResourceState::CopySrc;
        read.sourceStages = PipelineStage::ComputeShader;
        read.destinationStages = PipelineStage::Transfer;
        read.sourceAccess = ResourceAccess::ShaderRead;
        read.destinationAccess = ResourceAccess::TransferRead;
        write.resource = f.destination->id();
        write.before = write.after = ResourceState::CopyDst;
        write.sourceStages = write.destinationStages = PipelineStage::Transfer;
        write.sourceAccess = write.destinationAccess = ResourceAccess::TransferWrite;
    }

    core::UniquePtr<GpuTexture> makeTexture(const TextureDesc& desc)
    {
        const auto native = f.resources.createTexture(desc);
        assert(native.success && native.payload && native.allocation.success);
        const GpuResourceId id{f.nextId++};
        f.registry->registerTexture(id, native.payload);
        auto result =
            createVulkanRhiTexture(id, desc, native.allocation, native.payload, f.registry);
        assert(result && f.registry->textureObjectMatches(*result));
        return result;
    }

    ResourceBarrierDesc restore() const
    {
        auto result = read;
        std::swap(result.before, result.after);
        std::swap(result.sourceStages, result.destinationStages);
        std::swap(result.sourceAccess, result.destinationAccess);
        return result;
    }

    RenderPassDesc clearDescriptor(bool alternate = false) const
    {
        RenderPassDesc pass;
        if (depth) {
            pass.hasDepthStencilAttachment = true;
            pass.depthStencilAttachment.target = texture->id();
            pass.depthStencilAttachment.format = texture->desc().format;
            pass.depthStencilAttachment.depthLoadOp = LoadOp::Clear;
            pass.depthStencilAttachment.depthClearValue = alternate ? 0.75f : 0.25f;
        } else {
            RenderPassColorAttachmentDesc attachment;
            attachment.target = texture->id();
            attachment.format = texture->desc().format;
            attachment.loadOp = LoadOp::Clear;
            attachment.clearColor =
                alternate ? Color{0.5f, 0.25f, 1.0f, 0.75f} : Color{0.25f, 0.5f, 0.75f, 1.0f};
            pass.colorAttachments.push_back(attachment);
        }
        return pass;
    }

    void verify(const std::shared_ptr<VulkanRhiNativeResourcePayload>& staging,
        bool copied,
        bool alternate = false)
    {
        assert(staging->backendInvalidateMappedRead(0u, 1024u, f.diagnostic));
        const auto* data = static_cast<const unsigned char*>(staging->backendMappedData());
        const std::array<std::uint16_t, 4u> rgba = alternate
            ? std::array<std::uint16_t, 4u>{0x3800u, 0x3400u, 0x3c00u, 0x3a00u}
            : std::array<std::uint16_t, 4u>{0x3400u, 0x3800u, 0x3a00u, 0x3c00u};
        const float z = alternate ? 0.75f : 0.25f;
        const auto* pixel = depth ? reinterpret_cast<const unsigned char*>(&z)
                                  : reinterpret_cast<const unsigned char*>(rgba.data());
        const std::size_t bpp = depth ? 4u : 8u;
        for (std::size_t i = 0u; i < 1024u; ++i) {
            const bool texel =
                copied && i >= 256u && (i - 256u) / 256u < 3u && (i - 256u) % 256u < 5u * bpp;
            assert(data[i] == (texel ? pixel[((i - 256u) % 256u) % bpp] : 0xa5u));
        }
    }

    CopyFixture f;
    bool depth;
    core::UniquePtr<GpuTexture> texture;
    TextureSubresourceRange range, untouched;
    ResourceBarrierDesc read, write;
    TextureToBufferCopyDesc copy;

private:
    void seed()
    {
        const auto image = vulkanRhiNativePayload(*texture);
        auto command = createVulkanRhiNativeCommandPayload(f.resources, f.diagnostic);
        assert(command);
        ResourceBarrierDesc barrier;
        barrier.resource = texture->id();
        barrier.textureSubresources = range;
        barrier.after = depth ? ResourceState::DepthAttachment : ResourceState::ColorAttachment;
        barrier.sourceStages = PipelineStage::TopOfPipe;
        barrier.destinationStages =
            depth ? PipelineStage::DepthStencil : PipelineStage::ColorAttachmentOutput;
        barrier.destinationAccess =
            depth ? ResourceAccess::DepthStencilWrite : ResourceAccess::ColorAttachmentWrite;
        assert(transitionVulkanRhiTexture(*texture, barrier).success
            && command->recordImageBarrier(*image, barrier, f.diagnostic)
            && command->flushPendingBarriers(f.diagnostic));
        auto pass = clearDescriptor();
        core::pmr::Vector<std::shared_ptr<VulkanRhiNativeResourcePayload>> colors;
        if (!depth)
            colors.push_back(image);
        assert(command->recordRenderPassBegin(
                   pass, colors, depth ? image.get() : nullptr, f.diagnostic)
            && command->recordRenderPassEnd(f.diagnostic));
        barrier.before = barrier.after;
        barrier.after = ResourceState::ShaderRead;
        barrier.sourceStages = barrier.destinationStages;
        barrier.sourceAccess = barrier.destinationAccess;
        barrier.destinationStages = PipelineStage::ComputeShader;
        barrier.destinationAccess = ResourceAccess::ShaderRead;
        assert(transitionVulkanRhiTexture(*texture, barrier).success
            && command->recordImageBarrier(*image, barrier, f.diagnostic)
            && command->flushPendingBarriers(f.diagnostic) && command->end(f.diagnostic)
            && command->submit(f.diagnostic));
        assert(vkDeviceWaitIdle(f.runtime->logicalDevice()) == VK_SUCCESS
            && command->submittedFenceCompleted());
        assert(vulkanRhiTextureStateCommitted(*texture)
            && texture->subresourceState(range) == ResourceState::ShaderRead
            && texture->subresourceState(untouched) == ResourceState::Undefined);
    }
};

void textureRejection(const std::shared_ptr<VulkanRhiRuntime>& runtime, TextureFormat format)
{
    TextureFixture t(runtime, format);
    auto& f = t.f;
    for (const auto capacity : {0u, 1u}) {
        auto command = f.command(capacity);
        const auto before = f.resources.stats();
        if (!capacity) {
            assert(!command->transition(*t.texture, t.read, f.diagnostic)
                && vulkanRhiTextureStateCommitted(*t.texture)
                && !command->retentionStorage()->resourceOwnerCount());
        } else {
            assert(command->transition(*t.texture, t.read, f.diagnostic));
            assert(!command->copy(*t.texture, *f.destination, t.copy, f.diagnostic)
                && command->retentionStorage()->resourceOwnerCount() == 1u);
        }
        assert(f.diagnostic == "RHI-COMMAND-RETENTION-OWNER-CAPACITY"
            && f.resources.stats().nativeImageToBufferCopyRecordCount
                == before.nativeImageToBufferCopyRecordCount);
    }
    for (const bool sealed : {false, true}) {
        auto command = f.command();
        const auto before = f.resources.stats().nativeImageBarrierRecordCount;
        for (unsigned i = 0u; i < 13u; ++i) {
            auto bad = t.read;
            if (i == 0u)
                bad.resource = {};
            if (i == 1u)
                bad.textureSubresources.baseMipLevel = 2u;
            if (i == 2u)
                bad.textureSubresources.baseArrayLayer = 2u;
            if (i == 3u)
                bad.textureSubresources.mipLevelCount = 0u;
            if (i == 4u)
                bad.textureSubresources.aspect = TextureAspect::Stencil;
            if (i == 5u)
                bad.before = ResourceState::CopyDst;
            if (i == 6u)
                bad.after = ResourceState::Undefined;
            if (i == 7u)
                bad.destinationStages = PipelineStage::ComputeShader;
            if (i == 8u)
                bad.destinationAccess = ResourceAccess::None;
            if (i == 9u)
                bad.sourceStages = PipelineStage::VertexShader;
            if (i == 10u)
                bad.after =
                    t.depth ? ResourceState::ColorAttachment : ResourceState::DepthAttachment;
            if (i == 11u)
                bad.after = ResourceState::ShaderWrite;
            if (i == 12u)
                bad.textureSubresources.arrayLayerCount = UINT32_MAX;
            assert(!command->transition(*t.texture, bad, f.diagnostic)
                && !command->retentionStorage()->resourceOwnerCount()
                && vulkanRhiTextureStateCommitted(*t.texture)
                && f.resources.stats().nativeImageBarrierRecordCount == before);
        }
        {
            core::MutationJournal foreign;
            auto same = t.read;
            same.after = same.before;
            assert(transitionVulkanRhiTexture(*t.texture, same, &foreign).success);
            assert(!command->transition(*t.texture, t.read, f.diagnostic)
                && !command->retentionStorage()->resourceOwnerCount());
        }
        assert(command->transition(*t.texture, t.read, f.diagnostic));
        const auto copies = f.resources.stats().nativeImageToBufferCopyRecordCount;
        for (unsigned i = 0u; i < 15u; ++i) {
            auto bad = t.copy;
            if (i == 0u)
                bad.sourceTexture = {};
            if (i == 1u)
                bad.destinationBuffer = {};
            if (i == 2u)
                bad.bytesPerRow = 40u;
            if (i == 3u)
                bad.rowsPerImage = 2u;
            if (i == 4u)
                bad.bufferOffsetBytes = 1u;
            if (i == 5u)
                bad.bufferOffsetBytes = UINT64_MAX - 7u;
            if (i == 6u)
                bad.extent.width = 6u;
            if (i == 7u)
                bad.extent.depth = 2u;
            if (i == 8u)
                bad.textureSubresources.mipLevelCount = 2u;
            if (i == 9u)
                bad.textureSubresources.arrayLayerCount = 2u;
            if (i == 10u)
                bad.textureSubresources.aspect = TextureAspect::Stencil;
            if (i == 11u)
                bad.mipLayouts.push_back({});
            if (i == 12u)
                bad.bufferOffsetBytes = 768u;
            if (i == 13u)
                bad.extent.height = 0u;
            if (i == 14u)
                bad.bufferOffsetBytes = t.depth ? 2u : 260u;
            assert(!command->copy(*t.texture, *f.destination, bad, f.diagnostic)
                && command->retentionStorage()->resourceOwnerCount() == 1u
                && f.resources.stats().nativeImageToBufferCopyRecordCount == copies);
        }
        auto competitor = f.command();
        assert(!competitor->copy(*t.texture, *f.destination, t.copy, f.diagnostic));
        if (sealed)
            assert(command->finish(f.diagnostic));
        command.reset();
        assert(vulkanRhiTextureStateCommitted(*t.texture)
            && t.texture->subresourceState(t.range) == ResourceState::ShaderRead
            && t.texture->subresourceState(t.untouched) == ResourceState::Undefined);
    }
    const auto image = vulkanRhiNativePayload(*t.texture);
    {
        auto alias =
            createVulkanRhiTexture(t.texture->id(), t.texture->desc(), {}, image, f.registry);
        auto command = f.command();
        assert(!command->transition(*alias, t.read, f.diagnostic)
            && f.diagnostic == "RHI-VULKAN-RETENTION-TEXTURE-REGISTRATION"
            && !command->retentionStorage()->resourceOwnerCount());
    }
    f.registry->registerTexture(t.texture->id(), image);
    f.registry->registerTextureObject(t.texture->id(), *t.texture);
    auto command = f.command();
    auto foreignRegistry = core::makeShared<VulkanRhiResourceRegistry>();
    foreignRegistry->registerTexture(t.texture->id(), image);
    auto foreign = VulkanRhiRetainedCommand::prepare(
        f.resources, foreignRegistry, storage(f.quota), f.diagnostic);
    assert(foreign && !foreign->transition(*t.texture, t.read, f.diagnostic)
        && f.diagnostic == "RHI-VULKAN-RETENTION-TEXTURE-REGISTRATION");
    foreign.reset();
    auto unrelated = core::makeShared<int>(0);
    f.registry->registerTexture(t.texture->id(), {unrelated, image.get()});
    assert(!command->transition(*t.texture, t.read, f.diagnostic)
        && f.diagnostic == "RHI-VULKAN-RETENTION-TEXTURE-REGISTRATION"
        && !command->retentionStorage()->resourceOwnerCount());
    f.registry->registerTexture(t.texture->id(), image);
    auto uninitialized = t.read;
    uninitialized.textureSubresources = t.untouched;
    uninitialized.before = ResourceState::Undefined;
    uninitialized.sourceStages = PipelineStage::TopOfPipe;
    uninitialized.sourceAccess = ResourceAccess::None;
    assert(!command->transition(*t.texture, uninitialized, f.diagnostic));
    uninitialized.after = t.depth ? ResourceState::DepthAttachment : ResourceState::ColorAttachment;
    uninitialized.destinationStages =
        t.depth ? PipelineStage::DepthStencil : PipelineStage::ColorAttachmentOutput;
    uninitialized.destinationAccess =
        t.depth ? ResourceAccess::DepthStencilWrite : ResourceAccess::ColorAttachmentWrite;
    assert(command->transition(*t.texture, uninitialized, f.diagnostic));
    command.reset();
    assert(vulkanRhiTextureStateCommitted(*t.texture)
        && t.texture->subresourceState(t.untouched) == ResourceState::Undefined);
    auto wrongDesc = t.texture->desc();
    wrongDesc.usage = TextureUsage::Sampled;
    auto wrongUsage = t.makeTexture(wrongDesc);
    command = f.command();
    uninitialized.resource = wrongUsage->id();
    assert(!command->transition(*wrongUsage, uninitialized, f.diagnostic)
        && !command->retentionStorage()->resourceOwnerCount());
    wrongDesc.format = TextureFormat::Rgba8Unorm;
    auto wrongFormat = t.makeTexture(wrongDesc);
    uninitialized.resource = wrongFormat->id();
    assert(!command->transition(*wrongFormat, uninitialized, f.diagnostic)
        && f.diagnostic == "RHI-VULKAN-RETENTION-TEXTURE-SHAPE"
        && !command->retentionStorage()->resourceOwnerCount());
    assert(command->transition(*t.texture, t.read, f.diagnostic));
    {
        core::MutationJournal pending;
        assert(transitionVulkanRhiBuffer(*f.destination, t.write, &pending).success);
        assert(!command->copy(*t.texture, *f.destination, t.copy, f.diagnostic)
            && f.diagnostic == "RHI-VULKAN-RETENTION-BUFFER-STATE-UNCOMMITTED");
    }
    f.destination->sealWrites();
    assert(!command->copy(*t.texture, *f.destination, t.copy, f.diagnostic)
        && f.diagnostic == "RHI-BUFFER-GPU-WRITES-SEALED");
    t.verify(vulkanRhiNativePayload(*f.destination), false);
    std::printf("native texture negatives/cancellation: format=%u PASS\n", unsigned(format));
}

void textureCompletion(const std::shared_ptr<VulkanRhiRuntime>& runtime,
    TextureFormat format,
    bool dropWrapper,
    bool unproven,
    bool preparedClear = false)
{
    TextureFixture t(runtime, format);
    auto& f = t.f;
    auto command = f.command();
    if (preparedClear) {
        const auto desc = t.clearDescriptor(true);
        assert(command->prepareClearStorePass(desc, *t.texture, t.range, f.diagnostic));
    }
    const auto cold = f.resources.stats();
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    if (preparedClear) {
        auto enter = t.read;
        enter.after = t.depth ? ResourceState::DepthAttachment : ResourceState::ColorAttachment;
        enter.destinationStages =
            t.depth ? PipelineStage::DepthStencil : PipelineStage::ColorAttachmentOutput;
        enter.destinationAccess =
            t.depth ? ResourceAccess::DepthStencilWrite : ResourceAccess::ColorAttachmentWrite;
        assert(command->transition(*t.texture, enter, f.diagnostic)
            && command->clearPreparedPass(*t.texture, f.diagnostic));
        assert(!command->clearPreparedPass(*t.texture, f.diagnostic)
            && f.diagnostic == "RHI-VULKAN-RETENTION-CLEAR-PREPARED-PASS");
        enter.before = enter.after;
        enter.sourceStages = enter.destinationStages;
        enter.sourceAccess = enter.destinationAccess;
        enter.after = ResourceState::ShaderRead;
        enter.destinationStages = PipelineStage::ComputeShader;
        enter.destinationAccess = ResourceAccess::ShaderRead;
        assert(command->transition(*t.texture, enter, f.diagnostic));
    }
    assert(command->transition(*t.texture, t.read, f.diagnostic)
        && command->transition(*f.destination, t.write, f.diagnostic)
        && command->copy(*t.texture, *f.destination, t.copy, f.diagnostic)
        && command->transition(*t.texture, t.restore(), f.diagnostic)
        && command->finish(f.diagnostic));
    assert(core::defaultCpuAllocator().stats().allocationCount == allocations
        && !vulkanRhiTextureStateCommitted(*t.texture)
        && !vulkanRhiBufferStateCommitted(*f.destination)
        && command->retentionStorage()->resourceOwnerCount() == 2u);
    assert(f.resources.stats().nativeRenderPassCreateCount == cold.nativeRenderPassCreateCount
        && f.resources.stats().nativeFramebufferCreateCount == cold.nativeFramebufferCreateCount
        && f.resources.stats().nativeRenderAttachmentViewCreateCount
            == cold.nativeRenderAttachmentViewCreateCount);
    f.quota->setLimitBytes(kMetadataBytes + 1u);
    assert(!command->submit({}, f.diagnostic) && !vulkanRhiTextureStateCommitted(*t.texture));
    f.quota->setLimitBytes(kMetadataBytes);
    if (unproven) {
        failSubmit = true;
        assert(!command->submit({}, f.diagnostic) && !failSubmit
            && f.diagnostic == "RHI-VULKAN-RETENTION-SUBMIT-UNPROVEN");
        assert(!vulkanRhiTextureStateCommitted(*t.texture)
            && !vulkanRhiBufferStateCommitted(*f.destination));
        command.reset();
        retireCompletedVulkanRhiSubmittedCommands();
        assert(f.quota->liveBytes());
        if (preparedClear)
            assert((clearNativeLive == std::array<unsigned, 3u>{1u, 1u, 1u}));
        assert(!waitIdleAndRetireVulkanRhiSubmittedCommands(runtime));
        auto retry = t.read;
        retry.after = retry.before;
        assert(!transitionVulkanRhiTexture(*t.texture, retry).success
            && !transitionVulkanRhiBuffer(*f.destination, t.write).success);
        t.verify(vulkanRhiNativePayload(*f.destination), false);
    } else {
        auto gate = VulkanRhiTimelineSemaphorePayload::create(runtime, 0u, f.diagnostic);
        const auto before = core::defaultCpuAllocator().stats().allocationCount;
        assert(gate && command->submit({gate, 1u}, f.diagnostic));
        assert(core::defaultCpuAllocator().stats().allocationCount == before
            && vulkanRhiTextureStateCommitted(*t.texture)
            && vulkanRhiBufferStateCommitted(*f.destination) && !command->completed()
            && t.texture->subresourceState(t.range) == ResourceState::ShaderRead
            && t.texture->subresourceState(t.untouched) == ResourceState::Undefined);
        const std::weak_ptr<void> image = vulkanRhiNativePayload(*t.texture);
        const auto staging = vulkanRhiNativePayload(*f.destination);
        t.texture.reset();
        f.destination.reset();
        if (dropWrapper)
            command.reset();
        retireCompletedVulkanRhiSubmittedCommands();
        assert(!image.expired() && f.quota->liveBytes());
        if (preparedClear)
            assert((clearNativeLive == std::array<unsigned, 3u>{1u, 1u, 1u}));
        assert(gate->hostSignal(1u) == VK_SUCCESS);
        if (command) {
            assert(command->waitUntil(
                std::chrono::steady_clock::now() + std::chrono::seconds(5), f.diagnostic));
            command.reset();
        }
        assert(!waitIdleAndRetireVulkanRhiSubmittedCommands(runtime) && image.expired());
        t.verify(staging, true, preparedClear);
    }
    assert(!f.quota->liveBytes());
    assert((clearNativeLive == std::array<unsigned, 3u>{}));
    std::printf(
        "native texture transaction/copy: format=%u dropped=%d unproven=%d preparedClear=%d padded exact bytes/no hot allocations PASS\n",
        unsigned(format),
        dropWrapper,
        unproven,
        preparedClear);
}

void clearPreparation(const std::shared_ptr<VulkanRhiRuntime>& runtime, TextureFormat format)
{
    TextureFixture t(runtime, format);
    auto& f = t.f;
    const auto desc = t.clearDescriptor(true);
    const auto original = clearNativeLive;
    {
        auto command = f.command(0u);
        assert(!command->prepareClearStorePass(desc, *t.texture, t.range, f.diagnostic)
            && f.diagnostic == "RHI-COMMAND-RETENTION-OWNER-CAPACITY"
            && clearNativeLive == original);
    }
    {
        auto command = f.command(1u);
        assert(command->prepareClearStorePass(desc, *t.texture, t.range, f.diagnostic)
            && command->transition(*t.texture, t.read, f.diagnostic));
        const auto copies = f.resources.stats().nativeImageToBufferCopyRecordCount;
        assert(!command->copy(*t.texture, *f.destination, t.copy, f.diagnostic)
            && f.diagnostic == "RHI-COMMAND-RETENTION-OWNER-CAPACITY"
            && command->retentionStorage()->resourceOwnerCount() == 1u
            && f.resources.stats().nativeImageToBufferCopyRecordCount == copies);
    }
    assert(clearNativeLive == original && vulkanRhiTextureStateCommitted(*t.texture));
    for (unsigned failure = 1u; failure <= 3u; ++failure) {
        auto command = f.command();
        clearCreationFailure = failure;
        assert(!command->prepareClearStorePass(desc, *t.texture, t.range, f.diagnostic)
            && !clearCreationFailure && f.diagnostic == "RHI-VULKAN-RETENTION-CLEAR-NATIVE-CREATION"
            && !command->retentionStorage()->resourceOwnerCount() && clearNativeLive == original);
        assert(command->prepareClearStorePass(desc, *t.texture, t.range, f.diagnostic));
        assert(!command->prepareClearStorePass(desc, *t.texture, t.range, f.diagnostic)
            && f.diagnostic == "RHI-VULKAN-RETENTION-CLEAR-PREPARATION-STATE");
        assert(!command->clearPreparedPass(*t.texture, f.diagnostic)
            && f.diagnostic == "RHI-VULKAN-RETENTION-CLEAR-TARGET-STATE");
        command.reset();
        assert(clearNativeLive == original && !f.quota->liveBytes());
    }
    {
        auto command = f.command();
        for (unsigned i = 0u; i < 11u; ++i) {
            auto bad = desc;
            auto range = t.range;
            if (i == 0u)
                bad.layeredRendering = true;
            if (i == 1u)
                bad.subpasses.push_back({});
            if (i == 2u)
                bad.subpassDependencies.push_back({});
            if (i == 3u)
                range.arrayLayerCount = 2u;
            if (i == 4u)
                range.baseMipLevel = 2u;
            if (i == 10u)
                range.mipLevelCount = 2u;
            if (t.depth) {
                auto& a = bad.depthStencilAttachment;
                if (i == 5u)
                    a.depthLoadOp = LoadOp::Load;
                if (i == 6u)
                    a.depthStoreOp = StoreOp::DontCare;
                if (i == 7u)
                    a.target = {};
                if (i == 8u)
                    a.depthClearValue = std::numeric_limits<float>::quiet_NaN();
                if (i == 9u)
                    a.depthReadOnly = true;
            } else {
                auto& a = bad.colorAttachments.front();
                if (i == 5u)
                    a.loadOp = LoadOp::Load;
                if (i == 6u)
                    a.storeOp = StoreOp::DontCare;
                if (i == 7u)
                    a.target = {};
                if (i == 8u)
                    a.clearColor.r = std::numeric_limits<float>::quiet_NaN();
                if (i == 9u)
                    a.hasResolveTarget = true;
            }
            assert(!command->prepareClearStorePass(bad, *t.texture, range, f.diagnostic)
                && !command->retentionStorage()->resourceOwnerCount()
                && clearNativeLive == original);
        }
        assert(command->transition(*t.texture, t.read, f.diagnostic));
        assert(!command->prepareClearStorePass(desc, *t.texture, t.range, f.diagnostic)
            && f.diagnostic == "RHI-VULKAN-RETENTION-CLEAR-PREPARATION-STATE");
    }
    for (const bool sealed : {false, true}) {
        auto command = f.command();
        assert(command->prepareClearStorePass(desc, *t.texture, t.range, f.diagnostic));
        auto enter = t.read;
        enter.after = t.depth ? ResourceState::DepthAttachment : ResourceState::ColorAttachment;
        enter.destinationStages =
            t.depth ? PipelineStage::DepthStencil : PipelineStage::ColorAttachmentOutput;
        enter.destinationAccess =
            t.depth ? ResourceAccess::DepthStencilWrite : ResourceAccess::ColorAttachmentWrite;
        {
            core::MutationJournal foreign;
            assert(transitionVulkanRhiTexture(*t.texture, enter, &foreign).success);
            assert(!command->clearPreparedPass(*t.texture, f.diagnostic)
                && f.diagnostic == "RHI-VULKAN-RETENTION-CLEAR-TARGET-STATE");
        }
        assert(command->transition(*t.texture, enter, f.diagnostic));
        const auto nativeBegins = f.resources.stats().nativeRenderPassBeginRecordCount;
        missingClearRecord = true;
        assert(!command->clearPreparedPass(*t.texture, f.diagnostic)
            && f.diagnostic == "RHI-VULKAN-RETENTION-CLEAR-RECORD-ENTRYPOINT"
            && f.resources.stats().nativeRenderPassBeginRecordCount == nativeBegins);
        missingClearRecord = false;
        assert(command->clearPreparedPass(*t.texture, f.diagnostic));
        if (sealed)
            assert(command->finish(f.diagnostic));
        command.reset();
        assert(clearNativeLive == original && !f.quota->liveBytes()
            && vulkanRhiTextureStateCommitted(*t.texture)
            && t.texture->subresourceState(t.range) == ResourceState::ShaderRead);
    }
    std::printf(
        "native cold Clear/Store preparation: format=%u descriptor/creation failures/cancellation PASS\n",
        unsigned(format));
}
} // namespace

int main(int argc, char** argv)
{
    contract();
    if (argc == 2 && std::string_view(argv[1]) == "--contract")
        return 0;
    tests::VulkanHeadlessRuntime native(false, resolve);
    if (!native.runtime() || !native.runtime()->valid())
        return 77;
    tests::RenderDocCaptureScope capture;
    const bool onlyClear = argc == 2 && std::string_view(argv[1]) == "--clear";
    const bool onlyTexture = argc == 2 && std::string_view(argv[1]) == "--texture";
    for (const auto format : {TextureFormat::Rgba16Float, TextureFormat::D32Float}) {
        if (!onlyTexture) {
            clearPreparation(native.runtime(), format);
            textureCompletion(native.runtime(), format, false, false, true);
            textureCompletion(native.runtime(), format, true, false, true);
            textureCompletion(native.runtime(), format, false, true, true);
        }
        if (onlyClear)
            continue;
        textureRejection(native.runtime(), format);
        textureCompletion(native.runtime(), format, false, false);
        textureCompletion(native.runtime(), format, true, false);
        textureCompletion(native.runtime(), format, false, true);
    }
    if (onlyClear || (argc == 2 && std::string_view(argv[1]) == "--texture")) {
        drainVulkanRhiSubmittedCommandsForShutdown();
        return 0;
    }
    rejection(native.runtime());
    completion(native.runtime(), false);
    completion(native.runtime(), true);
    cancellationAndFailures(native.runtime());
    stateTransactions(native.runtime(), false);
    stateTransactions(native.runtime(), true);
    drainVulkanRhiSubmittedCommandsForShutdown();
    capture.end();
    return 0;
}

#include "core/ownership/lifetime_epoch.h"
#include "core/ownership/monotonic_id_sequence.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/backends/webgpu/webgpu_backend_objects.h"
#include "tests/fixtures/descriptor_liveness_contract.h"
#include "tests/fixtures/resource_identity_contract.h"

#include <algorithm>
#include <array>
#include <limits>
#include <thread>
#if defined(ARP_TEST_IDENTITY_METAL)
#include "rhi/backends/metal/metal_device.h"
#endif
#include <cstdio>

namespace {
void verifyDescriptorRevisionExhaustion()
{
    class RevisionProbe final : public arp::rhi::DescriptorObject {
    public:
        explicit RevisionProbe(std::uint64_t first) : DescriptorObject(first) {}

        arp::rhi::ValidationResult advance()
        {
            auto result = validateContentWrite();
            if (result.success)
                commitContentWrite();
            return result;
        }
    };

    RevisionProbe revision(std::numeric_limits<std::uint64_t>::max() - 1u);
    const auto ticket = revision.lifetimeTicket();
    const auto before = arp::core::defaultCpuAllocator().stats();
    assert(revision.advance().success);
    assert(arp::core::defaultCpuAllocator().stats().allocationCount == before.allocationCount);
    assert(revision.contentRevision() == std::numeric_limits<std::uint64_t>::max());
    for (unsigned i = 0; i < 3u; ++i) {
        const auto failure = revision.advance();
        assert(!failure.success && failure.diagnostics == "RHI-DESCRIPTOR-REVISION-EXHAUSTED");
        assert(revision.contentRevision() == std::numeric_limits<std::uint64_t>::max());
        assert(ticket.isCurrent());
    }
    RevisionProbe exhausted(0u);
    assert(!exhausted.advance().success && exhausted.contentRevision() == 0u);
}

void verifyWebGpuFailedWriteTransaction(
    const std::shared_ptr<arp::rhi_webgpu::WebGpuRhiResourceRegistry>& registry,
    arp::rhi::GpuResourceId view)
{
    using namespace arp::rhi;
    using namespace arp::rhi_webgpu;
    if (webGpuNativeDeviceAvailable())
        return; // this fixture exercises unavailable native creation
    auto arena = createWebGpuRhiDescriptorArena({"WriteFailure", 2u, 4u}, registry, {true, true});
    DescriptorLayoutDesc layout;
    layout.name = "WriteFailure";
    layout.bindings.push_back({0u, DescriptorType::SampledTexture, ShaderStage::Fragment});
    layout.bindings.push_back({1u, DescriptorType::SampledTexture, ShaderStage::Fragment});
    auto set = arena->allocateDescriptorSet({"WriteFailure", layout});
    auto table = arena->allocateDescriptorTable(
        {"WriteFailureTable", DescriptorType::SampledTexture, ShaderStage::Fragment, 2u});
    assert(set && table);
    assert(
        set->write({0u, DescriptorType::SampledTexture, view, DescriptorResourceKind::TextureView})
            .success);
    assert(table
            ->write({0u, DescriptorType::SampledTexture, view, DescriptorResourceKind::TextureView})
            .success);
    const auto setRevision = set->contentRevision();
    const auto tableRevision = table->contentRevision();
    BoundDescriptorSlots slots;
    assert(slots.bindSet(0u, *set, nullptr, 0u).success && slots.bindTable(0u, *table).success);
    const auto before = arp::core::defaultCpuAllocator().stats();
    for (unsigned retry = 0; retry < 3u; ++retry) {
        const auto setFailure = set->write(
            {1u, DescriptorType::SampledTexture, view, DescriptorResourceKind::TextureView});
        const auto tableFailure = table->write(
            {1u, DescriptorType::SampledTexture, view, DescriptorResourceKind::TextureView});
        assert(!setFailure.success && !tableFailure.success);
        assert(setFailure.diagnostics.find("RHI-DESCRIPTOR-PAYLOAD-CREATION-FAILED") == 0u);
        assert(tableFailure.diagnostics.find("RHI-DESCRIPTOR-PAYLOAD-CREATION-FAILED") == 0u);
        assert(set->contentRevision() == setRevision && table->contentRevision() == tableRevision);
        assert(set->stats().writtenBindings == 1u && table->stats().writtenDescriptors == 1u);
        assert(set->hasWrittenBinding(0u) && !set->hasWrittenBinding(1u));
        assert(table->hasWrittenDescriptor(0u) && !table->hasWrittenDescriptor(1u));
        assert(slots.validateLiveBindings().success);
    }
    assert(arp::core::defaultCpuAllocator().stats().liveBytes == before.liveBytes);
    std::puts(
        "DescriptorContentRevision/1 WebGPU unavailable-native transactional rejection: PASS");
}

void verifyLifetimeEpoch()
{
    using arp::core::LifetimeEpoch;
    LifetimeEpoch::Ticket old;
    arp::core::LifetimeObserver<const int> observer;
    assert(!old.isCurrent());
    assert(observer.get() == nullptr);
    {
        LifetimeEpoch owner;
        const int value = 17;
        observer = {&value, owner.ticket()};
        assert(observer.get() == &value && *observer.get() == 17);
        old = owner.ticket();
        auto copy = old;
        assert(old.isCurrent() && copy.isCurrent());
        const auto before = arp::core::defaultCpuAllocator().stats();
        for (unsigned i = 0; i < 4096u; ++i) {
            owner.advance();
            assert(owner.ticket().isCurrent() && !old.isCurrent());
            assert(observer.get() == nullptr);
        }
        const auto after = arp::core::defaultCpuAllocator().stats();
        assert(after.allocationCount == before.allocationCount);
        assert(after.liveBytes == before.liveBytes);
        old = owner.ticket();
        assert(old.isCurrent() && !copy.isCurrent());
    }
    assert(!old.isCurrent());
    assert(observer.get() == nullptr);
    LifetimeEpoch replacement;
    assert(replacement.ticket().isCurrent() && !old.isCurrent());
    LifetimeEpoch terminal(std::numeric_limits<std::uint64_t>::max() - 1u);
    auto beforeLast = terminal.ticket();
    terminal.advance();
    auto last = terminal.ticket();
    assert(last.isCurrent() && !beforeLast.isCurrent());
    terminal.advance();
    assert(!last.isCurrent() && !terminal.isActive() && !terminal.ticket().isCurrent());
    terminal.advance();
    assert(!terminal.isActive() && !last.isCurrent());
    LifetimeEpoch exhausted(0u);
    exhausted.advance();
    assert(!exhausted.isActive() && !exhausted.ticket().isCurrent());
}

void verifyWebGpuMetadataIdentity(bool liveness)
{
    using namespace arp::rhi;
    using namespace arp::rhi_webgpu;
    auto localRegistry = arp::core::makeShared<WebGpuRhiResourceRegistry>();
    auto foreignRegistry = arp::core::makeShared<WebGpuRhiResourceRegistry>();
    WebGpuRhiAllocation allocation;
    allocation.success = true;
    allocation.sizeBytes = 256u;
    const TextureDesc desc{"IdentityWebMetadata",
        {8u, 8u, 1u},
        TextureFormat::Rgba8Unorm,
        TextureUsage::Sampled,
        MemoryUsage::GpuOnly,
        ResourceState::ShaderRead};
    auto local = createWebGpuRhiTexture(
        allocateGpuResourceId(), desc, allocation, WebGpuRhiNativePayloadPolicy{}, localRegistry);
    auto foreign = createWebGpuRhiTexture(
        allocateGpuResourceId(), desc, allocation, WebGpuRhiNativePayloadPolicy{}, foreignRegistry);
    assert(local && foreign);
    localRegistry->registerTexture(*local);
    foreignRegistry->registerTexture(*foreign);
    TextureViewDesc viewDesc;
    viewDesc.usage = TextureUsage::Sampled;
    auto localView = createWebGpuRhiTextureView(
        allocateGpuResourceId(), local->id(), *local, viewDesc, {}, localRegistry);
    auto foreignView = createWebGpuRhiTextureView(
        allocateGpuResourceId(), foreign->id(), *foreign, viewDesc, {}, foreignRegistry);
    assert(localView && foreignView);
    localRegistry->registerTextureView(*localView);
    foreignRegistry->registerTextureView(*foreignView);
    verifyWebGpuFailedWriteTransaction(localRegistry, localView->id());
    auto arena =
        createWebGpuRhiDescriptorArena({"IdentityWebMetadataArena", 2u, 4u}, localRegistry, {});
    assert(arena);
    arp::tests::verifyForeignTextureWrites(*arena, localView->id(), foreignView->id());
    std::puts("ResourceIdentity/1 WebGPU metadata foreign set/table (no browser runtime): PASS");
    if (liveness) {
        auto arrayParent = createWebGpuRhiTexture(allocateGpuResourceId(),
            desc,
            allocation,
            WebGpuRhiNativePayloadPolicy{},
            localRegistry);
        assert(arrayParent);
        localRegistry->registerTexture(*arrayParent);
        auto arrayView = createWebGpuRhiTextureView(
            allocateGpuResourceId(), arrayParent->id(), *arrayParent, viewDesc, {}, localRegistry);
        auto arrayArena =
            createWebGpuRhiDescriptorArena({"WebArrayArena", 2u, 4u}, localRegistry, {});
        assert(arrayView && arrayArena);
        localRegistry->registerTextureView(*arrayView);
        arp::tests::verifyDescriptorArrayElements(
            *arrayArena, arrayView, localView->id(), arrayParent);
        arena->reset();
        DescriptorLayoutDesc layout;
        layout.name = "WebLiveness";
        layout.bindings.push_back({0u, DescriptorType::SampledTexture, ShaderStage::Fragment});
        auto set = arena->allocateDescriptorSet({"WebLiveness", layout});
        auto table = arena->allocateDescriptorTable(
            {"WebLivenessTable", DescriptorType::SampledTexture, ShaderStage::Fragment, 1u});
        assert(set && table);
        const DescriptorBindingWrite write{0u,
            DescriptorType::SampledTexture,
            localView->id(),
            DescriptorResourceKind::TextureView};
        const DescriptorTableWrite tableWrite{0u,
            DescriptorType::SampledTexture,
            localView->id(),
            DescriptorResourceKind::TextureView};
        assert(set->write(write).success && table->write(tableWrite).success);
        assert(validateDescriptorResources(*set, DescriptorContentRequirement::WrittenElements)
                .success);
        assert(validateDescriptorResources(*table, DescriptorContentRequirement::CompleteLayout)
                .success);
        localView.reset();
        assert(!set->validateBinding(0u).success);
        assert(!set->validateBindingElement(0u, 0u).success);
        assert(!table->validateDescriptor(0u).success);
        assert(!set->write(write).success && !table->write(tableWrite).success);
        assert(!validateDescriptorResources(*set, DescriptorContentRequirement::WrittenElements)
                .success);
        assert(!validateDescriptorResources(*table, DescriptorContentRequirement::CompleteLayout)
                .success);
        auto replacement = createWebGpuRhiTextureView(
            allocateGpuResourceId(), local->id(), *local, viewDesc, {}, localRegistry);
        assert(replacement);
        localRegistry->registerTextureView(*replacement);
        arp::tests::verifyDescriptorConsumptionObservers(*arena, replacement->id());
        arp::tests::verifyDescriptorArenaLifetime(std::move(arena), replacement->id());
        auto parentArena = createWebGpuRhiDescriptorArena({"WebParent", 4u, 4u}, localRegistry, {});
        auto second = createWebGpuRhiTexture(allocateGpuResourceId(),
            desc,
            allocation,
            WebGpuRhiNativePayloadPolicy{},
            localRegistry);
        assert(parentArena && second);
        localRegistry->registerTexture(*second);
        auto secondView = createWebGpuRhiTextureView(
            allocateGpuResourceId(), second->id(), *second, viewDesc, {}, localRegistry);
        assert(secondView);
        localRegistry->registerTextureView(*secondView);
        arp::tests::verifyDescriptorParentValidity(*parentArena,
            local,
            replacement->id(),
            secondView->id(),
            DescriptorType::SampledTexture);
        auto storageDesc = desc;
        storageDesc.usage = TextureUsage::Storage;
        auto storage = createWebGpuRhiTexture(allocateGpuResourceId(),
            storageDesc,
            allocation,
            WebGpuRhiNativePayloadPolicy{},
            localRegistry);
        auto storageReplacement = createWebGpuRhiTexture(allocateGpuResourceId(),
            storageDesc,
            allocation,
            WebGpuRhiNativePayloadPolicy{},
            localRegistry);
        assert(storage && storageReplacement);
        localRegistry->registerTexture(*storage);
        localRegistry->registerTexture(*storageReplacement);
        auto storageViewDesc = viewDesc;
        storageViewDesc.usage = TextureUsage::Storage;
        auto storageView = createWebGpuRhiTextureView(
            allocateGpuResourceId(), storage->id(), *storage, storageViewDesc, {}, localRegistry);
        auto storageReplacementView = createWebGpuRhiTextureView(allocateGpuResourceId(),
            storageReplacement->id(),
            *storageReplacement,
            storageViewDesc,
            {},
            localRegistry);
        assert(storageView && storageReplacementView);
        localRegistry->registerTextureView(*storageView);
        localRegistry->registerTextureView(*storageReplacementView);
        arp::tests::verifyDescriptorParentValidity(*parentArena,
            storage,
            storageView->id(),
            storageReplacementView->id(),
            DescriptorType::StorageTexture);
        std::puts("DescriptorResourceLiveness/1 WebGPU metadata: PASS");
    }
}
} // namespace

int main(int argc, char** argv)
{
    using namespace arp::rhi;
    const bool liveness = argc == 2 && std::string_view(argv[1]) == "--liveness";
    verifyLifetimeEpoch();
    verifyDescriptorRevisionExhaustion();
    static_assert(sizeof(GpuResourceId) == sizeof(std::uint64_t));
    arp::core::MonotonicIdSequence terminal(std::numeric_limits<std::uint64_t>::max() - 1u);
    assert(terminal.hasRemaining());
    assert(terminal.tryAcquire() == std::numeric_limits<std::uint64_t>::max() - 1u);
    assert(terminal.tryAcquire() == std::numeric_limits<std::uint64_t>::max());
    assert(!terminal.hasRemaining());
    assert(terminal.tryAcquire() == 0u);
    assert(terminal.tryAcquire() == 0u);
    arp::core::MonotonicIdSequence exhausted(0u);
    assert(exhausted.tryAcquire() == 0u);
    // Exercise the exported runtime allocator concurrently, not just separate
    // instances of the core sequence or device-local counters.
    std::array<std::uint64_t, 4096u> identities{};
    auto allocateHalf = [&](std::size_t begin, std::size_t end) {
        for (auto i = begin; i < end; ++i)
            identities[i] = allocateGpuResourceId().value;
    };
#if !defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)
    std::thread worker(allocateHalf, 0u, identities.size() / 2u);
    allocateHalf(identities.size() / 2u, identities.size());
    worker.join();
#else
    allocateHalf(0u, identities.size());
    std::puts(
        "ResourceIdentity/1 concurrent allocation: unavailable in this single-thread wasm build");
#endif
    std::sort(identities.begin(), identities.end());
    assert(identities.front() != 0u);
    assert(std::adjacent_find(identities.begin(), identities.end()) == identities.end());
    verifyWebGpuMetadataIdentity(liveness);
    auto local = arp::rhi::createNullDevice();
    auto foreign = arp::rhi::createNullDevice();
    assert(local && foreign);
    arp::tests::verifyForeignDescriptorResources(*local, *foreign);
    if (liveness)
        arp::tests::verifyDescriptorWrapperLiveness(*local);
    std::puts("ResourceIdentity/1 Null foreign set/table: PASS");
    const auto baseline = local->memoryStats();
    auto heap = local->createMemoryHeap(256u, 16u, MemoryUsage::GpuOnly);
    assert(heap && heap->id().isValid());
    const auto heapId = heap->id();
    assert(local->memoryStats().allocatedBytes == baseline.allocatedBytes + 256u);
    heap.reset();
    assert(local->memoryStats().allocatedBytes == baseline.allocatedBytes);
    assert(local->memoryStats().liveResourceCount == baseline.liveResourceCount);
    auto nextHeap = local->createMemoryHeap(256u, 16u, MemoryUsage::GpuOnly);
    assert(nextHeap && nextHeap->id() != heapId);
    const auto nextHeapId = nextHeap->id();
    nextHeap.reset();
    SwapchainDesc swapDesc;
    swapDesc.extent = {8u, 8u};
    swapDesc.format = TextureFormat::Rgba8Unorm;
    auto swapchain = local->createSwapchain(swapDesc);
    assert(swapchain);
    std::array<GpuResourceId, 8u> acquiredIds{};
    for (auto& id : acquiredIds) {
        const auto frame = swapchain->acquireNextFrame();
        assert(frame.success);
        auto acquired = swapchain->acquiredImage(frame);
        assert(acquired.success && acquired.texture);
        id = acquired.texture->id();
        assert(id != heapId && id != nextHeapId);
        assert(swapchain->discardFrame(frame).success);
    }
    for (std::size_t i = 0; i < acquiredIds.size(); ++i)
        for (std::size_t j = i + 1; j < acquiredIds.size(); ++j)
            assert(acquiredIds[i] != acquiredIds[j]);
    swapchain.reset();
    auto recreated = arp::rhi::createNullDevice();
    auto recreatedBuffer = recreated->createBuffer({"Recreated",
        256u,
        ResourceUsage::UniformBuffer,
        MemoryUsage::CpuToGpu,
        ResourceState::ShaderRead});
    assert(recreatedBuffer && recreatedBuffer->id() != heapId);
    for (auto id : acquiredIds)
        assert(recreatedBuffer->id() != id);
#if defined(ARP_TEST_IDENTITY_METAL)
    arp::rhi_metal::MetalRhiDeviceDesc desc;
    desc.capabilityContract =
        arp::rhi::semantics::nativeBackendCapabilityContract(arp::rhi::Backend::Metal);
    desc.capabilityContract.queriedFromNativeDevice = true;
    desc.capabilityContract.queriedExtendedFeatures = true;
    desc.memoryBudgetBytes = 1024u * 1024u;
    desc.uploadStagingPageSizeBytes = 4096u;
    desc.uploadStagingMaxPageCount = 1u;
    auto metalLocal = arp::rhi_metal::createMetalRhiDevice(desc);
    auto metalForeign = arp::rhi_metal::createMetalRhiDevice(desc);
    assert(metalLocal && metalForeign);
    arp::tests::verifyForeignDescriptorResources(*metalLocal, *metalForeign);
    if (liveness)
        arp::tests::verifyDescriptorWrapperLiveness(*metalLocal, false);
    arp::tests::verifyForeignDescriptorResources(*metalLocal, *local);
    std::puts("ResourceIdentity/1 native Metal foreign set/table: PASS");
#endif
}

#include "../fixtures/metal_ordinary_graphics_write_fixture.h"
#include "core/mutation_journal.h"
#include "rhi/backends/metal/metal_ordinary_graphics_write.h"

#import <Metal/Metal.h>
#include <cstdio>

#ifdef NDEBUG
#error Writable preparation tests require assertions
#endif

namespace {
using namespace arp;
using namespace rhi;
using namespace rhi_metal;
using Prepared = MetalRhiOrdinaryPreparedGraphicsWrite;

class WritePreparationFixture final : public test::OrdinaryGraphicsWriteFixture {
public:
    WritePreparationFixture(const char* vs, const char* fs) : OrdinaryGraphicsWriteFixture(vs, fs)
    {}

    auto accepted()
    {
        auto root = prepare();
        if (!root)
            std::fprintf(stderr, "%s\n", pass.graphics.diagnostic.c_str());
        assert(root);
        return root;
    }

    void reject(const char* expected)
    {
        auto& f = pass.graphics;
        const auto before = f.quota->liveBytes();
        const auto state = buffer->state();
        auto root = prepare();
        if (root || f.diagnostic.find(expected) == f.diagnostic.npos)
            std::fprintf(stderr, "Expected %s: %s\n", expected, f.diagnostic.c_str());
        assert(!root && f.diagnostic.find(expected) != f.diagnostic.npos
            && f.quota->liveBytes() == before && buffer->state() == state);
    }

    void run()
    {
        auto& f = pass.graphics;
        reject("SEAL-MISMATCH");
        assert(!buffer->hostWritesSealed());
        buffer->sealHostWrites();
        assert(!buffer->gpuWritesSealed() && !buffer->gpuTransferWritesSealed());
        assert(f.device->writeBuffer(*buffer, initial.data(), sizeof(initial)).diagnostics
            == "RHI-BUFFER-HOST-WRITES-SEALED");
        producer.bufferSize++;
        reject("SCOPE-MISMATCH");
        producer.bufferSize--;
        dependency.before = ResourceState::ShaderRead;
        reject("DEPENDENCY-MISMATCH");
        dependency.before = ResourceState::ShaderWrite;
        consumer.bufferSize--;
        reject("CONSUMER-RANGE");
        consumer.bufferSize++;
        {
            core::CpuMemoryQuotaScope scope(f.quota);
            auto other = core::shareUnique(f.device->createBuffer({"WrongOwnedWriter",
                sizeof(initial),
                ResourceUsage::StorageBuffer | ResourceUsage::CopySrc,
                MemoryUsage::CpuToGpu,
                ResourceState::ShaderWrite}));
            assert(other);
            other->sealHostWrites();
            BindlessAccessArray declarations(&producer, 1u);
            assert(
                !Prepared::prepare(binding, other, declarations, dependency, consumer, f.diagnostic)
                && f.diagnostic == "RHI-METAL-ORDINARY-WRITE-DESCRIPTOR-RESOURCE-MISMATCH");
        }
        std::array<BindlessResourceAccess, 2> duplicates{producer, producer};
        BindlessAccessArray both(duplicates.data(), duplicates.size());
        assert(!Prepared::prepare(binding, buffer, both, dependency, consumer, f.diagnostic)
            && f.diagnostic == "RHI-METAL-ORDINARY-WRITE-DECLARATION-DOMAIN");
        const auto baseline = f.quota->liveBytes();
        auto root = accepted();
        const auto charge = f.quota->liveBytes() - baseline;
        assert(charge && root->proof().replacement == 0x12345678u && root->proof().binding == 3u
            && root->proof().byteCount == sizeof(std::uint32_t) && !root->proof().byteOffset);
        assert(
            core::inspectCpuAllocationCharge(root->contract().descriptors().bindings().data()).quota
            == f.quota.get());
        producer.bufferSize++;
        dependency.before = ResourceState::ShaderRead;
        consumer.bufferSize++;
        assert(root->validateObservation().success
            && root->dependency().before == ResourceState::ShaderWrite
            && root->consumer().bufferSize == sizeof(std::uint32_t));
        producer.bufferSize--;
        dependency.before = ResourceState::ShaderWrite;
        consumer.bufferSize--;
        std::uint64_t preparationPeak = 0u;
        {
            // Measure before any exhausted-quota probe raises the historical
            // peak to the quota limit. Lift live bytes just above the old peak.
            core::CpuMemoryQuotaLease measurement(
                f.quota, f.quota->peakBytes() - f.quota->liveBytes() + 1u);
            assert(measurement.charged());
            const auto start = f.quota->liveBytes();
            auto measured = accepted();
            preparationPeak = f.quota->peakBytes() - start;
            assert(preparationPeak >= charge);
        }
        {
            core::CpuMemoryQuotaLease pressure(f.quota, f.cpuBytes - f.quota->liveBytes());
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            auto previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
            for (unsigned i = 0; i < 1000u; ++i)
                assert(root->validateObservation().success);
            std::pmr::set_default_resource(previous);
            assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
            reject("ALLOCATION-FAILED");
        }
        root.reset();
        assert(f.quota->liveBytes() == baseline);
        {
            core::CpuMemoryQuotaLease pressure(f.quota, f.cpuBytes - baseline - preparationPeak);
            auto exactMetadata = accepted();
            assert(exactMetadata->validateObservation().success);
        }
        {
            core::CpuMemoryQuotaLease pressure(
                f.quota, f.cpuBytes - baseline - preparationPeak + 1u);
            reject("ALLOCATION-FAILED");
        }
        root = accepted();
        {
            // Existing resource-state metadata only, without native encoding.
            // No fixture state map and no dummy command/completion are involved.
            core::CpuMemoryQuotaScope scope(f.quota);
            auto recording = core::makeUnique<core::MutationJournal>();
            auto conflicting = core::makeUnique<core::MutationJournal>();
            std::weak_ptr<GpuBuffer> wrapper = buffer;
            buffer.reset();
            assert(!wrapper.expired());
            auto owned = wrapper.lock();
            {
                assert(prepareMetalRhiRecordedState(*owned, *recording));
                const auto conflict =
                    setMetalRhiBufferStateForBarrier(*owned, root->dependency(), conflicting.get());
                assert(!conflict.success
                    && conflict.diagnostics == "RHI-RESOURCE-STATE-RECORDING-CONFLICT"
                    && owned->state() == ResourceState::ShaderWrite);
                recording->rollback();
            }
            core::CpuMemoryQuotaLease pressure(f.quota, f.cpuBytes - f.quota->liveBytes());
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            auto previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
            assert(canPrepareMetalRhiRecordedState(*owned, *recording)
                && canPrepareMetalRhiRecordedState(*owned, *conflicting) && recording->empty()
                && conflicting->empty() && owned->state() == ResourceState::ShaderWrite);
            assert(prepareMetalRhiRecordedState(*owned, *recording));
            assert(prepareMetalRhiRecordedState(*owned, *recording));
            assert(canPrepareMetalRhiRecordedState(*owned, *recording)
                && !canPrepareMetalRhiRecordedState(*owned, *conflicting) && conflicting->empty());
            assert(!prepareMetalRhiRecordedState(*owned, *conflicting));
            assert(setMetalRhiBufferStateForBarrier(*owned, root->dependency(), recording.get())
                    .success);
            assert(
                owned->state() == ResourceState::ShaderRead && root->validateObservation().success);
            auto finished = std::move(recording);
            assert(!recording && !finished->empty());
            assert(canPrepareMetalRhiRecordedState(*owned, *finished)
                && !canPrepareMetalRhiRecordedState(*owned, *conflicting));
            assert(!prepareMetalRhiRecordedState(*owned, *conflicting));
            owned.reset();
            finished.reset(); // Root owns the wrapper during rollback.
            assert(root->resource().state() == ResourceState::ShaderWrite);
            owned = wrapper.lock();
            assert(owned);
            assert(prepareMetalRhiRecordedState(*owned, *conflicting));
            assert(setMetalRhiBufferStateForBarrier(*owned, root->dependency(), conflicting.get())
                    .success);
            conflicting->commit();
            assert(conflicting->empty() && owned->state() == ResourceState::ShaderRead);
            assert(canPrepareMetalRhiRecordedState(*owned, *conflicting) && conflicting->empty());
            auto restore = root->dependency();
            restore.before = ResourceState::ShaderRead;
            restore.after = ResourceState::ShaderWrite;
            restore.sourceStages = root->consumer().pipelineStages;
            restore.sourceAccess = root->consumer().access;
            restore.destinationStages = root->dependency().sourceStages;
            restore.destinationAccess = root->dependency().sourceAccess;
            assert(prepareMetalRhiRecordedState(*owned, *conflicting));
            assert(setMetalRhiBufferStateForBarrier(*owned, restore, conflicting.get()).success);
            conflicting->rollback();
            assert(owned->state() == ResourceState::ShaderRead);
            std::pmr::set_default_resource(previous);
            assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
            pressure.release();
            // Immutable observation is valid; it cannot authorize a fresh writer
            // against the wrong current state.
            buffer = std::move(owned);
            reject("PRODUCER-STATE");
            assert(setMetalRhiBufferStateForBarrier(*buffer, restore, conflicting.get()).success);
            conflicting->commit();
            assert(root->validateObservation().success);
        }
        auto& facts = f.provider->native()->graphicsBufferFacts[1];
        assert(facts.entries.size() == 1u);
        const auto fact = facts.entries[0];
        for (auto access : {0u, UINT32_MAX}) {
            facts.entries[0].access = access;
            assert(!root->validateObservation().success);
            reject("ABI-DOMAIN");
        }
        facts.entries[0] = fact;
        facts.entries[0].alignment *= 2u;
        assert(!root->validateObservation().success);
        facts.entries[0] = fact;
        const auto native = metalRhiNativePayloadFor(*buffer);
        const auto object = native->resource.nativeObject;
        auto wrongKind = native->resource;
        wrongKind.nativeObject = metalRhiNativePayloadFor(*f.texture)->resource.nativeObject;
        assert(std::strcmp(validateMetalRhiTrackedNativeBufferObject(wrongKind, sizeof(initial)),
                   "RHI-METAL-TRACKED-BUFFER-OBSERVATION-KIND")
            == 0);
        auto shortObject =
            createMetalRhiTrackedNativeBufferObject(f.provider->native()->nativeDevice,
                sizeof(std::uint32_t),
                native->resource.storageMode,
                f.diagnostic);
        assert(shortObject);
        native->resource.nativeObject = shortObject;
        assert(validateMetalRhiTrackedNativeBufferObject(native->resource, sizeof(initial)));
        assert(!root->validateObservation().success);
        reject("NATIVE-IDENTITY-MISMATCH");
        auto replacement =
            createMetalRhiTrackedNativeBufferObject(f.provider->native()->nativeDevice,
                sizeof(initial),
                native->resource.storageMode,
                f.diagnostic);
        assert(replacement);
        native->resource.nativeObject = replacement;
        assert(!root->validateObservation().success);
        native->resource.nativeObject = object;
        native->resource.hazardsExplicitlyTracked = false;
        assert(!root->validateObservation().success);
        native->resource.hazardsExplicitlyTracked = true;
        assert(root->validateObservation().success);
        // Metadata retention/cancellation only; this never submits or signals.
        for (bool sealed : {false, true}) {
            const char* id = nullptr;
            const auto count = root->childOwnerCount() + 1u;
            const auto ownerAt = [&](std::size_t i) noexcept -> std::shared_ptr<const void> {
                return i ? root->childOwnerAt(i - 1u) : root;
            };
            auto probe = common::CommandRetentionStorage::prepare(
                {1u, true, static_cast<std::uint32_t>(count), 0u, 0u, 0u, f.cpuBytes}, f.quota, id);
            assert(probe && !probe->retainResourceProjection(count, ownerAt));
            const auto distinct = probe->resourceOwnerCount();
            assert(distinct && probe->abandonUnsubmitted());
            probe.reset();
            auto exact = common::CommandRetentionStorage::prepare(
                {1u, true, static_cast<std::uint32_t>(distinct), 0u, 0u, 0u, f.cpuBytes},
                f.quota,
                id);
            auto less = common::CommandRetentionStorage::prepare(
                {1u, true, static_cast<std::uint32_t>(distinct - 1u), 0u, 0u, 0u, f.cpuBytes},
                f.quota,
                id);
            assert(exact && less);
            assert(less->retainResourceProjection(count, ownerAt) && !less->resourceOwnerCount());
            assert(!exact->retainResourceProjection(count, ownerAt)
                && exact->resourceOwnerCount() == distinct);
            assert(exact->abandonUnsubmitted());
            // Repeat with caller owners dropped to verify actual L2 wrapper ownership.
            assert(less->abandonUnsubmitted());
            exact.reset();
            less.reset();
            auto retained = common::CommandRetentionStorage::prepare(
                {1u, true, static_cast<std::uint32_t>(distinct), 0u, 0u, 0u, f.cpuBytes},
                f.quota,
                id);
            assert(retained && !retained->retainResourceProjection(count, ownerAt));
            if (sealed)
                assert(retained->sealForSubmission());
            std::weak_ptr<const Prepared> weak = root;
            std::weak_ptr<GpuBuffer> wrapper = buffer;
            const auto logical = buffer->id();
            buffer.reset();
            root.reset();
            assert(!weak.expired() && !wrapper.expired() && f.registry->find(logical));
            root = weak.lock();
            buffer = wrapper.lock();
            assert(root->validateObservation().success);
            root.reset();
            assert(retained->abandonUnsubmitted() && weak.expired());
            assert(!wrapper.expired());
            root = accepted();
        }
        std::array<std::uint32_t, 4> actual{};
        assert(readMetalRhiNativeBuffer(
            native, actual.data(), sizeof(actual), 0u, sizeof(actual), f.diagnostic));
        assert(actual == initial); // No preparation or failed recording writes any bytes.
        auto renderPass = pass.prepare();
        auto command = pass.command(static_cast<std::uint32_t>(
            root->childOwnerCount() + renderPass->childOwnerCount() + 2u));
        assert(!command->recordOrdinaryPreparedRenderPassBegin(renderPass, binding, f.diagnostic));
        assert(!command->stats()->nativeRenderEncoderCreated
            && !command->retentionStorage()->resourceOwnerCount());
        assert(!command->recordBufferBarrier(f.diagnostic));
        command->abandonUnsubmitted();
        root.reset();
        // Descriptor revision, arena reset and descriptor destruction stay authoritative.
        root = accepted();
        assert(f.set
                ->write({3u,
                    DescriptorType::StorageBuffer,
                    buffer->id(),
                    DescriptorResourceKind::Buffer})
                .success);
        assert(!root->validateObservation().success);
        root.reset();
        binding = f.prepare();
        root = accepted();
        f.arena->reset();
        assert(!root->validateObservation().success);
        root.reset();
        binding.reset();
        f.set.reset();
        f.set = f.arena->allocateDescriptorSet({"after reset", f.inputs});
        assert(f.set);
        assert(f.set
                ->write({3u,
                    DescriptorType::StorageBuffer,
                    buffer->id(),
                    DescriptorResourceKind::Buffer})
                .success);
        binding = f.prepare();
        root = accepted();
        f.set.reset();
        assert(!root->validateObservation().success);
        root.reset();
        binding.reset();
        f.set = f.arena->allocateDescriptorSet({"fresh", f.inputs});
        assert(f.set);
        assert(f.set
                ->write({3u,
                    DescriptorType::StorageBuffer,
                    buffer->id(),
                    DescriptorResourceKind::Buffer})
                .success);
        binding = f.prepare();
        root = accepted();
        buffer->sealWrites();
        assert(!root->validateObservation().success);
        root.reset();
        assert(!prepare());
        // The native descriptor payload may survive while the last actual L2
        // wrapper retires. Its registry entry must disappear deterministically.
        std::weak_ptr<GpuBuffer> wrapper = buffer;
        const auto logical = buffer->id();
        buffer.reset();
        assert(wrapper.expired() && !f.registry->find(logical));
        for (auto backend : {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU})
            assert(!semantics::GraphicsStorageWriteLowering::current(backend).executable);
        assert(f.device->capabilities().commandRetention.contractVersion == 0u);
        std::puts(
            "Writable immutable preparation and existing state journal: actual proof/ABI/tracking, scope/dependency, stale identity/seals/descriptor, exact/one-less ownership, metadata cancellation/move/conflict/rollback/commit and allocation0 PASS; no writable execution");
    }
};
} // namespace

int main(int argc, char** argv)
{
    assert(argc == 3);
    @autoreleasepool {
        WritePreparationFixture(argv[1], argv[2]).run();
    }
}

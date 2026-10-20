#pragma once
#include "cpu_quota_factory_fixture.h"
#include "metal_native_identity_fixture.h"
#include "metal_ordinary_graphics_fixture.h"
#include "rhi/backends/metal/metal_indirect_preparation.h"

#include <cassert>
#include <cstdio>

namespace arp::test {
class MetalClosedConsumerRecordingPreparationFixture final {
    using Root = rhi_metal::MetalRhiClosedConsumerRecordingPreparation;
    using Contract = Root::Contract;

public:
    explicit MetalClosedConsumerRecordingPreparationFixture(OrdinaryGraphicsFixture& f) : f_(f) {}

    void verify(const std::shared_ptr<const Root::Issuance>& issuance)
    {
        using namespace rhi;
        using namespace rhi_metal;
        static_assert(
            !std::is_default_constructible_v<Root> && !std::is_copy_constructible_v<Root>);
        core::CpuMemoryQuotaScope scope(f_.quota);
        const auto makeCommand = [&](const auto& registry, const auto& quota) {
            const char* id = nullptr;
            auto retention = common::CommandRetentionStorage::prepare(
                {1u, true, Contract::referenceSlots, 0u, 0u, 0u, quota->limitBytes()}, quota, id);
            assert(retention);
            auto command = createMetalRhiNativeCommandPayload("ClosedConsumerAdmission",
                registry,
                &f_.diagnostic,
                0u,
                0u,
                0u,
                std::move(retention));
            assert(command);
            return command;
        };
        auto command = makeCommand(f_.registry, f_.quota);
        Root::Budget budget{
            {1u, true, Contract::commandUnits, Contract::readbackBytes, Contract::referenceSlots},
            Root::minimumNewBytes,
            512u};
        auto selected = issuance;
        const auto prepare = [&] {
            return Root::prepare(selected, *f_.device, command, budget, f_.diagnostic);
        };
        const auto reject = [&](const char* expected) {
            const auto cpu = f_.quota->liveBytes(), gpu = f_.device->memoryStats().allocatedBytes;
            auto denied = prepare();
            if (denied || f_.diagnostic != expected)
                std::fprintf(stderr,
                    "Closed admission expected %s got %s\n",
                    expected,
                    f_.diagnostic.c_str());
            assert(!denied && f_.diagnostic == expected && cpu == f_.quota->liveBytes()
                && gpu == f_.device->memoryStats().allocatedBytes);
        };
        {
            auto warm = prepare();
            if (!warm)
                std::fprintf(stderr, "Closed admission warm: %s\n", f_.diagnostic.c_str());
            assert(warm && !warm->validate());
            budget.maxChildOwners = static_cast<std::uint32_t>(warm->childOwnerCount());
        }
        --budget.maxChildOwners;
        reject("RHI-METAL-CLOSED-CONSUMER-ADMISSION-CHILD-OWNER-BUDGET");
        ++budget.maxChildOwners;
        --budget.maxNewBytes;
        reject("RHI-METAL-CLOSED-CONSUMER-ADMISSION-BUDGET");
        ++budget.maxNewBytes;
        --budget.values.maxCommands;
        reject("RHI-CLOSED-CONSUMER-RECORDING-BUDGET");
        ++budget.values.maxCommands;
        --budget.values.maxReadbackBytes;
        reject("RHI-CLOSED-CONSUMER-RECORDING-BUDGET");
        ++budget.values.maxReadbackBytes;
        --budget.values.maxReferenceSlots;
        reject("RHI-CLOSED-CONSUMER-RECORDING-BUDGET");
        ++budget.values.maxReferenceSlots;
        budget.values.prototypeOptIn = false;
        reject("RHI-CLOSED-CONSUMER-RECORDING-BUDGET");
        budget.values.prototypeOptIn = true;
        selected = std::shared_ptr<const Root::Issuance>(issuance->images(), issuance.get());
        reject("RHI-METAL-CLOSED-CONSUMER-COMMAND-ORIGINAL-OWNER");
        selected = issuance;
        auto originalCommand = command;
        command = std::shared_ptr<MetalRhiNativeCommandPayload>(issuance, command.get());
        reject("RHI-METAL-CLOSED-CONSUMER-COMMAND-ORIGINAL-OWNER");
        command = originalCommand;
        originalCommand.reset();
        auto otherQuota = core::makeShared<core::CpuMemoryQuota>(f_.cpuBytes);
        command = makeCommand(f_.registry, otherQuota);
        reject("RHI-COMMAND-RETENTION-CPU-QUOTA-MISMATCH");
        command.reset();
        assert(!otherQuota->liveBytes());
        otherQuota.reset();
        auto foreign = createMetalRhiDevice(OrdinaryGraphicsFixture::deviceDesc());
        assert(foreign);
        command = makeCommand(metalRhiIssuingResourceRegistry(*foreign), f_.quota);
        reject("RHI-METAL-CANONICAL-PRODUCER-ISSUER-MISMATCH");
        command.reset();
        foreign.reset();
        command = makeCommand(f_.registry, f_.quota);
        CpuQuotaFactoryFixture factory(f_.quota);
        const auto peak = factory.measure(prepare);
        factory.verify(peak, prepare);
        assert(f_.diagnostic == "RHI-METAL-CLOSED-CONSUMER-ADMISSION-ALLOCATION-FAILED");
        const auto gpu = f_.device->memoryStats().allocatedBytes,
                   capacity = f_.device->memoryStats().budgetCapacityBytes;
        for (const auto available : {Root::minimumNewBytes, Root::minimumNewBytes - 1u}) {
            auto pressure = f_.device->createBuffer({"ClosedAdmissionGpuBudget",
                capacity - gpu - available,
                ResourceUsage::StorageBuffer,
                MemoryUsage::CpuToGpu,
                ResourceState::ShaderRead});
            assert(pressure);
            auto candidate = prepare();
            if (available == Root::minimumNewBytes)
                assert(candidate && f_.device->memoryStats().allocatedBytes == capacity);
            else
                assert(!candidate
                    && f_.diagnostic == "RHI-METAL-CLOSED-CONSUMER-READBACK-CREATION-FAILED");
            candidate.reset();
            pressure.reset();
            assert(f_.device->memoryStats().allocatedBytes == gpu);
        }
        auto root = prepare();
        assert(root && !root->validate() && root->ownedBytes() == 36u
            && root->facts().actualReadbackBytes == 44u
            && root->childOwnerCount() == budget.maxChildOwners
            && !root->validateForCommand(command));
        auto sibling = makeCommand(f_.registry, f_.quota);
        assert(root->validateForCommand(sibling));
        sibling.reset();
        auto aliasedCommand =
            std::shared_ptr<MetalRhiNativeCommandPayload>(issuance, command.get());
        assert(root->validateForCommand(aliasedCommand));
        aliasedCommand.reset();
        // This admission must not reserve any producer/consumer fault domain.
        {
            auto producer = BindlessFaultSubmissionLease::acquire(
                issuance->cold()->producer()->faultDomain(), f_.diagnostic);
            auto consumer = BindlessFaultSubmissionLease::acquire(
                issuance->cold()->faultDomain(), f_.diagnostic);
            assert(producer.valid() && consumer.valid());
        }
        for (std::size_t i = 0u; i < root->buffers().size(); ++i) {
            auto& buffer = *root->buffers()[i];
            auto native = root->resources()[i].buffer;
            MetalNativeIdentityMutationProbe::verify(
                native->resource, root->resources()[1u - i].buffer->resource, [&] {
                    assert(root->validate());
                });
            auto& desc = const_cast<BufferDesc&>(buffer.desc());
            const auto memory = desc.memory;
            desc.memory = MemoryUsage::CpuToGpu;
            assert(root->validate());
            desc.memory = memory;
            const auto storage = native->resource.storageMode;
            native->resource.storageMode = MetalRhiNativeStorageMode::None;
            assert(root->validate());
            native->resource.storageMode = storage;
            native->resource.nativeRuntimeBacked = false;
            assert(root->validate());
            native->resource.nativeRuntimeBacked = true;
            native->resource.hazardsExplicitlyTracked = !native->resource.hazardsExplicitlyTracked;
            assert(root->validate());
            native->resource.hazardsExplicitlyTracked = !native->resource.hazardsExplicitlyTracked;
            f_.registry->unregisterResource(buffer.id());
            assert(root->validate());
            f_.registry->registerBuffer(buffer);
            assert(!root->validate());
            auto& owned = const_cast<std::shared_ptr<GpuBuffer>&>(root->buffers()[i]);
            const auto saved = owned;
            owned = std::shared_ptr<GpuBuffer>(issuance, saved.get());
            assert(root->validate());
            owned = saved;
        }
        auto& ownedRoot = const_cast<std::shared_ptr<const Root::Issuance>&>(root->issuance());
        ownedRoot = std::shared_ptr<const Root::Issuance>(issuance->images(), issuance.get());
        assert(root->validate());
        ownedRoot = issuance;
        command->stats()->nativeDispatchEncoded = true;
        assert(root->validate());
        command->stats()->nativeDispatchEncoded = false;
        command->stats()->nativeRuntimeDiagnosticOnly = true;
        assert(root->validate());
        command->stats()->nativeRuntimeDiagnosticOnly = false;
        {
            core::CpuMemoryQuotaLease pressure(f_.quota, f_.cpuBytes - f_.quota->liveBytes());
            assert(pressure.charged());
            auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            for (unsigned i = 0u; i < 100u; ++i) {
                assert(!root->validate());
                command->stats()->nativeCommandBufferFinished = true;
                assert(root->validate());
                command->stats()->nativeCommandBufferFinished = false;
            }
            std::pmr::set_default_resource(previous);
            assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
        }
        assert(!command->stats()->nativeDispatchEncoded
            && !command->stats()->nativeIndexedDrawEncoded
            && !command->stats()->nativeBlitCopyEncoded
            && !command->retentionStorage()->resourceOwnerCount());
        // A live but closed original command cannot reuse its admission.
        assert(command->finish(f_.diagnostic) && root->validate());
        root.reset();
        command.reset();
        assert(f_.device->memoryStats().allocatedBytes == gpu);
        command = makeCommand(f_.registry, f_.quota);
        root = prepare();
        assert(root);
        command->poisonRecording("FixtureClosedAdmissionPoison");
        assert(root->validate() && !command->finish(f_.diagnostic));
        root.reset();
        command.reset();
        command = makeCommand(f_.registry, f_.quota);
        root = prepare();
        assert(root);
        command->abandonUnsubmitted();
        assert(root->validate());
        root.reset();
        command.reset();
        command = makeCommand(f_.registry, f_.quota);
        root = prepare();
        assert(root);
        std::weak_ptr<MetalRhiNativeCommandPayload> weakCommand = command;
        command.reset();
        assert(weakCommand.expired()
            && root->validate()
                == std::string_view("RHI-METAL-CLOSED-CONSUMER-COMMAND-ORIGINAL-OWNER"));
        std::weak_ptr<void> token = root->resources()[0u].buffer->resource.allocationReleaseToken;
        root.reset();
        assert(token.expired() && f_.device->memoryStats().allocatedBytes == gpu
            && !inspectSharedMetalRhiCommandCapacity().charged);
        std::printf(
            "ClosedConsumerCommandAdmission/1: original native command/readback36+fault8=44 refs26 owners%u coldPeak%llu; exact/one-less CPU/GPU/owner and substituted owner/native/issuer/state/finish/poison/abandon/retirement negatives,100 allocation0 PASS; no lease/encoding/submission\n",
            budget.maxChildOwners,
            static_cast<unsigned long long>(peak));
        command = makeCommand(f_.registry, f_.quota);
        root = prepare();
        assert(root);
        f_.device.reset();
        assert(root->validate() == std::string_view("RHI-METAL-EXCLUSIVE-IMAGES-ISSUER-RETIRED"));
        root.reset();
        command.reset();
    }

private:
    OrdinaryGraphicsFixture& f_;
};
} // namespace arp::test

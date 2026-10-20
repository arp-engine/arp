#pragma once
#include "cpu_quota_factory_fixture.h"
#include "metal_native_identity_fixture.h"
#include "metal_ordinary_graphics_fixture.h"
#include "rhi/backends/metal/metal_closed_consumer_native_preparation.h"

#include <Metal/Metal.h>
#include <cassert>
#include <cstdio>

namespace arp::test {
class MetalClosedConsumerNativePreparationFixture final {
    using Root = rhi_metal::MetalRhiClosedConsumerNativePreparation;
    using Contract = Root::Contract;

public:
    explicit MetalClosedConsumerNativePreparationFixture(OrdinaryGraphicsFixture& f) : f_(f) {}

    void verify(const std::shared_ptr<const Root::Reservation>& reservation,
        std::shared_ptr<rhi_metal::MetalRhiNativeCommandPayload>& command)
    {
        using namespace rhi;
        using namespace rhi_metal;
        static_assert(
            !std::is_default_constructible_v<Root> && !std::is_copy_constructible_v<Root>);
        core::CpuMemoryQuotaScope scope(f_.quota);
        Contract::Input input{{16u, 8u, 1u},
            {0.0f, 0.0f, 16.0f, 8.0f, 0.0f, 1.0f},
            {0, 0, 16u, 8u},
            {0.25f, 0.5f, 0.75f, 1.0f}};
        Root::Budget budget{{1u, true, 512u}, 512u};
        auto selected = reservation;
        const auto prepare = [&] {
            return Root::prepare(selected, *f_.device, input, budget, f_.diagnostic);
        };
        const auto cpu = f_.quota->liveBytes(), gpu = f_.device->memoryStats().allocatedBytes;
        {
            auto warm = prepare();
            if (!warm)
                std::fprintf(stderr, "Native consumer warm: %s\n", f_.diagnostic.c_str());
            assert(warm && !warm->validate());
            budget.maxChildOwners = static_cast<std::uint32_t>(warm->childOwnerCount());
        }
        const auto reject = [&](const char* expected) {
            auto denied = prepare();
            if (denied || f_.diagnostic != expected)
                std::fprintf(stderr,
                    "Native consumer expected %s got %s\n",
                    expected,
                    f_.diagnostic.c_str());
            assert(!denied && f_.diagnostic == expected && cpu == f_.quota->liveBytes()
                && gpu == f_.device->memoryStats().allocatedBytes);
        };
        --budget.maxChildOwners;
        reject("RHI-METAL-CLOSED-CONSUMER-NATIVE-CHILD-OWNER-BUDGET");
        ++budget.maxChildOwners;
        --budget.values.maxTargetBytes;
        reject("RHI-CLOSED-CONSUMER-TARGET-BYTE-BUDGET");
        ++budget.values.maxTargetBytes;
        budget.values.prototypeOptIn = false;
        reject("RHI-CLOSED-CONSUMER-TARGET-BUDGET");
        budget.values.prototypeOptIn = true;
        input.scissor.x = 1;
        reject("RHI-CLOSED-CONSUMER-TARGET-DYNAMIC-BOUNDS");
        input.scissor.x = 0;
        selected =
            std::shared_ptr<const Root::Reservation>(reservation->preparation(), reservation.get());
        reject("RHI-METAL-CLOSED-CONSUMER-NATIVE-ORIGINAL-OWNER");
        selected = reservation;
        auto foreign = createMetalRhiDevice(OrdinaryGraphicsFixture::deviceDesc());
        assert(foreign);
        assert(!Root::prepare(reservation, *foreign, input, budget, f_.diagnostic)
            && f_.diagnostic == "RHI-METAL-CLOSED-CONSUMER-NATIVE-ISSUER");
        foreign.reset();
        CpuQuotaFactoryFixture factory(f_.quota);
        const auto peak = factory.measure(prepare);
        factory.verify(peak, prepare);
        assert(f_.diagnostic == "RHI-METAL-CLOSED-CONSUMER-NATIVE-ALLOCATION-FAILED");
        const auto capacity = f_.device->memoryStats().budgetCapacityBytes;
        for (const auto available : {512u, 511u}) {
            auto pressure = f_.device->createBuffer({"NativeConsumerGpuBudget",
                capacity - gpu - available,
                ResourceUsage::StorageBuffer,
                MemoryUsage::CpuToGpu,
                ResourceState::ShaderRead});
            assert(pressure);
            auto candidate = prepare();
            if (available == 512u)
                assert(candidate && f_.device->memoryStats().allocatedBytes == capacity);
            else
                assert(!candidate
                    && f_.diagnostic == "RHI-METAL-CLOSED-CONSUMER-NATIVE-TARGET-CREATION-FAILED");
            candidate.reset();
            pressure.reset();
            assert(f_.device->memoryStats().allocatedBytes == gpu);
        }
        auto root = prepare();
        assert(root && !root->validate() && root->ownedBytes() == 512u
            && root->childOwnerCount() == budget.maxChildOwners && root->slots().size() == 4u
            && root->states()->empty());
        auto& targetOwner = const_cast<std::shared_ptr<GpuTexture>&>(root->target());
        const auto savedTarget = targetOwner;
        targetOwner = std::shared_ptr<GpuTexture>(reservation, savedTarget.get());
        assert(root->validate());
        targetOwner = savedTarget;
        auto& reservationOwner =
            const_cast<std::shared_ptr<const Root::Reservation>&>(root->reservation());
        reservationOwner =
            std::shared_ptr<const Root::Reservation>(root->target(), reservation.get());
        assert(root->validate());
        reservationOwner = reservation;
        auto& native = root->resource().texture->resource;
        const auto& replacement = reservation->preparation()
                                      ->issuance()
                                      ->images()
                                      ->images()[0u]
                                      .parent()
                                      .texture->resource;
        MetalNativeIdentityMutationProbe::verify(native, replacement, [&] {
            assert(root->validate());
        });
        const auto storage = native.storageMode;
        native.storageMode = MetalRhiNativeStorageMode::Shared;
        assert(root->validate());
        native.storageMode = storage;
        native.nativeRuntimeDiagnosticOnly = true;
        assert(root->validate());
        native.nativeRuntimeDiagnosticOnly = false;
        native.hazardsExplicitlyTracked = !native.hazardsExplicitlyTracked;
        assert(root->validate());
        native.hazardsExplicitlyTracked = !native.hazardsExplicitlyTracked;
        f_.registry->unregisterResource(root->target()->id());
        assert(root->validate());
        f_.registry->registerTexture(*root->target());
        assert(!root->validate());
        auto descriptor =
            reinterpret_cast<MTLRenderPassDescriptor*>(root->native().descriptor.get());
        const auto load = descriptor.colorAttachments[0u].loadAction;
        descriptor.colorAttachments[0u].loadAction = MTLLoadActionLoad;
        assert(root->validate());
        descriptor.colorAttachments[0u].loadAction = load;
        const auto clear = descriptor.colorAttachments[0u].clearColor;
        descriptor.colorAttachments[0u].clearColor = MTLClearColorMake(1.0, 0.0, 0.0, 1.0);
        assert(root->validate());
        descriptor.colorAttachments[0u].clearColor = clear;
        auto& descriptorOwner =
            const_cast<MetalRhiNativeRenderPassPreparation&>(root->native()).descriptor;
        const auto savedDescriptor = descriptorOwner;
        descriptorOwner = std::shared_ptr<void>(root, savedDescriptor.get());
        assert(root->validate());
        descriptorOwner = savedDescriptor;
        auto& slot = const_cast<Root::Slots&>(root->slots())[0u];
        const auto savedSlot = slot;
        slot = std::shared_ptr<core::ExternalObjectSlot>(root, savedSlot.get());
        assert(root->validate());
        slot = savedSlot;
        id object = reinterpret_cast<id>(root->resource().texture->resource.nativeObject.get());
        [object retain];
        assert(slot->adopt(reinterpret_cast<void*>(object)) && root->validate());
        slot->reset();
        assert(prepareMetalRhiRecordedState(*root->target(), *root->states()) && root->validate());
        root->states()->rollback();
        assert(!root->validate());
        auto& desc = const_cast<TextureDesc&>(root->target()->desc());
        const auto usage = desc.usage;
        desc.usage = TextureUsage::Sampled;
        assert(root->validate());
        desc.usage = usage;
        auto& authored = const_cast<Contract::Input&>(root->input());
        authored.viewport.width = 8.0f;
        assert(root->validate());
        authored.viewport.width = 16.0f;
        auto& facts = const_cast<Contract::Facts&>(root->facts());
        ++facts.targetBytes;
        assert(root->validate());
        --facts.targetBytes;
        {
            core::CpuMemoryQuotaLease pressure(f_.quota, f_.cpuBytes - f_.quota->liveBytes());
            assert(pressure.charged());
            auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            for (unsigned i = 0u; i < 100u; ++i) {
                assert(!root->validate());
                descriptor.colorAttachments[0u].loadAction = MTLLoadActionLoad;
                assert(root->validate());
                descriptor.colorAttachments[0u].loadAction = load;
            }
            std::pmr::set_default_resource(previous);
            assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
        }
        auto completion = command->preparedCompletionState();
        assert(completion
            && core::SharedOwnerProjection::sameOwner(
                completion, command->preparedCompletionState()));
        completion->complete(true);
        assert(!root->validate() && reservation->status() == Root::Reservation::Status::Cold);
        assert(!BindlessFaultSubmissionLease::acquire(
                   reservation->preparation()->issuance()->cold()->producer()->faultDomain(),
                   f_.diagnostic)
                   .valid()
            && !BindlessFaultSubmissionLease::acquire(
                reservation->preparation()->issuance()->cold()->faultDomain(), f_.diagnostic)
                .valid());
        completion->reset();
        completion.reset();
        assert(!command->stats()->nativeDispatchEncoded
            && !command->stats()->nativeIndexedDrawEncoded
            && !command->stats()->nativeBlitCopyEncoded);
        std::printf(
            "ClosedConsumerTargetPreparation/1: explicit target16x8 RGBA8 bytes512 childRoles%u coldCpuPeak%llu; original reservation/target/descriptor/journal/four encoder slots, exact/one-less CPU/GPU/owners, descriptor actions/native/control-block/registry/storage/hazard/state/issuer negatives and100 allocation0 PASS; no GPU encoding/submission/completion\n",
            budget.maxChildOwners,
            static_cast<unsigned long long>(peak));
        command->abandonUnsubmitted();
        command.reset();
        assert(root->validate());
    }

private:
    OrdinaryGraphicsFixture& f_;
};
} // namespace arp::test

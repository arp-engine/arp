#pragma once
#include "cpu_quota_factory_fixture.h"
#include "metal_closed_consumer_native_preparation_fixture.h"
#include "metal_closed_consumer_native_recording_fixture.h"
#include "metal_closed_consumer_storage_observation_fixture.h"
#include "metal_ordinary_graphics_fixture.h"
#include "rhi/backends/metal/metal_indirect_preparation.h"

#include <cassert>
#include <cstdio>

namespace arp::test {
class MetalClosedConsumerRecordingReservationFixture final {
    using Root = rhi_metal::MetalRhiClosedConsumerRecordingReservation;
    using Preparation = Root::Preparation;
    using Contract = Preparation::Contract;

    struct Candidate {
        std::shared_ptr<rhi_metal::MetalRhiNativeCommandPayload> command;
        std::shared_ptr<const Preparation> preparation;
        std::shared_ptr<const Root> reservation;
    };

public:
    explicit MetalClosedConsumerRecordingReservationFixture(OrdinaryGraphicsFixture& f) : f_(f) {}

    void verify(const std::shared_ptr<const Preparation::Issuance>& issuance,
        unsigned preparationStage = 0u)
    {
        using namespace rhi;
        using namespace rhi_metal;
        static_assert(
            !std::is_default_constructible_v<Root> && !std::is_copy_constructible_v<Root>);
        core::CpuMemoryQuotaScope scope(f_.quota);
        if (preparationStage >= 3u) {
            assert(preparationStage <= 4u);
            MetalClosedConsumerNativeRecordingFixture(f_).verify(issuance, preparationStage == 4u);
            return;
        }
        Root::Budget budget{1u, true, 512u};
        const auto prepare = [&]() -> std::optional<Candidate> {
            try {
                const char* id = nullptr;
                auto retention = common::CommandRetentionStorage::prepare(
                    {1u, true, Contract::referenceSlots, 0u, 0u, 0u, f_.cpuBytes}, f_.quota, id);
                if (!retention) {
                    f_.diagnostic = id;
                    return std::nullopt;
                }
                auto command = createMetalRhiNativeCommandPayload("ClosedConsumerReservation",
                    f_.registry,
                    &f_.diagnostic,
                    0u,
                    0u,
                    0u,
                    std::move(retention));
                if (!command)
                    return std::nullopt;
                auto preparation = Preparation::prepare(issuance,
                    *f_.device,
                    command,
                    {{1u,
                         true,
                         Contract::commandUnits,
                         Contract::readbackBytes,
                         Contract::referenceSlots},
                        Preparation::minimumNewBytes,
                        512u},
                    f_.diagnostic);
                if (!preparation)
                    return std::nullopt;
                auto root = Root::prepare(preparation, budget, f_.diagnostic);
                if (!root)
                    return std::nullopt;
                assert(!root->validate());
                return Candidate{std::move(command), std::move(preparation), std::move(root)};
            } catch (const std::bad_alloc&) {
                return std::nullopt;
            }
        };
        const auto cpu = f_.quota->liveBytes(), gpu = f_.device->memoryStats().allocatedBytes;
        {
            auto warm = prepare();
            assert(warm);
            budget.maxChildOwners =
                static_cast<std::uint32_t>(warm->reservation->childOwnerCount());
        }
        assert(cpu == f_.quota->liveBytes() && gpu == f_.device->memoryStats().allocatedBytes
            && !inspectSharedMetalRhiCommandCapacity().charged);
        if (preparationStage) {
            auto candidate = prepare();
            assert(candidate);
            if (preparationStage == 1u)
                MetalClosedConsumerNativePreparationFixture(f_).verify(
                    candidate->reservation, candidate->command);
            else {
                assert(preparationStage == 2u);
                MetalClosedConsumerStorageObservationFixture(f_).verify(
                    candidate->preparation, candidate->command);
            }
            return;
        }
        const auto reject = [&](const char* expected) {
            const auto beforeCpu = f_.quota->liveBytes(),
                       beforeGpu = f_.device->memoryStats().allocatedBytes;
            assert(!prepare() && f_.diagnostic == expected);
            assert(beforeCpu == f_.quota->liveBytes()
                && beforeGpu == f_.device->memoryStats().allocatedBytes);
        };
        --budget.maxChildOwners;
        reject("RHI-METAL-CLOSED-CONSUMER-RESERVATION-CHILD-OWNER-BUDGET");
        ++budget.maxChildOwners;
        budget.prototypeOptIn = false;
        reject("RHI-METAL-CLOSED-CONSUMER-RESERVATION-BUDGET");
        budget.prototypeOptIn = true;
        budget.contractVersion = 0u;
        reject("RHI-METAL-CLOSED-CONSUMER-RESERVATION-BUDGET");
        budget.contractVersion = 1u;
        {
            auto producer = BindlessFaultSubmissionLease::acquire(
                issuance->cold()->producer()->faultDomain(), f_.diagnostic);
            assert(producer.valid());
            reject("RHI-BINDLESS-FAULT-SUBMISSION-IN-FLIGHT");
        }
        {
            auto consumer = BindlessFaultSubmissionLease::acquire(
                issuance->cold()->faultDomain(), f_.diagnostic);
            assert(consumer.valid());
            reject("RHI-BINDLESS-FAULT-SUBMISSION-IN-FLIGHT");
            auto producer = BindlessFaultSubmissionLease::acquire(
                issuance->cold()->producer()->faultDomain(), f_.diagnostic);
            assert(producer.valid());
        }
        // Packet reservation also rolled back after either partial fault failure.
        {
            auto retry = prepare();
            assert(retry && !retry->reservation->validate());
        }
        CpuQuotaFactoryFixture factory(f_.quota);
        const auto peak = factory.measure(prepare);
        factory.verify(peak, prepare);
        assert(cpu == f_.quota->liveBytes() && gpu == f_.device->memoryStats().allocatedBytes);
        auto candidate = prepare();
        assert(candidate);
        const auto root = candidate->reservation;
        assert(root->childOwnerCount() == budget.maxChildOwners
            && root->status() == Root::Status::Cold && !root->validate());
        const auto reservedGpu = f_.device->memoryStats().allocatedBytes;
        assert(!Root::prepare(candidate->preparation, budget, f_.diagnostic)
            && f_.diagnostic == "RHI-METAL-CLOSED-CONSUMER-COMMAND-RESERVATION-EXISTS");
        reject("RHI-BINDLESS-INDIRECT-PACKET-IN-FLIGHT");
        assert(!BindlessFaultSubmissionLease::acquire(
                   issuance->cold()->producer()->faultDomain(), f_.diagnostic)
                   .valid()
            && !BindlessFaultSubmissionLease::acquire(
                issuance->cold()->faultDomain(), f_.diagnostic)
                .valid());
        auto alias = std::shared_ptr<const Preparation>(issuance, candidate->preparation.get());
        assert(!Root::prepare(alias, budget, f_.diagnostic)
            && f_.diagnostic == "RHI-METAL-CLOSED-CONSUMER-RESERVATION-ORIGINAL-OWNER");
        alias.reset();
        assert(issuance->packet()->prototypeClosedConsumerCold()
            && !issuance->packet()->consumerPlan().success
            && issuance->packet()->validate().diagnostics
                == "RHI-BINDLESS-INDIRECT-PACKET-PROTOTYPE-CLOSED-CONSUMER-COLD");
        assert(!candidate->command->finish(f_.diagnostic)
            && f_.diagnostic == Root::executionDiagnostic() && !root->validate());
        assert(!candidate->command->recordBlitPassBegin(f_.diagnostic)
            && f_.diagnostic == "RHI-METAL-CLOSED-CONSUMER-EXCLUSIVE-COMMAND");
        {
            core::CpuMemoryQuotaLease pressure(f_.quota, f_.cpuBytes - f_.quota->liveBytes());
            assert(pressure.charged());
            auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            for (unsigned i = 0u; i < 100u; ++i) {
                assert(!root->validate());
                candidate->command->stats()->nativeRuntimeDiagnosticOnly = true;
                assert(root->validate());
                candidate->command->stats()->nativeRuntimeDiagnosticOnly = false;
            }
            std::pmr::set_default_resource(previous);
            assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
        }
        assert(f_.device->memoryStats().allocatedBytes == reservedGpu
            && !candidate->command->stats()->nativeDispatchEncoded
            && !candidate->command->stats()->nativeIndexedDrawEncoded
            && !candidate->command->stats()->nativeBlitCopyEncoded
            && !candidate->command->retentionStorage()->resourceOwnerCount());
        std::weak_ptr<MetalRhiNativeCommandPayload> weakCommand = candidate->command;
        candidate->command.reset();
        assert(
            weakCommand.expired() && root->status() == Root::Status::Abandoned && root->validate());
        // Retaining a terminal reservation cannot block independent original domains.
        {
            auto producer = BindlessFaultSubmissionLease::acquire(
                issuance->cold()->producer()->faultDomain(), f_.diagnostic);
            auto consumer = BindlessFaultSubmissionLease::acquire(
                issuance->cold()->faultDomain(), f_.diagnostic);
            assert(producer.valid() && consumer.valid());
        }
        {
            auto retry = prepare();
            assert(retry);
            retry->command->poisonRecording("ClosedReservationFixturePoison");
            assert(retry->reservation->status() == Root::Status::Failed
                && retry->reservation->validate());
        }
        {
            auto retry = prepare();
            assert(retry);
            retry->command->abandonUnsubmitted();
            assert(retry->reservation->status() == Root::Status::Abandoned
                && retry->reservation->validate());
        }
        candidate.reset();
        std::printf(
            "ClosedConsumerReservation/1: original command-owned packet+producer+consumer leases, childRoles%u combinedFactoryCpuPeak%llu, new reservation GPU0; transactional partial acquisition/packet contention, exact/one-less CPU/owner, original aliases/version/opt-in, finish/ordinary encoding closed, command destruction/poison/abandon and100 allocation0 PASS; no GPU recording or completion\n",
            budget.maxChildOwners,
            static_cast<unsigned long long>(peak));
    }

private:
    OrdinaryGraphicsFixture& f_;
};
} // namespace arp::test

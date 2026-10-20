#pragma once
#include "metal_native_identity_fixture.h"
#include "metal_ordinary_graphics_fixture.h"
#include "rhi/backends/metal/metal_indirect_preparation.h"

#include <cassert>
#include <cstdio>

namespace arp::test {
// Real original Metal resources and logical journals only. No native packet
// production, draw or completion may be inferred from these state observations.
class MetalClosedConsumerStorageObservationFixture final {
    using Preparation = rhi_metal::MetalRhiClosedConsumerRecordingPreparation;
    using Builder = Preparation::Builder;
    using Contract = Preparation::Contract;

public:
    explicit MetalClosedConsumerStorageObservationFixture(OrdinaryGraphicsFixture& fixture)
        : f_(fixture)
    {}

    void verify(const std::shared_ptr<const Preparation>& preparation,
        const std::shared_ptr<rhi_metal::MetalRhiNativeCommandPayload>& command)
    {
        using namespace rhi;
        using namespace rhi_metal;
        core::CpuMemoryQuotaScope scope(f_.quota);
        const auto& issuance = preparation->issuance();
        const auto& consumer = issuance->cold();
        const auto& producer = consumer->producer();
        const auto& packet = producer->packet();
        const Contract::Budget budget{
            1u, true, Contract::commandUnits, Contract::readbackBytes, Contract::referenceSlots};
        Contract::Inputs inputs;
        Contract::Facts original, current;
        CanonicalClosedConsumerRecordingAdmission admission;
        assert(!Builder::capture(issuance,
            preparation->buffers(),
            budget,
            command->recordingIdentity(),
            inputs,
            original));
        assert(!CanonicalClosedConsumerRecordingAdmission::capture(
            inputs, budget, command->recordingIdentity(), admission));
        core::MutationJournal journal;
        const std::array<GpuBuffer*, 6u> logical{producer->uploads()[1u].buffer.get(),
            producer->uploads()[2u].buffer.get(),
            producer->faultDomain()->faultLatchBuffer(),
            packet->packet()->physicalArgs(),
            packet->packet()->instanceMappings(),
            consumer->faultDomain()->faultLatchBuffer()};
        const auto finalState = [](std::size_t role) {
            return role == 3u || role == 4u ? ResourceState::ShaderRead : ResourceState::CopySrc;
        };
        const auto transition = [&](std::size_t role, bool ready) {
            auto& buffer = *logical[role];
            ResourceBarrierDesc intent;
            intent.resource = buffer.id();
            intent.before = ready ? ResourceState::ShaderWrite : finalState(role);
            intent.after = ready ? finalState(role) : ResourceState::ShaderWrite;
            const bool packetBuffer = role == 3u || role == 4u;
            const auto stage = role == 3u ? PipelineStage::DrawIndirect
                : role == 4u              ? PipelineStage::VertexShader
                                          : PipelineStage::Transfer;
            const auto access = role == 3u ? ResourceAccess::IndirectCommandRead
                : role == 4u               ? ResourceAccess::ShaderRead
                                           : ResourceAccess::TransferRead;
            intent.sourceStages = ready ? (packetBuffer        ? PipelineStage::Transfer
                                                  : role == 5u ? PipelineStage::FragmentShader
                                                               : PipelineStage::ComputeShader)
                                        : stage;
            intent.sourceAccess = ready
                ? (packetBuffer ? ResourceAccess::TransferWrite : ResourceAccess::ShaderWrite)
                : access;
            intent.destinationStages = ready ? stage : PipelineStage::ComputeShader;
            intent.destinationAccess = ready ? access : ResourceAccess::ShaderWrite;
            if (ready && packetBuffer) {
                auto prepare = intent;
                prepare.after = ResourceState::CopyDst;
                prepare.sourceStages = PipelineStage::ComputeShader;
                prepare.sourceAccess = ResourceAccess::ShaderWrite;
                prepare.destinationStages = PipelineStage::Transfer;
                prepare.destinationAccess = ResourceAccess::TransferWrite;
                assert(setMetalRhiBufferStateForBarrier(buffer, prepare, &journal).success);
                intent.before = ResourceState::CopyDst;
                assert(packet->validateClosedReadbackReady());
            }
            assert(setMetalRhiBufferStateForBarrier(buffer, intent, &journal).success);
        };
        const auto denied = [&] {
            current = original;
            Contract::Inputs observed = inputs;
            assert(Builder::observeClosedReadbackReady(issuance,
                       preparation->buffers(),
                       budget,
                       command->recordingIdentity(),
                       observed,
                       current)
                && !observed.producer.packet && !current.identity.valid()
                && !current.actualReadbackBytes);
            assert(issuance->validateClosedReadbackReady());
        };
        const auto ready = [&] {
            Contract::Inputs observed;
            assert(!packet->validateClosedReadbackReady()
                && !producer->validateClosedReadbackReady()
                && !consumer->validateClosedReadbackReady()
                && !issuance->validateClosedReadbackReady());
            assert(!Builder::observeClosedReadbackReady(issuance,
                       preparation->buffers(),
                       budget,
                       command->recordingIdentity(),
                       observed,
                       current)
                && current.identity == original.identity && current.metadata == original.metadata
                && current.bytes == original.bytes
                && current.actualReadbackBytes == original.actualReadbackBytes);
            assert(!admission.validate(
                observed, command->recordingIdentity(), Contract::StoragePhase::ReadbackReady));
            assert(packet->validate() && producer->validate() && consumer->validate()
                && issuance->validate() && preparation->validate());
        };
        denied();
        for (std::size_t i = 0u; i < logical.size(); ++i) {
            transition(i, true);
            if (i + 1u < logical.size())
                denied();
        }
        ready();
        Contract::Inputs discarded;
        Contract::Facts discardedFacts;
        assert(Builder::capture(issuance,
                   preparation->buffers(),
                   budget,
                   command->recordingIdentity(),
                   discarded,
                   discardedFacts)
            && !discarded.producer.packet && !discardedFacts.identity.valid());
        for (std::size_t i = 0u; i < logical.size(); ++i) {
            transition(i, false);
            denied();
            transition(i, true);
            ready();
        }
        for (const auto* resource :
            {&packet->resources()[0u], &producer->resources()[3u], &consumer->resources()[2u]}) {
            auto& native = resource->buffer->resource;
            MetalNativeIdentityMutationProbe::verify(
                native, preparation->resources()[0u].buffer->resource, [&] {
                    denied();
                });
            ready();
        }
        f_.registry->unregisterResource(logical[4u]->id());
        denied();
        f_.registry->registerBuffer(*logical[4u]);
        ready();
        assert(consumer->faultDomain()->generation()
            && packet->packet()->prototypeClosedConsumerCold()
            && !packet->packet()->consumerPlan().success);
        auto alias = std::shared_ptr<const Builder::Issuance>(consumer, issuance.get());
        assert(Builder::observeClosedReadbackReady(alias,
            preparation->buffers(),
            budget,
            command->recordingIdentity(),
            discarded,
            discardedFacts));
        alias.reset();
        auto readbacks = preparation->buffers();
        readbacks[0u] = std::shared_ptr<GpuBuffer>(issuance, readbacks[0u].get());
        assert(Builder::observeClosedReadbackReady(
            issuance, readbacks, budget, command->recordingIdentity(), discarded, discardedFacts));
        readbacks = {};
        auto completion = command->preparedCompletionState();
        assert(completion);
        completion->complete(true);
        ready();
        completion->reset();
        assert(!command->finish(f_.diagnostic)
            && f_.diagnostic == MetalRhiClosedConsumerRecordingReservation::executionDiagnostic());
        assert(
            !BindlessFaultSubmissionLease::acquire(producer->faultDomain(), f_.diagnostic).valid()
            && !BindlessFaultSubmissionLease::acquire(consumer->faultDomain(), f_.diagnostic)
                .valid());
        {
            core::CpuMemoryQuotaLease pressure(f_.quota, f_.cpuBytes - f_.quota->liveBytes());
            assert(pressure.charged());
            auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            for (unsigned i = 0u; i < 100u; ++i) {
                ready();
                transition(4u, false);
                denied();
                transition(4u, true);
            }
            std::pmr::set_default_resource(previous);
            assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
        }
        journal.rollback();
        assert(journal.empty() && !preparation->validate() && !issuance->validate());
        denied();
        assert(!command->stats()->nativeDispatchEncoded
            && !command->stats()->nativeIndexedDrawEncoded
            && !command->stats()->nativeBlitCopyEncoded);
        std::puts(
            "ClosedConsumerStorageObservation/1: exact original Metal resources, final six states/shared identity/frozen admission; every partial state, native/control-block/registry/owner/cold recapture negatives, journal rollback/spoofed completion/leases and100 exhausted allocation0 PASS; logical state only, no GPU encoding/draw/completion");
    }

private:
    OrdinaryGraphicsFixture& f_;
};
} // namespace arp::test

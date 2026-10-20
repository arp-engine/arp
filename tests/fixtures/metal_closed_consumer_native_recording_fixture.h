#pragma once
#include "cpu_quota_factory_fixture.h"
#include "metal_native_identity_fixture.h"
#include "metal_ordinary_graphics_fixture.h"
#include "rhi/backends/metal/metal_closed_consumer_native_preparation.h"
#include "rhi/backends/metal/metal_native_sync.h"

#include <cassert>
#include <cstdio>

namespace arp::test {
class MetalClosedConsumerNativeRecordingFixture final {
    using Native = rhi_metal::MetalRhiClosedConsumerNativePreparation;
    using Reservation = Native::Reservation;
    using Preparation = Reservation::Preparation;
    using Contract = Preparation::Contract;
    using Status = Reservation::Status;

    struct Candidate {
        std::shared_ptr<rhi_metal::MetalRhiNativeCommandPayload> command;
        std::shared_ptr<const Preparation> preparation;
        std::shared_ptr<const Reservation> reservation;
        std::shared_ptr<const Native> native;
    };

public:
    explicit MetalClosedConsumerNativeRecordingFixture(OrdinaryGraphicsFixture& fixture)
        : f_(fixture)
    {}

    void verify(const std::shared_ptr<const Preparation::Issuance>& issuance, bool execute = false)
    {
        using namespace rhi;
        using namespace rhi_metal;
        core::CpuMemoryQuotaScope scope(f_.quota);
        Contract::Budget budget{
            1u, true, Contract::commandUnits, Contract::readbackBytes, Contract::referenceSlots};
        std::uint32_t ownerCapacity = 512u;
        auto selectedIssuance = issuance;
        const auto makeCommand = [&]() {
            const char* id = nullptr;
            auto retention = common::CommandRetentionStorage::prepare(
                {1u, true, ownerCapacity, 0u, 0u, 0u, f_.cpuBytes}, f_.quota, id);
            if (!retention) {
                f_.diagnostic = id;
                return std::shared_ptr<MetalRhiNativeCommandPayload>{};
            }
            return createMetalRhiClosedConsumerNativeCommandPayload(budget,
                "ClosedConsumerRecording",
                f_.registry,
                std::move(retention),
                f_.diagnostic);
        };
        const auto prepare = [&]() -> std::optional<Candidate> {
            try {
                auto command = makeCommand();
                if (!command)
                    return std::nullopt;
                auto preparation = Preparation::prepare(selectedIssuance,
                    *f_.device,
                    command,
                    {budget, Preparation::minimumNewBytes, 512u},
                    f_.diagnostic);
                if (!preparation)
                    return std::nullopt;
                auto reservation =
                    Reservation::prepare(preparation, {1u, true, 512u}, f_.diagnostic);
                if (!reservation)
                    return std::nullopt;
                auto native = Native::prepare(reservation,
                    *f_.device,
                    {{16u, 8u, 1u},
                        {0.0f, 0.0f, 16.0f, 8.0f, 0.0f, 1.0f},
                        {0, 0, 16u, 8u},
                        {0.25f, 0.5f, 0.75f, 1.0f}},
                    {{1u, true, 512u}, 512u},
                    f_.diagnostic);
                if (!native)
                    return std::nullopt;
                return Candidate{std::move(command),
                    std::move(preparation),
                    std::move(reservation),
                    std::move(native)};
            } catch (const std::bad_alloc&) {
                return std::nullopt;
            }
        };
        const auto cpu = f_.quota->liveBytes(), gpu = f_.device->memoryStats().allocatedBytes;
        {
            auto warm = prepare();
            assert(warm);
            core::BoundedRetainer<void> owners(warm->native->childOwnerCount() + 1u);
            assert(owners.retainMapped(warm->native->childOwnerCount() + 1u,
                [&](std::size_t i) noexcept -> std::shared_ptr<const void> {
                    return i ? warm->native->childOwnerAt(i - 1u) : warm->native;
                }));
            ownerCapacity = static_cast<std::uint32_t>(owners.values().size());
        }
        assert(cpu == f_.quota->liveBytes() && gpu == f_.device->memoryStats().allocatedBytes);
        CpuQuotaFactoryFixture factory(f_.quota);
        const auto peak = factory.measure(prepare);
        factory.verify(peak, prepare);
        auto originalBudget = budget;
        for (unsigned invalid = 0u; invalid < 5u; ++invalid) {
            switch (invalid) {
            case 0u:
                budget.contractVersion = 0u;
                break;
            case 1u:
                budget.prototypeOptIn = false;
                break;
            case 2u:
                --budget.maxCommands;
                break;
            case 3u:
                --budget.maxReadbackBytes;
                break;
            case 4u:
                --budget.maxReferenceSlots;
                break;
            }
            assert(!makeCommand() && f_.diagnostic == "RHI-CLOSED-CONSUMER-RECORDING-BUDGET");
            budget = originalBudget;
        }
        auto candidate = prepare();
        assert(candidate);
        auto& command = candidate->command;
        const auto& root = candidate->native;
        const auto deny = [&](const auto& selected, const char* expected) {
            assert(!command->recordClosedConsumer(selected, f_.diagnostic));
            if (f_.diagnostic != expected)
                std::fprintf(stderr,
                    "Closed recording expected %s got %s\n",
                    expected,
                    f_.diagnostic.c_str());
            assert(f_.diagnostic == expected && candidate->reservation->status() == Status::Cold
                && root->states()->empty() && !command->stats()->nativeDispatchEncoded
                && !command->stats()->nativeIndexedDrawEncoded
                && !command->retentionStorage()->resourceOwnerCount());
        };
        deny(std::shared_ptr<const Native>(issuance, root.get()),
            "RHI-METAL-CLOSED-CONSUMER-RECORDING-DOMAIN");
        auto sibling = makeCommand();
        assert(sibling && !sibling->recordClosedConsumer(root, f_.diagnostic));
        sibling.reset();
        auto& bindings = const_cast<MetalRhiZeroBaseIndirectConsumerColdBindings::Buffers&>(
            issuance->cold()->buffers());
        const auto slot = bindings[0u].range.fact.slot;
        ++bindings[0u].range.fact.slot;
        const auto* id = root->validate();
        assert(id);
        bindings[0u].range.fact.slot = slot;
        ++bindings[0u].range.fact.slot;
        deny(root, id);
        bindings[0u].range.fact.slot = slot;
        core::MutationJournal foreign;
        auto& argument = *issuance->cold()->producer()->uploads()[1u].buffer;
        assert(prepareMetalRhiRecordedState(argument, foreign));
        deny(root, "RHI-RESOURCE-STATE-RECORDING-CONFLICT");
        foreign.rollback();
        const auto record = [&](Candidate& selected, bool finish = true) {
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            core::CpuMemoryQuotaLease pressure(f_.quota, f_.cpuBytes - f_.quota->liveBytes());
            assert(pressure.charged());
            auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
            const bool recorded =
                selected.command->recordClosedConsumer(selected.native, f_.diagnostic);
            if (!recorded)
                std::fprintf(stderr, "Closed record: %s\n", f_.diagnostic.c_str());
            assert(recorded);
            assert(selected.reservation->status() == Status::Recorded
                && selected.command->stats()->closedConsumerCommandCount == Contract::commandUnits
                && selected.command->stats()->nativeDispatchEncoded
                && selected.command->stats()->nativeIndexedDrawEncoded
                && selected.command->stats()->nativeBlitCopyEncoded
                && !selected.native->states()->empty()
                && selected.command->retentionStorage()->resourceOwnerCount() == ownerCapacity);
            for (const auto& encoder : selected.native->slots())
                assert(encoder && !encoder->get());
            assert(!selected.preparation->issuance()->validateClosedReadbackReady()
                && selected.native->validate());
            if (finish)
                assert(selected.command->finish(f_.diagnostic));
            std::pmr::set_default_resource(previous);
            assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
        };
        const auto terminal = [&](Candidate& selected, Status expected) {
            assert(selected.reservation->status() == expected && selected.native->states()->empty()
                && !selected.preparation->issuance()->validate());
            for (const auto& encoder : selected.native->slots())
                assert(!encoder->get());
            assert(BindlessFaultSubmissionLease::acquire(
                       selected.preparation->issuance()->cold()->producer()->faultDomain(),
                       f_.diagnostic)
                       .valid()
                && BindlessFaultSubmissionLease::acquire(
                    selected.preparation->issuance()->cold()->faultDomain(), f_.diagnostic)
                    .valid());
        };
        auto completion = command->preparedCompletionState();
        completion->complete(true);
        assert(!root->validate());
        completion->reset();
        completion.reset();
        assert(
            !command->finish(f_.diagnostic) && f_.diagnostic == Reservation::executionDiagnostic());
        record(*candidate);
        assert(!command->recordClosedConsumer(root, f_.diagnostic)
            && candidate->reservation->status() == Status::Recorded);
        command->abandonUnsubmitted();
        terminal(*candidate, Status::Abandoned);
        candidate.reset();
        --ownerCapacity;
        candidate = prepare();
        assert(candidate);
        assert(!candidate->command->recordClosedConsumer(candidate->native, f_.diagnostic)
            && f_.diagnostic == "RHI-COMMAND-RETENTION-OWNER-CAPACITY"
            && !candidate->command->stats()->nativeDispatchEncoded
            && candidate->native->states()->empty() && !candidate->native->validate());
        candidate.reset();
        ++ownerCapacity;
        candidate = prepare();
        assert(candidate);
        record(*candidate);
        candidate->command.reset();
        terminal(*candidate, Status::Abandoned);
        candidate.reset();
        candidate = prepare();
        assert(candidate);
        record(*candidate, false);
        auto& viewport = const_cast<Native::Contract::Input&>(candidate->native->input()).viewport;
        viewport.width = 8.0f;
        assert(!candidate->command->finish(f_.diagnostic));
        viewport.width = 16.0f;
        terminal(*candidate, Status::Failed);
        candidate.reset();
        candidate = prepare();
        assert(candidate);
        record(*candidate);
        MetalRhiNativeQueueStats queue;
        assert(!candidate->command->submit(queue, {}, {}, 0u, {}, 0u, f_.diagnostic)
            && f_.diagnostic == "RHI-METAL-COMPLETION-PAYLOAD-MISSING"
            && !queue.nativeQueueSubmitCount);
        terminal(*candidate, Status::Failed);
        candidate.reset();
        if (execute) {
            using Issuance = Preparation::Issuance;
            BindlessIndirectPacketPool pool(*f_.device,
                {1u,
                    Issuance::Packet::Contract::minimumBytes,
                    Issuance::Packet::Contract::minimumBytes});
            const auto fresh = [&]() {
                selectedIssuance = Issuance::prepare(issuance->images(),
                    issuance->cold()->producer()->packet()->pipeline(),
                    pool,
                    {1u,
                        true,
                        Issuance::minimumNewBytes,
                        512u,
                        {1u, true, Issuance::Packet::Contract::minimumBytes, 128u},
                        {1u, true, Issuance::Producer::Contract::minimumOwnedBytes, 128u},
                        {1u, true, Issuance::Cold::Contract::minimumOwnedBytes, 256u}},
                    f_.diagnostic);
                assert(selectedIssuance && !selectedIssuance->validate());
            };
            candidate = prepare();
            assert(candidate);
            record(*candidate);
            auto gate = createMetalRhiNativeSharedEventPayload("ClosedConsumerRejectedWait");
            assert(gate && gate->nativeRuntimeBacked);
            auto done = candidate->command->preparedCompletionState();
            {
                core::CpuMemoryQuotaLease pressure(f_.quota, f_.cpuBytes - f_.quota->liveBytes());
                assert(pressure.charged());
                auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
                assert(!candidate->command->submit(queue, done, gate, 1u, {}, 0u, f_.diagnostic));
                std::pmr::set_default_resource(previous);
            }
            assert(f_.diagnostic == "RHI-COMMAND-RETENTION-PREPARATION-ALLOCATION-FAILED"
                && done->outcome() == MetalRhiCompletionState::Outcome::Failed);
            terminal(*candidate, Status::Failed);
            candidate.reset();
            done.reset();
            gate.reset();
            candidate = prepare();
            assert(candidate);
            record(*candidate);
            auto& invalid =
                const_cast<Native::Contract::Input&>(candidate->native->input()).viewport;
            invalid.width = 8.0f;
            done = candidate->command->preparedCompletionState();
            assert(!candidate->command->submit(queue, done, {}, 0u, {}, 0u, f_.diagnostic)
                && !queue.nativeQueueSubmitCount);
            invalid.width = 16.0f;
            terminal(*candidate, Status::Failed);
            candidate.reset();
            done.reset();
            const auto& content =
                issuance->cold()->producer()->packet()->pipeline()->source()->content();
            MetalRhiZeroBaseIndirectSource::Content::Facts oracle;
            assert(!content.observe(oracle));
            const auto expectedArgs = IndexedIndirectCommandSchema::words(oracle.physical);
            const std::array<std::uint32_t, 4u> expectedMapping{oracle.mapping.firstInstance,
                oracle.mapping.instanceCount,
                oracle.mapping.instanceCapacity,
                oracle.mapping.contractVersion};
            const std::uint32_t expectedFault =
                oracle.outcome == MetalRhiZeroBaseIndirectSource::Content::Outcome::FaultSuppressed
                ? 1u
                : 0u;
            const auto read = [&](const auto& native, const auto& expected) {
                std::array<std::uint8_t, 20u> bytes{};
                const auto count = expected.size() * sizeof(std::uint32_t);
                assert(readMetalRhiNativeBuffer(
                    native, bytes.data(), count, 0u, count, f_.diagnostic));
                core::UnsignedElementSpan words(bytes.data(),
                    count,
                    core::UnsignedElementSpan::Width::Uint32,
                    core::UnsignedElementSpan::ByteOrder::LittleEndian);
                for (std::size_t i = 0u; i < expected.size(); ++i) {
                    std::uint32_t word = 0u;
                    assert(words.read(i, word));
                    if (word != expected[i])
                        std::fprintf(stderr,
                            "Closed GPU word%zu expected%u actual%u\n",
                            i,
                            expected[i],
                            word);
                    assert(word == expected[i]);
                }
            };
            for (unsigned variation = 0u; variation < 5u; ++variation) {
                if (variation)
                    fresh();
                candidate = prepare();
                assert(candidate);
                record(*candidate);
                const auto cold = selectedIssuance->cold();
                const auto args = candidate->preparation->resources()[0u].buffer,
                           mapping = candidate->preparation->resources()[1u].buffer;
                const auto target = candidate->native->target();
                done = candidate->command->preparedCompletionState();
                const bool delayed = variation < 4u, dropRoot = variation & 1u,
                           dropCommand = variation & 2u;
                gate = delayed
                    ? createMetalRhiNativeSharedEventPayload("ClosedConsumerActualDelayed")
                    : nullptr;
                if (delayed)
                    assert(gate && gate->nativeRuntimeBacked);
                const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
                if (delayed)
                    assert(
                        candidate->command->submit(queue, done, gate, 1u, {}, 0u, f_.diagnostic));
                else {
                    core::CpuMemoryQuotaLease pressure(
                        f_.quota, f_.cpuBytes - f_.quota->liveBytes());
                    assert(pressure.charged());
                    auto* previous =
                        std::pmr::set_default_resource(std::pmr::null_memory_resource());
                    assert(candidate->command->submit(queue, done, {}, 0u, {}, 0u, f_.diagnostic));
                    std::pmr::set_default_resource(previous);
                }
                if (!delayed)
                    assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
                std::weak_ptr<const Native> weak = candidate->native;
                if (delayed) {
                    assert(candidate->reservation->status() == Status::Submitted
                        && !done->isComplete());
                    assert(!BindlessFaultSubmissionLease::acquire(
                               cold->producer()->faultDomain(), f_.diagnostic)
                               .valid()
                        && !BindlessFaultSubmissionLease::acquire(
                            cold->faultDomain(), f_.diagnostic)
                            .valid());
                    done->complete(true);
                    assert(candidate->reservation->status() == Status::Submitted);
                    assert(!BindlessFaultSubmissionLease::acquire(
                               cold->producer()->faultDomain(), f_.diagnostic)
                               .valid()
                        && !BindlessFaultSubmissionLease::acquire(
                            cold->faultDomain(), f_.diagnostic)
                            .valid());
                    done->reset();
                    read(args, std::array<std::uint32_t, 5u>{});
                    read(mapping, std::array<std::uint32_t, 4u>{});
                }
                if (dropRoot) {
                    candidate->native.reset();
                    candidate->reservation.reset();
                    candidate->preparation.reset();
                }
                if (dropCommand)
                    candidate->command.reset();
                if (delayed) {
                    assert(!weak.expired() && !done->isComplete());
                    signalMetalRhiNativeSharedEvent(gate, 1u);
                }
                done->wait();
                assert(done->outcome() == MetalRhiCompletionState::Outcome::Completed);
                if (dropRoot)
                    assert(weak.expired());
                else
                    assert(candidate->reservation->status() == Status::Completed
                        && candidate->native->states()->empty());
                read(args, expectedArgs);
                read(mapping, expectedMapping);
                read(cold->producer()->resources()[3u].buffer, expectedArgs);
                read(cold->producer()->resources()[4u].buffer, expectedMapping);
                read(cold->producer()->packet()->resources()[0u].buffer, expectedArgs);
                read(cold->producer()->packet()->resources()[1u].buffer, expectedMapping);
                read(cold->producer()->readback().buffer,
                    std::array<std::uint32_t, 1u>{expectedFault});
                read(cold->readback().buffer, std::array<std::uint32_t, 1u>{0u});
                assert(cold->producer()->faultDomain()->faultLatchPriorState()
                        == ResourceState::CopySrc
                    && cold->faultDomain()->faultLatchPriorState() == ResourceState::CopySrc);
                assert(BindlessFaultSubmissionLease::acquire(
                           cold->producer()->faultDomain(), f_.diagnostic)
                           .valid()
                    && BindlessFaultSubmissionLease::acquire(cold->faultDomain(), f_.diagnostic)
                        .valid());
                verifyTarget(*target,
                    expectedFault != 0u,
                    *issuance->images(),
                    oracle.mapping.firstInstance + oracle.mapping.instanceCount - 1u);
                assert(!prepare()
                    && f_.diagnostic == "RHI-METAL-CANONICAL-PRODUCER-GENERATION-CONSUMED");
                if (candidate->command)
                    assert(!candidate->command->submit(queue, done, {}, 0u, {}, 0u, f_.diagnostic)
                        && f_.diagnostic == "RHI-METAL-COMMAND-BUFFER-ALREADY-SUBMITTED");
                candidate.reset();
                done.reset();
                gate.reset();
                selectedIssuance.reset();
            }
            selectedIssuance = issuance;
            assert(!pool.livePackets() && !pool.chargedBytes());
            std::printf(
                "ClosedConsumerNativeCompletion/1: actual native38 source-derived args/mapping/scratch/protected packet and producer fault%u/consumer fault0; four delayed drop-root/drop-command combinations, CPU spoof retains both fault leases, native success retirement, consumed-generation refusal, no-wait exhausted submit allocation0, precommit wait allocation/corrupt input rollback; separate target read-only oracle after completion PASS; private Prototype, RenderDoc visual acceptance pending\n",
                expectedFault);
        }
        assert(cpu == f_.quota->liveBytes() && gpu == f_.device->memoryStats().allocatedBytes
            && !inspectSharedMetalRhiCommandCapacity().charged);
        std::printf(
            "ClosedConsumerNativeRecording/1: single original native38 with prefix10, packet transfer, actual indexed-indirect encoding, consumer clear/descriptor/vertex/index/viewport/scissor/fault/four readbacks; exact native unique owners%u CPUpeak%llu, exhausted recording/finish allocation0, aliases/bindings/foreign journal/one-less owners preflight, recorded abandon/destruction/finish corruption/closed submit rollback PASS; recording subchecks only, completion evidence reported separately, visual acceptance pending\n",
            ownerCapacity,
            static_cast<unsigned long long>(peak));
    }

private:
    void verifyTarget(rhi::GpuTexture& target,
        bool suppressed,
        const Preparation::Issuance::Images& images,
        std::uint32_t lastInstance)
    {
        using namespace rhi;
        using namespace rhi_metal;
        // This ordinary read-only copy occurs only after native38 completion.
        const auto extent = target.desc().extent;
        const auto pitch = kTextureUploadRowPitchAlignment, bytes = pitch * extent.height;
        auto output = f_.device->createBuffer({"ClosedTargetOracle",
            bytes,
            ResourceUsage::CopyDst,
            MemoryUsage::GpuToCpu,
            ResourceState::CopyDst});
        assert(output);
        output->sealHostWrites();
        auto encoder = f_.device->createCommandEncoder({});
        assert(encoder);
        ResourceBarrierDesc dependency;
        dependency.resource = target.id();
        dependency.before = ResourceState::ColorAttachment;
        dependency.after = ResourceState::CopySrc;
        dependency.sourceStages = PipelineStage::ColorAttachmentOutput;
        dependency.sourceAccess = ResourceAccess::ColorAttachmentWrite;
        dependency.destinationStages = PipelineStage::Transfer;
        dependency.destinationAccess = ResourceAccess::TransferRead;
        const auto transition = encoder->transition(target, dependency);
        if (!transition.success)
            std::fprintf(stderr,
                "Closed target transition: %s state%u\n",
                transition.diagnostics.c_str(),
                static_cast<unsigned>(target.state()));
        assert(transition.success);
        auto transfer = encoder->beginTransferPass({});
        assert(transfer);
        TextureToBufferCopyDesc copy;
        copy.sourceTexture = target.id();
        copy.destinationBuffer = output->id();
        copy.bytesPerRow = pitch;
        copy.rowsPerImage = extent.height;
        copy.extent = extent;
        assert(transfer->copyTextureToBuffer(copy).success && transfer->end().success);
        dependency.before = ResourceState::CopySrc;
        dependency.after = ResourceState::ColorAttachment;
        dependency.sourceStages = PipelineStage::Transfer;
        dependency.sourceAccess = ResourceAccess::TransferRead;
        dependency.destinationStages = PipelineStage::ColorAttachmentOutput;
        dependency.destinationAccess = ResourceAccess::ColorAttachmentWrite;
        assert(encoder->transition(target, dependency).success);
        auto command = encoder->finish();
        assert(command && command->commandCount() == 5u);
        assert(f_.device->queue().submit(*command).success
            && waitMetalRhiNativeQueueIdle(f_.diagnostic));
        core::ByteBuffer actual(bytes, std::uint8_t{0xcdu}, &core::defaultCpuMemoryResource());
        assert(readMetalRhiNativeBuffer(
            metalRhiNativePayloadFor(*output), actual.data(), bytes, 0u, bytes, f_.diagnostic));
        // Frozen fixture triangle: indexed vertices (1,-1),(0,1),(.5,.5).
        // Pixel(11,3) lies strictly inside; Images_load uses texel(0,0) of last instance.
        const std::array<std::uint8_t, 4u> clear{64u, 128u, 191u, 255u};
        const auto* expected =
            suppressed ? clear.data() : images.contents()[lastInstance]->bytes().data();
        for (std::size_t c = 0u; c < 4u; ++c) {
            const auto value = actual[3u * pitch + 11u * 4u + c];
            if (value != expected[c])
                std::fprintf(stderr,
                    "Closed target channel%zu expected%u actual%u suppressed%d\n",
                    c,
                    expected[c],
                    value,
                    suppressed);
            assert(value == expected[c]);
        }
    }

    OrdinaryGraphicsFixture& f_;
};
} // namespace arp::test

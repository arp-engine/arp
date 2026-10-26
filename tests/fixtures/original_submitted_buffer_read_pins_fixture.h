#pragma once
#include "rhi/backends/null/null_backend.h"
#include "rhi/backends/null/null_backend_internal.h"
#include "rhi/frontend/command_stream_program.h"
#include "rhi/frontend/original_submitted_buffer_read_pins.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>

namespace arp::test {
class OriginalSubmittedBufferReadPinsFixture final {
    using Completion = rhi::OriginalSubmissionCompletionDomain;
    using Pins = rhi::OriginalSubmittedBufferReadPins;
    using Budget = rhi::semantics::OriginalSubmissionReadBudget;
    using Usage = rhi::semantics::OriginalSubmissionReadUsage;
    using Outcome = Completion::Outcome;
    using State = rhi::ResourceState;
    static constexpr std::uint64_t bytes = 1024u * 1024u;

    static std::shared_ptr<core::CpuMemoryQuota> quota()
    {
        return core::makeShared<core::CpuMemoryQuota>(bytes);
    }

    static void expect(const char* actual, const char* expected)
    {
        assert(actual && !std::strcmp(actual, expected));
    }

    static auto& state(const rhi::GpuBuffer& buffer)
    {
        auto* result = dynamic_cast<rhi::null_backend::StatefulResource*>(
            const_cast<rhi::GpuBuffer*>(&buffer));
        assert(result);
        return *result;
    }

    struct Participant {
        std::shared_ptr<const rhi::OriginalCommandBufferLease> source;
        core::MutationJournal journal;
        unsigned resolves = 0u, cancels = 0u;

        explicit Participant(std::shared_ptr<const rhi::OriginalCommandBufferLease> lease)
            : source(std::move(lease))
        {
            assert(state(*source->originalObject())
                    .transitionState(State::ShaderWrite, State::ShaderRead, &journal)
                    .success);
        }

        static bool resolve(void* context, bool success) noexcept
        {
            auto& self = *static_cast<Participant*>(context);
            assert(!self.resolves++ && !self.cancels && !self.journal.empty());
            if (success)
                self.journal.commit();
            else
                assert(self.journal.invalidate());
            return true;
        }

        static void cancel(void* context) noexcept
        {
            auto& self = *static_cast<Participant*>(context);
            assert(!self.cancels++ && !self.resolves && !self.journal.empty());
            self.journal.rollback();
        }
    };

    struct Fixture {
        core::UniquePtr<rhi::GpuDevice> device = rhi::createNullDevice();
        std::shared_ptr<core::CpuMemoryQuota> completionQuota = quota(), readQuota = quota();
        std::shared_ptr<Completion> completion;
        Budget budget{1u, true, 3u, 6u, 3u, bytes};
        std::shared_ptr<Pins> pins;

        explicit Fixture(Budget requested = {1u, true, 3u, 6u, 3u, bytes}, bool installPins = true)
            : budget(requested)
        {
            assert(!device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, quota()));
            assert(!device->prepareOriginalResourceRegistration(
                {1u, true, 12u, 0u, 0u, 27u, bytes}, quota()));
            const char* diagnostic = nullptr;
            completion = Completion::prepareValidator(
                *device, {1u, true, 8u, 12u, bytes}, completionQuota, diagnostic);
            assert(completion && !diagnostic);
            readQuota = core::makeShared<core::CpuMemoryQuota>(budget.maxCpuMetadataBytes);
            if (installPins) {
                pins = Pins::prepareValidator(*device, completion, budget, readQuota, diagnostic);
                assert(pins && !diagnostic);
            }
        }

        std::shared_ptr<Participant> participant()
        {
            rhi::BufferDesc desc;
            desc.sizeBytes = 64u;
            desc.usage = rhi::ResourceUsage::StorageBuffer;
            desc.initialState = State::ShaderWrite;
            auto object = device->createBuffer(desc);
            assert(object);
            object->sealHostWrites();
            const char* diagnostic = nullptr;
            auto lease = rhi::OriginalCommandBufferLease::prepareOriginal(
                object, {1u, true, 2u, bytes}, quota(), diagnostic);
            assert(lease && !diagnostic && !object);
            return core::makeShared<Participant>(std::move(lease));
        }

        Completion::Ticket prepare(const std::shared_ptr<Participant>& owner,
            const Completion::Ticket* predecessors = nullptr,
            std::size_t count = 0u)
        {
            const char* diagnostic = nullptr;
            auto ticket = completion->prepareValidatorCommand(
                owner, Participant::resolve, Participant::cancel, predecessors, count, diagnostic);
            assert(!diagnostic);
            return ticket;
        }

        Pins::Borrow borrow(
            const std::shared_ptr<Participant>& writer, const Completion::Ticket& ticket) const
        {
            return {writer->source,
                ticket,
                writer,
                &writer->journal,
                {State::ShaderRead,
                    rhi::PipelineStage::ComputeShader,
                    rhi::ResourceAccess::ShaderRead},
                0u,
                64u,
                nullptr};
        }

        void usage(std::uint64_t commands, std::uint64_t pinsCount, std::uint64_t links) const
        {
            Usage actual;
            assert(!pins->usageValidator(actual));
            assert(actual.pendingCommands == commands && actual.bufferReadPins == pinsCount
                && actual.predecessorLinks == links
                && actual.cpuMetadataBytes == readQuota->liveBytes());
        }
    };

    // The custody-only recipe uses Finish. Canonical integration optionally
    // transitions an independent output; neither recipe invents shader access.
    static std::shared_ptr<const rhi::OriginalCommandInputs> frozenInputs(
        const std::shared_ptr<const rhi::OriginalCommandBufferLease>& source,
        const std::shared_ptr<const rhi::OriginalCommandBufferLease>& output = {},
        bool borrowedBarrier = false)
    {
        using Sources = rhi::OriginalCommandResourceSources;
        using Inputs = rhi::OriginalCommandInputs;
        using K = rhi::CommandUsageKind;
        using Scalar = rhi::CommandScalarArguments;
        using Store = rhi::CommandVariableArguments;
        rhi::OriginalCommandAdmissionBudget budget;
        budget.contractVersion = 1u;
        budget.prototypeOptIn = true;
        budget.maxCommands = output ? 2u : 1u;
        budget.maxReferences = 1u;
        budget.maxGraphOwners = output ? 10u : 8u;
        budget.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Buffer)] =
            output ? 2u : 1u;
        budget.maxInputDescriptors = output ? 1u : 0u;
        budget.maxStateObservations = output ? 2u : 1u;
        budget.maxStateTransactions = output ? 2u : 0u;
        budget.maxCpuBytes[static_cast<std::size_t>(
            rhi::OriginalCommandAdmissionBudget::CpuDomain::Recording)] = bytes;
        budget.maxCpuBytes[0] = budget.maxCpuBytes[1] = bytes;
        const std::array<Sources::Source, 2> originals{source, output};
        const char* diagnostic = nullptr;
        auto sources = Sources::prepareOriginal(originals.data(),
            output ? 2u : 1u,
            budget,
            source->originalMetadataQuota(),
            diagnostic);
        assert(sources && !diagnostic);
        const std::array<rhi::CommandUsage, 2> commands{
            {{output ? K::BufferBarrier : K::Finish, 0u, output ? 1u : 0u}, {K::Finish, 1u, 0u}}};
        const rhi::OriginalCommandUsage::Reference reference{borrowedBarrier ? 0u : 1u};
        auto graphQuota = quota();
        auto usage = rhi::OriginalCommandUsage::prepareOriginal(sources,
            commands.data(),
            output ? 2u : 1u,
            output ? &reference : nullptr,
            output ? 1u : 0u,
            graphQuota,
            diagnostic);
        assert(usage && !diagnostic);
        const std::array<Store::Input, 2> fields{
            {{*Scalar::capture(commands[0].kind, Scalar::None{})},
                {*Scalar::capture(K::Finish, Scalar::None{})}}};
        auto store = Store::prepareOriginal(fields.data(),
            output ? 2u : 1u,
            {1u, true, output ? 2u : 1u, 0u, 0u, 0u, bytes},
            graphQuota,
            diagnostic);
        assert(store && !diagnostic);
        auto scalars =
            rhi::OriginalCommandScalars::prepareOriginal(usage, store, graphQuota, diagnostic);
        assert(scalars && !diagnostic);
        const rhi::ResourceBarrierDesc barrier{borrowedBarrier ? source->resource()
                : output                                       ? output->resource()
                                                               : rhi::GpuResourceId{},
            {},
            borrowedBarrier ? State::ShaderRead : State::ShaderWrite,
            borrowedBarrier ? State::ShaderWrite : State::ShaderRead,
            rhi::PipelineStage::ComputeShader,
            rhi::PipelineStage::ComputeShader,
            borrowedBarrier ? rhi::ResourceAccess::ShaderRead : rhi::ResourceAccess::ShaderWrite,
            borrowedBarrier ? rhi::ResourceAccess::ShaderWrite : rhi::ResourceAccess::ShaderRead};
        std::optional<Inputs::OriginalDescriptor> descriptor;
        if (output)
            descriptor.emplace(Inputs::OriginalDescriptor{
                0u, *rhi::ResourceDependencyArguments::capture(K::BufferBarrier, barrier)});
        auto inputs = Inputs::prepareOriginal(
            scalars, descriptor ? &*descriptor : nullptr, output ? 1u : 0u, graphQuota, diagnostic);
        assert(inputs && !diagnostic);
        return inputs;
    }

    struct CanonicalReader {
        std::shared_ptr<const rhi::OriginalCommandStreamProgram> program;
        std::shared_ptr<rhi::OriginalCommandStateTransaction> transaction;
        Completion* completion = nullptr;
        Pins* pins = nullptr;
        std::optional<Pins::Observation> read;
        unsigned resolves = 0u, cancels = 0u;

        void verifyResolverBoundary(bool valid) const noexcept
        {
            expect(completion->pollValidator(), "RHI-SUBMISSION-COMPLETION-RESOLVING");
            const char* diagnostic = nullptr;
            assert(!pins->reserveValidator({}, nullptr, nullptr, 0u, diagnostic));
            expect(diagnostic, "RHI-SUBMISSION-COMPLETION-RESOLVING");
            const auto* id = read->diagnostic();
            assert(!id || std::strcmp(id, "RHI-SUBMISSION-READ-PIN-EXPIRED"));
            if (valid) {
                if (const auto* failure = transaction->validate())
                    std::fprintf(stderr, "canonical resolver: %s\n", failure);
                assert(!transaction->validate());
            }
        }

        static bool resolve(void* context, bool success) noexcept
        {
            auto& self = *static_cast<CanonicalReader*>(context);
            assert(!self.resolves++ && !self.cancels);
            self.verifyResolverBoundary(success);
            // The completion record has not yet released this reader's batch.
            assert(!self.transaction->journalEmpty());
            return !(success ? self.transaction->commitOriginalPendingOwnership()
                             : self.transaction->abandonOriginalPendingOwnership());
        }

        static void cancel(void* context) noexcept
        {
            auto& self = *static_cast<CanonicalReader*>(context);
            assert(!self.cancels++ && !self.resolves);
            self.verifyResolverBoundary(false);
            assert(!self.transaction->abandonOriginalPendingOwnership());
        }
    };

    static std::shared_ptr<const rhi::OriginalCommandBufferLease> canonicalOutput(
        Fixture& f, const std::shared_ptr<const rhi::OriginalCommandBufferLease>& input)
    {
        auto desc = input->originalObject()->desc();
        desc.initialState = State::ShaderWrite;
        auto object = f.device->createBuffer(desc);
        assert(object);
        object->sealHostWrites();
        const char* diagnostic = nullptr;
        auto output = rhi::OriginalCommandBufferLease::prepareOriginal(
            object, {1u, true, 2u, bytes}, input->originalMetadataQuota(), diagnostic);
        assert(output && !diagnostic);
        return output;
    }

    static void canonicalJoin(bool planned, bool success, bool cancel, bool writerFirst)
    {
        using Graph = rhi::OriginalCommandResourceStates;
        using Program = rhi::OriginalCommandStreamProgram;
        using Transaction = rhi::OriginalCommandStateTransaction;
        Fixture f;
        auto writer = f.participant();
        auto a = f.prepare(writer);
        assert(!f.completion->acceptValidator(a));
        auto borrow = f.borrow(writer, a);
        auto output = canonicalOutput(f, writer->source);
        const auto inputSource = writer->source;
        auto inputs = frozenInputs(inputSource, output);
        const char* diagnostic = nullptr;
        const std::size_t index = 0u;
        auto reservation = f.pins->reserveValidator(inputs, &index, &borrow, 1u, diagnostic);
        assert(reservation && !diagnostic);
        auto observed = f.pins->observeValidator(*reservation, index, diagnostic);
        assert(observed && !diagnostic && observed->state() == State::ShaderRead);
        assert(!observed->validateReadScope(borrow.scope, 0u, 64u));
        expect(
            observed->validateReadScope(borrow.scope, 0u, 32u), "RHI-SUBMISSION-READ-WHOLE-BUFFER");
        auto scope = borrow.scope;
        scope.stages = rhi::PipelineStage::FragmentShader;
        expect(observed->validateReadScope(scope, 0u, 64u), "RHI-SUBMISSION-READ-STAGES");
        scope = {State::ShaderWrite,
            rhi::PipelineStage::ComputeShader,
            rhi::ResourceAccess::ShaderWrite};
        expect(observed->validateReadScope(scope, 0u, 64u), "RHI-SUBMISSION-READ-SCOPE");
        rhi::OriginalCommandStateAdmissionBudget states{1u, true, 1u, 2u, 2u, 0u, 32u, bytes};
        Graph::SubmittedReadAdmission admission{1u, true, {1u, true, 2u, 0u, 2u, 1u}, {}};
        auto stateQuota = quota();
        assert(!Graph::prepareOriginal(inputs, *f.device, states, stateQuota, diagnostic));
        assert(diagnostic && !stateQuota->liveBytes() && !writer->journal.empty());
        for (const auto& sharedQuota : {f.readQuota, f.completionQuota}) {
            assert(!Graph::prepareOriginalSubmittedReadValidator(
                inputs, *f.device, states, sharedQuota, *reservation, admission, diagnostic));
            expect(diagnostic, "RHI-SUBMISSION-READ-QUOTA");
        }
        const auto prepare = [&](const auto& limits, const auto& reads) {
            return Graph::prepareOriginalSubmittedReadValidator(
                inputs, *f.device, limits, stateQuota, *reservation, reads, diagnostic);
        };
        auto equalInputs = frozenInputs(inputSource, output);
        assert(!Graph::prepareOriginalSubmittedReadValidator(
            equalInputs, *f.device, states, stateQuota, *reservation, admission, diagnostic));
        expect(diagnostic, "RHI-SUBMISSION-READ-INPUTS");
        auto foreignDevice = rhi::createNullDevice();
        assert(!Graph::prepareOriginalSubmittedReadValidator(
            inputs, *foreignDevice, states, stateQuota, *reservation, admission, diagnostic));
        assert(diagnostic && !stateQuota->liveBytes());
        std::thread foreign([&] {
            assert(!prepare(states, admission));
            expect(diagnostic, "RHI-SUBMISSION-READ-THREAD");
        });
        foreign.join();
        {
            core::CpuMemoryQuotaLease pressure(stateQuota, bytes);
            assert(pressure.charged() && !prepare(states, admission));
            expect(diagnostic, "RHI-ORIGINAL-RESOURCE-STATES-ALLOCATION");
        }
        assert(!stateQuota->liveBytes() && !writer->journal.empty() && !observed->diagnostic());
        auto invalid = admission;
        invalid.prototypeOptIn = false;
        assert(!prepare(states, invalid));
        expect(diagnostic, "RHI-SUBMISSION-READ-OPT-IN");
        invalid = admission;
        invalid.sequence.maxCommands = 1u;
        assert(!prepare(states, invalid));
        expect(diagnostic, "RHI-ORIGINAL-SEQUENCE-COMMAND-CAPACITY");
        invalid = admission;
        invalid.sequence.maxStateCells = 1u;
        assert(!prepare(states, invalid));
        expect(diagnostic, "RHI-ORIGINAL-SEQUENCE-CELL-CAPACITY");
        auto scarce = states;
        scarce.maxStateObservations = 1u;
        assert(!prepare(scarce, admission));
        expect(diagnostic, "RHI-ORIGINAL-RESOURCE-STATES-OBSERVATION-CAPACITY");
        auto graph = prepare(states, admission);
        if (!graph)
            std::fprintf(stderr, "canonical graph: %s\n", diagnostic);
        assert(graph && !diagnostic && graph->bufferEntries().size() == 2u);
        assert(graph->bufferEntries()[0].observation.submitted()
            && !graph->bufferEntries()[0].observation.committed()
            && graph->bufferEntries()[1].observation.committed()
            && !graph->bufferEntries()[1].observation.submitted());
        scarce.maxStateObservations = 2u;
        scarce.maxRetainedOwners = graph->retainedOwnerCount() - 1u;
        assert(!prepare(scarce, admission));
        expect(diagnostic, "RHI-ORIGINAL-STATE-OWNER-CAPACITY");
        assert(!graph->compareOriginalInputs(inputs, *f.device));
        for (auto backend : {rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU})
            expect(
                graph->executionDiagnostic(backend), "RHI-SUBMISSION-READ-CANONICAL-UNAVAILABLE");
        rhi::OriginalCommandStreamBudget stream;
        stream.contractVersion = 1u;
        stream.prototypeOptIn = true;
        stream.maxCommands = 2u;
        stream.maxDescriptors = stream.maxReferences = 1u;
        stream.maxRetainedOwners = graph->retainedOwnerCount() + 1u;
        stream.maxCpuMetadataBytes = bytes;
        auto reader = core::makeShared<CanonicalReader>();
        reader->completion = f.completion.get();
        reader->pins = f.pins.get();
        reader->read = observed;
        for (const auto& sharedQuota : {f.readQuota, f.completionQuota}) {
            assert(!Program::prepareOriginalTyped(graph, stream, sharedQuota, diagnostic));
            expect(diagnostic, "RHI-SUBMISSION-READ-QUOTA");
            assert(!Transaction::prepareOriginalOwnership(
                graph, *f.device, states, sharedQuota, diagnostic));
            expect(diagnostic, "RHI-SUBMISSION-READ-QUOTA");
        }
        reader->program = Program::prepareOriginalTyped(graph, stream, quota(), diagnostic);
        if (!reader->program)
            std::fprintf(stderr, "canonical program: %s\n", diagnostic);
        assert(reader->program && !diagnostic && !reader->program->compareOriginalStates(graph)
            && !reader->program->compareOriginalInputs(inputs));
        reader->transaction =
            Transaction::prepareOriginalOwnership(graph, *f.device, states, quota(), diagnostic);
        assert(reader->transaction && !diagnostic);
        if (planned) {
            assert(!reader->transaction->materializeOriginalPrototypePlan(admission.sequence));
            assert(!reader->transaction->applyOriginalPrototypePlan());
        } else
            assert(!reader->transaction->beginOriginalPendingOwnership());
        assert(!reader->transaction->validate() && !writer->journal.empty());
        assert(!inputSource->originalObject()->observeOriginalCommittedState()
            && !output->originalObject()->observeOriginalCommittedState());
        auto b = f.completion->prepareValidatorCommand(
            reader, CanonicalReader::resolve, CanonicalReader::cancel, &a, 1u, diagnostic);
        assert(!diagnostic && !f.pins->attachReservationValidator(b, *reservation, inputs));
        reservation.reset();
        const std::weak_ptr<Participant> weakWriter = writer;
        borrow.writerRetention.reset();
        writer.reset();
        if (writerFirst) {
            assert(!Completion::deliverValidatorResult(a, true) && !f.completion->pollValidator());
            assert(weakWriter.expired() && !reader->transaction->validate());
        }
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        if (cancel) {
            assert(!f.completion->cancelValidator(b));
            assert(reader->cancels == 1u && !reader->resolves);
        } else {
            assert(!f.completion->acceptValidator(b));
            assert(
                !Completion::deliverValidatorResult(b, success) && !f.completion->pollValidator());
            if (!writerFirst && success)
                assert(!reader->resolves);
        }
        if (!writerFirst)
            assert(!Completion::deliverValidatorResult(a, true) && !f.completion->pollValidator());
        assert(weakWriter.expired()
            && allocations == core::defaultCpuAllocator().stats().allocationCount);
        assert(reader->transaction->journalEmpty());
        const auto initial = inputSource->originalObject()->observeOriginalCommittedState();
        const auto final = output->originalObject()->observeOriginalCommittedState();
        assert(initial && initial->state() == State::ShaderRead && final);
        assert(final->state()
            == (planned && success && !cancel ? State::ShaderRead : State::ShaderWrite));
        assert(b.outcome()
            == (cancel        ? Outcome::Cancelled
                    : success ? Outcome::Succeeded
                              : Outcome::Failed));
        expect(observed->diagnostic(), "RHI-SUBMISSION-READ-PIN-EXPIRED");
        f.usage(0u, 0u, 0u);
    }

    static void canonicalMultiReaders(bool writerSuccess)
    {
        using Graph = rhi::OriginalCommandResourceStates;
        using Program = rhi::OriginalCommandStreamProgram;
        using Transaction = rhi::OriginalCommandStateTransaction;
        Fixture f;
        auto writer = f.participant();
        const auto inputSource = writer->source;
        auto a = f.prepare(writer);
        assert(!f.completion->acceptValidator(a));
        auto borrow = f.borrow(writer, a);
        const std::size_t index = 0u;
        const char* diagnostic = nullptr;
        const rhi::OriginalCommandStateAdmissionBudget states{1u, true, 1u, 2u, 2u, 0u, 32u, bytes};
        const Graph::SubmittedReadAdmission admission{1u, true, {1u, true, 2u, 0u, 2u, 1u}, {}};
        std::array<std::shared_ptr<CanonicalReader>, 2> readers;
        std::array<Completion::Ticket, 2> tickets;
        std::array<std::shared_ptr<const rhi::OriginalCommandBufferLease>, 2> outputs;
        for (std::size_t i = 0u; i < readers.size(); ++i) {
            outputs[i] = canonicalOutput(f, inputSource);
            auto inputs = frozenInputs(inputSource, outputs[i]);
            auto reservation = f.pins->reserveValidator(inputs, &index, &borrow, 1u, diagnostic);
            assert(reservation && !diagnostic);
            auto graph = Graph::prepareOriginalSubmittedReadValidator(
                inputs, *f.device, states, quota(), *reservation, admission, diagnostic);
            assert(graph && !diagnostic);
            auto& reader = readers[i];
            reader = core::makeShared<CanonicalReader>();
            reader->completion = f.completion.get();
            reader->pins = f.pins.get();
            reader->read = f.pins->observeValidator(*reservation, index, diagnostic);
            rhi::OriginalCommandStreamBudget stream;
            stream.contractVersion = 1u;
            stream.prototypeOptIn = true;
            stream.maxCommands = 2u;
            stream.maxDescriptors = stream.maxReferences = 1u;
            stream.maxRetainedOwners = graph->retainedOwnerCount() + 1u;
            stream.maxCpuMetadataBytes = bytes;
            reader->program = Program::prepareOriginalTyped(graph, stream, quota(), diagnostic);
            assert(reader->program && !diagnostic);
            reader->transaction = Transaction::prepareOriginalOwnership(
                graph, *f.device, states, quota(), diagnostic);
            assert(reader->transaction && !diagnostic);
            assert(!reader->transaction->materializeOriginalPrototypePlan(admission.sequence)
                && !reader->transaction->applyOriginalPrototypePlan());
            tickets[i] = f.completion->prepareValidatorCommand(
                reader, CanonicalReader::resolve, CanonicalReader::cancel, &a, 1u, diagnostic);
            assert(!diagnostic
                && !f.pins->attachReservationValidator(tickets[i], *reservation, inputs));
            assert(!f.completion->acceptValidator(tickets[i]));
        }
        f.usage(2u, 2u, 2u);
        const std::weak_ptr<Participant> weakWriter = writer;
        borrow.writerRetention.reset();
        writer.reset();
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(!Completion::deliverValidatorResult(tickets[1], false)
            && !f.completion->pollValidator());
        assert(tickets[1].outcome() == Outcome::Failed && readers[1]->resolves == 1u);
        f.usage(1u, 1u, 1u);
        assert(!readers[0]->read->diagnostic() && !weakWriter.expired());
        assert(!Completion::deliverValidatorResult(tickets[0], true)
            && !f.completion->pollValidator());
        assert(!readers[0]->resolves);
        assert(!Completion::deliverValidatorResult(a, writerSuccess)
            && !f.completion->pollValidator());
        assert(weakWriter.expired() && readers[0]->resolves == 1u
            && tickets[0].outcome() == (writerSuccess ? Outcome::Succeeded : Outcome::Failed));
        assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
        for (std::size_t i = 0u; i < readers.size(); ++i) {
            assert(readers[i]->transaction->journalEmpty());
            expect(readers[i]->read->diagnostic(), "RHI-SUBMISSION-READ-PIN-EXPIRED");
            const auto output = outputs[i]->originalObject()->observeOriginalCommittedState();
            assert(output
                && output->state()
                    == (i == 0u && writerSuccess ? State::ShaderRead : State::ShaderWrite));
        }
        const auto original = inputSource->originalObject()->observeOriginalCommittedState();
        assert(bool(original) == writerSuccess);
        f.usage(0u, 0u, 0u);
    }

    static void canonicalExplicitRefusal()
    {
        using Graph = rhi::OriginalCommandResourceStates;
        Fixture f;
        auto writer = f.participant();
        auto a = f.prepare(writer);
        assert(!f.completion->acceptValidator(a));
        auto input = f.borrow(writer, a);
        auto output = canonicalOutput(f, writer->source);
        auto inputs = frozenInputs(writer->source, output, true);
        const std::size_t index = 0u;
        const char* diagnostic = nullptr;
        auto reservation = f.pins->reserveValidator(inputs, &index, &input, 1u, diagnostic);
        assert(reservation && !diagnostic);
        auto stateQuota = quota();
        assert(!Graph::prepareOriginalSubmittedReadValidator(inputs,
            *f.device,
            {1u, true, 1u, 2u, 2u, 0u, 32u, bytes},
            stateQuota,
            *reservation,
            {1u, true, {1u, true, 2u, 0u, 2u, 1u}, {}},
            diagnostic));
        expect(diagnostic, "RHI-SUBMISSION-READ-EXPLICIT-ACCESS");
        assert(!stateQuota->liveBytes() && !writer->journal.empty());
        f.usage(1u, 1u, 1u);
        reservation.reset();
        f.usage(0u, 0u, 0u);
        assert(!Completion::deliverValidatorResult(a, true) && !f.completion->pollValidator());
    }

    static void coldReservation(bool writerSuccess)
    {
        Fixture f, other;
        auto writer = f.participant(), reader1 = f.participant(), reader2 = f.participant();
        auto a = f.prepare(writer);
        assert(!f.completion->acceptValidator(a));
        auto input = f.borrow(writer, a);
        auto inputs = frozenInputs(writer->source), equalInputs = frozenInputs(writer->source);
        const std::weak_ptr<const rhi::OriginalCommandInputs> weakInputs = inputs;
        const std::size_t index = 0u, invalidIndex = 1u;
        const char* diagnostic = nullptr;
        assert(!f.pins->reserveValidator(inputs, &invalidIndex, &input, 1u, diagnostic));
        expect(diagnostic, "RHI-SUBMISSION-READ-SOURCE-INDEX");
        auto falseOwner = core::makeShared<int>(0);
        const std::shared_ptr<const rhi::OriginalCommandInputs> counterfeit(
            falseOwner, inputs.get());
        assert(!f.pins->reserveValidator(counterfeit, &index, &input, 1u, diagnostic));
        expect(diagnostic, "RHI-ORIGINAL-INPUTS-OWNER");
        f.usage(0u, 0u, 0u);
        auto first = f.pins->reserveValidator(inputs, &index, &input, 1u, diagnostic);
        assert(first && !diagnostic && first->identity() && !first->provesGpuCompletion());
        const auto identity = first->identity();
        auto observation = f.pins->observeValidator(*first, index, diagnostic);
        assert(observation && !diagnostic && !observation->diagnostic());
        auto second = f.pins->reserveValidator(inputs, &index, &input, 1u, diagnostic);
        assert(second && !diagnostic && second->identity() > identity);
        f.usage(2u, 2u, 2u);
        // Unattached RAII cancellation changes no writer journal and releases
        // only this cold reservation. No completion reader exists yet.
        second.reset();
        f.usage(1u, 1u, 1u);
        second = f.pins->reserveValidator(inputs, &index, &input, 1u, diagnostic);
        assert(second && second->identity() > identity + 1u);
        auto b = f.prepare(reader1, &a, 1u), c = f.prepare(reader2, &a, 1u);
        auto orphan = f.participant();
        auto orphanTicket = f.prepare(orphan);
        expect(f.pins->attachReservationValidator(orphanTicket, *first, inputs),
            "RHI-SUBMISSION-READ-PREDECESSOR");
        assert(!f.completion->cancelValidator(orphanTicket));
        expect(f.pins->attachReservationValidator(b, *first, equalInputs),
            "RHI-SUBMISSION-READ-INPUTS");
        expect(f.pins->attachReservationValidator(b, *first, counterfeit),
            "RHI-SUBMISSION-READ-INPUTS");
        expect(other.pins->attachReservationValidator(b, *first, inputs),
            "RHI-SUBMISSION-READ-RESERVATION");
        std::thread foreign([&] {
            expect(f.pins->attachReservationValidator(b, *first, inputs),
                "RHI-SUBMISSION-READ-THREAD");
            assert(!f.pins->observeValidator(*first, index, diagnostic));
            expect(diagnostic, "RHI-SUBMISSION-READ-THREAD");
        });
        foreign.join();
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(!f.pins->attachReservationValidator(b, *first, inputs));
        expect(f.pins->attachReservationValidator(c, *first, inputs),
            "RHI-SUBMISSION-READ-RESERVATION-BOUND");
        assert(!f.pins->attachReservationValidator(c, *second, inputs));
        assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
        first.reset();
        second.reset(); // bound custody now belongs to the original records
        assert(!f.completion->acceptValidator(b) && !f.completion->acceptValidator(c));
        inputs.reset();
        assert(!weakInputs.expired());
        assert(!Completion::deliverValidatorResult(c, true) && !f.completion->pollValidator());
        assert(c.outcome() == Outcome::Pending);
        input.writerRetention.reset();
        const std::weak_ptr<Participant> weakWriter = writer;
        writer.reset();
        const auto hotAllocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(!Completion::deliverValidatorResult(a, writerSuccess)
            && !f.completion->pollValidator());
        assert(weakWriter.expired());
        if (writerSuccess)
            assert(!observation->diagnostic());
        assert(!Completion::deliverValidatorResult(b, true) && !f.completion->pollValidator());
        assert(b.outcome() == (writerSuccess ? Outcome::Succeeded : Outcome::Failed));
        assert(c.outcome() == b.outcome());
        assert(hotAllocations == core::defaultCpuAllocator().stats().allocationCount);
        assert(weakInputs.expired());
        expect(observation->diagnostic(), "RHI-SUBMISSION-READ-PIN-EXPIRED");
        f.usage(0u, 0u, 0u);
    }

    static void coldCapacityAndCancellation()
    {
        std::uint64_t required = 0u;
        for (unsigned mode = 0u; mode < 5u; ++mode) {
            Budget budget{1u, true, 1u, 2u, 2u, bytes};
            if (mode == 1u || mode == 2u)
                budget.maxCpuMetadataBytes = mode == 1u ? required - 1u : required;
            if (mode == 3u)
                budget.maxBufferReadPins = 1u;
            if (mode == 4u)
                budget.maxPredecessorLinks = 1u;
            if (mode >= 3u)
                budget.maxPendingCommands = 3u;
            Fixture f(budget);
            auto writer = f.participant();
            auto a = f.prepare(writer);
            assert(!f.completion->acceptValidator(a));
            auto input = f.borrow(writer, a);
            auto inputs = frozenInputs(writer->source);
            const std::size_t index = 0u;
            const char* diagnostic = nullptr;
            auto reservation = f.pins->reserveValidator(inputs, &index, &input, 1u, diagnostic);
            if (mode == 1u) {
                assert(!reservation);
                expect(diagnostic, "RHI-SUBMISSION-READ-ALLOCATION");
                f.usage(0u, 0u, 0u);
            } else {
                assert(reservation && !diagnostic);
                if (mode == 0u)
                    required = f.readQuota->peakBytes();
                f.usage(1u, 1u, 1u);
                // Commands are independently bounded, including cold reservations.
                assert(!f.pins->reserveValidator(inputs, &index, &input, 1u, diagnostic));
                expect(diagnostic,
                    mode == 3u       ? "RHI-SUBMISSION-READ-PIN-CAPACITY"
                        : mode == 4u ? "RHI-SUBMISSION-READ-LINK-CAPACITY"
                                     : "RHI-SUBMISSION-READ-COMMAND-CAPACITY");
                auto observation = f.pins->observeValidator(*reservation, index, diagnostic);
                assert(observation && !diagnostic);
                assert(
                    !Completion::deliverValidatorResult(a, true) && !f.completion->pollValidator());
                assert(!observation->diagnostic());
                core::MutationJournal journal;
                assert(!state(*input.source->originalObject()).beginStateRecording(journal));
                const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
                reservation.reset();
                assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
                expect(observation->diagnostic(), "RHI-SUBMISSION-READ-PIN-EXPIRED");
                assert(state(*input.source->originalObject()).beginStateRecording(journal));
                journal.rollback();
                f.usage(0u, 0u, 0u);
            }
            if (mode == 1u)
                assert(
                    !Completion::deliverValidatorResult(a, true) && !f.completion->pollValidator());
        }
    }

    static void coldPublishedAttachmentAndBoundCancel()
    {
        Fixture f;
        auto writer = f.participant(), reader = f.participant();
        auto a = f.prepare(writer);
        assert(!f.completion->acceptValidator(a));
        auto input = f.borrow(writer, a);
        auto inputs = frozenInputs(writer->source);
        const std::size_t index = 0u;
        const char* diagnostic = nullptr;
        auto reservation = f.pins->reserveValidator(inputs, &index, &input, 1u, diagnostic);
        assert(reservation && !diagnostic);
        auto observation = f.pins->observeValidator(*reservation, index, diagnostic);
        assert(observation && !diagnostic);
        input.writerRetention.reset();
        const std::weak_ptr<Participant> weakWriter = writer;
        writer.reset();
        assert(!Completion::deliverValidatorResult(a, true) && !f.completion->pollValidator());
        assert(weakWriter.expired() && !observation->diagnostic());
        auto b = f.prepare(reader, &a, 1u);
        assert(!f.pins->attachReservationValidator(b, *reservation, inputs));
        reservation.reset(); // this must not cancel the bound batch
        assert(!observation->diagnostic());
        core::MutationJournal journal;
        assert(!state(*input.source->originalObject()).beginStateRecording(journal));
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(!f.completion->cancelValidator(b));
        assert(reader->cancels == 1u && reader->journal.empty());
        assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
        expect(observation->diagnostic(), "RHI-SUBMISSION-READ-PIN-EXPIRED");
        assert(state(*input.source->originalObject()).beginStateRecording(journal));
        journal.rollback();
        f.usage(0u, 0u, 0u);
    }

    static void multipleReaders(bool childFirst, bool writerSuccess)
    {
        Fixture f;
        auto writer = f.participant(), reader1 = f.participant(), reader2 = f.participant();
        auto a = f.prepare(writer);
        assert(!f.completion->acceptValidator(a));
        auto b = f.prepare(reader1, &a, 1u), c = f.prepare(reader2, &a, 1u);
        auto input = f.borrow(writer, a);
        assert(!f.pins->attachValidator(b, &input, 1u));
        assert(!f.pins->attachValidator(c, &input, 1u));
        f.usage(2u, 2u, 2u);
        const char* diagnostic = nullptr;
        auto observation = f.pins->observeValidator(b, writer->source, diagnostic);
        assert(observation && !diagnostic && !observation->diagnostic()
            && !observation->provesGpuCompletion());
        const auto* buffer = writer->source->originalObject();
        core::MutationJournal foreign;
        assert(!state(*buffer).beginStateRecording(foreign) && foreign.empty());
        auto refused =
            state(*buffer).transitionState(State::ShaderRead, State::ShaderWrite, &writer->journal);
        assert(!refused.success && refused.diagnostics == "RHI-SUBMISSION-READ-PINNED");
        assert(!f.completion->acceptValidator(b) && !f.completion->acceptValidator(c));
        if (childFirst) {
            assert(!Completion::deliverValidatorResult(c, true));
            assert(!f.completion->pollValidator());
            assert(c.outcome() == Outcome::Pending && !reader2->resolves);
        }
        const std::weak_ptr<Participant> weakWriter = writer;
        input.writerRetention.reset();
        writer.reset();
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(!Completion::deliverValidatorResult(a, writerSuccess));
        assert(!f.completion->pollValidator());
        assert(weakWriter.expired()); // the old journal is destroyed here
        assert(a.outcome() == (writerSuccess ? Outcome::Succeeded : Outcome::Failed));
        if (writerSuccess)
            assert(!observation->diagnostic()); // must not dereference that journal
        if (!childFirst)
            assert(!Completion::deliverValidatorResult(c, true));
        assert(!f.completion->pollValidator());
        assert(c.outcome() == (writerSuccess ? Outcome::Succeeded : Outcome::Failed));
        assert(b.outcome() == Outcome::Pending);
        assert(!Completion::deliverValidatorResult(b, true));
        assert(!f.completion->pollValidator());
        assert(b.outcome() == (writerSuccess ? Outcome::Succeeded : Outcome::Failed));
        assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
        expect(observation->diagnostic(), "RHI-SUBMISSION-READ-PIN-EXPIRED");
        f.usage(0u, 0u, 0u);
        if (writerSuccess)
            assert(state(*buffer).transitionState(State::ShaderRead, State::ShaderWrite).success);
    }

    static void clonePublishedAndCancel()
    {
        Fixture f;
        auto writer = f.participant(), reader1 = f.participant(), reader2 = f.participant(),
             cancelled = f.participant();
        auto a = f.prepare(writer);
        assert(!f.completion->acceptValidator(a));
        auto b = f.prepare(reader1, &a, 1u), c = f.prepare(cancelled, &a, 1u);
        auto input = f.borrow(writer, a);
        assert(!f.pins->attachValidator(b, &input, 1u));
        assert(!f.pins->attachValidator(c, &input, 1u));
        assert(!f.completion->cancelValidator(c) && cancelled->cancels == 1u);
        f.usage(1u, 1u, 1u);
        assert(!f.completion->acceptValidator(b));
        assert(!Completion::deliverValidatorResult(a, true) && !f.completion->pollValidator());
        const char* diagnostic = nullptr;
        auto observation = f.pins->observeValidator(b, writer->source, diagnostic);
        assert(observation && !diagnostic);
        auto d = f.prepare(reader2, &a, 1u);
        expect(f.pins->attachValidator(d, &input, 1u), "RHI-SUBMISSION-READ-OBSERVATION-REQUIRED");
        input.existing = &*observation;
        input.writerJournal = nullptr;
        input.writerRetention.reset();
        assert(!f.pins->attachValidator(d, &input, 1u));
        assert(!f.completion->acceptValidator(d));
        assert(!Completion::deliverValidatorResult(b, true) && !f.completion->pollValidator());
        expect(observation->diagnostic(), "RHI-SUBMISSION-READ-PIN-EXPIRED");
        assert(!state(*input.source->originalObject()).beginStateRecording(reader1->journal));
        f.usage(1u, 1u, 1u);
        assert(!Completion::deliverValidatorResult(d, true) && !f.completion->pollValidator());
        f.usage(0u, 0u, 0u);
        assert(state(*input.source->originalObject()).beginStateRecording(reader1->journal));
        reader1->journal.rollback();
    }

    static void refusalsAndCapacity()
    {
        for (unsigned bound = 0u; bound < 3u; ++bound) {
            Budget budget{1u, true, 3u, 6u, 3u, bytes};
            if (bound == 0u)
                budget.maxPendingCommands = 1u;
            if (bound == 1u)
                budget.maxBufferReadPins = 1u;
            if (bound == 2u)
                budget.maxPredecessorLinks = 1u;
            Fixture f(budget), other;
            auto writer = f.participant(), reader1 = f.participant(), reader2 = f.participant();
            auto a = f.prepare(writer);
            auto input = f.borrow(writer, a);
            auto b = f.prepare(reader1);
            expect(f.pins->attachValidator(b, &input, 1u), "RHI-SUBMISSION-READ-WRITER");
            assert(!f.completion->acceptValidator(a));
            expect(f.pins->attachValidator(b, &input, 1u), "RHI-SUBMISSION-READ-PREDECESSOR");
            assert(!f.completion->cancelValidator(b));
            reader1 = f.participant();
            b = f.prepare(reader1, &a, 1u);
            auto c = f.prepare(reader2, &a, 1u);
            auto invalid = input;
            invalid.writerJournal = &reader1->journal;
            expect(f.pins->attachValidator(b, &invalid, 1u), "RHI-SUBMISSION-READ-WRITER-STATE");
            invalid = input;
            auto fakeOwner = core::makeShared<int>(1);
            invalid.writerRetention = std::shared_ptr<void>(fakeOwner, writer.get());
            expect(f.pins->attachValidator(b, &invalid, 1u), "RHI-SUBMISSION-READ-WRITER-CUSTODY");
            invalid = input;
            invalid.source = other.participant()->source;
            expect(f.pins->attachValidator(b, &invalid, 1u), "RHI-SUBMISSION-READ-SOURCE");
            invalid = input;
            invalid.sizeBytes = 63u;
            expect(f.pins->attachValidator(b, &invalid, 1u), "RHI-SUBMISSION-READ-WHOLE-BUFFER");
            {
                core::CpuMemoryQuotaLease exhausted(f.readQuota,
                    static_cast<std::size_t>(
                        budget.maxCpuMetadataBytes - f.readQuota->liveBytes()));
                assert(exhausted.charged());
                expect(f.pins->attachValidator(b, &input, 1u), "RHI-SUBMISSION-READ-ALLOCATION");
            }
            f.usage(0u, 0u, 0u);
            assert(!f.pins->attachValidator(b, &input, 1u));
            constexpr std::array reasons{"RHI-SUBMISSION-READ-COMMAND-CAPACITY",
                "RHI-SUBMISSION-READ-PIN-CAPACITY",
                "RHI-SUBMISSION-READ-LINK-CAPACITY"};
            expect(f.pins->attachValidator(c, &input, 1u), reasons[bound]);
            f.usage(1u, 1u, 1u);
            assert(!f.completion->cancelValidator(b));
            assert(!f.pins->attachValidator(c, &input, 1u));
            assert(!f.completion->cancelValidator(c));
            assert(!Completion::deliverValidatorResult(a, true) && !f.completion->pollValidator());
            f.usage(0u, 0u, 0u);
        }
    }

    static void issuerAndThread()
    {
        Fixture f, other;
        const char* diagnostic = nullptr;
        assert(!Pins::prepareValidator(*f.device, f.completion, f.budget, f.readQuota, diagnostic));
        expect(diagnostic, "RHI-SUBMISSION-READ-ISSUER-ALREADY-PREPARED");
        assert(!Pins::prepareValidator(*other.device, f.completion, f.budget, quota(), diagnostic));
        expect(diagnostic, "RHI-ORIGINAL-DEVICE-LIFETIME-FOREIGN-DEVICE");
        auto fakeOwner = core::makeShared<int>(1);
        std::shared_ptr<Pins> counterfeit(fakeOwner, f.pins.get());
        expect(Pins::validateOriginal(counterfeit), "RHI-SUBMISSION-READ-ISSUER-OWNER");
        auto writer = f.participant(), reader = f.participant();
        auto a = f.prepare(writer);
        assert(!f.completion->acceptValidator(a));
        auto b = f.prepare(reader, &a, 1u);
        auto input = f.borrow(writer, a);
        std::thread foreign([&] {
            expect(f.pins->attachValidator(b, &input, 1u), "RHI-SUBMISSION-READ-THREAD");
            Usage usage{99u, 99u, 99u, 99u};
            expect(f.pins->usageValidator(usage), "RHI-SUBMISSION-READ-THREAD");
            assert(usage.pendingCommands == 99u);
        });
        foreign.join();
        f.usage(0u, 0u, 0u);
        assert(!f.pins->attachValidator(b, &input, 1u));
        auto observation = f.pins->observeValidator(b, writer->source, diagnostic);
        assert(observation && !diagnostic);
        std::thread observer([&] {
            expect(observation->diagnostic(), "RHI-SUBMISSION-READ-THREAD");
        });
        observer.join();
        assert(!f.completion->cancelValidator(b));
        assert(!Completion::deliverValidatorResult(a, true) && !f.completion->pollValidator());
    }

    static void multiBufferAndExactMetadata()
    {
        Fixture f;
        auto writer1 = f.participant(), writer2 = f.participant(), reader = f.participant();
        auto a = f.prepare(writer1), b = f.prepare(writer2);
        assert(!f.completion->acceptValidator(a) && !f.completion->acceptValidator(b));
        std::array predecessors{a, b};
        auto c = f.prepare(reader, predecessors.data(), predecessors.size());
        std::array inputs{f.borrow(writer1, a), f.borrow(writer2, b)};
        std::array duplicate{inputs[0], inputs[0]};
        expect(f.pins->attachValidator(c, duplicate.data(), duplicate.size()),
            "RHI-SUBMISSION-READ-DUPLICATE-BUFFER");
        assert(!f.pins->attachValidator(c, inputs.data(), inputs.size()));
        f.usage(1u, 2u, 2u);
        assert(!f.completion->acceptValidator(c));
        assert(!Completion::deliverValidatorResult(c, true));
        assert(!Completion::deliverValidatorResult(a, true) && !f.completion->pollValidator());
        assert(c.outcome() == Outcome::Pending && !reader->resolves);
        assert(!Completion::deliverValidatorResult(b, false) && !f.completion->pollValidator());
        assert(c.outcome() == Outcome::Failed && reader->resolves == 1u);
        f.usage(0u, 0u, 0u);

        const auto required = f.readQuota->peakBytes();
        // This measures the actual issuer+batch+two-entry layout, with no pin
        // object retained by a diagnostic/observation token after terminal cleanup.
        for (const auto limit : {required - 1u, required}) {
            Fixture exact({1u, true, 3u, 6u, 3u, limit});
            auto w1 = exact.participant(), w2 = exact.participant(), r = exact.participant();
            auto x = exact.prepare(w1), y = exact.prepare(w2);
            assert(!exact.completion->acceptValidator(x) && !exact.completion->acceptValidator(y));
            std::array roots{x, y};
            auto z = exact.prepare(r, roots.data(), roots.size());
            std::array borrows{exact.borrow(w1, x), exact.borrow(w2, y)};
            const auto result = exact.pins->attachValidator(z, borrows.data(), borrows.size());
            if (limit == required)
                assert(!result);
            else
                expect(result, "RHI-SUBMISSION-READ-ALLOCATION");
            exact.usage(limit == required ? 1u : 0u,
                limit == required ? 2u : 0u,
                limit == required ? 2u : 0u);
            assert(!exact.completion->cancelValidator(z));
            assert(!Completion::deliverValidatorResult(x, true));
            assert(!Completion::deliverValidatorResult(y, true));
            assert(!exact.completion->pollValidator());
        }

        Fixture cold({1u, true, 3u, 6u, 3u, 1u}, false);
        const char* diagnostic = nullptr;
        assert(!Pins::prepareValidator(
            *cold.device, cold.completion, cold.budget, cold.readQuota, diagnostic));
        expect(diagnostic, "RHI-SUBMISSION-READ-ALLOCATION");
        assert(!cold.readQuota->liveBytes());
        cold.budget.maxCpuMetadataBytes = bytes;
        cold.readQuota = quota();
        cold.pins = Pins::prepareValidator(
            *cold.device, cold.completion, cold.budget, cold.readQuota, diagnostic);
        assert(cold.pins && !diagnostic); // allocation failure did not consume issuer identity
    }

    static void deferredCleanup()
    {
        Fixture f;
        auto writer = f.participant(), reader = f.participant();
        auto a = f.prepare(writer);
        assert(!f.completion->acceptValidator(a));
        auto b = f.prepare(reader, &a, 1u);
        auto input = f.borrow(writer, a);
        assert(!f.pins->attachValidator(b, &input, 1u));
        assert(!f.completion->acceptValidator(b));
        std::weak_ptr<Pins> weakPins = f.pins;
        const std::weak_ptr<Participant> weakWriter = writer, weakReader = reader;
        f.device->retireOriginalDeviceLifetime();
        assert(!f.completion->retireValidatorAfterResults());
        input = {};
        writer.reset();
        reader.reset();
        f.pins.reset();
        assert(!weakPins.expired() && !weakWriter.expired() && !weakReader.expired());
        std::thread finalResult([domain = std::move(f.completion), a, b]() mutable {
            assert(!Completion::deliverValidatorResult(b, true));
            assert(!Completion::deliverValidatorResult(a, true));
            domain.reset(); // final retired result owner, not the device thread
        });
        finalResult.join();
        assert(weakPins.expired() && weakWriter.expired() && weakReader.expired());
        assert(a.outcome() == Outcome::Failed && b.outcome() == Outcome::Failed);
        // An expired external weak owner still retains its quota-accounted
        // shared control block; release that metadata before requiring zero.
        assert(f.readQuota->liveBytes());
        weakPins.reset();
        assert(!f.readQuota->liveBytes());
    }

public:
    static void verify(bool smoke = false)
    {
        canonicalJoin(true, true, false, true);
        multipleReaders(true, true);
        clonePublishedAndCancel();
        coldReservation(true);
        coldPublishedAttachmentAndBoundCancel();
        if (!smoke) {
            canonicalMultiReaders(true);
            canonicalMultiReaders(false);
            for (bool planned : {false, true})
                for (bool writerFirst : {false, true})
                    for (bool success : {false, true}) {
                        canonicalJoin(planned, success, false, writerFirst);
                        canonicalJoin(planned, success, true, writerFirst);
                    }
            canonicalExplicitRefusal();
            coldReservation(false);
            coldCapacityAndCancellation();
            multipleReaders(false, true);
            multipleReaders(true, false);
            multipleReaders(false, false);
            refusalsAndCapacity();
            issuerAndThread();
            multiBufferAndExactMetadata();
            deferredCleanup();
        }
        std::puts(
            "ORIGINAL_SUBMITTED_BUFFER_PINS_PASS originalRegisteredBuffers=true soleJournals=true nativeProof=false");
        std::puts(
            "ORIGINAL_SUBMITTED_CANONICAL_JOIN_PASS sameInputs=true soleProgram=true borrowedJournalClaims=0 nativeProof=false");
    }
};
} // namespace arp::test

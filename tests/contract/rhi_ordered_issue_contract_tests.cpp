#include "rhi/frontend/command_ordered_issue.h"
#include "rhi/semantics/ordered_command_issue.h"

#include <array>
#include <cassert>
#include <cstring>
#include <limits>

#ifdef NDEBUG
#error Ordered issue contract tests require assertions
#endif

using namespace arp;
using namespace arp::rhi;
using common::OrderedCommandIssue;

static void expect(const char* actual, const char* expected)
{
    assert(actual && std::strcmp(actual, expected) == 0);
}

static void verifyStorageAndIssue()
{
    core::LifetimeEpoch context, foreign;
    OrderedCommandIssueBudget budget{1u, true, 2u, 2u, 8u, 8192u, 4u};
    auto quota = core::makeShared<core::CpuMemoryQuota>(budget.maxCpuMetadataBytes);
    const char* diagnostic = nullptr;
    auto command = OrderedCommandIssue::prepare(budget, quota, context.ticket(), diagnostic);
    assert(command && !diagnostic);
    const auto charged = quota->liveBytes();
    assert(charged && charged <= budget.maxCpuMetadataBytes);
    auto first = core::makeShared<int>(1), second = core::makeShared<int>(2),
         excess = core::makeShared<int>(3);
    std::array<std::shared_ptr<const void>, 3> owners{first, second, excess};
    std::weak_ptr<int> firstWeak = first, secondWeak = second;
    std::array<unsigned char, 4> bytes{1, 2, 3, 4};
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes() - charged);
    assert(pressure.charged());
    expect(command->recordOperation(owners.data(), 3u, bytes.data(), bytes.size()),
        "RHI-ORDERED-ISSUE-OWNER-CAPACITY");
    assert(command->operationCount() == 0u && command->ownerCount() == 0u
        && command->snapshotByteCount() == 0u);
    expect(command->recordOperation(nullptr, 1u, bytes.data(), bytes.size()),
        "RHI-ORDERED-ISSUE-OWNER-CAPACITY");
    expect(command->recordOperation(
               owners.data(), std::numeric_limits<std::size_t>::max(), nullptr, 0u),
        "RHI-ORDERED-ISSUE-REFERENCE-CAPACITY");
    expect(command->recordOperation(
               nullptr, 0u, bytes.data(), std::numeric_limits<std::size_t>::max()),
        "RHI-ORDERED-ISSUE-SNAPSHOT-CAPACITY");
    expect(command->recordOperation(owners.data(), 2u, nullptr, 1u),
        "RHI-ORDERED-ISSUE-SNAPSHOT-SOURCE");
    assert(!command->recordOperation(owners.data(), 2u, bytes.data(), bytes.size()));
    bytes.fill(0);
    assert(command->operationCount() == 1u && command->snapshotByteCount() == 4u
        && command->ownerCount() == 2u);
    std::array<unsigned char, 5> tooLarge{};
    expect(command->recordOperation(nullptr, 0u, tooLarge.data(), tooLarge.size()),
        "RHI-ORDERED-ISSUE-SNAPSHOT-CAPACITY");
    assert(command->operationCount() == 1u && command->snapshotByteCount() == 4u);
    bytes = {5, 6, 7, 8};
    assert(!command->recordOperation(owners.data(), 2u, bytes.data(), bytes.size()));
    bytes.fill(0);
    expect(
        command->recordOperation(nullptr, 0u, nullptr, 0u), "RHI-ORDERED-ISSUE-OPERATION-CAPACITY");
    assert(command->snapshotByteCount() == 8u && command->ownerCount() == 2u);
    owners = {};
    first.reset();
    second.reset();
    assert(!firstWeak.expired() && !secondWeak.expired());
    assert(!command->seal());
    expect(command->recordOperation(nullptr, 0u, nullptr, 0u), "RHI-ORDERED-ISSUE-STATE");
    expect(command->beginIssue(foreign.ticket()), "RHI-ORDERED-ISSUE-CONTEXT-MISMATCH");
    assert(command->phase() == OrderedCommandIssue::Phase::Sealed);
    OrderedCommandIssue::PayloadView inspection;
    assert(!command->inspectSealed(context.ticket(), 1, inspection) && inspection.size == 4);
    assert(std::to_integer<unsigned char>(inspection.data[0]) == 5);
    expect(command->inspectSealed(foreign.ticket(), 0, inspection),
        "RHI-ORDERED-ISSUE-CONTEXT-MISMATCH");
    expect(
        command->inspectSealed(context.ticket(), 2, inspection), "RHI-ORDERED-ISSUE-NO-OPERATION");
    assert(!command->stats().workAccepted && !command->stats().mayHaveNativeSideEffects);
    assert(!command->beginIssue(context.ticket()));
    assert(!command->stats().workAccepted && !command->stats().mayHaveNativeSideEffects);
    expect(command->cancel(), "RHI-ORDERED-ISSUE-STATE");
    expect(command->finishIssue(context.ticket()), "RHI-ORDERED-ISSUE-OPERATIONS-REMAINING");
    expect(command->acceptOperation(context.ticket()), "RHI-ORDERED-ISSUE-NO-OPERATION");
    OrderedCommandIssue::PayloadView payload;
    for (unsigned operation = 0; operation < 2; ++operation) {
        assert(!command->beginOperation(context.ticket(), payload));
        assert(payload.size == 4u);
        for (unsigned i = 0; i < 4; ++i)
            assert(std::to_integer<unsigned char>(payload.data[i]) == operation * 4u + i + 1u);
        expect(command->beginOperation(context.ticket(), payload),
            "RHI-ORDERED-ISSUE-OPERATION-PENDING");
        expect(command->acceptOperation(foreign.ticket()), "RHI-ORDERED-ISSUE-CONTEXT-MISMATCH");
        assert(command->stats().acceptedOperations == operation);
        assert(!command->acceptOperation(context.ticket()));
    }
    assert(!firstWeak.expired() && command->stats().workAccepted);
    assert(!command->finishIssue(context.ticket()));
    assert(firstWeak.expired() && secondWeak.expired());
    assert(command->phase() == OrderedCommandIssue::Phase::Issued);
    assert(command->ownerCount() == 0u && command->snapshotByteCount() == 0u);
    assert(!command->stats().gpuCompleted && !command->nativeExecutionProven());
    expect(command->beginIssue(context.ticket()), "RHI-ORDERED-ISSUE-STATE");
    expect(command->finishIssue(context.ticket()), "RHI-ORDERED-ISSUE-STATE");
    assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
    assert(quota->liveBytes() == quota->limitBytes());
    pressure.release();
    assert(quota->liveBytes() == charged);
    command.reset();
    assert(quota->liveBytes() == 0u);

    // The exact allocator charge, rather than sizeof or payload byte count,
    // defines the preparation boundary. A failed nested reservation unwinds.
    for (auto limit : {charged - 1u, charged}) {
        budget.maxCpuMetadataBytes = limit;
        auto bounded = core::makeShared<core::CpuMemoryQuota>(limit);
        command = OrderedCommandIssue::prepare(budget, bounded, context.ticket(), diagnostic);
        assert(bool(command) == (limit == charged));
        if (!command)
            expect(diagnostic, "RHI-ORDERED-ISSUE-ALLOCATION");
        command.reset();
        assert(bounded->liveBytes() == 0u);
    }
}

static void verifyTerminalsAndContext()
{
    for (unsigned accepted : {0u, 1u}) {
        core::LifetimeEpoch context;
        OrderedCommandIssueBudget budget{1u, true, 2u, 1u, 0u, 8192u, 1u};
        auto quota = core::makeShared<core::CpuMemoryQuota>(budget.maxCpuMetadataBytes);
        const char* diagnostic = nullptr;
        auto command = OrderedCommandIssue::prepare(budget, quota, context.ticket(), diagnostic);
        auto owner = core::makeShared<int>(1);
        std::shared_ptr<const void> retained = owner;
        std::weak_ptr<int> weak = owner;
        assert(!command->recordOperation(&retained, 1u, nullptr, 0u));
        assert(!command->recordOperation(nullptr, 0u, nullptr, 0u));
        owner.reset();
        retained.reset();
        assert(!command->seal() && !command->beginIssue(context.ticket()));
        OrderedCommandIssue::PayloadView payload;
        assert(!command->beginOperation(context.ticket(), payload));
        assert(payload.data == nullptr && payload.size == 0u);
        if (accepted)
            assert(!command->acceptOperation(context.ticket()));
        assert(!command->fail(OrderedCommandIssue::Failure::NativeCall));
        assert(weak.expired() && command->phase() == OrderedCommandIssue::Phase::Failed);
        assert(command->stats().acceptedOperations == accepted);
        assert(command->stats().workAccepted == bool(accepted));
        assert(command->stats().mayHaveNativeSideEffects && !command->stats().gpuCompleted);
        expect(command->finishIssue(context.ticket()), "RHI-ORDERED-ISSUE-STATE");
    }
    for (unsigned phase = 0; phase < 4; ++phase) {
        core::LifetimeEpoch context;
        OrderedCommandIssueBudget budget{1u, true, 1u, 0u, 0u, 8192u};
        auto quota = core::makeShared<core::CpuMemoryQuota>(budget.maxCpuMetadataBytes);
        const char* diagnostic = nullptr;
        const auto old = context.ticket();
        auto command = OrderedCommandIssue::prepare(budget, quota, old, diagnostic);
        assert(!command->recordOperation(nullptr, 0u, nullptr, 0u));
        if (phase >= 1)
            assert(!command->seal());
        if (phase >= 2)
            assert(!command->beginIssue(old));
        if (phase >= 3) {
            OrderedCommandIssue::PayloadView payload;
            assert(!command->beginOperation(old, payload));
            assert(!command->acceptOperation(old));
        }
        context.advance();
        expect(command->finishIssue(context.ticket()), "RHI-ORDERED-ISSUE-CONTEXT-LOST");
        assert(
            command->phase() == OrderedCommandIssue::Phase::Lost && !command->stats().gpuCompleted);
        assert(command->stats().workAccepted == (phase == 3));
        expect(command->beginIssue(old), "RHI-ORDERED-ISSUE-CONTEXT-LOST");
        assert(!OrderedCommandIssue::prepare(budget, quota, old, diagnostic));
        expect(diagnostic, "RHI-ORDERED-ISSUE-CONTEXT");
        auto restored = OrderedCommandIssue::prepare(budget, quota, context.ticket(), diagnostic);
        assert(restored && !diagnostic);
        expect(restored->seal(), "RHI-ORDERED-ISSUE-EMPTY");
        assert(!restored->cancel());
        assert(!restored->stats().workAccepted && !restored->stats().mayHaveNativeSideEffects);
    }
}

static void verifyPreparationAndCancellation()
{
    core::LifetimeEpoch context;
    OrderedCommandIssueBudget budget{1u, true, 2u, 1u, 8u, 8192u, 1u};
    const char* diagnostic = nullptr;
    auto quota = core::makeShared<core::CpuMemoryQuota>(budget.maxCpuMetadataBytes);
    assert(!OrderedCommandIssue::prepare(budget, {}, context.ticket(), diagnostic));
    expect(diagnostic, "RHI-ORDERED-ISSUE-CPU-QUOTA");
    assert(!OrderedCommandIssue::prepare(budget, quota, {}, diagnostic));
    expect(diagnostic, "RHI-ORDERED-ISSUE-CONTEXT");
    auto tooLarge = budget;
    tooLarge.maxSnapshotBytes = std::numeric_limits<std::uint64_t>::max();
    assert(!OrderedCommandIssue::prepare(tooLarge, quota, context.ticket(), diagnostic));
    assert(quota->liveBytes() == 0u);
    for (bool sealed : {false, true}) {
        auto command = OrderedCommandIssue::prepare(budget, quota, context.ticket(), diagnostic);
        assert(command && !diagnostic);
        auto object = core::makeShared<int>(1);
        std::shared_ptr<const void> owner = object;
        std::weak_ptr<int> weak = object;
        assert(!command->recordOperation(&owner, 1u, nullptr, 0u));
        // Duplicate reference occurrences have an independent traversal bound.
        expect(command->recordOperation(&owner, 1u, nullptr, 0u),
            "RHI-ORDERED-ISSUE-REFERENCE-CAPACITY");
        assert(command->operationCount() == 1u && command->ownerCount() == 1u);
        owner.reset();
        object.reset();
        if (sealed)
            assert(!command->seal());
        quota->setLimitBytes(
            0u); // Core quota's unlimited convention cannot leak through this contract.
        expect(command->recordOperation(nullptr, 0u, nullptr, 0u), "RHI-ORDERED-ISSUE-CPU-QUOTA");
        assert(!command->cancel()); // Cleanup must remain possible after quota rejection.
        assert(weak.expired() && command->phase() == OrderedCommandIssue::Phase::Cancelled);
        assert(!command->stats().workAccepted && !command->stats().mayHaveNativeSideEffects);
        expect(command->fail(OrderedCommandIssue::Failure::PayloadValidation),
            "RHI-ORDERED-ISSUE-STATE");
        command.reset();
        assert(quota->liveBytes() == 0u);
        quota->setLimitBytes(budget.maxCpuMetadataBytes);
    }
    core::LifetimeEpoch exhausted(std::numeric_limits<std::uint64_t>::max());
    auto command = OrderedCommandIssue::prepare(budget, quota, exhausted.ticket(), diagnostic);
    assert(command);
    exhausted.advance();
    expect(command->seal(), "RHI-ORDERED-ISSUE-CONTEXT-LOST");
    assert(command->phase() == OrderedCommandIssue::Phase::Lost);
    assert(!OrderedCommandIssue::prepare(budget, quota, exhausted.ticket(), diagnostic));
    expect(diagnostic, "RHI-ORDERED-ISSUE-CONTEXT");
}

int main()
{
    verifyStorageAndIssue();
    verifyTerminalsAndContext();
    verifyPreparationAndCancellation();
    for (auto backend : {Backend::Null,
             Backend::Vulkan,
             Backend::Metal,
             Backend::WebGPU,
             Backend::WebGL,
             static_cast<Backend>(255)}) {
        const auto decision = semantics::OrderedCommandIssueLowering::current(backend);
        assert(!decision.executable && !decision.provesGpuCompletion);
        assert(decision.validatorOnly == (backend == Backend::Null));
        assert(decision.mode
            == (backend == Backend::Null ? CapabilityMode::Equivalent
                                         : CapabilityMode::DiagnosticFailure));
        assert(decision.diagnostic != nullptr);
    }
    OrderedCommandIssueBudget budget;
    expect(semantics::OrderedCommandIssueLowering::validate(budget), "RHI-ORDERED-ISSUE-OPT-IN");
    budget.optIn = true;
    expect(semantics::OrderedCommandIssueLowering::validate(budget), "RHI-ORDERED-ISSUE-CPU-QUOTA");
    budget.contractVersion = 0u;
    expect(semantics::OrderedCommandIssueLowering::validate(budget), "RHI-ORDERED-ISSUE-VERSION");
}

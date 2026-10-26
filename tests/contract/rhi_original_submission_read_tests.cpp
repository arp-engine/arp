#include "../fixtures/ordered_reuse_admission_fixture.h"
#include "core/ownership/monotonic_id_sequence.h"
#include "rhi/frontend/resource_state_control.h"
#include "rhi/semantics/original_submission_read.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>

#ifdef NDEBUG
#error Original submission read tests require assertions
#endif

namespace {
using namespace arp;
using namespace rhi;
using namespace rhi::semantics;
using Contract = OriginalSubmissionReadContract;

void expect(const char* actual, const char* expected)
{
    assert(actual && !std::strcmp(actual, expected));
}

void capacities()
{
    OriginalSubmissionReadBudget budget{1u, true, 2u, 6u, 4u, 1024u};
    OriginalSubmissionReadUsage usage{2u, 6u, 4u, 1024u};
    assert(!Contract::validateBudget(budget));
    assert(!Contract::validateUsage(budget, usage));
    assert(!Contract::validateUsage(budget, {}));
    assert(!Contract::validateUsage(budget, {0u, 0u, 0u, 1024u}));

    for (const auto version : {0u, 2u, UINT32_MAX}) {
        auto invalid = budget;
        invalid.contractVersion = version;
        invalid.prototypeOptIn = false;
        expect(Contract::validateUsage(invalid, {}), "RHI-SUBMISSION-READ-VERSION");
    }
    auto invalid = budget;
    invalid.prototypeOptIn = false;
    expect(Contract::validateBudget(invalid), "RHI-SUBMISSION-READ-OPT-IN");

    constexpr std::array fields{&OriginalSubmissionReadBudget::maxPendingCommands,
        &OriginalSubmissionReadBudget::maxBufferReadPins,
        &OriginalSubmissionReadBudget::maxPredecessorLinks,
        &OriginalSubmissionReadBudget::maxCpuMetadataBytes};
    constexpr std::array diagnostics{"RHI-SUBMISSION-READ-COMMAND-CAPACITY",
        "RHI-SUBMISSION-READ-PIN-CAPACITY",
        "RHI-SUBMISSION-READ-LINK-CAPACITY",
        "RHI-SUBMISSION-READ-METADATA-CAPACITY"};
    for (std::size_t i = 0u; i < fields.size(); ++i) {
        invalid = budget;
        --(invalid.*fields[i]);
        expect(Contract::validateUsage(invalid, usage), diagnostics[i]);
        invalid.*fields[i] = 0u;
        expect(Contract::validateBudget(invalid), "RHI-SUBMISSION-READ-CAPACITY");
    }
    for (const OriginalSubmissionReadUsage malformed : {OriginalSubmissionReadUsage{0u, 1u, 0u, 1u},
             {0u, 1u, 1u, 1u},
             {1u, 0u, 0u, 1u},
             {2u, 2u, 1u, 1u},
             {1u, 1u, 2u, 1u},
             {1u, 1u, 1u, 0u}})
        expect(Contract::validateUsage(budget, malformed), "RHI-SUBMISSION-READ-USAGE");

    const OriginalSubmissionReadBudget maximum{
        1u, true, UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX};
    const OriginalSubmissionReadUsage maximumUsage{UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX};
    assert(!Contract::validateUsage(maximum, maximumUsage));
    for (std::size_t i = 0u; i < fields.size(); ++i) {
        invalid = maximum;
        --(invalid.*fields[i]);
        expect(Contract::validateUsage(invalid, maximumUsage), diagnostics[i]);
    }
}

void scopes()
{
    BufferDesc buffer;
    buffer.sizeBytes = 544u;
    buffer.usage = ResourceUsage::StorageBuffer;
    ResourceAccessScope scope{
        ResourceState::ShaderRead, PipelineStage::ComputeShader, ResourceAccess::ShaderRead};
    assert(!Contract::inspectBufferRead(buffer, 0u, buffer.sizeBytes, scope));
    for (const auto memory : {MemoryUsage::GpuOnly, MemoryUsage::CpuToGpu})
        for (const auto usage : {ResourceUsage::UniformBuffer,
                 ResourceUsage::StorageBuffer,
                 ResourceUsage::UniformBuffer | ResourceUsage::StorageBuffer}) {
            buffer.memory = memory;
            buffer.usage = usage;
            for (std::uint32_t mask = 1u; mask < 8u; ++mask) {
                scope.stages = PipelineStage::None;
                if (mask & 1u)
                    scope.stages = scope.stages | PipelineStage::VertexShader;
                if (mask & 2u)
                    scope.stages = scope.stages | PipelineStage::FragmentShader;
                if (mask & 4u)
                    scope.stages = scope.stages | PipelineStage::ComputeShader;
                assert(!Contract::inspectBufferRead(buffer, 0u, buffer.sizeBytes, scope));
            }
        }
    scope.stages = PipelineStage::ComputeShader;
    for (const auto memory :
        {MemoryUsage::GpuToCpu, MemoryUsage::Transient, static_cast<MemoryUsage>(255u)}) {
        buffer.memory = memory;
        expect(Contract::inspectBufferRead(buffer, 0u, buffer.sizeBytes, scope),
            "RHI-SUBMISSION-READ-MEMORY");
    }
    buffer.memory = MemoryUsage::GpuOnly;
    for (const auto usage :
        {ResourceUsage::None, ResourceUsage::CopySrc, ResourceUsage::VertexBuffer}) {
        buffer.usage = usage;
        expect(Contract::inspectBufferRead(buffer, 0u, buffer.sizeBytes, scope),
            "RHI-SUBMISSION-READ-BUFFER-USAGE");
    }
    buffer.usage = ResourceUsage::StorageBuffer;
    for (const auto range : {std::array<std::uint64_t, 2u>{1u, buffer.sizeBytes},
             {0u, 0u},
             {0u, buffer.sizeBytes - 1u},
             {0u, buffer.sizeBytes + 1u},
             {UINT64_MAX, 1u},
             {1u, UINT64_MAX}})
        expect(Contract::inspectBufferRead(buffer, range[0], range[1], scope),
            "RHI-SUBMISSION-READ-WHOLE-BUFFER");
    buffer.sizeBytes = UINT64_MAX;
    assert(!Contract::inspectBufferRead(buffer, 0u, UINT64_MAX, scope));
    buffer.sizeBytes = 0u;
    expect(Contract::inspectBufferRead(buffer, 0u, 0u, scope), "RHI-SUBMISSION-READ-WHOLE-BUFFER");
    buffer.sizeBytes = 544u;

    for (unsigned bit = 0u; bit < 32u; ++bit) {
        auto wrong = scope;
        wrong.stages = static_cast<PipelineStage>(std::uint32_t{1u} << bit);
        const bool shader = wrong.stages == PipelineStage::VertexShader
            || wrong.stages == PipelineStage::FragmentShader
            || wrong.stages == PipelineStage::ComputeShader;
        assert(bool(Contract::inspectBufferRead(buffer, 0u, buffer.sizeBytes, wrong)) == !shader);
        wrong.stages = scope.stages;
        wrong.access = static_cast<ResourceAccess>(std::uint32_t{1u} << bit);
        assert(bool(Contract::inspectBufferRead(buffer, 0u, buffer.sizeBytes, wrong))
            == (wrong.access != ResourceAccess::ShaderRead));
    }
    for (const auto state : {ResourceState::Undefined,
             ResourceState::CopySrc,
             ResourceState::CopyDst,
             ResourceState::ShaderWrite,
             ResourceState::ColorAttachment,
             ResourceState::DepthAttachment,
             ResourceState::Present,
             static_cast<ResourceState>(255u)}) {
        auto wrong = scope;
        wrong.state = state;
        assert(Contract::inspectBufferRead(buffer, 0u, buffer.sizeBytes, wrong));
    }
    for (const auto access : {ResourceAccess::TransferWrite,
             ResourceAccess::ShaderWrite,
             ResourceAccess::ColorAttachmentWrite,
             ResourceAccess::DepthStencilWrite,
             ResourceAccess::IndirectCommandRead}) {
        auto wrong = scope;
        wrong.access = wrong.access | access;
        assert(Contract::inspectBufferRead(buffer, 0u, buffer.sizeBytes, wrong));
    }
}

void lowering()
{
    for (const auto backend : {Backend::Null,
             Backend::Vulkan,
             Backend::Metal,
             Backend::WebGPU,
             Backend::WebGL,
             static_cast<Backend>(255u)})
        for (const auto version : {0u, 1u, 2u, UINT32_MAX}) {
            const auto decision = Contract::lowering(backend, version);
            const auto acceptance = OriginalQueueAcceptanceContract::lowering(backend, version);
            assert(!acceptance.publicReceiptAvailable
                && !acceptance.pendingResourceAdmissionAvailable
                && !acceptance.provesGpuCompletion);
            assert(acceptance.backendPrivateMappingImplemented
                == (version == 1u && backend == Backend::Metal));
            assert(acceptance.validatorOnly == (version == 1u && backend == Backend::Null));
            assert(bool(acceptance.diagnostic) == !(version == 1u && backend == Backend::Metal));
            assert(!decision.nativeReceiptAvailable && !decision.pendingObservationAvailable
                && !decision.executionAvailable && !decision.provesGpuCompletion);
            if (version != 1u) {
                assert(!decision.validatorOnly && !decision.intentInspectionAvailable);
                expect(decision.diagnostic, "RHI-SUBMISSION-READ-VERSION");
            } else if (backend == Backend::Null) {
                assert(decision.validatorOnly && decision.intentInspectionAvailable);
                expect(decision.diagnostic, "RHI-SUBMISSION-READ-VALIDATOR-ONLY");
            } else if (backend == Backend::Vulkan || backend == Backend::Metal
                || backend == Backend::WebGPU) {
                assert(!decision.validatorOnly && decision.intentInspectionAvailable);
                expect(decision.diagnostic, "RHI-SUBMISSION-READ-NATIVE-RECEIPT-UNAVAILABLE");
            } else {
                assert(!decision.validatorOnly && !decision.intentInspectionAvailable);
                expect(decision.diagnostic,
                    backend == Backend::WebGL ? "RHI-SUBMISSION-READ-UNSUPPORTED"
                                              : "RHI-SUBMISSION-READ-BACKEND");
            }
        }
    test::OrderedReuseAdmissionFixture::verify();
    core::MonotonicIdSequence finalOrdinal(UINT64_MAX);
    assert(finalOrdinal.tryAcquire() == UINT64_MAX);
    assert(
        !finalOrdinal.hasRemaining() && !finalOrdinal.tryAcquire() && !finalOrdinal.tryAcquire());
}

void validatorSmoke()
{
    const auto decision = Contract::lowering(Backend::Null, 1u);
    assert(decision.validatorOnly && !decision.executionAvailable);
    common::RecordedResourceStateControl state(ResourceState::ShaderWrite);
    core::MutationJournal writer, foreignReader;
    assert(!state.writeBuffer(ResourceState::ShaderRead, &writer));
    const auto pending = state.observePending(writer);
    assert(pending && !state.observe());
    assert(!state.canPrepare(foreignReader) && !state.prepare(foreignReader));
    assert(!state.observePending(foreignReader));
    foreignReader.rollback();
    assert(state.pending() && state.matchesPending(*pending, writer));
    writer.commit();
    assert(!state.pending() && state.observe()->value == ResourceState::ShaderRead);
    assert(!state.matchesPending(*pending, writer));
    std::puts(
        "SUBMISSION_READ_VALIDATOR_SMOKE_PASS singleJournal=true foreignPendingRejected=true nativeSubmissions=0");
}
} // namespace

int main(int argc, char** argv)
{
    const auto allocations = arp::core::defaultCpuAllocator().stats().allocationCount;
    if (argc == 2 && !std::strcmp(argv[1], "--validator-smoke"))
        validatorSmoke();
    else {
        assert(argc == 1);
        capacities();
        scopes();
        lowering();
    }
    assert(allocations == arp::core::defaultCpuAllocator().stats().allocationCount);
    std::puts("OriginalSubmissionRead/1 inspection passed; native receipt/execution unavailable");
}

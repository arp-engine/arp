#include "core/memory/cpu_allocator.h"
#include "rhi/frontend/compute_dispatch_validation.h"
#include "rhi/frontend/compute_dispatch_validation_domain.h"
#include "rhi/frontend/compute_dispatch_validation_preparation.h"
#include "rhi/frontend/compute_dispatch_validation_session.h"
#include "rhi/frontend/descriptor_buffer_write_validation.h"
#include "rhi/frontend/pipeline_creation_validation.h"
#include "rhi/frontend/pipeline_synchronous_compute_creation.h"
#include "rhi/frontend/pipeline_synchronous_graphics_creation.h"
#include "rhi/frontend/resource_loss_validation.h"
#include "rhi/semantics/command_lowering.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory_resource>
#include <type_traits>

#ifdef NDEBUG
#error Dispatch validation contract tests require assertions
#endif

namespace {
using namespace arp;
using namespace rhi;
using Contract = ComputeDispatchValidationContract;

ComputeDispatchValidationRequest request()
{
    return {1u,
        true,
        {8u, 8u, 1u},
        {160u, 90u, 1u},
        {5207040u, 14400u, 52743600u, 3375590400u, 208281600u, 160u, 30000000000u}};
}

void assertEmpty(const ComputeDispatchValidationPlan& p)
{
    assert(!p.workgroup.width && !p.workgroup.height && !p.workgroup.depth);
    assert(!p.groups.width && !p.groups.height && !p.groups.depth);
    assert(!p.dispatches && !p.workgroups && !p.threadInvocations && !p.resultCellsPerSlice
        && !p.resultSliceBytes && !p.nativeResultBytes && !p.resultChecks && !p.queueSubmissions);
}

void numeric()
{
    const auto valid = request();
    ComputeDispatchValidationPlan p;
    assert(!Contract::inspect(valid, p));
    assert(p.workgroup == valid.workgroup && p.groups == valid.groups);
    // Independent workload totals, including padding and all untouched cells.
    assert(p.dispatches == 14400u && p.workgroups == 52743600u && p.threadInvocations == 3375590400u
        && p.resultCellsPerSlice == 14400u && p.resultSliceBytes == 57856u
        && p.nativeResultBytes == 5207040u && p.resultChecks == 208281600u
        && p.queueSubmissions == 160u);
    const auto reject = [&](const ComputeDispatchValidationRequest& invalid, const char* expected) {
        assert(!Contract::inspect(valid, p));
        const auto* diagnostic = Contract::inspect(invalid, p);
        assert(diagnostic && !std::strcmp(diagnostic, expected));
        assertEmpty(p);
    };
    auto changed = valid;
    changed.contractVersion = 0u;
    reject(changed, "RHI-DISPATCH-VALIDATION-VERSION");
    changed.contractVersion = 2u;
    reject(changed, "RHI-DISPATCH-VALIDATION-VERSION");
    changed = valid;
    changed.prototypeOptIn = false;
    reject(changed, "RHI-DISPATCH-VALIDATION-OPT-IN");
    for (unsigned axis = 0u; axis < 3u; ++axis) {
        changed = valid;
        auto& dimension = axis == 0u ? changed.workgroup.width
            : axis == 1u             ? changed.workgroup.height
                                     : changed.workgroup.depth;
        dimension = 0u;
        reject(changed, "RHI-DISPATCH-VALIDATION-SHAPE");
        dimension = axis == 2u ? 2u : 9u;
        reject(changed, "RHI-DISPATCH-VALIDATION-SHAPE");
        changed = valid;
        (axis == 0u          ? changed.groups.width
                : axis == 1u ? changed.groups.height
                             : changed.groups.depth) = 0u;
        reject(changed, "RHI-DISPATCH-VALIDATION-GROUPS");
    }
    changed = valid;
    changed.workgroup = {4u, 16u, 1u}; // Same invocation total is not the verified shape.
    reject(changed, "RHI-DISPATCH-VALIDATION-SHAPE");
    changed = valid;
    changed.groups = {UINT32_MAX, UINT32_MAX, UINT32_MAX};
    reject(changed, "RHI-DISPATCH-VALIDATION-RESULT-CODE-OVERFLOW");
    changed.groups = {1u, 1u, static_cast<std::uint32_t>((UINT32_MAX - 64u) / 128u + 2u)};
    reject(changed, "RHI-DISPATCH-VALIDATION-RESULT-CODE-OVERFLOW");
    for (const auto timeout : {std::uint64_t(0u), UINT64_MAX}) {
        changed = valid;
        changed.budget.maxColdValidationNanoseconds = timeout;
        reject(changed, "RHI-DISPATCH-VALIDATION-TIMEOUT");
    }
    const char* diagnostics[]{"RHI-DISPATCH-VALIDATION-NATIVE-BUDGET",
        "RHI-DISPATCH-VALIDATION-DISPATCH-BUDGET",
        "RHI-DISPATCH-VALIDATION-WORKGROUP-BUDGET",
        "RHI-DISPATCH-VALIDATION-THREAD-BUDGET",
        "RHI-DISPATCH-VALIDATION-CHECK-BUDGET",
        "RHI-DISPATCH-VALIDATION-SUBMISSION-BUDGET"};
    for (unsigned i = 0u; i < 6u; ++i) {
        changed = valid;
        auto& b = changed.budget;
        std::uint64_t* fields[]{&b.maxNativeResultBytes,
            &b.maxDispatches,
            &b.maxWorkgroups,
            &b.maxThreadInvocations,
            &b.maxResultChecks,
            &b.maxQueueSubmissions};
        --*fields[i];
        reject(changed, diagnostics[i]);
        *fields[i] = 0u;
        reject(changed, diagnostics[i]);
    }
    assert(!Contract::contains(valid, {8u, 8u, 1u}, {1u, 1u, 1u}));
    assert(!Contract::contains(valid, {8u, 8u, 1u}, valid.groups));
    for (const auto groups : {Extent3D{161u, 90u, 1u},
             Extent3D{160u, 91u, 1u},
             Extent3D{160u, 90u, 2u},
             Extent3D{0u, 1u, 1u},
             Extent3D{1u, 0u, 1u},
             Extent3D{1u, 1u, 0u}})
        assert(!std::strcmp(Contract::contains(valid, {8u, 8u, 1u}, groups),
            "RHI-DISPATCH-VALIDATION-OUTSIDE-DOMAIN"));
    assert(!std::strcmp(
        Contract::contains(valid, {4u, 16u, 1u}, {1u, 1u, 1u}), "RHI-DISPATCH-VALIDATION-SHAPE"));
    changed = valid;
    changed.budget.maxDispatches = 0u;
    assert(Contract::contains(changed, valid.workgroup, valid.groups));
    // Small 3D domain independently exercises Z and the batch result layout.
    changed = valid;
    changed.groups = {3u, 2u, 3u};
    changed.budget = {1536u, 18u, 108u, 6912u, 1152u, 3u, 1u};
    assert(!Contract::inspect(changed, p));
    assert(p.dispatches == 18u && p.workgroups == 108u && p.threadInvocations == 6912u
        && p.nativeResultBytes == 1536u && p.resultChecks == 1152u && p.queueSubmissions == 3u);
    changed.groups = {1u, 1u, 1u};
    changed.budget = {256u, 1u, 1u, 64u, 64u, 1u, 1u};
    assert(!Contract::inspect(changed, p));
    assert(p.resultCellsPerSlice == 1u && p.resultSliceBytes == 256u && p.resultChecks == 64u);
    // The final representable cell uses index count-1, not count. Inspect only:
    // this enormous declaration is not executed by a native fixture.
    changed = valid;
    changed.groups = {33554432u, 1u, 1u};
    changed.budget = {UINT64_MAX,
        UINT64_MAX,
        UINT64_MAX,
        UINT64_MAX,
        UINT64_MAX,
        UINT64_MAX,
        std::uint64_t(INT64_MAX)};
    assert(!Contract::inspect(changed, p) && p.resultCellsPerSlice == 33554432u);
    assert(64u + 128u * (p.resultCellsPerSlice - 1u) <= UINT32_MAX);
    ++changed.groups.width;
    reject(changed, "RHI-DISPATCH-VALIDATION-RESULT-CODE-OVERFLOW");
    auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    for (unsigned i = 0u; i < 100u; ++i) {
        assert(!Contract::inspect(valid, p));
        assert(!Contract::contains(valid, valid.workgroup, valid.groups));
        assert(Contract::contains(valid, {1u, 1u, 1u}, valid.groups));
    }
    assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
    std::pmr::set_default_resource(previous);
}

void lowering()
{
    using Lowering = semantics::ComputeDispatchValidationLowering;
    const Lowering::Facts present{true, true, true, true, true};
    for (const auto backend : {Backend::Null,
             Backend::Vulkan,
             Backend::Metal,
             Backend::WebGPU,
             Backend::WebGL,
             static_cast<Backend>(255u)}) {
        const auto decision = Lowering::decide(backend, present);
        assert(!decision.domainIssued && !decision.executionAvailable);
        assert(decision.validatorOnly == (backend == Backend::Null));
        assert(decision.coldValidationEligible == (backend == Backend::Metal));
        assert(bool(decision.diagnostic) == !decision.coldValidationEligible);
    }
    const char* diagnostics[]{"RHI-DISPATCH-VALIDATION-DEVICE-LIFETIME",
        "RHI-DISPATCH-VALIDATION-NATIVE-DEVICE",
        "RHI-DISPATCH-VALIDATION-PROBE-CUSTODY",
        "RHI-DISPATCH-VALIDATION-COMPLETION",
        "RHI-DISPATCH-VALIDATION-PAYLOAD-MISSING"};
    for (unsigned i = 0u; i < 5u; ++i) {
        auto facts = present;
        bool* fields[]{&facts.originalDeviceCurrent,
            &facts.nativeDevice,
            &facts.ownedProbe,
            &facts.completionRetainsBudget,
            &facts.exhaustiveValidationPayload};
        *fields[i] = false;
        const auto decision = Lowering::decide(Backend::Metal, facts);
        assert(!decision.coldValidationEligible && !decision.domainIssued
            && !decision.executionAvailable && decision.diagnostic
            && !std::strcmp(decision.diagnostic, diagnostics[i]));
    }
    const auto empty = Lowering::decide(Backend::Metal, {});
    assert(!empty.coldValidationEligible && !empty.domainIssued && !empty.executionAvailable);
}

void domain()
{
    using Domain = ComputeDispatchValidationDomain;
    using DomainContract = ComputeDispatchValidationDomainContract;
    static_assert(std::is_final_v<Domain>);
    static_assert(!std::is_default_constructible_v<Domain>);
    static_assert(!std::is_copy_constructible_v<Domain>);
    auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    assert(!DomainContract::inspect({1u, true, 1024u}, 1024u));
    assert(DomainContract::inspect({0u, true, 1024u}, 1024u));
    assert(DomainContract::inspect({2u, true, 1024u}, 1024u));
    assert(DomainContract::inspect({1u, false, 1024u}, 1024u));
    assert(DomainContract::inspect({1u, true, 0u}, 0u));
    assert(DomainContract::inspect({1u, true, 1024u}, 1023u));
    assert(DomainContract::inspect({1u, true, 1024u}, 1025u));
    for (const auto backend : {Backend::Null,
             Backend::Vulkan,
             Backend::Metal,
             Backend::WebGPU,
             Backend::WebGL,
             static_cast<Backend>(255u)})
        assert(bool(DomainContract::issuanceDiagnostic(backend)) == (backend != Backend::Metal));
    assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
    std::pmr::set_default_resource(previous);
    std::puts(
        "COMPUTE_DISPATCH_DOMAIN_CONTRACT_PASS nonforgeable/explicit-budget/four-backends/allocation0; no recording authority");
}

void preparation()
{
    using Facade = OriginalComputeDispatchValidation;
    static_assert(std::is_final_v<Facade>);
    static_assert(!std::is_default_constructible_v<Facade>);
    static_assert(!std::is_copy_constructible_v<Facade>);
    const ComputeDispatchValidationSessionBudget b{
        1u, true, 1u, 65536u, 4096u, 65536u, 262144u, 65536u, 65536u, 65536u, 65536u};
    const ComputeDispatchValidationSessionQuotas q{core::makeShared<core::CpuMemoryQuota>(65536u),
        core::makeShared<core::CpuMemoryQuota>(65536u),
        core::makeShared<core::CpuMemoryQuota>(65536u),
        core::makeShared<core::CpuMemoryQuota>(65536u)};
    auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    assert(!ComputeDispatchValidationSessionContract::inspectQuotas(b, q));
    assert(ComputeDispatchValidationSessionContract::inspectQuotas(b, {}));
    auto overlap = q;
    overlap.input = q.session;
    assert(ComputeDispatchValidationSessionContract::inspectQuotas(b, overlap));
    for (const auto& quota : {q.session, q.artifact, q.input, q.pipeline}) {
        quota->setLimitBytes(65535u);
        assert(ComputeDispatchValidationSessionContract::inspectQuotas(b, q));
        quota->setLimitBytes(65536u);
        assert(!ComputeDispatchValidationSessionContract::inspectQuotas(b, q));
    }
    assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
    std::pmr::set_default_resource(previous);
    std::puts(
        "COMPUTE_DISPATCH_PREPARATION_CONTRACT_PASS final/nonforgeable/independent-quotas/allocation0; observations grant no proof");
}

void session()
{
    static_assert(!std::is_default_constructible_v<ComputeDispatchValidationReservation>);
    static_assert(!std::is_copy_constructible_v<ComputeDispatchValidationReservation>);
    for (const auto backend : {Backend::Null,
             Backend::Vulkan,
             Backend::Metal,
             Backend::WebGPU,
             Backend::WebGL,
             static_cast<Backend>(255u)})
        assert(bool(ComputeDispatchValidationSessionContract::reservationDiagnostic(backend))
            == (backend != Backend::Metal));
    auto intent = request();
    intent.groups = {3u, 2u, 3u};
    intent.budget = {1536u, 18u, 108u, 6912u, 1152u, 3u, 1000u};
    const ComputeDispatchValidationSessionBudget budget{
        1u, true, 1u, 65536u, 4096u, 65536u, 262144u, 65536u, 65536u, 65536u, 65536u};
    assert(!ComputeDispatchValidationSessionContract::inspect(intent, budget));
    auto invalid = budget;
    invalid.contractVersion = 2u;
    assert(ComputeDispatchValidationSessionContract::inspect(intent, invalid));
    invalid = budget;
    invalid.prototypeOptIn = false;
    assert(ComputeDispatchValidationSessionContract::inspect(intent, invalid));
    for (auto count : {0u, 2u}) {
        invalid = budget;
        invalid.maxPendingSubmissions = count;
        assert(ComputeDispatchValidationSessionContract::inspect(intent, invalid));
    }
    invalid = budget;
    invalid.maxNativeAllocationBytes = 1535u;
    assert(ComputeDispatchValidationSessionContract::inspect(intent, invalid));
    for (unsigned i = 0u; i < 7u; ++i) {
        invalid = budget;
        std::uint64_t* fields[]{&invalid.maxCpuMetadataBytes,
            &invalid.maxArtifactFileBytes,
            &invalid.maxArtifactTotalBytes,
            &invalid.maxArtifactCpuBytes,
            &invalid.maxInputCpuBytes,
            &invalid.maxPipelineCpuBytes,
            &invalid.maxPipelineTargetBytes};
        *fields[i] = 0u;
        assert(ComputeDispatchValidationSessionContract::inspect(intent, invalid));
    }
    auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    ComputeDispatchValidationProgress completed(intent);
    assert(completed.completeRow(true, 6u, 18u, 384u, 0u)); // No preceding submission.
    for (std::uint32_t row = 1u; row <= 3u; ++row) {
        assert(!completed.verified() && !completed.pending() && completed.nextRow() == row);
        assert(!completed.beginRow(row));
        assert(completed.pending() && completed.beginRow(row)); // No overlapping row.
        assert(!completed.completeRow(true, 6u, 18u * row, 384u, row));
        assert(!completed.pending() && completed.checkedRows() == row);
        assert(completed.completeRow(true, 6u, 18u * row, 384u, row));
    }
    assert(completed.verified() && completed.beginRow(3u));
    assert(!completed.observeDeadline(UINT64_MAX) && completed.verified());
    // A final timely completion remains valid after later wall-clock passage.
    for (unsigned reason = 0u; reason < 5u; ++reason) {
        ComputeDispatchValidationProgress failed(intent);
        assert(!failed.beginRow(0u));
        if (reason == 0u)
            assert(failed.observeDeadline(1000u)); // Equality is timeout, not success.
        if (reason == 1u)
            assert(failed.invalidate("RHI-DISPATCH-SESSION-CANCELLED"));
        if (reason == 2u)
            assert(failed.invalidate("RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST"));
        if (reason < 3u) {
            const auto* first = failed.diagnostic();
            assert(failed.pending() && failed.beginRow(1001u));
            assert(failed.invalidate("later failure") == first);
            assert(failed.completeRow(true, 6u, 18u, 384u, 1001u) == first);
        } else {
            assert(failed.completeRow(reason != 3u, 6u, 18u, 383u, 999u));
        }
        assert(!failed.pending() && !failed.verified() && !failed.checkedRows());
        assert(failed.beginRow(0u)); // Drain never rearms failed verification.
    }
    ComputeDispatchValidationProgress expired(intent);
    assert(expired.beginRow(1000u) && !expired.pending());
    ComputeDispatchValidationProgress late(intent);
    assert(!late.beginRow(999u));
    assert(late.completeRow(true, 6u, 18u, 384u, 1000u) && !late.verified());
    for (unsigned wrong = 0u; wrong < 3u; ++wrong) {
        ComputeDispatchValidationProgress partial(intent);
        assert(!partial.beginRow(0u));
        assert(partial.completeRow(
            true, wrong == 0u ? 5u : 6u, wrong == 1u ? 17u : 18u, wrong == 2u ? 383u : 384u, 1u));
        assert(!partial.verified() && !partial.checkedRows());
    }
    assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
    std::pmr::set_default_resource(previous);
    std::puts(
        "COMPUTE_DISPATCH_SESSION_CONTRACT_PASS ordered rows/pending/timeout/failure/drain/no-rearm/allocation0; no native authority");
}
} // namespace

void descriptorKindLimits()
{
    using C = OriginalDescriptorBufferBindingContract;
    using P = semantics::DescriptorWholeBufferBinding;
    const auto before = core::defaultCpuAllocator().stats().allocationCount;
    const C::Limits legacy{1u, 16u, 32u, 1024u, 2048u};
    assert(!C::validateLimits(legacy));
    for (const auto type : {DescriptorType::UniformBuffer,
             DescriptorType::StorageBuffer,
             DescriptorType::ReadOnlyStorageBuffer}) {
        assert(!C::validateBinding(type, 64u, 0u, legacy));
        DescriptorBufferBindingRangeContract::Resolved output;
        assert(!C::validateFiniteInterval(type, {2u, 32u, 64u}, 1024u, 32u, 64u, legacy, output));
        assert(output.offsetBytes == 64u && output.sizeBytes == 64u);
        assert(C::validateFiniteInterval(type, {2u, 1u, 64u}, 1024u, 0u, 64u, legacy, output));
        assert(!output.offsetBytes && !output.sizeBytes);
    }
    for (std::uint32_t mask = 0u; mask < 4u; ++mask) {
        const C::Limits limits{2u,
            mask & P::uniformKind ? 16u : 0u,
            mask & P::storageKind ? 32u : 0u,
            mask & P::uniformKind ? 1024u : 0u,
            mask & P::storageKind ? 2048u : 0u,
            1u,
            mask};
        assert(!C::validateLimits(limits));
        for (const auto type : {DescriptorType::UniformBuffer,
                 DescriptorType::StorageBuffer,
                 DescriptorType::ReadOnlyStorageBuffer}) {
            const bool present =
                mask & (type == DescriptorType::UniformBuffer ? P::uniformKind : P::storageKind);
            assert(bool(C::validateBinding(type, 64u, 0u, limits)) == !present);
            assert(C::validateBinding(type, 64u, 16u, limits));
            assert(C::validateBinding(type, UINT64_MAX, 0u, limits));
            DescriptorBufferBindingRangeContract::Resolved output{99u, 99u};
            const auto* id =
                C::validateFiniteInterval(type, {2u, 32u, 64u}, 1024u, 32u, 64u, limits, output);
            assert(bool(id) == !present);
            if (present)
                assert(output.offsetBytes == 64u && output.sizeBytes == 64u);
            else {
                assert(!std::strcmp(id, "RHI-ORIGINAL-DESCRIPTOR-BUFFER-FINITE-KIND-ABSENT"));
                assert(!output.offsetBytes && !output.sizeBytes);
            }
        }
        for (unsigned mode = 0u; mode < 10u; ++mode) {
            auto bad = limits;
            switch (mode) {
            case 0u:
                bad.contractVersion = 0u;
                break;
            case 1u:
                bad.contractVersion = 3u;
                break;
            case 2u:
                bad.bufferKindCoverageVersion = 0u;
                break;
            case 3u:
                bad.bufferKindCoverageVersion = 2u;
                break;
            case 4u:
                bad.bufferKindMask |= 4u;
                break;
            case 5u:
                bad.uniformOffsetAlignment = bad.uniformOffsetAlignment ? 0u : 1u;
                break;
            case 6u:
                bad.storageOffsetAlignment = bad.storageOffsetAlignment ? 0u : 1u;
                break;
            case 7u:
                bad.maxUniformBindingBytes = bad.maxUniformBindingBytes ? 0u : 1u;
                break;
            case 8u:
                bad.maxStorageBindingBytes = bad.maxStorageBindingBytes ? 0u : 1u;
                break;
            case 9u:
                bad.contractVersion = 1u;
                break;
            }
            assert(C::validateLimits(bad));
            assert(C::validateBinding(DescriptorType::UniformBuffer, 64u, 0u, bad));
            DescriptorBufferBindingRangeContract::Resolved output{99u, 99u};
            assert(C::validateFiniteInterval(
                DescriptorType::StorageBuffer, {2u, 0u, 64u}, 1024u, 0u, 64u, bad, output));
            assert(!output.offsetBytes && !output.sizeBytes);
        }
    }
    auto badLegacy = legacy;
    badLegacy.storageOffsetAlignment = 0u;
    assert(C::validateLimits(badLegacy));
    assert(C::validateBinding(DescriptorType::UniformBuffer, 64u, 0u, badLegacy));
    assert(!C::lowering(Backend::Null).executionAvailable);
    assert(before == core::defaultCpuAllocator().stats().allocationCount);
    std::puts(
        "DESCRIPTOR_BUFFER_LIMITS_V2_PASS both/uniform/storage/empty/unknown/contradiction/finiteAbsent/noModuloZero/allocation0 legacyPreserved=true nativeAuthority=false");
}

int main()
{
    descriptorKindLimits();
    {
        using Graphics = OriginalSynchronousGraphicsCreationContract;
        constexpr Graphics::Budget b{1u, true, 1024u, 32u, 4096u, 4096u};
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        for (auto backend : {Backend::Null,
                 Backend::Vulkan,
                 Backend::Metal,
                 Backend::WebGPU,
                 Backend::WebGL,
                 static_cast<Backend>(255u)}) {
            const auto decision = Graphics::lowering(backend, b, {true, true});
            const bool native = backend == Backend::Vulkan || backend == Backend::Metal;
            assert(decision.creationEligible == native && !decision.executionAvailable);
            assert(decision.validatorOnly == (backend == Backend::Null));
            assert(bool(decision.diagnostic) == !native);
            assert(Graphics::lowering(backend, {}, {true, true}).diagnostic);
            if (native) {
                assert(!std::strcmp(Graphics::lowering(backend, b, {false, true}).diagnostic,
                    "RHI-SYNC-GRAPHICS-CREATION-DEVICE"));
                assert(!std::strcmp(Graphics::lowering(backend, b, {true, false}).diagnostic,
                    "RHI-SYNC-GRAPHICS-CREATION-METHODS"));
            }
        }
        for (unsigned field = 0u; field < 6u; ++field) {
            auto bad = b;
            if (field == 0u)
                bad.contractVersion = 0u;
            if (field == 1u)
                bad.prototypeOptIn = false;
            if (field == 2u)
                bad.maxTargetBytes = 0u;
            if (field == 3u)
                bad.maxLabelBytes = 0u;
            if (field == 4u)
                bad.maxCpuOwnerBytes = 0u;
            if (field == 5u)
                bad.maxCpuNativeBytes = 0u;
            assert(Graphics::validateBudget(bad));
        }
        assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
        std::puts(
            "ORIGINAL_GRAPHICS_CREATION_LOWERING_PASS four backends; acceptedGraphics=false execution=false allocation0=true");
    }
    {
        using Creation = OriginalSynchronousComputeCreationContract;
        constexpr Creation::Budget b{1u, true, 1024u, 32u, 4096u, 4096u};
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        for (auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU, Backend::WebGL}) {
            const auto loss = OriginalTypedNativeLossJoinContract::lowering(backend, 3u);
            assert(loss.registrationAvailable
                == (backend == Backend::Vulkan || backend == Backend::Metal));
            const bool compute = backend == Backend::Vulkan || backend == Backend::Metal;
            assert(loss.acceptedPipelineJoinAvailable == compute);
            const auto accepted =
                OriginalAcceptedComputePipelineFactoryContract::lowering(backend, true, 2u);
            const auto source =
                semantics::OriginalCommandPipelineSourceLowering::lowering(backend, 2u, false);
            assert(accepted.factoryEligible == compute && !accepted.executionAvailable);
            assert(source.custodyAvailable == compute && !source.executionAvailable
                && !source.positionalAdapterAvailable);
            assert(!OriginalAcceptedComputePipelineFactoryContract::lowering(backend, false, 2u)
                    .factoryEligible);
            assert(OriginalAcceptedGraphicsPipelineFactoryContract::lowering(backend, true, 2u)
                       .factoryEligible
                == (backend == Backend::Vulkan || backend == Backend::Metal));
            assert(semantics::OriginalCommandPipelineSourceLowering::lowering(backend, 2u, true)
                       .custodyAvailable
                == (backend == Backend::Vulkan || backend == Backend::Metal));
            assert(!loss.scopedGroupJoinAvailable && !loss.recordingAvailable
                && !loss.executionAvailable);
            const auto decision = Creation::lowering(backend, b, {true, true});
            assert(decision.creationEligible
                == (backend == Backend::Vulkan || backend == Backend::Metal));
            assert(!decision.executionAvailable);
            assert(Creation::lowering(backend, {}, {true, true}).diagnostic);
            if (backend == Backend::Vulkan || backend == Backend::Metal) {
                assert(!std::strcmp(Creation::lowering(backend, b, {false, true}).diagnostic,
                    "RHI-SYNC-COMPUTE-CREATION-DEVICE"));
                assert(!std::strcmp(Creation::lowering(backend, b, {true, false}).diagnostic,
                    "RHI-SYNC-COMPUTE-CREATION-METHODS"));
            }
        }
        assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
        std::puts(
            "ORIGINAL_COMPUTE_CREATION_LOWERING_PASS four backends; creation grants no execution or accepted-wrapper authority");
        std::puts(
            "ORIGINAL_COMPUTE_ACCEPTANCE_LOWERING_PASS Vulkan/Metal compute and graphics custody; execution=false");
    }
    numeric();
    lowering();
    session();
    domain();
    preparation();
    std::puts(
        "COMPUTE_DISPATCH_VALIDATION_CONTRACT_PASS exact/one-less/zero/overflow/shape/coverage/allocation0/four-backends; no domain or execution authority");
}

#include "../fixtures/bindless_produced_program_fixture.h"
#include "rhi/frontend/bindless_root_callback.h"
#include "rhi/frontend/bindless_root_sequence.h"
#include "rhi/frontend/command_indirect_accesses.h"
#include "rhi/frontend/resource_state_sequence.h"
#include "rhi/semantics/bindless_root_program_lowering.h"
#include "rhi/semantics/command_state_lowering.h"

#include <cassert>
#include <limits>

#ifdef NDEBUG
#error Original Root indirect checks require assertions
#endif

int main()
{
    arp::test::BindlessProducedProgramFixture::verify();
    using namespace arp::rhi;
    using Access = OriginalCommandIndirectAccesses;
    using Lifecycle = OriginalCommandNativeLifecycleContract;
    using Completion = OriginalGeneralSubmissionCompletionContract;
    using Execution = OriginalBindlessRootNativeExecutionContract;
    static_assert(Execution::produced(15u) && Execution::produced(16u));
    static_assert(Execution::native(15u) && !Execution::native(16u));
    static_assert(
        Execution::lifecycleVersion(15u) == 21u && Execution::lifecycleVersion(16u) == 22u);
    static_assert(Execution::admissionVersion(15u) == 7u && Execution::admissionVersion(16u) == 7u);
    static_assert(Execution::closureVersion(15u) == 21u && Execution::closureVersion(16u) == 21u);
    static_assert(
        !Execution::validateIntent({15u, true}) && !Execution::validateIntent({16u, true}));
    using Idle = semantics::OriginalCommandNativeLifecycleLowering;
    static_assert(Idle::deviceIdleLowering(Backend::Vulkan, 21u).completionAvailable);
    static_assert(!Idle::deviceIdleLowering(Backend::Vulkan, 22u).completionAvailable);
    static_assert(!Idle::deviceIdleLowering(Backend::Metal, 21u).completionAvailable);
    static_assert(!Idle::deviceIdleLowering(Backend::WebGPU, 21u).completionAvailable);
    static_assert(!Idle::deviceIdleLowering(Backend::Null, 21u).completionAvailable);
    static_assert(
        Execution::validateIntent({17u, true}) && Execution::validateIntent({16u, false}));

    // The original readback request's finite interval is also the native map
    // interval. Its allocation identity remains independent from result size.
    using MapRange = BufferReadbackRangeContract::Range;
    static_assert(!MapRange{32u, 8u, 8u}.diagnostic());
    static_assert(!MapRange{32u, 24u, 8u}.diagnostic());
    static_assert(MapRange{32u, 4u, 8u}.diagnostic());
    static_assert(MapRange{32u, 8u, 6u}.diagnostic());
    static_assert(MapRange{32u, 32u, 4u}.diagnostic());
    static_assert(MapRange{32u, 8u, 0u}.diagnostic());
    static_assert(MapRange{UINT64_MAX, UINT64_MAX - 7u, 8u}.diagnostic());
    static_assert(MapRange{32u, 8u, 8u}.matches({32u, 8u, 8u}));
    static_assert(!MapRange{32u, 8u, 8u}.matches({32u, 0u, 8u}));
    static_assert(!MapRange{32u, 8u, 8u}.matches({16u, 8u, 8u}));

    // Cross-check the selector against independent lifecycle/content and
    // transaction completion contracts, including preservation of old domains.
    for (std::uint32_t lifecycle = 6u; lifecycle <= 22u; ++lifecycle) {
        if (lifecycle == 7u || lifecycle == 10u)
            continue;
        const auto profile = OriginalBindlessRootCompletionProfile::inspect(lifecycle);
        assert(!profile.diagnostic && Lifecycle::isFenceProfile(lifecycle) != profile.callbacks);
        OriginalCommandNativeLifecycleBudget budget;
        budget.contractVersion = lifecycle;
        std::uint32_t accepted = 0u;
        for (std::uint32_t content = 0u; content <= 19u; ++content) {
            if (Lifecycle::validateContentProfile(budget, content))
                continue;
            ++accepted;
            assert(!Completion::validateProfile(profile.submissionVersion,
                false,
                false,
                content,
                lifecycle != 6u && lifecycle != 11u,
                lifecycle == 21u || lifecycle == 22u));
        }
        assert(accepted);
    }
    for (const auto lifecycle : {0u, 1u, 2u, 3u, 4u, 5u, 7u, 10u, 23u, UINT32_MAX})
        assert(OriginalBindlessRootCompletionProfile::inspect(lifecycle).diagnostic);
    // The produced geometry profile is a separate completion domain. Neither
    // a legacy Graphics Content9 nor a caller-selected submission number can
    // acquire it. These checks grant no native recording authority.
    assert(!Completion::validateProfile(15u, false, false, 9u, true, true));
    for (std::uint32_t version = 0u; version <= 15u; ++version) {
        assert(bool(Completion::validateProfile(version, false, false, 9u, true, true))
            == (version != 15u));
        assert(Completion::validateProfile(15u, false, false, version, true, false));
        if (version != 9u)
            assert(Completion::validateProfile(15u, false, false, version, true, true));
    }
    for (const bool whole : {false, true})
        for (const bool finite : {false, true})
            for (const bool root : {false, true})
                assert(bool(Completion::validateProfile(15u, whole, finite, 9u, root, true))
                    == (whole || finite || !root));
    for (const auto version : {21u, 22u}) {
        OriginalCommandNativeLifecycleBudget lifecycle;
        lifecycle.contractVersion = version;
        lifecycle.prototypeOptIn = true;
        lifecycle.maxPendingSubmissions = 1u;
        lifecycle.maxRecordingOwners = lifecycle.maxSubmissionOwners = 64u;
        lifecycle.maxRecordingCpuBytes = lifecycle.maxSubmissionCpuBytes = 65536u;
        if (version == 22u) {
            lifecycle.maxCallbackTickets = lifecycle.maxCallbackRegistrations = 13u;
            lifecycle.maxCallbackOwners = 64u;
            lifecycle.maxCallbackCpuBytes = 65536u;
        }
        assert(Lifecycle::isFenceProfile(version) == (version == 21u));
        assert(!Lifecycle::validateIntent(lifecycle));
        assert(!Lifecycle::validateContentProfile(lifecycle, 9u));
        for (const auto content : {0u, 8u, 18u, 19u})
            assert(Lifecycle::validateContentProfile(lifecycle, content));
        const OriginalCommandLifecycleCapacityRequest capacity{version == 21u ? 11u : 6u,
            true,
            1u,
            lifecycle.maxCallbackTickets,
            lifecycle.maxCallbackRegistrations};
        assert(!Lifecycle::validateCapacityRequest(capacity, lifecycle));
        auto wrong = capacity;
        wrong.contractVersion = version == 21u ? 5u : 11u;
        assert(Lifecycle::validateCapacityRequest(wrong, lifecycle));
        wrong = capacity;
        wrong.callbackTickets = version == 21u ? 1u : 0u;
        assert(Lifecycle::validateCapacityRequest(wrong, lifecycle));
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU, Backend::WebGL}) {
            const auto lowering =
                semantics::OriginalCommandNativeLifecycleLowering::lowering(backend, version);
            assert(!lowering.sealedIssuerAvailable && !lowering.executionAvailable
                && lowering.diagnostic);
        }
    }
    OriginalCommandBufferReadbackBindingBudget resultBinding{5u, true, 5u, 1u, 65536u};
    assert(!OriginalCommandBufferReadbackBinding::validateBudget(resultBinding));
    for (const auto backend : {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
        const auto readback =
            semantics::OriginalBufferReadbackBindingLowering::lowering(backend, 5u);
        assert(readback.cpuMetadataImplemented && readback.pendingAssociationImplemented
            && !readback.nativeExecutionImplemented && !readback.diagnostic);
    }
    assert(
        semantics::OriginalBufferReadbackBindingLowering::lowering(Backend::WebGL, 5u).diagnostic);
    {
        const arp::core::pmr::Vector<VertexBufferLayoutDesc> layouts{
            {0u, 16u, VertexInputRate::Vertex, {{0u, 0u, VertexFormat::Float32x4}}}};
        const VertexInputConsumptionContract input(layouts);
        arp::shader::ShaderReflection reflection;
        assert(!validateReflectedVertexInput(input, reflection).success);
        reflection.vertexAttributes.push_back(
            {"position", "POSITION", arp::shader::VertexAttributeFormat::Float4, 0u});
        assert(validateReflectedVertexInput(input, reflection).success);
        reflection.vertexAttributes[0].location = 1u;
        assert(!validateReflectedVertexInput(input, reflection).success);
        reflection.vertexAttributes[0].location = 0u;
        reflection.vertexAttributes[0].format = arp::shader::VertexAttributeFormat::Float3;
        assert(!validateReflectedVertexInput(input, reflection).success);
        reflection.vertexAttributes[0].format = arp::shader::VertexAttributeFormat::Float4;
        reflection.vertexAttributes.push_back(reflection.vertexAttributes[0]);
        assert(!validateReflectedVertexInput(input, reflection).success);
    }
    // Independent WebGPU Geometry/1 never broadens legacy Graphics/2. Each
    // opt-in, budget and backend mapping must be explicit before preparation.
    using Closure = OriginalBindlessRootNativeClosureContract;
    OriginalBindlessRootNativeClosureBudget geometry{5u, true, 4u, 8u, 16u, 65536u};
    geometry.maxAttachmentViews = 1u;
    assert(!Closure::validateBudget(geometry) && !Closure::hasInitializedGeometry(geometry));
    geometry.initializedGeometryContractVersion = 1u;
    assert(Closure::validateBudget(geometry));
    geometry.initializedGeometryOptIn = true;
    assert(Closure::validateBudget(geometry));
    geometry.maxInitializedGeometryActors = 2u;
    geometry.maxInitializedGeometryBytes = 72u;
    assert(!Closure::validateBudget(geometry) && Closure::hasInitializedGeometry(geometry));
    for (const auto backend : {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
        assert(bool(Closure::validateGeometryBackend(backend, geometry))
            == (backend == Backend::Vulkan || backend == Backend::Metal));
        const auto state =
            semantics::OriginalCommandStateAdmissionLowering::bindlessRootLowering(backend, 5u);
        assert(state.projectionAvailable && !state.executionAvailable && !state.diagnostic);
    }
    for (const auto version : {0u, 1u, 2u, 3u, 7u}) {
        auto wrong = geometry;
        wrong.contractVersion = version;
        assert(Closure::validateBudget(wrong));
    }
    auto wrongGeometry = geometry;
    wrongGeometry.initializedGeometryOptIn = false;
    assert(Closure::validateBudget(wrongGeometry));
    wrongGeometry = geometry;
    wrongGeometry.maxInitializedGeometryActors = geometry.maxResourceEntries + 1u;
    assert(Closure::validateBudget(wrongGeometry));
    using Program = OriginalBindlessRootProgramContract;
    OriginalBindlessRootProgramRequest program{2u, true, 32u, 256u};
    assert(!Program::validateIntent(program));
    for (const auto kind : {CommandUsageKind::BindVertexBuffer,
             CommandUsageKind::BindIndexBuffer,
             CommandUsageKind::DrawIndexed})
        assert(Program::validateOperation(kind, program));
    program.initializedGeometryContractVersion = 1u;
    assert(Program::validateIntent(program));
    program.initializedGeometryOptIn = true;
    assert(!Program::validateIntent(program));
    for (const auto kind : {CommandUsageKind::BindVertexBuffer,
             CommandUsageKind::BindIndexBuffer,
             CommandUsageKind::DrawIndexed})
        assert(!Program::validateOperation(kind, program));
    assert(Program::validateOperation(CommandUsageKind::Draw, program));
    // Private indirect emission alone cannot authorize Root6. Until its full
    // producer/content/completion profile exists, every public legacy program
    // refuses it, including an opted-in initialized Geometry/1 request.
    assert(Program::validateOperation(CommandUsageKind::DrawIndexedIndirect, program));
    for (std::uint32_t version = 1u; version <= 6u; ++version) {
        const OriginalBindlessRootProgramRequest legacy{version, true, 32u, 256u};
        assert(!Program::validateIntent(legacy));
        assert(Program::validateOperation(CommandUsageKind::DrawIndexedIndirect, legacy));
    }
    OriginalBindlessRootProgramRequest producedProgram{7u, true, 64u, 512u};
    assert(Program::validateIntent(producedProgram));
    producedProgram.initializedGeometryContractVersion = 2u;
    producedProgram.initializedGeometryOptIn = true;
    assert(!Program::validateIntent(producedProgram));
    for (const auto kind : {CommandUsageKind::BeginComputePass,
             CommandUsageKind::BindPreparedComputeBinding,
             CommandUsageKind::Dispatch,
             CommandUsageKind::EndComputePass,
             CommandUsageKind::BeginRenderPass,
             CommandUsageKind::BindPreparedGraphicsBinding,
             CommandUsageKind::BindVertexBuffer,
             CommandUsageKind::BindIndexBuffer,
             CommandUsageKind::DrawIndexedIndirect,
             CommandUsageKind::EndRenderPass,
             CommandUsageKind::BeginTransferPass,
             CommandUsageKind::BufferCopy,
             CommandUsageKind::EndTransferPass,
             CommandUsageKind::Finish})
        assert(!Program::validateOperation(kind, producedProgram));
    for (const auto kind : {CommandUsageKind::Draw,
             CommandUsageKind::DrawIndexed,
             CommandUsageKind::DrawIndexedIndirectCount,
             CommandUsageKind::DispatchIndirect,
             CommandUsageKind::PushConstants,
             CommandUsageKind::BindRenderDescriptor})
        assert(Program::validateOperation(kind, producedProgram));
    assert(Program::validateOperation(CommandUsageKind::DrawIndexedIndirect, 7u));
    auto wrongProducedProgram = producedProgram;
    wrongProducedProgram.initializedGeometryContractVersion = 1u;
    assert(Program::validateIntent(wrongProducedProgram));
    wrongProducedProgram = producedProgram;
    wrongProducedProgram.initializedGeometryOptIn = false;
    assert(Program::validateIntent(wrongProducedProgram));
    using Cold = OriginalBindlessRootCallbackColdContract;
    assert(Cold::matchesCompletionProfile(22u, 7u, 9u));
    assert(!Cold::matchesCompletionProfile(12u, 7u, 9u)
        && !Cold::matchesCompletionProfile(22u, 2u, 9u)
        && !Cold::matchesCompletionProfile(22u, 7u, 19u));
    for (const auto backend :
        {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU, Backend::WebGL}) {
        const auto programLowering =
            semantics::OriginalBindlessRootProgramLowering::lowering(backend, 7u);
        const auto coldLowering =
            semantics::OriginalBindlessRootCallbackColdLowering::lowering(backend, 7u);
        assert(!programLowering.preparationAvailable && !programLowering.executionAvailable
            && !programLowering.completionAvailable && programLowering.diagnostic
            && !coldLowering.preparationAvailable && coldLowering.diagnostic);
    }
    auto wrongProgram = program;
    wrongProgram.contractVersion = 1u;
    assert(Program::validateIntent(wrongProgram));
    BufferDesc buffer{"indirect-addresses",
        64u,
        ResourceUsage::StorageBuffer | ResourceUsage::IndirectBuffer,
        MemoryUsage::GpuOnly,
        ResourceState::ShaderWrite};
    std::uint64_t extent = 0u;
    assert(!Access::inspectRange(buffer, {4u, 2u, 24u}, extent) && extent == 44u);
    assert(!Access::inspectRange(buffer, {44u, 1u, 20u}, extent) && extent == 20u);
    for (const auto draw : {CommandScalarArguments::DrawIndirect{0u, 0u, 20u},
             {1u, 1u, 20u},
             {0u, 1u, 16u},
             {0u, 1u, 22u},
             {48u, 1u, 20u},
             {4u, 3u, 24u},
             {UINT64_MAX - 3u, 1u, 20u},
             {0u, UINT32_MAX, UINT32_MAX - 3u}}) {
        extent = UINT64_MAX;
        assert(Access::inspectRange(buffer, draw, extent) && !extent);
    }
    buffer.usage = ResourceUsage::StorageBuffer;
    assert(Access::inspectRange(buffer, {0u, 1u, 20u}, extent) && !extent);
    buffer.usage = ResourceUsage::IndirectBuffer;
    buffer.sizeBytes = UINT64_MAX;
    assert(!Access::inspectRange(buffer, {UINT64_MAX - 23u, 1u, 20u}, extent) && extent == 20u);
    OriginalBindlessRootStateBudget budget{6u, true, 1u, 1u, 1u, 1u, 1u};
    assert(budget.diagnostic());
    budget.maxIndirectCalls = 1u;
    budget.maxIndirectRecords = 2u;
    budget.maxDependencyVisits = 1u;
    assert(!budget.diagnostic() && budget.geometryJoined());
    auto changed = budget;
    --changed.maxIndirectRecords;
    assert(!changed.matches(budget));
    changed = budget;
    changed.contractVersion = 5u;
    assert(changed.diagnostic());
    changed.maxIndirectCalls = changed.maxIndirectRecords = changed.maxDependencyVisits = 0u;
    assert(!changed.diagnostic());
    assert(Access::visit({}, budget, [](const auto&) noexcept -> const char* {
        return nullptr;
    }).diagnostic);
    OriginalCommandInputsBudget inputs{8u, true, 5u};
    inputs.maxIndexBindings = 1u;
    assert(!OriginalCommandPipelineBindingContract::validateBudget(inputs));
    inputs.maxDescriptorBindings = 1u;
    assert(OriginalCommandPipelineBindingContract::validateBudget(inputs));
    for (const auto backend : {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
        const auto rootSequence = semantics::OriginalBindlessRootSequenceLowering::current(backend);
        assert(rootSequence.projectionAvailable && !rootSequence.nativeExecutionAvailable
            && !rootSequence.diagnostic);
        const auto state =
            semantics::OriginalCommandStateAdmissionLowering::bindlessRootLowering(backend, 6u);
        const auto stream =
            semantics::OriginalCommandStreamLowering::bindlessRootLowering(backend, 6u);
        assert(state.projectionAvailable && !state.executionAvailable && !state.diagnostic
            && !stream.nativeExecutionAvailable && stream.diagnostic);
    }
    assert(
        semantics::OriginalCommandStateAdmissionLowering::bindlessRootLowering(Backend::WebGL, 6u)
            .diagnostic);

    const semantics::ResourceAccessScope write{
        ResourceState::ShaderWrite, PipelineStage::ComputeShader, ResourceAccess::ShaderWrite};
    const semantics::ResourceAccessScope indirect{ResourceState::ShaderRead,
        PipelineStage::DrawIndirect,
        ResourceAccess::IndirectCommandRead};
    const semantics::ResourceAccessScope vertex{
        ResourceState::ShaderRead, PipelineStage::VertexShader, ResourceAccess::ShaderRead};
    const semantics::ResourceAccessScope both{ResourceState::ShaderRead,
        PipelineStage::DrawIndirect | PipelineStage::VertexShader,
        ResourceAccess::IndirectCommandRead | ResourceAccess::ShaderRead};
    semantics::RecordedResourceAccessDependencies dependencies;
    assert(!dependencies.consume(write));
    assert(dependencies.consume(indirect));
    assert(dependencies.consume(write));
    assert(dependencies.depend(indirect, both));
    assert(!dependencies.depend(write, indirect));
    assert(dependencies.consume(vertex));
    assert(!dependencies.consume(indirect));
    assert(!dependencies.depend(indirect, both));
    assert(!dependencies.consume(vertex));
    assert(!dependencies.consume(indirect));
    assert(dependencies.consume(write));
    assert(dependencies.depend(vertex, write));
    assert(!dependencies.depend(both, write));
    assert(!dependencies.consume(write));

    // A read->write dependency also has to name the real destination. There
    // need not have been an earlier write for its destination scope to matter.
    semantics::RecordedResourceAccessDependencies readFirst;
    assert(!readFirst.consume(vertex));
    assert(!readFirst.depend(vertex, indirect));
    assert(readFirst.consume(write));
    assert(!readFirst.consume(indirect));
    assert(!readFirst.depend(indirect, write));
    assert(!readFirst.consume(write));

    semantics::RecordedResourceAccessDependencies chained;
    assert(!chained.consume(write));
    assert(!chained.depend(write, indirect));
    assert(chained.depend(vertex, vertex));
    assert(!chained.depend(indirect, vertex));
    assert(!chained.consume(vertex));
}

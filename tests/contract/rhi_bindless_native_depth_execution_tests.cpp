#include "../fixtures/canonical_graphics_depth_contract_fixture.h"
#include "../fixtures/canonical_graphics_depth_execution_contract_fixture.h"
#include "../fixtures/completed_depth_write_return_contract_fixture.h"
#include "../fixtures/native_allocation_observation_contract_fixture.h"
#include "../fixtures/ordinary_depth_pipeline_contract_fixture.h"
#include "../fixtures/original_bindless_graphics_depth_content_contract_fixture.h"
#include "rhi/frontend/bindless_graphics_execution.h"
#include "rhi/frontend/bindless_root_fence_execution.h"
#include "rhi/semantics/bindless_ordinary_depth_pipeline.h"
#include "rhi/semantics/bindless_original_execution_lowering.h"

#include <cassert>

int main()
{
    arp::test::NativeAllocationObservationContractFixture::verify();
    arp::test::CompletedDepthWriteReturnContractFixture::verify();
    arp::test::CanonicalGraphicsDepthExecutionContractFixture::verify();
    arp::test::OriginalBindlessGraphicsDepthContentContractFixture::verify();
    arp::test::OriginalBindlessGraphicsDepthContentContractFixture::verifyCanonical();
    arp::test::OrdinaryDepthPipelineContractFixture::verify();
    arp::test::CanonicalGraphicsDepthContractFixture::verify();
    using namespace arp::rhi;
    using DepthPipeline = semantics::OriginalBindlessOrdinaryDepthPipelineDeclaration;
    DescriptorBindingDesc depthDeclaration;
    depthDeclaration.type = DescriptorType::SampledTexture;
    depthDeclaration.arrayCount = 1u;
    depthDeclaration.stages = ShaderStage::Fragment;
    depthDeclaration.depthTexture = true;
    depthDeclaration.textureViewDimensionConstrained = true;
    depthDeclaration.textureViewDimension = TextureViewDimension::Texture2D;
    for (const auto backend : {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
        const auto declaration = DepthPipeline::lowering(backend);
        const bool accepted = true;
        assert(declaration.declarationAvailable == accepted
            && declaration.validatorOnly == (backend == Backend::Null)
            && !declaration.nativeClosureAvailable && !declaration.executionAvailable);
        assert(bool(DepthPipeline::inspect(backend, depthDeclaration)) == !accepted);
        assert(DepthPipeline::lowering(backend, 0u).diagnostic
            && DepthPipeline::lowering(backend, 2u).diagnostic);
    }
    for (const auto stage : {ShaderStage::Vertex, ShaderStage::Vertex | ShaderStage::Fragment}) {
        auto invalid = depthDeclaration;
        invalid.stages = stage;
        assert(DepthPipeline::inspect(Backend::Vulkan, invalid));
        assert(DepthPipeline::inspect(Backend::Metal, invalid));
    }
    auto computeDeclaration = depthDeclaration;
    computeDeclaration.stages = ShaderStage::Compute;
    assert(!DepthPipeline::inspect(Backend::Vulkan, computeDeclaration));
    assert(DepthPipeline::inspect(Backend::Metal, computeDeclaration));
    assert(DepthPipeline::inspect(Backend::WebGPU, computeDeclaration));
    assert(!DepthPipeline::lowering(Backend::WebGL).declarationAvailable);
    using DepthInput = OriginalCompletedDepthReadInputContract;
    using SetSource = OriginalCommandDescriptorSetSource;
    const DepthInput::Budget ordinaryDepth{
        3u, true, 1u, 16u, OriginalCompletedDepthTextureSource::completeRetainedOwnerCount};
    assert(!DepthInput::validateIntent(ordinaryDepth));
    OriginalBindlessRootNativeClosureBudget ordinaryClosure{16u, true, 9u, 32u, 128u, 65536u};
    ordinaryClosure.completedDepthInputs = ordinaryDepth;
    assert(!OriginalBindlessRootNativeClosureContract::validateBudget(ordinaryClosure));
    for (const auto version : {0u, 1u, 2u, 4u}) {
        auto wrong = ordinaryClosure;
        wrong.completedDepthInputs.contractVersion = version;
        assert(OriginalBindlessRootNativeClosureContract::validateBudget(wrong));
    }
    for (const auto version : {0u, 1u, 9u, 13u, 14u, 15u, 17u}) {
        auto wrong = ordinaryClosure;
        wrong.contractVersion = version;
        assert(OriginalBindlessRootNativeClosureContract::validateBudget(wrong));
    }
    {
        auto wrong = ordinaryClosure;
        wrong.maxAttachmentViews = 1u;
        assert(OriginalBindlessRootNativeClosureContract::validateBudget(wrong));
        wrong = ordinaryClosure;
        wrong.prototypeOptIn = false;
        assert(OriginalBindlessRootNativeClosureContract::validateBudget(wrong));
    }
    for (const auto backend :
        {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU, Backend::WebGL}) {
        const auto decision =
            semantics::OriginalBindlessRootNativeClosureLowering::lowering(backend, 16u);
        assert(decision.contractVersion == 16u && !decision.executionAvailable
            && decision.validatorOnly == (backend == Backend::Null)
            && decision.nativeCorrespondenceAvailable
                == (backend == Backend::Vulkan || backend == Backend::Metal)
            && bool(decision.diagnostic)
                == (backend != Backend::Vulkan && backend != Backend::Metal));
    }
    {
        OriginalBindlessGraphicsExecutionBudget execution;
        execution.intent = {1u, true};
        execution.closure = {1u, true, 9u, 32u, 128u, 65536u};
        execution.pass = {1u, true, 1u, 1u, 1u, 65536u};
        execution.lifecycle = {9u, true, 1u, 0u, 0u, 1u, 1u, 0u, 65536u, 65536u, 0u};
        execution.capacity = {5u, true, 1u, 0u, 0u};
        execution.maxCpuViewBytes = 65536u;
        execution.maxDiagnosticBytes = 256u;
        assert(!OriginalBindlessGraphicsExecutionContract::validateBudget(execution));
        execution.closure = ordinaryClosure;
        const auto* id = OriginalBindlessGraphicsExecutionContract::validateBudget(execution);
        assert(id && arp::core::StringView(id) == "RHI-BINDLESS-GRAPHICS-EXECUTION-PROFILE");
    }
    for (const auto stages : {PipelineStage::FragmentShader,
             PipelineStage::ComputeShader,
             PipelineStage::FragmentShader | PipelineStage::ComputeShader})
        assert(!DepthInput::validateAccess(
            ResourceState::ShaderRead, stages, ResourceAccess::ShaderRead, 3u));
    for (const auto stages : {PipelineStage::None,
             PipelineStage::VertexShader,
             PipelineStage::Transfer,
             PipelineStage::FragmentShader | PipelineStage::VertexShader})
        assert(DepthInput::validateAccess(
            ResourceState::ShaderRead, stages, ResourceAccess::ShaderRead, 3u));
    OriginalCommandDescriptorSourceBudget sourceBudget{
        6u, true, 1u, 1u, SetSource::canonicalExclusiveRetainedOwnerCount, 65536u};
    sourceBudget.completedDepthInputs = ordinaryDepth;
    assert(!SetSource::validateBudget(sourceBudget));
    assert(OriginalCommandDescriptorTableSource::validateBudget(sourceBudget));
    // Defining Input3 must not expand the earlier mixed cold Closure13 schema.
    OriginalBindlessRootNativeClosureBudget mixedCold{13u, true, 1u, 1u, 1u, 65536u};
    mixedCold.completedDepthInputs = ordinaryDepth;
    assert(OriginalBindlessRootNativeClosureContract::validateBudget(mixedCold));
    for (const auto version : {1u, 2u}) {
        mixedCold.completedDepthInputs.contractVersion = version;
        assert(!OriginalBindlessRootNativeClosureContract::validateBudget(mixedCold));
    }
    auto badSource = sourceBudget;
    badSource.contractVersion = 2u;
    assert(SetSource::validateBudget(badSource));
    badSource = sourceBudget;
    badSource.completedDepthInputs.maxCells = 0u;
    assert(SetSource::validateBudget(badSource));
    badSource = sourceBudget;
    --badSource.maxExclusiveRetainedOwners;
    assert(SetSource::validateBudget(badSource));
    assert(OriginalCommandResourceSources::allowsDescriptorSourceVersion(6u, 6u));
    for (std::uint32_t assembly = 0u; assembly < 6u; ++assembly)
        assert(!OriginalCommandResourceSources::allowsDescriptorSourceVersion(assembly, 6u));
    for (const auto backend : {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
        const auto input = DepthInput::lowering(backend, 3u);
        assert(!input.executionAvailable && input.validatorOnly == (backend == Backend::Null));
        assert(input.nativeInputValidationAvailable
            == (backend == Backend::Vulkan || backend == Backend::Metal));
        const auto source =
            semantics::OriginalCommandDescriptorSourceLowering::completedDepthLowering(backend);
        assert(source.freezeAvailable && !source.executionAvailable
            && !source.positionalAdapterAvailable && !source.diagnostic);
    }
    assert(
        semantics::OriginalCommandDescriptorSourceLowering::completedDepthLowering(Backend::WebGL)
            .diagnostic);
    using Read = OriginalCompletedDepthRootReadPlanContract;
    using Life = OriginalCommandNativeLifecycleContract;
    using Completion = OriginalGeneralSubmissionCompletionContract;
    {
        // Independent ordinary Graphics depth profile. Earlier Compute and
        // color-only versions cannot acquire this journal/completion authority.
        const OriginalCommandNativeLifecycleBudget life{
            19u, true, 1u, 0u, 0u, 32u, 32u, 0u, 65536u, 65536u, 0u};
        assert(!Life::validateIntent(life) && Life::isFenceProfile(19u));
        assert(!Life::validateContentProfile(life, 18u));
        assert(!Life::validateCapacityRequest({10u, true, 1u, 0u, 0u}, life));
        for (std::uint32_t v = 0u; v <= 19u; ++v) {
            if (v != 18u)
                assert(Life::validateContentProfile(life, v));
            if (v != 19u) {
                auto old = life;
                old.contractVersion = v;
                assert(Life::validateContentProfile(old, 18u));
                assert(Read::validatePendingProfile(3u, v, 18u, true, true));
            }
            if (v != 10u)
                assert(Life::validateCapacityRequest({v, true, 1u, 0u, 0u}, life));
            if (v != 13u)
                assert(Completion::validateProfile(v, false, false, 18u, true));
        }
        assert(!Read::validatePendingProfile(3u, 19u, 18u, true, true));
        assert(Read::validatePendingProfile(3u, 19u, 18u, false, true));
        assert(Read::validatePendingProfile(3u, 19u, 18u, true, false));
        assert(Read::validatePendingProfile(2u, 19u, 18u, true, true));
        using Color = OriginalCompletedTextureReadPlanContract;
        for (const auto read : {0u, 2u}) {
            assert(!Color::validatePendingProfile(8u, true, false, 18u, false, true, read));
            assert(Color::validatePendingProfile(8u, true, false, 9u, false, true, read));
            assert(Color::validatePendingProfile(8u, true, false, 18u, false, false, read));
        }
        assert(Color::validatePendingProfile(8u, true, false, 18u, false, true, 1u));
        assert(!Color::validatePendingStates(ResourceState::ShaderRead,
            ResourceState::ShaderRead,
            ResourceState::ShaderRead,
            ResourceState::ShaderRead,
            5u,
            5u,
            8u));
        assert(Color::validatePendingStates(ResourceState::ShaderRead,
            ResourceState::ShaderRead,
            ResourceState::ShaderRead,
            ResourceState::ShaderRead,
            5u,
            6u,
            8u));
        assert(!Completion::validateProfile(13u, false, false, 18u, true));
        assert(Completion::validateProfile(13u, true, false, 18u, true));
        assert(Completion::validateProfile(13u, false, false, 18u, false));
        OriginalBindlessGraphicsExecutionBudget execution;
        execution.intent = {2u, true};
        execution.closure = ordinaryClosure;
        execution.pass = {1u, true, 1u, 1u, 1u, 65536u};
        execution.lifecycle = life;
        execution.capacity = {10u, true, 1u, 0u, 0u};
        execution.maxCpuViewBytes = 65536u;
        execution.maxDiagnosticBytes = 256u;
        assert(!OriginalBindlessGraphicsExecutionContract::validateBudget(execution));
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU, Backend::WebGL}) {
            const bool native = backend == Backend::Vulkan || backend == Backend::Metal;
            const auto lowering =
                semantics::OriginalCommandNativeLifecycleLowering::lowering(backend, 19u);
            assert(
                lowering.sealedIssuerAvailable == native && lowering.executionAvailable == native);
            const auto graphics =
                semantics::OriginalBindlessGraphicsExecutionLowering::lowering(backend, 2u);
            assert(graphics.executionAvailable == native && graphics.completionAvailable == native);
            assert(bool(graphics.diagnostic) == !native);
        }
    }
    using Execution = OriginalBindlessRootNativeExecutionContract;
    for (const auto backend : {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
        const bool nativeDepth = backend == Backend::Vulkan || backend == Backend::Metal;
        const auto execution = semantics::OriginalBindlessExecutionLowering::lowering(backend, 6u);
        assert(execution.executionAvailable == nativeDepth);
        assert(execution.completionAvailable == nativeDepth);
        assert(bool(execution.diagnostic) == !nativeDepth);
        const auto lifecycle =
            semantics::OriginalCommandNativeLifecycleLowering::lowering(backend, 14u);
        assert(lifecycle.executionAvailable == nativeDepth);
        assert(lifecycle.sealedIssuerAvailable == nativeDepth);
        const auto idle =
            semantics::OriginalCommandNativeLifecycleLowering::deviceIdleLowering(backend, 14u);
        assert(idle.completionAvailable == (backend == Backend::Vulkan));
    }
    BindlessCapabilities caps;
    caps.originalNativeDepthComputeExecutionContractVersion = 6u;
    assert(!semantics::OriginalBindlessExecutionLowering::capabilityAvailable(caps, 6u));
    caps.limitsQueried = true;
    assert(!semantics::OriginalBindlessExecutionLowering::capabilityAvailable(caps, 6u));
    caps.execution = BindlessExecution::Native;
    assert(semantics::OriginalBindlessExecutionLowering::capabilityAvailable(caps, 6u));
    caps.originalNativeDepthComputeExecutionContractVersion = 5u;
    assert(!semantics::OriginalBindlessExecutionLowering::capabilityAvailable(caps, 6u));

    OriginalBindlessRootFenceExecutionBudget b;
    b.intent = {6u, true};
    b.closure = {9u, true, 1u, 1u, 1u, 65536u};
    b.closure.completedDepthInputs = {
        2u, true, 1u, 4u, OriginalCompletedDepthTextureSource::completeRetainedOwnerCount};
    b.lifecycle = {14u, true, 1u, 0u, 0u, 16u, 16u, 0u, 65536u, 65536u, 0u};
    b.capacity = {7u, true, 1u, 0u, 0u};
    b.maxCpuViewBytes = 65536u;
    b.maxDiagnosticBytes = 512u;
    assert(!b.validate());
    auto wrong = b;
    wrong.intent = {5u, true};
    assert(wrong.validate());
    wrong = b;
    wrong.intent.prototypeOptIn = false;
    assert(wrong.validate());
    wrong = b;
    wrong.closure.contractVersion = 8u;
    assert(wrong.validate());
    wrong = b;
    wrong.lifecycle.contractVersion = 13u;
    assert(wrong.validate());
    wrong = b;
    wrong.capacity.contractVersion = 6u;
    assert(wrong.validate());
    wrong = b;
    wrong.lifecycle.maxCallbackTickets = 1u;
    assert(wrong.validate());
    wrong = b;
    wrong.capacity.callbackTickets = 1u;
    assert(wrong.validate());
    wrong = b;
    wrong.lifecycle.maxCallbackCpuBytes = 1u;
    assert(wrong.validate());
    wrong = b;
    wrong.maxDiagnosticBytes = b.maxCpuViewBytes;
    assert(wrong.validate());
    assert(!Life::validateContentProfile(b.lifecycle, 12u));
    for (std::uint32_t v = 0u; v <= 13u; ++v)
        if (v != 12u)
            assert(Life::validateContentProfile(b.lifecycle, v));
    assert(!Read::validatePendingProfile(2u, 14u, 12u, true, true));
    for (std::uint32_t v = 1u; v <= 14u; ++v) {
        if (v != 14u)
            assert(Read::validatePendingProfile(2u, v, 12u, true, true));
        if (v != 12u)
            assert(Read::validatePendingProfile(2u, 14u, v, true, true));
    }
    assert(Read::validatePendingProfile(1u, 14u, 12u, true, true));
    assert(Read::validatePendingProfile(2u, 14u, 12u, false, true));
    assert(Read::validatePendingProfile(2u, 14u, 12u, true, false));
    assert(!Completion::validateProfile(8u, false, false, 12u, true));
    for (std::uint32_t v = 0u; v <= 8u; ++v)
        if (v != 8u)
            assert(Completion::validateProfile(v, false, false, 12u, true));
    assert(Completion::validateProfile(8u, false, false, 12u, false));
    assert(Completion::validateProfile(8u, true, false, 12u, true));
    assert(Completion::validateProfile(8u, false, true, 12u, true));
    assert(Completion::validateProfile(8u, false, false, 11u, true));
    BindlessLoweringPlan p;
    p.success = p.executable = true;
    p.execution = BindlessExecution::Native;
    p.originalExecutionContractVersion = 6u;
    assert(Execution::hasExecutionPlan(p, 6u));
    p.originalPreparationContractVersion = 6u;
    assert(!Execution::hasExecutionPlan(p, 6u));
    p.originalPreparationContractVersion = 0u;
    p.executable = false;
    assert(!Execution::hasExecutionPlan(p, 6u));
}

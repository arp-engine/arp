#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/frontend/bindless_graphics_execution.h"
#include "rhi/frontend/bindless_graphics_pass_preparation.h"
#include "rhi/frontend/command_recording.h"
#include "rhi/frontend/finite_graphics_content_validation.h"

namespace arp::test {
class OriginalBindlessGraphicsContentContractFixture final {
public:
    static void verify()
    {
        using namespace rhi;
        using Public = OriginalBindlessGraphicsExecutionContract;
        assert(!Public::validateIntent({1u, true}));
        assert(Public::validateIntent({0u, true}) && Public::validateIntent({1u, false})
            && Public::validateIntent({3u, true}));
        assert(!Public::validateIntent({2u, true}));
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
            const auto d = semantics::OriginalBindlessGraphicsExecutionLowering::lowering(backend);
            const bool native = backend == Backend::Vulkan || backend == Backend::Metal;
            assert(d.executionAvailable == native && d.completionAvailable == native
                && bool(d.diagnostic) == !native);
            assert(semantics::OriginalBindlessGraphicsExecutionLowering::lowering(backend, 3u)
                    .diagnostic);
        }
        assert(OriginalBindlessGraphicsExecution::record({}, {1u, true})
            && OriginalBindlessGraphicsExecution::submit({}, {1u, true}));
        using C = OriginalBindlessGraphicsContentPlanContract;
        using Clear = OriginalBindlessAttachmentClearContract;
        assert(!Clear::validateObservation({}, 4u, true, false, 0u));
        assert(Clear::validateObservation({}, 4u, false, true, 1u));
        assert(!Clear::validateObservation({1u, true}, 4u, false, true, 1u));
        assert(!Clear::validateObservation({1u, true}, 4u, true, false, 0u));
        for (const auto intent :
            std::array<Clear::Intent, 3u>{{{0u, true}, {1u, false}, {2u, true}}})
            assert(Clear::validateIntent(intent));
        for (const auto state :
            std::array<std::array<bool, 2u>, 3u>{{{false, false}, {true, true}, {true, false}}})
            assert(Clear::validateObservation({1u, true}, 4u, state[0], state[1], 1u));
        assert(Clear::validateObservation({1u, true}, 3u, false, true, 1u));
        assert(Clear::validateObservation({1u, true}, 4u, false, true, 0u));
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
            const auto d = Clear::lowering(backend);
            assert(
                d.preparationAvailable == (backend == Backend::Null || backend == Backend::Vulkan)
                && d.validatorOnly == (backend == Backend::Null) && !d.executionAvailable);
            assert(Clear::lowering(backend, 2u).diagnostic);
        }
        using Creation = OriginalTextureContentCreationContract;
        using Pass = OriginalBindlessGraphicsPassContract;
        OriginalCommandNativeLifecycleBudget life{
            9u, true, 1u, 0u, 0u, 32u, 64u, 0u, 65536u, 65536u, 0u};
        assert(!OriginalCommandNativeLifecycleContract::validateIntent(life));
        assert(!OriginalCommandNativeLifecycleContract::validateContentProfile(life, 9u));
        assert(!OriginalCommandNativeLifecycleContract::validateCapacityRequest(
            {5u, true, 1u, 0u, 0u}, life));
        for (const auto version : {0u, 3u, 6u, 8u, 10u})
            assert(OriginalCommandNativeLifecycleContract::validateContentProfile(life, version));
        for (const auto version : {1u, 2u, 3u, 4u})
            assert(OriginalCommandNativeLifecycleContract::validateCapacityRequest(
                {version, true, 1u, 0u, 0u}, life));
        assert(!OriginalGeneralSubmissionCompletionContract::validateProfile(
            6u, false, false, 9u, true));
        for (std::uint32_t version = 0u; version < 6u; ++version)
            assert(OriginalGeneralSubmissionCompletionContract::validateProfile(
                version, false, false, 9u, true));
        assert(OriginalGeneralSubmissionCompletionContract::validateProfile(
            6u, false, false, 8u, true));
        assert(OriginalGeneralSubmissionCompletionContract::validateProfile(
            6u, true, false, 9u, true));
        assert(!OriginalCompletedTextureReadPlanContract::validatePendingProfile(
            5u, true, false, 9u, false, true));
        assert(OriginalCompletedTextureReadPlanContract::validatePendingProfile(
            4u, true, false, 9u, false, true));
        using Read = OriginalCompletedTextureReadPlanContract;
        assert(!Read::validateObservation(3u, true, 2u, 2u, 17u));
        assert(Read::validateObservation(1u, true, 2u, 2u, 17u));
        assert(!Read::validatePendingProfile(7u, true, false, 9u, false, true, 2u));
        assert(Read::validatePendingProfile(7u, true, false, 9u, false, true));
        assert(Read::validatePendingProfile(7u, true, false, 9u, false, true, 1u));
        assert(!Read::validateObservation(1u, true, 2u, 2u, 10u));
        assert(Read::validateObservation(1u, true, 2u, 2u, 8u));
        assert(Read::validateObservation(3u, true, 2u, 2u, 10u));
        for (const auto completed : {false, true})
            for (const auto generation : {0u, 2u})
                for (const auto revision : {0u, 2u})
                    assert(bool(Read::validateObservation(1u, completed, generation, revision, 10u))
                        != bool(completed && generation && revision));
        assert(!Read::validatePendingProfile(6u, true, false, 9u, false, true, 1u));
        assert(Read::validatePendingProfile(5u, true, false, 9u, false, true, 1u));
        assert(Read::validatePendingProfile(6u, true, false, 9u, false, true));
        assert(Read::validatePendingProfile(6u, false, false, 9u, false, true, 1u));
        assert(Read::validatePendingProfile(6u, true, false, 6u, false, true, 1u));
        assert(Read::validatePendingProfile(4u, true, false, 8u, false, true, 1u));
        assert(Read::validatePendingProfile(3u, true, false, 6u, true, false, 1u));
        assert(Read::validatePendingProfile(6u, true, false, 9u, false, true, 2u));
        assert(!Read::validatePendingStates(ResourceState::CopyDst,
            ResourceState::CopyDst,
            ResourceState::ShaderRead,
            ResourceState::ShaderRead,
            1u,
            2u,
            6u));
        assert(!Read::validatePendingStates(ResourceState::ShaderRead,
            ResourceState::ShaderRead,
            ResourceState::ShaderRead,
            ResourceState::ShaderRead,
            2u,
            2u,
            6u));
        assert(Read::validatePendingStates(ResourceState::CopyDst,
            ResourceState::CopyDst,
            ResourceState::ShaderRead,
            ResourceState::ShaderRead,
            1u,
            1u,
            6u));
        TextureDesc sampledTexture;
        sampledTexture.extent = {2u, 2u, 1u};
        sampledTexture.format = TextureFormat::Rgba8Unorm;
        sampledTexture.mipLevelCount = 2u;
        sampledTexture.usage = TextureUsage::Sampled;
        TextureViewDesc sampledView;
        sampledView.format = sampledTexture.format;
        sampledView.mipLevelCount = 2u;
        sampledView.usage = TextureUsage::Sampled;
        const auto fragment = PipelineStage::FragmentShader;
        assert(!Read::inspectViewBinding(sampledTexture,
            sampledView,
            ResourceState::ShaderRead,
            fragment,
            ResourceAccess::ShaderRead,
            10u));
        auto invalidView = sampledView;
        invalidView.mipLevelCount = 1u;
        assert(Read::inspectViewBinding(sampledTexture,
            invalidView,
            ResourceState::ShaderRead,
            fragment,
            ResourceAccess::ShaderRead,
            10u));
        assert(Read::inspectViewBinding(sampledTexture,
            sampledView,
            ResourceState::CopyDst,
            fragment,
            ResourceAccess::ShaderRead,
            10u));
        assert(Read::inspectViewBinding(sampledTexture,
            sampledView,
            ResourceState::ShaderRead,
            fragment,
            ResourceAccess::ShaderWrite,
            10u));
        sampledTexture.arrayLayerCount = 2u;
        sampledView.arrayLayerCount = 2u;
        sampledView.dimension = TextureViewDimension::Texture2DArray;
        assert(!Read::inspectViewBinding(sampledTexture,
            sampledView,
            ResourceState::ShaderRead,
            fragment,
            ResourceAccess::ShaderRead,
            10u));
        assert(!Read::inspectViewBinding(sampledTexture,
            sampledView,
            ResourceState::ShaderRead,
            fragment,
            ResourceAccess::ShaderRead,
            8u));
        OriginalFiniteGraphicsContentPlanBudget canonicalRead{
            9u, true, 2u, 1u, 1u, 2u, 2u, 64u, 8192u};
        canonicalRead.bindlessCompletedReadContractVersion = 1u;
        assert(!C::validateIntent(canonicalRead));
        auto invalidRead = canonicalRead;
        invalidRead.bindlessCompletedReadContractVersion = 4u;
        assert(C::validateIntent(invalidRead));
        invalidRead = canonicalRead;
        invalidRead.maxCompletedControls = 0u;
        assert(C::validateIntent(invalidRead));
        invalidRead = canonicalRead;
        invalidRead.bindlessGeometryContractVersion = 1u;
        invalidRead.bindlessGeometryOptIn = true;
        assert(C::validateIntent(invalidRead));
        invalidRead = canonicalRead;
        invalidRead.contractVersion = 6u;
        assert(OriginalFiniteGraphicsContentPlanContract::validateIntent(invalidRead));
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
            const auto d = C::lowering(backend, 0u, 0u, 1u);
            assert(d.rootAvailable == (backend == Backend::Null || backend == Backend::WebGPU));
            assert(d.validatorOnly == (backend == Backend::Null) && !d.executionAvailable);
            const auto hdr = C::lowering(backend, 0u, 0u, 2u);
            assert(hdr.rootAvailable
                == (backend == Backend::Null || backend == Backend::Vulkan
                    || backend == Backend::Metal));
            assert(hdr.validatorOnly == (backend == Backend::Null) && !hdr.executionAvailable);
            assert(!C::lowering(backend, 0u, 0u, 4u).rootAvailable);
            const auto standalone =
                semantics::OriginalCommandStateAdmissionLowering::textureCompletedReadPlanLowering(
                    backend, 10u);
            assert(standalone.validationAvailable == d.validationAvailable
                && standalone.validatorOnly == d.validatorOnly && !standalone.executionAvailable);
        }
        auto hdrRead = canonicalRead;
        hdrRead.bindlessCompletedReadContractVersion = 2u;
        assert(!C::validateIntent(hdrRead));
        sampledTexture.format = sampledView.format = TextureFormat::Rgba16Float;
        for (const auto stage : {PipelineStage::VertexShader,
                 PipelineStage::FragmentShader,
                 PipelineStage::ComputeShader}) {
            assert(!Read::inspectViewBinding(sampledTexture,
                sampledView,
                ResourceState::ShaderRead,
                stage,
                ResourceAccess::ShaderRead,
                17u));
            for (const auto legacy : {8u, 10u})
                assert(Read::inspectViewBinding(sampledTexture,
                    sampledView,
                    ResourceState::ShaderRead,
                    stage,
                    ResourceAccess::ShaderRead,
                    legacy));
        }
        for (const auto format : {TextureFormat::R32Float, TextureFormat::D32Float}) {
            sampledTexture.format = sampledView.format = format;
            assert(Read::inspectViewBinding(sampledTexture,
                sampledView,
                ResourceState::ShaderRead,
                fragment,
                ResourceAccess::ShaderRead,
                17u));
        }
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
            const auto d = semantics::OriginalCommandNativeLifecycleLowering::lowering(backend, 9u);
            const bool native = backend == Backend::Vulkan || backend == Backend::Metal;
            assert(d.executionAvailable == native && d.sealedIssuerAvailable == native
                && bool(d.diagnostic) == !native);
        }
        const Pass::Budget passBudget{1u, true, 1u, 3u, 9u, 65536u};
        OriginalBindlessGraphicsExecutionBudget publicBudget;
        publicBudget.intent = {1u, true};
        publicBudget.closure = {1u, true, 1u, 4u, 16u, 65536u};
        publicBudget.pass = passBudget;
        publicBudget.lifecycle = life;
        publicBudget.capacity = {5u, true, 1u, 0u, 0u};
        publicBudget.maxCpuViewBytes = 65536u;
        publicBudget.maxDiagnosticBytes = 512u;
        assert(!Public::validateBudget(publicBudget));
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
            const auto decision =
                semantics::OriginalBindlessGraphicsExecutionLowering::lowering(backend);
            const bool native = backend == Backend::Vulkan || backend == Backend::Metal;
            assert(decision.executionAvailable == native && decision.completionAvailable == native
                && bool(decision.diagnostic) == !native);
        }
        {
            auto nullDevice = createNullDevice();
            assert(nullDevice);
            auto* extension = static_cast<BindlessCrossExtension*>(
                nullDevice->queryInterface(kExtensionBindlessCross));
            assert(
                extension && !extension->capabilities().originalGraphicsExecutionContractVersion);
            core::pmr::String diagnostic;
            assert(!extension->prepareOriginalGraphicsExecution(
                       *nullDevice, {}, publicBudget, diagnostic)
                && diagnostic == "RHI-BINDLESS-GRAPHICS-EXECUTION-NULL-VALIDATOR");
        }
        {
            auto wrong = publicBudget;
            wrong.lifecycle.contractVersion = 6u;
            assert(Public::validateBudget(wrong));
        }
        {
            auto wrong = publicBudget;
            wrong.maxCpuViewBytes = 0u;
            assert(Public::validateBudget(wrong));
        }
        {
            auto wrong = publicBudget;
            wrong.capacity.contractVersion = 2u;
            assert(Public::validateBudget(wrong));
        }
        {
            auto wrong = publicBudget;
            wrong.fetch = {2u, true, 1u, 1u, 1u, 1u, 1u};
            assert(Public::validateBudget(wrong));
        }
        {
            auto wrong = publicBudget;
            wrong.closure.contractVersion = 3u;
            assert(Public::validateBudget(wrong));
        }
        assert(!Pass::validateCounts(passBudget, 1u, 9u));
        auto descriptorOnly = passBudget;
        descriptorOnly.maxNativeObjects = 1u;
        assert(!Pass::validateCounts(descriptorOnly, 1u, 1u, 9u));
        assert(Pass::validateCounts(descriptorOnly, 1u, 0u, 9u));
        assert(Pass::validateCounts(descriptorOnly, 1u, 2u, 9u));
        assert(Pass::validateCounts(descriptorOnly, 1u, 1u, 10u));
        assert(Pass::validateCounts(descriptorOnly, UINT64_MAX, 1u, 9u));
        assert(Pass::validateCounts(descriptorOnly, 1u, 9u));
        assert(Pass::validateCounts(passBudget, UINT64_MAX, 9u));
        assert(Pass::validateCounts(passBudget, 0u, 9u));
        for (const auto member : {&Pass::Budget::maxAttachmentViews,
                 &Pass::Budget::maxNativeObjects,
                 &Pass::Budget::maxRetainedOwners}) {
            auto less = passBudget;
            --(less.*member);
            assert(Pass::validateCounts(less, 1u, 9u));
        }
        RenderPassDesc pass;
        RenderPassColorAttachmentDesc color;
        color.target = {1u};
        color.format = TextureFormat::Rgba8Unorm;
        color.loadOp = LoadOp::Clear;
        color.storeOp = StoreOp::Store;
        pass.colorAttachments.push_back(color);
        RenderPipelineDesc pipeline;
        pipeline.colorFormat = TextureFormat::Rgba8Unorm;
        const auto capturedPipeline = Pass::capturePipeline(pipeline);
        assert(!Pass::inspectPass(pass, pipeline));
        pass.colorAttachments[0].loadOp = LoadOp::Load;
        assert(Pass::inspectPass(pass, pipeline));
        pass.colorAttachments[0].loadOp = LoadOp::Clear;
        pipeline.multisample.sampleCount = 4u;
        assert(Pass::inspectPass(pass, pipeline));
        pipeline.multisample.sampleCount = 1u;
        assert(!Pass::inspectPass(pass, capturedPipeline));
        pass.layeredRendering = true;
        assert(Pass::inspectPass(pass, pipeline));
        pass.layeredRendering = false;
        pass.colorAttachments[0].hasResolveTarget = true;
        assert(Pass::inspectPass(pass, pipeline));
        pass.colorAttachments.clear();
        pass.hasDepthStencilAttachment = true;
        pass.depthStencilAttachment.target = {2u};
        pass.depthStencilAttachment.format = TextureFormat::D32Float;
        pass.depthStencilAttachment.depthLoadOp = LoadOp::Clear;
        pass.depthStencilAttachment.depthStoreOp = StoreOp::Store;
        pipeline.colorFormat = TextureFormat::Unknown;
        pipeline.depthFormat = TextureFormat::D32Float;
        assert(!Pass::inspectPass(pass, pipeline));
        pass.depthStencilAttachment.depthReadOnly = true;
        assert(Pass::inspectPass(pass, pipeline));
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
            const auto d = Pass::lowering(backend);
            const bool native = backend == Backend::Vulkan || backend == Backend::Metal;
            assert(d.preparationAvailable == native && d.validatorOnly == (backend == Backend::Null)
                && !d.executionAvailable && bool(d.diagnostic) == !native);
            const auto selected = Pass::lowering(backend, 2u);
            assert(selected.preparationAvailable == native
                && selected.validatorOnly == (backend == Backend::Null)
                && !selected.executionAvailable && bool(selected.diagnostic) == !native);
            assert(!Pass::lowering(backend, 3u).preparationAvailable);
        }
        const C::Budget budget{9u, true, 2u, 1u, 1u, 2u, 2u, 40u, 256u};
        const C::Counts counts{2u, 1u, 1u, 0u, 2u, 2u, 40u, 256u};
        assert(!C::validateCounts(budget, counts));
        assert(!C::validateProfile(6u, 1u, 1u, 1u, 1u, 4u, true, false));
        auto geometryContent = budget;
        geometryContent.bindlessGeometryContractVersion = 1u;
        geometryContent.bindlessGeometryOptIn = true;
        assert(!C::validateCounts(geometryContent, counts));
        assert(!C::validateProfile(6u, 7u, 1u, 1u, 1u, 5u, true, false, 1u));
        assert(C::validateProfile(6u, 1u, 1u, 1u, 1u, 4u, true, false, 1u));
        assert(C::validateProfile(6u, 7u, 1u, 1u, 1u, 5u, true, false));
        assert(C::validateProfile(6u, 7u, 1u, 1u, 1u, 5u, true, false, 2u));
        auto invalidGeometry = geometryContent;
        invalidGeometry.bindlessGeometryOptIn = false;
        assert(C::validateIntent(invalidGeometry));
        invalidGeometry = geometryContent;
        invalidGeometry.bindlessGeometryContractVersion = 0u;
        assert(C::validateIntent(invalidGeometry));
        invalidGeometry = geometryContent;
        invalidGeometry.bindlessGeometryContractVersion = 3u;
        assert(C::validateIntent(invalidGeometry));
        invalidGeometry = geometryContent;
        invalidGeometry.contractVersion = 6u;
        assert(OriginalFiniteGraphicsContentPlanContract::validateIntent(invalidGeometry));
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
            const auto geometry = C::lowering(backend, 1u);
            assert(geometry.rootAvailable
                    == (backend == Backend::Null || backend == Backend::Vulkan
                        || backend == Backend::Metal || backend == Backend::WebGPU)
                && !geometry.executionAvailable && geometry.diagnostic);
            const auto produced = C::lowering(backend, 2u);
            assert(produced.rootAvailable && !produced.executionAvailable && produced.diagnostic);
            assert(!C::lowering(backend, 3u).rootAvailable);
        }
        auto producedContent = geometryContent;
        producedContent.bindlessGeometryContractVersion = 2u;
        assert(!C::validateCounts(producedContent, counts));
        assert(!C::validateProfile(6u, 8u, 1u, 1u, 1u, 6u, true, true, 2u));
        assert(C::validateProfile(6u, 8u, 1u, 1u, 1u, 6u, false, true, 2u));
        assert(C::validateProfile(6u, 7u, 1u, 1u, 1u, 6u, true, true, 2u));
        assert(C::validateProfile(6u, 8u, 1u, 1u, 1u, 5u, true, true, 2u));
        assert(C::consumes(CommandUsageKind::Dispatch, 2u)
            && C::consumes(CommandUsageKind::DrawIndexedIndirect, 2u)
            && !C::consumes(CommandUsageKind::DrawIndexedIndirectCount, 2u)
            && !C::consumes(CommandUsageKind::DrawIndexed, 2u)
            && !C::consumes(CommandUsageKind::Dispatch, 1u));
        auto repeat = geometryContent;
        repeat.attachmentClear = {1u, true};
        assert(!C::validateIntent(repeat));
        auto invalidRepeat = repeat;
        invalidRepeat.attachmentClear = {1u, false};
        assert(C::validateIntent(invalidRepeat));
        invalidRepeat = repeat;
        invalidRepeat.attachmentClear = {0u, true};
        assert(C::validateIntent(invalidRepeat));
        invalidRepeat = repeat;
        invalidRepeat.attachmentClear = {2u, true};
        assert(C::validateIntent(invalidRepeat));
        invalidRepeat = repeat;
        invalidRepeat.contractVersion = 6u;
        assert(OriginalFiniteGraphicsContentPlanContract::validateIntent(invalidRepeat));
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
            const auto d = C::lowering(backend, 1u, 1u);
            assert(d.rootAvailable == (backend == Backend::Null || backend == Backend::Vulkan)
                && !d.executionAvailable);
            assert(!C::lowering(backend, 1u, 2u).rootAvailable);
        }
        for (unsigned field = 0u; field < 8u; ++field) {
            std::array<std::uint32_t, 6u> versions{6u, 1u, 1u, 1u, 1u, 4u};
            if (field < 6u)
                --versions[field];
            assert(C::validateProfile(versions[0],
                versions[1],
                versions[2],
                versions[3],
                versions[4],
                versions[5],
                field != 6u,
                field == 7u));
        }
        for (const auto member : {&C::Budget::maxCompletedControls,
                 &C::Budget::maxAttachmentControls,
                 &C::Budget::maxAttachmentCells,
                 &C::Budget::maxSampledSteps,
                 &C::Budget::maxAttachmentSteps,
                 &C::Budget::maxCommandVisits,
                 &C::Budget::maxProjectionVisits}) {
            auto less = budget;
            --(less.*member);
            assert(C::validateCounts(less, counts));
        }
        auto incomplete = counts;
        incomplete.drawReads = 1u;
        assert(C::validateCounts(budget, incomplete));
        incomplete = counts;
        incomplete.renderBindingReads = 1u;
        assert(C::validateCounts(budget, incomplete));
        incomplete = counts;
        incomplete.attachmentSteps = 1u;
        assert(C::validateCounts(budget, incomplete));
        incomplete = counts;
        incomplete.attachmentSteps = UINT64_MAX;
        assert(C::validateCounts(budget, incomplete));
        auto buffers = budget;
        buffers.maxCompletedControls = buffers.maxSampledSteps = 0u;
        auto bufferCounts = counts;
        bufferCounts.completedControls = bufferCounts.drawReads = 0u;
        assert(!C::validateCounts(buffers, bufferCounts));
        assert(C::validateCounts(buffers, counts));
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
            const auto d = C::lowering(backend);
            assert(d.contractVersion == 9u && d.validationAvailable && d.rootAvailable
                && !d.executionAvailable && d.validatorOnly == (backend == Backend::Null)
                && d.diagnostic);
            assert(!OriginalFiniteGraphicsContentPlanContract::lowering(backend, 9u).rootAvailable);
        }
        for (std::uint32_t control = 0u; control <= 5u; ++control)
            for (std::uint32_t plan = 0u; plan <= 10u; ++plan)
                assert(Creation::cellControlMatchesPlan(control, plan)
                    == ((control == 2u && (plan == 2u || plan == 3u || plan == 6u))
                        || ((control == 4u || control == 5u) && plan == 9u)));
        TextureDesc texture;
        texture.extent = {2u, 2u, 1u};
        texture.initialState = ResourceState::Undefined;
        texture.usage = TextureUsage::RenderAttachment | TextureUsage::CopySrc;
        TextureViewDesc view;
        view.usage = TextureUsage::RenderAttachment;
        OriginalTextureContentCreationBudget creation{4u, true, 1u, 1u, 32u, 4u, 65536u, 1u, 4096u};
        for (const auto format :
            {TextureFormat::Rgba8Unorm, TextureFormat::Rgba16Float, TextureFormat::D32Float}) {
            texture.format = view.format = format;
            view.aspect =
                format == TextureFormat::D32Float ? TextureAspect::Depth : TextureAspect::Color;
            std::uint64_t bytes = 0u;
            assert(!C::inspectAttachment(
                       texture, view, creation, LoadOp::Clear, StoreOp::Store, false, false, bytes)
                && bytes == 4u * bytesPerPixel(format));
            assert(C::inspectAttachment(
                       texture, view, creation, LoadOp::Load, StoreOp::Store, false, false, bytes)
                && !bytes);
            assert(
                C::inspectAttachment(
                    texture, view, creation, LoadOp::Clear, StoreOp::DontCare, false, false, bytes)
                && !bytes);
            auto legacy = creation;
            legacy.contractVersion = 2u;
            legacy.maxNativeAllocationBytes = 0u;
            assert(C::inspectAttachment(
                       texture, view, legacy, LoadOp::Clear, StoreOp::Store, false, false, bytes)
                && !bytes);
        }
        texture.format = view.format = TextureFormat::Depth24Stencil8;
        view.aspect = TextureAspect::Depth;
        std::uint64_t bytes = 77u;
        assert(C::inspectAttachment(
                   texture, view, creation, LoadOp::Clear, StoreOp::Store, false, false, bytes)
            && !bytes);
        const auto quota = core::makeShared<core::CpuMemoryQuota>(65536u);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{quota}, [&]() noexcept -> const char* {
                return C::validateCounts(budget, counts);
            }));
    }
};
} // namespace arp::test

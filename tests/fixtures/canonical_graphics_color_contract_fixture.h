#pragma once
#include "rhi/frontend/command_sources.h"
#include "rhi/frontend/descriptor_completed_texture.h"
#include "rhi/frontend/finite_graphics_content_validation.h"
#include "rhi/frontend/resource_texture_validation.h"
#include "rhi/semantics/bindless_ordinary_color_domain.h"
#include "rhi/semantics/bindless_original_execution_lowering.h"
#include "rhi/semantics/bindless_original_preparation_lowering.h"
#include "rhi/semantics/bindless_pipeline_interface.h"
#include "rhi/semantics/command_lowering.h"

#include <cassert>

namespace arp::test {
// Canonical whole-upload color profile3 is independent of control3 HDR /2.
// These value checks cannot issue an original Source or native command.
class CanonicalGraphicsColorContractFixture final {
public:
    static void verify()
    {
        using namespace rhi;
        using Publication = OriginalCompletedTextureDescriptorContract;
        using Read = OriginalCompletedTextureReadPlanContract;
        using Content = OriginalBindlessGraphicsContentPlanContract;
        using Color = semantics::OriginalBindlessOrdinaryColorDomain;
        using Closure = OriginalBindlessRootNativeClosureContract;
        OriginalBindlessRootNativeClosureBudget closure{17u, true, 8u, 16u, 32u, 65536u};
        closure.maxAttachmentViews = 1u;
        assert(!Closure::validateBudget(closure));
        auto invalidClosure = closure;
        invalidClosure.contractVersion = 19u;
        assert(Closure::validateBudget(invalidClosure));
        invalidClosure = closure;
        invalidClosure.prototypeOptIn = false;
        assert(Closure::validateBudget(invalidClosure));
        invalidClosure = closure;
        invalidClosure.maxAttachmentViews = 0u;
        assert(Closure::validateBudget(invalidClosure));
        invalidClosure = closure;
        invalidClosure.completedDepthInputs = {1u, true, 1u, 16u, 5u};
        assert(Closure::validateBudget(invalidClosure));
        BindlessLoweringPlan cold;
        cold.success = true;
        cold.execution = BindlessExecution::Emulated;
        cold.originalPreparationContractVersion = 11u;
        assert(!Closure::canonicalGraphicsColorProfileDiagnostic(cold, 2u, true, true));
        auto executing = cold;
        executing.executable = true;
        executing.originalPreparationContractVersion = 0u;
        executing.originalExecutionContractVersion = 13u;
        assert(Closure::canonicalGraphicsColorProfileDiagnostic(executing, 2u, true, true));
        assert(!Closure::canonicalGraphicsColorProfileDiagnostic(executing, 2u, true, true, 18u));
        assert(Closure::canonicalGraphicsColorProfileDiagnostic(cold, 2u, true, true, 18u));
        assert(Closure::canonicalGraphicsColorProfileDiagnostic(executing, 2u, true, true, 19u));
        auto executableClosure = closure;
        executableClosure.contractVersion = 18u;
        assert(!Closure::validateBudget(executableClosure));
        executableClosure.maxAttachmentViews = 0u;
        assert(Closure::validateBudget(executableClosure));
        for (const auto version : {0u, 1u, 3u})
            assert(Closure::canonicalGraphicsColorProfileDiagnostic(cold, version, true, true));
        assert(Closure::canonicalGraphicsColorProfileDiagnostic(cold, 2u, false, true));
        assert(Closure::canonicalGraphicsColorProfileDiagnostic(cold, 2u, true, false));
        auto legacy = cold;
        legacy.originalPreparationContractVersion = 2u;
        assert(Closure::canonicalGraphicsColorProfileDiagnostic(legacy, 2u, true, true));
        BindlessCapabilities caps;
        assert(!semantics::OriginalBindlessPreparationLowering::capabilityAvailable(caps, 11u));
        caps.limitsQueried = true;
        assert(!semantics::OriginalBindlessPreparationLowering::capabilityAvailable(caps, 11u));
        caps.originalOrdinaryColorGraphicsPreparationContractVersion = 11u;
        assert(semantics::OriginalBindlessPreparationLowering::capabilityAvailable(caps, 11u));
        BindlessShaderArtifact ordinary;
        ordinary.additionalDomains.emplace_back();
        DescriptorBindingDesc sampled;
        sampled.type = DescriptorType::SampledTexture;
        sampled.stages = ShaderStage::Vertex | ShaderStage::Fragment;
        sampled.textureViewDimensionConstrained = true;
        sampled.textureViewDimension = TextureViewDimension::TextureCube;
        ordinary.additionalDomains.back().layout.bindings.push_back(sampled);
        using Interface = semantics::BindlessPipelineInterfaceContract;
        assert(Interface::originalEmulatedBufferDomains(ordinary, 11u, true));
        for (const auto old : {1u, 2u, 3u, 4u})
            assert(!Interface::originalEmulatedBufferDomains(ordinary, old, true));
        assert(!Interface::originalEmulatedBufferDomains(ordinary, 11u, false));
        assert(Interface::originalEmulatedBufferDomains(ordinary, 13u, false));
        assert(!Interface::originalEmulatedBufferDomains(ordinary, 13u, true));
        auto& shape = ordinary.additionalDomains.back().layout.bindings.back();
        shape.stages = ShaderStage::Compute;
        assert(!Interface::originalEmulatedBufferDomains(ordinary, 11u, true));
        shape = sampled;
        shape.arrayCount = 2u;
        assert(!Interface::originalEmulatedBufferDomains(ordinary, 11u, true));
        shape = sampled;
        shape.depthTexture = true;
        assert(!Interface::originalEmulatedBufferDomains(ordinary, 11u, true));
        shape = sampled;
        for (const auto version : {1u, 2u, 3u, 4u, 12u}) {
            auto execution = cold;
            execution.executable = true;
            execution.originalPreparationContractVersion = 0u;
            execution.originalExecutionContractVersion = version;
            assert(Closure::canonicalGraphicsColorProfileDiagnostic(execution, 2u, true, true));
        }
        OriginalCompletedTextureDescriptorRequest publication{
            6u, true, 1u, 1u, OriginalCommandTextureViewSource::completeRetainedOwnerCount};
        assert(!Publication::validateIntent(publication));
        auto invalidPublication = publication;
        invalidPublication.prototypeOptIn = false;
        assert(Publication::validateIntent(invalidPublication));
        invalidPublication = publication;
        --invalidPublication.maxRetainedSourceOwners;
        assert(Publication::validateIntent(invalidPublication));
        assert(!Publication::validateControl(1u, 6u));
        for (const auto control : {0u, 2u, 3u, 4u, 7u})
            assert(Publication::validateControl(control, 6u));
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU, Backend::WebGL}) {
            const auto native =
                semantics::OriginalBindlessRootNativeClosureLowering::lowering(backend, 17u);
            assert(native.nativeCorrespondenceAvailable == (backend == Backend::WebGPU)
                && native.validatorOnly == (backend == Backend::Null)
                && !native.executionAvailable);
            const auto executableNative =
                semantics::OriginalBindlessRootNativeClosureLowering::lowering(backend, 18u);
            assert(executableNative.nativeCorrespondenceAvailable == (backend == Backend::WebGPU)
                && executableNative.validatorOnly == (backend == Backend::Null)
                && !executableNative.executionAvailable);
            const auto execution =
                semantics::OriginalBindlessExecutionLowering::lowering(backend, 13u);
            assert(execution.executionAvailable == (backend == Backend::WebGPU)
                && execution.completionAvailable == execution.executionAvailable
                && bool(execution.diagnostic) == (backend != Backend::WebGPU));
            const auto prep =
                semantics::OriginalBindlessPreparationLowering::lowering(backend, 11u);
            assert(prep.preparationAvailable == (backend == Backend::WebGPU)
                && prep.validatorOnly == (backend == Backend::Null) && !prep.executionAvailable);
            const auto p = Publication::lowering(backend, 6u);
            assert(p.publicationAvailable == (backend == Backend::WebGPU));
            assert(p.validatorOnly == (backend == Backend::Null) && !p.shaderExecutionAvailable);
            const auto c = Content::lowering(backend, 0u, 0u, 3u);
            const auto r =
                semantics::OriginalCommandStateAdmissionLowering::textureCompletedReadPlanLowering(
                    backend, 18u);
            const bool planning = backend == Backend::Null || backend == Backend::WebGPU;
            assert(c.rootAvailable == planning && c.validationAvailable == planning
                && c.validatorOnly == (backend == Backend::Null) && !c.executionAvailable);
            assert(r.validationAvailable == planning
                && r.validatorOnly == (backend == Backend::Null) && !r.executionAvailable);
        }
        OriginalFiniteGraphicsContentPlanBudget budget{9u, true, 2u, 1u, 1u, 2u, 2u, 64u, 8192u};
        budget.bindlessCompletedReadContractVersion = 3u;
        assert(!Content::validateIntent(budget));
        auto invalid = budget;
        invalid.bindlessCompletedReadContractVersion = 4u;
        assert(Content::validateIntent(invalid));
        invalid = budget;
        invalid.maxSampledSteps = 0u;
        assert(Content::validateIntent(invalid));
        assert(Read::graphicsReadVersion(3u) == 18u);
        assert(Read::graphicsReadVersion(0u) == 8u && Read::graphicsReadVersion(1u) == 10u
            && Read::graphicsReadVersion(2u) == 17u);
        for (const auto control : {0u, 1u, 2u, 3u})
            assert(bool(Read::validateObservation(control, true, 2u, 2u, 18u)) == (control != 1u));
        assert(Read::validateObservation(1u, false, 2u, 2u, 18u));
        assert(Read::validateObservation(1u, true, 0u, 2u, 18u));
        assert(Read::validateObservation(1u, true, 2u, 0u, 18u));
        assert(!Read::validatePendingProfile(9u, true, false, 9u, false, true, 3u));
        assert(Read::validatePendingProfile(9u, false, false, 9u, false, true, 3u));
        assert(Read::validatePendingProfile(9u, true, false, 9u, false, false, 3u));
        assert(Read::validatePendingProfile(9u, true, false, 8u, false, true, 3u));
        for (const auto profile : {0u, 1u, 2u, 4u})
            assert(Read::validatePendingProfile(9u, true, false, 9u, false, true, profile));
        for (const auto pending : {4u, 5u, 6u, 7u, 8u})
            assert(Read::validatePendingProfile(pending, true, false, 9u, false, true, 3u));
        assert(!Read::validatePendingStates(ResourceState::CopyDst,
            ResourceState::CopyDst,
            ResourceState::ShaderRead,
            ResourceState::ShaderRead,
            1u,
            2u,
            9u));
        assert(!Read::validatePendingStates(ResourceState::ShaderRead,
            ResourceState::ShaderRead,
            ResourceState::ShaderRead,
            ResourceState::ShaderRead,
            2u,
            2u,
            9u));
        assert(Read::validatePendingStates(ResourceState::ShaderRead,
            ResourceState::ShaderRead,
            ResourceState::ShaderRead,
            ResourceState::ShaderRead,
            2u,
            3u,
            9u));
        for (const auto format : {TextureFormat::Rgba8Unorm, TextureFormat::Rgba16Float})
            for (const auto dimension : {TextureViewDimension::Texture2D,
                     TextureViewDimension::Texture2DArray,
                     TextureViewDimension::TextureCube}) {
                TextureDesc texture;
                texture.format = format;
                texture.extent = {2u, 2u, 1u};
                texture.mipLevelCount = 2u;
                texture.arrayLayerCount = dimension == TextureViewDimension::TextureCube ? 6u
                    : dimension == TextureViewDimension::Texture2DArray                  ? 2u
                                                                                         : 1u;
                texture.cubeCompatible = dimension == TextureViewDimension::TextureCube;
                texture.usage = TextureUsage::CopyDst | TextureUsage::Sampled;
                TextureViewDesc view;
                view.format = format;
                view.dimension = dimension;
                view.mipLevelCount = texture.mipLevelCount;
                view.arrayLayerCount = texture.arrayLayerCount;
                view.usage = TextureUsage::Sampled;
                view.aspect = TextureAspect::Color;
                assert(!Publication::validateView(texture, view, 6u));
                assert(!Color::completedViewDiagnostic(texture, view, 1u, 3u));
                assert(Color::completedViewDiagnostic(texture, view, 3u, 3u));
                for (const auto stage :
                    {PipelineStage::VertexShader, PipelineStage::FragmentShader})
                    assert(!Read::inspectViewBinding(texture,
                        view,
                        ResourceState::ShaderRead,
                        stage,
                        ResourceAccess::ShaderRead,
                        18u));
                if (format == TextureFormat::Rgba16Float
                    || dimension == TextureViewDimension::TextureCube) {
                    assert(Publication::validateView(texture, view, 2u));
                    assert(Read::inspectViewBinding(texture,
                        view,
                        ResourceState::ShaderRead,
                        PipelineStage::FragmentShader,
                        ResourceAccess::ShaderRead,
                        10u));
                }
                auto partial = view;
                --partial.mipLevelCount;
                assert(Publication::validateView(texture, partial, 6u));
                assert(Color::completedViewDiagnostic(texture, partial, 1u, 3u));
                assert(Read::inspectViewBinding(texture,
                    partial,
                    ResourceState::ShaderRead,
                    PipelineStage::FragmentShader,
                    ResourceAccess::ShaderRead,
                    18u));
                auto unsupported = texture;
                auto wrong = view;
                unsupported.format = wrong.format = TextureFormat::R32Float;
                assert(Publication::validateView(unsupported, wrong, 6u));
                assert(Color::completedViewDiagnostic(unsupported, wrong, 1u, 3u));
                unsupported = texture;
                unsupported.sampleCount = 4u;
                assert(Publication::validateView(unsupported, view, 6u));
                unsupported = texture;
                unsupported.extent.width = 0u;
                assert(Publication::validateView(unsupported, view, 6u));
                wrong = view;
                wrong.aspect = TextureAspect::Depth;
                assert(Publication::validateView(texture, wrong, 6u));
            }
    }
};
} // namespace arp::test

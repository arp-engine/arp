#pragma once
#include "rhi/semantics/bindless_ordinary_depth_pipeline.h"
#include "rhi/semantics/bindless_original_preparation_lowering.h"
#include "rhi/semantics/bindless_pipeline_interface.h"

#include <cassert>

namespace arp::test {
class OrdinaryDepthPipelineContractFixture final {
public:
    static void verify()
    {
        using namespace rhi;
        using Preparation = semantics::OriginalBindlessPreparationLowering;
        using Pipeline = semantics::OriginalBindlessOrdinaryDepthPipelineDeclaration;
        using Interface = semantics::BindlessPipelineInterfaceContract;
        BindlessCapabilities caps;
        assert(!Preparation::capabilityAvailable(caps, 12u));
        caps.limitsQueried = true;
        caps.execution = BindlessExecution::DiagnosticFailure;
        caps.originalOrdinaryColorGraphicsPreparationContractVersion = 11u;
        assert(!Preparation::capabilityAvailable(caps, 12u));
        caps.originalOrdinaryDepthGraphicsPreparationContractVersion = 12u;
        assert(Preparation::capabilityAvailable(caps, 12u));
        caps.execution = BindlessExecution::Emulated;
        assert(Preparation::capabilityAvailable(caps, 12u));
        caps.execution = BindlessExecution::Native;
        assert(!Preparation::capabilityAvailable(caps, 12u));
        caps.execution = BindlessExecution::DiagnosticFailure;
        caps.limitsQueried = false;
        assert(!Preparation::capabilityAvailable(caps, 12u));
        BindlessLoweringPlan plan;
        plan.success = true;
        plan.execution = BindlessExecution::Emulated;
        plan.originalPreparationContractVersion = 12u;
        assert(Preparation::hasPlan(plan, 12u));
        plan.executable = true;
        assert(!Preparation::hasPlan(plan, 12u));
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU, Backend::WebGL}) {
            const auto decision = Preparation::lowering(backend, 12u);
            assert(decision.preparationAvailable == (backend == Backend::WebGPU)
                && decision.validatorOnly == (backend == Backend::Null)
                && !decision.executionAvailable
                && bool(decision.diagnostic) == (backend != Backend::WebGPU));
        }
        BindlessShaderArtifact artifact;
        artifact.additionalDomains.emplace_back();
        auto& bindings = artifact.additionalDomains.back().layout.bindings;
        DescriptorBindingDesc depth;
        depth.type = DescriptorType::SampledTexture;
        depth.stages = ShaderStage::Fragment;
        depth.depthTexture = true;
        depth.textureViewDimensionConstrained = true;
        depth.textureViewDimension = TextureViewDimension::Texture2D;
        bindings.push_back(depth);
        assert(!Interface::originalEmulatedBufferDomains(artifact, 12u, true));
        DescriptorBindingDesc comparison;
        comparison.binding = 1u;
        comparison.type = DescriptorType::Sampler;
        comparison.stages = ShaderStage::Fragment;
        comparison.comparisonSampler = true;
        bindings.push_back(comparison);
        assert(Interface::originalEmulatedBufferDomains(artifact, 12u, true));
        for (const auto version : {1u, 2u, 3u, 11u, 13u}) {
            assert(!Interface::originalEmulatedBufferDomains(artifact, version, true));
            assert(!Interface::originalEmulatedBufferDomains(artifact, version, false));
        }
        assert(!Interface::originalEmulatedBufferDomains(artifact, 12u, false));
        for (const auto stage : {ShaderStage::None,
                 ShaderStage::Vertex,
                 ShaderStage::Compute,
                 ShaderStage::Vertex | ShaderStage::Fragment}) {
            bindings[0].stages = stage;
            assert(Pipeline::inspect(Backend::WebGPU, bindings[0]));
            assert(!Interface::originalEmulatedBufferDomains(artifact, 12u, true));
        }
        bindings[0] = depth;
        for (const auto dimension :
            {TextureViewDimension::Texture2DArray, TextureViewDimension::TextureCube}) {
            bindings[0].textureViewDimension = dimension;
            assert(!Interface::originalEmulatedBufferDomains(artifact, 12u, true));
        }
        bindings[0] = depth;
        bindings[0].arrayCount = 2u;
        assert(!Interface::originalEmulatedBufferDomains(artifact, 12u, true));
        bindings[0] = depth;
        bindings[0].dynamicOffset = true;
        assert(!Interface::originalEmulatedBufferDomains(artifact, 12u, true));
        bindings[0] = depth;
        bindings[0].tableBacked = true;
        assert(!Interface::originalEmulatedBufferDomains(artifact, 12u, true));
        bindings[0] = depth;
        bindings[1].comparisonSampler = false;
        assert(!Interface::originalEmulatedBufferDomains(artifact, 12u, true));
    }
};
} // namespace arp::test

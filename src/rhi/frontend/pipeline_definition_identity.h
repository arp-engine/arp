#pragma once
#include "rhi/frontend/descriptor_set_validation.h"
#include "rhi/frontend/pipeline_push_constant_abi_identity.h"
#include "rhi/gpu_pipeline.h"

#include <algorithm>

namespace arp::rhi {
// Exact immutable definition comparison. Preserve inactive authored fields and
// float bits; neither cache hashes nor native effective defaults prove identity.
class PipelineDefinitionIdentity final {
public:
    static bool matches(const DescriptorBindingDesc& a, const DescriptorBindingDesc& b) noexcept
    {
        return a.stages == b.stages && descriptorBindingLayoutMatches(a, b);
    }

    static bool matches(const DescriptorLayoutDesc& a, const DescriptorLayoutDesc& b) noexcept
    {
        return a.name == b.name && sequence(a.bindings, b.bindings);
    }

    static bool matches(const DescriptorSetSlotDesc& a, const DescriptorSetSlotDesc& b) noexcept
    {
        return a.set == b.set && a.layoutName == b.layoutName;
    }

    static bool matches(const DescriptorTableSlotDesc& a, const DescriptorTableSlotDesc& b) noexcept
    {
        return a.set == b.set && a.tableName == b.tableName && a.type == b.type
            && a.stages == b.stages && a.capacity == b.capacity
            && a.textureViewDimensionConstrained == b.textureViewDimensionConstrained
            && a.textureViewDimension == b.textureViewDimension && a.depthTexture == b.depthTexture;
    }

    static bool matches(const PushConstantRange& a, const PushConstantRange& b) noexcept
    {
        return a.stages == b.stages && a.offsetBytes == b.offsetBytes && a.sizeBytes == b.sizeBytes;
    }

    static bool matches(const PipelineLayoutDesc& a, const PipelineLayoutDesc& b) noexcept
    {
        return sequence(a.descriptorLayouts, b.descriptorLayouts)
            && sequence(a.descriptorSets, b.descriptorSets)
            && sequence(a.descriptorTables, b.descriptorTables)
            && sequence(a.pushConstantRanges, b.pushConstantRanges);
    }

    static bool matches(const ShaderEntrypoint& a, const ShaderEntrypoint& b) noexcept
    {
        return a.matchesDefinition(b);
    }

    static bool matches(
        const ShaderGeneratedArtifacts& a, const ShaderGeneratedArtifacts& b) noexcept
    {
        return a.spirvPath == b.spirvPath && a.mslPath == b.mslPath && a.wgslPath == b.wgslPath
            && a.reflectionPath == b.reflectionPath && a.publicationPath == b.publicationPath;
    }

    static bool matches(const ShaderReflectionBinding& a, const ShaderReflectionBinding& b) noexcept
    {
        return a.set == b.set && a.binding == b.binding && a.type == b.type && a.stages == b.stages
            && a.arrayCount == b.arrayCount && a.subpassAttachmentIndex == b.subpassAttachmentIndex
            && a.depthTexture == b.depthTexture;
    }

    static bool matches(const ShaderReflectionDesc& a, const ShaderReflectionDesc& b) noexcept
    {
        return a.reflectionContractVersion == b.reflectionContractVersion
            && a.sourceSha256 == b.sourceSha256
            && a.bindlessContractVersion == b.bindlessContractVersion
            && a.bindlessExecution == b.bindlessExecution
            && a.bindlessLoweringSha256 == b.bindlessLoweringSha256
            && a.pushConstantByteCount == b.pushConstantByteCount
            && a.pushConstantContractVersion == b.pushConstantContractVersion
            && a.pushConstantStages == b.pushConstantStages
            && a.pushConstantWebGpuBindingGroup == b.pushConstantWebGpuBindingGroup
            && a.pushConstantWebGpuBindingIndex == b.pushConstantWebGpuBindingIndex
            && a.pushConstantMetalBufferIndex == b.pushConstantMetalBufferIndex
            && a.computeWorkgroupSizeX == b.computeWorkgroupSizeX
            && a.computeWorkgroupSizeY == b.computeWorkgroupSizeY
            && a.computeWorkgroupSizeZ == b.computeWorkgroupSizeZ
            && a.tessellationControlPointCount == b.tessellationControlPointCount
            && a.metalGpuCountedIndirectContractVersion == b.metalGpuCountedIndirectContractVersion
            && a.metalGpuCountedIndirectStatus == b.metalGpuCountedIndirectStatus
            && a.metalGpuCountedIndirectCountBufferIndex
            == b.metalGpuCountedIndirectCountBufferIndex
            && a.metalGpuCountedIndirectUsesDrawId == b.metalGpuCountedIndirectUsesDrawId
            && a.metalGpuCountedIndirectMasksOutOfRange == b.metalGpuCountedIndirectMasksOutOfRange
            && a.metalTessellationContractVersion == b.metalTessellationContractVersion
            && a.metalTessellationStatus == b.metalTessellationStatus
            && a.metalTessellationVertexEntryPoint == b.metalTessellationVertexEntryPoint
            && a.metalTessellationFactorBufferIndex == b.metalTessellationFactorBufferIndex
            && a.metalTessellationControlPointCount == b.metalTessellationControlPointCount
            && a.metalTessellationPatchType == b.metalTessellationPatchType
            && a.metalTessellationFactorFormat == b.metalTessellationFactorFormat
            && a.metalTessellationFactorBytesPerPatch == b.metalTessellationFactorBytesPerPatch
            && a.metalTessellationFactorInstanceStrideBytes
            == b.metalTessellationFactorInstanceStrideBytes
            && sequence(a.descriptorBindings, b.descriptorBindings)
            && ShaderPushConstantAbiIdentity::matchesMembers(
                a.pushConstantMembers, b.pushConstantMembers);
    }

    static bool matches(const ShaderPackageDesc& a, const ShaderPackageDesc& b) noexcept
    {
        return a.label == b.label && a.slangModulePath == b.slangModulePath
            && a.backendFallbacksHiddenFromUpperLayer == b.backendFallbacksHiddenFromUpperLayer
            && a.nativeHandleExposedToUpperLayer == b.nativeHandleExposedToUpperLayer
            && a.generatedReflectionHydrated == b.generatedReflectionHydrated
            && generatedShaderPackageArtifactsEqual(a, b) && sequence(a.entrypoints, b.entrypoints)
            && matches(a.artifacts, b.artifacts) && matches(a.reflection, b.reflection);
    }

    static bool matches(const BlendComponent& a, const BlendComponent& b) noexcept
    {
        return a.sourceFactor == b.sourceFactor && a.destinationFactor == b.destinationFactor
            && a.operation == b.operation;
    }

    static bool matches(const ColorTargetState& a, const ColorTargetState& b) noexcept
    {
        return a.format == b.format && a.blendEnabled == b.blendEnabled
            && a.writeMask == b.writeMask && a.resolveTargetRequired == b.resolveTargetRequired
            && matches(a.colorBlend, b.colorBlend) && matches(a.alphaBlend, b.alphaBlend);
    }

    static bool matches(const StencilFaceState& a, const StencilFaceState& b) noexcept
    {
        return a.failOp == b.failOp && a.depthFailOp == b.depthFailOp && a.passOp == b.passOp
            && a.compare == b.compare;
    }

    static bool matches(const DepthStencilState& a, const DepthStencilState& b) noexcept
    {
        return a.format == b.format && a.depthTestEnabled == b.depthTestEnabled
            && a.depthWriteEnabled == b.depthWriteEnabled && a.depthCompare == b.depthCompare
            && a.stencilEnabled == b.stencilEnabled && a.stencilReadMask == b.stencilReadMask
            && a.stencilWriteMask == b.stencilWriteMask && a.depthBiasEnabled == b.depthBiasEnabled
            && pipelineFloatBits(a.depthBiasConstantFactor)
            == pipelineFloatBits(b.depthBiasConstantFactor)
            && pipelineFloatBits(a.depthBiasClamp) == pipelineFloatBits(b.depthBiasClamp)
            && pipelineFloatBits(a.depthBiasSlopeFactor)
            == pipelineFloatBits(b.depthBiasSlopeFactor)
            && matches(a.front, b.front) && matches(a.back, b.back);
    }

    static bool matches(const MultisampleState& a, const MultisampleState& b) noexcept
    {
        return a.sampleCount == b.sampleCount && a.sampleMask == b.sampleMask
            && a.alphaToCoverageEnabled == b.alphaToCoverageEnabled;
    }

    static bool matches(const ViewportState& a, const ViewportState& b) noexcept
    {
        return pipelineFloatBits(a.x) == pipelineFloatBits(b.x)
            && pipelineFloatBits(a.y) == pipelineFloatBits(b.y)
            && pipelineFloatBits(a.width) == pipelineFloatBits(b.width)
            && pipelineFloatBits(a.height) == pipelineFloatBits(b.height)
            && pipelineFloatBits(a.minDepth) == pipelineFloatBits(b.minDepth)
            && pipelineFloatBits(a.maxDepth) == pipelineFloatBits(b.maxDepth);
    }

    static bool matches(const ScissorState& a, const ScissorState& b) noexcept
    {
        return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
    }

    static bool matches(const VertexAttributeDesc& a, const VertexAttributeDesc& b) noexcept
    {
        return a.shaderLocation == b.shaderLocation && a.offsetBytes == b.offsetBytes
            && a.format == b.format;
    }

    static bool matches(const VertexBufferLayoutDesc& a, const VertexBufferLayoutDesc& b) noexcept
    {
        return a.slot == b.slot && a.strideBytes == b.strideBytes && a.inputRate == b.inputRate
            && sequence(a.attributes, b.attributes);
    }

    static bool matches(const SubpassDesc& a, const SubpassDesc& b) noexcept
    {
        return a.colorAttachmentIndices == b.colorAttachmentIndices
            && a.inputAttachmentIndices == b.inputAttachmentIndices
            && a.useDepthStencilAttachment == b.useDepthStencilAttachment
            && a.readDepthInputAttachment == b.readDepthInputAttachment;
    }

    static bool matches(const SubpassDependency& a, const SubpassDependency& b) noexcept
    {
        return a.srcSubpass == b.srcSubpass && a.dstSubpass == b.dstSubpass;
    }

    static bool matches(
        const RenderPassCompatibilityDesc& a, const RenderPassCompatibilityDesc& b) noexcept
    {
        return a.colorAttachmentFormats == b.colorAttachmentFormats
            && a.depthStencilFormat == b.depthStencilFormat && sequence(a.subpasses, b.subpasses)
            && sequence(a.subpassDependencies, b.subpassDependencies);
    }

    static bool matches(const RenderPipelineDesc& a, const RenderPipelineDesc& b) noexcept
    {
        return a.label == b.label && a.layout == b.layout && a.colorFormat == b.colorFormat
            && a.depthFormat == b.depthFormat && a.cullMode == b.cullMode
            && a.frontFace == b.frontFace && a.topology == b.topology
            && a.hasViewport == b.hasViewport && a.hasScissor == b.hasScissor
            && a.dynamicViewportScissor == b.dynamicViewportScissor
            && a.subpassIndex == b.subpassIndex
            && a.patchControlPointCount == b.patchControlPointCount && a.shaderPackage
            && b.shaderPackage && matches(*a.shaderPackage, *b.shaderPackage)
            && matches(a.vertexShader, b.vertexShader)
            && matches(a.fragmentShader, b.fragmentShader)
            && matches(a.tessellationControlShader, b.tessellationControlShader)
            && matches(a.tessellationEvaluationShader, b.tessellationEvaluationShader)
            && sequence(a.colorTargets, b.colorTargets) && matches(a.depthStencil, b.depthStencil)
            && matches(a.multisample, b.multisample) && matches(a.viewport, b.viewport)
            && matches(a.scissor, b.scissor) && sequence(a.vertexBuffers, b.vertexBuffers)
            && matches(a.renderPassCompatibility, b.renderPassCompatibility);
    }

    static bool matches(const ComputePipelineDesc& a, const ComputePipelineDesc& b) noexcept
    {
        return a.label == b.label && a.layout == b.layout && a.shaderPackage && b.shaderPackage
            && matches(*a.shaderPackage, *b.shaderPackage)
            && matches(a.computeShader, b.computeShader);
    }

private:
    template <class Sequence> static bool sequence(const Sequence& a, const Sequence& b) noexcept
    {
        return std::equal(
            a.begin(), a.end(), b.begin(), b.end(), [](const auto& x, const auto& y) noexcept {
                return matches(x, y);
            });
    }
};
} // namespace arp::rhi

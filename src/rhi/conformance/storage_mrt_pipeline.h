#pragma once
#include "core/diagnostic_error.h"
#include "rhi/frontend/pipeline_definition_identity.h"

namespace arp::rhi {
class ConformanceStorageMrtPipeline final {
public:
    static PipelineLayoutDesc layoutDefinition(const ShaderPackageDesc& package)
    {
        auto desc = buildPipelineLayoutDescFromShaderReflection(package.reflection);
        for (auto& set : desc.descriptorLayouts)
            for (auto& binding : set.bindings)
                if (binding.type == DescriptorType::StorageTexture) {
                    binding.storageTextureFormat = TextureFormat::Rgba16Float;
                    binding.storageTextureAccess = StorageTextureAccess::WriteOnly;
                } else if (binding.type == DescriptorType::SampledTexture) {
                    binding.sampledTextureFilterable = false;
                }
        return desc;
    }

    static RenderPipelineDesc pipelineDefinition(
        const PipelineLayout& layout, const ShaderPackageDesc& package, TextureFormat format)
    {
        if (package.entrypoints.size() != 2)
            throw core::DiagnosticError("RHI-CONFORMANCE-STORAGE-MRT-SHADER-ENTRIES");
        RenderPipelineDesc state;
        state.label = "ConformanceStorageMrtWorkload/1";
        state.layout = &layout;
        state.shaderPackage = &package;
        state.vertexShader = package.entrypoints[0];
        state.fragmentShader = package.entrypoints[1];
        state.colorFormat = format;
        state.colorTargets = {{format}, {TextureFormat::Rgba8Unorm}};
        state.dynamicViewportScissor = true;
        return state;
    }

    static ComputePipelineDesc computeDefinition(
        const PipelineLayout& layout, const ShaderPackageDesc& package)
    {
        if (package.entrypoints.size() != 1)
            throw core::DiagnosticError("RHI-CONFORMANCE-STORAGE-MRT-COMPUTE-ENTRIES");
        ComputePipelineDesc state;
        state.label = "Conformance.StorageFill";
        state.layout = &layout;
        state.shaderPackage = &package;
        state.computeShader = package.entrypoints[0];
        return state;
    }
};
} // namespace arp::rhi

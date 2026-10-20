#pragma once
#include "rhi/backends/webgpu/webgpu_scalar_storage_correlation.h"
#include "shader/graphics_triangle_storage_cross_target_proof.h"

namespace arp::rhi_webgpu {
class WebGpuGraphicsTriangleCorrelation final {
public:
    static const char* validateSource(
        const core::pmr::Vector<WebGpuNativePipelineStageFact>& stages,
        core::StringView vertex,
        core::StringView fragment,
        std::uint64_t bytes,
        const std::array<std::uint8_t, core::Sha256::digestByteCount>& digest) noexcept
    {
        if (stages.size() != 2u || !stages[0].moduleIdentity
            || stages[0].moduleIdentity != stages[1].moduleIdentity)
            return "RHI-WEBGPU-ORDINARY-RANGE-SOURCE-MISMATCH";
        for (const auto& stage : stages) {
            if (stage.stage != rhi::ShaderStage::Vertex
                && stage.stage != rhi::ShaderStage::Fragment)
                return "RHI-WEBGPU-ORDINARY-RANGE-SOURCE-MISMATCH";
            if (const auto* id = WebGpuScalarStorageCorrelation::validateSource(stage,
                    stage.stage,
                    stage.stage == rhi::ShaderStage::Vertex ? vertex : fragment,
                    bytes,
                    digest))
                return id;
        }
        if (stages[0].stage == stages[1].stage)
            return "RHI-WEBGPU-ORDINARY-RANGE-SOURCE-MISMATCH";
        return nullptr;
    }

    static const char* validateBinding(const WebGpuNativePipelineBindingFact& fact,
        std::uint32_t groupCount,
        const shader::ShaderReflection& vertex,
        const shader::ShaderReflection& fragment,
        const shader::GraphicsTriangleStorageCrossTargetProof::Facts& proof) noexcept
    {
        // Selected Vertex has no live resource; its explicitly published dead
        // logical binding must match the Fragment's proven resource.
        if (vertex.pushConstantSizeBytes || fragment.pushConstantSizeBytes
            || !vertex.vertexAttributes.empty() || !fragment.vertexAttributes.empty()
            || !vertex.bindings.empty() || vertex.webGpuBindingSpaceGroupCount
            || vertex.webGpuBindingSpaceContractVersion != kWebGpuBindingSpaceContractVersion
            || vertex.webGpuBindingSpaceStatus != "Available"
            || vertex.webGpuBindingSpace.size() != 1u)
            return "RHI-WEBGPU-ORDINARY-RANGE-BINDING-MISMATCH";
        const auto& dead = vertex.webGpuBindingSpace[0];
        if (dead.active || dead.set != proof.fragment.set || dead.binding != proof.fragment.binding)
            return "RHI-WEBGPU-ORDINARY-RANGE-BINDING-MISMATCH";
        return WebGpuScalarStorageCorrelation::validateBinding(fact,
            groupCount,
            fragment,
            proof.fragment.set,
            proof.fragment.binding,
            proof.target.group,
            proof.target.binding,
            rhi::ShaderStage::Fragment,
            rhi::DescriptorType::ReadOnlyStorageBuffer,
            shader::BindingType::ReadOnlyStorageBuffer);
    }
};
} // namespace arp::rhi_webgpu

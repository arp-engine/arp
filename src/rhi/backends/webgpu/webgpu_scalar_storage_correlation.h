#pragma once
#include "rhi/backends/webgpu/webgpu_binding_space_lowering.h"
#include "rhi/backends/webgpu/webgpu_pipeline_ownership.h"
#include "shader/shader_compiler.h"

namespace arp::rhi_webgpu {
// Pure correlation only; callers must independently establish original native
// ownership, complete program proofs, static validation and physical ranges.
class WebGpuScalarStorageCorrelation final {
public:
    static const char* validateSource(const WebGpuNativePipelineStageFact& stage,
        rhi::ShaderStage expectedStage,
        core::StringView entry,
        std::uint64_t sourceBytes,
        const std::array<std::uint8_t, core::Sha256::digestByteCount>& sourceDigest) noexcept
    {
        if (stage.stage != expectedStage || stage.entry != entry || stage.sourceBytes != sourceBytes
            || stage.sourceSha256 != sourceDigest)
            return "RHI-WEBGPU-ORDINARY-RANGE-SOURCE-MISMATCH";
        return nullptr;
    }

    static const char* validateBinding(const WebGpuNativePipelineBindingFact& fact,
        std::uint32_t groupCount,
        const shader::ShaderReflection& reflection,
        std::uint32_t set,
        std::uint32_t binding,
        std::uint32_t group,
        std::uint32_t targetBinding,
        rhi::ShaderStage expectedStage,
        rhi::DescriptorType nativeType,
        shader::BindingType shaderType) noexcept
    {
        if (fact.mapping.logicalSet != set || fact.mapping.logicalBinding != binding
            || fact.mapping.webGpuGroup != group || fact.mapping.webGpuBinding != targetBinding
            || fact.shape.type != nativeType || fact.physicalType != nativeType
            || fact.shape.binding != binding || fact.shape.arrayCount != 1u
            || fact.shape.stages != expectedStage || fact.shape.tableBacked
            || reflection.webGpuBindingSpaceContractVersion != kWebGpuBindingSpaceContractVersion
            || reflection.webGpuBindingSpaceStatus != "Available"
            || reflection.webGpuBindingSpaceGroupCount != groupCount
            || reflection.webGpuBindingSpace.size() != 1u || reflection.bindings.size() != 1u) {
            return "RHI-WEBGPU-ORDINARY-RANGE-BINDING-MISMATCH";
        }
        const auto& logical = reflection.bindings[0];
        const auto& physical = reflection.webGpuBindingSpace[0];
        if (logical.set != set || logical.binding != binding || logical.type != shaderType
            || logical.arrayCount != 1u
            || logical.stageMask != static_cast<std::uint32_t>(expectedStage) || !physical.active
            || physical.set != set || physical.binding != binding || physical.group != group
            || physical.webGpuBinding != targetBinding || physical.physicalType != shaderType
            || physical.arrayElement || physical.capacity != 1u || physical.requiresSampler
            || physical.depthTexture) {
            return "RHI-WEBGPU-ORDINARY-RANGE-BINDING-MISMATCH";
        }
        return nullptr;
    }
};
} // namespace arp::rhi_webgpu

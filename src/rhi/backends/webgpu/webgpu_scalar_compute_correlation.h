#pragma once
#include "rhi/backends/webgpu/webgpu_scalar_storage_correlation.h"
#include "shader/scalar_compute_cross_target_proof.h"

namespace arp::rhi_webgpu {
class WebGpuScalarComputeCorrelation final {
public:
    static const char* validateSource(const WebGpuNativePipelineStageFact& stage,
        core::StringView entry,
        std::uint64_t bytes,
        const std::array<std::uint8_t, core::Sha256::digestByteCount>& digest) noexcept
    {
        return WebGpuScalarStorageCorrelation::validateSource(
            stage, rhi::ShaderStage::Compute, entry, bytes, digest);
    }

    static const char* validateBinding(const WebGpuNativePipelineBindingFact& fact,
        std::uint32_t groupCount,
        const shader::ShaderReflection& reflection,
        const shader::ScalarComputeCrossTargetProof::Facts& proof) noexcept
    {
        return WebGpuScalarStorageCorrelation::validateBinding(fact,
            groupCount,
            reflection,
            proof.binary.set,
            proof.binary.binding,
            proof.target.group,
            proof.target.binding,
            rhi::ShaderStage::Compute,
            rhi::DescriptorType::StorageBuffer,
            shader::BindingType::StorageBuffer);
    }
};
} // namespace arp::rhi_webgpu

#pragma once

#include "rhi/gpu_pipeline.h"
#include "rhi/semantics/push_constant_validation_values.h"

namespace arp::rhi::semantics {

/// Validate the bound layout and its push-constant range before recording.
/// If reflection is provided, its push-constant contract must match the layout.
ValidationResult validatePushConstantBinding(const PipelineLayout* pipelineLayout,
    ShaderStage passType,
    ShaderStage stages,
    std::uint32_t offsetBytes,
    std::uint32_t sizeBytes,
    const ShaderReflectionDesc* reflection = nullptr);

} // namespace arp::rhi::semantics

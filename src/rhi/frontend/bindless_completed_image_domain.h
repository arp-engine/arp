#pragma once
#include "rhi/gpu_bindless.h"

namespace arp::rhi {
// Shared prerequisite for authentic completed color images. This is a resource
// domain, not command authority: publication, authorization, the original graph
// and real completion remain independently required by each public executor.
enum class OriginalBindlessCompletedImageScope { ComputeColor8, SampledGraphics1 };

class OriginalBindlessCompletedImageDomain final {
public:
    using Scope = OriginalBindlessCompletedImageScope;
    static constexpr std::uint32_t contractVersion = 1u;

    static constexpr bool acceptsPlan(const BindlessLoweringPlan& plan, Scope scope) noexcept
    {
        return plan.success && plan.executable && plan.execution == BindlessExecution::Native
            && !plan.originalPreparationContractVersion
            && (scope == Scope::ComputeColor8
                    ? plan.originalExecutionContractVersion == 8u
                    : scope == Scope::SampledGraphics1 && !plan.originalExecutionContractVersion);
    }

    static bool acceptsStages(Scope scope, DescriptorType type, ShaderStage stages) noexcept
    {
        if (scope == Scope::ComputeColor8)
            return stages == ShaderStage::Compute;
        if (scope != Scope::SampledGraphics1)
            return false;
        if (type == DescriptorType::SampledTexture || type == DescriptorType::Sampler)
            return stages == ShaderStage::Fragment;
        return stages != ShaderStage::None
            && (stages & (ShaderStage::Vertex | ShaderStage::Fragment)) == stages;
    }

    static constexpr bool acceptsTexture(TextureFormat format,
        TextureViewDimension dimension,
        TextureAspect aspect,
        std::uint32_t samples) noexcept
    {
        return format == TextureFormat::Rgba8Unorm && aspect == TextureAspect::Color
            && samples == 1u
            && (dimension == TextureViewDimension::Texture2D
                || dimension == TextureViewDimension::Texture2DArray);
    }

    static BindlessOperation sampledOperations(Scope scope) noexcept
    {
        using Op = BindlessOperation;
        return scope == Scope::ComputeColor8 ? Op::Load | Op::Query | Op::SampleLevel
            : scope == Scope::SampledGraphics1
            ? Op::Load | Op::Query | Op::SampleLevel | Op::SampleGrad
            : Op::None;
    }
};
} // namespace arp::rhi

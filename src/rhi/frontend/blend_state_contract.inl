#pragma once

// Included by gpu_pipeline.h after complete public blend declarations.

namespace arp::rhi {

// ColorBlendFactors/1. Descriptor admission is shared by all creation paths;
// native API enum translation remains private to each backend.
class BlendStateContract final {
public:
    static constexpr std::uint32_t version = 1;

    static constexpr bool factor(BlendFactor value) noexcept
    {
        switch (value) {
        case BlendFactor::Zero:
        case BlendFactor::One:
        case BlendFactor::SrcAlpha:
        case BlendFactor::OneMinusSrcAlpha:
        case BlendFactor::DstAlpha:
        case BlendFactor::OneMinusDstAlpha:
        case BlendFactor::SrcColor:
        case BlendFactor::OneMinusSrcColor:
        case BlendFactor::DstColor:
        case BlendFactor::OneMinusDstColor:
            return true;
        }
        return false;
    }

    static constexpr const char* validate(const BlendComponent& blend) noexcept
    {
        if (!factor(blend.sourceFactor) || !factor(blend.destinationFactor))
            return "RHI-PIPELINE-BLEND-FACTOR";
        switch (blend.operation) {
        case BlendOp::Add:
        case BlendOp::Subtract:
        case BlendOp::ReverseSubtract:
        case BlendOp::Min:
        case BlendOp::Max:
            return nullptr;
        }
        return "RHI-PIPELINE-BLEND-OPERATION";
    }
};
} // namespace arp::rhi

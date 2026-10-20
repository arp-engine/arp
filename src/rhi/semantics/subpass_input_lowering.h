#pragma once

#include "rhi/gpu_capabilities.h"
#include "rhi/gpu_resources.h"

#include <cstdint>

namespace arp::rhi::semantics {

/// Code-owned lowering selected for a logical SubpassGraph. The renderer
/// consumes this result and never selects a backend or shader target itself.
enum class SubpassInputLoweringMode {
    Native,
    SeparatePass,
    DiagnosticFailure,
};

/// Physical resource contract selected by code-owned subpass lowering. The
/// logical shader declaration is identical for every backend; this describes
/// what the compiler/RHI must execute after capability selection.
enum class SubpassInputPhysicalBinding {
    NativeInputAttachment,
    SampledTexture,
    Unsupported,
};

enum class SubpassInputLoadSemantics {
    CurrentFragment,
};

inline constexpr std::uint32_t kInvalidSubpassShaderBinding = UINT32_MAX;

/// Explicit logical shader binding selected by the render graph.  The graph
/// owns the attachment identity; the shader package owns the reflected
/// binding.  Keeping both in this code-level record prevents either backend
/// from inferring a route from a resource or descriptor name.
struct SubpassInputBindingReference {
    std::uint32_t set = kInvalidSubpassShaderBinding;
    std::uint32_t binding = kInvalidSubpassShaderBinding;
    std::uint32_t logicalAttachmentIndex = kInvalidSubpassShaderBinding;
    SubpassInputPhysicalBinding physicalBinding = SubpassInputPhysicalBinding::Unsupported;
    bool depthTexture = false;

    bool specified() const noexcept
    {
        return set != kInvalidSubpassShaderBinding && binding != kInvalidSubpassShaderBinding;
    }
};

/// Resource route produced by FrameGraph after the source attachment view has
/// been materialized. The logical binding remains separate from the physical
/// texture-view identity so descriptor consumers cannot infer either one.
struct SubpassInputDescriptorRoute {
    SubpassInputBindingReference shaderBinding;
    GpuResourceId sourceTexture;
    GpuResourceId textureView;
    std::uint32_t loweringId = UINT32_MAX;
};

struct SubpassInputLoweringContract {
    SubpassInputLoweringMode mode = SubpassInputLoweringMode::DiagnosticFailure;
    SubpassInputPhysicalBinding physicalBinding = SubpassInputPhysicalBinding::Unsupported;
    SubpassInputLoadSemantics loadSemantics = SubpassInputLoadSemantics::CurrentFragment;
    bool requiresSampler = false;
    bool executable = false;
};

inline SubpassInputLoweringMode subpassInputLoweringMode(const DeviceCapabilities& caps) noexcept
{
    if (caps.features.subpassInputAttachment) {
        return SubpassInputLoweringMode::Native;
    }
    if (caps.features.subpassInputAttachmentDegraded) {
        return SubpassInputLoweringMode::SeparatePass;
    }
    return SubpassInputLoweringMode::DiagnosticFailure;
}

inline SubpassInputLoweringContract subpassInputLoweringContract(
    const DeviceCapabilities& caps) noexcept
{
    const auto mode = subpassInputLoweringMode(caps);
    switch (mode) {
    case SubpassInputLoweringMode::Native:
        return {
            mode,
            SubpassInputPhysicalBinding::NativeInputAttachment,
            SubpassInputLoadSemantics::CurrentFragment,
            false,
            true,
        };
    case SubpassInputLoweringMode::SeparatePass:
        return {
            mode,
            SubpassInputPhysicalBinding::SampledTexture,
            SubpassInputLoadSemantics::CurrentFragment,
            // Current-fragment lowering uses textureLoad, which addresses a
            // texel directly and therefore does not require a sampler.
            false,
            true,
        };
    case SubpassInputLoweringMode::DiagnosticFailure:
        return {};
    }
    return {};
}

} // namespace arp::rhi::semantics

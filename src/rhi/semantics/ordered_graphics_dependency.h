#pragma once

#include "rhi/frontend/resource_state_sequence.h"
#include "rhi/semantics/graphics_workload_requirements.h"

namespace arp::rhi::semantics {
// OrderedGraphicsDependency/1. Same-state whole-attachment dependencies use
// original command order, not completion. The native issuer retains the exact
// dependency position and resource owner until submission validation retires.
class OrderedGraphicsDependency final {
public:
    static const char* memory(Backend backend,
        std::uint32_t version,
        bool optIn,
        const ResourceMemoryBarrierDesc& desc,
        ResourceState state) noexcept
    {
        if (version != 1)
            return "RHI-ORDERED-GRAPHICS-DEPENDENCY-VERSION";
        if (!optIn)
            return "RHI-ORDERED-GRAPHICS-DEPENDENCY-OPT-IN";
        if (const auto* id = GraphicsWorkloadRequirementsContract::orderedLowering(backend))
            return id;
        if (const auto* id = RecordedResourceStateSequence::validateMemoryBarrier(desc, state))
            return id;
        if (state != ResourceState::ColorAttachment && state != ResourceState::DepthAttachment
            && state != ResourceState::ShaderRead)
            return "RHI-ORDERED-GRAPHICS-DEPENDENCY-STATE";
        return nullptr;
    }
};
} // namespace arp::rhi::semantics

#pragma once

#include "rhi/conformance/sampled_workload.h"
#include "rhi/frontend/resource_access_scope.h"
#include "rhi/gpu_pipeline.h"
#include "rhi/gpu_resources.h"

#include <cstring>

namespace arp::rhi {
// Shared semantic predicates. Native identity and emission remain backend-owned.
class ConformanceSampledCommandContract final {
public:
    static bool imageScopes(const ResourceBarrierDesc& desc) noexcept
    {
        for (const auto& scope :
            {semantics::ResourceAccessScope{desc.before, desc.sourceStages, desc.sourceAccess},
                semantics::ResourceAccessScope{
                    desc.after, desc.destinationStages, desc.destinationAccess}}) {
            if (scope.state == ResourceState::Undefined) {
                if (scope.stages != PipelineStage::TopOfPipe
                    || scope.access != ResourceAccess::None)
                    return false;
                continue;
            }
            if (semantics::validateResourceAccessScope(scope))
                return false;
            switch (scope.state) {
            case ResourceState::ColorAttachment:
                if ((scope.access & ResourceAccess::ColorAttachmentWrite) == ResourceAccess::None)
                    return false;
                break;
            case ResourceState::ShaderRead:
                if ((scope.stages & PipelineStage::FragmentShader) == PipelineStage::None)
                    return false;
                break;
            case ResourceState::Present:
                break;
            default:
                return false;
            }
        }
        return true;
    }

    static bool viewport(const ViewportState& v, Extent2D extent) noexcept
    {
        return v.x == 0 && v.y == 0 && v.width == extent.width && v.height == extent.height
            && v.minDepth == 0 && v.maxDepth == 1;
    }

    static bool scissor(const ScissorState& s, Extent2D extent) noexcept
    {
        return s.x == 0 && s.y == 0 && s.width == extent.width && s.height == extent.height;
    }

    static bool push(const ConformanceSampledDraw& draw,
        ShaderStage stages,
        std::uint32_t offset,
        std::uint32_t size,
        const void* bytes) noexcept
    {
        return stages == (ShaderStage::Vertex | ShaderStage::Fragment) && !offset
            && size == sizeof(draw.push) && bytes && !std::memcmp(bytes, draw.push.data(), size);
    }

    static bool draw(const ConformanceSampledDraw& draw,
        bool indexed,
        std::uint32_t count,
        std::uint32_t instances,
        std::uint32_t first,
        std::int32_t offset,
        std::uint32_t firstInstance,
        std::uint32_t bindings) noexcept
    {
        return indexed == draw.indexed && count == 3 && instances == 1 && !first && !offset
            && !firstInstance && bindings == (indexed ? 127u : 63u);
    }
};
} // namespace arp::rhi

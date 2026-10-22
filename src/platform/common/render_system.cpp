#include "platform/render_system.h"

#include "platform/render_system_ordered_profile.h"

namespace arp::platform {
OrderedRenderSystemCapabilities RenderSystem::orderedGraphicsCapabilities() const
{
    return {};
}

std::shared_ptr<rhi::OriginalOrderedGraphicsExecution>
RenderSystem::orderedGraphicsExecution() const
{
    return {};
}

std::shared_ptr<rhi::RenderPipeline> RenderSystem::createOrderedPipeline(
    const OrderedPipelineLoadDesc&, core::pmr::String& diagnostic)
{
    diagnostic = "PLATFORM-ORDERED-PROFILE-BACKEND-UNAVAILABLE";
    return {};
}

RenderSystemOrderedGraphicsFrameReport RenderSystem::renderOrderedFrame(
    OrderedGraphicsFrameWorkload&)
{
    RenderSystemOrderedGraphicsFrameReport report;
    report.diagnostics = "PLATFORM-ORDERED-PROFILE-BACKEND-UNAVAILABLE";
    return report;
}

RenderSystemResult RenderSystem::setSurfaceAvailable(bool available)
{
    if (!valid() || !swapchain())
        return {false, "RHI-PLATFORM-SURFACE-MISSING"};
    const auto result = swapchain()->setSurfaceAvailable(available);
    return {result.success, result.diagnostics};
}

RenderSystemResult RenderSystem::synchronizeSurfaceLifecycle()
{
    return {false, "PLATFORM-ORDERED-PROFILE-BACKEND-UNAVAILABLE"};
}
} // namespace arp::platform

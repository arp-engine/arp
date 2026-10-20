#pragma once

#include "core/cpu_allocator.h"
#include "rhi/gpu_common.h"

#include <cstdint>

namespace arp::rhi {

/// Logical description of one subpass in a render-pass compatibility contract.
/// Attachment indices refer to RenderPassCompatibilityDesc::colorAttachmentFormats.
struct SubpassDesc {
    std::pmr::vector<std::uint32_t> colorAttachmentIndices{&core::defaultCpuMemoryResource()};
    std::pmr::vector<std::uint32_t> inputAttachmentIndices{&core::defaultCpuMemoryResource()};
    bool useDepthStencilAttachment = false;
    bool readDepthInputAttachment = false;
};

/// Explicit ordering dependency between two logical subpasses.
struct SubpassDependency {
    std::uint32_t srcSubpass = 0;
    std::uint32_t dstSubpass = 0;
};

/// Render-pass compatibility information required by APIs where a graphics
/// pipeline is created against a concrete render pass (Vulkan).
///
/// RenderPipelineDesc::colorTargets describes the attachments written by the
/// pipeline's selected subpass. This descriptor describes the complete
/// render pass, including attachments written by other subpasses. Keeping both
/// contracts explicit prevents a pipeline built for a single-subpass
/// throwaway render pass from being bound inside a native multi-subpass pass.
struct RenderPassCompatibilityDesc {
    static constexpr std::uint32_t kMaxSubpasses = 9u;

    std::pmr::vector<TextureFormat> colorAttachmentFormats{&core::defaultCpuMemoryResource()};
    TextureFormat depthStencilFormat = TextureFormat::Unknown;
    std::pmr::vector<SubpassDesc> subpasses{&core::defaultCpuMemoryResource()};
    std::pmr::vector<SubpassDependency> subpassDependencies{&core::defaultCpuMemoryResource()};

    bool enabled() const noexcept { return !subpasses.empty(); }
};

} // namespace arp::rhi

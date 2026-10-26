#pragma once
#include "rhi/backends/metal/metal_original_buffer_limits.h"
#include "rhi/semantics/native_backend_capabilities.h"

namespace arp::rhi_metal {
class MetalRhiDevice;

class MetalAcceptedOriginalGraphicsPipelineFactory final {
    friend class MetalRhiDevice;
    static const char* validate(const rhi::GpuDevice&, const rhi::RenderPipeline&) noexcept;
    static core::UniquePtr<rhi::RenderPipeline> create(const rhi::GpuDevice&,
        const rhi::semantics::NativeBackendCapabilityContract&,
        const rhi::RenderPipelineDesc&,
        const std::shared_ptr<const rhi::OriginalGraphicsPipelineCreation>&,
        const std::shared_ptr<core::CpuMemoryQuota>&,
        const char*&);
};
} // namespace arp::rhi_metal

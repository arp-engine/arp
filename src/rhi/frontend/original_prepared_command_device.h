#pragma once

#include "rhi/frontend/command_render_pass_preparation.h"
#include "rhi/frontend/pipeline_synchronous_graphics_creation.h"
#include "rhi/gpu_device.h"

namespace arp::rhi {

// Explicit Prototype extension, independently selected by the caller. This
// exposes existing original factories to renderer without backend handles,
// backend casts, native parameter descriptors or public capability promotion.
class OriginalPreparedCommandDeviceExtension {
public:
    static constexpr std::string_view id = "arp.rhi.original-prepared-commands/1";
    virtual ~OriginalPreparedCommandDeviceExtension() = default;
    virtual const GpuDevice& originalDevice() const noexcept = 0;

    static const char* validateRequest(const OriginalRenderPassPreparationRequest& request) noexcept
    {
        if (const auto* diagnostic = OriginalRenderPassPreparationContract::validate(request))
            return diagnostic;
        return request.contractVersion == 6u || request.contractVersion == 7u
                || OriginalRenderPassPreparationContract::hasPassTiming(request)
            ? nullptr
            : "RHI-ORIGINAL-PREPARED-EXTENSION-PROFILE";
    }

    static OriginalPreparedCommandDeviceExtension* query(GpuDevice& device,
        const OriginalRenderPassPreparationRequest& request,
        const char*& diagnostic) noexcept
    {
        diagnostic = validateRequest(request);
        if (diagnostic)
            return nullptr;
        const auto decision =
            OriginalRenderPassPreparationContract::lowering(device.backend(), request);
        if (!decision.recordingAvailable) {
            diagnostic = decision.diagnostic;
            return nullptr;
        }
        if ((diagnostic = device.originalDeviceLifetimeTicket().diagnostic()))
            return nullptr;
        auto* extension =
            static_cast<OriginalPreparedCommandDeviceExtension*>(device.queryInterface(id));
        if (!extension || &extension->originalDevice() != &device) {
            diagnostic = "RHI-ORIGINAL-PREPARED-EXTENSION-UNAVAILABLE";
            return nullptr;
        }
        return extension;
    }

    virtual std::shared_ptr<const OriginalGraphicsPipelineCreation> createGraphics(
        const std::shared_ptr<const OriginalGraphicsPipelineInput>&,
        const std::shared_ptr<PipelineLayout>&,
        const RenderPipelineDesc&,
        OriginalSynchronousGraphicsCreationContract::Budget,
        const std::shared_ptr<core::CpuMemoryQuota>& ownerQuota,
        const std::shared_ptr<core::CpuMemoryQuota>& nativeQuota,
        const char*& diagnostic) = 0;

    virtual core::UniquePtr<CommandEncoder> prepare(
        std::shared_ptr<const OriginalCommandStreamProgram>,
        const OriginalRenderPassPreparationRequest&,
        core::pmr::String& diagnostic) = 0;
};

} // namespace arp::rhi

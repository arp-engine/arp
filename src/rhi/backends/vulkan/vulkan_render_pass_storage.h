#pragma once

#include "rhi/frontend/command_render_pass_preparation.h"
#include "rhi/frontend/command_render_pass_storage.h"

namespace arp::rhi_vulkan {

class VulkanRhiRuntime;

// Native object cardinality for cold per-literal-pass ownership. This is not
// a byte footprint, a native creation result or permission to record a pass.
class VulkanRhiRenderPassStorageProjection final {
public:
    struct Result {
        const char* diagnostic = nullptr;
        rhi::OriginalRenderPassStorageContract::Counts inputs;
        std::uint64_t renderPasses = 0u, framebuffers = 0u, imageViews = 0u, nativeObjects = 0u;
    };

    static const char* nativeObjectCount(
        std::uint64_t passes, std::uint64_t attachmentReferences, std::uint64_t& output) noexcept
    {
        // One VkRenderPass and one VkFramebuffer per literal pass, plus one
        // VkImageView for each exact color/resolve/depth attachment reference.
        if (!core::canAddUnsigned(passes, passes)
            || !core::canAddUnsigned(passes + passes, attachmentReferences))
            return "RHI-VULKAN-COLD-RENDER-PASS-OBJECT-OVERFLOW";
        output = passes + passes + attachmentReferences;
        return nullptr;
    }

    static Result inspect(const std::shared_ptr<const rhi::OriginalCommandStreamProgram>& program,
        const rhi::OriginalRenderPassStorageContract::Budget& budget,
        std::uint64_t maxNativeObjects) noexcept
    {
        const auto inputs = rhi::OriginalRenderPassStorageContract::inspect(program, budget);
        if (inputs.diagnostic)
            return {inputs.diagnostic};
        std::uint64_t objects = 0u;
        if (const auto* id = nativeObjectCount(
                inputs.counts.passes, inputs.counts.attachmentReferences, objects))
            return {id};
        if (objects > maxNativeObjects)
            return {"RHI-VULKAN-COLD-RENDER-PASS-OBJECT-CAPACITY"};
        return {nullptr,
            inputs.counts,
            inputs.counts.passes,
            inputs.counts.passes,
            inputs.counts.attachmentReferences,
            objects};
    }

    // Authenticate the existing canonical view/parent association against the
    // exact common device and real Vulkan image payload. No native pass/view is
    // created; sample policy, pass compatibility and execution are separate.
    static Result inspectRegistered(const std::shared_ptr<const rhi::OriginalCommandStreamProgram>&,
        const rhi::OriginalRenderPassStorageContract::Budget&,
        std::uint64_t maxNativeObjects,
        const rhi::GpuDevice&,
        const VulkanRhiRuntime&) noexcept;

    // Complete cold profile/entrypoint/format admission before any creation.
    static Result inspectPreparation(
        const std::shared_ptr<const rhi::OriginalCommandStreamProgram>&,
        const rhi::OriginalRenderPassPreparationRequest&,
        const rhi::GpuDevice&,
        const VulkanRhiRuntime&) noexcept;
};

} // namespace arp::rhi_vulkan

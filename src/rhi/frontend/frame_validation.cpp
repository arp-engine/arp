#include "core/diagnostics/diagnostic_builder.h"
#include "core/memory/cpu_allocator.h"
#include "rhi/gpu_swapchain.h"

namespace arp::rhi {

namespace {

RhiFrameContractValidationResult resultFrom(std::pmr::string diagnostics)
{
    RhiFrameContractValidationResult result;
    result.success = diagnostics.empty();
    result.diagnostics = diagnostics;
    return result;
}

bool isNonZero(const Extent2D& extent)
{
    return extent.width > 0 && extent.height > 0;
}

bool isNonZero(const Extent3D& extent)
{
    return extent.width > 0 && extent.height > 0 && extent.depth > 0;
}

bool isColorFormat(TextureFormat format)
{
    return format == TextureFormat::Bgra8Unorm || format == TextureFormat::Rgba8Unorm
        || format == TextureFormat::Rgba8UnormSrgb || format == TextureFormat::R8Unorm
        || format == TextureFormat::Rgba16Float || format == TextureFormat::Rg16Float;
}

bool isDepthFormat(TextureFormat format)
{
    return format == TextureFormat::D32Float || format == TextureFormat::Depth24Stencil8;
}

bool hasUsage(TextureUsage value, TextureUsage flag)
{
    return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(flag)) != 0;
}

void validateAcquireLifecycleStatus(const AcquiredFrame& frame, std::pmr::string& diagnostics)
{
    if (frame.status == AcquireStatus::Unknown) {
        arp::core::appendDiagnostic(
            diagnostics, "RHI acquire result requires an explicit lifecycle status");
        return;
    }
    const bool expectedRecoverable = acquireStatusIsRecoverable(frame.status);
    if (frame.recoverable != expectedRecoverable) {
        arp::core::appendDiagnostic(
            diagnostics, "RHI acquire recoverable flag must match lifecycle status");
    }
    if (frame.swapchainRebuildRequired != acquireStatusRequiresSwapchainRebuild(frame.status)) {
        arp::core::appendDiagnostic(
            diagnostics, "RHI acquire swapchain rebuild flag must match lifecycle status");
    }
    if (frame.surfaceLost != (frame.status == AcquireStatus::SurfaceLost)) {
        arp::core::appendDiagnostic(
            diagnostics, "RHI acquire surface lost flag must match lifecycle status");
    }
}

void validatePresentLifecycleStatus(const PresentResult& present, std::pmr::string& diagnostics)
{
    if (present.status == PresentStatus::Unknown) {
        arp::core::appendDiagnostic(
            diagnostics, "RHI present result requires an explicit lifecycle status");
        return;
    }
    const bool expectedRecoverable = presentStatusIsRecoverable(present.status);
    if (present.recoverable != expectedRecoverable) {
        arp::core::appendDiagnostic(
            diagnostics, "RHI present recoverable flag must match lifecycle status");
    }
    if (present.swapchainRebuildRequired != presentStatusRequiresSwapchainRebuild(present.status)) {
        arp::core::appendDiagnostic(
            diagnostics, "RHI present swapchain rebuild flag must match lifecycle status");
    }
    if (present.surfaceLost != (present.status == PresentStatus::SurfaceLost)) {
        arp::core::appendDiagnostic(
            diagnostics, "RHI present surface lost flag must match lifecycle status");
    }
}

void validateRendererCommon(const RendererInputContract& input, std::pmr::string& diagnostics)
{
    if (!input.pipelineLayoutKnown) {
        arp::core::appendDiagnostic(diagnostics, "renderer input requires a known pipeline layout");
    }
    if (!input.explicitResourceStates) {
        arp::core::appendDiagnostic(
            diagnostics, "renderer input requires explicit resource states");
    }
}

} // namespace

RhiFrameContractValidator::RhiFrameContractValidator(const RhiFrameContractValidatorDesc& desc)
    : desc_(desc)
{}

RhiFrameContractValidationResult RhiFrameContractValidator::validateAcquireResult(
    const AcquiredFrame& frame) const
{
    std::pmr::string diagnostics;
    if (desc_.logicalFrameSlots != 2) {
        arp::core::appendDiagnostic(
            diagnostics, "RHI frame contract requires exactly two logical frame slots");
    }
    validateAcquireLifecycleStatus(frame, diagnostics);
    if (!frame.success) {
        if (frame.diagnostics.empty()) {
            arp::core::appendDiagnostic(
                diagnostics, "RHI acquire failure requires deterministic diagnostics");
        }
        if (frame.status != AcquireStatus::Failed && frame.status != AcquireStatus::OutOfDate
            && frame.status != AcquireStatus::SurfaceLost) {
            arp::core::appendDiagnostic(diagnostics,
                "RHI acquire failure status must be failed, out-of-date, or surface-lost");
        }
        return resultFrom(diagnostics);
    }
    if (frame.status != AcquireStatus::Acquired && frame.status != AcquireStatus::Suboptimal) {
        arp::core::appendDiagnostic(
            diagnostics, "RHI successful acquire status must be acquired or suboptimal");
    }
    if (!frame.diagnostics.empty()) {
        arp::core::appendDiagnostic(
            diagnostics, "RHI successful acquire result must not carry diagnostics");
    }
    if (frame.frameSerial == 0) {
        arp::core::appendDiagnostic(
            diagnostics, "RHI acquire result requires a non-zero frame serial");
    }
    if (frame.frameIndex >= desc_.logicalFrameSlots) {
        arp::core::appendDiagnostic(
            diagnostics, "RHI acquire result frame index is outside the logical frame slots");
    }
    return resultFrom(diagnostics);
}

RhiFrameContractValidationResult RhiFrameContractValidator::validatePresentResult(
    const AcquiredFrame& acquired, const PresentResult& present) const
{
    std::pmr::string diagnostics = validateAcquireResult(acquired).diagnostics;
    validatePresentLifecycleStatus(present, diagnostics);
    if (!present.success) {
        if (present.diagnostics.empty()) {
            arp::core::appendDiagnostic(
                diagnostics, "RHI present failure requires deterministic diagnostics");
        }
        if (present.status != PresentStatus::Failed && present.status != PresentStatus::OutOfDate
            && present.status != PresentStatus::SurfaceLost) {
            arp::core::appendDiagnostic(diagnostics,
                "RHI present failure status must be failed, out-of-date, or surface-lost");
        }
        return resultFrom(diagnostics);
    }
    if (present.status != PresentStatus::Presented && present.status != PresentStatus::Suboptimal) {
        arp::core::appendDiagnostic(
            diagnostics, "RHI successful present status must be presented or suboptimal");
    }
    if (!present.diagnostics.empty()) {
        arp::core::appendDiagnostic(
            diagnostics, "RHI successful present result must not carry diagnostics");
    }
    if (present.frameSerial == 0) {
        arp::core::appendDiagnostic(
            diagnostics, "RHI present result requires a non-zero frame serial");
    }
    if (present.frameSerial != acquired.frameSerial) {
        arp::core::appendDiagnostic(
            diagnostics, "RHI present result frame serial must match the acquired frame");
    }
    if (present.frameIndex != acquired.frameIndex) {
        arp::core::appendDiagnostic(
            diagnostics, "RHI present result frame index must match the acquired frame");
    }
    return resultFrom(diagnostics);
}

RhiFrameContractValidationResult RhiFrameContractValidator::validateSwapchainImage(
    const SwapchainImageRef& image) const
{
    std::pmr::string diagnostics;
    if (!image.image.isValid()) {
        arp::core::appendDiagnostic(diagnostics, "RHI swapchain image requires a resource id");
    }
    if (image.frameSerial == 0) {
        arp::core::appendDiagnostic(diagnostics, "RHI swapchain image requires a frame serial");
    }
    if (image.imageIndex >= desc_.logicalFrameSlots) {
        arp::core::appendDiagnostic(
            diagnostics, "RHI swapchain image index is outside the logical frame slots");
    }
    if (!isNonZero(image.extent)) {
        arp::core::appendDiagnostic(diagnostics, "RHI swapchain image requires non-zero extent");
    }
    if (!isColorFormat(image.format)) {
        arp::core::appendDiagnostic(diagnostics, "RHI swapchain image requires a color format");
    }
    if (image.state != ResourceState::Present && image.state != ResourceState::ColorAttachment) {
        arp::core::appendDiagnostic(
            diagnostics, "RHI swapchain image state must be present or color attachment");
    }
    return resultFrom(diagnostics);
}

RhiFrameContractValidationResult RhiFrameContractValidator::validateRenderOutput(
    const RenderOutputDesc& output) const
{
    std::pmr::string diagnostics;
    if (!output.color.target.isValid()) {
        arp::core::appendDiagnostic(diagnostics, "RHI render output requires a color target");
    }
    if (!isNonZero(output.color.extent)) {
        arp::core::appendDiagnostic(
            diagnostics, "RHI render output requires non-zero color extent");
    }
    if (!isColorFormat(output.color.format)) {
        arp::core::appendDiagnostic(diagnostics, "RHI render output requires a color format");
    }
    if (output.presentFormat == TextureFormat::Unknown) {
        arp::core::appendDiagnostic(diagnostics, "RHI render output requires a present format");
    }
    if (output.presentFormat != TextureFormat::Unknown
        && output.color.format != output.presentFormat) {
        arp::core::appendDiagnostic(
            diagnostics, "RHI render output color format must match present format");
    }
    if (output.color.finalState != ResourceState::Present
        && output.color.finalState != ResourceState::ColorAttachment
        && output.color.finalState != ResourceState::ShaderRead) {
        arp::core::appendDiagnostic(
            diagnostics, "RHI render output color target final state is unsupported");
    }
    if (output.hasDepth) {
        if (!output.depth.target.isValid()) {
            arp::core::appendDiagnostic(diagnostics, "RHI render output requires a depth target");
        }
        if (!isNonZero(output.depth.extent)) {
            arp::core::appendDiagnostic(
                diagnostics, "RHI render output requires non-zero depth extent");
        }
        if (!isDepthFormat(output.depth.format)) {
            arp::core::appendDiagnostic(diagnostics, "RHI render output requires a depth format");
        }
        if (output.depth.finalState != ResourceState::DepthAttachment
            && output.depth.finalState != ResourceState::ShaderRead) {
            arp::core::appendDiagnostic(
                diagnostics, "RHI render output depth target final state is unsupported");
        }
    }
    return resultFrom(diagnostics);
}

RhiFrameContractValidationResult RhiFrameContractValidator::validateTextureUploadMetadata(
    const TextureUploadMetadata& upload) const
{
    std::pmr::string diagnostics;
    if (upload.layoutSource != TextureUploadLayoutSource::ExplicitMetadata) {
        arp::core::appendDiagnostic(
            diagnostics, "RHI explicit texture upload metadata is required");
    }
    if (!upload.destinationTexture.isValid()) {
        arp::core::appendDiagnostic(
            diagnostics, "RHI texture upload requires a destination texture");
    }
    if (!isNonZero(upload.extent)) {
        arp::core::appendDiagnostic(diagnostics, "RHI texture upload requires non-zero extent");
    }
    if (!isColorFormat(upload.format) && !isDepthFormat(upload.format)) {
        arp::core::appendDiagnostic(diagnostics, "RHI texture upload requires a known format");
    }
    if (upload.bytesPerRow == 0 || upload.rowsPerImage == 0) {
        arp::core::appendDiagnostic(
            diagnostics, "RHI texture upload requires explicit row and image pitch");
    }
    if (upload.bytesPerRow != 0 && (upload.bytesPerRow % 256u) != 0u) {
        arp::core::appendDiagnostic(
            diagnostics, "RHI texture upload row pitch must be 256-byte aligned");
    }
    if (upload.subresources.mipLevelCount == 0 || upload.subresources.arrayLayerCount == 0) {
        arp::core::appendDiagnostic(
            diagnostics, "RHI texture upload requires explicit mip and layer range");
    }
    if (upload.stagingIntent != StagingIntent::Upload || !upload.backendOwnedStaging) {
        arp::core::appendDiagnostic(
            diagnostics, "RHI texture upload requires backend-owned upload staging intent");
    }
    return resultFrom(diagnostics);
}

RhiFrameContractValidationResult RhiFrameContractValidator::validateMeshBufferUploadIntent(
    const MeshBufferUploadIntent& intent) const
{
    std::pmr::string diagnostics;
    if (!intent.vertexBuffer.isValid()) {
        arp::core::appendDiagnostic(diagnostics, "RHI mesh upload requires a vertex buffer");
    }
    if (intent.vertexBytes == 0 || intent.vertexStrideBytes == 0) {
        arp::core::appendDiagnostic(
            diagnostics, "RHI mesh upload requires explicit vertex byte size and stride");
    }
    if (intent.indexBytes > 0) {
        if (!intent.indexBuffer.isValid()) {
            arp::core::appendDiagnostic(diagnostics, "RHI mesh upload requires an index buffer");
        }
        if (intent.indexFormatBytes != 2 && intent.indexFormatBytes != 4) {
            arp::core::appendDiagnostic(
                diagnostics, "RHI mesh upload requires explicit 16-bit or 32-bit index format");
        }
    }
    if (intent.stagingIntent != StagingIntent::Upload || !intent.backendOwnedStaging) {
        arp::core::appendDiagnostic(
            diagnostics, "RHI mesh upload requires backend-owned upload staging intent");
    }
    return resultFrom(diagnostics);
}

RhiFrameContractValidationResult RhiFrameContractValidator::validateMaterialTextureBindingIntent(
    const MaterialTextureBindingIntent& intent) const
{
    std::pmr::string diagnostics;
    if (intent.materialName.empty()) {
        arp::core::appendDiagnostic(
            diagnostics, "RHI material texture binding requires a material name");
    }
    if (!intent.texture.isValid() || !intent.sampler.isValid()) {
        arp::core::appendDiagnostic(
            diagnostics, "RHI material texture binding requires texture and sampler resources");
    }
    if (!isColorFormat(intent.format)) {
        arp::core::appendDiagnostic(
            diagnostics, "RHI material texture binding requires a sampled color format");
    }
    if (!hasUsage(intent.textureUsage, TextureUsage::Sampled)) {
        arp::core::appendDiagnostic(
            diagnostics, "RHI material texture binding requires sampled texture usage");
    }
    if (!intent.descriptorBacked) {
        arp::core::appendDiagnostic(
            diagnostics, "RHI material texture binding must be descriptor-backed");
    }
    return resultFrom(diagnostics);
}

RhiFrameContractValidationResult RhiFrameContractValidator::validateRendererInput(
    const RendererInputContract& input) const
{
    std::pmr::string diagnostics;
    if (input.kind == RendererInputKind::Unknown) {
        arp::core::appendDiagnostic(diagnostics, "RHI renderer input kind is unknown");
        return resultFrom(diagnostics);
    }

    validateRendererCommon(input, diagnostics);

    switch (input.kind) {
    case RendererInputKind::GuiDraw:
        if (!input.descriptorBacked) {
            arp::core::appendDiagnostic(
                diagnostics, "RHI GUI draw input requires descriptor-backed draw recording");
        }
        if (input.drawCommandCount == 0 || input.vertexBytes == 0 || input.indexBytes == 0) {
            arp::core::appendDiagnostic(diagnostics,
                "RHI GUI draw input requires draw commands and explicit vertex/index bytes");
        }
        break;
    case RendererInputKind::ParticleDraw:
        if (input.drawCommandCount == 0 || input.particleCount == 0 || input.vertexBytes == 0) {
            arp::core::appendDiagnostic(diagnostics,
                "RHI particle draw input requires particles, vertex bytes, and draw commands");
        }
        if (!input.backendOwnedStaging) {
            arp::core::appendDiagnostic(
                diagnostics, "RHI particle draw input requires backend-owned upload staging");
        }
        break;
    case RendererInputKind::Fxaa:
        if (input.drawCommandCount == 0 || input.sampledTextureCount == 0) {
            arp::core::appendDiagnostic(
                diagnostics, "RHI FXAA input requires a draw command and sampled texture binding");
        }
        break;
    case RendererInputKind::ScenePass:
        if (!input.descriptorBacked) {
            arp::core::appendDiagnostic(
                diagnostics, "RHI scene pass input requires descriptor-backed resources");
        }
        if (input.drawCommandCount == 0 || input.meshCount == 0
            || input.materialBindingCount == 0) {
            arp::core::appendDiagnostic(diagnostics,
                "RHI scene pass input requires meshes, material bindings, and draw commands");
        }
        break;
    case RendererInputKind::ShadowPass:
        if (input.drawCommandCount == 0 || input.shadowCasterCount == 0 || !input.hasDepthTarget) {
            arp::core::appendDiagnostic(diagnostics,
                "RHI shadow pass input requires shadow casters, depth target, and draw commands");
        }
        break;
    case RendererInputKind::GiCompute:
        if (!input.descriptorBacked) {
            arp::core::appendDiagnostic(
                diagnostics, "RHI GI compute input requires descriptor-backed resources");
        }
        if (input.dispatchCount == 0 || input.storageBufferCount == 0
            || input.storageTextureCount == 0) {
            arp::core::appendDiagnostic(diagnostics,
                "RHI GI compute input requires dispatch, storage buffer, and storage texture "
                "bindings");
        }
        break;
    case RendererInputKind::Unknown:
        break;
    }

    return resultFrom(diagnostics);
}

} // namespace arp::rhi

#pragma once
#include "core/cpu_allocator.h"
#include "renderer/renderer_result.h"
#include "rhi/gpu_commands.h"
#include "rhi/gpu_device.h"
#include "rhi/gpu_resources.h"

#include <cstring>
#include <limits>

namespace arp::renderer {
struct PackedRgba8TextureRows {
    core::pmr::Vector<std::uint8_t> pixels{&core::defaultCpuMemoryResource()};
    std::uint32_t bytesPerRow = 0u;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};

    bool success() const noexcept { return diagnostics.empty() && bytesPerRow != 0u; }
};

inline PackedRgba8TextureRows packRgba8TextureRows(const std::uint8_t* rgba8,
    std::size_t sourceSizeBytes,
    std::uint32_t width,
    std::uint32_t height,
    std::uint64_t maxStagingBytes)
{
    PackedRgba8TextureRows result;
    if (rgba8 == nullptr || width == 0u || height == 0u || maxStagingBytes == 0u) {
        result.diagnostics =
            "RGBA8 row packing requires pixels, dimensions, and an explicit staging budget";
        return result;
    }

    const auto tightRowBytes = static_cast<std::uint64_t>(width) * 4u;
    if (tightRowBytes > std::numeric_limits<std::uint32_t>::max()
        || tightRowBytes > std::numeric_limits<std::uint64_t>::max() / height) {
        result.diagnostics = "RGBA8 source dimensions exceed the upload contract";
        return result;
    }
    const auto tightSizeBytes = tightRowBytes * height;
    if (tightSizeBytes != sourceSizeBytes) {
        result.diagnostics = "RGBA8 source size does not match the declared dimensions";
        return result;
    }

    const auto alignedRow = core::alignCpuAllocationOffsetUp(
        static_cast<std::size_t>(tightRowBytes), rhi::kTextureUploadRowPitchAlignment);
    if (!alignedRow.success
        || alignedRow.alignedValue > std::numeric_limits<std::uint32_t>::max()) {
        result.diagnostics = "RGBA8 aligned row pitch exceeds the upload contract";
        return result;
    }
    const auto stagingSizeBytes = static_cast<std::uint64_t>(alignedRow.alignedValue) * height;
    if (stagingSizeBytes == 0u || stagingSizeBytes > maxStagingBytes
        || stagingSizeBytes > std::numeric_limits<std::uint32_t>::max()) {
        result.diagnostics = "RGBA8 aligned staging data exceeds its explicit budget";
        return result;
    }

    result.bytesPerRow = static_cast<std::uint32_t>(alignedRow.alignedValue);
    try {
        result.pixels.resize(static_cast<std::size_t>(stagingSizeBytes), 0u);
    } catch (const std::bad_alloc&) {
        result.bytesPerRow = 0u;
        result.diagnostics = "RGBA8 aligned staging allocation failed";
        return result;
    }
    for (std::uint32_t row = 0u; row < height; ++row) {
        std::memcpy(result.pixels.data() + static_cast<std::size_t>(row) * result.bytesPerRow,
            rgba8 + static_cast<std::size_t>(row) * static_cast<std::size_t>(tightRowBytes),
            static_cast<std::size_t>(tightRowBytes));
    }
    return result;
}

inline RendererResult uploadTextureRgba8(rhi::GpuDevice& device,
    rhi::GpuTexture& texture,
    const std::uint8_t* rgba8,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t bytesPerRow,
    std::uint32_t rowsPerImage)
{
    if (rgba8 == nullptr || width == 0u || height == 0u || bytesPerRow == 0u
        || rowsPerImage == 0u) {
        return RendererResult{
            false,
            "RGBA8 texture upload requires explicit RGBA8 pixels and upload metadata",
        };
    }

    const auto uploadSizeBytes =
        static_cast<std::uint64_t>(bytesPerRow) * static_cast<std::uint64_t>(rowsPerImage);
    if (uploadSizeBytes == 0u || uploadSizeBytes > std::numeric_limits<std::uint32_t>::max()) {
        return RendererResult{
            false,
            "RGBA8 texture upload size is outside the supported contract",
        };
    }

    auto encoder = device.createCommandEncoder({texture.desc().label + "Upload"});
    if (!encoder) {
        return RendererResult{false, "RGBA8 texture upload encoder creation failed"};
    }

    rhi::TextureSubresourceRange range;
    const auto transitionedToCopy = encoder->transition(
        texture, range, rhi::ResourceState::Undefined, rhi::ResourceState::CopyDst);
    if (!transitionedToCopy.success) {
        return RendererResult{false, transitionedToCopy.diagnostics};
    }

    auto transfer = encoder->beginTransferPass({texture.desc().label + "Upload"});
    if (!transfer) {
        return RendererResult{false, encoder->lastValidation().diagnostics};
    }

    rhi::UploadTextureDesc uploadDesc;
    uploadDesc.label = texture.desc().label + "Upload";
    uploadDesc.destinationTexture = texture.id();
    uploadDesc.extent = {width, height, 1u};
    uploadDesc.format = texture.desc().format;
    uploadDesc.bytesPerRow = bytesPerRow;
    uploadDesc.rowsPerImage = rowsPerImage;
    uploadDesc.subresources = range;
    uploadDesc.pixelData = rgba8;
    uploadDesc.pixelDataSize = static_cast<std::uint32_t>(uploadSizeBytes);
    uploadDesc.backendOwnedStaging = true;
    const auto uploaded = transfer->uploadTexture(uploadDesc);
    if (!uploaded.success) {
        transfer->end();
        return RendererResult{false, uploaded.diagnostics};
    }
    const auto transferEnded = transfer->end();
    if (!transferEnded.success) {
        return RendererResult{false, transferEnded.diagnostics};
    }

    const auto transitionedToShader = encoder->transition(
        texture, range, rhi::ResourceState::CopyDst, rhi::ResourceState::ShaderRead);
    if (!transitionedToShader.success) {
        return RendererResult{false, transitionedToShader.diagnostics};
    }

    auto commands = encoder->finish();
    if (!commands) {
        return RendererResult{false, encoder->lastValidation().diagnostics};
    }
    const auto submitted = device.queue().submit(*commands);
    return RendererResult{submitted.success, submitted.diagnostics};
}

} // namespace arp::renderer

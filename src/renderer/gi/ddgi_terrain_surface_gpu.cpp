#include "renderer/gi/ddgi_terrain_surface_gpu.h"

#include "rhi/gpu_commands.h"

#include <cmath>
#include <cstring>
#include <limits>

namespace arp::renderer {
namespace {

DdgiTerrainSurfaceGpuResult failure(DdgiTerrainSurfaceGpuStatus status, const char* diagnostics)
{
    DdgiTerrainSurfaceGpuResult result;
    result.status = status;
    result.diagnostics = diagnostics;
    return result;
}

} // namespace

DdgiTerrainSurfaceGpuResult DdgiTerrainSurfaceGpuPayload::create(
    rhi::GpuDevice& device, const DdgiBakedTerrainGrid& grid, std::uint64_t terrainGeneration)
{
    clear();
    if (!grid.valid() || terrainGeneration == 0u) {
        return failure(
            DdgiTerrainSurfaceGpuStatus::DiagnosticFailure, "DDGI-BAKED-TERRAIN-PAYLOAD-INVALID");
    }
    for (const float height : grid.heights) {
        if (!std::isfinite(height)) {
            return failure(DdgiTerrainSurfaceGpuStatus::DiagnosticFailure,
                "DDGI-BAKED-TERRAIN-PAYLOAD-NONFINITE-HEIGHT");
        }
    }
    if (!device.capabilities().features.r32FloatSampledUpload) {
        return failure(DdgiTerrainSurfaceGpuStatus::Degraded,
            "DDGI-BAKED-TERRAIN-PAYLOAD-DEGRADED: R32SampledUpload/1 capability unavailable");
    }

    rhi::TextureDesc textureDesc;
    textureDesc.label = "DdgiBakedTerrainHeight";
    textureDesc.extent = {grid.samplesX, grid.samplesZ, 1u};
    textureDesc.format = rhi::TextureFormat::R32Float;
    textureDesc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst;
    textureDesc.memory = rhi::MemoryUsage::GpuOnly;
    textureDesc.initialState = rhi::ResourceState::Undefined;
    texture_ = device.createTexture(textureDesc);
    if (!texture_) {
        return failure(DdgiTerrainSurfaceGpuStatus::DiagnosticFailure,
            "DDGI-BAKED-TERRAIN-TEXTURE-CREATE-FAILED");
    }
    rhi::TextureViewDesc viewDesc;
    viewDesc.label = "DdgiBakedTerrainHeightView";
    viewDesc.format = rhi::TextureFormat::R32Float;
    viewDesc.usage = rhi::TextureUsage::Sampled;
    viewDesc.dimension = rhi::TextureViewDimension::Texture2D;
    view_ = device.createTextureView(*texture_, viewDesc);
    if (!view_) {
        clear();
        return failure(DdgiTerrainSurfaceGpuStatus::DiagnosticFailure,
            "DDGI-BAKED-TERRAIN-VIEW-CREATE-FAILED");
    }

    const auto tightRowBytes = static_cast<std::size_t>(grid.samplesX) * sizeof(float);
    const auto rowPitch =
        core::alignCpuAllocationOffsetUp(tightRowBytes, rhi::kTextureUploadRowPitchAlignment);
    if (!rowPitch.success || rowPitch.alignedValue > std::numeric_limits<std::uint32_t>::max()) {
        clear();
        return failure(DdgiTerrainSurfaceGpuStatus::DiagnosticFailure,
            "DDGI-BAKED-TERRAIN-UPLOAD-ROW-PITCH-INVALID");
    }
    core::pmr::Vector<std::uint8_t> uploadBytes{&core::defaultCpuMemoryResource()};
    uploadBytes.resize(rowPitch.alignedValue * grid.samplesZ, 0u);
    for (std::uint32_t z = 0u; z < grid.samplesZ; ++z) {
        std::memcpy(uploadBytes.data() + rowPitch.alignedValue * z,
            grid.heights.data() + static_cast<std::size_t>(z) * grid.samplesX,
            tightRowBytes);
    }
    auto encoder = device.createCommandEncoder({"DdgiBakedTerrainUpload"});
    if (!encoder) {
        clear();
        return failure(DdgiTerrainSurfaceGpuStatus::DiagnosticFailure,
            "DDGI-BAKED-TERRAIN-UPLOAD-ENCODER-CREATE-FAILED");
    }
    const rhi::TextureSubresourceRange range{0u, 1u, 0u, 1u, rhi::TextureAspect::Color};
    if (!encoder
            ->transition(
                *texture_, range, texture_->subresourceState(range), rhi::ResourceState::CopyDst)
            .success) {
        clear();
        return failure(DdgiTerrainSurfaceGpuStatus::DiagnosticFailure,
            "DDGI-BAKED-TERRAIN-UPLOAD-TRANSITION-FAILED");
    }
    auto transfer = encoder->beginTransferPass({"DdgiBakedTerrainUploadTransfer"});
    if (!transfer) {
        clear();
        return failure(DdgiTerrainSurfaceGpuStatus::DiagnosticFailure,
            "DDGI-BAKED-TERRAIN-UPLOAD-TRANSFER-BEGIN-FAILED");
    }
    rhi::UploadTextureDesc upload;
    upload.label = "DdgiBakedTerrainHeightUpload";
    upload.destinationTexture = texture_->id();
    upload.extent = {grid.samplesX, grid.samplesZ, 1u};
    upload.format = rhi::TextureFormat::R32Float;
    upload.bytesPerRow = static_cast<std::uint32_t>(rowPitch.alignedValue);
    upload.rowsPerImage = grid.samplesZ;
    upload.subresources = range;
    upload.pixelData = uploadBytes.data();
    upload.pixelDataSize = static_cast<std::uint32_t>(uploadBytes.size());
    upload.backendOwnedStaging = true;
    upload.frameSerial = terrainGeneration;
    upload.resourceGeneration = terrainGeneration;
    auto recorded = transfer->uploadTexture(upload);
    if (recorded.success)
        recorded = transfer->end();
    if (recorded.success) {
        const auto transitioned = encoder->transition(
            *texture_, range, rhi::ResourceState::CopyDst, rhi::ResourceState::ShaderRead);
        recorded.success = transitioned.success;
        recorded.diagnostics = transitioned.diagnostics;
    }
    if (!recorded.success) {
        clear();
        auto result = failure(DdgiTerrainSurfaceGpuStatus::DiagnosticFailure,
            "DDGI-BAKED-TERRAIN-UPLOAD-RECORD-FAILED");
        result.diagnostics += ": ";
        result.diagnostics += recorded.diagnostics;
        return result;
    }
    auto commands = encoder->finish();
    if (!commands || !device.queue().submit(*commands).success) {
        clear();
        return failure(DdgiTerrainSurfaceGpuStatus::DiagnosticFailure,
            "DDGI-BAKED-TERRAIN-UPLOAD-SUBMIT-FAILED");
    }
    grid_ = grid;
    DdgiTerrainSurfaceGpuResult result;
    result.success = true;
    result.status = device.backend() == rhi::Backend::Null
        ? DdgiTerrainSurfaceGpuStatus::NullValidation
        : DdgiTerrainSurfaceGpuStatus::Native;
    return result;
}

void DdgiTerrainSurfaceGpuPayload::clear() noexcept
{
    view_.reset();
    texture_.reset();
    grid_ = {};
}

} // namespace arp::renderer

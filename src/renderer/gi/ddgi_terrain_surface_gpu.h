#pragma once

#include "core/cpu_allocator.h"
#include "renderer/gi/ddgi_terrain_surface.h"
#include "rhi/gpu_device.h"

namespace arp::renderer {

enum class DdgiTerrainSurfaceGpuStatus : std::uint8_t {
    Native,
    NullValidation,
    Degraded,
    DiagnosticFailure,
};

struct DdgiTerrainSurfaceGpuResult final {
    bool success = false;
    DdgiTerrainSurfaceGpuStatus status = DdgiTerrainSurfaceGpuStatus::DiagnosticFailure;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
};

/// Backend-owned R32_SFLOAT representation of a fully materialized baked
/// terrain grid. The payload does not sample TerrainSurface or invoke PCG;
/// callers must recreate it for a new immutable terrain generation.
class DdgiTerrainSurfaceGpuPayload final {
public:
    DdgiTerrainSurfaceGpuResult create(
        rhi::GpuDevice& device, const DdgiBakedTerrainGrid& grid, std::uint64_t terrainGeneration);

    void clear() noexcept;

    bool ready() const noexcept { return texture_ != nullptr && view_ != nullptr; }

    const rhi::GpuTextureView* view() const noexcept { return view_.get(); }

    const rhi::GpuTexture* texture() const noexcept { return texture_.get(); }

    const DdgiBakedTerrainGrid& grid() const noexcept { return grid_; }

private:
    core::UniquePtr<rhi::GpuTexture> texture_;
    core::UniquePtr<rhi::GpuTextureView> view_;
    DdgiBakedTerrainGrid grid_;
};

} // namespace arp::renderer

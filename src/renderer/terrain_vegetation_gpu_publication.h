#pragma once

#include "renderer/terrain_vegetation_gpu_upload.h"

namespace arp::renderer {

// TerrainVegetationGpuPublication/1. Serialized cold-path instance publication;
// no geometry/material/descriptor or consuming-draw authority.
class ARP_API TerrainVegetationGpuPublication final {
public:
    enum class Status { Failed, Pending, Ready };

    struct Result final {
        Status status = Status::Failed;
        core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    };

    // Device outlives this owner, pending work and external snapshot readers.
    TerrainVegetationGpuPublication(rhi::GpuDevice&, const TerrainVegetationGpuUpload::Desc&);
    TerrainVegetationGpuPublication(const TerrainVegetationGpuPublication&) = delete;
    TerrainVegetationGpuPublication& operator=(const TerrainVegetationGpuPublication&) = delete;

    Result stage(std::shared_ptr<const TerrainVegetationSnapshot>);
    // Only after Ready from the most recent stage. No allocation, wait or buffer destruction.
    void publishPrepared() noexcept;
    std::shared_ptr<const TerrainVegetationSnapshot> preparedSource() const noexcept;

    std::shared_ptr<const TerrainVegetationGpuSnapshot> snapshot() const noexcept
    {
        return published_;
    }

    TerrainVegetationGpuUpload::Result drainAndClear(const rhi::semantics::ShutdownDrainRequest&);

private:
    rhi::GpuDevice& device_;
    const TerrainVegetationGpuUpload::Desc desc_;
    core::UniquePtr<TerrainVegetationGpuUpload> upload_;
    std::shared_ptr<const TerrainVegetationSnapshot> uploadingSource_;
    std::shared_ptr<const TerrainVegetationGpuSnapshot> published_, staged_, displaced_;
    bool ready_ = false;
};

} // namespace arp::renderer

#pragma once

#include "core/memory/cpu_allocator.h"

#include <cmath>
#include <cstddef>
#include <limits>

namespace arp::terrain {

struct TerrainMeshSample final {
    float height = 0.0f;
    float offsetHeightX = 0.0f;
    float offsetHeightZ = 0.0f;
};

// Materialized input for the existing forward-difference grid mesh contract.
// The producing sampler is consumed at this boundary and is never retained.
class TerrainMeshSamples final {
public:
    static constexpr float legacyNormalSampleSpacing = 0.1f;

    template <class HeightSource>
    static TerrainMeshSamples build(const HeightSource& source,
        std::size_t cellsX,
        std::size_t cellsZ,
        float worldSize,
        float normalSampleSpacing)
    {
        TerrainMeshSamples result;
        if (cellsX == 0u || cellsZ == 0u || !std::isfinite(worldSize) || worldSize <= 0.0f
            || !std::isfinite(normalSampleSpacing) || normalSampleSpacing <= 0.0f
            || cellsX == std::numeric_limits<std::size_t>::max()
            || cellsZ == std::numeric_limits<std::size_t>::max()
            || cellsX + 1u > std::numeric_limits<std::size_t>::max() / (cellsZ + 1u)) {
            result.diagnostic_ = "TERRAIN-MESH-SAMPLES-GRID-INVALID";
            return result;
        }
        result.samples_.reserve((cellsX + 1u) * (cellsZ + 1u));
        const float half = worldSize * 0.5f;
        const float stepX = worldSize / static_cast<float>(cellsX);
        const float stepZ = worldSize / static_cast<float>(cellsZ);
        for (std::size_t z = 0; z <= cellsZ; ++z) {
            for (std::size_t x = 0; x <= cellsX; ++x) {
                const float px = -half + static_cast<float>(x) * stepX;
                const float pz = -half + static_cast<float>(z) * stepZ;
                const TerrainMeshSample sample{source.sample(px, pz),
                    source.sample(px + normalSampleSpacing, pz),
                    source.sample(px, pz + normalSampleSpacing)};
                if (!std::isfinite(sample.height) || !std::isfinite(sample.offsetHeightX)
                    || !std::isfinite(sample.offsetHeightZ)) {
                    result.samples_.clear();
                    result.diagnostic_ = "TERRAIN-MESH-SAMPLES-SOURCE-UNAVAILABLE";
                    return result;
                }
                result.samples_.push_back(sample);
            }
        }
        return result;
    }

    bool valid() const noexcept { return !samples_.empty() && diagnostic_ == nullptr; }

    const char* diagnostic() const noexcept { return diagnostic_; }

    const core::pmr::Vector<TerrainMeshSample>& samples() const noexcept { return samples_; }

private:
    core::pmr::Vector<TerrainMeshSample> samples_{&core::defaultCpuMemoryResource()};
    const char* diagnostic_ = nullptr;
};

} // namespace arp::terrain

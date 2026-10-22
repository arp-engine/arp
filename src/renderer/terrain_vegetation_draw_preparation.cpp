#include "renderer/terrain_vegetation_draw_preparation.h"

#include "renderer/terrain_vegetation_runtime.h"

#include <cmath>
#include <limits>

namespace arp::renderer {

TerrainVegetationDrawPreparation::Result TerrainVegetationDrawPreparation::prepare(
    std::shared_ptr<const TerrainVegetationSnapshot> source, const Desc& desc)
{
    Result result;
    if (!source || !resource::isResourceUuid(source->terrainResourceId)
        || !resource::isResourceUuid(source->catalogResourceId)
        || !source->terrainResourceGeneration || !source->catalogResourceGeneration
        || !source->terrainGeneration || !source->catalog || !std::isfinite(source->translation.x)
        || !std::isfinite(source->translation.y) || !std::isfinite(source->translation.z)) {
        result.diagnostics = "VEGETATION-DRAW-SOURCE-INVALID";
        return result;
    }
    if (!desc.maxTiles || !desc.maxInstances || source->tiles.empty()
        || source->tiles.size() > desc.maxTiles || source->instanceCount > desc.maxInstances
        || source->instanceCount
            > std::numeric_limits<std::size_t>::max() / sizeof(vegetation::InstanceDataGpu)) {
        result.diagnostics = "VEGETATION-DRAW-BUDGET";
        return result;
    }
    std::size_t count = 0u;
    const TerrainVegetationSnapshot::Tile* previous = nullptr;
    for (const auto& tile : source->tiles) {
        if (tile.coord.x < 0 || tile.coord.z < 0
            || (previous
                && (tile.coord.z < previous->coord.z
                    || (tile.coord.z == previous->coord.z && tile.coord.x <= previous->coord.x)))) {
            result.diagnostics = "VEGETATION-DRAW-TILE-ORDER-INVALID";
            return result;
        }
        previous = &tile;
        if (tile.instances.size() > desc.maxInstances - count) {
            result.diagnostics = "VEGETATION-DRAW-BUDGET";
            return result;
        }
        count += tile.instances.size();
    }
    if (count != source->instanceCount) {
        result.diagnostics = "VEGETATION-DRAW-COUNT-MISMATCH";
        return result;
    }
    if (snapshot_ && snapshot_->source == source) {
        result.success = result.unchanged = true;
        return result;
    }

    auto candidate = core::makeShared<TerrainVegetationDrawSnapshot>();
    candidate->source = std::move(source);
    candidate->tiles.reserve(candidate->source->tiles.size());
    for (const auto& tile : candidate->source->tiles) {
        // Validate before grouping; the source records already include translation.
        for (const auto& instance : tile.instances) {
            if (!instance.hasValidTransform()) {
                result.diagnostics = "VEGETATION-DRAW-TRANSFORM-INVALID";
                return result;
            }
            const auto* asset = instance.prototypeId
                    && instance.prototypeId <= std::numeric_limits<std::uint16_t>::max()
                ? candidate->source->catalog->findAssetByTemplateId(
                      static_cast<std::uint16_t>(instance.prototypeId))
                : nullptr;
            const auto* geometry =
                asset ? candidate->source->catalog->findTemplate(asset->id) : nullptr;
            if (!geometry || geometry->id != instance.prototypeId) {
                result.diagnostics = "VEGETATION-DRAW-PROTOTYPE-UNAVAILABLE";
                return result;
            }
        }
        TerrainVegetationDrawSnapshot::Tile output;
        output.coord = tile.coord;
        auto groups = groupTerrainVegetationInstances(tile.instances);
        output.prototypes.reserve(groups.size());
        for (const auto& group : groups) {
            TerrainVegetationDrawSnapshot::Prototype prototype;
            prototype.prototypeId = group.prototypeId;
            core::pmr::String loweringDiagnostics{&core::defaultCpuMemoryResource()};
            if (!lowerTerrainVegetationInstancesForCull(
                    group, prototype.instances, loweringDiagnostics)) {
                result.diagnostics = "VEGETATION-DRAW-ABI-UNREPRESENTABLE: ";
                result.diagnostics += loweringDiagnostics;
                return result;
            }
            output.prototypes.push_back(std::move(prototype));
        }
        candidate->tiles.push_back(std::move(output));
    }
    snapshot_ = std::move(candidate);
    result.success = true;
    return result;
}

} // namespace arp::renderer

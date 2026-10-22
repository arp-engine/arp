#include "renderer/terrain_vegetation_binding.h"

#include <limits>

namespace arp::renderer {
bool TerrainVegetationSnapshot::equivalent(const TerrainVegetationSnapshot& other) const noexcept
{
    if (terrainResourceId != other.terrainResourceId || catalogResourceId != other.catalogResourceId
        || terrainResourceGeneration != other.terrainResourceGeneration
        || catalogResourceGeneration != other.catalogResourceGeneration
        || terrainGeneration != other.terrainGeneration || catalog != other.catalog
        || catalog.owner_before(other.catalog) || other.catalog.owner_before(catalog)
        || translation.x != other.translation.x || translation.y != other.translation.y
        || translation.z != other.translation.z || instanceCount != other.instanceCount
        || tiles.size() != other.tiles.size())
        return false;
    for (std::size_t i = 0; i < tiles.size(); ++i) {
        const auto& a = tiles[i];
        const auto& b = other.tiles[i];
        if (!(a.coord == b.coord) || a.instances.size() != b.instances.size())
            return false;
        for (std::size_t j = 0; j < a.instances.size(); ++j) {
            const auto& x = a.instances[j];
            const auto& y = b.instances[j];
            if (x.prototypeId != y.prototypeId || x.position.x != y.position.x
                || x.position.y != y.position.y || x.position.z != y.position.z
                || x.rotation != y.rotation || x.scale.x != y.scale.x || x.scale.y != y.scale.y
                || x.scale.z != y.scale.z || x.variationSeed != y.variationSeed)
                return false;
        }
    }
    return true;
}

bool TerrainVegetationBinding::retainEquivalentSnapshot(
    std::shared_ptr<const TerrainVegetationSnapshot> source) noexcept
{
    if (!snapshot_ || !source)
        return !snapshot_ && !source;
    if (!snapshot_->equivalent(*source))
        return false;
    snapshot_ = std::move(source);
    return true;
}

TerrainVegetationBinding::Result TerrainVegetationBinding::bind(
    const resource::ResourceManager& resources,
    resource::NameView terrainResourceId,
    resource::ResourceGeneration expectedGeneration,
    core::Vec3 translation,
    const Desc& desc)
{
    Result result;
    if (!std::isfinite(translation.x) || !std::isfinite(translation.y)
        || !std::isfinite(translation.z)) {
        result.diagnostics = "VEGETATION-TERRAIN-TRANSLATION-INVALID";
        return result;
    }
    const auto terrainHandle = resources.find<resource::TerrainAssetResource>(terrainResourceId);
    const auto* asset = terrainHandle.get();
    if (!asset || terrainHandle.generation() != expectedGeneration) {
        result.diagnostics = "VEGETATION-TERRAIN-RESOURCE-NOT-READY";
        return result;
    }
    if (!asset->hasVegetation()) {
        result.unchanged = !snapshot_;
        clear();
        result.success = true;
        return result;
    }
    const auto catalogHandle =
        resources.find<resource::VegetationCatalogResource>(asset->vegetationCatalogResourceId());
    const auto* catalog = catalogHandle.get();
    if (!catalog || !catalog->data) {
        result.diagnostics = "VEGETATION-TERRAIN-CATALOG-NOT-READY";
        return result;
    }
    const auto nx = asset->tileCountX(), nz = asset->tileCountZ();
    if (!desc.maxTiles || !desc.maxInstances || !nx || !nz || nx > desc.maxTiles / nz
        || nx > static_cast<std::uint32_t>(std::numeric_limits<int>::max())
        || nz > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
        result.diagnostics = "VEGETATION-TERRAIN-TILE-BUDGET";
        return result;
    }
    std::size_t count = 0u;
    for (std::uint32_t z = 0; z < nz; ++z) {
        for (std::uint32_t x = 0; x < nx; ++x) {
            const auto* instances =
                asset->vegetationFor({static_cast<int>(x), static_cast<int>(z)});
            if (!instances || instances->size() > desc.maxInstances - count) {
                result.diagnostics = "VEGETATION-TERRAIN-INSTANCE-BUDGET-OR-TILE-MISSING";
                return result;
            }
            count += instances->size();
        }
    }
    if (snapshot_ && snapshot_->terrainResourceId == terrainResourceId
        && snapshot_->terrainResourceGeneration == expectedGeneration
        && snapshot_->terrainGeneration == asset->generation()
        && snapshot_->catalogResourceId == asset->vegetationCatalogResourceId()
        && snapshot_->catalogResourceGeneration == catalogHandle.generation()
        && snapshot_->catalog == catalog->data && snapshot_->translation.x == translation.x
        && snapshot_->translation.y == translation.y && snapshot_->translation.z == translation.z) {
        result.success = result.unchanged = true;
        return result;
    }
    auto candidate = core::makeShared<TerrainVegetationSnapshot>();
    candidate->terrainResourceId = terrainResourceId;
    candidate->terrainResourceGeneration = expectedGeneration;
    candidate->terrainGeneration = asset->generation();
    candidate->catalogResourceId = asset->vegetationCatalogResourceId();
    candidate->catalogResourceGeneration = catalogHandle.generation();
    candidate->catalog = catalog->data;
    candidate->translation = translation;
    candidate->instanceCount = count;
    candidate->tiles.reserve(static_cast<std::size_t>(nx) * nz);
    for (std::uint32_t z = 0; z < nz; ++z) {
        for (std::uint32_t x = 0; x < nx; ++x) {
            TerrainVegetationSnapshot::Tile tile;
            tile.coord = {static_cast<int>(x), static_cast<int>(z)};
            const auto& instances = *asset->vegetationFor(tile.coord);
            tile.instances.reserve(instances.size());
            for (auto instance : instances) {
                if (!instance.prototypeId
                    || instance.prototypeId > std::numeric_limits<std::uint16_t>::max()) {
                    result.diagnostics = "VEGETATION-TERRAIN-PROTOTYPE-INVALID";
                    return result;
                }
                const auto* prototype = candidate->catalog->findAssetByTemplateId(
                    static_cast<std::uint16_t>(instance.prototypeId));
                const auto* geometry =
                    prototype ? candidate->catalog->findTemplate(prototype->id) : nullptr;
                if (!geometry || geometry->id != instance.prototypeId) {
                    result.diagnostics = "VEGETATION-TERRAIN-PROTOTYPE-UNAVAILABLE";
                    return result;
                }
                instance.position.x += translation.x;
                instance.position.y += translation.y;
                instance.position.z += translation.z;
                if (!std::isfinite(instance.position.x) || !std::isfinite(instance.position.y)
                    || !std::isfinite(instance.position.z)) {
                    result.diagnostics = "VEGETATION-TERRAIN-POSITION-OVERFLOW";
                    return result;
                }
                tile.instances.push_back(instance);
            }
            candidate->tiles.push_back(std::move(tile));
        }
    }
    snapshot_ = std::move(candidate);
    result.success = true;
    return result;
}
} // namespace arp::renderer

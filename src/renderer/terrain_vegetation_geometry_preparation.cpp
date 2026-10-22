#include "renderer/terrain_vegetation_geometry_preparation.h"

#include "resource/vegetation_catalog_geometry.h"

namespace arp::renderer {

TerrainVegetationGeometryPreparation::Result TerrainVegetationGeometryPreparation::prepare(
    std::shared_ptr<const TerrainVegetationSnapshot> source, const Desc& desc)
{
    if (desc.contractVersion != 1u || !desc.maxTiles || !desc.maxInstances
        || !desc.maxCatalogEntries || !desc.maxPrototypes || !desc.maxVertices || !desc.maxIndices
        || !desc.maxSections || !desc.maxCpuBytes)
        return {false, "VEGETATION-GEOMETRY-BUDGET-INVALID"};
    if (!source || !source->catalog)
        return {false, "VEGETATION-GEOMETRY-SOURCE-INVALID"};
    const auto& catalog = source->catalog;
    if (catalog->assets.size() > desc.maxCatalogEntries
        || catalog->templates.size() > desc.maxCatalogEntries
        || catalog->templateIdsByAssetId.size() > desc.maxCatalogEntries)
        return {false, "VEGETATION-GEOMETRY-CATALOG-BUDGET"};
    if (const auto* diagnostic =
            resource::VegetationCatalogGeometryContract::validateIndex(*catalog))
        return {false, diagnostic};
    try {
        auto quota = core::makeShared<core::CpuMemoryQuota>(desc.maxCpuBytes);
        core::CpuMemoryQuotaScope scope(quota);
        TerrainVegetationDrawPreparation draw;
        const auto prepared = draw.prepare(source, {desc.maxTiles, desc.maxInstances});
        if (!prepared.success)
            return {false, prepared.diagnostics};
        core::pmr::Vector<std::uint32_t> ids{&core::defaultCpuMemoryResource()};
        ids.reserve(std::min(desc.maxPrototypes, catalog->assets.size()));
        for (const auto& tile : draw.snapshot()->tiles)
            for (const auto& prototype : tile.prototypes) {
                const auto at = std::lower_bound(ids.begin(), ids.end(), prototype.prototypeId);
                if (at == ids.end() || *at != prototype.prototypeId) {
                    if (ids.size() == desc.maxPrototypes)
                        return {false, "VEGETATION-GEOMETRY-PROTOTYPE-BUDGET"};
                    ids.insert(at, prototype.prototypeId);
                }
            }
        auto candidate = core::makeShared<TerrainVegetationGeometrySnapshot>();
        candidate->instances = draw.snapshot();
        candidate->prototypes.reserve(ids.size());
        std::size_t vertices = 0u, indices = 0u, sections = 0u;
        for (const auto id : ids) {
            const auto& asset = catalog->assets[id - 1u];
            const auto& geometry = catalog->templates[id - 1u];
            if (geometry.vertices.size() > desc.maxVertices - vertices
                || geometry.indices.size() > desc.maxIndices - indices
                || geometry.sections.size() > desc.maxSections - sections)
                return {false, "VEGETATION-GEOMETRY-PAYLOAD-BUDGET"};
            vertices += geometry.vertices.size();
            indices += geometry.indices.size();
            sections += geometry.sections.size();
            if (const auto* diagnostic =
                    resource::VegetationCatalogGeometryContract::validateBindings(geometry, asset))
                return {false, diagnostic};
            TerrainVegetationGeometrySnapshot::Prototype output;
            output.prototypeId = id;
            output.geometry = {catalog, &geometry};
            output.asset = {catalog, &asset};
            output.sections.reserve(geometry.sections.size());
            for (const auto& section : geometry.sections) {
                const auto* binding =
                    resource::VegetationCatalogGeometryContract::findMaterialBinding(
                        asset, section.materialSlot);
                output.sections.push_back({section.firstIndex,
                    section.indexCount,
                    section.materialSlot,
                    static_cast<std::uint32_t>(binding - asset.materials.data())});
            }
            candidate->prototypes.push_back(std::move(output));
        }
        snapshot_ = std::move(candidate);
        return {true, {}};
    } catch (const std::bad_alloc&) {
        return {false, "VEGETATION-GEOMETRY-CPU-BUDGET"};
    }
}

} // namespace arp::renderer

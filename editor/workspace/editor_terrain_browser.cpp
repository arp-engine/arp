#include "editor/workspace/editor_terrain_browser.h"

#include "resource/terrain_asset_resource.h"

namespace arp::editor {
bool EditorTerrainBrowser::refresh(const resource::ResourceManager& resources)
{
    entries_.clear();
    diagnostic_.clear();
    const auto snapshot = resources.snapshotReferences("MaterialResource", maxMaterialReferences);
    if (!snapshot.success) {
        diagnostic_ = snapshot.diagnostics;
        return false;
    }
    core::pmr::Vector<EditorTerrainBrowserEntry> candidate;
    for (const auto& entry : snapshot.entries) {
        if (entry.state != resource::ResourceState::Ready)
            continue;
        const auto material = resources.find<resource::MaterialResource>(entry.reference.id);
        const auto* preview = material.ready()
            ? dynamic_cast<const EditorTerrainPreviewMaterial*>(material.get())
            : nullptr;
        if (!preview)
            continue;
        const auto terrain = resources.find<resource::TerrainAssetResource>(preview->terrainId);
        if (!terrain.ready() || terrain.generation() != preview->terrainGeneration
            || candidate.size() == maxPreviews) {
            diagnostic_ = "EDITOR-TERRAIN-BROWSER-SOURCE-OR-BUDGET";
            return false;
        }
        const auto& source = *terrain.get();
        const auto count = std::uint64_t(source.tileCountX()) * source.tileCountZ();
        if (preview->tiles.size() != count) {
            diagnostic_ = "EDITOR-TERRAIN-BROWSER-BINDING";
            return false;
        }
        for (std::uint32_t z = 0; z < source.tileCountZ(); ++z)
            for (std::uint32_t x = 0; x < source.tileCountX(); ++x)
                if (resolveEditorTerrainPreviewMaterial(resources,
                        preview->terrainId,
                        terrain.generation(),
                        entry.reference.id,
                        {static_cast<std::int32_t>(x), static_cast<std::int32_t>(z)})
                        .empty()) {
                    diagnostic_ = "EDITOR-TERRAIN-BROWSER-BINDING";
                    return false;
                }
        EditorTerrainBrowserEntry value;
        value.previewId = entry.reference.id;
        value.previewGeneration = material.generation();
        value.terrainId = preview->terrainId;
        value.terrainGeneration = terrain.generation();
        value.tilesX = source.tileCountX();
        value.tilesZ = source.tileCountZ();
        value.samplesPerEdge = source.samplesPerEdge();
        value.layerCount = static_cast<std::uint32_t>(source.layers().size());
        value.sourceBytes = source.processedByteCount();
        value.vegetationInstances = preview->vegetationInstanceCount;
        const auto bounds = source.bounds();
        const auto heights = source.heightRange();
        value.minimum = {bounds.minX, heights.minimum, bounds.minZ};
        value.maximum = {bounds.maxX, heights.maximum, bounds.maxZ};
        candidate.push_back(std::move(value));
    }
    entries_ = std::move(candidate);
    return true;
}
} // namespace arp::editor

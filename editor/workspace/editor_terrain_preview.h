#pragma once

#include "3d/terrain/terrain_surface.h"
#include "resource/prefab_asset_resources.h"

namespace arp::editor {
class ProjectInputSnapshot;

// A project-owned, explicitly opted-in editor material. Scene YAML still holds
// a stable MaterialResource reference; tile materials are deterministic outputs.
class EditorTerrainPreviewMaterial final : public resource::MaterialResource {
public:
    struct Tile final {
        terrain::TerrainTileCoord coord;
        core::pmr::String materialId;
        resource::ResourceGeneration materialGeneration = 0;
        std::array<resource::ResourceGeneration, 5> textureGenerations{};
    };

    core::pmr::String terrainId;
    resource::ResourceGeneration terrainGeneration = 0;
    std::uint32_t resolution = 0;
    std::uint64_t vegetationInstanceCount = 0;
    core::pmr::Vector<Tile> tiles;
};

bool loadEditorTerrainPreview(const std::filesystem::path& projectRoot,
    std::string_view relativeConfig,
    resource::ResourceManager& candidate,
    core::pmr::String& diagnostic,
    ProjectInputSnapshot* inputSnapshot = nullptr);

// Empty result means missing/stale/wrong binding; callers fail closed.
core::pmr::String resolveEditorTerrainPreviewMaterial(const resource::ResourceManager& resources,
    const core::pmr::String& terrainId,
    resource::ResourceGeneration terrainGeneration,
    const core::pmr::String& previewId,
    terrain::TerrainTileCoord tile);

} // namespace arp::editor

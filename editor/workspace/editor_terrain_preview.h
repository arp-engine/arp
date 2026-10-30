#pragma once

#include "3d/terrain/terrain_surface.h"
#include "resource/prefab_asset_resources.h"

namespace arp::resource {
struct AssetPackageReadPlan;
struct AssetPackageReadBudget;
struct AssetPackageSourceContext;
} // namespace arp::resource

namespace arp::editor {
class ProjectInputSnapshot;
class ProjectResourceSources;

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

// Strict preloaded profile: no filesystem fallback. Legacy vegetation source
// import has no mounted-source contract yet and returns an explicit diagnostic.
bool loadEditorTerrainPreviewSources(const ProjectResourceSources&,
    std::string_view relativeConfig,
    resource::ResourceManager& candidate,
    core::pmr::String& diagnostic,
    ProjectInputSnapshot* inputSnapshot = nullptr);

// Declared inputs only; missing sources become requests, never host reads.
resource::AssetPackageReadPlan planEditorTerrainPreviewSources(const std::filesystem::path& root,
    std::string_view relativeConfig,
    const resource::AssetPackageSourceContext&,
    const resource::AssetPackageReadBudget&);

// Empty result means missing/stale/wrong binding; callers fail closed.
core::pmr::String resolveEditorTerrainPreviewMaterial(const resource::ResourceManager& resources,
    const core::pmr::String& terrainId,
    resource::ResourceGeneration terrainGeneration,
    const core::pmr::String& previewId,
    terrain::TerrainTileCoord tile);

} // namespace arp::editor

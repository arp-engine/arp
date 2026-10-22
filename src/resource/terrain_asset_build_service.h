#pragma once
#include "resource/terrain_asset_baker.h"

#include <string_view>

namespace arp::resource {
// Preparation/tooling service shared by the CLI and Editor. Runtime never
// consumes authoring YAML or retains an importer/generator.
class ARP_API TerrainAssetBuildService final {
public:
    TerrainAssetBakeResult prepare(
        std::string_view authoringYaml, const std::filesystem::path& sourceRoot) const;
    // Both publication entry points require an absent destination with an
    // existing parent. A complete package is decoded before exclusive rename.
    TerrainAssetIoResult build(const std::filesystem::path& authoringFile,
        const std::filesystem::path& outputDirectory) const;
    TerrainAssetIoResult packageGeometry(
        const TerrainAssetBuildDesc& desc, const std::filesystem::path& outputDirectory) const;
    // Publishes an explicitly prepared, self-contained terrain package. Does
    // not infer authoring dependencies, import models or decode image files.
    TerrainAssetIoResult packageClosed(const std::filesystem::path& preparedDirectory,
        const std::filesystem::path& outputDirectory) const;

private:
    TerrainAssetIoResult buildComposed(std::string_view authoringYaml,
        const std::filesystem::path& sourceRoot,
        const TerrainAssetBuildDesc& terrain,
        const std::filesystem::path& outputDirectory) const;
};
} // namespace arp::resource

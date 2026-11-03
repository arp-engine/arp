#include "editor/workspace/editor_terrain_preview_vegetation.h"

#include "resource/resource_source_io.h"

namespace arp::editor {
namespace {
using Yaml = core::YamlContract;

void require(bool condition, const char* diagnostic)
{
    if (!condition)
        throw core::YamlContractError{diagnostic};
}

std::size_t size(const Yaml::Node& node, const char* key)
{
    const auto value = Yaml::uint64(Yaml::required(node, key));
    require(value && value < SIZE_MAX, "EDITOR-TERRAIN-VEGETATION-LIMITS");
    return static_cast<std::size_t>(value);
}

resource::VegetationCatalogSourceLoadLimits decodeLimits(const Yaml::Node& node)
{
    Yaml::onlyKeys(node,
        {"maxCatalogBytes",
            "maxAssets",
            "maxDecodedBytes",
            "maxMetadataDepth",
            "maxMetadataValues",
            "maxMetadataCollectionLength",
            "maxTotalSourceBytes",
            "maxImportBytes",
            "mesh"});
    resource::VegetationCatalogSourceLoadLimits result;
    result.maxCatalogBytes = size(node, "maxCatalogBytes");
    result.maxAssets = size(node, "maxAssets");
    result.maxDecodedBytes = size(node, "maxDecodedBytes");
    result.maxMetadataDepth = size(node, "maxMetadataDepth");
    result.maxMetadataValues = size(node, "maxMetadataValues");
    result.maxMetadataCollectionLength = size(node, "maxMetadataCollectionLength");
    result.maxTotalSourceBytes = Yaml::uint64(Yaml::required(node, "maxTotalSourceBytes"));
    result.maxImportBytes = Yaml::uint64(Yaml::required(node, "maxImportBytes"));
    const auto& mesh = Yaml::required(node, "mesh");
    Yaml::onlyKeys(mesh,
        {"maxSourceBytes",
            "maxVertices",
            "maxIndices",
            "maxPrimitives",
            "maxJsonDepth",
            "maxJsonValues",
            "maxImportBytes",
            "maxDecodedBytes",
            "maxNodeVisits",
            "maxNodeDepth",
            "colorPolicy"});
    auto& limits = result.mesh;
    limits.geometry.maxSourceBytes = Yaml::uint64(Yaml::required(mesh, "maxSourceBytes"));
    limits.geometry.maxVertices = Yaml::uint32(mesh, "maxVertices");
    limits.geometry.maxIndices = Yaml::uint32(mesh, "maxIndices");
    limits.geometry.maxPrimitives = Yaml::uint32(mesh, "maxPrimitives");
    limits.geometry.maxJsonDepth = Yaml::uint32(mesh, "maxJsonDepth");
    limits.geometry.maxJsonValues = Yaml::uint32(mesh, "maxJsonValues");
    limits.geometry.strictGeometryOnly = false;
    limits.maxImportBytes = Yaml::uint64(Yaml::required(mesh, "maxImportBytes"));
    limits.maxDecodedBytes = Yaml::uint64(Yaml::required(mesh, "maxDecodedBytes"));
    limits.maxNodeVisits = Yaml::uint32(mesh, "maxNodeVisits");
    limits.maxNodeDepth = Yaml::uint32(mesh, "maxNodeDepth");
    const auto color = Yaml::text(mesh, "colorPolicy");
    if (color == "reject-color0")
        limits.colorPolicy = assets::StaticMeshVertexColorPolicy::RejectColor0;
    else if (color == "discard-validated-rgba")
        limits.colorPolicy = assets::StaticMeshVertexColorPolicy::DiscardValidatedRgba;
    else
        throw core::YamlContractError{"EDITOR-TERRAIN-VEGETATION-COLOR-POLICY"};
    return result;
}
} // namespace

EditorTerrainPreviewVegetation EditorTerrainPreviewVegetation::decode(
    const Yaml::Node& node, core::StringView previewContract)
{
    Yaml::onlyKeys(node, {"resourceId", "catalog", "input", "sourceLimits"});
    const bool v2 = previewContract == "editor.terrain-preview/v2";
    require(
        v2 || previewContract == "editor.terrain-preview/v1", "EDITOR-TERRAIN-PREVIEW-CONTRACT");
    EditorTerrainPreviewVegetation result;
    result.resourceId = Yaml::text(node, "resourceId");
    result.catalog = Yaml::text(node, "catalog");
    const auto input = Yaml::text(node, "input");
    const auto& mapping = node.get_value_ref<const Yaml::Node::mapping_type&>();
    const bool hasLimits = mapping.find(Yaml::Node("sourceLimits")) != mapping.end();
    if (input == "absent") {
        require(result.resourceId.empty() && result.catalog.empty() && !hasLimits,
            "EDITOR-TERRAIN-PREVIEW-VEGETATION-CONTRACT");
        return result;
    }
    require(resource::isResourceUuid(result.resourceId)
            && core::FilePath::isSafeRelativePath(result.catalog),
        "EDITOR-TERRAIN-PREVIEW-VEGETATION-CONTRACT");
    if (input == "legacy-source/v2") {
        require(!v2 && !hasLimits, "EDITOR-TERRAIN-PREVIEW-VEGETATION-CONTRACT");
        result.input = Input::LegacyV2;
    } else {
        require(
            v2 && input == "source/v2" && hasLimits, "EDITOR-TERRAIN-PREVIEW-VEGETATION-CONTRACT");
        result.input = Input::SourcesV2;
        result.limits_ = decodeLimits(Yaml::required(node, "sourceLimits"));
    }
    return result;
}

core::UniquePtr<resource::VegetationCatalogResource> EditorTerrainPreviewVegetation::sourceResource(
    const std::filesystem::path& root, resource::ResourceFileOrigin origin) const
{
    require(input == Input::SourcesV2 && limits_, "EDITOR-TERRAIN-PREVIEW-VEGETATION-CONTRACT");
    resource::VegetationCatalogResourceLoadDesc desc;
    require(resource::resolveResourceSourcePath(root, catalog, origin, desc.catalogPath),
        "EDITOR-TERRAIN-PREVIEW-PATH");
    desc.resourceId = resourceId;
    desc.status = "Prototype Complete";
    desc.input = resource::VegetationCatalogInput::SourceCatalogV2;
    desc.sourceOrigin = origin;
    desc.sourceLimits = limits_;
    return core::makeUnique<resource::VegetationCatalogResource>(std::move(desc));
}
} // namespace arp::editor

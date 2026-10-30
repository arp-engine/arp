#include "editor/workspace/editor_terrain_preview.h"

#include "assets/terrain/terrain_surface_preview.h"
#include "core/encoding/yaml_contract.h"
#include "core/io/file_io.h"
#include "core/io/filesystem_path.h"
#include "editor/project/project_input_snapshot.h"
#include "editor/project/project_resource_sources.h"
#include "resource/resource_source_io.h"
#include "resource/terrain_asset_resource.h"
#include "resource/vegetation_catalog_resource.h"

#include <algorithm>

namespace arp::editor {
namespace {
using Yaml = core::YamlContract;

void require(bool value, const char* diagnostic)
{
    if (!value)
        throw core::YamlContractError{diagnostic};
}

void validatePreviewConfig(const Yaml::Node& config)
{
    Yaml::onlyKeys(config,
        {"type",
            "contractVersion",
            "resourceId",
            "terrainId",
            "terrain",
            "vegetation",
            "layers",
            "resolution",
            "snowLine",
            "snowFeather",
            "maxFileBytes",
            "maxDecodedBytes",
            "maxPreviewBytes",
            "vegetationPreview"});
    require(Yaml::text(config, "type") == "EditorTerrainPreview"
            && Yaml::text(config, "contractVersion") == "editor.terrain-preview/v1"
            && Yaml::text(config, "vegetationPreview") == "omit-with-diagnostic",
        "EDITOR-TERRAIN-PREVIEW-CONTRACT");
}

void validateImageInput(const Yaml::Node& node, bool srgb)
{
    Yaml::onlyKeys(node, {"path", "mime", "colorSpace"});
    require(Yaml::text(node, "colorSpace") == (srgb ? "srgb" : "linear"),
        "EDITOR-TERRAIN-PREVIEW-COLOR-SPACE");
    const auto mime = Yaml::text(node, "mime");
    require(mime == "image/jpeg" || mime == "image/png" || mime == "image/webp",
        "EDITOR-TERRAIN-PREVIEW-MIME");
}

class PreviewInput final {
public:
    std::filesystem::path root;
    std::size_t remainingFileBytes = 0;
    std::size_t remainingDecodedBytes = 0;
    ProjectInputSnapshot* snapshot = nullptr;
    const ProjectResourceSources* sources = nullptr;

    resource::AssetPackageSourceContext context() const
    {
        if (sources)
            return sources->context(remainingDecodedBytes);
        resource::AssetPackageSourceContext result;
        result.resources.maxDecodedBytes = remainingDecodedBytes;
        return result;
    }

    std::filesystem::path contained(core::StringView relative) const
    {
        require(core::FilePath::isSafeRelativePath(relative), "EDITOR-TERRAIN-PREVIEW-PATH");
        if (sources) {
            std::filesystem::path path;
            require(sources->path(relative, path), "EDITOR-TERRAIN-PREVIEW-PATH");
            return path;
        }
        std::error_code error;
        const auto path = std::filesystem::canonical(root / std::filesystem::path(relative), error);
        require(!error && core::FilePath::contains(root, path)
                && std::filesystem::is_regular_file(path),
            "EDITOR-TERRAIN-PREVIEW-PATH");
        return path;
    }

    core::ByteBuffer read(core::StringView relative, std::size_t maxBytes)
    {
        const auto path = contained(relative);
        const auto sourceContext = context();
        auto result = resource::readResourceSource(core::FilePath::utf8(path),
            sourceContext.resources,
            std::min(maxBytes, remainingFileBytes),
            false,
            &sourceContext.origin);
        require(result.success, "EDITOR-TERRAIN-PREVIEW-READ-BUDGET");
        if (snapshot) {
            core::pmr::String diagnostic;
            const std::string_view raw{
                reinterpret_cast<const char*>(result.bytes.data()), result.bytes.size()};
            require(sources ? snapshot->recordLogical(relative, raw, diagnostic)
                            : snapshot->record(path, raw, diagnostic),
                "EDITOR-TERRAIN-PREVIEW-INPUT-SNAPSHOT");
        }
        remainingFileBytes -= result.bytes.size();
        return std::move(result.bytes);
    }

    Yaml::Node yaml(core::StringView relative)
    {
        const auto bytes = read(relative, 1024u * 1024u);
        core::TextDecodeDesc desc;
        desc.bom = core::TextBomPolicy::Strip;
        desc.maxOutputBytes = 1024u * 1024u;
        const auto text = core::TextDecoder::decode(
            {reinterpret_cast<const char*>(bytes.data()), bytes.size()}, desc);
        require(text.status == core::TextDecodeStatus::Success, "EDITOR-TERRAIN-PREVIEW-TEXT");
        return Yaml::parseSingleDocument(text.text);
    }

    void admitFile(const std::filesystem::path& path)
    {
        if (sources) {
            const auto relative = core::FilePath::utf8(path.lexically_relative(root));
            require(contained(relative) == path, "EDITOR-TERRAIN-PREVIEW-PATH");
            (void)read(relative, remainingFileBytes);
            return;
        }
        const auto resolved = std::filesystem::canonical(path);
        require(core::FilePath::contains(root, resolved), "EDITOR-TERRAIN-PREVIEW-PATH");
        const auto bytes = std::filesystem::file_size(resolved);
        require(bytes <= remainingFileBytes, "EDITOR-TERRAIN-PREVIEW-READ-BUDGET");
        if (snapshot) {
            core::pmr::String diagnostic;
            require(snapshot->read(resolved, diagnostic), "EDITOR-TERRAIN-PREVIEW-INPUT-SNAPSHOT");
        }
        remainingFileBytes -= static_cast<std::size_t>(bytes);
    }

    assets::DecodedImage image(const Yaml::Node& node, bool srgb)
    {
        validateImageInput(node, srgb);
        const auto bytes = read(Yaml::text(node, "path"), remainingFileBytes);
        const auto mime = Yaml::text(node, "mime");
        auto decoded = assets::decodeImageToRgba8(
            {bytes.data(), bytes.size()}, mime, srgb, remainingDecodedBytes);
        require(decoded.success && decoded.image.rgba8.size() <= remainingDecodedBytes,
            "EDITOR-TERRAIN-PREVIEW-DECODE-BUDGET");
        remainingDecodedBytes -= decoded.image.rgba8.size();
        return std::move(decoded.image);
    }
};

resource::ResourceReference reference(core::StringView id, const char* type)
{
    resource::ResourceReference ref;
    ref.id = id;
    ref.type = type;
    ref.status = "Prototype Complete";
    return ref;
}

void publish(resource::ResourceManager& manager, core::UniquePtr<resource::ResourceBase> value)
{
    require(manager.recordSnapshot(value->reference().id) == nullptr,
        "EDITOR-TERRAIN-PREVIEW-DUPLICATE-ID");
    const auto loaded = manager.loadResource(std::move(value));
    if (!loaded.success)
        throw core::YamlContractError{"EDITOR-TERRAIN-PREVIEW-RESOURCE-LOAD"};
}

void materialFields(resource::MaterialResource& material,
    const core::pmr::String& source,
    const core::pmr::String& shader)
{
    material.name = "Baked terrain surface preview (Prototype)";
    material.sourceResourceId = source;
    material.shaderPackageId = shader;
    material.materialContractVersion = resource::kPbrMetallicRoughnessContractVersion;
    material.surfaceModel = resource::kPbrMetallicRoughnessContract;
    material.alphaMode = "OPAQUE";
    material.metallicFactor = 0;
    material.roughnessFactor = 1;
    material.emissiveR = material.emissiveG = material.emissiveB = 0;
}
} // namespace

static bool prepareEditorTerrainPreview(const std::filesystem::path& projectRoot,
    std::string_view relativeConfig,
    resource::ResourceManager& candidate,
    core::pmr::String& diagnostic,
    ProjectInputSnapshot* inputSnapshot,
    const ProjectResourceSources* sources)
{
    try {
        PreviewInput input;
        input.sources = sources;
        input.root = sources ? sources->root() : std::filesystem::canonical(projectRoot);
        input.snapshot = inputSnapshot;
        // Metadata bootstrap has a fixed 1 MiB hard limit, as project manifests do.
        input.remainingFileBytes = 1024u * 1024u;
        const auto config = input.yaml(relativeConfig);
        validatePreviewConfig(config);
        const auto id = Yaml::text(config, "resourceId"),
                   terrainId = Yaml::text(config, "terrainId");
        require(resource::isResourceUuid(id) && resource::isResourceUuid(terrainId),
            "EDITOR-TERRAIN-PREVIEW-IDENTITY");
        const auto limit = [&](const char* key) {
            const auto value = Yaml::uint64(Yaml::required(config, key));
            require(value && value <= SIZE_MAX, "EDITOR-TERRAIN-PREVIEW-BUDGET");
            return static_cast<std::size_t>(value);
        };
        input.remainingFileBytes = limit("maxFileBytes");
        input.remainingDecodedBytes = limit("maxDecodedBytes");
        const auto maxPreview = limit("maxPreviewBytes");
        const auto resolution = Yaml::uint32(config, "resolution");
        require(resolution >= 2 && std::uint64_t(resolution) * resolution <= maxPreview / 20,
            "EDITOR-TERRAIN-PREVIEW-BUDGET");
        assets::TerrainSurfacePreviewInput surfaces;
        surfaces.snowLine = static_cast<float>(Yaml::number(config, "snowLine"));
        surfaces.snowFeather = static_cast<float>(Yaml::number(config, "snowFeather"));
        const auto sourceId = resource::deriveResourceUuid(id, "source");
        const auto shaderId = resource::deriveResourceUuid(id, "builtin-pbr-v1");
        auto source = core::makeUnique<resource::ExternalAssetSourceResource>();
        source->configureMetadata(reference(sourceId, "ExternalAssetSourceResource"));
        source->sourcePath = relativeConfig;
        source->importerKind = "editor.terrain-preview/v1";
        publish(candidate, std::move(source));
        auto shader = core::makeUnique<resource::ShaderPackageResource>();
        shader->configureMetadata(reference(shaderId, "ShaderPackageResource"));
        shader->slangPath = "builtin/editor_viewport_pbr.slang";
        shader->spirvPath = "builtin/editor_viewport_pbr.spv";
        shader->mslPath = "builtin/editor_viewport_pbr.metal";
        shader->reflectionPath = "builtin/editor_viewport_pbr.reflection.yaml";
        shader->materialInterfaceContract = resource::kPbrMetallicRoughnessContract;
        shader->materialInterfaceVersion = resource::kPbrMetallicRoughnessContractVersion;
        publish(candidate, std::move(shader));
        const auto& layers = Yaml::sequence(config, "layers");
        require(layers.size() == 6, "EDITOR-TERRAIN-PREVIEW-LAYERS");
        std::array<core::pmr::String, 6> layerIds;
        constexpr std::array<core::StringView, 6> semantics{
            "grassMoss", "forestFloor", "wetMud", "rockyPath", "mossyRock", "dirtySnow"};
        for (std::size_t i = 0; i < 6; ++i) {
            const auto& layer = layers[i];
            Yaml::onlyKeys(layer,
                {"semantic",
                    "materialId",
                    "physicalSizeMeters",
                    "normalConvention",
                    "albedo",
                    "normal",
                    "roughness",
                    "occlusion"});
            require(Yaml::text(layer, "semantic") == semantics[i]
                    && Yaml::text(layer, "normalConvention") == "opengl",
                "EDITOR-TERRAIN-PREVIEW-LAYER-SEMANTIC");
            layerIds[i] = Yaml::text(layer, "materialId");
            require(resource::isResourceUuid(layerIds[i]), "EDITOR-TERRAIN-PREVIEW-IDENTITY");
            auto& surface = surfaces.layers[i];
            surface.physicalSizeMeters =
                static_cast<float>(Yaml::number(layer, "physicalSizeMeters"));
            surface.albedo = input.image(Yaml::required(layer, "albedo"), true);
            surface.normal = input.image(Yaml::required(layer, "normal"), false);
            surface.roughness = input.image(Yaml::required(layer, "roughness"), false);
            surface.occlusion = input.image(Yaml::required(layer, "occlusion"), false);
            auto material = core::makeUnique<resource::MaterialResource>();
            material->configureMetadata(reference(layerIds[i], "MaterialResource"));
            materialFields(*material, sourceId, shaderId);
            publish(candidate, std::move(material));
        }

        const auto terrainPath = Yaml::text(config, "terrain");
        const auto terrainManifest = input.yaml(terrainPath);
        require(Yaml::text(terrainManifest, "resourceId") == terrainId,
            "EDITOR-TERRAIN-PREVIEW-IDENTITY");
        const auto& vegetation = Yaml::required(config, "vegetation");
        Yaml::onlyKeys(vegetation, {"resourceId", "catalog", "input"});
        const auto vegetationId = Yaml::text(vegetation, "resourceId");
        require(vegetationId == Yaml::text(terrainManifest, "vegetationCatalogResourceId"),
            "EDITOR-TERRAIN-PREVIEW-VEGETATION-CONTRACT");
        if (vegetationId.empty()) {
            require(Yaml::text(vegetation, "input") == "absent"
                    && Yaml::text(vegetation, "catalog").empty(),
                "EDITOR-TERRAIN-PREVIEW-VEGETATION-CONTRACT");
        } else {
            // The legacy importer still opens model files synchronously. Mounted
            // projects cannot invoke it until its source profile is migrated.
            require(!sources, "EDITOR-TERRAIN-PREVIEW-LEGACY-VEGETATION-SOURCE-UNSUPPORTED");
            require(candidate.recordSnapshot(vegetationId) == nullptr,
                "EDITOR-TERRAIN-PREVIEW-DUPLICATE-ID");
            require(Yaml::text(vegetation, "input") == "legacy-source/v2",
                "EDITOR-TERRAIN-PREVIEW-VEGETATION-CONTRACT");
            const auto catalogPath = Yaml::text(vegetation, "catalog");
            // Validate the legacy catalog's complete, explicit model input set before
            // invoking the existing importer. No directory search or guessed path.
            const auto catalog = input.yaml(catalogPath);
            const auto& fields = Yaml::required(catalog, "fields");
            require(Yaml::uint32(fields, "contractVersion") == 2,
                "EDITOR-TERRAIN-PREVIEW-VEGETATION-CONTRACT");
            const auto runtimePath = input.contained(catalogPath).parent_path()
                / std::filesystem::path(Yaml::text(fields, "runtimeCatalog"));
            input.admitFile(runtimePath);
            const auto runtimeText = core::readTextFile(runtimePath, 1024u * 1024u);
            require(runtimeText.success, "EDITOR-TERRAIN-PREVIEW-READ-BUDGET");
            const auto runtime = Yaml::parseSingleDocument(runtimeText.text);
            for (const auto& asset : Yaml::sequence(Yaml::required(runtime, "fields"), "assets"))
                input.admitFile(runtimePath.parent_path()
                    / std::filesystem::path(Yaml::text(Yaml::required(asset, "fields"), "source")));
            resource::VegetationCatalogResourceLoadDesc catalogDesc;
            catalogDesc.catalogPath = input.contained(catalogPath);
            catalogDesc.resourceId = Yaml::text(vegetation, "resourceId");
            catalogDesc.status = "Prototype Complete";
            const auto catalogLoaded =
                resource::loadVegetationCatalogResource(candidate, catalogDesc);
            if (!catalogLoaded.success) {
                diagnostic = catalogLoaded.diagnostics;
                return false;
            }
        }
        auto sourceContext = input.context();
        resource::TerrainAssetResource terrainSource(reference(terrainId, "TerrainAssetResource"),
            input.contained(terrainPath),
            sourceContext.origin);
        const auto manifest = terrainSource.inspectManifest(sourceContext.resources);
        require(manifest.success, "EDITOR-TERRAIN-PREVIEW-TERRAIN-MANIFEST");
        for (const auto& file : manifest.files)
            input.admitFile(std::filesystem::path(file.path));
        require(input.remainingDecodedBytes > 0, "EDITOR-TERRAIN-PREVIEW-DECODE-BUDGET");
        sourceContext = input.context();
        auto terrainObject = core::makeUnique<resource::TerrainAssetResource>(
            reference(terrainId, "TerrainAssetResource"),
            input.contained(terrainPath),
            sourceContext.origin);
        require(
            candidate.recordSnapshot(terrainId) == nullptr, "EDITOR-TERRAIN-PREVIEW-DUPLICATE-ID");
        const auto loaded =
            candidate.loadResource(std::move(terrainObject), sourceContext.resources);
        if (!loaded.success) {
            diagnostic = loaded.diagnostics;
            return false;
        }
        const auto handle = candidate.find<resource::TerrainAssetResource>(terrainId);
        const auto& asset = *handle.get();
        require(asset.hasSurfaceWeights() && asset.layers().size() == 8,
            "EDITOR-TERRAIN-PREVIEW-LAYERS");
        for (std::size_t i = 0; i < 6; ++i)
            require(asset.layers()[i].materialResourceId == layerIds[i],
                "EDITOR-TERRAIN-PREVIEW-MATERIAL-BINDING");
        const auto tileCount = std::uint64_t(asset.tileCountX()) * asset.tileCountZ();
        const auto tileBytes = std::uint64_t(resolution) * resolution * 20;
        require(tileCount <= maxPreview / tileBytes, "EDITOR-TERRAIN-PREVIEW-BUDGET");
        auto preview = core::makeUnique<EditorTerrainPreviewMaterial>();
        preview->configureMetadata(reference(id, "MaterialResource"));
        materialFields(*preview, sourceId, shaderId);
        preview->terrainId = terrainId;
        preview->terrainGeneration = handle.generation();
        preview->resolution = resolution;
        for (std::uint32_t z = 0; z < asset.tileCountZ(); ++z)
            for (std::uint32_t x = 0; x < asset.tileCountX(); ++x) {
                const terrain::TerrainTileCoord coord{
                    static_cast<std::int32_t>(x), static_cast<std::int32_t>(z)};
                resource::TerrainAssetTileReadView view;
                require(
                    asset.tileReadView(coord, view) && view.valid(), "EDITOR-TERRAIN-PREVIEW-TILE");
                assets::TerrainSurfacePreviewTile tile;
                tile.samplesPerEdge = view.samplesPerEdge;
                tile.outputResolution = resolution;
                tile.originX = asset.bounds().minX + x * asset.tileWorldSize();
                tile.originZ = asset.bounds().minZ + z * asset.tileWorldSize();
                tile.worldSize = asset.tileWorldSize();
                tile.heights = {view.heights, view.heightCount};
                tile.pageA = {view.weightPageA, view.weightPageAByteCount};
                tile.pageB = {view.weightPageB, view.weightPageBByteCount};
                tile.maxOutputBytes = tileBytes;
                auto baked = assets::TerrainSurfacePreviewBaker().bake(surfaces, tile);
                require(baked.success, "EDITOR-TERRAIN-PREVIEW-BAKE");
                const auto tileId =
                    resource::deriveResourceUuid(id, "tile", z * asset.tileCountX() + x);
                auto material = core::makeUnique<resource::MaterialResource>();
                material->configureMetadata(reference(tileId, "MaterialResource"));
                materialFields(*material, sourceId, shaderId);
                core::pmr::String* slots[]{&material->baseColorTextureId,
                    &material->normalTextureId,
                    &material->metallicRoughnessTextureId,
                    &material->occlusionTextureId,
                    &material->emissiveTextureId};
                std::array<resource::ResourceGeneration, 5> textureGenerations{};
                for (std::size_t i = 0; i < 5; ++i) {
                    *slots[i] = resource::deriveResourceUuid(tileId, "texture", i);
                    auto texture = core::makeUnique<resource::TextureResource>();
                    texture->configureMetadata(reference(*slots[i], "TextureResource"));
                    texture->sourceResourceId = sourceId;
                    const auto& image = baked.textures[i];
                    texture->usage = image.srgb ? "color" : i == 1 ? "normal" : "data";
                    texture->srgb = texture->decodedSrgb = image.srgb;
                    texture->decodedWidth = image.width;
                    texture->decodedHeight = image.height;
                    texture->decodedRgba8 = std::move(baked.textures[i].rgba8);
                    publish(candidate, std::move(texture));
                    textureGenerations[i] =
                        candidate.find<resource::TextureResource>(*slots[i]).generation();
                }
                publish(candidate, std::move(material));
                preview->tiles.push_back({coord,
                    tileId,
                    candidate.find<resource::MaterialResource>(tileId).generation(),
                    textureGenerations});
                if (const auto* instances = asset.vegetationFor(coord))
                    preview->vegetationInstanceCount += instances->size();
            }
        publish(candidate, std::move(preview));
        diagnostic.clear();
        return true;
    } catch (const core::YamlContractError& error) {
        diagnostic = error.diagnostic;
    } catch (...) {
        diagnostic = "EDITOR-TERRAIN-PREVIEW-PREPARATION";
    }
    return false;
}

bool loadEditorTerrainPreview(const std::filesystem::path& projectRoot,
    std::string_view relativeConfig,
    resource::ResourceManager& candidate,
    core::pmr::String& diagnostic,
    ProjectInputSnapshot* inputSnapshot)
{
    return prepareEditorTerrainPreview(
        projectRoot, relativeConfig, candidate, diagnostic, inputSnapshot, nullptr);
}

bool loadEditorTerrainPreviewSources(const ProjectResourceSources& sources,
    std::string_view relativeConfig,
    resource::ResourceManager& candidate,
    core::pmr::String& diagnostic,
    ProjectInputSnapshot* inputSnapshot)
{
    return prepareEditorTerrainPreview(
        sources.root(), relativeConfig, candidate, diagnostic, inputSnapshot, &sources);
}

resource::AssetPackageReadPlan planEditorTerrainPreviewSources(const std::filesystem::path& root,
    std::string_view relativeConfig,
    const resource::AssetPackageSourceContext& context,
    const resource::AssetPackageReadBudget& budget)
{
    resource::AssetPackageReadPlan result;
    try {
        require(context.resources.sources && context.resources.requirePreloadedSources
                && context.origin.pathScope == core::io::FilePathScope::MountedResource
                && context.origin.mount && budget.maxMetadataBytes
                && budget.maxMetadataBytes != SIZE_MAX && budget.maxFileBytes
                && budget.maxFileBytes != SIZE_MAX,
            "EDITOR-TERRAIN-PREVIEW-READ-PLAN-CONFIG");
        const auto add = [&](core::StringView relative, std::size_t limit) {
            std::filesystem::path path;
            require(ProjectInputSnapshot::validLogicalPath(relative)
                    && resource::resolveResourceSourcePath(root, relative, context.origin, path),
                "EDITOR-TERRAIN-PREVIEW-PATH");
            require(resource::appendResourceReadIntent(core::FilePath::utf8(path),
                        limit,
                        result.reads,
                        result.diagnostics,
                        context.origin),
                "EDITOR-TERRAIN-PREVIEW-READ-PLAN-CONFIG");
            return path;
        };
        const auto configPath = add(relativeConfig, budget.maxMetadataBytes);
        if (!context.resources.sources->find(core::FilePath::utf8(configPath))) {
            result.success = true;
            return result;
        }
        const auto text = resource::readResourceText(core::FilePath::utf8(configPath),
            context.resources,
            budget.maxMetadataBytes,
            &context.origin);
        require(text.success, "EDITOR-TERRAIN-PREVIEW-TEXT");
        const auto config = Yaml::parseSingleDocument(text.text);
        validatePreviewConfig(config);
        const auto& vegetation = Yaml::required(config, "vegetation");
        Yaml::onlyKeys(vegetation, {"resourceId", "catalog", "input"});
        require(Yaml::text(vegetation, "resourceId").empty()
                && Yaml::text(vegetation, "catalog").empty()
                && Yaml::text(vegetation, "input") == "absent",
            "EDITOR-TERRAIN-PREVIEW-LEGACY-VEGETATION-SOURCE-UNSUPPORTED");
        const auto& layers = Yaml::sequence(config, "layers");
        require(layers.size() == 6, "EDITOR-TERRAIN-PREVIEW-LAYERS");
        for (const auto& layer : layers)
            for (const auto* channel : {"albedo", "normal", "roughness", "occlusion"}) {
                const auto& image = Yaml::required(layer, channel);
                validateImageInput(image, core::StringView(channel) == "albedo");
                add(Yaml::text(image, "path"), budget.maxFileBytes);
            }
        const auto terrainPath = add(Yaml::text(config, "terrain"), budget.maxMetadataBytes);
        const auto terrainId = Yaml::text(config, "terrainId");
        require(resource::isResourceUuid(terrainId), "EDITOR-TERRAIN-PREVIEW-IDENTITY");
        if (context.resources.sources->find(core::FilePath::utf8(terrainPath))) {
            const auto manifest = resource::TerrainAssetResource(
                reference(terrainId, "TerrainAssetResource"), terrainPath, context.origin)
                                      .inspectManifest(context.resources);
            if (!manifest.success) {
                result.diagnostics = manifest.diagnostics;
                return result;
            }
            for (const auto& file : manifest.files)
                result.reads.push_back(file);
        }
        result.success = true;
    } catch (const core::YamlContractError& error) {
        result.diagnostics = error.diagnostic;
    } catch (...) {
        result.diagnostics = "EDITOR-TERRAIN-PREVIEW-READ-PLAN-PREPARATION";
    }
    return result;
}

core::pmr::String resolveEditorTerrainPreviewMaterial(const resource::ResourceManager& resources,
    const core::pmr::String& terrainId,
    resource::ResourceGeneration generation,
    const core::pmr::String& previewId,
    terrain::TerrainTileCoord coord)
{
    const auto root = resources.find<resource::MaterialResource>(previewId);
    const auto* preview =
        root.ready() ? dynamic_cast<const EditorTerrainPreviewMaterial*>(root.get()) : nullptr;
    if (!preview || preview->terrainId != terrainId || preview->terrainGeneration != generation)
        return {};
    for (const auto& tile : preview->tiles)
        if (tile.coord == coord) {
            const auto material = resources.find<resource::MaterialResource>(tile.materialId);
            if (material.ready() && material.generation() == tile.materialGeneration) {
                const auto& m = *material.get();
                const core::pmr::String* ids[]{&m.baseColorTextureId,
                    &m.normalTextureId,
                    &m.metallicRoughnessTextureId,
                    &m.occlusionTextureId,
                    &m.emissiveTextureId};
                for (std::size_t i = 0; i < 5; ++i) {
                    const auto texture = resources.find<resource::TextureResource>(*ids[i]);
                    if (!texture.ready() || texture.generation() != tile.textureGenerations[i])
                        return {};
                }
                return tile.materialId;
            }
        }
    return {};
}
} // namespace arp::editor

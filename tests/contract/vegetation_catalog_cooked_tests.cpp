#include "core/encoding/unsigned_element_span.h"
#include "core/encoding/yaml_contract.h"
#include "core/hash/stable_hash.h"
#include "core/io/file_io.h"
#include "core/io/filesystem_path.h"
#include "core/runtime/async_runtime.h"
#include "reflection/serialization.h"
#include "resource/asset_package_closure.h"
#include "resource/asset_package_payload.h"
#include "resource/internal_asset_package.h"
#include "resource/prefab_asset_resources.h"
#include "resource/resource_graph_executor.h"
#include "resource/terrain_asset_build_service.h"
#include "resource/terrain_asset_resource.h"
#include "resource/vegetation_catalog_build_service.h"
#include "resource/vegetation_catalog_cooked.h"
#include "tests/fixtures/cooked_vegetation_fixture.h"
#include "tests/fixtures/package_async_fixture.h"
#include "tests/fixtures/package_source_fixture.h"
#include "tests/fixtures/static_glb_geometry.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <thread>

namespace {
using namespace arp;
using namespace arp::resource;
using Yaml = core::YamlContract;
constexpr const char* catalogId = "3ea4c197-d012-4e64-8f5f-6a415ec2c109";
constexpr const char* materialId = "33c5dcdf-4f06-482a-94eb-26baf7c937fb";
constexpr const char* terrainId = "3f6fbcca-17d2-46a6-9817-2af357399301";
constexpr const char* textureId = "51a4c197-d012-4e64-8f5f-6a415ec2c109";
constexpr const char* shaderId = "61a4c197-d012-4e64-8f5f-6a415ec2c109";
constexpr const char* sourceId = "71a4c197-d012-4e64-8f5f-6a415ec2c109";
constexpr std::size_t geometryBytes = 204u; // Three 60-byte vertices, 3 indices, one section.

ResourceReference reference(
    const char* id = catalogId, const char* type = "VegetationCatalogResource")
{
    return {id, type, {}, "Contract Complete", {}};
}

class Fixture final {
public:
    static VegetationCatalogResourceData catalog()
    {
        return tests::CookedVegetationFixture::catalog();
    }

    static core::UniquePtr<VegetationCatalogResource> resource(const std::filesystem::path& path)
    {
        VegetationCatalogResourceLoadDesc desc;
        desc.catalogPath = path;
        desc.resourceId = catalogId;
        desc.status = "Contract Complete";
        desc.input = VegetationCatalogInput::Cooked;
        return core::makeUnique<VegetationCatalogResource>(std::move(desc));
    }

    static void material(ResourceManager& manager)
    {
        auto value = core::makeUnique<MaterialResource>();
        value->configureMetadata(reference(materialId, "MaterialResource"));
        assert(manager.loadResource(std::move(value)).success);
    }

    static InternalAssetPackageEntry envelope(
        const ResourceReference& ref, const Yaml::Node& payload, const char* path)
    {
        const auto hash = computeResourcePayloadHash(Yaml::serialize(payload));
        assert(hash.success);
        auto yaml = Yaml::Node::mapping();
        yaml["format"] = "ArpRuntimeResource";
        yaml["version"] = 1;
        yaml["resourceId"] = ref.id.c_str();
        yaml["resourceType"] = ref.type.c_str();
        yaml["status"] = ref.status.c_str();
        yaml["contentHash"] = hash.hash;
        yaml["dependencies"] = Yaml::Node::sequence();
        yaml["payload"] = payload;
        InternalAssetPackageEntry entry;
        entry.resourceId = ref.id;
        entry.resourceType = ref.type;
        entry.status = ref.status;
        entry.contentHash = hash.hash;
        entry.envelopePath = path;
        entry.envelopeYaml = Yaml::serialize(yaml);
        return entry;
    }

    static InternalAssetPackageDesc package()
    {
        InternalAssetPackageDesc desc;
        desc.name = "cooked-terrain";
        desc.status = "Contract Complete";
        auto payload = Yaml::Node::mapping();
        payload["type"] = "TerrainAssetResource";
        payload["fields"] = Yaml::Node::mapping();
        payload["fields"]["manifestPath"] = "terrain.asset.yaml";
        desc.entries.push_back(envelope(
            reference(terrainId, "TerrainAssetResource"), payload, "resources/terrain.yaml"));
        payload["type"] = "VegetationCatalogResource";
        payload["fields"]["manifestPath"] = "catalog/catalog.cooked.yaml";
        desc.entries.push_back(envelope(reference(), payload, "resources/catalog.yaml"));
        registerPrefabAssetReflection();
        const auto materialYaml =
            reflection::serialize_to_yaml(reflection::object(MaterialResource{}));
        assert(materialYaml.success);
        desc.entries.push_back(envelope(reference(materialId, "MaterialResource"),
            Yaml::parseSingleDocument(materialYaml.yaml),
            "resources/material.yaml"));
        return desc;
    }

    static void writePackage(const std::filesystem::path& root)
    {
        std::filesystem::create_directories(root);
        const auto cooked = VegetationCatalogCookedCodec().write(
            reference(), catalog(), root / "catalog", geometryBytes);
        if (!cooked.success)
            std::fprintf(stderr, "%s\n", cooked.diagnostics.c_str());
        assert(cooked.success);
        TerrainAssetBuildDesc terrain;
        terrain.version = TerrainAssetVersion::V2;
        terrain.hasSurfaceWeights = false;
        terrain.hasVegetation = true;
        terrain.resourceId = terrainId;
        terrain.bounds = {0.0f, 2.0f, 0.0f, 2.0f};
        terrain.tileCountX = terrain.tileCountZ = 1u;
        terrain.samplesPerEdge = 2u;
        terrain.tileWorldSize = 2.0f;
        terrain.vegetationCatalogResourceId = catalogId;
        TerrainAssetBuildTile tile;
        tile.heights = {1.0f, 3.0f, 5.0f, 7.0f};
        TerrainStaticVegetationInstance instance;
        instance.prototypeId = 1u;
        instance.position = {1.0f, 4.0f, 1.0f};
        instance.variationSeed = 42u;
        tile.vegetation.push_back(instance);
        terrain.tiles.push_back(std::move(tile));
        assert(TerrainAssetWriter().write(terrain, root).success);
        const auto written = InternalAssetPackageWriter().write(package(), root);
        if (!written.success)
            std::fprintf(stderr, "%s\n", written.diagnostics.c_str());
        assert(written.success);
    }

    static void readPackage(const std::filesystem::path& root)
    {
        ResourceManager manager;
        const auto result = InternalAssetPackageReader().load(root, manager);
        if (!result.success)
            std::fprintf(stderr, "%s\n", result.diagnostics.c_str());
        assert(result.success);
        auto terrain = manager.find<TerrainAssetResource>(terrainId);
        assert(terrain.ready() && terrain.get()->hasVegetation());
        assert(terrain.get()->sample(1.0f, 1.0f).sample.height == 4.0f);
        assert(manager.find<VegetationCatalogResource>(catalogId).get()->data->findTemplate(
            "grass-a"));
        assert(manager.find<MaterialResource>(materialId).ready());
        const auto surface = terrain.get()->snapshotSurface();
        assert(manager.unload(terrainId).success);
        assert(manager.unload(catalogId).success);
        assert(manager.unload(materialId).success);
        for (const auto* id : {textureId, shaderId, sourceId})
            if (manager.recordSnapshot(id))
                assert(manager.unload(id).success);
        assert(surface->sample(1.0f, 1.0f).sample.height == 4.0f);
        assert(InternalAssetPackageReader().load(root, manager).success);
    }
};

void writeManifest(const std::filesystem::path& path, const Yaml::Node& yaml)
{
    std::ofstream file(path, std::ios::binary);
    file << Yaml::serialize(yaml);
    assert(file.good());
}

// CPU file-integrity fixtures. Shader and texture bytes deliberately have no
// GPU/decode meaning; this suite never uses them as executable/decoded assets.
class ClosureFixture final {
public:
    static void seal(const std::filesystem::path& root, InternalAssetPackageDesc desc)
    {
        desc.closurePath = "closure.arpkg.yaml";
        assert(InternalAssetPackageWriter().write(desc, root).success);
        core::pmr::Vector<core::pmr::String> paths;
        const auto append = [&](const auto& intents) {
            for (const auto& file : intents)
                paths.push_back(core::FilePath::utf8(
                    std::filesystem::path(file.path.c_str()).lexically_relative(root)));
        };
        append(TerrainAssetResource(
            reference(terrainId, "TerrainAssetResource"), root / "terrain.asset.yaml")
                .inspectManifest()
                .files);
        append(VegetationCatalogCookedCodec()
                .inspect(reference(), root / "catalog/catalog.cooked.yaml")
                .files);
        for (const auto& entry : desc.entries) {
            if (!AssetPackagePayloadInspector().supports(entry.resourceType))
                continue;
            const auto payload = AssetPackagePayloadInspector().inspect(entry, true);
            assert(payload.success);
            for (const auto& file : payload.files)
                paths.push_back(file);
        }
        std::sort(paths.begin(), paths.end());
        paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
        auto index = Yaml::Node::mapping();
        index["type"] = "AssetPackageClosure";
        index["contractVersion"] = "resource.package.closure/v1";
        index["status"] = "Contract Complete";
        index["maxResources"] = 32;
        index["maxFiles"] = 64;
        index["maxFileBytes"] = 1048576;
        index["maxTotalFileBytes"] = 4194304;
        index["maxDecodedBytes"] = 1048576;
        index["files"] = Yaml::Node::sequence();
        for (const auto& path : paths) {
            const auto content = core::readBinaryFile(root / path.c_str());
            assert(content.success);
            core::StableHash64 hash;
            hash.appendBytes(content.bytes.data(), content.bytes.size());
            char hex[17]{};
            std::snprintf(
                hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(hash.value()));
            auto file = Yaml::Node::mapping();
            file["path"] = path.c_str();
            file["byteCount"] = static_cast<std::int64_t>(content.bytes.size());
            file["contentHash"] = hex;
            index["files"].get_value_ref<Yaml::Node::sequence_type&>().push_back(std::move(file));
        }
        writeManifest(root / "closure.arpkg.yaml", index);
    }

    static void write(const std::filesystem::path& root)
    {
        Fixture::writePackage(root);
        auto desc = Fixture::package();
        MaterialResource material;
        material.baseColorTextureId = textureId;
        material.shaderPackageId = shaderId;
        material.sourceResourceId = sourceId;
        TextureResource texture;
        texture.sourceResourceId = sourceId;
        texture.sourcePath = "files/texture.cpu-fixture";
        ShaderPackageResource shader;
        // Shared file binding is listed/copied once, with no assumed extension semantics.
        shader.slangPath = shader.spirvPath = shader.mslPath = shader.reflectionPath =
            "files/shader.cpu-fixture";
        ExternalAssetSourceResource source;
        source.sourcePath = "absent-authoring-provenance.glb";
        const auto entry =
            [&](const auto& object, const char* id, const char* type, const char* path) {
                const auto payload = reflection::serialize_to_yaml(reflection::object(object));
                assert(payload.success);
                return Fixture::envelope(
                    reference(id, type), Yaml::parseSingleDocument(payload.yaml), path);
            };
        desc.entries[2] =
            entry(material, materialId, "MaterialResource", "resources/material.yaml");
        desc.entries.push_back(
            entry(texture, textureId, "TextureResource", "resources/texture.yaml"));
        desc.entries.push_back(
            entry(shader, shaderId, "ShaderPackageResource", "resources/shader.yaml"));
        desc.entries.push_back(
            entry(source, sourceId, "ExternalAssetSourceResource", "resources/source.yaml"));
        std::filesystem::create_directory(root / "files");
        for (const auto* name :
            {"texture.cpu-fixture", "shader.cpu-fixture", "unlisted.cpu-fixture"}) {
            std::ofstream file(root / "files" / name, std::ios::binary);
            file << "CPU file integrity fixture only";
        }
        seal(root, std::move(desc));
    }

    static void read(const std::filesystem::path& root)
    {
        Fixture::readPackage(root);
        ResourceManager manager;
        assert(InternalAssetPackageReader().load(root, manager).success);
        assert(manager.find<TextureResource>(textureId).ready());
        assert(manager.find<ShaderPackageResource>(shaderId).ready());
        assert(manager.find<ExternalAssetSourceResource>(sourceId).ready());
        const auto material = manager.find<MaterialResource>(materialId);
        assert(material.get()->baseColorTextureId == textureId
            && material.get()->shaderPackageId == shaderId);
        assert(manager.unload(terrainId).success && manager.unload(catalogId).success
            && manager.unload(materialId).success && manager.unload(textureId).success
            && manager.unload(shaderId).success && manager.unload(sourceId).success);
        assert(InternalAssetPackageReader().load(root, manager).success);
    }
};

class AuthoringFixture final {
public:
    static Yaml::Node document()
    {
        auto root = Yaml::parseSingleDocument(
            "type: TerrainAssetAuthoring\ncontractVersion: terrain.authoring/v2\n"
            "resourceId: 3f6fbcca-17d2-46a6-9817-2af357399301\nalias: terrain/composed\n"
            "minX: 0.0\nminZ: 0.0\ntileCountX: 1\ntileCountZ: 1\nsamplesPerEdge: 2\n"
            "tileWorldSize: 2.0\nmaxDecodedBytes: 96\ninput: {type: constant/v1, height: 4.0}\n"
            "dependencyPackage: dependencies\npackageBudget:\n  maxResources: 16\n  maxFiles: 64\n"
            "  maxFileBytes: 1048576\n  maxTotalFileBytes: 4194304\n  maxDecodedBytes: 1048576\n"
            "surface:\n  type: constant-base6-overlays2/v1\n  pageA: [255, 0, 0, 0]\n"
            "  pageB: [0, 0, 51, 102]\n  layers: []\nvegetation:\n  type: static-instances/v1\n"
            "  catalogResourceId: 3ea4c197-d012-4e64-8f5f-6a415ec2c109\n  instances:\n"
            "    - tileX: 0\n      tileZ: 0\n      prototypeId: 1\n      position: [1.0, 4.0, 1.0]\n"
            "      rotation: [0.0, 0.0, 0.0, 1.0]\n      scale: [1.0, 1.0, 1.0]\n      variationSeed: 42\n");
        constexpr const char* names[] = {"grassMoss",
            "forestFloor",
            "wetMud",
            "rockyPath",
            "mossyRock",
            "dirtySnow",
            "snowOverlay",
            "wetnessOverlay"};
        for (std::size_t i = 0; i < 8u; ++i) {
            auto layer = Yaml::Node::mapping();
            layer["layerId"] = names[i];
            layer["semantic"] = names[i];
            layer["materialResourceId"] = materialId;
            layer["page"] = static_cast<std::int64_t>(i / 4u);
            layer["channel"] = static_cast<std::int64_t>(i % 4u);
            root["surface"]["layers"].get_value_ref<Yaml::Node::sequence_type&>().push_back(
                std::move(layer));
        }
        return root;
    }

    static void write(const std::filesystem::path& root)
    {
        const auto dependencies = root / "dependencies";
        ClosureFixture::write(dependencies);
        auto desc = InternalAssetPackageReader().read(dependencies).package;
        desc.entries.erase(desc.entries.begin());
        const auto indexed = AssetPackageClosureService().prepareIndex(
            desc, dependencies, {16u, 64u, 1048576u, 4194304u, 1048576u});
        if (!indexed.success)
            std::fprintf(stderr, "%s\n", indexed.diagnostics.c_str());
        assert(indexed.success);
        writeManifest(root / "authoring.yaml", document());
    }

    static void read(const std::filesystem::path& root)
    {
        ClosureFixture::read(root);
        ResourceManager manager;
        assert(InternalAssetPackageReader().load(root, manager).success);
        const auto terrain = manager.find<TerrainAssetResource>(terrainId);
        assert(terrain.get()->hasSurfaceWeights() && terrain.get()->hasVegetation());
        TerrainAssetTileReadView tile;
        assert(terrain.get()->tileReadView({0, 0}, tile) && tile.valid());
        assert(tile.weightPageA[0] == 255u && tile.weightPageB[2] == 51u
            && tile.weightPageB[3] == 102u);
        const auto* instances = terrain.get()->vegetationFor({0, 0});
        assert(instances && instances->size() == 1u && (*instances)[0].prototypeId == 1u
            && (*instances)[0].position.y == 4.0f && (*instances)[0].variationSeed == 42u);
    }
};

class CatalogAuthoringFixture final {
public:
    static Yaml::Node document()
    {
        return Yaml::parseSingleDocument(
            "type: VegetationCatalogAuthoring\ncontractVersion: vegetation.catalog.authoring/v1\n"
            "resourceId: 3ea4c197-d012-4e64-8f5f-6a415ec2c109\nalias: vegetation/authored\n"
            "inputContract: static-glb-geometry/v1\nmaxSourceBytes: 1048576\n"
            "maxTotalSourceBytes: 2097152\nmaxImportBytes: 4194304\nmaxDecodedBytes: 204\n"
            "maxTemplates: 4\nmaxVertices: 32\nmaxIndices: 64\nmaxPrimitives: 4\n"
            "maxJsonDepth: 16\nmaxJsonValues: 256\ndependencyPackage: dependencies\n"
            "packageBudget:\n  maxResources: 16\n  maxFiles: 64\n  maxFileBytes: 1048576\n"
            "  maxTotalFileBytes: 4194304\n  maxDecodedBytes: 1048576\nassets:\n"
            "  - assetId: grass-a\n    source: plant.glb\n    role: ground_cover\n"
            "    category: ground_cover\n    heightClass: low\n    templateCategory: foliage_grass\n"
            "    templateId: 1\n    variantIndex: 0\n    lodIndex: 0\n    metersPerUnit: 2.0\n"
            "    baseColor: [0.125, 0.25, 0.5, 1.0]\n    wind: [0.25, 0.5, 0.75]\n"
            "    materials:\n      - slot: 0\n        resourceId: 33c5dcdf-4f06-482a-94eb-26baf7c937fb\n");
    }

    static void write(const std::filesystem::path& root)
    {
        ClosureFixture::write(root / "dependencies");
        auto desc = InternalAssetPackageReader().read(root / "dependencies").package;
        desc.entries.erase(desc.entries.begin(), desc.entries.begin() + 2);
        assert(AssetPackageClosureService()
                .prepareIndex(desc, root / "dependencies", {16u, 64u, 1048576u, 4194304u, 1048576u})
                .success);
        tests::StaticGlbGeometryFixture::write(
            root / "plant.glb", tests::StaticGlbGeometryFixture::document());
        writeManifest(root / "catalog-authoring.yaml", document());
        auto terrain = AuthoringFixture::document();
        terrain["dependencyPackage"] = "catalog.arpkg";
        writeManifest(root / "authoring.yaml", terrain);
    }

    static std::uint64_t read(const std::filesystem::path& root)
    {
        ResourceManager manager;
        const auto loaded = InternalAssetPackageReader().load(root, manager);
        if (!loaded.success)
            std::fprintf(stderr, "%s\n", loaded.diagnostics.c_str());
        assert(loaded.success);
        const auto catalog = manager.find<VegetationCatalogResource>(catalogId);
        assert(catalog.ready());
        const auto* mesh = catalog.get()->data->findTemplate("grass-a");
        assert(mesh && mesh->vertices.size() == 3u && mesh->indices.size() == 3u);
        assert(mesh->vertices[0].position[0] == 2.5f && mesh->vertices[2].position[2] == -1.5f);
        assert(mesh->vertices[0].primaryBend == 0.25f && mesh->vertices[0].flutter == 0.5f
            && mesh->vertices[0].stiffness == 0.75f && mesh->vertices[0].normal[2] == 1.0f);
        assert(mesh->baseColor[0] == 0.125f && mesh->baseColor[3] == 1.0f);
        assert(mesh->sections.size() == 1u && mesh->sections[0].materialSlot == 0u);
        assert(catalog.get()->data->assets[0].materials[0].resourceId == materialId);
        core::StableHash64 hash;
        catalog.get()->appendContentFingerprint(hash);
        const auto fingerprint = hash.value();
        for (const auto* id : {catalogId, materialId, textureId, shaderId, sourceId})
            assert(manager.unload(id).success);
        assert(InternalAssetPackageReader().load(root, manager).success);
        core::StableHash64 reloaded;
        manager.find<VegetationCatalogResource>(catalogId).get()->appendContentFingerprint(
            reloaded);
        assert(reloaded.value() == fingerprint);
        return fingerprint;
    }
};

void testCatalogAuthoring(const std::filesystem::path& root)
{
    CatalogAuthoringFixture::write(root);
    VegetationCatalogBuildService service;
    const auto document = CatalogAuthoringFixture::document();
    const auto prepared = service.prepare(Yaml::serialize(document), root);
    if (!prepared.success)
        std::fprintf(stderr, "%s\n", prepared.diagnostics.c_str());
    assert(prepared.success && prepared.data && prepared.decodedBytes == geometryBytes
        && prepared.importPeakBytes > geometryBytes && prepared.sourceBytes > geometryBytes);
    assert(prepared.data->findTemplate("grass-a") && prepared.data->contractVersion == 1u);
    const auto authoring = root / "catalog-authoring.yaml";
    const auto output = root / "catalog.arpkg";
    const auto built = service.build(authoring, output);
    if (!built.success)
        std::fprintf(stderr, "%s\n", built.diagnostics.c_str());
    assert(built.success);
    const auto stable = CatalogAuthoringFixture::read(output);
    assert(!service.build(authoring, output).success);
    assert(service.build(authoring, root / "repeated").success);
    assert(CatalogAuthoringFixture::read(root / "repeated") == stable);
    assert(core::readBinaryFile(output / "closure.arpkg.yaml").bytes
        == core::readBinaryFile(root / "repeated/closure.arpkg.yaml").bytes);
    const auto rejected = [&](const Yaml::Node& bad) {
        writeManifest(authoring, bad);
        const auto failed = root / "failed";
        const auto result = service.build(authoring, failed);
        if (result.success)
            std::fprintf(
                stderr, "Unexpected catalog authoring success: %s\n", Yaml::serialize(bad).c_str());
        assert(!result.success && !result.diagnostics.empty());
        assert(!std::filesystem::exists(failed)
            && !std::filesystem::exists(root / "failed.arp-staging")
            && !std::filesystem::exists(root / "failed.authoring.arp-staging"));
    };
    for (const auto* budget : {"maxSourceBytes",
             "maxTotalSourceBytes",
             "maxImportBytes",
             "maxDecodedBytes",
             "maxVertices",
             "maxIndices",
             "maxJsonDepth",
             "maxJsonValues"}) {
        auto bad = document;
        bad[budget] = 1;
        rejected(bad);
    }
    for (const auto* budget :
        {"maxResources", "maxFiles", "maxFileBytes", "maxTotalFileBytes", "maxDecodedBytes"}) {
        auto bad = document;
        bad["packageBudget"][budget] = 1;
        rejected(bad);
    }
    auto bad = document;
    bad["assets"][0]["wind"][1] = 1.01;
    rejected(bad);
    for (double units : {0.0, -1.0, 1.0e39, 1.0e-60}) {
        bad = document;
        bad["assets"][0]["metersPerUnit"] = units;
        rejected(bad);
    }
    for (const auto* id : {shaderId, terrainId, "invalid"}) {
        bad = document;
        bad["assets"][0]["materials"][0]["resourceId"] = id;
        rejected(bad);
    }
    bad = document;
    bad["assets"][0]["materials"][0]["slot"] = 1;
    rejected(bad);
    for (const auto* field : {"role", "category", "heightClass", "templateCategory"}) {
        bad = document;
        bad["assets"][0][field] = "unsupported";
        rejected(bad);
    }
    bad = document;
    bad["resourceId"] = materialId;
    rejected(bad);
    for (const auto* source : {"../plant.glb", "absent.glb", "dependencies"}) {
        bad = document;
        bad["assets"][0]["source"] = source;
        rejected(bad);
    }
    bad = document;
    bad["assets"][0]["templateId"] = 2;
    rejected(bad);
    bad = document;
    bad["assets"][0]["variantIndex"] = 1;
    rejected(bad);
    bad = document;
    bad["assets"][0]["lodIndex"] = 1;
    rejected(bad);
    bad = document;
    bad["assets"][0]["guessNormals"] = true;
    rejected(bad);
    tests::StaticGlbGeometryFixture::write(root / "plant.glb", "{}");
    rejected(document);
    tests::StaticGlbGeometryFixture::write(
        root / "plant.glb", tests::StaticGlbGeometryFixture::document());
    writeManifest(authoring, document);
    std::filesystem::create_directory(root / "occupied.authoring.arp-staging");
    assert(!service.build(authoring, root / "occupied").success);
    assert(std::filesystem::is_directory(root / "occupied.authoring.arp-staging"));
    assert(CatalogAuthoringFixture::read(output) == stable);
    assert(
        TerrainAssetBuildService().build(root / "authoring.yaml", root / "terrain.arpkg").success);
    AuthoringFixture::read(root / "terrain.arpkg");
    auto multiple = document;
    auto second = multiple["assets"][0];
    second["assetId"] = "grass-b";
    second["templateId"] = 2;
    second["variantIndex"] = 1;
    multiple["assets"].get_value_ref<Yaml::Node::sequence_type&>().push_back(second);
    multiple["maxDecodedBytes"] = 2 * geometryBytes;
    writeManifest(authoring, multiple);
    assert(service.build(authoring, root / "multiple").success);
    ResourceManager multipleManager;
    assert(InternalAssetPackageReader().load(root / "multiple", multipleManager).success);
    const auto data = multipleManager.find<VegetationCatalogResource>(catalogId).get()->data;
    assert(data->templates.size() == 2u && data->findTemplate("grass-b")->variantIndex == 1u);
    bad = multiple;
    bad["maxTemplates"] = 1;
    rejected(bad);
    bad = multiple;
    bad["maxTotalSourceBytes"] = prepared.sourceBytes;
    rejected(bad);
    bad = multiple;
    bad["maxDecodedBytes"] = 2 * geometryBytes - 1u;
    rejected(bad);
    bad = multiple;
    bad["assets"][1]["assetId"] = "grass-a";
    rejected(bad);
    bad = multiple;
    bad["assets"][0]["assetId"] = "grass-c";
    rejected(bad);
    // Reserved directory and envelope collisions must fail before writer use.
    const auto dependency = root / "dependencies";
    auto desc = InternalAssetPackageReader().read(dependency).package;
    auto texture = Yaml::parseSingleDocument(desc.entries[1].envelopeYaml)["payload"];
    std::filesystem::create_directory(dependency / "catalog");
    std::filesystem::copy_file(
        dependency / "files/texture.cpu-fixture", dependency / "catalog/occupied");
    texture["fields"]["sourcePath"] = "catalog/occupied";
    desc.entries[1] = Fixture::envelope(
        reference(textureId, "TextureResource"), texture, "resources/texture.yaml");
    assert(AssetPackageClosureService()
            .prepareIndex(desc, dependency, {16u, 64u, 1048576u, 4194304u, 1048576u})
            .success);
    rejected(document);
    texture["fields"]["sourcePath"] = "files/texture.cpu-fixture";
    desc.entries[1] = Fixture::envelope(
        reference(textureId, "TextureResource"), texture, "resources/CATALOG.ARPRES.YAML");
    assert(AssetPackageClosureService()
            .prepareIndex(desc, dependency, {16u, 64u, 1048576u, 4194304u, 1048576u})
            .success);
#ifdef _WIN32
    rejected(document);
#endif
    desc.entries[1].envelopePath = "resources/catalog.arpres.yaml";
    assert(AssetPackageClosureService()
            .prepareIndex(desc, dependency, {16u, 64u, 1048576u, 4194304u, 1048576u})
            .success);
    rejected(document);
}

class SpatialAuthoringFixture final {
public:
    static Yaml::Node document()
    {
        auto root = AuthoringFixture::document();
        root["minX"] = 11.25;
        root["minZ"] = -7.5;
        root["tileCountX"] = root["tileCountZ"] = 2;
        root["samplesPerEdge"] = 3;
        root["maxDecodedBytes"] = 480; // 4 tiles * 9 samples * 12 bytes + 48 instance.
        auto surface =
            Yaml::parseSingleDocument("type: rgba8-base6-overlays2/v1\npageA: a.bin\npageB: b.bin\n"
                                      "width: 5\nheight: 5\nrowDirection: decreasing-z\n");
        surface["layers"] = root["surface"]["layers"];
        root["surface"] = std::move(surface);
        root["vegetation"]["instances"][0]["position"] =
            Yaml::parseSingleDocument("[12.25, 4.0, -6.5]");
        return root;
    }

    static void writePages(const std::filesystem::path& root, bool increasing = false)
    {
        std::ofstream a(root / "a.bin", std::ios::binary);
        std::ofstream b(root / "b.bin", std::ios::binary);
        for (unsigned row = 0; row < 5u; ++row) {
            const auto z = increasing ? row : 4u - row;
            for (unsigned x = 0; x < 5u; ++x) {
                const auto v = x + 5u * z;
                const std::uint8_t pageA[] = {
                    static_cast<std::uint8_t>(x == 4u && z == 4u ? 0u : 255u - v),
                    static_cast<std::uint8_t>(x == 4u && z == 4u ? 0u : v),
                    0u,
                    0u};
                const std::uint8_t pageB[] = {0u,
                    0u,
                    static_cast<std::uint8_t>(7u * x + 11u * z),
                    static_cast<std::uint8_t>(3u * x + 5u * z)};
                a.write(reinterpret_cast<const char*>(pageA), sizeof(pageA));
                b.write(reinterpret_cast<const char*>(pageB), sizeof(pageB));
            }
        }
    }

    static void write(const std::filesystem::path& root)
    {
        AuthoringFixture::write(root);
        writePages(root);
        writeManifest(root / "authoring.yaml", document());
    }

    static std::uint64_t read(const std::filesystem::path& root)
    {
        ResourceManager manager;
        std::uint64_t fingerprint = 0;
        for (unsigned cycle = 0; cycle < 2u; ++cycle) {
            const auto loaded = InternalAssetPackageReader().load(root, manager);
            if (!loaded.success)
                std::fprintf(
                    stderr, "Spatial load cycle %u: %s\n", cycle, loaded.diagnostics.c_str());
            assert(loaded.success);
            const auto* terrain = manager.find<TerrainAssetResource>(terrainId).get();
            assert(terrain && terrain->hasSurfaceWeights() && terrain->hasVegetation());
            const auto probe = terrain->sample(12.25f, -4.5f); // Global lattice (1,3).
            assert(probe.ready() && probe.sample.height == 4.0f);
            assert(probe.sample.baseWeights[1] == 16.0f / 255.0f
                && probe.sample.snowOverlay == 40.0f / 255.0f
                && probe.sample.wetnessOverlay == 18.0f / 255.0f);
            const auto between = terrain->sample(12.75f, -5.0f); // Lattice (1.5,2.5).
            assert(between.ready() && between.sample.baseWeights[1] == 14.0f / 255.0f
                && between.sample.snowOverlay == 38.0f / 255.0f);
            const auto zero = terrain->sample(15.25f, -3.5f);
            assert(zero.ready());
            for (auto weight : zero.sample.baseWeights)
                assert(weight == 0.0f);
            std::array<TerrainAssetTileReadView, 4> views;
            for (int z = 0; z < 2; ++z)
                for (int x = 0; x < 2; ++x) {
                    auto& view = views[z * 2 + x];
                    assert(terrain->tileReadView({x, z}, view) && view.valid());
                    assert(view.weightPageAByteCount == 36u && view.weightPageBByteCount == 36u);
                }
            // Both axes share all endpoint bytes on both pages, including the four-tile corner.
            for (unsigned tile = 0; tile < 2u; ++tile)
                for (unsigned i = 0; i < 3u; ++i)
                    for (unsigned channel = 0; channel < 4u; ++channel) {
                        for (bool pageB : {false, true}) {
                            const auto page = [&](unsigned index) {
                                return pageB ? views[index].weightPageB : views[index].weightPageA;
                            };
                            assert(page(tile * 2u)[(i * 3u + 2u) * 4u + channel]
                                == page(tile * 2u + 1u)[i * 3u * 4u + channel]);
                            assert(page(tile)[(6u + i) * 4u + channel]
                                == page(tile + 2u)[i * 4u + channel]);
                        }
                    }
            const auto* instances = terrain->vegetationFor({0, 0});
            assert(instances && instances->size() == 1u && (*instances)[0].position.x == 12.25f
                && (*instances)[0].variationSeed == 42u);
            const auto generation = terrain->generation();
            if (cycle)
                assert(generation == fingerprint);
            fingerprint = generation;
            assert(manager.unload(terrainId).success && manager.unload(catalogId).success
                && manager.unload(materialId).success && manager.unload(textureId).success
                && manager.unload(shaderId).success && manager.unload(sourceId).success);
        }
        return fingerprint;
    }
};

void testSpatialAuthoring(const std::filesystem::path& root)
{
    SpatialAuthoringFixture::write(root);
    TerrainAssetBuildService service;
    const auto original = SpatialAuthoringFixture::document();
    const auto authoring = root / "authoring.yaml";
    const auto output = root / "built";
    const auto built = service.build(authoring, output);
    if (!built.success)
        std::fprintf(stderr, "%s\n", built.diagnostics.c_str());
    assert(built.success);
    const auto stable = SpatialAuthoringFixture::read(output);
    SpatialAuthoringFixture::writePages(root, true);
    auto increasing = original;
    increasing["surface"]["rowDirection"] = "increasing-z";
    writeManifest(authoring, increasing);
    assert(service.build(authoring, root / "increasing").success);
    assert(SpatialAuthoringFixture::read(root / "increasing") == stable);
    assert(core::readBinaryFile(output / "closure.arpkg.yaml").bytes
        == core::readBinaryFile(root / "increasing/closure.arpkg.yaml").bytes);
    SpatialAuthoringFixture::writePages(root);
    const auto reject = [&](const auto& change, const char* diagnostic) {
        auto bad = original;
        change(bad);
        writeManifest(authoring, bad);
        const auto result = service.build(authoring, root / "failed");
        if (result.success || result.diagnostics != diagnostic)
            std::fprintf(stderr, "Expected %s, got %s\n", diagnostic, result.diagnostics.c_str());
        assert(!result.success && result.diagnostics == diagnostic);
        assert(!std::filesystem::exists(root / "failed")
            && !std::filesystem::exists(root / "failed.arp-staging")
            && !std::filesystem::exists(root / "failed.authoring.arp-staging"));
    };
    for (const auto* dimension : {"width", "height"})
        for (auto value : {0u, 4u, UINT32_MAX})
            reject(
                [&](auto& n) {
                    n["surface"][dimension] = value;
                },
                "TERRAIN-WEIGHTMAP-LAYOUT-OR-BUDGET");
    reject(
        [](auto& n) {
            n["surface"]["rowDirection"] = "guess";
        },
        "TERRAIN-WEIGHTMAP-LAYOUT-OR-BUDGET");
    reject(
        [](auto& n) {
            n["maxDecodedBytes"] = 199;
        },
        "TERRAIN-WEIGHTMAP-LAYOUT-OR-BUDGET");
    reject(
        [](auto& n) {
            n["maxDecodedBytes"] = 479;
        },
        "TERRAIN-BAKE-BUDGET-OR-PRESENCE");
    reject(
        [](auto& n) {
            n["packageBudget"]["maxDecodedBytes"] = 199;
        },
        "TERRAIN-WEIGHTMAP-LAYOUT-OR-BUDGET");
    for (const auto* path : {"../a.bin", "C:/outside.bin", "absent.bin", "dependencies"})
        reject(
            [&](auto& n) {
                n["surface"]["pageB"] = path;
            },
            "TERRAIN-WEIGHTMAP-PATH");
    reject(
        [](auto& n) {
            n["surface"]["pageA"] = std::string("a.bin\0hidden", 12u);
        },
        "TERRAIN-WEIGHTMAP-PATH");
    // Exact length: reject both oversized and truncated pages before baking.
    for (auto bytes : {99u, 101u}) {
        std::filesystem::resize_file(root / "b.bin", bytes);
        reject([](auto&) {}, "TERRAIN-WEIGHTMAP-BYTE-COUNT");
    }
    SpatialAuthoringFixture::writePages(root);
    // Raw height + both weight inputs share the same explicit input allocation allowance.
    {
        const std::array<char, 100> height{};
        std::ofstream file(root / "height.bin", std::ios::binary);
        file.write(height.data(), height.size());
    }
    reject(
        [](auto& n) {
            n["input"] = Yaml::parseSingleDocument(
                "type: r32-float-le/v1\npath: height.bin\nwidth: 5\nheight: 5\n"
                "rowDirection: increasing-z\nscale: 1.0\noffset: 4.0\n");
            n["maxDecodedBytes"] = 299;
        },
        "TERRAIN-AUTHORING-INPUT-BUDGET");
    // A source content change updates generation, while identity and the old output remain stable.
    {
        std::fstream file(root / "b.bin", std::ios::binary | std::ios::in | std::ios::out);
        file.seekp(2);
        file.put(static_cast<char>(99));
    }
    writeManifest(authoring, original);
    assert(service.build(authoring, root / "changed").success);
    ResourceManager manager;
    assert(InternalAssetPackageReader().load(root / "changed", manager).success);
    assert(manager.find<TerrainAssetResource>(terrainId).get()->generation() != stable);
    assert(!service.build(authoring, output).success);
    assert(SpatialAuthoringFixture::read(output) == stable);
}

void testAuthoring(const std::filesystem::path& root)
{
    AuthoringFixture::write(root);
    TerrainAssetBuildService service;
    const auto original = AuthoringFixture::document();
    const auto authoring = root / "authoring.yaml";
    const auto output = root / "built";
    const auto built = service.build(authoring, output);
    if (!built.success)
        std::fprintf(stderr, "%s\n", built.diagnostics.c_str());
    assert(built.success);
    AuthoringFixture::read(output);
    const auto generation = [&](const std::filesystem::path& path) {
        ResourceManager manager;
        assert(InternalAssetPackageReader().load(path, manager).success);
        return manager.find<TerrainAssetResource>(terrainId).get()->generation();
    };
    const auto stable = generation(output);
    assert(!service.build(authoring, output).success && generation(output) == stable);
    assert(service.build(authoring, root / "repeated").success);
    assert(generation(root / "repeated") == stable);
    assert(core::readBinaryFile(output / "closure.arpkg.yaml").bytes
        == core::readBinaryFile(root / "repeated/closure.arpkg.yaml").bytes);
    const auto rejected = [&](const Yaml::Node& document) {
        writeManifest(authoring, document);
        const auto failed = root / "failed";
        const auto result = service.build(authoring, failed);
        assert(!result.success && !result.diagnostics.empty());
        assert(!std::filesystem::exists(failed)
            && !std::filesystem::exists(root / "failed.arp-staging")
            && !std::filesystem::exists(root / "failed.authoring.arp-staging"));
        assert(generation(output) == stable);
    };
    auto bad = original;
    bad["surface"]["layers"][0]["page"] = 1;
    rejected(bad);
    bad = original;
    bad["surface"]["layers"][1]["semantic"] = "grassMoss";
    rejected(bad);
    bad = original;
    bad["surface"]["pageA"][0] = 256;
    rejected(bad);
    bad = original;
    bad["surface"]["layers"][0]["materialResourceId"] = shaderId;
    rejected(bad);
    bad = original;
    bad["vegetation"]["instances"][0]["prototypeId"] = 2;
    rejected(bad);
    bad = original;
    bad["vegetation"]["instances"][0]["rotation"][3] = 0.0;
    rejected(bad);
    bad = original;
    bad["vegetation"]["instances"][0]["tileX"] = 1;
    rejected(bad);
    bad = original;
    bad["vegetation"]["instances"][0]["position"][0] = 3.0;
    rejected(bad);
    bad = original;
    bad["maxDecodedBytes"] = 95;
    rejected(bad);
    bad = original;
    bad["packageBudget"]["maxDecodedBytes"] = 95;
    rejected(bad);
    bad = original;
    bad["packageBudget"]["maxResources"] = 5;
    rejected(bad);
    bad = original;
    bad["packageBudget"]["maxFileBytes"] = 1;
    rejected(bad);
    bad = original;
    bad["resourceId"] = materialId;
    rejected(bad);
    bad = original;
    bad["dependencyPackage"] = "../outside";
    rejected(bad);
    bad = original;
    bad["dependencyPackage"] = "";
    rejected(bad);
    bad = original;
    bad["surface"]["layers"][0]["layerId"] = std::string("grass\0ignored", 13u);
    rejected(bad);
    const auto occupied = root / "occupied.authoring.arp-staging";
    std::filesystem::create_directory(occupied);
    writeManifest(authoring, original);
    assert(!service.build(authoring, root / "occupied").success
        && std::filesystem::is_directory(occupied));
    std::filesystem::remove(occupied);
    // Reserved paths must reject aliases on the native filesystem before any
    // dependency bytes can be overwritten by the terrain writer.
    const auto dependencyRoot = root / "dependencies";
    auto depDesc = InternalAssetPackageReader().read(dependencyRoot).package;
    auto texture = Yaml::parseSingleDocument(depDesc.entries[2].envelopeYaml)["payload"];
    texture["fields"]["sourcePath"] = "TERRAIN/occupied.cpu-fixture";
    depDesc.entries[2] = Fixture::envelope(
        reference(textureId, "TextureResource"), texture, "resources/texture.yaml");
    std::filesystem::create_directory(dependencyRoot / "TERRAIN");
    std::filesystem::copy_file(dependencyRoot / "files/texture.cpu-fixture",
        dependencyRoot / "TERRAIN/occupied.cpu-fixture");
    const auto collisionIndex = AssetPackageClosureService().prepareIndex(
        depDesc, dependencyRoot, {16u, 64u, 1048576u, 4194304u, 1048576u});
    assert(collisionIndex.success);
    // Native Windows is case insensitive; other filesystems may permit it.
#ifdef _WIN32
    rejected(original);
#endif
    texture["fields"]["sourcePath"] = "terrain/occupied.cpu-fixture";
    depDesc.entries[2] = Fixture::envelope(
        reference(textureId, "TextureResource"), texture, "resources/texture.yaml");
    std::filesystem::create_directories(dependencyRoot / "terrain");
    if (!std::filesystem::exists(dependencyRoot / "terrain/occupied.cpu-fixture"))
        std::filesystem::copy_file(dependencyRoot / "TERRAIN/occupied.cpu-fixture",
            dependencyRoot / "terrain/occupied.cpu-fixture");
    assert(AssetPackageClosureService()
            .prepareIndex(depDesc, dependencyRoot, {16u, 64u, 1048576u, 4194304u, 1048576u})
            .success);
    rejected(original);
    // Explicit absent layouts also work without a dependency package.
    auto geometry = original;
    geometry["surface"] = Yaml::parseSingleDocument("type: none\n");
    geometry["vegetation"] = Yaml::parseSingleDocument("type: none\n");
    geometry["dependencyPackage"] = "";
    geometry["maxDecodedBytes"] = 16;
    writeManifest(authoring, geometry);
    assert(service.build(authoring, root / "geometry").success);
    writeManifest(authoring, original);
}

void testFileClosure(const std::filesystem::path& root)
{
    ClosureFixture::write(root);
    AssetPackageClosureService service;
    const auto valid = service.inspect(root);
    if (!valid.success)
        std::fprintf(stderr, "%s\n", valid.diagnostics.c_str());
    assert(valid.success && valid.package.entries.size() == 6u);
    const auto indexPath = root / "closure.arpkg.yaml";
    const auto original = Yaml::parseSingleDocument(core::readTextFile(indexPath).text);
    const auto rejected = [&](Yaml::Node index, const char* diagnostic) {
        writeManifest(indexPath, index);
        const auto result = service.inspect(root);
        assert(!result.success && result.diagnostics == diagnostic);
        ResourceManager manager;
        assert(!InternalAssetPackageReader().load(root, manager).success);
        assert(!manager.recordSnapshot(terrainId) && !manager.recordSnapshot(materialId));
        writeManifest(indexPath, original);
    };
    auto bad = original;
    bad["files"][0]["contentHash"] = "0000000000000000";
    rejected(bad, "PACKAGE-CLOSURE-FILE-HASH-OR-SIZE");
    bad = original;
    bad["files"][0]["byteCount"] = 0;
    rejected(bad, "PACKAGE-CLOSURE-FILE-HASH-OR-SIZE");
    bad = original;
    bad["files"].get_value_ref<Yaml::Node::sequence_type&>().pop_back();
    rejected(bad, "PACKAGE-CLOSURE-FILE-MISSING");
    bad = original;
    bad["files"][0]["path"] = "files/unlisted.cpu-fixture";
    rejected(bad, "PACKAGE-CLOSURE-FILE-SET-OR-ORDER");
    bad = original;
    bad["files"][1] = bad["files"][0];
    rejected(bad, "PACKAGE-CLOSURE-FILE-SET-OR-ORDER");
    for (const auto* field : {"maxFileBytes", "maxTotalFileBytes"}) {
        bad = original;
        bad[field] = 1;
        rejected(bad, "PACKAGE-CLOSURE-FILE-BUDGET");
    }
    bad = original;
    bad["maxResources"] = 1;
    rejected(bad, "PACKAGE-CLOSURE-BUDGET");
    bad = original;
    bad["maxFiles"] = 1;
    rejected(bad, "PACKAGE-CLOSURE-FILE-COUNT");
    bad = original;
    bad["maxDecodedBytes"] = 0;
    rejected(bad, "PACKAGE-CLOSURE-BUDGET");
    ResourceManager small;
    ResourceBudgetConfig budget;
    budget.maxDecodedBytes = geometryBytes - 1u;
    small.setBudgetConfig(budget);
    assert(!InternalAssetPackageReader().load(root, small).success);
    assert(!small.recordSnapshot(terrainId)
        && small.telemetry().budget.maxDecodedBytes == budget.maxDecodedBytes);
    auto desc = valid.package;
    auto texture = Yaml::parseSingleDocument(desc.entries[3].envelopeYaml)["payload"];
    texture["fields"]["sourcePath"] = "../outside";
    desc.entries[3] = Fixture::envelope(
        reference(textureId, "TextureResource"), texture, "resources/texture.yaml");
    assert(InternalAssetPackageWriter().write(desc, root).success);
    assert(service.inspect(root).diagnostics == "PACKAGE-TEXTURE-FILE-BINDING");
    texture["fields"]["sourcePath"] = "resources/material.yaml";
    desc.entries[3] = Fixture::envelope(
        reference(textureId, "TextureResource"), texture, "resources/texture.yaml");
    assert(InternalAssetPackageWriter().write(desc, root).success);
    assert(service.inspect(root).diagnostics == "PACKAGE-CLOSURE-METADATA-COLLISION");
    desc = valid.package;
    desc.entries.pop_back();
    assert(InternalAssetPackageWriter().write(desc, root).success);
    assert(service.inspect(root).diagnostics == "PACKAGE-CLOSURE-DEPENDENCY-MISSING-OR-TYPE");
    desc = valid.package;
    auto unknown = Yaml::parseSingleDocument(desc.entries[4].envelopeYaml)["payload"];
    unknown["type"] = "UnknownClosureResource";
    desc.entries[4] = Fixture::envelope(
        reference(shaderId, "UnknownClosureResource"), unknown, "resources/shader.yaml");
    // Remove the material reference first so unsupported-type validation is the failure.
    auto material = Yaml::parseSingleDocument(desc.entries[2].envelopeYaml)["payload"];
    material["fields"]["shaderPackageId"] = "";
    desc.entries[2] = Fixture::envelope(
        reference(materialId, "MaterialResource"), material, "resources/material.yaml");
    assert(InternalAssetPackageWriter().write(desc, root).success);
    assert(service.inspect(root).diagnostics == "PACKAGE-CLOSURE-RESOURCE-TYPE-UNSUPPORTED");
    assert(InternalAssetPackageWriter().write(valid.package, root).success);
    const auto unbounded = InternalAssetPackageReader().read(root);
    assert(unbounded.success);
    const auto bounded =
        InternalAssetPackageReader().read(root, AssetPackageClosureService::maxMetadataBytes, 2u);
    assert(!bounded.success && bounded.diagnostics == "PACKAGE-RESOURCE-COUNT-BUDGET");
    const auto manifestPath = root / "manifest.arpkg.yaml";
    const auto manifestBytes = core::readTextFile(manifestPath).text;
    {
        std::ofstream file(manifestPath, std::ios::binary);
        file << std::string(AssetPackageClosureService::maxMetadataBytes + 1u, ' ');
    }
    ResourceManager oversized;
    assert(!InternalAssetPackageReader().load(root, oversized).success
        && !oversized.recordSnapshot(terrainId));
    {
        std::ofstream file(manifestPath, std::ios::binary);
        file << manifestBytes;
    }
    const auto output = root.parent_path() / "published";
    const auto staging = std::filesystem::path(output.string() + ".arp-staging");
    std::filesystem::create_directory(staging);
    assert(!service.publish(root, output).success && std::filesystem::is_directory(staging));
    std::filesystem::remove(staging);
    const auto published = service.publish(root, output);
    if (!published.success)
        std::fprintf(stderr, "%s\n", published.diagnostics.c_str());
    assert(published.success && !std::filesystem::exists(output / "files/unlisted.cpu-fixture"));
    assert(!service.publish(root, output).success);
    const auto moved = root.parent_path() / "relocated";
    std::filesystem::rename(output, moved);
    ClosureFixture::read(moved);
    std::filesystem::remove(root / "files/texture.cpu-fixture");
    assert(!service.inspect(root).success);
    assert(service.inspect(moved).success);
    // Hash-correct, structurally invalid geometry must fail the staged decode,
    // after the integrity/index check succeeds, and remove only owned staging.
    const auto blockPath = moved / "catalog/geometry.bin";
    auto block = core::readBinaryFile(blockPath).bytes;
    block[180] = 255u;
    {
        std::ofstream file(blockPath, std::ios::binary);
        file.write(reinterpret_cast<const char*>(block.data()), block.size());
    }
    auto catalog =
        Yaml::parseSingleDocument(core::readTextFile(moved / "catalog/catalog.cooked.yaml").text);
    core::StableHash64 hash;
    hash.appendBytes(block.data(), block.size());
    catalog["geometry"]["contentHash"] = std::to_string(hash.value());
    writeManifest(moved / "catalog/catalog.cooked.yaml", catalog);
    ClosureFixture::seal(moved, valid.package);
    assert(service.inspect(moved).success);
    const auto late = service.publish(moved, output);
    assert(!late.success && late.diagnostics == "VEGETATION-COOKED-GEOMETRY-INVALID");
    assert(!std::filesystem::exists(output)
        && !std::filesystem::exists(output.string() + ".arp-staging"));
}

void testGraph(const std::filesystem::path& manifest)
{
    core::runtime::AsyncRuntime runtime({16u, 1u, 4u, 4u});
    ResourceManager manager;
    ResourceGraphExecutor executor(
        manager, runtime.fileService(), runtime.workService(), runtime.completionQueue());
    ResourceGraphExecutionRequest request;
    request.graph.rootResourceId = catalogId;
    ResourceGraphNode catalog;
    catalog.resource = Fixture::resource(manifest);
    const auto index = VegetationCatalogCookedCodec().inspect(reference(), manifest);
    assert(index.success);
    catalog.dependencies = index.dependencies;
    catalog.resource->configureMetadata(reference(), index.dependencies);
    request.graph.nodes.push_back(std::move(catalog));
    ResourceGraphNode material;
    material.resource = core::makeUnique<MaterialResource>();
    material.resource->configureMetadata(reference(materialId, "MaterialResource"));
    request.graph.nodes.push_back(std::move(material));
    request.budget = {2u, 2u, 1024u * 1024u, geometryBytes, 4u};

    struct Completion final {
        bool done = false;
        ResourceGraphExecutionResult result;
    } completion;

    assert(executor.submit(std::move(request),
               {[](void* context, ResourceGraphExecutionResult result) {
                    auto& completion = *static_cast<Completion*>(context);
                    completion.result = std::move(result);
                    completion.done = true;
                },
                   &completion})
        == ResourceGraphSubmitStatus::Accepted);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!completion.done && std::chrono::steady_clock::now() < deadline) {
        runtime.dispatch();
        std::this_thread::yield();
    }
    if (completion.done && !completion.result.resource.success)
        std::fprintf(stderr, "%s\n", completion.result.resource.diagnostics.c_str());
    assert(completion.done && completion.result.resource.success);
    assert(completion.result.telemetry.completedNodes == 2u);
    assert(manager.find<VegetationCatalogResource>(catalogId).get()->processedByteCount()
        == geometryBytes);
    runtime.shutdown(core::runtime::AsyncRuntimeShutdownPolicy::Drain);
}

void testCodec(const std::filesystem::path& root)
{
    VegetationCatalogCookedCodec codec;
    auto input = Fixture::catalog();
    const auto cooked = root / "one";
    assert(codec.write(reference(), input, cooked, geometryBytes).success);
    const auto manifest = cooked / "catalog.cooked.yaml";
    const auto original = Yaml::parseSingleDocument(core::readTextFile(manifest).text);
    const auto decoded = codec.decode(reference(), manifest);
    assert(decoded.success && decoded.decodedBytes == geometryBytes && decoded.files.size() == 2u);
    assert(decoded.dependencies.size() == 1u && decoded.dependencies[0].id == materialId);
    const auto& mesh = *decoded.data->findTemplate("grass-a");
    assert(mesh.id == 1u && mesh.materialIndex == 7u && mesh.sections[0].materialSlot == 7u);
    assert(mesh.vertices[0].position[0] == 1.25f && mesh.vertices[2].position[2] == -0.75f);
    assert(mesh.vertices[0].primaryBend == 0.25f && mesh.vertices[0].flutter == 0.5f
        && mesh.vertices[0].stiffness == 0.75f && mesh.baseColor[0] == 0.125f);
    assert(decoded.data->templateConfig.grassCardCount == 1u);
    const auto bytes = core::readBinaryFile(cooked / "geometry.bin");
    assert(bytes.success && bytes.bytes.size() == geometryBytes);
    assert(bytes.bytes[0] == 0u && bytes.bytes[1] == 0u && bytes.bytes[2] == 0xa0u
        && bytes.bytes[3] == 0x3fu);
    assert(bytes.bytes[184u] == 1u && bytes.bytes[188u] == 2u && bytes.bytes[200u] == 7u);
    assert(!codec.write(reference(), input, cooked, geometryBytes).success);
    assert(core::readBinaryFile(cooked / "geometry.bin").bytes == bytes.bytes);
    assert(!codec.write(reference(), input, root / "budget", geometryBytes - 1u).success);
    assert(!std::filesystem::exists(root / "budget"));
    assert(codec.write(reference(), input, root / "two", geometryBytes).success);
    assert(codec.decode(reference(), root / "two/catalog.cooked.yaml").contentFingerprint
        == decoded.contentFingerprint);
    input.templates[0].vertices[0].position[0] = 1.5f;
    assert(codec.write(reference(), input, root / "changed", geometryBytes).success);
    assert(codec.decode(reference(), root / "changed/catalog.cooked.yaml").contentFingerprint
        != decoded.contentFingerprint);
    auto variants = Fixture::catalog();
    variants.assets.push_back(variants.assets[0]);
    variants.assets[1].id = "grass-b";
    variants.assets[1].templateId = 2u;
    variants.templates.push_back(variants.templates[0]);
    variants.templates[1].id = 2u;
    variants.templates[1].variantIndex = 1u;
    variants.templateConfig.grassCardCount = 2u;
    assert(codec.write(reference(), variants, root / "variants", geometryBytes * 2u).success);
    const auto multi = codec.decode(reference(), root / "variants/catalog.cooked.yaml");
    assert(multi.success
        && multi.data->familyVariantCount(vegetation::TemplateCategory::FoliageGrass) == 2u);
    assert(multi.data->templateIdFor("grass-b") == 2u
        && multi.data->findTemplate("grass-b")->variantIndex == 1u);
    variants.templates[1].variantIndex = 0u;
    assert(!codec.write(reference(), variants, root / "wrong-variant", geometryBytes * 2u).success);
    assert(!std::filesystem::exists(root / "wrong-variant"));
    variants.templates[1].variantIndex = 1u;
    variants.assets[1].id = "grass-a";
    assert(!codec.write(reference(), variants, root / "duplicate-id", geometryBytes * 2u).success);
    input = Fixture::catalog();
    input.assets[0].source.append(1u, '\0');
    input.assets[0].source += "hidden";
    assert(!codec.write(reference(), input, root / "nul-source", geometryBytes).success);
    input = Fixture::catalog();
    input.templateConfig.treeLodCount = 2u;
    assert(!codec.write(reference(), input, root / "lod", geometryBytes).success);

    const auto reject = [&](const auto& mutate) {
        auto changed = original;
        mutate(changed);
        writeManifest(manifest, changed);
        assert(!codec.decode(reference(), manifest).success);
    };
    reject([](auto& n) {
        n["unknown"] = true;
    });
    reject([](auto& n) {
        n["status"] = "Production Ready";
    });
    reject([](auto& n) {
        n["contractVersion"] = "vegetation.catalog/v2";
    });
    reject([](auto& n) {
        n["maxDecodedBytes"] = 1;
    });
    reject([](auto& n) {
        n["geometry"]["byteCount"] = 203;
    });
    reject([](auto& n) {
        n["geometry"]["contentHash"] = "0";
    });
    reject([](auto& n) {
        n["geometry"]["path"] = "../one/geometry.bin";
    });
    reject([](auto& n) {
        const std::string value("geometry.bin\0hidden", 19u);
        n["geometry"]["path"] = value;
    });
    reject([](auto& n) {
        n["templates"][0]["templateId"] = 0;
    });
    reject([](auto& n) {
        n["templates"][0]["variantIndex"] = 1;
    });
    reject([](auto& n) {
        n["templates"][0]["lodIndex"] = 1;
    });
    reject([](auto& n) {
        n["templates"][0]["templateCategory"] = "procedural_grass";
    });
    reject([](auto& n) {
        n["templates"][0]["role"] = "guess";
    });
    reject([](auto& n) {
        n["templates"][0]["vertexCount"] = UINT32_MAX;
    });
    reject([](auto& n) {
        n["templates"][0]["baseColor"][0] = std::numeric_limits<double>::max();
    });
    reject([](auto& n) {
        n["templates"][0]["materials"][0]["resourceId"] = "bad-id";
    });
    writeManifest(manifest, original);
    ResourceLoadContext limited;
    limited.maxDecodedBytes = geometryBytes - 1u;
    assert(!codec.decode(reference(), manifest, limited).success);

    // Corrupt decoded geometry while recomputing the block hash: integrity alone must not admit it.
    const auto rejectWord = [&](std::size_t offset, std::uint32_t word) {
        auto corrupt = bytes.bytes;
        assert(core::UnsignedElementSpan::writeLittleEndian(word,
            core::UnsignedElementSpan::Width::Uint32,
            corrupt.data() + offset,
            corrupt.size() - offset));
        {
            std::ofstream file(cooked / "geometry.bin", std::ios::binary);
            file.write(reinterpret_cast<const char*>(corrupt.data()), corrupt.size());
        }
        core::StableHash64 hash;
        hash.appendBytes(corrupt.data(), corrupt.size());
        auto changed = original;
        changed["geometry"]["contentHash"] = core::formatUnsigned(hash.value()).c_str();
        writeManifest(manifest, changed);
        assert(!codec.decode(reference(), manifest).success);
    };
    rejectWord(0u, 0x7fc00000u);  // NaN position.
    rejectWord(48u, 0x40000000u); // Wind weight 2.
    rejectWord(180u, 3u);         // Out-of-range index.
    rejectWord(192u, 1u);         // Section gap.
    rejectWord(200u, 8u);         // Unbound slot.
    {
        std::ofstream file(cooked / "geometry.bin", std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.bytes.data()), bytes.bytes.size());
    }
    writeManifest(manifest, original);

    ResourceSourceSet sources;
    for (const auto& intent : decoded.files) {
        ResourceSourceBuffer source;
        source.path = intent.path;
        source.pathScope = intent.pathScope;
        source.mount = intent.mount;
        source.bytes = core::readBinaryFile(std::filesystem::path(intent.path.c_str())).bytes;
        assert(sources.add(std::move(source)));
    }
    ResourceSourceSet manifestOnly;
    ResourceSourceBuffer source;
    source.path = decoded.files[0].path;
    source.pathScope = decoded.files[0].pathScope;
    source.mount = decoded.files[0].mount;
    source.bytes = core::readBinaryFile(manifest).bytes;
    assert(manifestOnly.add(std::move(source)));
    std::filesystem::rename(cooked, root / "hidden");
    ResourceLoadContext preloaded;
    preloaded.sources = &sources;
    preloaded.requirePreloadedSources = true;
    preloaded.maxDecodedBytes = geometryBytes;
    assert(codec.decode(reference(), manifest, preloaded).success);
    preloaded.sources = &manifestOnly;
    const auto missingBlock = codec.decode(reference(), manifest, preloaded);
    assert(
        !missingBlock.success && missingBlock.diagnostics == "ARP-RESOURCE-SOURCE-NOT-PRELOADED");
    preloaded.sources = nullptr;
    assert(!codec.decode(reference(), manifest, preloaded).success);
    std::filesystem::rename(root / "hidden", cooked);
    // Relative block paths are relocatable semantic equivalents.
    std::filesystem::rename(cooked / "geometry.bin", cooked / "renamed.bin");
    auto relocated = original;
    relocated["geometry"]["path"] = "renamed.bin";
    writeManifest(manifest, relocated);
    assert(codec.decode(reference(), manifest).contentFingerprint == decoded.contentFingerprint);
    std::filesystem::rename(cooked / "renamed.bin", cooked / "geometry.bin");
    writeManifest(manifest, original);

    ResourceManager manager;
    assert(!manager.loadResource(Fixture::resource(manifest)).success);
    Fixture::material(manager);
    assert(manager.loadResource(Fixture::resource(manifest)).success);
    const auto epoch = manager.generationOf(catalogId);
    const auto oldData = manager.find<VegetationCatalogResource>(catalogId).get()->data;
    auto invalid = original;
    invalid["geometry"]["contentHash"] = "1";
    writeManifest(manifest, invalid);
    assert(!manager.reloadResource(Fixture::resource(manifest)).success);
    assert(manager.generationOf(catalogId) == epoch
        && manager.find<VegetationCatalogResource>(catalogId).ready());
    assert(manager.find<VegetationCatalogResource>(catalogId).get()->data == oldData);
    writeManifest(manifest, original);
    testGraph(manifest);
}

void testPackage(const std::filesystem::path& root)
{
    Fixture::writePackage(root);
    Fixture::readPackage(root);
    auto desc = Fixture::package();
    auto mismatched = desc;
    mismatched.entries[2].dependencies.push_back({catalogId, "source"});
    assert(!InternalAssetPackageWriter().write(mismatched, root / "invalid-metadata").success);
    assert(!std::filesystem::exists(root / "invalid-metadata"));
    const auto materialEnvelope = root / "resources/material.yaml";
    const auto originalEnvelope =
        Yaml::parseSingleDocument(core::readTextFile(materialEnvelope).text);
    auto changedEnvelope = originalEnvelope;
    auto hiddenDependency = Yaml::Node::mapping();
    hiddenDependency["id"] = catalogId;
    hiddenDependency["usage"] = "source";
    changedEnvelope["dependencies"].template get_value_ref<Yaml::Node::sequence_type&>().push_back(
        hiddenDependency);
    writeManifest(materialEnvelope, changedEnvelope);
    ResourceManager inconsistent;
    assert(!InternalAssetPackageReader().load(root, inconsistent).success);
    assert(!inconsistent.recordSnapshot(materialId) && !inconsistent.recordSnapshot(catalogId));
    writeManifest(materialEnvelope, originalEnvelope);
    // A catalog-only package must receive the same atomic typed treatment.
    desc.entries.erase(desc.entries.begin());
    assert(InternalAssetPackageWriter().write(desc, root).success);
    ResourceManager only;
    assert(InternalAssetPackageReader().load(root, only).success);
    assert(only.find<VegetationCatalogResource>(catalogId).ready());
    desc.entries.pop_back();
    assert(InternalAssetPackageWriter().write(desc, root).success);
    ResourceManager missing;
    assert(!InternalAssetPackageReader().load(root, missing).success);
    assert(!missing.recordSnapshot(catalogId));
    Fixture::material(missing);
    assert(InternalAssetPackageReader().load(root, missing).success);
    ResourceManager wrong;
    auto fake = core::makeUnique<ResourceBase>(reference(materialId, "MaterialResource"));
    assert(wrong.loadResource(std::move(fake)).success);
    assert(!InternalAssetPackageReader().load(root, wrong).success);
    assert(!wrong.recordSnapshot(catalogId));
    assert(InternalAssetPackageWriter().write(Fixture::package(), root).success);
    const auto manifest = root / "catalog/catalog.cooked.yaml";
    auto corrupted = Yaml::parseSingleDocument(core::readTextFile(manifest).text);
    corrupted["geometry"]["contentHash"] = "0";
    writeManifest(manifest, corrupted);
    ResourceManager rollback;
    assert(!InternalAssetPackageReader().load(root, rollback).success);
    assert(!rollback.recordSnapshot(terrainId) && !rollback.recordSnapshot(catalogId)
        && !rollback.recordSnapshot(materialId));
}

void testMaterialClosure(const std::filesystem::path& root)
{
    constexpr const char* textureId = "51a4c197-d012-4e64-8f5f-6a415ec2c109";
    Fixture::writePackage(root);
    const auto replaceMaterial = [&](MaterialResource material) {
        auto desc = Fixture::package();
        const auto payload = reflection::serialize_to_yaml(reflection::object(material));
        assert(payload.success);
        desc.entries[2] = Fixture::envelope(reference(materialId, "MaterialResource"),
            Yaml::parseSingleDocument(payload.yaml),
            "resources/material.yaml");
        return desc;
    };
    MaterialResource material;
    material.baseColorTextureId = textureId;
    auto desc = replaceMaterial(material);
    assert(InternalAssetPackageWriter().write(desc, root).success);
    ResourceManager missing;
    const auto rejected = InternalAssetPackageReader().load(root, missing);
    assert(!rejected.success && !missing.recordSnapshot(terrainId)
        && !missing.recordSnapshot(materialId));
    TextureResource texture;
    const auto payload = reflection::serialize_to_yaml(reflection::object(texture));
    assert(payload.success);
    desc.entries.push_back(Fixture::envelope(reference(textureId, "TextureResource"),
        Yaml::parseSingleDocument(payload.yaml),
        "resources/texture.yaml"));
    assert(InternalAssetPackageWriter().write(desc, root).success);
    ResourceManager complete;
    assert(InternalAssetPackageReader().load(root, complete).success);
    assert(complete.find<TextureResource>(textureId).ready());
    assert(complete.find<MaterialResource>(materialId).ready());
    // A preloaded record carrying the right type string is insufficient.
    auto externalDesc = replaceMaterial(material);
    assert(InternalAssetPackageWriter().write(externalDesc, root).success);
    ResourceManager wrongExternal;
    assert(wrongExternal
            .loadResource(core::makeUnique<ResourceBase>(reference(textureId, "TextureResource")))
            .success);
    assert(!InternalAssetPackageReader().load(root, wrongExternal).success);
    assert(!wrongExternal.recordSnapshot(terrainId));
    ResourceManager external;
    auto typed = core::makeUnique<TextureResource>();
    typed->configureMetadata(reference(textureId, "TextureResource"));
    assert(external.loadResource(std::move(typed)).success);
    assert(InternalAssetPackageReader().load(root, external).success);
    material.baseColorTextureId = materialId;
    assert(InternalAssetPackageWriter().write(replaceMaterial(material), root).success);
    ResourceManager wrongType;
    assert(!InternalAssetPackageReader().load(root, wrongType).success);
    material.baseColorTextureId = "invalid-uuid";
    assert(InternalAssetPackageWriter().write(replaceMaterial(material), root).success);
    ResourceManager invalid;
    const auto invalidResult = InternalAssetPackageReader().load(root, invalid);
    assert(!invalidResult.success
        && invalidResult.diagnostics == "PACKAGE-PAYLOAD-INVALID-RESOURCE-ID");
    assert(!invalid.recordSnapshot(materialId));
}
} // namespace

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--async-contracts") {
        unified_io_fixture::Workspace workspace;
        const auto root = workspace.root / "snapshot";
        ClosureFixture::write(root);
        tests::PackageAsyncFixture::contracts(root);
#if defined(_WIN32)
        tests::PackageAsyncFixture::run(
            core::makeShared<core::io::NativeDirectoryFileBackend>(workspace.descriptor()), true);
#endif
        return 0;
    }
    if (argc == 3 && std::string_view(argv[1]) == "--async-http") {
        for (const auto action : {tests::PackageAsyncFixture::Action::CancelAfterRead,
                 tests::PackageAsyncFixture::Action::Remount,
                 tests::PackageAsyncFixture::Action::Shutdown})
            tests::PackageAsyncFixture::run(
                tests::PackageAsyncFixture::http(argv[2]), false, action);
        for (unsigned i = 0; i < 20; ++i)
            tests::PackageAsyncFixture::run(tests::PackageAsyncFixture::http(argv[2]), true);
        const auto before = core::defaultCpuAllocator().stats();
        for (unsigned i = 0; i < 20; ++i)
            tests::PackageAsyncFixture::run(tests::PackageAsyncFixture::http(argv[2]), true);
        const auto after = core::defaultCpuAllocator().stats();
        assert(before.liveBytes == after.liveBytes);
        assert(before.allocationCount - before.deallocationCount
            == after.allocationCount - after.deallocationCount);
        std::puts(
            "Async package real HTTP with no native file list, 20 ordinary cycles core live bytes/count delta 0 PASS");
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--source-contracts") {
        using Source = tests::PackageSourceFixture;
        Source::codecContracts();
        unified_io_fixture::Workspace workspace;
        const auto sourceRoot = workspace.root / "snapshot";
        ClosureFixture::write(sourceRoot);
        for (const auto mutation : {Source::Mutation::None,
                 Source::Mutation::Missing,
                 Source::Mutation::Corrupt,
                 Source::Mutation::BadUtf8,
                 Source::Mutation::UnknownVersion,
                 Source::Mutation::WrongOrigin,
                 Source::Mutation::UnclosedCorrupt})
            Source::load(sourceRoot, Source::Backend::Memory, mutation);
#if defined(_WIN32)
        Source::load(sourceRoot, Source::Backend::Native);
#endif
        for (unsigned i = 0; i < 20; ++i)
            Source::load(sourceRoot, Source::Backend::Memory);
        const auto before = core::defaultCpuAllocator().stats();
        for (unsigned i = 0; i < 20; ++i)
            Source::load(sourceRoot, Source::Backend::Memory);
        const auto after = core::defaultCpuAllocator().stats();
        assert(before.liveBytes == after.liveBytes);
        assert(before.allocationCount - before.deallocationCount
            == after.allocationCount - after.deallocationCount);
        std::puts(
            "Package source-only native/memory, failed atomic candidate, 20 ordinary cycles: core live bytes/count delta 0 PASS");
        return 0;
    }
    if (argc == 4 && std::string_view(argv[1]) == "--source-http") {
        using Source = tests::PackageSourceFixture;
        for (unsigned i = 0; i < 20; ++i)
            Source::load(std::filesystem::u8path(argv[2]),
                Source::Backend::Http,
                Source::Mutation::None,
                argv[3]);
        const auto before = core::defaultCpuAllocator().stats();
        for (unsigned i = 0; i < 20; ++i)
            Source::load(std::filesystem::u8path(argv[2]),
                Source::Backend::Http,
                Source::Mutation::None,
                argv[3]);
        const auto after = core::defaultCpuAllocator().stats();
        assert(before.liveBytes == after.liveBytes);
        assert(before.allocationCount - before.deallocationCount
            == after.allocationCount - after.deallocationCount);
        std::puts(
            "Package source-only real engine HTTP, 20 ordinary cycles: core live bytes/count delta 0 PASS");
        return 0;
    }
    if (argc == 3) {
        if (std::string_view(argv[1]) == "--cook")
            Fixture::writePackage(argv[2]);
        else if (std::string_view(argv[1]) == "--prepare-closure")
            ClosureFixture::write(argv[2]);
        else if (std::string_view(argv[1]) == "--load-closure")
            ClosureFixture::read(argv[2]);
        else if (std::string_view(argv[1]) == "--prepare-authoring")
            AuthoringFixture::write(argv[2]);
        else if (std::string_view(argv[1]) == "--load-authoring")
            AuthoringFixture::read(argv[2]);
        else if (std::string_view(argv[1]) == "--prepare-catalog-authoring")
            CatalogAuthoringFixture::write(argv[2]);
        else if (std::string_view(argv[1]) == "--load-catalog-authoring")
            CatalogAuthoringFixture::read(argv[2]);
        else if (std::string_view(argv[1]) == "--prepare-spatial-authoring")
            SpatialAuthoringFixture::write(argv[2]);
        else if (std::string_view(argv[1]) == "--load-spatial-authoring")
            SpatialAuthoringFixture::read(argv[2]);
        else {
            assert(std::string_view(argv[1]) == "--load");
            Fixture::readPackage(argv[2]);
        }
        return 0;
    }
    const auto root = std::filesystem::temp_directory_path()
        / ("arp_cooked_vegetation_"
            + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    testCodec(root);
    testPackage(root / "package");
    testMaterialClosure(root / "material-closure");
    testFileClosure(root / "file-closure");
    testAuthoring(root / "authoring");
    testSpatialAuthoring(root / "spatial-authoring");
    testCatalogAuthoring(root / "catalog-authoring");
    std::filesystem::remove_all(root);
    std::puts(
        "Cooked vegetation codec, source, budget, identity and atomic typed package contracts passed.");
}

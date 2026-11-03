#pragma once

#include "core/encoding/yaml_contract.h"
#include "editor/tests/fixtures/project_fixture.h"
#include "editor/workspace/editor_project_resources.h"
#include "resource/terrain_asset_resource.h"
#include "tests/fixtures/vegetation_catalog_source_fixture.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace arp::editor::tests {
class TerrainPreviewSourceFixture final {
public:
    using Yaml = core::YamlContract;
    // Explicit constant PBR fixtures: sRGB albedo, OpenGL normal, linear roughness/AO.
    inline static constexpr std::uint8_t albedoPng[] = {
        0x89,
        0x50,
        0x4e,
        0x47,
        0x0d,
        0x0a,
        0x1a,
        0x0a,
        0x00,
        0x00,
        0x00,
        0x0d,
        0x49,
        0x48,
        0x44,
        0x52,
        0x00,
        0x00,
        0x00,
        0x04,
        0x00,
        0x00,
        0x00,
        0x04,
        0x08,
        0x06,
        0x00,
        0x00,
        0x00,
        0xa9,
        0xf1,
        0x9e,
        0x7e,
        0x00,
        0x00,
        0x00,
        0x15,
        0x49,
        0x44,
        0x41,
        0x54,
        0x78,
        0xda,
        0x63,
        0x74,
        0x5b,
        0xe0,
        0xf6,
        0x9f,
        0x01,
        0x09,
        0x30,
        0x31,
        0xa0,
        0x01,
        0xc2,
        0x02,
        0x00,
        0x8e,
        0x6c,
        0x02,
        0x33,
        0x88,
        0x3e,
        0x31,
        0x7e,
        0x00,
        0x00,
        0x00,
        0x00,
        0x49,
        0x45,
        0x4e,
        0x44,
        0xae,
        0x42,
        0x60,
        0x82,
    };
    inline static constexpr std::uint8_t normalPng[] = {
        0x89,
        0x50,
        0x4e,
        0x47,
        0x0d,
        0x0a,
        0x1a,
        0x0a,
        0x00,
        0x00,
        0x00,
        0x0d,
        0x49,
        0x48,
        0x44,
        0x52,
        0x00,
        0x00,
        0x00,
        0x04,
        0x00,
        0x00,
        0x00,
        0x04,
        0x08,
        0x06,
        0x00,
        0x00,
        0x00,
        0xa9,
        0xf1,
        0x9e,
        0x7e,
        0x00,
        0x00,
        0x00,
        0x15,
        0x49,
        0x44,
        0x41,
        0x54,
        0x78,
        0xda,
        0x63,
        0x6c,
        0x68,
        0xf8,
        0xff,
        0x9f,
        0x01,
        0x09,
        0x30,
        0x31,
        0xa0,
        0x01,
        0xc2,
        0x02,
        0x00,
        0xc4,
        0x53,
        0x03,
        0x06,
        0xac,
        0xd4,
        0x6a,
        0x41,
        0x00,
        0x00,
        0x00,
        0x00,
        0x49,
        0x45,
        0x4e,
        0x44,
        0xae,
        0x42,
        0x60,
        0x82,
    };
    inline static constexpr std::uint8_t roughnessPng[] = {
        0x89,
        0x50,
        0x4e,
        0x47,
        0x0d,
        0x0a,
        0x1a,
        0x0a,
        0x00,
        0x00,
        0x00,
        0x0d,
        0x49,
        0x48,
        0x44,
        0x52,
        0x00,
        0x00,
        0x00,
        0x04,
        0x00,
        0x00,
        0x00,
        0x04,
        0x08,
        0x06,
        0x00,
        0x00,
        0x00,
        0xa9,
        0xf1,
        0x9e,
        0x7e,
        0x00,
        0x00,
        0x00,
        0x16,
        0x49,
        0x44,
        0x41,
        0x54,
        0x78,
        0xda,
        0x63,
        0xdc,
        0xb2,
        0x65,
        0xcb,
        0x7f,
        0x06,
        0x24,
        0xc0,
        0xc4,
        0x80,
        0x06,
        0x08,
        0x0b,
        0x00,
        0x00,
        0xcc,
        0x4c,
        0x03,
        0x23,
        0xe7,
        0xb5,
        0xb9,
        0x05,
        0x00,
        0x00,
        0x00,
        0x00,
        0x49,
        0x45,
        0x4e,
        0x44,
        0xae,
        0x42,
        0x60,
        0x82,
    };
    inline static constexpr std::uint8_t occlusionPng[] = {
        0x89,
        0x50,
        0x4e,
        0x47,
        0x0d,
        0x0a,
        0x1a,
        0x0a,
        0x00,
        0x00,
        0x00,
        0x0d,
        0x49,
        0x48,
        0x44,
        0x52,
        0x00,
        0x00,
        0x00,
        0x04,
        0x00,
        0x00,
        0x00,
        0x04,
        0x08,
        0x06,
        0x00,
        0x00,
        0x00,
        0xa9,
        0xf1,
        0x9e,
        0x7e,
        0x00,
        0x00,
        0x00,
        0x16,
        0x49,
        0x44,
        0x41,
        0x54,
        0x78,
        0xda,
        0x63,
        0xfc,
        0xff,
        0xff,
        0xff,
        0x7f,
        0x06,
        0x24,
        0xc0,
        0xc4,
        0x80,
        0x06,
        0x08,
        0x0b,
        0x00,
        0x00,
        0x06,
        0x5d,
        0x04,
        0x04,
        0xdc,
        0x90,
        0x10,
        0xbb,
        0x00,
        0x00,
        0x00,
        0x00,
        0x49,
        0x45,
        0x4e,
        0x44,
        0xae,
        0x42,
        0x60,
        0x82,
    };

    static constexpr const char* terrainId = "7c023e4a-b5b6-4444-8123-100000000001";
    static constexpr const char* previewId = "7c023e4a-b5b6-4444-8123-100000000002";
    static constexpr std::size_t maxFileBytes = 1024 * 1024, maxTotalBytes = 4 * maxFileBytes;
    static constexpr const char* semantics[] = {"grassMoss",
        "forestFloor",
        "wetMud",
        "rockyPath",
        "mossyRock",
        "dirtySnow",
        "snowOverlay",
        "wetnessOverlay"};

    explicit TerrainPreviewSourceFixture(const std::filesystem::path& directory)
        : root(directory / "workspace")
    {
        const auto loaded = native.load(root);
        if (!loaded.success)
            std::fprintf(stderr, "native terrain source fixture: %s\n", loaded.diagnostics.c_str());
        assert(loaded.success);
        paths = {"project.arp.yaml",
            "editor.yaml",
            "preview.yaml",
            "map.png",
            "normal.png",
            "roughness.png",
            "occlusion.png",
            "source.yaml",
            "runtime/catalog.yaml",
            "runtime/models/plant.glb"};
        resource::TerrainAssetResource index(
            {terrainId, "TerrainAssetResource", {}, "Contract Complete", {}},
            root / "terrain/terrain.asset.yaml");
        const auto manifest = index.inspectManifest();
        assert(manifest.success);
        for (const auto& read : manifest.files)
            paths.push_back(
                core::FilePath::utf8(std::filesystem::u8path(read.path).lexically_relative(root)));
        std::sort(paths.begin(), paths.end());
        assert(std::adjacent_find(paths.begin(), paths.end()) == paths.end());
        for (const auto& path : paths) {
            auto bytes = core::readBinaryFile(root / std::filesystem::u8path(path), maxFileBytes);
            assert(bytes.success);
            resource::ResourceSourceBuffer file;
            file.path = path;
            file.bytes = std::move(bytes.bytes);
            files.push_back(std::move(file));
        }
        assert(native.inputBindings().size() + 1 == paths.size());
    }

    static Yaml::Node limits()
    {
        const auto desc = arp::tests::VegetationCatalogSourceFixture::descriptor({});
        const auto& source = *desc.sourceLimits;
        auto result = Yaml::Node::mapping();
        result["maxCatalogBytes"] = source.maxCatalogBytes;
        result["maxAssets"] = source.maxAssets;
        result["maxDecodedBytes"] = source.maxDecodedBytes;
        result["maxMetadataDepth"] = source.maxMetadataDepth;
        result["maxMetadataValues"] = source.maxMetadataValues;
        result["maxMetadataCollectionLength"] = source.maxMetadataCollectionLength;
        result["maxTotalSourceBytes"] = source.maxTotalSourceBytes;
        result["maxImportBytes"] = source.maxImportBytes;
        result["mesh"] = Yaml::Node::mapping();
        auto& mesh = result["mesh"];
        mesh["maxSourceBytes"] = source.mesh.geometry.maxSourceBytes;
        mesh["maxVertices"] = source.mesh.geometry.maxVertices;
        mesh["maxIndices"] = source.mesh.geometry.maxIndices;
        mesh["maxPrimitives"] = source.mesh.geometry.maxPrimitives;
        mesh["maxJsonDepth"] = source.mesh.geometry.maxJsonDepth;
        mesh["maxJsonValues"] = source.mesh.geometry.maxJsonValues;
        mesh["maxImportBytes"] = source.mesh.maxImportBytes;
        mesh["maxDecodedBytes"] = source.mesh.maxDecodedBytes;
        mesh["maxNodeVisits"] = source.mesh.maxNodeVisits;
        mesh["maxNodeDepth"] = source.mesh.maxNodeDepth;
        mesh["colorPolicy"] = "discard-validated-rgba";
        return result;
    }

    static Yaml::Node preview()
    {
        auto result = Yaml::Node::mapping();
        result["type"] = "EditorTerrainPreview";
        result["contractVersion"] = "editor.terrain-preview/v2";
        result["resourceId"] = previewId;
        result["terrainId"] = terrainId;
        result["terrain"] = "terrain/terrain.asset.yaml";
        result["vegetationPreview"] = "omit-with-diagnostic";
        result["resolution"] = 4;
        result["snowLine"] = 60.0;
        result["snowFeather"] = 20.0;
        result["maxFileBytes"] = maxFileBytes;
        result["maxDecodedBytes"] = maxTotalBytes;
        result["maxPreviewBytes"] = 640;
        result["vegetation"] = Yaml::Node::mapping();
        result["vegetation"]["input"] = "source/v2";
        result["vegetation"]["resourceId"] = arp::tests::VegetationCatalogSourceFixture::id;
        result["vegetation"]["catalog"] = "source.yaml";
        result["vegetation"]["sourceLimits"] = limits();
        result["layers"] = Yaml::Node::sequence();
        for (std::size_t i = 0; i < 6; ++i) {
            auto layer = Yaml::Node::mapping();
            layer["semantic"] = semantics[i];
            layer["materialId"] = resource::deriveResourceUuid(previewId, "layer", i).c_str();
            layer["physicalSizeMeters"] = 2.0;
            layer["normalConvention"] = "opengl";
            for (const auto* channel : {"albedo", "normal", "roughness", "occlusion"}) {
                layer[channel] = Yaml::Node::mapping();
                layer[channel]["path"] = core::StringView(channel) == "albedo" ? "map.png"
                    : core::StringView(channel) == "normal"                    ? "normal.png"
                    : core::StringView(channel) == "roughness"                 ? "roughness.png"
                                                                               : "occlusion.png";
                layer[channel]["mime"] = "image/png";
                layer[channel]["colorSpace"] =
                    core::StringView(channel) == "albedo" ? "srgb" : "linear";
            }
            result["layers"].get_value_ref<Yaml::Node::sequence_type&>().push_back(
                std::move(layer));
        }
        return result;
    }

    static void generate(const std::filesystem::path& directory)
    {
        const auto root = directory / "workspace";
        std::filesystem::create_directories(root / "runtime/models");
        const auto write = [&](const char* path, const void* bytes, std::size_t count) {
            std::ofstream file(root / path, std::ios::binary);
            file.write(static_cast<const char*>(bytes), count);
            assert(file.good());
        };
        arp::tests::VegetationCatalogSourceFixture vegetation;
        const auto inputs = vegetation.sources({});
        for (std::size_t i = 0; i < inputs.size(); ++i) {
            const auto& input = *inputs.sourceAt(i);
            write(input.path.c_str(), input.bytes.data(), input.bytes.size());
        }
        write("map.png", albedoPng, sizeof(albedoPng));
        write("normal.png", normalPng, sizeof(normalPng));
        write("roughness.png", roughnessPng, sizeof(roughnessPng));
        write("occlusion.png", occlusionPng, sizeof(occlusionPng));
        auto desc = resource::TerrainAssetBuildDesc{};
        desc.version = resource::TerrainAssetVersion::V2;
        desc.resourceId = terrainId;
        desc.vegetationCatalogResourceId = arp::tests::VegetationCatalogSourceFixture::id;
        desc.bounds = {0, 4, 0, 2};
        desc.tileCountX = 2;
        desc.tileCountZ = 1;
        desc.samplesPerEdge = 2;
        desc.tileWorldSize = 2;
        for (std::size_t i = 0; i < 8; ++i) {
            desc.layers[i].layerId = desc.layers[i].semantic = semantics[i];
            desc.layers[i].materialResourceId =
                resource::deriveResourceUuid(previewId, "layer", std::min(i, std::size_t(5)));
            desc.layers[i].page = i / 4;
            desc.layers[i].channel = i % 4;
        }
        for (std::int32_t x = 0; x < 2; ++x) {
            resource::TerrainAssetBuildTile tile;
            tile.coord = {x, 0};
            tile.heights = {float(x), float(x + 1), float(x), float(x + 1)};
            tile.weightPageA = {255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0};
            tile.weightPageB.resize(16);
            if (!x) {
                resource::TerrainStaticVegetationInstance instance;
                instance.prototypeId = 1;
                instance.position = {1, .5f, 1};
                tile.vegetation.push_back(instance);
            }
            desc.tiles.push_back(std::move(tile));
        }
        assert(resource::TerrainAssetWriter().write(desc, root / "terrain").success);
        const auto config = Yaml::serialize(preview());
        write("preview.yaml", config.data(), config.size());
        const core::pmr::String metadata =
            "type: arp.editor-project\ncontractVersion: 1\nterrainPreviews: [preview.yaml]\n";
        write("editor.yaml", metadata.data(), metadata.size());
        const auto project =
            ProjectFixture::document("assetPackages: []\nphysics2d: null\n", "editor.yaml");
        write("project.arp.yaml", project.data(), project.size());
    }

    std::filesystem::path root;
    EditorProjectResources native;
    core::pmr::Vector<core::pmr::String> paths;
    core::pmr::Vector<resource::ResourceSourceBuffer> files;
};
} // namespace arp::editor::tests

#include "resource/terrain_asset_resource.h"

#include "core/encoding/yaml_contract.h"
#include "core/io/atomic_file_service.h"
#include "core/io/file_io.h"
#include "core/io/filesystem_path.h"
#include "core/io/staged_directory.h"
#include "core/text/number_format.h"
#include "resource/resource_source_io.h"
#include "resource/vegetation_catalog_resource.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fkyaml/node.hpp>
#include <fstream>
#include <limits>
#include <string>

namespace arp::resource {
namespace {

using YamlNode = fkyaml::node;

core::pmr::String text(const char* value)
{
    return core::pmr::String(value, &core::defaultCpuMemoryResource());
}

const YamlNode* valueFor(const YamlNode& mapping, const char* key)
{
    if (!mapping.is_mapping()) {
        return nullptr;
    }
    const auto& values = mapping.get_value_ref<const YamlNode::mapping_type&>();
    const auto found = values.find(YamlNode(key));
    return found == values.end() ? nullptr : &found->second;
}

bool stringFor(const YamlNode& mapping, const char* key, core::pmr::String& out)
{
    const auto* value = valueFor(mapping, key);
    if (value == nullptr || !value->is_string()) {
        return false;
    }
    const auto& source = value->get_value_ref<const YamlNode::string_type&>();
    out.assign(source.data(), source.size());
    return true;
}

template <typename T> bool integerFor(const YamlNode& mapping, const char* key, T& out)
{
    const auto* value = valueFor(mapping, key);
    if (value == nullptr || !value->is_integer()) {
        return false;
    }
    const auto raw = value->get_value<YamlNode::integer_type>();
    if (raw < 0 || static_cast<std::uint64_t>(raw) > std::numeric_limits<T>::max()) {
        return false;
    }
    out = static_cast<T>(raw);
    return true;
}

bool u64For(const YamlNode& mapping, const char* key, std::uint64_t& out)
{
    const auto* value = valueFor(mapping, key);
    if (value == nullptr) {
        return false;
    }
    if (value->is_integer()) {
        const auto raw = value->get_value<YamlNode::integer_type>();
        if (raw < 0) {
            return false;
        }
        out = static_cast<std::uint64_t>(raw);
        return true;
    }
    if (!value->is_string()) {
        return false;
    }
    const auto& source = value->get_value_ref<const YamlNode::string_type&>();
    const auto parsed = std::from_chars(source.data(), source.data() + source.size(), out, 10);
    return parsed.ec == std::errc{} && parsed.ptr == source.data() + source.size();
}

bool floatFor(const YamlNode& mapping, const char* key, float& out)
{
    const auto* value = valueFor(mapping, key);
    if (value == nullptr || (!value->is_float_number() && !value->is_integer())) {
        return false;
    }
    out = value->is_integer() ? static_cast<float>(value->get_value<YamlNode::integer_type>())
                              : value->get_value<float>();
    return std::isfinite(out);
}

bool safeRelativePath(const core::pmr::String& path)
{
    return core::FilePath::isSafeRelativePath(path);
}

std::uint64_t hashBytes(const std::uint8_t* bytes, std::size_t byteCount)
{
    core::StableHash64 hasher;
    hasher.appendBytes(bytes, byteCount);
    return hasher.value();
}

TerrainAssetIoResult failure(const char* message)
{
    TerrainAssetIoResult result;
    result.diagnostics = text(message);
    return result;
}

void appendU32Le(core::pmr::Vector<std::uint8_t>& bytes, std::uint32_t value)
{
    for (std::uint32_t index = 0u; index < 4u; ++index) {
        bytes.push_back(static_cast<std::uint8_t>((value >> (index * 8u)) & 0xffu));
    }
}

void appendF32Le(core::pmr::Vector<std::uint8_t>& bytes, float value)
{
    std::uint32_t bits = 0u;
    std::memcpy(&bits, &value, sizeof(bits));
    appendU32Le(bytes, bits);
}

bool readU32Le(
    const core::pmr::Vector<std::uint8_t>& bytes, std::size_t& offset, std::uint32_t& out)
{
    if (offset > bytes.size() || bytes.size() - offset < 4u) {
        return false;
    }
    out = static_cast<std::uint32_t>(bytes[offset])
        | (static_cast<std::uint32_t>(bytes[offset + 1u]) << 8u)
        | (static_cast<std::uint32_t>(bytes[offset + 2u]) << 16u)
        | (static_cast<std::uint32_t>(bytes[offset + 3u]) << 24u);
    offset += 4u;
    return true;
}

bool readF32Le(const core::pmr::Vector<std::uint8_t>& bytes, std::size_t& offset, float& out)
{
    std::uint32_t bits = 0u;
    if (!readU32Le(bytes, offset, bits)) {
        return false;
    }
    std::memcpy(&out, &bits, sizeof(out));
    return std::isfinite(out);
}

bool writeFile(const std::filesystem::path& path,
    const std::uint8_t* bytes,
    std::size_t byteCount,
    core::pmr::String& diagnostics,
    bool immutable = false)
{
    if (immutable) {
        std::error_code error;
        const bool exists = std::filesystem::exists(path, error);
        if (error) {
            diagnostics = text("TERRAIN-OUTPUT-STAT");
            return false;
        }
        if (exists) {
            const auto previous = core::readBinaryFile(path, byteCount);
            if (previous.success && previous.bytes.size() == byteCount
                && (byteCount == 0u || std::memcmp(previous.bytes.data(), bytes, byteCount) == 0)) {
                return true;
            }
            diagnostics = text("TERRAIN-IMMUTABLE-BLOCK-CONFLICT");
            return false;
        }
    }
    core::pmr::Vector<std::uint8_t> owned(&core::defaultCpuMemoryResource());
    if (byteCount)
        owned.assign(bytes, bytes + byteCount);
    const auto result = core::io::replaceFileAtomically(text(path.generic_string().c_str()), owned);
    diagnostics = result.diagnostic;
    return result.success;
}

bool writeTextFile(const std::filesystem::path& path,
    const core::pmr::String& contents,
    core::pmr::String& diagnostics)
{
    core::pmr::Vector<std::uint8_t> bytes(
        contents.begin(), contents.end(), &core::defaultCpuMemoryResource());
    const auto result = core::io::replaceFileAtomically(text(path.generic_string().c_str()), bytes);
    diagnostics = result.diagnostic;
    return result.success;
}

const char* formatName(TerrainAssetBinaryFormat format)
{
    switch (format) {
    case TerrainAssetBinaryFormat::R32SFloat:
        return "R32_SFLOAT";
    case TerrainAssetBinaryFormat::Rgba8Unorm:
        return "RGBA8_UNORM";
    case TerrainAssetBinaryFormat::StaticVegetationV1:
        return "STATIC_VEGETATION_V1";
    }
    return "INVALID";
}

bool formatFor(const core::pmr::String& value, TerrainAssetBinaryFormat& out)
{
    if (value == "R32_SFLOAT") {
        out = TerrainAssetBinaryFormat::R32SFloat;
        return true;
    }
    if (value == "RGBA8_UNORM") {
        out = TerrainAssetBinaryFormat::Rgba8Unorm;
        return true;
    }
    if (value == "STATIC_VEGETATION_V1") {
        out = TerrainAssetBinaryFormat::StaticVegetationV1;
        return true;
    }
    return false;
}

template <typename Layers>
bool validLayerBindings(const Layers& layers, core::pmr::String& diagnostics)
{
    bool occupied[2][4]{};
    for (std::uint32_t index = 0u; index < layers.size(); ++index) {
        const auto& layer = layers[index];
        const auto expectedPage = index < 4u ? 0u : 1u;
        const auto expectedChannel = index < 4u ? index : index - 4u;
        if (layer.layerId.empty() || layer.semantic.empty() || layer.materialResourceId.empty()
            || layer.page != expectedPage || layer.channel != expectedChannel
            || occupied[layer.page][layer.channel]) {
            diagnostics =
                text("TerrainAsset layer binding does not match the fixed v1 weight-page contract");
            return false;
        }
        occupied[layer.page][layer.channel] = true;
        for (std::uint32_t other = 0u; other < index; ++other) {
            if (layers[other].layerId == layer.layerId
                || layers[other].semantic == layer.semantic) {
                diagnostics = text("TerrainAsset layer IDs and semantics must be unique");
                return false;
            }
        }
    }
    return true;
}

bool validVegetationInstance(
    const TerrainStaticVegetationInstance& instance, const terrain::TerrainBounds& bounds)
{
    return instance.prototypeId != 0u && bounds.contains(instance.position.x, instance.position.z)
        && instance.hasValidTransform();
}

template <typename Tiles>
bool hasContinuousHeightBoundaries(const Tiles& tiles,
    std::uint32_t tileCountX,
    std::uint32_t tileCountZ,
    std::uint32_t samplesPerEdge)
{
    const auto findTile =
        [&](terrain::TerrainTileCoord coord) -> const typename Tiles::value_type* {
        const auto found = std::find_if(tiles.begin(), tiles.end(), [&](const auto& tile) {
            return tile.coord == coord;
        });
        return found == tiles.end() ? nullptr : &*found;
    };
    const auto index = [samplesPerEdge](std::uint32_t x, std::uint32_t z) {
        return static_cast<std::size_t>(z) * samplesPerEdge + x;
    };
    for (const auto& tile : tiles) {
        if (static_cast<std::uint32_t>(tile.coord.x) + 1u < tileCountX) {
            const auto* right = findTile({tile.coord.x + 1, tile.coord.z});
            if (right == nullptr)
                return false;
            for (std::uint32_t z = 0u; z < samplesPerEdge; ++z) {
                if (std::memcmp(&tile.heights[index(samplesPerEdge - 1u, z)],
                        &right->heights[index(0u, z)],
                        sizeof(float))
                    != 0)
                    return false;
            }
        }
        if (static_cast<std::uint32_t>(tile.coord.z) + 1u < tileCountZ) {
            const auto* upper = findTile({tile.coord.x, tile.coord.z + 1});
            if (upper == nullptr)
                return false;
            for (std::uint32_t x = 0u; x < samplesPerEdge; ++x) {
                if (std::memcmp(&tile.heights[index(x, samplesPerEdge - 1u)],
                        &upper->heights[index(x, 0u)],
                        sizeof(float))
                    != 0)
                    return false;
            }
        }
    }
    return true;
}

bool validBuildDesc(const TerrainAssetBuildDesc& desc, core::pmr::String& diagnostics)
{
    if (desc.resourceId.empty() || !desc.bounds.valid() || desc.tileCountX == 0u
        || desc.tileCountZ == 0u || desc.samplesPerEdge < 2u || !std::isfinite(desc.tileWorldSize)
        || desc.tileWorldSize <= 0.0f
        || desc.tiles.size() != static_cast<std::size_t>(desc.tileCountX) * desc.tileCountZ) {
        diagnostics =
            text("TerrainAsset build descriptor has invalid geometry or incomplete tile coverage");
        return false;
    }
    if (!isResourceUuid(desc.resourceId) || !std::isfinite(desc.bounds.minX)
        || !std::isfinite(desc.bounds.maxX) || !std::isfinite(desc.bounds.minZ)
        || !std::isfinite(desc.bounds.maxZ)) {
        diagnostics = text("TERRAIN-IDENTITY-GEOMETRY: invalid identity or nonfinite bounds");
        return false;
    }
    const bool v2 = desc.version == TerrainAssetVersion::V2;
    if ((desc.version != TerrainAssetVersion::V1 && desc.version != TerrainAssetVersion::V2)
        || (!v2 && (!desc.hasSurfaceWeights || !desc.hasVegetation))
        || (desc.hasVegetation == desc.vegetationCatalogResourceId.empty())) {
        diagnostics =
            text("TerrainAsset requires an explicit VegetationCatalogResource dependency");
        return false;
    }
    if (desc.hasSurfaceWeights && !validLayerBindings(desc.layers, diagnostics)) {
        return false;
    }
    if (v2
        && (desc.tileCountX > INT32_MAX || desc.tileCountZ > INT32_MAX
            || desc.bounds.maxX != desc.bounds.minX + desc.tileCountX * desc.tileWorldSize
            || desc.bounds.maxZ != desc.bounds.minZ + desc.tileCountZ * desc.tileWorldSize
            || static_cast<std::uint64_t>(desc.samplesPerEdge) * desc.samplesPerEdge
                > std::numeric_limits<std::size_t>::max() / (4u * sizeof(float)))) {
        diagnostics = text("TERRAIN-GEOMETRY-LAYOUT: bounds, coordinates or byte count invalid");
        return false;
    }
    if (!desc.hasSurfaceWeights) {
        for (const auto& layer : desc.layers) {
            if (!layer.layerId.empty() || !layer.semantic.empty()
                || !layer.materialResourceId.empty()) {
                diagnostics = text("TERRAIN-SURFACE-ABSENT: unexpected layer binding");
                return false;
            }
        }
    }
    const auto sampleCount = static_cast<std::size_t>(desc.samplesPerEdge) * desc.samplesPerEdge;
    core::pmr::Vector<std::uint8_t> coverage(&core::defaultCpuMemoryResource());
    coverage.resize(static_cast<std::size_t>(desc.tileCountX) * desc.tileCountZ, 0u);
    for (const auto& tile : desc.tiles) {
        if (tile.coord.x < 0 || tile.coord.z < 0
            || static_cast<std::uint32_t>(tile.coord.x) >= desc.tileCountX
            || static_cast<std::uint32_t>(tile.coord.z) >= desc.tileCountZ
            || tile.heights.size() != sampleCount
            || tile.weightPageA.size() != (desc.hasSurfaceWeights ? sampleCount * 4u : 0u)
            || tile.weightPageB.size() != (desc.hasSurfaceWeights ? sampleCount * 4u : 0u)
            || (!desc.hasVegetation && !tile.vegetation.empty())) {
            diagnostics = text("TerrainAsset tile data does not match declared sampling contract");
            return false;
        }
        const auto coverageIndex = static_cast<std::size_t>(tile.coord.z) * desc.tileCountX
            + static_cast<std::uint32_t>(tile.coord.x);
        if (coverage[coverageIndex] != 0u) {
            diagnostics = text("TerrainAsset build descriptor has duplicate tile coordinates");
            return false;
        }
        coverage[coverageIndex] = 1u;
        for (const float height : tile.heights) {
            if (!std::isfinite(height)) {
                diagnostics = text("TerrainAsset height block contains a non-finite sample");
                return false;
            }
        }
        for (const auto& instance : tile.vegetation) {
            if (!validVegetationInstance(instance, desc.bounds)) {
                diagnostics = text("TerrainAsset vegetation instance is invalid");
                return false;
            }
        }
    }
    if (!hasContinuousHeightBoundaries(
            desc.tiles, desc.tileCountX, desc.tileCountZ, desc.samplesPerEdge)) {
        diagnostics = text("TerrainAsset adjacent tile height boundaries must be bit-identical");
        return false;
    }
    return true;
}

} // namespace

TerrainAssetIoResult TerrainAssetWriter::validate(const TerrainAssetBuildDesc& desc) const
{
    TerrainAssetIoResult result;
    result.success = validBuildDesc(desc, result.diagnostics);
    return result;
}

TerrainAssetIoResult TerrainAssetWriter::write(
    const TerrainAssetBuildDesc& desc, const std::filesystem::path& outputDirectory) const
{
    auto result = validate(desc);
    if (!result.success) {
        return result;
    }
    result.success = false;
    std::error_code error;
    std::filesystem::create_directories(outputDirectory / "tiles", error);
    if (error) {
        result.diagnostics = text("TerrainAsset failed to create output directory: ");
        result.diagnostics += error.message().c_str();
        return result;
    }

    YamlNode root = YamlNode::mapping();
    root["type"] = "TerrainAssetResource";
    root["contractVersion"] =
        desc.version == TerrainAssetVersion::V2 ? "terrain.asset/v2" : "terrain.asset/v1";
    if (desc.version == TerrainAssetVersion::V2) {
        root["coordinateSystem"] = "local-y-up-metres/v1";
        root["heightEncoding"] = "ieee754-f32-le/v1";
        root["surfaceLayout"] = desc.hasSurfaceWeights ? "base6-overlays2/v1" : "none";
        root["vegetationLayout"] = desc.hasVegetation ? "static-instances/v1" : "none";
    }
    root["resourceId"] = desc.resourceId.c_str();
    root["status"] = desc.status.c_str();
    root["bounds"] = YamlNode::mapping();
    root["bounds"]["minX"] = desc.bounds.minX;
    root["bounds"]["maxX"] = desc.bounds.maxX;
    root["bounds"]["minZ"] = desc.bounds.minZ;
    root["bounds"]["maxZ"] = desc.bounds.maxZ;
    root["tileCountX"] = desc.tileCountX;
    root["tileCountZ"] = desc.tileCountZ;
    root["samplesPerEdge"] = desc.samplesPerEdge;
    root["tileWorldSize"] = desc.tileWorldSize;
    root["vegetationCatalogResourceId"] =
        desc.hasVegetation ? desc.vegetationCatalogResourceId.c_str() : "";
    root["layers"] = YamlNode::sequence();
    if (desc.hasSurfaceWeights)
        for (const auto& layer : desc.layers) {
            auto node = YamlNode::mapping();
            node["layerId"] = layer.layerId.c_str();
            node["semantic"] = layer.semantic.c_str();
            node["materialResourceId"] = layer.materialResourceId.c_str();
            node["page"] = layer.page;
            node["channel"] = layer.channel;
            root["layers"].get_value_ref<YamlNode::sequence_type&>().push_back(std::move(node));
        }
    root["tiles"] = YamlNode::sequence();
    core::pmr::Vector<const TerrainAssetBuildTile*> ordered{&core::defaultCpuMemoryResource()};
    ordered.reserve(desc.tiles.size());
    for (const auto& tile : desc.tiles)
        ordered.push_back(&tile);
    std::sort(ordered.begin(), ordered.end(), [](const auto* a, const auto* b) {
        return a->coord.z < b->coord.z || (a->coord.z == b->coord.z && a->coord.x < b->coord.x);
    });
    for (const auto* sourceTile : ordered) {
        const auto& tile = *sourceTile;
        auto tileNode = YamlNode::mapping();
        const auto stem = core::pmr::String("tiles/", &core::defaultCpuMemoryResource())
            + std::to_string(tile.coord.x).c_str() + "_" + std::to_string(tile.coord.z).c_str();
        core::pmr::Vector<std::uint8_t> heightBytes(&core::defaultCpuMemoryResource());
        heightBytes.reserve(tile.heights.size() * sizeof(float));
        for (const float height : tile.heights)
            appendF32Le(heightBytes, height);
        core::pmr::Vector<std::uint8_t> vegetationBytes(&core::defaultCpuMemoryResource());
        vegetationBytes.reserve(tile.vegetation.size() * 48u);
        for (const auto& instance : tile.vegetation) {
            appendU32Le(vegetationBytes, instance.prototypeId);
            appendF32Le(vegetationBytes, instance.position.x);
            appendF32Le(vegetationBytes, instance.position.y);
            appendF32Le(vegetationBytes, instance.position.z);
            for (const float component : instance.rotation)
                appendF32Le(vegetationBytes, component);
            appendF32Le(vegetationBytes, instance.scale.x);
            appendF32Le(vegetationBytes, instance.scale.y);
            appendF32Le(vegetationBytes, instance.scale.z);
            appendU32Le(vegetationBytes, instance.variationSeed);
        }
        const auto blockPath = [&](const char* suffix, const auto& bytes) {
            auto path = stem;
            if (desc.version == TerrainAssetVersion::V2) {
                path += ".";
                path += core::formatUnsigned(hashBytes(bytes.data(), bytes.size()));
            }
            path += suffix;
            return path;
        };
        const auto heightPath = blockPath(".height.bin", heightBytes);
        const auto pageAPath = blockPath(".weight-a.bin", tile.weightPageA);
        const auto pageBPath = blockPath(".weight-b.bin", tile.weightPageB);
        const auto vegetationPath = blockPath(".vegetation.bin", vegetationBytes);
        if (!writeFile(outputDirectory / std::filesystem::path(heightPath.c_str()),
                heightBytes.data(),
                heightBytes.size(),
                result.diagnostics,
                desc.version == TerrainAssetVersion::V2)
            || (desc.hasSurfaceWeights
                && (!writeFile(outputDirectory / std::filesystem::path(pageAPath.c_str()),
                        tile.weightPageA.data(),
                        tile.weightPageA.size(),
                        result.diagnostics,
                        desc.version == TerrainAssetVersion::V2)
                    || !writeFile(outputDirectory / std::filesystem::path(pageBPath.c_str()),
                        tile.weightPageB.data(),
                        tile.weightPageB.size(),
                        result.diagnostics,
                        desc.version == TerrainAssetVersion::V2)))
            || (desc.hasVegetation
                && !writeFile(outputDirectory / std::filesystem::path(vegetationPath.c_str()),
                    vegetationBytes.data(),
                    vegetationBytes.size(),
                    result.diagnostics,
                    desc.version == TerrainAssetVersion::V2))) {
            return result;
        }
        auto block = [&](const char* name,
                         const core::pmr::String& path,
                         TerrainAssetBinaryFormat format,
                         const std::uint8_t* bytes,
                         std::size_t size) {
            auto node = YamlNode::mapping();
            node["path"] = path.c_str();
            node["byteCount"] = static_cast<std::int64_t>(size);
            node["contentHash"] = core::formatUnsigned(hashBytes(bytes, size)).c_str();
            node["format"] = formatName(format);
            tileNode[name] = std::move(node);
        };
        tileNode["x"] = tile.coord.x;
        tileNode["z"] = tile.coord.z;
        if (desc.version == TerrainAssetVersion::V2) {
            tileNode["minimumHeight"] = *std::min_element(tile.heights.begin(), tile.heights.end());
            tileNode["maximumHeight"] = *std::max_element(tile.heights.begin(), tile.heights.end());
        }
        block("height",
            heightPath,
            TerrainAssetBinaryFormat::R32SFloat,
            heightBytes.data(),
            heightBytes.size());
        if (desc.hasSurfaceWeights) {
            block("weightPageA",
                pageAPath,
                TerrainAssetBinaryFormat::Rgba8Unorm,
                tile.weightPageA.data(),
                tile.weightPageA.size());
            block("weightPageB",
                pageBPath,
                TerrainAssetBinaryFormat::Rgba8Unorm,
                tile.weightPageB.data(),
                tile.weightPageB.size());
        }
        if (desc.hasVegetation)
            block("vegetation",
                vegetationPath,
                TerrainAssetBinaryFormat::StaticVegetationV1,
                vegetationBytes.data(),
                vegetationBytes.size());
        root["tiles"].get_value_ref<YamlNode::sequence_type&>().push_back(std::move(tileNode));
    }
    const auto manifest = core::YamlContract::serialize(root);
    if (manifest.size() > 1024u * 1024u)
        return failure("TERRAIN-MANIFEST-BUDGET");
    if (!writeTextFile(outputDirectory / "terrain.asset.yaml", manifest, result.diagnostics)) {
        return result;
    }
    result.success = true;
    return result;
}

TerrainAssetResource::TerrainAssetResource(
    ResourceReference reference, std::filesystem::path manifestPath)
    : TerrainAssetResource(std::move(reference), std::move(manifestPath), {})
{}

TerrainAssetResource::TerrainAssetResource(
    ResourceReference reference, std::filesystem::path manifestPath, ResourceFileOrigin origin)
    : ResourceBase(std::move(reference)), manifestPath_(std::move(manifestPath)),
      sourceOrigin_(origin)
{}

TerrainAssetResource::TerrainAssetResource(const TerrainAssetResource& other)
    : ResourceBase(other), manifestPath_(other.manifestPath_), sourceOrigin_(other.sourceOrigin_),
      bounds_(other.bounds_), tileCountX_(other.tileCountX_), tileCountZ_(other.tileCountZ_),
      samplesPerEdge_(other.samplesPerEdge_), tileWorldSize_(other.tileWorldSize_),
      hasSurfaceWeights_(other.hasSurfaceWeights_), hasVegetation_(other.hasVegetation_),
      generation_(other.generation_), heightRange_(other.heightRange_),
      vegetationCatalogResourceId_(other.vegetationCatalogResourceId_), layers_(other.layers_),
      tiles_(other.tiles_)
{}

TerrainAssetManifestIndex TerrainAssetResource::inspectManifest(
    const ResourceLoadContext& context) const
{
    TerrainAssetManifestIndex result;
    constexpr std::size_t manifestBudget = 1024u * 1024u;
    const auto manifestPath = core::FilePath::utf8(manifestPath_);
    const auto file = readResourceText(manifestPath, context, manifestBudget, &sourceOrigin_);
    if (!file.success) {
        result.diagnostics = file.diagnostics;
        return result;
    }
    try {
        using Yaml = core::YamlContract;
        const auto root = Yaml::parseSingleDocument(file.text);
        const auto version = Yaml::text(root, "contractVersion");
        if (Yaml::text(root, "type") != resourceTypeName()
            || Yaml::text(root, "resourceId") != reference().id
            || (version != "terrain.asset/v1" && version != "terrain.asset/v2")) {
            result.diagnostics =
                "TERRAIN-INDEX-IDENTITY: resourceId, type or contractVersion mismatch";
            return result;
        }
        result.version =
            version == "terrain.asset/v2" ? TerrainAssetVersion::V2 : TerrainAssetVersion::V1;
        bool weights = true, vegetation = true;
        if (result.version == TerrainAssetVersion::V2) {
            Yaml::onlyKeys(root,
                {"type",
                    "contractVersion",
                    "status",
                    "resourceId",
                    "bounds",
                    "tileCountX",
                    "tileCountZ",
                    "samplesPerEdge",
                    "tileWorldSize",
                    "vegetationCatalogResourceId",
                    "layers",
                    "tiles",
                    "coordinateSystem",
                    "heightEncoding",
                    "surfaceLayout",
                    "vegetationLayout"});
            const auto surface = Yaml::text(root, "surfaceLayout");
            const auto instances = Yaml::text(root, "vegetationLayout");
            if (Yaml::text(root, "coordinateSystem") != "local-y-up-metres/v1"
                || Yaml::text(root, "heightEncoding") != "ieee754-f32-le/v1"
                || (surface != "none" && surface != "base6-overlays2/v1")
                || (instances != "none" && instances != "static-instances/v1")) {
                result.diagnostics = "TERRAIN-INDEX-LAYOUT";
                return result;
            }
            weights = surface != "none";
            vegetation = instances != "none";
        }
        const auto catalog = Yaml::text(root, "vegetationCatalogResourceId");
        if (vegetation == catalog.empty()) {
            result.diagnostics = "TERRAIN-INDEX-CATALOG";
            return result;
        }
        const auto addDependency = [&](const core::pmr::String& id, const char* usage) {
            if (!isResourceUuid(id))
                throw core::YamlContractError{"TERRAIN-INDEX-DEPENDENCY-ID"};
            for (const auto& dependency : result.dependencies)
                if (dependency.id == id && dependency.usage == usage)
                    return;
            result.dependencies.push_back({id, text(usage)});
        };
        if (vegetation)
            addDependency(catalog, "terrain/vegetation-catalog");
        const auto& layers = Yaml::sequence(root, "layers");
        if (layers.size() != (weights ? terrain::kTerrainBaseLayerCount + 2u : 0u)) {
            result.diagnostics = "TERRAIN-INDEX-LAYERS";
            return result;
        }
        for (const auto& layer : layers)
            addDependency(Yaml::text(layer, "materialResourceId"), "terrain/material-layer");
        if (!appendResourceReadIntent(
                manifestPath, manifestBudget, result.files, result.diagnostics, sourceOrigin_))
            return result;
        const auto countX = Yaml::uint32(root, "tileCountX"),
                   countZ = Yaml::uint32(root, "tileCountZ");
        const auto samples = Yaml::uint32(root, "samplesPerEdge");
        const auto& tiles = Yaml::sequence(root, "tiles");
        const auto sampleCount = static_cast<std::uint64_t>(samples) * samples;
        if (!countX || !countZ || countX > INT32_MAX || countZ > INT32_MAX || samples < 2u
            || sampleCount > std::numeric_limits<std::size_t>::max() / (4u * sizeof(float))
            || static_cast<std::uint64_t>(countX) * countZ != tiles.size()) {
            result.diagnostics = "TERRAIN-INDEX-GEOMETRY";
            return result;
        }
        const auto sampleBytes = sampleCount * sizeof(float);
        std::size_t decoded = 0u;
        for (const auto& tile : tiles) {
            if (result.version == TerrainAssetVersion::V2) {
                Yaml::onlyKeys(tile,
                    {"x",
                        "z",
                        "minimumHeight",
                        "maximumHeight",
                        "height",
                        "weightPageA",
                        "weightPageB",
                        "vegetation"});
                const auto minimum = Yaml::number(tile, "minimumHeight"),
                           maximum = Yaml::number(tile, "maximumHeight");
                if (minimum > maximum)
                    throw core::YamlContractError{"TERRAIN-HEIGHT-RANGE"};
            }
            if (Yaml::uint32(tile, "x") >= countX || Yaml::uint32(tile, "z") >= countZ) {
                result.diagnostics = "TERRAIN-INDEX-COORDINATE";
                return result;
            }
            for (const auto* key : {"height", "weightPageA", "weightPageB", "vegetation"}) {
                const bool present = key == core::StringView("height")
                    || (key == core::StringView("vegetation") ? vegetation : weights);
                if (!present) {
                    if (valueFor(tile, key))
                        throw core::YamlContractError{"TERRAIN-PRESENCE"};
                    continue;
                }
                const auto& block = Yaml::required(tile, key);
                if (result.version == TerrainAssetVersion::V2)
                    Yaml::onlyKeys(block, {"path", "byteCount", "contentHash", "format"});
                const auto path = Yaml::text(block, "path");
                const auto bytes = Yaml::uint64(Yaml::required(block, "byteCount"));
                const bool instances = key == core::StringView("vegetation");
                const auto format = Yaml::text(block, "format");
                std::uint64_t hash = 0u;
                if (!safeRelativePath(path) || !u64For(block, "contentHash", hash)
                    || format
                        != (instances ? "STATIC_VEGETATION_V1"
                                      : (key == core::StringView("height") ? "R32_SFLOAT"
                                                                           : "RGBA8_UNORM"))
                    || (!instances && bytes != sampleBytes) || (instances && bytes % 48u)
                    || bytes > std::numeric_limits<std::size_t>::max() - decoded
                    || (context.maxDecodedBytes
                        && bytes > context.maxDecodedBytes
                                - std::min(decoded, context.maxDecodedBytes))) {
                    result.diagnostics = "TERRAIN-INDEX-BLOCK-BUDGET-OR-LAYOUT";
                    return result;
                }
                decoded += static_cast<std::size_t>(bytes);
                const auto fullPath = core::FilePath::utf8(manifestPath_.parent_path()
                    / std::filesystem::u8path(path.begin(), path.end()));
                if (!context.requirePreloadedSources
                    && sourceOrigin_.pathScope == core::io::FilePathScope::HostFilesystem) {
                    std::error_code pathError;
                    const auto rootPath = std::filesystem::weakly_canonical(
                        manifestPath_.has_parent_path() ? manifestPath_.parent_path()
                                                        : std::filesystem::path("."),
                        pathError);
                    if (pathError) {
                        result.diagnostics = "TERRAIN-BLOCK-PATH";
                        return result;
                    }
                    const auto blockPath = std::filesystem::weakly_canonical(
                        std::filesystem::u8path(fullPath.begin(), fullPath.end()), pathError);
                    if (pathError || !core::FilePath::contains(rootPath, blockPath)) {
                        result.diagnostics = "TERRAIN-BLOCK-PATH-ESCAPE";
                        return result;
                    }
                }
                // Repeated immutable blocks need one acquisition, but remain in decoded budget.
                if (std::none_of(result.files.begin(), result.files.end(), [&](const auto& intent) {
                        return intent.path == fullPath;
                    })) {
                    if (!appendResourceReadIntent(fullPath,
                            std::max<std::size_t>(1u, bytes),
                            result.files,
                            result.diagnostics,
                            sourceOrigin_))
                        return result;
                }
            }
        }
        result.success = true;
    } catch (...) {
        result.diagnostics = "TERRAIN-INDEX-INVALID-MANIFEST";
    }
    return result;
}

bool TerrainAssetResource::collectFileReadIntents(ResourceManager&,
    core::pmr::Vector<ResourceFileReadIntent>& intents,
    core::pmr::String& diagnostics) const
{
    // Explicit synchronous inspection remains available to existing cold callers.
    // ResourceGraph uses planFileReadIntents and never enters this host IO path.
    auto index = inspectManifest();
    diagnostics = std::move(index.diagnostics);
    if (!index.success)
        return false;
    intents.insert(intents.end(),
        std::make_move_iterator(index.files.begin()),
        std::make_move_iterator(index.files.end()));
    return true;
}

bool TerrainAssetResource::planFileReadIntents(ResourceManager&,
    const ResourceLoadContext& context,
    core::pmr::Vector<ResourceFileReadIntent>& intents,
    core::pmr::String& diagnostics) const
{
    if (!context.sources || !context.sources->find(core::FilePath::utf8(manifestPath_)))
        return appendResourceReadIntent(core::FilePath::utf8(manifestPath_),
            1024u * 1024u,
            intents,
            diagnostics,
            sourceOrigin_);
    // Parse exactly the acquired manifest, never a second filesystem version.
    auto index = inspectManifest(context);
    diagnostics = std::move(index.diagnostics);
    if (!index.success)
        return false;
    intents = std::move(index.files);
    return true;
}

TerrainAssetIoResult TerrainAssetWriter::migrateV1(ResourceManager& dependencies,
    ResourceReference reference,
    const std::filesystem::path& sourceManifest,
    const std::filesystem::path& outputDirectory) const
{
    TerrainAssetResource source(reference, sourceManifest);
    const auto index = source.inspectManifest();
    if (!index.success || index.version != TerrainAssetVersion::V1)
        return failure("TERRAIN-MIGRATE-REQUIRES-V1");
    const auto loaded = source.decode(dependencies, {});
    if (!loaded.success)
        return {false, loaded.diagnostics};
    TerrainAssetBuildDesc desc;
    desc.version = TerrainAssetVersion::V2;
    desc.resourceId = reference.id;
    desc.alias = reference.alias;
    desc.bounds = source.bounds();
    desc.tileCountX = source.tileCountX();
    desc.tileCountZ = source.tileCountZ();
    desc.samplesPerEdge = source.samplesPerEdge();
    desc.tileWorldSize = source.tileWorldSize();
    desc.vegetationCatalogResourceId = source.vegetationCatalogResourceId();
    std::copy(source.layers().begin(), source.layers().end(), desc.layers.begin());
    for (std::uint32_t z = 0; z < desc.tileCountZ; ++z)
        for (std::uint32_t x = 0; x < desc.tileCountX; ++x) {
            TerrainAssetTileReadView view;
            if (!source.tileReadView(
                    {static_cast<std::int32_t>(x), static_cast<std::int32_t>(z)}, view)
                || !view.valid())
                return failure("TERRAIN-MIGRATE-TILE");
            TerrainAssetBuildTile tile;
            tile.coord = view.coord;
            tile.heights.assign(view.heights, view.heights + view.heightCount);
            tile.weightPageA.assign(view.weightPageA, view.weightPageA + view.weightPageAByteCount);
            tile.weightPageB.assign(view.weightPageB, view.weightPageB + view.weightPageBByteCount);
            tile.vegetation = *source.vegetationFor(view.coord);
            desc.tiles.push_back(std::move(tile));
        }
    // v2 requires Ready materials. Reject before publishing any output.
    for (const auto& dependency : index.dependencies) {
        const auto* record = dependencies.recordSnapshot(dependency.id);
        if (!record || record->state != ResourceState::Ready)
            return failure("TERRAIN-MIGRATE-DEPENDENCY-NOT-READY");
    }
    core::io::StagedDirectory transaction;
    if (!transaction.begin(outputDirectory))
        return failure(transaction.diagnostic().c_str());
    const auto written = write(desc, transaction.path());
    if (!written.success)
        return written;
    TerrainAssetResource target(reference, transaction.path() / "terrain.asset.yaml");
    const auto verified = target.decode(dependencies, {});
    if (!verified.success || target.generation() != source.generation())
        return failure("TERRAIN-MIGRATE-VERIFICATION-FAILED");
    auto report = YamlNode::mapping();
    report["type"] = "TerrainAssetMigrationReport";
    report["contractVersion"] = "terrain.migration-report/v1";
    report["sourceContract"] = "terrain.asset/v1";
    report["targetContract"] = "terrain.asset/v2";
    report["resourceId"] = reference.id.c_str();
    report["sourceGeneration"] = core::formatUnsigned(source.generation()).c_str();
    report["targetGeneration"] = core::formatUnsigned(target.generation()).c_str();
    report["contentVerified"] = true;
    report["tileCount"] = static_cast<std::int64_t>(desc.tiles.size());
    report["dependencies"] = YamlNode::sequence();
    for (const auto& dependency : index.dependencies) {
        auto node = YamlNode::mapping();
        node["id"] = dependency.id.c_str();
        node["usage"] = dependency.usage.c_str();
        report["dependencies"].get_value_ref<YamlNode::sequence_type&>().push_back(std::move(node));
    }
    TerrainAssetIoResult result;
    if (!writeTextFile(transaction.path() / "terrain.migration.yaml",
            core::YamlContract::serialize(report),
            result.diagnostics))
        return result;
    if (!transaction.publish())
        return failure(transaction.diagnostic().c_str());
    result.success = true;
    result.diagnostics = "TERRAIN-MIGRATE-V1-V2: content verified; report terrain.migration.yaml";
    return result;
}

ResourceLoadResult TerrainAssetResource::load(
    ResourceManager& manager, const ResourceLoadContext& context)
{
    const auto decoded = decode(manager, context);
    return decoded.success ? ResourceBase::load(manager, context) : decoded;
}

ResourceLoadResult TerrainAssetResource::decode(
    ResourceManager& manager, const ResourceLoadContext& context)
{
    const auto index = inspectManifest(context);
    if (!index.success)
        return {false, ResourceState::Failed, reference().id, reference().type, index.diagnostics};
    for (const auto& dependency : index.dependencies)
        addDependency(dependency);
    {
        std::lock_guard lock(snapshotMutex_);
        surfaceSnapshot_.reset();
    }
    constexpr std::size_t manifestBudget = 1024u * 1024u;
    const auto manifestFile = readResourceText(
        core::FilePath::utf8(manifestPath_), context, manifestBudget, &sourceOrigin_);
    if (!manifestFile.success) {
        return {false,
            ResourceState::Failed,
            reference().id,
            reference().type,
            manifestFile.diagnostics};
    }
    YamlNode root;
    try {
        root = core::YamlContract::parseSingleDocument(manifestFile.text);
    } catch (const std::exception& error) {
        auto diagnostics = text("TerrainAsset manifest YAML parse failed: ");
        diagnostics += error.what();
        return {false, ResourceState::Failed, reference().id, reference().type, diagnostics};
    } catch (...) {
        return resourceLoadFailure(*this, "TERRAIN-MANIFEST-YAML");
    }
    core::pmr::String type(&core::defaultCpuMemoryResource());
    core::pmr::String version(&core::defaultCpuMemoryResource());
    core::pmr::String resourceId(&core::defaultCpuMemoryResource());
    core::pmr::String status(&core::defaultCpuMemoryResource());
    if (!stringFor(root, "type", type) || !stringFor(root, "contractVersion", version)
        || !stringFor(root, "resourceId", resourceId) || !stringFor(root, "status", status)) {
        return {false,
            ResourceState::Failed,
            reference().id,
            reference().type,
            text("TerrainAsset manifest has incomplete identity metadata")};
    }
    const bool v2 = version == "terrain.asset/v2";
    const auto contract = validateResourceContract(
        type, version, v2 ? kTerrainAssetResourceV2Contract : kTerrainAssetResourceContract);
    if (!contract.success || resourceId != reference().id) {
        auto diagnostics = contract.success
            ? text("TerrainAsset manifest resourceId does not match resource reference")
            : contract.diagnostics;
        return {false, ResourceState::Failed, reference().id, reference().type, diagnostics};
    }
    const auto statusResult = validateResourceStatus(status,
        {ResourceStatus::ContractComplete,
            ResourceStatus::BackendValidated,
            ResourceStatus::PlatformValidated,
            ResourceStatus::ProductionReady},
        "TerrainAsset");
    if (!statusResult.success) {
        return {false,
            ResourceState::Failed,
            reference().id,
            reference().type,
            statusResult.diagnostics};
    }
    hasSurfaceWeights_ = true;
    hasVegetation_ = true;
    if (v2) {
        core::pmr::String coordinates{&core::defaultCpuMemoryResource()};
        core::pmr::String encoding{&core::defaultCpuMemoryResource()};
        core::pmr::String surface{&core::defaultCpuMemoryResource()};
        core::pmr::String vegetation{&core::defaultCpuMemoryResource()};
        if (!stringFor(root, "coordinateSystem", coordinates)
            || coordinates != "local-y-up-metres/v1" || !stringFor(root, "heightEncoding", encoding)
            || encoding != "ieee754-f32-le/v1" || !stringFor(root, "surfaceLayout", surface)
            || (surface != "none" && surface != "base6-overlays2/v1")
            || !stringFor(root, "vegetationLayout", vegetation)
            || (vegetation != "none" && vegetation != "static-instances/v1")) {
            return resourceLoadFailure(*this, "TERRAIN-LAYOUT: unknown or missing v2 layout");
        }
        hasSurfaceWeights_ = surface != "none";
        hasVegetation_ = vegetation != "none";
    }
    const auto* boundsNode = valueFor(root, "bounds");
    if (boundsNode == nullptr || !floatFor(*boundsNode, "minX", bounds_.minX)
        || !floatFor(*boundsNode, "maxX", bounds_.maxX)
        || !floatFor(*boundsNode, "minZ", bounds_.minZ)
        || !floatFor(*boundsNode, "maxZ", bounds_.maxZ)
        || !integerFor(root, "tileCountX", tileCountX_)
        || !integerFor(root, "tileCountZ", tileCountZ_)
        || !integerFor(root, "samplesPerEdge", samplesPerEdge_)
        || !floatFor(root, "tileWorldSize", tileWorldSize_)
        || !stringFor(root, "vegetationCatalogResourceId", vegetationCatalogResourceId_)
        || !bounds_.valid() || tileCountX_ == 0u || tileCountZ_ == 0u || samplesPerEdge_ < 2u
        || tileWorldSize_ <= 0.0f || (hasVegetation_ == vegetationCatalogResourceId_.empty())) {
        return {false,
            ResourceState::Failed,
            reference().id,
            reference().type,
            text("TerrainAsset manifest geometry or vegetation dependency is invalid")};
    }
    const auto samples64 = static_cast<std::uint64_t>(samplesPerEdge_) * samplesPerEdge_;
    if (tileCountX_ > INT32_MAX || tileCountZ_ > INT32_MAX
        || samples64 > std::numeric_limits<std::size_t>::max() / (4u * sizeof(float))
        || (v2
            && (bounds_.maxX != bounds_.minX + tileCountX_ * tileWorldSize_
                || bounds_.maxZ != bounds_.minZ + tileCountZ_ * tileWorldSize_))) {
        return resourceLoadFailure(*this, "TERRAIN-GEOMETRY-LAYOUT: bounds or byte count invalid");
    }
    const auto* layerNodes = valueFor(root, "layers");
    if (layerNodes == nullptr || !layerNodes->is_sequence()
        || layerNodes->get_value_ref<const YamlNode::sequence_type&>().size()
            != (hasSurfaceWeights_ ? terrain::kTerrainBaseLayerCount + 2u : 0u)) {
        return {false,
            ResourceState::Failed,
            reference().id,
            reference().type,
            text("TerrainAsset requires exactly six base layers and two overlays")};
    }
    layers_.clear();
    for (const auto& layerNode : layerNodes->get_value_ref<const YamlNode::sequence_type&>()) {
        TerrainAssetLayerDesc layer;
        if (!stringFor(layerNode, "layerId", layer.layerId)
            || !stringFor(layerNode, "semantic", layer.semantic)
            || !stringFor(layerNode, "materialResourceId", layer.materialResourceId)
            || !integerFor(layerNode, "page", layer.page)
            || !integerFor(layerNode, "channel", layer.channel) || layer.layerId.empty()
            || layer.semantic.empty() || layer.materialResourceId.empty() || layer.page > 1u
            || layer.channel > 3u) {
            return {false,
                ResourceState::Failed,
                reference().id,
                reference().type,
                text("TerrainAsset layer binding is invalid")};
        }
        layers_.push_back(std::move(layer));
    }
    core::pmr::String layerDiagnostics(&core::defaultCpuMemoryResource());
    if (!validLayerBindings(layers_, layerDiagnostics)) {
        return {false, ResourceState::Failed, reference().id, reference().type, layerDiagnostics};
    }

    const auto catalog = manager.find<VegetationCatalogResource>(vegetationCatalogResourceId_);
    if (hasVegetation_ && (!catalog.ready() || catalog.get() == nullptr || !catalog.get()->data)) {
        return {false,
            ResourceState::Failed,
            reference().id,
            reference().type,
            text(
                "TerrainAsset vegetation catalog dependency must be loaded before the terrain asset")};
    }
    for (const auto& layer : layers_) {
        const auto* material = manager.recordSnapshot(layer.materialResourceId);
        if (material == nullptr || material->type != "MaterialResource"
            || material->state == ResourceState::Failed
            || material->state == ResourceState::Unloaded
            || (v2 && material->state != ResourceState::Ready)) {
            return {false,
                ResourceState::Failed,
                reference().id,
                reference().type,
                text(
                    "TerrainAsset layer material dependency is missing or has the wrong resource type")};
        }
    }
    const auto* tileNodes = valueFor(root, "tiles");
    const auto expectedTileCount = static_cast<std::size_t>(tileCountX_) * tileCountZ_;
    if (tileNodes == nullptr || !tileNodes->is_sequence()
        || tileNodes->get_value_ref<const YamlNode::sequence_type&>().size() != expectedTileCount) {
        return {false,
            ResourceState::Failed,
            reference().id,
            reference().type,
            text("TerrainAsset manifest has incomplete tile coverage")};
    }
    tiles_ = core::makeShared<LoadedTiles>();
    std::size_t decodedBytes = 0u;
    tiles_->reserve(expectedTileCount);
    heightRange_.minimum = std::numeric_limits<float>::infinity();
    heightRange_.maximum = -std::numeric_limits<float>::infinity();
    const auto rootDirectory = manifestPath_.parent_path();
    for (const auto& tileNode : tileNodes->get_value_ref<const YamlNode::sequence_type&>()) {
        LoadedTile tile;
        std::uint32_t x = 0u, z = 0u;
        if (!integerFor(tileNode, "x", x) || !integerFor(tileNode, "z", z) || x >= tileCountX_
            || z >= tileCountZ_) {
            return {false,
                ResourceState::Failed,
                reference().id,
                reference().type,
                text("TerrainAsset tile coordinate is invalid")};
        }
        tile.coord = {static_cast<std::int32_t>(x), static_cast<std::int32_t>(z)};
        if ((!hasSurfaceWeights_
                && (valueFor(tileNode, "weightPageA") || valueFor(tileNode, "weightPageB")))
            || (!hasVegetation_ && valueFor(tileNode, "vegetation"))) {
            return resourceLoadFailure(
                *this, "TERRAIN-PRESENCE: block contradicts declared layout");
        }
        for (const auto& existing : *tiles_) {
            if (existing.coord == tile.coord) {
                return {false,
                    ResourceState::Failed,
                    reference().id,
                    reference().type,
                    text("TerrainAsset manifest has duplicate tile coordinates")};
            }
        }
        auto readBlock = [&](const char* key,
                             TerrainAssetBinaryFormat expected,
                             core::pmr::Vector<std::uint8_t>& bytes) -> bool {
            const auto* block = valueFor(tileNode, key);
            TerrainAssetBlockDesc desc;
            core::pmr::String format(&core::defaultCpuMemoryResource());
            if (block == nullptr || !stringFor(*block, "path", desc.path)
                || !integerFor(*block, "byteCount", desc.byteCount)
                || !u64For(*block, "contentHash", desc.contentHash)
                || !stringFor(*block, "format", format) || !safeRelativePath(desc.path)
                || !formatFor(format, desc.format) || desc.format != expected) {
                return false;
            }
            const auto sampleBytes = static_cast<std::size_t>(samples64) * sizeof(float);
            const auto byteLimit = expected == TerrainAssetBinaryFormat::StaticVegetationV1
                ? (context.maxDecodedBytes ? context.maxDecodedBytes
                                           : std::numeric_limits<std::size_t>::max())
                : sampleBytes;
            if (desc.byteCount > byteLimit
                || (expected != TerrainAssetBinaryFormat::StaticVegetationV1
                    && desc.byteCount != sampleBytes)
                || (context.maxDecodedBytes
                    && (decodedBytes > context.maxDecodedBytes
                        || desc.byteCount > context.maxDecodedBytes - decodedBytes))) {
                return false;
            }
            const auto path =
                rootDirectory / std::filesystem::u8path(desc.path.begin(), desc.path.end());
            const auto file = readResourceSource(core::FilePath::utf8(path),
                context,
                byteLimit,
                expected == TerrainAssetBinaryFormat::StaticVegetationV1,
                &sourceOrigin_);
            if (!file.success || file.bytes.size() != desc.byteCount
                || hashBytes(file.bytes.data(), file.bytes.size()) != desc.contentHash) {
                return false;
            }
            bytes = file.bytes;
            decodedBytes += bytes.size();
            return true;
        };
        core::pmr::Vector<std::uint8_t> heightBytes(&core::defaultCpuMemoryResource());
        core::pmr::Vector<std::uint8_t> vegetationBytes(&core::defaultCpuMemoryResource());
        if (!readBlock("height", TerrainAssetBinaryFormat::R32SFloat, heightBytes)
            || (hasSurfaceWeights_
                && (!readBlock(
                        "weightPageA", TerrainAssetBinaryFormat::Rgba8Unorm, tile.weightPageA)
                    || !readBlock(
                        "weightPageB", TerrainAssetBinaryFormat::Rgba8Unorm, tile.weightPageB)))
            || (hasVegetation_
                && !readBlock(
                    "vegetation", TerrainAssetBinaryFormat::StaticVegetationV1, vegetationBytes))) {
            return {false,
                ResourceState::Failed,
                reference().id,
                reference().type,
                text("TerrainAsset block is missing, corrupt, or has an undeclared format")};
        }
        const auto sampleCount = static_cast<std::size_t>(samplesPerEdge_) * samplesPerEdge_;
        if (heightBytes.size() != sampleCount * sizeof(float)
            || tile.weightPageA.size() != (hasSurfaceWeights_ ? sampleCount * 4u : 0u)
            || tile.weightPageB.size() != (hasSurfaceWeights_ ? sampleCount * 4u : 0u)
            || vegetationBytes.size() % 48u != 0u) {
            return {false,
                ResourceState::Failed,
                reference().id,
                reference().type,
                text("TerrainAsset block byte count violates its sampling layout")};
        }
        std::size_t offset = 0u;
        tile.heights.reserve(sampleCount);
        while (offset < heightBytes.size()) {
            float height = 0.0f;
            if (!readF32Le(heightBytes, offset, height)) {
                return {false,
                    ResourceState::Failed,
                    reference().id,
                    reference().type,
                    text("TerrainAsset height block contains non-finite data")};
            }
            tile.heights.push_back(height);
            heightRange_.minimum = std::min(heightRange_.minimum, height);
            heightRange_.maximum = std::max(heightRange_.maximum, height);
        }
        if (v2) {
            float minimum = 0.0f, maximum = 0.0f;
            if (!floatFor(tileNode, "minimumHeight", minimum)
                || !floatFor(tileNode, "maximumHeight", maximum)
                || minimum != *std::min_element(tile.heights.begin(), tile.heights.end())
                || maximum != *std::max_element(tile.heights.begin(), tile.heights.end())) {
                return resourceLoadFailure(
                    *this, "TERRAIN-HEIGHT-RANGE: metadata does not match payload");
            }
        }
        offset = 0u;
        while (offset < vegetationBytes.size()) {
            TerrainStaticVegetationInstance instance;
            if (!readU32Le(vegetationBytes, offset, instance.prototypeId)
                || !readF32Le(vegetationBytes, offset, instance.position.x)
                || !readF32Le(vegetationBytes, offset, instance.position.y)
                || !readF32Le(vegetationBytes, offset, instance.position.z)) {
                return {false,
                    ResourceState::Failed,
                    reference().id,
                    reference().type,
                    text("TerrainAsset vegetation block is malformed")};
            }
            for (float& component : instance.rotation)
                if (!readF32Le(vegetationBytes, offset, component))
                    return {false,
                        ResourceState::Failed,
                        reference().id,
                        reference().type,
                        text("TerrainAsset vegetation rotation is malformed")};
            if (!readF32Le(vegetationBytes, offset, instance.scale.x)
                || !readF32Le(vegetationBytes, offset, instance.scale.y)
                || !readF32Le(vegetationBytes, offset, instance.scale.z)
                || !readU32Le(vegetationBytes, offset, instance.variationSeed)
                || !validVegetationInstance(instance, bounds_)
                || instance.prototypeId > std::numeric_limits<std::uint16_t>::max()
                || catalog.get()->data->findAssetByTemplateId(
                       static_cast<std::uint16_t>(instance.prototypeId))
                    == nullptr) {
                return {false,
                    ResourceState::Failed,
                    reference().id,
                    reference().type,
                    text("TerrainAsset vegetation instance is invalid or outside terrain bounds")};
            }
            tile.vegetation.push_back(instance);
        }
        if (hasVegetation_)
            addDependency({vegetationCatalogResourceId_, text("terrain/vegetation-catalog")});
        for (const auto& layer : layers_)
            addDependency({layer.materialResourceId, text("terrain/material-layer")});
        tiles_->push_back(std::move(tile));
    }
    std::sort(tiles_->begin(), tiles_->end(), [](const auto& lhs, const auto& rhs) {
        return lhs.coord.z != rhs.coord.z ? lhs.coord.z < rhs.coord.z : lhs.coord.x < rhs.coord.x;
    });
    if (!heightRange_.valid()
        || !hasContinuousHeightBoundaries(*tiles_, tileCountX_, tileCountZ_, samplesPerEdge_)) {
        return {false,
            ResourceState::Failed,
            reference().id,
            reference().type,
            text("TerrainAsset adjacent tile height boundaries are not continuous")};
    }
    core::StableHash64 generationHasher;
    appendContentFingerprint(generationHasher);
    generation_ = generationHasher.value();
    if (generation_ == 0u)
        generation_ = 1u;
    return {true, ResourceState::Ready, reference().id, reference().type, {}};
}

std::size_t TerrainAssetResource::processedByteCount() const noexcept
{
    std::size_t total = 0u;
    for (const auto& tile : *tiles_) {
        total += tile.heights.size() * sizeof(float) + tile.weightPageA.size()
            + tile.weightPageB.size()
            + tile.vegetation.size() * sizeof(TerrainStaticVegetationInstance);
    }
    return total;
}

void TerrainAssetResource::appendContentFingerprint(core::StableHash64& hasher) const noexcept
{
    hasher.appendU64(hasSurfaceWeights_);
    hasher.appendU64(hasVegetation_);
    hasher.appendFloat(bounds_.minX);
    hasher.appendFloat(bounds_.maxX);
    hasher.appendFloat(bounds_.minZ);
    hasher.appendFloat(bounds_.maxZ);
    hasher.appendU64(tileCountX_);
    hasher.appendU64(tileCountZ_);
    hasher.appendU64(samplesPerEdge_);
    hasher.appendFloat(tileWorldSize_);
    hasher.appendString(vegetationCatalogResourceId_);
    for (const auto& layer : layers_) {
        hasher.appendString(layer.layerId);
        hasher.appendString(layer.semantic);
        hasher.appendString(layer.materialResourceId);
        hasher.appendU64(layer.page);
        hasher.appendU64(layer.channel);
    }
    for (const auto& tile : *tiles_) {
        hasher.appendU64(static_cast<std::uint64_t>(tile.coord.x));
        hasher.appendU64(static_cast<std::uint64_t>(tile.coord.z));
        hasher.appendBytes(tile.heights.data(), tile.heights.size() * sizeof(float));
        hasher.appendBytes(tile.weightPageA.data(), tile.weightPageA.size());
        hasher.appendBytes(tile.weightPageB.data(), tile.weightPageB.size());
        for (const auto& instance : tile.vegetation) {
            hasher.appendU64(instance.prototypeId);
            hasher.appendFloat(instance.position.x);
            hasher.appendFloat(instance.position.y);
            hasher.appendFloat(instance.position.z);
            for (const float component : instance.rotation)
                hasher.appendFloat(component);
            hasher.appendFloat(instance.scale.x);
            hasher.appendFloat(instance.scale.y);
            hasher.appendFloat(instance.scale.z);
            hasher.appendU64(instance.variationSeed);
        }
    }
}

terrain::TerrainSurfaceQuery TerrainAssetResource::sample(float worldX, float worldZ) const
{
    terrain::TerrainSurfaceQuery query;
    if (!bounds_.contains(worldX, worldZ)) {
        query.status = terrain::TerrainSurfaceQueryStatus::OutOfBounds;
        return query;
    }
    const auto tileIndex = [&](float value, float min, std::uint32_t count) {
        const float normalized = (value - min) / tileWorldSize_;
        return std::min(static_cast<std::uint32_t>(normalized), count - 1u);
    };
    const terrain::TerrainTileCoord coord{
        static_cast<std::int32_t>(tileIndex(worldX, bounds_.minX, tileCountX_)),
        static_cast<std::int32_t>(tileIndex(worldZ, bounds_.minZ, tileCountZ_)),
    };
    const auto found = std::find_if(tiles_->begin(), tiles_->end(), [&](const LoadedTile& tile) {
        return tile.coord == coord;
    });
    if (found == tiles_->end()) {
        query.status = terrain::TerrainSurfaceQueryStatus::TileUnavailable;
        return query;
    }
    const float originX = bounds_.minX + static_cast<float>(coord.x) * tileWorldSize_;
    const float originZ = bounds_.minZ + static_cast<float>(coord.z) * tileWorldSize_;
    const float u = std::clamp((worldX - originX) / tileWorldSize_, 0.0f, 1.0f);
    const float v = std::clamp((worldZ - originZ) / tileWorldSize_, 0.0f, 1.0f);
    const float fx = u * static_cast<float>(samplesPerEdge_ - 1u);
    const float fz = v * static_cast<float>(samplesPerEdge_ - 1u);
    const auto x0 = std::min(static_cast<std::uint32_t>(fx), samplesPerEdge_ - 2u),
               z0 = std::min(static_cast<std::uint32_t>(fz), samplesPerEdge_ - 2u);
    const auto x1 = std::min(x0 + 1u, samplesPerEdge_ - 1u),
               z1 = std::min(z0 + 1u, samplesPerEdge_ - 1u);
    const float tx = fx - static_cast<float>(x0), tz = fz - static_cast<float>(z0);
    const auto index = [&](std::uint32_t x, std::uint32_t z) {
        return static_cast<std::size_t>(z) * samplesPerEdge_ + x;
    };
    const auto interpolate = [&](const auto& values, std::size_t stride, std::size_t channel) {
        const auto a = static_cast<float>(values[index(x0, z0) * stride + channel]);
        const auto b = static_cast<float>(values[index(x1, z0) * stride + channel]);
        const auto c = static_cast<float>(values[index(x0, z1) * stride + channel]);
        const auto d = static_cast<float>(values[index(x1, z1) * stride + channel]);
        return (a + (b - a) * tx) * (1.0f - tz) + (c + (d - c) * tx) * tz;
    };
    query.sample.height = interpolate(found->heights, 1u, 0u);
    query.sample.hasSurfaceWeights = hasSurfaceWeights_;
    if (hasSurfaceWeights_) {
        for (std::uint32_t layer = 0u; layer < 4u; ++layer)
            query.sample.baseWeights[layer] = interpolate(found->weightPageA, 4u, layer) / 255.0f;
        query.sample.baseWeights[4] = interpolate(found->weightPageB, 4u, 0u) / 255.0f;
        query.sample.baseWeights[5] = interpolate(found->weightPageB, 4u, 1u) / 255.0f;
        query.sample.snowOverlay = interpolate(found->weightPageB, 4u, 2u) / 255.0f;
        query.sample.wetnessOverlay = interpolate(found->weightPageB, 4u, 3u) / 255.0f;
    }
    const float spacing = tileWorldSize_ / static_cast<float>(samplesPerEdge_ - 1u);
    const auto a = found->heights[index(x0, z0)], b = found->heights[index(x1, z0)],
               c = found->heights[index(x0, z1)], d = found->heights[index(x1, z1)];
    const float dx = ((b - a) * (1.0f - tz) + (d - c) * tz) / spacing;
    const float dz = ((c - a) * (1.0f - tx) + (d - b) * tx) / spacing;
    core::Vec3 normal{-dx, 1.0f, -dz};
    const float length = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
    if (!std::isfinite(length) || length <= 0.0f) {
        query.status = terrain::TerrainSurfaceQueryStatus::InvalidData;
        return query;
    }
    query.sample.normal = {normal.x / length, normal.y / length, normal.z / length};
    query.status = terrain::TerrainSurfaceQueryStatus::Ready;
    return query;
}

const core::pmr::Vector<TerrainStaticVegetationInstance>* TerrainAssetResource::vegetationFor(
    terrain::TerrainTileCoord coord) const
{
    const auto found = std::find_if(tiles_->begin(), tiles_->end(), [&](const LoadedTile& tile) {
        return tile.coord == coord;
    });
    return found == tiles_->end() ? nullptr : &found->vegetation;
}

bool TerrainAssetResource::tileReadView(
    terrain::TerrainTileCoord coord, TerrainAssetTileReadView& outView) const noexcept
{
    outView = {};
    const auto found = std::find_if(tiles_->begin(), tiles_->end(), [&](const LoadedTile& tile) {
        return tile.coord == coord;
    });
    if (found == tiles_->end()) {
        return false;
    }
    outView.coord = coord;
    outView.samplesPerEdge = samplesPerEdge_;
    outView.tileWorldSize = tileWorldSize_;
    outView.generation = generation_;
    outView.heights = found->heights.data();
    outView.heightCount = found->heights.size();
    outView.weightPageA = found->weightPageA.data();
    outView.weightPageAByteCount = found->weightPageA.size();
    outView.weightPageB = found->weightPageB.data();
    outView.weightPageBByteCount = found->weightPageB.size();
    return outView.geometryValid();
}

namespace {

/// ResourceManager owns the mutable publication slot. Runtime consumers own a
/// copied immutable generation so unload/reload cannot leave a dangling
/// TerrainSurface pointer in a submitted frame.
class TerrainAssetSurfaceSnapshot final : public terrain::TerrainSurface {
public:
    explicit TerrainAssetSurfaceSnapshot(const TerrainAssetResource& resource) : resource_(resource)
    {}

    terrain::TerrainBounds bounds() const noexcept override { return resource_.bounds(); }

    std::uint64_t generation() const noexcept override { return resource_.generation(); }

    terrain::TerrainSurfaceQuery sample(float worldX, float worldZ) const override
    {
        return resource_.sample(worldX, worldZ);
    }

private:
    TerrainAssetResource resource_;
};

} // namespace

std::shared_ptr<const terrain::TerrainSurface> TerrainAssetResource::snapshotSurface() const
{
    std::lock_guard lock(snapshotMutex_);
    if (!surfaceSnapshot_) {
        surfaceSnapshot_ = core::makeShared<TerrainAssetSurfaceSnapshot>(*this);
    }
    return surfaceSnapshot_;
}

TerrainAssetIoResult writeTerrainAsset(
    const TerrainAssetBuildDesc& desc, const std::filesystem::path& outputDirectory)
{
    return TerrainAssetWriter().write(desc, outputDirectory);
}

ResourceLoadResult loadTerrainAssetResource(ResourceManager& manager,
    ResourceReference reference,
    const std::filesystem::path& manifestPath,
    ResourceLoadPolicy policy)
{
    auto resource = core::makeUnique<TerrainAssetResource>(std::move(reference), manifestPath);
    return manager.loadResource(std::move(resource), {policy});
}

} // namespace arp::resource

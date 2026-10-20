#include "resource/terrain_asset_resource.h"

#include "core/file_io.h"
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
    try {
        std::size_t consumed = 0u;
        const auto parsed = std::stoull(std::string(source.data(), source.size()), &consumed, 10);
        if (consumed != source.size()) {
            return false;
        }
        out = parsed;
        return true;
    } catch (const std::exception&) {
        return false;
    }
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
    const std::filesystem::path parsed(path.c_str());
    if (path.empty() || parsed.is_absolute()) {
        return false;
    }
    for (const auto& part : parsed) {
        if (part == "..") {
            return false;
        }
    }
    return true;
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
    core::pmr::String& diagnostics)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.good()) {
        diagnostics = text("TerrainAsset failed to open output block: ");
        diagnostics += path.generic_string().c_str();
        return false;
    }
    file.write(reinterpret_cast<const char*>(bytes), static_cast<std::streamsize>(byteCount));
    if (!file.good()) {
        diagnostics = text("TerrainAsset failed to write output block: ");
        diagnostics += path.generic_string().c_str();
        return false;
    }
    return true;
}

bool writeTextFile(const std::filesystem::path& path,
    const core::pmr::String& contents,
    core::pmr::String& diagnostics)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.good()) {
        diagnostics = text("TerrainAsset failed to open manifest: ");
        diagnostics += path.generic_string().c_str();
        return false;
    }
    file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!file.good()) {
        diagnostics = text("TerrainAsset failed to write manifest: ");
        diagnostics += path.generic_string().c_str();
        return false;
    }
    return true;
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
    if (instance.prototypeId == 0u || !bounds.contains(instance.position.x, instance.position.z)
        || !std::isfinite(instance.position.x) || !std::isfinite(instance.position.y)
        || !std::isfinite(instance.position.z) || !std::isfinite(instance.scale.x)
        || !std::isfinite(instance.scale.y) || !std::isfinite(instance.scale.z)
        || instance.scale.x <= 0.0f || instance.scale.y <= 0.0f || instance.scale.z <= 0.0f) {
        return false;
    }
    float rotationLengthSquared = 0.0f;
    for (const float component : instance.rotation) {
        if (!std::isfinite(component))
            return false;
        rotationLengthSquared += component * component;
    }
    return std::fabs(rotationLengthSquared - 1.0f) <= 0.001f;
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
                if (tile.heights[index(samplesPerEdge - 1u, z)] != right->heights[index(0u, z)])
                    return false;
            }
        }
        if (static_cast<std::uint32_t>(tile.coord.z) + 1u < tileCountZ) {
            const auto* upper = findTile({tile.coord.x, tile.coord.z + 1});
            if (upper == nullptr)
                return false;
            for (std::uint32_t x = 0u; x < samplesPerEdge; ++x) {
                if (tile.heights[index(x, samplesPerEdge - 1u)] != upper->heights[index(x, 0u)])
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
    if (desc.vegetationCatalogResourceId.empty()) {
        diagnostics =
            text("TerrainAsset requires an explicit VegetationCatalogResource dependency");
        return false;
    }
    if (!validLayerBindings(desc.layers, diagnostics)) {
        return false;
    }
    const auto sampleCount = static_cast<std::size_t>(desc.samplesPerEdge) * desc.samplesPerEdge;
    core::pmr::Vector<std::uint8_t> coverage(&core::defaultCpuMemoryResource());
    coverage.resize(static_cast<std::size_t>(desc.tileCountX) * desc.tileCountZ, 0u);
    for (const auto& tile : desc.tiles) {
        if (tile.coord.x < 0 || tile.coord.z < 0
            || static_cast<std::uint32_t>(tile.coord.x) >= desc.tileCountX
            || static_cast<std::uint32_t>(tile.coord.z) >= desc.tileCountZ
            || tile.heights.size() != sampleCount || tile.weightPageA.size() != sampleCount * 4u
            || tile.weightPageB.size() != sampleCount * 4u) {
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

TerrainAssetIoResult TerrainAssetWriter::write(
    const TerrainAssetBuildDesc& desc, const std::filesystem::path& outputDirectory) const
{
    TerrainAssetIoResult result;
    if (!validBuildDesc(desc, result.diagnostics)) {
        return result;
    }
    std::error_code error;
    std::filesystem::create_directories(outputDirectory / "tiles", error);
    if (error) {
        result.diagnostics = text("TerrainAsset failed to create output directory: ");
        result.diagnostics += error.message().c_str();
        return result;
    }

    core::pmr::String manifest(&core::defaultCpuMemoryResource());
    manifest += "type: TerrainAssetResource\ncontractVersion: terrain.asset/v1\n";
    manifest += "resourceId: ";
    manifest += desc.resourceId;
    manifest += "\n";
    manifest += "status: ";
    manifest += desc.status;
    manifest += "\n";
    manifest += "bounds:\n  minX: ";
    manifest += std::to_string(desc.bounds.minX).c_str();
    manifest += "\n  maxX: ";
    manifest += std::to_string(desc.bounds.maxX).c_str();
    manifest += "\n  minZ: ";
    manifest += std::to_string(desc.bounds.minZ).c_str();
    manifest += "\n  maxZ: ";
    manifest += std::to_string(desc.bounds.maxZ).c_str();
    manifest += "\ntileCountX: ";
    manifest += std::to_string(desc.tileCountX).c_str();
    manifest += "\ntileCountZ: ";
    manifest += std::to_string(desc.tileCountZ).c_str();
    manifest += "\nsamplesPerEdge: ";
    manifest += std::to_string(desc.samplesPerEdge).c_str();
    manifest += "\ntileWorldSize: ";
    manifest += std::to_string(desc.tileWorldSize).c_str();
    manifest += "\nvegetationCatalogResourceId: ";
    manifest += desc.vegetationCatalogResourceId;
    manifest += "\nlayers:\n";
    for (const auto& layer : desc.layers) {
        manifest += "  - layerId: ";
        manifest += layer.layerId;
        manifest += "\n    semantic: ";
        manifest += layer.semantic;
        manifest += "\n    materialResourceId: ";
        manifest += layer.materialResourceId;
        manifest += "\n    page: ";
        manifest += std::to_string(layer.page).c_str();
        manifest += "\n    channel: ";
        manifest += std::to_string(layer.channel).c_str();
        manifest += "\n";
    }
    manifest += "tiles:\n";

    for (const auto& tile : desc.tiles) {
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
        const auto heightPath = stem + ".height.bin";
        const auto pageAPath = stem + ".weight-a.bin";
        const auto pageBPath = stem + ".weight-b.bin";
        const auto vegetationPath = stem + ".vegetation.bin";
        if (!writeFile(outputDirectory / std::filesystem::path(heightPath.c_str()),
                heightBytes.data(),
                heightBytes.size(),
                result.diagnostics)
            || !writeFile(outputDirectory / std::filesystem::path(pageAPath.c_str()),
                tile.weightPageA.data(),
                tile.weightPageA.size(),
                result.diagnostics)
            || !writeFile(outputDirectory / std::filesystem::path(pageBPath.c_str()),
                tile.weightPageB.data(),
                tile.weightPageB.size(),
                result.diagnostics)
            || !writeFile(outputDirectory / std::filesystem::path(vegetationPath.c_str()),
                vegetationBytes.data(),
                vegetationBytes.size(),
                result.diagnostics)) {
            return result;
        }
        auto block = [&](const char* name,
                         const core::pmr::String& path,
                         TerrainAssetBinaryFormat format,
                         const std::uint8_t* bytes,
                         std::size_t size) {
            manifest += "\n    ";
            manifest += name;
            manifest += ":\n      path: ";
            manifest += path;
            manifest += "\n      byteCount: ";
            manifest += std::to_string(size).c_str();
            manifest += "\n      contentHash: \"";
            manifest += std::to_string(hashBytes(bytes, size)).c_str();
            manifest += "\"";
            manifest += "\n      format: ";
            manifest += formatName(format);
        };
        manifest += "  - x: ";
        manifest += std::to_string(tile.coord.x).c_str();
        manifest += "\n    z: ";
        manifest += std::to_string(tile.coord.z).c_str();
        block("height",
            heightPath,
            TerrainAssetBinaryFormat::R32SFloat,
            heightBytes.data(),
            heightBytes.size());
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
        block("vegetation",
            vegetationPath,
            TerrainAssetBinaryFormat::StaticVegetationV1,
            vegetationBytes.data(),
            vegetationBytes.size());
        manifest += "\n";
    }
    if (!writeTextFile(outputDirectory / "terrain.asset.yaml", manifest, result.diagnostics)) {
        return result;
    }
    result.success = true;
    return result;
}

TerrainAssetResource::TerrainAssetResource(
    ResourceReference reference, std::filesystem::path manifestPath)
    : ResourceBase(std::move(reference)), manifestPath_(std::move(manifestPath))
{}

TerrainAssetResource::TerrainAssetResource(const TerrainAssetResource& other)
    : ResourceBase(other), manifestPath_(other.manifestPath_), bounds_(other.bounds_),
      tileCountX_(other.tileCountX_), tileCountZ_(other.tileCountZ_),
      samplesPerEdge_(other.samplesPerEdge_), tileWorldSize_(other.tileWorldSize_),
      generation_(other.generation_), heightRange_(other.heightRange_),
      vegetationCatalogResourceId_(other.vegetationCatalogResourceId_), layers_(other.layers_),
      tiles_(other.tiles_)
{}

ResourceLoadResult TerrainAssetResource::load(
    ResourceManager& manager, const ResourceLoadContext& context)
{
    {
        std::lock_guard lock(snapshotMutex_);
        surfaceSnapshot_.reset();
    }
    const auto manifestFile = core::readTextFile(manifestPath_);
    if (!manifestFile.success) {
        return {false,
            ResourceState::Failed,
            reference().id,
            reference().type,
            manifestFile.diagnostics};
    }
    YamlNode root;
    try {
        root = YamlNode::deserialize(
            manifestFile.text.data(), manifestFile.text.data() + manifestFile.text.size());
    } catch (const std::exception& error) {
        auto diagnostics = text("TerrainAsset manifest YAML parse failed: ");
        diagnostics += error.what();
        return {false, ResourceState::Failed, reference().id, reference().type, diagnostics};
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
    const auto contract = validateResourceContract(type, version, kTerrainAssetResourceContract);
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
        || tileWorldSize_ <= 0.0f || vegetationCatalogResourceId_.empty()) {
        return {false,
            ResourceState::Failed,
            reference().id,
            reference().type,
            text("TerrainAsset manifest geometry or vegetation dependency is invalid")};
    }
    const auto* layerNodes = valueFor(root, "layers");
    if (layerNodes == nullptr || !layerNodes->is_sequence()
        || layerNodes->get_value_ref<const YamlNode::sequence_type&>().size()
            != terrain::kTerrainBaseLayerCount + 2u) {
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
    if (!catalog.ready() || catalog.get() == nullptr || !catalog.get()->data) {
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
            || material->state == ResourceState::Unloaded) {
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
    tiles_.clear();
    tiles_.reserve(expectedTileCount);
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
        for (const auto& existing : tiles_) {
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
            const auto file =
                core::readBinaryFile(rootDirectory / std::filesystem::path(desc.path.c_str()));
            if (!file.success || file.bytes.size() != desc.byteCount
                || hashBytes(file.bytes.data(), file.bytes.size()) != desc.contentHash) {
                return false;
            }
            bytes = file.bytes;
            return true;
        };
        core::pmr::Vector<std::uint8_t> heightBytes(&core::defaultCpuMemoryResource());
        core::pmr::Vector<std::uint8_t> vegetationBytes(&core::defaultCpuMemoryResource());
        if (!readBlock("height", TerrainAssetBinaryFormat::R32SFloat, heightBytes)
            || !readBlock("weightPageA", TerrainAssetBinaryFormat::Rgba8Unorm, tile.weightPageA)
            || !readBlock("weightPageB", TerrainAssetBinaryFormat::Rgba8Unorm, tile.weightPageB)
            || !readBlock(
                "vegetation", TerrainAssetBinaryFormat::StaticVegetationV1, vegetationBytes)) {
            return {false,
                ResourceState::Failed,
                reference().id,
                reference().type,
                text("TerrainAsset block is missing, corrupt, or has an undeclared format")};
        }
        const auto sampleCount = static_cast<std::size_t>(samplesPerEdge_) * samplesPerEdge_;
        if (heightBytes.size() != sampleCount * sizeof(float)
            || tile.weightPageA.size() != sampleCount * 4u
            || tile.weightPageB.size() != sampleCount * 4u || vegetationBytes.size() % 48u != 0u) {
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
        addDependency({vegetationCatalogResourceId_, text("terrain/vegetation-catalog")});
        for (const auto& layer : layers_)
            addDependency({layer.materialResourceId, text("terrain/material-layer")});
        tiles_.push_back(std::move(tile));
    }
    if (!heightRange_.valid()
        || !hasContinuousHeightBoundaries(tiles_, tileCountX_, tileCountZ_, samplesPerEdge_)) {
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
    return ResourceBase::load(manager, context);
}

std::size_t TerrainAssetResource::processedByteCount() const noexcept
{
    std::size_t total = 0u;
    for (const auto& tile : tiles_) {
        total += tile.heights.size() * sizeof(float) + tile.weightPageA.size()
            + tile.weightPageB.size()
            + tile.vegetation.size() * sizeof(TerrainStaticVegetationInstance);
    }
    return total;
}

void TerrainAssetResource::appendContentFingerprint(core::StableHash64& hasher) const noexcept
{
    const auto manifest = manifestPath_.generic_string();
    hasher.appendString(manifest);
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
    for (const auto& tile : tiles_) {
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
    const auto found = std::find_if(tiles_.begin(), tiles_.end(), [&](const LoadedTile& tile) {
        return tile.coord == coord;
    });
    if (found == tiles_.end()) {
        query.status = terrain::TerrainSurfaceQueryStatus::TileUnavailable;
        return query;
    }
    const float originX = bounds_.minX + static_cast<float>(coord.x) * tileWorldSize_;
    const float originZ = bounds_.minZ + static_cast<float>(coord.z) * tileWorldSize_;
    const float u = std::clamp((worldX - originX) / tileWorldSize_, 0.0f, 1.0f);
    const float v = std::clamp((worldZ - originZ) / tileWorldSize_, 0.0f, 1.0f);
    const float fx = u * static_cast<float>(samplesPerEdge_ - 1u);
    const float fz = v * static_cast<float>(samplesPerEdge_ - 1u);
    const auto x0 = static_cast<std::uint32_t>(fx), z0 = static_cast<std::uint32_t>(fz);
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
    for (std::uint32_t layer = 0u; layer < 4u; ++layer)
        query.sample.baseWeights[layer] = interpolate(found->weightPageA, 4u, layer) / 255.0f;
    query.sample.baseWeights[4] = interpolate(found->weightPageB, 4u, 0u) / 255.0f;
    query.sample.baseWeights[5] = interpolate(found->weightPageB, 4u, 1u) / 255.0f;
    query.sample.snowOverlay = interpolate(found->weightPageB, 4u, 2u) / 255.0f;
    query.sample.wetnessOverlay = interpolate(found->weightPageB, 4u, 3u) / 255.0f;
    const float spacing = tileWorldSize_ / static_cast<float>(samplesPerEdge_ - 1u);
    const auto left = found->heights[index(x0 == 0u ? 0u : x0 - 1u, z0)];
    const auto right = found->heights[index(x1, z0)];
    const auto down = found->heights[index(x0, z0 == 0u ? 0u : z0 - 1u)];
    const auto up = found->heights[index(x0, z1)];
    core::Vec3 normal{-(right - left) / std::max(spacing, 0.0001f),
        1.0f,
        -(up - down) / std::max(spacing, 0.0001f)};
    const float length = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
    if (!std::isfinite(length) || length <= 0.0001f) {
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
    const auto found = std::find_if(tiles_.begin(), tiles_.end(), [&](const LoadedTile& tile) {
        return tile.coord == coord;
    });
    return found == tiles_.end() ? nullptr : &found->vegetation;
}

bool TerrainAssetResource::tileReadView(
    terrain::TerrainTileCoord coord, TerrainAssetTileReadView& outView) const noexcept
{
    outView = {};
    const auto found = std::find_if(tiles_.begin(), tiles_.end(), [&](const LoadedTile& tile) {
        return tile.coord == coord;
    });
    if (found == tiles_.end()) {
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
    return outView.valid();
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

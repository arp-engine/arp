#pragma once

#include "core/cpu_allocator.h"
#include "core/diagnostic_builder.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace arp::assets {

enum class MaterialType {
    SceneStylizedPbr,
    CharacterSkin,
    CharacterFace,
    CharacterHair,
    CharacterCloth,
    CharacterEye,
    Skybox,
    Water,
    VfxTransparent,
    GuiSurface
};

struct SceneStylizedMaterial {
    float lightWrap = 0.35f;
    float biomeTintStrength = 0.0f;
    float weatherMaskStrength = 0.0f;

    static SceneStylizedMaterial defaults() { return SceneStylizedMaterial{}; }
};

struct SceneMaterialAsset {
    std::pmr::string name;
    MaterialType type = MaterialType::SceneStylizedPbr;
    std::pmr::string baseColorTexture;
    std::pmr::string normalTexture;
    std::pmr::string roughnessMetallicAoTexture;
    SceneStylizedMaterial parameters;

    static SceneMaterialAsset stylizedProp(std::pmr::string name,
        std::pmr::string baseColorTexture,
        std::pmr::string normalTexture = {},
        std::pmr::string roughnessMetallicAoTexture = {})
    {
        SceneMaterialAsset material;
        material.name = std::move(name);
        material.type = MaterialType::SceneStylizedPbr;
        material.baseColorTexture = std::move(baseColorTexture);
        material.normalTexture = std::move(normalTexture);
        material.roughnessMetallicAoTexture = std::move(roughnessMetallicAoTexture);
        material.parameters = SceneStylizedMaterial::defaults();
        return material;
    }
};

constexpr uint32_t invalidImportedTextureIndex = UINT32_MAX;
constexpr uint32_t invalidImportedImageIndex = UINT32_MAX;
constexpr uint32_t invalidImportedBufferViewIndex = UINT32_MAX;

enum class ImportedAlphaMode { Opaque, Mask, Blend };

struct ImportedImageAsset {
    std::pmr::string name;
    std::pmr::string uri;
    std::pmr::string mimeType;
    uint32_t bufferView = invalidImportedBufferViewIndex;
    std::pmr::vector<uint8_t> encodedBytes;
    bool alphaIsTransparency = false;
    float alphaTransparencyReference = 0.5f;
};

struct ImportedTextureAsset {
    std::pmr::string name;
    uint32_t sourceImage = invalidImportedImageIndex;
    uint32_t sampler = invalidImportedTextureIndex;
    // glTF sampler enums; default wrap is REPEAT per the glTF specification.
    uint32_t wrapS = 10497u;
    uint32_t wrapT = 10497u;
};

struct ImportedMaterialAsset {
    std::pmr::string name;
    std::pmr::string sourceMaterialDocument;
    float baseColorFactor[4]{1.0f, 1.0f, 1.0f, 1.0f};
    float emissiveFactor[3]{};
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    uint32_t baseColorTexture = invalidImportedTextureIndex;
    uint32_t baseColorTextureCoordinateSet = 0;
    uint32_t normalTexture = invalidImportedTextureIndex;
    uint32_t normalTextureCoordinateSet = 0;
    uint32_t metallicRoughnessTexture = invalidImportedTextureIndex;
    uint32_t metallicRoughnessTextureCoordinateSet = 0;
    uint32_t occlusionTexture = invalidImportedTextureIndex;
    uint32_t occlusionTextureCoordinateSet = 0;
    uint32_t emissiveTexture = invalidImportedTextureIndex;
    uint32_t emissiveTextureCoordinateSet = 0;
    uint32_t clearcoatTexture = invalidImportedTextureIndex;
    uint32_t clearcoatRoughnessTexture = invalidImportedTextureIndex;
    uint32_t clearcoatNormalTexture = invalidImportedTextureIndex;
    uint32_t specularTexture = invalidImportedTextureIndex;
    uint32_t specularColorTexture = invalidImportedTextureIndex;
    uint32_t sheenColorTexture = invalidImportedTextureIndex;
    uint32_t sheenRoughnessTexture = invalidImportedTextureIndex;
    uint32_t transmissionTexture = invalidImportedTextureIndex;
    uint32_t thicknessTexture = invalidImportedTextureIndex;
    uint32_t iridescenceTexture = invalidImportedTextureIndex;
    uint32_t iridescenceThicknessTexture = invalidImportedTextureIndex;
    float occlusionStrength = 1.0f;
    float clearcoatFactor = 0.0f;
    float clearcoatRoughnessFactor = 0.0f;
    float specularFactor = 1.0f;
    float specularColorFactor[3]{1.0f, 1.0f, 1.0f};
    float sheenColorFactor[3]{};
    float sheenRoughnessFactor = 0.0f;
    float transmissionFactor = 0.0f;
    float thicknessFactor = 0.0f;
    float attenuationColor[3]{1.0f, 1.0f, 1.0f};
    float attenuationDistance = 0.0f;
    float ior = 1.5f;
    float iridescenceFactor = 0.0f;
    float iridescenceIor = 1.3f;
    float emissiveStrength = 1.0f;
    ImportedAlphaMode alphaMode = ImportedAlphaMode::Opaque;
    float alphaCutoff = 0.5f;
    bool doubleSided = false;
    bool hasCompleteMaterialImportMetadata = false;
    bool hasUnsupportedMaterialExtensions = false;
    // Authored additive Lambertian transmission. This is diffuse leaf
    // translucency, distinct from KHR_materials_transmission refraction.
    bool authoredLeafSurface = false;
    float leafTranslucency = 0.0f;
    float leafDielectricF0 = 0.04f;
    float leafTranslucentSaturation = 1.0f;
};

struct MeshVertex {
    float position[3]{};
    float normal[3]{0.0f, 0.0f, 1.0f};
    float uv[2]{};
    float tangent[4]{1.0f, 0.0f, 0.0f, 1.0f}; // xyz + bitangent sign
};

/// Foliage vertex: extends MeshVertex with wind deformation weights.
/// These are read from GLB COLOR_0 (R=primaryBend, G=flutter, B=stiffness,
/// A=reserved) per the plant_asset_set contract.
/// This is a SEPARATE vertex type -- MeshVertex is NOT modified, so the
/// existing prop/terrain importers are unaffected.
struct FoliageVertex {
    float position[3]{};
    float normal[3]{0.0f, 0.0f, 1.0f};
    float uv[2]{};
    float tangent[4]{1.0f, 0.0f, 0.0f, 1.0f};
    float primaryBendWeight = 0.0f; // COLOR_0.R [0,1], 0=root pinned
    float flutterWeight = 0.0f;     // COLOR_0.G [0,1], 0=wood, >0=foliage
    float stiffness = 1.0f;         // COLOR_0.B [0,1], normalized
};

struct Submesh {
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    uint32_t materialSlot = 0;
};

struct MeshBounds {
    float min[3]{};
    float max[3]{};
    bool valid = false;
};

struct MeshAsset {
    std::pmr::vector<MeshVertex> vertices;
    std::pmr::vector<uint32_t> indices;
    std::pmr::vector<Submesh> submeshes;
    MeshBounds bounds;

    bool recomputeBounds() noexcept
    {
        if (vertices.empty()) {
            bounds = {};
            return false;
        }
        MeshBounds computed;
        for (std::size_t axis = 0u; axis < 3u; ++axis) {
            computed.min[axis] = std::numeric_limits<float>::max();
            computed.max[axis] = std::numeric_limits<float>::lowest();
        }
        bool hasIndexedPosition = false;
        for (const auto& submesh : submeshes) {
            const auto firstIndex = static_cast<std::size_t>(submesh.firstIndex);
            const auto indexCount = static_cast<std::size_t>(submesh.indexCount);
            if (firstIndex > indices.size() || indexCount > indices.size() - firstIndex) {
                bounds = {};
                return false;
            }
            for (std::size_t offset = 0u; offset < indexCount; ++offset) {
                const auto vertexIndex = indices[firstIndex + offset];
                if (vertexIndex >= vertices.size()) {
                    bounds = {};
                    return false;
                }
                const auto& vertex = vertices[vertexIndex];
                for (std::size_t axis = 0u; axis < 3u; ++axis) {
                    const float value = vertex.position[axis];
                    if (!std::isfinite(value)) {
                        bounds = {};
                        return false;
                    }
                    computed.min[axis] = std::min(computed.min[axis], value);
                    computed.max[axis] = std::max(computed.max[axis], value);
                }
                hasIndexedPosition = true;
            }
        }
        if (!hasIndexedPosition) {
            bounds = {};
            return false;
        }
        computed.valid = true;
        bounds = computed;
        return true;
    }

    bool validForIndexedDraw() const
    {
        if (vertices.empty() || indices.empty() || submeshes.empty()) {
            return false;
        }
        for (const auto index : indices) {
            if (index >= vertices.size()) {
                return false;
            }
        }
        for (const auto& submesh : submeshes) {
            if (submesh.indexCount == 0
                || submesh.firstIndex + submesh.indexCount > indices.size()) {
                return false;
            }
        }
        return true;
    }

    static MeshAsset debugTriangle()
    {
        MeshAsset mesh;
        mesh.vertices = {
            MeshVertex{{0.0f, -0.62f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 1.0f}},
            MeshVertex{{0.66f, 0.54f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
            MeshVertex{{-0.66f, 0.54f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        };
        mesh.indices = {0, 1, 2};
        mesh.submeshes = {Submesh{0, 3, 0}};
        return mesh;
    }

    static MeshAsset toonCharacter()
    {
        MeshAsset mesh;
        auto appendBox =
            [&mesh](
                float cx, float cy, float cz, float hx, float hy, float hz, uint32_t materialSlot) {
                const uint32_t firstIndex = static_cast<uint32_t>(mesh.indices.size());
                auto addFace = [&mesh](const MeshVertex& a,
                                   const MeshVertex& b,
                                   const MeshVertex& c,
                                   const MeshVertex& d) {
                    const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
                    mesh.vertices.push_back(a);
                    mesh.vertices.push_back(b);
                    mesh.vertices.push_back(c);
                    mesh.vertices.push_back(d);
                    mesh.indices.insert(mesh.indices.end(),
                        {base + 0, base + 1, base + 2, base + 0, base + 2, base + 3});
                };

                const float x0 = cx - hx;
                const float x1 = cx + hx;
                const float y0 = cy - hy;
                const float y1 = cy + hy;
                const float z0 = cz - hz;
                const float z1 = cz + hz;
                addFace({{x0, y0, z1}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
                    {{x1, y0, z1}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
                    {{x1, y1, z1}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
                    {{x0, y1, z1}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}});
                addFace({{x1, y0, z0}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f}},
                    {{x0, y0, z0}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f}},
                    {{x0, y1, z0}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f}},
                    {{x1, y1, z0}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f}});
                addFace({{x1, y0, z1}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},
                    {{x1, y0, z0}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
                    {{x1, y1, z0}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
                    {{x1, y1, z1}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}});
                addFace({{x0, y0, z0}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},
                    {{x0, y0, z1}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
                    {{x0, y1, z1}, {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
                    {{x0, y1, z0}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}});
                addFace({{x0, y1, z1}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}},
                    {{x1, y1, z1}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},
                    {{x1, y1, z0}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
                    {{x0, y1, z0}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}});
                addFace({{x0, y0, z0}, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f}},
                    {{x1, y0, z0}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f}},
                    {{x1, y0, z1}, {0.0f, -1.0f, 0.0f}, {1.0f, 0.0f}},
                    {{x0, y0, z1}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f}});
                mesh.submeshes.push_back(Submesh{firstIndex,
                    static_cast<uint32_t>(mesh.indices.size()) - firstIndex,
                    materialSlot});
            };

        appendBox(0.0f, -0.36f, 0.0f, 0.36f, 0.52f, 0.24f, 0);
        appendBox(0.0f, 0.48f, 0.0f, 0.46f, 0.42f, 0.34f, 1);
        appendBox(-0.34f, 0.98f, 0.0f, 0.16f, 0.20f, 0.18f, 2);
        appendBox(0.34f, 0.98f, 0.0f, 0.16f, 0.20f, 0.18f, 2);
        appendBox(-0.16f, 0.52f, 0.35f, 0.055f, 0.06f, 0.025f, 3);
        appendBox(0.16f, 0.52f, 0.35f, 0.055f, 0.06f, 0.025f, 3);
        appendBox(-0.18f, -0.98f, 0.0f, 0.14f, 0.28f, 0.18f, 0);
        appendBox(0.18f, -0.98f, 0.0f, 0.14f, 0.28f, 0.18f, 0);
        return mesh;
    }
};

struct StaticMeshImportResult {
    bool success = false;
    std::pmr::string diagnostics;
    MeshAsset mesh;
    std::pmr::vector<ImportedMaterialAsset> materials;
    std::pmr::vector<ImportedTextureAsset> textures;
    std::pmr::vector<ImportedImageAsset> images;
};

/// A single named variant extracted from a GLB file. Each named node in
/// the GLB scene graph that carries a mesh becomes one variant entry.
/// This preserves the node-name -> mesh/material/transform mapping that
/// the merged StaticMeshImportResult loses.
struct NamedVariant {
    std::pmr::string name;                    ///< GLB node name (the variant id)
    MeshAsset mesh;                           ///< per-variant mesh (not merged)
    std::pmr::vector<uint32_t> materialSlots; ///< material indices used by this variant
    float worldTransform[16]{
        ///< local-to-world matrix of the node
        1.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        1.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        1.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        1.0f,
    };
    bool hasWindData = false; ///< true if COLOR_0 was present
    /// Per-vertex COLOR_0 wind weights (RGBA stride) read from the GLB.
    /// R=primaryBend, G=flutter, B=stiffness, A=reserved.
    /// Empty when hasWindData is false. Size == vertices.size() * 4 when present.
    std::pmr::vector<float> windWeights;
};

/// Result of importing a GLB file with named variant preservation.
/// Each mesh-bearing named node is extracted as a separate NamedVariant,
/// and COLOR_0 (wind weights) is read into FoliageVertex where present.
struct NamedVariantImportResult {
    bool success = false;
    std::pmr::string diagnostics;
    std::pmr::vector<NamedVariant> variants;
    std::pmr::vector<ImportedMaterialAsset> materials;
    std::pmr::vector<ImportedTextureAsset> textures;
    std::pmr::vector<ImportedImageAsset> images;
};

struct MaterialJsonSchema {
    std::pmr::string typeName;
    std::pmr::vector<std::pmr::string> requiredFields;
    std::pmr::vector<std::pmr::string> optionalFields;

    static MaterialJsonSchema sceneStylizedPbr()
    {
        MaterialJsonSchema schema;
        schema.typeName = "SceneStylizedPbr";
        schema.requiredFields = {"type", "name", "baseColorTexture"};
        schema.optionalFields = {"normalTexture",
            "roughnessMetallicAoTexture",
            "lightWrap",
            "biomeTintStrength",
            "weatherMaskStrength"};
        return schema;
    }
};

struct SceneMaterialImportResult {
    bool success = false;
    std::pmr::string diagnostics;
    SceneMaterialAsset material;
};

namespace detail {

inline const char* skipWhitespace(const char* cursor)
{
    while (*cursor && std::isspace(static_cast<unsigned char>(*cursor))) {
        ++cursor;
    }
    return cursor;
}

inline bool containsGltf20Asset(const std::pmr::string& json)
{
    const auto asset = json.find("\"asset\"");
    const auto version = json.find("\"version\"", asset == std::pmr::string::npos ? 0 : asset);
    const auto value = json.find("\"2.0\"", version == std::pmr::string::npos ? 0 : version);
    return asset != std::pmr::string::npos && version != std::pmr::string::npos
        && value != std::pmr::string::npos;
}

inline bool extractArrayTextAfterKey(
    const std::pmr::string& json, const char* key, std::pmr::string& out)
{
    const auto keyOffset = json.find(key);
    if (keyOffset == std::pmr::string::npos) {
        return false;
    }
    const auto open = json.find('[', keyOffset);
    if (open == std::pmr::string::npos) {
        return false;
    }
    int depth = 0;
    for (size_t index = open; index < json.size(); ++index) {
        if (json[index] == '[') {
            ++depth;
        } else if (json[index] == ']') {
            --depth;
            if (depth == 0) {
                out = json.substr(open + 1, index - open - 1);
                return true;
            }
        }
    }
    return false;
}

inline std::pmr::vector<float> parseFloatArray(const std::pmr::string& text)
{
    std::pmr::vector<float> values;
    const char* cursor = text.c_str();
    while (*cursor) {
        cursor = skipWhitespace(cursor);
        if (*cursor == ',' || *cursor == ':') {
            ++cursor;
            continue;
        }
        char* end = nullptr;
        const auto value = std::strtof(cursor, &end);
        if (end == cursor) {
            ++cursor;
            continue;
        }
        values.push_back(value);
        cursor = end;
    }
    return values;
}

inline std::pmr::vector<uint32_t> parseUIntArray(const std::pmr::string& text)
{
    std::pmr::vector<uint32_t> values;
    const char* cursor = text.c_str();
    while (*cursor) {
        cursor = skipWhitespace(cursor);
        if (*cursor == ',' || *cursor == ':') {
            ++cursor;
            continue;
        }
        char* end = nullptr;
        const auto value = std::strtoul(cursor, &end, 10);
        if (end == cursor) {
            ++cursor;
            continue;
        }
        values.push_back(static_cast<uint32_t>(value));
        cursor = end;
    }
    return values;
}

inline uint32_t parseUIntAfterKey(const std::pmr::string& json, const char* key, uint32_t fallback)
{
    const auto keyOffset = json.find(key);
    if (keyOffset == std::pmr::string::npos) {
        return fallback;
    }
    const auto colon = json.find(':', keyOffset);
    if (colon == std::pmr::string::npos) {
        return fallback;
    }
    char* end = nullptr;
    const auto value = std::strtoul(json.c_str() + colon + 1, &end, 10);
    return end == json.c_str() + colon + 1 ? fallback : static_cast<uint32_t>(value);
}

inline bool parseStringAfterKey(
    const std::pmr::string& json, const char* key, std::pmr::string& out)
{
    const auto keyOffset = json.find(key);
    if (keyOffset == std::pmr::string::npos) {
        return false;
    }
    const auto colon = json.find(':', keyOffset);
    if (colon == std::pmr::string::npos) {
        return false;
    }
    const auto open = json.find('"', colon + 1);
    if (open == std::pmr::string::npos) {
        return false;
    }
    const auto close = json.find('"', open + 1);
    if (close == std::pmr::string::npos) {
        return false;
    }
    out = json.substr(open + 1, close - open - 1);
    return true;
}

inline bool extractObjectArrayAfterKey(
    const std::pmr::string& json, const char* key, std::pmr::vector<std::pmr::string>& out)
{
    std::pmr::string arrayText;
    if (!extractArrayTextAfterKey(json, key, arrayText)) {
        return false;
    }
    int depth = 0;
    size_t objectStart = std::pmr::string::npos;
    for (size_t index = 0; index < arrayText.size(); ++index) {
        if (arrayText[index] == '{') {
            if (depth == 0) {
                objectStart = index;
            }
            ++depth;
        } else if (arrayText[index] == '}') {
            --depth;
            if (depth == 0 && objectStart != std::pmr::string::npos) {
                out.push_back(arrayText.substr(objectStart, index - objectStart + 1));
                objectStart = std::pmr::string::npos;
            }
        }
    }
    return !out.empty();
}

inline float parseFloatAfterKey(const std::pmr::string& json, const char* key, float fallback)
{
    const auto keyOffset = json.find(key);
    if (keyOffset == std::pmr::string::npos) {
        return fallback;
    }
    const auto colon = json.find(':', keyOffset);
    if (colon == std::pmr::string::npos) {
        return fallback;
    }
    char* end = nullptr;
    const auto value = std::strtof(json.c_str() + colon + 1, &end);
    return end == json.c_str() + colon + 1 ? fallback : value;
}

} // namespace detail

inline SceneMaterialImportResult importSceneMaterialFromJson(const std::pmr::string& json)
{
    SceneMaterialImportResult result;
    std::pmr::string type;
    if (!detail::parseStringAfterKey(json, "\"type\"", type) || type != "SceneStylizedPbr") {
        result.diagnostics = "material JSON must declare type SceneStylizedPbr";
        return result;
    }
    std::pmr::string name;
    if (!detail::parseStringAfterKey(json, "\"name\"", name)) {
        result.diagnostics = "material JSON missing required name";
        return result;
    }
    std::pmr::string baseColorTexture;
    if (!detail::parseStringAfterKey(json, "\"baseColorTexture\"", baseColorTexture)) {
        result.diagnostics = "material JSON missing required baseColorTexture";
        return result;
    }
    std::pmr::string normalTexture;
    std::pmr::string roughnessMetallicAoTexture;
    detail::parseStringAfterKey(json, "\"normalTexture\"", normalTexture);
    detail::parseStringAfterKey(json, "\"roughnessMetallicAoTexture\"", roughnessMetallicAoTexture);
    result.material = SceneMaterialAsset::stylizedProp(
        name, baseColorTexture, normalTexture, roughnessMetallicAoTexture);
    result.material.parameters.lightWrap =
        detail::parseFloatAfterKey(json, "\"lightWrap\"", result.material.parameters.lightWrap);
    result.material.parameters.biomeTintStrength = detail::parseFloatAfterKey(
        json, "\"biomeTintStrength\"", result.material.parameters.biomeTintStrength);
    result.material.parameters.weatherMaskStrength = detail::parseFloatAfterKey(
        json, "\"weatherMaskStrength\"", result.material.parameters.weatherMaskStrength);
    result.success = true;
    return result;
}

inline StaticMeshImportResult importStaticMeshFromGltfJson(const std::pmr::string& json)
{
    StaticMeshImportResult result;
    if (!detail::containsGltf20Asset(json)) {
        result.diagnostics = "glTF asset.version 2.0 is required";
        return result;
    }
    std::pmr::string positionsText;
    std::pmr::string normalsText;
    std::pmr::string uvsText;
    std::pmr::string indicesText;
    if (!detail::extractArrayTextAfterKey(json, "\"POSITION\"", positionsText)) {
        result.diagnostics = "glTF POSITION attribute is required";
        return result;
    }
    if (!detail::extractArrayTextAfterKey(json, "\"indices\"", indicesText)) {
        result.diagnostics = "glTF indices array is required";
        return result;
    }
    detail::extractArrayTextAfterKey(json, "\"NORMAL\"", normalsText);
    detail::extractArrayTextAfterKey(json, "\"TEXCOORD_0\"", uvsText);
    const auto positions = detail::parseFloatArray(positionsText);
    const auto normals = detail::parseFloatArray(normalsText);
    const auto uvs = detail::parseFloatArray(uvsText);
    auto indices = detail::parseUIntArray(indicesText);
    if (positions.empty() || positions.size() % 3 != 0) {
        result.diagnostics = "glTF POSITION must contain float3 data";
        return result;
    }
    if (!normals.empty() && normals.size() != positions.size()) {
        result.diagnostics = "glTF NORMAL count must match POSITION count";
        return result;
    }
    if (!uvs.empty() && uvs.size() / 2 != positions.size() / 3) {
        result.diagnostics = "glTF TEXCOORD_0 count must match POSITION count";
        return result;
    }
    if (indices.empty()) {
        result.diagnostics = "glTF indices array cannot be empty";
        return result;
    }

    const auto vertexCount = positions.size() / 3;
    MeshAsset mesh;
    mesh.vertices.reserve(vertexCount);
    for (size_t index = 0; index < vertexCount; ++index) {
        MeshVertex vertex;
        vertex.position[0] = positions[index * 3 + 0];
        vertex.position[1] = positions[index * 3 + 1];
        vertex.position[2] = positions[index * 3 + 2];
        if (!normals.empty()) {
            vertex.normal[0] = normals[index * 3 + 0];
            vertex.normal[1] = normals[index * 3 + 1];
            vertex.normal[2] = normals[index * 3 + 2];
        }
        if (!uvs.empty()) {
            vertex.uv[0] = uvs[index * 2 + 0];
            vertex.uv[1] = uvs[index * 2 + 1];
        }
        mesh.vertices.push_back(vertex);
    }
    mesh.indices = std::move(indices);
    mesh.submeshes = {Submesh{0,
        static_cast<uint32_t>(mesh.indices.size()),
        detail::parseUIntAfterKey(json, "\"material\"", 0)}};
    if (!mesh.validForIndexedDraw()) {
        result.diagnostics = "glTF mesh is not valid for indexed draw";
        return result;
    }
    result.success = true;
    result.mesh = std::move(mesh);
    return result;
}

enum class MobileTextureCompression { None, Astc, Etc2, Bc };

struct RuntimeMeshAssetDesc {
    std::pmr::string name;
    std::pmr::string sourcePath;
    bool optimizeIndices = false;
    bool generateBounds = false;
    bool prepareMeshlets = false;
};

struct RuntimeMaterialAssetDesc {
    std::pmr::string name;
    std::pmr::string sourcePath;
};

struct RuntimeTextureAssetDesc {
    std::pmr::string name;
    std::pmr::string sourcePath;
    bool generateMips = false;
    bool srgb = false;
    bool mobileTarget = false;
    MobileTextureCompression compression = MobileTextureCompression::None;
};

struct RuntimeShaderReferenceDesc {
    std::pmr::string name;
    std::pmr::string slangSourcePath;
    std::pmr::string spirvPath;
    std::pmr::string mslPath;
    std::pmr::string reflectionPath;
};

struct RuntimeUiLayoutAssetDesc {
    std::pmr::string name;
    std::pmr::string sourcePath;
};

struct RuntimeSceneManifestDesc {
    std::pmr::string name;
    std::pmr::string sourcePath;
};

struct RuntimeAssetPackageDesc {
    std::pmr::string name;
    std::pmr::vector<RuntimeMeshAssetDesc> meshes;
    std::pmr::vector<RuntimeMaterialAssetDesc> materials;
    std::pmr::vector<RuntimeTextureAssetDesc> textures;
    std::pmr::vector<RuntimeShaderReferenceDesc> shaderReferences;
    std::pmr::vector<RuntimeUiLayoutAssetDesc> uiLayouts;
    std::pmr::vector<RuntimeSceneManifestDesc> sceneManifests;
};

struct RuntimeAssetPackagePlan {
    bool valid = false;
    bool runtimeReady = false;
    std::pmr::string packageName;
    std::pmr::vector<std::pmr::string> entries;
    MobileTextureCompression textureCompression = MobileTextureCompression::None;
    uint32_t deterministicCacheKey = 0;
    std::pmr::string diagnostics;
};

inline uint32_t hashString(uint32_t hash, const std::pmr::string& value)
{
    for (const auto ch : value) {
        hash ^= static_cast<uint8_t>(ch);
        hash *= 16777619u;
    }
    return hash;
}

inline void addPackageEntry(RuntimeAssetPackagePlan& plan,
    const std::pmr::string& kind,
    const std::pmr::string& name,
    const std::pmr::string& sourcePath)
{
    plan.entries.push_back(kind + ":" + name + ":" + sourcePath);
    plan.deterministicCacheKey = hashString(plan.deterministicCacheKey, kind);
    plan.deterministicCacheKey = hashString(plan.deterministicCacheKey, name);
    plan.deterministicCacheKey = hashString(plan.deterministicCacheKey, sourcePath);
}

inline RuntimeAssetPackagePlan buildRuntimeAssetPackagePlan(const RuntimeAssetPackageDesc& desc)
{
    RuntimeAssetPackagePlan plan;
    plan.packageName = desc.name;
    plan.valid = !desc.name.empty();
    plan.runtimeReady = plan.valid;
    plan.deterministicCacheKey = hashString(2166136261u, desc.name);

    auto fail = [&](const std::pmr::string& message) {
        plan.valid = false;
        plan.runtimeReady = false;
        arp::core::appendDiagnostic(plan.diagnostics, message);
    };

    for (const auto& mesh : desc.meshes) {
        if (mesh.name.empty() || mesh.sourcePath.empty()) {
            fail("mesh entry requires name and source");
        }
        addPackageEntry(plan, "mesh", mesh.name, mesh.sourcePath);
        plan.deterministicCacheKey ^= mesh.optimizeIndices ? 0x11u : 0x01u;
        plan.deterministicCacheKey ^= mesh.generateBounds ? 0x22u : 0x02u;
        plan.deterministicCacheKey ^= mesh.prepareMeshlets ? 0x44u : 0x04u;
    }
    for (const auto& material : desc.materials) {
        if (material.name.empty() || material.sourcePath.empty()) {
            fail("material entry requires name and source");
        }
        addPackageEntry(plan, "material", material.name, material.sourcePath);
    }
    for (const auto& texture : desc.textures) {
        if (texture.name.empty() || texture.sourcePath.empty()) {
            fail("texture entry requires name and source");
        }
        if (texture.mobileTarget && texture.compression == MobileTextureCompression::None) {
            fail("mobile texture entry requires compression");
        }
        if (plan.textureCompression == MobileTextureCompression::None) {
            plan.textureCompression = texture.compression;
        }
        addPackageEntry(plan, "texture", texture.name, texture.sourcePath);
        plan.deterministicCacheKey ^= texture.generateMips ? 0x101u : 0x010u;
        plan.deterministicCacheKey ^= texture.srgb ? 0x202u : 0x020u;
        plan.deterministicCacheKey ^= static_cast<uint32_t>(texture.compression) << 24u;
    }
    for (const auto& shader : desc.shaderReferences) {
        if (shader.name.empty() || shader.slangSourcePath.empty() || shader.spirvPath.empty()
            || shader.mslPath.empty() || shader.reflectionPath.empty()) {
            fail("shader entry requires Slang, SPIR-V, MSL, and reflection outputs");
        }
        addPackageEntry(plan,
            "shader",
            shader.name,
            shader.slangSourcePath + "|" + shader.spirvPath + "|" + shader.mslPath + "|"
                + shader.reflectionPath);
    }
    for (const auto& layout : desc.uiLayouts) {
        if (layout.name.empty() || layout.sourcePath.empty()) {
            fail("ui layout entry requires name and source");
        }
        addPackageEntry(plan, "ui", layout.name, layout.sourcePath);
    }
    for (const auto& scene : desc.sceneManifests) {
        if (scene.name.empty() || scene.sourcePath.empty()) {
            fail("scene manifest entry requires name and source");
        }
        addPackageEntry(plan, "scene", scene.name, scene.sourcePath);
    }
    return plan;
}

enum class AssetDependencyKind {
    Assimp,
    Meshoptimizer,
    KtxSoftware,
    Json,
    DirectXTex,
    StbImage,
    Cgltf
};

enum class SourceAssetFormat { Gltf, Glb, Fbx, Obj, Collada, Ply, Stl, Unknown };

struct AssetDependencyDesc {
    AssetDependencyKind kind = AssetDependencyKind::Assimp;
    std::pmr::string packageName;
    bool required = true;
    bool runtimeDependency = false;
};

struct AssetDependencyPlan {
    std::pmr::vector<AssetDependencyDesc> dependencies;
    std::pmr::vector<SourceAssetFormat> assimpSourceFormats;

    bool assimpBroadImportEnabled() const
    {
        return std::find_if(dependencies.begin(),
                   dependencies.end(),
                   [](const AssetDependencyDesc& dependency) {
                       return dependency.kind == AssetDependencyKind::Assimp && dependency.required
                           && !dependency.runtimeDependency;
                   })
            != dependencies.end();
    }

    bool supportsSourceFormat(SourceAssetFormat format) const
    {
        return std::find(assimpSourceFormats.begin(), assimpSourceFormats.end(), format)
            != assimpSourceFormats.end();
    }
};

inline AssetDependencyPlan buildDefaultAssetDependencyPlan()
{
    AssetDependencyPlan plan;
    plan.dependencies = {
        {AssetDependencyKind::Assimp, "assimp", true, false},
        {AssetDependencyKind::Meshoptimizer, "meshoptimizer", true, false},
        {AssetDependencyKind::KtxSoftware, "KTX-Software", true, false},
        {AssetDependencyKind::Json, "nlohmann_json", true, false},
        {AssetDependencyKind::DirectXTex, "DirectXTex", false, false},
        {AssetDependencyKind::StbImage, "stb_image", false, false},
        {AssetDependencyKind::Cgltf, "cgltf", false, false},
    };
    plan.assimpSourceFormats = {
        SourceAssetFormat::Gltf,
        SourceAssetFormat::Glb,
        SourceAssetFormat::Fbx,
        SourceAssetFormat::Obj,
        SourceAssetFormat::Collada,
        SourceAssetFormat::Ply,
        SourceAssetFormat::Stl,
    };
    return plan;
}

} // namespace arp::assets

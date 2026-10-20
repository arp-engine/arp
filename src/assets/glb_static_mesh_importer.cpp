#include "assets/glb_static_mesh_importer.h"

#include "assets/glb_importer_common.h"
#include "assets/tangent_generation.h"
#include "core/cpu_allocator.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>

namespace arp::assets {

namespace {

constexpr uint32_t glbMagic = 0x46546C67u;
constexpr uint32_t jsonChunkType = 0x4E4F534Au;
constexpr uint32_t binChunkType = 0x004E4942u;

struct GlbChunks {
    std::pmr::string json;
    std::pmr::string bin;
};

struct BufferViewDesc {
    uint32_t buffer = 0;
    uint32_t byteOffset = 0;
    uint32_t byteLength = 0;
    uint32_t byteStride = 0;
};

struct AccessorDesc {
    uint32_t bufferView = 0;
    uint32_t byteOffset = 0;
    uint32_t componentType = 0;
    uint32_t count = 0;
    std::pmr::string type;
    bool normalized = false;
};

struct PrimitiveDesc {
    uint32_t positionAccessor = UINT32_MAX;
    uint32_t normalAccessor = UINT32_MAX;
    uint32_t texcoordAccessor = UINT32_MAX;
    uint32_t tangentAccessor = UINT32_MAX;
    uint32_t color0Accessor = UINT32_MAX; // COLOR_0: wind weights (R=bend, G=flutter, B=stiffness)
    uint32_t indicesAccessor = UINT32_MAX;
    uint32_t material = 0;
    uint32_t mode = 4;
};

struct MeshDesc {
    std::pmr::vector<PrimitiveDesc> primitives;
};

struct NodeDesc {
    uint32_t mesh = UINT32_MAX;
    std::pmr::string name; ///< GLB node name (for variant preservation)
    std::pmr::vector<uint32_t> children;
    float matrix[16]{
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
    bool transformValid = true;
};

uint32_t readU32(const std::pmr::string& bytes, const std::size_t offset)
{
    if (offset + 4 > bytes.size()) {
        return 0;
    }
    return static_cast<uint32_t>(static_cast<unsigned char>(bytes[offset + 0]))
        | (static_cast<uint32_t>(static_cast<unsigned char>(bytes[offset + 1])) << 8u)
        | (static_cast<uint32_t>(static_cast<unsigned char>(bytes[offset + 2])) << 16u)
        | (static_cast<uint32_t>(static_cast<unsigned char>(bytes[offset + 3])) << 24u);
}

StaticMeshImportResult fail(const std::pmr::string& diagnostics)
{
    StaticMeshImportResult result;
    result.diagnostics = diagnostics;
    return result;
}

bool readFile(const std::filesystem::path& path, std::pmr::string& out)
{
    std::ifstream file(path, std::ios::binary);
    if (!file.good()) {
        return false;
    }
    std::ostringstream stream;
    stream << file.rdbuf();
    out = stream.str();
    return true;
}

bool readGlbChunks(const std::pmr::string& bytes, GlbChunks& chunks, std::pmr::string& diagnostics)
{
    if (bytes.size() < 20 || readU32(bytes, 0) != glbMagic || readU32(bytes, 4) != 2) {
        diagnostics = "GLB v2 header is required";
        return false;
    }
    if (readU32(bytes, 8) != bytes.size()) {
        diagnostics = "GLB declared length mismatch";
        return false;
    }

    const auto jsonLength = readU32(bytes, 12);
    const auto jsonType = readU32(bytes, 16);
    if (jsonType != jsonChunkType || 20u + jsonLength > bytes.size()) {
        diagnostics = "GLB JSON chunk is invalid";
        return false;
    }
    chunks.json = bytes.substr(20, jsonLength);

    const auto binOffset = 20u + jsonLength;
    if (binOffset + 8u > bytes.size()) {
        diagnostics = "GLB binary chunk is required";
        return false;
    }
    const auto binLength = readU32(bytes, binOffset);
    const auto binType = readU32(bytes, binOffset + 4u);
    if (binType != binChunkType || binOffset + 8u + binLength > bytes.size()) {
        diagnostics = "GLB binary chunk is invalid";
        return false;
    }
    chunks.bin = bytes.substr(binOffset + 8u, binLength);
    return true;
}

bool extractArrayTextAfterKeyAt(const std::pmr::string& json,
    const char* key,
    const std::size_t offset,
    std::pmr::string& out,
    std::size_t& foundOffset)
{
    const auto keyOffset = json.find(key, offset);
    if (keyOffset == std::pmr::string::npos) {
        return false;
    }
    foundOffset = keyOffset;
    const auto open = json.find('[', keyOffset);
    if (open == std::pmr::string::npos) {
        return false;
    }

    bool inString = false;
    bool escaped = false;
    int depth = 0;
    for (std::size_t index = open; index < json.size(); ++index) {
        const auto ch = json[index];
        if (inString) {
            escaped = !escaped && ch == '\\';
            if (ch == '"' && !escaped) {
                inString = false;
            } else if (ch != '\\') {
                escaped = false;
            }
            continue;
        }
        if (ch == '"') {
            inString = true;
        } else if (ch == '[') {
            ++depth;
        } else if (ch == ']') {
            --depth;
            if (depth == 0) {
                out = json.substr(open + 1, index - open - 1);
                return true;
            }
        }
    }
    return false;
}

bool extractArrayTextAfterKey(const std::pmr::string& json, const char* key, std::pmr::string& out)
{
    std::size_t foundOffset = 0;
    return extractArrayTextAfterKeyAt(json, key, 0, out, foundOffset);
}

bool extractFirstTopLevelObject(const std::pmr::string& arrayText, std::pmr::string& out)
{
    bool inString = false;
    bool escaped = false;
    int depth = 0;
    std::size_t begin = std::pmr::string::npos;
    for (std::size_t index = 0; index < arrayText.size(); ++index) {
        const auto ch = arrayText[index];
        if (inString) {
            escaped = !escaped && ch == '\\';
            if (ch == '"' && !escaped) {
                inString = false;
            } else if (ch != '\\') {
                escaped = false;
            }
            continue;
        }
        if (ch == '"') {
            inString = true;
        } else if (ch == '{') {
            if (depth == 0) {
                begin = index;
            }
            ++depth;
        } else if (ch == '}') {
            --depth;
            if (depth == 0 && begin != std::pmr::string::npos) {
                out = arrayText.substr(begin, index - begin + 1);
                return true;
            }
        }
    }
    return false;
}

std::pmr::vector<std::pmr::string> extractObjectArray(const std::pmr::string& json, const char* key)
{
    std::pmr::vector<std::pmr::string> objects;
    std::pmr::string arrayText;
    if (!extractArrayTextAfterKey(json, key, arrayText)) {
        return objects;
    }

    bool inString = false;
    bool escaped = false;
    int depth = 0;
    std::size_t begin = std::pmr::string::npos;
    for (std::size_t index = 0; index < arrayText.size(); ++index) {
        const auto ch = arrayText[index];
        if (inString) {
            escaped = !escaped && ch == '\\';
            if (ch == '"' && !escaped) {
                inString = false;
            } else if (ch != '\\') {
                escaped = false;
            }
            continue;
        }
        if (ch == '"') {
            inString = true;
        } else if (ch == '{') {
            if (depth == 0) {
                begin = index;
            }
            ++depth;
        } else if (ch == '}') {
            --depth;
            if (depth == 0 && begin != std::pmr::string::npos) {
                objects.push_back(arrayText.substr(begin, index - begin + 1));
                begin = std::pmr::string::npos;
            }
        }
    }
    return objects;
}

uint32_t parseUIntAfterKey(const std::pmr::string& json, const char* key, const uint32_t fallback)
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

std::pmr::string parseStringAfterKey(const std::pmr::string& json, const char* key)
{
    const auto keyOffset = json.find(key);
    if (keyOffset == std::pmr::string::npos) {
        return {};
    }
    const auto colon = json.find(':', keyOffset);
    const auto open = json.find('"', colon == std::pmr::string::npos ? keyOffset : colon + 1);
    const auto close = json.find('"', open == std::pmr::string::npos ? keyOffset : open + 1);
    if (open == std::pmr::string::npos || close == std::pmr::string::npos) {
        return {};
    }
    return json.substr(open + 1, close - open - 1);
}

uint32_t jsonUInt(const detail::JsonValue* value, uint32_t fallback)
{
    if (value == nullptr || value->type != detail::JsonValue::Type::Number
        || value->numberValue < 0.0) {
        return fallback;
    }
    return static_cast<uint32_t>(value->numberValue);
}

float jsonFloat(const detail::JsonValue* value, float fallback)
{
    return value != nullptr && value->type == detail::JsonValue::Type::Number
        ? static_cast<float>(value->numberValue)
        : fallback;
}

bool copyFloatArray(const detail::JsonValue* value, float* out, std::size_t count)
{
    if (value == nullptr || value->type != detail::JsonValue::Type::Array
        || value->arrayValue.size() != count) {
        return false;
    }
    for (std::size_t index = 0; index < count; ++index) {
        if (value->arrayValue[index].type != detail::JsonValue::Type::Number) {
            return false;
        }
        out[index] = static_cast<float>(value->arrayValue[index].numberValue);
    }
    return true;
}

void setIdentity(float* matrix)
{
    for (uint32_t index = 0; index < 16u; ++index) {
        matrix[index] = 0.0f;
    }
    matrix[0] = 1.0f;
    matrix[5] = 1.0f;
    matrix[10] = 1.0f;
    matrix[15] = 1.0f;
}

void multiplyMatrix(const float* lhs, const float* rhs, float* out)
{
    float result[16]{};
    for (uint32_t column = 0; column < 4u; ++column) {
        for (uint32_t row = 0; row < 4u; ++row) {
            result[column * 4u + row] = lhs[0u * 4u + row] * rhs[column * 4u + 0u]
                + lhs[1u * 4u + row] * rhs[column * 4u + 1u]
                + lhs[2u * 4u + row] * rhs[column * 4u + 2u]
                + lhs[3u * 4u + row] * rhs[column * 4u + 3u];
        }
    }
    std::copy(result, result + 16u, out);
}

void makeTranslationMatrix(const float* translation, float* out)
{
    setIdentity(out);
    out[12] = translation[0];
    out[13] = translation[1];
    out[14] = translation[2];
}

void makeScaleMatrix(const float* scale, float* out)
{
    setIdentity(out);
    out[0] = scale[0];
    out[5] = scale[1];
    out[10] = scale[2];
}

bool makeRotationMatrix(const float* rotation, float* out)
{
    const auto x = rotation[0];
    const auto y = rotation[1];
    const auto z = rotation[2];
    const auto w = rotation[3];
    const auto length = std::sqrt(x * x + y * y + z * z + w * w);
    if (length <= 0.000001f) {
        return false;
    }
    const auto nx = x / length;
    const auto ny = y / length;
    const auto nz = z / length;
    const auto nw = w / length;

    setIdentity(out);
    out[0] = 1.0f - 2.0f * ny * ny - 2.0f * nz * nz;
    out[1] = 2.0f * nx * ny + 2.0f * nw * nz;
    out[2] = 2.0f * nx * nz - 2.0f * nw * ny;
    out[4] = 2.0f * nx * ny - 2.0f * nw * nz;
    out[5] = 1.0f - 2.0f * nx * nx - 2.0f * nz * nz;
    out[6] = 2.0f * ny * nz + 2.0f * nw * nx;
    out[8] = 2.0f * nx * nz + 2.0f * nw * ny;
    out[9] = 2.0f * ny * nz - 2.0f * nw * nx;
    out[10] = 1.0f - 2.0f * nx * nx - 2.0f * ny * ny;
    return true;
}

void transformPoint(const float* matrix, const float* in, float* out)
{
    out[0] = matrix[0] * in[0] + matrix[4] * in[1] + matrix[8] * in[2] + matrix[12];
    out[1] = matrix[1] * in[0] + matrix[5] * in[1] + matrix[9] * in[2] + matrix[13];
    out[2] = matrix[2] * in[0] + matrix[6] * in[1] + matrix[10] * in[2] + matrix[14];
}

void normalizeVector(float* value)
{
    const auto length = std::sqrt(value[0] * value[0] + value[1] * value[1] + value[2] * value[2]);
    if (length <= 0.000001f) {
        return;
    }
    value[0] /= length;
    value[1] /= length;
    value[2] /= length;
}

void transformVector(const float* matrix, const float* in, float* out)
{
    out[0] = matrix[0] * in[0] + matrix[4] * in[1] + matrix[8] * in[2];
    out[1] = matrix[1] * in[0] + matrix[5] * in[1] + matrix[9] * in[2];
    out[2] = matrix[2] * in[0] + matrix[6] * in[1] + matrix[10] * in[2];
    normalizeVector(out);
}

PrimitiveDesc parsePrimitive(const detail::JsonValue& object)
{
    PrimitiveDesc primitive;
    const auto* attributes = object.find("attributes");
    if (attributes != nullptr && attributes->type == detail::JsonValue::Type::Object) {
        primitive.positionAccessor = jsonUInt(attributes->find("POSITION"), UINT32_MAX);
        primitive.normalAccessor = jsonUInt(attributes->find("NORMAL"), UINT32_MAX);
        primitive.texcoordAccessor = jsonUInt(attributes->find("TEXCOORD_0"), UINT32_MAX);
        primitive.tangentAccessor = jsonUInt(attributes->find("TANGENT"), UINT32_MAX);
        primitive.color0Accessor = jsonUInt(attributes->find("COLOR_0"), UINT32_MAX);
    }
    primitive.indicesAccessor = jsonUInt(object.find("indices"), UINT32_MAX);
    primitive.material = jsonUInt(object.find("material"), 0);
    primitive.mode = jsonUInt(object.find("mode"), 4);
    return primitive;
}

std::pmr::vector<MeshDesc> parseMeshes(const detail::JsonValue& root)
{
    std::pmr::vector<MeshDesc> meshes;
    const auto* meshArray = root.find("meshes");
    if (meshArray == nullptr || meshArray->type != detail::JsonValue::Type::Array) {
        return meshes;
    }

    meshes.reserve(meshArray->arrayValue.size());
    for (const auto& meshValue : meshArray->arrayValue) {
        MeshDesc mesh;
        const auto* primitiveArray = meshValue.find("primitives");
        if (primitiveArray != nullptr && primitiveArray->type == detail::JsonValue::Type::Array) {
            mesh.primitives.reserve(primitiveArray->arrayValue.size());
            for (const auto& primitiveValue : primitiveArray->arrayValue) {
                mesh.primitives.push_back(parsePrimitive(primitiveValue));
            }
        }
        meshes.push_back(std::move(mesh));
    }
    return meshes;
}

NodeDesc parseNode(const detail::JsonValue& object)
{
    NodeDesc Node;
    Node.mesh = jsonUInt(object.find("mesh"), UINT32_MAX);

    // Read node name (used for named-variant preservation).
    const auto* nameVal = object.find("name");
    if (nameVal != nullptr && nameVal->type == detail::JsonValue::Type::String) {
        Node.name = nameVal->stringValue;
    }

    const auto* children = object.find("children");
    if (children != nullptr && children->type == detail::JsonValue::Type::Array) {
        Node.children.reserve(children->arrayValue.size());
        for (const auto& child : children->arrayValue) {
            Node.children.push_back(jsonUInt(&child, UINT32_MAX));
        }
    }

    const auto* matrix = object.find("matrix");
    if (matrix != nullptr) {
        Node.transformValid = copyFloatArray(matrix, Node.matrix, 16u);
        return Node;
    }

    float translation[3]{};
    float rotation[4]{0.0f, 0.0f, 0.0f, 1.0f};
    float scale[3]{1.0f, 1.0f, 1.0f};
    if (object.find("translation") != nullptr
        && !copyFloatArray(object.find("translation"), translation, 3u)) {
        Node.transformValid = false;
        return Node;
    }
    if (object.find("rotation") != nullptr
        && !copyFloatArray(object.find("rotation"), rotation, 4u)) {
        Node.transformValid = false;
        return Node;
    }
    if (object.find("scale") != nullptr && !copyFloatArray(object.find("scale"), scale, 3u)) {
        Node.transformValid = false;
        return Node;
    }

    float translationMatrix[16]{};
    float rotationMatrix[16]{};
    float scaleMatrix[16]{};
    float rotationScale[16]{};
    makeTranslationMatrix(translation, translationMatrix);
    makeScaleMatrix(scale, scaleMatrix);
    if (!makeRotationMatrix(rotation, rotationMatrix)) {
        Node.transformValid = false;
        return Node;
    }
    multiplyMatrix(rotationMatrix, scaleMatrix, rotationScale);
    multiplyMatrix(translationMatrix, rotationScale, Node.matrix);
    return Node;
}

std::pmr::vector<NodeDesc> parseNodes(const detail::JsonValue& root)
{
    std::pmr::vector<NodeDesc> nodes;
    const auto* nodeArray = root.find("nodes");
    if (nodeArray == nullptr || nodeArray->type != detail::JsonValue::Type::Array) {
        return nodes;
    }

    nodes.reserve(nodeArray->arrayValue.size());
    for (const auto& nodeValue : nodeArray->arrayValue) {
        nodes.push_back(parseNode(nodeValue));
    }
    return nodes;
}

bool parseSceneRoots(const detail::JsonValue& root,
    uint32_t nodeCount,
    std::pmr::vector<uint32_t>& roots,
    std::pmr::string& diagnostics)
{
    const auto* scenes = root.find("scenes");
    if (scenes == nullptr || scenes->type != detail::JsonValue::Type::Array) {
        diagnostics = "GLB static mesh requires a scene Node graph";
        return false;
    }
    const auto sceneIndex = jsonUInt(root.find("scene"), 0);
    if (sceneIndex >= scenes->arrayValue.size()) {
        diagnostics = "GLB default scene index is out of range";
        return false;
    }
    const auto* sceneNodes = scenes->arrayValue[sceneIndex].find("nodes");
    if (sceneNodes == nullptr || sceneNodes->type != detail::JsonValue::Type::Array) {
        diagnostics = "GLB default scene must list root nodes";
        return false;
    }
    for (const auto& Node : sceneNodes->arrayValue) {
        const auto nodeIndex = jsonUInt(&Node, UINT32_MAX);
        if (nodeIndex >= nodeCount) {
            diagnostics = "GLB scene root Node index is out of range";
            return false;
        }
        roots.push_back(nodeIndex);
    }
    if (roots.empty()) {
        diagnostics = "GLB default scene contains no root nodes";
        return false;
    }
    return true;
}

PrimitiveDesc parseFirstPrimitive(const std::pmr::string& json)
{
    PrimitiveDesc primitive;
    std::pmr::string meshesText;
    std::pmr::string meshObject;
    std::pmr::string primitivesText;
    std::pmr::string primitiveObject;
    if (!extractArrayTextAfterKey(json, "\"meshes\"", meshesText)
        || !extractFirstTopLevelObject(meshesText, meshObject)
        || !extractArrayTextAfterKey(meshObject, "\"primitives\"", primitivesText)
        || !extractFirstTopLevelObject(primitivesText, primitiveObject)) {
        return primitive;
    }

    primitive.positionAccessor = parseUIntAfterKey(primitiveObject, "\"POSITION\"", UINT32_MAX);
    primitive.normalAccessor = parseUIntAfterKey(primitiveObject, "\"NORMAL\"", UINT32_MAX);
    primitive.texcoordAccessor = parseUIntAfterKey(primitiveObject, "\"TEXCOORD_0\"", UINT32_MAX);
    primitive.tangentAccessor = parseUIntAfterKey(primitiveObject, "\"TANGENT\"", UINT32_MAX);
    primitive.indicesAccessor = parseUIntAfterKey(primitiveObject, "\"indices\"", UINT32_MAX);
    primitive.material = parseUIntAfterKey(primitiveObject, "\"material\"", 0);
    return primitive;
}

std::pmr::vector<BufferViewDesc> parseBufferViews(const std::pmr::string& json)
{
    std::pmr::vector<BufferViewDesc> views;
    for (const auto& object : extractObjectArray(json, "\"bufferViews\"")) {
        views.push_back(BufferViewDesc{
            parseUIntAfterKey(object, "\"buffer\"", 0),
            parseUIntAfterKey(object, "\"byteOffset\"", 0),
            parseUIntAfterKey(object, "\"byteLength\"", 0),
            parseUIntAfterKey(object, "\"byteStride\"", 0),
        });
    }
    return views;
}

std::pmr::vector<AccessorDesc> parseAccessors(const std::pmr::string& json)
{
    std::pmr::vector<AccessorDesc> accessors;
    for (const auto& object : extractObjectArray(json, "\"accessors\"")) {
        AccessorDesc desc;
        desc.bufferView = parseUIntAfterKey(object, "\"bufferView\"", UINT32_MAX);
        desc.byteOffset = parseUIntAfterKey(object, "\"byteOffset\"", 0);
        desc.componentType = parseUIntAfterKey(object, "\"componentType\"", 0);
        desc.count = parseUIntAfterKey(object, "\"count\"", 0);
        desc.type = parseStringAfterKey(object, "\"type\"");
        desc.normalized = object.find("\"normalized\"") != std::pmr::string::npos;
        accessors.push_back(std::move(desc));
    }
    return accessors;
}

float readF32(const std::pmr::string& bytes, const std::size_t offset)
{
    float value = 0.0f;
    std::memcpy(&value, bytes.data() + offset, sizeof(float));
    return value;
}

uint32_t readIndex(
    const std::pmr::string& bytes, const std::size_t offset, const uint32_t componentType)
{
    if (componentType == 5123) {
        uint16_t value = 0;
        std::memcpy(&value, bytes.data() + offset, sizeof(uint16_t));
        return value;
    }
    if (componentType == 5125) {
        uint32_t value = 0;
        std::memcpy(&value, bytes.data() + offset, sizeof(uint32_t));
        return value;
    }
    return UINT32_MAX;
}

uint32_t componentByteSize(const uint32_t componentType)
{
    switch (componentType) {
    case 5120:
        return 1u; // BYTE
    case 5121:
        return 1u; // UNSIGNED_BYTE
    case 5122:
        return 2u; // SHORT
    case 5123:
        return 2u; // UNSIGNED_SHORT
    case 5125:
        return 4u; // UNSIGNED_INT
    case 5126:
        return 4u; // FLOAT
    default:
        return 0u;
    }
}

bool readAccessorToFloats(const std::pmr::string& bin,
    const std::pmr::vector<BufferViewDesc>& views,
    const AccessorDesc& accessor,
    std::pmr::vector<float>& out,
    const uint32_t componentsPerElement)
{
    if (accessor.bufferView >= views.size()) {
        return false;
    }
    const auto compSize = componentByteSize(accessor.componentType);
    if (compSize == 0u) {
        return false;
    }
    const auto elementSize = compSize * componentsPerElement;
    const auto& view = views[accessor.bufferView];
    const auto stride = view.byteStride == 0 ? elementSize : view.byteStride;
    const auto base = view.byteOffset + accessor.byteOffset;
    if (base + stride * (accessor.count - 1u) + elementSize > bin.size()) {
        return false;
    }

    out.resize(accessor.count * componentsPerElement);
    for (uint32_t index = 0u; index < accessor.count; ++index) {
        const auto offset = base + stride * index;
        for (uint32_t comp = 0u; comp < componentsPerElement; ++comp) {
            const auto src = offset + comp * compSize;
            float value = 0.0f;
            switch (accessor.componentType) {
            case 5126: // FLOAT
                value = readF32(bin, src);
                break;
            case 5122: { // SHORT (signed)
                int16_t raw;
                std::memcpy(&raw, bin.data() + src, sizeof(raw));
                value = accessor.normalized ? std::max(static_cast<float>(raw) / 32767.0f, -1.0f)
                                            : static_cast<float>(raw);
                break;
            }
            case 5123: { // UNSIGNED_SHORT
                uint16_t raw;
                std::memcpy(&raw, bin.data() + src, sizeof(raw));
                value = accessor.normalized ? raw / 65535.0f : static_cast<float>(raw);
                break;
            }
            case 5120: { // BYTE (signed)
                int8_t raw;
                std::memcpy(&raw, bin.data() + src, sizeof(raw));
                value = accessor.normalized ? std::max(static_cast<float>(raw) / 127.0f, -1.0f)
                                            : static_cast<float>(raw);
                break;
            }
            case 5121: { // UNSIGNED_BYTE
                uint8_t raw;
                std::memcpy(&raw, bin.data() + src, sizeof(raw));
                value = accessor.normalized ? raw / 255.0f : static_cast<float>(raw);
                break;
            }
            case 5125: // UNSIGNED_INT
                uint32_t raw;
                std::memcpy(&raw, bin.data() + src, sizeof(raw));
                value = static_cast<float>(raw);
                break;
            default:
                return false;
            }
            out[index * componentsPerElement + comp] = value;
        }
    }
    return true;
}

bool readVec3Accessor(const std::pmr::string& bin,
    const std::pmr::vector<BufferViewDesc>& views,
    const AccessorDesc& accessor,
    std::pmr::vector<float>& out)
{
    if (accessor.type != "VEC3") {
        return false;
    }
    return readAccessorToFloats(bin, views, accessor, out, 3u);
}

bool readVec2Accessor(const std::pmr::string& bin,
    const std::pmr::vector<BufferViewDesc>& views,
    const AccessorDesc& accessor,
    std::pmr::vector<float>& out)
{
    if (accessor.type != "VEC2") {
        return false;
    }
    return readAccessorToFloats(bin, views, accessor, out, 2u);
}

bool readVec4Accessor(const std::pmr::string& bin,
    const std::pmr::vector<BufferViewDesc>& views,
    const AccessorDesc& accessor,
    std::pmr::vector<float>& out)
{
    if (accessor.type != "VEC4") {
        return false;
    }
    return readAccessorToFloats(bin, views, accessor, out, 4u);
}

bool readIndexAccessor(const std::pmr::string& bin,
    const std::pmr::vector<BufferViewDesc>& views,
    const AccessorDesc& accessor,
    std::pmr::vector<uint32_t>& out)
{
    if (accessor.bufferView >= views.size()) {
        return false;
    }
    const auto componentSize = accessor.componentType == 5123 ? 2u
        : accessor.componentType == 5125                      ? 4u
                                                              : 0u;
    if (componentSize == 0 || accessor.type != "SCALAR") {
        return false;
    }
    const auto& view = views[accessor.bufferView];
    const auto base = view.byteOffset + accessor.byteOffset;
    if (base + componentSize * accessor.count > bin.size()) {
        return false;
    }
    out.resize(accessor.count);
    for (uint32_t index = 0; index < accessor.count; ++index) {
        out[index] = readIndex(bin, base + componentSize * index, accessor.componentType);
    }
    return true;
}

bool appendPrimitiveToMesh(const std::pmr::string& bin,
    const std::pmr::vector<BufferViewDesc>& bufferViews,
    const std::pmr::vector<AccessorDesc>& accessors,
    const PrimitiveDesc& primitive,
    const float* transform,
    MeshAsset& mesh,
    std::pmr::string& diagnostics)
{
    if (primitive.mode != 4u) {
        diagnostics = "GLB static mesh only supports triangle primitives";
        return false;
    }
    if (primitive.positionAccessor == UINT32_MAX || primitive.indicesAccessor == UINT32_MAX
        || primitive.positionAccessor >= accessors.size()
        || primitive.indicesAccessor >= accessors.size()) {
        diagnostics = "GLB primitive accessor index is out of range";
        return false;
    }

    std::pmr::vector<float> positions;
    std::pmr::vector<float> normals;
    std::pmr::vector<float> uvs;
    std::pmr::vector<float> tangents;
    std::pmr::vector<uint32_t> indices;
    if (!readVec3Accessor(bin, bufferViews, accessors[primitive.positionAccessor], positions)) {
        diagnostics = "POSITION accessor must be float3 binary data";
        return false;
    }
    if (primitive.normalAccessor != UINT32_MAX) {
        if (primitive.normalAccessor >= accessors.size()
            || !readVec3Accessor(bin, bufferViews, accessors[primitive.normalAccessor], normals)
            || normals.size() != positions.size()) {
            diagnostics = "NORMAL accessor must match POSITION count";
            return false;
        }
    }
    if (primitive.texcoordAccessor != UINT32_MAX) {
        if (primitive.texcoordAccessor >= accessors.size()
            || !readVec2Accessor(bin, bufferViews, accessors[primitive.texcoordAccessor], uvs)
            || uvs.size() / 2u != positions.size() / 3u) {
            diagnostics = "TEXCOORD_0 accessor must match POSITION count";
            return false;
        }
    }
    if (primitive.tangentAccessor != UINT32_MAX) {
        if (primitive.tangentAccessor >= accessors.size()
            || !readVec4Accessor(bin, bufferViews, accessors[primitive.tangentAccessor], tangents)
            || tangents.size() / 4u != positions.size() / 3u) {
            diagnostics = "TANGENT accessor must match POSITION count";
            return false;
        }
    }
    if (!readIndexAccessor(bin, bufferViews, accessors[primitive.indicesAccessor], indices)) {
        diagnostics = "indices accessor must be scalar u16/u32 binary data";
        return false;
    }
    if (indices.size() % 3u != 0u) {
        diagnostics = "triangle primitive index count must be a multiple of three";
        return false;
    }

    const auto baseVertex = static_cast<uint32_t>(mesh.vertices.size());
    const auto firstIndex = static_cast<uint32_t>(mesh.indices.size());
    const auto vertexCount = positions.size() / 3u;
    mesh.vertices.reserve(mesh.vertices.size() + vertexCount);
    for (std::size_t index = 0; index < vertexCount; ++index) {
        MeshVertex vertex;
        const float position[3]{
            positions[index * 3u + 0u],
            positions[index * 3u + 1u],
            positions[index * 3u + 2u],
        };
        transformPoint(transform, position, vertex.position);
        if (!normals.empty()) {
            const float normal[3]{
                normals[index * 3u + 0u],
                normals[index * 3u + 1u],
                normals[index * 3u + 2u],
            };
            transformVector(transform, normal, vertex.normal);
        }
        if (!uvs.empty()) {
            vertex.uv[0] = uvs[index * 2u + 0u];
            vertex.uv[1] = uvs[index * 2u + 1u];
        }
        if (!tangents.empty()) {
            const float tangent[3]{
                tangents[index * 4u + 0u],
                tangents[index * 4u + 1u],
                tangents[index * 4u + 2u],
            };
            transformVector(transform, tangent, vertex.tangent);
            vertex.tangent[3] = tangents[index * 4u + 3u];
        }
        mesh.vertices.push_back(vertex);
    }

    mesh.indices.reserve(mesh.indices.size() + indices.size());
    for (const auto index : indices) {
        if (index >= vertexCount) {
            diagnostics = "GLB primitive index references vertex outside accessor";
            return false;
        }
        mesh.indices.push_back(baseVertex + index);
    }
    mesh.submeshes.push_back(Submesh{
        firstIndex,
        static_cast<uint32_t>(indices.size()),
        primitive.material,
    });
    if (tangents.empty()
        && !generateTangentsForIndexedMeshRange(mesh,
            {baseVertex,
                static_cast<uint32_t>(vertexCount),
                firstIndex,
                static_cast<uint32_t>(indices.size())})) {
        diagnostics = "GLB primitive tangent generation failed";
        return false;
    }
    return true;
}

bool appendMeshNode(const std::pmr::string& bin,
    const std::pmr::vector<BufferViewDesc>& bufferViews,
    const std::pmr::vector<AccessorDesc>& accessors,
    const std::pmr::vector<MeshDesc>& meshes,
    const std::pmr::vector<NodeDesc>& nodes,
    uint32_t nodeIndex,
    const float* parentTransform,
    std::pmr::vector<uint32_t>& recursionStack,
    MeshAsset& mesh,
    std::pmr::string& diagnostics)
{
    if (nodeIndex >= nodes.size()) {
        diagnostics = "GLB Node index is out of range";
        return false;
    }
    if (std::find(recursionStack.begin(), recursionStack.end(), nodeIndex)
        != recursionStack.end()) {
        diagnostics = "GLB Node graph contains a cycle";
        return false;
    }
    const auto& Node = nodes[nodeIndex];
    if (!Node.transformValid) {
        diagnostics = "GLB Node transform is invalid";
        return false;
    }

    float worldTransform[16]{};
    multiplyMatrix(parentTransform, Node.matrix, worldTransform);
    recursionStack.push_back(nodeIndex);
    if (Node.mesh != UINT32_MAX) {
        if (Node.mesh >= meshes.size()) {
            diagnostics = "GLB Node mesh index is out of range";
            return false;
        }
        const auto& sourceMesh = meshes[Node.mesh];
        if (sourceMesh.primitives.empty()) {
            diagnostics = "GLB mesh contains no primitives";
            return false;
        }
        for (const auto& primitive : sourceMesh.primitives) {
            if (!appendPrimitiveToMesh(
                    bin, bufferViews, accessors, primitive, worldTransform, mesh, diagnostics)) {
                return false;
            }
        }
    }
    for (const auto child : Node.children) {
        if (!appendMeshNode(bin,
                bufferViews,
                accessors,
                meshes,
                nodes,
                child,
                worldTransform,
                recursionStack,
                mesh,
                diagnostics)) {
            return false;
        }
    }
    recursionStack.pop_back();
    return true;
}

} // namespace

StaticMeshImportResult importStaticMeshFromGlbFile(const std::filesystem::path& path)
{
    std::pmr::string bytes;
    if (!readFile(path, bytes)) {
        return fail("failed to read GLB file");
    }

    GlbChunks chunks;
    std::pmr::string diagnostics;
    if (!readGlbChunks(bytes, chunks, diagnostics)) {
        return fail(diagnostics);
    }

    detail::JsonValue root;
    if (!detail::parseJson(chunks.json, root)) {
        return fail("GLB JSON could not be parsed");
    }

    const auto bufferViews = parseBufferViews(chunks.json);
    const auto accessors = parseAccessors(chunks.json);
    const auto meshes = parseMeshes(root);
    const auto nodes = parseNodes(root);
    if (meshes.empty()) {
        return fail("GLB static mesh requires at least one mesh");
    }
    if (nodes.empty()) {
        return fail("GLB static mesh requires scene nodes");
    }

    std::pmr::vector<uint32_t> sceneRoots;
    if (!parseSceneRoots(root, static_cast<uint32_t>(nodes.size()), sceneRoots, diagnostics)) {
        return fail(diagnostics);
    }

    MeshAsset mesh;
    float identity[16]{};
    setIdentity(identity);
    for (const auto rootNode : sceneRoots) {
        std::pmr::vector<uint32_t> recursionStack;
        if (!appendMeshNode(chunks.bin,
                bufferViews,
                accessors,
                meshes,
                nodes,
                rootNode,
                identity,
                recursionStack,
                mesh,
                diagnostics)) {
            return fail(diagnostics);
        }
    }
    if (!mesh.validForIndexedDraw()) {
        return fail("imported GLB mesh is not valid for indexed draw");
    }

    StaticMeshImportResult result;
    result.success = true;
    result.mesh = std::move(mesh);
    const auto materialAssets = detail::parseImportedGlbMaterialAssets(
        root, chunks.bin, detail::parseGlbBufferViewRanges(root));
    if (!materialAssets.success)
        return fail(materialAssets.diagnostics);
    result.materials = std::move(materialAssets.materials);
    result.textures = std::move(materialAssets.textures);
    result.images = std::move(materialAssets.images);
    return result;
}

// ─── Named variant import ────────────────────────────────────────────
//
// Walks the GLB node tree and extracts each mesh-bearing named node as a
// separate NamedVariant. Each variant gets its own MeshAsset (not merged),
// preserving the node name and world transform. COLOR_0 (wind weights) is
// read where present and stored as a flag (the raw wind data is available
// via a parallel read; the FoliageVertex expansion happens in the resource
// loader, not here, to keep this function focused on extraction).
//
// Nodes without a name are skipped (they are typically intermediate
// transform nodes). Only named mesh-bearing nodes become variants.

namespace {

/// Append a single primitive to a variant's mesh, reading COLOR_0 if present.
/// Returns true if the primitive had COLOR_0 data.
bool appendPrimitiveToVariant(const std::pmr::string& bin,
    const std::pmr::vector<BufferViewDesc>& bufferViews,
    const std::pmr::vector<AccessorDesc>& accessors,
    const PrimitiveDesc& primitive,
    const float* transform,
    MeshAsset& mesh,
    std::pmr::vector<float>& windWeights,
    bool& hasColor0,
    std::pmr::string& diagnostics)
{
    if (primitive.mode != 4u) {
        diagnostics = "named variant import only supports triangle primitives";
        return false;
    }
    if (primitive.positionAccessor == UINT32_MAX || primitive.indicesAccessor == UINT32_MAX
        || primitive.positionAccessor >= accessors.size()
        || primitive.indicesAccessor >= accessors.size()) {
        diagnostics = "named variant primitive accessor index is out of range";
        return false;
    }

    std::pmr::vector<float> positions;
    std::pmr::vector<float> normals;
    std::pmr::vector<float> uvs;
    std::pmr::vector<float> tangents;
    std::pmr::vector<float> colors;
    std::pmr::vector<uint32_t> indices;

    if (!readVec3Accessor(bin, bufferViews, accessors[primitive.positionAccessor], positions)) {
        diagnostics = "POSITION accessor must be float3 binary data";
        return false;
    }
    if (primitive.normalAccessor != UINT32_MAX) {
        if (primitive.normalAccessor >= accessors.size()
            || !readVec3Accessor(bin, bufferViews, accessors[primitive.normalAccessor], normals)
            || normals.size() != positions.size()) {
            diagnostics = "NORMAL accessor must match POSITION count";
            return false;
        }
    }
    if (primitive.texcoordAccessor != UINT32_MAX) {
        if (primitive.texcoordAccessor >= accessors.size()
            || !readVec2Accessor(bin, bufferViews, accessors[primitive.texcoordAccessor], uvs)
            || uvs.size() / 2u != positions.size() / 3u) {
            diagnostics = "TEXCOORD_0 accessor must match POSITION count";
            return false;
        }
    }
    if (primitive.tangentAccessor != UINT32_MAX) {
        if (primitive.tangentAccessor >= accessors.size()
            || !readVec4Accessor(bin, bufferViews, accessors[primitive.tangentAccessor], tangents)
            || tangents.size() / 4u != positions.size() / 3u) {
            diagnostics = "TANGENT accessor must match POSITION count";
            return false;
        }
    }
    // Read COLOR_0 (wind weights) if present.
    if (primitive.color0Accessor != UINT32_MAX && primitive.color0Accessor < accessors.size()) {
        if (readVec4Accessor(bin, bufferViews, accessors[primitive.color0Accessor], colors)
            && colors.size() / 4u == positions.size() / 3u) {
            hasColor0 = true;
        }
        // If COLOR_0 read fails or mismatches, we just skip wind data.
    }
    if (!readIndexAccessor(bin, bufferViews, accessors[primitive.indicesAccessor], indices)) {
        diagnostics = "indices accessor must be scalar u16/u32 binary data";
        return false;
    }
    if (indices.size() % 3u != 0u) {
        diagnostics = "triangle primitive index count must be a multiple of three";
        return false;
    }

    const auto baseVertex = static_cast<uint32_t>(mesh.vertices.size());
    const auto firstIndex = static_cast<uint32_t>(mesh.indices.size());
    const auto vertexCount = positions.size() / 3u;
    mesh.vertices.reserve(mesh.vertices.size() + vertexCount);
    for (std::size_t index = 0; index < vertexCount; ++index) {
        MeshVertex vertex;
        const float position[3]{
            positions[index * 3u + 0u],
            positions[index * 3u + 1u],
            positions[index * 3u + 2u],
        };
        transformPoint(transform, position, vertex.position);
        if (!normals.empty()) {
            const float normal[3]{
                normals[index * 3u + 0u],
                normals[index * 3u + 1u],
                normals[index * 3u + 2u],
            };
            transformVector(transform, normal, vertex.normal);
        }
        if (!uvs.empty()) {
            vertex.uv[0] = uvs[index * 2u + 0u];
            vertex.uv[1] = uvs[index * 2u + 1u];
        }
        if (!tangents.empty()) {
            const float tangent[3]{
                tangents[index * 4u + 0u],
                tangents[index * 4u + 1u],
                tangents[index * 4u + 2u],
            };
            transformVector(transform, tangent, vertex.tangent);
            vertex.tangent[3] = tangents[index * 4u + 3u];
        }
        mesh.vertices.push_back(vertex);
        // Store per-vertex COLOR_0 wind weights (RGBA stride) when present.
        // This keeps windWeights.size() == vertices.size() * 4 for the variant.
        if (hasColor0 && !colors.empty()) {
            windWeights.push_back(colors[index * 4u + 0u]);
            windWeights.push_back(colors[index * 4u + 1u]);
            windWeights.push_back(colors[index * 4u + 2u]);
            windWeights.push_back(colors[index * 4u + 3u]);
        }
    }
    mesh.indices.reserve(mesh.indices.size() + indices.size());
    for (const auto index : indices) {
        if (index >= vertexCount) {
            diagnostics = "named variant index references vertex outside accessor";
            return false;
        }
        mesh.indices.push_back(baseVertex + index);
    }
    mesh.submeshes.push_back(Submesh{
        firstIndex,
        static_cast<uint32_t>(indices.size()),
        primitive.material,
    });
    if (tangents.empty()
        && !generateTangentsForIndexedMeshRange(mesh,
            {baseVertex,
                static_cast<uint32_t>(vertexCount),
                firstIndex,
                static_cast<uint32_t>(indices.size())})) {
        diagnostics = "GLB variant primitive tangent generation failed";
        return false;
    }
    return true;
}

/// Recursively walk the node tree, extracting named mesh-bearing nodes
/// as separate NamedVariant entries.
bool extractNamedVariants(const std::pmr::string& bin,
    const std::pmr::vector<BufferViewDesc>& bufferViews,
    const std::pmr::vector<AccessorDesc>& accessors,
    const std::pmr::vector<MeshDesc>& meshes,
    const std::pmr::vector<NodeDesc>& nodes,
    uint32_t nodeIndex,
    const float* parentTransform,
    NamedVariantGeometrySpace geometrySpace,
    std::pmr::vector<uint32_t>& recursionStack,
    std::pmr::vector<NamedVariant>& variants,
    std::pmr::string& diagnostics)
{
    if (nodeIndex >= nodes.size()) {
        diagnostics = "GLB Node index is out of range";
        return false;
    }
    if (std::find(recursionStack.begin(), recursionStack.end(), nodeIndex)
        != recursionStack.end()) {
        diagnostics = "GLB Node graph contains a cycle";
        return false;
    }
    const auto& node = nodes[nodeIndex];
    if (!node.transformValid) {
        diagnostics = "GLB Node transform is invalid";
        return false;
    }

    float worldTransform[16]{};
    multiplyMatrix(parentTransform, node.matrix, worldTransform);
    recursionStack.push_back(nodeIndex);

    // Extract this node as a variant if it has a name and a mesh.
    if (node.mesh != UINT32_MAX && !node.name.empty()) {
        if (node.mesh >= meshes.size()) {
            diagnostics = "GLB Node mesh index is out of range";
            return false;
        }
        const auto& sourceMesh = meshes[node.mesh];
        if (!sourceMesh.primitives.empty()) {
            NamedVariant variant;
            variant.name = node.name;
            std::memcpy(variant.worldTransform, worldTransform, sizeof(float) * 16);
            float geometryTransform[16];
            std::memcpy(geometryTransform, worldTransform, sizeof(geometryTransform));
            if (geometrySpace == NamedVariantGeometrySpace::RootRelative) {
                // GLB parser-private column-major transform: preserve its
                // linear part and remove only the declared node origin.
                geometryTransform[12] = geometryTransform[13] = geometryTransform[14] = 0.0f;
            }
            bool hasColor0 = false;
            for (const auto& primitive : sourceMesh.primitives) {
                if (!appendPrimitiveToVariant(bin,
                        bufferViews,
                        accessors,
                        primitive,
                        geometryTransform,
                        variant.mesh,
                        variant.windWeights,
                        hasColor0,
                        diagnostics)) {
                    return false;
                }
                variant.materialSlots.push_back(primitive.material);
            }
            if (variant.mesh.validForIndexedDraw()) {
                variant.hasWindData = hasColor0;
                variants.push_back(std::move(variant));
            }
        }
    }

    // Recurse into children.
    for (const auto child : node.children) {
        if (!extractNamedVariants(bin,
                bufferViews,
                accessors,
                meshes,
                nodes,
                child,
                worldTransform,
                geometrySpace,
                recursionStack,
                variants,
                diagnostics)) {
            return false;
        }
    }

    recursionStack.pop_back();
    return true;
}

} // anonymous namespace

NamedVariantImportResult importNamedVariantsFromGlbFile(
    const std::filesystem::path& path, NamedVariantGeometrySpace geometrySpace)
{
    if (geometrySpace != NamedVariantGeometrySpace::Scene
        && geometrySpace != NamedVariantGeometrySpace::RootRelative) {
        NamedVariantImportResult fail;
        fail.diagnostics = "ASSET-NAMED-VARIANT-SPACE-INVALID";
        return fail;
    }
    std::pmr::string bytes;
    if (!readFile(path, bytes)) {
        NamedVariantImportResult fail;
        fail.diagnostics = "failed to read GLB file";
        return fail;
    }

    GlbChunks chunks;
    std::pmr::string diagnostics;
    if (!readGlbChunks(bytes, chunks, diagnostics)) {
        NamedVariantImportResult fail;
        fail.diagnostics = diagnostics;
        return fail;
    }

    detail::JsonValue root;
    if (!detail::parseJson(chunks.json, root)) {
        NamedVariantImportResult fail;
        fail.diagnostics = "GLB JSON could not be parsed";
        return fail;
    }

    const auto bufferViews = parseBufferViews(chunks.json);
    const auto accessors = parseAccessors(chunks.json);
    const auto meshes = parseMeshes(root);
    const auto nodes = parseNodes(root);
    if (meshes.empty()) {
        NamedVariantImportResult fail;
        fail.diagnostics = "GLB named variant import requires at least one mesh";
        return fail;
    }
    if (nodes.empty()) {
        NamedVariantImportResult fail;
        fail.diagnostics = "GLB named variant import requires scene nodes";
        return fail;
    }

    std::pmr::vector<uint32_t> sceneRoots;
    if (!parseSceneRoots(root, static_cast<uint32_t>(nodes.size()), sceneRoots, diagnostics)) {
        NamedVariantImportResult fail;
        fail.diagnostics = diagnostics;
        return fail;
    }

    std::pmr::vector<NamedVariant> variants;
    float identity[16]{};
    setIdentity(identity);
    for (const auto rootNode : sceneRoots) {
        std::pmr::vector<uint32_t> recursionStack;
        if (!extractNamedVariants(chunks.bin,
                bufferViews,
                accessors,
                meshes,
                nodes,
                rootNode,
                identity,
                geometrySpace,
                recursionStack,
                variants,
                diagnostics)) {
            NamedVariantImportResult fail;
            fail.diagnostics = diagnostics;
            return fail;
        }
    }

    NamedVariantImportResult result;
    result.success = !variants.empty();
    result.variants = std::move(variants);
    if (!result.success) {
        result.diagnostics = "no named mesh-bearing nodes found in GLB file";
    }
    const auto materialAssets = detail::parseImportedGlbMaterialAssets(
        root, chunks.bin, detail::parseGlbBufferViewRanges(root));
    if (!materialAssets.success) {
        result.success = false;
        result.diagnostics = materialAssets.diagnostics;
        return result;
    }
    result.materials = std::move(materialAssets.materials);
    result.textures = std::move(materialAssets.textures);
    result.images = std::move(materialAssets.images);
    return result;
}

} // namespace arp::assets

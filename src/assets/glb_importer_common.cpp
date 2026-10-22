#include "assets/glb_importer_common.h"

#include "assets/glb_static_mesh_importer.h"
#include "core/encoding/base64.h"
#include "core/memory/cpu_allocator.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace arp::assets::detail {

namespace {

constexpr uint32_t glbMagic = 0x46546C67u;
constexpr uint32_t jsonChunkType = 0x4E4F534Au;
constexpr uint32_t binChunkType = 0x004E4942u;

class JsonParser {
public:
    explicit JsonParser(const std::pmr::string& text, JsonParseLimits limits = {})
        : text_(text), cursor_(text_.c_str()), limits_(limits)
    {}

    bool parse(JsonValue& out)
    {
        if (limits_.strictAscii
            && (text_.find('\0') != std::pmr::string::npos || !limits_.maxDepth
                || !limits_.maxValues))
            return false;
        skipWhitespace();
        if (!parseValue(out)) {
            return false;
        }
        skipWhitespace();
        return *cursor_ == '\0';
    }

private:
    bool parseValue(JsonValue& out)
    {
        if (limits_.strictAscii && (depth_ >= limits_.maxDepth || values_ >= limits_.maxValues))
            return false;
        ++values_;
        ++depth_;

        struct DepthScope final {
            std::uint32_t& value;

            ~DepthScope() { --value; }
        } depthScope{depth_};

        skipWhitespace();
        if (*cursor_ == '{') {
            return parseObject(out);
        }
        if (*cursor_ == '[') {
            return parseArray(out);
        }
        if (*cursor_ == '"') {
            out.type = JsonValue::Type::String;
            return parseString(out.stringValue);
        }
        if (*cursor_ == 't') {
            out.type = JsonValue::Type::Bool;
            out.boolValue = true;
            return consumeLiteral("true");
        }
        if (*cursor_ == 'f') {
            out.type = JsonValue::Type::Bool;
            out.boolValue = false;
            return consumeLiteral("false");
        }
        if (*cursor_ == 'n') {
            out.type = JsonValue::Type::Null;
            return consumeLiteral("null");
        }
        return parseNumber(out);
    }

    bool parseObject(JsonValue& out)
    {
        if (*cursor_ != '{') {
            return false;
        }
        ++cursor_;
        out.type = JsonValue::Type::Object;
        out.objectValue.clear();
        skipWhitespace();
        if (*cursor_ == '}') {
            ++cursor_;
            return true;
        }

        for (;;) {
            std::pmr::string key;
            if (!parseString(key)) {
                return false;
            }
            if (limits_.strictAscii
                && std::any_of(
                    out.objectValue.begin(), out.objectValue.end(), [&](const auto& item) {
                        return item.first == key;
                    }))
                return false;
            skipWhitespace();
            if (*cursor_ != ':') {
                return false;
            }
            ++cursor_;
            JsonValue value;
            if (!parseValue(value)) {
                return false;
            }
            out.objectValue.push_back({std::move(key), std::move(value)});
            skipWhitespace();
            if (*cursor_ == '}') {
                ++cursor_;
                return true;
            }
            if (*cursor_ != ',') {
                return false;
            }
            ++cursor_;
            skipWhitespace();
        }
    }

    bool parseArray(JsonValue& out)
    {
        if (*cursor_ != '[') {
            return false;
        }
        ++cursor_;
        out.type = JsonValue::Type::Array;
        out.arrayValue.clear();
        skipWhitespace();
        if (*cursor_ == ']') {
            ++cursor_;
            return true;
        }

        for (;;) {
            JsonValue value;
            if (!parseValue(value)) {
                return false;
            }
            out.arrayValue.push_back(std::move(value));
            skipWhitespace();
            if (*cursor_ == ']') {
                ++cursor_;
                return true;
            }
            if (*cursor_ != ',') {
                return false;
            }
            ++cursor_;
            skipWhitespace();
        }
    }

    bool parseString(std::pmr::string& out)
    {
        if (*cursor_ != '"') {
            return false;
        }
        ++cursor_;
        out.clear();
        while (*cursor_ != '\0') {
            const char ch = *cursor_++;
            if (limits_.strictAscii
                && (static_cast<unsigned char>(ch) < 32u || static_cast<unsigned char>(ch) > 126u))
                return false;
            if (ch == '"') {
                return true;
            }
            if (ch != '\\') {
                out.push_back(ch);
                continue;
            }
            if (*cursor_ == '\0')
                return false;
            const char escaped = *cursor_++;
            switch (escaped) {
            case '"':
            case '\\':
            case '/':
                out.push_back(escaped);
                break;
            case 'b':
                out.push_back('\b');
                break;
            case 'f':
                out.push_back('\f');
                break;
            case 'n':
                out.push_back('\n');
                break;
            case 'r':
                out.push_back('\r');
                break;
            case 't':
                out.push_back('\t');
                break;
            case 'u':
                if (limits_.strictAscii)
                    return false;
                for (int i = 0; i < 4 && std::isxdigit(static_cast<unsigned char>(*cursor_)); ++i) {
                    ++cursor_;
                }
                out.push_back('?');
                break;
            default:
                return false;
            }
        }
        return false;
    }

    bool parseNumber(JsonValue& out)
    {
        const char* expectedEnd = cursor_;
        if (limits_.strictAscii) {
            if (*expectedEnd == '-')
                ++expectedEnd;
            if (*expectedEnd == '0')
                ++expectedEnd;
            else {
                if (*expectedEnd < '1' || *expectedEnd > '9')
                    return false;
                while (*expectedEnd >= '0' && *expectedEnd <= '9')
                    ++expectedEnd;
            }
            if (*expectedEnd == '.') {
                ++expectedEnd;
                if (*expectedEnd < '0' || *expectedEnd > '9')
                    return false;
                while (*expectedEnd >= '0' && *expectedEnd <= '9')
                    ++expectedEnd;
            }
            if (*expectedEnd == 'e' || *expectedEnd == 'E') {
                ++expectedEnd;
                if (*expectedEnd == '+' || *expectedEnd == '-')
                    ++expectedEnd;
                if (*expectedEnd < '0' || *expectedEnd > '9')
                    return false;
                while (*expectedEnd >= '0' && *expectedEnd <= '9')
                    ++expectedEnd;
            }
        }
        char* end = nullptr;
        const auto value = std::strtod(cursor_, &end);
        if (end == cursor_
            || (limits_.strictAscii && (end != expectedEnd || !std::isfinite(value)))) {
            return false;
        }
        cursor_ = end;
        out.type = JsonValue::Type::Number;
        out.numberValue = value;
        return true;
    }

    bool consumeLiteral(const char* literal)
    {
        const auto length = std::strlen(literal);
        if (std::strncmp(cursor_, literal, length) != 0) {
            return false;
        }
        cursor_ += length;
        return true;
    }

    void skipWhitespace()
    {
        while (limits_.strictAscii
                ? (*cursor_ == ' ' || *cursor_ == '\t' || *cursor_ == '\n' || *cursor_ == '\r')
                : std::isspace(static_cast<unsigned char>(*cursor_))) {
            ++cursor_;
        }
    }

    std::pmr::string text_;
    const char* cursor_ = nullptr;
    JsonParseLimits limits_;
    std::uint32_t depth_ = 0, values_ = 0;
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

uint32_t jsonUInt(const JsonValue* value, const uint32_t fallback)
{
    if (value == nullptr || value->type != JsonValue::Type::Number || value->numberValue < 0.0) {
        return fallback;
    }
    return static_cast<uint32_t>(value->numberValue);
}

float jsonFloat(const JsonValue* value, const float fallback)
{
    if (value == nullptr || value->type != JsonValue::Type::Number) {
        return fallback;
    }
    return static_cast<float>(value->numberValue);
}

bool jsonBool(const JsonValue* value, const bool fallback)
{
    return value != nullptr && value->type == JsonValue::Type::Bool ? value->boolValue : fallback;
}

std::pmr::string jsonString(const JsonValue* value, const std::pmr::string& fallback = {})
{
    return value != nullptr && value->type == JsonValue::Type::String ? value->stringValue
                                                                      : fallback;
}

std::pmr::string indexedName(const char* prefix, const std::size_t index)
{
    return std::pmr::string(prefix) + arp::core::toPmrString(std::to_string(index));
}

void copyFloatArray(const JsonValue* value, float* out, const std::size_t count)
{
    if (value == nullptr || value->type != JsonValue::Type::Array
        || value->arrayValue.size() < count) {
        return;
    }
    for (std::size_t index = 0; index < count; ++index) {
        if (value->arrayValue[index].type == JsonValue::Type::Number) {
            out[index] = static_cast<float>(value->arrayValue[index].numberValue);
        }
    }
}

bool validFloatArray(const JsonValue* value, const std::size_t count)
{
    if (value == nullptr) {
        return true;
    }
    if (value->type != JsonValue::Type::Array || value->arrayValue.size() != count) {
        return false;
    }
    for (const auto& element : value->arrayValue) {
        if (element.type != JsonValue::Type::Number || !std::isfinite(element.numberValue)) {
            return false;
        }
    }
    return true;
}

bool validOptionalFiniteNumber(const JsonValue* value)
{
    return value == nullptr
        || (value->type == JsonValue::Type::Number && std::isfinite(value->numberValue));
}

uint32_t textureIndexFromInfo(const JsonValue* value)
{
    if (value == nullptr || value->type != JsonValue::Type::Object) {
        return invalidImportedTextureIndex;
    }
    return jsonUInt(value->find("index"), invalidImportedTextureIndex);
}

bool textureInfoFromJson(const JsonValue* value, uint32_t& textureIndex, uint32_t& coordinateSet)
{
    if (value == nullptr) {
        textureIndex = invalidImportedTextureIndex;
        coordinateSet = 0u;
        return true;
    }
    if (value->type != JsonValue::Type::Object) {
        return false;
    }
    const auto* index = value->find("index");
    if (index == nullptr || index->type != JsonValue::Type::Number
        || !std::isfinite(index->numberValue) || index->numberValue < 0.0
        || std::floor(index->numberValue) != index->numberValue
        || index->numberValue > static_cast<double>(UINT32_MAX)) {
        return false;
    }
    textureIndex = static_cast<uint32_t>(index->numberValue);
    const auto* coordinate = value->find("texCoord");
    if (coordinate == nullptr) {
        coordinateSet = 0u;
        return true;
    }
    if (coordinate->type != JsonValue::Type::Number || !std::isfinite(coordinate->numberValue)
        || coordinate->numberValue < 0.0
        || std::floor(coordinate->numberValue) != coordinate->numberValue
        || coordinate->numberValue > static_cast<double>(UINT32_MAX)) {
        return false;
    }
    coordinateSet = static_cast<uint32_t>(coordinate->numberValue);
    return true;
}

bool alphaModeFromJson(const JsonValue* value, ImportedAlphaMode& out)
{
    if (value == nullptr) {
        out = ImportedAlphaMode::Opaque;
        return true;
    }
    if (value->type != JsonValue::Type::String) {
        return false;
    }
    if (value->stringValue == "OPAQUE") {
        out = ImportedAlphaMode::Opaque;
        return true;
    }
    if (value->stringValue == "MASK") {
        out = ImportedAlphaMode::Mask;
        return true;
    }
    if (value->stringValue == "BLEND") {
        out = ImportedAlphaMode::Blend;
        return true;
    }
    return false;
}

} // namespace

const JsonValue* JsonValue::find(const char* key) const
{
    if (type != Type::Object) {
        return nullptr;
    }
    for (const auto& entry : objectValue) {
        if (entry.first == key) {
            return &entry.second;
        }
    }
    return nullptr;
}

bool parseJson(const std::pmr::string& text, JsonValue& out)
{
    JsonParser parser(text);
    return parser.parse(out);
}

bool parseJson(const std::pmr::string& text, JsonValue& out, JsonParseLimits limits)
{
    JsonParser parser(text, limits);
    return parser.parse(out);
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

std::pmr::vector<GlbBufferViewRange> parseGlbBufferViewRanges(const JsonValue& root)
{
    std::pmr::vector<GlbBufferViewRange> ranges;
    const auto* array = root.find("bufferViews");
    if (array == nullptr || array->type != JsonValue::Type::Array) {
        return ranges;
    }
    ranges.reserve(array->arrayValue.size());
    for (const auto& object : array->arrayValue) {
        ranges.push_back(GlbBufferViewRange{
            jsonUInt(object.find("byteOffset"), 0),
            jsonUInt(object.find("byteLength"), 0),
        });
    }
    return ranges;
}

ImportedGlbMaterialAssets parseImportedGlbMaterialAssets(const JsonValue& root,
    const std::pmr::string& bin,
    const std::pmr::vector<GlbBufferViewRange>& bufferViews)
{
    ImportedGlbMaterialAssets result;

    const auto* textures = root.find("textures");
    if (textures != nullptr && textures->type == JsonValue::Type::Array) {
        result.textures.reserve(textures->arrayValue.size());
        for (std::size_t index = 0; index < textures->arrayValue.size(); ++index) {
            const auto& object = textures->arrayValue[index];
            ImportedTextureAsset texture;
            texture.name = jsonString(object.find("name"), indexedName("Texture", index));
            texture.sourceImage = jsonUInt(object.find("source"), invalidImportedImageIndex);
            // Fallback: EXT_texture_webp stores source in extensions.EXT_texture_webp.source
            if (texture.sourceImage >= result.images.size()) {
                const auto* texExt = object.find("extensions");
                if (texExt != nullptr && texExt->type == JsonValue::Type::Object) {
                    const auto* webpExt = texExt->find("EXT_texture_webp");
                    if (webpExt != nullptr && webpExt->type == JsonValue::Type::Object) {
                        texture.sourceImage =
                            jsonUInt(webpExt->find("source"), invalidImportedImageIndex);
                    }
                }
            }
            texture.sampler = jsonUInt(object.find("sampler"), invalidImportedTextureIndex);
            const auto* samplers = root.find("samplers");
            if (texture.sampler != invalidImportedTextureIndex) {
                if (!samplers || samplers->type != JsonValue::Type::Array
                    || texture.sampler >= samplers->arrayValue.size())
                    throw std::runtime_error("GLTF-TEXTURE-SAMPLER-INVALID");
                const auto& sampler = samplers->arrayValue[texture.sampler];
                texture.wrapS = jsonUInt(sampler.find("wrapS"), 10497u);
                texture.wrapT = jsonUInt(sampler.find("wrapT"), 10497u);
            }
            result.textures.push_back(std::move(texture));
        }
    }

    const auto* images = root.find("images");
    if (images != nullptr && images->type == JsonValue::Type::Array) {
        result.images.reserve(images->arrayValue.size());
        for (std::size_t index = 0; index < images->arrayValue.size(); ++index) {
            const auto& object = images->arrayValue[index];
            ImportedImageAsset image;
            image.name = jsonString(object.find("name"), indexedName("Image", index));
            image.uri = jsonString(object.find("uri"));
            image.mimeType = jsonString(object.find("mimeType"));
            image.bufferView = jsonUInt(object.find("bufferView"), invalidImportedBufferViewIndex);
            const bool hasUri = !image.uri.empty();
            const bool hasBufferView = image.bufferView != invalidImportedBufferViewIndex;
            if (hasUri == hasBufferView) {
                result.success = false;
                result.diagnostics = "ASSET-GLB-IMAGE-SOURCE-INVALID";
                return result;
            }
            if (hasUri) {
                const std::string_view uri(image.uri.data(), image.uri.size());
                if (uri.substr(0u, 5u) != "data:") {
                    result.success = false;
                    result.diagnostics = "ASSET-GLB-IMAGE-URI-UNSUPPORTED";
                    return result;
                }
                const auto separator = uri.find(",");
                const auto header = uri.substr(0u, separator);
                const auto encoding = header.find(";base64");
                if (separator == std::string_view::npos || encoding == std::string_view::npos
                    || encoding + 7u != header.size() || encoding <= 5u
                    || !core::Base64::decode(uri.substr(separator + 1u), image.encodedBytes)) {
                    result.success = false;
                    result.diagnostics = "ASSET-GLB-IMAGE-DATA-URI-INVALID";
                    return result;
                }
                const auto mime = header.substr(5u, encoding - 5u);
                if (!image.mimeType.empty() && image.mimeType != mime) {
                    result.success = false;
                    result.diagnostics = "ASSET-GLB-IMAGE-MIME-CONFLICT";
                    return result;
                }
                image.mimeType.assign(mime.begin(), mime.end());
            } else if (image.bufferView < bufferViews.size()) {
                const auto& range = bufferViews[image.bufferView];
                if (range.byteOffset > bin.size()
                    || range.byteLength > bin.size() - range.byteOffset || range.byteLength == 0u) {
                    result.success = false;
                    result.diagnostics = "ASSET-GLB-IMAGE-SOURCE-INVALID";
                    return result;
                }
                const auto* begin = reinterpret_cast<const uint8_t*>(bin.data() + range.byteOffset);
                image.encodedBytes.assign(begin, begin + range.byteLength);
            } else {
                result.success = false;
                result.diagnostics = "ASSET-GLB-IMAGE-SOURCE-INVALID";
                return result;
            }
            result.images.push_back(std::move(image));
        }
    }

    const auto* materials = root.find("materials");
    if (materials != nullptr && materials->type == JsonValue::Type::Array) {
        result.materials.reserve(materials->arrayValue.size());
        for (std::size_t index = 0; index < materials->arrayValue.size(); ++index) {
            const auto& object = materials->arrayValue[index];
            if (object.type != JsonValue::Type::Object) {
                result.success = false;
                result.diagnostics = "ASSET-GLB-MATERIAL-OBJECT-INVALID";
                return result;
            }
            ImportedMaterialAsset material;
            material.hasCompleteMaterialImportMetadata = true;
            material.name = jsonString(object.find("name"), indexedName("Material", index));
            const auto* doubleSided = object.find("doubleSided");
            if (doubleSided != nullptr && doubleSided->type != JsonValue::Type::Bool) {
                result.success = false;
                result.diagnostics = "ASSET-GLB-MATERIAL-CORE-PARAMETER-INVALID";
                return result;
            }
            material.doubleSided = jsonBool(object.find("doubleSided"), false);
            if (!alphaModeFromJson(object.find("alphaMode"), material.alphaMode)) {
                result.success = false;
                result.diagnostics = "ASSET-GLB-MATERIAL-ALPHA-MODE-UNSUPPORTED";
                return result;
            }
            if (!validOptionalFiniteNumber(object.find("alphaCutoff"))
                || !validFloatArray(object.find("emissiveFactor"), 3u)) {
                result.success = false;
                result.diagnostics = "ASSET-GLB-MATERIAL-CORE-PARAMETER-INVALID";
                return result;
            }
            material.alphaCutoff = jsonFloat(object.find("alphaCutoff"), material.alphaCutoff);
            copyFloatArray(object.find("emissiveFactor"), material.emissiveFactor, 3);
            if (!textureInfoFromJson(object.find("normalTexture"),
                    material.normalTexture,
                    material.normalTextureCoordinateSet)) {
                result.success = false;
                result.diagnostics = "ASSET-GLB-MATERIAL-TEXTURE-INFO-INVALID";
                return result;
            }
            const auto* occlusionTexture = object.find("occlusionTexture");
            if (!textureInfoFromJson(occlusionTexture,
                    material.occlusionTexture,
                    material.occlusionTextureCoordinateSet)) {
                result.success = false;
                result.diagnostics = "ASSET-GLB-MATERIAL-TEXTURE-INFO-INVALID";
                return result;
            }
            if (occlusionTexture != nullptr && occlusionTexture->type == JsonValue::Type::Object) {
                material.occlusionStrength =
                    jsonFloat(occlusionTexture->find("strength"), material.occlusionStrength);
            }
            if (!textureInfoFromJson(object.find("emissiveTexture"),
                    material.emissiveTexture,
                    material.emissiveTextureCoordinateSet)) {
                result.success = false;
                result.diagnostics = "ASSET-GLB-MATERIAL-TEXTURE-INFO-INVALID";
                return result;
            }

            const auto* extras = object.find("extras");
            const auto* leaf = extras ? extras->find("arpLeafSurface") : nullptr;
            if (leaf) {
                const auto* version = leaf->find("contractVersion");
                const auto* weight = leaf->find("translucency");
                const auto* f0 = leaf->find("dielectricF0");
                const auto* saturation = leaf->find("translucentSaturation");
                const auto validNumber = [](const JsonValue* v, double maximum) {
                    return v && v->type == JsonValue::Type::Number && std::isfinite(v->numberValue)
                        && v->numberValue >= 0 && v->numberValue <= maximum;
                };
                if (leaf->type != JsonValue::Type::Object || !validNumber(version, 1)
                    || version->numberValue != 1 || !validNumber(weight, 1) || !validNumber(f0, 1)
                    || !validNumber(saturation, 2)) {
                    result.success = false;
                    result.diagnostics = "ASSET-GLB-LEAF-SURFACE-CONTRACT-INVALID";
                    return result;
                }
                material.authoredLeafSurface = true;
                material.leafTranslucency = static_cast<float>(weight->numberValue);
                material.leafDielectricF0 = static_cast<float>(f0->numberValue);
                material.leafTranslucentSaturation = static_cast<float>(saturation->numberValue);
            }

            const auto* pbr = object.find("pbrMetallicRoughness");
            if (pbr != nullptr && pbr->type != JsonValue::Type::Object) {
                result.success = false;
                result.diagnostics = "ASSET-GLB-MATERIAL-PBR-OBJECT-INVALID";
                return result;
            }
            if (pbr != nullptr && pbr->type == JsonValue::Type::Object) {
                if (!validFloatArray(pbr->find("baseColorFactor"), 4u)
                    || !validOptionalFiniteNumber(pbr->find("metallicFactor"))
                    || !validOptionalFiniteNumber(pbr->find("roughnessFactor"))) {
                    result.success = false;
                    result.diagnostics = "ASSET-GLB-MATERIAL-CORE-PARAMETER-INVALID";
                    return result;
                }
                copyFloatArray(pbr->find("baseColorFactor"), material.baseColorFactor, 4);
                if (!textureInfoFromJson(pbr->find("baseColorTexture"),
                        material.baseColorTexture,
                        material.baseColorTextureCoordinateSet)) {
                    result.success = false;
                    result.diagnostics = "ASSET-GLB-MATERIAL-TEXTURE-INFO-INVALID";
                    return result;
                }
                if (!textureInfoFromJson(pbr->find("metallicRoughnessTexture"),
                        material.metallicRoughnessTexture,
                        material.metallicRoughnessTextureCoordinateSet)) {
                    result.success = false;
                    result.diagnostics = "ASSET-GLB-MATERIAL-TEXTURE-INFO-INVALID";
                    return result;
                }
                material.metallicFactor =
                    jsonFloat(pbr->find("metallicFactor"), material.metallicFactor);
                material.roughnessFactor =
                    jsonFloat(pbr->find("roughnessFactor"), material.roughnessFactor);
            }
            const auto* extensions = object.find("extensions");
            if (extensions != nullptr && extensions->type != JsonValue::Type::Object) {
                result.success = false;
                result.diagnostics = "ASSET-GLB-MATERIAL-EXTENSIONS-INVALID";
                return result;
            }
            if (extensions != nullptr && extensions->type == JsonValue::Type::Object) {
                for (const auto& extension : extensions->objectValue) {
                    if (extension.first != "KHR_materials_emissive_strength") {
                        material.hasUnsupportedMaterialExtensions = true;
                        break;
                    }
                }
                if (const auto* clearcoat = extensions->find("KHR_materials_clearcoat")) {
                    if (clearcoat->type == JsonValue::Type::Object) {
                        material.clearcoatFactor =
                            jsonFloat(clearcoat->find("clearcoatFactor"), material.clearcoatFactor);
                        material.clearcoatRoughnessFactor =
                            jsonFloat(clearcoat->find("clearcoatRoughnessFactor"),
                                material.clearcoatRoughnessFactor);
                        material.clearcoatTexture =
                            textureIndexFromInfo(clearcoat->find("clearcoatTexture"));
                        material.clearcoatRoughnessTexture =
                            textureIndexFromInfo(clearcoat->find("clearcoatRoughnessTexture"));
                        material.clearcoatNormalTexture =
                            textureIndexFromInfo(clearcoat->find("clearcoatNormalTexture"));
                    }
                }
                if (const auto* specular = extensions->find("KHR_materials_specular")) {
                    if (specular->type == JsonValue::Type::Object) {
                        material.specularFactor =
                            jsonFloat(specular->find("specularFactor"), material.specularFactor);
                        copyFloatArray(
                            specular->find("specularColorFactor"), material.specularColorFactor, 3);
                        material.specularTexture =
                            textureIndexFromInfo(specular->find("specularTexture"));
                        material.specularColorTexture =
                            textureIndexFromInfo(specular->find("specularColorTexture"));
                    }
                }
                if (const auto* sheen = extensions->find("KHR_materials_sheen")) {
                    if (sheen->type == JsonValue::Type::Object) {
                        copyFloatArray(
                            sheen->find("sheenColorFactor"), material.sheenColorFactor, 3);
                        material.sheenRoughnessFactor = jsonFloat(
                            sheen->find("sheenRoughnessFactor"), material.sheenRoughnessFactor);
                        material.sheenColorTexture =
                            textureIndexFromInfo(sheen->find("sheenColorTexture"));
                        material.sheenRoughnessTexture =
                            textureIndexFromInfo(sheen->find("sheenRoughnessTexture"));
                    }
                }
                if (const auto* transmission = extensions->find("KHR_materials_transmission")) {
                    if (transmission->type == JsonValue::Type::Object) {
                        material.transmissionFactor = jsonFloat(
                            transmission->find("transmissionFactor"), material.transmissionFactor);
                        material.transmissionTexture =
                            textureIndexFromInfo(transmission->find("transmissionTexture"));
                    }
                }
                if (const auto* volume = extensions->find("KHR_materials_volume")) {
                    if (volume->type == JsonValue::Type::Object) {
                        material.thicknessFactor =
                            jsonFloat(volume->find("thicknessFactor"), material.thicknessFactor);
                        copyFloatArray(
                            volume->find("attenuationColor"), material.attenuationColor, 3);
                        material.attenuationDistance = jsonFloat(
                            volume->find("attenuationDistance"), material.attenuationDistance);
                        material.thicknessTexture =
                            textureIndexFromInfo(volume->find("thicknessTexture"));
                    }
                }
                if (const auto* ior = extensions->find("KHR_materials_ior")) {
                    if (ior->type == JsonValue::Type::Object) {
                        material.ior = jsonFloat(ior->find("ior"), material.ior);
                    }
                }
                if (const auto* iridescence = extensions->find("KHR_materials_iridescence")) {
                    if (iridescence->type == JsonValue::Type::Object) {
                        material.iridescenceFactor = jsonFloat(
                            iridescence->find("iridescenceFactor"), material.iridescenceFactor);
                        material.iridescenceIor =
                            jsonFloat(iridescence->find("iridescenceIor"), material.iridescenceIor);
                        material.iridescenceTexture =
                            textureIndexFromInfo(iridescence->find("iridescenceTexture"));
                        material.iridescenceThicknessTexture =
                            textureIndexFromInfo(iridescence->find("iridescenceThicknessTexture"));
                    }
                }
                if (const auto* emissiveStrength =
                        extensions->find("KHR_materials_emissive_strength")) {
                    if (emissiveStrength->type != JsonValue::Type::Object
                        || !validOptionalFiniteNumber(emissiveStrength->find("emissiveStrength"))) {
                        result.success = false;
                        result.diagnostics = "ASSET-GLB-MATERIAL-EMISSIVE-STRENGTH-INVALID";
                        return result;
                    }
                    material.emissiveStrength = jsonFloat(
                        emissiveStrength->find("emissiveStrength"), material.emissiveStrength);
                    if (material.emissiveStrength < 0.0f) {
                        result.success = false;
                        result.diagnostics = "ASSET-GLB-MATERIAL-EMISSIVE-STRENGTH-INVALID";
                        return result;
                    }
                }
            }
            for (float& emissiveChannel : material.emissiveFactor) {
                emissiveChannel *= material.emissiveStrength;
            }

            result.materials.push_back(material);
        }
    }

    return result;
}

} // namespace arp::assets::detail

namespace arp::assets {

GlbMaterialImportResult importGlbMaterialsFromFile(const std::filesystem::path& path)
{
    GlbMaterialImportResult result;
    core::pmr::String bytes{&core::defaultCpuMemoryResource()};
    if (!detail::readFile(path, bytes)) {
        result.diagnostics = "ASSET-GLB-MATERIALS-FILE-READ-FAILED";
        return result;
    }
    detail::GlbChunks chunks;
    if (!detail::readGlbChunks(bytes, chunks, result.diagnostics))
        return result;
    detail::JsonValue root;
    if (!detail::parseJson(chunks.json, root) || root.type != detail::JsonValue::Type::Object) {
        result.diagnostics = "ASSET-GLB-MATERIALS-JSON-INVALID";
        return result;
    }
    auto materials = detail::parseImportedGlbMaterialAssets(
        root, chunks.bin, detail::parseGlbBufferViewRanges(root));
    if (!materials.success) {
        result.diagnostics = std::move(materials.diagnostics);
        return result;
    }
    result.materials = std::move(materials.materials);
    result.textures = std::move(materials.textures);
    result.images = std::move(materials.images);
    result.success = true;
    return result;
}

} // namespace arp::assets

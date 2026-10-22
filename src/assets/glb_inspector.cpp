#include "assets/glb_inspector.h"

#include "core/memory/cpu_allocator.h"

#include <fstream>
#include <sstream>
#include <vector>

namespace arp::assets {

namespace {

constexpr uint32_t glbMagic = 0x46546C67u;
constexpr uint32_t jsonChunkType = 0x4E4F534Au;
constexpr uint32_t binChunkType = 0x004E4942u;

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

bool extractArrayTextAfterKeyAt(const std::pmr::string& json,
    const char* key,
    const std::size_t searchOffset,
    std::pmr::string& out,
    std::size_t& foundOffset)
{
    const auto keyOffset = json.find(key, searchOffset);
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

uint32_t countTopLevelObjects(const std::pmr::string& arrayText)
{
    uint32_t count = 0;
    bool inString = false;
    bool escaped = false;
    int objectDepth = 0;
    int arrayDepth = 0;
    for (const auto ch : arrayText) {
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
            ++arrayDepth;
        } else if (ch == ']') {
            --arrayDepth;
        } else if (ch == '{') {
            if (objectDepth == 0 && arrayDepth == 0) {
                ++count;
            }
            ++objectDepth;
        } else if (ch == '}') {
            --objectDepth;
        }
    }
    return count;
}

uint32_t countArrayObjectsAfterKey(const std::pmr::string& json, const char* key)
{
    uint32_t bestCount = 0;
    std::size_t offset = 0;
    for (;;) {
        std::pmr::string arrayText;
        std::size_t foundOffset = 0;
        if (!extractArrayTextAfterKeyAt(json, key, offset, arrayText, foundOffset)) {
            break;
        }
        bestCount = std::max(bestCount, countTopLevelObjects(arrayText));
        offset = foundOffset + 1;
    }
    return bestCount;
}

GlbInspectionResult fail(std::pmr::string diagnostics)
{
    GlbInspectionResult result;
    result.diagnostics = std::move(diagnostics);
    return result;
}

} // namespace

GlbInspectionResult inspectGlbFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file.good()) {
        return fail("failed to open GLB file");
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    const auto bytes = buffer.str();
    return inspectGlbBytes(std::pmr::string(bytes.c_str(), bytes.size()));
}

GlbInspectionResult inspectGlbBytes(const std::pmr::string& bytes)
{
    if (bytes.size() < 20) {
        return fail("GLB file is too small");
    }

    GlbInspectionResult result;
    result.info.version = readU32(bytes, 4);
    result.info.declaredLength = readU32(bytes, 8);

    if (readU32(bytes, 0) != glbMagic) {
        return fail("GLB magic is invalid");
    }
    if (result.info.version != 2) {
        return fail("GLB version 2 is required");
    }
    if (result.info.declaredLength != bytes.size()) {
        return fail("GLB declared length does not match file size");
    }

    const auto firstChunkLength = readU32(bytes, 12);
    const auto firstChunkType = readU32(bytes, 16);
    if (firstChunkType != jsonChunkType || firstChunkLength == 0) {
        return fail("GLB first chunk must be JSON");
    }
    if (20u + firstChunkLength > bytes.size()) {
        return fail("GLB JSON chunk exceeds file size");
    }

    const auto json = bytes.substr(20, firstChunkLength);
    result.info.validGlb = true;
    result.info.jsonChunkLength = firstChunkLength;
    result.info.sceneCount = countArrayObjectsAfterKey(json, "\"scenes\"");
    result.info.nodeCount = countArrayObjectsAfterKey(json, "\"nodes\"");
    result.info.meshCount = countArrayObjectsAfterKey(json, "\"meshes\"");
    result.info.materialCount = countArrayObjectsAfterKey(json, "\"materials\"");
    result.info.animationCount = countArrayObjectsAfterKey(json, "\"animations\"");
    result.info.skinCount = countArrayObjectsAfterKey(json, "\"skins\"");

    const auto secondChunkOffset = 20u + firstChunkLength;
    if (secondChunkOffset + 8u <= bytes.size()) {
        const auto secondChunkLength = readU32(bytes, secondChunkOffset);
        const auto secondChunkType = readU32(bytes, secondChunkOffset + 4u);
        if (secondChunkType == binChunkType) {
            result.info.binaryChunkLength = secondChunkLength;
        }
    }

    result.success = true;
    return result;
}

} // namespace arp::assets

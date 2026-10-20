#pragma once

#include "assets/asset_pipeline.h"
#include "core/api.h"
#include "core/cpu_allocator.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace arp::assets::detail {

struct ARP_API JsonValue {
    enum class Type {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object,
    };

    Type type = Type::Null;
    bool boolValue = false;
    double numberValue = 0.0;
    std::pmr::string stringValue;
    std::pmr::vector<JsonValue> arrayValue;
    std::pmr::vector<std::pair<std::pmr::string, JsonValue>> objectValue;

    const JsonValue* find(const char* key) const;
};

struct GlbChunks {
    std::pmr::string json;
    std::pmr::string bin;
};

struct GlbBufferViewRange {
    uint32_t byteOffset = 0;
    uint32_t byteLength = 0;
};

struct ImportedGlbMaterialAssets {
    bool success = true;
    std::pmr::string diagnostics;
    std::pmr::vector<ImportedMaterialAsset> materials;
    std::pmr::vector<ImportedTextureAsset> textures;
    std::pmr::vector<ImportedImageAsset> images;
};

ARP_API bool parseJson(const std::pmr::string& text, JsonValue& out);
ARP_API bool readFile(const std::filesystem::path& path, std::pmr::string& out);
ARP_API bool readGlbChunks(
    const std::pmr::string& bytes, GlbChunks& chunks, std::pmr::string& diagnostics);
ARP_API std::pmr::vector<GlbBufferViewRange> parseGlbBufferViewRanges(const JsonValue& root);
ARP_API ImportedGlbMaterialAssets parseImportedGlbMaterialAssets(const JsonValue& root,
    const std::pmr::string& bin,
    const std::pmr::vector<GlbBufferViewRange>& bufferViews);

} // namespace arp::assets::detail

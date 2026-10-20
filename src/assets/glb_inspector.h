#pragma once

#include "core/api.h"
#include "core/cpu_allocator.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace arp::assets {

struct GlbAssetInfo {
    bool validGlb = false;
    uint32_t version = 0;
    uint32_t declaredLength = 0;
    uint32_t jsonChunkLength = 0;
    uint32_t binaryChunkLength = 0;
    uint32_t sceneCount = 0;
    uint32_t nodeCount = 0;
    uint32_t meshCount = 0;
    uint32_t materialCount = 0;
    uint32_t animationCount = 0;
    uint32_t skinCount = 0;
};

struct GlbInspectionResult {
    bool success = false;
    std::pmr::string diagnostics;
    GlbAssetInfo info;
};

ARP_API GlbInspectionResult inspectGlbFile(const std::filesystem::path& path);
ARP_API GlbInspectionResult inspectGlbBytes(const std::pmr::string& bytes);

} // namespace arp::assets

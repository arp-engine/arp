#pragma once

#include "core/api.h"
#include "core/memory/cpu_allocator.h"

#include <filesystem>

namespace arp::assets {

struct UnityAssetDatabaseScanBridge final {
    bool success = false;
    int assetCount = 0;
    int errorCount = 0;
    int warningCount = 0;
    std::pmr::string diagnostics{&core::defaultCpuMemoryResource()};
};

struct UnityAssetRecordBridge final {
    bool success = false;
    std::pmr::string guid{&core::defaultCpuMemoryResource()};
    std::pmr::string metaPath{&core::defaultCpuMemoryResource()};
    std::pmr::string assetPath{&core::defaultCpuMemoryResource()};
    std::pmr::string diagnostics{&core::defaultCpuMemoryResource()};
};

ARP_API UnityAssetDatabaseScanBridge scanUnityAssetDatabase(const std::filesystem::path& root);

ARP_API UnityAssetRecordBridge findUnityAsset(
    const std::filesystem::path& root, const std::pmr::string& guid);

} // namespace arp::assets

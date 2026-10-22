#pragma once

#include "assets/imported_asset_types.h"
#include "core/api.h"
#include "core/memory/cpu_allocator.h"

#include <filesystem>

namespace arp::assets {

// Backward-compatible aliases (preferred new names in imported_asset_types.h).
using UnitySourceLocation = SourceLocation;
using UnityImportReportRecord = ImportReportRecord;
using UnityImportReport = ImportReport;
using UnityImportReportWriteResult = ImportReportWriteResult;

struct UnityAssetDatabaseEntry {
    std::pmr::string guid;
    std::filesystem::path metaPath;
    std::filesystem::path assetPath;
    UnitySourceLocation guidLocation;
};

struct UnityAssetDatabaseBuildResult {
    bool success = false;
    std::pmr::string diagnostics;
    std::pmr::vector<UnityAssetDatabaseEntry> entries;
    UnityImportReport report;

    [[nodiscard]] const UnityAssetDatabaseEntry* findByGuid(const std::pmr::string& guid) const;
};

ARP_API UnityAssetDatabaseBuildResult buildUnityAssetDatabase(const std::filesystem::path& root);

} // namespace arp::assets

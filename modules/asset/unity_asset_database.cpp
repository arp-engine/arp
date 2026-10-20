#include "unity_asset_database.h"

#include "core/cpu_allocator.h"
#include "unity_asset_database_bridge.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace arp::assets {

namespace {

UnityAssetDatabaseBuildResult buildBridgeDatabase(const std::filesystem::path& root)
{
    UnityAssetDatabaseBuildResult result;
    const auto assetsRoot = root / "Assets";
    if (!std::filesystem::exists(assetsRoot)) {
        result.success = true;
        return result;
    }
    return buildUnityAssetDatabase(assetsRoot);
}

std::pmr::string toPmrString(const std::filesystem::path& path)
{
    return core::toPmrString(path.string());
}

std::pmr::string toProjectRelativePath(
    const std::filesystem::path& projectRoot, const std::filesystem::path& path)
{
    const auto relative = path.lexically_relative(projectRoot);
    if (!relative.empty() && !relative.is_absolute()) {
        return core::toPmrString(relative.generic_string());
    }
    return toPmrString(path);
}

std::pmr::string trim(const std::pmr::string& value)
{
    const auto begin = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
    const auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    }).base();
    if (begin >= end) {
        return {};
    }
    return std::pmr::string(begin, end);
}

bool startsWith(const std::pmr::string& value, const char* prefix)
{
    const std::pmr::string prefixText(prefix);
    return value.size() >= prefixText.size()
        && std::equal(prefixText.begin(), prefixText.end(), value.begin());
}

std::pmr::string afterColon(const std::pmr::string& line)
{
    const auto colon = line.find(':');
    if (colon == std::pmr::string::npos) {
        return {};
    }
    return trim(line.substr(colon + 1));
}

struct ParsedGuidLine {
    bool found = false;
    std::pmr::string guid;
    UnitySourceLocation location;
};

ParsedGuidLine parseGuidLine(const std::filesystem::path& path, const std::pmr::string& text)
{
    ParsedGuidLine result;
    std::istringstream stream(core::toStdString(text));
    std::pmr::string line;
    std::uint32_t lineNumber = 0;
    while (std::getline(stream, line)) {
        ++lineNumber;
        const auto stripped = trim(line);
        if (!startsWith(stripped, "guid:")) {
            continue;
        }
        result.found = true;
        result.guid = afterColon(stripped);
        result.location = UnitySourceLocation{path, lineNumber, 1};
        return result;
    }
    return result;
}

bool readTextFile(const std::filesystem::path& path, std::pmr::string& text)
{
    std::ifstream file(path, std::ios::binary);
    if (!file.good()) {
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    text = core::toPmrString(buffer.str());
    return true;
}

bool isTextureLikeExtension(const std::filesystem::path& path)
{
    const auto extension = core::toPmrString(path.extension().string());
    return extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".tga"
        || extension == ".psd";
}

int textureRuntimePriority(const std::filesystem::path& path)
{
    const auto extension = core::toPmrString(path.extension().string());
    if (extension == ".png") {
        return 0;
    }
    if (extension == ".tga") {
        return 1;
    }
    if (extension == ".jpg" || extension == ".jpeg") {
        return 2;
    }
    if (extension == ".psd") {
        return 100;
    }
    return 1000;
}

bool isAuthoringDuplicatePair(
    const UnityAssetDatabaseEntry& current, const UnityAssetDatabaseEntry& candidate)
{
    return isTextureLikeExtension(current.assetPath) && isTextureLikeExtension(candidate.assetPath)
        && current.assetPath.stem() == candidate.assetPath.stem()
        && textureRuntimePriority(current.assetPath) != textureRuntimePriority(candidate.assetPath);
}

} // namespace

const UnityAssetDatabaseEntry* UnityAssetDatabaseBuildResult::findByGuid(
    const std::pmr::string& guid) const
{
    for (const auto& entry : entries) {
        if (entry.guid == guid) {
            return &entry;
        }
    }
    return nullptr;
}

UnityAssetDatabaseBuildResult buildUnityAssetDatabase(const std::filesystem::path& root)
{
    UnityAssetDatabaseBuildResult result;
    if (!std::filesystem::exists(root)) {
        result.diagnostics = "Unity asset database root does not exist";
        result.report.add("error",
            "UnityAssetDatabase.RootExists",
            result.diagnostics,
            UnitySourceLocation{root, 0, 0});
        return result;
    }

    std::pmr::vector<std::filesystem::path> metaFiles;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (entry.is_regular_file() && entry.path().extension() == ".meta") {
            metaFiles.push_back(entry.path());
        }
    }
    std::sort(metaFiles.begin(), metaFiles.end());

    for (const auto& metaPath : metaFiles) {
        std::pmr::string text;
        if (!readTextFile(metaPath, text)) {
            result.report.add("error",
                "UnityAssetDatabase.MetaReadable",
                std::pmr::string("could not read meta file ") + toPmrString(metaPath),
                UnitySourceLocation{metaPath, 0, 0});
            continue;
        }

        const auto guidLine = parseGuidLine(metaPath, text);
        if (!guidLine.found || guidLine.guid.empty()) {
            result.report.add("error",
                "UnityAssetDatabase.GuidPresent",
                std::pmr::string("meta file is missing guid: ") + toPmrString(metaPath),
                UnitySourceLocation{metaPath, 0, 0});
            continue;
        }

        UnityAssetDatabaseEntry candidate;
        candidate.guid = guidLine.guid;
        candidate.metaPath = metaPath;
        candidate.assetPath = metaPath;
        candidate.assetPath.replace_extension();
        candidate.guidLocation = guidLine.location;

        auto* existing = const_cast<UnityAssetDatabaseEntry*>(result.findByGuid(candidate.guid));
        if (existing == nullptr) {
            result.entries.push_back(candidate);
            continue;
        }

        if (isAuthoringDuplicatePair(*existing, candidate)) {
            const auto currentPriority = textureRuntimePriority(existing->assetPath);
            const auto candidatePriority = textureRuntimePriority(candidate.assetPath);
            if (candidatePriority < currentPriority) {
                *existing = candidate;
            }
            result.report.add("warning",
                "UnityAssetDatabase.GuidAuthoringDuplicate",
                std::pmr::string("guid ") + candidate.guid
                    + " is shared by runtime/authoring texture variants; keeping "
                    + toPmrString(existing->assetPath),
                candidate.guidLocation);
            continue;
        }

        result.report.add("error",
            "UnityAssetDatabase.UniqueGuid",
            std::pmr::string("guid ") + candidate.guid + " already mapped to "
                + toPmrString(existing->assetPath),
            candidate.guidLocation);
    }

    std::sort(result.entries.begin(),
        result.entries.end(),
        [](const UnityAssetDatabaseEntry& lhs, const UnityAssetDatabaseEntry& rhs) {
            return lhs.guid < rhs.guid;
        });

    result.success = !result.report.hasErrors();
    if (!result.success) {
        result.diagnostics = result.report.toText();
    }
    return result;
}

UnityAssetDatabaseScanBridge scanUnityAssetDatabase(const std::filesystem::path& root)
{
    const auto database = buildBridgeDatabase(root);
    UnityAssetDatabaseScanBridge result;
    result.success = database.success;
    result.assetCount = static_cast<int>(database.entries.size());
    result.diagnostics = database.report.toText();
    for (const auto& record : database.report.records) {
        if (record.severity == "error") {
            ++result.errorCount;
        } else if (record.severity == "warning") {
            ++result.warningCount;
        }
    }
    if (!result.success && result.diagnostics.empty()) {
        result.diagnostics = database.diagnostics;
    }
    return result;
}

UnityAssetRecordBridge findUnityAsset(
    const std::filesystem::path& root, const std::pmr::string& guid)
{
    const auto database = buildBridgeDatabase(root);
    UnityAssetRecordBridge result;
    const auto* entry = database.findByGuid(guid);
    result.diagnostics = database.report.toText();
    if (!database.success || entry == nullptr) {
        if (result.diagnostics.empty() && entry == nullptr) {
            result.diagnostics = "Unity asset GUID was not found";
        }
        return result;
    }

    result.success = true;
    result.guid = entry->guid;
    result.metaPath = toProjectRelativePath(root, entry->metaPath);
    result.assetPath = toProjectRelativePath(root, entry->assetPath);
    return result;
}

} // namespace arp::assets

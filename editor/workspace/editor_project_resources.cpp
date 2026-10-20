#include "editor/workspace/editor_project_resources.h"

#include "core/filesystem_path.h"
#include "2d/sprite_resources.h"
#include "resource/internal_asset_package.h"
#include "resource/prefab_asset_resources.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fkYAML/Node.hpp>
#include <fstream>
#include <limits>
#include <optional>
#include <string_view>
#include <system_error>

namespace arp::editor {
namespace {
using YamlNode = fkyaml::node;
constexpr const char* kProjectManifestName = "project.arp.yaml";
constexpr std::size_t kMaxManifestBytes = 1024u * 1024u;
constexpr std::size_t kMaxAssetPackages = 256u;

EditorProjectResourceLoadResult failure(EditorProjectResourceLoadResult result, const char* message)
{
    result.diagnostics = message;
    return result;
}

const YamlNode* findValue(const YamlNode& root, const char* key)
{
    if (!root.is_mapping())
        return nullptr;
    const auto& mapping = root.get_value_ref<const YamlNode::mapping_type&>();
    const auto found = mapping.find(YamlNode(key));
    return found == mapping.end() ? nullptr : &found->second;
}

bool resolveContainedPath(
    const std::filesystem::path& root, std::string_view relative, std::filesystem::path& resolved)
{
    if (!core::FilePath::isSafeRelativePath(relative))
        return false;
    std::error_code error;
    resolved = std::filesystem::canonical(root / std::filesystem::u8path(relative), error);
    return !error && core::FilePath::contains(root, resolved);
}

bool readManifest(const std::filesystem::path& path,
    core::pmr::String& text,
    EditorProjectResourceLoadResult& result)
{
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > kMaxManifestBytes) {
        result.diagnostics =
            "EDITOR-PROJECT-MANIFEST-SIZE: manifest is unreadable or exceeds 1 MiB";
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        result.diagnostics = "EDITOR-PROJECT-MANIFEST-READ: cannot open project manifest";
        return false;
    }
    text.resize(static_cast<std::size_t>(size));
    if (size > 0u)
        input.read(text.data(), static_cast<std::streamsize>(size));
    if (!input && !input.eof()) {
        result.diagnostics = "EDITOR-PROJECT-MANIFEST-READ: cannot read project manifest";
        return false;
    }
    return true;
}

template <std::size_t N>
bool containsOnlyFields(const YamlNode& node, const std::array<std::string_view, N>& allowed)
{
    if (!node.is_mapping())
        return false;
    const auto& mapping = node.get_value_ref<const YamlNode::mapping_type&>();
    for (const auto& entry : mapping) {
        if (!entry.first.is_string())
            return false;
        const auto key = entry.first.get_value<YamlNode::string_type>();
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end())
            return false;
    }
    return true;
}

bool readUnsigned32(const YamlNode* value, std::uint32_t& output)
{
    if (!value || !value->is_integer())
        return false;
    const auto integer = value->get_value<YamlNode::integer_type>();
    if (integer <= 0 || static_cast<std::uint64_t>(integer)
            > std::numeric_limits<std::uint32_t>::max())
        return false;
    output = static_cast<std::uint32_t>(integer);
    return true;
}

bool readFiniteFloat(const YamlNode* value, float& output)
{
    if (!value || (!value->is_integer() && !value->is_float_number()))
        return false;
    output = value->is_integer()
        ? static_cast<float>(value->get_value<YamlNode::integer_type>())
        : static_cast<float>(value->get_value<YamlNode::float_number_type>());
    return std::isfinite(output);
}

bool parsePhysicsWorldConfig(const YamlNode* node,
    std::optional<physics2d::WorldConfig>& config,
    core::pmr::String& diagnostics)
{
    if (!node)
        return true;
    constexpr std::array<std::string_view, 10> fields{"gravity",
        "maxBodies",
        "maxColliders",
        "maxJoints",
        "maxNativeShapes",
        "maxEvents",
        "maxQueryResults",
        "maxChainPoints",
        "substeps",
        "fixedSeconds"};
    if (!containsOnlyFields(*node, fields)) {
        diagnostics = "EDITOR-PROJECT-PHYSICS2D-SCHEMA: unknown field or non-mapping config";
        return false;
    }
    const auto* gravity = findValue(*node, "gravity");
    constexpr std::array<std::string_view, 2> gravityFields{"x", "y"};
    physics2d::WorldConfig parsed{};
    if (!gravity || !containsOnlyFields(*gravity, gravityFields)
        || !readFiniteFloat(findValue(*gravity, "x"), parsed.gravity.x)
        || !readFiniteFloat(findValue(*gravity, "y"), parsed.gravity.y)
        || !readUnsigned32(findValue(*node, "maxBodies"), parsed.maxBodies)
        || !readUnsigned32(findValue(*node, "maxColliders"), parsed.maxColliders)
        || !readUnsigned32(findValue(*node, "maxJoints"), parsed.maxJoints)
        || !readUnsigned32(findValue(*node, "maxNativeShapes"), parsed.maxNativeShapes)
        || !readUnsigned32(findValue(*node, "maxEvents"), parsed.maxEvents)
        || !readUnsigned32(findValue(*node, "maxQueryResults"), parsed.maxQueryResults)
        || !readUnsigned32(findValue(*node, "maxChainPoints"), parsed.maxChainPoints)
        || !readUnsigned32(findValue(*node, "substeps"), parsed.substeps)
        || !readFiniteFloat(findValue(*node, "fixedSeconds"), parsed.fixedSeconds)) {
        diagnostics =
            "EDITOR-PROJECT-PHYSICS2D-SCHEMA: all world capacities and timing fields are required and bounded";
        return false;
    }
    config = parsed;
    return true;
}

bool parsePackagePaths(std::string_view yaml,
    core::pmr::Vector<core::pmr::String>& paths,
    std::optional<physics2d::WorldConfig>& physicsConfig,
    core::pmr::String& diagnostics)
{
    YamlNode root;
    try {
        root = YamlNode::deserialize(yaml.data(), yaml.data() + yaml.size());
    } catch (const std::exception&) {
        diagnostics = "EDITOR-PROJECT-MANIFEST-YAML: malformed project manifest";
        return false;
    }
    if (!root.is_mapping()) {
        diagnostics = "EDITOR-PROJECT-MANIFEST-SCHEMA: root must be a mapping";
        return false;
    }
    constexpr std::array<std::string_view, 4> allowed{
        "format", "version", "assetPackages", "physics2d"};
    if (!containsOnlyFields(root, allowed)) {
        diagnostics = "EDITOR-PROJECT-MANIFEST-SCHEMA: keys must be known strings";
        return false;
    }
    const auto* format = findValue(root, "format");
    const auto* version = findValue(root, "version");
    const auto* packages = findValue(root, "assetPackages");
    if (!format || !format->is_string()
        || format->get_value<YamlNode::string_type>() != "ArpEditorProject" || !version
        || !version->is_integer() || version->get_value<YamlNode::integer_type>() != 1 || !packages
        || !packages->is_sequence()) {
        diagnostics =
            "EDITOR-PROJECT-MANIFEST-SCHEMA: expected ArpEditorProject/v1 and assetPackages sequence";
        return false;
    }
    const auto& sequence = packages->get_value_ref<const YamlNode::sequence_type&>();
    if (sequence.size() > kMaxAssetPackages) {
        diagnostics = "EDITOR-PROJECT-MANIFEST-BUDGET: asset package count exceeds 256";
        return false;
    }
    if (!parsePhysicsWorldConfig(findValue(root, "physics2d"), physicsConfig, diagnostics))
        return false;
    core::pmr::UnorderedSet<core::pmr::String> unique{&core::defaultCpuMemoryResource()};
    for (const auto& entry : sequence) {
        if (!entry.is_string()) {
            diagnostics =
                "EDITOR-PROJECT-MANIFEST-SCHEMA: each asset package path must be a string";
            return false;
        }
        const auto value = entry.get_value<YamlNode::string_type>();
        const std::string_view path(value.data(), value.size());
        if (!core::FilePath::isSafeRelativePath(path)) {
            diagnostics =
                "EDITOR-PROJECT-MANIFEST-PATH: asset package path must be a safe relative path";
            return false;
        }
        core::pmr::String owned(path, &core::defaultCpuMemoryResource());
        if (!unique.insert(owned).second) {
            diagnostics = "EDITOR-PROJECT-MANIFEST-DUPLICATE: duplicate asset package path";
            return false;
        }
        paths.push_back(std::move(owned));
    }
    return true;
}
} // namespace

EditorProjectResources::EditorProjectResources()
    : manager_(core::makeUnique<resource::ResourceManager>())
{}

EditorProjectResourceLoadResult EditorProjectResources::load(
    const std::filesystem::path& projectRoot)
{
    scene2d::registerSpriteFontResourceType();
    EditorProjectResourceLoadResult result;
    std::error_code error;
    const auto root = std::filesystem::canonical(projectRoot, error);
    if (error || !std::filesystem::is_directory(root, error) || error) {
        result = failure(
            std::move(result), "EDITOR-PROJECT-ROOT: project root is not a readable directory");
        lastLoad_ = result;
        return result;
    }

    const auto manifestPath = root / kProjectManifestName;
    if (!std::filesystem::exists(manifestPath, error) && !error) {
        auto emptyManager = core::makeUnique<resource::ResourceManager>();
        manager_ = std::move(emptyManager);
        physicsWorld2D_.reset();
        packageRoots_.clear();
        result.success = true;
        result.diagnostics =
            "EDITOR-PROJECT-MANIFEST-MISSING: opened with an empty project resource set";
        lastLoad_ = result;
        return result;
    }
    if (error) {
        result = failure(
            std::move(result), "EDITOR-PROJECT-MANIFEST-STAT: cannot inspect project manifest");
        lastLoad_ = result;
        return result;
    }

    result.manifestFound = true;
    std::filesystem::path canonicalManifest;
    if (!resolveContainedPath(root, kProjectManifestName, canonicalManifest)
        || !std::filesystem::is_regular_file(canonicalManifest, error) || error) {
        result = failure(std::move(result),
            "EDITOR-PROJECT-MANIFEST-PATH: manifest must resolve inside project root");
        lastLoad_ = result;
        return result;
    }

    core::pmr::String yaml(&core::defaultCpuMemoryResource());
    if (!readManifest(canonicalManifest, yaml, result)) {
        lastLoad_ = result;
        return result;
    }
    core::pmr::Vector<core::pmr::String> packagePaths{&core::defaultCpuMemoryResource()};
    std::optional<physics2d::WorldConfig> physicsConfig;
    if (!parsePackagePaths(
            std::string_view(yaml.data(), yaml.size()),
            packagePaths,
            physicsConfig,
            result.diagnostics)) {
        lastLoad_ = result;
        return result;
    }

    auto candidate = core::makeUnique<resource::ResourceManager>();
    core::SharedPtr<physics2d::PhysicsWorld2D> candidatePhysicsWorld;
    if (physicsConfig) {
        candidatePhysicsWorld = core::makeShared<physics2d::PhysicsWorld2D>(*physicsConfig);
        if (!candidatePhysicsWorld->valid()) {
            result.diagnostics = "EDITOR-PROJECT-PHYSICS2D-CREATE: ";
            result.diagnostics += candidatePhysicsWorld->diagnostic();
            lastLoad_ = result;
            return result;
        }
    }
    core::pmr::Vector<PackageResourceRoot> candidatePackageRoots{
        &core::defaultCpuMemoryResource()};
    for (const auto& relative : packagePaths) {
        std::filesystem::path packagePath;
        if (!resolveContainedPath(
                root, std::string_view(relative.data(), relative.size()), packagePath)
            || !std::filesystem::is_directory(packagePath, error) || error) {
            result.diagnostics =
                "EDITOR-PROJECT-ASSET-PACKAGE-PATH: package must resolve to a directory inside project root";
            lastLoad_ = result;
            return result;
        }
        const auto reader = resource::InternalAssetPackageReader();
        const auto package = reader.read(packagePath);
        if (!package.success) {
            result.diagnostics = "EDITOR-PROJECT-ASSET-PACKAGE-LOAD: ";
            result.diagnostics += package.diagnostics;
            lastLoad_ = result;
            return result;
        }
        const auto loaded = reader.load(packagePath, *candidate);
        if (!loaded.success) {
            result.diagnostics = "EDITOR-PROJECT-ASSET-PACKAGE-LOAD: ";
            result.diagnostics += loaded.diagnostics;
            lastLoad_ = result;
            return result;
        }
        for (const auto& entry : package.package.entries) {
            if (entry.resourceType != resource::PrefabResource::resourceTypeName())
                continue;
            PackageResourceRoot mapping;
            mapping.resourceId = entry.resourceId;
            mapping.packageRoot = packagePath;
            candidatePackageRoots.push_back(std::move(mapping));
        }
        ++result.packageCount;
    }

    result.success = true;
    result.physicsWorld2DReady = bool(candidatePhysicsWorld);
    result.diagnostics.clear();
    manager_ = std::move(candidate);
    physicsWorld2D_ = std::move(candidatePhysicsWorld);
    packageRoots_ = std::move(candidatePackageRoots);
    lastLoad_ = result;
    return result;
}

bool EditorProjectResources::packageRootForResource(
    resource::NameView resourceId, std::filesystem::path& packageRoot) const
{
    const auto found = std::find_if(packageRoots_.begin(), packageRoots_.end(),
        [resourceId](const PackageResourceRoot& mapping) {
            return std::string_view(mapping.resourceId.data(), mapping.resourceId.size())
                == resourceId;
        });
    if (found == packageRoots_.end())
        return false;
    packageRoot = found->packageRoot;
    return true;
}

} // namespace arp::editor

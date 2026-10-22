#include "resource/asset_package_closure.h"

#include "core/encoding/yaml_contract.h"
#include "core/hash/stable_hash.h"
#include "core/io/atomic_file_service.h"
#include "core/io/file_io.h"
#include "core/io/filesystem_path.h"
#include "core/io/staged_directory.h"
#include "resource/asset_package_payload.h"
#include "resource/terrain_asset_resource.h"
#include "resource/vegetation_catalog_cooked.h"

#include <algorithm>
#include <cstdio>

namespace arp::resource {
namespace {
using Yaml = core::YamlContract;

void require(bool condition, const char* diagnostic)
{
    if (!condition)
        throw core::YamlContractError{diagnostic};
}

bool safePath(core::StringView path)
{
    return path.find('\0') == core::StringView::npos && core::FilePath::isSafeRelativePath(path);
}

std::size_t constrained(std::size_t declared, std::size_t caller)
{
    return caller ? std::min(declared, caller) : declared;
}

std::filesystem::path contained(const std::filesystem::path& root, const core::pmr::String& path)
{
    require(safePath(path), "PACKAGE-CLOSURE-UNSAFE-PATH");
    std::error_code error;
    const auto resolved =
        std::filesystem::canonical(root / std::filesystem::path(path.c_str()), error);
    require(!error && core::FilePath::contains(root, resolved),
        "PACKAGE-CLOSURE-PATH-ESCAPE-OR-MISSING");
    return resolved;
}

std::uint64_t hashBytes(const core::ByteBuffer& bytes)
{
    core::StableHash64 hash;
    hash.appendBytes(bytes.data(), bytes.size());
    return hash.value();
}

bool copyContent(const std::filesystem::path& root,
    const std::filesystem::path& target,
    const AssetPackageClosureFile& file,
    core::pmr::String& diagnostic)
{
    const auto bytes = core::readBinaryFile(contained(root, file.path), file.byteCount);
    if (!bytes.success || bytes.bytes.size() != file.byteCount
        || hashBytes(bytes.bytes) != file.contentHash) {
        diagnostic = "PACKAGE-CLOSURE-FILE-HASH-OR-SIZE";
        return false;
    }
    const auto output = target / std::filesystem::path(file.path.c_str());
    std::filesystem::create_directories(output.parent_path());
    const auto written = core::io::replaceFileAtomically(core::FilePath::utf8(output), bytes.bytes);
    diagnostic = written.diagnostic;
    return written.success;
}

bool discoverFiles(const std::filesystem::path& root,
    AssetPackageClosureResult& result,
    std::size_t maxFiles,
    core::pmr::UnorderedSet<core::pmr::String>& required)
{
    const auto addFile = [&](core::pmr::String path) {
        require(safePath(path), "PACKAGE-CLOSURE-UNSAFE-PATH");
        require(path != "manifest.arpkg.yaml" && path != result.package.closurePath
                && std::none_of(result.package.entries.begin(),
                    result.package.entries.end(),
                    [&](const auto& e) {
                        return e.envelopePath == path;
                    }),
            "PACKAGE-CLOSURE-METADATA-COLLISION");
        require(required.find(path) != required.end() || required.size() < maxFiles,
            "PACKAGE-CLOSURE-FILE-COUNT");
        required.insert(std::move(path));
    };
    const auto dependency = [&](const ResourceDependency& binding, const char* type) {
        const auto found = std::find_if(
            result.package.entries.begin(), result.package.entries.end(), [&](const auto& e) {
                return e.resourceId == binding.id;
            });
        require(found != result.package.entries.end() && found->resourceType == type,
            "PACKAGE-CLOSURE-DEPENDENCY-MISSING-OR-TYPE");
    };
    AssetPackagePayloadInspector payloads;
    for (const auto& entry : result.package.entries) {
        require(entry.status == "Contract Complete", "PACKAGE-CLOSURE-RESOURCE-STATUS");
        if (payloads.supports(entry.resourceType)) {
            const auto payload = payloads.inspect(entry, true);
            if (!payload.success) {
                result.diagnostics = payload.diagnostics;
                return false;
            }
            for (const auto& binding : payload.dependencies)
                dependency(binding.binding, binding.expectedType);
            for (const auto& path : payload.files)
                addFile(path);
            continue;
        }
        require(entry.resourceType == TerrainAssetResource::resourceTypeName()
                || entry.resourceType == kVegetationCatalogCookedContract.type,
            "PACKAGE-CLOSURE-RESOURCE-TYPE-UNSUPPORTED");
        const auto envelope = Yaml::parseSingleDocument(entry.envelopeYaml);
        const auto& fields = Yaml::required(Yaml::required(envelope, "payload"), "fields");
        const auto path = Yaml::text(fields, "manifestPath");
        const auto manifest = root / std::filesystem::path(path.c_str());
        contained(root, path);
        const ResourceReference reference{
            entry.resourceId, entry.resourceType, entry.alias, entry.status, {}};
        core::pmr::Vector<ResourceFileReadIntent> intents{&core::defaultCpuMemoryResource()};
        ResourceLoadContext context;
        context.maxDecodedBytes = result.maxDecodedBytes;
        if (entry.resourceType == TerrainAssetResource::resourceTypeName()) {
            const auto terrain = TerrainAssetResource(reference, manifest).inspectManifest(context);
            if (!terrain.success) {
                result.diagnostics = terrain.diagnostics;
                return false;
            }
            for (const auto& binding : terrain.dependencies) {
                require(binding.usage == "terrain/vegetation-catalog"
                        || binding.usage == "terrain/material-layer",
                    "PACKAGE-CLOSURE-DEPENDENCY-USAGE-UNSUPPORTED");
                dependency(binding,
                    binding.usage == "terrain/vegetation-catalog" ? "VegetationCatalogResource"
                                                                  : "MaterialResource");
            }
            intents = terrain.files;
        } else if (entry.resourceType == kVegetationCatalogCookedContract.type) {
            const auto catalog =
                VegetationCatalogCookedCodec().inspect(reference, manifest, context);
            if (!catalog.success) {
                result.diagnostics = catalog.diagnostics;
                return false;
            }
            for (const auto& binding : catalog.dependencies)
                dependency(binding, "MaterialResource");
            intents = catalog.files;
        } else {
            result.diagnostics = "PACKAGE-CLOSURE-RESOURCE-TYPE-UNSUPPORTED";
            return false;
        }
        for (const auto& intent : intents)
            addFile(core::FilePath::utf8(
                std::filesystem::path(intent.path.c_str()).lexically_relative(root)));
    }
    return true;
}
} // namespace

AssetPackageClosureResult AssetPackageClosureService::inspect(
    const std::filesystem::path& directory, const AssetPackageClosureBudget& limits) const
{
    AssetPackageClosureResult result;
    try {
        const auto package = InternalAssetPackageReader().read(
            directory, maxMetadataBytes, constrained(maxResources, limits.maxResources));
        if (!package.success) {
            result.diagnostics = package.diagnostics;
            return result;
        }
        result.package = package.package;
        require(!result.package.closurePath.empty(), "PACKAGE-CLOSURE-DECLARATION-REQUIRED");
        const auto root = std::filesystem::canonical(directory);
        const auto indexFile =
            core::readTextFile(contained(root, result.package.closurePath), maxMetadataBytes);
        require(indexFile.success, "PACKAGE-CLOSURE-INDEX-READ");
        auto index = Yaml::parseSingleDocument(indexFile.text);
        Yaml::onlyKeys(index,
            {"type",
                "contractVersion",
                "status",
                "maxResources",
                "maxFiles",
                "maxFileBytes",
                "maxTotalFileBytes",
                "maxDecodedBytes",
                "files"});
        require(Yaml::text(index, "type") == "AssetPackageClosure"
                && Yaml::text(index, "contractVersion") == "resource.package.closure/v1"
                && Yaml::text(index, "status") == "Contract Complete"
                && result.package.status == "Contract Complete",
            "PACKAGE-CLOSURE-CONTRACT");
        const auto resources = Yaml::uint64(Yaml::required(index, "maxResources"));
        auto maxFiles = Yaml::uint64(Yaml::required(index, "maxFiles"));
        auto maxFile = Yaml::uint64(Yaml::required(index, "maxFileBytes"));
        auto maxTotal = Yaml::uint64(Yaml::required(index, "maxTotalFileBytes"));
        const auto decoded = Yaml::uint64(Yaml::required(index, "maxDecodedBytes"));
        require(resources && resources <= maxResources && result.package.entries.size() <= resources
                && maxFiles && maxFiles <= SIZE_MAX && maxFile && maxFile <= SIZE_MAX && maxTotal
                && maxTotal <= SIZE_MAX && decoded && decoded <= SIZE_MAX,
            "PACKAGE-CLOSURE-BUDGET");
        require(result.package.entries.size() <= constrained(maxResources, limits.maxResources),
            "PACKAGE-CLOSURE-BUDGET");
        maxFiles = constrained(static_cast<std::size_t>(maxFiles), limits.maxFiles);
        maxFile = constrained(static_cast<std::size_t>(maxFile), limits.maxFileBytes);
        maxTotal = constrained(static_cast<std::size_t>(maxTotal), limits.maxTotalFileBytes);
        result.maxDecodedBytes =
            constrained(static_cast<std::size_t>(decoded), limits.maxDecodedBytes);
        const auto& files = Yaml::sequence(index, "files");
        require(files.size() <= maxFiles, "PACKAGE-CLOSURE-FILE-COUNT");
        core::pmr::UnorderedSet<core::pmr::String> required{&core::defaultCpuMemoryResource()};
        if (!discoverFiles(root, result, maxFiles, required))
            return result;
        std::size_t total = 0u;
        for (const auto& node : files) {
            Yaml::onlyKeys(node, {"path", "byteCount", "contentHash"});
            AssetPackageClosureFile file;
            file.path = Yaml::text(node, "path");
            const auto bytes = Yaml::uint64(Yaml::required(node, "byteCount"));
            file.contentHash = Yaml::hex64(node, "contentHash");
            require(bytes <= maxFile && total <= maxTotal && bytes <= maxTotal - total,
                "PACKAGE-CLOSURE-FILE-BUDGET");
            require(required.find(file.path) != required.end()
                    && (result.files.empty() || result.files.back().path < file.path),
                "PACKAGE-CLOSURE-FILE-SET-OR-ORDER");
            file.byteCount = static_cast<std::size_t>(bytes);
            total += file.byteCount;
            const auto resolved = contained(root, file.path);
            require(resolved != contained(root, core::pmr::String("manifest.arpkg.yaml"))
                    && resolved != contained(root, result.package.closurePath),
                "PACKAGE-CLOSURE-METADATA-COLLISION");
            for (const auto& entry : result.package.entries)
                require(resolved != contained(root, entry.envelopePath),
                    "PACKAGE-CLOSURE-METADATA-COLLISION");
            const auto content = core::readBinaryFile(resolved, file.byteCount);
            require(content.success && content.bytes.size() == file.byteCount
                    && hashBytes(content.bytes) == file.contentHash,
                "PACKAGE-CLOSURE-FILE-HASH-OR-SIZE");
            result.files.push_back(std::move(file));
        }
        require(result.files.size() == required.size(), "PACKAGE-CLOSURE-FILE-MISSING");
        result.indexYaml = Yaml::serialize(index);
        result.success = true;
    } catch (const core::YamlContractError& error) {
        result.diagnostics = error.diagnostic;
    } catch (...) {
        result.diagnostics = "PACKAGE-CLOSURE-INVALID";
    }
    return result;
}

AssetPackageClosureResult AssetPackageClosureService::prepareIndex(
    const InternalAssetPackageDesc& package,
    const std::filesystem::path& directory,
    const AssetPackageClosureBudget& budget) const
{
    AssetPackageClosureResult result;
    result.package = package;
    result.maxDecodedBytes = budget.maxDecodedBytes;
    try {
        require(budget.maxResources && budget.maxResources <= maxResources
                && package.entries.size() <= budget.maxResources && budget.maxFiles
                && budget.maxFileBytes && budget.maxTotalFileBytes && budget.maxDecodedBytes,
            "PACKAGE-CLOSURE-BUDGET");
        for (const auto value : {budget.maxFiles,
                 budget.maxFileBytes,
                 budget.maxTotalFileBytes,
                 budget.maxDecodedBytes})
            require(value <= INT64_MAX, "PACKAGE-CLOSURE-BUDGET");
        require(safePath(package.closurePath), "PACKAGE-CLOSURE-DECLARATION-REQUIRED");
        const auto written = InternalAssetPackageWriter().write(package, directory);
        if (!written.success) {
            result.diagnostics = written.diagnostics;
            return result;
        }
        const auto root = std::filesystem::canonical(directory);
        core::pmr::UnorderedSet<core::pmr::String> required{&core::defaultCpuMemoryResource()};
        if (!discoverFiles(root, result, budget.maxFiles, required))
            return result;
        core::pmr::Vector<core::pmr::String> paths(
            required.begin(), required.end(), &core::defaultCpuMemoryResource());
        std::sort(paths.begin(), paths.end());
        auto index = Yaml::Node::mapping();
        index["type"] = "AssetPackageClosure";
        index["contractVersion"] = "resource.package.closure/v1";
        index["status"] = "Contract Complete";
        index["maxResources"] = static_cast<std::int64_t>(budget.maxResources);
        index["maxFiles"] = static_cast<std::int64_t>(budget.maxFiles);
        index["maxFileBytes"] = static_cast<std::int64_t>(budget.maxFileBytes);
        index["maxTotalFileBytes"] = static_cast<std::int64_t>(budget.maxTotalFileBytes);
        index["maxDecodedBytes"] = static_cast<std::int64_t>(budget.maxDecodedBytes);
        index["files"] = Yaml::Node::sequence();
        std::size_t total = 0u;
        for (const auto& path : paths) {
            const auto bytes = core::readBinaryFile(contained(root, path),
                std::min(budget.maxFileBytes, budget.maxTotalFileBytes - total));
            require(bytes.success, "PACKAGE-CLOSURE-FILE-BUDGET-OR-READ");
            total += bytes.bytes.size();
            char hex[17]{};
            std::snprintf(hex,
                sizeof(hex),
                "%016llx",
                static_cast<unsigned long long>(hashBytes(bytes.bytes)));
            auto file = Yaml::Node::mapping();
            file["path"] = path.c_str();
            file["byteCount"] = static_cast<std::int64_t>(bytes.bytes.size());
            file["contentHash"] = hex;
            index["files"].get_value_ref<Yaml::Node::sequence_type&>().push_back(std::move(file));
        }
        const auto text = Yaml::serialize(index);
        require(text.size() <= maxMetadataBytes, "PACKAGE-CLOSURE-METADATA-BUDGET");
        const auto indexPath = root / std::filesystem::path(package.closurePath.c_str());
        std::filesystem::create_directories(indexPath.parent_path());
        core::ByteBuffer bytes(text.begin(), text.end(), &core::defaultCpuMemoryResource());
        const auto saved = core::io::replaceFileAtomically(core::FilePath::utf8(indexPath), bytes);
        if (!saved.success) {
            result.diagnostics = saved.diagnostic;
            return result;
        }
        return inspect(root);
    } catch (const core::YamlContractError& error) {
        result.diagnostics = error.diagnostic;
    } catch (...) {
        result.diagnostics = "PACKAGE-CLOSURE-PREPARATION-FAILED";
    }
    return result;
}

AssetPackageClosureResult AssetPackageClosureService::publish(const std::filesystem::path& prepared,
    const std::filesystem::path& output,
    const AssetPackageClosureBudget& limits) const
{
    auto result = inspect(prepared, limits);
    if (!result.success)
        return result;
    result.success = false;
    try {
        core::io::StagedDirectory staging;
        if (!staging.begin(output)) {
            result.diagnostics = staging.diagnostic();
            return result;
        }
        const auto root = std::filesystem::canonical(prepared);
        for (const auto& file : result.files)
            if (!copyContent(root, staging.path(), file, result.diagnostics))
                return result;
        const auto closurePath =
            staging.path() / std::filesystem::path(result.package.closurePath.c_str());
        std::filesystem::create_directories(closurePath.parent_path());
        core::ByteBuffer bytes(
            result.indexYaml.begin(), result.indexYaml.end(), &core::defaultCpuMemoryResource());
        const auto indexWritten =
            core::io::replaceFileAtomically(core::FilePath::utf8(closurePath), bytes);
        if (!indexWritten.success) {
            result.diagnostics = indexWritten.diagnostic;
            return result;
        }
        const auto packaged = InternalAssetPackageWriter().write(result.package, staging.path());
        if (!packaged.success) {
            result.diagnostics = packaged.diagnostics;
            return result;
        }
        ResourceManager candidate;
        ResourceBudgetConfig decodeBudget;
        decodeBudget.maxDecodedBytes = result.maxDecodedBytes;
        candidate.setBudgetConfig(decodeBudget);
        const auto verified = InternalAssetPackageReader().load(staging.path(), candidate);
        if (!verified.success) {
            result.diagnostics = verified.diagnostics;
            return result;
        }
        if (!staging.publish()) {
            result.diagnostics = staging.diagnostic();
            return result;
        }
        result.success = true;
    } catch (const core::YamlContractError& error) {
        result.diagnostics = error.diagnostic;
    } catch (...) {
        result.diagnostics = "PACKAGE-CLOSURE-PUBLISH-FAILED";
    }
    return result;
}
} // namespace arp::resource

#include "resource/asset_package_closure.h"

#include "core/encoding/yaml_contract.h"
#include "core/hash/stable_hash.h"
#include "core/io/atomic_file_service.h"
#include "core/io/file_io.h"
#include "core/io/filesystem_path.h"
#include "core/io/staged_directory.h"
#include "resource/asset_package_payload.h"
#include "resource/resource_source_io.h"
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

std::filesystem::path contained(const std::filesystem::path& root,
    const core::pmr::String& path,
    const AssetPackageSourceContext* sources = nullptr)
{
    require(safePath(path), "PACKAGE-CLOSURE-UNSAFE-PATH");
    if (sources) {
        std::filesystem::path output;
        require(resolveResourceSourcePath(root, path, sources->origin, output),
            "PACKAGE-CLOSURE-UNSAFE-PATH");
        return output;
    }
    std::error_code error;
    const auto resolved =
        std::filesystem::canonical(root / std::filesystem::u8path(path.begin(), path.end()), error);
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
    core::pmr::UnorderedSet<core::pmr::String>& required,
    core::SharedPtr<const AssetPackageCodecRegistry> codecs,
    const AssetPackageSourceContext* sources = nullptr)
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
    const auto dependency = [&](const ResourceDependency& binding, NameView type) {
        const auto found = std::find_if(
            result.package.entries.begin(), result.package.entries.end(), [&](const auto& e) {
                return e.resourceId == binding.id;
            });
        require(found != result.package.entries.end() && found->resourceType == type,
            "PACKAGE-CLOSURE-DEPENDENCY-MISSING-OR-TYPE");
    };
    AssetPackagePayloadInspector payloads = sources
        ? AssetPackagePayloadInspector(codecs, root, *sources)
        : AssetPackagePayloadInspector(codecs, root);
    std::size_t codecDecodedBytes = 0;
    for (const auto& entry : result.package.entries) {
        require(entry.status == "Contract Complete", "PACKAGE-CLOSURE-RESOURCE-STATUS");
        if (payloads.supports(entry.resourceType)) {
            const auto payload = payloads.inspect(entry, true);
            if (!payload.success) {
                result.diagnostics = payload.diagnostics;
                return false;
            }
            require(payload.maxDecodedBytes <= result.maxDecodedBytes - codecDecodedBytes,
                "PACKAGE-CODEC-DECODED-BUDGET");
            codecDecodedBytes += payload.maxDecodedBytes;
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
        const auto manifest = contained(root, path, sources);
        const ResourceReference reference{
            entry.resourceId, entry.resourceType, entry.alias, entry.status, {}};
        core::pmr::Vector<ResourceFileReadIntent> intents{&core::defaultCpuMemoryResource()};
        ResourceLoadContext context = sources ? sources->resources : ResourceLoadContext{};
        context.maxDecodedBytes = result.maxDecodedBytes;
        if (entry.resourceType == TerrainAssetResource::resourceTypeName()) {
            const auto terrain = TerrainAssetResource(
                reference, manifest, sources ? sources->origin : ResourceFileOrigin{})
                                     .inspectManifest(context);
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
            const auto catalog = VegetationCatalogCookedCodec().inspect(
                reference, manifest, context, sources ? sources->origin : ResourceFileOrigin{});
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
            addFile(
                core::FilePath::utf8(std::filesystem::u8path(intent.path.begin(), intent.path.end())
                        .lexically_relative(root)));
    }
    return true;
}

struct ClosureDeclaration {
    Yaml::Node index;
    core::pmr::Vector<AssetPackageClosureFile> files{&core::defaultCpuMemoryResource()};
    std::size_t maxFiles = 0;
    std::size_t maxDecodedBytes = 0;
};

ClosureDeclaration parseClosureDeclaration(const InternalAssetPackageDesc& package,
    core::StringView text,
    const AssetPackageClosureBudget& limits,
    std::size_t callerDecodedBytes)
{
    ClosureDeclaration declaration;
    auto index = Yaml::parseSingleDocument(text);
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
            && package.status == "Contract Complete",
        "PACKAGE-CLOSURE-CONTRACT");
    const auto resources = Yaml::uint64(Yaml::required(index, "maxResources"));
    auto maxFiles = Yaml::uint64(Yaml::required(index, "maxFiles"));
    auto maxFile = Yaml::uint64(Yaml::required(index, "maxFileBytes"));
    auto maxTotal = Yaml::uint64(Yaml::required(index, "maxTotalFileBytes"));
    const auto decoded = Yaml::uint64(Yaml::required(index, "maxDecodedBytes"));
    require(resources && resources <= AssetPackageClosureService::maxResources
            && package.entries.size() <= resources && maxFiles && maxFiles <= SIZE_MAX && maxFile
            && maxFile <= SIZE_MAX && maxTotal && maxTotal <= SIZE_MAX && decoded
            && decoded <= SIZE_MAX,
        "PACKAGE-CLOSURE-BUDGET");
    require(package.entries.size()
            <= constrained(AssetPackageClosureService::maxResources, limits.maxResources),
        "PACKAGE-CLOSURE-BUDGET");
    maxFiles = constrained(static_cast<std::size_t>(maxFiles), limits.maxFiles);
    maxFile = constrained(static_cast<std::size_t>(maxFile), limits.maxFileBytes);
    maxTotal = constrained(static_cast<std::size_t>(maxTotal), limits.maxTotalFileBytes);
    declaration.maxDecodedBytes =
        constrained(static_cast<std::size_t>(decoded), limits.maxDecodedBytes);
    declaration.maxDecodedBytes = constrained(declaration.maxDecodedBytes, callerDecodedBytes);
    const auto& files = Yaml::sequence(index, "files");
    require(files.size() <= maxFiles, "PACKAGE-CLOSURE-FILE-COUNT");

    std::size_t total = 0;
    for (const auto& node : files) {
        Yaml::onlyKeys(node, {"path", "byteCount", "contentHash"});
        AssetPackageClosureFile file;
        file.path = Yaml::text(node, "path");
        const auto bytes = Yaml::uint64(Yaml::required(node, "byteCount"));
        file.contentHash = Yaml::hex64(node, "contentHash");
        require(safePath(file.path) && bytes <= maxFile && total <= maxTotal
                && bytes <= maxTotal - total,
            "PACKAGE-CLOSURE-FILE-BUDGET");
        require(declaration.files.empty() || declaration.files.back().path < file.path,
            "PACKAGE-CLOSURE-FILE-SET-OR-ORDER");
        require(file.path != "manifest.arpkg.yaml" && file.path != package.closurePath,
            "PACKAGE-CLOSURE-METADATA-COLLISION");
        for (const auto& entry : package.entries)
            require(file.path != entry.envelopePath, "PACKAGE-CLOSURE-METADATA-COLLISION");
        file.byteCount = static_cast<std::size_t>(bytes);
        total += file.byteCount;
        declaration.files.push_back(std::move(file));
    }
    declaration.maxFiles = static_cast<std::size_t>(maxFiles);
    declaration.index = std::move(index);
    return declaration;
}

} // namespace

AssetPackageClosureResult AssetPackageClosureService::inspect(
    const std::filesystem::path& directory, const AssetPackageClosureBudget& limits) const
{
    return inspectImpl(directory, limits, nullptr);
}

AssetPackageClosureResult AssetPackageClosureService::inspectSources(
    const std::filesystem::path& directory,
    const AssetPackageSourceContext& sources,
    const AssetPackageClosureBudget& limits) const
{
    if (!sources.resources.requirePreloadedSources || !sources.resources.sources)
        return {false, "PACKAGE-IO-SOURCE-CONFIG"};
    return inspectImpl(directory, limits, &sources);
}

AssetPackageReadPlan AssetPackageClosureService::planSourceReads(
    const std::filesystem::path& directory,
    const AssetPackageSourceContext& sources,
    const AssetPackageClosureBudget& limits) const
{
    AssetPackageReadPlan result;
    try {
        auto package = InternalAssetPackageReader().readSources(
            directory, sources, maxMetadataBytes, constrained(maxResources, limits.maxResources));
        if (!package.success) {
            result.diagnostics = std::move(package.diagnostics);
            return result;
        }
        require(!package.package.closurePath.empty(), "PACKAGE-CLOSURE-DECLARATION-REQUIRED");
        const auto path = contained(directory, package.package.closurePath, &sources);
        const auto text = readResourceText(
            core::FilePath::utf8(path), sources.resources, maxMetadataBytes, &sources.origin);
        if (!text.success) {
            result.diagnostics = text.diagnostics;
            return result;
        }
        auto declaration = parseClosureDeclaration(
            package.package, text.text, limits, sources.resources.maxDecodedBytes);
        for (const auto& file : declaration.files) {
            const auto sourcePath = contained(directory, file.path, &sources);
            if (!appendResourceReadIntent(core::FilePath::utf8(sourcePath),
                    std::max<std::size_t>(1, file.byteCount),
                    result.reads,
                    result.diagnostics,
                    sources.origin))
                return result;
        }
        result.success = true;
    } catch (const core::YamlContractError& error) {
        result.diagnostics = error.diagnostic;
    }
    return result;
}

AssetPackageClosureResult AssetPackageClosureService::inspectImpl(
    const std::filesystem::path& directory,
    const AssetPackageClosureBudget& limits,
    const AssetPackageSourceContext* sources) const
{
    AssetPackageClosureResult result;
    try {
        require(!codecs_ || codecs_->frozen(), "PACKAGE-CODEC-REGISTRY-NOT-FROZEN");
        const auto package = sources
            ? InternalAssetPackageReader().readSources(directory,
                  *sources,
                  maxMetadataBytes,
                  constrained(maxResources, limits.maxResources))
            : InternalAssetPackageReader().read(
                  directory, maxMetadataBytes, constrained(maxResources, limits.maxResources));
        if (!package.success) {
            result.diagnostics = package.diagnostics;
            return result;
        }
        result.package = package.package;
        require(!result.package.closurePath.empty(), "PACKAGE-CLOSURE-DECLARATION-REQUIRED");
        const auto root = sources ? directory : std::filesystem::canonical(directory);
        const auto indexPath = contained(root, result.package.closurePath, sources);
        const auto indexFile = sources ? readResourceText(core::FilePath::utf8(indexPath),
                                             sources->resources,
                                             maxMetadataBytes,
                                             &sources->origin)
                                       : core::readTextFile(indexPath, maxMetadataBytes);
        require(indexFile.success, "PACKAGE-CLOSURE-INDEX-READ");
        auto declaration = parseClosureDeclaration(result.package,
            indexFile.text,
            limits,
            sources ? sources->resources.maxDecodedBytes : 0);
        result.maxDecodedBytes = declaration.maxDecodedBytes;
        core::pmr::UnorderedSet<core::pmr::String> required{&core::defaultCpuMemoryResource()};
        if (!discoverFiles(root, result, declaration.maxFiles, required, codecs_, sources))
            return result;
        for (auto& file : declaration.files) {
            require(
                required.find(file.path) != required.end(), "PACKAGE-CLOSURE-FILE-SET-OR-ORDER");
            const auto resolved = contained(root, file.path, sources);
            require(resolved != contained(root, core::pmr::String("manifest.arpkg.yaml"), sources)
                    && resolved != contained(root, result.package.closurePath, sources),
                "PACKAGE-CLOSURE-METADATA-COLLISION");
            for (const auto& entry : result.package.entries)
                require(resolved != contained(root, entry.envelopePath, sources),
                    "PACKAGE-CLOSURE-METADATA-COLLISION");
            const auto content = sources ? readResourceSource(core::FilePath::utf8(resolved),
                                               sources->resources,
                                               std::max<std::size_t>(1, file.byteCount),
                                               true,
                                               &sources->origin)
                                         : core::readBinaryFile(resolved, file.byteCount);
            require(content.success && content.bytes.size() == file.byteCount
                    && hashBytes(content.bytes) == file.contentHash,
                "PACKAGE-CLOSURE-FILE-HASH-OR-SIZE");
            result.files.push_back(std::move(file));
        }
        require(result.files.size() == required.size(), "PACKAGE-CLOSURE-FILE-MISSING");
        result.indexYaml = Yaml::serialize(declaration.index);
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
        require(!codecs_ || codecs_->frozen(), "PACKAGE-CODEC-REGISTRY-NOT-FROZEN");
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
        if (!discoverFiles(root, result, budget.maxFiles, required, codecs_))
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
        const auto verified = InternalAssetPackageReader().load(staging.path(), candidate, codecs_);
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

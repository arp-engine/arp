#include "resource/vegetation_catalog_build_service.h"

#include "assets/glb_static_mesh_importer.h"
#include "core/encoding/yaml_contract.h"
#include "core/io/file_io.h"
#include "core/io/filesystem_path.h"
#include "core/io/staged_directory.h"
#include "resource/asset_package_closure.h"
#include "resource/vegetation_catalog_layout.h"
#include "resource/vegetation_template_geometry_builder.h"

namespace arp::resource {
namespace {
using Yaml = core::YamlContract;

void require(bool value, const char* diagnostic)
{
    if (!value)
        throw core::YamlContractError{diagnostic};
}

std::size_t positive(const Yaml::Node& node, const char* field)
{
    const auto value = Yaml::uint64(Yaml::required(node, field));
    require(value && value <= SIZE_MAX, "CATALOG-AUTHORING-BUDGET");
    return static_cast<std::size_t>(value);
}

AssetPackageClosureBudget packageBudget(const Yaml::Node& root)
{
    const auto& node = Yaml::required(root, "packageBudget");
    Yaml::onlyKeys(
        node, {"maxResources", "maxFiles", "maxFileBytes", "maxTotalFileBytes", "maxDecodedBytes"});
    AssetPackageClosureBudget result{positive(node, "maxResources"),
        positive(node, "maxFiles"),
        positive(node, "maxFileBytes"),
        positive(node, "maxTotalFileBytes"),
        positive(node, "maxDecodedBytes")};
    require(result.maxResources <= AssetPackageClosureService::maxResources,
        "CATALOG-AUTHORING-BUDGET");
    return result;
}

std::filesystem::path resolve(const std::filesystem::path& root, core::StringView relative)
{
    require(relative.find('\0') == core::StringView::npos
            && core::FilePath::isSafeRelativePath(relative),
        "CATALOG-AUTHORING-PATH");
    std::error_code error;
    const auto resolved = std::filesystem::canonical(root / std::filesystem::path(relative), error);
    require(!error && core::FilePath::contains(root, resolved), "CATALOG-AUTHORING-PATH");
    return resolved;
}

InternalAssetPackageEntry catalogEntry(const ResourceReference& reference)
{
    auto payload = Yaml::Node::mapping();
    payload["type"] = "VegetationCatalogResource";
    payload["fields"] = Yaml::Node::mapping();
    payload["fields"]["manifestPath"] = "catalog/catalog.cooked.yaml";
    const auto hash = computeResourcePayloadHash(Yaml::serialize(payload));
    require(hash.success, "CATALOG-AUTHORING-ENVELOPE");
    auto envelope = Yaml::Node::mapping();
    envelope["format"] = "ArpRuntimeResource";
    envelope["version"] = 1;
    envelope["resourceId"] = reference.id.c_str();
    envelope["resourceType"] = reference.type.c_str();
    envelope["alias"] = reference.alias.c_str();
    envelope["status"] = reference.status.c_str();
    envelope["contentHash"] = hash.hash;
    envelope["dependencies"] = Yaml::Node::sequence();
    envelope["payload"] = std::move(payload);
    InternalAssetPackageEntry entry;
    entry.resourceId = reference.id;
    entry.resourceType = reference.type;
    entry.alias = reference.alias;
    entry.status = reference.status;
    entry.envelopePath = "resources/catalog.arpres.yaml";
    entry.contentHash = hash.hash;
    entry.envelopeYaml = Yaml::serialize(envelope);
    return entry;
}
} // namespace

VegetationCatalogPrepareResult VegetationCatalogBuildService::prepare(
    std::string_view yaml, const std::filesystem::path& sourceRoot) const
{
    VegetationCatalogPrepareResult result;
    std::shared_ptr<core::CpuMemoryQuota> quota;
    try {
        require(yaml.size() <= maxAuthoringBytes, "CATALOG-AUTHORING-METADATA-BUDGET");
        const auto root = Yaml::parseSingleDocument(yaml);
        Yaml::onlyKeys(root,
            {"type",
                "contractVersion",
                "resourceId",
                "alias",
                "inputContract",
                "maxSourceBytes",
                "maxTotalSourceBytes",
                "maxImportBytes",
                "maxDecodedBytes",
                "maxTemplates",
                "maxVertices",
                "maxIndices",
                "maxPrimitives",
                "maxJsonDepth",
                "maxJsonValues",
                "dependencyPackage",
                "packageBudget",
                "assets"});
        require(Yaml::text(root, "type") == "VegetationCatalogAuthoring"
                && Yaml::text(root, "contractVersion") == "vegetation.catalog.authoring/v1"
                && Yaml::text(root, "inputContract") == "static-glb-geometry/v1",
            "CATALOG-AUTHORING-CONTRACT");
        result.reference.id = Yaml::text(root, "resourceId");
        result.reference.type = "VegetationCatalogResource";
        result.reference.status = "Contract Complete";
        result.reference.alias = Yaml::text(root, "alias");
        require(isResourceUuid(result.reference.id)
                && result.reference.alias.find('\0') == core::pmr::String::npos,
            "CATALOG-AUTHORING-IDENTITY");
        const auto closedBudget = packageBudget(root);
        const auto dependency = Yaml::text(root, "dependencyPackage");
        require(!dependency.empty() && dependency.find('\0') == core::pmr::String::npos
                && core::FilePath::isSafeRelativePath(dependency),
            "CATALOG-AUTHORING-PATH");
        const auto decodedLimit =
            std::min(positive(root, "maxDecodedBytes"), closedBudget.maxDecodedBytes);
        const auto sourceLimit = positive(root, "maxSourceBytes");
        const auto totalLimit = positive(root, "maxTotalSourceBytes");
        const auto templateLimit = positive(root, "maxTemplates");
        const auto importLimit = positive(root, "maxImportBytes");
        require(sourceLimit <= UINT32_MAX
                && templateLimit <= VegetationCatalogCookedCodec::maxTemplates,
            "CATALOG-AUTHORING-BUDGET");
        assets::StaticMeshSourceImportLimits limits;
        limits.maxSourceBytes = sourceLimit;
        limits.maxVertices = Yaml::uint32(root, "maxVertices");
        limits.maxIndices = Yaml::uint32(root, "maxIndices");
        limits.maxPrimitives = Yaml::uint32(root, "maxPrimitives");
        limits.strictGeometryOnly = true;
        limits.maxJsonDepth = Yaml::uint32(root, "maxJsonDepth");
        limits.maxJsonValues = Yaml::uint32(root, "maxJsonValues");
        require(limits.maxVertices && limits.maxIndices && limits.maxPrimitives
                && limits.maxJsonDepth && limits.maxJsonDepth <= 64u && limits.maxJsonValues,
            "CATALOG-AUTHORING-BUDGET");
        const auto& entries = Yaml::sequence(root, "assets");
        require(!entries.empty() && entries.size() <= templateLimit,
            "CATALOG-AUTHORING-TEMPLATE-BUDGET");
        const auto base = std::filesystem::canonical(sourceRoot);
        quota = core::makeShared<core::CpuMemoryQuota>(importLimit);
        core::CpuMemoryQuotaScope scope(quota);
        auto data = core::makeShared<VegetationCatalogResourceData>();
        data->contractVersion = 1u;
        data->status = result.reference.status;
        data->templateConfig.hasEnvironmentAssets = true;
        for (const auto& node : entries) {
            Yaml::onlyKeys(node,
                {"assetId",
                    "source",
                    "role",
                    "category",
                    "heightClass",
                    "templateCategory",
                    "templateId",
                    "variantIndex",
                    "lodIndex",
                    "metersPerUnit",
                    "baseColor",
                    "wind",
                    "materials"});
            VegetationCatalogAssetEntry entry;
            entry.id = Yaml::text(node, "assetId");
            entry.source = Yaml::text(node, "source");
            entry.role = Yaml::text(node, "role");
            entry.category = Yaml::text(node, "category");
            entry.heightClass = Yaml::text(node, "heightClass");
            for (const auto* value : {&entry.id, &entry.role, &entry.category, &entry.heightClass})
                require(!value->empty() && value->find('\0') == core::pmr::String::npos,
                    "CATALOG-AUTHORING-LABEL");
            require(detail::CatalogFamilyContract::parse(
                        Yaml::text(node, "templateCategory"), entry.templateCategory)
                    && Yaml::uint32(node, "templateId") == data->assets.size() + 1u
                    && Yaml::uint32(node, "variantIndex")
                        == vegetation::familyVariantCount(
                            data->templateConfig, entry.templateCategory)
                    && Yaml::uint32(node, "lodIndex") == 0u,
                "CATALOG-AUTHORING-LAYOUT");
            entry.templateId = static_cast<std::uint16_t>(data->assets.size() + 1u);
            for (const auto& previous : data->assets)
                require(previous.id != entry.id, "CATALOG-AUTHORING-LAYOUT");
            if (!data->assets.empty()) {
                const auto& previous = data->assets.back();
                require(previous.templateCategory < entry.templateCategory
                        || (previous.templateCategory == entry.templateCategory
                            && previous.id < entry.id),
                    "CATALOG-AUTHORING-LAYOUT");
            }
            require(detail::CatalogFamilyContract::increment(
                        data->templateConfig, entry.templateCategory),
                "CATALOG-AUTHORING-LAYOUT");
            const auto path = resolve(base, entry.source);
            std::error_code error;
            require(
                std::filesystem::is_regular_file(path, error) && !error, "CATALOG-AUTHORING-PATH");
            const auto size = std::filesystem::file_size(path, error);
            require(!error && size <= sourceLimit && result.sourceBytes <= totalLimit
                    && size <= totalLimit - result.sourceBytes,
                "CATALOG-AUTHORING-SOURCE-BUDGET");
            result.sourceBytes += size;
            auto sourceLimits = limits;
            sourceLimits.maxSourceBytes = size;
            auto imported =
                assets::importStaticMeshWithSourceIdentityFromGlbFile(path, sourceLimits);
            if (!imported.geometry.success) {
                result.diagnostics = imported.geometry.diagnostics;
                return result;
            }
            const auto& mesh = imported.geometry.mesh;
            const auto bytes =
                std::uint64_t(mesh.vertices.size()) * sizeof(vegetation::InstanceVertex)
                + mesh.indices.size() * sizeof(std::uint32_t)
                + mesh.submeshes.size() * sizeof(vegetation::VegetationTemplate::Section);
            require(
                result.decodedBytes <= decodedLimit && bytes <= decodedLimit - result.decodedBytes,
                "CATALOG-AUTHORING-DECODE-BUDGET");
            result.decodedBytes += static_cast<std::size_t>(bytes);
            const auto metersPerUnit = Yaml::number(node, "metersPerUnit");
            require(metersPerUnit > 0 && metersPerUnit <= std::numeric_limits<float>::max(),
                "CATALOG-AUTHORING-UNITS");
            const auto wind = Yaml::floatArray<3>(node, "wind");
            for (auto weight : wind)
                require(weight >= 0 && weight <= 1, "CATALOG-AUTHORING-WIND");
            const auto& bindings = Yaml::sequence(node, "materials");
            require(!bindings.empty() && bindings.size() <= mesh.submeshes.size(),
                "CATALOG-AUTHORING-MATERIALS");
            for (const auto& binding : bindings) {
                Yaml::onlyKeys(binding, {"slot", "resourceId"});
                VegetationCatalogAssetEntry::MaterialBinding value;
                value.slot = Yaml::uint32(binding, "slot");
                value.resourceId = Yaml::text(binding, "resourceId");
                require(isResourceUuid(value.resourceId)
                        && (entry.materials.empty() || entry.materials.back().slot < value.slot),
                    "CATALOG-AUTHORING-MATERIALS");
                entry.materials.push_back(std::move(value));
            }
            auto geometry = detail::VegetationTemplateGeometryBuilder::build(mesh);
            geometry.id = entry.templateId;
            geometry.category = entry.templateCategory;
            geometry.variantIndex = static_cast<std::uint16_t>(Yaml::uint32(node, "variantIndex"));
            const auto color = Yaml::floatArray<4>(node, "baseColor");
            std::copy(color.begin(), color.end(), geometry.baseColor);
            for (auto& converted : geometry.vertices) {
                for (unsigned axis = 0; axis < 3u; ++axis) {
                    const double position = converted.position[axis] * metersPerUnit;
                    require(std::isfinite(position)
                            && std::abs(position) <= std::numeric_limits<float>::max(),
                        "CATALOG-AUTHORING-UNITS");
                    converted.position[axis] = static_cast<float>(position);
                    require(
                        position == 0 || converted.position[axis] != 0, "CATALOG-AUTHORING-UNITS");
                }
                converted.primaryBend = wind[0];
                converted.flutter = wind[1];
                converted.stiffness = wind[2];
            }
            data->templateIdsByAssetId.emplace(entry.id, entry.templateId);
            data->assets.push_back(std::move(entry));
            data->templates.push_back(std::move(geometry));
        }
        result.importPeakBytes = quota->peakBytes();
        result.data = std::move(data);
        result.success = true;
    } catch (const core::YamlContractError& error) {
        result.diagnostics = error.diagnostic;
    } catch (const std::bad_alloc&) {
        result.diagnostics = "CATALOG-AUTHORING-IMPORT-BUDGET";
    } catch (...) {
        result.diagnostics = "CATALOG-AUTHORING-INVALID";
    }
    return result;
}

VegetationCatalogCookedResult VegetationCatalogBuildService::build(
    const std::filesystem::path& authoringFile, const std::filesystem::path& outputDirectory) const
{
    VegetationCatalogCookedResult result;
    try {
        const auto file = core::readTextFile(authoringFile, maxAuthoringBytes);
        if (!file.success) {
            result.diagnostics = file.diagnostics;
            return result;
        }
        const auto prepared = prepare(file.text, authoringFile.parent_path());
        if (!prepared.success) {
            result.diagnostics = prepared.diagnostics;
            return result;
        }
        const auto root = Yaml::parseSingleDocument(file.text);
        const auto budget = packageBudget(root);
        const auto dependency = resolve(std::filesystem::canonical(authoringFile.parent_path()),
            Yaml::text(root, "dependencyPackage"));
        AssetPackageClosureService closure;
        const auto inspected = closure.inspect(dependency, budget);
        if (!inspected.success) {
            result.diagnostics = inspected.diagnostics;
            return result;
        }
        require(inspected.package.entries.size() < budget.maxResources,
            "CATALOG-AUTHORING-PACKAGE-BUDGET");
        for (const auto& entry : inspected.package.entries)
            require(entry.resourceId != prepared.reference.id,
                "CATALOG-AUTHORING-DEPENDENCY-COLLISION");
        auto privateName = outputDirectory;
        privateName += ".authoring";
        core::io::StagedDirectory preparation;
        if (!preparation.begin(privateName)) {
            result.diagnostics = preparation.diagnostic();
            return result;
        }
        const auto packageRoot = preparation.path() / "package";
        auto copied = closure.publish(dependency, packageRoot, budget);
        if (!copied.success) {
            result.diagnostics = copied.diagnostics;
            return result;
        }
        require(!std::filesystem::exists(packageRoot / "catalog")
                && !std::filesystem::exists(packageRoot / "resources/catalog.arpres.yaml"),
            "CATALOG-AUTHORING-DEPENDENCY-COLLISION");
        result = VegetationCatalogCookedCodec().write(
            prepared.reference, *prepared.data, packageRoot / "catalog", budget.maxDecodedBytes);
        if (!result.success)
            return result;
        copied.package.name = prepared.reference.id;
        copied.package.status = "Contract Complete";
        copied.package.entries.push_back(catalogEntry(prepared.reference));
        const auto indexed = closure.prepareIndex(copied.package, packageRoot, budget);
        if (!indexed.success) {
            result.success = false;
            result.diagnostics = indexed.diagnostics;
            return result;
        }
        const auto published = closure.publish(packageRoot, outputDirectory, budget);
        result.success = published.success;
        result.diagnostics = published.diagnostics;
    } catch (const core::YamlContractError& error) {
        result.success = false;
        result.diagnostics = error.diagnostic;
    } catch (...) {
        result.success = false;
        result.diagnostics = "CATALOG-AUTHORING-COMPOSITION-FAILED";
    }
    return result;
}
} // namespace arp::resource

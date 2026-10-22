#include "resource/vegetation_catalog_resource.h"

#include "assets/glb_static_mesh_importer.h"
#include "core/diagnostics/diagnostic_builder.h"
#include "core/io/file_io.h"
#include "reflection/serialization.h"
#include "resource/prefab_asset_resources.h"
#include "resource/resource_source_io.h"
#include "resource/vegetation_catalog_cooked.h"
#include "resource/vegetation_catalog_layout.h"
#include "resource/vegetation_template_geometry_builder.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <string>

namespace arp::resource {

namespace {

using arp::reflection::registration::class_;

core::pmr::String makeString(std::string_view text)
{
    return core::pmr::String(text.data(), text.size(), &core::defaultCpuMemoryResource());
}

core::pmr::String makeDiagnostic(std::string_view text)
{
    return makeString(text);
}

template <typename T> core::pmr::String makeDiagnostic(std::string_view prefix, const T& value)
{
    core::pmr::String result = makeString(prefix);
    result += value;
    return result;
}

std::string toLowerAscii(std::string text)
{
    for (auto& ch : text) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return text;
}

bool startsWith(std::string_view text, std::string_view prefix) noexcept
{
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

bool isAllowedValue(
    std::string_view value, std::initializer_list<std::string_view> allowed) noexcept
{
    for (const auto candidate : allowed) {
        if (candidate == value) {
            return true;
        }
    }
    return false;
}

std::filesystem::path resolveRelativePath(
    const std::filesystem::path& baseDir, std::string_view pathText)
{
    const std::filesystem::path path(pathText);
    return path.is_absolute() ? path : baseDir / path;
}

vegetation::TemplateCategory classifyTemplateCategory(std::string_view sourceStem)
{
    const auto stem = toLowerAscii(std::string(sourceStem));

    if (startsWith(stem, "grass_common_") || startsWith(stem, "grass_wispy_")) {
        return vegetation::TemplateCategory::FoliageGrass;
    }
    if (startsWith(stem, "clover_")) {
        return vegetation::TemplateCategory::FoliageBroadleaf;
    }
    if (startsWith(stem, "fern_")) {
        return vegetation::TemplateCategory::FoliageFern;
    }
    if (startsWith(stem, "bush_common") || startsWith(stem, "plant_1")
        || startsWith(stem, "plant_7")) {
        return vegetation::TemplateCategory::FoliageBush;
    }
    if (startsWith(stem, "flower_") || startsWith(stem, "petal_")) {
        return vegetation::TemplateCategory::FoliageFlower;
    }
    if (startsWith(stem, "mushroom_")) {
        return vegetation::TemplateCategory::FoliageMushroom;
    }
    if (startsWith(stem, "commontree_") || startsWith(stem, "twistedtree_")) {
        return vegetation::TemplateCategory::TreeMaturePine;
    }
    if (startsWith(stem, "deadtree_")) {
        return vegetation::TemplateCategory::TreeYoungFir;
    }
    if (startsWith(stem, "pine_")) {
        return vegetation::TemplateCategory::TreeYoungPine;
    }
    if (startsWith(stem, "pebble_") || startsWith(stem, "rock_medium_")) {
        return vegetation::TemplateCategory::RockMossy01;
    }
    if (startsWith(stem, "rockpath_")) {
        return vegetation::TemplateCategory::RockMossy02;
    }

    return vegetation::TemplateCategory::ProceduralGrass;
}

vegetation::VegetationTemplate makeVegetationTemplate(const VegetationCatalogAssetEntry& entry,
    std::uint16_t templateId,
    const assets::MeshAsset& mesh,
    const std::pmr::vector<assets::ImportedMaterialAsset>& materials)
{
    auto templateData = detail::VegetationTemplateGeometryBuilder::build(mesh);
    templateData.category = entry.templateCategory;
    templateData.id = templateId;
    templateData.variantIndex = 0u;
    templateData.lodIndex = 0u;

    if (!mesh.submeshes.empty()) {
        templateData.materialIndex = mesh.submeshes.front().materialSlot;
        if (templateData.materialIndex < materials.size()) {
            const auto& material = materials[templateData.materialIndex];
            templateData.baseColor[0] = material.baseColorFactor[0];
            templateData.baseColor[1] = material.baseColorFactor[1];
            templateData.baseColor[2] = material.baseColorFactor[2];
            templateData.baseColor[3] = material.baseColorFactor[3];
        }
    }

    return templateData;
}

core::pmr::Vector<const VegetationCatalogAssetEntry*> flattenAssetsByFamily(
    const core::pmr::Vector<VegetationCatalogAssetEntry>& assets, core::pmr::String& diagnostics)
{
    std::array<core::pmr::Vector<const VegetationCatalogAssetEntry*>, 12> buckets{
        core::pmr::Vector<const VegetationCatalogAssetEntry*>{&core::defaultCpuMemoryResource()},
        core::pmr::Vector<const VegetationCatalogAssetEntry*>{&core::defaultCpuMemoryResource()},
        core::pmr::Vector<const VegetationCatalogAssetEntry*>{&core::defaultCpuMemoryResource()},
        core::pmr::Vector<const VegetationCatalogAssetEntry*>{&core::defaultCpuMemoryResource()},
        core::pmr::Vector<const VegetationCatalogAssetEntry*>{&core::defaultCpuMemoryResource()},
        core::pmr::Vector<const VegetationCatalogAssetEntry*>{&core::defaultCpuMemoryResource()},
        core::pmr::Vector<const VegetationCatalogAssetEntry*>{&core::defaultCpuMemoryResource()},
        core::pmr::Vector<const VegetationCatalogAssetEntry*>{&core::defaultCpuMemoryResource()},
        core::pmr::Vector<const VegetationCatalogAssetEntry*>{&core::defaultCpuMemoryResource()},
        core::pmr::Vector<const VegetationCatalogAssetEntry*>{&core::defaultCpuMemoryResource()},
        core::pmr::Vector<const VegetationCatalogAssetEntry*>{&core::defaultCpuMemoryResource()},
        core::pmr::Vector<const VegetationCatalogAssetEntry*>{&core::defaultCpuMemoryResource()},
    };

    auto bucketIndex = [](vegetation::TemplateCategory category) -> std::size_t {
        switch (category) {
        case vegetation::TemplateCategory::FoliageGrass:
            return 0u;
        case vegetation::TemplateCategory::FoliageWeed:
            return 1u;
        case vegetation::TemplateCategory::FoliageBroadleaf:
            return 2u;
        case vegetation::TemplateCategory::FoliageFern:
            return 3u;
        case vegetation::TemplateCategory::FoliageBush:
            return 4u;
        case vegetation::TemplateCategory::FoliageFlower:
            return 5u;
        case vegetation::TemplateCategory::FoliageMushroom:
            return 6u;
        case vegetation::TemplateCategory::TreeMaturePine:
            return 7u;
        case vegetation::TemplateCategory::TreeYoungFir:
            return 8u;
        case vegetation::TemplateCategory::TreeYoungPine:
            return 9u;
        case vegetation::TemplateCategory::RockMossy01:
            return 10u;
        case vegetation::TemplateCategory::RockMossy02:
            return 11u;
        default:
            return std::numeric_limits<std::size_t>::max();
        }
    };

    for (const auto& asset : assets) {
        const auto index = bucketIndex(asset.templateCategory);
        if (index == std::numeric_limits<std::size_t>::max()) {
            diagnostics = makeDiagnostic("unsupported vegetation template category for asset: ");
            diagnostics += asset.id;
            return {};
        }
        buckets[index].push_back(&asset);
    }

    for (auto& bucket : buckets) {
        std::sort(bucket.begin(), bucket.end(), [](const auto* lhs, const auto* rhs) {
            return lhs->id < rhs->id;
        });
    }

    core::pmr::Vector<const VegetationCatalogAssetEntry*> flattened{
        &core::defaultCpuMemoryResource()};
    flattened.reserve(assets.size());
    for (auto& bucket : buckets) {
        for (const auto* asset : bucket) {
            flattened.push_back(asset);
        }
    }
    return flattened;
}

void registerVegetationCatalogReflection()
{
    static const bool registered = []() {
        class_<VegetationCatalogSourceInfo>("VegetationCatalogSourceInfo")
            .construct<>()
            .property("creator", &VegetationCatalogSourceInfo::creator)
            .property("pack", &VegetationCatalogSourceInfo::pack)
            .property("sourceUrl", &VegetationCatalogSourceInfo::sourceUrl)
            .property("downloadUrl", &VegetationCatalogSourceInfo::downloadUrl)
            .property("upstreamPage", &VegetationCatalogSourceInfo::upstreamPage)
            .property("license", &VegetationCatalogSourceInfo::license)
            .property("verifiedOn", &VegetationCatalogSourceInfo::verifiedOn)
            .property("upstreamContentLengthBytes",
                &VegetationCatalogSourceInfo::upstreamContentLengthBytes)
            .property("verifiedZipBytes", &VegetationCatalogSourceInfo::verifiedZipBytes);

        class_<VegetationCatalogImportedInfo>("VegetationCatalogImportedInfo")
            .construct<>()
            .property("format", &VegetationCatalogImportedInfo::format)
            .property("generatedBy", &VegetationCatalogImportedInfo::generatedBy);

        class_<VegetationCatalogValidationInfo>("VegetationCatalogValidationInfo")
            .construct<>()
            .property("required", &VegetationCatalogValidationInfo::required)
            .property("prohibited", &VegetationCatalogValidationInfo::prohibited);

        class_<VegetationCatalogAssetRecord>("VegetationCatalogAssetRecord")
            .construct<>()
            .property("id", &VegetationCatalogAssetRecord::id)
            .property("source", &VegetationCatalogAssetRecord::source)
            .property("role", &VegetationCatalogAssetRecord::role)
            .property("category", &VegetationCatalogAssetRecord::category)
            .property("heightClass", &VegetationCatalogAssetRecord::heightClass)
            .property("templateCategory", &VegetationCatalogAssetRecord::templateCategory);

        class_<VegetationSourceCatalog>("VegetationSourceCatalog")
            .construct<>()
            .property("contractVersion", &VegetationSourceCatalog::contractVersion)
            .property("status", &VegetationSourceCatalog::status)
            .property("source", &VegetationSourceCatalog::source)
            .property("runtimeCatalog", &VegetationSourceCatalog::runtimeCatalog)
            .property("imported", &VegetationSourceCatalog::imported)
            .property("assets", &VegetationSourceCatalog::assets)
            .property("validation", &VegetationSourceCatalog::validation);
        return true;
    }();
    (void)registered;
}

core::pmr::String readFileText(
    const std::filesystem::path& path, const char* label, core::pmr::String& diagnostics)
{
    const auto result = core::readTextFile(path);
    if (!result.success) {
        diagnostics = makeDiagnostic(label);
        diagnostics += ": ";
        diagnostics += result.diagnostics;
        return {};
    }
    return result.text;
}

bool loadTypedCatalog(const std::filesystem::path& path,
    const char* label,
    VegetationSourceCatalog& out,
    core::pmr::String& diagnostics)
{
    const auto text = readFileText(path, label, diagnostics);
    if (text.empty()) {
        return false;
    }
    // vegetation.catalog/v1 and /v2 explicitly retain their versioned wire
    // schema, including optional metadata. Do not relax global Authoring or
    // retry another profile on failure. See the serialization binding contract.
    arp::reflection::SerializationOptions options;
    options.profile = arp::reflection::SerializationProfile::LegacyAllPropertiesV1;
    const auto result = arp::reflection::deserialize_from_yaml(text, options);
    if (!result.success) {
        diagnostics = makeDiagnostic(label);
        diagnostics += " parse failed: ";
        diagnostics += result.diagnostics;
        return false;
    }
    auto* catalog = result.value.is<VegetationSourceCatalog>()
        ? result.value.cast<VegetationSourceCatalog*>()
        : nullptr;
    if (catalog == nullptr) {
        diagnostics = makeDiagnostic(label);
        diagnostics += " root type mismatch";
        return false;
    }
    if (catalog->contractVersion != 1u && catalog->contractVersion != 2u) {
        diagnostics = makeDiagnostic("unsupported vegetation catalog contract version: ");
        diagnostics += std::to_string(catalog->contractVersion);
        return false;
    }
    std::clog << "[vegetation-catalog-serialization] contractVersion=" << catalog->contractVersion
              << " profile=LegacyAllPropertiesV1\n";
    out = *catalog;
    return true;
}

} // namespace

const VegetationCatalogAssetEntry* VegetationCatalogResourceData::findAsset(
    std::string_view id) const noexcept
{
    for (const auto& asset : assets) {
        if (asset.id == id) {
            return &asset;
        }
    }
    return nullptr;
}

const vegetation::VegetationTemplate* VegetationCatalogResourceData::findTemplate(
    std::string_view id) const noexcept
{
    const auto templateId = templateIdFor(id);
    if (templateId == 0u || templateId == std::numeric_limits<std::uint16_t>::max()) {
        return nullptr;
    }
    const auto storageIndex = static_cast<std::size_t>(templateId - 1u);
    if (storageIndex >= templates.size()) {
        return nullptr;
    }
    return &templates[storageIndex];
}

const VegetationCatalogAssetEntry* VegetationCatalogResourceData::findAssetByTemplateId(
    std::uint16_t templateId) const noexcept
{
    for (const auto& asset : assets) {
        if (asset.templateId == templateId) {
            return &asset;
        }
    }
    return nullptr;
}

std::uint16_t VegetationCatalogResourceData::familyBase(
    vegetation::TemplateCategory category) const noexcept
{
    // TerrainAsset reserves prototype ID zero as invalid. Catalog-facing
    // family IDs therefore begin at one even though the internal template
    // storage remains densely zero-indexed.
    return static_cast<std::uint16_t>(
        vegetation::familyTemplateBase(templateConfig, category) + 1u);
}

std::uint16_t VegetationCatalogResourceData::familyVariantCount(
    vegetation::TemplateCategory category) const noexcept
{
    return vegetation::familyVariantCount(templateConfig, category);
}

std::uint16_t VegetationCatalogResourceData::templateIdFor(std::string_view id) const noexcept
{
    const auto it = templateIdsByAssetId.find(
        core::pmr::String(id.data(), id.size(), &core::defaultCpuMemoryResource()));
    if (it == templateIdsByAssetId.end()) {
        return std::numeric_limits<std::uint16_t>::max();
    }
    return it->second;
}

ResourceLoadResult VegetationCatalogResource::load(
    ResourceManager& resourceManager, const ResourceLoadContext& context)
{
    if (loadDesc.input == VegetationCatalogInput::Cooked) {
        auto decoded =
            VegetationCatalogCookedCodec().decode(reference(), loadDesc.catalogPath, context);
        if (!decoded.success)
            return {false,
                ResourceState::Failed,
                reference().id,
                reference().type,
                decoded.diagnostics};
        for (const auto& dependency : decoded.dependencies) {
            const auto material = resourceManager.find<MaterialResource>(dependency.id);
            if (!material.ready())
                return resourceLoadFailure(*this, "VEGETATION-COOKED-MATERIAL-NOT-READY-OR-TYPE");
        }
        for (const auto& dependency : decoded.dependencies)
            addDependency(dependency);
        data = std::move(decoded.data);
        cookedBytes_ = decoded.decodedBytes;
        cookedFingerprint_ = decoded.contentFingerprint;
        return ResourceBase::load(resourceManager, context);
    }
    if (loadDesc.input != VegetationCatalogInput::LegacySourceCatalog
        || context.requirePreloadedSources || context.maxDecodedBytes)
        return resourceLoadFailure(*this, "VEGETATION-LEGACY-SOURCE-CONTRACT-UNSUPPORTED");
    registerVegetationCatalogReflection();

    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    VegetationSourceCatalog sourceCatalog;
    if (!loadTypedCatalog(loadDesc.catalogPath, "vegetation catalog", sourceCatalog, diagnostics)) {
        return ResourceLoadResult{
            false,
            ResourceState::Failed,
            loadDesc.resourceId,
            makeString(kVegetationCatalogResourceContract.type),
            diagnostics,
        };
    }

    if (sourceCatalog.runtimeCatalog.empty()) {
        return ResourceLoadResult{
            false,
            ResourceState::Failed,
            loadDesc.resourceId,
            makeString(kVegetationCatalogResourceContract.type),
            makeDiagnostic("vegetation catalog missing runtimeCatalog"),
        };
    }
    if (sourceCatalog.contractVersion != 1u && sourceCatalog.contractVersion != 2u) {
        diagnostics = makeDiagnostic("unsupported vegetation catalog contract version: ");
        diagnostics += std::to_string(sourceCatalog.contractVersion);
        return ResourceLoadResult{
            false,
            ResourceState::Failed,
            loadDesc.resourceId,
            makeString(kVegetationCatalogResourceContract.type),
            diagnostics,
        };
    }

    const auto runtimeCatalogPath =
        resolveRelativePath(loadDesc.catalogPath.parent_path(), sourceCatalog.runtimeCatalog);

    VegetationSourceCatalog runtimeCatalog;
    if (!loadTypedCatalog(
            runtimeCatalogPath, "vegetation runtime catalog", runtimeCatalog, diagnostics)) {
        return ResourceLoadResult{
            false,
            ResourceState::Failed,
            loadDesc.resourceId,
            makeString(kVegetationCatalogResourceContract.type),
            diagnostics,
        };
    }

    if (runtimeCatalog.imported.format.empty() || runtimeCatalog.imported.generatedBy.empty()) {
        return ResourceLoadResult{
            false,
            ResourceState::Failed,
            loadDesc.resourceId,
            makeString(kVegetationCatalogResourceContract.type),
            makeDiagnostic("vegetation runtime catalog missing imported metadata"),
        };
    }
    if (runtimeCatalog.contractVersion != sourceCatalog.contractVersion) {
        diagnostics = makeDiagnostic(
            "vegetation catalog contract version mismatch between source and runtime catalogs");
        return ResourceLoadResult{
            false,
            ResourceState::Failed,
            loadDesc.resourceId,
            makeString(kVegetationCatalogResourceContract.type),
            diagnostics,
        };
    }

    const auto hasAssetRecords = [](const auto& catalog) {
        return std::all_of(catalog.assets.begin(), catalog.assets.end(), [](const auto& item) {
            return item.template is<VegetationCatalogAssetRecord>();
        });
    };
    if (!hasAssetRecords(sourceCatalog) || !hasAssetRecords(runtimeCatalog)) {
        return ResourceLoadResult{false,
            ResourceState::Failed,
            loadDesc.resourceId,
            makeString(kVegetationCatalogResourceContract.type),
            makeDiagnostic("vegetation catalog assets require VegetationCatalogAssetRecord")};
    }

    const auto sourceAssets = [&]() -> core::pmr::Vector<VegetationCatalogAssetRecord> {
        core::pmr::Vector<VegetationCatalogAssetRecord> out{&core::defaultCpuMemoryResource()};
        out.reserve(sourceCatalog.assets.size());
        for (const auto& item : sourceCatalog.assets) {
            out.push_back(item.cast<const VegetationCatalogAssetRecord&>());
        }
        return out;
    }();

    const auto runtimeAssets = [&]() -> core::pmr::Vector<VegetationCatalogAssetRecord> {
        core::pmr::Vector<VegetationCatalogAssetRecord> out{&core::defaultCpuMemoryResource()};
        out.reserve(runtimeCatalog.assets.size());
        for (const auto& item : runtimeCatalog.assets) {
            out.push_back(item.cast<const VegetationCatalogAssetRecord&>());
        }
        return out;
    }();

    if (sourceAssets.empty() || runtimeAssets.empty()) {
        return ResourceLoadResult{
            false,
            ResourceState::Failed,
            loadDesc.resourceId,
            makeString(kVegetationCatalogResourceContract.type),
            makeDiagnostic("vegetation catalog contains no assets"),
        };
    }

    if (sourceAssets.size() != runtimeAssets.size()) {
        diagnostics =
            makeDiagnostic("vegetation asset count mismatch between source and runtime catalogs");
        return ResourceLoadResult{
            false,
            ResourceState::Failed,
            loadDesc.resourceId,
            makeString(kVegetationCatalogResourceContract.type),
            diagnostics,
        };
    }

    for (std::size_t i = 0; i < sourceAssets.size(); ++i) {
        const auto& sourceAsset = sourceAssets[i];
        const auto& runtimeAsset = runtimeAssets[i];
        if (!isAllowedValue(sourceAsset.role, {"canopy", "landmark", "ground_cover", "understory"})
            || !isAllowedValue(
                sourceAsset.category, {"canopy", "landmark", "ground_cover", "understory"})
            || !isAllowedValue(sourceAsset.heightClass, {"low", "medium", "tall", "tall_robust"})) {
            diagnostics = makeDiagnostic("vegetation source catalog asset has invalid metadata: ");
            diagnostics += sourceAsset.id;
            return ResourceLoadResult{
                false,
                ResourceState::Failed,
                loadDesc.resourceId,
                makeString(kVegetationCatalogResourceContract.type),
                diagnostics,
            };
        }
        if (!isAllowedValue(runtimeAsset.role, {"canopy", "landmark", "ground_cover", "understory"})
            || !isAllowedValue(
                runtimeAsset.category, {"canopy", "landmark", "ground_cover", "understory"})
            || !isAllowedValue(
                runtimeAsset.heightClass, {"low", "medium", "tall", "tall_robust"})) {
            diagnostics = makeDiagnostic("vegetation runtime catalog asset has invalid metadata: ");
            diagnostics += runtimeAsset.id;
            return ResourceLoadResult{
                false,
                ResourceState::Failed,
                loadDesc.resourceId,
                makeString(kVegetationCatalogResourceContract.type),
                diagnostics,
            };
        }
        if (sourceAsset.id != runtimeAsset.id) {
            diagnostics = makeDiagnostic("vegetation asset id mismatch at index ");
            diagnostics += std::to_string(i);
            diagnostics += ": source=";
            diagnostics += sourceAsset.id;
            diagnostics += " runtime=";
            diagnostics += runtimeAsset.id;
            return ResourceLoadResult{
                false,
                ResourceState::Failed,
                loadDesc.resourceId,
                makeString(kVegetationCatalogResourceContract.type),
                diagnostics,
            };
        }
        if (std::filesystem::path(sourceAsset.source).stem().generic_string()
            != std::filesystem::path(runtimeAsset.source).stem().generic_string()) {
            diagnostics = makeDiagnostic("vegetation asset source stem mismatch for: ");
            diagnostics += sourceAsset.id;
            return ResourceLoadResult{
                false,
                ResourceState::Failed,
                loadDesc.resourceId,
                makeString(kVegetationCatalogResourceContract.type),
                diagnostics,
            };
        }
        if (sourceAsset.role != runtimeAsset.role || sourceAsset.category != runtimeAsset.category
            || sourceAsset.heightClass != runtimeAsset.heightClass
            || sourceAsset.templateCategory != runtimeAsset.templateCategory) {
            diagnostics = makeDiagnostic("vegetation asset metadata mismatch for: ");
            diagnostics += sourceAsset.id;
            return ResourceLoadResult{
                false,
                ResourceState::Failed,
                loadDesc.resourceId,
                makeString(kVegetationCatalogResourceContract.type),
                diagnostics,
            };
        }
    }

    auto resourceData = core::makeShared<VegetationCatalogResourceData>();
    resourceData->sourceCatalogPath = makeString(loadDesc.catalogPath.generic_string());
    resourceData->runtimeCatalogPath = makeString(runtimeCatalogPath.generic_string());
    resourceData->status = runtimeCatalog.status;
    resourceData->contractVersion = runtimeCatalog.contractVersion;
    resourceData->source = sourceCatalog.source;
    resourceData->imported = runtimeCatalog.imported;
    resourceData->validation = runtimeCatalog.validation;
    resourceData->assets.reserve(runtimeAssets.size());
    resourceData->templates.reserve(runtimeAssets.size());
    resourceData->templateIdsByAssetId.reserve(runtimeAssets.size());

    struct ImportedAssetBundle {
        VegetationCatalogAssetEntry entry;
        assets::MeshAsset mesh;
        std::pmr::vector<assets::ImportedMaterialAsset> materials{
            &core::defaultCpuMemoryResource()};
    };

    core::pmr::Vector<ImportedAssetBundle> importedAssets{&core::defaultCpuMemoryResource()};
    importedAssets.reserve(runtimeAssets.size());
    core::pmr::Vector<VegetationCatalogAssetEntry> classifiedAssets{
        &core::defaultCpuMemoryResource()};
    classifiedAssets.reserve(runtimeAssets.size());

    for (const auto& runtimeAsset : runtimeAssets) {
        VegetationCatalogAssetEntry entry;
        entry.id = runtimeAsset.id;
        entry.source = runtimeAsset.source;
        entry.role = runtimeAsset.role;
        entry.category = runtimeAsset.category;
        entry.heightClass = runtimeAsset.heightClass;
        if (runtimeCatalog.contractVersion == 2u) {
            if (!detail::CatalogFamilyContract::parse(
                    runtimeAsset.templateCategory, entry.templateCategory)) {
                diagnostics = makeDiagnostic(
                    "vegetation.catalog/v2 asset requires a valid templateCategory: ");
                diagnostics += runtimeAsset.id;
                return ResourceLoadResult{
                    false,
                    ResourceState::Failed,
                    loadDesc.resourceId,
                    makeString(kVegetationCatalogResourceContract.type),
                    diagnostics,
                };
            }
        } else {
            // v1 is a separately versioned legacy contract whose category is
            // filename-defined. New catalogs must use v2 explicit metadata.
            entry.templateCategory = classifyTemplateCategory(
                std::filesystem::path(runtimeAsset.source.c_str()).stem().generic_string());
        }

        if (entry.templateCategory == vegetation::TemplateCategory::ProceduralGrass) {
            diagnostics = makeDiagnostic("unsupported vegetation asset family for source: ");
            diagnostics += runtimeAsset.source;
            return ResourceLoadResult{
                false,
                ResourceState::Failed,
                loadDesc.resourceId,
                makeString(kVegetationCatalogResourceContract.type),
                diagnostics,
            };
        }

        const auto glbPath =
            resolveRelativePath(runtimeCatalogPath.parent_path(), runtimeAsset.source);
        const auto importResult = assets::importStaticMeshFromGlbFile(glbPath);
        if (!importResult.success) {
            diagnostics = makeDiagnostic("GLB import failed for ");
            diagnostics += runtimeAsset.source;
            diagnostics += ": ";
            diagnostics += importResult.diagnostics.c_str();
            return ResourceLoadResult{
                false,
                ResourceState::Failed,
                loadDesc.resourceId,
                makeString(kVegetationCatalogResourceContract.type),
                diagnostics,
            };
        }
        if (!importResult.mesh.validForIndexedDraw()) {
            diagnostics = makeDiagnostic("imported GLB mesh is not valid for indexed draw: ");
            diagnostics += runtimeAsset.source;
            return ResourceLoadResult{
                false,
                ResourceState::Failed,
                loadDesc.resourceId,
                makeString(kVegetationCatalogResourceContract.type),
                diagnostics,
            };
        }

        ImportedAssetBundle bundle;
        bundle.entry = std::move(entry);
        bundle.mesh = std::move(importResult.mesh);
        bundle.materials = std::move(importResult.materials);
        classifiedAssets.push_back(bundle.entry);
        importedAssets.push_back(std::move(bundle));
    }

    auto familyOrder = flattenAssetsByFamily(classifiedAssets, diagnostics);
    if (familyOrder.empty() && !classifiedAssets.empty()) {
        return ResourceLoadResult{
            false,
            ResourceState::Failed,
            loadDesc.resourceId,
            makeString(kVegetationCatalogResourceContract.type),
            diagnostics,
        };
    }

    // Convert imported bundles into runtime entries and templates in family order.
    for (const auto* orderedAsset : familyOrder) {
        auto bundleIt = std::find_if(
            importedAssets.begin(), importedAssets.end(), [&](const ImportedAssetBundle& bundle) {
                return bundle.entry.id == orderedAsset->id;
            });
        if (bundleIt == importedAssets.end()) {
            diagnostics = makeDiagnostic("missing imported bundle for vegetation asset: ");
            diagnostics += orderedAsset->id;
            return ResourceLoadResult{
                false,
                ResourceState::Failed,
                loadDesc.resourceId,
                makeString(kVegetationCatalogResourceContract.type),
                diagnostics,
            };
        }

        auto entry = bundleIt->entry;
        entry.templateId = static_cast<std::uint16_t>(resourceData->assets.size() + 1u);
        resourceData->templateIdsByAssetId.emplace(entry.id, entry.templateId);
        resourceData->assets.push_back(entry);
        resourceData->templates.push_back(
            makeVegetationTemplate(entry, entry.templateId, bundleIt->mesh, bundleIt->materials));
    }

    resourceData->templateConfig.hasEnvironmentAssets = true;
    resourceData->templateConfig.treeLodCount = 1u;
    resourceData->templateConfig.rockLodCount = 1u;
    resourceData->templateConfig.proceduralGrassTemplate = 0u;
    resourceData->templateConfig.proceduralTreeTemplate = 0u;
    resourceData->templateConfig.proceduralUnderstoryTemplate = 0u;
    resourceData->templateConfig.proceduralRockTemplate = 0u;
    resourceData->templateConfig.proceduralSlabRockTemplate = 0u;

    for (const auto& asset : resourceData->assets) {
        if (!detail::CatalogFamilyContract::increment(
                resourceData->templateConfig, asset.templateCategory))
            return resourceLoadFailure(*this, "VEGETATION-LEGACY-FAMILY-COUNT");
    }

    if (resourceData->assets.size() != resourceData->templates.size()) {
        diagnostics = makeDiagnostic("vegetation template count mismatch");
        return ResourceLoadResult{
            false,
            ResourceState::Failed,
            loadDesc.resourceId,
            makeString(kVegetationCatalogResourceContract.type),
            diagnostics,
        };
    }

    data = std::move(resourceData);
    return ResourceBase::load(resourceManager, context);
}

bool VegetationCatalogResource::collectFileReadIntents(ResourceManager&,
    core::pmr::Vector<ResourceFileReadIntent>& intents,
    core::pmr::String& diagnostics) const
{
    if (loadDesc.input != VegetationCatalogInput::Cooked) {
        diagnostics = "VEGETATION-LEGACY-SOURCE-CONTRACT-UNSUPPORTED";
        return false;
    }
    auto index = VegetationCatalogCookedCodec().inspect(reference(), loadDesc.catalogPath);
    diagnostics = std::move(index.diagnostics);
    if (!index.success)
        return false;
    intents.insert(intents.end(),
        std::make_move_iterator(index.files.begin()),
        std::make_move_iterator(index.files.end()));
    return true;
}

std::size_t VegetationCatalogResource::processedByteCount() const noexcept
{
    return cookedBytes_;
}

void VegetationCatalogResource::appendContentFingerprint(core::StableHash64& hasher) const noexcept
{
    hasher.appendU64(cookedFingerprint_);
}

VegetationCatalogResourceLoadResult loadVegetationCatalogResource(
    ResourceManager& resourceManager, const VegetationCatalogResourceLoadDesc& desc)
{
    auto resource = core::makeUnique<VegetationCatalogResource>(desc);
    const auto loaded = resourceManager.loadResource(std::move(resource));
    return VegetationCatalogResourceLoadResult{
        loaded.success,
        loaded,
        loaded.diagnostics,
    };
}

} // namespace arp::resource

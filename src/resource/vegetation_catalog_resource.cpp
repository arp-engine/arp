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
    core::pmr::String& diagnostics,
    const ResourceLoadContext* context = nullptr,
    const VegetationCatalogResourceLoadDesc* desc = nullptr)
{
    core::pmr::String text;
    if (context) {
        auto read = readResourceText(core::FilePath::utf8(path),
            *context,
            desc->sourceLimits->maxCatalogBytes,
            &desc->sourceOrigin);
        if (!read.success) {
            diagnostics = std::move(read.diagnostics);
            return false;
        }
        text = std::move(read.text);
    } else
        text = readFileText(path, label, diagnostics);
    if (text.empty()) {
        return false;
    }
    // vegetation.catalog/v1 and /v2 explicitly retain their versioned wire
    // schema, including optional metadata. Do not relax global Authoring or
    // retry another profile on failure. See the serialization binding contract.
    arp::reflection::SerializationOptions options;
    options.profile = arp::reflection::SerializationProfile::LegacyAllPropertiesV1;
    if (context) {
        options.maxDepth = desc->sourceLimits->maxMetadataDepth;
        options.maxValues = desc->sourceLimits->maxMetadataValues;
        options.maxCollectionLength = desc->sourceLimits->maxMetadataCollectionLength;
    }
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
    if (!context)
        std::clog << "[vegetation-catalog-serialization] contractVersion="
                  << catalog->contractVersion << " profile=LegacyAllPropertiesV1\n";
    out = *catalog;
    return true;
}

bool runtimeCatalogLocation(const VegetationCatalogResourceLoadDesc& desc,
    const VegetationSourceCatalog& catalog,
    std::filesystem::path& output,
    core::pmr::String& diagnostic)
{
    if (desc.input == VegetationCatalogInput::SourceCatalogV2) {
        if (!resolveResourceSourcePath(desc.catalogPath.parent_path(),
                catalog.runtimeCatalog,
                desc.sourceOrigin,
                output)) {
            diagnostic = "VEGETATION-SOURCE-PATH";
            return false;
        }
    } else
        output = resolveRelativePath(desc.catalogPath.parent_path(), catalog.runtimeCatalog);
    return true;
}

const char* validateCatalogSource(const VegetationCatalogResourceLoadDesc& desc)
{
    if (!desc.sourceLimits)
        return "VEGETATION-SOURCE-LIMITS";
    const auto& limits = *desc.sourceLimits;
    if (!limits.maxCatalogBytes || limits.maxCatalogBytes == SIZE_MAX || !limits.maxAssets
        || limits.maxAssets > UINT16_MAX || !limits.maxDecodedBytes
        || limits.maxDecodedBytes == SIZE_MAX || !limits.maxTotalSourceBytes
        || limits.maxTotalSourceBytes == UINT64_MAX
        || limits.maxCatalogBytes > limits.maxTotalSourceBytes || !limits.maxImportBytes
        || !limits.maxMetadataDepth || limits.maxMetadataDepth > 64 || !limits.maxMetadataValues
        || limits.maxMetadataValues == SIZE_MAX
        || limits.maxMetadataCollectionLength < limits.maxAssets
        || limits.maxMetadataCollectionLength == SIZE_MAX || limits.maxImportBytes == UINT64_MAX
        || assets::validateStaticMeshSceneSourceImportLimits(limits.mesh)
        || limits.maxAssets > limits.maxImportBytes / limits.mesh.maxImportBytes
        || limits.maxAssets * limits.mesh.maxImportBytes >= limits.maxImportBytes)
        return "VEGETATION-SOURCE-LIMITS";
    if (validateResourceReadIntent(
            core::FilePath::utf8(desc.catalogPath), limits.maxCatalogBytes, desc.sourceOrigin)
            .diagnostic)
        return "VEGETATION-SOURCE-PATH";
    return nullptr;
}

std::uint64_t catalogOverhead(const VegetationCatalogSourceLoadLimits& limits) noexcept
{
    return limits.maxImportBytes - limits.maxAssets * limits.mesh.maxImportBytes;
}

class CatalogSourceIdentity final {
public:
    CatalogSourceIdentity(
        const VegetationCatalogResourceLoadDesc& desc, const ResourceLoadContext& context)
        : desc_(desc), context_(context)
    {
        appendResourceOriginFingerprint(hash_, desc.sourceOrigin);
        const auto& limits = *desc.sourceLimits;
        const auto& mesh = limits.mesh;
        for (const auto value : {std::uint64_t(limits.maxCatalogBytes),
                 std::uint64_t(limits.maxAssets),
                 std::uint64_t(limits.maxDecodedBytes),
                 std::uint64_t(limits.maxMetadataDepth),
                 std::uint64_t(limits.maxMetadataValues),
                 std::uint64_t(limits.maxMetadataCollectionLength),
                 limits.maxTotalSourceBytes,
                 limits.maxImportBytes,
                 mesh.maxImportBytes,
                 mesh.maxDecodedBytes,
                 std::uint64_t(mesh.maxNodeVisits),
                 std::uint64_t(mesh.maxNodeDepth),
                 mesh.geometry.maxSourceBytes,
                 std::uint64_t(mesh.geometry.maxVertices),
                 std::uint64_t(mesh.geometry.maxIndices),
                 std::uint64_t(mesh.geometry.maxPrimitives),
                 std::uint64_t(mesh.geometry.maxJsonDepth),
                 std::uint64_t(mesh.geometry.maxJsonValues),
                 std::uint64_t(mesh.colorPolicy)})
            hash_.appendU64(value);
    }

    bool append(const std::filesystem::path& path, std::size_t maxBytes)
    {
        const auto logical = core::FilePath::utf8(path);
        if (std::find(paths_.begin(), paths_.end(), logical) != paths_.end())
            return true;
        const auto* source = context_.sources ? context_.sources->find(logical) : nullptr;
        if (!source || source->pathScope != desc_.sourceOrigin.pathScope
            || !(source->mount == desc_.sourceOrigin.mount) || source->bytes.size() > maxBytes
            || source->bytes.size() > desc_.sourceLimits->maxTotalSourceBytes - total_)
            return false;
        total_ += source->bytes.size();
        paths_.push_back(logical);
        hash_.appendU64(logical.size());
        hash_.appendBytes(logical.data(), logical.size());
        hash_.appendU64(source->bytes.size());
        const auto digest = core::Sha256::digest(
            {reinterpret_cast<const char*>(source->bytes.data()), source->bytes.size()});
        hash_.appendBytes(digest.data(), digest.size());
        return true;
    }

    std::uint64_t value() const noexcept { return hash_.value(); }

private:
    const VegetationCatalogResourceLoadDesc& desc_;
    const ResourceLoadContext& context_;
    core::StableHash64 hash_;
    core::pmr::Vector<core::pmr::String> paths_;
    std::uint64_t total_ = 0;
};

struct CatalogInputPair final {
    VegetationSourceCatalog source, runtime;
    std::filesystem::path runtimePath;
    core::pmr::Vector<VegetationCatalogAssetRecord> runtimeAssets;
    core::pmr::Vector<std::filesystem::path> paths;
};

ResourceLoadResult prepareCatalogPair(const VegetationCatalogResourceLoadDesc& loadDesc,
    const ResourceLoadContext* context,
    CatalogInputPair& pair)
{
    registerVegetationCatalogReflection();

    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    auto& sourceCatalog = pair.source;
    if (!loadTypedCatalog(loadDesc.catalogPath,
            "vegetation catalog",
            sourceCatalog,
            diagnostics,
            context,
            &loadDesc)) {
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

    if (context && sourceCatalog.contractVersion != 2u)
        return {false,
            ResourceState::Failed,
            loadDesc.resourceId,
            makeString(kVegetationCatalogResourceContract.type),
            "VEGETATION-SOURCE-V2-REQUIRED"};
    if (!runtimeCatalogLocation(loadDesc, sourceCatalog, pair.runtimePath, diagnostics))
        return {false,
            ResourceState::Failed,
            loadDesc.resourceId,
            makeString(kVegetationCatalogResourceContract.type),
            diagnostics};
    const auto& runtimeCatalogPath = pair.runtimePath;

    auto& runtimeCatalog = pair.runtime;
    if (!loadTypedCatalog(runtimeCatalogPath,
            "vegetation runtime catalog",
            runtimeCatalog,
            diagnostics,
            context,
            &loadDesc)) {
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

    if (context
        && (sourceCatalog.assets.size() > loadDesc.sourceLimits->maxAssets
            || runtimeCatalog.assets.size() > loadDesc.sourceLimits->maxAssets))
        return {false,
            ResourceState::Failed,
            loadDesc.resourceId,
            makeString(kVegetationCatalogResourceContract.type),
            "VEGETATION-SOURCE-ASSET-BUDGET"};
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

    pair.runtimeAssets = [&]() -> core::pmr::Vector<VegetationCatalogAssetRecord> {
        core::pmr::Vector<VegetationCatalogAssetRecord> out{&core::defaultCpuMemoryResource()};
        out.reserve(runtimeCatalog.assets.size());
        for (const auto& item : runtimeCatalog.assets) {
            out.push_back(item.cast<const VegetationCatalogAssetRecord&>());
        }
        return out;
    }();
    const auto& runtimeAssets = pair.runtimeAssets;

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

    if (context) {
        pair.paths.reserve(runtimeAssets.size());
        for (std::size_t i = 0; i < runtimeAssets.size(); ++i) {
            const auto& asset = runtimeAssets[i];
            if (asset.id.empty() || asset.id.find('\0') != core::pmr::String::npos)
                return {false,
                    ResourceState::Failed,
                    loadDesc.resourceId,
                    makeString(kVegetationCatalogResourceContract.type),
                    "VEGETATION-SOURCE-ASSET-ID"};
            for (std::size_t j = 0; j < i; ++j)
                if (runtimeAssets[j].id == asset.id)
                    return {false,
                        ResourceState::Failed,
                        loadDesc.resourceId,
                        makeString(kVegetationCatalogResourceContract.type),
                        "VEGETATION-SOURCE-ASSET-ID"};
            vegetation::TemplateCategory category;
            if (!detail::CatalogFamilyContract::parse(asset.templateCategory, category)
                || category == vegetation::TemplateCategory::ProceduralGrass)
                return {false,
                    ResourceState::Failed,
                    loadDesc.resourceId,
                    makeString(kVegetationCatalogResourceContract.type),
                    "VEGETATION-SOURCE-CATEGORY"};
            std::filesystem::path path;
            if (!resolveResourceSourcePath(
                    runtimeCatalogPath.parent_path(), asset.source, loadDesc.sourceOrigin, path))
                return {false,
                    ResourceState::Failed,
                    loadDesc.resourceId,
                    makeString(kVegetationCatalogResourceContract.type),
                    "VEGETATION-SOURCE-PATH"};
            pair.paths.push_back(std::move(path));
        }
    }
    return {true,
        ResourceState::Ready,
        loadDesc.resourceId,
        makeString(kVegetationCatalogResourceContract.type),
        {}};
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
    // C++17 unordered_map lookup cannot consume string_view without creating
    // an owned key. Keep this noexcept resource lookup allocation-free.
    for (const auto& entry : templateIdsByAssetId)
        if (entry.first == id)
            return entry.second;
    return std::numeric_limits<std::uint16_t>::max();
}

ResourceLoadResult VegetationCatalogResource::load(
    ResourceManager& resourceManager, const ResourceLoadContext& context)
{
    if (loadDesc.input == VegetationCatalogInput::Cooked) {
        auto decoded = VegetationCatalogCookedCodec().decode(
            reference(), loadDesc.catalogPath, context, loadDesc.sourceOrigin);
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
    if (loadDesc.input == VegetationCatalogInput::SourceCatalogV2) {
        try {
            if (const auto* invalid = validateCatalogSource(loadDesc))
                return resourceLoadFailure(*this, invalid);
            if (!context.maxDecodedBytes || context.maxDecodedBytes == SIZE_MAX)
                return resourceLoadFailure(*this, "VEGETATION-SOURCE-DECODE-BUDGET-REQUIRED");
            auto quota =
                core::makeShared<core::CpuMemoryQuota>(catalogOverhead(*loadDesc.sourceLimits));
            core::CpuMemoryQuotaScope scope(quota);
            auto inputs = context;
            inputs.requirePreloadedSources = true;
            return loadCatalog(resourceManager, inputs);
        } catch (const std::bad_alloc&) {
            return resourceLoadFailure(*this, "VEGETATION-SOURCE-IMPORT-BUDGET");
        } catch (const std::exception&) {
            return resourceLoadFailure(*this, "VEGETATION-SOURCE-INVALID");
        }
    }
    if (loadDesc.input != VegetationCatalogInput::LegacySourceCatalog
        || context.requirePreloadedSources || context.maxDecodedBytes
        || loadDesc.sourceOrigin.pathScope != core::io::FilePathScope::HostFilesystem
        || loadDesc.sourceOrigin.mount)
        return resourceLoadFailure(*this, "VEGETATION-LEGACY-SOURCE-CONTRACT-UNSUPPORTED");
    return loadCatalog(resourceManager, context);
}

ResourceLoadResult VegetationCatalogResource::loadCatalog(
    ResourceManager& resourceManager, const ResourceLoadContext& context)
{
    const bool acquired = loadDesc.input == VegetationCatalogInput::SourceCatalogV2;
    core::pmr::String diagnostics;
    CatalogInputPair pair;
    auto prepared = prepareCatalogPair(loadDesc, acquired ? &context : nullptr, pair);
    if (!prepared.success)
        return prepared;
    const auto& sourceCatalog = pair.source;
    const auto& runtimeCatalog = pair.runtime;
    const auto& runtimeCatalogPath = pair.runtimePath;
    const auto& runtimeAssets = pair.runtimeAssets;
    std::optional<CatalogSourceIdentity> identity;
    if (acquired) {
        identity.emplace(loadDesc, context);
        if (!identity->append(loadDesc.catalogPath, loadDesc.sourceLimits->maxCatalogBytes)
            || !identity->append(runtimeCatalogPath, loadDesc.sourceLimits->maxCatalogBytes))
            return resourceLoadFailure(*this, "VEGETATION-SOURCE-TOTAL-BUDGET");
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

    for (std::size_t assetIndex = 0; assetIndex < runtimeAssets.size(); ++assetIndex) {
        const auto& runtimeAsset = runtimeAssets[assetIndex];
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

        const auto glbPath = acquired
            ? pair.paths[assetIndex]
            : resolveRelativePath(runtimeCatalogPath.parent_path(), runtimeAsset.source);
        assets::StaticMeshImportResult importResult;
        if (acquired) {
            const auto& limits = loadDesc.sourceLimits->mesh;
            auto input = readResourceSource(core::FilePath::utf8(glbPath),
                context,
                limits.geometry.maxSourceBytes,
                false,
                &loadDesc.sourceOrigin);
            if (!input.success)
                return resourceLoadFailure(*this, input.diagnostics.c_str());
            if (!identity->append(glbPath, limits.geometry.maxSourceBytes))
                return resourceLoadFailure(*this, "VEGETATION-SOURCE-TOTAL-BUDGET");
            auto imported = assets::importStaticMeshSceneFromGlbBytes(
                {input.bytes.data(), input.bytes.size()}, limits);
            importResult = std::move(imported.geometry);
        } else
            importResult = assets::importStaticMeshFromGlbFile(glbPath);
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

    if (acquired) {
        std::uint64_t decoded = 0;
        for (const auto& item : resourceData->templates)
            decoded += item.vertices.size() * sizeof(vegetation::InstanceVertex)
                + item.indices.size() * sizeof(std::uint32_t)
                + item.sections.size() * sizeof(vegetation::VegetationTemplate::Section);
        if (decoded > loadDesc.sourceLimits->maxDecodedBytes || decoded > context.maxDecodedBytes)
            return resourceLoadFailure(*this, "VEGETATION-SOURCE-DECODE-BUDGET");
        cookedBytes_ = static_cast<std::size_t>(decoded);
        cookedFingerprint_ = identity->value();
    }
    data = std::move(resourceData);
    return ResourceBase::load(resourceManager, context);
}

bool VegetationCatalogResource::collectFileReadIntents(ResourceManager&,
    core::pmr::Vector<ResourceFileReadIntent>& intents,
    core::pmr::String& diagnostics) const
{
    if (loadDesc.input == VegetationCatalogInput::SourceCatalogV2) {
        if (const auto* invalid = validateCatalogSource(loadDesc)) {
            diagnostics = invalid;
            return false;
        }
        return appendResourceReadIntent(core::FilePath::utf8(loadDesc.catalogPath),
            loadDesc.sourceLimits->maxCatalogBytes,
            intents,
            diagnostics,
            loadDesc.sourceOrigin);
    }
    if (loadDesc.input != VegetationCatalogInput::Cooked) {
        diagnostics = "VEGETATION-LEGACY-SOURCE-CONTRACT-UNSUPPORTED";
        return false;
    }
    auto index = VegetationCatalogCookedCodec().inspect(
        reference(), loadDesc.catalogPath, {}, loadDesc.sourceOrigin);
    diagnostics = std::move(index.diagnostics);
    if (!index.success)
        return false;
    intents.insert(intents.end(),
        std::make_move_iterator(index.files.begin()),
        std::make_move_iterator(index.files.end()));
    return true;
}

bool VegetationCatalogResource::planFileReadIntents(ResourceManager&,
    const ResourceLoadContext& context,
    core::pmr::Vector<ResourceFileReadIntent>& intents,
    core::pmr::String& diagnostics) const
{
    if (loadDesc.input == VegetationCatalogInput::SourceCatalogV2) {
        try {
            if (const auto* invalid = validateCatalogSource(loadDesc)) {
                diagnostics = invalid;
                return false;
            }
            const auto& limits = *loadDesc.sourceLimits;
            auto quota = core::makeShared<core::CpuMemoryQuota>(catalogOverhead(limits));
            core::CpuMemoryQuotaScope scope(quota);
            core::pmr::Vector<ResourceFileReadIntent> candidate;
            const auto append = [&](const std::filesystem::path& path, std::size_t budget) {
                const auto logical = core::FilePath::utf8(path);
                if (std::any_of(candidate.begin(), candidate.end(), [&](const auto& read) {
                        return read.path == logical;
                    }))
                    return true;
                return appendResourceReadIntent(
                    logical, budget, candidate, diagnostics, loadDesc.sourceOrigin);
            };
            const auto has = [&](const std::filesystem::path& path) {
                return context.sources && context.sources->find(core::FilePath::utf8(path));
            };
            if (!has(loadDesc.catalogPath)) {
                if (!append(loadDesc.catalogPath, limits.maxCatalogBytes))
                    return false;
            } else {
                registerVegetationCatalogReflection();
                auto inputs = context;
                inputs.requirePreloadedSources = true;
                VegetationSourceCatalog source;
                if (!loadTypedCatalog(loadDesc.catalogPath,
                        "vegetation catalog",
                        source,
                        diagnostics,
                        &inputs,
                        &loadDesc))
                    return false;
                if (source.contractVersion != 2) {
                    diagnostics = "VEGETATION-SOURCE-V2-REQUIRED";
                    return false;
                }
                if (source.assets.size() > limits.maxAssets) {
                    diagnostics = "VEGETATION-SOURCE-ASSET-BUDGET";
                    return false;
                }
                std::filesystem::path runtime;
                if (!runtimeCatalogLocation(loadDesc, source, runtime, diagnostics))
                    return false;
                if (!has(runtime)) {
                    if (!append(runtime, limits.maxCatalogBytes))
                        return false;
                } else {
                    CatalogInputPair pair;
                    auto prepared = prepareCatalogPair(loadDesc, &inputs, pair);
                    if (!prepared.success) {
                        diagnostics = std::move(prepared.diagnostics);
                        return false;
                    }
                    for (const auto& path : pair.paths)
                        if (!append(path, limits.mesh.geometry.maxSourceBytes))
                            return false;
                }
            }
            intents = std::move(candidate);
            diagnostics.clear();
            return true;
        } catch (const std::bad_alloc&) {
            diagnostics = "VEGETATION-SOURCE-IMPORT-BUDGET";
        } catch (const std::exception&) {
            diagnostics = "VEGETATION-SOURCE-INVALID";
        }
        return false;
    }
    if (loadDesc.input != VegetationCatalogInput::Cooked) {
        diagnostics = "VEGETATION-LEGACY-SOURCE-CONTRACT-UNSUPPORTED";
        return false;
    }
    const auto path = core::FilePath::utf8(loadDesc.catalogPath);
    if (!context.sources || !context.sources->find(path))
        return appendResourceReadIntent(path,
            VegetationCatalogCookedCodec::maxManifestBytes,
            intents,
            diagnostics,
            loadDesc.sourceOrigin);
    auto index = VegetationCatalogCookedCodec().inspect(
        reference(), loadDesc.catalogPath, context, loadDesc.sourceOrigin);
    diagnostics = std::move(index.diagnostics);
    if (!index.success)
        return false;
    intents = std::move(index.files);
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

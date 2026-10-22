#include "assets/glb_static_mesh_importer.h"
#include "core/io/file_io.h"
#include "core/memory/cpu_allocator.h"
#include "reflection/serialization.h"
#include "resource/mesh_asset_resources.h"
#include "resource/resource_manager.h"
#include "resource/vegetation_catalog_resource.h"

#include <cassert>
#include <filesystem>
#include <iostream>

namespace {

arp::core::pmr::String text(const char* value)
{
    return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
}

std::filesystem::path projectRoot()
{
    auto current = std::filesystem::current_path();
    while (!current.empty()) {
        if (std::filesystem::exists(
                current / "assets" / "vegetation" / "quaternius_stylized_nature_megakit")) {
            return current;
        }
        const auto parent = current.parent_path();
        if (parent == current) {
            break;
        }
        current = parent;
    }
    return std::filesystem::current_path();
}

void runtime_catalog_lists_all_generated_glbs()
{
    const auto root = projectRoot();
    const auto manifestPath = root / "assets" / "vegetation" / "quaternius_stylized_nature_megakit"
        / "imported" / "vegetation_runtime_catalog.yaml";

    assert(std::filesystem::exists(manifestPath));

    arp::resource::ResourceManager manager;
    arp::resource::VegetationCatalogResourceLoadDesc desc;
    desc.catalogPath = root / "assets" / "vegetation" / "quaternius_stylized_nature_megakit"
        / "vegetation_catalog.yaml";
    desc.resourceId = text("8f6b0ad3-0bc7-4f3c-a04f-2a0212110068");
    desc.alias = text("vegetation/quaternius_stylized_nature_megakit");
    desc.status = text("Backend Validated");

    const auto loaded = arp::resource::loadVegetationCatalogResource(manager, desc);
    if (!loaded.success) {
        std::cerr << "failed to load vegetation catalog\n" << loaded.diagnostics << "\n";
    }
    assert(loaded.success);

    const auto textResult = arp::core::readTextFile(manifestPath);
    assert(textResult.success);
    const auto parsed = arp::reflection::deserialize_from_yaml(textResult.text);
    if (!parsed.success) {
        std::cerr << "failed to parse vegetation runtime catalog\n" << parsed.diagnostics << "\n";
        assert(false);
    }
    auto* manifest = parsed.value.cast<arp::resource::VegetationSourceCatalog*>();
    assert(manifest != nullptr);
    assert(manifest->assets.size() == 68u);

    std::size_t glbCount = 0u;
    for (const auto& assetObject : manifest->assets) {
        const auto& asset = assetObject.cast<const arp::resource::VegetationCatalogAssetRecord&>();
        assert(asset.source.rfind("glb/", 0) == 0);
        assert(asset.source.size() > 4u);
        assert(std::filesystem::exists(root / "assets" / "vegetation"
            / "quaternius_stylized_nature_megakit" / "imported" / asset.source));
        ++glbCount;
    }

    assert(glbCount == 68u);
}

void representative_glbs_load_through_resource_manager()
{
    const auto root = projectRoot();
    const auto importedRoot =
        root / "assets" / "vegetation" / "quaternius_stylized_nature_megakit" / "imported" / "glb";

    arp::resource::ResourceManager manager;

    const struct Sample {
        const char* id;
        const char* file;
    } samples[] = {
        {"8f6b0ad3-0bc7-4f3c-a04f-2a0212110001", "CommonTree_1.glb"},
        {"8f6b0ad3-0bc7-4f3c-a04f-2a0212110002", "Pine_1.glb"},
        {"8f6b0ad3-0bc7-4f3c-a04f-2a0212110003", "TwistedTree_1.glb"},
        {"8f6b0ad3-0bc7-4f3c-a04f-2a0212110004", "Grass_Common_Tall.glb"},
        {"8f6b0ad3-0bc7-4f3c-a04f-2a0212110005", "Rock_Medium_1.glb"},
    };

    for (const auto& sample : samples) {
        const auto glbPath = importedRoot / sample.file;
        assert(std::filesystem::exists(glbPath));

        const auto loaded = manager.load<arp::resource::MeshAssetResource>(
            arp::resource::ResourceLoadContext{}, text(sample.id), glbPath);
        if (!loaded.success) {
            std::cerr << "failed to load " << glbPath << "\n" << loaded.diagnostics << "\n";
        }
        assert(loaded.success);

        const auto handle = manager.find<arp::resource::MeshAssetResource>(sample.id);
        assert(handle.ready());
        assert(handle.get() != nullptr);
        assert(handle.get()->mesh() != nullptr);
        assert(handle.get()->mesh()->validForIndexedDraw());
        assert(handle.get()->mesh()->vertices.size() > 0u);
        assert(handle.get()->mesh()->indices.size() > 0u);
        assert(!handle.get()->materials().empty());
        assert(!handle.get()->textures().empty());
        assert(!handle.get()->images().empty());
    }
}

void vegetation_catalog_resource_builds_template_registry()
{
    const auto root = projectRoot();
    const auto catalogPath = root / "assets" / "vegetation" / "quaternius_stylized_nature_megakit"
        / "vegetation_catalog.yaml";

    arp::resource::ResourceManager manager;
    arp::resource::VegetationCatalogResourceLoadDesc desc;
    desc.catalogPath = catalogPath;
    desc.resourceId = text("8f6b0ad3-0bc7-4f3c-a04f-2a0212110068");
    desc.alias = text("vegetation/quaternius_stylized_nature_megakit");
    desc.status = text("Backend Validated");

    const auto loaded = arp::resource::loadVegetationCatalogResource(manager, desc);
    if (!loaded.success) {
        std::cerr << "failed to load vegetation catalog\n" << loaded.diagnostics << "\n";
    }
    assert(loaded.success);

    const auto handle = manager.find<arp::resource::VegetationCatalogResource>(desc.resourceId);
    assert(handle.ready());
    assert(handle.get() != nullptr);
    assert(handle.get()->data != nullptr);

    const auto& data = *handle.get()->data;
    assert(data.assets.size() == 68u);
    assert(data.templates.size() == 68u);
    assert(data.templateConfig.hasEnvironmentAssets);
    assert(data.templateConfig.grassCardCount == 4u);
    assert(data.templateConfig.weedCardCount == 0u);
    assert(data.templateConfig.broadleafCardCount == 2u);
    assert(data.templateConfig.fernCardCount == 1u);
    assert(data.templateConfig.bushCardCount == 6u);
    assert(data.templateConfig.flowerCardCount == 9u);
    assert(data.templateConfig.mushroomCardCount == 2u);
    assert(data.templateConfig.maturePineVariantCount == 10u);
    assert(data.templateConfig.youngFirVariantCount == 5u);
    assert(data.templateConfig.youngPineVariantCount == 5u);
    assert(data.templateConfig.mossyRock01VariantCount == 14u);
    assert(data.templateConfig.mossyRock02VariantCount == 10u);

    const auto* commonTree = data.findAsset("quaternius.commontree_1");
    assert(commonTree != nullptr);
    assert(commonTree->templateCategory == arp::vegetation::TemplateCategory::TreeMaturePine);
    const auto* pine = data.findAsset("quaternius.pine_1");
    assert(pine != nullptr);
    assert(pine->templateCategory == arp::vegetation::TemplateCategory::TreeYoungPine);
    const auto* deadTree = data.findAsset("quaternius.deadtree_1");
    assert(deadTree != nullptr);
    assert(deadTree->templateCategory == arp::vegetation::TemplateCategory::TreeYoungFir);
    const auto* rock = data.findAsset("quaternius.rockpath_round_small_1");
    assert(rock != nullptr);
    assert(rock->templateCategory == arp::vegetation::TemplateCategory::RockMossy02);
    const auto* grass = data.findAsset("quaternius.grass_common_short");
    assert(grass != nullptr);
    assert(grass->templateCategory == arp::vegetation::TemplateCategory::FoliageGrass);

    const auto* commonTreeTemplate = data.findTemplate("quaternius.commontree_1");
    assert(commonTreeTemplate != nullptr);
    assert(commonTreeTemplate->vertices.size() > 0u);
    assert(commonTreeTemplate->indices.size() > 0u);
    assert(commonTreeTemplate->sections.size() > 0u);
    assert(commonTreeTemplate->category == arp::vegetation::TemplateCategory::TreeMaturePine);
}

} // namespace

int main()
{
    runtime_catalog_lists_all_generated_glbs();
    representative_glbs_load_through_resource_manager();
    vegetation_catalog_resource_builds_template_registry();
    return 0;
}

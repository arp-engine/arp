#include "core/memory/cpu_allocator.h"
#include "resource/prefab_asset_resources.h"
#include "resource/resource_manager.h"

#include <cassert>
#include <filesystem>
#include <iostream>

namespace {

constexpr const char* kUnityChanRoot = "artifacts/external-assets/UnityChan_1.4.0_pika_visible";

arp::core::pmr::String text(const char* value)
{
    return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
}

arp::resource::PrefabAssetGraphBuildDesc makeDesc()
{
    using namespace arp::resource;

    PrefabAssetGraphBuildDesc desc;
    desc.alias = text("prefab/unitychan-pika-visible");
    desc.displayName = text("UnityChan Pika Visible");
    desc.sourcePath = text(kUnityChanRoot);
    desc.sourceKind = text("UnityPrefabFolder");
    desc.runtimePayloadPackageRoot =
        std::filesystem::path("artifacts/prefab-asset-resource-smoke-package");
    desc.shaderPaths.basicMaterialPreview = text("shaders/samples/material_preview_pbr.slang");
    desc.shaderPaths.stylizedScene = text("shaders/samples/stylized_scene.slang");
    desc.shaderPaths.guiOverlay = text("shaders/gui/gui_overlay.slang");
    desc.startupSplashShaderPath = text("shaders/samples/startup_splash.slang");
    return desc;
}

bool packageHasType(const arp::resource::InternalAssetPackageDesc& package, const char* typeName)
{
    const auto expected = text(typeName);
    for (const auto& entry : package.entries) {
        if (entry.resourceType == expected) {
            return true;
        }
    }
    return false;
}

} // namespace

int main()
{
    if (!std::filesystem::exists(std::filesystem::path(kUnityChanRoot))) {
        std::cout
            << "prefab_asset_resource_smoke skipped: UnityChan external asset folder is not present\n";
        return 0;
    }

    arp::resource::registerPrefabAssetReflection();

    const auto graph = arp::resource::PrefabAssetGraphBuilder().buildPackage(makeDesc());
    if (!graph.success) {
        std::cerr << graph.diagnostics << "\n";
    }
    assert(graph.success);
    assert(packageHasType(graph.package, "ExternalAssetSourceResource"));
    assert(packageHasType(graph.package, "PrefabResource"));
    assert(packageHasType(graph.package, "ModelResource"));
    assert(packageHasType(graph.package, "SkinnedMeshResource"));
    assert(packageHasType(graph.package, "MaterialResource"));
    assert(packageHasType(graph.package, "TextureResource"));
    assert(packageHasType(graph.package, "PrefabRuntimePayloadResource"));
    assert(packageHasType(graph.package, "SequenceResource"));
    assert(packageHasType(graph.package, "SequenceBindingSetResource"));

    arp::resource::ResourceManager manager;
    for (const auto& entry : graph.package.entries) {
        arp::core::pmr::String diagnostics(&arp::core::defaultCpuMemoryResource());
        auto typed =
            arp::resource::createPrefabResourceFromEnvelope(entry.envelopeYaml, diagnostics);
        if (!typed) {
            std::cerr << diagnostics << "\n";
            assert(false);
        }
        const auto loaded = manager.loadResource(std::move(typed));
        if (!loaded.success) {
            std::cerr << loaded.diagnostics << "\n";
        }
        assert(loaded.success);
    }

    const auto prefab =
        manager.findByAlias<arp::resource::PrefabResource>("prefab/unitychan-pika-visible");
    assert(prefab.ready());
    assert(prefab.get()->objectCount > 0);
    assert(prefab.get()->componentCount > 0);
    assert(!prefab.get()->runtimePayloadId.empty());
    assert(prefab.get()->shaderPackageIds.size() == 4u);

    return 0;
}

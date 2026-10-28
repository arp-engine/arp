#include "3d/cloud_component.h"
#include "reflection/serialization.h"
#include "scene/scene.h"
#include "scriptvm/plugin/cpp_plugin_importer.h"
#include "tests/fixtures/cloud/cloud_fixture.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <filesystem>

namespace {
using namespace arp;

class CloudPluginSmoke final {
    static core::UniquePtr<scene::Component> factory(
        scene::GameObject&, const core::pmr::String& type)
    {
        if (type == "VolumetricCloudComponent")
            return core::makeUnique<scene3d::VolumetricCloudComponent>();
        return {};
    }

public:
    static int run(const std::filesystem::path& cache)
    {
        scriptvm::ScriptPluginServiceRegistry services;
        scene::MainSceneRegistry mainSceneRegistry;
        scene::Scene scene;
        scene.setComponentFactory(&factory);
        assert(services.provide(
            core::pmr::String(scene::kMainSceneService), scene::kMainSceneServiceVersion, &scene));
        assert(services.provide(core::pmr::String(scene::kMainSceneRegistryService),
            scene::kMainSceneRegistryServiceVersion,
            &mainSceneRegistry));
        std::array<std::shared_ptr<scriptvm::ModuleExports>, 2> dependencies;
        std::shared_ptr<scriptvm::ModuleExports> exports;
        {
            scriptvm::CppPluginImporterDesc desc;
            desc.sourceRoot = std::filesystem::path(ARP_CLOUD_PLUGIN_SOURCE) / "modules";
            desc.includeRoot = std::filesystem::path(ARP_CLOUD_PLUGIN_SOURCE) / "src";
            desc.additionalIncludeRoots.emplace_back(ARP_CLOUD_PLUGIN_FKYAML);
            desc.cacheRoot = cache;
            desc.hostLibrary = ARP_CLOUD_PLUGIN_HOST;
            desc.compilerCommand = ARP_CLOUD_PLUGIN_COMPILER;
            desc.reflectionCodegenCommand = ARP_CLOUD_PLUGIN_CODEGEN;
            desc.services = &services;
            scriptvm::CppPluginImporter importer(std::move(desc));
            assert(!importer.loadModule("arp_cloud_missing_module"));
            assert(!importer.lastDiagnostics().empty());
            const char* names[] = {"arp_core", "arp_scene"};
            const char* hashes[] = {
                "sha256:64ec4ff4e94ccc7eeeda00410452a69ab80f21c3cd5133aeb5988981600e4ff8",
                "sha256:25e2a198e25b72f5c3bb10a18fa74a69a233ffeb2378dd956f6646e6d28560a5"};
            for (std::size_t i = 0; i < dependencies.size(); ++i) {
                dependencies[i] = importer.loadModule(names[i]);
                if (!dependencies[i]) {
                    std::fprintf(stderr,
                        "CLOUD_DYNAMIC_DEPENDENCY_REJECTED module=%s %s\n",
                        names[i],
                        importer.lastDiagnostics().c_str());
                    return 1;
                }
                assert(dependencies[i]->exportContractHash == hashes[i]);
                assert(importer.loadModule(names[i]) == dependencies[i]);
            }
            exports = importer.loadModule("arp_scene3d");
            if (!exports) {
                std::fprintf(stderr,
                    "CLOUD_DYNAMIC_PLUGIN_REJECTED %s\n",
                    importer.lastDiagnostics().c_str());
                return 1;
            }
            assert(importer.loadModule("arp_scene3d") == exports);
        }
        assert(exports->reflectionRegistration && exports->moduleVersion == 1u);
        assert(exports->exportContractHash
            == "sha256:ca279a1393eb15d91407633a0a5fe86d83d10b4f5037fac7aa90cdc62862565f");
        for (const auto* name : {"CloudLayerDesc", "CloudDesc", "VolumetricCloudComponent"}) {
            bool found = false;
            for (const auto& binding : exports->reflectionRegistration->types())
                found |= binding.name == name && binding.classInfo;
            assert(found);
        }

        // The host owns scene/component state. Dispatch metadata and callables
        // come from the real dynamically loaded formal module.
        auto& object = scene.createObject("dynamic-cloud");
        auto* component = object.create_component("VolumetricCloudComponent");
        assert(component);
        const auto native = component->nativeReflectionObject();
        assert(native.is_native_reference() && native.native_reference_alive());
        scene3d::CloudDesc authored;
        authored.layer = tests::cloud::layer();
        authored.densityResource = "cloud/dynamic-formal";
        assert(native.call("set_cloud", authored).cast<bool>());
        const auto current = native.call("cloud");
        const auto* cloud = current.cast<scene3d::CloudDesc*>();
        assert(cloud && cloud->densityResource == authored.densityResource);
        const auto serialized = reflection::serialize_to_yaml(current);
        assert(serialized.success);
        const auto restored = reflection::deserialize_from_yaml(serialized.yaml);
        assert(restored.success);
        const auto* roundtrip = restored.value.cast<scene3d::CloudDesc*>();
        assert(roundtrip && roundtrip->densityResource == authored.densityResource);
        authored.layer.extinction = -1.f;
        assert(!native.call("set_cloud", authored).cast<bool>());
        assert(scene.destroyObject(object.id()).ok());
        assert(!native.native_reference_alive());
        std::printf(
            "CLOUD_DYNAMIC_PLUGIN_PASS formal-manifest shared-engine-ABI reflection YAML owner-expiry importer-destroyed\n");
        return 0;
    }
};
} // namespace

int main(int argc, char** argv)
{
    return argc == 2 ? CloudPluginSmoke::run(argv[1]) : 2;
}

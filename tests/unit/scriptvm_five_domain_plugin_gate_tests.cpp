#include "3d/collider_component.h"
#include "3d/components.h"
#include "core/math.h"
#include "core/runtime/async_runtime.h"
#include "network/io_context.h"
#include "network/network_module.h"
#include "resource/resource_manager.h"
#include "resource/resource_module.h"
#include "scene/camera.h"
#include "scene/scene.h"
#include "scriptvm/runtime/cpp_plugin_api.h"
#include "scriptvm/runtime/module_registry.h"

#include <cassert>
#include <cstdint>
#include <memory>

extern "C" int arp_register_arp_core(ArpScriptPluginHostV2*);
extern "C" int arp_register_arp_scene(ArpScriptPluginHostV2*);
extern "C" int arp_register_arp_scene3d(ArpScriptPluginHostV2*);
extern "C" int arp_register_arp_resources(ArpScriptPluginHostV2*);
extern "C" int arp_register_arp_network(ArpScriptPluginHostV2*);

bool arp_add_arp_core_static_plugin(arp::scriptvm::StaticPluginCatalog&);
bool arp_add_arp_scene_static_plugin(arp::scriptvm::StaticPluginCatalog&);
bool arp_add_arp_scene3d_static_plugin(arp::scriptvm::StaticPluginCatalog&);
bool arp_add_arp_resources_static_plugin(arp::scriptvm::StaticPluginCatalog&);
bool arp_add_arp_network_static_plugin(arp::scriptvm::StaticPluginCatalog&);

const char* arp_arp_core_manifest_module();
const char* arp_arp_scene_manifest_module();
const char* arp_arp_scene3d_manifest_module();
const char* arp_arp_resources_manifest_module();
const char* arp_arp_network_manifest_module();
unsigned int arp_arp_core_manifest_version();
unsigned int arp_arp_scene_manifest_version();
unsigned int arp_arp_scene3d_manifest_version();
unsigned int arp_arp_resources_manifest_version();
unsigned int arp_arp_network_manifest_version();
const char* arp_arp_core_manifest_export_contract_hash();
const char* arp_arp_scene_manifest_export_contract_hash();
const char* arp_arp_scene3d_manifest_export_contract_hash();
const char* arp_arp_resources_manifest_export_contract_hash();
const char* arp_arp_network_manifest_export_contract_hash();
const char* arp_arp_core_manifest_reflection_registry_hash();
const char* arp_arp_scene_manifest_reflection_registry_hash();
const char* arp_arp_scene3d_manifest_reflection_registry_hash();
const char* arp_arp_resources_manifest_reflection_registry_hash();
const char* arp_arp_network_manifest_reflection_registry_hash();

namespace {

using arp::core::pmr::String;
using arp::scriptvm::ScriptPluginRegistrar;
using arp::scriptvm::ScriptPluginServiceRegistry;

struct PluginSpec {
    const char* module;
    ArpScriptPluginEntryV2 entry;
    const char* (*manifestModule)();
    unsigned int (*manifestVersion)();
    const char* (*manifestHash)();
    const char* (*manifestReflectionHash)();
    const char* requiredServices[2];
    std::size_t requiredServiceCount;
};

const PluginSpec kPlugins[] = {
    {"arp_core",
        &arp_register_arp_core,
        &arp_arp_core_manifest_module,
        &arp_arp_core_manifest_version,
        &arp_arp_core_manifest_export_contract_hash,
        &arp_arp_core_manifest_reflection_registry_hash,
        {},
        0u},
    {"arp_scene",
        &arp_register_arp_scene,
        &arp_arp_scene_manifest_module,
        &arp_arp_scene_manifest_version,
        &arp_arp_scene_manifest_export_contract_hash,
        &arp_arp_scene_manifest_reflection_registry_hash,
        {"arp.scene.main-scene", "arp.scene.main-scene-registry"},
        2u},
    {"arp_scene3d",
        &arp_register_arp_scene3d,
        &arp_arp_scene3d_manifest_module,
        &arp_arp_scene3d_manifest_version,
        &arp_arp_scene3d_manifest_export_contract_hash,
        &arp_arp_scene3d_manifest_reflection_registry_hash,
        {},
        0u},
    {"arp_resources",
        &arp_register_arp_resources,
        &arp_arp_resources_manifest_module,
        &arp_arp_resources_manifest_version,
        &arp_arp_resources_manifest_export_contract_hash,
        &arp_arp_resources_manifest_reflection_registry_hash,
        {"arp.resources.manager", "arp.runtime.async"},
        2u},
    {"arp_network",
        &arp_register_arp_network,
        &arp_arp_network_manifest_module,
        &arp_arp_network_manifest_version,
        &arp_arp_network_manifest_export_contract_hash,
        &arp_arp_network_manifest_reflection_registry_hash,
        {"arp.network.io-context"},
        1u},
};

ArpScriptPluginHostV2 makeHost(
    ScriptPluginRegistrar& registrar, ScriptPluginServiceRegistry& services)
{
    ArpScriptPluginHostV2 host{};
    host.abiVersion = ARP_SCRIPT_PLUGIN_ABI_VERSION_V2;
    host.registrar = &registrar;
    host.services = &services;
    host.platform = "test";
    host.buildConfiguration = "debug";
    host.capabilities = "script,reflection,services";
    return host;
}

void stampManifestMetadata(
    const PluginSpec& spec, const std::shared_ptr<arp::scriptvm::ModuleExports>& exports)
{
    assert(exports != nullptr);
    assert(String(spec.manifestModule(), &arp::core::defaultCpuMemoryResource()) == spec.module);
    exports->moduleVersion = spec.manifestVersion();
    exports->exportContractHash =
        String(spec.manifestHash(), &arp::core::defaultCpuMemoryResource());
    exports->reflectionRegistryHash =
        String(spec.manifestReflectionHash(), &arp::core::defaultCpuMemoryResource());
    for (std::size_t index = 0; index < spec.requiredServiceCount; ++index) {
        arp::scriptvm::ScriptPluginServiceRequirement service;
        service.serviceId =
            String(spec.requiredServices[index], &arp::core::defaultCpuMemoryResource());
        service.version = 1u;
        exports->requiredServices.push_back(std::move(service));
    }
}

std::shared_ptr<arp::scriptvm::ModuleExports> loadDynamic(
    const PluginSpec& spec, ScriptPluginServiceRegistry& services)
{
    ScriptPluginRegistrar registrar(
        String(spec.module, &arp::core::defaultCpuMemoryResource()), &services);
    auto host = makeHost(registrar, services);
    assert(spec.entry(&host) == 1);
    auto exports = registrar.build();
    stampManifestMetadata(spec, exports);
    return exports;
}

void assertExportParity(const PluginSpec& spec,
    const arp::scriptvm::ModuleExports& dynamicExports,
    const arp::scriptvm::ModuleExports& staticExports)
{
    assert(dynamicExports.moduleVersion == spec.manifestVersion());
    assert(staticExports.moduleVersion == spec.manifestVersion());
    assert(dynamicExports.exportContractHash
        == String(spec.manifestHash(), &arp::core::defaultCpuMemoryResource()));
    assert(staticExports.exportContractHash == dynamicExports.exportContractHash);
    assert(dynamicExports.reflectionRegistryHash
        == String(spec.manifestReflectionHash(), &arp::core::defaultCpuMemoryResource()));
    assert(staticExports.reflectionRegistryHash == dynamicExports.reflectionRegistryHash);
    assert(staticExports.requiredServices.size() == dynamicExports.requiredServices.size());
    for (const auto& service : dynamicExports.requiredServices) {
        bool found = false;
        for (const auto& candidate : staticExports.requiredServices) {
            if (candidate.serviceId == service.serviceId && candidate.version == service.version) {
                found = true;
                break;
            }
        }
        assert(found);
    }
    assert(dynamicExports.symbols.size() == staticExports.symbols.size());
    for (const auto& [name, dynamicValue] : dynamicExports.symbols) {
        const auto found = staticExports.symbols.find(name);
        assert(found != staticExports.symbols.end());
        const auto& staticValue = found->second;
        assert(dynamicValue.is_native_reference() == staticValue.is_native_reference());
        if (dynamicValue.is_native_reference()) {
            assert(dynamicValue.class_() != nullptr);
            assert(staticValue.class_() != nullptr);
            assert(dynamicValue.class_()->name() == staticValue.class_()->name());
        }
    }
    assert(dynamicExports.reflectionRegistration != nullptr);
    assert(staticExports.reflectionRegistration != nullptr);
    assert(dynamicExports.reflectionRegistration->types().size()
        == staticExports.reflectionRegistration->types().size());
    assert(dynamicExports.reflectionRegistration->functions().size()
        == staticExports.reflectionRegistration->functions().size());
    assert(dynamicExports.reflectionRegistration->enums().size()
        == staticExports.reflectionRegistration->enums().size());
    for (const auto& type : dynamicExports.reflectionRegistration->types()) {
        bool found = false;
        for (const auto& candidate : staticExports.reflectionRegistration->types()) {
            if (candidate.name == type.name && candidate.classInfo == type.classInfo) {
                found = true;
                break;
            }
        }
        assert(found);
    }
    for (const auto& function : dynamicExports.reflectionRegistration->functions()) {
        bool found = false;
        for (const auto& candidate : staticExports.reflectionRegistration->functions()) {
            if (candidate.name == function.name
                && candidate.function.params_info().size() == function.function.params_info().size()
                && candidate.function.return_type() == function.function.return_type()) {
                found = true;
                break;
            }
        }
        assert(found);
    }
    for (const auto& enumeration : dynamicExports.reflectionRegistration->enums()) {
        bool found = false;
        for (const auto& candidate : staticExports.reflectionRegistration->enums()) {
            if (candidate.name == enumeration.name) {
                found = true;
                break;
            }
        }
        assert(found);
    }
}

arp::core::UniquePtr<arp::scene::Component> scene3dFactory(
    arp::scene::GameObject&, const String& type)
{
    if (type == "MeshComponent")
        return arp::core::makeUnique<arp::scene3d::MeshComponent>();
    if (type == "LightComponent")
        return arp::core::makeUnique<arp::scene3d::LightComponent>();
    if (type == "TerrainComponent")
        return arp::core::makeUnique<arp::scene3d::TerrainComponent>();
    if (type == "CameraComponent")
        return arp::core::makeUnique<arp::scene::CameraComponent>();
    if (type == "ColliderComponent")
        return arp::core::makeUnique<arp::scene3d::ColliderComponent>();
    return {};
}

void assertDirectObjectSmoke(const std::shared_ptr<arp::scriptvm::ModuleExports>& sceneExports,
    const std::shared_ptr<arp::scriptvm::ModuleExports>& scene3dExports,
    const std::shared_ptr<arp::scriptvm::ModuleExports>& resourceExports,
    const std::shared_ptr<arp::scriptvm::ModuleExports>& networkExports)
{
    assert(sceneExports->reflectionRegistration != nullptr);
    assert(scene3dExports->reflectionRegistration != nullptr);
    arp::scene::Scene scene{String("joint_gate", &arp::core::defaultCpuMemoryResource()),
        arp::scene::SceneId::fromValue(901u)};
    scene.setComponentFactory(&scene3dFactory);
    const auto sceneObject = arp::reflection::object::native_reference(&scene);
    const auto object = sceneObject.call(
        "create_object_ref", String("root", &arp::core::defaultCpuMemoryResource()));
    assert(object.is_native_reference());
    const auto meshComponent = object.call(
        "create_component", String("MeshComponent", &arp::core::defaultCpuMemoryResource()));
    assert(meshComponent.is_native_reference());
    assert(meshComponent.native_reference_alive());
    assert(object.call("component_count").cast<std::uint32_t>() == 2u);
    assert(sceneObject.call("destroy_object_ref", object.cast<arp::scene::GameObject*>())
            .cast<bool>());
    assert(!object.native_reference_alive());
    assert(!meshComponent.native_reference_alive());

    const auto manager =
        resourceExports->symbols.find(String("manager", &arp::core::defaultCpuMemoryResource()));
    assert(manager != resourceExports->symbols.end());
    assert(manager->second.is_native_reference());
    assert(manager->second.class_()->name() == "ResourceManager");

    const auto network =
        networkExports->symbols.find(String("network", &arp::core::defaultCpuMemoryResource()));
    assert(network != networkExports->symbols.end());
    assert(network->second.is_native_reference());
    assert(network->second.class_()->name() == "NetworkRuntime");
    const auto connection = network->second.call("create_connection",
        arp::scriptvm::NetworkConnectionKind::Http,
        String("https://joint.test", &arp::core::defaultCpuMemoryResource()));
    assert(connection.is_native_reference());
    assert(connection.class_()->name() == "NetworkConnection");
}

void rejectMissingRequiredServices()
{
    ScriptPluginServiceRegistry services;
    for (const auto& spec : kPlugins) {
        ScriptPluginRegistrar registrar(
            String(spec.module, &arp::core::defaultCpuMemoryResource()), &services);
        auto host = makeHost(registrar, services);
        const int result = spec.entry(&host);
        if (String(spec.module, &arp::core::defaultCpuMemoryResource()) == "arp_resources"
            || String(spec.module, &arp::core::defaultCpuMemoryResource()) == "arp_network"
            || String(spec.module, &arp::core::defaultCpuMemoryResource()) == "arp_scene") {
            assert(result == 0);
        } else {
            assert(result == 1);
        }
    }
}

} // namespace

int main()
{
    rejectMissingRequiredServices();

    arp::core::runtime::AsyncRuntimeDesc runtimeDesc;
    runtimeDesc.completionQueueCapacity = 8u;
    runtimeDesc.workerCount = 1u;
    runtimeDesc.workQueueCapacity = 4u;
    runtimeDesc.completionBudget = 8u;
    arp::core::runtime::AsyncRuntime asyncRuntime(runtimeDesc);
    arp::resource::ResourceManager resourceManager;
    arp::network::io_context ioContext;
    arp::scene::Scene mainScene{String("gate-main", &arp::core::defaultCpuMemoryResource()),
        arp::scene::SceneId::fromValue(902u)};
    arp::scene::MainSceneRegistry mainSceneRegistry;
    ScriptPluginServiceRegistry services;
    assert(services.provide(
        String(arp::scene::kMainSceneService, &arp::core::defaultCpuMemoryResource()),
        arp::scene::kMainSceneServiceVersion,
        &mainScene));
    assert(services.provide(
        String(arp::scene::kMainSceneRegistryService, &arp::core::defaultCpuMemoryResource()),
        arp::scene::kMainSceneRegistryServiceVersion,
        &mainSceneRegistry));
    assert(services.provide(
        String(arp::scriptvm::kResourceManagerService, &arp::core::defaultCpuMemoryResource()),
        arp::scriptvm::kResourceManagerServiceVersion,
        &resourceManager));
    assert(services.provide(
        String(arp::scriptvm::kAsyncRuntimeService, &arp::core::defaultCpuMemoryResource()),
        arp::scriptvm::kAsyncRuntimeServiceVersion,
        &asyncRuntime));
    assert(services.provide(
        String(arp::scriptvm::kNetworkIoContextService, &arp::core::defaultCpuMemoryResource()),
        arp::scriptvm::kNetworkIoContextServiceVersion,
        &ioContext));

    std::shared_ptr<arp::scriptvm::ModuleExports> dynamic[5];
    for (std::size_t index = 0; index < 5u; ++index)
        dynamic[index] = loadDynamic(kPlugins[index], services);

    arp::scriptvm::StaticPluginCatalog catalog;
    assert(arp_add_arp_core_static_plugin(catalog));
    assert(arp_add_arp_scene_static_plugin(catalog));
    assert(arp_add_arp_scene3d_static_plugin(catalog));
    assert(arp_add_arp_resources_static_plugin(catalog));
    assert(arp_add_arp_network_static_plugin(catalog));

    std::shared_ptr<arp::scriptvm::ModuleExports> statik[5];
    for (std::size_t index = 0; index < 5u; ++index) {
        String diagnostics(&arp::core::defaultCpuMemoryResource());
        statik[index] =
            catalog.load(String(kPlugins[index].module, &arp::core::defaultCpuMemoryResource()),
                &services,
                diagnostics);
        assert(statik[index] != nullptr);
        assert(diagnostics.empty());
        assertExportParity(kPlugins[index], *dynamic[index], *statik[index]);
    }

    // AOT must validate the same immutable descriptor that static loading
    // exposes. Exercise every formal domain, including its manifest service
    // requirements, before allowing execution to proceed.
    arp::scriptvm::ModuleRegistry aotRegistry;
    aotRegistry.setStaticPluginCatalog(&catalog, &services);
    arp::scriptvm::AotServiceDependencyV5 serviceDependencies[5][2]{};
    for (std::size_t index = 0; index < 5u; ++index) {
        const auto& spec = kPlugins[index];
        for (std::size_t serviceIndex = 0; serviceIndex < spec.requiredServiceCount;
            ++serviceIndex) {
            serviceDependencies[index][serviceIndex] = {spec.requiredServices[serviceIndex], 1u};
        }
        const arp::scriptvm::AotPluginDependencyV5 dependency{spec.module,
            spec.manifestVersion(),
            spec.manifestHash(),
            spec.manifestReflectionHash(),
            serviceDependencies[index],
            spec.requiredServiceCount};
        arp::scriptvm::VmError error;
        assert(aotRegistry.resolveAotPluginDependency(dependency, &error));
    }

    assertDirectObjectSmoke(dynamic[1], dynamic[2], dynamic[3], dynamic[4]);
    assertDirectObjectSmoke(statik[1], statik[2], statik[3], statik[4]);

    // A catalog containing a duplicate module must be rejected before any
    // plugin entry is invoked.
    assert(!arp_add_arp_core_static_plugin(catalog));
    return 0;
}

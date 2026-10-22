#include "3d/collider_component.h"
#include "3d/components.h"
#include "core/math/math.h"
#include "reflection/reflection.h"
#include "scene/camera.h"
#include "scene/diagnostics.h"
#include "scene/scene.h"
#include "scene/script_behavior_component.h"
#include "scene/transform.h"
#include "scriptvm/plugin/cpp_plugin_api.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <string_view>
#include <utility>

extern "C" int arp_register_arp_core(ArpScriptPluginHostV2*);
extern "C" int arp_register_arp_scene(ArpScriptPluginHostV2*);
extern "C" int arp_register_arp_scene3d(ArpScriptPluginHostV2*);

namespace arp::reflection::generated {
bool register_arp_scene_reflection(arp::scriptvm::ModuleRegistration*);
bool register_arp_scene3d_reflection(arp::scriptvm::ModuleRegistration*);
} // namespace arp::reflection::generated

namespace {

using arp::core::pmr::String;
using arp::scriptvm::ScriptPluginRegistrar;
using arp::scriptvm::ScriptPluginServiceRegistry;

bool hasReflectedType(const arp::scriptvm::ModuleExports& exports, const char* name)
{
    for (const auto& binding : exports.reflectionRegistration->types()) {
        if (binding.name == name)
            return true;
    }
    return false;
}

const arp::reflection::class_info* findReflectedType(
    const arp::scriptvm::ModuleExports& exports, const char* name)
{
    for (const auto& binding : exports.reflectionRegistration->types()) {
        if (binding.name == name)
            return binding.classInfo;
    }
    return nullptr;
}

ArpScriptPluginHostV2 makeHost(
    ScriptPluginRegistrar& registrar, ScriptPluginServiceRegistry& services)
{
    ArpScriptPluginHostV2 host{};
    host.abiVersion = ARP_SCRIPT_PLUGIN_ABI_VERSION_V2;
    host.registrar = &registrar;
    host.services = &services;
    host.platform = "test";
    host.buildConfiguration = "debug";
    return host;
}

std::shared_ptr<arp::scriptvm::ModuleExports> loadStatic(const char* module,
    ArpScriptPluginEntryV2 entry,
    ScriptPluginServiceRegistry& services,
    const char* exportContractHash)
{
    arp::scriptvm::StaticPluginCatalog catalog;
    arp::scriptvm::ScriptPluginDescriptor descriptor{
        String(module, &arp::core::defaultCpuMemoryResource()),
        2u,
        1u,
        entry,
        String(exportContractHash, &arp::core::defaultCpuMemoryResource()),
    };
    if (std::string_view(module) == "arp_scene") {
        descriptor.requiredServices.push_back(
            {String(arp::scene::kMainSceneService, &arp::core::defaultCpuMemoryResource()),
                arp::scene::kMainSceneServiceVersion});
    }
    assert(catalog.add(std::move(descriptor)));
    String diagnostics(&arp::core::defaultCpuMemoryResource());
    auto exports = catalog.load(
        String(module, &arp::core::defaultCpuMemoryResource()), &services, diagnostics);
    assert(exports != nullptr);
    assert(diagnostics.empty());
    return exports;
}

void stampManifestMetadata(
    const std::shared_ptr<arp::scriptvm::ModuleExports>& exports, const char* exportContractHash)
{
    assert(exports != nullptr);
    exports->moduleVersion = 1u;
    exports->exportContractHash =
        String(exportContractHash, &arp::core::defaultCpuMemoryResource());
}

void assertDynamicStaticParity(const arp::scriptvm::ModuleExports& dynamicExports,
    const arp::scriptvm::ModuleExports& staticExports)
{
    assert(dynamicExports.moduleVersion == staticExports.moduleVersion);
    assert(dynamicExports.exportContractHash == staticExports.exportContractHash);
    assert(dynamicExports.symbols.size() == staticExports.symbols.size());
    for (const auto& [name, value] : dynamicExports.symbols) {
        (void)value;
        assert(staticExports.symbols.find(name) != staticExports.symbols.end());
    }
    assert(dynamicExports.reflectionRegistration != nullptr);
    assert(staticExports.reflectionRegistration != nullptr);
    assert(dynamicExports.reflectionRegistration->types().size()
        == staticExports.reflectionRegistration->types().size());
    assert(dynamicExports.reflectionRegistration->functions().size()
        == staticExports.reflectionRegistration->functions().size());
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
}

void core_exports_are_reflection_bound()
{
    ScriptPluginServiceRegistry services;
    ScriptPluginRegistrar registrar(
        String("arp_core", &arp::core::defaultCpuMemoryResource()), &services);
    auto host = makeHost(registrar, services);
    assert(arp_register_arp_core(&host) == 1);
    const auto exports = registrar.build();
    assert(exports != nullptr);
    assert(exports->symbols.find(String("abi_version", &arp::core::defaultCpuMemoryResource()))
        != exports->symbols.end());
    assert(exports->reflectionRegistration->types().size() >= 5u);
    assert(hasReflectedType(*exports, "ScriptResult"));
    assert(hasReflectedType(*exports, "Mat4"));
    const auto* mat4 = findReflectedType(*exports, "Mat4");
    assert(mat4 != nullptr);
    assert(!mat4->operator[]("m").is_undefined());
    arp::core::Mat4 matrix = arp::core::Mat4::identity();
    matrix.m[3] = 7.0f;
    auto matrixObject = arp::reflection::object::native_reference(&matrix);
    const auto reflectedElements = matrixObject["m"].cast<std::array<float, 16>>();
    assert(reflectedElements[0] == 1.0f && reflectedElements[3] == 7.0f);
    for (const auto* name : {"vec3_dot",
             "vec3_length",
             "vec3_normalize",
             "vec3_lerp",
             "scalar_sin",
             "scalar_cos",
             "quat_mul",
             "quat_slerp",
             "mat4_identity",
             "mat4_multiply",
             "mat4_transform_point"}) {
        assert(exports->symbols.find(String(name, &arp::core::defaultCpuMemoryResource()))
            != exports->symbols.end());
    }
}

void scene_exports_are_reflection_bound()
{
    arp::scene::Scene mainScene{String("test-main", &arp::core::defaultCpuMemoryResource()),
        arp::scene::SceneId::fromValue(700u)};
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
    ScriptPluginRegistrar registrar(
        String("arp_scene", &arp::core::defaultCpuMemoryResource()), &services);
    auto host = makeHost(registrar, services);
    assert(arp_register_arp_scene(&host) == 1);
    const auto exports = registrar.build();
    assert(exports != nullptr);
    assert(exports->reflectionRegistration->types().size() >= 7u);
    assert(hasReflectedType(*exports, "SceneUpdateContext"));
    assert(hasReflectedType(*exports, "SceneDiagnostic"));
    assert(hasReflectedType(*exports, "ScriptBehaviorComponent"));
    assert(hasReflectedType(*exports, "CameraTemporalComponent"));
    assert(hasReflectedType(*exports, "TransformComponent"));
    assert(hasReflectedType(*exports, "GameObject"));
    assert(hasReflectedType(*exports, "Component"));
    // SceneDiagnostic's script surface must come from diagnostics.h markers
    // and generated registration, including its reflected properties.
    const auto* diagnostic = findReflectedType(*exports, "SceneDiagnostic");
    assert(diagnostic != nullptr);
    const auto properties = diagnostic->get_all_properties();
    assert(properties.find(String("code", &arp::core::defaultCpuMemoryResource()))
        != properties.end());
    assert(properties.find(String("message", &arp::core::defaultCpuMemoryResource()))
        != properties.end());
    // The direct-object scene contract has no module-level service callables.
    // All ordinary calls enter generated methods on a real Scene/GameObject/
    // Component object. Scene registry operations are added in the P5 main
    // scene phase, not as a service facade in this module.
    for (const auto* name : {"generation",
             "create_object",
             "find_object",
             "destroy_object",
             "create_game_object",
             "find_game_object",
             "reparent",
             "set_active",
             "set_world_transform",
             "get_world_transform",
             "is_active",
             "has_component",
             "get_parent",
             "get_name",
             "find_component",
             "create_component",
             "component_enabled",
             "set_component_enabled",
             "remove_component",
             "attach_script_behavior",
             "detach_script_behavior",
             "last_error",
             "create_scene",
             "find_scene",
             "set_main_scene",
             "destroy_scene",
             "scene_root",
             "scene_name",
             "is_main_scene",
             "commit_main_scene"}) {
        const auto found =
            exports->symbols.find(String(name, &arp::core::defaultCpuMemoryResource()));
        assert(found == exports->symbols.end());
    }
    const auto* gameObject = findReflectedType(*exports, "GameObject");
    const auto* component = findReflectedType(*exports, "Component");
    const auto* scene = findReflectedType(*exports, "Scene");
    assert(gameObject != nullptr && component != nullptr && scene != nullptr);
    for (const auto* name : {"object_id",
             "object_name",
             "component_count",
             "component_at",
             "parent_object",
             "child_at",
             "destroy_self"})
        assert(!gameObject->operator[](name).is_undefined());
    for (const auto* name : {"scene_id_value",
             "create_object_ref",
             "find_object_by_name_ref",
             "destroy_object_ref",
             "reparent_objects",
             "create_scene",
             "destroy_scene",
             "request_main_scene",
             "is_main_scene",
             "main_scene_generation"})
        assert(!scene->operator[](name).is_undefined());
    const auto* transform = findReflectedType(*exports, "TransformComponent");
    const auto* temporal = findReflectedType(*exports, "CameraTemporalComponent");
    const auto* behavior = findReflectedType(*exports, "ScriptBehaviorComponent");
    assert(transform != nullptr && temporal != nullptr && behavior != nullptr);
    for (const auto* name : {"component_id",
             "owner_id",
             "enabled",
             "set_enabled",
             "local_transform",
             "world_transform",
             "set_local_transform",
             "set_local_translation",
             "set_local_rotation",
             "set_local_yaw_pitch"})
        assert(!transform->operator[](name).is_undefined());
    for (const auto* name : {"component_id",
             "owner_id",
             "enabled",
             "set_enabled",
             "previousFovDegrees",
             "previous_position",
             "previous_target",
             "set_previous_projection",
             "setPoseValid"})
        assert(!temporal->operator[](name).is_undefined());
    for (const auto* name :
        {"component_id", "owner_id", "enabled", "set_enabled", "failed", "failure_hook"})
        assert(!behavior->operator[](name).is_undefined());
}

void scene3d_exports_are_allow_listed()
{
    ScriptPluginServiceRegistry services;
    ScriptPluginRegistrar registrar(
        String("arp_scene3d", &arp::core::defaultCpuMemoryResource()), &services);
    auto host = makeHost(registrar, services);
    assert(arp_register_arp_scene3d(&host) == 1);
    const auto exports = registrar.build();
    assert(exports != nullptr);
    // Enums are module-level exports, not reflected type bindings. Keep the
    // type count focused on constructible/field-bearing classes and assert
    // the generated enum surface separately.
    assert(exports->reflectionRegistration->types().size() >= 13u);
    assert(exports->reflectionRegistration->enums().size() >= 5u);
    assert(hasReflectedType(*exports, "Bounds3D"));
    assert(!hasReflectedType(*exports, "TerrainHandle"));
    assert(!hasReflectedType(*exports, "MeshComponentDesc"));
    assert(!hasReflectedType(*exports, "TerrainComponentDesc"));
    assert(!hasReflectedType(*exports, "ScriptMeshComponentDesc"));
    assert(!hasReflectedType(*exports, "ScriptTerrainComponentDesc"));
    assert(!hasReflectedType(*exports, "ScriptCameraComponentDesc"));
    assert(hasReflectedType(*exports, "LightComponentDesc"));
    assert(hasReflectedType(*exports, "CameraComponentDesc"));
    assert(hasReflectedType(*exports, "ColliderComponentDesc"));
    // Concrete scene components are part of the generated script surface.
    // Renderer/physics handles stay unexported; only their safe authored
    // properties and lifecycle operations are reflected.
    const auto* meshComponent = findReflectedType(*exports, "MeshComponent");
    const auto* terrainComponent = findReflectedType(*exports, "TerrainComponent");
    const auto* lightComponent = findReflectedType(*exports, "LightComponent");
    const auto* cameraComponent = findReflectedType(*exports, "CameraComponent");
    const auto* colliderComponent = findReflectedType(*exports, "ColliderComponent");
    assert(meshComponent != nullptr && terrainComponent != nullptr && lightComponent != nullptr
        && cameraComponent != nullptr && colliderComponent != nullptr);
    for (const auto* component :
        {meshComponent, terrainComponent, lightComponent, cameraComponent, colliderComponent}) {
        assert((*component)["__new__"].is_undefined());
    }
    for (const auto* name :
        {"local_bounds", "set_local_bounds", "base_color_tint", "set_base_color_tint"})
        assert(!meshComponent->operator[](name).is_undefined());
    for (const auto* name : {"metadata", "has_terrain"})
        assert(!terrainComponent->operator[](name).is_undefined());
    for (const auto* name :
        {"type", "color", "intensity", "set_type", "set_color", "set_intensity"})
        assert(!lightComponent->operator[](name).is_undefined());
    for (const auto* name :
        {"type", "color", "direction", "intensity", "range", "decay", "cast_shadow"}) {
        const auto member = lightComponent->get_field(name);
        assert(member.is_property());
    }
    for (const auto* name : {"type", "color", "intensity"})
        assert(!lightComponent->get_field(name).is_function());
    for (const auto* name : {"set_type", "set_color", "set_intensity"})
        assert(lightComponent->get_field(name).is_function());
    for (const auto* name : {"script_desc", "set_script_desc", "set_projection", "set_near_plane"})
        assert(!cameraComponent->operator[](name).is_undefined());
    for (const auto* name : {"script_desc",
             "set_script_desc",
             "world_position",
             "set_world_transform",
             "set_linear_velocity",
             "apply_impulse"})
        assert(!colliderComponent->operator[](name).is_undefined());
    bool foundLightType = false;
    for (const auto& enumeration : exports->reflectionRegistration->enums()) {
        if (enumeration.name == "LightType") {
            foundLightType = true;
            assert(enumeration.enumeration.get_enum_value("Directional") == 0);
        }
    }
    assert(foundLightType);
    assert(exports->symbols.find(String("LightType", &arp::core::defaultCpuMemoryResource()))
        != exports->symbols.end());
    assert(exports->symbols.find(
               String("ScriptMeshComponentDesc", &arp::core::defaultCpuMemoryResource()))
        == exports->symbols.end());
    // Ordinary scene3d calls are intentionally absent from the plugin export
    // table. Scripts create and mutate the concrete components through the
    // generated GameObject/Component object API in arp_scene.
    for (const char* name : {"create_mesh_component",
             "create_light_component",
             "create_camera_component",
             "create_collider_component",
             "create_terrain_component",
             "add_mesh",
             "add_light",
             "update_mesh",
             "update_light",
             "remove_mesh",
             "remove_light",
             "last_error"}) {
        assert(exports->symbols.find(String(name, &arp::core::defaultCpuMemoryResource()))
            == exports->symbols.end());
    }
}

arp::core::UniquePtr<arp::scene::Component> directScene3dFactory(
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

void scene3d_components_are_direct_native_objects()
{
    arp::scriptvm::ModuleRegistration sceneRegistration("arp_scene");
    arp::scriptvm::ModuleRegistration scene3dRegistration("arp_scene3d");
    assert(arp::reflection::generated::register_arp_scene_reflection(&sceneRegistration));
    assert(arp::reflection::generated::register_arp_scene3d_reflection(&scene3dRegistration));

    const auto lightIntensity = arp::reflection::class_of<arp::scene3d::LightComponentDesc>()
                                    ->get_field("intensity")
                                    .cast<arp::reflection::property&>();
    assert(lightIntensity.check_hints(
        static_cast<std::uint8_t>(arp::reflection::PropertyHint::Editor)));
    assert(lightIntensity.check_hints(
        static_cast<std::uint8_t>(arp::reflection::PropertyHint::Serialize)));
    const auto runtimeOnlyMetadata = arp::reflection::class_of<arp::scene3d::TerrainMetadata>()
                                         ->get_field("hasExplicitMetadata")
                                         .cast<arp::reflection::property&>();
    assert(runtimeOnlyMetadata.check_hints(
        static_cast<std::uint8_t>(arp::reflection::PropertyHint::Serialize)));
    assert(!runtimeOnlyMetadata.check_hints(
        static_cast<std::uint8_t>(arp::reflection::PropertyHint::Editor)));

    arp::scene::Scene scene{String("main", &arp::core::defaultCpuMemoryResource()),
        arp::scene::SceneId::fromValue(11u)};
    scene.setComponentFactory(&directScene3dFactory);
    auto sceneValue = arp::reflection::object::native_reference(&scene);
    const auto objectValue = sceneValue.call(
        "create_object_ref", String("direct", &arp::core::defaultCpuMemoryResource()));
    assert(objectValue.is_native_reference());
    auto* object = objectValue.cast<arp::scene::GameObject*>();
    assert(object != nullptr);

    const char* componentTypes[] = {"MeshComponent",
        "LightComponent",
        "TerrainComponent",
        "CameraComponent",
        "ColliderComponent"};
    for (const auto* type : componentTypes) {
        const auto componentValue = objectValue.call(
            "create_component", String(type, &arp::core::defaultCpuMemoryResource()));
        assert(componentValue.is_native_reference());
        assert(componentValue.native_reference_alive());
        assert(componentValue.call("component_id").cast<std::uint64_t>() != 0u);
    }
    assert(objectValue.call("component_count").cast<std::uint32_t>() == 6u);

    const auto meshValue = objectValue.call("component_at", static_cast<std::uint32_t>(1));
    assert(meshValue.is_native_reference());
    const auto tint =
        meshValue.class_()->get_attr(meshValue, "base_color_tint").cast<arp::core::Vec4>();
    assert(tint.x == 1.0f && tint.y == 1.0f && tint.z == 1.0f && tint.w == 1.0f);

    assert(sceneValue.call("destroy_object_ref", object).cast<bool>());
    assert(!objectValue.native_reference_alive());
    assert(!meshValue.native_reference_alive());
    assert(meshValue.class_()->get_attr(meshValue, "base_color_tint").is_undefined());
}

void dynamic_and_static_catalogs_are_identical()
{
    arp::scene::Scene mainScene{String("parity-main", &arp::core::defaultCpuMemoryResource()),
        arp::scene::SceneId::fromValue(701u)};
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

    ScriptPluginRegistrar coreRegistrar(
        String("arp_core", &arp::core::defaultCpuMemoryResource()), &services);
    auto coreHost = makeHost(coreRegistrar, services);
    assert(arp_register_arp_core(&coreHost) == 1);
    const auto coreDynamic = coreRegistrar.build();
    stampManifestMetadata(
        coreDynamic, "sha256:64ec4ff4e94ccc7eeeda00410452a69ab80f21c3cd5133aeb5988981600e4ff8");
    const auto coreStatic = loadStatic("arp_core",
        &arp_register_arp_core,
        services,
        "sha256:64ec4ff4e94ccc7eeeda00410452a69ab80f21c3cd5133aeb5988981600e4ff8");
    assertDynamicStaticParity(*coreDynamic, *coreStatic);

    ScriptPluginRegistrar sceneRegistrar(
        String("arp_scene", &arp::core::defaultCpuMemoryResource()), &services);
    auto sceneHost = makeHost(sceneRegistrar, services);
    assert(arp_register_arp_scene(&sceneHost) == 1);
    const auto sceneDynamic = sceneRegistrar.build();
    stampManifestMetadata(
        sceneDynamic, "sha256:915612d5bf0d67157bccf3397941abf1229faae2a67d7327b16eb73faf15f4f7");
    const auto sceneStatic = loadStatic("arp_scene",
        &arp_register_arp_scene,
        services,
        "sha256:915612d5bf0d67157bccf3397941abf1229faae2a67d7327b16eb73faf15f4f7");
    assertDynamicStaticParity(*sceneDynamic, *sceneStatic);

    ScriptPluginRegistrar scene3dRegistrar(
        String("arp_scene3d", &arp::core::defaultCpuMemoryResource()), &services);
    auto scene3dHost = makeHost(scene3dRegistrar, services);
    assert(arp_register_arp_scene3d(&scene3dHost) == 1);
    const auto scene3dDynamic = scene3dRegistrar.build();
    stampManifestMetadata(
        scene3dDynamic, "sha256:18ea186a8492da03b90660ab22e564d48c2c163d2041c7a75690d4814666681b");
    const auto scene3dStatic = loadStatic("arp_scene3d",
        &arp_register_arp_scene3d,
        services,
        "sha256:18ea186a8492da03b90660ab22e564d48c2c163d2041c7a75690d4814666681b");
    assertDynamicStaticParity(*scene3dDynamic, *scene3dStatic);
}

void static_catalog_rejects_missing_service_contract()
{
    ScriptPluginServiceRegistry services;
    arp::scriptvm::StaticPluginCatalog catalog;
    arp::scriptvm::ScriptPluginDescriptor descriptor{
        String("arp_scene", &arp::core::defaultCpuMemoryResource()),
        2u,
        1u,
        &arp_register_arp_scene,
        String("sha256:scene", &arp::core::defaultCpuMemoryResource()),
    };
    descriptor.requiredServices.push_back(
        {String(arp::scene::kMainSceneService, &arp::core::defaultCpuMemoryResource()),
            arp::scene::kMainSceneServiceVersion});
    assert(catalog.add(std::move(descriptor)));

    String diagnostics(&arp::core::defaultCpuMemoryResource());
    const auto exports = catalog.load(
        String("arp_scene", &arp::core::defaultCpuMemoryResource()), &services, diagnostics);
    assert(exports == nullptr);
    assert(diagnostics.find("requires unavailable service") != String::npos);
}

} // namespace

int main()
{
    core_exports_are_reflection_bound();
    scene_exports_are_reflection_bound();
    scene3d_exports_are_allow_listed();
    scene3d_components_are_direct_native_objects();
    dynamic_and_static_catalogs_are_identical();
    static_catalog_rejects_missing_service_contract();
    return 0;
}

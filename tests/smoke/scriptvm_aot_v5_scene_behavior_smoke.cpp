#include "scene/scene.h"
#include "scene/script_behavior_component.h"
#include "scriptvm/aot/aot_v5.h"
#include "scriptvm/aot/aot_v5_scene_behavior_dispatcher.h"

#include <memory>

extern "C" int arp_register_script_aot_package_v5(arp::scriptvm::AotRegistrationHostV5* host);

int main()
{
    using namespace arp::scriptvm;
    ScriptAotPackageRegistry registry;
    auto registration = registry.registrationHost();
    if (arp_register_script_aot_package_v5(&registration) != 1)
        return 1;

    const AotPackageDescriptorV5* package = nullptr;
    AotRegistrationHostV5 capture{kAotContractVersion,
        [](const AotPackageDescriptorV5* value, void* userData) {
            *static_cast<const AotPackageDescriptorV5**>(userData) = value;
            return value != nullptr;
        },
        &package};
    if (arp_register_script_aot_package_v5(&capture) != 1 || package == nullptr)
        return 2;

    ScriptAotModuleInstanceV5 instance;
    if (!createAotOnlyModuleInstanceV5(
            registry, package->packageIdentity, "scene_behavior", instance)
            .ok()) {
        return 3;
    }
    auto dispatcher = arp::core::makeShared<DirectNativeDispatcherV5>(std::move(instance));
    auto invoke = makeDirectNativeScriptBehaviorInvokeV5(dispatcher,
        arp::core::pmr::String("scene_behavior", &arp::core::defaultCpuMemoryResource()));
    const arp::scene::SceneUpdateContext context{0.25f, 7u};
    if (!invoke("on_update", context) || !invoke("on_start", context)
        || invoke("on_late_update", context)) {
        return 4;
    }

    arp::scene::Scene scene;
    auto& object = scene.createObject("direct-native-scripted");
    if (!scene.reparent(object.id(), scene.rootId()).ok())
        return 5;
    auto& behavior = object.addComponent<arp::scene::ScriptBehaviorComponent>(
        makeDirectNativeScriptBehaviorInvokeV5(dispatcher,
            arp::core::pmr::String("scene_behavior", &arp::core::defaultCpuMemoryResource())));
    const auto update = scene.update({0.25f, 7u, 32u});
    if (update.ok() || !behavior.failed()
        || update.diagnostic().code != arp::scene::SceneDiagnosticCode::ScriptExecutionFailed) {
        return 6;
    }

    arp::scene::Scene factoryScene(
        arp::core::pmr::String("factory-scene", &arp::core::defaultCpuMemoryResource()),
        arp::scene::SceneId::fromValue(91u));
    const auto behaviorFactory = makeDirectNativeScriptBehaviorFactoryV5(
        registry, package->packageIdentity, "scene_behavior");
    factoryScene.setComponentFactory(
        [behaviorFactory](arp::scene::GameObject&,
            const arp::core::pmr::String& type) -> arp::core::UniquePtr<arp::scene::Component> {
            if (type != "ScriptBehaviorComponent")
                return {};
            auto invoke = behaviorFactory({});
            return arp::core::makeUnique<arp::scene::ScriptBehaviorComponent>(std::move(invoke));
        });
    auto& factoryObject = factoryScene.createObject(
        arp::core::pmr::String("registry-backed", &arp::core::defaultCpuMemoryResource()));
    if (!factoryScene.reparent(factoryObject.id(), factoryScene.rootId()).ok())
        return 7;
    const auto* factoryRoot = factoryScene.root_object();
    if (factoryRoot == nullptr || !factoryObject.nativeReflectionObject().native_reference_alive())
        return 8;
    auto* factoryBehavior = factoryObject.create_component("ScriptBehaviorComponent");
    if (factoryBehavior == nullptr
        || !factoryBehavior->nativeReflectionObject().native_reference_alive())
        return 9;
    const auto factoryUpdate = factoryScene.update({0.25f, 8u, 32u});
    if (factoryUpdate.ok()
        || factoryUpdate.diagnostic().code
            != arp::scene::SceneDiagnosticCode::ScriptExecutionFailed) {
        return 10;
    }
    return dispatcher->telemetry().empty() ? 11 : 0;
}

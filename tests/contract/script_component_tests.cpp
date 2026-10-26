#include "scene/scene_asset_resource.h"
#include "scene/script_component_persistence.h"
#include "tests/fixtures/script_component_fixture.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>

using namespace arp;
using namespace arp::scriptvm;
extern "C" int arp_register_components_v5(AotRegistrationHostV5*);

namespace {

void passiveLifecycleTests(ScriptComponentModule& module)
{
    Value constructorsBefore, constructorsAfter, hooksBefore, hooksAfter;
    Value reset;
    assert(module.session().call("reset_passive_probes", {}, reset));
    assert(module.session().call("constructor_count", {}, constructorsBefore));
    assert(module.session().call("passive_hook_count", {}, hooksBefore));
    Value closedReceiver;
    {
        scene::Scene passive(
            "passive", scene::SceneId::fromValue(8), scene::SceneExecutionMode::Passive);
        assert(module.bind(passive));
        auto& object = passive.createObject("authored probe");
        assert(!module.create(object, "PassiveProbe", true));
        assert(module.diagnostic() == "ARP-SCRIPT-COMPONENT-PASSIVE-CONSTRUCTOR");
        assert(module.liveInstances() == 0);
        auto value = object.add_component("PassiveProbe");
        assert(value.is<reflection::instance>());
        auto* receiver = value.ptr<reflection::instance>();
        assert(isNil(receiver->slot("value")));
        // Authored state is explicit. Do not infer defaults by running a constructor.
        receiver->slot("value") = makeInt(42);
        assert(!passive.update({}).ok() && !passive.fixedUpdate({}).ok());
        assert(!object.send_event("blocked", {}));
        assert(!passive.dispatchEvents({}).ok());
        assert(
            !passive.dispatchPhysicsEvents({{object.id(), object.id(), "on_collision_enter"}}, {})
                .ok());
        assert(receiver->slot("value").cast<int>() == 42);

        auto& unboundOwner = passive.createObject("explicit no-constructor instance");
        auto unbound = module.create(unboundOwner, "PassiveProbe", false);
        const auto unboundReceiver = unbound->scriptInstance();
        unbound.reset();
        assert(!unboundReceiver.ptr<reflection::instance>()->alive());

        auto shared = core::makeShared<ScriptComponentModule>(module);
        scene::SceneComponentFactory factory;
        assert(registerScriptComponentPersistence(factory, shared));
        core::pmr::String yaml, diagnostic;
        const core::pmr::String id = "00000000-0000-4000-8000-000000000092";
        auto unconfigured = unboundOwner.add_component("PassiveProbe");
        core::pmr::String preserved = "previous output";
        assert(!scene::SceneDocumentResource::encode(
            passive, factory, "arp.scene-prefab", id, preserved, diagnostic));
        assert(preserved == "previous output" && !diagnostic.empty());
        assert(unboundOwner.removeComponent(unboundOwner.component_at(1)->id()));
        assert(!unconfigured.ptr<reflection::instance>()->alive());
        assert(scene::SceneDocumentResource::encode(
            passive, factory, "arp.scene-prefab", id, yaml, diagnostic));
        const auto path = std::filesystem::temp_directory_path()
            / ("arp-passive-scene-"
                + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
                + ".yaml");
        {
            std::ofstream out(path, std::ios::binary);
            out.write(yaml.data(), static_cast<std::streamsize>(yaml.size()));
            assert(out.good());
        }
        resource::ResourceManager resources;
        assert(resources
                .load<scene::ScenePrefabResource>(
                    {}, id, core::pmr::String(path.string().c_str()), 65536, 8)
                .success);
        const auto document = resources.find<scene::ScenePrefabResource>(id);
        auto restored = document.get()->instantiate(
            resources, factory, diagnostic, scene::SceneExecutionMode::Passive);
        assert(restored && restored->executionMode() == scene::SceneExecutionMode::Passive);
        auto restoredValue =
            restored->findObjectByName("authored probe")->get_component("PassiveProbe");
        assert(restoredValue.ptr<reflection::instance>()->slot("value").cast<int>() == 42);
        core::pmr::String roundtrip;
        assert(scene::SceneDocumentResource::encode(
            *restored, factory, "arp.scene-prefab", id, roundtrip, diagnostic));
        assert(yaml == roundtrip);
        restored.reset();
        assert(!restoredValue.ptr<reflection::instance>()->alive());
        assert(module.liveInstances() == 1);

        scene::SceneComponentFactory rejected;
        auto binding = *factory.persistence("arp.script-component/1");
        binding.publish = [](scene::Component&) {
            return false;
        };
        assert(rejected.addPersistence(std::move(binding)));
        const auto objectsBefore = passive.objects().size();
        assert(!document.get()
                ->instantiateInto(passive, passive.rootId(), resources, rejected, diagnostic)
                .valid());
        assert(passive.objects().size() == objectsBefore && module.liveInstances() == 1);
        assert(!document.get()->instantiate(
            resources, rejected, diagnostic, scene::SceneExecutionMode::Passive));
        assert(module.liveInstances() == 1);
        assert(resources.unload(id).success);
        assert(std::filesystem::remove(path));

        const auto componentId = object.component_at(1)->id();
        assert(object.removeComponent(componentId));
        assert(!receiver->alive() && !ScriptSemanticRuntime::loadField(value, "value").ok());
        auto& deleted = passive.createObject("deleted");
        auto deletedValue = deleted.add_component("PassiveProbe");
        assert(passive.destroyObject(deleted.id()).ok());
        assert(!deletedValue.ptr<reflection::instance>()->alive());
        {
            scene::Scene::RestorationTransaction rollback(passive);
            assert(!passive.createObject("rollback").add_component("PassiveProbe").is_undefined());
        }
        closedReceiver = passive.createObject("close").add_component("PassiveProbe");
    }
    assert(!closedReceiver.ptr<reflection::instance>()->alive() && module.liveInstances() == 0);
    assert(module.session().call("constructor_count", {}, constructorsAfter));
    assert(module.session().call("passive_hook_count", {}, hooksAfter));
    assert(constructorsBefore.cast<int>() == constructorsAfter.cast<int>());
    assert(hooksBefore.cast<int>() == hooksAfter.cast<int>());

    {
        scene::Scene runtime;
        runtime.configureEventCapacity(1);
        assert(module.bind(runtime));
        auto& object = runtime.createObject("runtime probe");
        const auto value = object.add_component("PassiveProbe");
        assert(value.ptr<reflection::instance>()->slot("value").cast<int>() == 123);
        assert(runtime.update({}).ok() && runtime.fixedUpdate({}).ok());
        assert(object.send_event("runtime event", {}));
        assert(runtime.dispatchEvents({}).ok());
        assert(runtime.dispatchPhysicsEvents({{object.id(), object.id(), "on_collision_enter"}}, {})
                .ok());
    }
    assert(module.session().call("constructor_count", {}, constructorsAfter));
    assert(module.session().call("passive_hook_count", {}, hooksAfter));
    assert(constructorsAfter.cast<int>() == constructorsBefore.cast<int>() + 1);
    assert(hooksAfter.cast<int>() == hooksBefore.cast<int>() + 9);
    assert(module.liveInstances() == 0);
}

} // namespace

int main(int argc, char** argv)
{
    tests::ScriptComponentFixture fixture;
    assert(fixture.install());
    auto& registry = fixture.scope.modules();
    auto budget = tests::ScriptComponentFixture::budget();
    ScriptComponentModule module;
    std::ifstream stream(ARP_SOURCE_DIR "/tests/fixtures/script_components.arp");
    const std::string bytes((std::istreambuf_iterator<char>(stream)), {});
    const core::pmr::String source(bytes.c_str());
    const AotPackageDescriptorV5* package = nullptr;
    if (argc > 1 && std::string(argv[1]) == "--aot") {
        AotRegistrationHostV5 host{kAotContractVersion,
            [](const AotPackageDescriptorV5* p, void* out) {
                *static_cast<const AotPackageDescriptorV5**>(out) = p;
                return true;
            },
            &package};
        assert(arp_register_components_v5(&host) == 1 && package);
    }
    if (!module.load(
            source, "components", "test-script", 1, registry, {"arp_scene"}, budget, 32, package)) {
        std::fprintf(stderr, "%s\n", module.diagnostic().c_str());
        return 1;
    }
    scene::Scene scene;
    assert(module.bind(scene));
    assert(module.bind(scene)); // Same shared execution environment is idempotent.
    ScriptComponentModule conflict;
    assert(conflict.load(
        source, "components", "test-script", 1, registry, {"arp_scene"}, budget, 32, package));
    assert(!conflict.bind(scene)); // A matching resource ID cannot rebind another session.
    passiveLifecycleTests(module);
    const auto passiveBaseline = core::defaultCpuAllocator().stats();
    // Repeat the same counter values, just as a restarted world does. Otherwise
    // the documented bounded uint16 integer cache receives new values each run.
    for (unsigned cycle = 0; cycle < 20; ++cycle)
        passiveLifecycleTests(module);
    const auto passiveFinal = core::defaultCpuAllocator().stats();
    assert(passiveFinal.liveBytes == passiveBaseline.liveBytes);
    assert(passiveFinal.allocationCount - passiveFinal.deallocationCount
        == passiveBaseline.allocationCount - passiveBaseline.deallocationCount);
    std::puts(
        "PASS Passive Scene: 20 author/restore/reject/destroy cycles; core live bytes/count delta = 0");
    scene.configureEventCapacity(2);
    auto& a = scene.createObject("first");
    auto& b = scene.createObject("second");
    auto av = a.add_component("Counter"), bv = b.add_component("Counter");
    if (av.is_undefined() || bv.is_undefined())
        std::fprintf(stderr, "%s\n", module.diagnostic().c_str());
    assert(av.is<reflection::instance>() && bv.is<reflection::instance>());
    assert(
        a.get_component("Counter").ptr<reflection::instance>() == av.ptr<reflection::instance>());
    assert(a.add_component("Counter").is_undefined());
    assert(a.add_component("MissingType").is_undefined());
    auto value = [](const Value& v) {
        return v.ptr<reflection::instance>()->slot("count").cast<int>();
    };
    assert(value(av) == 10 && value(bv) == 10);
    Value probe;
    assert(module.session().call("arity_probe", {}, probe) && probe.cast<int>() == 16);
    assert(module.session().call("marker_probe", {av}, probe) && probe.cast<int>() == 7);
    assert(scene.update({0.1f, 0, 32}).ok());
    assert(value(av) == 13 && value(bv) == 13);
    assert(scene.fixedUpdate({1.f / 60, 1, 32}).ok());
    assert(value(av) == 16 && value(bv) == 16);
    assert(a.send_event("damage", makeInt(4)));
    assert(a.send_event("damage", makeInt(5)));
    assert(!a.send_event("damage", makeInt(100)));
    assert(scene.dispatchEvents({0, 1, 32}).ok());
    assert(value(av) == 25);
    core::pmr::Vector<scene::ScriptPhysicsEvent> physicsEvents;
    physicsEvents.push_back({a.id(), b.id(), "on_collision_enter"});
    physicsEvents.push_back({b.id(), a.id(), "on_collision_enter"});
    physicsEvents.push_back({a.id(), b.id(), "on_collision_exit"});
    physicsEvents.push_back({a.id(), b.id(), "on_trigger_enter"});
    physicsEvents.push_back({b.id(), a.id(), "on_trigger_enter"});
    physicsEvents.push_back({a.id(), b.id(), "on_trigger_exit"});
    assert(scene.dispatchPhysicsEvents(physicsEvents, {0, 2, 32}).ok());
    auto field = [](const Value& v, const char* name) {
        return v.ptr<reflection::instance>()->slot(name).cast<int>();
    };
    assert(field(av, "collision_enters") == 1 && field(bv, "collision_enters") == 1);
    assert(field(av, "collision_exits") == 1 && field(bv, "collision_exits") == 0);
    assert(field(av, "trigger_enters") == 1 && field(bv, "trigger_enters") == 1);
    assert(field(av, "trigger_exits") == 1 && field(bv, "trigger_exits") == 0);
    assert(field(av, "last_other_id") == int(b.id().value()));
    assert(field(bv, "last_other_id") == int(a.id().value()));
    a.setActive(false);
    assert(scene.update({0.1f, 2, 32}).ok());
    assert(value(av) == 25 && value(bv) == 18);
    a.setActive(true);
    assert(scene.update({0.1f, 3, 32}).ok());
    assert(value(av) == 27);
    auto& removed = scene.createObject("removed");
    auto objectRef = removed.nativeReflectionObject();
    auto removal = removed.add_component("Remover");
    assert(!removal.is_undefined());
    assert(scene.update({0.1f, 4, 32}).ok());
    assert(!objectRef.native_reference_alive());
    assert(module.liveInstances() == 2);
    const auto id = a.id();
    assert(scene.destroyObject(id).ok());
    assert(module.liveInstances() == 1);
    assert(!av.ptr<reflection::instance>()->alive());
    assert(!module.session().call("marker_probe", {av}, probe));
    assert(!ScriptSemanticRuntime::loadField(av, "count").ok());
    assert(b.removeComponent(b.component_at(1)->id()));
    assert(module.liveInstances() == 0);
    {
        scene::Scene deferred;
        assert(module.bind(deferred));
        auto& doomed = deferred.createObject("destroy-on-collision");
        const auto doomedRef = doomed.nativeReflectionObject();
        assert(!doomed.add_component("DestroyOnCollision").is_undefined());
        auto& other = deferred.createObject("other");
        assert(deferred.fixedUpdate({1.f / 60, 0, 32}).ok());
        core::pmr::Vector<scene::ScriptPhysicsEvent> destroyEvents;
        destroyEvents.push_back({doomed.id(), other.id(), "on_collision_enter"});
        destroyEvents.push_back({other.id(), doomed.id(), "on_collision_enter"});
        assert(deferred.dispatchPhysicsEvents(destroyEvents, {0, 1, 32}).ok());
        assert(!doomedRef.native_reference_alive());
        assert(module.liveInstances() == 0);
    }
    auto& spawner = scene.createObject("spawner");
    assert(!spawner.add_component("Spawner").is_undefined());
    assert(scene.update({0.1f, 5, 32}).ok());
    auto spawned = spawner.get_component("Counter");
    assert(value(spawned) == 10);
    assert(scene.update({0.1f, 6, 32}).ok());
    assert(value(spawned) == 13);
    assert(scene.destroyObject(spawner.id()).ok());
    assert(module.liveInstances() == 0);
    {
        resource::ResourceManager resources;
        scene::Scene authored("typed fields");
        auto shared = core::makeShared<ScriptComponentModule>(module);
        shared->bind(authored);
        auto& target = authored.createObject("target");
        auto counter = target.add_component("Counter");
        auto& owner = authored.createObject("owner");
        auto link = owner.add_component("Link");
        link.ptr<reflection::instance>()->slot("target") = target.nativeReflectionObject();
        link.ptr<reflection::instance>()->slot("other") = counter;
        link.ptr<reflection::instance>()->slot("native") =
            target.transform().nativeReflectionObject();
        assert(target.transform().nativeReflectionObject().can_safe_cast<scene::Component*>());
        assert(!target.transform().nativeReflectionObject().can_safe_cast<scene::GameObject*>());
        scene::SceneComponentFactory codecs;
        assert(registerScriptComponentPersistence(codecs, shared));
        core::pmr::String yaml, diagnostic;
        const core::pmr::String id = "00000000-0000-4000-8000-000000000091";
        const bool encoded = scene::SceneDocumentResource::encode(
            authored, codecs, "arp.scene-prefab", id, yaml, diagnostic);
        if (!encoded)
            std::fprintf(stderr, "%s\n", diagnostic.c_str());
        assert(encoded);
        const auto folder = std::filesystem::current_path() / "script-component-test-output";
        std::filesystem::create_directories(folder);
        const auto path = folder / (package ? "native.yaml" : "interpreter.yaml");
        auto write = [&](const core::pmr::String& text) {
            std::ofstream out(path, std::ios::binary);
            out.write(text.data(), text.size());
        };
        write(yaml);
        const auto load = [&]() {
            return resources
                .load<scene::ScenePrefabResource>(
                    {}, id, core::pmr::String(path.string().c_str()), 65536, 8)
                .success;
        };
        assert(load());
        auto document = resources.find<scene::ScenePrefabResource>(id);
        Value countBefore, countAfter;
        assert(module.session().call("constructor_count", {}, countBefore));
        auto restored = document.get()->instantiate(resources, codecs, diagnostic);
        assert(module.session().call("constructor_count", {}, countAfter));
        assert(countBefore.cast<int>() == countAfter.cast<int>());
        if (!restored)
            std::fprintf(stderr, "%s\n", diagnostic.c_str());
        assert(restored);
        auto restoredLink =
            restored->findObjectByName("owner")->get_component("Link").ptr<reflection::instance>();
        assert(restoredLink->slot("target").cast<scene::GameObject*>()
            == restored->findObjectByName("target"));
        assert(restoredLink->slot("other").ptr<reflection::instance>()
            == restored->findObjectByName("target")
                ->get_component("Counter")
                .ptr<reflection::instance>());
        assert(restoredLink->slot("native").cast<scene::Component*>()
            == &restored->findObjectByName("target")->transform());
        assert(isNil(restoredLink->slot("optional")));
        core::pmr::String roundtrip;
        assert(scene::SceneDocumentResource::encode(
            *restored, codecs, "arp.scene-prefab", id, roundtrip, diagnostic));
        assert(yaml == roundtrip);
        restored.reset();
        assert(module.liveInstances() == 2);
        {
            scene::Scene untouched;
            scene::SceneComponentFactory failing;
            auto binding = *codecs.persistence("arp.script-component/1");
            int publications = 0;
            binding.publish = [shared, &publications](scene::Component& component) {
                return ++publications == 1 && shared->bind(*component.gameObject()->scene());
            };
            assert(failing.addPersistence(std::move(binding)));
            assert(!document.get()
                    ->instantiateInto(untouched, untouched.rootId(), resources, failing, diagnostic)
                    .valid());
            assert(publications == 2 && untouched.objects().empty() && module.liveInstances() == 2);
            auto& probe = untouched.createObject("rollback-probe");
            assert(probe.id().value() == 2 && probe.transform().id().value() == 2);
            assert(probe.add_component("Counter").is_undefined());
            assert(module.session().call("constructor_count", {}, countAfter));
            assert(countBefore.cast<int>() == countAfter.cast<int>());
        }
        {
            scene::SceneComponentFactory cyclic;
            auto binding = *codecs.persistence("arp.script-component/1");
            binding.after.push_back("arp.script-component/1");
            assert(cyclic.addPersistence(std::move(binding)));
            assert(!document.get()->instantiate(resources, cyclic, diagnostic));
            assert(diagnostic == "ARP-SCENE-INITIALIZATION-DEPENDENCY-CYCLE");
            assert(module.liveInstances() == 2);
        }
        assert(resources.unload(id).success);
        auto malformed = core::YamlContract::Node::deserialize(yaml);
        malformed["objects"][1]["components"][0]["fields"]["other"]["object"] = 999;
        auto text = core::YamlContract::Node::serialize(malformed);
        write(core::pmr::String(text.c_str()));
        assert(load());
        document = resources.find<scene::ScenePrefabResource>(id);
        const auto before = authored.objects().size();
        assert(!document.get()
                ->instantiateInto(authored, authored.rootId(), resources, codecs, diagnostic)
                .valid());
        assert(authored.objects().size() == before && module.liveInstances() == 2);
        assert(resources.unload(id).success);
        malformed["objects"][1]["components"][0]["configuration"]["moduleSchemaVersion"] = 2;
        text = core::YamlContract::Node::serialize(malformed);
        write(core::pmr::String(text.c_str()));
        assert(load());
        assert(!resources.find<scene::ScenePrefabResource>(id).get()->instantiate(
            resources, codecs, diagnostic));
        assert(module.liveInstances() == 2);
    }
    assert(module.liveInstances() == 0);
    {
        scene::Scene failed;
        assert(module.bind(failed));
        assert(!failed.createObject("failure").add_component("Faulty").is_undefined());
        const auto result = failed.update({.1f, 0, 32});
        assert(!result.ok());
        assert(
            result.diagnostic().message.find("fixture script failure") != core::pmr::String::npos);
    }
    assert(module.liveInstances() == 0);
    {
        scene::Scene nested;
        assert(module.bind(nested));
        auto value = nested.createObject("constructor-reentry").add_component("Nested");
        assert(value.is<reflection::instance>());
        assert(!value.ptr<reflection::instance>()->slot("nested_accepted").cast<bool>());
        assert(module.liveInstances() == 1);
    }
    assert(module.liveInstances() == 0);
    if (argc > 1)
        for (int i = 0; i < 120; ++i)
            assert(scene.fixedUpdate({1.f / 60, std::uint64_t(i), 32}).ok());
    std::puts("PASS shared ScriptComponent instances / real GameObject / fixed / deferred destroy");
}

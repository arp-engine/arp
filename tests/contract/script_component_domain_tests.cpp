#include "2d/script_value_memory.h"
#include "core/checked_integer_cast.h"
#include "tests/fixtures/script_component_fixture.h"

#include <cassert>
#include <cstdio>
#include <fstream>
using namespace arp;
using namespace scriptvm;
extern "C" int arp_register_components_v5(AotRegistrationHostV5*);

int main(int argc, char**)
{
    int id = 0;
    std::uint64_t wide = 0;
    assert(core::checkedIntegerCast(std::uint64_t(123), id) && id == 123);
    assert(!core::checkedIntegerCast(UINT64_MAX, id) && id == 123);
    assert(!core::checkedIntegerCast(-1, wide));
    assert(tryGetInt(Value{std::uint64_t(123)}, id) && id == 123);
    assert(!tryGetInt(Value{UINT64_MAX}, id));
    tests::ScriptComponentFixture fixture;
    assert(fixture.installDomains());
    auto factory = core::makeShared<scene::SceneComponentFactory>();
    auto world = fixture.world;
    assert(factory->add("RigidBody2DComponent", [world] {
        return core::makeUnique<physics2d::RigidBody2DComponent>(world);
    }));
    assert(factory->add("TimerComponent", [] {
        return core::makeUnique<scene::TimerComponent>(8);
    }));
    assert(factory->add("TweenComponent", [] {
        return core::makeUnique<scene::TweenComponent>();
    }));
    fixture.mainScene.setComponentFactory([factory](auto&, const auto& name) {
        return factory->create(name);
    });
    fixture.mainScene.configureEventCapacity(8);
    fixture.mainScene.configureObjectCapacity(2);
    std::ifstream input(ARP_SOURCE_DIR "/tests/fixtures/script_component_domains.arp");
    std::string bytes((std::istreambuf_iterator<char>(input)), {});
    const AotPackageDescriptorV5* package = nullptr;
    if (argc > 1) {
        AotRegistrationHostV5 host{kAotContractVersion,
            [](const AotPackageDescriptorV5* p, void* out) {
                *static_cast<const AotPackageDescriptorV5**>(out) = p;
                return true;
            },
            &package};
        assert(arp_register_components_v5(&host) == 1);
    }
    ScriptComponentModule module;
    if (!module.load(core::pmr::String(bytes.c_str()),
            "components",
            "test-domains",
            1,
            fixture.scope.modules(),
            {"arp_scene", "arp_scene2d"},
            fixture.budget(),
            8,
            package)) {
        std::fprintf(stderr, "%s\n", module.diagnostic().c_str());
        return 1;
    }
    module.session().setValueMemoryDescriptor(&scene2d::describeScriptValueMemory, nullptr);
    assert(module.bind(fixture.mainScene));
    auto& parent = fixture.mainScene.createObject("parent");
    parent.transform().setLocalTranslation({2, 1, 0});
    auto& object = fixture.mainScene.createObject("mover");
    assert(object.set_parent_object(&parent));
    assert(object.world_transform().translation.x == 2
        && object.transform().world_transform().translation.y == 1);
    auto value = object.add_component("Mover");
    if (value.is_undefined()) {
        std::fprintf(stderr, "%s\n", module.diagnostic().c_str());
        return 1;
    }
    auto* instance = value.ptr<reflection::instance>();
    auto* body = object.getComponent<physics2d::RigidBody2DComponent>();
    assert(body);
    assert(body->body_state().position.x == 3 && body->body_state().position.y == 2);
    const auto* collider = object.getComponent<physics2d::Collider2DComponent>();
    assert(collider);
    assert(collider->desc().filter.category == 4 && collider->desc().filter.mask == 16);
    assert(fixture.mainScene.update({0, 0, 4, 32, .2f}).ok());
    for (int i = 0; i < 6; ++i) {
        body->beforeStep();
        auto result = fixture.mainScene.fixedUpdate({1.f / 60, std::uint64_t(i), 4, 32});
        if (!result.ok())
            std::fprintf(stderr, "%s\n", result.diagnostic().message.c_str());
        assert(result.ok());
        assert(world->step());
        assert(body->afterStep());
        assert(body->synchronize(fixture.mainScene, 1));
        assert(fixture.mainScene.dispatchEvents({0, std::uint64_t(i), 4, 32}).ok());
    }
    assert(instance->slot("events").cast<int>() == 2);
    assert(instance->slot("hits").cast<int>() == 1);
    assert(body->body_state().position.x > 3.09f && body->body_state().position.y == 2);
    assert(object.getComponent<scene::TweenComponent>()->completed());
    auto ref = body->nativeReflectionObject();
    assert(fixture.mainScene.destroyObject(object.id()).ok());
    assert(world->telemetry().bodies == 0 && world->telemetry().colliders == 0);
    assert(!ref.native_reference_alive());
    assert(!instance->alive());
    std::puts(
        "PASS actual native components: script attachment, physics query, fixed timer, tween, destruction");
}

#include "tests/fixtures/script_component_fixture.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
using namespace arp;

bool exported(scriptvm::ModuleRegistry& modules, const char* type, const char* member, int arity)
{
    const auto* info = modules.findType("arp_scene2d", type);
    const auto name = core::pmr::String(type) + "." + member;
    const auto* contract = modules.findReflectionCallableContract("arp_scene2d", name);
    if (!info || !contract || !contract->valid()) {
        std::fprintf(stderr, "SCENE2D-GAMEPLAY-EXPORT-MISSING: %s\n", name.c_str());
        return false;
    }
    const auto field = info->get_field(member);
    if (!field.is<reflection::function>())
        return false;
    const auto& method = field.cast<const reflection::function&>();
    assert(!contract->maySuspend && contract->fixedArity == arity);
    assert(contract->functionIdentity == method.identity());
    assert(method.parameter_count() == std::size_t(arity));
    return true;
}

bool cycle()
{
    tests::ScriptComponentFixture fixture;
    assert(fixture.installDomains());
    auto& modules = fixture.scope.modules();
    if (!exported(modules, "Joint2DComponent", "initialize_joint", 3)
        || !exported(modules, "TweenComponent", "value", 0))
        return false;

    auto& scene = fixture.mainScene;
    auto& anchorObject = scene.createObject("anchor");
    auto& movingObject = scene.createObject("moving");
    auto& jointObject = scene.createObject("joint");
    auto& tweenObject = scene.createObject("tween");
    auto& authoringObject = scene.createObject("authoring-joint");
    auto& anchor = anchorObject.addComponent<physics2d::RigidBody2DComponent>(fixture.world);
    physics2d::BodyDesc dynamic;
    dynamic.type = physics2d::BodyType::Dynamic;
    auto& moving =
        movingObject.addComponent<physics2d::RigidBody2DComponent>(fixture.world, dynamic);
    scene::Transform transform;
    assert(anchor.initialize(transform));
    transform.translation.x = 1;
    assert(moving.initialize(transform));
    auto& joint = jointObject.addComponent<physics2d::Joint2DComponent>(fixture.world);
    auto& authoring = authoringObject.addComponent<physics2d::Joint2DComponent>(
        fixture.world, physics2d::ComponentUsage::Authoring);
    const auto* jointType = modules.findType("arp_scene2d", "Joint2DComponent");
    const auto method = jointType->get_field("initialize_joint").cast<reflection::function>();
    physics2d::JointDesc description;
    description.length = 1;
    core::pmr::Vector<reflection::object> arguments{anchor.nativeReflectionObject(),
        moving.nativeReflectionObject(),
        reflection::object(description)};
    assert(method.call_v(joint.nativeReflectionObject(), arguments).cast<bool>());
    assert(fixture.world->telemetry().joints == 1);
    // Repeated initialization and authoring-mode simulation remain rejected.
    assert(!method.call_v(joint.nativeReflectionObject(), arguments).cast<bool>());
    bool rejected = false;
    try {
        (void)method.call_v(authoring.nativeReflectionObject(), arguments);
    } catch (const std::exception& error) {
        rejected = std::strstr(error.what(), "ARP-PHYSICS2D-AUTHORING-NOT-SIMULATED") != nullptr;
    }
    assert(rejected && fixture.world->telemetry().joints == 1);

    auto& tween = tweenObject.addComponent<scene::TweenComponent>();
    assert(tween.start_tween(1, 10, 20, false));
    const auto* tweenType = modules.findType("arp_scene2d", "TweenComponent");
    const auto value = tweenType->get_field("value").cast<reflection::function>();
    core::pmr::Vector<reflection::object> empty;
    tween.onFixedUpdate({.25f, 0});
    assert(std::abs(value.call_v(tween.nativeReflectionObject(), empty).cast<float>() - 12.5f)
        < .001f);
    tween.onFixedUpdate({.75f, 1});
    assert(value.call_v(tween.nativeReflectionObject(), empty).cast<float>() == 20);
    const auto oldTween = tween.nativeReflectionObject();
    assert(scene.destroyObject(authoringObject.id()).ok());
    assert(scene.destroyObject(jointObject.id()).ok());
    assert(scene.destroyObject(anchorObject.id()).ok());
    assert(scene.destroyObject(movingObject.id()).ok());
    assert(scene.destroyObject(tweenObject.id()).ok());
    assert(!oldTween.native_reference_alive());
    assert(!fixture.world->telemetry().joints && !fixture.world->telemetry().bodies);
    return true;
}
} // namespace

int main(int argc, char**)
{
    if (!cycle())
        return 1;
    const auto before = core::defaultCpuAllocator().stats();
    for (int i = 0; i < (argc > 1 ? 20 : 1); ++i)
        if (!cycle())
            return 1;
    const auto after = core::defaultCpuAllocator().stats();
    assert(before.liveBytes == after.liveBytes);
    assert(before.allocationCount - before.deallocationCount
        == after.allocationCount - after.deallocationCount);
    std::printf(
        "PASS actual arp_scene2d synchronous exports, Box2D joint and Tween calls; core=%llu -> %llu\n",
        static_cast<unsigned long long>(before.liveBytes),
        static_cast<unsigned long long>(after.liveBytes));
}

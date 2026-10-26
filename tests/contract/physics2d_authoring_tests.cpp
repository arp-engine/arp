#include "2d/physics_components.h"
#include "tests/fixtures/tile_map_fixture.h"

#include <cassert>
#include <cstdio>
#include <limits>
#include <string_view>

namespace {
using namespace arp;
using namespace physics2d;

// Keep a live native body while authoring. The monotonic hook count also
// detects a transient create/destroy concealed by equal final live counts.
void authoringCycle()
{
    auto world = core::makeShared<PhysicsWorld2D>(
        WorldConfig{{0, -10}, 32, 64, 16, 128, 256, 128, 32, 4, 1.f / 60.f});
    const auto liveBody = world->createBody({BodyType::Dynamic, {3, 4}});
    assert(liveBody);
    const auto before = world->telemetry();
    scene::Scene scene(
        "authoring", scene::SceneId::fromValue(1), scene::SceneExecutionMode::Passive);
    auto& a = scene.createObject("a");
    auto& b = scene.createObject("b");
    auto& bodyA = a.addComponent<RigidBody2DComponent>(world, ComponentUsage::Authoring);
    auto& bodyB = b.addComponent<RigidBody2DComponent>(world, ComponentUsage::Authoring);
    assert(bodyA.initializeForOwner(a.world_transform(), a.id()));
    assert(bodyB.initializeForOwner(b.world_transform(), b.id()));
    auto props = bodyA.editorAuthoringProperties().cast<RigidBody2DEditorProperties>();
    props.linearDamping = .75f;
    assert(bodyA.applyEditorAuthoringProperties(reflection::object(props)));
    props.linearDamping = -1;
    assert(!bodyA.applyEditorAuthoringProperties(reflection::object(props))
        && bodyA.desc().linearDamping == .75f);
    props.linearDamping = 0;
    props.type = static_cast<BodyType>(99);
    assert(!bodyA.applyEditorAuthoringProperties(reflection::object(props)));
    bool refused = false;
    try {
        (void)bodyA.body_state();
    } catch (const std::logic_error& e) {
        refused = std::string_view(e.what()) == "ARP-PHYSICS2D-AUTHORING-NOT-SIMULATED";
    }
    assert(refused && !bodyA.handle());
    ShapeDesc circle;
    circle.radius = .5f;
    auto& collider =
        a.addComponent<Collider2DComponent>(world, BodyHandle{}, circle, ComponentUsage::Authoring);
    auto shape = collider.editorAuthoringProperties().cast<Collider2DEditorProperties>();
    shape.radius = .75f;
    assert(
        collider.applyEditorAuthoringProperties(reflection::object(shape)) && !collider.handle());
    shape.radius = -1;
    assert(!collider.applyEditorAuthoringProperties(reflection::object(shape))
        && collider.desc().radius == .75f);
    shape.radius = .75f;
    shape.type = static_cast<ShapeType>(99);
    assert(!collider.applyEditorAuthoringProperties(reflection::object(shape)));
    auto& joint = a.addComponent<Joint2DComponent>(world, ComponentUsage::Authoring);
    Joint2DEditorProperties links;
    links.bodyA = a.id().value();
    links.bodyB = b.id().value();
    for (auto type : {JointType::Distance, JointType::Revolute, JointType::Prismatic}) {
        links.type = type;
        assert(joint.applyEditorAuthoringProperties(reflection::object(links)) && !joint.handle());
    }
    links.bodyB = links.bodyA;
    assert(!joint.applyEditorAuthoringProperties(reflection::object(links))
        && joint.bodyBObjectId() == b.id().value());
    links.bodyB = b.id().value();
    links.type = static_cast<JointType>(99);
    assert(!joint.applyEditorAuthoringProperties(reflection::object(links)));
    BodyDesc invalidBody;
    invalidBody.position.x = std::numeric_limits<float>::quiet_NaN();
    assert(validateBodyDesc(invalidBody) && !world->createBody(invalidBody));
    JointDesc invalidJoint;
    invalidJoint.type = static_cast<JointType>(99);
    assert(validateJointDesc(invalidJoint));
    ShapeDesc chain;
    chain.type = ShapeType::StaticChain;
    chain.points = {{0, 0}, {1, 0}, {2, 0}, {3, 0}};
    assert(!validateShapeDesc(chain, 4) && validateShapeDesc(chain, 3));
    chain.sensor = true;
    assert(validateShapeDesc(chain, 4));
    const auto previous = bodyA.desc();
    auto transform = a.local_transform();
    transform.scale.x = 2;
    a.set_local_transform(transform);
    props.type = BodyType::Static;
    assert(!bodyA.applyEditorAuthoringProperties(reflection::object(props))
        && bodyA.desc().linearDamping == previous.linearDamping);
    bool invalidUsage = false;
    try {
        RigidBody2DComponent rejected(world, static_cast<ComponentUsage>(99));
    } catch (const std::invalid_argument&) {
        invalidUsage = true;
    }
    assert(invalidUsage);
    const auto after = world->telemetry();
    assert(before.bodies == after.bodies && before.colliders == after.colliders
        && before.joints == after.joints
        && before.thirdPartyAllocations == after.thirdPartyAllocations
        && before.ticks == after.ticks && world->events().empty());
    BodyState state;
    assert(world->state(liveBody, state) && state.position.x == 3 && state.position.y == 4);
}

void tileProjection()
{
    resource::ResourceManager resources;
    assert(tests::loadSpriteFixture(resources) && tests::loadTileFixture(resources));
    PhysicsWorld2D world({{0, -10}, 32, 64, 16, 128, 256, 128, 32, 4, 1.f / 60.f});
    const auto map = resources.find<scene2d::TileMapResource>(tests::tileMapId);
    scene2d::TileMapRuntime preview(resources, map, {4, 128, 1000});
    const auto before = world.telemetry();
    assert(preview.usage() == ComponentUsage::Authoring);
    assert(preview.updateResidency({-100, -100, 200, 200}) && preview.extract(.2)
        && !preview.items().empty());
    assert(world.telemetry().thirdPartyAllocations == before.thirdPartyAllocations
        && world.telemetry().bodies == 0);
    scene2d::TileMapRuntime simulation(resources, map, world, {4, 128, 1000});
    assert(simulation.updateResidency({-100, -100, 200, 200}) && simulation.extract(.2));
    assert(preview.items().size() == simulation.items().size() && world.telemetry().colliders > 0);
    for (std::size_t i = 0; i < preview.items().size(); ++i) {
        const auto& a = preview.items()[i];
        const auto& b = simulation.items()[i];
        assert(a.sprite.id() == b.sprite.id() && a.sprite.generation() == b.sprite.generation()
            && a.world.translation.x == b.world.translation.x
            && a.world.translation.y == b.world.translation.y
            && a.world.rotation.z == b.world.rotation.z && a.world.scale.x == b.world.scale.x
            && a.world.scale.y == b.world.scale.y && a.flipX == b.flipX && a.flipY == b.flipY
            && a.sorting.objectId == b.sorting.objectId);
    }
    const auto allocated = world.telemetry().thirdPartyAllocations;
    assert(preview.updateResidency({1000, 1000, 2, 2}) && preview.residentCount() == 0
        && preview.extract(0) && preview.items().empty());
    assert(world.telemetry().thirdPartyAllocations == allocated);
}
} // namespace

int main(int argc, char**)
{
    authoringCycle();
    tileProjection();
    const auto before = arp::core::defaultCpuAllocator().stats();
    for (int n = 0; n < (argc > 1 ? 20 : 1); ++n) {
        authoringCycle();
        tileProjection();
    }
    const auto after = arp::core::defaultCpuAllocator().stats();
    assert(before.liveBytes == after.liveBytes
        && before.allocationCount - before.deallocationCount
            == after.allocationCount - after.deallocationCount);
    std::puts(
        "PASS authoring descriptors, explicit simulation refusal, shared world isolation, real tile projection; core live bytes/count delta=0");
}

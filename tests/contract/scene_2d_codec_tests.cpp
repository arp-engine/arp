#include "2d/scene_component_persistence.h"
#include "2d/sprite_animation_component.h"
#include "2d/tile_map_resource_component.h"
#include "core/algorithm/dependency_order.h"
#include "core/io/file_io.h"
#include "scene/scene_asset_resource.h"
#include "tests/fixtures/tile_map_fixture.h"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>

int main()
{
    using namespace arp;
    using namespace physics2d;
    resource::ResourceManager resources;
    assert(tests::loadSpriteFixture(resources) && tests::loadTileFixture(resources));
    WorldConfig config{{0, 0}, 32, 64, 16, 128, 256, 128, 32, 4, 1.f / 60.f};
    auto world = core::makeShared<PhysicsWorld2D>(config);
    scene::SceneComponentFactory codecs;
    assert(scene2d::registerScene2DPersistence(codecs, resources, world));
    const core::pmr::String animationId = "00000000-0000-4000-8000-000000000071";
    core::pmr::Vector<scene2d::SpriteAnimationFrame> frames;
    frames.push_back({tests::spriteA, .1, 7});
    frames.push_back({tests::spriteB, .1, 8});
    assert(
        resources.load<scene2d::SpriteAnimationResource>({}, animationId, std::move(frames), false)
            .success);
    core::DependencyOrder::Dependencies edges;
    edges.resize(2);
    edges[0].push_back(1);
    core::pmr::Vector<std::size_t> order;
    assert(core::DependencyOrder::resolve(edges, order) && order[0] == 1);
    edges[1].push_back(0);
    assert(!core::DependencyOrder::resolve(edges, order));
    scene::Scene authored("complete components");
    // Joint owner precedes both body objects: verifies phased forward references.
    auto& joint = authored.createObject("joint");
    auto& tile = authored.createObject("tiles");
    auto& dynamic = authored.createObject("dynamic");
    auto& anchor = authored.createObject("anchor");
    dynamic.transform().setLocalTranslation({1, 0, 0});
    BodyDesc desc;
    desc.type = BodyType::Dynamic;
    auto& body = dynamic.addComponent<RigidBody2DComponent>(world, desc);
    assert(body.initialize(dynamic.world_transform()));
    auto& fixed = anchor.addComponent<RigidBody2DComponent>(world, BodyDesc{});
    assert(fixed.initialize(anchor.world_transform()));
    ShapeDesc shape;
    shape.type = ShapeType::Box;
    shape.halfSize = {.2f, .3f};
    shape.filter.mask = UINT64_MAX;
    assert(dynamic.addComponent<Collider2DComponent>(world, body.handle(), shape).handle());
    JointDesc connection;
    connection.bodyA = fixed.handle();
    connection.bodyB = body.handle();
    connection.length = 1;
    assert(joint.addComponent<Joint2DComponent>(world, connection).handle());
    dynamic.addComponent<scene2d::SpriteAnimationComponent>(
        resources.find<scene2d::SpriteAnimationResource>(animationId), .5, true, 8);
    anchor.addComponent<scene2d::SpriteAnimationComponent>(resources, 8);
    tile.addComponent<scene2d::TileMapResourceComponent>(resources,
        resources.find<scene2d::TileMapResource>(tests::tileMapId),
        world,
        scene2d::TileMapRuntimeDesc{2, 12, 1000});
    const core::pmr::String id = "00000000-0000-4000-8000-000000000072";
    // Saving authoring state must not capture live simulation velocity.
    assert(body.set_velocity({7, 8}, 2));
    core::pmr::String yaml, diagnostic;
    assert(scene::SceneDocumentResource::encode(
        authored, codecs, "arp.scene-asset", id, yaml, diagnostic));
    const auto folder = std::filesystem::current_path() / "scene2d-codec-test-output";
    std::filesystem::create_directories(folder);
    const auto file = folder / "scene.yaml";
    auto write = [&](const core::pmr::String& text) {
        std::ofstream stream(file, std::ios::binary);
        stream.write(text.data(), text.size());
    };
    write(yaml);
    assert(resources
            .load<scene::SceneAssetResource>(
                {}, id, core::pmr::String(file.string().c_str()), 65536, 8)
            .success);
    auto document = resources.find<scene::SceneAssetResource>(id);
    auto restored = document.get()->instantiate(resources, codecs, diagnostic);
    if (!restored)
        std::puts(diagnostic.c_str());
    assert(restored);
    const auto& components = restored->findObjectByName("dynamic")->components();
    assert(components.size() == 4);
    assert(dynamic_cast<const RigidBody2DComponent*>(components[1].get()));
    assert(dynamic_cast<const Collider2DComponent*>(components[2].get()));
    assert(dynamic_cast<const scene2d::SpriteAnimationComponent*>(components[3].get()));
    const auto* stopped =
        restored->findObjectByName("anchor")->getComponent<scene2d::SpriteAnimationComponent>();
    assert(stopped && stopped->animationResourceId().empty() && !stopped->animationReady());
    assert(restored->findObjectByName("dynamic")
               ->getComponent<RigidBody2DComponent>()
               ->desc()
               .velocity.x
        == 0);
    assert(restored->findObjectByName("dynamic")
               ->getComponent<Collider2DComponent>()
               ->desc()
               .filter.mask
        == UINT64_MAX);
    assert(world->telemetry().bodies == 4 && world->telemetry().joints == 2);
    auto* animation =
        restored->findObjectByName("dynamic")->getComponent<scene2d::SpriteAnimationComponent>();
    assert(animation && animation->paused() && animation->speed() == .5 && animation->advance(1)
        && animation->animator().frame() == 0);
    auto* map =
        restored->findObjectByName("tiles")->getComponent<scene2d::TileMapResourceComponent>();
    assert(map && map->runtime() && map->runtime()->updateResidency({-2, -1, 4, 2})
        && map->runtime()->extract(0));
    core::pmr::String roundtrip;
    assert(scene::SceneDocumentResource::encode(
        *restored, codecs, "arp.scene-asset", id, roundtrip, diagnostic));
    if (yaml != roundtrip) {
        std::ofstream dump(folder / "roundtrip.yaml");
        dump.write(roundtrip.data(), roundtrip.size());
    }
    assert(yaml == roundtrip);
    restored.reset();
    assert(world->telemetry().bodies == 2 && world->telemetry().colliders == 1
        && world->telemetry().joints == 1);
    assert(resources.unload(id).success);
    auto malformed = core::YamlContract::Node::deserialize(yaml);
    malformed["objects"][0]["components"][0]["fields"]["bodyB"] = 999;
    const auto encoded = core::YamlContract::Node::serialize(malformed);
    write(core::pmr::String(encoded.data(), encoded.size()));
    assert(resources
            .load<scene::SceneAssetResource>(
                {}, id, core::pmr::String(file.string().c_str()), 65536, 8)
            .success);
    assert(!resources.find<scene::SceneAssetResource>(id).get()->instantiate(
        resources, codecs, diagnostic));
    assert(world->telemetry().bodies == 2 && world->telemetry().colliders == 1
        && world->telemetry().joints == 1);
    // Fail after native bodies/colliders have been prepared, then verify teardown.
    assert(resources.unload(id).success);
    malformed = core::YamlContract::Node::deserialize(yaml);
    malformed["objects"][0]["components"][0]["fields"]["length"] = -1.0;
    write(core::YamlContract::serialize(malformed));
    assert(resources
            .load<scene::SceneAssetResource>(
                {}, id, core::pmr::String(file.string().c_str()), 65536, 8)
            .success);
    assert(!resources.find<scene::SceneAssetResource>(id).get()->instantiate(
        resources, codecs, diagnostic));
    assert(world->telemetry().bodies == 2 && world->telemetry().colliders == 1
        && world->telemetry().joints == 1);
    // Object-level typed references cannot choose between two matching bodies.
    assert(resources.unload(id).success);
    malformed = core::YamlContract::Node::deserialize(yaml);
    auto& anchorComponents = malformed["objects"][3]["components"];
    const auto duplicate = anchorComponents[0];
    anchorComponents.get_value_ref<core::YamlContract::Node::sequence_type&>().push_back(duplicate);
    write(core::YamlContract::serialize(malformed));
    assert(resources
            .load<scene::SceneAssetResource>(
                {}, id, core::pmr::String(file.string().c_str()), 65536, 8)
            .success);
    assert(!resources.find<scene::SceneAssetResource>(id).get()->instantiate(
        resources, codecs, diagnostic));
    assert(diagnostic.find("REFERENCE-DANGLING-OR-TYPE") != core::pmr::String::npos);
    assert(world->telemetry().bodies == 2 && world->telemetry().colliders == 1
        && world->telemetry().joints == 1);
    std::puts(
        "PASS: physics/animation/tile scene roundtrip, forward joints, full-width filters, rollback lifecycle");
}

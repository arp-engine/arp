#include "2d/scene_sprite_codec.h"
#include "core/file_io.h"
#include "core/io/versioned_yaml_store.h"
#include "tests/fixtures/tile_map_fixture.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

namespace {
struct RecordSchema {
    static const char* type() { return "arp.test-record"; }

    static std::uint32_t version() { return 1; }

    static bool validate(const arp::core::YamlContract::Node& n, arp::core::pmr::String&)
    {
        return arp::core::YamlContract::uint32(n, "score") <= 10000;
    }
};

arp::core::io::AtomicFileResult await(
    arp::core::io::AtomicFileService& files, arp::core::PoolHandle handle)
{
    assert(handle);
    arp::core::io::AtomicFileResult result;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!files.take(handle, result)) {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::yield();
    }
    return result;
}
} // namespace

int main()
{
    using namespace arp;
    resource::ResourceManager resources;
    assert(tests::loadSpriteFixture(resources) && tests::loadTileFixture(resources));
    physics2d::WorldConfig config{{0, 0}, 8, 32, 4, 64, 64, 32, 16, 4, 1.f / 60.f};
    physics2d::PhysicsWorld2D world(config);
    const auto map = resources.find<scene2d::TileMapResource>(tests::tileMapId);
    {
        scene2d::TileMapRuntime runtime(resources, map, world, {2, 12, 100});
        assert(runtime.updateResidency({-2, -1, 4, 2}) && runtime.residentCount() == 2);
        assert(runtime.extract(0) && runtime.items().size() == 12);
        assert(world.telemetry().colliders == 4);
        scene2d::SpriteDrawPlan plan(12);
        assert(plan.build(resources, runtime.items()) && plan.instances().size() == 12);
        assert(runtime.extract(.125));
        assert(runtime.items()[0].sprite.id() == tests::spriteB);
        const auto warm = core::defaultCpuAllocator().stats().allocationCount;
        for (int frame = 0; frame < 100; ++frame)
            assert(runtime.extract(frame / 60.0));
        assert(core::defaultCpuAllocator().stats().allocationCount == warm);
        assert(runtime.updateResidency({-2, -1, 2, 2}) && runtime.residentCount() == 1
            && world.telemetry().colliders == 2);
        assert(runtime.updateResidency({0, -1, 2, 2}) && runtime.residentCount() == 1
            && world.telemetry().colliders == 2);
        assert(world.raycast({-1.5f, 1}, {0, -3}, {}) && world.queryResults().empty());
        assert(world.raycast({.5f, 1}, {0, -3}, {}) && !world.queryResults().empty());
        assert(runtime.updateResidency({20, 20, 2, 2}) && runtime.residentCount() == 0
            && world.telemetry().colliders == 0);
    }
    assert(world.telemetry().bodies == 0);
    {
        scene2d::TileMapRuntime bounded(resources, map, world, {1, 6, 1});
        assert(bounded.updateResidency({-2, -1, 2, 2}));
        assert(!bounded.updateResidency({-2, -1, 4, 2}));
        assert(bounded.residentCount() == 1 && world.telemetry().colliders == 2);
    }
    {
        physics2d::WorldConfig limitedConfig = config;
        limitedConfig.maxColliders = 1;
        physics2d::PhysicsWorld2D limited(limitedConfig);
        scene2d::TileMapRuntime bounded(resources, map, limited, {2, 12, 1});
        assert(!bounded.updateResidency({-2, -1, 4, 2}));
        assert(bounded.residentCount() == 0);
        assert(limited.telemetry().colliders == 0 && limited.telemetry().bodies == 0);
    }
    const auto folder = std::filesystem::current_path() / "scene2d-world-test-output";
    std::filesystem::create_directories(folder);
    const auto scenePath = folder / "scene.yaml";
    const auto savePath = folder / "save.yaml";
    std::filesystem::remove(scenePath);
    std::filesystem::remove(savePath);
    core::io::AtomicFileService files(2, 65536);
    scene::SceneCodecRegistry codecs;
    assert(codecs.add(core::makeUnique<scene2d::SceneSpriteCodec>()));
    assert(codecs.add(core::makeUnique<scene::ObjectLinksCodec>()));
    scene::Scene authored("round trip");
    auto& parent = authored.createObject("parent");
    auto& child = authored.createObject("child");
    assert(authored.reparent(child.id(), parent.id()).ok());
    child.transform().setLocalTranslation({2, 3, 0});
    scene2d::SpriteRendererComponentDesc sprite;
    sprite.spriteResourceId = tests::spriteA;
    sprite.flipX = true;
    child.addComponent<scene2d::SpriteRendererComponent>(sprite);
    parent.addComponent<scene::ObjectLinksComponent>().links.push_back({"child", child.id()});
    const core::pmr::String sceneId = "00000000-0000-4000-8000-000000000030";
    core::pmr::String yaml, diagnostic;
    assert(scene::SceneDocumentResource::encode(
        authored, codecs, "arp.scene-asset", sceneId, yaml, diagnostic));
    const core::pmr::String sceneFile(scenePath.string().c_str());
    assert(await(
        files, files.replace(sceneFile, core::pmr::Vector<std::uint8_t>(yaml.begin(), yaml.end())))
            .success);
    auto loaded = resources.load<scene::SceneAssetResource>({}, sceneId, sceneFile, 65536, 8);
    assert(loaded.success);
    auto resource = resources.find<scene::SceneAssetResource>(sceneId);
    auto candidate = resource.get()->instantiate(resources, codecs, diagnostic);
    assert(candidate && candidate->objects().size() == 2);
    auto* restored = candidate->findObjectByName("child");
    assert(restored && restored->parent_object()->name() == "parent"
        && restored->local_transform().translation.y == 3);
    assert(restored->getComponent<scene2d::SpriteRendererComponent>()->desc().flipX);
    assert(candidate->findObjectByName("parent")
               ->getComponent<scene::ObjectLinksComponent>()
               ->links[0]
               .target
        == restored->id());
    scene::Scene instances("instances");
    auto instance = resource.get()->instantiateInto(
        instances, instances.rootId(), resources, codecs, diagnostic);
    assert(instance.valid() && instances.objects().size() == 3);
    core::pmr::String roundtrip;
    assert(scene::SceneDocumentResource::encode(
        *candidate, codecs, "arp.scene-asset", sceneId, roundtrip, diagnostic));
    assert(roundtrip == yaml);
    // A prefab has a separate resource/document type, with the same hierarchy contract.
    const core::pmr::String prefabId = "00000000-0000-4000-8000-000000000031";
    const core::pmr::String prefabFile((folder / "prefab.yaml").string().c_str());
    core::pmr::String prefabYaml;
    assert(scene::SceneDocumentResource::encode(
        authored, codecs, "arp.scene-prefab", prefabId, prefabYaml, diagnostic));
    auto write = [&](const core::pmr::String& path, const core::pmr::String& bytes) {
        assert(await(files, files.replace(path, {bytes.begin(), bytes.end()})).success);
    };
    write(prefabFile, prefabYaml);
    assert(resources.load<scene::ScenePrefabResource>({}, prefabId, prefabFile, 65536, 8).success);
    auto prefab = resources.find<scene::ScenePrefabResource>(prefabId);
    assert(prefab.get()
            ->instantiateInto(instances, instances.rootId(), resources, codecs, diagnostic)
            .valid());
    assert(instances.objects().size() == 6);
    assert(resources.unload(prefabId).success && !prefab.ready());
    auto malformed = core::YamlContract::Node::deserialize(prefabYaml);
    malformed["contractVersion"] = 9;
    auto serialized = core::YamlContract::Node::serialize(malformed);
    write(prefabFile, core::pmr::String(serialized.data(), serialized.size()));
    assert(!resources.load<scene::ScenePrefabResource>({}, prefabId, prefabFile, 65536, 8).success);
    malformed = core::YamlContract::Node::deserialize(prefabYaml);
    malformed["objects"][0]["parent"] = 1;
    serialized = core::YamlContract::Node::serialize(malformed);
    write(prefabFile, core::pmr::String(serialized.data(), serialized.size()));
    assert(!resources.load<scene::ScenePrefabResource>({}, prefabId, prefabFile, 65536, 8).success);
    malformed = core::YamlContract::Node::deserialize(prefabYaml);
    malformed["objects"][0]["components"][0]["links"][0]["target"] = 999;
    serialized = core::YamlContract::Node::serialize(malformed);
    write(prefabFile, core::pmr::String(serialized.data(), serialized.size()));
    assert(resources.load<scene::ScenePrefabResource>({}, prefabId, prefabFile, 65536, 8).success);
    auto invalid = resources.find<scene::ScenePrefabResource>(prefabId);
    assert(!invalid.get()
               ->instantiateInto(instances, instances.rootId(), resources, codecs, diagnostic)
               .valid()
        && instances.objects().size() == 6);
    assert(resources.unload(tests::spriteA).success);
    auto rejected = resource.get()->instantiate(resources, codecs, diagnostic);
    assert(
        !rejected && candidate->objects().size() == 2); // Candidate failure preserves live scene.
    assert(!resource.get()
               ->instantiateInto(instances, instances.rootId(), resources, codecs, diagnostic)
               .valid()
        && instances.objects().size() == 6);
    core::io::VersionedYamlStore<RecordSchema> records(files);
    const core::pmr::String saveFile(savePath.string().c_str());
    const core::pmr::String good = "type: arp.test-record\ncontractVersion: 1\nscore: 42\n";
    assert(await(files, records.save(saveFile, good)).success);
    assert(await(files, records.load(saveFile)).success);
    const core::pmr::String unsupported = "type: arp.test-record\ncontractVersion: 9\nscore: 99\n";
    assert(!await(files, records.save(saveFile, unsupported)).success);
    assert(core::readTextFile(savePath, 65536).text == good);
    const core::pmr::String corrupt = "type: [ broken yaml";
    assert(await(files,
        files.replace(saveFile, core::pmr::Vector<std::uint8_t>(corrupt.begin(), corrupt.end())))
            .success);
    assert(!await(files, records.load(saveFile)).success);
    assert(!await(files, records.save(saveFile, good)).success);
    assert(core::readTextFile(savePath, 65536).text == corrupt);
    auto h1 = files.read(sceneFile), h2 = files.read(sceneFile);
    assert(h1 && h2 && !files.read(sceneFile));
    assert(await(files, h1).success && await(files, h2).success);
    core::io::AtomicFileResult stale;
    assert(!files.take(h1, stale));
    std::puts(
        "PASS: tile chunks/collision retirement/rollback; scene hierarchy and references/roundtrip; async atomic saves and corruption preservation");
}

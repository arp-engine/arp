#include "3d/authoring_components.h"
#include "3d/components.h"
#include "3d/scene_component_persistence.h"
#include "core/io/file_io.h"
#include "resource/asset_package_closure.h"
#include "resource/prefab_asset_resources.h"
#include "scene/scene_asset_package.h"
#include "scene/scene_asset_resource.h"
#include "scene/scene_component_persistence.h"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>

namespace {
using namespace arp;
using Y = core::YamlContract;
using Node = Y::Node;
constexpr const char* sceneId = "00000000-0000-4000-8000-000000000081";
constexpr const char* secondId = "00000000-0000-4000-8000-000000000082";
constexpr const char* meshId = "00000000-0000-4000-8000-000000000083";

void check(bool ok, const core::pmr::String& diagnostic = {})
{
    if (!ok)
        std::fprintf(stderr, "FAIL: %s\n", diagnostic.c_str());
    assert(ok);
}

void write(const std::filesystem::path& path, const core::pmr::String& text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    assert(stream.good());
}

class Fixture final {
public:
    explicit Fixture(bool smoke)
        : root(std::filesystem::current_path()
              / (smoke ? "scene-package-smoke" : "scene-package-contract")),
          factory(core::makeShared<scene::SceneComponentFactory>()),
          registry(core::makeShared<resource::AssetPackageCodecRegistry>(2))
    {
        check(scene::registerScenePersistence(*factory));
        check(scene3d::registerScene3DPersistence(*factory));
        check(scene::registerSceneAssetPackageCodecs(*registry, factory, diagnostic), diagnostic);
        check(registry->freeze(diagnostic), diagnostic);
        std::filesystem::create_directories(root);
        scene::Scene authored(
            "native package", scene::SceneId::fromValue(1), scene::SceneExecutionMode::Passive);
        auto& parent = authored.createObject("parent");
        auto& camera = authored.createObject("camera");
        check(authored.reparent(camera.id(), parent.id()).ok());
        auto d = scene::CameraComponentDesc::perspective(73, 1.75f, .23f, 321);
        d.viewHandedness = scene::CameraViewHandedness::RightHanded;
        d.orthographicHeight = 11;
        d.viewport = {.1f, .2f, .7f, .6f};
        d.visibleQueues = scene::RenderQueueMask::world2DAnd3D();
        d.priority = -7;
        d.clearIntent = scene::CameraClearIntent::clear(.17f, .31f, .59f, .7f);
        camera.addComponent<scene::CameraComponent>(d);
        auto& mesh = parent.addComponent<scene3d::MeshComponent>();
        mesh.setBaseColorTint({.1f, .2f, .3f, .4f});
        scene3d::LightComponentDesc light;
        light.type = scene3d::LightType::Spot;
        light.color = {.3f, .5f, .7f};
        light.direction = {1, 2, 3};
        light.intensity = 9;
        light.range = 17;
        light.decay = 1.3f;
        light.spotConeAngle = .6f;
        light.spotPenumbra = .4f;
        light.castShadow = true;
        parent.addComponent<scene3d::LightComponent>(light);
        auto& sky = parent.addComponent<scene3d::SkyComponent>();
        sky.set_zenith_color(.1f, .3f, .7f);
        sky.set_dome_radius(123);
        check(scene::SceneDocumentResource::encode(
                  authored, *factory, "arp.scene-asset", sceneId, yaml, diagnostic),
            diagnostic);
    }

    resource::InternalAssetPackageEntry entry(const char* id = sceneId,
        const char* type = "SceneAssetResource",
        const char* document = "scene.yaml")
    {
        auto payload = Node::mapping();
        payload["type"] = "arp.scene-package";
        payload["contractVersion"] = 1;
        payload["documentPath"] = document;
        payload["maxDocumentBytes"] = 65536;
        payload["maxObjects"] = 16;
        return withPayload(id, type, payload);
    }

    resource::InternalAssetPackageEntry withPayload(const char* id,
        const char* type,
        const Node& payload,
        core::pmr::Vector<resource::ResourceDependency> deps = {})
    {
        resource::InternalAssetPackageEntry e;
        e.resourceId = id;
        e.resourceType = type;
        e.status = "Contract Complete";
        e.envelopePath = core::pmr::String(id) + ".yaml";
        e.dependencies = deps;
        const auto hash = resource::computeResourcePayloadHash(Y::serialize(payload));
        check(hash.success, hash.diagnostics);
        e.contentHash = hash.hash;
        auto envelope = Node::mapping();
        envelope["format"] = "ArpRuntimeResource";
        envelope["version"] = 1;
        envelope["resourceId"] = id;
        envelope["resourceType"] = type;
        envelope["status"] = "Contract Complete";
        envelope["alias"] = "";
        envelope["contentHash"] = static_cast<std::int64_t>(e.contentHash);
        envelope["payload"] = payload;
        envelope["dependencies"] = Node::sequence();
        for (const auto& dep : deps) {
            auto node = Node::mapping();
            node["id"] = dep.id.c_str();
            node["usage"] = dep.usage.c_str();
            envelope["dependencies"].get_value_ref<Node::sequence_type&>().push_back(
                std::move(node));
        }
        e.envelopeYaml = Y::serialize(envelope);
        return e;
    }

    resource::InternalAssetPackageDesc package(resource::InternalAssetPackageEntry e)
    {
        resource::InternalAssetPackageDesc p;
        p.name = "native scene";
        p.status = "Contract Complete";
        p.entries.push_back(std::move(e));
        return p;
    }

    void store(const resource::InternalAssetPackageDesc& p, const core::pmr::String& document)
    {
        write(root / "scene.yaml", document);
        const auto result = resource::InternalAssetPackageWriter().write(p, root);
        check(result.success, result.diagnostics);
    }

    void roundtrip(resource::ResourceManager& manager)
    {
        auto handle = manager.find<scene::SceneAssetResource>(sceneId);
        check(handle.ready());
        for (auto mode : {scene::SceneExecutionMode::Passive, scene::SceneExecutionMode::Runtime}) {
            auto scene = handle.get()->instantiate(manager, *factory, diagnostic, mode);
            check(bool(scene), diagnostic);
            const auto* parent = scene->findObjectByName("parent");
            const auto* camera = scene->findObjectByName("camera");
            check(parent && camera && camera->parentId() == parent->id());
            const auto& d = camera->getComponent<scene::CameraComponent>()->desc();
            check(d.projection == scene::CameraProjection::Perspective && d.verticalFovDegrees == 73
                && d.visibleQueues.bits() == 3 && d.clearIntent.clearColor[0] == .17f
                && d.clearIntent.clearColor[3] == .7f && d.priority == -7 && d.viewport.x == .1f);
            check(d.viewHandedness == scene::CameraViewHandedness::RightHanded
                && d.nearPlane == .23f && d.farPlane == 321 && d.aspectPolicy.aspectRatio == 1.75f
                && d.orthographicHeight == 11);
            const auto& light = parent->getComponent<scene3d::LightComponent>()->desc();
            check(light.type == scene3d::LightType::Spot && light.castShadow && light.intensity == 9
                && light.range == 17 && light.decay == 1.3f && light.spotPenumbra == .4f
                && light.direction.z == 3);
            check(parent->getComponent<scene3d::MeshComponent>()->desc().baseColorTint.w == .4f);
            check(parent->getComponent<scene3d::MeshComponent>()->resourceId().empty());
            check(parent->getComponent<scene3d::SkyComponent>()->dome_radius() == 123);
            core::pmr::String encoded;
            check(scene::SceneDocumentResource::encode(
                      *scene, *factory, "arp.scene-asset", sceneId, encoded, diagnostic),
                diagnostic);
            check(encoded == yaml);
        }
    }

    std::filesystem::path root;
    core::SharedPtr<scene::SceneComponentFactory> factory;
    core::SharedPtr<resource::AssetPackageCodecRegistry> registry;
    core::pmr::String yaml, diagnostic;
};

void contracts(Fixture& f)
{
    auto p = f.package(f.entry());
    f.store(p, f.yaml);
    resource::ResourceManager manager;
    auto loaded = resource::InternalAssetPackageReader().load(f.root, manager, f.registry);
    check(loaded.success, loaded.diagnostics);
    f.roundtrip(manager);
    auto generation = manager.find<scene::SceneAssetResource>(sceneId);
    check(manager.unload(sceneId).success && !generation.ready());
    check(resource::InternalAssetPackageReader().load(f.root, manager, f.registry).success
        && !generation.ready());

    resource::ResourceManager bounded;
    resource::ResourceBudgetConfig budget;
    budget.maxDecodedBytes = 16;
    bounded.setBudgetConfig(budget);
    check(!resource::InternalAssetPackageReader().load(f.root, bounded, f.registry).success);
    auto prefab = Y::parseSingleDocument(f.yaml);
    prefab["type"] = "arp.scene-prefab";
    f.store(f.package(f.entry(sceneId, "ScenePrefabResource")), Y::serialize(prefab));
    resource::ResourceManager prefabs;
    loaded = resource::InternalAssetPackageReader().load(f.root, prefabs, f.registry);
    check(loaded.success, loaded.diagnostics);
    check(prefabs.find<scene::ScenePrefabResource>(sceneId).ready());
    auto instance = prefabs.find<scene::ScenePrefabResource>(sceneId).get()->instantiate(
        prefabs, *f.factory, f.diagnostic);
    check(bool(instance), f.diagnostic);
    f.store(p, f.yaml);
    auto zero = core::makeShared<resource::AssetPackageCodecRegistry>(0);
    check(!zero->freeze(f.diagnostic));
    resource::AssetPackageCodecRegistry small(1);
    check(!scene::registerSceneAssetPackageCodecs(small, f.factory, f.diagnostic));
    check(small.find("SceneAssetResource") == nullptr
        && small.find("ScenePrefabResource") == nullptr);
    check(!scene::registerSceneAssetPackageCodecs(*f.registry, f.factory, f.diagnostic));
    auto unfrozen = core::makeShared<resource::AssetPackageCodecRegistry>(2);
    check(scene::registerSceneAssetPackageCodecs(*unfrozen, f.factory, f.diagnostic));
    check(!resource::InternalAssetPackageReader().load(f.root, manager, unfrozen).success);

    const auto fail = [&](resource::InternalAssetPackageEntry e, const core::pmr::String& yaml) {
        auto validSecond = Y::parseSingleDocument(f.yaml);
        validSecond["resourceId"] = secondId;
        write(f.root / "valid-second.yaml", Y::serialize(validSecond));
        auto candidate = f.package(f.entry(secondId, "SceneAssetResource", "valid-second.yaml"));
        candidate.entries.push_back(std::move(e));
        f.store(candidate, yaml);
        resource::ResourceManager isolated;
        const auto r = resource::InternalAssetPackageReader().load(f.root, isolated, f.registry);
        check(!r.success && !r.diagnostics.empty());
        check(!isolated.find<scene::SceneAssetResource>(sceneId).ready()
            && !isolated.find<scene::SceneAssetResource>(secondId).ready());
    };
    fail(f.entry(), "type: bad");
    auto wrongId = Y::parseSingleDocument(f.yaml);
    wrongId["resourceId"] = meshId;
    fail(f.entry(), Y::serialize(wrongId));
    auto payload = Y::required(Y::parseSingleDocument(f.entry().envelopeYaml), "payload");
    payload["contractVersion"] = 2;
    fail(f.withPayload(sceneId, "SceneAssetResource", payload), f.yaml);
    payload["contractVersion"] = 1;
    payload["maxDocumentBytes"] = 8;
    fail(f.withPayload(sceneId, "SceneAssetResource", payload), f.yaml);
    payload["maxDocumentBytes"] = 65536;
    payload["maxObjects"] = 1;
    fail(f.withPayload(sceneId, "SceneAssetResource", payload), f.yaml);
    payload["maxObjects"] = 16;
    payload["documentPath"] = "../outside.yaml";
    fail(f.withPayload(sceneId, "SceneAssetResource", payload), f.yaml);
    payload["documentPath"] = "scene.yaml";
    fail(f.withPayload(sceneId, "ScenePrefabResource", payload), f.yaml);

    auto document = Y::parseSingleDocument(f.yaml);
    auto& mesh = document["objects"][0]["components"][0];
    mesh["fields"]["resourceId"] = meshId;
    write(f.root / "scene.yaml", Y::serialize(document));
    resource::AssetPackagePayloadInspector inspector(f.registry, f.root);
    auto e = f.entry();
    check(!inspector.inspect(e).success); // missing explicit envelope binding
    e = f.withPayload(
        sceneId, "SceneAssetResource", payload, {{meshId, "scene/resource/MeshAssetResource"}});
    const auto indexed = inspector.inspect(e);
    check(indexed.success && indexed.dependencies.size() == 1
        && indexed.dependencies.front().expectedType == "MeshAssetResource");
    resource::ResourceManager external;
    check(!inspector.externalReady(external, indexed.dependencies.front()));
    check(external
            .loadResource(core::makeUnique<resource::ResourceBase>(resource::ResourceReference{
                              meshId, "MaterialResource", {}, "Contract Complete", {}}),
                {})
            .success);
    check(!inspector.externalReady(external, indexed.dependencies.front()));
    // A real typed material participates in the same atomic graph and closure.
    mesh["fields"]["resourceId"] = "";
    mesh["fields"]["materialResourceId"] = meshId;
    resource::registerPrefabAssetReflection();
    resource::MaterialResource material;
    resource::AssetResourcePackageBuildDesc materialDesc;
    materialDesc.resourceId = meshId;
    materialDesc.status = "Contract Complete";
    const auto materialEntry = material.buildPackageEntry(materialDesc);
    check(materialEntry.success, materialEntry.diagnostics);
    auto boundPackage = f.package(f.withPayload(
        sceneId, "SceneAssetResource", payload, {{meshId, "scene/resource/MaterialResource"}}));
    boundPackage.entries.push_back(materialEntry.entry);
    f.store(boundPackage, Y::serialize(document));
    resource::ResourceManager bound;
    loaded = resource::InternalAssetPackageReader().load(f.root, bound, f.registry);
    check(loaded.success, loaded.diagnostics);
    check(bound.find<resource::MaterialResource>(meshId).ready());
    auto withMaterial = bound.find<scene::SceneAssetResource>(sceneId).get()->instantiate(
        bound, *f.factory, f.diagnostic);
    check(bool(withMaterial), f.diagnostic);
    check(withMaterial->findObjectByName("parent")
              ->getComponent<scene3d::MeshComponent>()
              ->materialResourceId()
        == meshId);

    document = Y::parseSingleDocument(f.yaml);
    document["objects"][0]["components"][0]["type"] = "unsupported/1";
    f.store(p, Y::serialize(document));
    check(!inspector.inspect(f.entry()).success);
    document = Y::parseSingleDocument(f.yaml);
    document["objects"][1]["components"][0]["fields"]["verticalFovDegrees"] = 180;
    f.store(p, Y::serialize(document));
    check(!inspector.inspect(f.entry()).success);
    document = Y::parseSingleDocument(f.yaml);
    document["objects"][0]["components"][1]["fields"]["range"] = -1;
    f.store(p, Y::serialize(document));
    check(!inspector.inspect(f.entry()).success);
    document = Y::parseSingleDocument(f.yaml);
    document["objects"][0]["components"][2]["fields"]["domeRadius"] = 0;
    f.store(p, Y::serialize(document));
    check(!inspector.inspect(f.entry()).success);

    // Orthographic and Load still preserve the explicit RGBA, queue and policy.
    document = Y::parseSingleDocument(f.yaml);
    auto& camera = document["objects"][1]["components"][0]["fields"];
    // Use the declared enum values of the existing reflection archive contract.
    camera["projection"] = static_cast<int>(scene::CameraProjection::Orthographic);
    camera["visible2D"] = true;
    camera["visible3D"] = false;
    camera["colorLoadOp"] = static_cast<int>(scene::CameraLoadOp::Load);
    camera["aspectPolicy"]["fields"]["kind"] =
        static_cast<int>(scene::CameraAspectPolicyKind::MatchViewport);
    f.store(p, Y::serialize(document));
    resource::ResourceManager ortho;
    loaded = resource::InternalAssetPackageReader().load(f.root, ortho, f.registry);
    check(loaded.success, loaded.diagnostics);
    auto restored = ortho.find<scene::SceneAssetResource>(sceneId).get()->instantiate(
        ortho, *f.factory, f.diagnostic);
    check(bool(restored), f.diagnostic);
    const auto& d =
        restored->findObjectByName("camera")->getComponent<scene::CameraComponent>()->desc();
    check(d.projection == scene::CameraProjection::Orthographic && d.visibleQueues.bits() == 1
        && d.clearIntent.colorLoadOp == scene::CameraLoadOp::Load
        && d.clearIntent.clearColor[3] == .7f
        && d.aspectPolicy.kind == scene::CameraAspectPolicyKind::MatchViewport);

    f.store(p, f.yaml);
    p.closurePath = "closure.yaml";
    resource::AssetPackageClosureService closure(f.registry);
    auto closed = closure.prepareIndex(p, f.root, {4, 4, 65536, 65536, 65536});
    check(closed.success, closed.diagnostics);
    check(closed.files.size() == 1 && closed.files.front().path == "scene.yaml");
    resource::ResourceManager closedManager;
    loaded = resource::InternalAssetPackageReader().load(f.root, closedManager, f.registry);
    check(loaded.success, loaded.diagnostics);
    write(f.root / "scene.yaml", f.yaml + "\n");
    check(!closure.inspect(f.root).success);
    f.store(f.package(f.entry()), f.yaml);
    std::puts(
        "PASS scene package contracts, Camera/Mesh/Light/Sky round-trip, closure and rejection");
}

void smoke(Fixture& f)
{
    const auto p = f.package(f.entry());
    f.store(p, f.yaml);
    auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned cycle = 0; cycle <= 20; ++cycle) {
        {
            resource::ResourceManager manager;
            const auto loaded =
                resource::InternalAssetPackageReader().load(f.root, manager, f.registry);
            check(loaded.success, loaded.diagnostics);
            f.roundtrip(manager);
            check(manager.unload(sceneId).success);
            write(f.root / "scene.yaml", "type: invalid");
            check(
                !resource::InternalAssetPackageReader().load(f.root, manager, f.registry).success);
            write(f.root / "scene.yaml", f.yaml);
        }
        const auto released = core::defaultCpuAllocator().stats();
        if (!cycle)
            baseline = released;
        check(released.liveBytes == baseline.liveBytes
            && released.allocationCount - released.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts(
        "PASS scene package native CPU smoke: 20 load/instantiate/fail/unload cycles; core live bytes/count delta=0");
}
} // namespace

int main(int argc, char**)
{
    Fixture f(argc > 1);
    if (argc > 1)
        smoke(f);
    else
        contracts(f);
}

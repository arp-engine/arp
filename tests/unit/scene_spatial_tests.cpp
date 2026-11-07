#include "3d/scene_3d_spatial_index.h"
#include "3d/scene_3d_visibility.h"
#include "core/math/conservative_clip_volume.h"
#include "scene/scene.h"

#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <utility>

namespace {
using namespace arp;
using namespace arp::scene3d;

void check(bool value, const char* why)
{
    if (!value)
        throw std::runtime_error(why);
}

template <class F> void rejects(F action)
{
    bool failed = false;
    try {
        action();
    } catch (const core::DiagnosticError&) {
        failed = true;
    }
    check(failed, "invalid spatial input accepted");
}

void registry()
{
    constexpr unsigned count = 8192;
    Scene3DSpatialIndex index;
    index.reserve(count);
    core::pmr::Vector<SceneSpatialHandle> handles{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<Aabb> boxes{&core::defaultCpuMemoryResource()};
    for (unsigned i = 0; i < count; ++i) {
        const float x = (int(i % 128) - 64) * 3.f, z = (i / 128) * 3.f;
        boxes.push_back(Aabb::fromCenterExtents({x, 0, z}, {.4f, .4f, .4f}));
        handles.push_back(index.insert(
            {scene::GameObjectId::fromValue(i + 1), scene::ComponentId::fromValue(i + 1)},
            boxes.back(),
            i % 2 ? SpatialMobility::Dynamic : SpatialMobility::Static));
    }
    auto matrix = core::Mat4::identity().m;
    const auto compare = [&] {
        core::ConservativeClipVolume volume(matrix.data());
        index.query(&matrix);
        std::size_t visible = 0;
        for (unsigned i = 0; i < count; ++i) {
            const auto& b = boxes[i];
            const float low[]{b.min.x, b.min.y, b.min.z}, high[]{b.max.x, b.max.y, b.max.z};
            const bool expected = volume.intersects(low, high);
            check(
                index.visible(handles[i]) == expected, "tree differs from brute-force visibility");
            visible += expected;
        }
        check(index.visibleHandles().size() == visible, "duplicate or missing leaf");
    };
    compare();
    check(index.telemetry().nodeTests < count / 4, "no subtree pruning");
    const auto builds = index.telemetry().staticBuilds, queries = index.telemetry().queries;
    index.query(&matrix);
    check(index.telemetry().nodeTests == 0 && index.telemetry().queries == queries
            && index.changedHandles().empty(),
        "identical query did not reuse visibility");
    std::mt19937 random(77129);
    std::uniform_real_distribution<float> coordinate(-100, 100);
    for (unsigned frame = 0; frame < 32; ++frame) {
        for (unsigned moved = 0; moved < 64; ++moved) {
            const auto slot = (random() % (count / 2)) * 2 + 1;
            boxes[slot] = Aabb::fromCenterExtents(
                {coordinate(random), 0, coordinate(random)}, {.4f, .4f, .4f});
            index.update(handles[slot], boxes[slot]);
        }
        matrix = core::Mat4::translation({coordinate(random), 0, coordinate(random)}).m;
        compare();
    }
    check(index.telemetry().staticBuilds == builds && index.telemetry().dynamicUpdates > 0,
        "dynamic motion rebuilt the static BVH");
    const auto old = handles[0];
    index.erase(old);
    rejects([&] {
        index.update(old, boxes[0]);
    });
    handles[0] = index.insert(
        {scene::GameObjectId::fromValue(99999), {}}, boxes[0], SpatialMobility::Static);
    check(handles[0] != old, "generation reused stale handle");
    compare();
    boxes[0] = {{-1, -1, -.1f}, {1, 1, .1f}};
    index.update(handles[0], boxes[0]);
    matrix = core::Mat4::identity().m;
    compare(); // Near/side boundary contact and a changed static placement.
    index.query(nullptr);
    check(index.visibleHandles().size() == count, "disabled culling lost an object");
    for (const auto h : handles)
        index.erase(h);
    index.query(&matrix);
    check(index.visibleHandles().empty(), "retired scene retained proxies");
    rejects([&] {
        index.erase(old);
    });
    auto invalid = boxes[0];
    invalid.min.x = std::numeric_limits<float>::quiet_NaN();
    rejects([&] {
        index.insert({scene::GameObjectId::fromValue(1), {}}, invalid, SpatialMobility::Dynamic);
    });
}

void dynamicBalance()
{
    DynamicAabbTree tree;
    core::pmr::Vector<std::uint32_t> proxies{&core::defaultCpuMemoryResource()};
    for (unsigned i = 0; i < 4096; ++i)
        proxies.push_back(
            tree.insert({Aabb::fromCenterExtents({float(i), 0, 0}, {1, 1, 1}), i, 0}));
    check(tree.height() < 32, "ordered insertions degenerated the dynamic tree");
    for (unsigned i = 0; i < proxies.size(); ++i)
        tree.update(proxies[i], Aabb::fromCenterExtents({-float(i), 0, 0}, {1, 1, 1}));
    check(tree.height() < 32, "teleports degenerated the dynamic tree");
    for (const auto p : proxies)
        tree.remove(p);
    check(tree.leafCount() == 0 && tree.height() == 0, "dynamic removal leaked a node");
}

void standardQueue()
{
    Scene3DRenderQueue queue;
    Scene3DRenderItem item;
    item.objectId = scene::GameObjectId::fromValue(1);
    item.worldBounds = Bounds3D::explicitBounds({0, 0, .5f}, {.1f, .1f, .1f});
    queue.addItem(item);
    item.objectId = scene::GameObjectId::fromValue(2);
    item.worldBounds.center.x = 10;
    queue.addItem(item);
    Scene3DVisibilitySystem system;
    const auto matrix = core::Mat4::identity().m;
    const auto result = system.filterVisible(queue, {}, matrix);
    check(result.ok() && result.queue().items().size() == 1,
        "standard matrix visibility did not use spatial query");
    check(system.filterVisible(queue, {}, matrix).ok() && system.spatialTelemetry().cacheHits > 0,
        "standard queue did not retain its spatial registry");
    check(system.filterVisible(queue, {}).ok(), "parameter-camera visibility failed");
    const auto queries = system.spatialTelemetry().queries;
    check(system.filterVisible(queue, {}).ok() && system.spatialTelemetry().queries == queries,
        "parameter-camera entry did not reuse the spatial index");
    Scene3DRenderQueue empty;
    check(system.filterVisible(empty, {}, matrix).ok() && system.spatialTelemetry().objects == 0,
        "standard queue retained destroyed objects");
}

class Probe final : public scene::Component {
public:
    std::uint64_t tick = 0;

    void extract(
        const scene::RenderExtractContext& context, scene::RenderExtractSink& sink) const override
    {
        scene::RenderExtractPayload payload;
        payload.objectId = ownerId();
        payload.sortKey = tick;
        sink.accept(context, payload);
    }
};

class ContextObserver final : public scene::RenderExtractSink {
public:
    unsigned count = 0;
    std::uint64_t tick = 0;
    scene::GameObject* mutate = nullptr;
    const Scene3DSpatialIndex* reenter = nullptr;
    const scene::Scene* reentryScene = nullptr;
    core::Vec3 lastPosition{};

    void accept(const scene::RenderExtractContext& context,
        const scene::RenderExtractPayload& payload) override
    {
        ++count;
        tick = payload.sortKey;
        lastPosition = context.worldTransform->translation;
        const auto expected = context.owner->world_transform();
        check(context.activeInHierarchy && context.owner->activeInHierarchy()
                && std::memcmp(&expected, context.worldTransform, sizeof(expected)) == 0,
            "retained extraction context differs from live scene");
        if (mutate) {
            auto* object = std::exchange(mutate, nullptr);
            object->transform().setLocalTranslation({7, 8, 9});
        }
        if (reenter) {
            const auto* index = std::exchange(reenter, nullptr);
            rejects([&] {
                index->extractVisible(*reentryScene, *this);
            });
        }
    }
};

void retainedExtraction()
{
    Scene3DSpatialIndex index;
    index.reserve(8);
    scene::Scene scene;
    auto& parent = scene.createObject("parent");
    auto& a = scene.createObject("a");
    auto& b = scene.createObject("b");
    check(scene.reparent(a.id(), parent.id()).ok(), "test hierarchy");
    auto& pa = a.addComponent<Probe>();
    auto& pb = b.addComponent<Probe>();
    const auto handle =
        index.insert({a.id(), pa.id()}, {{0, 0, 0}, {1, 1, 1}}, SpatialMobility::Static);
    const SceneSpatialOwner extra{b.id(), pb.id()};
    index.query(nullptr);
    const auto extract = [&] {
        ContextObserver sink;
        index.extractVisible(scene, sink, {&extra, 1});
        return sink;
    };
    check(extract().count == 2, "cold context extraction");
    const auto resolves = index.telemetry().contextResolves;
    const auto warmAllocations = core::defaultCpuAllocator().stats().allocationCount;
    for (unsigned frame = 0; frame < 100; ++frame) {
        pb.tick = frame;
        check(extract().tick == frame, "retained cache froze dynamic payload");
    }
    check(
        index.telemetry().contextResolves == resolves && index.telemetry().contextCacheHits == 200,
        "unchanged scene repeated object/component resolution");
    check(core::defaultCpuAllocator().stats().allocationCount == warmAllocations,
        "warm extraction allocated CPU memory");
    ContextObserver recursive;
    recursive.reenter = &index;
    recursive.reentryScene = &scene;
    index.extractVisible(scene, recursive, {&extra, 1});
    auto matrix = core::Mat4::identity().m;
    index.query(&matrix);
    extract();
    check(index.telemetry().contextResolves == resolves, "camera invalidated world context");
    parent.transform().set_local_transform(scene::Transform::fromTranslation({1, 2, 3}));
    extract();
    a.transform().setLocalTransform(scene::Transform::fromTranslation({2, 3, 4}));
    extract();
    a.transform().set_local_translation({4, 5, 6});
    extract();
    parent.transform().setLocalRotation(core::Quat::fromYawPitchRoll(20, 30, 40));
    extract();
    a.transform().set_local_yaw_pitch(30, 40);
    extract();
    scene::TransformEditorProperties properties;
    properties.position = {8, 3, 2};
    properties.rotation = {30, 20, 10};
    properties.scale = {2, 3, 4};
    check(a.transform().applyEditorAuthoringProperties(arp::reflection::object(properties)),
        "editor transform");
    extract();
    check(scene.setWorldTransform(a.id(), scene::Transform::fromTranslation({1, 5, 2})).ok(),
        "world transform");
    extract();
    parent.set_active(false);
    check(extract().count == 1, "parent inactive context retained");
    parent.setActive(true);
    pa.set_enabled(false);
    check(extract().count == 1, "component disabled context retained");
    pa.setEnabled(true);
    check(scene.reparent(a.id(), b.id()).ok(), "reparent");
    check(extract().count == 2, "reparent lost component");
    ContextObserver mutation;
    mutation.mutate = &b;
    index.extractVisible(scene, mutation, {&extra, 1});
    check(mutation.count == 2 && mutation.lastPosition.x == 7,
        "sink mutation used stale next context");
    index.extractVisible(scene, mutation); // Unbounded stream removal and re-entry.
    check(extract().lastPosition.x == 7, "unbounded context re-entry");
    const auto oldComponent = pa.id();
    check(a.removeComponent(oldComponent), "component removal");
    rejects([&] {
        extract();
    });
    auto& replacement = a.addComponent<Probe>();
    index.erase(handle);
    const auto fresh =
        index.insert({a.id(), replacement.id()}, {{0, 0, 0}, {1, 1, 1}}, SpatialMobility::Static);
    index.query(nullptr);
    check(extract().count == 2 && fresh != handle, "slot reuse retained old component");
    index.erase(fresh);
    check(scene.destroyObject(a.id()).ok(), "object removal");
    index.query(nullptr);
    check(extract().count == 1, "destroyed object retained");

    // A different scene may have exactly the same IDs. The old token remains
    // owned by the index, so allocator address reuse cannot validate a binding.
    scene::Scene another;
    auto& otherParent = another.createObject("parent");
    auto& otherA = another.createObject("a");
    auto& otherB = another.createObject("b");
    otherA.addComponent<Probe>();
    auto& otherProbe = otherB.addComponent<Probe>();
    otherProbe.tick = 999;
    otherB.transform().setLocalTranslation({44, 0, 0});
    check(otherB.id() == extra.object && otherProbe.id() == extra.component,
        "scene identity fixture");
    ContextObserver switched;
    index.extractVisible(another, switched, {&extra, 1});
    check(switched.tick == 999 && switched.lastPosition.x == 44,
        "scene token reused stale identities");
    (void)otherParent;
}

void restoredContextIdentity()
{
    scene::Scene passive(
        "restored", scene::SceneId::fromValue(1), scene::SceneExecutionMode::Passive);
    auto& live = passive.createObject("live");
    auto& probe = live.addComponent<Probe>();
    Scene3DSpatialIndex index;
    index.insert({live.id(), probe.id()}, {{0, 0, 0}, {1, 1, 1}}, SpatialMobility::Static);
    index.query(nullptr);
    ContextObserver sink;
    index.extractVisible(passive, sink);
    const auto before = index.telemetry().contextResolves;
    scene::Scene::IdentityRestoration restore(passive, {1, 1});
    restore.restoreObject(scene::GameObjectId::fromValue(100),
        scene::ComponentId::fromValue(100),
        "restored",
        passive.rootId(),
        1);
    restore.prepare();
    index.extractVisible(passive, sink);
    check(index.telemetry().contextResolves == before,
        "unpublished identity invalidated live bindings");
    restore.commit();
    index.extractVisible(passive, sink);
    check(index.telemetry().contextResolves == before + 1,
        "identity publication did not invalidate context");

    scene::Scene runtime;
    SceneSpatialOwner old;
    Scene3DSpatialIndex rollback;
    {
        scene::Scene::RestorationTransaction transaction(runtime);
        auto& object = runtime.createObject("candidate");
        auto& component = object.addComponent<Probe>();
        old = {object.id(), component.id()};
        rollback.insert(old, {{0, 0, 0}, {1, 1, 1}}, SpatialMobility::Static);
        rollback.query(nullptr);
        rollback.extractVisible(runtime, sink);
    }
    auto& replacement = runtime.createObject("replacement");
    auto& component = replacement.addComponent<Probe>();
    component.tick = 123;
    replacement.transform().setLocalTranslation({11, 0, 0});
    check(replacement.id() == old.object && component.id() == old.component,
        "rollback identity fixture");
    rollback.extractVisible(runtime, sink);
    check(
        sink.tick == 123 && sink.lastPosition.x == 11, "rollback reused cached candidate identity");
}

class Counter final : public scene::RenderExtractSink {
public:
    unsigned count = 0;
    core::pmr::Vector<std::uint64_t> order{&core::defaultCpuMemoryResource()};

    void accept(
        const scene::RenderExtractContext&, const scene::RenderExtractPayload& payload) override
    {
        ++count;
        order.push_back(payload.objectId.value());
    }
};

void extraction()
{
    scene::Scene scene;
    Scene3DSpatialIndex index;
    auto& inside = scene.createObject("inside");
    auto& unbounded = scene.createObject("unbounded emitter");
    auto& hidden = scene.createObject("hidden");
    auto& emitter = scene.createObject("offscreen emitter");
    auto& a = inside.addComponent<Probe>();
    auto& b = hidden.addComponent<Probe>();
    auto& c = emitter.addComponent<Probe>();
    auto& d = unbounded.addComponent<Probe>();
    index.insert({inside.id(), a.id()}, {{0, 0, 0}, {1, 1, 1}}, SpatialMobility::Dynamic);
    const auto discarded =
        index.insert({hidden.id(), b.id()}, {{20, 20, 20}, {21, 21, 21}}, SpatialMobility::Static);
    index.insert(
        {emitter.id(), c.id(), true}, {{20, 20, 20}, {21, 21, 21}}, SpatialMobility::Dynamic);
    auto matrix = core::Mat4::identity().m;
    index.query(&matrix);
    Counter selected;
    index.extractVisible(scene, selected);
    check(selected.count == 2, "extraction lost an emitter or dispatched an offscreen mesh");
    const SceneSpatialOwner unboundedOwner{unbounded.id(), d.id(), true};
    Counter merged;
    index.extractVisible(scene, merged, {&unboundedOwner, 1});
    check(merged.count == 3 && merged.order[0] == inside.id().value()
            && merged.order[1] == unbounded.id().value() && merged.order[2] == emitter.id().value(),
        "Unbounded emitters changed stable extraction order");
    const SceneSpatialOwner duplicate{inside.id(), a.id(), true};
    rejects([&] {
        index.extractVisible(scene, merged, {&duplicate, 1});
    });
    const std::array<SceneSpatialOwner, 2> unordered{unboundedOwner, duplicate};
    rejects([&] {
        index.extractVisible(scene, merged, {unordered.data(), unordered.size()});
    });
    index.update(discarded, {{0, 0, 0}, {1, 1, 1}});
    rejects([&] {
        index.extractVisible(scene, selected);
    });
    index.query(&matrix);
    Counter reentry;
    index.extractVisible(scene, reentry);
    check(reentry.count == 3, "extraction lost a returning component");
    index.erase(discarded);
    index.query(&matrix);
    Counter retired;
    index.extractVisible(scene, retired);
    check(retired.count == 2, "erased proxy remained in the extraction set");
}
} // namespace

int main()
{
    try {
        registry();
        dynamicBalance();
        standardQueue();
        extraction();
        retainedExtraction();
        restoredContextIdentity();
        std::cout
            << "PASS scene spatial: 8192 proxies, 32 moving views, lifecycle, balance, cache and standard queue\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

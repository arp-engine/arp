#include "3d/scene_3d_spatial_index.h"
#include "3d/scene_3d_visibility.h"
#include "core/math/conservative_clip_volume.h"
#include "scene/scene.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>

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
    void extract(
        const scene::RenderExtractContext& context, scene::RenderExtractSink& sink) const override
    {
        scene::RenderExtractPayload payload;
        payload.objectId = ownerId();
        sink.accept(context, payload);
    }
};

class Counter final : public scene::RenderExtractSink {
public:
    unsigned count = 0;

    void accept(const scene::RenderExtractContext&, const scene::RenderExtractPayload&) override
    {
        ++count;
    }
};

void extraction()
{
    scene::Scene scene;
    Scene3DSpatialIndex index;
    auto& inside = scene.createObject("inside");
    auto& hidden = scene.createObject("hidden");
    auto& emitter = scene.createObject("offscreen emitter");
    auto& a = inside.addComponent<Probe>();
    auto& b = hidden.addComponent<Probe>();
    auto& c = emitter.addComponent<Probe>();
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
        std::cout
            << "PASS scene spatial: 8192 proxies, 32 moving views, lifecycle, balance, cache and standard queue\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

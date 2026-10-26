#include "editor/viewport/editor_scene_transform_batch.h"
#include "editor/workspace/editor_scene_authoring_codec.h"
#include "scene/scene.h"

#include <array>
#include <cassert>
#include <cstdio>

namespace {
using namespace arp;
using namespace arp::editor;
using Batch = EditorSceneTransformBatch;
using Pivot = EditorSceneTransformPivot;
using Tool = EditorSceneTransformTool;
using Space = EditorSceneTransformSpace;
using Frame = EditorSceneTransformFrame;
constexpr Batch::Budget budget{
    EditorSceneAuthoringCodec::maxObjects, EditorSceneAuthoringCodec::maxObjects + 1};
constexpr float quarterTurn = 1.5707963267948966f;

bool close(core::Vec3 a, core::Vec3 b)
{
    return core::distance(a, b) < 0.0002f;
}

class Fixture final {
public:
    Fixture()
    {
        first = scene.createObject("first").id();
        second = scene.createObject("second").id();
        child = scene.createObject("child").id();
        scene::Transform a, b;
        a.translation = {2, 0, 0};
        a.scale = {2, 1, 1};
        b.translation = {6, 0, 0};
        b.scale = {4, 1, 1};
        set(first, a);
        set(second, b);
        set(child, scene::Transform::fromTranslation({2, 0, 0}));
        assert(scene.reparent(child, first).ok());
    }

    void set(scene::GameObjectId id, const scene::Transform& value)
    {
        scene.findObject(id)->set_local_transform(value);
    }

    scene::Transform local(scene::GameObjectId id) const
    {
        return scene.findObject(id)->local_transform();
    }

    scene::SceneTraversalSnapshot snapshot()
    {
        scene::SceneTraversalOptions options;
        options.maxNodeCount = budget.maxSnapshotNodes;
        return scene.extractTraversalSnapshot(options);
    }

    std::array<scene::GameObjectId, 2> selection() const { return {first, second}; }

    scene::Scene scene{"batch", scene::SceneId::fromValue(1), scene::SceneExecutionMode::Passive};
    scene::GameObjectId first, second, child;
};

const Batch::Change& change(const core::pmr::Vector<Batch::Change>& values, scene::GameObjectId id)
{
    for (const auto& value : values)
        if (value.objectId == id)
            return value;
    assert(false);
    return values.front();
}

void pivotsAndHierarchy()
{
    for (Pivot pivot : {Pivot::Active, Pivot::Center, Pivot::Individual}) {
        Fixture fixture;
        const auto snapshot = fixture.snapshot();
        const auto selection = fixture.selection();
        Batch batch;
        assert(!Batch::capture(snapshot, fixture.first, selection, pivot, budget, batch));
        assert(batch.effectiveCount() == 2 && batch.members().size() == 2);
        assert(close(batch.origin(), {pivot == Pivot::Center ? 4.f : 2.f, 0, 0}));
        core::pmr::Vector<Batch::Change> changes;
        assert(!batch.prepare(Tool::Move, Space::World, 0, 0.5, changes));
        assert(close(change(changes, fixture.first).afterLocal.translation, {2.5f, 0, 0}));
        assert(close(change(changes, fixture.second).afterLocal.translation, {6.5f, 0, 0}));
        assert(!batch.prepare(Tool::Rotate, Space::World, 2, quarterTurn, changes));
        const auto a = change(changes, fixture.first).afterLocal;
        const auto b = change(changes, fixture.second).afterLocal;
        assert(close(core::rotate(a.rotation, {1, 0, 0}), {0, 1, 0}));
        assert(close(a.translation,
            pivot == Pivot::Active       ? core::Vec3{2, 0, 0}
                : pivot == Pivot::Center ? core::Vec3{4, -2, 0}
                                         : core::Vec3{2, 0, 0}));
        assert(close(b.translation,
            pivot == Pivot::Active       ? core::Vec3{2, 4, 0}
                : pivot == Pivot::Center ? core::Vec3{4, 2, 0}
                                         : core::Vec3{6, 0, 0}));
        assert(!batch.prepare(Tool::Scale, Space::Local, 0, 1, changes));
        assert(close(change(changes, fixture.first).afterLocal.scale, {3, 1, 1}));
        assert(close(change(changes, fixture.second).afterLocal.scale,
            {pivot == Pivot::Individual ? 5.f : 6.f, 1, 1}));
        assert(close(change(changes, fixture.first).afterLocal.translation,
            {pivot == Pivot::Center ? 1.f : 2.f, 0, 0}));
        assert(close(change(changes, fixture.second).afterLocal.translation,
            {pivot == Pivot::Active          ? 8.f
                    : pivot == Pivot::Center ? 7.f
                                             : 6.f,
                0,
                0}));
        assert(batch.matchesStart(fixture.snapshot(), fixture.first, selection));
        assert(close(fixture.local(fixture.first).translation, {2, 0, 0}));
    }
    Fixture fixture;
    const std::array selected{fixture.second, fixture.child, fixture.first};
    Batch batch;
    assert(
        !Batch::capture(fixture.snapshot(), fixture.child, selected, Pivot::Active, budget, batch));
    assert(batch.effectiveCount() == 2 && close(batch.origin(), {6, 0, 0}));
    assert(batch.members()[0].objectId == fixture.first
        && batch.members()[1].objectId == fixture.second && !batch.members()[2].effective);
    core::pmr::Vector<Batch::Change> changes;
    assert(!batch.prepare(Tool::Move, Space::World, 0, 0.5, changes) && changes.size() == 2);
    // A native Scene verifies hierarchy inheritance of the prepared values.
    // This test application is not a production command/history implementation.
    for (const auto& value : changes)
        fixture.set(value.objectId, value.afterLocal);
    assert(close(
        fixture.snapshot().requireNode(fixture.child).worldTransform.translation, {6.5f, 0, 0}));
    assert(close(fixture.local(fixture.child).translation, {2, 0, 0}));
    std::puts(
        "PASS batch pivots, proportional group scaling, stable IDs and selected ancestor inheritance; preparation never writes Scene");
}

void rotatedAndNegative()
{
    Fixture fixture;
    auto a = fixture.local(fixture.first);
    auto b = fixture.local(fixture.second);
    a.rotation = b.rotation = core::Quat::fromAxisAngle({0, 0, 1}, quarterTurn);
    a.translation = {0, 2, 0};
    b.translation = {0, 6, 0};
    a.scale.x = -2;
    fixture.set(fixture.first, a);
    fixture.set(fixture.second, b);
    Batch batch;
    assert(!Batch::capture(
        fixture.snapshot(), fixture.first, fixture.selection(), Pivot::Active, budget, batch));
    core::pmr::Vector<Batch::Change> changes;
    assert(!batch.prepare(Tool::Move, Space::Local, 0, 0.5, changes));
    assert(close(change(changes, fixture.second).afterLocal.translation, {0, 6.5f, 0}));
    assert(!batch.prepare(Tool::Scale, Space::Local, 0, 1, changes));
    assert(close(change(changes, fixture.first).afterLocal.scale, {-1, 1, 1}));
    assert(close(change(changes, fixture.second).afterLocal.scale, {2, 1, 1}));
    assert(close(change(changes, fixture.second).afterLocal.translation, {0, 4, 0}));
    assert(!batch.prepare(Tool::Scale, Space::World, 1, 1, changes));
    assert(close(change(changes, fixture.second).afterLocal.scale, {2, 1, 1}));
    b.rotation = {};
    fixture.set(fixture.second, b);
    assert(!Batch::capture(
        fixture.snapshot(), fixture.first, fixture.selection(), Pivot::Individual, budget, batch));
    assert(!batch.prepare(Tool::Move, Space::Local, 0, 0.5, changes));
    assert(close(change(changes, fixture.first).afterLocal.translation, {0, 2.5f, 0}));
    assert(close(change(changes, fixture.second).afterLocal.translation, {0.5f, 6, 0}));
    std::puts(
        "PASS rotated local/world group basis, signed reference scale and individual local axes");
}

void failureAndStaleness()
{
    Fixture fixture;
    auto snapshot = fixture.snapshot();
    const auto selected = fixture.selection();
    Batch batch;
    assert(!Batch::capture(snapshot, fixture.first, selected, Pivot::Center, budget, batch));
    const auto* oldMembers = batch.members().data();
    const auto oldOrigin = batch.origin();
    const auto rejected = [&](const scene::SceneTraversalSnapshot& input,
                              core::ArrayView<scene::GameObjectId> ids,
                              Batch::Budget limits = budget) {
        assert(Batch::capture(input, fixture.first, ids, Pivot::Active, limits, batch));
        assert(batch.members().data() == oldMembers && close(batch.origin(), oldOrigin));
    };
    rejected(snapshot, selected, {1, budget.maxSnapshotNodes});
    rejected(snapshot, selected, {budget.maxSelectedObjects, 1});
    rejected(snapshot, selected, {});
    rejected(snapshot, {});
    const std::array duplicates{fixture.first, fixture.first};
    rejected(snapshot, duplicates);
    const std::array missing{fixture.first, scene::GameObjectId::fromValue(9999)};
    rejected(snapshot, missing);
    auto nodes = snapshot.nodes();
    nodes.back().objectId = nodes.front().objectId;
    rejected(scene::SceneTraversalSnapshot::success(std::move(nodes)), selected);
    nodes = snapshot.nodes();
    nodes.back().parentId = scene::GameObjectId::fromValue(9999);
    rejected(scene::SceneTraversalSnapshot::success(std::move(nodes)), selected);
    nodes = snapshot.nodes();
    nodes[1].localTransform = nodes[1].worldTransform = {};
    nodes[2].localTransform = nodes[2].worldTransform = {};
    nodes[1].parentId = nodes[2].objectId;
    nodes[2].parentId = nodes[1].objectId;
    rejected(scene::SceneTraversalSnapshot::success(std::move(nodes)), selected);
    const std::array reversed{fixture.second, fixture.first};
    assert(batch.matchesStart(snapshot, fixture.first, reversed));
    assert(!batch.matchesStart(snapshot, fixture.second, selected));
    assert(!batch.matchesStart(snapshot, fixture.first, duplicates));
    auto changed = fixture.local(fixture.first);
    changed.translation.y = 1;
    fixture.set(fixture.first, changed);
    assert(!batch.matchesStart(fixture.snapshot(), fixture.first, selected));
    fixture.set(fixture.first, snapshot.requireNode(fixture.first).localTransform);

    core::pmr::Vector<Batch::Change> output(1);
    output.front().objectId = scene::GameObjectId::fromValue(999);
    const auto* oldOutput = output.data();
    const auto unchanged = [&] {
        assert(output.size() == 1 && output.data() == oldOutput
            && output.front().objectId.value() == 999);
    };
    assert(batch.prepare(Tool::Select, Space::World, 0, 1, output));
    unchanged();
    assert(batch.prepare(Tool::Move, Space::World, 3, 1, output));
    unchanged();
    assert(batch.prepare(
        Tool::Move, Space::World, 0, std::numeric_limits<double>::quiet_NaN(), output));
    unchanged();
    auto b = fixture.local(fixture.second);
    b.rotation = core::Quat::fromAxisAngle({0, 0, 1}, 0.3f);
    fixture.set(fixture.second, b);
    assert(
        !Batch::capture(fixture.snapshot(), fixture.first, selected, Pivot::Center, budget, batch));
    assert(std::string_view(batch.prepare(Tool::Scale, Space::World, 0, 1, output))
        == "EDITOR-TRANSFORM-WORLD-SCALE-SHEAR");
    unchanged();
    b.rotation = {};
    b.translation.x = Frame::maximumTranslation;
    fixture.set(fixture.second, b);
    assert(
        !Batch::capture(fixture.snapshot(), fixture.first, selected, Pivot::Center, budget, batch));
    assert(batch.prepare(Tool::Move, Space::World, 0, 1, output));
    unchanged();
    auto a = fixture.local(fixture.first);
    a.scale.x = 0;
    fixture.set(fixture.first, a);
    assert(
        !Batch::capture(fixture.snapshot(), fixture.first, selected, Pivot::Center, budget, batch));
    assert(std::string_view(batch.prepare(Tool::Scale, Space::Local, 0, 1, output))
        == "EDITOR-TRANSFORM-BATCH-SCALE-ZERO-REFERENCE");
    unchanged();
    assert(!batch.prepare(Tool::Scale, Space::Local, 0, 0, output));
    assert(Frame::same(change(output, fixture.first).afterLocal, a));
    std::puts(
        "PASS batch malformed snapshots, budgets, stale start, zero reference, late shear/range rejection and exact zero isolation");
}

void allocationAndParent()
{
    Fixture fixture;
    const auto snapshot = fixture.snapshot();
    const auto selected = fixture.selection();
    Batch batch;
    assert(!Batch::capture(snapshot, fixture.first, selected, Pivot::Active, budget, batch));
    const auto* before = batch.members().data();
    core::pmr::Vector<Batch::Change> output(1);
    output.front().objectId = scene::GameObjectId::fromValue(999);
    const auto* oldOutput = output.data();
    auto quota = core::makeShared<core::CpuMemoryQuota>(1);
    {
        core::CpuMemoryQuotaScope scope(quota);
        assert(std::string_view(
                   Batch::capture(snapshot, fixture.first, selected, Pivot::Center, budget, batch))
            == "EDITOR-TRANSFORM-BATCH-ALLOCATION");
        assert(batch.members().data() == before && batch.pivot() == Pivot::Active);
        assert(std::string_view(batch.prepare(Tool::Move, Space::World, 0, 1, output))
            == "EDITOR-TRANSFORM-BATCH-ALLOCATION");
    }
    assert(!quota->liveBytes() && output.data() == oldOutput
        && output.front().objectId.value() == 999);
    auto& parent = fixture.scene.createObject("parent");
    scene::Transform parentPose;
    parentPose.rotation = core::Quat::fromAxisAngle({0, 0, 1}, quarterTurn);
    parentPose.scale = {-2, 3, -4};
    parent.set_local_transform(parentPose);
    assert(fixture.scene.reparent(fixture.second, parent.id()).ok());
    assert(
        !Batch::capture(fixture.snapshot(), fixture.first, selected, Pivot::Active, budget, batch));
    assert(!batch.prepare(Tool::Move, Space::World, 0, 3, output));
    auto world = fixture.snapshot().requireNode(fixture.second).worldTransform;
    world.translation.x += 3;
    const auto expected = scene::deriveLocalTransform(parentPose, world);
    assert(close(change(output, fixture.second).afterLocal.translation, expected.translation));
    parentPose.scale.x = Frame::minimumParentScale;
    parent.set_local_transform(parentPose);
    assert(
        !Batch::capture(fixture.snapshot(), fixture.first, selected, Pivot::Active, budget, batch));
    const auto saved = output;
    assert(std::string_view(batch.prepare(Tool::Move, Space::World, 0, 1, output))
        == "EDITOR-TRANSFORM-PARENT-SINGULAR");
    assert(output.size() == saved.size()
        && Frame::same(output.back().afterLocal, saved.back().afterLocal));
    std::puts(
        "PASS batch allocation failure and parent signed/nonuniform rotation; singular late member rejects atomically");
}

void lifetime()
{
    auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned cycle = 0; cycle <= 20; ++cycle) {
        {
            Fixture fixture;
            const auto snapshot = fixture.snapshot();
            const std::array selected{fixture.first, fixture.second, fixture.child};
            Batch batch;
            assert(
                !Batch::capture(snapshot, fixture.child, selected, Pivot::Center, budget, batch));
            core::pmr::Vector<Batch::Change> output;
            assert(!batch.prepare(Tool::Rotate, Space::World, 2, 0.25, output));
            assert(batch.matchesStart(snapshot, fixture.child, selected) && output.size() == 2);
            for (const auto& value : output)
                fixture.set(value.objectId, value.afterLocal);
            assert(fixture.snapshot().ok());
        }
        const auto current = core::defaultCpuAllocator().stats();
        if (!cycle)
            baseline = current;
        assert(current.liveBytes == baseline.liveBytes
            && current.allocationCount - current.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts(
        "PASS 20 warmed ordinary native Scene/batch capture/prepare/destruction cycles: core live bytes/count delta=0");
}
} // namespace

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--smoke")
        lifetime();
    else {
        pivotsAndHierarchy();
        rotatedAndNegative();
        failureAndStaleness();
        allocationAndParent();
    }
}

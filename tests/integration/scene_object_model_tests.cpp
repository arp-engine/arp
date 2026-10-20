#include "scene/scene.h"

#include <cassert>
#include <string>
#include <type_traits>
#include <vector>

namespace {

using namespace arp::scene;

bool nearlyEqual(float lhs, float rhs)
{
    const auto delta = lhs - rhs;
    return delta > -0.0001f && delta < 0.0001f;
}

void assertVec3(arp::core::Vec3 actual, arp::core::Vec3 expected)
{
    assert(nearlyEqual(actual.x, expected.x));
    assert(nearlyEqual(actual.y, expected.y));
    assert(nearlyEqual(actual.z, expected.z));
}

class RecordingComponent final : public Component {
public:
    RecordingComponent(std::string label, std::vector<std::string>* log)
        : label_(std::move(label)), log_(log)
    {}

    void onEnable(const SceneUpdateContext&) override { log("enable"); }

    void onStart(const SceneUpdateContext&) override { log("start"); }

    void onUpdate(const SceneUpdateContext&) override { log("update"); }

    void onLateUpdate(const SceneUpdateContext&) override { log("late_update"); }

    void onDisable(const SceneUpdateContext&) override { log("disable"); }

    void onDestroy(const SceneUpdateContext&) override { log("destroy"); }

private:
    void log(const char* event) { log_->push_back(label_ + ":" + event); }

    std::string label_;
    std::vector<std::string>* log_ = nullptr;
};

SceneTraversalSnapshot extractSnapshot(Scene& scene)
{
    SceneTraversalOptions options;
    options.maxNodeCount = 16;
    auto snapshot = scene.extractTraversalSnapshot(options);
    assert(snapshot.ok());
    return snapshot;
}

void scene_root_satisfies_game_object_contract()
{
    static_assert(
        std::is_base_of<GameObject, Scene>::value, "Scene must inherit the GameObject contract");

    Scene scene("contract_scene");
    GameObject& root = scene;

    assert(scene.sceneId().valid());
    assert(root.id().valid());
    assert(scene.rootId() == root.id());
    assert(!root.parentId().valid());
    assert(root.active());
    assert(root.activeInHierarchy());

    TransformComponent& transform = scene.transform();
    Component& asComponent = transform;
    assert(asComponent.id().valid());
    assert(asComponent.ownerId() == scene.rootId());
    assert(asComponent.enabled());
    assert(scene.componentIds().size() == 1);
    assert(scene.componentIds()[0] == transform.id());

    scene.transform().setLocalTranslation({2.0f, 3.0f, 4.0f});
    const auto snapshot = extractSnapshot(scene);
    assert(snapshot.nodes().size() == 1);
    assert(snapshot.nodes()[0].objectId == scene.rootId());
    assertVec3(snapshot.nodes()[0].localTransform.translation, {2.0f, 3.0f, 4.0f});
    assertVec3(snapshot.nodes()[0].worldTransform.translation, {2.0f, 3.0f, 4.0f});
}

void hierarchy_ids_order_and_transforms_are_deterministic()
{
    Scene scene("hierarchy_scene");

    auto& parent = scene.createObject("parent");
    auto& childA = scene.createObject("child_a");
    auto& grandchild = scene.createObject("grandchild");
    auto& childB = scene.createObject("child_b");

    const auto parentId = parent.id();
    const auto childAId = childA.id();
    const auto childBId = childB.id();
    const auto grandchildId = grandchild.id();
    const auto parentTransformId = parent.transform().id();

    assert(scene.reparent(parentId, scene.rootId()).ok());
    assert(scene.reparent(childAId, parentId).ok());
    assert(scene.reparent(grandchildId, childAId).ok());
    assert(scene.reparent(childBId, parentId).ok());

    assert(parent.id() == parentId);
    assert(parent.transform().id() == parentTransformId);
    assert(parent.parentId() == scene.rootId());
    assert(parent.childIds().size() == 2);
    assert(parent.childIds()[0] == childAId);
    assert(parent.childIds()[1] == childBId);

    scene.transform().setLocalTranslation({1.0f, 0.0f, 0.0f});
    parent.transform().setLocalTranslation({0.0f, 2.0f, 0.0f});
    childA.transform().setLocalTranslation({0.0f, 0.0f, 3.0f});
    grandchild.transform().setLocalTranslation({4.0f, 0.0f, 0.0f});
    childB.transform().setLocalTranslation({0.0f, 5.0f, 0.0f});

    auto snapshot = extractSnapshot(scene);
    assert(snapshot.nodes().size() == 5);
    assert(snapshot.nodes()[0].objectId == scene.rootId());
    assert(snapshot.nodes()[1].objectId == parentId);
    assert(snapshot.nodes()[2].objectId == childAId);
    assert(snapshot.nodes()[3].objectId == grandchildId);
    assert(snapshot.nodes()[4].objectId == childBId);
    assertVec3(snapshot.requireNode(grandchildId).worldTransform.translation, {5.0f, 2.0f, 3.0f});

    const auto setWorld =
        scene.setWorldTransform(grandchildId, Transform::fromTranslation({9.0f, 8.0f, 7.0f}));
    assert(setWorld.ok());
    assertVec3(grandchild.transform().localTransform().translation, {8.0f, 6.0f, 4.0f});

    snapshot = extractSnapshot(scene);
    assertVec3(snapshot.requireNode(grandchildId).worldTransform.translation, {9.0f, 8.0f, 7.0f});
}

void active_component_and_diagnostic_contracts_are_deterministic()
{
    Scene scene("diagnostic_scene");
    auto& parent = scene.createObject("parent");
    auto& child = scene.createObject("child");
    assert(scene.reparent(parent.id(), scene.rootId()).ok());
    assert(scene.reparent(child.id(), parent.id()).ok());

    child.transform().setEnabled(false);
    assert(!child.transform().enabled());
    child.transform().setEnabled(true);

    parent.setActive(false);
    auto snapshot = extractSnapshot(scene);
    assert(!snapshot.requireNode(parent.id()).activeInHierarchy);
    assert(!snapshot.requireNode(child.id()).activeInHierarchy);
    parent.setActive(true);

    const auto invalidObject = scene.reparent(GameObjectId::fromValue(9001), scene.rootId());
    assert(!invalidObject.ok());
    assert(invalidObject.diagnostic().code == SceneDiagnosticCode::InvalidObjectId);

    const auto invalidParent = scene.reparent(child.id(), GameObjectId::fromValue(9002));
    assert(!invalidParent.ok());
    assert(invalidParent.diagnostic().code == SceneDiagnosticCode::InvalidParentId);

    const auto rootReparent = scene.reparent(scene.rootId(), parent.id());
    assert(!rootReparent.ok());
    assert(rootReparent.diagnostic().code == SceneDiagnosticCode::CannotReparentSceneRoot);

    const auto cycle = scene.reparent(parent.id(), child.id());
    assert(!cycle.ok());
    assert(cycle.diagnostic().code == SceneDiagnosticCode::HierarchyCycle);

    const auto destroyInvalid = scene.destroyObject(GameObjectId::invalid());
    assert(!destroyInvalid.ok());
    assert(destroyInvalid.diagnostic().code == SceneDiagnosticCode::InvalidObjectId);
}

void lifecycle_runs_in_stable_root_to_leaf_order()
{
    Scene scene("lifecycle_scene");
    auto& child = scene.createObject("child");
    assert(scene.reparent(child.id(), scene.rootId()).ok());

    std::vector<std::string> log;
    scene.addComponent<RecordingComponent>("root", &log);
    child.addComponent<RecordingComponent>("child", &log);

    SceneUpdateOptions options;
    options.deltaSeconds = 1.0f / 60.0f;
    options.frameIndex = 1;
    options.maxObjectCount = 8;

    const auto firstUpdate = scene.update(options);
    assert(firstUpdate.ok());
    assert(firstUpdate.visitedObjectCount() == 2);
    const std::vector<std::string> expectedFirst{
        "root:enable",
        "child:enable",
        "root:start",
        "child:start",
        "root:update",
        "child:update",
        "root:late_update",
        "child:late_update",
    };
    assert(log == expectedFirst);

    log.clear();
    child.setActive(false);
    options.frameIndex = 2;
    const auto disableUpdate = scene.update(options);
    assert(disableUpdate.ok());
    const std::vector<std::string> expectedDisable{
        "child:disable",
        "root:update",
        "root:late_update",
    };
    assert(log == expectedDisable);

    log.clear();
    const auto destroy = scene.destroyObject(child.id());
    assert(destroy.ok());
    const std::vector<std::string> expectedDestroy{
        "child:destroy",
    };
    assert(log == expectedDestroy);
}

} // namespace

int main()
{
    scene_root_satisfies_game_object_contract();
    hierarchy_ids_order_and_transforms_are_deterministic();
    active_component_and_diagnostic_contracts_are_deterministic();
    lifecycle_runs_in_stable_root_to_leaf_order();
    return 0;
}

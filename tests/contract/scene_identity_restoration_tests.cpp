#include "scene/scene.h"

#include <cassert>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {
using namespace arp;
using Transaction = scene::Scene::IdentityRestoration;
using ObjectId = scene::GameObjectId;
using ComponentId = scene::ComponentId;

class CandidateComponent final : public scene::Component {
public:
    const char* scriptTypeName() const override { return "CandidateComponent"; }

    void onEnable(const scene::SceneUpdateContext&) override { ++hooks; }

    void onDestroy(const scene::SceneUpdateContext&) override { ++hooks; }

    static inline unsigned hooks = 0;
};

scene::Scene::ComponentFactory factory()
{
    return
        [](scene::GameObject&, const core::pmr::String& type) -> core::UniquePtr<scene::Component> {
            return type == "CandidateComponent" ? core::makeUnique<CandidateComponent>() : nullptr;
        };
}

template <class F> void rejects(F&& f)
{
    bool failed = false;
    try {
        f();
    } catch (const std::logic_error&) {
        failed = true;
    }
    assert(failed);
}

void identities()
{
    scene::Scene world(
        "identity", scene::SceneId::fromValue(1), scene::SceneExecutionMode::Passive);
    auto& before = world.createObject("before");
    const auto objectId = ObjectId::fromValue(77), childId = ObjectId::fromValue(78);
    const auto transformId = ComponentId::fromValue(101), componentId = ComponentId::fromValue(103);
    reflection::object oldObject, oldTransform, oldComponent;
    for (unsigned round = 0; round < 2; ++round) {
        {
            Transaction transaction(world, {2, 4}, factory());
            rejects([&] {
                Transaction nested(world, {1, 1});
            });
            rejects([&] {
                scene::Scene::RestorationTransaction legacy(world);
            });
            rejects([&] {
                world.createObject("conflict");
            });
            rejects([&] {
                world.destroyObject(before.id());
            });
            rejects([&] {
                world.reparent(before.id(), world.rootId());
            });
            rejects([&] {
                before.addComponent<CandidateComponent>();
            });
            auto& restored =
                transaction.restoreObject(objectId, transformId, "restored", world.rootId(), 0);
            auto& child = transaction.restoreObject(
                childId, ComponentId::fromValue(102), "child", objectId, 0);
            auto& component =
                transaction.restoreComponent(objectId, componentId, "CandidateComponent", 1);
            restored.setActive(false);
            assert(!world.findObject(objectId) && !world.findObject(childId));
            assert(transaction.findCandidate(objectId) == &restored);
            assert(world.childIds().size() == 1 && world.childIds()[0] == before.id());
            assert(!oldObject.native_reference_alive() && !oldComponent.native_reference_alive());
            transaction.prepare();
            auto quota = core::makeShared<core::CpuMemoryQuota>(1);
            {
                core::CpuMemoryQuotaScope scope(quota);
                transaction.commit();
            }
            assert(world.findObject(objectId) == &restored);
            assert(world.findObject(childId) == &child);
            assert(world.childIds()[0] == objectId && world.childIds()[1] == before.id());
            assert(restored.childIds().size() == 1 && restored.childIds()[0] == childId);
            assert(restored.componentIds()[0] == transformId
                && restored.componentIds()[1] == componentId);
            assert(component.ownerId() == objectId && component.gameObject() == &restored);
            assert(!restored.active());
            oldObject = restored.nativeReflectionObject();
            oldTransform = restored.transform().nativeReflectionObject();
            oldComponent = component.nativeReflectionObject();
        }
        assert(world.destroyObject(objectId).ok());
        assert(!oldObject.native_reference_alive() && !oldTransform.native_reference_alive()
            && !oldComponent.native_reference_alive());
    }
    // Restoration on a live owner also stays invisible until the single commit.
    {
        Transaction transaction(world, {0, 1}, factory());
        auto& component = transaction.restoreComponent(
            before.id(), ComponentId::fromValue(104), "CandidateComponent", 1);
        assert(!before.findComponent(component.id()));
        transaction.commit();
        assert(before.findComponent(component.id()) == &component);
    }
    auto& next = world.createObject("next");
    assert(next.id().value() > childId.value() && next.transform().id().value() > 104);
    assert(CandidateComponent::hooks == 0);
}

void invalidAndRollback()
{
    scene::Scene runtime;
    rejects([&] {
        Transaction transaction(runtime, {1, 2});
    });
    scene::Scene world(
        "negative", scene::SceneId::fromValue(2), scene::SceneExecutionMode::Passive);
    auto& existing = world.createObject("existing");
    const auto test = [&](auto&& request) {
        Transaction transaction(world, {2, 3}, factory());
        rejects([&] {
            request(transaction);
        });
        rejects([&] {
            transaction.commit();
        });
        assert(world.objects().size() == 1 && world.childIds().size() == 1);
    };
    const auto oid = ObjectId::fromValue(55);
    const auto cid = ComponentId::fromValue(55);
    for (auto id : {ObjectId{}, world.rootId(), existing.id(), ObjectId::fromValue(UINT64_MAX)})
        test([&](auto& t) {
            t.restoreObject(id, cid, "bad", world.rootId(), 0);
        });
    for (auto id : {ComponentId{},
             world.transform().id(),
             existing.transform().id(),
             ComponentId::fromValue(UINT64_MAX)})
        test([&](auto& t) {
            t.restoreObject(oid, id, "bad", world.rootId(), 0);
        });
    test([&](auto& t) {
        t.restoreObject(oid, cid, "bad", oid, 0);
    });
    test([&](auto& t) {
        t.restoreObject(oid, cid, "bad", world.rootId(), 2);
    });
    test([&](auto& t) {
        t.restoreComponent(existing.id(), cid, "unknown", 1);
    });
    test([&](auto& t) {
        t.restoreComponent(existing.id(), cid, "TransformComponent", 1);
    });
    test([&](auto& t) {
        t.restoreComponent(existing.id(), cid, "CandidateComponent", 0);
    });
    test([&](auto& t) {
        t.restoreComponent(oid, cid, "CandidateComponent", 1);
    });
    test([&](auto& t) {
        t.restoreObject(oid, cid, "first", world.rootId(), 0);
        t.restoreComponent(oid, cid, "CandidateComponent", 1);
    });
    test([&](auto& t) {
        t.restoreObject(oid, cid, "first", world.rootId(), 0);
        t.restoreObject(oid, ComponentId::fromValue(56), "duplicate", world.rootId(), 0);
    });
    reflection::object unpublished;
    {
        Transaction t(world, {1, 1});
        unpublished =
            t.restoreObject(oid, cid, "abandoned", world.rootId(), 0).nativeReflectionObject();
        assert(unpublished.native_reference_alive() && !world.findObject(oid));
    }
    assert(!unpublished.native_reference_alive());
    {
        Transaction t(world, {0, 0});
        rejects([&] {
            t.restoreObject(oid, cid, "budget", world.rootId(), 0);
        });
    }
    {
        scene::Scene::RestorationTransaction legacy(world);
        rejects([&] {
            Transaction t(world, {1, 1});
        });
        world.createObject("legacy rollback");
    }
    assert(world.objects().size() == 1);
    auto& next = world.createObject("next");
    assert(next.id().value() == existing.id().value() + 1);
    assert(next.transform().id().value() == existing.transform().id().value() + 1);

    // The last representable identity may be restored, but ordinary allocation
    // must fail deterministically instead of wrapping into zero/root/live IDs.
    scene::Scene exhausted(
        "overflow", scene::SceneId::fromValue(4), scene::SceneExecutionMode::Passive);
    {
        Transaction t(exhausted, {1, 1});
        t.restoreObject(ObjectId::fromValue(UINT64_MAX - 1),
            ComponentId::fromValue(UINT64_MAX - 1),
            "last",
            exhausted.rootId(),
            0);
        t.commit();
    }
    bool overflow = false;
    try {
        exhausted.createObject("overflow");
    } catch (const std::overflow_error&) {
        overflow = true;
    }
    assert(overflow && exhausted.objects().size() == 1);
}

void allocations()
{
    // Quotas cover candidate construction, registry nodes, and commit prepare.
    unsigned failures = 0, successes = 0;
    for (std::size_t bytes = 1; bytes <= 32768; bytes *= 2) {
        scene::Scene world(
            "quota", scene::SceneId::fromValue(3), scene::SceneExecutionMode::Passive);
        const auto factoryOwner = factory();
        auto quota = core::makeShared<core::CpuMemoryQuota>(bytes);
        bool committed = false;
        {
            core::CpuMemoryQuotaScope scope(quota);
            try {
                Transaction t(world, {2, 3}, factoryOwner);
                t.restoreObject(ObjectId::fromValue(40),
                    ComponentId::fromValue(40),
                    "candidate",
                    world.rootId(),
                    0);
                t.restoreComponent(
                    ObjectId::fromValue(40), ComponentId::fromValue(41), "CandidateComponent", 1);
                t.restoreObject(ObjectId::fromValue(41),
                    ComponentId::fromValue(42),
                    "child",
                    ObjectId::fromValue(40),
                    0);
                t.commit();
                committed = true;
            } catch (const std::bad_alloc&) {
                ++failures;
            }
        }
        if (committed) {
            ++successes;
            assert(world.objects().size() == 2);
        } else {
            assert(world.objects().empty() && world.childIds().empty());
            auto& next = world.createObject("retry");
            assert(next.id().value() == 2 && next.transform().id().value() == 2);
        }
    }
    assert(failures && successes);
}

void candidateWorldTransform()
{
    scene::Scene world(
        "candidate-transform", scene::SceneId::fromValue(1), scene::SceneExecutionMode::Passive);
    auto& parent = world.createObject("live-parent");
    parent.transform().setLocalTranslation({2, 3, 0});
    Transaction transaction(world, {2, 2});
    auto& a = transaction.createObject("candidate-a", parent.id(), 0);
    a.transform().setLocalTranslation({4, 5, 0});
    auto& b = transaction.createObject("candidate-b", a.id(), 0);
    b.transform().setLocalTranslation({6, 7, 0});
    scene::Transform result;
    assert(transaction.worldTransform(b.id(), result) && result.translation.x == 12
        && result.translation.y == 15);
    assert(!world.findObject(a.id()) && !world.findObject(b.id()));
    assert(!transaction.worldTransform(ObjectId::fromValue(UINT64_MAX), result)
        && result.translation.x == 12);
    transaction.commit();
    const auto published = b.world_transform();
    assert(published.translation.x == result.translation.x
        && published.translation.y == result.translation.y);
}

void run()
{
    candidateWorldTransform();
    identities();
    invalidAndRollback();
    allocations();
}
} // namespace

int main(int argc, char** argv)
{
    const bool smoke = argc == 2 && std::string_view(argv[1]) == "--smoke";
    core::CpuMemoryStats baseline;
    for (unsigned cycle = 0; cycle <= (smoke ? 20u : 0u); ++cycle) {
        run();
        const auto now = core::defaultCpuAllocator().stats();
        if (!cycle)
            baseline = now;
        assert(now.liveBytes == baseline.liveBytes);
        assert(now.allocationCount - now.deallocationCount
            == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts(smoke
            ? "PASS identity ordinary warmed20 cycles; core live bytes/count delta=0"
            : "PASS exact identity, invisible candidates, new controls, gates, quota rollback and allocation-free commit");
}

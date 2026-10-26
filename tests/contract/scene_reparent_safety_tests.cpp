#include "scene/scene.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <new>
#include <string_view>
#include <type_traits>

namespace {
using namespace arp;

// Observe the public hierarchy and every local TRS, not private vector capacity.
class HierarchyFixture final {
public:
    explicit HierarchyFixture(scene::SceneExecutionMode mode)
        : world("reparent safety", scene::SceneId::fromValue(92), mode)
    {
        objects[0] = &world;
        objects[1] = &world.createObject("old parent");
        objects[2] = &world.createObject("target parent");
        objects[3] = &world.createObject("before");
        objects[4] = &world.createObject("child");
        objects[5] = &world.createObject("after");
        for (std::size_t i = 3; i < objects.size(); ++i)
            assert(world.reparent(objects[i]->id(), objects[1]->id()).ok());
        for (std::size_t i = 0; i < objects.size(); ++i) {
            auto transform = scene::Transform::identity();
            transform.translation = {float(i), -float(i), float(i + 1)};
            transform.rotation = core::Quat::fromAxisAngle({0, 1, 0}, float(i) * 0.2f);
            transform.scale = {1, float(i + 1), 2};
            objects[i]->set_local_transform(transform);
        }
        remember();
    }

    void remember()
    {
        for (std::size_t i = 0; i < objects.size(); ++i) {
            parents[i] = objects[i]->parentId();
            children[i] = objects[i]->childIds();
            transforms[i] = objects[i]->local_transform();
        }
    }

    void verifyTransforms() const
    {
        for (std::size_t i = 0; i < objects.size(); ++i) {
            const auto actual = objects[i]->local_transform();
            const auto expected = transforms[i];
            const auto same = [](core::Vec3 a, core::Vec3 b) {
                return a.x == b.x && a.y == b.y && a.z == b.z;
            };
            assert(same(actual.translation, expected.translation));
            assert(same(actual.scale, expected.scale));
            assert(actual.rotation.x == expected.rotation.x
                && actual.rotation.y == expected.rotation.y
                && actual.rotation.z == expected.rotation.z
                && actual.rotation.w == expected.rotation.w);
        }
    }

    void verifyUnchanged() const
    {
        for (std::size_t i = 0; i < objects.size(); ++i) {
            assert(objects[i]->parentId() == parents[i]);
            assert(objects[i]->childIds() == children[i]);
        }
        verifyTransforms();
    }

    void allocationAndRetry()
    {
        const auto child = objects[4]->id(), old = objects[1]->id(), target = objects[2]->id();
        auto quota = core::makeShared<core::CpuMemoryQuota>(1);
        bool rejected = false;
        {
            core::CpuMemoryQuotaScope scope(quota);
            try {
                (void)world.reparent(child, target, 0);
            } catch (const std::bad_alloc&) {
                rejected = true;
            }
        }
        assert(rejected);
        verifyUnchanged();
        assert(world.reparent(child, target, 0).ok());
        assert(objects[4]->parentId() == target);
        assert((objects[1]->childIds()
            == core::pmr::Vector<scene::GameObjectId>{objects[3]->id(), objects[5]->id()}));
        assert(objects[2]->childIds().size() == 1 && objects[2]->childIds()[0] == child);
        // Both lists now retain sufficient capacity: the commit must not allocate.
        {
            core::CpuMemoryQuotaScope scope(quota);
            assert(world.reparent(child, old, 1).ok());
            assert(world.reparent(child, target).ok());
            assert(world.reparent(child, old, 1).ok());
            assert(world.reparent(child, old).ok());
        }
        verifyUnchanged();
        {
            core::CpuMemoryQuotaScope scope(quota);
            assert(world.reparent(child, old, 2).ok());
            assert(objects[1]->childIds()[2] == child);
            assert(world.reparent(child, old, 2).ok());
            assert(world.reparent(child, old, 0).ok());
            assert(objects[1]->childIds()[0] == child);
            assert(world.reparent(child, old, 1).ok());
        }
        verifyUnchanged();

        // Also fail growth of a populated target through the append overload.
        assert(world.reparent(objects[3]->id(), target).ok());
        remember();
        rejected = false;
        {
            core::CpuMemoryQuotaScope scope(quota);
            try {
                (void)world.reparent(child, target);
            } catch (const std::bad_alloc&) {
                rejected = true;
            }
        }
        assert(rejected);
        verifyUnchanged();
        assert(world.reparent(child, target).ok());
        assert((objects[2]->childIds()
            == core::pmr::Vector<scene::GameObjectId>{objects[3]->id(), child}));
        assert(objects[1]->childIds().size() == 1 && objects[1]->childIds()[0] == objects[5]->id());
        verifyTransforms();
    }

    void invalidRequests()
    {
        const auto child = objects[4]->id(), old = objects[1]->id(), target = objects[2]->id();
        const auto missing = scene::GameObjectId::fromValue(99999);
        const auto reject = [&](scene::GameObjectId object,
                                scene::GameObjectId parent,
                                std::size_t index,
                                scene::SceneDiagnosticCode diagnostic) {
            const auto result = world.reparent(object, parent, index);
            assert(!result.ok() && result.diagnostic().code == diagnostic);
            verifyUnchanged();
        };
        reject(child, target, 1, scene::SceneDiagnosticCode::InvalidChildIndex);
        reject(child, old, 3, scene::SceneDiagnosticCode::InvalidChildIndex);
        reject(world.rootId(), target, 0, scene::SceneDiagnosticCode::CannotReparentSceneRoot);
        reject(missing, target, 0, scene::SceneDiagnosticCode::InvalidObjectId);
        reject(child, missing, 0, scene::SceneDiagnosticCode::InvalidParentId);
        reject(child, child, 0, scene::SceneDiagnosticCode::HierarchyCycle);
        reject(old, child, 0, scene::SceneDiagnosticCode::HierarchyCycle);
        assert(world.reparent(child, missing).diagnostic().code
            == scene::SceneDiagnosticCode::InvalidParentId);
        verifyUnchanged();
    }

private:
    scene::Scene world;
    std::array<scene::GameObject*, 6> objects{};
    std::array<scene::GameObjectId, 6> parents{};
    std::array<core::pmr::Vector<scene::GameObjectId>, 6> children{};
    std::array<scene::Transform, 6> transforms{};
};

void run()
{
    static_assert(std::is_nothrow_move_constructible_v<scene::SceneResult>);
    for (auto mode : {scene::SceneExecutionMode::Passive, scene::SceneExecutionMode::Runtime}) {
        HierarchyFixture fixture(mode);
        fixture.invalidRequests();
        fixture.allocationAndRetry();
    }
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
            ? "PASS 20 warmed Runtime/Passive failure/retry cycles; core live bytes/count delta=0"
            : "PASS Runtime/Passive reparent allocation atomicity, exact order/TRS and diagnostics");
}

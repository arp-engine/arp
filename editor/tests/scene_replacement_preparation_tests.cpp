#include "2d/physics_components.h"
#include "editor/workspace/editor_scene_document.h"
#include "resource/resource_contract.h"

#include <cassert>
#include <cstdio>
#include <string_view>
#include <utility>

namespace {
using namespace arp;
using Document = editor::EditorSceneDocument;
using Result = Document::ReplacementResult;

// Track destruction independently of the core allocation counters.
class RetainedComponent final : public scene::Component {
public:
    explicit RetainedComponent(unsigned& destroyed) : destroyed_(destroyed) {}

    ~RetainedComponent() override { ++destroyed_; }

private:
    unsigned& destroyed_;
};

void replacement()
{
    unsigned destroyed = 0, resourceCalls = 0, physicsCalls = 0;
    resource::ResourceManager resources;
    const physics2d::WorldConfig config{{0, -9.8f}, 8, 8, 4, 8, 16, 8, 8, 4, 1.f / 60.f};
    auto world = core::makeShared<physics2d::PhysicsWorld2D>(config);
    assert(world->valid());
    core::pmr::String diagnostic;
    Document live(false);
    live.setResourceManagerProvider([&] {
        ++resourceCalls;
        return &resources;
    });
    live.setPhysicsWorld2DProvider([&] {
        ++physicsCalls;
        return world;
    });
    const auto oldSelection = live.createObject("retired");
    live.scene().findObject(oldSelection)->addComponent<RetainedComponent>(destroyed);
    const auto oldIdentity = resource::deriveResourceUuid("replacement-test", "old");
    const auto newIdentity = resource::deriveResourceUuid("replacement-test", "new");
    assert(live.setRuntimeSceneId(oldIdentity));
    auto* oldScene = &live.scene();
    const auto oldEpoch = live.ownerTicket();
    {
        Document candidate(false);
        candidate.setResourceManagerProvider([]() -> resource::ResourceManager* {
            return nullptr;
        });
        candidate.setSchemaOnlyComponentCreation(true);
        const auto selection = candidate.createObject("incoming");
        candidate.scene().findObject(selection)->addComponent<RetainedComponent>(destroyed);
        assert(candidate.setRuntimeSceneId(newIdentity));
        auto* newScene = &candidate.scene();
        const auto candidateEpoch = candidate.ownerTicket();
        auto prepared = live.prepareReplacement(candidate, diagnostic);
        assert(prepared && diagnostic.empty() && !prepared->committed());
        assert(&live.scene() == oldScene && &candidate.scene() == newScene && oldEpoch.isCurrent()
            && candidateEpoch.isCurrent());
        const auto before = core::defaultCpuAllocator().stats();
        assert(live.replacementReadiness(*prepared, candidate) == Result::Ready);
        assert(live.commitReplacement(*prepared, candidate) == Result::Committed);
        const auto after = core::defaultCpuAllocator().stats();
        assert(before.allocationCount == after.allocationCount);
        assert(!destroyed && !resourceCalls && !physicsCalls && !oldEpoch.isCurrent()
            && !candidateEpoch.isCurrent() && prepared->committed());
        assert(&live.scene() == newScene && &candidate.scene() == oldScene
            && live.selectedObject() == selection && candidate.selectedObject() == oldSelection
            && live.runtimeSceneId() == newIdentity && candidate.runtimeSceneId() == oldIdentity);
        assert(live.commitReplacement(*prepared, candidate) == Result::Consumed);
        // Both sides must use their current document, audio and provider state.
        auto* incomingBody = dynamic_cast<physics2d::RigidBody2DComponent*>(
            live.addComponent("RigidBody2DComponent"));
        auto* retiredBody = dynamic_cast<physics2d::RigidBody2DComponent*>(
            candidate.addComponent("RigidBody2DComponent"));
        // Passive documents retain the correct service owner and descriptors,
        // but the approved Authoring mode must never create simulation bodies.
        assert(incomingBody && !incomingBody->handle() && incomingBody->world() == world.get()
            && incomingBody->usage() == physics2d::ComponentUsage::Authoring && retiredBody
            && !retiredBody->handle() && retiredBody->world() == world.get()
            && retiredBody->usage() == physics2d::ComponentUsage::Authoring
            && world->telemetry().bodies == 0 && resourceCalls == 2 && physicsCalls == 2);
        assert(candidate.resourceManagerProvider()() == &resources
            && candidate.physicsWorld2DProvider()() == world);
        assert(live.addComponent("AudioSourceComponent")
            && candidate.addComponent("AudioSourceComponent"));
        std::printf(
            "PASS commit core allocations=0, callback deallocations=%llu, no provider/factory invocation or component destruction\n",
            static_cast<unsigned long long>(after.deallocationCount - before.deallocationCount));
    }
    assert(destroyed == 1 && world->telemetry().bodies == 0);
    // The incoming factory must survive the original candidate and its token.
    assert(live.addComponent("CameraComponent"));
    std::puts(
        "PASS coherent Scene/audio/identity/selection retirement and correctly rebound factories");
}

void invalidAndStale()
{
    Document live(false), candidate(false), other(false);
    core::pmr::String diagnostic;
    Document::PreparedReplacement incomplete;
    assert(live.commitReplacement(incomplete, candidate) == Result::Incomplete);
    assert(!live.prepareReplacement(live, diagnostic));
    auto prepared = live.prepareReplacement(candidate, diagnostic);
    assert(prepared && live.commitReplacement(*prepared, other) == Result::Stale
        && other.commitReplacement(*prepared, candidate) == Result::Stale
        && live.replacementReadiness(*prepared, candidate) == Result::Ready);
    auto epoch = live.ownerTicket();
    prepared.reset();
    assert(epoch.isCurrent());
    const auto reject = [&](auto mutate) {
        auto token = live.prepareReplacement(candidate, diagnostic);
        assert(token);
        auto* oldScene = &live.scene();
        auto* coldScene = &candidate.scene();
        mutate();
        assert(live.commitReplacement(*token, candidate) == Result::Stale
            && &live.scene() == oldScene && &candidate.scene() == coldScene && !token->committed());
    };
    reject([&] {
        live.setResourceManagerProvider(nullptr);
    });
    reject([&] {
        candidate.setPhysicsWorld2DProvider(nullptr);
    });
    reject([&] {
        candidate.setSchemaOnlyComponentCreation(true);
    });
    reject([&] {
        assert(candidate.setRuntimeSceneId(resource::deriveResourceUuid("test", "other")));
    });
    reject([&] {
        candidate.createObject("changed selection");
    });
    reject([&] {
        live.createObject("changed live selection");
    });
    reject([&] {
        assert(candidate.destroySelected());
    });
    Document newer(false);
    assert(candidate.replaceWith(std::move(newer)));
    auto replacement = live.prepareReplacement(candidate, diagnostic);
    Document identical(false);
    assert(candidate.replaceWith(std::move(identical)));
    assert(live.commitReplacement(*replacement, candidate) == Result::Stale);
    // This primitive intentionally observes ownership/scalars, not all content.
    auto content = live.prepareReplacement(candidate, diagnostic);
    candidate.scene().createObject("direct content requires outer admission");
    assert(live.replacementReadiness(*content, candidate) == Result::Ready);
    std::puts(
        "PASS abandoned, invalid, cross-owner, stale epoch/scalar and same-content replacement rejection; outer content gate remains required");
}

void movementAndDestruction()
{
    core::pmr::String diagnostic;
    Document live(false), other(false);
    core::UniquePtr<Document::PreparedReplacement> token;
    {
        auto candidate = core::makeUnique<Document>(false);
        token = live.prepareReplacement(*candidate, diagnostic);
        Document moved(std::move(*candidate));
        candidate.reset();
        // A ticket can remain current in 'moved'. No saved pointer is read.
        assert(live.commitReplacement(*token, moved) == Result::Stale);
    }
    assert(live.commitReplacement(*token, other) == Result::Stale);
    {
        auto destination = core::makeUnique<Document>(false);
        token = destination->prepareReplacement(other, diagnostic);
        Document moved(std::move(*destination));
        assert(!destination->prepareReplacement(other, diagnostic));
        destination.reset();
        assert(moved.commitReplacement(*token, other) == Result::Stale);
    }
    token.reset();
    Document emptyAfterMove(false);
    Document moved(std::move(emptyAfterMove));
    Document incoming(false);
    const auto selection = incoming.createObject("legacy wrapper");
    assert(emptyAfterMove.replaceWith(std::move(incoming))
        && emptyAfterMove.selectedObject() == selection
        && emptyAfterMove.addComponent("CameraComponent"));
    std::puts(
        "PASS moved/destroyed operands reject without dereferencing saved addresses; moved-from combined API remains usable");
}

void allocationFailure()
{
    Document live(false), candidate(false);
    core::pmr::String diagnostic;
    diagnostic.reserve(128);
    const auto identity = resource::deriveResourceUuid("replacement-test", "quota");
    assert(candidate.setRuntimeSceneId(identity));
    const auto epoch = live.ownerTicket(), candidateEpoch = candidate.ownerTicket();
    const auto* original = &live.scene();
    const auto* cold = &candidate.scene();
    auto quota = core::makeShared<core::CpuMemoryQuota>();
    {
        core::CpuMemoryQuotaScope scope(quota);
        auto token = live.prepareReplacement(candidate, diagnostic);
        assert(token);
    }
    assert(quota->peakBytes() > sizeof(Document::PreparedReplacement) && !quota->liveBytes());
    // One byte below the measured complete preparation injects a late failure.
    quota->setLimitBytes(quota->peakBytes() - 1);
    {
        core::CpuMemoryQuotaScope scope(quota);
        assert(!live.prepareReplacement(candidate, diagnostic)
            && diagnostic == "EDITOR-SCENE-REPLACEMENT-PREPARATION-FAILED");
    }
    assert(!quota->liveBytes() && epoch.isCurrent() && candidateEpoch.isCurrent()
        && &live.scene() == original && &candidate.scene() == cold
        && candidate.runtimeSceneId() == identity);
    assert(live.addComponent("CameraComponent") && candidate.addComponent("CameraComponent"));
    std::puts(
        "PASS bounded late allocation failure cleans partial preparation and preserves both documents/factories/epochs");
}

void smoke()
{
    auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned cycle = 0; cycle <= 20; ++cycle) {
        {
            replacement();
            allocationFailure();
            Document live(false), candidate(false);
            core::pmr::String diagnostic;
            auto abandoned = live.prepareReplacement(candidate, diagnostic);
            assert(abandoned);
        }
        const auto current = core::defaultCpuAllocator().stats();
        if (!cycle)
            baseline = current;
        assert(current.liveBytes == baseline.liveBytes
            && current.allocationCount - current.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts(
        "PASS 20 warmed ordinary prepared/committed/abandoned destruction cycles: core live bytes/count delta=0");
}
} // namespace

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--smoke") {
        smoke();
    } else {
        replacement();
        invalidAndStale();
        movementAndDestruction();
        allocationFailure();
    }
}

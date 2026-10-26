#include "3d/animator_component.h"
#include "editor/tests/fixtures/project_fixture.h"
#include "editor/tests/fixtures/recovery_fixture.h"
#include "editor/workspace/editor_project_resources.h"

#include <cassert>
#include <cstdio>

namespace {
using namespace arp;
using namespace arp::editor;

core::pmr::String manifest()
{
    return tests::ProjectFixture::document("assetPackages: []\n"
                                           "physics2d:\n"
                                           "  gravity: {x: 0, y: -9.8}\n"
                                           "  maxBodies: 32\n"
                                           "  maxColliders: 64\n"
                                           "  maxJoints: 16\n"
                                           "  maxNativeShapes: 128\n"
                                           "  maxEvents: 256\n"
                                           "  maxQueryResults: 128\n"
                                           "  maxChainPoints: 32\n"
                                           "  substeps: 4\n"
                                           "  fixedSeconds: 0.016666667\n");
}

void frozenOwners(const tests::RecoveryFixture& files)
{
    core::pmr::String diagnostic;
    auto document = core::makeUnique<EditorSceneDocument>(false);
    std::weak_ptr<resource::ResourceManager> oldManager;
    std::weak_ptr<physics2d::PhysicsWorld2D> oldWorld;
    core::UniquePtr<scene3d::AnimatorComponent> animator;
    {
        EditorProjectResources resources;
        assert(resources.loadManifest(files.root, manifest()).success);
        oldManager = resources.retainManager();
        oldWorld = resources.physicsWorld2D();
        const auto epoch = document->ownerTicket();
        assert(resources.bindSceneServices(*document, diagnostic) && diagnostic.empty());
        assert(!epoch.isCurrent());
        const auto object = document->createObject("Cold owner");
        assert(object.value() != 0 && document->addComponent("AnimatorComponent", object));
        // A real component can retain a copied callback after the document dies.
        animator = core::makeUnique<scene3d::AnimatorComponent>(
            scene3d::AnimatorComponentDesc{}, document->resourceManagerProvider());
        EditorProjectResources moved(std::move(resources));
        assert(moved.loadManifest(files.root, manifest()).success);
        assert(document->resourceManagerProvider()() == oldManager.lock().get()
            && document->resourceManagerProvider()() != &moved.manager()
            && document->physicsWorld2DProvider()() == oldWorld.lock()
            && document->physicsWorld2DProvider()() != moved.physicsWorld2D());
        const auto ticket = document->ownerTicket();
        assert(!resources.bindSceneServices(*document, diagnostic)
            && diagnostic == "EDITOR-SCENE-SERVICES-OWNER-UNAVAILABLE"
            && ticket.sameCurrentEpoch(document->ownerTicket()));
    }
    assert(!oldManager.expired() && !oldWorld.expired());
    assert(document->resourceManagerProvider()() == oldManager.lock().get()
        && document->physicsWorld2DProvider()()->valid());
    document.reset();
    assert(!oldManager.expired() && oldWorld.expired());
    scene3d::AnimatorEditorProperties properties;
    properties.prefabResourceId = "4305ee93-3897-43ae-93da-ecbb0c569c83";
    // This invokes the real resource query after the original bundle/document
    // are gone. Missing content must reject; it must never switch managers.
    assert(!animator->applyEditorAuthoringProperties(reflection::object(properties)));
    animator.reset();
    assert(oldManager.expired());
}

void quotaFailure()
{
    EditorProjectResources resources;
    core::pmr::String diagnostic;
    diagnostic.reserve(128);
    auto quota = core::makeShared<core::CpuMemoryQuota>();
    {
        EditorSceneDocument measured(false);
        core::CpuMemoryQuotaScope scope(quota);
        assert(resources.bindSceneServices(measured, diagnostic));
    }
    assert(quota->peakBytes() && !quota->liveBytes());
    quota->setLimitBytes(quota->peakBytes() - 1);
    EditorProjectResources original;
    EditorSceneDocument unchanged(false);
    assert(original.bindSceneServices(unchanged, diagnostic));
    const auto ticket = unchanged.ownerTicket();
    const auto* manager = unchanged.resourceManagerProvider()();
    {
        core::CpuMemoryQuotaScope scope(quota);
        assert(!resources.bindSceneServices(unchanged, diagnostic)
            && diagnostic == "EDITOR-SCENE-SERVICES-PREPARATION-FAILED");
    }
    assert(!quota->liveBytes() && ticket.sameCurrentEpoch(unchanged.ownerTicket())
        && unchanged.resourceManagerProvider()() == manager
        && !unchanged.physicsWorld2DProvider()());
}

void smoke(const tests::RecoveryFixture& files)
{
    frozenOwners(files);
    quotaFailure();
    const auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned cycle = 0; cycle < 20; ++cycle) {
        frozenOwners(files);
        quotaFailure();
        const auto current = core::defaultCpuAllocator().stats();
        assert(current.liveBytes == baseline.liveBytes
            && current.allocationCount - current.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts("PASS 20 warmed ordinary frozen service owner cycles: core live bytes/count delta=0");
}
} // namespace

int main(int argc, char** argv)
{
    tests::RecoveryFixture files;
    if (argc == 2 && std::string_view(argv[1]) == "--smoke")
        smoke(files);
    else {
        frozenOwners(files);
        quotaFailure();
        std::puts(
            "PASS frozen manager/physics owners, move/reload/destruction, real late component query and atomic quota rejection");
    }
}

#include "2d/physics_components.h"
#include "2d/sprite_animation_component.h"
#include "core/hash/sha256.h"
#include "editor/project/project_scene_authoring.h"
#include "editor/tests/fixtures/project_fixture.h"
#include "editor/tests/fixtures/recovery_fixture.h"
#include "editor/workspace/editor_scene_service_session.h"
#include "editor/workspace/editor_scene_transaction.h"
#include "resource/resource_contract.h"

#include <cassert>
#include <cstdio>

namespace {
using namespace arp;
using namespace arp::editor;
using Document = EditorSceneDocument;
using Policy = Document::ServicePolicy;
using Result = Document::ReplacementResult;

core::SharedPtr<EditorSceneServiceSession> session()
{
    const physics2d::WorldConfig config{{0, -9.8f}, 8, 8, 4, 8, 16, 8, 8, 4, 1.f / 60.f};
    return core::makeShared<EditorSceneServiceSession>(
        core::makeShared<resource::ResourceManager>(),
        core::makeShared<physics2d::PhysicsWorld2D>(config));
}

void adoption(Policy policy)
{
    auto original = session(), incoming = session();
    Document live(false), candidate(false);
    core::pmr::String diagnostic;
    assert(live.bindServiceSession(original, diagnostic));
    assert(candidate.bindServiceSession(incoming, diagnostic));
    const auto oldObject = live.createObject("original");
    const auto newObject = candidate.createObject("incoming");
    auto* oldBody =
        dynamic_cast<physics2d::RigidBody2DComponent*>(live.addComponent("RigidBody2DComponent"));
    auto* newBody = dynamic_cast<physics2d::RigidBody2DComponent*>(
        candidate.addComponent("RigidBody2DComponent"));
    assert(oldBody && newBody && oldBody->world() == original->physics().get()
        && newBody->world() == incoming->physics().get());
    const auto oldCallback = live.resourceManagerProvider(),
               incomingCallback = candidate.resourceManagerProvider();
    candidate.setSchemaOnlyComponentCreation(true);
    auto prepared = live.prepareReplacement(candidate, diagnostic, policy);
    assert(prepared && live.replacementReadiness(*prepared, candidate) == Result::Ready);
    const auto before = core::defaultCpuAllocator().stats();
    assert(live.commitReplacement(*prepared, candidate) == Result::Committed);
    const auto after = core::defaultCpuAllocator().stats();
    assert(before.allocationCount == after.allocationCount);
    const auto expected = policy == Policy::AdoptCandidate ? incoming : original;
    assert(live.sceneServiceSession() == expected && candidate.sceneServiceSession() == original
        && oldCallback() == original->resources() && incomingCallback() == incoming->resources());
    assert(candidate.scene().findObject(oldObject)->getComponent<physics2d::RigidBody2DComponent>()
            == oldBody
        && live.scene().findObject(newObject)->getComponent<physics2d::RigidBody2DComponent>()
            == newBody);
    // Existing components keep the world from construction; factory additions
    // must use the policy's current document binding, not the former address.
    assert(oldBody->world() == original->physics().get()
        && newBody->world() == incoming->physics().get());
    assert(live.select(live.createObject("future")));
    auto* future =
        dynamic_cast<physics2d::RigidBody2DComponent*>(live.addComponent("RigidBody2DComponent"));
    assert(future && future->world() == expected->physics().get());
    assert(!future->handle() && future->usage() == physics2d::ComponentUsage::Authoring);
    assert(candidate.select(candidate.createObject("retired future")));
    auto* retired = dynamic_cast<physics2d::RigidBody2DComponent*>(
        candidate.addComponent("RigidBody2DComponent"));
    assert(retired && retired->world() == original->physics().get() && !retired->handle()
        && retired->usage() == physics2d::ComponentUsage::Authoring);
    assert(live.commitReplacement(*prepared, candidate) == Result::Consumed);
}

void rejectionAndQuota()
{
    auto original = session(), incoming = session();
    Document live(false), candidate(false);
    core::pmr::String diagnostic;
    diagnostic.reserve(128);
    assert(live.bindServiceSession(original, diagnostic));
    assert(candidate.bindServiceSession(incoming, diagnostic));
    const auto ticket = live.ownerTicket();
    assert(!live.bindServiceSession({}, diagnostic)
        && diagnostic == "EDITOR-SCENE-SERVICE-SESSION-INVALID" && ticket.isCurrent());
    auto prepared = live.prepareReplacement(candidate, diagnostic, Policy::AdoptCandidate);
    assert(prepared);
    candidate.setResourceManagerProvider(nullptr);
    assert(!candidate.sceneServiceSession()
        && live.commitReplacement(*prepared, candidate) == Result::Stale);
    assert(!live.prepareReplacement(candidate, diagnostic, static_cast<Policy>(255)));
    auto quota = core::makeShared<core::CpuMemoryQuota>();
    {
        Document measured(false);
        core::CpuMemoryQuotaScope scope(quota);
        assert(measured.bindServiceSession(incoming, diagnostic));
    }
    assert(quota->peakBytes() && !quota->liveBytes());
    quota->setLimitBytes(quota->peakBytes() - 1);
    {
        core::CpuMemoryQuotaScope scope(quota);
        assert(!live.bindServiceSession(incoming, diagnostic)
            && diagnostic == "EDITOR-SCENE-SERVICE-SESSION-PREPARATION-FAILED");
    }
    assert(!quota->liveBytes() && ticket.isCurrent() && live.sceneServiceSession() == original);
    Document callbacks(false), replacement(false);
    unsigned invocations = 0;
    callbacks.setResourceManagerProvider([&] {
        ++invocations;
        return original->resources();
    });
    replacement.setResourceManagerProvider([&] {
        ++invocations;
        return incoming->resources();
    });
    callbacks.setPhysicsWorld2DProvider([&] {
        ++invocations;
        return original->physics();
    });
    replacement.setPhysicsWorld2DProvider([&] {
        ++invocations;
        return incoming->physics();
    });
    auto adoption = callbacks.prepareReplacement(replacement, diagnostic, Policy::AdoptCandidate);
    assert(adoption && !invocations
        && callbacks.commitReplacement(*adoption, replacement) == Result::Committed
        && !invocations);
    assert(callbacks.resourceManagerProvider()() == incoming->resources()
        && replacement.resourceManagerProvider()() == original->resources() && invocations == 2);
}

void shellRetirement(const tests::RecoveryFixture& files)
{
    core::SharedCallback<resource::ResourceManager*()> retiredCallback, currentCallback;
    std::weak_ptr<resource::ResourceManager> retired, current;
    {
        EditorShell shell;
        assert(shell
                .load(std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui",
                    files.root)
                .ok());
        retired = shell.retainProjectResourceManager();
        retiredCallback = shell.sceneDocument().resourceManagerProvider();
        const auto result = shell.execute(CloseProjectCommand{true});
        assert(result.success);
        current = shell.retainProjectResourceManager();
        currentCallback = shell.sceneDocument().resourceManagerProvider();
        assert(retired.lock() != current.lock() && retiredCallback() == retired.lock().get()
            && currentCallback() == current.lock().get());
        assert(shell.execute(NewSceneCommand{true}).success
            && shell.sceneDocument().resourceManagerProvider()() == current.lock().get());
    }
    // Both callbacks remain callable after the original Shell has been destroyed.
    assert(!retired.expired() && !current.expired() && retiredCallback() == retired.lock().get()
        && currentCallback() == current.lock().get());
    retiredCallback = nullptr;
    currentCallback = nullptr;
    assert(retired.expired() && current.expired());
}

void runtimeEntryReload(const tests::RecoveryFixture& files)
{
    const physics2d::WorldConfig config{{0, -9.8f}, 8, 8, 4, 8, 16, 8, 8, 4, 1.f / 60.f};
    ProjectManifest manifest;
    ProjectDiagnostic issue;
    core::pmr::String diagnostic, yaml;
    assert(ProjectManifestCodec::decode(
        tests::ProjectFixture::document("assetPackages: []\nphysics2d: null\n"), manifest, issue));
    manifest.physics2d = config;
    assert(ProjectManifestCodec::encode(manifest, yaml, issue));
    EditorProjectResources resources;
    assert(resources.loadManifest(files.root, yaml).success);
    Document source(false);
    assert(resources.bindSceneServices(source, diagnostic));
    const auto entryObject = source.createObject("Animated entry");
    // An empty optional animation binding is a valid native persistence case.
    // This CPU fixture does not author a SpriteRenderer with a missing resource.
    source.scene()
        .findObject(entryObject)
        ->addComponent<scene2d::SpriteAnimationComponent>(
            resources.manager(), scene2d::kEditorSpriteAnimationEventBudget);
    const auto factory =
        ProjectSceneAuthoring::factory(resources.manager(), resources.physicsWorld2D(), diagnostic);
    assert(factory);
    manifest.entryScene = resource::deriveResourceUuid("live-service-test", "entry");
    const auto package = files.root / "Entry.arpkg";
    const auto exported = ProjectSceneAuthoring::exportScene(
        source.scene(), *factory, manifest.entryScene, package, diagnostic);
    if (!exported)
        std::fprintf(stderr, "runtime entry fixture export: %s\n", diagnostic.c_str());
    assert(exported);
    manifest.assetPackages.push_back("Entry.arpkg");
    assert(ProjectManifestCodec::encode(manifest, yaml, issue));
    tests::RecoveryFixture::write(files.root / "project.arp.yaml", yaml);
    {
        EditorShell shell;
        assert(shell
                .load(std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui",
                    files.root)
                .ok());
        const auto originalSession = shell.sceneDocument().sceneServiceSession();
        std::weak_ptr<resource::ResourceManager> originalManager =
            shell.retainProjectResourceManager();
        auto& document = shell.sceneDocument();
        auto* animation = document.scene()
                              .findObjectByName("Animated entry")
                              ->getComponent<scene2d::SpriteAnimationComponent>();
        assert(animation);
        const auto liveFactory = ProjectSceneAuthoring::factory(shell.projectResources(),
            originalSession->physics(),
            diagnostic,
            document.resourceManagerProvider(),
            document.physicsWorld2DProvider());
        assert(liveFactory);
        auto detached = liveFactory->create("SpriteAnimationComponent");
        auto* detachedAnimation = dynamic_cast<scene2d::SpriteAnimationComponent*>(detached.get());
        assert(detachedAnimation);
        assert(!ProjectSceneAuthoring::factory(shell.projectResources(),
            originalSession->physics(),
            diagnostic,
            document.resourceManagerProvider(),
            {}));
        assert(diagnostic == "EDITOR-SCENE-PERSISTENCE-SERVICE-BINDING");
        assert(!ProjectSceneAuthoring::factory(resources.manager(),
                   originalSession->physics(),
                   diagnostic,
                   document.resourceManagerProvider(),
                   document.physicsWorld2DProvider())
            && diagnostic == "EDITOR-SCENE-PERSISTENCE-SERVICE-BINDING");
        // Remove the disk entry for this same-Shell reload: the existing Scene
        // remains in place, so only the service epoch changes.
        manifest.entryScene.clear();
        assert(ProjectManifestCodec::encode(manifest, yaml, issue));
        tests::RecoveryFixture::write(files.root / "project.arp.yaml", yaml);
        Document candidate(false);
        candidate.copySceneServicesFrom(document);
        const auto documentTicket = document.ownerTicket();
        const auto serviceTicket = originalSession->ownerTicket();
        auto prepared = document.prepareReplacement(candidate, diagnostic, Policy::AdoptCandidate);
        assert(prepared);
        Document partial(false), destination(false);
        partial.copySceneServicesFrom(document);
        partial.setResourceManagerProvider(nullptr);
        assert(!partial.sceneServiceSession());
        auto partialToken =
            destination.prepareReplacement(partial, diagnostic, Policy::AdoptCandidate);
        assert(partialToken);
        assert(shell
                .load(std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui",
                    files.root)
                .ok());
        assert(documentTicket.isCurrent() && !serviceTicket.isCurrent() && originalManager.expired()
            && document.sceneServiceSession() == originalSession
            && document.commitReplacement(*prepared, candidate) == Result::Stale);
        assert(destination.commitReplacement(*partialToken, partial) == Result::Stale);
        assert(!animation->play_resource("missing-animation")
            && !detachedAnimation->play_resource("missing-animation"));
        assert(originalSession->resources() == &shell.projectResources());
    }
    std::filesystem::remove(files.root / "project.arp.yaml");
}

void assetPackagePublication(const tests::RecoveryFixture& files)
{
    // This worker fixture has no Scene assets: it does not exercise the known
    // unresolved asynchronous Scene/reflection ownership path.
    const auto root = files.root / "terrain-session";
    std::filesystem::create_directory(root);
    const auto terrain = std::filesystem::path(ARP_SOURCE_DIR) / "tests/fixtures/terrain";
    std::filesystem::copy_file(terrain / "editor-hill.terrain.yaml", root / "hill.yaml");
    std::filesystem::copy_file(terrain / "editor-hill.r32", root / "editor-hill.r32");
    ProjectManifest manifest;
    ProjectDiagnostic issue;
    core::pmr::String yaml, diagnostic;
    assert(ProjectManifestCodec::decode(
        tests::ProjectFixture::document("assetPackages: []\nphysics2d: null\n"), manifest, issue));
    manifest.physics2d = physics2d::WorldConfig{{0, -9.8f}, 8, 8, 4, 8, 16, 8, 8, 4, 1.f / 60.f};
    assert(ProjectManifestCodec::encode(manifest, yaml, issue));
    tests::RecoveryFixture::write(root / "project.arp.yaml", yaml);
    EditorShell shell;
    assert(shell.load(std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui", root)
            .ok());
    auto services = shell.sceneDocument().sceneServiceSession();
    const auto world = services->physics();
    const auto* manager = services->resources();
    std::weak_ptr<resource::ResourceManager> oldManager = shell.retainProjectResourceManager();
    const auto documentEpoch = shell.sceneDocument().ownerTicket(),
               serviceEpoch = services->ownerTicket();
    Document candidate(false);
    candidate.copySceneServicesFrom(shell.sceneDocument());
    auto prepared = shell.sceneDocument().prepareReplacement(candidate, diagnostic);
    assert(prepared);
    BuildTerrainResourceCommand build;
    build.authoringPath = "hill.yaml";
    build.packagePath = "hill.arpkg";
    assert(shell.execute(build).success);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (&shell.projectResources() == manager && std::chrono::steady_clock::now() < deadline) {
        shell.pollTasks();
        std::this_thread::yield();
    }
    assert(&shell.projectResources() != manager
        && services->resources() == &shell.projectResources() && services->physics() == world
        && oldManager.expired() && documentEpoch.isCurrent() && !serviceEpoch.isCurrent()
        && shell.sceneDocument().commitReplacement(*prepared, candidate) == Result::Stale);
    std::puts(
        "PASS actual terrain package publication updates original session, preserves physics and rejects stale prepared Scene without changing its document epoch");
}

void smoke(const tests::RecoveryFixture& files)
{
    adoption(Policy::AdoptCandidate);
    shellRetirement(files);
    rejectionAndQuota();
    const auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned cycle = 0; cycle < 20; ++cycle) {
        adoption(Policy::AdoptCandidate);
        adoption(Policy::KeepDestination);
        shellRetirement(files);
        rejectionAndQuota();
        const auto current = core::defaultCpuAllocator().stats();
        assert(current.liveBytes == baseline.liveBytes
            && current.allocationCount - current.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts(
        "PASS 20 warmed ordinary live service adoption/keep/Shell retirement/quota cycles: core live bytes/count delta=0");
}
} // namespace

int main(int argc, char** argv)
{
    tests::RecoveryFixture files;
    if (argc == 2 && std::string_view(argv[1]) == "--smoke")
        smoke(files);
    else {
        adoption(Policy::KeepDestination);
        adoption(Policy::AdoptCandidate);
        rejectionAndQuota();
        shellRetirement(files);
        runtimeEntryReload(files);
        assetPackagePublication(files);
        std::puts(
            "PASS explicit service policies, allocation-free commit, original callback owners after Shell destruction, native entry callbacks and service-only epoch rejection");
    }
}

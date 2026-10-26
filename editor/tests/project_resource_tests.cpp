#include "3d/animator_component.h"
#include "core/io/file_io.h"
#include "editor/tests/fixtures/project_fixture.h"
#include "editor/tests/fixtures/project_package_fixture.h"
#include "editor/workspace/editor_project_resources.h"
#include "editor/workspace/editor_terrain_build.h"
#include "resource/internal_asset_package.h"
#include "resource/prefab_asset_resources.h"
#include "resource/terrain_asset_resource.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <type_traits>

namespace {
std::filesystem::path makeRoot()
{
    const auto root = std::filesystem::temp_directory_path()
        / ("arp-editor-project-resources-"
            + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    return root;
}

void writeManifest(const std::filesystem::path& root, std::string_view contents)
{
    std::ofstream output(root / "project.arp.yaml", std::ios::binary);
    output << contents;
    assert(output.good());
}

arp::core::pmr::String text(const char* value)
{
    return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
}

arp::resource::PrefabAssetGraphBuildDesc packageDesc()
{
    return arp::editor::tests::ProjectPackageFixture::descriptor();
}

void projectIntegrationTests(const std::filesystem::path& root)
{
    using namespace arp;
    using editor::tests::ProjectFixture;
    std::filesystem::create_directory(root);
    const auto yaml =
        ProjectFixture::document("assetPackages: []\nphysics2d: null\n", "editor-project.yaml");
    writeManifest(root, yaml);
    constexpr auto metadata = "type: arp.editor-project\ncontractVersion: 1\nterrainPreviews: []\n";
    std::ofstream(root / "editor-project.yaml") << metadata;
    editor::EditorProjectResources owner;
    assert(owner.load(root).success);
    assert(owner.manifest() && owner.manifest()->editorMetadata == "editor-project.yaml");
    assert(owner.packageProvenanceBound() && owner.packageBindings().empty());
    const auto originalMetadata = core::readTextFile(
        root / "editor-project.yaml", editor::EditorProjectResources::maxManifestBytes);
    assert(originalMetadata.success && owner.metadataSnapshot() == originalMetadata.text
        && owner.inputBindings().size() == 1);
    const auto originalBindings = owner.inputBindings();
    const auto* original = &owner.manager();
    std::ofstream(root / "editor-project.yaml")
        << "type: arp.editor-project\ncontractVersion: 2\nterrainPreviews: []\n";
    const auto invalidMetadata = owner.load(root);
    assert(!invalidMetadata.success
        && invalidMetadata.diagnostics.find("PROJECT-METADATA-VERSION-UNSUPPORTED") == 0);
    assert(&owner.manager() == original && owner.manifestSnapshot() == yaml);
    assert(owner.metadataSnapshot() == originalMetadata.text
        && editor::ProjectInputSnapshot::equal(owner.inputBindings(), originalBindings));
    std::ofstream(root / "editor-project.yaml") << metadata;

    auto scripted = yaml;
    const auto offset = scripted.find("scriptModules: []");
    assert(offset != core::pmr::String::npos);
    scripted.replace(offset,
        std::string_view("scriptModules: []").size(),
        "scriptModules:\n  - id: gameplay\n"
        "    resourceId: 4305ee93-3897-43ae-93da-ecbb0c569c84\n"
        "    source: gameplay.arp\n    schemaVersion: 1\n    dependencies: []");
    const auto missingSource = owner.loadManifest(root, scripted);
    assert(!missingSource.success && &owner.manager() == original);
    assert(owner.manifestSnapshot() == yaml);
    // Opening references checks the file without executing or compiling it.
    std::ofstream(root / "gameplay.arp") << "// Source-only authoring fixture\n";
    assert(owner.loadManifest(root, scripted).success);
    assert(owner.manifest()->scriptModules.size() == 1 && owner.inputBindings().size() == 2);
    core::pmr::String inputDiagnostic;
    assert(editor::ProjectInputSnapshot::verify(
        std::filesystem::canonical(root), owner.inputBindings(), inputDiagnostic));
    const auto previousSnapshot = core::pmr::String(owner.manifestSnapshot());
    const auto* scriptOwner = &owner.manager();
    auto escaping =
        ProjectFixture::document("assetPackages: []\nphysics2d: null\n", "../outside.yaml");
    assert(!owner.loadManifest(root, escaping).success);
    assert(&owner.manager() == scriptOwner && owner.manifestSnapshot() == previousSnapshot);

    const auto invalidRoot = root / "invalid-project";
    std::filesystem::create_directories(invalidRoot / "project.arp.yaml");
    assert(!owner.load(invalidRoot).success);
    assert(&owner.manager() == scriptOwner && owner.manifestSnapshot() == previousSnapshot);
    std::filesystem::remove(invalidRoot / "project.arp.yaml");
    std::error_code linkError;
    std::filesystem::create_symlink(
        root / "missing.yaml", invalidRoot / "project.arp.yaml", linkError);
    if (!linkError) {
        const auto dangling = owner.load(invalidRoot);
        assert(!dangling.success && dangling.manifestFound);
        assert(&owner.manager() == scriptOwner && owner.manifestSnapshot() == previousSnapshot);
    } else {
        std::puts("PROJECT-SYMLINK-TEST: OS denied symlink creation; case not exercised");
    }

    // Normal project open, rejected reload and destruction must return owned
    // core allocations to the same warmed baseline; this is not a stress test.
    const auto cycle = [&] {
        editor::EditorProjectResources resources;
        assert(resources.load(root).success);
        assert(!resources.loadManifest(root, "type: invalid\n").success);
        assert(resources.manifest() && resources.manifestSnapshot() == yaml);
    };
    cycle();
    const auto baseline = core::defaultCpuAllocator().stats();
    for (int i = 0; i < 20; ++i) {
        cycle();
        const auto current = core::defaultCpuAllocator().stats();
        assert(current.liveBytes == baseline.liveBytes);
        assert(current.allocationCount - current.deallocationCount
            == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts(
        "PROJECT-RESOURCE-LIFETIME: 20 open/reject/destroy cycles, core live bytes/count delta = 0");
}

void retainedOwnerTests(const std::filesystem::path& root)
{
    using namespace arp;
    static_assert(!std::is_copy_constructible_v<editor::EditorProjectResources>);
    static_assert(std::is_nothrow_move_constructible_v<editor::EditorProjectResources>);
    std::filesystem::create_directories(root);
    const auto package = resource::PrefabAssetGraphBuilder().buildPackage(packageDesc());
    assert(package.success);
    assert(resource::InternalAssetPackageWriter().write(package.package, root / "asset").success);
    const auto yaml =
        editor::tests::ProjectFixture::document("assetPackages: [asset]\nphysics2d: null\n");
    const auto cycle = [&] {
        core::SharedPtr<resource::ResourceManager> retained;
        std::weak_ptr<resource::ResourceManager> weak;
        resource::ResourceHandle<resource::PrefabResource> borrowed;
        {
            editor::EditorProjectResources owner;
            assert(owner.loadManifest(root, yaml).success);
            retained = owner.retainManager();
            weak = retained;
            borrowed = retained->find<resource::PrefabResource>(package.ids.prefabId);
            assert(borrowed.ready());
            assert(!owner.loadManifest(root, "type: invalid\n").success);
            assert(owner.retainManager() == retained);
            editor::EditorProjectResources candidate;
            assert(candidate.loadManifest(root, yaml).success);
            owner.publishAssetPackages(std::move(candidate));
            assert(owner.retainManager() != retained && !weak.expired() && borrowed.ready());
            // Same UUID/generation is a different owner, not cache equivalence.
            const auto replacement =
                owner.manager().find<resource::PrefabResource>(package.ids.prefabId);
            assert(replacement.ready() && replacement.generation() == borrowed.generation()
                && replacement.get() != borrowed.get());
        }
        // Owner destruction cannot invalidate an attached consumer's record.
        assert(!weak.expired() && borrowed.ready());
        class Observer final : public resource::ResourceLifecycleObserver {
        public:
            bool beforeResourceUnload(resource::NameView) noexcept override
            {
                ++unloads;
                return true;
            }
            bool beforeRetiredResourcesRelease() noexcept override { return true; }
            unsigned unloads = 0;
        } observer;
        retained->addLifecycleObserver(observer);
        assert(retained->unload(package.ids.prefabId).success && observer.unloads == 1);
        assert(!borrowed.ready());
        retained->removeLifecycleObserver(observer);
        borrowed = {};
        retained.reset();
        assert(weak.expired());
    };
    cycle();
    const auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < 20; ++i) {
        cycle();
        const auto stats = core::defaultCpuAllocator().stats();
        assert(stats.liveBytes == baseline.liveBytes
            && stats.allocationCount - stats.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts(
        "PROJECT-RESOURCE-OWNER: 20 warmed reload/reject/replace/destroy/detach cycles; core live bytes/count delta = 0");
}
} // namespace

int main()
{
    using namespace arp;
    const auto root = makeRoot();
    {
        editor::EditorProjectResources empty;
        assert(!empty.packageProvenanceBound() && !empty.packageProvenanceDiagnostic().empty());
        assert(empty.load(root).success && !empty.packageProvenanceBound()
            && empty.packageBindings().empty() && !empty.packageProvenanceDiagnostic().empty());
    }
    {
        const auto terrainRoot = root / "terrain";
        std::filesystem::create_directory(terrainRoot);
        const auto fixture =
            std::filesystem::path(__FILE__).parent_path().parent_path().parent_path()
            / "tests/fixtures/terrain";
        std::filesystem::copy_file(fixture / "editor-hill.terrain.yaml", terrainRoot / "hill.yaml");
        std::filesystem::copy_file(fixture / "editor-hill.r32", terrainRoot / "editor-hill.r32");
        editor::EditorTerrainBuild build;
        const auto wait = [&] {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
            while (!build.ready() && std::chrono::steady_clock::now() < deadline)
                std::this_thread::yield();
            assert(build.ready());
        };
        assert(!build.start(terrainRoot, "../hill.yaml", "escape.arpkg"));
        assert(build.start(terrainRoot, "hill.yaml", "hill.arpkg"));
        wait();
        const auto noProject = build.finish(terrainRoot);
        assert(!noProject.success
            && noProject.diagnostic.find("EDITOR-TERRAIN-PROJECT-REQUIRED") == 0);
        assert(!std::filesystem::exists(terrainRoot / "project.arp.yaml"));
        assert(!std::filesystem::exists(terrainRoot / "hill.arpkg"));
        writeManifest(terrainRoot,
            editor::tests::ProjectFixture::document("assetPackages: []\nphysics2d: null\n"));
        assert(build.start(terrainRoot, "hill.yaml", "hill.arpkg"));
        assert(!build.start(terrainRoot, "hill.yaml", "parallel.arpkg"));
        wait();
        auto completed = build.finish(terrainRoot);
        assert(completed.success && completed.resources);
        constexpr auto id = "3f6fbcca-17d2-46a6-9817-2af357399301";
        assert(completed.resources->manager().find<resource::TerrainAssetResource>(id).ready());
        editor::EditorProjectResources reopened;
        assert(reopened.load(terrainRoot).success);
        assert(reopened.manifest()
            && reopened.manifest()->id == "4305ee93-3897-43ae-93da-ecbb0c569c83");
        assert(reopened.manager().find<resource::TerrainAssetResource>(id).ready());
        assert(build.start(terrainRoot, "hill.yaml", "hill.arpkg"));
        wait();
        assert(!build.finish(terrainRoot).success);
        assert(reopened.manager().find<resource::TerrainAssetResource>(id).ready());

        const auto conflictRoot = root / "terrain-conflict";
        std::filesystem::create_directory(conflictRoot);
        std::filesystem::copy_file(
            fixture / "editor-hill.terrain.yaml", conflictRoot / "hill.yaml");
        std::filesystem::copy_file(fixture / "editor-hill.r32", conflictRoot / "editor-hill.r32");
        writeManifest(conflictRoot,
            editor::tests::ProjectFixture::document("assetPackages: []\nphysics2d: null\n"));
        assert(build.start(conflictRoot, "hill.yaml", "hill.arpkg"));
        wait();
        writeManifest(conflictRoot,
            arp::editor::tests::ProjectFixture::document("assetPackages: []\n"
                                                         "physics2d: null\n")
                + "# external edit\n");
        auto conflict = build.finish(conflictRoot);
        assert(!conflict.success && conflict.diagnostic == "EDITOR-TERRAIN-MANIFEST-CONFLICT");
        assert(!std::filesystem::exists(conflictRoot / "hill.arpkg"));
        assert(reopened.load(conflictRoot).success && reopened.lastLoad().packageCount == 0u);

        const auto invalidRoot = root / "terrain-invalid";
        std::filesystem::create_directory(invalidRoot);
        std::ofstream(invalidRoot / "bad.yaml") << "type: unsupported\n";
        assert(build.start(invalidRoot, "bad.yaml", "bad.arpkg"));
        wait();
        assert(!build.finish(invalidRoot).success);
        assert(build.start(terrainRoot, "hill.yaml", "stale.arpkg", std::string_view{}));
        wait();
        const auto staleProject = build.finish(terrainRoot);
        assert(!staleProject.success
            && staleProject.diagnostic == "EDITOR-TERRAIN-PROJECT-RELOAD-REQUIRED");
        assert(!std::filesystem::exists(terrainRoot / "stale.arpkg"));
        assert(!std::filesystem::exists(invalidRoot / "project.arp.yaml"));
        assert(!std::filesystem::exists(invalidRoot / "bad.arpkg"));
        writeManifest(invalidRoot,
            editor::tests::ProjectFixture::document("assetPackages: []\nphysics2d: null\n"));
        assert(build.start(invalidRoot, "bad.yaml", "bad.arpkg"));
        wait();
        assert(!build.finish(invalidRoot).success);
    }
    {
        editor::EditorProjectResources resources;

        const auto missing = resources.load(root);
        assert(missing.success && !missing.manifestFound && missing.packageCount == 0u);
        assert(!resources.manifest());
        assert(
            missing.diagnostics.find("EDITOR-PROJECT-MANIFEST-MISSING") != core::pmr::String::npos);

        writeManifest(root,
            arp::editor::tests::ProjectFixture::document("assetPackages: []\n"
                                                         "physics2d: null\n"));
        const auto empty = resources.load(root);
        assert(empty.success && empty.manifestFound && empty.packageCount == 0u);
        assert(empty.diagnostics.empty());
        assert(resources.manifest()
            && resources.manifest()->id == "4305ee93-3897-43ae-93da-ecbb0c569c83");
        const auto* stableManager = &resources.manager();
        const core::pmr::String stableSnapshot(resources.manifestSnapshot());
        const auto legacy = resources.loadManifest(
            root, "format: ArpEditorProject\nversion: 1\nassetPackages: []\n");
        assert(
            !legacy.success && legacy.diagnostics.find("PROJECT-LEGACY-MIGRATION-REQUIRED") == 0);
        assert(&resources.manager() == stableManager
            && resources.manifestSnapshot() == stableSnapshot);
        writeManifest(root,
            arp::editor::tests::ProjectFixture::document("assetPackages: []\n"
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
                                                         "  fixedSeconds: 0.016666667\n"));
        const auto configuredPhysics = resources.load(root);
        const auto physicsOwner = resources.physicsWorld2D();
        assert(configuredPhysics.success && configuredPhysics.physicsWorld2DReady);
        assert(physicsOwner && physicsOwner->valid());
        editor::EditorProjectResources packageCandidate;
        assert(packageCandidate.load(root).success);
        resources.publishAssetPackages(std::move(packageCandidate));
        assert(
            resources.physicsWorld2D() == physicsOwner && resources.lastLoad().physicsWorld2DReady);
        assert(!resources.manifestSnapshot().empty());

        writeManifest(root,
            arp::editor::tests::ProjectFixture::document("assetPackages: []\n"
                                                         "physics2d:\n"
                                                         "  gravity: {x: 0, y: -9.8}\n"
                                                         "  maxBodies: 0\n"
                                                         "  maxColliders: 64\n"
                                                         "  maxJoints: 16\n"
                                                         "  maxNativeShapes: 128\n"
                                                         "  maxEvents: 256\n"
                                                         "  maxQueryResults: 128\n"
                                                         "  maxChainPoints: 32\n"
                                                         "  substeps: 4\n"
                                                         "  fixedSeconds: 0.016666667\n"));
        const auto invalidPhysics = resources.load(root);
        assert(!invalidPhysics.success);
        assert(invalidPhysics.diagnostics.find("PROJECT-PHYSICS2D-FIELD-INVALID")
            != core::pmr::String::npos);
        assert(resources.physicsWorld2D() == physicsOwner);

        writeManifest(root,
            arp::editor::tests::ProjectFixture::document("assetPackages: []\n"
                                                         "physics2d: null\n"));
        const auto clearedPhysics = resources.load(root);
        assert(clearedPhysics.success && !clearedPhysics.physicsWorld2DReady);
        assert(!resources.physicsWorld2D());

        const auto package = resource::PrefabAssetGraphBuilder().buildPackage(packageDesc());
        assert(package.success);
        const auto packageRoot = root / "Assets" / "Base.arpkg";
        assert(resource::InternalAssetPackageWriter().write(package.package, packageRoot).success);
        writeManifest(root,
            arp::editor::tests::ProjectFixture::document("assetPackages:\n"
                                                         "  - Assets/Base.arpkg\n"
                                                         "physics2d: null\n"));
        const auto populated = resources.load(root);
        assert(populated.success && populated.manifestFound && populated.packageCount == 1u);
        const auto loadedPrefab = resources.manager().find<resource::PrefabResource>(
            "0b5f7a2d-78a7-47d2-91f5-4ab0f8b827e1");
        assert(loadedPrefab.ready());
        assert(resources.resourceLocations().size() == package.package.entries.size());
        for (const auto& entry : package.package.entries) {
            std::filesystem::path definition;
            assert(!resources.resourceLocations().resolve(
                entry.resourceId, entry.resourceType, resources.manager(), definition));
            assert(definition == packageRoot / std::filesystem::u8path(entry.envelopePath));
        }
        editor::EditorProjectResources definitionCandidate;
        assert(definitionCandidate.load(root).success);
        resources.publishAssetPackages(std::move(definitionCandidate));
        assert(resources.resourceLocations().size() == package.package.entries.size());

        std::filesystem::path resolvedPackageRoot;
        assert(resources.packageRootForResource(
            "0b5f7a2d-78a7-47d2-91f5-4ab0f8b827e1", resolvedPackageRoot));
        assert(resolvedPackageRoot == packageRoot);
        assert(!resources.packageRootForResource("unknown-prefab-id", resolvedPackageRoot));

        const auto packageManifestPath = packageRoot / "manifest.arpkg.yaml";
        std::ifstream packageManifestInput(packageManifestPath, std::ios::binary);
        std::string packageManifest((std::istreambuf_iterator<char>(packageManifestInput)),
            std::istreambuf_iterator<char>());
        const auto packageEnvelopePath = package.package.entries.front().envelopePath;
        const auto envelopePathPosition = packageManifest.find(packageEnvelopePath.c_str());
        assert(envelopePathPosition != std::string::npos);
        packageManifest.replace(
            envelopePathPosition, packageEnvelopePath.size(), "../escape.arpres.yaml");
        {
            std::ofstream packageManifestOutput(
                packageManifestPath, std::ios::binary | std::ios::trunc);
            packageManifestOutput << packageManifest;
            assert(packageManifestOutput.good());
        }
        writeManifest(root,
            arp::editor::tests::ProjectFixture::document("assetPackages:\n"
                                                         "  - Assets/Base.arpkg\n"
                                                         "physics2d: null\n"));
        const auto escapedEnvelope = resources.load(root);
        assert(!escapedEnvelope.success);
        assert(escapedEnvelope.diagnostics.find("unsafe package envelope path")
            != core::pmr::String::npos);
        assert(resources.manager()
                .find<resource::PrefabResource>("0b5f7a2d-78a7-47d2-91f5-4ab0f8b827e1")
                .ready());
        assert(resources.packageRootForResource(
            "0b5f7a2d-78a7-47d2-91f5-4ab0f8b827e1", resolvedPackageRoot));
        assert(resolvedPackageRoot == packageRoot);

        writeManifest(root,
            arp::editor::tests::ProjectFixture::document("assetPackages: []\n"
                                                         "physics2d: null\n",
                "null",
                2));
        const auto unsupported = resources.load(root);
        assert(!unsupported.success);
        assert(
            unsupported.diagnostics.find("PROJECT-VERSION-UNSUPPORTED") != core::pmr::String::npos);
        assert(resources.lastLoad().diagnostics == unsupported.diagnostics);
        assert(resources.manager()
                .find<resource::PrefabResource>("0b5f7a2d-78a7-47d2-91f5-4ab0f8b827e1")
                .ready());

        writeManifest(root,
            arp::editor::tests::ProjectFixture::document("assetPackages:\n"
                                                         "  - ../outside\n"
                                                         "physics2d: null\n"));
        const auto traversal = resources.load(root);
        assert(!traversal.success);
        assert(traversal.diagnostics.find("PROJECT-PATH-INVALID") != core::pmr::String::npos);

        writeManifest(root,
            arp::editor::tests::ProjectFixture::document("assetPackages:\n"
                                                         "  - Assets/Main.arpkg\n"
                                                         "  - Assets/Main.arpkg\n"
                                                         "physics2d: null\n"));
        const auto duplicate = resources.load(root);
        assert(!duplicate.success);
        assert(duplicate.diagnostics.find("PROJECT-PATH-DUPLICATE") != core::pmr::String::npos);

        writeManifest(root,
            arp::editor::tests::ProjectFixture::document("assetPackages:\n"
                                                         "  - Assets/Missing.arpkg\n"
                                                         "physics2d: null\n"));
        const auto missingPackage = resources.load(root);
        assert(!missingPackage.success);
        assert(missingPackage.diagnostics.find("EDITOR-PROJECT-ASSET-PACKAGE-PATH")
            != core::pmr::String::npos);
        assert(resources.packageRootForResource(
            "0b5f7a2d-78a7-47d2-91f5-4ab0f8b827e1", resolvedPackageRoot));

        writeManifest(root,
            arp::editor::tests::ProjectFixture::document("assetPackages: []\n"
                                                         "physics2d: null\n"));
        const auto cleared = resources.load(root);
        assert(cleared.success && cleared.packageCount == 0u);
        assert(!resources.packageRootForResource(
            "0b5f7a2d-78a7-47d2-91f5-4ab0f8b827e1", resolvedPackageRoot));
    }

    {
        const auto animatorRoot = root / "animator-project";
        const auto sourceRoot = animatorRoot / "Source";
        const auto packageRoot = animatorRoot / "Assets" / "Animated.arpkg";
        std::filesystem::create_directories(sourceRoot);
        assets::CharacterImportResult imported;
        imported.success = true;
        imported.character.mesh = assets::MeshAsset::debugTriangle();
        imported.character.skinnedVertices.resize(imported.character.mesh.vertices.size());
        for (auto& vertex : imported.character.skinnedVertices) {
            vertex.joints = {0u, 0u, 0u, 0u};
            vertex.weights = {1.0f, 0.0f, 0.0f, 0.0f};
        }
        animation::Joint rootJoint;
        rootJoint.name = "root";
        imported.character.skeleton.joints.push_back(rootJoint);
        imported.character.bindPose.push_back(animation::LocalTransform{});
        animation::AnimationClip idleClip;
        idleClip.name = "idle";
        idleClip.durationSeconds = 1.0f;
        animation::AnimationChannel idleChannel;
        idleChannel.jointIndex = 0u;
        idleChannel.jointName = "root";
        idleChannel.targetPath = "translation";
        idleChannel.keyframes.push_back(animation::JointKeyframe{});
        animation::JointKeyframe idleEnd;
        idleEnd.timeSeconds = 1.0f;
        idleChannel.keyframes.push_back(idleEnd);
        idleClip.channels.push_back(idleChannel);
        imported.character.clips.push_back(idleClip);
        imported.stateMachine.animator.name = "editor-test-controller";
        imported.stateMachine.animator.defaultStateName = "idle";
        animation::AnimatorState idleState;
        idleState.name = "idle";
        idleState.clipName = "idle";
        imported.stateMachine.animator.states.push_back(idleState);

        auto animatedDesc = packageDesc();
        animatedDesc.alias = text("prefab/editor-animated");
        animatedDesc.displayName = text("editor-animated");
        animatedDesc.sourcePath = text(sourceRoot.string().c_str());
        animatedDesc.runtimePayloadPackageRoot = packageRoot;
        animatedDesc.importedCharacter = &imported;
        const auto animatedPackage = resource::PrefabAssetGraphBuilder().buildPackage(animatedDesc);
        assert(animatedPackage.success);
        assert(resource::InternalAssetPackageWriter()
                .write(animatedPackage.package, packageRoot)
                .success);
        writeManifest(animatorRoot,
            arp::editor::tests::ProjectFixture::document("assetPackages:\n"
                                                         "  - Assets/Animated.arpkg\n"
                                                         "physics2d: null\n"));
        editor::EditorProjectResources animatorResources;
        const auto loaded = animatorResources.load(animatorRoot);
        assert(loaded.success && loaded.packageCount == 1u);
        const auto prefab = animatorResources.manager().find<resource::PrefabResource>(
            animatedPackage.ids.prefabId);
        const auto runtime =
            animatorResources.manager().find<resource::PrefabRuntimePayloadResource>(
                animatedPackage.ids.runtimePayloadId);
        assert(prefab.ready() && runtime.ready() && runtime.get()->runtimeAsset
            && runtime.get()->controllerAsset);

        core::SharedCallback<resource::ResourceManager*()> provider([&animatorResources] {
            return &animatorResources.manager();
        });
        scene3d::AnimatorComponent animator(scene3d::AnimatorComponentDesc{}, provider);
        scene3d::AnimatorEditorProperties properties;
        properties.prefabResourceId = animatedPackage.ids.prefabId;
        assert(animator.applyEditorAuthoringProperties(reflection::object(properties)));
        assert(animator.desc().character == runtime.get()->runtimeAsset.get()
            && animator.desc().controller == runtime.get()->controllerAsset.get());
        assert(animator.evaluate(scene3d::AnimatorEvaluationRequest{}).ok());

        auto rejectedSwitch = properties;
        rejectedSwitch.prefabResourceId = text("missing-animated-prefab");
        assert(!animator.applyEditorAuthoringProperties(reflection::object(rejectedSwitch)));
        assert(animator.editorAuthoringProperties()
                   .cast<const scene3d::AnimatorEditorProperties&>()
                   .prefabResourceId
            == animatedPackage.ids.prefabId);
        assert(animator.evaluate(scene3d::AnimatorEvaluationRequest{}).ok());

        writeManifest(animatorRoot,
            arp::editor::tests::ProjectFixture::document("assetPackages: []\n"
                                                         "physics2d: null\n"));
        assert(animatorResources.load(animatorRoot).success);
        assert(!animator.evaluate(scene3d::AnimatorEvaluationRequest{}).ok());
        assert(animator.desc().character == nullptr && animator.desc().controller == nullptr
            && !animator.editorBindingDiagnostic().empty());
    }

    projectIntegrationTests(root / "project-integration");
    retainedOwnerTests(root / "retained-owner");
    std::error_code error;
    std::filesystem::remove_all(root, error);
    assert(!error);
}

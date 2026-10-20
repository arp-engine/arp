#include "3d/animator_component.h"
#include "editor/workspace/editor_project_resources.h"
#include "resource/internal_asset_package.h"
#include "resource/prefab_asset_resources.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {
std::filesystem::path makeRoot()
{
    const auto root = std::filesystem::temp_directory_path()
        / ("arp-editor-project-resources-"
            + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    return root;
}

void writeManifest(const std::filesystem::path& root, const char* contents)
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
    using namespace arp::resource;
    PrefabAssetGraphBuildDesc desc;
    desc.ids.rootId = text("0b5f7a2d-78a7-47d2-91f5-4ab0f8b827e1");
    desc.ids.prefabId = desc.ids.rootId;
    desc.ids.basicShaderId = text("f0817aa6-f639-46c9-b9de-0ea317a2b94d");
    desc.alias = text("prefab/editor-test");
    desc.displayName = text("editor-test");
    desc.sourcePath = text("synthetic/editor-test");
    desc.sourceKind = text("SyntheticPrefabFolder");
    desc.shaderPaths.basicMaterialPreview = text("shaders/basic.slang");
    desc.shaderPaths.stylizedScene = text("shaders/stylized.slang");
    desc.shaderPaths.guiOverlay = text("shaders/gui.slang");
    desc.startupSplashShaderPath = text("shaders/startup.slang");
    return desc;
}
} // namespace

int main()
{
    using namespace arp;
    const auto root = makeRoot();
    {
        editor::EditorProjectResources resources;

        const auto missing = resources.load(root);
        assert(missing.success && !missing.manifestFound && missing.packageCount == 0u);
        assert(
            missing.diagnostics.find("EDITOR-PROJECT-MANIFEST-MISSING") != core::pmr::String::npos);

        writeManifest(root, "format: ArpEditorProject\nversion: 1\nassetPackages: []\n");
        const auto empty = resources.load(root);
        assert(empty.success && empty.manifestFound && empty.packageCount == 0u);
        assert(empty.diagnostics.empty());
        writeManifest(root,
            "format: ArpEditorProject\nversion: 1\nassetPackages: []\nphysics2d:\n"
            "  gravity: {x: 0, y: -9.8}\n  maxBodies: 32\n  maxColliders: 64\n"
            "  maxJoints: 16\n  maxNativeShapes: 128\n  maxEvents: 256\n"
            "  maxQueryResults: 128\n  maxChainPoints: 32\n  substeps: 4\n"
            "  fixedSeconds: 0.016666667\n");
        const auto configuredPhysics = resources.load(root);
        const auto physicsOwner = resources.physicsWorld2D();
        assert(configuredPhysics.success && configuredPhysics.physicsWorld2DReady);
        assert(physicsOwner && physicsOwner->valid());

        writeManifest(root,
            "format: ArpEditorProject\nversion: 1\nassetPackages: []\nphysics2d:\n"
            "  gravity: {x: 0, y: -9.8}\n  maxBodies: 0\n  maxColliders: 64\n"
            "  maxJoints: 16\n  maxNativeShapes: 128\n  maxEvents: 256\n"
            "  maxQueryResults: 128\n  maxChainPoints: 32\n  substeps: 4\n"
            "  fixedSeconds: 0.016666667\n");
        const auto invalidPhysics = resources.load(root);
        assert(!invalidPhysics.success);
        assert(invalidPhysics.diagnostics.find("EDITOR-PROJECT-PHYSICS2D-SCHEMA")
            != core::pmr::String::npos);
        assert(resources.physicsWorld2D() == physicsOwner);

        writeManifest(root, "format: ArpEditorProject\nversion: 1\nassetPackages: []\n");
        const auto clearedPhysics = resources.load(root);
        assert(clearedPhysics.success && !clearedPhysics.physicsWorld2DReady);
        assert(!resources.physicsWorld2D());

        const auto package = resource::PrefabAssetGraphBuilder().buildPackage(packageDesc());
        assert(package.success);
        const auto packageRoot = root / "Assets" / "Base.arpkg";
        assert(resource::InternalAssetPackageWriter().write(package.package, packageRoot).success);
        writeManifest(
            root, "format: ArpEditorProject\nversion: 1\nassetPackages:\n  - Assets/Base.arpkg\n");
        const auto populated = resources.load(root);
        assert(populated.success && populated.manifestFound && populated.packageCount == 1u);
        const auto loadedPrefab = resources.manager().find<resource::PrefabResource>(
            "0b5f7a2d-78a7-47d2-91f5-4ab0f8b827e1");
        assert(loadedPrefab.ready());
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
        writeManifest(
            root, "format: ArpEditorProject\nversion: 1\nassetPackages:\n  - Assets/Base.arpkg\n");
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

        writeManifest(root, "format: ArpEditorProject\nversion: 2\nassetPackages: []\n");
        const auto unsupported = resources.load(root);
        assert(!unsupported.success);
        assert(unsupported.diagnostics.find("EDITOR-PROJECT-MANIFEST-SCHEMA")
            != core::pmr::String::npos);
        assert(resources.lastLoad().diagnostics == unsupported.diagnostics);
        assert(resources.manager()
                .find<resource::PrefabResource>("0b5f7a2d-78a7-47d2-91f5-4ab0f8b827e1")
                .ready());

        writeManifest(
            root, "format: ArpEditorProject\nversion: 1\nassetPackages:\n  - ../outside\n");
        const auto traversal = resources.load(root);
        assert(!traversal.success);
        assert(
            traversal.diagnostics.find("EDITOR-PROJECT-MANIFEST-PATH") != core::pmr::String::npos);

        writeManifest(root,
            "format: ArpEditorProject\nversion: 1\nassetPackages:\n  - Assets/Main.arpkg\n  - Assets/Main.arpkg\n");
        const auto duplicate = resources.load(root);
        assert(!duplicate.success);
        assert(duplicate.diagnostics.find("EDITOR-PROJECT-MANIFEST-DUPLICATE")
            != core::pmr::String::npos);

        writeManifest(root,
            "format: ArpEditorProject\nversion: 1\nassetPackages:\n  - Assets/Missing.arpkg\n");
        const auto missingPackage = resources.load(root);
        assert(!missingPackage.success);
        assert(missingPackage.diagnostics.find("EDITOR-PROJECT-ASSET-PACKAGE-PATH")
            != core::pmr::String::npos);
        assert(resources.packageRootForResource(
            "0b5f7a2d-78a7-47d2-91f5-4ab0f8b827e1", resolvedPackageRoot));

        writeManifest(root, "format: ArpEditorProject\nversion: 1\nassetPackages: []\n");
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
        assert(resource::InternalAssetPackageWriter().write(
            animatedPackage.package, packageRoot).success);
        writeManifest(animatorRoot,
            "format: ArpEditorProject\nversion: 1\nassetPackages:\n  - Assets/Animated.arpkg\n");
        editor::EditorProjectResources animatorResources;
        const auto loaded = animatorResources.load(animatorRoot);
        assert(loaded.success && loaded.packageCount == 1u);
        const auto prefab = animatorResources.manager().find<resource::PrefabResource>(
            animatedPackage.ids.prefabId);
        const auto runtime = animatorResources.manager().find<resource::PrefabRuntimePayloadResource>(
            animatedPackage.ids.runtimePayloadId);
        assert(prefab.ready() && runtime.ready() && runtime.get()->runtimeAsset
            && runtime.get()->controllerAsset);

        core::SharedCallback<resource::ResourceManager*()> provider(
            [&animatorResources] { return &animatorResources.manager(); });
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

        writeManifest(animatorRoot, "format: ArpEditorProject\nversion: 1\nassetPackages: []\n");
        assert(animatorResources.load(animatorRoot).success);
        assert(!animator.evaluate(scene3d::AnimatorEvaluationRequest{}).ok());
        assert(animator.desc().character == nullptr && animator.desc().controller == nullptr
            && !animator.editorBindingDiagnostic().empty());
    }

    std::error_code error;
    std::filesystem::remove_all(root, error);
    assert(!error);
}

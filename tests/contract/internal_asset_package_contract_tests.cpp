#include "resource/internal_asset_package.h"
#include "resource/prefab_asset_resources.h"
#include "tests/fixtures/prefab_runtime_package_fixture.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

namespace {

using namespace arp::tests::prefab_runtime_package_fixture;

void assertContains(const arp::core::pmr::String& haystack, const char* needle)
{
    if (haystack.find(needle) == arp::core::pmr::String::npos) {
        std::cerr << "missing expected diagnostic fragment: " << needle
                  << "\nactual diagnostic: " << haystack << "\n";
    }
    assert(haystack.find(needle) != arp::core::pmr::String::npos);
}

std::uint32_t hashBytes(const std::vector<std::uint8_t>& bytes)
{
    std::uint32_t hash = 2166136261u;
    for (const auto byte : bytes) {
        hash ^= byte;
        hash *= 16777619u;
    }
    return hash;
}

arp::resource::PrefabAssetGraphBuildDesc makeDesc()
{
    using namespace arp::resource;

    PrefabAssetGraphBuildDesc desc;
    desc.ids.rootId = text(kPrefabId);
    desc.ids.prefabId = text(kPrefabId);
    desc.ids.basicShaderId = text(kBasicShaderId);
    desc.alias = text("prefab/male000");
    desc.displayName = text("male000");
    desc.sourcePath = text("synthetic/male000");
    desc.sourceKind = text("SyntheticPrefabFolder");
    desc.shaderPaths.basicMaterialPreview = text("shaders/basic.slang");
    desc.shaderPaths.stylizedScene = text("shaders/stylized.slang");
    desc.shaderPaths.guiOverlay = text("shaders/gui.slang");
    desc.startupSplashShaderPath = text("shaders/startup.slang");
    return desc;
}

arp::resource::InternalAssetPackageDesc makePackage()
{
    const auto graph = arp::resource::PrefabAssetGraphBuilder().buildPackage(makeDesc());
    if (!graph.success) {
        std::cerr << graph.diagnostics << "\n";
    }
    assert(graph.success);
    return graph.package;
}

std::filesystem::path packagePath(const char* name)
{
    return std::filesystem::temp_directory_path() / "arp_internal_asset_package_contract" / name;
}

} // namespace

int main()
{
    arp::resource::registerPrefabAssetReflection();

    {
        const auto package = makePackage();
        const auto written =
            arp::resource::InternalAssetPackageWriter().write(package, packagePath("valid"));
        if (!written.success)
            std::cerr << written.diagnostics << '\n';
        assert(written.success);

        const auto read = arp::resource::InternalAssetPackageReader().read(packagePath("valid"));
        assert(read.success);
        assert(read.package.name == text("male000"));
        assert(read.package.entries.size() == 18u);
        assert(read.package.entries.back().resourceType == text("PrefabResource"));
        assert(read.package.entries.back().dependencies.size() == 15u);

        arp::resource::ResourceManager manager;
        const auto loaded =
            arp::resource::InternalAssetPackageReader().load(packagePath("valid"), manager);
        assert(loaded.success);
        const auto root = manager.find<arp::resource::PrefabResource>(kPrefabId);
        assert(root.ready());
        assert(root.get()->modelId.size() > 0u);
        const auto shader = manager.find<arp::resource::ShaderPackageResource>(kBasicShaderId);
        assert(shader.ready());
        assert(shader.get()->reflectionPath == text("shaders/basic.reflection.yaml"));
    }

    {
        arp::resource::ResourceEnvelopeBuildDesc desc;
        desc.resourceType = text("PrefabResource");
        desc.status = text("Prototype Complete");
        arp::resource::PrefabResource payload;
        const auto envelope =
            arp::resource::buildResourceEnvelope(desc, arp::reflection::object(payload));
        assert(!envelope.success);
        assertContains(envelope.diagnostics, "UUID resourceId");
    }

    {
        auto package = makePackage();
        auto& shader = package.entries[1];
        arp::resource::ShaderPackageResource brokenShader;
        brokenShader.name = text("broken");
        brokenShader.slangPath = text("shaders/broken.slang");
        brokenShader.spirvPath = text("generated.spv");
        brokenShader.mslPath = text("generated.metal");
        arp::core::pmr::Vector<arp::resource::ResourceDependency> dependencies{
            &arp::core::defaultCpuMemoryResource()};
        shader = makeEntry(kBasicShaderId,
            "ShaderPackageResource",
            "prefab/male000/shader/basic",
            "resources/shader-basic.arpres.yaml",
            dependencies,
            brokenShader);
        const auto written = arp::resource::InternalAssetPackageWriter().write(
            package, packagePath("missing_shader_reflection"));
        assert(!written.success);
        assertContains(written.diagnostics, "reflectionPath");
    }

    {
        auto package = makePackage();
        package.entries[0].contentHash = 1u;
        const auto written =
            arp::resource::InternalAssetPackageWriter().write(package, packagePath("bad_hash"));
        assert(!written.success);
        assertContains(written.diagnostics, "metadata does not match envelope");
    }

    {
        auto package = makePackage();
        arp::resource::SkinnedMeshResource brokenMesh;
        brokenMesh.name = text("broken");
        brokenMesh.sourcePath = text("synthetic/male000");
        brokenMesh.vertexCount = 0;
        brokenMesh.indexCount = 3;
        brokenMesh.submeshCount = 1;
        brokenMesh.materialSlotCount = 1;
        arp::core::pmr::Vector<arp::resource::ResourceDependency> dependencies{
            &arp::core::defaultCpuMemoryResource()};
        package.entries[7] = makeEntry(package.entries[7].resourceId.c_str(),
            "SkinnedMeshResource",
            "prefab/male000/skinned-mesh",
            "resources/skinned-mesh.arpres.yaml",
            dependencies,
            brokenMesh);
        const auto written = arp::resource::InternalAssetPackageWriter().write(
            package, packagePath("bad_skinned_mesh"));
        assert(!written.success);
        assertContains(written.diagnostics, "SkinnedMeshResource counts");
    }

    {
        auto package = makePackage();
        package.entries[0].dependencies.push_back(
            dependency("c8591a23-0f99-4070-a4d0-d79ab87379eb", "missing"));
        const auto written = arp::resource::InternalAssetPackageWriter().write(
            package, packagePath("unresolved_dependency"));
        assert(!written.success);
        assertContains(written.diagnostics, "unresolved package dependency");
    }

    {
        const auto root = packagePath("runtime_payload");
        arp::resource::PrefabRuntimePayloadWriteDesc payloadDesc;
        auto asset = makeRuntimeAsset();
        auto controller = arp::tests::prefab_runtime_package_fixture::makeController();
        payloadDesc.packageRoot = root;
        payloadDesc.blobPath = text("blobs/prefab-runtime.arpa.bin");
        payloadDesc.displayName = text("runtime");
        payloadDesc.asset = &asset;
        payloadDesc.controllerAsset = &controller;
        payloadDesc.defaultStateName = text("idle");
        arp::resource::PrefabRuntimeStateBinding binding;
        binding.stateName = text("idle");
        binding.clipName = text("idle");
        payloadDesc.stateBindings.push_back(binding);
        arp::resource::PrefabRuntimeStateBinding runBinding;
        runBinding.stateName = text("run");
        runBinding.clipName = text("run");
        runBinding.speed = 1.5f;
        payloadDesc.stateBindings.push_back(runBinding);
        const auto payload = arp::resource::writePrefabRuntimePayload(payloadDesc);
        assert(payload.success);

        auto invalidController = controller;
        invalidController.transitions[0].destinationState = "missing";
        payloadDesc.controllerAsset = &invalidController;
        const auto rejectedController = arp::resource::writePrefabRuntimePayload(payloadDesc);
        assert(!rejectedController.success);
        assertContains(rejectedController.diagnostics, "animator transition is invalid");
        payloadDesc.controllerAsset = &controller;

        arp::core::pmr::Vector<arp::resource::ResourceDependency> noDependencies;
        auto package = arp::tests::prefab_runtime_package_fixture::makePackage(payload.resource);

        const auto written = arp::resource::InternalAssetPackageWriter().write(package, root);
        assert(written.success);
        arp::resource::ResourceManager packageManager;
        const auto packageLoaded =
            arp::resource::InternalAssetPackageReader().load(root, packageManager);
        assert(packageLoaded.success);
        const auto prefabHandle = packageManager.find<arp::resource::PrefabResource>(kPrefabId);
        assert(prefabHandle.ready());
        const auto runtimeHandle =
            packageManager.find<arp::resource::PrefabRuntimePayloadResource>(kRuntimePayloadId);
        assert(runtimeHandle.ready());
        assert(runtimeHandle.get()->runtimeAsset);
        assert(runtimeHandle.get()->controllerAsset);
        assert(runtimeHandle.get()->runtimeAsset->mesh.validForIndexedDraw());
        assert(runtimeHandle.get()->controllerAsset->states.size() == 2u);
        const auto controllerHandle =
            packageManager.find<arp::resource::AnimatorControllerResource>(kAnimatorControllerId);
        assert(controllerHandle.ready());
        assert(controllerHandle.get()->controllerAsset);
        assert(controllerHandle.get()->controllerAsset->transitions.size() == 1u);

        const auto loaded =
            arp::resource::loadPrefabRuntimePayloadFromPackage(root, text("prefab/runtime"));
        assert(loaded.success);
        assert(loaded.asset.mesh.validForIndexedDraw());
        assert(loaded.asset.mesh.vertices.size() == asset.mesh.vertices.size());
        assert(loaded.asset.materials.size() == 1u);
        assert(loaded.asset.materials[0].baseColorTextureCoordinateSet == 2u);
        assert(loaded.asset.materials[0].normalTextureCoordinateSet == 1u);
        assert(loaded.asset.materials[0].hasCompleteMaterialImportMetadata);
        assert(loaded.asset.materials[0].hasUnsupportedMaterialExtensions);
        assert(loaded.defaultStateName == text("idle"));
        assert(loaded.stateBindings.size() == 2u);
        assert(loaded.hasCompleteControllerData);
        assert(loaded.controllerAsset.name == controller.name);
        assert(loaded.controllerAsset.defaultStateName == controller.defaultStateName);
        assert(loaded.controllerAsset.parameters.size() == 1u);
        assert(loaded.controllerAsset.parameters[0].name == controller.parameters[0].name);
        assert(loaded.controllerAsset.states.size() == 2u);
        assert(loaded.controllerAsset.states[1].clipName == controller.states[1].clipName);
        assert(loaded.controllerAsset.states[1].speed == controller.states[1].speed);
        assert(loaded.controllerAsset.transitions.size() == 1u);
        assert(loaded.controllerAsset.transitions[0].conditions.size() == 1u);
        assert(loaded.controllerAsset.transitions[0].conditions[0].parameter == text("moving"));

        const auto loadedByStableId =
            arp::resource::loadPrefabRuntimePayloadByResourceIdFromPackage(root, text(kPrefabId));
        assert(loadedByStableId.success);
        assert(loadedByStableId.asset.mesh.vertices.size() == loaded.asset.mesh.vertices.size());
        assert(loadedByStableId.defaultStateName == loaded.defaultStateName);
        assert(loadedByStableId.stateBindings.size() == loaded.stateBindings.size());
        assert(loadedByStableId.hasCompleteControllerData);
        assert(loadedByStableId.controllerAsset.transitions.size() == 1u);

        const auto missingStableId = arp::resource::loadPrefabRuntimePayloadByResourceIdFromPackage(
            root, text("prefab/not-in-this-package"));
        assert(!missingStableId.success);
        assertContains(missingStableId.diagnostics, "missing requested PrefabResource ID");

        auto escapedPayload = payload.resource;
        escapedPayload.blobPath = text("../outside-runtime-payload.bin");
        package.entries[0] = makeEntry(kRuntimePayloadId,
            "PrefabRuntimePayloadResource",
            "prefab/runtime/runtime-payload",
            "resources/runtime-payload.arpres.yaml",
            noDependencies,
            escapedPayload);
        const auto escapedPackage =
            arp::resource::InternalAssetPackageWriter().write(package, root);
        assert(escapedPackage.success);
        const auto escapedLoad =
            arp::resource::loadPrefabRuntimePayloadFromPackage(root, text("prefab/runtime"));
        assert(!escapedLoad.success);
        assertContains(escapedLoad.diagnostics, "not a safe relative package path");

        auto mismatchedPayload = payload.resource;
        ++mismatchedPayload.blobByteSize;
        package.entries[0] = makeEntry(kRuntimePayloadId,
            "PrefabRuntimePayloadResource",
            "prefab/runtime/runtime-payload",
            "resources/runtime-payload.arpres.yaml",
            noDependencies,
            mismatchedPayload);
        const auto mismatchedPackage =
            arp::resource::InternalAssetPackageWriter().write(package, root);
        assert(mismatchedPackage.success);
        const auto mismatchedLoad =
            arp::resource::loadPrefabRuntimePayloadFromPackage(root, text("prefab/runtime"));
        assert(!mismatchedLoad.success);
        assertContains(mismatchedLoad.diagnostics, "blob byte size mismatch");

        std::ifstream blobInput(root / "blobs/prefab-runtime.arpa.bin", std::ios::binary);
        auto malformedBytes = std::vector<std::uint8_t>(
            std::istreambuf_iterator<char>{blobInput}, std::istreambuf_iterator<char>{});
        assert(malformedBytes.size() > 20u);
        constexpr std::size_t stateBindingCountOffset = 16u;
        malformedBytes[stateBindingCountOffset] = 0xffu;
        malformedBytes[stateBindingCountOffset + 1u] = 0xffu;
        malformedBytes[stateBindingCountOffset + 2u] = 0xffu;
        malformedBytes[stateBindingCountOffset + 3u] = 0xffu;
        std::ofstream blobOutput(
            root / "blobs/prefab-runtime.arpa.bin", std::ios::binary | std::ios::trunc);
        blobOutput.write(reinterpret_cast<const char*>(malformedBytes.data()),
            static_cast<std::streamsize>(malformedBytes.size()));
        blobOutput.close();
        auto malformedPayload = payload.resource;
        malformedPayload.blobHash = static_cast<int>(hashBytes(malformedBytes));
        package.entries[0] = makeEntry(kRuntimePayloadId,
            "PrefabRuntimePayloadResource",
            "prefab/runtime/runtime-payload",
            "resources/runtime-payload.arpres.yaml",
            noDependencies,
            malformedPayload);
        const auto malformedPackage =
            arp::resource::InternalAssetPackageWriter().write(package, root);
        assert(malformedPackage.success);
        const auto malformedLoad =
            arp::resource::loadPrefabRuntimePayloadFromPackage(root, text("prefab/runtime"));
        assert(!malformedLoad.success);
        assertContains(malformedLoad.diagnostics, "runtime payload state bindings are invalid");
    }

    {
        const auto brokenPath = packagePath("missing_envelope");
        std::filesystem::create_directories(brokenPath);
        std::ofstream manifest(brokenPath / "manifest.arpkg.yaml", std::ios::binary);
        manifest << "format: ArpInternalAssetPackage\n"
                 << "version: 1\n"
                 << "name: broken\n"
                 << "status: Contract Complete\n"
                 << "entries:\n"
                 << "  - resourceId: " << kPrefabId << "\n"
                 << "    resourceType: PrefabResource\n"
                 << "    alias: prefab/missing\n"
                 << "    envelopePath: resources/missing.arpres.yaml\n"
                 << "    status: Prototype Complete\n"
                 << "    contentHash: 1\n"
                 << "    dependencies: []\n";
        manifest.close();

        const auto read = arp::resource::InternalAssetPackageReader().read(brokenPath);
        assert(!read.success);
        assertContains(read.diagnostics, "missing envelope file");
    }

    {
        const auto pikaPackageRoot = std::filesystem::path("assets/pika/unitychan-pika-visible");
        if (std::filesystem::exists(pikaPackageRoot / "manifest.arpkg.yaml")) {
            const auto loaded = arp::resource::loadPrefabRuntimePayloadFromPackage(
                pikaPackageRoot, text("prefab/unitychan-pika-visible"));
            if (!loaded.success) {
                std::cerr << loaded.diagnostics << "\n";
            }
            assert(loaded.success);
            assert(loaded.asset.mesh.validForIndexedDraw());
            assert(!loaded.asset.skinnedVertices.empty());
            assert(!loaded.asset.materials.empty());
            assert(!loaded.asset.textures.empty());
            assert(!loaded.asset.images.empty());
            assert(!loaded.asset.clips.empty());
        }
    }

    return 0;
}

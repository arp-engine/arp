#include "core/memory/cpu_allocator.h"
#include "resource/prefab_asset_resources.h"
#include "resource/resource_manager.h"

#include <cassert>
#include <iostream>
#include <type_traits>

namespace {

constexpr const char* kPrefabId = "0b5f7a2d-78a7-47d2-91f5-4ab0f8b827e1";
constexpr const char* kModelId = "56b4e6a3-938e-4014-b9e9-a9ad4eb2da39";
constexpr const char* kSkinnedMeshId = "05cbbe53-8361-493d-b0d5-9162d046931b";
constexpr const char* kSkeletonId = "ae1671d5-daf1-4bc2-9b1c-a926d1e5d876";
constexpr const char* kAvatarId = "73d742ef-f72e-4d25-8a39-d36f4146fe2f";
constexpr const char* kAnimationSetId = "b5fcf0f0-f526-4c72-9381-ec417f718d37";
constexpr const char* kAnimatorControllerId = "f0817aa6-f639-46c9-b9de-0ea317a2b94d";
constexpr const char* kMaterialSetId = "e210f17a-4608-4512-9d48-6eb2b8849a86";
constexpr const char* kSequenceId = "f51e8923-4448-4fa7-a2f4-a0baf6dd4653";
constexpr const char* kSequenceBindingSetId = "43b85bfd-22ef-4a9b-b789-fac2f61e6e3e";
constexpr const char* kPreviewStudioId = "88a9c646-6d56-42b0-a603-8a05e56e8c04";
constexpr const char* kBasicShaderId = "9f55fbf8-15e6-4f3c-855c-a3b8dbda9a1f";
constexpr const char* kTypedMaterialId = "d3a57e84-6d70-4ca8-89dd-0d4fb413a8d0";
constexpr const char* kStylizedShaderId = "48fb9fd6-4d77-4834-a0dc-c68d6f7ea5a1";
constexpr const char* kGuiShaderId = "bcaa074c-3c62-4df9-8e88-d9c1e7ac7ee5";
constexpr const char* kStartupSplashShaderId = "b68c73f1-4b2c-43c2-8077-a975f8ecba0f";

arp::core::pmr::String text(const char* value)
{
    return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
}

void assertContains(const arp::core::pmr::String& haystack, const char* needle)
{
    assert(haystack.find(needle) != arp::core::pmr::String::npos);
}

arp::resource::PrefabAssetGraphBuildDesc makeDesc()
{
    using namespace arp::resource;

    PrefabAssetGraphBuildDesc desc;
    desc.ids.rootId = text(kPrefabId);
    desc.ids.prefabId = text(kPrefabId);
    desc.ids.modelId = text(kModelId);
    desc.ids.skinnedMeshId = text(kSkinnedMeshId);
    desc.ids.skeletonId = text(kSkeletonId);
    desc.ids.avatarId = text(kAvatarId);
    desc.ids.animationSetId = text(kAnimationSetId);
    desc.ids.animatorControllerId = text(kAnimatorControllerId);
    desc.ids.materialSetId = text(kMaterialSetId);
    desc.ids.sequenceId = text(kSequenceId);
    desc.ids.sequenceBindingSetId = text(kSequenceBindingSetId);
    desc.ids.previewStudioId = text(kPreviewStudioId);
    desc.ids.basicShaderId = text(kBasicShaderId);
    desc.ids.stylizedShaderId = text(kStylizedShaderId);
    desc.ids.guiShaderId = text(kGuiShaderId);
    desc.ids.startupSplashShaderId = text(kStartupSplashShaderId);
    desc.alias = text("prefab/male000");
    desc.displayName = text("male000");
    desc.sourcePath = text("synthetic/male000");
    desc.sourceKind = text("SyntheticPrefabFolder");
    desc.shaderPaths.basicMaterialPreview = text("shaders/samples/material_preview_pbr.slang");
    desc.shaderPaths.stylizedScene = text("shaders/samples/stylized_scene.slang");
    desc.shaderPaths.guiOverlay = text("shaders/gui/gui_overlay.slang");
    desc.startupSplashShaderPath = text("shaders/samples/startup_splash.slang");
    return desc;
}

} // namespace

int main()
{
    using namespace arp::resource;

    registerPrefabAssetReflection();

    static_assert(std::is_base_of_v<ResourceBase, ExternalAssetSourceResource>);
    static_assert(std::is_base_of_v<ResourceBase, PrefabResource>);
    static_assert(std::is_base_of_v<ResourceBase, PrefabRuntimePayloadResource>);
    static_assert(std::is_base_of_v<ResourceBase, ModelResource>);
    static_assert(std::is_base_of_v<ResourceBase, SkinnedMeshResource>);
    static_assert(std::is_base_of_v<ResourceBase, SkeletonResource>);
    static_assert(std::is_base_of_v<ResourceBase, AvatarResource>);
    static_assert(std::is_base_of_v<ResourceBase, AnimationClipResource>);
    static_assert(std::is_base_of_v<ResourceBase, AnimationSetResource>);
    static_assert(std::is_base_of_v<ResourceBase, MaterialResource>);
    static_assert(std::is_base_of_v<ResourceBase, MaterialSetResource>);
    static_assert(std::is_base_of_v<ResourceBase, TextureResource>);
    static_assert(std::is_base_of_v<ResourceBase, AuthoringAssetResource>);
    static_assert(std::is_base_of_v<ResourceBase, ShaderGraphSourceResource>);
    static_assert(std::is_base_of_v<ResourceBase, SequenceResource>);
    static_assert(std::is_base_of_v<ResourceBase, SequenceBindingSetResource>);
    static_assert(std::is_base_of_v<ResourceBase, PhysicsColliderResource>);
    static_assert(std::is_base_of_v<ResourceBase, AnimatorControllerResource>);
    static_assert(std::is_base_of_v<ResourceBase, ShaderPackageResource>);
    static_assert(std::is_base_of_v<ResourceBase, PreviewStudioResource>);

    {
        const auto graph = PrefabAssetGraphBuilder().buildPackage(makeDesc());
        if (!graph.success) {
            std::cerr << graph.diagnostics << "\n";
        }
        assert(graph.success);
        assert(graph.ids.prefabId == text(kPrefabId));
        assert(graph.package.status == text("Prototype Complete"));
        assert(graph.package.entries.size() == 18u);
        assert(graph.package.entries[0].resourceType
            == text(ExternalAssetSourceResource::resourceTypeName()));
        assert(
            graph.package.entries[7].resourceType == text(SkinnedMeshResource::resourceTypeName()));
        assert(graph.package.entries[15].resourceType
            == text(SequenceBindingSetResource::resourceTypeName()));
        assert(
            graph.package.entries.back().resourceType == text(PrefabResource::resourceTypeName()));
        assert(graph.package.entries.back().resourceId == text(kPrefabId));
        assert(graph.package.entries.back().alias == text("prefab/male000"));
        assert(graph.package.entries.back().envelopePath
            == text(PrefabResource::defaultEnvelopePath()));
        assert(graph.package.entries.back().dependencies.size() == 15u);
        assert(graph.package.entries.back().dependencies[0].id == text(kModelId));
        assert(graph.package.entries.back().dependencies[0].usage == text("model"));

        arp::resource::ResourceManager manager;
        for (const auto& entry : graph.package.entries) {
            arp::core::pmr::String diagnostics(&arp::core::defaultCpuMemoryResource());
            auto typed =
                arp::resource::createPrefabResourceFromEnvelope(entry.envelopeYaml, diagnostics);
            if (!typed) {
                std::cerr << diagnostics << "\n";
                assert(false);
            }
            const auto loaded = manager.loadResource(std::move(typed));
            if (!loaded.success) {
                std::cerr << loaded.diagnostics << "\n";
            }
            assert(loaded.success);
        }
        const auto root = manager.findByAlias<PrefabResource>("prefab/male000");
        assert(root.ready());
        assert(root.id() == text(kPrefabId));
        assert(root.get()->modelId == text(kModelId));
        assert(root.get()->skinnedMeshId == text(kSkinnedMeshId));
        assert(root.get()->shaderPackageIds.size() == 4u);

        const auto shader = manager.find<ShaderPackageResource>(kBasicShaderId);
        assert(shader.ready());
        assert(shader.get()->slangPath == text("shaders/samples/material_preview_pbr.slang"));
        assert(shader.get()->spirvPath == text("shaders/samples/material_preview_pbr.spv"));
        assert(shader.get()->mslPath == text("shaders/samples/material_preview_pbr.metal"));
        assert(shader.get()->reflectionPath
            == text("shaders/samples/material_preview_pbr.reflection.yaml"));

        arp::resource::ResourceManager typedManager;
        ShaderPackageResource typedShader;
        typedShader.name = text("basic");
        typedShader.slangPath = text("shaders/samples/material_preview_pbr.slang");
        typedShader.spirvPath = text("shaders/samples/material_preview_pbr.spv");
        typedShader.mslPath = text("shaders/samples/material_preview_pbr.metal");
        typedShader.reflectionPath = text("shaders/samples/material_preview_pbr.reflection.yaml");
        typedShader.materialInterfaceContract = text(kPbrMetallicRoughnessContract);
        typedShader.materialInterfaceVersion = kPbrMetallicRoughnessContractVersion;
        AssetResourceRuntimeLoadDesc typedDesc;
        typedDesc.package.resourceId = text(kBasicShaderId);
        typedDesc.package.alias = text("shader/basic");
        typedDesc.package.displayName = text("basic shader");
        typedDesc.package.status = text("Contract Complete");
        const auto typedLoaded = typedShader.loadInto(typedManager, typedDesc);
        assert(typedLoaded.success);
        const auto* typedRecord = typedManager.recordSnapshot(kBasicShaderId);
        assert(typedRecord != nullptr);
        assert(typedRecord->ownedResource != nullptr);
        assert(typedRecord->value.is_undefined());
        assert(typedManager.find<ShaderPackageResource>(kBasicShaderId).ready());

        assert(typedManager.declareResource(kPrefabId, "ExternalAssetSourceResource").success);
        MaterialResource typedMaterial;
        typedMaterial.name = text("contract test material");
        typedMaterial.sourceResourceId = text(kPrefabId);
        typedMaterial.shaderPackageId = text(kBasicShaderId);
        typedMaterial.materialContractVersion = kPbrMetallicRoughnessContractVersion;
        typedMaterial.surfaceModel = text(kPbrMetallicRoughnessContract);
        typedMaterial.baseColorR = 0.2f;
        typedMaterial.baseColorG = 0.4f;
        typedMaterial.baseColorB = 0.6f;
        typedMaterial.emissiveG = 0.1f;
        typedMaterial.metallicFactor = 0.35f;
        typedMaterial.roughnessFactor = 0.7f;
        typedMaterial.alphaMode = text("MASK");
        typedMaterial.alphaCutoff = 0.4f;
        typedMaterial.doubleSided = true;
        AssetResourceRuntimeLoadDesc materialDesc;
        materialDesc.package.resourceId = text(kTypedMaterialId);
        materialDesc.package.alias = text("material/contract-test");
        materialDesc.package.displayName = text("contract test material");
        materialDesc.package.status = text("Contract Complete");
        assert(typedMaterial.loadInto(typedManager, materialDesc).success);
        const auto resolvedMaterial = typedManager.find<MaterialResource>(kTypedMaterialId);
        assert(resolvedMaterial.ready());
        assert(resolvedMaterial.get()->baseColorB == 0.6f);
        assert(resolvedMaterial.get()->emissiveG == 0.1f);
        assert(resolvedMaterial.get()->alphaMode == text("MASK"));
        assert(resolvedMaterial.get()->doubleSided);
        assert(validateMaterialContract(*resolvedMaterial.get(),
            *typedManager.find<ShaderPackageResource>(kBasicShaderId).get())
                .valid());
    }

    {
        auto desc = makeDesc();
        desc.ids.modelId = text(kPrefabId);
        const auto result = PrefabAssetGraphBuilder().buildPackage(desc);
        assert(!result.success);
        assertContains(result.diagnostics, "duplicate internal resource UUID");
    }

    {
        auto desc = makeDesc();
        desc.shaderPaths.basicMaterialPreview.clear();
        const auto result = PrefabAssetGraphBuilder().buildPackage(desc);
        assert(!result.success);
        assertContains(result.diagnostics, "shader resource requires Slang");
    }

    return 0;
}

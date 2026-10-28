#pragma once

#include "3d/scene_3d.h"
#include "cloud_fixture.h"
#include "core/io/file_io.h"
#include "renderer/volumetric_cloud/cloud_scene_binding.h"
#include "resource/cloud_asset_cooked.h"
#include "resource/resource_contract.h"

namespace arp::tests::cloud {

class SceneResourceFixture final {
public:
    resource::ResourceManager resources;
    resource::CloudAssetResourceDesc desc;
    resource::ResourceSourceSet sources;
    scene3d::Scene3DRenderQueue queue;
    renderer::cloud::CloudSceneBindingDesc bindingDesc;
    std::shared_ptr<const renderer::cloud::CloudSceneBinding> binding;
    core::pmr::String recipe;
    assets::cloud::CloudCookDesc recipeDesc;

    explicit SceneResourceFixture(
        renderer::cloud::CloudSceneReferenceMode mode, std::string_view identity = "cloud-scene")
    {
        desc.reference = {resource::deriveResourceUuid(identity, "density"),
            "CloudAssetResource",
            "cloud.scene",
            "Prototype Complete",
            "Cloud scene fixture"};
        desc.cookedPath = "fixture/cloud.cooked.yaml";
        desc.maxDocumentBytes = desc.maxPayloadBytes = 65536u;
        const auto document = core::readTextFile(
            std::filesystem::path(__FILE__).parent_path() / "scene-reference-v3.cook.yaml", 8192u);
        assert(document.success);
        recipe = document.text;
        assert(assets::cloud::CloudAssetCooker::parse(recipe, 8192u, recipeDesc).success);
        assets::cloud::CloudAsset cooked;
        assert(assets::cloud::CloudAssetCooker::cook(recipeDesc, cooked).success);
        setSource(cooked);
        assert(resources.load<resource::CloudAssetResource>(context(), desc).success);
        scene::Scene scene;
        auto& cloud = scene.createObject("cloud").addComponent<scene3d::VolumetricCloudComponent>();
        scene3d::CloudDesc authored;
        authored.layer = layer();
        authored.densityResource = mode == renderer::cloud::CloudSceneReferenceMode::ResourceId
            ? desc.reference.id
            : desc.reference.alias;
        assert(cloud.set_cloud(authored));
        const auto extracted =
            scene3d::Scene3DRenderExtractor{}.extract(scene, scene.extractTraversalSnapshot({}));
        assert(extracted.ok());
        const auto visible =
            scene3d::Scene3DVisibilitySystem{}.filterVisible(extracted.queue(), {});
        assert(visible.ok() && visible.queue().hasCloud());
        queue = visible.queue();
        bindingDesc.contractVersion = 1u;
        bindingDesc.prototypeOptIn = true;
        bindingDesc.referenceMode = mode;
        bindingDesc.maxReferenceBytes = 128u;
        bindingDesc.maxCpuBytes = 65536u;
        bindingDesc.quota = core::makeShared<core::CpuMemoryQuota>(bindingDesc.maxCpuBytes);
        assert(renderer::cloud::CloudSceneBinding::create(queue, resources, bindingDesc, binding)
                .success);
    }

    resource::ResourceLoadContext context() const
    {
        resource::ResourceLoadContext c;
        c.sources = &sources;
        c.requirePreloadedSources = true;
        return c;
    }

    void setSource(const assets::cloud::CloudAsset& a)
    {
        core::pmr::String cooked;
        assert(resource::CloudAssetCookedCodec::encode(a, desc.maxDocumentBytes, cooked).success);
        resource::ResourceSourceBuffer source;
        source.path = desc.cookedPath;
        source.role = "yaml-resource";
        source.bytes.assign(cooked.begin(), cooked.end());
        sources.clear();
        assert(sources.add(std::move(source)));
    }

    void setLayer(const scene3d::CloudLayerDesc& layer)
    {
        auto value = queue.cloud();
        value.layer = layer;
        queue.setCloud(value);
    }
};

} // namespace arp::tests::cloud

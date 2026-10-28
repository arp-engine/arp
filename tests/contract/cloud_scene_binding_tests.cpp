#include "renderer/volumetric_cloud/cloud_prepared_feature.h"
#include "renderer/volumetric_cloud/volumetric_cloud_feature.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/backends/null/null_hidden_frame_smoke.h"
#include "tests/fixtures/cloud/cloud_scene_fixture.h"

#include <cstring>
#include <iostream>
#include <thread>

using namespace arp;
using namespace renderer::cloud;

namespace {
void rejected(CloudResult result, const char* id)
{
    if (result.success || std::strcmp(result.diagnostic, id))
        std::cerr << "expected " << id << " actual " << result.diagnostic << '\n';
    assert(!result.success && !std::strcmp(result.diagnostic, id));
}

class WrongResource final : public resource::ResourceBase {
public:
    using ResourceBase::ResourceBase;

    resource::ResourceLoadResult load(
        resource::ResourceManager& manager, const resource::ResourceLoadContext&) override
    {
        return manager.admitResourceMetadata(*this);
    }
};

void contract(CloudSceneReferenceMode mode)
{
    tests::cloud::SceneResourceFixture f(mode);
    assert(f.binding->validateCurrent(f.queue, f.resources).success);
    assert(f.binding->asset()
        == f.resources.find<resource::CloudAssetResource>(f.desc.reference.id).get()->asset());
    assert(f.binding->asset()->source.algorithmVersion == 3u);
    assets::cloud::CloudCookDesc parsed;
    assert(assets::cloud::CloudAssetCooker::parse(f.recipe, 8192u, parsed).success);
    assets::cloud::CloudAsset recooked;
    assert(assets::cloud::CloudAssetCooker::cook(parsed, recooked).success);
    assert(recooked.cells.size() == f.binding->asset()->cells.size());
    assert(!std::memcmp(recooked.cells.data(),
        f.binding->asset()->cells.data(),
        recooked.cells.size() * sizeof(core::Vec4)));
    auto output = f.binding;
    const auto failCreate = [&](const auto& queue, const auto& desc, const char* id) {
        const auto charge = f.bindingDesc.quota->liveBytes();
        rejected(CloudSceneBinding::create(queue, f.resources, desc, output), id);
        assert(output == f.binding && f.bindingDesc.quota->liveBytes() == charge);
        assert(f.resources.stateOf(f.desc.reference.id) == resource::ResourceState::Ready);
    };
    auto d = f.bindingDesc;
    d.contractVersion = 0u;
    failCreate(f.queue, d, "CLOUD-SCENE-BINDING-OPT-IN");
    d = f.bindingDesc;
    d.prototypeOptIn = false;
    failCreate(f.queue, d, "CLOUD-SCENE-BINDING-OPT-IN");
    d = f.bindingDesc;
    d.referenceMode = CloudSceneReferenceMode::Unspecified;
    failCreate(f.queue, d, "CLOUD-SCENE-REFERENCE-MODE");
    d = f.bindingDesc;
    d.maxReferenceBytes = 1u;
    failCreate(f.queue, d, "CLOUD-SCENE-REFERENCE-BUDGET");
    d = f.bindingDesc;
    d.maxCpuBytes = 1u;
    d.quota = core::makeShared<core::CpuMemoryQuota>(1u);
    failCreate(f.queue, d, "CLOUD-SCENE-BINDING-BUDGET");
    assert(!d.quota->liveBytes());
    d = f.bindingDesc;
    d.maxCpuBytes++;
    failCreate(f.queue, d, "CLOUD-SCENE-BINDING-QUOTA");
    failCreate(scene3d::Scene3DRenderQueue{}, f.bindingDesc, "CLOUD-SCENE-CLOUD-ABSENT");
    auto q = f.queue;
    auto cloud = q.cloud();
    cloud.densityResource = "not-a-resource-id-or-file-path";
    q.setCloud(cloud);
    rejected(f.binding->validateCurrent(q, f.resources), "CLOUD-SCENE-REFERENCE-CHANGED");
    failCreate(q,
        f.bindingDesc,
        mode == CloudSceneReferenceMode::ResourceId ? "CLOUD-SCENE-RESOURCE-ID"
                                                    : "CLOUD-SCENE-REFERENCE-UNRESOLVED");
    auto layer = f.queue.cloud().layer;
    layer.coverage = .25f;
    f.setLayer(layer);
    assert(f.binding->validateCurrent(f.queue, f.resources).success);
    layer.extinction = -1.f;
    f.setLayer(layer);
    assert(!f.binding->validateCurrent(f.queue, f.resources).success);
    f.setLayer(tests::cloud::layer());
    std::thread thread([&] {
        rejected(
            f.binding->validateCurrent(f.queue, f.resources), "CLOUD-SCENE-BINDING-OWNER-THREAD");
    });
    thread.join();
    if (mode == CloudSceneReferenceMode::Alias) {
        tests::cloud::SceneResourceFixture retarget(mode, "other-resource");
        rejected(
            f.binding->validateCurrent(f.queue, retarget.resources), "CLOUD-SCENE-ALIAS-CHANGED");
    }
    auto changed = tests::cloud::asset();
    auto recipe = changed.source;
    ++recipe.seed;
    assert(assets::cloud::CloudAssetCooker::cook(recipe, changed).success);
    f.setSource(changed);
    const auto oldAsset = f.binding->asset();
    auto reload = f.resources.reloadResource(
        core::makeUnique<resource::CloudAssetResource>(f.desc), f.context());
    assert(reload.success && reload.telemetry.committed);
    rejected(f.binding->validateCurrent(f.queue, f.resources), "CLOUD-SCENE-RESOURCE-GENERATION");
    f.resources.releaseRetiredResources();
    assert(assets::cloud::CloudAssetCooker::validate(*oldAsset).success);
    assert(CloudSceneBinding::create(f.queue, f.resources, f.bindingDesc, output).success);
    assert(output->generation() != f.binding->generation());
    assert(f.resources.unload(f.desc.reference.id).success);
    rejected(output->validateCurrent(f.queue, f.resources), "CLOUD-SCENE-RESOURCE-NOT-READY");
    assert(f.resources.load<resource::CloudAssetResource>(f.context(), f.desc).success);
    rejected(output->validateCurrent(f.queue, f.resources), "CLOUD-SCENE-RESOURCE-GENERATION");
}

void missingAndType()
{
    tests::cloud::SceneResourceFixture f(CloudSceneReferenceMode::ResourceId);
    const auto id = resource::deriveResourceUuid("cloud-scene", "missing");
    auto cloud = f.queue.cloud();
    cloud.densityResource = id;
    f.queue.setCloud(cloud);
    auto output = f.binding;
    const auto notReady = [&] {
        rejected(CloudSceneBinding::create(f.queue, f.resources, f.bindingDesc, output),
            "CLOUD-SCENE-RESOURCE-NOT-READY");
        assert(output == f.binding);
    };
    notReady();
    assert(f.resources.declareResource(id, "CloudAssetResource").success);
    notReady();
    resource::ResourceReference fake{
        id, "CloudAssetResource", "", "Prototype Complete", "Wrong type"};
    assert(f.resources.load<WrongResource>({}, fake).success);
    rejected(CloudSceneBinding::create(f.queue, f.resources, f.bindingDesc, output),
        "CLOUD-SCENE-RESOURCE-TYPE");
    assert(f.resources.unload(id).success);
    auto bad = f.desc;
    bad.reference = fake;
    bad.reference.type = "InvalidCloudResourceType";
    assert(!f.resources.load<resource::CloudAssetResource>(f.context(), bad).success);
    notReady();

    const auto failedId = resource::deriveResourceUuid("cloud-scene", "failed");
    assert(f.resources.declareResource(failedId, "DeclaredWrongType").success);
    bad = f.desc;
    bad.reference.id = failedId;
    bad.reference.alias.clear();
    assert(!f.resources.load<resource::CloudAssetResource>(f.context(), bad).success);
    assert(f.resources.stateOf(failedId) == resource::ResourceState::Failed);
    cloud.densityResource = failedId;
    f.queue.setCloud(cloud);
    notReady();

    // Equal UUID/generation/content in another live registry is insufficient:
    // the accepted snapshot must retain the exact loaded asset owner.
    tests::cloud::SceneResourceFixture a(CloudSceneReferenceMode::ResourceId);
    tests::cloud::SceneResourceFixture b(CloudSceneReferenceMode::ResourceId);
    rejected(a.binding->validateCurrent(a.queue, b.resources), "CLOUD-SCENE-RESOURCE-OWNER");
}

void snapshotLifetime()
{
    std::shared_ptr<const CloudSceneBinding> snapshot;
    std::shared_ptr<core::CpuMemoryQuota> quota;
    {
        tests::cloud::SceneResourceFixture f(CloudSceneReferenceMode::Alias);
        snapshot = f.binding;
        quota = f.bindingDesc.quota;
    }
    assert(quota->liveBytes()
        && assets::cloud::CloudAssetCooker::validate(*snapshot->asset()).success);
    snapshot.reset();
    assert(!quota->liveBytes());
}

void nullHiddenFrame()
{
    tests::cloud::SceneResourceFixture f(CloudSceneReferenceMode::Alias);
    auto device = rhi::createNullDevice();
    assert(device && f.binding->validateCurrent(f.queue, f.resources).success);
    renderer::cloud::CloudPreparedPipelineCreateContext p{*device};
    auto foreignAsset = tests::cloud::asset();
    CloudPreparedResourceCreateContext r{*device, foreignAsset};
    CloudPreparedFeatureCreateContext c{p, r};
    VolumetricCloudFeature feature;
    core::pmr::String diagnostic;
    rejected(feature.createScene(f.binding, f.queue, f.resources, c, diagnostic),
        "CLOUD-SCENE-FEATURE-ASSET");
    assert(!feature.created());
    rejected(
        feature.createScene({}, f.queue, f.resources, c, diagnostic), "CLOUD-SCENE-BINDING-EMPTY");
    rhi::NullHiddenFrameSmokeRunner runner(*device);
    const auto report = runner.run({});
    assert(report.success && report.submittedFrameCount == 2u);
}
} // namespace

int main(int argc, char** argv)
{
    if (argc == 2 && !std::strcmp(argv[1], "--null-hidden-frame"))
        nullHiddenFrame();
    else {
        contract(CloudSceneReferenceMode::ResourceId);
        contract(CloudSceneReferenceMode::Alias);
        missingAndType();
        snapshotLifetime();
    }
    std::cout << "CloudSceneBinding/1 finite contract passed\n";
}

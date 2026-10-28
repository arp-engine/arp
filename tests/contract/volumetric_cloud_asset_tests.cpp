#include "3d/scene_3d.h"
#include "assets/cloud/cloud_asset.h"
#include "core/hash/sha256.h"
#include "pcg/simplex_noise.h"
#include "reflection/serialization.h"
#include "resource/cloud_asset_cooked.h"
#include "resource/cloud_asset_resource.h"
#include "resource/resource_contract.h"

#include <cassert>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>

namespace arp::scriptvm {
class ModuleRegistration;
}

namespace arp::reflection::generated {
bool register_cloud_contract_reflection(arp::scriptvm::ModuleRegistration*);
}

using namespace arp;
using namespace assets::cloud;

void noiseRange()
{
    CloudCookDesc d;
    d.algorithmVersion = kCloudLegacyCookAlgorithmVersion;
    d.seed = 137;
    d.weatherDimension = d.shapeDimension = d.detailDimension = 2;
    d.weatherFrequency = d.shapeFrequency = d.detailFrequency = 1;
    d.octaves = 1;
    d.maxOutputBytes = 4096;
    d.maxNoiseEvaluations = 4096;
    d.heightProfiles.fill({0, .1f, .9f, 1});
    CloudAsset a;
    assert(CloudAssetCooker::cook(d, a).success);
    // The first shape cell lies at (.25,.25,.25). Independently evaluate its
    // eight periodic images in double accumulation, using the PCG range API.
    double expected = 0;
    const double f = .25 * .25 * (3 - 2 * .25);
    for (int z = 0; z < 2; ++z)
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 2; ++x)
                expected += (x ? f : 1 - f) * (y ? f : 1 - f) * (z ? f : 1 - f)
                    * pcg::simplex3D(.25f - x, .25f - y, .25f - z, d.seed + 0x30000u);
    assert(std::fabs(a.cells[kCloudHeightProfileCount + 4].x - expected) < .000001);
    d.algorithmVersion = 1;
    assert(CloudAssetCooker::cook(d, a).diagnostic
        == std::string_view("CLOUD-ASSET-ALGORITHM-VERSION"));
}

void resources()
{
    std::ifstream file(ARP_CLOUD_SOURCE_DIR "/tests/fixtures/cloud/reference.cook.yaml");
    const core::pmr::String recipe{
        std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    CloudCookDesc desc;
    auto result = CloudAssetCooker::parse(recipe, 8192, desc);
    if (!result.success)
        std::cerr << result.diagnostic << '\n';
    assert(result.success);
    assert(desc.algorithmVersion == kCloudLegacyCookAlgorithmVersion);
    auto legacy = recipe;
    const auto version = legacy.find("algorithmVersion: 2");
    assert(version != legacy.npos);
    legacy.replace(version, std::strlen("algorithmVersion: 2"), "algorithmVersion: 1");
    CloudCookDesc legacyDesc;
    assert(CloudAssetCooker::parse(legacy, 8192, legacyDesc).diagnostic
        == std::string_view("CLOUD-ASSET-ALGORITHM-VERSION"));
    noiseRange();
    assert(desc.seed == 137 && desc.shapeDimension == 32);
    desc.weatherDimension = 8;
    desc.shapeDimension = 8;
    desc.detailDimension = 4;
    CloudAsset a, b;
    assert(CloudAssetCooker::cook(desc, a).success);
    assert(CloudAssetCooker::cook(desc, b).success);
    assert(CloudAssetCooker::validate(a).success);
    assert(a.cells.size() == 3 + 64 + 512 + 64);
    assert(std::memcmp(a.cells.data(), b.cells.data(), a.cells.size() * sizeof(core::Vec4)) == 0);
    auto altered = desc;
    ++altered.seed;
    assert(CloudAssetCooker::cook(altered, b).success);
    assert(std::memcmp(a.cells.data(), b.cells.data(), a.cells.size() * sizeof(core::Vec4)) != 0);
    for (const auto uv : {core::Vec2{0, 0}, core::Vec2{0.23f, 0.81f}, core::Vec2{-3.2f, 7.4f}}) {
        const auto x = CloudAssetCooker::sampleWeather(a, uv);
        const auto y = CloudAssetCooker::sampleWeather(a, {uv.x + 1, uv.y - 1});
        assert(std::fabs(x.x - y.x) < 0.000001f && std::fabs(x.y - y.y) < 0.000001f);
    }
    for (const auto uvw : {core::Vec3{0, 0, 0}, core::Vec3{0.31f, 0.72f, 0.28f}}) {
        const auto shifted = uvw + core::Vec3{1, -1, 2};
        assert(std::fabs(CloudAssetCooker::sampleShape(a, uvw)
                   - CloudAssetCooker::sampleShape(a, shifted))
            < 0.000001f);
        assert(std::fabs(CloudAssetCooker::sampleDetail(a, uvw)
                   - CloudAssetCooker::sampleDetail(a, shifted))
            < 0.000001f);
    }
    core::pmr::String cooked;
    result = resource::CloudAssetCookedCodec::encode(a, 65536, cooked);
    if (!result.success)
        std::cerr << result.diagnostic << '\n';
    assert(result.success);
    assert(cooked.find("arp.cloud.cooked") != cooked.npos);
    result = resource::CloudAssetCookedCodec::decode(cooked, 65536, 65536, b);
    if (!result.success)
        std::cerr << result.diagnostic << '\n';
    assert(result.success && b.cells.size() == a.cells.size());
    assert(std::memcmp(a.cells.data(), b.cells.data(), a.cells.size() * sizeof(core::Vec4)) == 0);
    assert(!resource::CloudAssetCookedCodec::decode(cooked, 1, 65536, b).success);
    resource::CloudAssetResourceDesc load;
    load.reference = {resource::deriveResourceUuid("cloud-reference", "density"),
        "CloudAssetResource",
        "cloud.reference",
        "Contract Complete",
        "Cloud reference"};
    load.cookedPath = "virtual/cloud.cooked.yaml";
    load.maxDocumentBytes = load.maxPayloadBytes = 65536u;
    resource::ResourceSourceSet sources;
    resource::ResourceSourceBuffer source;
    source.path = load.cookedPath;
    source.role = "yaml-resource";
    source.bytes.assign(cooked.begin(), cooked.end());
    assert(sources.add(std::move(source)));
    resource::ResourceLoadContext context;
    context.sources = &sources;
    context.requirePreloadedSources = true;
    resource::ResourceManager manager;
    const auto loaded = manager.load<resource::CloudAssetResource>(context, load);
    if (!loaded.success)
        std::cerr << loaded.diagnostics << '\n';
    assert(loaded.success);
    const auto handle = manager.find<resource::CloudAssetResource>(load.reference.id);
    assert(handle.ready() && handle.get()->asset()
        && handle.get()->processedByteCount() == a.cells.size() * sizeof(core::Vec4));
    resource::CloudAssetResource isolated(load);
    resource::ResourceLoadContext missing;
    missing.requirePreloadedSources = true;
    assert(!isolated.load(manager, missing).success && !isolated.asset());
    core::pmr::Vector<resource::ResourceFileReadIntent> intents;
    core::pmr::String diagnostic;
    assert(isolated.collectFileReadIntents(manager, intents, diagnostic));
    assert(intents.size() == 1u && intents[0].path == load.cookedPath
        && intents[0].maxBytes == 65536u);
    assert(!resource::CloudAssetCookedCodec::decode(cooked, 65536, 1, b).success);
    auto corrupt = cooked;
    const auto hash = corrupt.find("payloadSha256:");
    assert(hash != corrupt.npos);
    auto digit = corrupt.find_first_of("0123456789abcdef", hash + 14);
    assert(digit != corrupt.npos);
    corrupt[digit] = corrupt[digit] == '0' ? '1' : '0';
    assert(!resource::CloudAssetCookedCodec::decode(corrupt, 65536, 65536, b).success);
    core::pmr::String serialized;
    assert(
        !resource::CloudAssetCookedCodec::encode(a, 1, serialized).success && serialized.empty());
    altered = desc;
    altered.maxOutputBytes = 1;
    assert(!CloudAssetCooker::cook(altered, b).success && b.cells.empty());
    altered = desc;
    altered.maxNoiseEvaluations = 1;
    assert(!CloudAssetCooker::cook(altered, b).success);
    altered = desc;
    altered.shapeDimension = UINT32_MAX;
    assert(!CloudAssetCooker::cook(altered, b).success);
    altered = desc;
    altered.octaves = UINT32_MAX;
    assert(!CloudAssetCooker::cook(altered, b).success);
    altered = desc;
    altered.heightProfiles[0].w = std::numeric_limits<float>::quiet_NaN();
    assert(!CloudAssetCooker::cook(altered, b).success);
    b = a;
    b.cells.back().w = 1;
    assert(!CloudAssetCooker::validate(b).success);
    b = a;
    b.cells[0].x = 0.1f;
    assert(!CloudAssetCooker::validate(b).success);
    assert(!CloudAssetCooker::parse(recipe, 1, desc).success);
    assert(!CloudAssetCooker::parse(recipe + "\nunknown: 1\n", 8192, desc).success);
    assert(!CloudAssetCooker::parse(recipe + "\n---\ntype: other\n", 8192, desc).success);
    auto invalidRecipe = recipe;
    const auto space = invalidRecipe.find("linear");
    invalidRecipe.replace(space, 6, "srgb");
    assert(!CloudAssetCooker::parse(invalidRecipe, 8192, desc).success);
}

void components()
{
    assert(reflection::generated::register_cloud_contract_reflection(nullptr));
    scene::Scene scene;
    auto& root = scene.createObject("cloud");
    auto& cloud = root.addComponent<scene3d::VolumetricCloudComponent>();
    const auto extract = [&]() {
        return scene3d::Scene3DRenderExtractor{}.extract(scene, scene.extractTraversalSnapshot({}));
    };
    assert(extract().diagnostic().code == scene3d::Scene3DDiagnosticCode::InvalidCloud);
    scene3d::CloudDesc desc;
    desc.layer.planetCenter = {17, -6371000, 29};
    desc.layer.planetRadius = 6371000;
    desc.layer.baseAltitude = 1500;
    desc.layer.thickness = 2000;
    desc.layer.extinction = 0.002f;
    desc.layer.scatteringAlbedo = {0.9f, 0.8f, 0.7f};
    desc.layer.anisotropy = 0.4f;
    desc.layer.coverage = 0.55f;
    desc.layer.erosion = 0.2f;
    desc.layer.weatherPeriod = 40000;
    desc.layer.shapePeriod = 8000;
    desc.layer.detailPeriod = 700;
    desc.layer.windVelocity = {10, 0, 3};
    desc.densityResource = "cloud/reference";
    assert(cloud.set_cloud(desc));
    auto result = extract();
    assert(result.ok() && result.queue().hasCloud());
    assert(result.queue().cloud().densityResource == desc.densityResource);
    const auto serialized = reflection::serialize_to_yaml(reflection::object(desc));
    if (!serialized.success)
        std::cerr << serialized.diagnostics << '\n';
    assert(serialized.success);
    for (const auto projection :
        {scene::CameraProjection::Perspective, scene::CameraProjection::Orthographic}) {
        scene3d::Scene3DCameraBinding camera;
        camera.camera = scene::CameraComponentDesc::perspective(60.f, 1.f, .1f, 100.f).camera;
        camera.camera.position = {500000.f, 20000.f, -500000.f};
        camera.projection = projection;
        camera.orthographicWidth = 32.f;
        camera.orthographicHeight = 24.f;
        const auto visible =
            scene3d::Scene3DVisibilitySystem{}.filterVisible(result.queue(), camera);
        assert(visible.ok() && visible.queue().hasCloud() && visible.queue().items().empty());
        const auto forwarded =
            reflection::serialize_to_yaml(reflection::object(visible.queue().cloud()));
        assert(forwarded.success && forwarded.yaml == serialized.yaml);
        assert(!visible.telemetry().totalRenderables && !visible.telemetry().culledRenderables);

        auto invalidQueue = result.queue();
        invalidQueue.addItem({});
        const auto rejected =
            scene3d::Scene3DVisibilitySystem{}.filterVisible(invalidQueue, camera);
        assert(!rejected.ok()
            && rejected.diagnostic().code == scene3d::Scene3DDiagnosticCode::InvalidBounds);
        assert(!rejected.queue().hasCloud());
    }
    const auto restored = reflection::deserialize_from_yaml(serialized.yaml);
    if (!restored.success)
        std::cerr << restored.diagnostics << '\n';
    assert(restored.success);
    const auto* roundtrip = restored.value.cast<scene3d::CloudDesc*>();
    assert(roundtrip && roundtrip->layer.thickness == 2000
        && roundtrip->densityResource == desc.densityResource
        && roundtrip->layer.windVelocity.x == 10);
    auto& duplicate =
        scene.createObject("duplicate").addComponent<scene3d::VolumetricCloudComponent>();
    assert(duplicate.set_cloud(desc));
    assert(extract().diagnostic().code == scene3d::Scene3DDiagnosticCode::DuplicateGlobalCloud);
    duplicate.setEnabled(false);
    assert(extract().ok());
    cloud.setEnabled(false);
    const auto disabled = extract();
    assert(disabled.ok() && !disabled.queue().hasCloud());
    const auto absent = scene3d::Scene3DVisibilitySystem{}.filterVisible(disabled.queue(), {});
    assert(absent.ok() && !absent.queue().hasCloud());
    desc.layer.extinction = -1;
    assert(!cloud.set_cloud(desc));
    assert(cloud.desc().layer.extinction >= 0);
}

int main()
{
    resources();
    components();
    std::cout << "VolumetricCloud/1 deterministic assets, codec and components passed\n";
}

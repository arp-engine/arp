#include "renderer/pipeline/scene_pbr_cloud_composition.h"
#include "renderer/pipeline/scene_runtime_pipeline_payload.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/backends/null/null_hidden_frame_smoke.h"
#include "tests/fixtures/scene_pbr_fixture.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <limits>

using namespace arp;
using namespace arp::renderer;

namespace {
void expect(const char* actual, const char* expected)
{
    if (!actual || std::strcmp(actual, expected)) {
        std::cerr << "Expected " << expected << ", got " << (actual ? actual : "success") << '\n';
        std::abort();
    }
}

void geometryAndMaterials()
{
    tests::ScenePbrFixture f;
    auto& m = *f.opaque.material;
    m.metallicFactor = .4f;
    m.roughnessFactor = .7f;
    m.alphaMode = "MASK";
    m.doubleSided = true;
    m.emissiveR = 2.f;
    auto& source = f.opaque.mesh->vertices[0];
    const std::array p{1.f, 2.f, 3.f}, n{1.f, 1.f, 0.f};
    const std::array t{1.f, -1.f, 0.f, 1.f};
    std::copy(p.begin(), p.end(), source.position);
    std::copy(n.begin(), n.end(), source.normal);
    std::copy(t.begin(), t.end(), source.tangent);
    source.uv[0] = .25f;
    source.uv[1] = .75f;
    f.opaque.item.worldTransform.scale = {-2.f, 1.f, 3.f};
    f.opaque.item.worldTransform.translation = {10.f, 0.f, 0.f};
    std::shared_ptr<const ScenePbrGeometry> original;
    assert(!f.create(original));
    const auto& v = original->vertices()[0];
    assert(v.position.x == 8.f && v.position.y == 2.f && v.position.z == 9.f);
    assert(std::fabs(v.normal.x + 1.f / std::sqrt(5.f)) < 1.e-6f);
    assert(std::fabs(v.normal.y - 2.f / std::sqrt(5.f)) < 1.e-6f);
    assert(std::fabs(core::dot(core::Vec3{v.normal.x, v.normal.y, v.normal.z},
               core::Vec3{v.tangent.x, v.tangent.y, v.tangent.z}))
        < 1.e-6f);
    assert(v.tangent.w == -1.f && v.uv.x == .25f && v.uv.y == .75f);
    const auto& constants = original->sections()[0].constants;
    assert(constants.factors.x == .4f && constants.factors.y == .7f && constants.factors.w == 1.f
        && constants.flags.x == 1.f && constants.emissive.x == 2.f);
    const std::array bindings{f.opaque.binding()};
    assert(!original->validateCurrent(f.opaque.queue(), bindings));
    source.uv[0] = .5f;
    expect(original->validateCurrent(f.opaque.queue(), bindings), "SCENE-PBR-MESH-MUTATED");
    source.uv[0] = .25f;
    m.roughnessFactor = .6f;
    expect(original->validateCurrent(f.opaque.queue(), bindings), "SCENE-PBR-MATERIAL-MUTATED");
    m.roughnessFactor = .7f;
    const auto fail = [&](const char* id) {
        auto output = original;
        const auto live = f.budget.quota->liveBytes();
        expect(f.create(output), id);
        assert(output == original && f.budget.quota->liveBytes() == live);
    };
    m.alphaMode = "BLEND";
    fail("SCENE-PBR-ALPHA-PROFILE");
    m.alphaMode = "MASK";
    m.normalTextureId.clear();
    fail("SCENE-PBR-TEXTURE-IDENTITY");
    m.normalTextureId = tests::ScenePbrFixture::textureIds[1];
    m.textureBindingCount = 4;
    fail("SCENE-PBR-TEXTURE-COUNT");
    m.textureBindingCount = 5;
    m.surfaceModel = "unsupported";
    fail("SCENE-PBR-MATERIAL-CONTRACT");
    m.surfaceModel = resource::kPbrMetallicRoughnessContract;
    source.tangent[3] = 0.f;
    fail("SCENE-PBR-TANGENT-SIGN");
    source.tangent[3] = 1.f;
    std::fill_n(source.tangent, 3u, 0.f);
    fail("SCENE-PBR-TANGENT-SINGULAR");
    std::copy(t.begin(), t.end(), source.tangent);
    source.tangent[0] = std::numeric_limits<float>::quiet_NaN();
    fail("SCENE-PBR-TANGENT-NONFINITE");
    source.tangent[0] = t[0];
    f.budget.maxExpandedVertices = 2u;
    fail("SCENE-PBR-GEOMETRY-BUDGET");
    f.budget.maxExpandedVertices = 12u;
    f.budget.prototypeOptIn = false;
    fail("SCENE-PBR-GEOMETRY-OPT-IN");
    f.budget.prototypeOptIn = true;
    assert(!original->validateCurrent(f.opaque.queue(), bindings));

    // Two visible items, multiple authored sections; canonical queue order.
    f.opaque.mesh->indices = {0u, 1u, 2u, 0u, 2u, 1u};
    f.opaque.mesh->submeshes = {{0u, 3u, 0u}, {3u, 3u, 1u}};
    f.opaque.profile.maxIndices = 6u;
    f.opaque.profile.maxIndexBytes = 12u;
    f.opaque.profile.maxSections = f.opaque.profile.materialCount = 2u;
    auto second = core::makeShared<resource::MaterialResource>(m);
    second->roughnessFactor = .2f;
    const std::array<std::shared_ptr<const resource::MaterialResource>, 2u> materials{
        f.opaque.material, second};
    auto queue = f.opaque.queue();
    auto item = f.opaque.item;
    item.objectId = scene::GameObjectId::fromValue(2u);
    item.worldTransform.translation.x = 20.f;
    queue.addItem(item);
    std::array pairs{SceneOpaqueMeshBinding{1u, f.opaque.mesh, f.opaque.profile, materials},
        SceneOpaqueMeshBinding{0u, f.opaque.mesh, f.opaque.profile, materials}};
    std::shared_ptr<const ScenePbrGeometry> multi;
    assert(!ScenePbrGeometry::create(
        queue, pairs, f.opaque.shader, f.opaque.shaderId, f.budget, multi));
    assert(multi->vertices().size() == 12u && multi->sections().size() == 4u);
    for (std::size_t i = 0; i < 4u; ++i) {
        const auto& section = multi->sections()[i];
        assert(section.firstVertex == i * 3u && section.vertexCount == 3u);
        assert(section.itemIndex == i / 2u && section.materialSlot == i % 2u);
        assert(section.constants.factors.y == (i % 2u ? .2f : .7f));
    }
    assert(!multi->validateCurrent(queue, pairs));
    pairs[1].itemIndex = 1u;
    auto preserved = multi;
    assert(ScenePbrGeometry::create(
        queue, pairs, f.opaque.shader, f.opaque.shaderId, f.budget, preserved));
    assert(preserved == multi);
}

void camerasAndHdr()
{
    tests::ScenePbrFixture fixture;
    std::shared_ptr<const ScenePbrGeometry> geometry;
    assert(!fixture.create(geometry));
    const BasicForwardLighting light{
        1u, 1u, 1u, {0.f, 0.f, -1.f}, 1.f, {1.f, 1.f, 1.f}, {.1f, .1f, .1f}};
    for (bool orthographic : {false, true}) {
        cloud::CloudFrameState pending;
        const auto camera = core::Vec3{3.f, 5.f, -7.f};
        const auto vp = core::multiply(orthographic ? core::orthographic(10.f, 8.f, .1f, 50.f)
                                                    : core::perspective(1.f, 1.25f, .1f, 50.f),
            core::lookAt(camera, {1.f, 2.f, 3.f}));
        pending.parameters.inverseViewProjection = core::inverse(vp);
        pending.parameters.cameraTime = {camera.x, camera.y, camera.z, 0.f};
        pending.parameters.jitter.w = orthographic ? 1.f : 0.f;
        ScenePbrView view;
        assert(!ScenePbrCloudComposition::view(pending, view));
        ScenePbrFrameConstants frame;
        assert(!ScenePbrProducer::frameConstants(vp, light, view, *geometry, frame));
        assert(!ScenePbrCloudComposition::validateView(pending, frame));
        assert(frame.cameraPosition.w == (orthographic ? 0.f : 1.f));
        frame.cameraPosition.x += .125f;
        expect(
            ScenePbrCloudComposition::validateView(pending, frame), "SCENE-CLOUD-COMPOSITION-VIEW");
        pending.parameters.jitter.w = .5f;
        expect(ScenePbrCloudComposition::view(pending, view), "SCENE-CLOUD-COMPOSITION-VIEW");
    }
    core::Vec4 sentinel{1.f, 2.f, 3.f, 4.f};
    expect(ScenePbrProducer::viewConstants({}, sentinel), "SCENE-PBR-CAMERA");
    assert(sentinel.w == 4.f);
    expect(ScenePbrProducer::viewConstants({ScenePbrView::Kind::Orthographic, {}}, sentinel),
        "SCENE-PBR-CAMERA");
    auto excessive = light;
    excessive.intensity = SceneOpaqueProducer::maxLinearColor;
    ScenePbrFrameConstants frame;
    expect(ScenePbrProducer::frameConstants(core::Mat4::identity(),
               excessive,
               {ScenePbrView::Kind::Perspective, {}},
               *geometry,
               frame),
        "SCENE-PBR-RGBA16F-RANGE");
}

void pipelineAndPayload()
{
    auto device = rhi::createNullDevice();
    rhi::DescriptorLayoutDesc bindings;
    bindings.name = "ScenePbr.Contract";
    for (std::uint32_t i = 0; i < 9u; ++i)
        bindings.bindings.push_back({i,
            i == 0u || i == 2u ? rhi::DescriptorType::UniformBuffer
                : i == 1u      ? rhi::DescriptorType::ReadOnlyStorageBuffer
                : i == 8u      ? rhi::DescriptorType::Sampler
                               : rhi::DescriptorType::SampledTexture,
            i == 0u       ? rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment
                : i == 1u ? rhi::ShaderStage::Vertex
                          : rhi::ShaderStage::Fragment});
    rhi::PipelineLayoutDesc ld;
    ld.descriptorLayouts.push_back(bindings);
    ld.descriptorSets.push_back({0u, bindings.name});
    auto layout = device->createPipelineLayout(ld);
    assert(layout);
    rhi::ShaderPackageDesc shader;
    for (const auto& b : bindings.bindings)
        shader.reflection.descriptorBindings.push_back(rhi::makeShaderReflectionBinding(0u, b));
    rhi::RenderPipelineDesc d;
    d.layout = layout.get();
    d.shaderPackage = &shader;
    d.vertexShader.entryPoint = "vertexMain";
    d.fragmentShader.entryPoint = "fragmentMain";
    d.dynamicViewportScissor = true;
    d.cullMode = rhi::CullMode::None;
    d.colorFormat = rhi::TextureFormat::Rgba16Float;
    d.colorTargets = {{d.colorFormat}};
    d.depthFormat = rhi::TextureFormat::D32Float;
    d.depthStencil.depthTestEnabled = d.depthStencil.depthWriteEnabled = true;
    d.depthStencil.depthCompare = rhi::CompareOp::Less;
    assert(!ScenePbrProducer::validatePipeline(d));
    shader.reflection.descriptorBindings[0].stages = rhi::ShaderStage::Fragment;
    expect(ScenePbrProducer::validatePipeline(d), "SCENE-PBR-SHADER-ABI");
    shader.reflection.descriptorBindings[0].stages = bindings.bindings[0].stages;
    d.depthStencil.depthWriteEnabled = false;
    assert(ScenePbrProducer::validatePipeline(d));
    SceneRuntimePbrCloudPayload payload;
    CustomWorkloadPayloadRef ref{CustomWorkloadPayloadKind::SceneDrawPlan,
        &payload,
        kSceneRuntimePbrCloudPayloadContract,
        kSceneRuntimePbrCloudPayloadVersion};
    assert(sceneRuntimePbrCloudPayloadFrom(ref) == &payload);
    assert(!sceneRuntimeOpaqueCloudPayloadFrom(ref));
    const auto rejected = validateSceneRuntimePipelinePayload(ref);
    assert(!rejected.success && rejected.diagnostics == "SCENE-CLOUD-COMPOSITION-REQUIRED");
    ++ref.contractVersion;
    assert(!sceneRuntimePbrCloudPayloadFrom(ref));
    for (auto backend :
        {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU})
        assert(bool(ScenePbrProducer::textureExecutionDiagnostic(backend, 5u, 65536u))
            == (backend != rhi::Backend::Vulkan));
}

void nullSmoke()
{
    tests::ScenePbrFixture f;
    std::shared_ptr<const ScenePbrGeometry> geometry;
    assert(!f.create(geometry));
    auto device = rhi::createNullDevice();
    ScenePbrProducer producer;
    SceneOpaqueProducerBudget b;
    b.contractVersion = 1u;
    b.prototypeOptIn = true;
    b.maxCpuBytes = b.maxGpuBytes = 65536u;
    b.quota = core::makeShared<core::CpuMemoryQuota>(b.maxCpuBytes);
    b.sourceQuota = core::makeShared<core::CpuMemoryQuota>(65536u);
    b.retainedGpuBytes = core::makeShared<core::AtomicCapacityBudget>(b.maxGpuBytes);
    b.shaderSourceSha256.fill('a');
    std::array<ScenePbrMaterialBinding, 1u> bindings;
    expect(producer.create(*device, geometry, bindings, b), "RHI-ORIGINAL-DEVICE-LIFETIME-EXPIRED");
    auto lifetimeQuota = core::makeShared<core::CpuMemoryQuota>(b.maxCpuBytes);
    assert(!device->prepareOriginalDeviceLifetime(
        {1u, true, 1u, rhi::OriginalDeviceLifetimeContract::retainedOwnerCount, b.maxCpuBytes},
        lifetimeQuota));
    expect(producer.create(*device, geometry, bindings, b),
        "RHI-ORDINARY-TEXTURE-UPLOAD-NULL-VALIDATOR");
    assert(!producer.geometryBuffer() && !producer.geometry() && b.retainedGpuBytes->used() == 0u);
    rhi::NullHiddenFrameSmokeRunner runner(*device);
    const auto smoke =
        runner.run({"ScenePbr.NullContract", {32u, 24u}, rhi::TextureFormat::Bgra8Unorm, 2u});
    assert(smoke.success && smoke.submittedFrameCount == 2u);
    std::cout << "ScenePbr Null hidden-frame; native sampled content remains fail-closed\n";
}
} // namespace

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--null-hidden-frame")
        nullSmoke();
    else {
        geometryAndMaterials();
        camerasAndHdr();
        pipelineAndPayload();
    }
    std::cout << "ScenePbrProducer/1 CPU contract passed; no native authority\n";
}

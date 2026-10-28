#include "rhi/backends/null/null_backend.h"
#include "rhi/backends/null/null_hidden_frame_smoke.h"
#include "tests/fixtures/scene_opaque_fixture.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <limits>

using namespace arp;
using namespace renderer;

namespace {
void expect(const char* actual, const char* wanted)
{
    if (!actual || std::strcmp(actual, wanted))
        std::cerr << "expected " << wanted << " actual " << (actual ? actual : "success") << '\n';
    assert(actual && !std::strcmp(actual, wanted));
}

void geometry()
{
    tests::SceneOpaqueFixture f;
    const float position[]{1.f, 2.f, 3.f}, normal[]{1.f, 1.f, 0.f};
    std::copy_n(position, 3u, f.mesh->vertices[0].position);
    std::copy_n(normal, 3u, f.mesh->vertices[0].normal);
    f.item.worldTransform.translation = {10.f, 0.f, 0.f};
    f.item.worldTransform.scale = {2.f, 1.f, 1.f};
    f.item.baseColorTint = {.5f, 1.f, 1.f, 1.f};
    std::shared_ptr<const SceneOpaqueGeometry> original;
    assert(!f.create(original));
    const auto v = original->vertices()[0];
    assert(
        v.position.x == 12.f && v.position.y == 2.f && v.position.z == 3.f && v.position.w == 1.f);
    assert(std::abs(v.normal.x - 1.f / std::sqrt(5.f)) < .000001f);
    assert(std::abs(v.normal.y - 2.f / std::sqrt(5.f)) < .000001f);
    assert(v.albedo.x == .25f && v.albedo.y == .25f && v.albedo.z == .125f && v.albedo.w == 1.f);
    {
        f.item.worldTransform.scale = {2.e7f, 1.e7f, 1.e7f};
        std::shared_ptr<const SceneOpaqueGeometry> largeScale;
        assert(!f.create(largeScale));
        const auto transformed = largeScale->vertices()[0].normal;
        assert(std::abs(transformed.x - v.normal.x) < .000001f);
        assert(std::abs(transformed.y - v.normal.y) < .000001f);
        assert(transformed.z == 0.f);
        f.item.worldTransform.scale = {2.f, 1.f, 1.f};
    }
    {
        f.item.worldTransform.rotation = {0.f, 0.f, 1.e-8f, 0.f};
        std::shared_ptr<const SceneOpaqueGeometry> smallRotation;
        assert(!f.create(smallRotation));
        const auto transformed = smallRotation->vertices()[0];
        assert(transformed.position.x == 8.f && transformed.position.y == -2.f);
        assert(std::abs(transformed.normal.x + v.normal.x) < .000001f);
        assert(std::abs(transformed.normal.y + v.normal.y) < .000001f);
        f.item.worldTransform.rotation = {};
    }
    const auto fail = [&](const char* id) {
        const auto live = f.budget.quota->liveBytes();
        auto output = original;
        expect(f.create(output), id);
        assert(output == original && f.budget.quota->liveBytes() == live);
    };
    f.budget.prototypeOptIn = false;
    fail("SCENE-OPAQUE-GEOMETRY-OPT-IN");
    f.budget.prototypeOptIn = true;
    f.budget.maxExpandedVertices = 2u;
    fail("SCENE-OPAQUE-VERTEX-BUDGET");
    f.budget.maxExpandedVertices = 6u;
    f.profile.worldSpaceStatic = true;
    fail("SCENE-OPAQUE-LOCAL-MESH-PROFILE");
    f.profile.worldSpaceStatic = false;
    f.mesh->indices[2] = 3u;
    fail("BASIC-FORWARD-INDEX-RANGE");
    f.mesh->indices[2] = 2u;
    f.mesh->submeshes[0].materialSlot = 1u;
    fail("BASIC-FORWARD-SECTION-MATERIAL-RELATION");
    f.mesh->submeshes[0].materialSlot = 0u;
    std::fill_n(f.mesh->vertices[0].normal, 3u, 0.f);
    fail("BASIC-FORWARD-NORMAL-ZERO");
    std::copy_n(normal, 3u, f.mesh->vertices[0].normal);
    f.item.worldTransform.scale.x = 0.f;
    fail("SCENE-OPAQUE-TRANSFORM-SINGULAR");
    f.item.worldTransform.scale.x = 2.f;
    f.item.baseColorTint.w = .5f;
    fail("SCENE-OPAQUE-TINT");
    f.item.baseColorTint.w = 1.f;
    f.material->alphaMode = "Blend";
    fail("SCENE-OPAQUE-MATERIAL-PROFILE");
    f.material->alphaMode = "Opaque";
    f.material->baseColorTextureId = f.shaderId;
    f.material->textureBindingCount = 1u;
    fail("SCENE-OPAQUE-MATERIAL-PROFILE");
    f.material->baseColorTextureId.clear();
    f.material->textureBindingCount = 0u;
    f.material->surfaceModel = resource::kPbrMetallicRoughnessContract;
    fail("BASIC-FORWARD-MATERIAL-SHADER-CONTRACT");
    f.material->surfaceModel = kBasicForwardMaterialContract;
    {
        auto quota = f.budget.quota;
        f.budget.quota = core::makeShared<core::CpuMemoryQuota>(1u);
        f.budget.maxCpuBytes = 1u;
        fail("SCENE-OPAQUE-GEOMETRY-CPU-BUDGET");
        f.budget.quota = quota;
        f.budget.maxCpuBytes = quota->limitBytes();
    }
    std::array bindings{f.binding()};
    assert(!original->validateCurrent(f.queue(), bindings));
    ++bindings[0].profile.maxVertices;
    expect(original->validateCurrent(f.queue(), bindings), "SCENE-OPAQUE-SNAPSHOT-PROFILE");
    bindings[0] = f.binding();
    bindings[0].mesh = core::makeShared<assets::MeshAsset>(*f.mesh);
    expect(original->validateCurrent(f.queue(), bindings), "SCENE-OPAQUE-SNAPSHOT-OWNER");
    bindings[0] = f.binding();
    ++f.item.worldTransform.translation.x;
    expect(original->validateCurrent(f.queue(), bindings), "SCENE-OPAQUE-SNAPSHOT-CHANGED");
    --f.item.worldTransform.translation.x;
    const auto queue = f.queue();
    std::shared_ptr<const SceneOpaqueGeometry> output = original;
    expect(SceneOpaqueGeometry::create(queue, {}, f.shader, f.shaderId, f.budget, output),
        "SCENE-OPAQUE-BINDING-COVERAGE");
    auto pair = queue;
    auto second = f.item;
    second.objectId = scene::GameObjectId::fromValue(2u);
    second.kind = scene3d::Scene3DRenderItemKind::Terrain;
    second.terrain = {2u};
    pair.addItem(second);
    std::array two{f.binding(), f.binding()};
    expect(SceneOpaqueGeometry::create(pair, two, f.shader, f.shaderId, f.budget, output),
        "SCENE-OPAQUE-BINDING-DUPLICATE");
    two[1].itemIndex = 1u;
    assert(!SceneOpaqueGeometry::create(pair, two, f.shader, f.shaderId, f.budget, output));
    assert(output->vertices().size() == 6u);
    std::swap(two[0], two[1]);
    assert(!output->validateCurrent(pair, two));
    output.reset();
    original.reset();
    assert(f.budget.quota->liveBytes() == 0u);
}

void lighting()
{
    BasicForwardLighting light{
        1u, 1u, 1u, {0.f, 0.f, -2.f}, 2.f, {1.f, 1.f, 1.f}, {.25f, .25f, .25f}};
    BasicForwardLightingConstants result;
    expect(BasicForwardContract::lighting(light, BasicForwardLightingRange::Ldr1, result),
        "BASIC-FORWARD-LDR-IRRADIANCE-RANGE");
    assert(!BasicForwardContract::lighting(light, BasicForwardLightingRange::LinearHdr1, result));
    assert(result.directionIntensity.z == -1.f && result.directionIntensity.w == 2.f);
    light.intensity = std::numeric_limits<float>::infinity();
    expect(BasicForwardContract::lighting(light, BasicForwardLightingRange::LinearHdr1, result),
        "BASIC-FORWARD-LIGHT-BUDGET");
    light.intensity = std::numeric_limits<float>::max();
    light.color.x = 2.f;
    expect(BasicForwardContract::lighting(light, BasicForwardLightingRange::LinearHdr1, result),
        "BASIC-FORWARD-HDR-IRRADIANCE-RANGE");
}

void nullSmoke()
{
    tests::SceneOpaqueFixture f;
    std::shared_ptr<const SceneOpaqueGeometry> geometry;
    assert(!f.create(geometry));
    auto device = rhi::createNullDevice();
    SceneOpaqueProducer producer;
    std::shared_ptr<const SceneOpaqueFrame> frame;
    BasicForwardLighting light{1u, 1u, 0u, {}, 0.f, {}, {.25f, .25f, .25f}};
    expect(producer.prepareFrame(core::Mat4::identity(), light, frame), "SCENE-OPAQUE-NOT-CREATED");
    assert(!frame);
    SceneOpaqueProducerBudget b;
    expect(producer.create(*device, geometry, b), "SCENE-OPAQUE-PRODUCER-PROFILE");
    b.contractVersion = 1u;
    b.prototypeOptIn = true;
    b.maxCpuBytes = b.maxGpuBytes = 65536u;
    b.quota = core::makeShared<core::CpuMemoryQuota>(b.maxCpuBytes);
    b.sourceQuota = core::makeShared<core::CpuMemoryQuota>(65536u);
    b.retainedGpuBytes = core::makeShared<core::AtomicCapacityBudget>(b.maxGpuBytes);
    b.shaderSourceSha256.fill('a');
    // A Null device is not native ordinary buffer/pipeline execution authority.
    const auto* diagnostic = producer.create(*device, geometry, b);
    assert(diagnostic && !producer.geometryBuffer() && b.retainedGpuBytes->used() == 0u);
    rhi::NullHiddenFrameSmokeRunner runner(*device);
    const auto smoke =
        runner.run({"SceneOpaque.NullContract", {32u, 24u}, rhi::TextureFormat::Bgra8Unorm, 2u});
    assert(smoke.success && smoke.submittedFrameCount == 2u);
    std::cout << "SceneOpaque Null hidden-frame contract; native producer refused: " << diagnostic
              << '\n';
}

void pipelineContract()
{
    auto device = rhi::createNullDevice();
    rhi::DescriptorLayoutDesc bindings;
    bindings.name = "SceneOpaque.Contract";
    bindings.bindings = {{0u,
                             rhi::DescriptorType::UniformBuffer,
                             rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment},
        {1u, rhi::DescriptorType::ReadOnlyStorageBuffer, rhi::ShaderStage::Vertex}};
    rhi::PipelineLayoutDesc ld;
    ld.descriptorLayouts.push_back(bindings);
    ld.descriptorSets.push_back({0u, bindings.name});
    auto layout = device->createPipelineLayout(ld);
    assert(layout);
    rhi::ShaderPackageDesc shader;
    for (const auto& binding : bindings.bindings)
        shader.reflection.descriptorBindings.push_back(
            rhi::makeShaderReflectionBinding(0u, binding));
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
    assert(!SceneOpaqueProducer::validatePipeline(d));
    auto bad = d;
    bad.colorFormat = rhi::TextureFormat::Rgba8Unorm;
    expect(SceneOpaqueProducer::validatePipeline(bad), "SCENE-OPAQUE-PIPELINE-STATE");
    bad = d;
    bad.depthStencil.depthWriteEnabled = false;
    expect(SceneOpaqueProducer::validatePipeline(bad), "SCENE-OPAQUE-PIPELINE-STATE");
    bad = d;
    bad.fragmentShader.entryPoint = "other";
    expect(SceneOpaqueProducer::validatePipeline(bad), "SCENE-OPAQUE-SHADER-ABI");
    shader.reflection.descriptorBindings[1].stages = rhi::ShaderStage::Fragment;
    expect(SceneOpaqueProducer::validatePipeline(d), "SCENE-OPAQUE-SHADER-ABI");
}
} // namespace

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--null-hidden-frame")
        nullSmoke();
    else {
        geometry();
        lighting();
        pipelineContract();
    }
    std::cout << "SceneOpaqueProducer/1 contract passed\n";
}

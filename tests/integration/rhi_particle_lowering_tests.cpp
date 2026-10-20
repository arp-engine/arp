#include "renderer/particles/particle_draw_plan.h"
#include "renderer/particles/particle_rhi_draw_commands.h"
#include "renderer/render_frame.h"
#include "renderer/renderer_common.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/gpu_descriptors.h"
#include "rhi/gpu_device.h"
#include "rhi/gpu_pipeline.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

// ---------------------------------------------------------------------------
// Helpers: build minimal particle data and RHI objects through Null backend
// ---------------------------------------------------------------------------

namespace {

arp::rhi::PipelineLayoutDesc makeParticlePipelineLayoutDesc()
{
    arp::rhi::PipelineLayoutDesc desc;
    desc.pushConstantRanges.push_back({
        arp::rhi::ShaderStage::Vertex | arp::rhi::ShaderStage::Fragment,
        0,
        sizeof(arp::renderer::ParticlePushConstants),
    });
    return desc;
}

arp::rhi::ShaderPackageDesc makeParticleShaderPackage()
{
    arp::rhi::ShaderPackageDesc shaderPackage;
    shaderPackage.label = "ParticleTestShaderPackage";
    shaderPackage.slangModulePath = "particles.slang";
    shaderPackage.entrypoints.push_back(
        {"particles.slang", "vertexMain", arp::rhi::ShaderStage::Vertex});
    shaderPackage.entrypoints.push_back(
        {"particles.slang", "fragmentMain", arp::rhi::ShaderStage::Fragment});
    shaderPackage.artifacts.spirvPath = "particles.spv";
    shaderPackage.artifacts.mslPath = "particles.metal";
    shaderPackage.artifacts.reflectionPath = "particles.reflection.yaml";
    shaderPackage.reflection.pushConstantByteCount = sizeof(arp::renderer::ParticlePushConstants);
    return shaderPackage;
}

arp::rhi::RenderPipelineDesc makeParticlePipelineDesc(
    const arp::rhi::PipelineLayout& layout, const arp::rhi::ShaderPackageDesc& shaderPackage)
{
    arp::rhi::RenderPipelineDesc desc;
    desc.label = "ParticleTestPipeline";
    desc.layout = &layout;
    desc.shaderPackage = &shaderPackage;
    desc.vertexShader = {
        shaderPackage.slangModulePath, "vertexMain", arp::rhi::ShaderStage::Vertex};
    desc.fragmentShader = {
        shaderPackage.slangModulePath, "fragmentMain", arp::rhi::ShaderStage::Fragment};
    desc.colorFormat = arp::rhi::TextureFormat::Bgra8Unorm;
    desc.hasViewport = true;
    desc.viewport = {0.0f, 0.0f, 320.0f, 180.0f, 0.0f, 1.0f};
    desc.hasScissor = true;
    desc.scissor = {0, 0, 320, 180};
    // Explicit pipeline state (no native defaults — see L2 contract §4)
    desc.cullMode = arp::rhi::CullMode::None;
    desc.frontFace = arp::rhi::FrontFace::CounterClockwise;
    desc.topology = arp::rhi::PrimitiveTopology::TriangleList;
    desc.depthStencil.depthTestEnabled = false;
    desc.depthStencil.depthWriteEnabled = false;
    desc.colorTargets.push_back({
        arp::rhi::TextureFormat::Bgra8Unorm,
        false, // blendEnabled
        {arp::rhi::BlendFactor::One, arp::rhi::BlendFactor::Zero, arp::rhi::BlendOp::Add},
        {arp::rhi::BlendFactor::One, arp::rhi::BlendFactor::Zero, arp::rhi::BlendOp::Add},
        arp::rhi::ColorWriteMask::All,
        false,
    });
    desc.vertexBuffers.push_back({
        0,
        sizeof(arp::renderer::ParticleVertex),
        {
            {0,
                offsetof(arp::renderer::ParticleVertex, worldPosition),
                arp::rhi::VertexFormat::Float32x3},
            {1, offsetof(arp::renderer::ParticleVertex, corner), arp::rhi::VertexFormat::Float32x2},
            {2, offsetof(arp::renderer::ParticleVertex, color), arp::rhi::VertexFormat::Float32x4},
            {3, offsetof(arp::renderer::ParticleVertex, size), arp::rhi::VertexFormat::Float32x2},
        },
    });
    return desc;
}

arp::renderer::ParticleInstance makeTestParticle(float x, float y, float z)
{
    arp::renderer::ParticleInstance p;
    p.position = {x, y, z};
    p.velocity = {0.0f, 0.0f, 0.0f};
    p.color = {1.0f, 1.0f, 1.0f, 1.0f};
    p.size = 1.0f;
    p.ageSeconds = 0.5f;
    p.lifetimeSeconds = 2.0f;
    p.emitterIndex = 0;
    p.seed = 0;
    return p;
}

arp::renderer::SceneConstants makeSceneConstants()
{
    arp::renderer::SceneConstants scene;
    // Identity-ish view-projection so particles are visible
    // Orthographic: left=-10, right=10, bottom=-10, top=10, near=0, far=100
    const float l = -10.0f, r = 10.0f, b = -10.0f, t = 10.0f, n = 0.0f, f_ = 100.0f;
    const float m[16] = {
        2.0f / (r - l),
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        2.0f / (t - b),
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        1.0f / (f_ - n),
        0.0f,
        -(r + l) / (r - l),
        -(t + b) / (t - b),
        -n / (f_ - n),
        1.0f,
    };
    for (int i = 0; i < 16; ++i)
        scene.viewProjection[i] = m[i];
    scene.cameraPosition[0] = 0.0f;
    scene.cameraPosition[1] = 0.0f;
    scene.cameraPosition[2] = -10.0f;
    return scene;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Test 1: Particle pipeline creation through Null backend
// ---------------------------------------------------------------------------
static void test_particle_pipeline_creation()
{
    auto device = arp::rhi::createNullDevice();
    assert(device);

    const auto layoutDesc = makeParticlePipelineLayoutDesc();
    auto pipelineLayout = device->createPipelineLayout(layoutDesc);
    assert(pipelineLayout);

    const auto shaderPackage = makeParticleShaderPackage();
    const auto pipelineDesc = makeParticlePipelineDesc(*pipelineLayout, shaderPackage);

    auto pipeline = device->createRenderPipeline(pipelineDesc);
    assert(pipeline);
    assert(pipeline->layout());
    assert(pipeline->shaderSourceLanguage() == arp::rhi::ShaderSourceLanguage::Slang);
    assert(pipeline->backendFallbacksHiddenFromUpperLayer());
    assert(!pipeline->nativeHandleExposedToUpperLayer());

    const auto& report = pipeline->validationReport();
    assert(report.success);
    assert(report.pushConstantBytes == sizeof(arp::renderer::ParticlePushConstants));
    assert(report.colorTargetCount == 1);
    assert(report.blendStateValidated);
    assert(report.rasterStateValidated);
    assert(report.topologyValidated);
    assert(report.viewportScissorStateValidated);

    std::printf("  PASS particle_pipeline_creation\n");
}

// ---------------------------------------------------------------------------
// Test 2: Particle draw plan building (L2 semantics: vertex generation, sorting, bounds)
// ---------------------------------------------------------------------------
static void test_particle_draw_plan_build()
{
    const uint32_t kMaxParticles = 32;
    arp::core::pmr::Vector<arp::renderer::ParticleInstance> particles(
        &arp::core::defaultCpuMemoryResource());
    particles.push_back(makeTestParticle(1.0f, 2.0f, 3.0f));
    particles.push_back(makeTestParticle(-1.0f, 0.0f, 5.0f));

    const auto scene = makeSceneConstants();
    const auto plan = arp::renderer::buildParticleDrawPlan(particles, scene, kMaxParticles, true);

    assert(plan.success);
    assert(plan.diagnostics.empty());
    assert(!plan.vertices.empty());
    assert(plan.vertices.size() == particles.size() * 6); // 6 verts per quad
    assert(!plan.commands.empty());

    // Verify vertex data: each particle generates 6 vertices
    // Note: buildParticleDrawPlan sorts back-to-front, so the farthest
    // particle (dist²=226, at -1,0,5) appears first in sorted order.
    // Farthest particle at vertices[0..5]:
    assert(plan.vertices[0].worldPosition[0] == -1.0f);
    assert(plan.vertices[0].worldPosition[1] == 0.0f);
    assert(plan.vertices[0].worldPosition[2] == 5.0f);
    assert(plan.vertices[0].corner[0] == -1.0f);
    assert(plan.vertices[0].corner[1] == -1.0f);
    assert(plan.vertices[0].color[3] == 1.0f);
    assert(plan.vertices[0].size == 1.0f);
    // Nearest particle at vertices[6..11]:
    assert(plan.vertices[6].worldPosition[0] == 1.0f);
    assert(plan.vertices[6].worldPosition[1] == 2.0f);
    assert(plan.vertices[6].worldPosition[2] == 3.0f);

    // Verify draw command
    assert(plan.commands[0].vertexCount == plan.vertices.size());
    assert(plan.commands[0].instanceCount == 1);

    // Verify push constants
    assert(plan.pushConstantSizeBytes() == sizeof(arp::renderer::ParticlePushConstants));

    // Test empty plan
    arp::core::pmr::Vector<arp::renderer::ParticleInstance> empty(
        &arp::core::defaultCpuMemoryResource());
    const auto emptyPlan = arp::renderer::buildParticleDrawPlan(empty, scene, kMaxParticles, true);
    assert(!emptyPlan.success);
    assert(!emptyPlan.diagnostics.empty());

    std::printf("  PASS particle_draw_plan_build\n");
}

// ---------------------------------------------------------------------------
// Test 3: Particle draw plan budget enforcement
// ---------------------------------------------------------------------------
static void test_particle_draw_plan_budget()
{
    const uint32_t kMaxParticles = 1;
    arp::core::pmr::Vector<arp::renderer::ParticleInstance> particles(
        &arp::core::defaultCpuMemoryResource());
    particles.push_back(makeTestParticle(1.0f, 2.0f, 3.0f));
    particles.push_back(makeTestParticle(-1.0f, 0.0f, 5.0f));

    const auto scene = makeSceneConstants();
    const auto plan = arp::renderer::buildParticleDrawPlan(particles, scene, kMaxParticles, true);

    assert(!plan.success);
    assert(plan.diagnostics.find("budget") != arp::core::pmr::String::npos);

    std::printf("  PASS particle_draw_plan_budget\n");
}

// ---------------------------------------------------------------------------
// Test 4: Particle RHI draw command recording through Null backend
// ---------------------------------------------------------------------------
static void test_particle_rhi_draw_recording()
{
    auto device = arp::rhi::createNullDevice();
    assert(device);

    // Create pipeline
    const auto layoutDesc = makeParticlePipelineLayoutDesc();
    auto pipelineLayout = device->createPipelineLayout(layoutDesc);
    assert(pipelineLayout);
    const auto shaderPackage = makeParticleShaderPackage();
    const auto pipelineDesc = makeParticlePipelineDesc(*pipelineLayout, shaderPackage);
    auto pipeline = device->createRenderPipeline(pipelineDesc);
    assert(pipeline);

    // Create vertex and upload buffers
    constexpr uint32_t kMaxParticles = 8;
    constexpr uint64_t kBufferBytes = kMaxParticles * 6 * sizeof(arp::renderer::ParticleVertex);
    auto vertexBuffer = device->createBuffer({
        "ParticleTestVerts",
        kBufferBytes,
        arp::rhi::ResourceUsage::VertexBuffer | arp::rhi::ResourceUsage::CopyDst,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
    });
    assert(vertexBuffer);

    auto uploadBuffer = device->createBuffer({
        "ParticleTestUpload",
        kBufferBytes,
        arp::rhi::ResourceUsage::CopySrc,
        arp::rhi::MemoryUsage::CpuToGpu,
        arp::rhi::ResourceState::Undefined,
    });
    assert(uploadBuffer);

    // Build draw data
    arp::core::pmr::Vector<arp::renderer::ParticleInstance> particles(
        &arp::core::defaultCpuMemoryResource());
    particles.push_back(makeTestParticle(0.0f, 0.0f, 0.0f));
    particles.push_back(makeTestParticle(2.0f, 1.0f, 0.0f));

    const auto scene = makeSceneConstants();
    const auto drawPlan =
        arp::renderer::buildParticleDrawPlan(particles, scene, kMaxParticles, true);
    assert(drawPlan.success);

    // Create a color target texture
    auto colorTexture = device->createTexture({
        "ParticleTestColor",
        {320, 180, 1},
        arp::rhi::TextureFormat::Bgra8Unorm,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::CopySrc,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
    });
    assert(colorTexture);

    // Record via RHI through Null backend
    auto encoder = device->createCommandEncoder({"ParticleTestEncoder"});
    assert(encoder);

    arp::renderer::RenderFrameContext frameContext;
    frameContext.frame.success = true;
    frameContext.frame.frameSerial = 1;
    frameContext.colorTarget.extent = {320, 180};
    frameContext.colorTarget.format = arp::rhi::TextureFormat::Bgra8Unorm;
    frameContext.colorTexture = colorTexture.get();

    const auto result = arp::renderer::recordParticleDrawPlan(*encoder,
        arp::renderer::ParticleRhiDrawDesc{
            *colorTexture,
            *pipeline,
            *vertexBuffer,
            uploadBuffer.get(),
            drawPlan,
            true, // backendOwnedStaging
            1,    // frameSerial
        });
    assert(result.success);
    assert(result.diagnostics.empty());

    auto cmdBuffer = encoder->finish();
    assert(cmdBuffer);
    assert(cmdBuffer->commandCount() > 0);

    std::printf("  PASS particle_rhi_draw_recording\n");
}

// ---------------------------------------------------------------------------
// Test 5: Null backend rejects non-backend-owned staging
// ---------------------------------------------------------------------------
static void test_particle_null_rejects_foreign_staging()
{
    auto device = arp::rhi::createNullDevice();
    assert(device);

    // Create minimal pipeline
    const auto layoutDesc = makeParticlePipelineLayoutDesc();
    auto pipelineLayout = device->createPipelineLayout(layoutDesc);
    assert(pipelineLayout);
    const auto shaderPackage = makeParticleShaderPackage();
    const auto pipelineDesc = makeParticlePipelineDesc(*pipelineLayout, shaderPackage);
    auto pipeline = device->createRenderPipeline(pipelineDesc);
    assert(pipeline);

    constexpr uint32_t kMaxParticles = 4;
    constexpr uint64_t kBufferBytes = kMaxParticles * 6 * sizeof(arp::renderer::ParticleVertex);
    auto vertexBuffer = device->createBuffer({
        "ParticleTestVerts",
        kBufferBytes,
        arp::rhi::ResourceUsage::VertexBuffer | arp::rhi::ResourceUsage::CopyDst,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
    });
    assert(vertexBuffer);

    arp::core::pmr::Vector<arp::renderer::ParticleInstance> particles(
        &arp::core::defaultCpuMemoryResource());
    particles.push_back(makeTestParticle(0.0f, 0.0f, 0.0f));

    const auto scene = makeSceneConstants();
    const auto drawPlan =
        arp::renderer::buildParticleDrawPlan(particles, scene, kMaxParticles, true);
    assert(drawPlan.success);

    auto encoder = device->createCommandEncoder({"ParticleTestEncoder"});
    assert(encoder);

    auto colorTexture = device->createTexture({
        "ParticleTestColor",
        {320, 180, 1},
        arp::rhi::TextureFormat::Bgra8Unorm,
        arp::rhi::TextureUsage::RenderAttachment,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
    });
    assert(colorTexture);

    // backendOwnedStaging=false should be rejected by lowering contract §4
    const auto result = arp::renderer::recordParticleDrawPlan(*encoder,
        arp::renderer::ParticleRhiDrawDesc{
            *colorTexture,
            *pipeline,
            *vertexBuffer,
            nullptr, // no upload source
            drawPlan,
            false, // backendOwnedStaging = false → should fail
            1,
        });

    assert(!result.success);
    assert(!result.diagnostics.empty());
    assert(result.diagnostics.find("backend-owned") != arp::core::pmr::String::npos
        || result.diagnostics.find("staging") != arp::core::pmr::String::npos);

    std::printf("  PASS particle_null_rejects_foreign_staging\n");
}

// ---------------------------------------------------------------------------
// Test 6: Particle draw plan deterministic seed test
// ---------------------------------------------------------------------------
static void test_particle_deterministic_sort()
{
    const uint32_t kMaxParticles = 32;
    arp::core::pmr::Vector<arp::renderer::ParticleInstance> particles(
        &arp::core::defaultCpuMemoryResource());
    // Add particles at different distances from camera
    particles.push_back(makeTestParticle(5.0f, 0.0f, 0.0f)); // far
    particles.push_back(makeTestParticle(1.0f, 0.0f, 0.0f)); // near
    particles.push_back(makeTestParticle(3.0f, 0.0f, 0.0f)); // mid

    const auto scene = makeSceneConstants();
    // Camera at (0, 0, -10):
    // particle 0: distance = sqrt(25+100) ≈ 11.18
    // particle 1: distance = sqrt(1+100) ≈ 10.05
    // particle 2: distance = sqrt(9+100) ≈ 10.44
    // Sorted back-to-front: far (0) → mid (2) → near (1)

    const auto plan = arp::renderer::buildParticleDrawPlan(particles, scene, kMaxParticles, true);
    assert(plan.success);

    // Since vertices are append in sorted order, the first quad (6 vertices)
    // should be the farthest particle, and the last quad should be the nearest.
    // Particle 0 (farthest) first vertex worldPosition should be x=5
    assert(plan.vertices[0].worldPosition[0] == 5.0f);
    // Last particle vertices (nearest) should be x=1
    const auto lastVertex = plan.vertices.size() - 6;
    assert(plan.vertices[lastVertex].worldPosition[0] == 1.0f);

    // Running twice with same data should produce identical result
    const auto plan2 = arp::renderer::buildParticleDrawPlan(particles, scene, kMaxParticles, true);
    assert(plan2.vertices.size() == plan.vertices.size());
    for (std::size_t i = 0; i < plan.vertices.size(); ++i) {
        assert(plan.vertices[i].worldPosition[0] == plan2.vertices[i].worldPosition[0]);
    }

    std::printf("  PASS particle_deterministic_sort\n");
}

// ---------------------------------------------------------------------------
int main()
{
    std::printf("RHI particle lowering contract tests\n");
    test_particle_pipeline_creation();
    test_particle_draw_plan_build();
    test_particle_draw_plan_budget();
    test_particle_rhi_draw_recording();
    test_particle_null_rejects_foreign_staging();
    test_particle_deterministic_sort();
    std::printf("All RHI particle lowering contract tests passed.\n");
    return 0;
}

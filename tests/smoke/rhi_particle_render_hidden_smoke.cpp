#include "renderer/particles/particle_draw_plan.h"
#include "renderer/particles/particle_rhi_draw_commands.h"
#include "renderer/render_frame.h"
#include "renderer/renderer_common.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/gpu_descriptors.h"
#include "rhi/gpu_device.h"
#include "rhi/gpu_pipeline.h"
#include "rhi/gpu_swapchain.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

// ---------------------------------------------------------------------------
// Hidden-window smoke: runs the full particle render pipeline through the
// Null backend (device → pipeline → buffer → draw plan → RHI command recording
// → submit → present).
// ---------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kFrameCount = 2;
constexpr std::uint32_t kMaxParticles = 16;
constexpr std::uint32_t kWidth = 320;
constexpr std::uint32_t kHeight = 180;

arp::rhi::PipelineLayoutDesc makeLayoutDesc()
{
    arp::rhi::PipelineLayoutDesc desc;
    desc.pushConstantRanges.push_back({
        arp::rhi::ShaderStage::Vertex | arp::rhi::ShaderStage::Fragment,
        0,
        sizeof(arp::renderer::ParticlePushConstants),
    });
    return desc;
}

arp::rhi::ShaderPackageDesc makeShaderPackage()
{
    arp::rhi::ShaderPackageDesc pkg;
    pkg.label = "ParticleSmokeShaderPackage";
    pkg.slangModulePath = "particles.slang";
    pkg.entrypoints.push_back({"particles.slang", "vertexMain", arp::rhi::ShaderStage::Vertex});
    pkg.entrypoints.push_back({"particles.slang", "fragmentMain", arp::rhi::ShaderStage::Fragment});
    pkg.artifacts.spirvPath = "particles.spv";
    pkg.artifacts.mslPath = "particles.metal";
    pkg.artifacts.reflectionPath = "particles.reflection.yaml";
    pkg.reflection.pushConstantByteCount = sizeof(arp::renderer::ParticlePushConstants);
    return pkg;
}

arp::rhi::RenderPipelineDesc makePipelineDesc(
    const arp::rhi::PipelineLayout& layout, const arp::rhi::ShaderPackageDesc& pkg)
{
    arp::rhi::RenderPipelineDesc desc;
    desc.label = "ParticleSmokePipeline";
    desc.layout = &layout;
    desc.shaderPackage = &pkg;
    desc.vertexShader = {pkg.slangModulePath, "vertexMain", arp::rhi::ShaderStage::Vertex};
    desc.fragmentShader = {pkg.slangModulePath, "fragmentMain", arp::rhi::ShaderStage::Fragment};
    desc.colorFormat = arp::rhi::TextureFormat::Bgra8Unorm;
    desc.hasViewport = true;
    desc.viewport = {0.0f, 0.0f, kWidth, kHeight, 0.0f, 1.0f};
    desc.hasScissor = true;
    desc.scissor = {0, 0, kWidth, kHeight};
    desc.cullMode = arp::rhi::CullMode::None;
    desc.frontFace = arp::rhi::FrontFace::CounterClockwise;
    desc.topology = arp::rhi::PrimitiveTopology::TriangleList;
    desc.depthStencil.depthTestEnabled = false;
    desc.depthStencil.depthWriteEnabled = false;
    desc.colorTargets.push_back({
        arp::rhi::TextureFormat::Bgra8Unorm,
        false,
        {arp::rhi::BlendFactor::One, arp::rhi::BlendFactor::Zero, arp::rhi::BlendOp::Add},
        {arp::rhi::BlendFactor::One, arp::rhi::BlendFactor::Zero, arp::rhi::BlendOp::Add},
        arp::rhi::ColorWriteMask::All,
        false,
    });
    desc.vertexBuffers.push_back({
        0,
        sizeof(arp::renderer::ParticleVertex),
        arp::rhi::VertexInputRate::Vertex,
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

arp::renderer::SceneConstants makeScene()
{
    arp::renderer::SceneConstants scene;
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

void buildParticlesAlpha(arp::core::pmr::Vector<arp::renderer::ParticleInstance>& particles)
{
    for (uint32_t i = 0; i < 2; ++i) {
        arp::renderer::ParticleInstance p;
        p.position = {static_cast<float>(i) * 2.0f - 3.0f, 0.0f, 0.0f};
        p.velocity = {0.0f, 0.0f, 0.0f};
        p.color = {1.0f, 0.5f, 0.2f, 1.0f};
        p.size = 1.0f + static_cast<float>(i) * 0.25f;
        p.ageSeconds = 0.3f;
        p.lifetimeSeconds = 2.0f;
        p.emitterIndex = 0;
        p.seed = i;
        p.visualFamily = arp::renderer::ParticleVisualFamily::SpriteAlpha;
        particles.push_back(p);
    }
}

void buildParticlesAdditive(arp::core::pmr::Vector<arp::renderer::ParticleInstance>& particles)
{
    for (uint32_t i = 0; i < 2; ++i) {
        arp::renderer::ParticleInstance p;
        p.position = {static_cast<float>(i) * 2.0f + 1.0f, 0.0f, 0.0f};
        p.velocity = {0.0f, 0.0f, 0.0f};
        p.color = {0.2f, 0.6f, 1.0f, 0.5f};
        p.size = 0.8f + static_cast<float>(i) * 0.25f;
        p.ageSeconds = 0.3f;
        p.lifetimeSeconds = 2.0f;
        p.emitterIndex = 1;
        p.seed = i + 10;
        p.visualFamily = arp::renderer::ParticleVisualFamily::SpriteAdditive;
        particles.push_back(p);
    }
}

} // anonymous namespace

int main()
{
    std::printf("RHI particle render hidden smoke — Null backend\n");

    // 1. Create Null device
    auto device = arp::rhi::createNullDevice();
    if (!device) {
        std::fprintf(stderr, "FAIL: createNullDevice failed\n");
        return 1;
    }

    // 2. Create swapchain
    auto swapchain = device->createSwapchain({
        "ParticleSmokeSwapchain",
        {kWidth, kHeight},
        arp::rhi::TextureFormat::Bgra8Unorm,
        2u,
    });
    if (!swapchain) {
        std::fprintf(stderr, "FAIL: createSwapchain failed\n");
        return 1;
    }

    // 3. Create pipeline
    const auto layoutDesc = makeLayoutDesc();
    auto pipelineLayout = device->createPipelineLayout(layoutDesc);
    if (!pipelineLayout) {
        std::fprintf(stderr, "FAIL: createPipelineLayout failed\n");
        return 1;
    }

    const auto shaderPackage = makeShaderPackage();
    auto pipeline = device->createRenderPipeline(makePipelineDesc(*pipelineLayout, shaderPackage));
    if (!pipeline) {
        std::fprintf(stderr, "FAIL: createRenderPipeline failed\n");
        return 1;
    }

    // 4. Create vertex + upload buffers
    constexpr uint64_t kBufferBytes = kMaxParticles * 6 * sizeof(arp::renderer::ParticleVertex);
    auto vertexBuffer = device->createBuffer({
        "ParticleSmokeVerts",
        kBufferBytes,
        arp::rhi::ResourceUsage::VertexBuffer | arp::rhi::ResourceUsage::CopyDst,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
    });
    if (!vertexBuffer) {
        std::fprintf(stderr, "FAIL: createBuffer (vertex) failed\n");
        return 1;
    }
    auto uploadBuffer = device->createBuffer({
        "ParticleSmokeUpload",
        kBufferBytes,
        arp::rhi::ResourceUsage::CopySrc,
        arp::rhi::MemoryUsage::CpuToGpu,
        arp::rhi::ResourceState::Undefined,
    });
    if (!uploadBuffer) {
        std::fprintf(stderr, "FAIL: createBuffer (upload) failed\n");
        return 1;
    }

    // 5. Create color target texture
    auto colorTexture = device->createTexture({
        "ParticleSmokeColor",
        {kWidth, kHeight, 1},
        arp::rhi::TextureFormat::Bgra8Unorm,
        arp::rhi::TextureUsage::RenderAttachment,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
    });
    if (!colorTexture) {
        std::fprintf(stderr, "FAIL: createTexture failed\n");
        return 1;
    }

    // 6. Build particle data with mixed visual families (stable across frames)
    arp::core::pmr::Vector<arp::renderer::ParticleInstance> particles(
        &arp::core::defaultCpuMemoryResource());
    buildParticlesAlpha(particles);
    buildParticlesAdditive(particles);

    const auto scene = makeScene();

    // 7. Render smoke frames
    uint32_t successfulFrames = 0;
    for (uint32_t frameIndex = 0; frameIndex < kFrameCount; ++frameIndex) {
        const auto acquired = swapchain->acquireNextFrame();
        if (!acquired.success) {
            std::fprintf(
                stderr, "FAIL: acquireNextFrame failed: %s\n", acquired.diagnostics.c_str());
            continue;
        }

        auto encoder = device->createCommandEncoder({"ParticleSmokeFrame"});
        if (!encoder) {
            std::fprintf(stderr, "FAIL: createCommandEncoder failed\n");
            continue;
        }

        // Build draw plan set covering multiple visual families
        const auto drawSet =
            arp::renderer::buildParticleDrawPlanSet(particles, scene, kMaxParticles, true);
        if (!drawSet.success) {
            std::fprintf(
                stderr, "FAIL: buildParticleDrawPlanSet: %s\n", drawSet.diagnostics.c_str());
            continue;
        }
        if (drawSet.plans.size() < 2u) {
            std::fprintf(
                stderr, "FAIL: expected >=2 visual family plans, got %zu\n", drawSet.plans.size());
            continue;
        }

        // Record RHI draw commands for each family plan
        arp::renderer::RenderFrameContext frameContext;
        frameContext.frame.success = true;
        frameContext.frame.frameSerial = acquired.frameSerial;
        frameContext.colorTarget.extent = {kWidth, kHeight};
        frameContext.colorTarget.format = arp::rhi::TextureFormat::Bgra8Unorm;
        frameContext.colorTexture = colorTexture.get();

        for (const auto& plan : drawSet.plans) {
            const auto result = arp::renderer::recordParticleDrawPlan(*encoder,
                arp::renderer::ParticleRhiDrawDesc{
                    *colorTexture,
                    *pipeline,
                    *vertexBuffer,
                    uploadBuffer.get(),
                    plan,
                    true, // backendOwnedStaging
                    acquired.frameSerial,
                });
            if (!result.success) {
                std::fprintf(
                    stderr, "FAIL: recordParticleDrawPlan: %s\n", result.diagnostics.c_str());
                continue;
            }
        }

        auto cmdBuffer = encoder->finish();
        if (!cmdBuffer) {
            std::fprintf(stderr, "FAIL: finish command buffer failed\n");
            continue;
        }

        const auto submitted = device->queue().submit(*cmdBuffer);
        if (!submitted.success) {
            std::fprintf(stderr, "FAIL: queue submit: %s\n", submitted.diagnostics.c_str());
            continue;
        }

        const auto presented = swapchain->present(acquired);
        if (!presented.success) {
            std::fprintf(stderr, "FAIL: present: %s\n", presented.diagnostics.c_str());
            continue;
        }

        ++successfulFrames;
    }

    // 8. Verify smoke criteria
    if (successfulFrames != kFrameCount) {
        std::fprintf(stderr, "FAIL: only %u/%u frames succeeded\n", successfulFrames, kFrameCount);
        return 1;
    }

    std::printf("PASS: %u/%u particle render frames submitted and presented "
                "through Null backend (mixed visual families).\n",
        successfulFrames,
        kFrameCount);
    return 0;
}

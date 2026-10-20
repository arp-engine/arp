#include "renderer/particles/particle_draw_plan.h"
#include "renderer/particles/particle_renderer.h"
#include "renderer/particles/particle_rhi_draw_commands.h"
#include "renderer/render_frame.h"
#include "renderer/renderer_common.h"
#include "rhi/backends/metal/metal_device.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/backends/webgpu/webgpu_device.h"
#include "rhi/gpu_descriptors.h"
#include "rhi/gpu_device.h"
#include "rhi/gpu_pipeline.h"
#include "rhi/gpu_swapchain.h"
#include "rhi/semantics/native_backend_capabilities.h"
#include "rhi/semantics/webgpu_runtime_capability_bridge.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

// ---------------------------------------------------------------------------
// Hidden-window smoke: textured particle render pipeline through the Null
// backend. Creates a dummy texture, binds it via descriptor set, and verifies
// the textured particle draw path works end-to-end.
// ---------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kFrameCount = 2;
constexpr std::uint32_t kMaxParticles = 8;
constexpr std::uint32_t kWidth = 320;
constexpr std::uint32_t kHeight = 180;

arp::rhi_metal::MetalRhiDeviceDesc makeMetalDeviceDesc()
{
    auto contract = arp::rhi::semantics::nativeBackendCapabilityContract(arp::rhi::Backend::Metal);
    contract.queriedFromNativeDevice = true;
    contract.queriedExtendedFeatures = true;

    arp::rhi_metal::MetalRhiDeviceDesc desc;
    desc.label = "ParticleDepthFadeMetalContract";
    desc.capabilityContract = contract;
    desc.memoryBudgetBytes = 16u * 1024u * 1024u;
    desc.enableArgumentBuffers = true;
    desc.enablePushConstantUploadBuffer = true;
    desc.enableMemorylessTransientTextures = true;
    desc.enableDrawableLifecycleGuards = true;
    return desc;
}

arp::rhi_webgpu::WebGpuRhiDeviceDesc makeWebGpuDeviceDesc()
{
    arp::rhi::WebGpuRuntimeProbe probe;
    probe.browserRuntime = true;
    probe.canvasSurface = true;
    probe.nativeDevice = true;
    probe.computeDispatch = true;
    probe.storageTextureBinding = true;
    probe.storageBuffer = true;
    probe.maxTexture2DSize = 4096;
    probe.maxColorAttachments = 4;
    probe.maxDescriptorBindings = 16;
    probe.maxSampledTexturesPerShaderStage = 16;
    probe.uniformBufferOffsetAlignment = 256;
    probe.storageBufferOffsetAlignment = 256;
    probe.maxUniformBufferBindingSize = 65536;
    probe.maxStorageBufferBindingSize = 134217728;
    const auto profile = arp::rhi::buildWebGpuRuntimeProfile(probe);

    const auto bridge = arp::rhi::semantics::buildWebGpuRuntimeCapabilityContract(profile);
    assert(bridge.success);

    arp::rhi_webgpu::WebGpuRhiDeviceDesc desc;
    desc.label = "ParticleDepthFadeWebGpuContract";
    desc.runtimeProfile = profile;
    desc.capabilityContract = bridge.contract;
    desc.memoryBudgetBytes = 16u * 1024u * 1024u;
    desc.enableBackendDescriptorGroups = true;
    desc.enablePushConstantUploadRing = true;
    desc.pushConstantUploadRingByteCapacity = 4096;
    desc.enableCanvasLifecycleDiagnostics = true;
    desc.enableResizeDiagnostics = true;
    desc.enableSurfaceLostDiagnostics = true;
    desc.enableHiddenTabFrameSkipping = true;
    desc.requireComputeStorageRuntimePromotion = true;
    return desc;
}

arp::rhi::PipelineLayoutDesc makeLayoutDesc()
{
    arp::rhi::PipelineLayoutDesc desc;
    desc.descriptorLayouts.push_back({
        "ParticleTextureSet",
        {
            {0, arp::rhi::DescriptorType::SampledTexture, arp::rhi::ShaderStage::Fragment},
            {1, arp::rhi::DescriptorType::Sampler, arp::rhi::ShaderStage::Fragment},
        },
    });
    desc.descriptorSets.push_back({arp::renderer::kParticleTextureSetIndex, "ParticleTextureSet"});
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
    pkg.label = "ParticleTexturedSmokeShaderPackage";
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
    desc.label = "ParticleTexturedSmokePipeline";
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

void buildTexturedParticles(arp::core::pmr::Vector<arp::renderer::ParticleInstance>& particles)
{
    for (uint32_t i = 0; i < 2; ++i) {
        arp::renderer::ParticleInstance p;
        p.position = {static_cast<float>(i) * 2.0f - 1.0f, 0.0f, 0.0f};
        p.velocity = {0.0f, 0.0f, 0.0f};
        p.color = {1.0f, 1.0f, 1.0f, 1.0f};
        p.size = 1.0f;
        p.ageSeconds = 0.3f;
        p.lifetimeSeconds = 2.0f;
        p.emitterIndex = 0;
        p.seed = i;
        p.visualFamily = arp::renderer::ParticleVisualFamily::SpriteAlpha;
        p.isTextured = true;
        particles.push_back(p);
    }
}

} // anonymous namespace

// Forward declarations for focused tests defined after main
int verifyFocusedTests();

int main()
{
    std::printf("RHI textured particle render hidden smoke -- Null backend\n");

    // 1. Create Null device
    auto device = arp::rhi::createNullDevice();
    if (!device) {
        std::fprintf(stderr, "FAIL: createNullDevice failed\n");
        return 1;
    }

    // 2. Create swapchain
    auto swapchain = device->createSwapchain({
        "ParticleTexturedSmokeSwapchain",
        {kWidth, kHeight},
        arp::rhi::TextureFormat::Bgra8Unorm,
        2u,
    });
    if (!swapchain) {
        std::fprintf(stderr, "FAIL: createSwapchain failed\n");
        return 1;
    }

    // 3. Create pipeline layout with descriptor set for texture+sampler
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

    // 4. Create descriptor arena and allocate descriptor set
    auto descriptorArena = device->createDescriptorArena({
        "ParticleTexturedSmokeArena",
        1u,
        2u,
    });
    if (!descriptorArena) {
        std::fprintf(stderr, "FAIL: createDescriptorArena failed\n");
        return 1;
    }
    auto descriptorSet = descriptorArena->allocateDescriptorSet({
        "ParticleTextureSet",
        layoutDesc.descriptorLayouts[0],
    });
    if (!descriptorSet) {
        std::fprintf(stderr, "FAIL: allocateDescriptorSet failed\n");
        return 1;
    }

    // 5. Create sampler and dummy texture
    auto sampler = device->createSampler({
        "ParticleTexturedSmokeSampler",
        arp::rhi::SamplerFilter::Linear,
        arp::rhi::SamplerFilter::Linear,
        arp::rhi::SamplerAddressMode::ClampToEdge,
        arp::rhi::SamplerAddressMode::ClampToEdge,
        arp::rhi::SamplerAddressMode::ClampToEdge,
    });
    if (!sampler) {
        std::fprintf(stderr, "FAIL: createSampler failed\n");
        return 1;
    }

    auto dummyTexture = device->createTexture({
        "ParticleDummyTexture",
        {2, 2, 1},
        arp::rhi::TextureFormat::Rgba8Unorm,
        arp::rhi::TextureUsage::Sampled,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
    });
    if (!dummyTexture) {
        std::fprintf(stderr, "FAIL: createTexture (dummy) failed\n");
        return 1;
    }

    auto textureView = device->createTextureView(*dummyTexture,
        {
            "ParticleDummyTextureView",
            dummyTexture->desc().format,
            arp::rhi::TextureUsage::Sampled,
        });
    if (!textureView) {
        std::fprintf(stderr, "FAIL: createTextureView failed\n");
        return 1;
    }

    const auto textureWritten = descriptorSet->write({
        0,
        arp::rhi::DescriptorType::SampledTexture,
        textureView->id(),
        arp::rhi::DescriptorResourceKind::TextureView,
    });
    if (!textureWritten.success) {
        std::fprintf(stderr,
            "FAIL: descriptor texture write failed: %s\n",
            textureWritten.diagnostics.c_str());
        return 1;
    }
    const auto samplerWritten = descriptorSet->write({
        1,
        arp::rhi::DescriptorType::Sampler,
        sampler->id(),
        arp::rhi::DescriptorResourceKind::Sampler,
    });
    if (!samplerWritten.success) {
        std::fprintf(stderr,
            "FAIL: descriptor sampler write failed: %s\n",
            samplerWritten.diagnostics.c_str());
        return 1;
    }

    // 6. Create vertex + upload buffers
    constexpr uint64_t kBufferBytes = kMaxParticles * 6 * sizeof(arp::renderer::ParticleVertex);
    auto vertexBuffer = device->createBuffer({
        "ParticleTexturedSmokeVerts",
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
        "ParticleTexturedSmokeUpload",
        kBufferBytes,
        arp::rhi::ResourceUsage::CopySrc,
        arp::rhi::MemoryUsage::CpuToGpu,
        arp::rhi::ResourceState::Undefined,
    });
    if (!uploadBuffer) {
        std::fprintf(stderr, "FAIL: createBuffer (upload) failed\n");
        return 1;
    }

    // 7. Create color target texture
    auto colorTexture = device->createTexture({
        "ParticleTexturedSmokeColor",
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

    // 8. Build textured particle data
    arp::core::pmr::Vector<arp::renderer::ParticleInstance> particles(
        &arp::core::defaultCpuMemoryResource());
    buildTexturedParticles(particles);

    const auto scene = makeScene();

    // 9. Verify the production ParticleRenderer descriptor path.
    arp::renderer::ParticleRenderer particleRenderer(*device);
    const auto rendererCreated = particleRenderer.create({
        "particles.slang",
        arp::rhi::TextureFormat::Bgra8Unorm,
        {kWidth, kHeight},
        kMaxParticles,
    });
    if (!rendererCreated.success) {
        std::fprintf(stderr,
            "FAIL: ParticleRenderer create failed: %s\n",
            rendererCreated.diagnostics.c_str());
        return 1;
    }
    arp::renderer::RenderFrameContext rendererFrame;
    rendererFrame.frame.success = true;
    rendererFrame.frame.frameSerial = 0;
    rendererFrame.colorTarget.extent = {kWidth, kHeight};
    rendererFrame.colorTarget.format = arp::rhi::TextureFormat::Bgra8Unorm;
    rendererFrame.colorTexture = colorTexture.get();

    const auto missingBinding = particleRenderer.drawFrame(rendererFrame, particles, scene, true);
    if (missingBinding.success) {
        std::fprintf(stderr,
            "FAIL: textured ParticleRenderer draw succeeded without explicit texture binding\n");
        return 1;
    }

    arp::renderer::ParticleRenderer::ParticleTextureBinding textureBinding;
    textureBinding.texture = dummyTexture.get();
    textureBinding.resourceId =
        arp::core::pmr::String("texture/particle-dummy", &arp::core::defaultCpuMemoryResource());
    const auto rendererDrawn =
        particleRenderer.drawFrame(rendererFrame, particles, scene, true, &textureBinding);
    if (!rendererDrawn.success) {
        std::fprintf(stderr,
            "FAIL: textured ParticleRenderer draw failed: %s\n",
            rendererDrawn.diagnostics.c_str());
        return 1;
    }

    // 10. Render smoke frames with explicit manual texture binding
    uint32_t successfulFrames = 0;
    for (uint32_t frameIndex = 0; frameIndex < kFrameCount; ++frameIndex) {
        const auto acquired = swapchain->acquireNextFrame();
        if (!acquired.success) {
            std::fprintf(
                stderr, "FAIL: acquireNextFrame failed: %s\n", acquired.diagnostics.c_str());
            continue;
        }

        auto encoder = device->createCommandEncoder({"ParticleTexturedSmokeFrame"});
        if (!encoder) {
            std::fprintf(stderr, "FAIL: createCommandEncoder failed\n");
            continue;
        }

        // Build draw plan
        const auto drawPlan =
            arp::renderer::buildParticleDrawPlan(particles, scene, kMaxParticles, true);
        if (!drawPlan.success) {
            std::fprintf(stderr, "FAIL: buildParticleDrawPlan: %s\n", drawPlan.diagnostics.c_str());
            continue;
        }

        // Set texturing flag in push constants
        auto mutablePlan = drawPlan;
        mutablePlan.pushConstants.isTextured = 1;
        mutablePlan.pushConstants.visualFamily =
            static_cast<std::uint32_t>(arp::renderer::ParticleVisualFamily::SpriteAlpha);

        // Record RHI draw commands with texture descriptor set
        arp::renderer::RenderFrameContext frameContext;
        frameContext.frame.success = true;
        frameContext.frame.frameSerial = acquired.frameSerial;
        frameContext.colorTarget.extent = {kWidth, kHeight};
        frameContext.colorTarget.format = arp::rhi::TextureFormat::Bgra8Unorm;
        frameContext.colorTexture = colorTexture.get();

        const auto result = arp::renderer::recordParticleDrawPlan(*encoder,
            arp::renderer::ParticleRhiDrawDesc{
                *colorTexture,
                *pipeline,
                *vertexBuffer,
                uploadBuffer.get(),
                mutablePlan,
                true, // backendOwnedStaging
                acquired.frameSerial,
                descriptorSet.get(),
            });
        if (!result.success) {
            std::fprintf(stderr, "FAIL: recordParticleDrawPlan: %s\n", result.diagnostics.c_str());
            continue;
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

    // 10. Verify smoke criteria
    if (successfulFrames != kFrameCount) {
        std::fprintf(stderr, "FAIL: only %u/%u frames succeeded\n", successfulFrames, kFrameCount);
        return 1;
    }

    std::printf("PASS: %u/%u textured particle render frames submitted and presented "
                "through Null backend (texture + sampler bound).\n",
        successfulFrames,
        kFrameCount);

    // Run focused validation tests
    return verifyFocusedTests();
}

// ---------------------------------------------------------------------------
// Focused test: ParticleRenderer create with trailBeamShaderPath succeeds.
// ---------------------------------------------------------------------------
int testTrailBeamShaderPath()
{
    auto device = arp::rhi::createNullDevice();
    if (!device) {
        std::fprintf(stderr, "FAIL: createNullDevice failed\n");
        return 1;
    }

    arp::renderer::ParticleRenderer renderer(*device);
    arp::renderer::ParticleRendererDesc desc;
    desc.shaderPath = "particles.slang";
    desc.trailBeamShaderPath = "trail_beam.slang";
    desc.colorFormat = arp::rhi::TextureFormat::Bgra8Unorm;
    desc.extent = {kWidth, kHeight};
    desc.maxParticles = kMaxParticles;

    const auto result = renderer.create(desc);
    if (!result.success) {
        std::fprintf(stderr,
            "FAIL: ParticleRenderer create with trailBeamShaderPath failed: %s\n",
            result.diagnostics.c_str());
        return 1;
    }

    std::printf("PASS: ParticleRenderer create with trailBeamShaderPath succeeded.\n");
    return 0;
}

// ---------------------------------------------------------------------------
// Focused test: depthFadeEnabled == true && no depthTexture emits deterministic
// diagnostic.
// ---------------------------------------------------------------------------
int testDepthFadeNoDepthTexture()
{
    auto device = arp::rhi::createNullDevice();
    if (!device) {
        std::fprintf(stderr, "FAIL: createNullDevice failed\n");
        return 1;
    }

    // Create dummy color texture for the frame context
    auto colorTexture = device->createTexture({
        "ParticleDepthFadeTestColor",
        {kWidth, kHeight, 1},
        arp::rhi::TextureFormat::Bgra8Unorm,
        arp::rhi::TextureUsage::RenderAttachment,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
    });
    if (!colorTexture) {
        std::fprintf(stderr, "FAIL: createTexture (depth fade test) failed\n");
        return 1;
    }

    arp::renderer::ParticleRenderer renderer(*device);
    const auto rendererCreated = renderer.create({
        "particles.slang",
        arp::rhi::TextureFormat::Bgra8Unorm,
        {kWidth, kHeight},
        kMaxParticles,
    });
    if (!rendererCreated.success) {
        std::fprintf(stderr,
            "FAIL: ParticleRenderer create failed: %s\n",
            rendererCreated.diagnostics.c_str());
        return 1;
    }

    // Build particles with depthFadeEnabled set
    arp::core::pmr::Vector<arp::renderer::ParticleInstance> particles(
        &arp::core::defaultCpuMemoryResource());
    for (uint32_t i = 0; i < 2; ++i) {
        arp::renderer::ParticleInstance p;
        p.position = {static_cast<float>(i) * 2.0f - 1.0f, 0.0f, 0.0f};
        p.velocity = {0.0f, 0.0f, 0.0f};
        p.color = {1.0f, 1.0f, 1.0f, 1.0f};
        p.size = 1.0f;
        p.ageSeconds = 0.3f;
        p.lifetimeSeconds = 2.0f;
        p.emitterIndex = 0;
        p.seed = i;
        p.visualFamily = arp::renderer::ParticleVisualFamily::SpriteAlpha;
        p.depthFadeEnabled = 1u; // Enable depth fade
        particles.push_back(p);
    }

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

    // Frame context with NO depth texture → must produce deterministic diagnostic
    arp::renderer::RenderFrameContext frame;
    frame.frame.success = true;
    frame.frame.frameSerial = 0;
    frame.colorTarget.extent = {kWidth, kHeight};
    frame.colorTarget.format = arp::rhi::TextureFormat::Bgra8Unorm;
    frame.colorTexture = colorTexture.get();
    // depthTexture left as nullptr

    const auto result = renderer.drawFrame(frame, particles, scene, true);
    if (result.success) {
        std::fprintf(stderr,
            "FAIL: depth-fade draw succeeded despite missing depth texture; "
            "expected deterministic diagnostic\n");
        return 1;
    }

    std::printf("PASS: depthFadeEnabled + no depthTexture correctly returned diagnostic: %s\n",
        result.diagnostics.c_str());
    return 0;
}

template <typename DeviceFactory>
int testDepthFadeUnsupportedBackendDiagnostic(
    const char* backendLabel, DeviceFactory&& makeDevice, const char* expectedNeedle)
{
    auto device = makeDevice();
    if (!device) {
        std::fprintf(stderr, "FAIL: create %s device failed\n", backendLabel);
        return 1;
    }

    auto colorTexture = device->createTexture({
        "ParticleDepthFadeUnsupportedColor",
        {kWidth, kHeight, 1},
        arp::rhi::TextureFormat::Bgra8Unorm,
        arp::rhi::TextureUsage::RenderAttachment,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
    });
    if (!colorTexture) {
        std::fprintf(
            stderr, "FAIL: createTexture (%s depth fade unsupported test) failed\n", backendLabel);
        return 1;
    }

    auto depthTexture = device->createTexture({
        "ParticleDepthFadeUnsupportedDepth",
        {kWidth, kHeight, 1},
        arp::rhi::TextureFormat::D32Float,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::DepthAttachment,
    });
    if (!depthTexture) {
        std::fprintf(stderr,
            "FAIL: create depth texture (%s depth fade unsupported test) failed\n",
            backendLabel);
        return 1;
    }

    arp::renderer::ParticleRenderer renderer(*device);
    arp::renderer::ParticleRendererDesc desc;
    desc.shaderPath = "particles.slang";
    desc.colorFormat = arp::rhi::TextureFormat::Bgra8Unorm;
    desc.extent = {kWidth, kHeight};
    desc.maxParticles = kMaxParticles;

    const auto rendererCreated = renderer.create(desc);
    if (!rendererCreated.success) {
        std::fprintf(stderr,
            "FAIL: ParticleRenderer create failed for %s: %s\n",
            backendLabel,
            rendererCreated.diagnostics.c_str());
        return 1;
    }

    arp::core::pmr::Vector<arp::renderer::ParticleInstance> particles(
        &arp::core::defaultCpuMemoryResource());
    arp::renderer::ParticleInstance p;
    p.position = {0.0f, 0.0f, 0.0f};
    p.velocity = {0.0f, 0.0f, 0.0f};
    p.color = {1.0f, 1.0f, 1.0f, 1.0f};
    p.size = 1.0f;
    p.ageSeconds = 0.2f;
    p.lifetimeSeconds = 1.0f;
    p.visualFamily = arp::renderer::ParticleVisualFamily::SpriteAlpha;
    p.depthFadeEnabled = true;
    p.depthFadeDistance = 0.25f;
    particles.push_back(p);

    auto scene = makeScene();

    arp::renderer::RenderFrameContext frame;
    frame.frame.success = true;
    frame.frame.frameSerial = 0;
    frame.colorTarget.extent = {kWidth, kHeight};
    frame.colorTarget.format = arp::rhi::TextureFormat::Bgra8Unorm;
    frame.colorTexture = colorTexture.get();
    frame.depthTexture = depthTexture.get();

    const auto result = renderer.drawFrame(frame, particles, scene, true);
    if (result.success) {
        std::fprintf(stderr,
            "FAIL: depth fade unexpectedly succeeded on unsupported backend %s\n",
            backendLabel);
        return 1;
    }
    if (result.diagnostics.find(expectedNeedle) == std::string::npos) {
        std::fprintf(stderr,
            "FAIL: unexpected %s depth-fade diagnostic: %s\n",
            backendLabel,
            result.diagnostics.c_str());
        return 1;
    }

    std::printf("PASS: %s depth-fade path returned deterministic diagnostic: %s\n",
        backendLabel,
        result.diagnostics.c_str());
    return 0;
}

// ---------------------------------------------------------------------------
// Focused test: MeshParticle prototype renderer path succeeds with explicit
// shader and known prototype mesh id.
// ---------------------------------------------------------------------------
int testMeshParticlePrototypePath()
{
    auto device = arp::rhi::createNullDevice();
    if (!device) {
        std::fprintf(stderr, "FAIL: createNullDevice failed\n");
        return 1;
    }

    auto colorTexture = device->createTexture({
        "MeshParticleTestColor",
        {kWidth, kHeight, 1},
        arp::rhi::TextureFormat::Bgra8Unorm,
        arp::rhi::TextureUsage::RenderAttachment,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
    });
    if (!colorTexture) {
        std::fprintf(stderr, "FAIL: createTexture (mesh particle test) failed\n");
        return 1;
    }

    arp::renderer::ParticleRenderer renderer(*device);
    arp::renderer::ParticleRendererDesc desc;
    desc.shaderPath = "particles.slang";
    desc.trailBeamShaderPath = "trail_beam.slang";
    desc.meshParticleShaderPath = "mesh_particle.slang";
    desc.colorFormat = arp::rhi::TextureFormat::Bgra8Unorm;
    desc.extent = {kWidth, kHeight};
    desc.maxParticles = kMaxParticles;

    const auto created = renderer.create(desc);
    if (!created.success) {
        std::fprintf(stderr,
            "FAIL: ParticleRenderer create for mesh particle path failed: %s\n",
            created.diagnostics.c_str());
        return 1;
    }

    arp::core::pmr::Vector<arp::renderer::ParticleInstance> particles(
        &arp::core::defaultCpuMemoryResource());
    arp::renderer::ParticleInstance p;
    p.position = {0.0f, 0.0f, 0.0f};
    p.velocity = {0.0f, 0.0f, 0.0f};
    p.color = {0.9f, 0.7f, 0.3f, 1.0f};
    p.size = 1.25f;
    p.rotation = 0.35f;
    p.ageSeconds = 0.2f;
    p.lifetimeSeconds = 1.0f;
    p.visualFamily = arp::renderer::ParticleVisualFamily::MeshParticle;
    p.meshAssetId = "debris_rock";
    particles.push_back(p);

    auto scene = makeScene();

    arp::renderer::RenderFrameContext frame;
    frame.frame.success = true;
    frame.frame.frameSerial = 0;
    frame.colorTarget.extent = {kWidth, kHeight};
    frame.colorTarget.format = arp::rhi::TextureFormat::Bgra8Unorm;
    frame.colorTexture = colorTexture.get();

    const auto result = renderer.drawFrame(frame, particles, scene, true);
    if (!result.success) {
        std::fprintf(
            stderr, "FAIL: mesh particle prototype draw failed: %s\n", result.diagnostics.c_str());
        return 1;
    }

    std::printf("PASS: mesh particle prototype path rendered through Null backend.\n");
    return 0;
}

// ---------------------------------------------------------------------------
// Focused test: unknown MeshParticle mesh id fails with deterministic
// diagnostic instead of silent fallback.
// ---------------------------------------------------------------------------
int testMeshParticleUnknownMeshDiagnostic()
{
    auto device = arp::rhi::createNullDevice();
    if (!device) {
        std::fprintf(stderr, "FAIL: createNullDevice failed\n");
        return 1;
    }

    auto colorTexture = device->createTexture({
        "MeshParticleUnknownColor",
        {kWidth, kHeight, 1},
        arp::rhi::TextureFormat::Bgra8Unorm,
        arp::rhi::TextureUsage::RenderAttachment,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
    });
    if (!colorTexture) {
        std::fprintf(stderr, "FAIL: createTexture (mesh particle diagnostic test) failed\n");
        return 1;
    }

    arp::renderer::ParticleRenderer renderer(*device);
    arp::renderer::ParticleRendererDesc desc;
    desc.shaderPath = "particles.slang";
    desc.meshParticleShaderPath = "mesh_particle.slang";
    desc.colorFormat = arp::rhi::TextureFormat::Bgra8Unorm;
    desc.extent = {kWidth, kHeight};
    desc.maxParticles = kMaxParticles;

    const auto created = renderer.create(desc);
    if (!created.success) {
        std::fprintf(stderr,
            "FAIL: ParticleRenderer create for mesh diagnostic test failed: %s\n",
            created.diagnostics.c_str());
        return 1;
    }

    arp::core::pmr::Vector<arp::renderer::ParticleInstance> particles(
        &arp::core::defaultCpuMemoryResource());
    arp::renderer::ParticleInstance p;
    p.position = {0.0f, 0.0f, 0.0f};
    p.color = {1.0f, 1.0f, 1.0f, 1.0f};
    p.size = 1.0f;
    p.ageSeconds = 0.1f;
    p.lifetimeSeconds = 1.0f;
    p.visualFamily = arp::renderer::ParticleVisualFamily::MeshParticle;
    p.meshAssetId = "unknown_mesh_asset";
    particles.push_back(p);

    auto scene = makeScene();

    arp::renderer::RenderFrameContext frame;
    frame.frame.success = true;
    frame.frame.frameSerial = 0;
    frame.colorTarget.extent = {kWidth, kHeight};
    frame.colorTarget.format = arp::rhi::TextureFormat::Bgra8Unorm;
    frame.colorTexture = colorTexture.get();

    const auto result = renderer.drawFrame(frame, particles, scene, true);
    if (result.success) {
        std::fprintf(
            stderr, "FAIL: mesh particle draw unexpectedly succeeded for unknown mesh asset id\n");
        return 1;
    }
    if (result.diagnostics.find("prototype only supports meshAssetId 'debris_rock'")
        == std::string::npos) {
        std::fprintf(stderr, "FAIL: unexpected mesh diagnostic: %s\n", result.diagnostics.c_str());
        return 1;
    }

    std::printf("PASS: unknown mesh particle asset returned deterministic diagnostic: %s\n",
        result.diagnostics.c_str());
    return 0;
}

// ---------------------------------------------------------------------------
// Focused test: depthFadeEnabled == true WITH depth texture → succeeds (happy
// path for the Null backend depth descriptor set + shader depth fade path).
// ---------------------------------------------------------------------------
int testDepthFadeWithDepthTexture()
{
    auto device = arp::rhi::createNullDevice();
    if (!device) {
        std::fprintf(stderr, "FAIL: createNullDevice failed\n");
        return 1;
    }

    // Create color texture
    auto colorTexture = device->createTexture({
        "DepthFadeWithDepthColor",
        {kWidth, kHeight, 1},
        arp::rhi::TextureFormat::Bgra8Unorm,
        arp::rhi::TextureUsage::RenderAttachment,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
    });
    if (!colorTexture) {
        std::fprintf(stderr, "FAIL: createTexture (color) for depth fade happy path failed\n");
        return 1;
    }

    // Create depth texture (readable in fragment shader for soft fade)
    auto depthTexture = device->createTexture({
        "DepthFadeWithDepthDepth",
        {kWidth, kHeight, 1},
        arp::rhi::TextureFormat::D32Float,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::DepthAttachment,
    });
    if (!depthTexture) {
        std::fprintf(stderr, "FAIL: createTexture (depth) for depth fade happy path failed\n");
        return 1;
    }

    arp::renderer::ParticleRenderer renderer(*device);
    const auto rendererCreated = renderer.create({
        "particles.slang",
        arp::rhi::TextureFormat::Bgra8Unorm,
        {kWidth, kHeight},
        kMaxParticles,
    });
    if (!rendererCreated.success) {
        std::fprintf(stderr,
            "FAIL: ParticleRenderer create for depth fade happy path failed: %s\n",
            rendererCreated.diagnostics.c_str());
        return 1;
    }

    // Build particles with depthFadeEnabled set
    arp::core::pmr::Vector<arp::renderer::ParticleInstance> particles(
        &arp::core::defaultCpuMemoryResource());
    for (uint32_t i = 0; i < 2; ++i) {
        arp::renderer::ParticleInstance p;
        p.position = {static_cast<float>(i) * 2.0f - 1.0f, 0.0f, 0.0f};
        p.velocity = {0.0f, 0.0f, 0.0f};
        p.color = {1.0f, 1.0f, 1.0f, 1.0f};
        p.size = 1.0f;
        p.ageSeconds = 0.3f;
        p.lifetimeSeconds = 2.0f;
        p.emitterIndex = 0;
        p.seed = i;
        p.visualFamily = arp::renderer::ParticleVisualFamily::SpriteAlpha;
        p.depthFadeEnabled = 1u;
        p.depthFadeDistance = 0.5f;
        particles.push_back(p);
    }

    auto scene = makeScene();

    // Frame context WITH depth texture → must succeed
    arp::renderer::RenderFrameContext frame;
    frame.frame.success = true;
    frame.frame.frameSerial = 0;
    frame.colorTarget.extent = {kWidth, kHeight};
    frame.colorTarget.format = arp::rhi::TextureFormat::Bgra8Unorm;
    frame.colorTexture = colorTexture.get();
    frame.depthTexture = depthTexture.get();

    const auto result = renderer.drawFrame(frame, particles, scene, true);
    if (!result.success) {
        std::fprintf(stderr,
            "FAIL: depth-fade draw with depth texture unexpectedly failed: %s\n",
            result.diagnostics.c_str());
        return 1;
    }

    std::printf("PASS: depthFadeEnabled + depthTexture correctly produced a valid draw frame.\n");
    return 0;
}

// ---------------------------------------------------------------------------
// Combined verify entry
// ---------------------------------------------------------------------------
int verifyFocusedTests()
{
    int failures = 0;

    std::printf("\n--- Focused test: trailBeamShaderPath ---\n");
    failures += testTrailBeamShaderPath();

    std::printf("\n--- Focused test: depthFade + no depthTexture diagnostic ---\n");
    failures += testDepthFadeNoDepthTexture();

    std::printf("\n--- Focused test: depthFade + WITH depthTexture (happy path) ---\n");
    failures += testDepthFadeWithDepthTexture();

    std::printf("\n--- Focused test: Metal depth-fade unsupported backend diagnostic ---\n");
    failures += testDepthFadeUnsupportedBackendDiagnostic(
        "Metal",
        []() {
            return arp::rhi_metal::createMetalRhiDevice(makeMetalDeviceDesc());
        },
        "Metal backend is not backend-validated");

    std::printf("\n--- Focused test: WebGPU depth-fade unsupported backend diagnostic ---\n");
    failures += testDepthFadeUnsupportedBackendDiagnostic(
        "WebGPU",
        []() {
            return arp::rhi_webgpu::createWebGpuRhiDevice(makeWebGpuDeviceDesc());
        },
        "WebGPU backend is not backend-validated");

    std::printf("\n--- Focused test: mesh particle prototype path ---\n");
    failures += testMeshParticlePrototypePath();

    std::printf("\n--- Focused test: mesh particle unknown mesh diagnostic ---\n");
    failures += testMeshParticleUnknownMeshDiagnostic();

    if (failures > 0) {
        std::fprintf(stderr, "FAIL: %d focused test(s) failed\n", failures);
        return 1;
    }
    std::printf("PASS: All focused tests passed.\n");
    return 0;
}

#include "tools/render_feature_lab/forward_plus_fixture.h"

#include "renderer/lighting/forward_plus_feature.h"
#include "renderer/render_graph/compiled_frame_graph.h"
#include "renderer/shader_package_reflection.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/gpu_extensions.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <memory>
#include <stdexcept>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

namespace arp::tools::render_feature_lab {

namespace {

struct ForwardPlusDebugPushConstants {
    std::uint32_t tileCount[2]{};
    std::uint32_t expectedCountMode = 0u;
    std::uint32_t padding = 0u;
};

static_assert(sizeof(ForwardPlusDebugPushConstants) == 16u);

enum class DebugReadbackStatus : std::uint8_t {
    Unavailable,
    Available,
    Failed,
};

DebugReadbackStatus readDebugTexture(rhi::GpuDevice& device,
    rhi::GpuTexture& texture,
    rhi::TextureReadbackSample& sample,
    core::pmr::String& diagnostic)
{
    auto* extension = static_cast<rhi::TextureReadbackExtension*>(
        device.queryInterface(rhi::kExtensionTextureReadback));
    if (!extension)
        return DebugReadbackStatus::Unavailable;
    auto encoder = device.createCommandEncoder({"FeatureLab.ForwardPlusReadback"});
    if (!encoder) {
        diagnostic = "FEATURE-LAB-FORWARD-PLUS-READBACK-ENCODER";
        return DebugReadbackStatus::Failed;
    }
    const auto previousState = texture.state();
    const auto toCopy = encoder->transition(texture, previousState, rhi::ResourceState::CopySrc);
    if (!toCopy.success) {
        diagnostic = toCopy.diagnostics;
        return DebugReadbackStatus::Failed;
    }
    std::string backendDiagnostic;
    if (!extension->schedule(*encoder, texture, "FeatureLab.ForwardPlusDebug", backendDiagnostic)) {
        diagnostic = core::toPmrString(backendDiagnostic);
        return DebugReadbackStatus::Failed;
    }
    const auto restored = encoder->transition(texture, rhi::ResourceState::CopySrc, previousState);
    if (!restored.success) {
        diagnostic = restored.diagnostics;
        return DebugReadbackStatus::Failed;
    }
    auto commands = encoder->finish();
    if (!commands) {
        diagnostic = encoder->lastValidation().diagnostics;
        return DebugReadbackStatus::Failed;
    }
    const auto submitted = device.queue().submit(*commands);
    if (!submitted.success) {
        diagnostic = submitted.diagnostics;
        return DebugReadbackStatus::Failed;
    }
    device.waitIdle();
#if defined(__EMSCRIPTEN__)
    for (std::uint32_t attempt = 0u; attempt < 200u; ++attempt) {
        if (extension->poll(sample))
            return DebugReadbackStatus::Available;
        emscripten_sleep(1);
    }
#endif
    if (!extension->poll(sample)) {
        diagnostic = "FEATURE-LAB-FORWARD-PLUS-READBACK-NOT-READY";
        return DebugReadbackStatus::Failed;
    }
    if (!sample.valid) {
        diagnostic = sample.diagnostic.empty() ? "FEATURE-LAB-FORWARD-PLUS-READBACK-INVALID"
                                               : core::toPmrString(sample.diagnostic);
        return DebugReadbackStatus::Failed;
    }
    return DebugReadbackStatus::Available;
}

struct ForwardPlusDebugPipeline {
    rhi::ShaderPackageDesc package;
    core::UniquePtr<rhi::PipelineLayout> layout;
    core::UniquePtr<rhi::ComputePipeline> pipeline;
    core::UniquePtr<rhi::DescriptorArena> arena;
    core::UniquePtr<rhi::GpuBuffer> constants;
    std::array<core::UniquePtr<rhi::DescriptorSet>, 2> sets;
};

ForwardPlusDebugPipeline createDebugPipeline(
    rhi::GpuDevice& device, const std::filesystem::path& shaderPath, core::pmr::String& diagnostic)
{
    ForwardPlusDebugPipeline result;
    rhi::PipelineLayoutDesc layoutDesc;
    layoutDesc.descriptorLayouts.push_back({
        "FeatureLab.ForwardPlusDebug",
        {
            {0u, rhi::DescriptorType::ReadOnlyStorageBuffer, rhi::ShaderStage::Compute},
            {1u, rhi::DescriptorType::ReadOnlyStorageBuffer, rhi::ShaderStage::Compute},
            {2u,
                rhi::DescriptorType::StorageTexture,
                rhi::ShaderStage::Compute,
                1u,
                false,
                false,
                false,
                rhi::TextureViewDimension::Texture2D,
                false,
#if defined(__EMSCRIPTEN__)
                rhi::TextureFormat::Rgba8Unorm
#else
                rhi::TextureFormat::Rgba16Float
#endif
            },
            {3u, rhi::DescriptorType::UniformBuffer, rhi::ShaderStage::Compute},
        },
    });
    layoutDesc.descriptorSets.push_back({0u, "FeatureLab.ForwardPlusDebug"});
    result.layout = device.createPipelineLayout(layoutDesc);
    if (!result.layout) {
        diagnostic = "FEATURE-LAB-FORWARD-PLUS-DEBUG-LAYOUT";
        return result;
    }

    const auto shader = core::toPmrString(shaderPath.string());
    result.package.label = "FeatureLab.ForwardPlusDebugShader";
    auto& package = result.package;
    package.slangModulePath = shader;
    package.entrypoints.push_back({shader,
        "forwardPlusDebugMain",
        rhi::ShaderStage::Compute,
        rhi::ShaderSourceLanguage::Slang});
    const auto artifactPath = [&](const char* extension) {
        auto path = shaderPath;
        path.replace_extension(extension);
        return core::toPmrString(path.string());
    };
    package.artifacts.spirvPath = artifactPath(".spv");
    package.artifacts.mslPath = artifactPath(".metal");
    package.artifacts.wgslPath = artifactPath(".wgsl");
    package.artifacts.reflectionPath = artifactPath(".reflection.yaml");
    if (!renderer::hydrateShaderPackageReflectionFromArtifact(package)) {
        diagnostic = "FEATURE-LAB-FORWARD-PLUS-DEBUG-REFLECTION";
        return result;
    }
    for (const auto& binding : layoutDesc.descriptorLayouts.front().bindings) {
        package.reflection.descriptorBindings.push_back(
            rhi::makeShaderReflectionBinding(0u, binding));
    }
    const auto shaderValidation = rhi::validateShaderPackage(package);
    if (!shaderValidation.success) {
        diagnostic = shaderValidation.diagnostics;
        return result;
    }
    rhi::ComputePipelineDesc pipelineDesc;
    pipelineDesc.label = "FeatureLab.ForwardPlusDebug";
    pipelineDesc.layout = result.layout.get();
    pipelineDesc.shaderPackage = &package;
    pipelineDesc.computeShader = package.entrypoints.front();
    result.pipeline = device.createComputePipeline(pipelineDesc);
    if (!result.pipeline) {
        const auto validation = device.validateComputePipeline(pipelineDesc);
        diagnostic = validation.diagnostics.empty() ? "FEATURE-LAB-FORWARD-PLUS-DEBUG-PIPELINE"
                                                    : validation.diagnostics;
        return result;
    }
    result.arena = device.createDescriptorArena({"FeatureLab.ForwardPlusDebugArena", 2u, 8u});
    if (!result.arena) {
        diagnostic = "FEATURE-LAB-FORWARD-PLUS-DEBUG-ARENA";
        return result;
    }
    for (auto& set : result.sets) {
        set = result.arena->allocateDescriptorSet(
            {"FeatureLab.ForwardPlusDebugSet", layoutDesc.descriptorLayouts.front()});
        if (!set) {
            diagnostic = "FEATURE-LAB-FORWARD-PLUS-DEBUG-SET";
            return result;
        }
    }
    result.constants = device.createBuffer({
        "FeatureLab.ForwardPlusDebugConstants",
        sizeof(ForwardPlusDebugPushConstants),
        rhi::ResourceUsage::UniformBuffer | rhi::ResourceUsage::CopyDst,
        rhi::MemoryUsage::CpuToGpu,
        rhi::ResourceState::Undefined,
    });
    if (!result.constants) {
        diagnostic = "FEATURE-LAB-FORWARD-PLUS-DEBUG-CONSTANTS";
        return result;
    }
    return result;
}

rhi::BufferDesc debugExpectedBufferDesc(std::uint64_t sizeBytes)
{
    return {
        "FeatureLab.ForwardPlusExpectedCounts",
        sizeBytes,
        rhi::ResourceUsage::StorageBuffer | rhi::ResourceUsage::CopyDst,
        rhi::MemoryUsage::CpuToGpu,
        rhi::ResourceState::Undefined,
    };
}

rhi::TextureDesc debugOutputDesc(const char* label, rhi::Extent2D extent)
{
    rhi::TextureDesc desc;
    desc.label = label;
    desc.extent = {extent.width, extent.height, 1u};
    desc.format =
#if defined(__EMSCRIPTEN__)
        rhi::TextureFormat::Rgba8Unorm;
#else
        rhi::TextureFormat::Rgba16Float;
#endif
    desc.usage = rhi::TextureUsage::Storage | rhi::TextureUsage::CopySrc;
    desc.memory = rhi::MemoryUsage::GpuOnly;
    return desc;
}

core::pmr::Vector<std::uint32_t> expectedTileCounts(
    const core::pmr::Vector<renderer::lighting::GpuLocalLight>& lights,
    rhi::Extent2D extent,
    const renderer::lighting::ForwardPlusConfig& config,
    const renderer::lighting::ForwardPlusCullView& view,
    bool opaque)
{
    const auto tileCountX = renderer::lighting::forwardPlusTileCount(extent.width, config.tileSize);
    const auto tileCountY =
        renderer::lighting::forwardPlusTileCount(extent.height, config.tileSize);
    core::pmr::Vector<std::uint32_t> counts(std::size_t(tileCountX) * tileCountY, 0u);
    const auto rowDot = [&view](std::uint32_t row, const core::Vec4& p) {
        const auto* values = view.viewRows + row * 4u;
        return values[0] * p.x + values[1] * p.y + values[2] * p.z + values[3] * p.w;
    };
    for (const auto& light : lights) {
        const core::Vec4 centerWorld{
            light.positionAndRange.x, light.positionAndRange.y, light.positionAndRange.z, 1.0f};
        const float centerX = rowDot(0u, centerWorld);
        const float centerY = rowDot(1u, centerWorld);
        const float centerZ = rowDot(2u, centerWorld);
        const float radius = light.positionAndRange.w;
        const float nearestDepth = centerZ - radius;
        const float farthestDepth = centerZ + radius;
        if (farthestDepth <= 0.0f)
            continue;
        const float safeDepth = std::max(nearestDepth, 0.00001f);
        const core::Vec2 centerNdc{centerX * view.projectionScale[0] / std::max(centerZ, 0.00001f),
            centerY * view.projectionScale[1] / std::max(centerZ, 0.00001f)};
        const core::Vec2 radiusNdc{std::abs(view.projectionScale[0]) * radius / safeDepth,
            std::abs(view.projectionScale[1]) * radius / safeDepth};
        const core::Vec2 centerPixel{(centerNdc.x * 0.5f + 0.5f) * extent.width,
            (centerNdc.y * 0.5f + 0.5f) * extent.height};
        const core::Vec2 radiusPixel{
            radiusNdc.x * 0.5f * extent.width, radiusNdc.y * 0.5f * extent.height};
        const float minX = centerPixel.x - radiusPixel.x;
        const float maxX = centerPixel.x + radiusPixel.x;
        const float minY = centerPixel.y - radiusPixel.y;
        const float maxY = centerPixel.y + radiusPixel.y;
        const float sphereMinDepth = nearestDepth <= 0.0f
            ? 0.0f
            : view.depthScale + view.depthBias / std::max(nearestDepth, 0.00001f);
        const float sphereMaxDepth =
            view.depthScale + view.depthBias / std::max(farthestDepth, 0.00001f);
        const float orderedMin = std::min(sphereMinDepth, sphereMaxDepth);
        const float orderedMax = std::max(sphereMinDepth, sphereMaxDepth);
        for (std::uint32_t tileY = 0u; tileY < tileCountY; ++tileY) {
            const float tileMinY = static_cast<float>(tileY * config.tileSize);
            const float tileMaxY = std::min(
                tileMinY + static_cast<float>(config.tileSize), static_cast<float>(extent.height));
            if (maxY < tileMinY || minY > tileMaxY)
                continue;
            for (std::uint32_t tileX = 0u; tileX < tileCountX; ++tileX) {
                const float tileMinX = static_cast<float>(tileX * config.tileSize);
                const float tileMaxX = std::min(tileMinX + static_cast<float>(config.tileSize),
                    static_cast<float>(extent.width));
                if (maxX < tileMinX || minX > tileMaxX)
                    continue;
                if (opaque && !(orderedMax >= 1.0f && orderedMin <= 1.0f))
                    continue;
                ++counts[tileY * tileCountX + tileX];
            }
        }
    }
    return counts;
}

bool recordDebugPass(render_graph::RenderContext& context,
    render_graph::FrameGraph& graph,
    render_graph::BufferHandle headers,
    rhi::GpuBuffer& expectedBuffer,
    render_graph::TextureHandle output,
    ForwardPlusDebugPipeline& debugPipeline,
    std::uint32_t descriptorSetIndex,
    rhi::Extent2D extent,
    core::pmr::String& diagnostic)
{
    auto& outputTexture = graph.texture(output);
    auto outputView = context.device().createTextureView(outputTexture,
        {
            "FeatureLab.ForwardPlusDebugOutputView",

#if defined(__EMSCRIPTEN__)
            rhi::TextureFormat::Rgba8Unorm,
#else
            rhi::TextureFormat::Rgba16Float,
#endif
            rhi::TextureUsage::Storage,
        });
    if (!outputView) {
        diagnostic = "FEATURE-LAB-FORWARD-PLUS-DEBUG-OUTPUT-VIEW";
        return false;
    }
    auto& set = *debugPipeline.sets[descriptorSetIndex];
    if (headers.id == 0u) {
        diagnostic = "FEATURE-LAB-FORWARD-PLUS-DEBUG-HEADER";
        return false;
    }
    const std::array<rhi::DescriptorBindingWrite, 4> writes{
        rhi::DescriptorBindingWrite{0u,
            rhi::DescriptorType::ReadOnlyStorageBuffer,
            graph.buffer(headers).id(),
            rhi::DescriptorResourceKind::Buffer},
        rhi::DescriptorBindingWrite{1u,
            rhi::DescriptorType::ReadOnlyStorageBuffer,
            expectedBuffer.id(),
            rhi::DescriptorResourceKind::Buffer},
        rhi::DescriptorBindingWrite{2u,
            rhi::DescriptorType::StorageTexture,
            outputView->id(),
            rhi::DescriptorResourceKind::TextureView},
        rhi::DescriptorBindingWrite{3u,
            rhi::DescriptorType::UniformBuffer,
            debugPipeline.constants->id(),
            rhi::DescriptorResourceKind::Buffer},
    };
    for (const auto& write : writes) {
        const auto written = set.write(write);
        if (!written.success) {
            diagnostic = written.diagnostics;
            return false;
        }
    }
    const auto expectedTransition = context.commands().transition(
        expectedBuffer, expectedBuffer.state(), rhi::ResourceState::ShaderRead);
    if (!expectedTransition.success) {
        diagnostic = expectedTransition.diagnostics;
        return false;
    }
    auto pass = context.commands().beginComputePass({"FeatureLab.ForwardPlusDebug"});
    if (!pass) {
        diagnostic = context.commands().lastValidation().diagnostics;
        return false;
    }
    ForwardPlusDebugPushConstants push{};
    push.tileCount[0] = renderer::lighting::forwardPlusTileCount(
        extent.width, renderer::lighting::ForwardPlusConfig::kDefaultTileSize);
    push.tileCount[1] = renderer::lighting::forwardPlusTileCount(
        extent.height, renderer::lighting::ForwardPlusConfig::kDefaultTileSize);
    const auto constantsWritten =
        context.device().writeBuffer(*debugPipeline.constants, &push, sizeof(push));
    if (!constantsWritten.success) {
        diagnostic = constantsWritten.diagnostics;
        return false;
    }
    const auto pipelineBound = pass->setPipeline(*debugPipeline.pipeline);
    const auto descriptorsBound = pipelineBound.success ? pass->bindDescriptorSet(0u, set)
                                                        : rhi::CommandValidationResult{false, {}};
    const auto dispatched = descriptorsBound.success
        ? pass->dispatch((push.tileCount[0] + 7u) / 8u, (push.tileCount[1] + 7u) / 8u, 1u)
        : rhi::CommandValidationResult{false, ""};
    const auto ended = pass->end();
    if (!pipelineBound.success || !descriptorsBound.success || !dispatched.success
        || !ended.success) {
        diagnostic = !pipelineBound.success ? pipelineBound.diagnostics
            : !descriptorsBound.success     ? descriptorsBound.diagnostics
            : !dispatched.success           ? dispatched.diagnostics
                                            : ended.diagnostics;
        return false;
    }
    return true;
}

} // namespace

ForwardPlusFixtureResult runForwardPlusFeatureFixture(const std::uint32_t localLightCount)
{
    auto device = rhi::createNullDevice();
    if (!device) {
        ForwardPlusFixtureResult result;
        result.diagnostic = "FEATURE-LAB-NULL-DEVICE-CREATION-FAILED";
        return result;
    }
    return runForwardPlusFeatureFixture(*device, {16u, 16u}, localLightCount);
}

ForwardPlusFixtureResult runForwardPlusFeatureFixture(
    rhi::GpuDevice& device, rhi::Extent2D extent, const std::uint32_t localLightCount)
{
    ForwardPlusFixtureResult result;

    renderer::lighting::ForwardPlusConfig config;
    render_graph::FrameGraph graph(device);
    rhi::TextureDesc depthDesc;
    depthDesc.label = "FeatureLab.ExactDepth";
    depthDesc.extent = {extent.width, extent.height, 1u};
    depthDesc.format = rhi::TextureFormat::D32Float;
    depthDesc.usage = rhi::TextureUsage::RenderAttachment | rhi::TextureUsage::Sampled;
    depthDesc.memory = rhi::MemoryUsage::Transient;
    const auto depth = graph.createTransientTexture(depthDesc);
    graph.addPass("FeatureLab.DepthPrepass")
        .writeAttachment(
            depth, rhi::ResourceState::DepthAttachment, rhi::LoadOp::Clear, rhi::StoreOp::Store)
        .executeRenderPass([](render_graph::RenderPassContext&) {});

    renderer::lighting::ForwardPlusFeature feature;
#ifdef ARP_SHADER_DIR
    const auto shaderPath = std::filesystem::path(ARP_SHADER_DIR) / "forward_plus_cull.slang";
#else
    const auto shaderPath = std::filesystem::path("shaders") / "forward_plus_cull.slang";
#endif
    const auto created = feature.create({
        device,
        extent,
        config,
        shaderPath.string().c_str(),
    });
    if (!created.success) {
        result.diagnostic = created.diagnostic.id;
        result.diagnostic += ": ";
        result.diagnostic += created.diagnostic.message;
        return result;
    }

    core::pmr::Vector<renderer::lighting::RenderLight> lights;
    renderer::lighting::RenderLight sun;
    sun.type = renderer::lighting::RenderLightType::Directional;
    sun.direction = {0.0f, -1.0f, 0.0f};
    lights.push_back(sun);
    for (std::uint32_t index = 0u; index < localLightCount; ++index) {
        renderer::lighting::RenderLight light;
        light.type = index % 2u == 0u ? renderer::lighting::RenderLightType::Point
                                      : renderer::lighting::RenderLightType::Spot;
        light.position = {float(index % 4u) - 1.5f, 0.0f, 4.0f};
        light.direction = {0.0f, 0.0f, -1.0f};
        light.range = 8.0f;
        light.outerConeCos = 0.7f;
        light.innerConeCos = 0.9f;
        lights.push_back(light);
    }
    renderer::lighting::RenderLightPlan plan;
    const auto planned = renderer::lighting::buildRenderLightPlan(
        renderer::lighting::LightingPath::ForwardPlus, config, lights, plan);
    if (!planned.success) {
        result.diagnostic = planned.diagnostic.id;
        feature.release();
        return result;
    }

    renderer::lighting::ForwardPlusCullView view;
    view.viewRows[0] = 1.0f;
    view.viewRows[5] = 1.0f;
    view.viewRows[10] = 1.0f;
    view.viewRows[15] = 1.0f;
    view.projectionScale[0] = 1.0f;
    view.projectionScale[1] = -1.0f;
    view.depthScale = 1.01f;
    view.depthBias = -0.1f;

#ifdef ARP_SHADER_DIR
    const auto debugShaderPath = std::filesystem::path(ARP_SHADER_DIR) / "forward_plus_debug.slang";
#else
    const auto debugShaderPath = std::filesystem::path("shaders") / "forward_plus_debug.slang";
#endif
    core::pmr::String debugDiagnostic;
    auto debugPipeline = createDebugPipeline(device, debugShaderPath, debugDiagnostic);
    if (!debugPipeline.pipeline) {
        result.diagnostic =
            debugDiagnostic.empty() ? "FEATURE-LAB-FORWARD-PLUS-DEBUG-PIPELINE" : debugDiagnostic;
        feature.release();
        return result;
    }
    const auto tileCountX = renderer::lighting::forwardPlusTileCount(extent.width, config.tileSize);
    const auto tileCountY =
        renderer::lighting::forwardPlusTileCount(extent.height, config.tileSize);
    const auto opaqueExpected = expectedTileCounts(plan.localLights, extent, config, view, true);
    const auto conservativeExpected =
        expectedTileCounts(plan.localLights, extent, config, view, false);
    const rhi::Extent2D tileExtent{tileCountX, tileCountY};
    auto opaqueExpectedBuffer = device.createBuffer(
        debugExpectedBufferDesc(std::uint64_t(opaqueExpected.size()) * sizeof(std::uint32_t)));
    auto conservativeExpectedBuffer = device.createBuffer(debugExpectedBufferDesc(
        std::uint64_t(conservativeExpected.size()) * sizeof(std::uint32_t)));
    if (!opaqueExpectedBuffer || !conservativeExpectedBuffer) {
        result.diagnostic = "FEATURE-LAB-FORWARD-PLUS-DEBUG-EXPECTED-BUFFERS";
        feature.release();
        return result;
    }
    const auto opaqueExpectedWritten = device.writeBuffer(*opaqueExpectedBuffer,
        opaqueExpected.data(),
        opaqueExpected.size() * sizeof(std::uint32_t));
    const auto conservativeExpectedWritten = device.writeBuffer(*conservativeExpectedBuffer,
        conservativeExpected.data(),
        conservativeExpected.size() * sizeof(std::uint32_t));
    if (!opaqueExpectedWritten.success || !conservativeExpectedWritten.success) {
        result.diagnostic = !opaqueExpectedWritten.success
            ? opaqueExpectedWritten.diagnostics
            : conservativeExpectedWritten.diagnostics;
        feature.release();
        return result;
    }
    renderer::lighting::ForwardPlusFeatureOutputs outputs;
    const auto contributed = feature.contributeGraph(
        {
            graph,
            depth,
            plan,
            view,
            0u,
        },
        outputs);
    if (!contributed.success) {
        result.diagnostic = contributed.diagnostic.id;
        feature.release();
        return result;
    }
    auto opaqueDebugTexture =
        device.createTexture(debugOutputDesc("FeatureLab.ForwardPlusOpaqueDebug", tileExtent));
    auto conservativeDebugTexture = device.createTexture(
        debugOutputDesc("FeatureLab.ForwardPlusConservativeDebug", tileExtent));
    if (!opaqueDebugTexture || !conservativeDebugTexture) {
        result.diagnostic = "FEATURE-LAB-FORWARD-PLUS-DEBUG-TEXTURES";
        feature.release();
        return result;
    }
    const auto opaqueDebug = graph.importTexture(std::move(opaqueDebugTexture));
    const auto conservativeDebug = graph.importTexture(std::move(conservativeDebugTexture));
    auto opaqueDebugPass = graph.addPass("FeatureLab.ForwardPlusOpaqueDebug")
                               .kind(render_graph::PassKind::Compute)
                               .read(outputs.opaqueHeaders, rhi::ResourceState::ShaderRead)
                               .write(opaqueDebug, rhi::ResourceState::ShaderWrite);
    opaqueDebugPass.execute(
        [&graph, outputs, opaqueDebug, &debugPipeline, &opaqueExpectedBuffer, extent](
            render_graph::RenderContext& context) {
            core::pmr::String diagnostic;
            if (!recordDebugPass(context,
                    graph,
                    outputs.opaqueHeaders,
                    *opaqueExpectedBuffer,
                    opaqueDebug,
                    debugPipeline,
                    0u,
                    extent,
                    diagnostic)) {
                throw std::runtime_error(diagnostic.c_str());
            }
        });
    auto conservativeDebugPass =
        graph.addPass("FeatureLab.ForwardPlusConservativeDebug")
            .kind(render_graph::PassKind::Compute)
            .read(outputs.conservativeHeaders, rhi::ResourceState::ShaderRead)
            .write(conservativeDebug, rhi::ResourceState::ShaderWrite);
    conservativeDebugPass.execute(
        [&graph, outputs, conservativeDebug, &debugPipeline, &conservativeExpectedBuffer, extent](
            render_graph::RenderContext& context) {
            core::pmr::String diagnostic;
            if (!recordDebugPass(context,
                    graph,
                    outputs.conservativeHeaders,
                    *conservativeExpectedBuffer,
                    conservativeDebug,
                    debugPipeline,
                    1u,
                    extent,
                    diagnostic)) {
                throw std::runtime_error(diagnostic.c_str());
            }
        });
    graph.addPass("FeatureLab.OpaqueConsumer")
        .read(outputs.directionalLights, rhi::ResourceState::ShaderRead)
        .read(outputs.localLights, rhi::ResourceState::ShaderRead)
        .read(outputs.opaqueHeaders, rhi::ResourceState::ShaderRead)
        .read(outputs.opaqueIndices, rhi::ResourceState::ShaderRead)
        .execute([](render_graph::RenderContext&) {});
    graph.addPass("FeatureLab.TransparentConsumer")
        .read(outputs.conservativeHeaders, rhi::ResourceState::ShaderRead)
        .read(outputs.conservativeIndices, rhi::ResourceState::ShaderRead)
        .execute([](render_graph::RenderContext&) {});

    const auto compiled = graph.compile();
    result.graphPassCount = static_cast<std::uint32_t>(compiled.passes().size());
    for (const auto& dependency : compiled.dependencies()) {
        const auto& from = compiled.passes()[dependency.fromPass].name;
        const auto& to = compiled.passes()[dependency.toPass].name;
        result.depthToCullDependency = result.depthToCullDependency
            || (from == "FeatureLab.DepthPrepass" && to == "ForwardPlus.Cull");
        result.uploadToCullDependency = result.uploadToCullDependency
            || (from == "ForwardPlus.LightUpload" && to == "ForwardPlus.Cull");
    }
    graph.execute();
    device.waitIdle();
    rhi::TextureReadbackSample opaqueSample;
    rhi::TextureReadbackSample conservativeSample;
    core::pmr::String readbackDiagnostic;
    const auto opaqueStatus =
        readDebugTexture(device, graph.texture(opaqueDebug), opaqueSample, readbackDiagnostic);
    const auto conservativeStatus = readDebugTexture(
        device, graph.texture(conservativeDebug), conservativeSample, readbackDiagnostic);
    result.tileReadbackAvailable = opaqueStatus == DebugReadbackStatus::Available
        && conservativeStatus == DebugReadbackStatus::Available;
    if (result.tileReadbackAvailable) {
        result.opaqueCpuGpuMatch =
            opaqueSample.minValue == 1.0 && opaqueSample.maxValue == 1.0 ? 1.0f : 0.0f;
        result.conservativeCpuGpuMatch =
            conservativeSample.minValue == 1.0 && conservativeSample.maxValue == 1.0 ? 1.0f : 0.0f;
    }
    result.gpuMemoryBytes = device.memoryStats().allocatedBytes;
    feature.release();
    result.success = result.depthToCullDependency && result.uploadToCullDependency
        && (device.backend() == rhi::Backend::Null
            || (result.tileReadbackAvailable && result.opaqueCpuGpuMatch == 1.0f
                && result.conservativeCpuGpuMatch == 1.0f));
    if (!result.success) {
        if (!readbackDiagnostic.empty()) {
            result.diagnostic = readbackDiagnostic;
        } else {
            result.diagnostic =
                core::toPmrString("FEATURE-LAB-FORWARD-PLUS-TOPOLOGY-OR-GPU-MATCH-FAILED"
                                  " opaque[min="
                    + std::to_string(opaqueSample.minValue)
                    + ",max=" + std::to_string(opaqueSample.maxValue)
                    + ",mean=" + std::to_string(opaqueSample.meanValue)
                    + ",center=" + std::to_string(opaqueSample.centerValue)
                    + "] conservative[min=" + std::to_string(conservativeSample.minValue)
                    + ",max=" + std::to_string(conservativeSample.maxValue)
                    + ",mean=" + std::to_string(conservativeSample.meanValue)
                    + ",center=" + std::to_string(conservativeSample.centerValue) + "]");
        }
    }
    return result;
}

} // namespace arp::tools::render_feature_lab

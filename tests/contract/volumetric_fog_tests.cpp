#include "3d/scene_3d.h"
#include "reflection/reflection.h"
#include "reflection/serialization.h"
#include "renderer/render_graph/compiled_frame_graph.h"
#include "renderer/volumetric_fog/volumetric_fog_feature.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/semantics/render_target_coordinates.h"
#include "scriptvm/runtime/module_registration.h"

#include <cassert>
#include <iostream>
#include <limits>

namespace arp::reflection::generated {
bool register_fog_contract_reflection(arp::scriptvm::ModuleRegistration*);
}

using namespace arp;
using namespace renderer::fog;

void components()
{
    scene::Scene scene;
    auto& root = scene.createObject("environment");
    auto& global = root.addComponent<scene3d::VolumetricFogComponent>();
    auto& region = scene.createObject("region");
    auto& local = region.addComponent<scene3d::LocalFogVolumeComponent>();
    scene::Transform transform;
    transform.translation = {4, 2, 10};
    transform.scale = {-2, 3, 4};
    transform.rotation = core::Quat::fromAxisAngle({0, 1, 0}, 0.4f);
    region.set_local_transform(transform);
    const auto extract = [&]() {
        return scene3d::Scene3DRenderExtractor{}.extract(scene, scene.extractTraversalSnapshot({}));
    };
    auto result = extract();
    assert(result.ok() && result.queue().hasFog() && result.queue().fogVolumes().size() == 1);
    core::pmr::Vector<FogGpuVolume> upload;
    assert(buildFogVolumes(result.queue(), upload).success && upload.size() == 1);
    assert(upload[0].velocity.w == 1);
    auto center = core::transformPoint(upload[0].worldToLocal, transform.translation);
    assert(core::length(center) < 0.0001f);
    auto contrasted = local.fog();
    contrasted.noiseContrast = 3.0f;
    assert(local.set_fog(contrasted));
    assert(buildFogVolumes(extract().queue(), upload).success && upload[0].velocity.w == 3.0f);
    auto& duplicate =
        scene.createObject("duplicate").addComponent<scene3d::VolumetricFogComponent>();
    assert(extract().diagnostic().code == scene3d::Scene3DDiagnosticCode::DuplicateGlobalFog);
    duplicate.setEnabled(false);
    assert(extract().ok());
    global.setEnabled(false);
    assert(!extract().queue().hasFog() && extract().queue().fogVolumes().size() == 1);
    transform.scale.y = 0;
    region.set_local_transform(transform);
    assert(extract().diagnostic().code == scene3d::Scene3DDiagnosticCode::InvalidFog);
    local.setEnabled(false);
    assert(extract().ok());
    auto invalid = global.fog();
    invalid.density = -1;
    assert(!global.set_fog(invalid));
    invalid = global.fog();
    invalid.anisotropy = 1;
    assert(!global.set_fog(invalid));
    auto d = local.fog();
    d.noiseScale = 0;
    assert(!local.set_fog(d));
    d = local.fog();
    d.noiseContrast = 0.5f;
    assert(!local.set_fog(d));
    d.noiseContrast = std::numeric_limits<float>::infinity();
    assert(!local.set_fog(d));
    d = local.fog();
    d.noiseStrength = std::numeric_limits<float>::quiet_NaN();
    assert(!local.set_fog(d));
    assert(reflection::generated::register_fog_contract_reflection(nullptr));
    scene3d::LocalFogVolumeDesc authored;
    authored.shape = scene3d::FogVolumeShape::Sphere;
    authored.noiseSeed = std::numeric_limits<std::uint32_t>::max();
    authored.noiseVelocity = {2, 3, 4};
    authored.noiseContrast = 3.0f;
    const auto yaml = reflection::serialize_to_yaml(reflection::object(authored));
    if (!yaml.success)
        std::cerr << yaml.diagnostics << std::endl;
    assert(yaml.success);
    const auto restored = reflection::deserialize_from_yaml(yaml.yaml);
    if (!restored.success)
        std::cerr << restored.diagnostics << '\n';
    assert(restored.success);
    const auto* roundtrip = restored.value.cast<scene3d::LocalFogVolumeDesc*>();
    assert(roundtrip && roundtrip->shape == authored.shape
        && roundtrip->noiseSeed == authored.noiseSeed && roundtrip->noiseVelocity.z == 4
        && roundtrip->noiseContrast == 3.0f);
    auto overflow = yaml.yaml;
    const auto at = overflow.find("4294967295");
    assert(at != overflow.npos);
    overflow.replace(at, 10, "4294967296");
    assert(!reflection::deserialize_from_yaml(overflow).success);
}

void contracts()
{
    auto device = rhi::createNullDevice();
    assert(device);
    FogConfig config;
    config.distance = 100;
    FogGrid grid;
    assert(planFog(config, {1920, 1080}, device->capabilities(), grid).success);
    assert(grid.width == 160 && grid.height == 90 && grid.depth == 64);
    config.gpuBudgetBytes = 1;
    assert(planFog(config, {1920, 1080}, device->capabilities(), grid).diagnostic
        == "FOG-MEMORY-BUDGET");
    config.gpuBudgetBytes = 512ull * 1024 * 1024;
    config.pixelsPerCell = std::numeric_limits<std::uint32_t>::max();
    assert(planFog(config, {UINT32_MAX, UINT32_MAX}, device->capabilities(), grid).diagnostic
        == "FOG-GRID-OVERFLOW");
    config.pixelsPerCell = 0;
    auto caps = device->capabilities();
    caps.features.storageBufferBinding = false;
    assert(!planFog(config, {640, 360}, caps, grid).success);
    caps = device->capabilities();
    caps.computeResources.maxStorageBufferBindingSizeBytes = 16;
    assert(planFog(config, {640, 360}, caps, grid).diagnostic == "FOG-STORAGE-LIMIT");
    for (auto backend :
        {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
        caps = device->capabilities();
        caps.backend = backend;
        caps.computeResources.maxStorageBufferBindingsPerStage = 4;
        assert(planFog(config, {640, 360}, caps, grid).success);
        caps.computeResources.maxStorageBufferBindingsPerStage = 3;
        assert(planFog(config, {640, 360}, caps, grid).diagnostic == "FOG-COMPUTE-LIMIT");
        caps.executionStatuses.clear();
        assert(!planFog(config, {640, 360}, caps, grid).success);
    }
    // A tilted camera and asymmetric world probe expose raster/sampled-Y errors.
    for (auto backend :
        {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
        auto projection = core::perspective(61, 1.7f, 0.2f, 100);
        const auto view = core::lookAt({1, 5, -8}, {3, 2, 10});
        rhi::semantics::applyProjectionYConvention(projection.m.data(), backend);
        auto rasterVP = core::multiply(projection, view);
        auto sampledVP = rasterVP;
        rhi::semantics::applyRenderTargetSamplingYConvention(sampledVP.m.data(), backend);
        const core::Vec3 point{3, 4, 13};
        const auto clip = core::transformPoint(rasterVP, point);
        const auto sample = core::transformPoint(sampledVP, point);
        const auto sign = rhi::semantics::projectionRequiresYFlip(backend) ? -1.0f : 1.0f;
        assert(std::fabs(sample.y - sign * clip.y) < 0.00001f);
        const auto restored = core::transformPoint(core::inverse(sampledVP), sample);
        assert(core::length(restored - point) < 0.002f);
    }
    assert(integratedLength(0, 4) == 4);
    assert(std::fabs(integratedLength(0.2f, 8) - (1 - std::exp(-1.6f)) / 0.2f) < 0.00001f);
    assert(std::fabs(henyeyGreenstein(0.5f, 0) - 1 / (4 * 3.141592653589793f)) < 0.00001f);
    // Homogeneous integration must be invariant to the number of slices.
    float sum = 0, transmittance = 1;
    for (int i = 0; i < 96; ++i) {
        sum += transmittance * 0.3f * integratedLength(0.2f, 8.0f / 96);
        transmittance *= std::exp(-0.2f * 8 / 96);
    }
    assert(std::fabs(sum - 0.3f * integratedLength(0.2f, 8)) < 0.00001f);
}

void smoke()
{
    auto device = rhi::createNullDevice();
    FogConfig config;
    config.distance = 50;
    config.depthSlices = 8;
    config.pixelsPerCell = 8;
    VolumetricFogFeature feature;
    assert(!feature.frameReady());
    auto created = feature.create({*device, {64, 48}, config, ARP_FOG_SHADER_DIR});
    if (!created.success)
        std::cerr << created.diagnostic << '\n';
    assert(created.success);
    assert(feature.frameReady());
    scene3d::Scene3DRenderQueue scene;
    scene.setFog({});
    for (int frame = 0; frame < 20; ++frame) {
        render_graph::FrameGraph graph(*device);
        auto color = graph.createTransientTexture({"SceneColor",
            {64, 48, 1},
            rhi::TextureFormat::Rgba16Float,
            rhi::TextureUsage::RenderAttachment | rhi::TextureUsage::Sampled,
            rhi::MemoryUsage::GpuOnly});
        auto depth = graph.createTransientTexture({"SceneDepth",
            {64, 48, 1},
            rhi::TextureFormat::D32Float,
            rhi::TextureUsage::RenderAttachment | rhi::TextureUsage::Sampled,
            rhi::MemoryUsage::GpuOnly});
        graph.addPass("Scene.Clear")
            .writeAttachment(
                color, rhi::ResourceState::ColorAttachment, rhi::LoadOp::Clear, rhi::StoreOp::Store)
            .writeAttachment(
                depth, rhi::ResourceState::DepthAttachment, rhi::LoadOp::Clear, rhi::StoreOp::Store)
            .executeRenderPass([](render_graph::RenderPassContext&) {});
        FogGraphContext context{graph, scene, color, depth};
        context.view = core::lookAt({0, 2, -8}, {0, 2, 0});
        context.projection = core::perspective(60, 64.0f / 48, 0.1f, 100);
        context.cameraPosition = {0, 2, -8};
        context.time = float(frame) / 60;
        if (frame >= 9)
            context.ambientRadiance = {0.15f, 0.2f, 0.3f};
        if (frame == 9) {
            context.projectionJitter = {0.001f, -0.002f};
            context.projection.m[2] += context.projectionJitter.x;
            context.projection.m[6] += context.projectionJitter.y;
        }
        context.primaryLight.objectId = scene::GameObjectId::fromValue(1);
        context.primaryLight.type = scene3d::LightType::Directional;
        if (frame == 2)
            context.cameraCut = true;
        if (frame == 3) {
            auto desc = scene.fog();
            desc.density *= 2;
            scene.setFog(desc);
        }
        if (frame >= 5)
            context.sceneRevision = 1;
        FogOutputs outputs;
        auto bad = context;
        bad.cameraId = 2;
        assert(feature.contributeGraph(bad, outputs).diagnostic == "FOG-CAMERA-IDENTITY");
        auto badAmbient = context;
        badAmbient.ambientRadiance.x = -0.01f;
        assert(feature.contributeGraph(badAmbient, outputs).diagnostic == "FOG-AMBIENT-RADIANCE");
        badAmbient.ambientRadiance.x = std::numeric_limits<float>::quiet_NaN();
        assert(feature.contributeGraph(badAmbient, outputs).diagnostic == "FOG-AMBIENT-RADIANCE");
        auto badShadow = context;
        badShadow.primaryLight.castShadow = true;
        assert(feature.contributeGraph(badShadow, outputs).diagnostic == "FOG-SHADOW-INPUT");
        if (frame >= 10) {
            context.primaryLight.castShadow = true;
            context.shadow.depth = depth;
            context.shadow.cascadeCount = 1;
            context.shadow.matrices[0] = core::Mat4::identity();
            context.shadow.uvRects[0] = {0, 0, 1, 1};
            context.shadow.cascadeEnds[0] = 50;
            if (frame >= 12)
                context.nearPlane = 0.2f;
            if (frame >= 14)
                context.shadow.matrices[0] = core::Mat4::translation({0.1f, 0, 0});
            if (frame >= 16)
                context.shadow.uvRects[0] = {0, 0, 0.5f, 1};
            if (frame >= 16) {
                context.shadow.selection = FogShadowCascadeSelection::DistanceFromOrigin;
                context.shadow.selectionOrigin = {3, 4, 5};
            }
            if (frame >= 18) {
                context.shadow.cascadeEnds[0] = 70;
                context.shadow.selectionOrigin.x = 4;
            }
            auto badSelection = context;
            badSelection.shadow.selection = static_cast<FogShadowCascadeSelection>(2);
            assert(feature.contributeGraph(badSelection, outputs).diagnostic
                == "FOG-SHADOW-SELECTION");
            badSelection.shadow = context.shadow;
            badSelection.shadow.selectionOrigin.x = std::numeric_limits<float>::quiet_NaN();
            assert(feature.contributeGraph(badSelection, outputs).diagnostic
                == "FOG-SHADOW-SELECTION");
            auto singular = context;
            singular.shadow.matrices[0] = core::Mat4::scale({0, 0, 0});
            assert(feature.contributeGraph(singular, outputs).diagnostic == "FOG-SHADOW-METADATA");
        }
        auto added = feature.contributeGraph(context, outputs);
        if (!added.success)
            std::cerr << added.diagnostic << '\n';
        assert(added.success
            && outputs.historyUsed
                == (frame == 1 || frame == 4 || frame == 6 || (frame >= 10 && frame % 2 == 1)));
        assert(!feature.frameReady());
        if (frame == 7) {
            feature.cancelPendingFrame();
            assert(feature.frameReady());
            continue;
        }
        FogOutputs rejected;
        assert(feature.contributeGraph(context, rejected).diagnostic == "FOG-GRAPH-PENDING");
        graph.addPass("Test.Consume")
            .read(outputs.color, rhi::ResourceState::ShaderRead)
            .externalSideEffects()
            .execute([](render_graph::RenderContext&) {});
        auto compiled = graph.compile();
        assert(compiled.passes().size() >= 6);
        graph.execute(compiled);
        if (frame == 6)
            feature.resetHistory();
    }
    assert(feature.resize({32, 32}).success);
}

int main(int argc, char**)
{
    try {
        if (argc > 1)
            smoke();
        else {
            components();
            contracts();
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "VolumetricFog/1 passed\n";
}

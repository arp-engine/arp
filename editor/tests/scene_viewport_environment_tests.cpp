#include "editor/viewport/editor_scene_viewport_environment.h"
#include "rhi/backends/null/null_backend.h"
#include "shader/shader_reflection_cache.h"

#include <cassert>
#include <cmath>
#include <cstring>

int main()
{
    using namespace arp;
    const core::pmr::String root(ARP_EDITOR_ENVIRONMENT_SHADER_ROOT);
    const core::pmr::String source = root + "/editor_viewport_environment.slang";
    const core::pmr::String spirv = root + "/editor_viewport_environment.spv";
    const core::pmr::String msl = root + "/editor_viewport_environment.metal";
    const core::pmr::String wgsl = root + "/editor_viewport_environment.wgsl";
    const core::pmr::String reflectionPath = root + "/editor_viewport_environment.reflection.yaml";
    const editor::EditorSceneViewportShaderArtifacts artifacts{
        source, spirv, msl, wgsl, reflectionPath};
    const auto loaded = shader::ShaderReflectionCache::loadFromFile(reflectionPath);
    assert(loaded.success);
    assert(editor::validateEditorSceneViewportEnvironmentReflection(loaded.module.reflection));
    auto invalid = loaded.module.reflection;
    invalid.pushConstantMembers[4].offset += 4u;
    assert(!editor::validateEditorSceneViewportEnvironmentReflection(invalid));
    invalid = loaded.module.reflection;
    invalid.pushConstantMembers[4].name = "transposedDepth";
    assert(!editor::validateEditorSceneViewportEnvironmentReflection(invalid));
    invalid = loaded.module.reflection;
    invalid.pushConstantSizeBytes = 192u;
    assert(!editor::validateEditorSceneViewportEnvironmentReflection(invalid));

    rhi::ShaderReflectionDesc reflection;
    rhi::copyArtifactOwnedShaderReflection(reflection, loaded.module.reflection);
    for (auto backend :
        {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
        rhi::DeviceCapabilities caps;
        caps.backend = backend;
        assert(!editor::editorSceneViewportEnvironmentExecutable(caps, reflection));
        caps.core.graphics = true;
        caps.pushConstantLoweringContractVersion = 1u;
        caps.pushConstantLoweringHasExecutablePayload = true;
        caps.pushConstantLoweringBoundedFrameBudgetBytes = 256u;
        caps.pushConstantLoweringAlignmentBytes = 256u;
        caps.pushConstantLoweringOutcome = backend == rhi::Backend::WebGPU
            ? rhi::PushConstantLoweringOutcome::EquivalentLowering
            : rhi::PushConstantLoweringOutcome::Native;
        caps.pushConstantLoweringLifetimeOwner = backend == rhi::Backend::Metal
            ? rhi::PushConstantLifetimeOwner::NativeRenderEncoder
            : backend == rhi::Backend::WebGPU ? rhi::PushConstantLifetimeOwner::FrameUploadRing
                                              : rhi::PushConstantLifetimeOwner::NativeCommandBuffer;
        const auto lowering = editor::evaluateEditorSceneViewportPipelineLowering(reflection, caps);
        assert(lowering.executable() == (backend != rhi::Backend::Null));
        assert(lowering.disposition
            == (backend == rhi::Backend::Null
                    ? editor::EditorSceneViewportPipelineLoweringDisposition::ContractValidationOnly
                    : backend == rhi::Backend::WebGPU
                    ? editor::EditorSceneViewportPipelineLoweringDisposition::EquivalentLowering
                    : editor::EditorSceneViewportPipelineLoweringDisposition::Native));
        assert(editor::editorSceneViewportEnvironmentExecutable(caps, reflection)
            == (backend == rhi::Backend::Vulkan));
        caps.pushConstantLoweringHasExecutablePayload = false;
        assert(!editor::editorSceneViewportEnvironmentExecutable(caps, reflection));
    }
    auto device = rhi::createNullDevice();
    editor::EditorSceneViewportEnvironment environment;
    core::pmr::String diagnostics;
    assert(!environment.initialize(*device, artifacts, diagnostics));
    assert(diagnostics == "ARP-EDITOR-ENVIRONMENT-EXECUTION-UNAVAILABLE");
    assert(!environment.valid());
    auto missing = artifacts;
    missing.wgslPath = "missing.wgsl";
    assert(!environment.initialize(*device, missing, diagnostics));
    assert(diagnostics == "ARP-EDITOR-ENVIRONMENT-ARTIFACT-INVALID");

    for (const auto profile : {editor::EditorSceneViewportSurfaceProfile{},
             editor::EditorSceneViewportSurfaceProfile::baseline()}) {
        assert(profile.valid());
        const auto pipeline = editor::buildEditorSceneViewportEnvironmentPipeline(source, profile);
        assert(pipeline.colorFormat == profile.color && pipeline.depthFormat == profile.depth);
        assert(pipeline.vertexBuffers.size() == 1 && pipeline.vertexBuffers[0].strideBytes == 8);
        assert(pipeline.vertexBuffers[0].attributes.size() == 1);
        assert(pipeline.depthStencil.depthTestEnabled && !pipeline.depthStencil.depthWriteEnabled);
        assert(rhi::validateRenderPipelineState(pipeline).success);
    }
    assert((!editor::EditorSceneViewportSurfaceProfile{
        rhi::TextureFormat::Rgba8Unorm, rhi::TextureFormat::D32Float}
            .valid()));

    editor::EditorSceneViewportCamera camera;
    assert(camera.position().y > 0.0f);
    const auto checkRays = [&](float aspect) {
        const auto push = editor::buildEditorSceneViewportEnvironmentPush(camera, aspect, true);
        const auto vp = camera.viewProjectionMatrix(aspect);
        for (const auto uv : {core::Vec2{0, 0},
                 core::Vec2{1, 0},
                 core::Vec2{0, 1},
                 core::Vec2{1, 1},
                 core::Vec2{0.5f, 0.5f}}) {
            const core::Vec3 ray{
                push.rayOrigin.x + push.rayHorizontal.x * uv.x + push.rayVertical.x * uv.y,
                push.rayOrigin.y + push.rayHorizontal.y * uv.x + push.rayVertical.y * uv.y,
                push.rayOrigin.z + push.rayHorizontal.z * uv.x + push.rayVertical.z * uv.y};
            const auto point = camera.position() + core::scale(ray, 50.0f);
            const auto ndc = core::transformPoint(vp, point);
            assert(std::abs(ndc.x - (2 * uv.x - 1)) < 0.0002f);
            assert(std::abs(ndc.y - (2 * uv.y - 1)) < 0.0002f);
            const auto numerator =
                core::dot(push.depthNumerator, core::Vec4{point.x, point.y, point.z, 1});
            const auto denominator =
                core::dot(push.depthDenominator, core::Vec4{point.x, point.y, point.z, 1});
            assert(std::abs(numerator / denominator - ndc.z) < 0.00001f);
        }
        assert(push.grid.x == 1 && push.grid.y == 10 && push.grid.z == 100 && push.grid.w == 1);
    };
    checkRays(16.0f / 9.0f);
    checkRays(0.75f);
    camera.orbit(147.0f, -93.0f);
    checkRays(1.37f);
    editor::EditorSceneViewportSky authoredSky;
    authoredSky.present = true;
    authoredSky.zenith = {0.04f, 0.12f, 0.3f};
    authoredSky.horizon = {0.8f, 0.2f, 0.1f};
    authoredSky.sunDirection = {1.0f, 2.0f, 3.0f};
    authoredSky.sunColor = {0.3f, 0.5f, 0.7f};
    const auto authoredPush =
        editor::buildEditorSceneViewportEnvironmentPush(camera, 1.37f, false, authoredSky);
    assert(authoredPush.zenith.x == 0.04f && authoredPush.zenith.y == 0.12f
        && authoredPush.zenith.z == 0.3f);
    assert(authoredPush.horizon.x == 0.8f && authoredPush.horizon.y == 0.2f
        && authoredPush.horizon.z == 0.1f && authoredPush.horizon.w == 1.0f);
    assert(authoredPush.sunDirection.x == 1.0f && authoredPush.sunDirection.y == 2.0f
        && authoredPush.sunDirection.z == 3.0f);
    assert(authoredPush.sunColor.x == 0.3f && authoredPush.sunColor.y == 0.5f
        && authoredPush.sunColor.z == 0.7f);
    const auto beforePan = editor::buildEditorSceneViewportEnvironmentPush(camera, 1.37f, false);
    camera.pan(80, -40, 720);
    const auto afterPan = editor::buildEditorSceneViewportEnvironmentPush(camera, 1.37f, false);
    assert(std::abs(beforePan.rayOrigin.x - afterPan.rayOrigin.x) < 0.00001f);
    assert(std::abs(beforePan.rayOrigin.y - afterPan.rayOrigin.y) < 0.00001f);
    assert(std::abs(beforePan.rayOrigin.z - afterPan.rayOrigin.z) < 0.00001f);
    checkRays(1.37f);
}

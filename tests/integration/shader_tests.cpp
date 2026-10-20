#include "core/cpu_allocator.h"
#include "shader/shader_compiler.h"
#include "shader/shader_pipeline_reflection_validator.h"
#include "shader/shader_reflection_cache.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {

std::pmr::string readTextFile(const char* path)
{
    const std::pmr::string candidates[] = {
        path,
        std::pmr::string("../") + path,
        std::pmr::string("../../") + path,
        std::pmr::string("../../../") + path,
    };
    for (const auto& candidate : candidates) {
        std::ifstream file(candidate.c_str());
        if (file.is_open()) {
            std::pmr::string content;
            content.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
            // An empty result from an "open" stream on Windows can be a
            // spurious match (e.g. a directory or a phantom file descriptor).
            // Only accept non-empty content so the real file is found via a
            // later candidate path.
            if (!content.empty()) {
                return content;
            }
        }
    }
    return {};
}

bool fileExists(const char* path)
{
    const std::pmr::string candidates[] = {
        path,
        std::pmr::string("../") + path,
        std::pmr::string("../../") + path,
        std::pmr::string("../../../") + path,
        std::pmr::string("artifacts/cmake/ninja-mingw-debug/") + path,
        std::pmr::string("artifacts/direct/") + path,
    };
    for (const auto& candidate : candidates) {
        std::ifstream file(candidate.c_str(), std::ios::binary);
        if (file.is_open()) {
            // Guard against spurious Windows "open" on phantom paths.
            file.seekg(0, std::ios::end);
            if (file.tellg() > 0) {
                return true;
            }
        }
    }
    return false;
}

std::pmr::vector<uint8_t> readBinaryFile(const char* path)
{
    const std::pmr::string candidates[] = {
        path,
        std::pmr::string("../") + path,
        std::pmr::string("../../") + path,
        std::pmr::string("../../../") + path,
        std::pmr::string("artifacts/cmake/ninja-mingw-debug/") + path,
        std::pmr::string("artifacts/direct/") + path,
    };
    for (const auto& candidate : candidates) {
        std::ifstream file(candidate.c_str(), std::ios::binary);
        if (file.good()) {
            return std::pmr::vector<uint8_t>(
                std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        }
    }
    return {};
}

bool hasBinding(const std::pmr::vector<arp::shader::ShaderBinding>& bindings,
    const std::pmr::string& name,
    arp::shader::BindingType type)
{
    for (const auto& binding : bindings) {
        if (binding.name == name && binding.type == type) {
            return true;
        }
    }
    return false;
}

uint32_t bindingNumber(
    const std::pmr::vector<arp::shader::ShaderBinding>& bindings, const std::pmr::string& name)
{
    for (const auto& binding : bindings) {
        if (binding.name == name) {
            return binding.binding;
        }
    }
    return UINT32_MAX;
}

uint32_t bindingSet(
    const std::pmr::vector<arp::shader::ShaderBinding>& bindings, const std::pmr::string& name)
{
    for (const auto& binding : bindings) {
        if (binding.name == name) {
            return binding.set;
        }
    }
    return UINT32_MAX;
}

std::pmr::vector<uint32_t> spirvWords(const std::pmr::vector<uint8_t>& bytecode)
{
    assert(bytecode.size() % sizeof(uint32_t) == 0u);
    std::pmr::vector<uint32_t> words(bytecode.size() / sizeof(uint32_t));
    if (!words.empty()) {
        std::memcpy(words.data(), bytecode.data(), bytecode.size());
    }
    return words;
}

std::pmr::string spirvString(
    const std::pmr::vector<uint32_t>& words, std::size_t wordOffset, const std::size_t wordEnd)
{
    std::pmr::string value;
    for (; wordOffset < wordEnd; ++wordOffset) {
        const auto word = words[wordOffset];
        for (uint32_t byte = 0u; byte < 4u; ++byte) {
            const auto ch = static_cast<char>((word >> (byte * 8u)) & 0xffu);
            if (ch == '\0') {
                return value;
            }
            value.push_back(ch);
        }
    }
    return value;
}

uint32_t spirvNamedId(const std::pmr::vector<uint32_t>& words, const std::pmr::string& name)
{
    constexpr uint32_t opName = 5u;
    for (std::size_t offset = 5u; offset < words.size();) {
        const auto wordCount = words[offset] >> 16u;
        const auto opcode = words[offset] & 0xffffu;
        if (wordCount == 0u || offset + wordCount > words.size()) {
            return 0u;
        }
        if (opcode == opName && wordCount >= 3u) {
            const auto id = words[offset + 1u];
            if (spirvString(words, offset + 2u, offset + wordCount) == name) {
                return id;
            }
        }
        offset += wordCount;
    }
    return 0u;
}

uint32_t spirvMemberIndex(const std::pmr::vector<uint32_t>& words,
    const uint32_t parentId,
    const std::pmr::string& memberName)
{
    constexpr uint32_t opMemberName = 6u;
    for (std::size_t offset = 5u; offset < words.size();) {
        const auto wordCount = words[offset] >> 16u;
        const auto opcode = words[offset] & 0xffffu;
        if (wordCount == 0u || offset + wordCount > words.size()) {
            return UINT32_MAX;
        }
        if (opcode == opMemberName && wordCount >= 4u && words[offset + 1u] == parentId
            && spirvString(words, offset + 3u, offset + wordCount) == memberName) {
            return words[offset + 2u];
        }
        offset += wordCount;
    }
    return UINT32_MAX;
}

bool spirvMemberHasDecoration(const std::pmr::vector<uint8_t>& bytecode,
    const std::pmr::string& structName,
    const std::pmr::string& memberName,
    const uint32_t decoration)
{
    constexpr uint32_t opMemberDecorate = 72u;
    const auto words = spirvWords(bytecode);
    const auto structId = spirvNamedId(words, structName);
    if (structId == 0u) {
        return false;
    }
    const auto memberIndex = spirvMemberIndex(words, structId, memberName);
    if (memberIndex == UINT32_MAX) {
        return false;
    }
    for (std::size_t offset = 5u; offset < words.size();) {
        const auto wordCount = words[offset] >> 16u;
        const auto opcode = words[offset] & 0xffffu;
        if (wordCount == 0u || offset + wordCount > words.size()) {
            return false;
        }
        if (opcode == opMemberDecorate && wordCount >= 4u && words[offset + 1u] == structId
            && words[offset + 2u] == memberIndex && words[offset + 3u] == decoration) {
            return true;
        }
        offset += wordCount;
    }
    return false;
}

} // namespace

int main()
{
    arp::shader::ShaderCompiler compiler;
    const std::pmr::string source = "cbuffer SceneConstants;"
                                    "StructuredBuffer<float4> ClusterLightList;"
                                    "Texture2D BaseColorTexture;"
                                    "SamplerState LinearSampler;";

    auto spirv = compiler.compile(arp::shader::ShaderCompileRequest{
        "scene.slang",
        source,
        "SceneStylizedPbr",
        arp::shader::TargetBackend::SpirV,
        {{"vertexMain", arp::shader::ShaderStage::Vertex},
            {"fragmentMain", arp::shader::ShaderStage::Fragment}},
    });

    auto msl = compiler.compile(arp::shader::ShaderCompileRequest{
        "scene.slang",
        source,
        "SceneStylizedPbr",
        arp::shader::TargetBackend::Msl,
        {{"vertexMain", arp::shader::ShaderStage::Vertex},
            {"fragmentMain", arp::shader::ShaderStage::Fragment}},
    });

    auto wgsl = compiler.compile(arp::shader::ShaderCompileRequest{
        "scene.slang",
        source,
        "SceneStylizedPbr",
        arp::shader::TargetBackend::Wgsl,
        {{"vertexMain", arp::shader::ShaderStage::Vertex},
            {"fragmentMain", arp::shader::ShaderStage::Fragment}},
    });

    assert(spirv.success);
    assert(msl.success);
    assert(wgsl.success);
    assert(spirv.bindings.size() == 4);
    assert(msl.bindings.size() == 4);
    assert(wgsl.bindings.size() == 4);
    assert(!spirv.bytecode.empty());
    assert(!msl.bytecode.empty());
    assert(!wgsl.bytecode.empty());
    assert(spirv.reflection.entryPoints.size() == 2);
    assert(wgsl.reflection.entryPoints.size() == 2);

    const auto clearShaderSource = readTextFile("shaders/samples/clear.slang");
    assert(clearShaderSource.find("SceneRuntimeDemoPushConstants") != std::pmr::string::npos);
    assert(clearShaderSource.find("[[vk::push_constant]]") != std::pmr::string::npos);
    assert(clearShaderSource.find("cubePosition") != std::pmr::string::npos);
    assert(clearShaderSource.find("spritePosition") != std::pmr::string::npos);
    assert(clearShaderSource.find("uiPosition") != std::pmr::string::npos);
    auto clearReflection = compiler.compile(arp::shader::ShaderCompileRequest{
        "samples/clear.slang",
        clearShaderSource,
        "SceneRuntimeVisibleDemo",
        arp::shader::TargetBackend::SpirV,
        {{"vertexMain", arp::shader::ShaderStage::Vertex},
            {"fragmentMain", arp::shader::ShaderStage::Fragment}},
    });
    assert(clearReflection.success);
    assert(clearReflection.reflection.pushConstantSizeBytes == 16u);

    const std::pmr::string stylizedSource =
        "struct StylizedScenePushConstants {"
        "float4x4 viewProjection;"
        "float4 cameraPositionAndTime;"
        "float4 baseColor;"
        "float4 shadeColor;"
        "float4 lightDirectionAndWrap;"
        "float4 rampParams;"
        "float4 fogParams;"
        "float4 materialParams;"
        "float4 pbrParams;"
        "float4 pbrExtraParams;"
        "float4 emissiveParams;"
        "float4 stylizedParams;"
        "float4 gltfExtensionParams;"
        "};"
        "[[vk::push_constant]] StylizedScenePushConstants gScene;"
        "struct VertexInput {"
        "float3 position : POSITION0;"
        "float3 normal : NORMAL0;"
        "float2 uv : TEXCOORD0;"
        "float4 tangent : TANGENT0;"
        "};";
    auto reflected = compiler.compile(arp::shader::ShaderCompileRequest{
        "stylized/scene.slang",
        stylizedSource,
        "StylizedScene",
        arp::shader::TargetBackend::SpirV,
        {{"vertexMain", arp::shader::ShaderStage::Vertex},
            {"fragmentMain", arp::shader::ShaderStage::Fragment}},
    });
    assert(reflected.success);
    assert(reflected.reflection.pushConstantSizeBytes == 256);
    assert(reflected.reflection.vertexAttributes.size() == 4);
    assert(reflected.reflection.vertexAttributes[0].semantic == std::pmr::string("POSITION0"));
    assert(reflected.reflection.vertexAttributes[0].format
        == arp::shader::VertexAttributeFormat::Float3);
    assert(reflected.reflection.vertexAttributes[2].semantic == std::pmr::string("TEXCOORD0"));
    assert(reflected.reflection.vertexAttributes[2].format
        == arp::shader::VertexAttributeFormat::Float2);
    assert(reflected.reflection.vertexAttributes[3].semantic == std::pmr::string("TANGENT0"));
    assert(reflected.reflection.vertexAttributes[3].format
        == arp::shader::VertexAttributeFormat::Float4);

    const std::pmr::string reflectionYaml = R"yaml(
moduleName: StylizedScene
sourcePath: "D:\\work\\ai-api\\shaders\\stylized\\scene.slang"
entryPoints:
  - name: vertexMain
    stage: Vertex
  - name: fragmentMain
    stage: Fragment
bindings:
  - name: BaseColorTexture
    type: SampledTexture
    set: 1
    binding: 0
  - name: LinearSampler
    type: Sampler
    set: 1
    binding: 1
pushConstantSizeBytes: 256
vertexAttributes:
  - name: position
    semantic: POSITION0
    format: Float3
    location: 0
  - name: normal
    semantic: NORMAL0
    format: Float3
    location: 1
  - name: uv
    semantic: TEXCOORD0
    format: Float2
    location: 2
  - name: tangent
    semantic: TANGENT0
    format: Float4
    location: 3
targets:
  spirv: "D:\\work\\ai-api\\build\\shaders\\stylized_scene.spv"
  metal: "D:\\work\\ai-api\\build\\shaders\\stylized_scene.metal"
  wgsl: "D:\\work\\ai-api\\build\\shaders\\stylized_scene.wgsl"
)yaml";
    const auto cached = arp::shader::ShaderReflectionCache::loadFromYaml(reflectionYaml);
    assert(cached.success);
    assert(cached.module.moduleName == std::pmr::string("StylizedScene"));
    assert(cached.module.spirvPath.find("stylized_scene.spv") != std::pmr::string::npos);
    assert(cached.module.metalPath.find("stylized_scene.metal") != std::pmr::string::npos);
    assert(cached.module.wgslPath.find("stylized_scene.wgsl") != std::pmr::string::npos);
    assert(cached.module.reflection.entryPoints.size() == 2);
    assert(cached.module.reflection.bindings.size() == 2);
    assert(cached.module.reflection.bindings[0].type == arp::shader::BindingType::SampledTexture);
    assert(cached.module.reflection.bindings[1].type == arp::shader::BindingType::Sampler);
    assert(cached.module.reflection.pushConstantSizeBytes == 256);
    assert(cached.module.reflection.vertexAttributes.size() == 4);
    assert(cached.module.reflection.vertexAttributes[1].semantic == std::pmr::string("NORMAL0"));
    assert(cached.module.reflection.vertexAttributes[2].format
        == arp::shader::VertexAttributeFormat::Float2);
    assert(cached.module.reflection.vertexAttributes[3].format
        == arp::shader::VertexAttributeFormat::Float4);

    const auto validPipeline = arp::shader::ShaderPipelineReflectionValidator::validate(
        arp::shader::ShaderPipelineLayoutDesc{
            256,
            cached.module.reflection.vertexAttributes,
        },
        cached.module.reflection);
    assert(validPipeline.success);

    const auto badPushConstants = arp::shader::ShaderPipelineReflectionValidator::validate(
        arp::shader::ShaderPipelineLayoutDesc{
            64,
            cached.module.reflection.vertexAttributes,
        },
        cached.module.reflection);
    assert(!badPushConstants.success);
    assert(badPushConstants.diagnostics.find("push constant") != std::pmr::string::npos);

    auto badAttributes = cached.module.reflection.vertexAttributes;
    badAttributes[2].format = arp::shader::VertexAttributeFormat::Float3;
    const auto badVertexLayout = arp::shader::ShaderPipelineReflectionValidator::validate(
        arp::shader::ShaderPipelineLayoutDesc{
            256,
            badAttributes,
        },
        cached.module.reflection);
    assert(!badVertexLayout.success);
    assert(badVertexLayout.diagnostics.find("vertex attribute") != std::pmr::string::npos);

    const auto sceneShaderSource = readTextFile("shaders/stylized/scene.slang");
    assert(sceneShaderSource.find("float4x4 viewProjection") != std::pmr::string::npos);
    assert(sceneShaderSource.find("float4 materialParams") != std::pmr::string::npos);
    assert(sceneShaderSource.find("float4 pbrParams") != std::pmr::string::npos);
    assert(sceneShaderSource.find("float4 pbrExtraParams") != std::pmr::string::npos);
    assert(sceneShaderSource.find("float4 emissiveParams") != std::pmr::string::npos);
    assert(sceneShaderSource.find("float4 stylizedParams") != std::pmr::string::npos);
    assert(sceneShaderSource.find("float4 gltfExtensionParams") != std::pmr::string::npos);
    assert(sceneShaderSource.find("float4 pikaPassParams") == std::pmr::string::npos);
    assert(sceneShaderSource.find("[[vk::binding(7, 2)]]\nTexture2D<float4> GiIrradianceInput")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("[[vk::binding(8, 2)]]\nSamplerState GiIrradianceSampler")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("GiIrradianceInput.Sample(GiIrradianceSampler")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("float3 fallbackIrradiance = float3(0.36, 0.40, 0.50)")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("return color + irradiance * intensity * occlusionFactor")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("float giAlbedoTintStrength = saturate(gScene.stylizedParams.z)")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("float2 giUv = saturate(normal.xz * 0.5 + float2(0.5, 0.5))")
        != std::pmr::string::npos);
    assert(
        sceneShaderSource.find(
            "float3 irradiance = lerp(fallbackIrradiance, sampledIrradiance, step(0.0001, intensity))")
        != std::pmr::string::npos);
    assert(
        sceneShaderSource.find(
            "irradiance = lerp(irradiance, irradiance * materialBaseColor, giAlbedoTintStrength)")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("[[vk::binding(0, 1)]]\nTexture2D<float4> BaseColorTexture")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("[[vk::binding(4, 1)]]\nTexture2D<float4> OcclusionTexture")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("[[vk::binding(5, 1)]]\nSamplerState LinearSampler")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("[[vk::binding(6, 1)]]\nTexture2D<float4> ClearcoatTexture")
        != std::pmr::string::npos);
    assert(
        sceneShaderSource.find("[[vk::binding(16, 1)]]\nTexture2D<float4> ClearcoatNormalTexture")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("[[vk::binding(17, 1)]]\nTexture2D<float4> IlmTexture")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("[[vk::binding(18, 1)]]\nTexture2D<float4> LightingRampTexture")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("[[vk::binding(19, 1)]]\nTexture2D<float4> FaceSdfTexture")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("[[vk::binding(20, 1)]]\nTexture2D<float4> MatcapTexture")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("[[vk::binding(21, 1)]]\nTexture2D<float4> FlowTexture")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("[[vk::binding(22, 1)]]\nTexture2D<float4> ScreenTexture")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("[[vk::binding(23, 1)]]\nTexture2D<float4> FurCoverageTexture")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("[[vk::binding(24, 1)]]\nTexture2D<float4> FurNoiseTexture")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("[[vk::binding(0, 2)]]\nTextureCube<float4> SceneIrradianceCube")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find(
               "[[vk::binding(1, 2)]]\nTextureCube<float4> ScenePrefilteredSpecularCube")
        != std::pmr::string::npos);
    assert(
        sceneShaderSource.find("[[vk::binding(2, 2)]]\nTexture2D<float4> SceneBrdfIntegrationLut")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("[[vk::binding(3, 2)]]\nSamplerState SceneIblSampler")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("[[vk::binding(4, 2)]]\nTexture2D<float> SceneShadowAtlas")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("[[vk::binding(5, 2)]]\nSamplerState SceneShadowSampler")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("struct SceneShadowParams") != std::pmr::string::npos);
    assert(
        sceneShaderSource.find("[[vk::binding(6, 2)]]\nConstantBuffer<SceneShadowParams> gShadow")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("float4 shadowPcfParams") != std::pmr::string::npos);
    assert(sceneShaderSource.find("float4 shadowCascadeFarPlanes") != std::pmr::string::npos);
    assert(sceneShaderSource.find("float4 shadowCascadeTexelWorldSizes") != std::pmr::string::npos);
    assert(
        sceneShaderSource.find("float4 shadowAtlasTexelSizeAndPadding") != std::pmr::string::npos);
    assert(sceneShaderSource.find("float4 shadowAtlasRegions[4]") != std::pmr::string::npos);
    assert(sceneShaderSource.find("float4 shadowLightViewProjectionRows[16]")
        != std::pmr::string::npos);
    assert(
        sceneShaderSource.find("float4x4 shadowLightViewProjection[4]") == std::pmr::string::npos);
    assert(sceneShaderSource.find("float3 sampleSceneIblDiffuse(float3 normal)")
        != std::pmr::string::npos);
    assert(
        sceneShaderSource.find(
            "float3 sampleSceneIblSpecular(float3 reflectionDirection, float roughness, float nDotV, float3 fresnel)")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("SceneIrradianceCube.SampleLevel(SceneIblSampler, normal, 0.0)")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("ScenePrefilteredSpecularCube.SampleLevel(")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("SceneBrdfIntegrationLut.Sample(SceneIblSampler")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("float sampleSceneShadowPcf(float3 worldPosition, float nDotL)")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("SceneShadowAtlas.SampleLevel(SceneShadowSampler")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("gShadow.shadowParams.x") != std::pmr::string::npos);
    assert(sceneShaderSource.find("gShadow.shadowCascadeFarPlanes") != std::pmr::string::npos);
    assert(
        sceneShaderSource.find(
            "float4 shadowLightClipFromLightViewProjectionRows(int cascadeIndex, float3 worldPosition)")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("int rowBase = cascadeIndex * 4") != std::pmr::string::npos);
    assert(
        sceneShaderSource.find("dot(gShadow.shadowLightViewProjectionRows[rowBase + 0], position)")
        != std::pmr::string::npos);
    assert(
        sceneShaderSource.find("mul(gShadow.shadowLightViewProjection") == std::pmr::string::npos);
    assert(sceneShaderSource.find("gShadow.shadowAtlasRegions[cascadeIndex]")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("gShadow.shadowAtlasTexelSizeAndPadding.xy")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("gShadow.shadowPcfParams.w") != std::pmr::string::npos);
    const auto shadowPcfFunction =
        sceneShaderSource.find("float sampleSceneShadowPcf(float3 worldPosition, float nDotL)");
    const auto shadowPcfFunctionEnd =
        sceneShaderSource.find("float3 applyGlobalIllumination", shadowPcfFunction);
    assert(shadowPcfFunction != std::pmr::string::npos);
    assert(shadowPcfFunctionEnd != std::pmr::string::npos);
    const auto shadowPcfFunctionBody =
        sceneShaderSource.substr(shadowPcfFunction, shadowPcfFunctionEnd - shadowPcfFunction);
    assert(shadowPcfFunctionBody.find(
               "float2 cascadeUv = projected.xy * float2(0.5, 0.5) + float2(0.5, 0.5)")
        != std::pmr::string::npos);
    assert(shadowPcfFunctionBody.find("float2(0.5, -0.5)") == std::pmr::string::npos);
    const auto prototypeShadowAtlasUv = std::pmr::string("frac(worldPosition.xz") + " * 0.035";
    const auto prototypeShadowReceiverUv = std::pmr::string("worldPosition.xz") + " * 0.035";
    assert(sceneShaderSource.find(prototypeShadowAtlasUv) == std::pmr::string::npos);
    assert(sceneShaderSource.find(prototypeShadowReceiverUv) == std::pmr::string::npos);
    assert(sceneShaderSource.find("shadowCascadeParams") == std::pmr::string::npos);
    assert(sceneShaderSource.find(
               "color *= lerp(1.0 - saturate(gShadow.shadowParams.w), 1.0, sceneShadowVisibility)")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("BaseColorTexture") != std::pmr::string::npos);
    assert(sceneShaderSource.find("NormalTexture") != std::pmr::string::npos);
    assert(sceneShaderSource.find("MetallicRoughnessTexture") != std::pmr::string::npos);
    assert(sceneShaderSource.find("OcclusionTexture") != std::pmr::string::npos);
    assert(sceneShaderSource.find("EmissiveTexture") != std::pmr::string::npos);
    assert(sceneShaderSource.find("SpecularColorTexture") != std::pmr::string::npos);
    assert(sceneShaderSource.find("SheenColorTexture") != std::pmr::string::npos);
    assert(sceneShaderSource.find("TransmissionTexture") != std::pmr::string::npos);
    assert(sceneShaderSource.find("IridescenceThicknessTexture") != std::pmr::string::npos);
    assert(sceneShaderSource.find("LinearSampler") != std::pmr::string::npos);
    assert(sceneShaderSource.find("BaseColorTexture.Sample") != std::pmr::string::npos);
    assert(
        sceneShaderSource.find(
            "float3 shadedBaseColor = materialBaseColor * max(gScene.shadeColor.rgb, float3(0.34, 0.34, 0.34))")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("float colorHintStrength = saturate(gScene.materialParams.y)")
        != std::pmr::string::npos);
    assert(
        sceneShaderSource.find(
            "float3 litBaseColor = materialBaseColor * lerp(float3(1.0, 1.0, 1.0), input.colorHint, colorHintStrength)")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("float skyGradientStrength = saturate(gScene.materialParams.x)")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("float skyT = saturate(input.heightTint)")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find(
               "float3 skyColor = lerp(gScene.shadeColor.rgb, gScene.baseColor.rgb, skyT)")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("if (skyGradientStrength > 0.0)") != std::pmr::string::npos);
    assert(sceneShaderSource.find("lerp(shadedBaseColor, litBaseColor") != std::pmr::string::npos);
    assert(sceneShaderSource.find("lerp(shadedBaseColor, materialBaseColor * input.colorHint")
        == std::pmr::string::npos);
    assert(
        sceneShaderSource.find("float albedoPreserveStrength = saturate(gScene.stylizedParams.z)")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("color = lerp(color, materialBaseColor, albedoPreserveStrength)")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("float heightTintStrength = saturate(gScene.materialParams.z)")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("color = lerp(color, color * heightColor, heightTintStrength)")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("NormalTexture.Sample") != std::pmr::string::npos);
    assert(sceneShaderSource.find("TANGENT0") != std::pmr::string::npos);
    assert(sceneShaderSource.find("cross(geometryNormal, tangent") != std::pmr::string::npos);
    assert(sceneShaderSource.find("baseColor * gScene.lightColor.rgb") != std::pmr::string::npos);
    assert(sceneShaderSource.find("saturate(gScene.lightColor.w) * ambientShadow * occlusion")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("MetallicRoughnessTexture.Sample") != std::pmr::string::npos);
    assert(sceneShaderSource.find("OcclusionTexture.Sample") != std::pmr::string::npos);
    assert(sceneShaderSource.find("EmissiveTexture.Sample") != std::pmr::string::npos);
    assert(sceneShaderSource.find("materialKind") == std::pmr::string::npos);
    assert(sceneShaderSource.find("slot->slot") == std::pmr::string::npos);
    assert(sceneShaderSource.find("materialKind == 1.0") == std::pmr::string::npos);
    assert(sceneShaderSource.find("materialKind == 2.0") == std::pmr::string::npos);
    assert(sceneShaderSource.find("materialKind == 3.0") == std::pmr::string::npos);
    assert(sceneShaderSource.find("materialKind == 4.0") == std::pmr::string::npos);
    assert(sceneShaderSource.find("materialKind == 5.0") == std::pmr::string::npos);
    assert(sceneShaderSource.find("roughnessFactor") != std::pmr::string::npos);
    assert(sceneShaderSource.find(
               "float distributionGGX(float3 normal, float3 halfway, float roughness)")
        != std::pmr::string::npos);
    assert(
        sceneShaderSource.find(
            "float geometrySmith(float3 normal, float3 viewDirection, float3 lightDirection, float roughness)")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("float3 fresnelSchlick(float cosTheta, float3 f0)")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("float3 normal : TEXCOORD0") != std::pmr::string::npos);
    assert(sceneShaderSource.find("float3 worldPosition : TEXCOORD6") != std::pmr::string::npos);
    assert(sceneShaderSource.find("float4 tangentAndSign : TEXCOORD7") != std::pmr::string::npos);
    assert(
        sceneShaderSource.find(
            "float sampledOcclusion = saturate(OcclusionTexture.Sample(LinearSampler, input.materialUv).r)")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("float packedOcclusion = saturate(sampledMetallicRoughness.r)")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("float occlusionStrength = saturate(gScene.pbrExtraParams.x)")
        != std::pmr::string::npos);
    assert(
        sceneShaderSource.find(
            "float occlusionFactor = lerp(1.0, min(sampledOcclusion, packedOcclusion), occlusionStrength)")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find(
               "float3 dielectricF0 = float3(0.04, 0.04, 0.04) * max(specularFactor, 0.04)")
        != std::pmr::string::npos);
    assert(
        sceneShaderSource.find("float3 f0 = lerp(dielectricF0, materialBaseColor, metallicFactor)")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find(
               "float3 kD = (float3(1.0, 1.0, 1.0) - fresnel) * (1.0 - metallicFactor)")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("float3 pbrSpecular = numerator / max(denominator, 0.001)")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("clearcoatFactor") != std::pmr::string::npos);
    assert(sceneShaderSource.find("clearcoatSpecular") != std::pmr::string::npos);
    assert(sceneShaderSource.find("clearcoatTextureFactor") != std::pmr::string::npos);
    assert(sceneShaderSource.find("sampledClearcoatNormal") != std::pmr::string::npos);
    assert(sceneShaderSource.find("specularFactor") != std::pmr::string::npos);
    assert(sceneShaderSource.find("specularColorTexture") != std::pmr::string::npos);
    assert(sceneShaderSource.find("sheenStrength") != std::pmr::string::npos);
    assert(sceneShaderSource.find("sheenColorTexture") != std::pmr::string::npos);
    assert(sceneShaderSource.find("transmissionFactor") != std::pmr::string::npos);
    assert(sceneShaderSource.find("thicknessTextureFactor") != std::pmr::string::npos);
    assert(sceneShaderSource.find("iridescenceFactor") != std::pmr::string::npos);
    assert(sceneShaderSource.find("iridescenceThicknessTextureFactor") != std::pmr::string::npos);
    assert(sceneShaderSource.find("emissiveParams.rgb") != std::pmr::string::npos);
    assert(sceneShaderSource.find("alphaMode") != std::pmr::string::npos);
    assert(sceneShaderSource.find("discard") != std::pmr::string::npos);
    assert(sceneShaderSource.find("applyGlobalIllumination") != std::pmr::string::npos);
    assert(sceneShaderSource.find("directionalShadow") != std::pmr::string::npos);
    assert(sceneShaderSource.find("outlineRim") != std::pmr::string::npos);
    assert(sceneShaderSource.find("int resolvePiKaMaterialId(float ilmMaterialSample)")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("float resolvePiKaRampRow(int materialId)")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("float3 samplePiKaLightingRamp(float rampMask, float rampRow)")
        != std::pmr::string::npos);
    assert(
        sceneShaderSource.find(
            "float evalPiKaIlmRampLighting(float3 normal, float3 lightDirection, float4 ilmSample)")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("bool isPiKaUnityForward()") != std::pmr::string::npos);
    assert(sceneShaderSource.find("bool hasPiKaIlmRamp()") != std::pmr::string::npos);
    assert(sceneShaderSource.find("bool hasPiKaFaceSdf()") != std::pmr::string::npos);
    assert(
        sceneShaderSource.find("return resolvePiKaFeatureMask() > 0.0;") != std::pmr::string::npos);
    const auto pikaIlmRampFunction = sceneShaderSource.find("bool hasPiKaIlmRamp()");
    const auto pikaUnityForwardFunction = sceneShaderSource.find("bool isPiKaUnityForward()");
    assert(pikaIlmRampFunction != std::pmr::string::npos);
    assert(pikaUnityForwardFunction != std::pmr::string::npos);
    assert(sceneShaderSource.find("return hasPiKaFeature(1.0);", pikaIlmRampFunction)
        != std::pmr::string::npos);
    const auto pikaUnityForwardFunctionBody = sceneShaderSource.substr(
        pikaUnityForwardFunction, pikaIlmRampFunction - pikaUnityForwardFunction);
    assert(pikaUnityForwardFunctionBody.find("return resolvePiKaFeatureMask() > 0.0;")
        != std::pmr::string::npos);
    assert(
        pikaUnityForwardFunctionBody.find("return hasPiKaFeature(1.0);") == std::pmr::string::npos);
    assert(sceneShaderSource.find(
               "float evalFallbackPiKaRampMask(float3 normal, float3 lightDirection)")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("float3 sampleFallbackPiKaRamp(float rampMask)")
        != std::pmr::string::npos);
    assert(
        sceneShaderSource.find(
            "hasPiKaIlmRamp() ? IlmTexture.Sample(LinearSampler, input.materialUv) : float4(0.5, 1.0, 0.0, 0.0)")
        != std::pmr::string::npos);
    assert(
        sceneShaderSource.find(
            "hasPiKaFaceSdf() ? FaceSdfTexture.Sample(LinearSampler, input.materialUv) : float4(1.0, 1.0, 1.0, 1.0)")
        != std::pmr::string::npos);
    assert(
        sceneShaderSource.find(
            "hasPiKaIlmRamp() ? evalPiKaIlmRampLighting(shadingNormal, lightDirection, pikaIlmSample) : evalFallbackPiKaRampMask(shadingNormal, lightDirection)")
        != std::pmr::string::npos);
    assert(
        sceneShaderSource.find(
            "hasPiKaIlmRamp() ? samplePiKaLightingRamp(pikaRampMask, resolvePiKaRampRow(pikaMaterialId)) : sampleFallbackPiKaRamp(pikaRampMask)")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("float resolvePiKaMaterialModel()") == std::pmr::string::npos);
    assert(sceneShaderSource.find("float3 evalPiKaUnityFaceForward(") == std::pmr::string::npos);
    assert(
        sceneShaderSource.find("if (resolvePiKaMaterialModel() == 1.0)") == std::pmr::string::npos);
    assert(sceneShaderSource.find("return float4(evalPiKaUnityFaceForward(")
        == std::pmr::string::npos);
    assert(sceneShaderSource.find("float3 color = materialBaseColor * pikaRamp")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("return float4(color, materialAlpha)") != std::pmr::string::npos);
    const auto pikaUnityForwardBranch = sceneShaderSource.find("if (isPiKaUnityForward())");
    const auto cpuPackedStylizedShadow = sceneShaderSource.find(
        "ShadowSamplingParams shadowParams = { gShadow.shadowPcfParams.x, gShadow.shadowPcfParams.y, gShadow.shadowPcfParams.z, gShadow.shadowParams.w }");
    const auto pbrSpecularPath =
        sceneShaderSource.find("float3 pbrSpecular = numerator / max(denominator, 0.001)");
    assert(pikaUnityForwardBranch != std::pmr::string::npos);
    assert(cpuPackedStylizedShadow != std::pmr::string::npos);
    assert(pbrSpecularPath != std::pmr::string::npos);
    const auto pikaUnityForwardBranchEnd =
        sceneShaderSource.find("float metallicFactor", pikaUnityForwardBranch);
    assert(pikaUnityForwardBranchEnd != std::pmr::string::npos);
    const auto pikaUnityForwardBranchBody = sceneShaderSource.substr(
        pikaUnityForwardBranch, pikaUnityForwardBranchEnd - pikaUnityForwardBranch);
    assert(pikaUnityForwardBranchBody.find("ShadowSamplingParams shadowParams")
        == std::pmr::string::npos);
    assert(pikaUnityForwardBranchBody.find(
               "float sceneShadowVisibility = sampleSceneShadowPcf(input.worldPosition, nDotL)")
        != std::pmr::string::npos);
    assert(
        pikaUnityForwardBranchBody.find("color *= sampleStylizedShadow(pikaRampMask, shadowParams)")
        == std::pmr::string::npos);
    assert(pikaUnityForwardBranchBody.find(
               "color *= lerp(1.0 - saturate(gShadow.shadowParams.w), 1.0, sceneShadowVisibility)")
        != std::pmr::string::npos);
    assert(pikaUnityForwardBranchBody.find("return float4(color, materialAlpha)")
        > pikaUnityForwardBranchBody.find("sampleSceneShadowPcf(input.worldPosition, nDotL)"));
    assert(pikaUnityForwardBranch < pbrSpecularPath);
    assert(sceneShaderSource.find(
               "float3 evalPiKaMatCap(float3 albedo, float3 normal, float matcapRoute)")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find(
               "float applyPiKaFaceSdfShadow(float lit, float4 faceSdf, float lightSide)")
        != std::pmr::string::npos);
    assert(
        sceneShaderSource.find("float3 applyPiKaRimAccentEmissionFlow(") != std::pmr::string::npos);
    assert(
        sceneShaderSource.find("bool hasPiKaFeature(float featureBit)") != std::pmr::string::npos);
    assert(sceneShaderSource.find("float resolvePiKaFeatureMask()") != std::pmr::string::npos);
    assert(sceneShaderSource.find("hasPiKaFeature(8.0)") != std::pmr::string::npos);
    assert(sceneShaderSource.find("hasPiKaFeature(16.0)") != std::pmr::string::npos);
    assert(sceneShaderSource.find("hasPiKaFeature(256.0)") != std::pmr::string::npos);
    assert(sceneShaderSource.find("hasPiKaFeature(512.0)") != std::pmr::string::npos);
    assert(sceneShaderSource.find("float evalPiKaFurShellAlpha(float2 uv, float shellLayer)")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("float3 applyPiKaOutlineShading(float3 color, float outlineMask)")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find(
               "float3 applyPiKaPlanarShadowTint(float3 color, float3 worldPosition)")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("int resolvePiKaPassKind()") != std::pmr::string::npos);
    assert(sceneShaderSource.find("float resolveMaterialAlphaMode()") != std::pmr::string::npos);
    assert(sceneShaderSource.find("float resolvePiKaFurShellLayer()") != std::pmr::string::npos);
    assert(sceneShaderSource.find("float resolvePiKaFurShellCount()") != std::pmr::string::npos);
    assert(sceneShaderSource.find("float resolvePiKaOutlineExtrusion()") != std::pmr::string::npos);
    assert(sceneShaderSource.find("min(resolvePiKaOutlineWidth(), 0.65) * 0.0018")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("resolvePiKaOutlineWidth() * 0.012") == std::pmr::string::npos);
    assert(
        sceneShaderSource.find("float effectiveMaterialAlpha(float alphaMode, float sampledAlpha)")
        != std::pmr::string::npos);
    assert(
        sceneShaderSource.find("float outlineSilhouetteMask(float3 normal, float3 worldPosition)")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("if (outlineMask < 0.08)") != std::pmr::string::npos);
    assert(sceneShaderSource.find(
               "float auxiliaryPassAlpha = effectiveMaterialAlpha(alphaMode, materialAlpha)")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("return smoothstep(0.72, 0.94, 1.0 - facing)")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find(
               "float4(resolvePiKaOutlineColor(), auxiliaryPassAlpha * 0.22 * outlineMask)")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("float4(resolvePiKaOutlineColor(), materialAlpha * 0.28)")
        == std::pmr::string::npos);
    assert(sceneShaderSource.find("float3 applyPiKaCharacterColorGrade(float3 color)")
        == std::pmr::string::npos);
    assert(sceneShaderSource.find("color = applyPiKaCharacterColorGrade(color)")
        == std::pmr::string::npos);
    assert(sceneShaderSource.find("return int(gScene.emissiveParams.w * 0.1 + 0.001)")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("if (pikaPassKind == 1)") != std::pmr::string::npos);
    assert(sceneShaderSource.find("if (pikaPassKind == 2)") != std::pmr::string::npos);
    assert(sceneShaderSource.find("if (pikaPassKind == 3)") != std::pmr::string::npos);
    assert(sceneShaderSource.find("if (pikaPassKind == 4)") != std::pmr::string::npos);
    assert(sceneShaderSource.find("float3 resolvePiKaPlanarShadowColor(float materialAlpha)")
        != std::pmr::string::npos);
    assert(
        sceneShaderSource.find("applyPiKaPlanarShadowTint(materialBaseColor, input.worldPosition)")
        == std::pmr::string::npos);
    const auto fragmentMain = sceneShaderSource.find("float4 fragmentMain(VertexOutput input)");
    assert(fragmentMain != std::pmr::string::npos);
    const auto pikaXRayBranch = sceneShaderSource.find("if (pikaPassKind == 1)", fragmentMain);
    const auto pikaOutlineBranch = sceneShaderSource.find("if (pikaPassKind == 2)", fragmentMain);
    const auto pikaPlanarShadowBranch =
        sceneShaderSource.find("if (pikaPassKind == 3)", fragmentMain);
    const auto pikaFurShellBranch = sceneShaderSource.find("if (pikaPassKind == 4)", fragmentMain);
    assert(pikaXRayBranch != std::pmr::string::npos);
    assert(pikaOutlineBranch != std::pmr::string::npos);
    assert(pikaPlanarShadowBranch != std::pmr::string::npos);
    assert(pikaFurShellBranch != std::pmr::string::npos);
    assert(pikaXRayBranch < pikaOutlineBranch);
    assert(pikaOutlineBranch < pikaPlanarShadowBranch);
    assert(pikaPlanarShadowBranch < pikaFurShellBranch);
    assert(pikaFurShellBranch < pikaUnityForwardBranch);
    const auto pikaForwardSegment = sceneShaderSource.substr(
        pikaUnityForwardBranch, cpuPackedStylizedShadow - pikaUnityForwardBranch);
    const auto planarShadowSegment = sceneShaderSource.substr(
        pikaPlanarShadowBranch, pikaFurShellBranch - pikaPlanarShadowBranch);
    assert(pikaForwardSegment.find("resolvePiKaPlanarShadowColor") == std::pmr::string::npos);
    assert(pikaForwardSegment.find("applyPiKaPlanarShadowTint") == std::pmr::string::npos);
    assert(planarShadowSegment.find("resolvePiKaPlanarShadowColor(auxiliaryPassAlpha)")
        != std::pmr::string::npos);
    assert(planarShadowSegment.find("materialBaseColor") == std::pmr::string::npos);
    assert(sceneShaderSource.find("float threshold = saturate(gScene.rampParams.x)")
        != std::pmr::string::npos);
    assert(
        sceneShaderSource.find(
            "float softness = max(saturate(gScene.rampParams.x) * saturate(gScene.rampParams.w), 0.0001)")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("0.025 + gScene.rampParams.w * 0.25") == std::pmr::string::npos);
    assert(sceneShaderSource.find(
               "evalPiKaFurShellAlpha(input.materialUv, resolvePiKaFurShellLayer())")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("IlmTexture.Sample(LinearSampler, input.materialUv)")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("FaceSdfTexture.Sample(LinearSampler, input.materialUv)")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("FlowTexture.Sample(LinearSampler") != std::pmr::string::npos);
    assert(sceneShaderSource.find("FurCoverageTexture.Sample(LinearSampler")
        != std::pmr::string::npos);
    assert(sceneShaderSource.find("applyToneMapping") != std::pmr::string::npos);
    assert(sceneShaderSource.find("applyTimeOfDayGrade") != std::pmr::string::npos);
    assert(sceneShaderSource.find("applyDistanceFog") != std::pmr::string::npos);
    assert(sceneShaderSource.find("mul(gScene.viewProjection") != std::pmr::string::npos);
    assert(sceneShaderSource.find("sin(gScene.modelParams.x)") == std::pmr::string::npos);
    assert(sceneShaderSource.find("cos(gScene.modelParams.x)") == std::pmr::string::npos);
    const auto sceneDrawPlanSource = readTextFile("samples/stylized/stylized_scene_draw_plan.h");
    assert(sceneDrawPlanSource.find("materialKindForDrawPlan") == std::pmr::string::npos);
    assert(sceneDrawPlanSource.find("switch (slot->slot)") == std::pmr::string::npos);
    assert(sceneDrawPlanSource.find("case 10:") == std::pmr::string::npos);
    assert(sceneDrawPlanSource.find("case 20:") == std::pmr::string::npos);
    assert(sceneDrawPlanSource.find("case 30:") == std::pmr::string::npos);
    assert(sceneDrawPlanSource.find("case 40:") == std::pmr::string::npos);
    assert(sceneShaderSource.find("ForwardPlusLightRecord") != std::pmr::string::npos);
    assert(sceneShaderSource.find("ClusterLightList") != std::pmr::string::npos);
    assert(sceneShaderSource.find("ShadowSamplingParams") != std::pmr::string::npos);
    assert(sceneShaderSource.find("sampleStylizedShadow") != std::pmr::string::npos);
    assert(sceneShaderSource.find("contactShadow") != std::pmr::string::npos);
    assert(sceneShaderSource.find("float2(projected.x, -projected.y)") == std::pmr::string::npos);
    assert(sceneShaderSource.find("smoothstep(6.5, 10.5") == std::pmr::string::npos);
    auto sceneForwardPlus = compiler.compile(arp::shader::ShaderCompileRequest{
        "stylized/scene.slang",
        sceneShaderSource,
        "StylizedScene",
        arp::shader::TargetBackend::SpirV,
        {{"vertexMain", arp::shader::ShaderStage::Vertex},
            {"fragmentMain", arp::shader::ShaderStage::Fragment}},
    });
    assert(sceneForwardPlus.success);
    assert(hasBinding(sceneForwardPlus.reflection.bindings,
        "ClusterLightList",
        arp::shader::BindingType::StorageBuffer));
    assert(hasBinding(sceneForwardPlus.reflection.bindings,
        "BaseColorTexture",
        arp::shader::BindingType::SampledTexture));
    assert(hasBinding(sceneForwardPlus.reflection.bindings,
        "NormalTexture",
        arp::shader::BindingType::SampledTexture));
    assert(hasBinding(sceneForwardPlus.reflection.bindings,
        "MetallicRoughnessTexture",
        arp::shader::BindingType::SampledTexture));
    assert(hasBinding(sceneForwardPlus.reflection.bindings,
        "OcclusionTexture",
        arp::shader::BindingType::SampledTexture));
    assert(hasBinding(sceneForwardPlus.reflection.bindings,
        "EmissiveTexture",
        arp::shader::BindingType::SampledTexture));
    assert(hasBinding(sceneForwardPlus.reflection.bindings,
        "ClearcoatTexture",
        arp::shader::BindingType::SampledTexture));
    assert(hasBinding(sceneForwardPlus.reflection.bindings,
        "ClearcoatRoughnessTexture",
        arp::shader::BindingType::SampledTexture));
    assert(hasBinding(sceneForwardPlus.reflection.bindings,
        "ClearcoatNormalTexture",
        arp::shader::BindingType::SampledTexture));
    assert(hasBinding(sceneForwardPlus.reflection.bindings,
        "SpecularTexture",
        arp::shader::BindingType::SampledTexture));
    assert(hasBinding(sceneForwardPlus.reflection.bindings,
        "SpecularColorTexture",
        arp::shader::BindingType::SampledTexture));
    assert(hasBinding(sceneForwardPlus.reflection.bindings,
        "SheenColorTexture",
        arp::shader::BindingType::SampledTexture));
    assert(hasBinding(sceneForwardPlus.reflection.bindings,
        "SheenRoughnessTexture",
        arp::shader::BindingType::SampledTexture));
    assert(hasBinding(sceneForwardPlus.reflection.bindings,
        "TransmissionTexture",
        arp::shader::BindingType::SampledTexture));
    assert(hasBinding(sceneForwardPlus.reflection.bindings,
        "ThicknessTexture",
        arp::shader::BindingType::SampledTexture));
    assert(hasBinding(sceneForwardPlus.reflection.bindings,
        "IridescenceTexture",
        arp::shader::BindingType::SampledTexture));
    assert(hasBinding(sceneForwardPlus.reflection.bindings,
        "IridescenceThicknessTexture",
        arp::shader::BindingType::SampledTexture));
    assert(hasBinding(sceneForwardPlus.reflection.bindings,
        "IlmTexture",
        arp::shader::BindingType::SampledTexture));
    assert(hasBinding(sceneForwardPlus.reflection.bindings,
        "LightingRampTexture",
        arp::shader::BindingType::SampledTexture));
    assert(hasBinding(sceneForwardPlus.reflection.bindings,
        "FaceSdfTexture",
        arp::shader::BindingType::SampledTexture));
    assert(hasBinding(sceneForwardPlus.reflection.bindings,
        "MatcapTexture",
        arp::shader::BindingType::SampledTexture));
    assert(hasBinding(sceneForwardPlus.reflection.bindings,
        "FlowTexture",
        arp::shader::BindingType::SampledTexture));
    assert(hasBinding(sceneForwardPlus.reflection.bindings,
        "ScreenTexture",
        arp::shader::BindingType::SampledTexture));
    assert(hasBinding(sceneForwardPlus.reflection.bindings,
        "FurCoverageTexture",
        arp::shader::BindingType::SampledTexture));
    assert(hasBinding(sceneForwardPlus.reflection.bindings,
        "FurNoiseTexture",
        arp::shader::BindingType::SampledTexture));
    assert(hasBinding(
        sceneForwardPlus.reflection.bindings, "LinearSampler", arp::shader::BindingType::Sampler));
    assert(hasBinding(sceneForwardPlus.reflection.bindings,
        "SceneShadowParams",
        arp::shader::BindingType::UniformBuffer));
    assert(hasBinding(sceneForwardPlus.reflection.bindings,
        "GiIrradianceInput",
        arp::shader::BindingType::SampledTexture));
    assert(hasBinding(sceneForwardPlus.reflection.bindings,
        "GiIrradianceSampler",
        arp::shader::BindingType::Sampler));
    assert(bindingNumber(sceneForwardPlus.reflection.bindings, "GiIrradianceInput") == 7);
    assert(bindingNumber(sceneForwardPlus.reflection.bindings, "GiIrradianceSampler") == 8);
    assert(bindingNumber(sceneForwardPlus.reflection.bindings, "ClearcoatTexture") == 6);
    assert(bindingNumber(sceneForwardPlus.reflection.bindings, "ClearcoatNormalTexture") == 16);
    assert(bindingNumber(sceneForwardPlus.reflection.bindings, "IlmTexture") == 17);
    assert(bindingNumber(sceneForwardPlus.reflection.bindings, "FurNoiseTexture") == 24);
    assert(bindingSet(sceneForwardPlus.reflection.bindings, "BaseColorTexture") == 1);
    assert(bindingSet(sceneForwardPlus.reflection.bindings, "SceneIrradianceCube") == 2);
    assert(bindingSet(sceneForwardPlus.reflection.bindings, "SceneShadowAtlas") == 2);
    assert(bindingNumber(sceneForwardPlus.reflection.bindings, "SceneShadowAtlas") == 4);
    assert(bindingSet(sceneForwardPlus.reflection.bindings, "GiIrradianceInput") == 2);
    assert(bindingNumber(sceneForwardPlus.reflection.bindings, "GiIrradianceInput") == 7);
    assert(sceneForwardPlus.reflection.pushConstantSizeBytes == 256);
    constexpr uint32_t spirvDecorationRowMajor = 4u;
    constexpr uint32_t spirvDecorationColMajor = 5u;
    const auto stylizedSceneSpirv = readBinaryFile("shaders/stylized_scene.spv");
    assert(!stylizedSceneSpirv.empty());
    constexpr uint32_t spirvDecorationMatrixStride = 7u;
    const auto stylizedSceneSpirvWords = spirvWords(stylizedSceneSpirv);
    const auto sceneShadowParamsStruct =
        spirvNamedId(stylizedSceneSpirvWords, "SceneShadowParams_std140");
    assert(sceneShadowParamsStruct != 0u);
    assert(spirvMemberIndex(
               stylizedSceneSpirvWords, sceneShadowParamsStruct, "shadowLightViewProjectionRows")
        != UINT32_MAX);
    assert(!spirvMemberHasDecoration(stylizedSceneSpirv,
        "SceneShadowParams_std140",
        "shadowLightViewProjectionRows",
        spirvDecorationColMajor));
    assert(!spirvMemberHasDecoration(stylizedSceneSpirv,
        "SceneShadowParams_std140",
        "shadowLightViewProjectionRows",
        spirvDecorationRowMajor));
    assert(!spirvMemberHasDecoration(stylizedSceneSpirv,
        "SceneShadowParams_std140",
        "shadowLightViewProjectionRows",
        spirvDecorationMatrixStride));

    const auto renderSystemSource = readTextFile("src/platform/render_system_vulkan.cpp");
    assert(renderSystemSource.find("arp::samples::stylized::StylizedSceneRenderer")
        == std::pmr::string::npos);
    assert(renderSystemSource.find("renderer::GuiRenderer") != std::pmr::string::npos);
    assert(renderSystemSource.find("renderer::FxaaRenderer") == std::pmr::string::npos);
    assert(renderSystemSource.find("siblingFxaaShaderPath") == std::pmr::string::npos);
    assert(renderSystemSource.find("ensureSceneColorTarget") == std::pmr::string::npos);
    assert(renderSystemSource.find("buildStylizedSceneDrawPlan") == std::pmr::string::npos);
    assert(renderSystemSource.find("buildGuiDrawPlan") == std::pmr::string::npos);
    const auto stylizedPipelineSource =
        readTextFile("samples/stylized_showcase/stylized_render_pipeline.cpp");
    assert(stylizedPipelineSource.find("arp::samples::stylized::StylizedSceneRenderer")
        != std::pmr::string::npos);
    assert(stylizedPipelineSource.find("renderer::FxaaRenderer") != std::pmr::string::npos);
    const auto rhiRendererFacadeSource = readTextFile("src/renderer/renderers.h");
    const auto guiRendererSource = readTextFile("src/renderer/ui/gui_renderer.h");
    const auto fxaaRendererSource = readTextFile("src/renderer/postprocess/fxaa_renderer.h");
    assert(rhiRendererFacadeSource.find("buildStylizedSceneDrawPlan") == std::pmr::string::npos);
    assert(guiRendererSource.find("buildGuiDrawPlan") != std::pmr::string::npos);
    assert(fxaaRendererSource.find("recordFxaaPass") != std::pmr::string::npos);
    const auto stylizedRendererSource = readTextFile("samples/stylized/stylized_scene_renderer.h");
    assert(stylizedRendererSource.find("buildStylizedSceneDrawPlan") != std::pmr::string::npos);
    assert(stylizedRendererSource.find("recordStylizedSceneDrawPlan") != std::pmr::string::npos);
    const auto guiDrawPlanSource = readTextFile("src/renderer/gui/gui_draw_plan.h");
    assert(guiDrawPlanSource.find("quad.type == gui::NodeType::Label ? 0.0f")
        != std::pmr::string::npos);

    const auto characterShaderSource = readTextFile("shaders/stylized/character_npr.slang");
    assert(characterShaderSource.find("ForwardPlusLightRecord") != std::pmr::string::npos);
    assert(characterShaderSource.find("ClusterLightList") != std::pmr::string::npos);
    assert(characterShaderSource.find("FaceSdfShadowParams") != std::pmr::string::npos);
    assert(characterShaderSource.find("applyFaceSdfShadow") != std::pmr::string::npos);
    auto characterForwardPlus = compiler.compile(arp::shader::ShaderCompileRequest{
        "stylized/character_npr.slang",
        characterShaderSource,
        "CharacterNpr",
        arp::shader::TargetBackend::SpirV,
        {{"vertexMain", arp::shader::ShaderStage::Vertex},
            {"fragmentMain", arp::shader::ShaderStage::Fragment}},
    });
    assert(characterForwardPlus.success);
    assert(hasBinding(characterForwardPlus.reflection.bindings,
        "ClusterLightList",
        arp::shader::BindingType::StorageBuffer));
    assert(characterForwardPlus.reflection.pushConstantSizeBytes == 64);

    const auto shadowDepthSource = readTextFile("shaders/stylized/shadow_depth.slang");
    assert(!shadowDepthSource.empty());
    assert(shadowDepthSource.find("struct ShadowDepthPushConstants") != std::pmr::string::npos);
    // lightViewProjection must use the DEFAULT float4x4 layout (no row_major).
    // Slang's default emits a SPIR-V RowMajor decoration, which reads the
    // row-major float4x4 produced by renderer::computeShadowLightViewProjection()
    // (core::math stores m[row*4+col]) without transposition. This must match the
    // PBR receiver's lightViewProjection declaration in pbr_scene.slang
    // (also default) so caster and receiver share the same clip space —
    // otherwise the shadow map is written at texel X but sampled at X^T and
    // receivers show no shadow. Asserting the default (no row_major) guards
    // against a regression to the old row_major declaration.
    assert(
        shadowDepthSource.find("row_major float4x4 lightViewProjection") == std::pmr::string::npos);
    assert(shadowDepthSource.find("    float4x4 lightViewProjection;") != std::pmr::string::npos);
    assert(shadowDepthSource.find("float4 depthBiasAndFlags") != std::pmr::string::npos);
    assert(shadowDepthSource.find("[[vk::push_constant]]") != std::pmr::string::npos);
    assert(shadowDepthSource.find("struct ShadowSkinningMatrix") != std::pmr::string::npos);
    assert(shadowDepthSource.find("float4 row0") != std::pmr::string::npos);
    assert(
        shadowDepthSource.find(
            "[[vk::binding(0, 0)]]\nStructuredBuffer<ShadowSkinningMatrix> ShadowSkinningMatrices")
        != std::pmr::string::npos);
    assert(shadowDepthSource.find("struct VertexInput") != std::pmr::string::npos);
    assert(shadowDepthSource.find("float3 position : POSITION0") != std::pmr::string::npos);
    assert(shadowDepthSource.find("float3 normal : NORMAL0") != std::pmr::string::npos);
    assert(shadowDepthSource.find("float2 uv : TEXCOORD0") != std::pmr::string::npos);
    assert(shadowDepthSource.find("float4 tangent : TANGENT0") != std::pmr::string::npos);
    assert(shadowDepthSource.find("struct SkinnedVertexInput") != std::pmr::string::npos);
    assert(shadowDepthSource.find("float4 joints : BLENDINDICES0") != std::pmr::string::npos);
    assert(shadowDepthSource.find("float4 weights : BLENDWEIGHT0") != std::pmr::string::npos);
    assert(
        shadowDepthSource.find(
            "float3 transformSkinningPosition(ShadowSkinningMatrix skinningMatrix, float4 localPosition)")
        != std::pmr::string::npos);
    assert(shadowDepthSource.find("dot(skinningMatrix.row0, localPosition)")
        != std::pmr::string::npos);
    assert(shadowDepthSource.find("float3 skinPosition") != std::pmr::string::npos);
    assert(shadowDepthSource.find("VertexOutput vertexMain(VertexInput input)")
        != std::pmr::string::npos);
    assert(shadowDepthSource.find("VertexOutput skinnedVertexMain(SkinnedVertexInput input)")
        != std::pmr::string::npos);
    assert(
        shadowDepthSource.find("void fragmentMain(VertexOutput input)") != std::pmr::string::npos);
    assert(shadowDepthSource.find("SV_Target") == std::pmr::string::npos);
    assert(shadowDepthSource.find("float4 fragmentMain") == std::pmr::string::npos);
    assert(shadowDepthSource.find("return float4") == std::pmr::string::npos);
    auto shadowDepthReflection = compiler.compile(arp::shader::ShaderCompileRequest{
        "stylized/shadow_depth.slang",
        shadowDepthSource,
        "ShadowDepth",
        arp::shader::TargetBackend::SpirV,
        {{"vertexMain", arp::shader::ShaderStage::Vertex},
            {"skinnedVertexMain", arp::shader::ShaderStage::Vertex},
            {"fragmentMain", arp::shader::ShaderStage::Fragment}},
    });
    assert(shadowDepthReflection.success);
    assert(hasBinding(shadowDepthReflection.reflection.bindings,
        "ShadowSkinningMatrices",
        arp::shader::BindingType::StorageBuffer));
    assert(bindingNumber(shadowDepthReflection.reflection.bindings, "ShadowSkinningMatrices") == 0);
    assert(bindingSet(shadowDepthReflection.reflection.bindings, "ShadowSkinningMatrices") == 0);
    assert(shadowDepthReflection.reflection.pushConstantSizeBytes == 80);
    const auto shadowDepthSpirv = readBinaryFile("shaders/shadow_depth.spv");
    assert(!shadowDepthSpirv.empty());
    // SPV DECORATION ASSERTION (AGENTS.md section 5, mandatory): a default
    // float4x4 (no row_major keyword) in Slang emits an EXPLICIT SPIR-V
    // RowMajor decoration (id 4), which reads the row-major bytes produced by
    // renderer::computeShadowLightViewProjection() (core::math stores
    // m[row*4+col]) without transposition. The caster (shadow_depth) and
    // receiver (pbr_scene.slang lightViewProjection, also default) must share
    // this exact convention so both project into the same light clip space.
    //
    // Asserting RowMajor-PRESENT (not merely ColMajor-absent) makes the
    // contract deterministic: for push-constant/UBO members, a MISSING matrix
    // decoration defaults to ColMajor (re-transposing row-major C++ data),
    // which is the exact bug this contract must catch. Verified against slangc
    // shipped in Vulkan SDK 1.4.350.0 and 1.4.341.1 (both emit explicit
    // RowMajor for default float4x4 push-constant members).
    assert(spirvMemberHasDecoration(shadowDepthSpirv,
        "ShadowDepthPushConstants_std430",
        "lightViewProjection",
        spirvDecorationColMajor));
    assert(!spirvMemberHasDecoration(shadowDepthSpirv,
        "ShadowDepthPushConstants_std430",
        "lightViewProjection",
        spirvDecorationRowMajor));
    assert(shadowDepthReflection.reflection.vertexAttributes.size() == 4);
    assert(shadowDepthReflection.reflection.vertexAttributes[0].semantic
        == std::pmr::string("POSITION0"));
    assert(shadowDepthReflection.reflection.vertexAttributes[3].semantic
        == std::pmr::string("TANGENT0"));
    auto shadowDepthMsl = compiler.compile(arp::shader::ShaderCompileRequest{
        "stylized/shadow_depth.slang",
        shadowDepthSource,
        "ShadowDepth",
        arp::shader::TargetBackend::Msl,
        {{"vertexMain", arp::shader::ShaderStage::Vertex},
            {"skinnedVertexMain", arp::shader::ShaderStage::Vertex},
            {"fragmentMain", arp::shader::ShaderStage::Fragment}},
    });
    assert(shadowDepthMsl.success);
    assert(fileExists("shaders/shadow_depth.spv"));
    assert(fileExists("shaders/shadow_depth.metal"));
    assert(fileExists("shaders/shadow_depth.reflection.yaml"));

    const auto giShaderSource = readTextFile("shaders/stylized/global_illumination.slang");
    assert(giShaderSource.find("GiProbeRecord") != std::pmr::string::npos);
    assert(giShaderSource.find("GiProbeUpdate") != std::pmr::string::npos);
    assert(giShaderSource.find("GiIrradianceResolve") != std::pmr::string::npos);
    assert(giShaderSource.find("float probePhase = frac(float(index) * 0.6180339)")
        != std::pmr::string::npos);
    assert(giShaderSource.find(
               "uint probeIndex = (dispatchThreadId.x + dispatchThreadId.y * width) % probeCount")
        != std::pmr::string::npos);
    assert(giShaderSource.find("GiProbeState[probeIndex].irradiance") != std::pmr::string::npos);
    assert(giShaderSource.find("RWTexture2D<float4> GiIrradianceOutput") != std::pmr::string::npos);
    assert(giShaderSource.find(
               "GiIrradianceOutput[dispatchThreadId.xy] = float4(resolveIrradiance, 1.0)")
        != std::pmr::string::npos);
    assert(giShaderSource.find(
               "GiIrradianceOutput[dispatchThreadId.xy] = float4(0.22, 0.26, 0.34, 1.0)")
        == std::pmr::string::npos);
    auto giReflection = compiler.compile(arp::shader::ShaderCompileRequest{
        "stylized/global_illumination.slang",
        giShaderSource,
        "GlobalIllumination",
        arp::shader::TargetBackend::SpirV,
        {{"GiProbeUpdate", arp::shader::ShaderStage::Compute},
            {"GiIrradianceResolve", arp::shader::ShaderStage::Compute}},
    });
    assert(giReflection.success);
    assert(hasBinding(
        giReflection.reflection.bindings, "GiProbeState", arp::shader::BindingType::StorageBuffer));
    assert(hasBinding(giReflection.reflection.bindings,
        "GiIrradianceOutput",
        arp::shader::BindingType::StorageTexture));
    assert(bindingNumber(giReflection.reflection.bindings, "GiProbeState") == 0);
    assert(bindingNumber(giReflection.reflection.bindings, "GiIrradianceOutput") == 1);

    const auto guiShaderSource = readTextFile("shaders/gui/overlay.slang");
    assert(
        guiShaderSource.find("pixel.y / gGui.screenSize.y * 2.0 - 1.0") != std::pmr::string::npos);
    assert(guiShaderSource.find("128.0 / 255.0") != std::pmr::string::npos);
    assert(guiShaderSource.find("length(float2(ddx(distance), ddy(distance)))")
        != std::pmr::string::npos);
    assert(guiShaderSource.find("0.5 * max(pixelDistance, 1.0 / 255.0)") != std::pmr::string::npos);
    assert(guiShaderSource.find("smoothstep(0.45, 0.55, distance)") == std::pmr::string::npos);

    const auto particleShaderSource = readTextFile("shaders/stylized/particles.slang");
    assert(particleShaderSource.find("struct ParticlePushConstants") != std::pmr::string::npos);
    assert(particleShaderSource.find("float3 worldPosition : POSITION0") != std::pmr::string::npos);
    assert(particleShaderSource.find("float2 corner : TEXCOORD0") != std::pmr::string::npos);
    assert(particleShaderSource.find("float4 color : COLOR0") != std::pmr::string::npos);
    assert(particleShaderSource.find("mul(gParticles.viewProjection") != std::pmr::string::npos);
    assert(
        particleShaderSource.find("ParticleVisualFamilySpriteAlpha = 1") != std::pmr::string::npos);
    assert(particleShaderSource.find("ParticleVisualFamilySpriteAdditive = 2")
        != std::pmr::string::npos);
    assert(particleShaderSource.find("ParticleVisualFamilyLightningThin = 3")
        != std::pmr::string::npos);
    assert(particleShaderSource.find("gParticles.visualFamily == ParticleVisualFamilyLightningThin")
        != std::pmr::string::npos);
    assert(particleShaderSource.find("electricFilament") != std::pmr::string::npos);
    assert(particleShaderSource.find("electricCore") != std::pmr::string::npos);
    assert(particleShaderSource.find("electricHalo") != std::pmr::string::npos);
    assert(particleShaderSource.find("branchFlash") != std::pmr::string::npos);
    assert(particleShaderSource.find("tintEnergy") != std::pmr::string::npos);
    assert(particleShaderSource.find("electricPulse") != std::pmr::string::npos);
    assert(particleShaderSource.find("baseSparks") != std::pmr::string::npos);
    assert(particleShaderSource.find("stormCenter") != std::pmr::string::npos);
    assert(particleShaderSource.find("stormSlope") != std::pmr::string::npos);
    assert(particleShaderSource.find("flipbookExplosion") != std::pmr::string::npos);
    assert(particleShaderSource.find("softCore") != std::pmr::string::npos);
    assert(particleShaderSource.find("spokes") != std::pmr::string::npos);
    assert(particleShaderSource.find("shockRing") != std::pmr::string::npos);
    assert(particleShaderSource.find("widthScale") != std::pmr::string::npos);
    assert(particleShaderSource.find("heightScale") != std::pmr::string::npos);
    assert(particleShaderSource.find("electricMask") == std::pmr::string::npos);
    auto particleReflection = compiler.compile(arp::shader::ShaderCompileRequest{
        "stylized/particles.slang",
        particleShaderSource,
        "Particles",
        arp::shader::TargetBackend::SpirV,
        {{"vertexMain", arp::shader::ShaderStage::Vertex},
            {"fragmentMain", arp::shader::ShaderStage::Fragment}},
    });
    assert(particleReflection.success);
    // push-constant size: float4x4(64) + 6*uint(24) + 3*float(12) = 100.
    // struct body has no inline // comments (moved above struct) so the
    // reflectedPushConstantSize parser computes the exact size.
    assert(particleReflection.reflection.pushConstantSizeBytes == 100);
    assert(particleReflection.reflection.vertexAttributes.size() == 4);
    assert(particleReflection.reflection.vertexAttributes[0].semantic
        == std::pmr::string("POSITION0"));
    assert(
        particleReflection.reflection.vertexAttributes[2].semantic == std::pmr::string("COLOR0"));

    const auto fxaaShaderSource = readTextFile("shaders/stylized/fxaa.slang");
    assert(fxaaShaderSource.find("Texture2D<float4> SceneColorTexture") != std::pmr::string::npos);
    assert(fxaaShaderSource.find("SamplerState SceneColorSampler") != std::pmr::string::npos);
    assert(fxaaShaderSource.find("struct FxaaPushConstants") != std::pmr::string::npos);
    assert(fxaaShaderSource.find("float2 inputUvScaleBias") != std::pmr::string::npos);
    assert(fxaaShaderSource.find("sampleSceneColor") != std::pmr::string::npos);
    assert(fxaaShaderSource.find("edgeThreshold") != std::pmr::string::npos);
    assert(fxaaShaderSource.find("vertexMain") != std::pmr::string::npos);
    assert(fxaaShaderSource.find("fragmentMain") != std::pmr::string::npos);
    assert(fxaaShaderSource.find("SceneColorTexture.SampleLevel") != std::pmr::string::npos);
    auto fxaaReflection = compiler.compile(arp::shader::ShaderCompileRequest{
        "stylized/fxaa.slang",
        fxaaShaderSource,
        "Fxaa",
        arp::shader::TargetBackend::SpirV,
        {{"vertexMain", arp::shader::ShaderStage::Vertex},
            {"fragmentMain", arp::shader::ShaderStage::Fragment}},
    });
    assert(fxaaReflection.success);
    assert(hasBinding(fxaaReflection.reflection.bindings,
        "SceneColorTexture",
        arp::shader::BindingType::SampledTexture));
    assert(hasBinding(fxaaReflection.reflection.bindings,
        "SceneColorSampler",
        arp::shader::BindingType::Sampler));
    assert(bindingNumber(fxaaReflection.reflection.bindings, "SceneColorTexture") == 0);
    assert(bindingNumber(fxaaReflection.reflection.bindings, "SceneColorSampler") == 1);
    assert(bindingSet(fxaaReflection.reflection.bindings, "SceneColorTexture") == 1);
    assert(bindingSet(fxaaReflection.reflection.bindings, "SceneColorSampler") == 1);
    assert(fxaaReflection.reflection.pushConstantSizeBytes == 24);
    assert(fxaaReflection.reflection.vertexAttributes.empty());

    const auto ssaoShaderSource = readTextFile("shaders/wocc/ssao.slang");
    assert(ssaoShaderSource.find("struct SsaoPushConstants") != std::pmr::string::npos);
    assert(ssaoShaderSource.find("ARP_SAMPLED_DEPTH(SceneDepthTexture)") != std::pmr::string::npos);
    assert(ssaoShaderSource.find("SceneColorTexture") == std::pmr::string::npos);
    assert(ssaoShaderSource.find("SceneColorSampler") == std::pmr::string::npos);
    assert(ssaoShaderSource.find("float2 inputUvScaleBias") != std::pmr::string::npos);
    assert(ssaoShaderSource.find("sampleSceneDepth") != std::pmr::string::npos);
    assert(ssaoShaderSource.find("computeOcclusion") != std::pmr::string::npos);
    assert(ssaoShaderSource.find("float depthDelta = centerViewZ - sampleViewZ;")
        != std::pmr::string::npos);
    assert(ssaoShaderSource.find("if (sampleDepth >= 0.9999)") != std::pmr::string::npos);
    assert(ssaoShaderSource.find("return 1.0 - occlusion;") != std::pmr::string::npos);
    assert(ssaoShaderSource.find("vertexMain") != std::pmr::string::npos);
    assert(ssaoShaderSource.find("fragmentMain") != std::pmr::string::npos);
    auto ssaoReflection = compiler.compile(arp::shader::ShaderCompileRequest{
        "wocc/ssao.slang",
        ssaoShaderSource,
        "Ssao",
        arp::shader::TargetBackend::SpirV,
        {{"vertexMain", arp::shader::ShaderStage::Vertex},
            {"fragmentMain", arp::shader::ShaderStage::Fragment}},
    });
    assert(ssaoReflection.success);
    assert(hasBinding(ssaoReflection.reflection.bindings,
        "SceneDepthTexture",
        arp::shader::BindingType::SampledTexture));
    assert(!hasBinding(ssaoReflection.reflection.bindings,
        "SceneColorTexture",
        arp::shader::BindingType::SampledTexture));
    assert(!hasBinding(ssaoReflection.reflection.bindings,
        "SceneColorSampler",
        arp::shader::BindingType::Sampler));
    assert(bindingNumber(ssaoReflection.reflection.bindings, "SceneDepthTexture") == 0);
    assert(bindingSet(ssaoReflection.reflection.bindings, "SceneDepthTexture") == 1);
    assert(ssaoReflection.reflection.pushConstantSizeBytes == 40);
    assert(ssaoReflection.reflection.vertexAttributes.empty());

    const auto postprocessShaderSource = readTextFile("shaders/stylized/postprocess.slang");
    assert(postprocessShaderSource.find("struct PostProcessParams") != std::pmr::string::npos);
    assert(postprocessShaderSource.find(
               "[[vk::binding(0, 0)]] Texture2D<float4> PostProcessInputTexture")
        != std::pmr::string::npos);
    assert(
        postprocessShaderSource.find("[[vk::binding(1, 0)]] SamplerState PostProcessInputSampler")
        != std::pmr::string::npos);
    assert(postprocessShaderSource.find("[[vk::binding(2, 0)]] Texture2D<float4> BloomTexture")
        != std::pmr::string::npos);
    assert(postprocessShaderSource.find("[[vk::binding(3, 0)]] Texture2D<float4> LutTexture")
        != std::pmr::string::npos);
    assert(postprocessShaderSource.find("float3 applyAcesToneMapping(float3 color)")
        != std::pmr::string::npos);
    assert(
        postprocessShaderSource.find("float4 toneMappingFragmentMain") != std::pmr::string::npos);
    assert(postprocessShaderSource.find("float4 bloomDownsampleFragmentMain")
        != std::pmr::string::npos);
    assert(postprocessShaderSource.find("float4 bloomCompositeFragmentMain")
        != std::pmr::string::npos);
    assert(postprocessShaderSource.find("float4 lutFragmentMain") != std::pmr::string::npos);
    assert(postprocessShaderSource.find("float4 heightFogFragmentMain") != std::pmr::string::npos);
    assert(postprocessShaderSource.find("float4 sharpenFragmentMain") != std::pmr::string::npos);
    auto postprocessReflection = compiler.compile(arp::shader::ShaderCompileRequest{
        "stylized/postprocess.slang",
        postprocessShaderSource,
        "PostProcess",
        arp::shader::TargetBackend::SpirV,
        {{"vertexMain", arp::shader::ShaderStage::Vertex},
            {"toneMappingFragmentMain", arp::shader::ShaderStage::Fragment},
            {"bloomDownsampleFragmentMain", arp::shader::ShaderStage::Fragment},
            {"bloomCompositeFragmentMain", arp::shader::ShaderStage::Fragment},
            {"lutFragmentMain", arp::shader::ShaderStage::Fragment},
            {"heightFogFragmentMain", arp::shader::ShaderStage::Fragment},
            {"sharpenFragmentMain", arp::shader::ShaderStage::Fragment}},
    });
    assert(postprocessReflection.success);
    assert(hasBinding(postprocessReflection.reflection.bindings,
        "PostProcessInputTexture",
        arp::shader::BindingType::SampledTexture));
    assert(hasBinding(postprocessReflection.reflection.bindings,
        "PostProcessInputSampler",
        arp::shader::BindingType::Sampler));
    assert(hasBinding(postprocessReflection.reflection.bindings,
        "BloomTexture",
        arp::shader::BindingType::SampledTexture));
    assert(hasBinding(postprocessReflection.reflection.bindings,
        "LutTexture",
        arp::shader::BindingType::SampledTexture));
    assert(
        bindingNumber(postprocessReflection.reflection.bindings, "PostProcessInputTexture") == 0);
    assert(
        bindingNumber(postprocessReflection.reflection.bindings, "PostProcessInputSampler") == 1);
    assert(bindingNumber(postprocessReflection.reflection.bindings, "BloomTexture") == 2);
    assert(bindingNumber(postprocessReflection.reflection.bindings, "LutTexture") == 3);
    assert(postprocessReflection.reflection.pushConstantSizeBytes == 80);
    assert(postprocessReflection.reflection.vertexAttributes.empty());

    const auto startupSplashShaderSource = readTextFile("shaders/samples/startup_splash.slang");
    assert(startupSplashShaderSource.find("struct StartupSplashPushConstants")
        != std::pmr::string::npos);
    assert(startupSplashShaderSource.find("[[vk::push_constant]]") != std::pmr::string::npos);
    assert(startupSplashShaderSource.find("StartupSplashPushConstants gStartupSplash")
        != std::pmr::string::npos);
    assert(startupSplashShaderSource.find(
               "[[vk::binding(0, 1)]] Texture2D<float4> StartupSplashLogoTexture")
        != std::pmr::string::npos);
    assert(startupSplashShaderSource.find(
               "[[vk::binding(1, 1)]] SamplerState StartupSplashLogoSampler")
        != std::pmr::string::npos);
    assert(startupSplashShaderSource.find("float sweepCoordinate") != std::pmr::string::npos);
    assert(startupSplashShaderSource.find("StartupSplashLogoTexture.SampleLevel")
        != std::pmr::string::npos);
    assert(startupSplashShaderSource.find("logoAlpha") != std::pmr::string::npos);
    auto startupSplashReflection = compiler.compile(arp::shader::ShaderCompileRequest{
        "samples/startup_splash.slang",
        startupSplashShaderSource,
        "StartupSplash",
        arp::shader::TargetBackend::SpirV,
        {{"vertexMain", arp::shader::ShaderStage::Vertex},
            {"fragmentMain", arp::shader::ShaderStage::Fragment}},
    });
    assert(startupSplashReflection.success);
    assert(hasBinding(startupSplashReflection.reflection.bindings,
        "StartupSplashLogoTexture",
        arp::shader::BindingType::SampledTexture));
    assert(hasBinding(startupSplashReflection.reflection.bindings,
        "StartupSplashLogoSampler",
        arp::shader::BindingType::Sampler));
    assert(bindingNumber(startupSplashReflection.reflection.bindings, "StartupSplashLogoTexture")
        == 0);
    assert(bindingNumber(startupSplashReflection.reflection.bindings, "StartupSplashLogoSampler")
        == 1);
    assert(
        bindingSet(startupSplashReflection.reflection.bindings, "StartupSplashLogoTexture") == 1);
    assert(
        bindingSet(startupSplashReflection.reflection.bindings, "StartupSplashLogoSampler") == 1);
    assert(startupSplashReflection.reflection.pushConstantSizeBytes == 48);
    assert(startupSplashReflection.reflection.vertexAttributes.empty());

    const auto shaderCompileScript = readTextFile("scripts/compile-shaders.ps1");
    assert(shaderCompileScript.find("shadow_depth.slang") != std::pmr::string::npos);
    assert(shaderCompileScript.find("shadow_depth.spv") != std::pmr::string::npos);
    assert(shaderCompileScript.find("shadow_depth.metal") != std::pmr::string::npos);
    assert(shaderCompileScript.find("shadow_depth.wgsl") != std::pmr::string::npos);
    assert(shaderCompileScript.find("shadow_depth.reflection.yaml") != std::pmr::string::npos);
    assert(shaderCompileScript.find("Write-ShaderReflection $shadowDepthSource \"ShadowDepth\"")
        != std::pmr::string::npos);
    assert(shaderCompileScript.find("fxaa.slang") != std::pmr::string::npos);
    assert(shaderCompileScript.find("fxaa.spv") != std::pmr::string::npos);
    assert(shaderCompileScript.find("fxaa.metal") != std::pmr::string::npos);
    assert(shaderCompileScript.find("fxaa.wgsl") != std::pmr::string::npos);
    assert(shaderCompileScript.find("fxaa.reflection.yaml") != std::pmr::string::npos);
    assert(shaderCompileScript.find("ssao.slang") != std::pmr::string::npos);
    assert(shaderCompileScript.find("ssao.spv") != std::pmr::string::npos);
    assert(shaderCompileScript.find("ssao.metal") != std::pmr::string::npos);
    assert(shaderCompileScript.find("ssao.wgsl") != std::pmr::string::npos);
    assert(shaderCompileScript.find("ssao.reflection.yaml") != std::pmr::string::npos);
    assert(shaderCompileScript.find("postprocess.slang") != std::pmr::string::npos);
    assert(shaderCompileScript.find("postprocess.spv") != std::pmr::string::npos);
    assert(shaderCompileScript.find("postprocess.metal") != std::pmr::string::npos);
    assert(shaderCompileScript.find("postprocess.wgsl") != std::pmr::string::npos);
    assert(shaderCompileScript.find("postprocess.reflection.yaml") != std::pmr::string::npos);
    assert(shaderCompileScript.find("startup_splash.slang") != std::pmr::string::npos);
    assert(shaderCompileScript.find("startup_splash.spv") != std::pmr::string::npos);
    assert(shaderCompileScript.find("startup_splash.metal") != std::pmr::string::npos);
    assert(shaderCompileScript.find("startup_splash.wgsl") != std::pmr::string::npos);
    assert(shaderCompileScript.find("startup_splash.reflection.yaml") != std::pmr::string::npos);
    assert(shaderCompileScript.find("webgpu_compute_smoke.slang") != std::pmr::string::npos);
    assert(shaderCompileScript.find("webgpu_compute_smoke.spv") != std::pmr::string::npos);
    assert(shaderCompileScript.find("webgpu_compute_smoke.metal") != std::pmr::string::npos);
    assert(shaderCompileScript.find("webgpu_compute_smoke.wgsl") != std::pmr::string::npos);
    assert(
        shaderCompileScript.find("webgpu_compute_smoke.reflection.yaml") != std::pmr::string::npos);

    const auto webGpuComputeSmokeSource =
        readTextFile("shaders/samples/webgpu_compute_smoke.slang");
    assert(webGpuComputeSmokeSource.find("RWTexture2D<float4> ComputeSmokeOutput")
        != std::pmr::string::npos);
    assert(webGpuComputeSmokeSource.find("computeSmokeMain") != std::pmr::string::npos);
    assert(webGpuComputeSmokeSource.find("computeSmokeVertexMain") != std::pmr::string::npos);
    assert(webGpuComputeSmokeSource.find("computeSmokeFragmentMain") != std::pmr::string::npos);
    auto webGpuComputeSmokeReflection = compiler.compile(arp::shader::ShaderCompileRequest{
        "samples/webgpu_compute_smoke.slang",
        webGpuComputeSmokeSource,
        "WebGpuComputeSmoke",
        arp::shader::TargetBackend::Wgsl,
        {{"computeSmokeMain", arp::shader::ShaderStage::Compute},
            {"computeSmokeVertexMain", arp::shader::ShaderStage::Vertex},
            {"computeSmokeFragmentMain", arp::shader::ShaderStage::Fragment}},
    });
    assert(webGpuComputeSmokeReflection.success);
    assert(hasBinding(webGpuComputeSmokeReflection.reflection.bindings,
        "ComputeSmokeOutput",
        arp::shader::BindingType::StorageTexture));
    assert(hasBinding(webGpuComputeSmokeReflection.reflection.bindings,
        "ComputeSmokeInput",
        arp::shader::BindingType::SampledTexture));
    assert(hasBinding(webGpuComputeSmokeReflection.reflection.bindings,
        "ComputeSmokeSampler",
        arp::shader::BindingType::Sampler));

    const auto pbrPreviewShaderSource = readTextFile("shaders/material_preview/pbr_preview.slang");
    assert(
        pbrPreviewShaderSource.find("MaterialPreviewPbrPushConstants") != std::pmr::string::npos);
    assert(pbrPreviewShaderSource.find("TextureCube<float4> IrradianceCube")
        != std::pmr::string::npos);
    assert(pbrPreviewShaderSource.find("TextureCube<float4> PrefilteredSpecularCube")
        != std::pmr::string::npos);
    assert(pbrPreviewShaderSource.find("Texture2D<float4> BrdfIntegrationLut")
        != std::pmr::string::npos);
    assert(pbrPreviewShaderSource.find("sampleIblDiffuse") != std::pmr::string::npos);
    assert(pbrPreviewShaderSource.find("sampleIblSpecular") != std::pmr::string::npos);
    assert(pbrPreviewShaderSource.find("distributionGGX") != std::pmr::string::npos);
    assert(pbrPreviewShaderSource.find("geometrySmith") != std::pmr::string::npos);
    assert(pbrPreviewShaderSource.find("fresnelSchlick") != std::pmr::string::npos);
    assert(pbrPreviewShaderSource.find("applyAcesToneMapping") != std::pmr::string::npos);
    auto pbrPreviewReflection = compiler.compile(arp::shader::ShaderCompileRequest{
        "material_preview/pbr_preview.slang",
        pbrPreviewShaderSource,
        "MaterialPreviewPbr",
        arp::shader::TargetBackend::SpirV,
        {{"vertexMain", arp::shader::ShaderStage::Vertex},
            {"fragmentMain", arp::shader::ShaderStage::Fragment}},
    });
    assert(pbrPreviewReflection.success);
    assert(hasBinding(pbrPreviewReflection.reflection.bindings,
        "BaseColorTexture",
        arp::shader::BindingType::SampledTexture));
    assert(hasBinding(pbrPreviewReflection.reflection.bindings,
        "NormalTexture",
        arp::shader::BindingType::SampledTexture));
    assert(hasBinding(pbrPreviewReflection.reflection.bindings,
        "MetallicRoughnessTexture",
        arp::shader::BindingType::SampledTexture));
    assert(hasBinding(pbrPreviewReflection.reflection.bindings,
        "IrradianceCube",
        arp::shader::BindingType::SampledTexture));
    assert(hasBinding(pbrPreviewReflection.reflection.bindings,
        "PrefilteredSpecularCube",
        arp::shader::BindingType::SampledTexture));
    assert(hasBinding(pbrPreviewReflection.reflection.bindings,
        "BrdfIntegrationLut",
        arp::shader::BindingType::SampledTexture));
    assert(hasBinding(
        pbrPreviewReflection.reflection.bindings, "IblSampler", arp::shader::BindingType::Sampler));
    assert(bindingNumber(pbrPreviewReflection.reflection.bindings, "IrradianceCube") == 0);
    assert(bindingNumber(pbrPreviewReflection.reflection.bindings, "PrefilteredSpecularCube") == 1);
    assert(bindingNumber(pbrPreviewReflection.reflection.bindings, "BrdfIntegrationLut") == 2);
    assert(pbrPreviewReflection.reflection.pushConstantSizeBytes == 144);

    assert(
        shaderCompileScript.find("material_preview\\pbr_preview.slang") != std::pmr::string::npos);
    assert(shaderCompileScript.find("material_preview_pbr.spv") != std::pmr::string::npos);
    assert(shaderCompileScript.find("material_preview_pbr.metal") != std::pmr::string::npos);
    assert(shaderCompileScript.find("material_preview_pbr.wgsl") != std::pmr::string::npos);
    assert(
        shaderCompileScript.find("material_preview_pbr.reflection.yaml") != std::pmr::string::npos);
    assert(shaderCompileScript.find("Set-WgslPushConstantBinding $wgslOut 0 0")
        != std::pmr::string::npos);
    assert(shaderCompileScript.find("Set-WgslPushConstantBinding $guiWgslOut 0 0")
        != std::pmr::string::npos);
    assert(shaderCompileScript.find("Set-WgslPushConstantBinding $stylizedSceneWgslOut 0 0")
        != std::pmr::string::npos);
    assert(shaderCompileScript.find("Set-WgslPushConstantBinding $fxaaWgslOut 0 0")
        != std::pmr::string::npos);
    assert(shaderCompileScript.find("Set-WgslPushConstantBinding $ssaoWgslOut 0 0")
        != std::pmr::string::npos);
    assert(shaderCompileScript.find("Set-WgslPushConstantBinding $startupSplashWgslOut 0 0")
        != std::pmr::string::npos);
    assert(shaderCompileScript.find("Set-WgslPushConstantBinding $materialPreviewPbrWgslOut 0 0")
        != std::pmr::string::npos);
    assert(
        shaderCompileScript.find(
            "Set-WgslStorageTextureFormatAndAccess $globalIlluminationWgslOut \"GiIrradianceOutput\" \"rgba16float\" \"write\"")
        != std::pmr::string::npos);
    assert(shaderCompileScript.find("PushConstants[^\\r\\n;]*;'") != std::pmr::string::npos);
    assert(shaderCompileScript.find("Set-WgslVertexOutputLocations") != std::pmr::string::npos);
    assert(shaderCompileScript.find("@location($location)") != std::pmr::string::npos);
    assert(shaderCompileScript.find("System.Text.UTF8Encoding($false)") != std::pmr::string::npos);
    assert(
        shaderCompileScript.find("Write-Utf8NoBom $WgslPath $updated") != std::pmr::string::npos);

    // ----------------------------------------------------------------
    // WoCC Procedural Sky shader contract test
    // Validates shaders/wocc/sky.slang source structure + reflection +
    // compile-shaders.ps1 wiring. The sky shader is push-constants-only
    // (no descriptor bindings) and must produce SPIR-V, MSL, WGSL,
    // and reflection.json artifacts.
    // ----------------------------------------------------------------
    const auto woccSkySource = readTextFile("shaders/wocc/sky.slang");
    assert(!woccSkySource.empty());
    assert(woccSkySource.find("struct WoccSkyPushConstants") != std::pmr::string::npos);
    // viewProjection must use the DEFAULT float4x4 layout (no row_major),
    // identical to pbr_scene.slang's viewProjection. Slang's default emits a
    // SPIR-V RowMajor decoration, which reads the row-major VP data the C++
    // side fills (app.cpp transposes the column-major VP into row-major
    // storage) without an extra transpose. Declaring `row_major` here (SPIR-V
    // ColMajor) would re-transpose the data and project the dome through a
    // wrong clip space — the same matrix-layout mismatch class as the
    // shadow_depth bug, and the actual root cause of the sky rendering
    // incorrectly. Asserting the default (no row_major) guards against that
    // regression.
    assert(woccSkySource.find("row_major float4x4 viewProjection") == std::pmr::string::npos);
    assert(woccSkySource.find("    float4x4 viewProjection;") != std::pmr::string::npos);
    assert(woccSkySource.find("float4 cameraXzDomeRadius") != std::pmr::string::npos);
    assert(woccSkySource.find("float4 sunDirSunColorR") != std::pmr::string::npos);
    assert(woccSkySource.find("[[vk::push_constant]]") != std::pmr::string::npos);
    assert(woccSkySource.find("float3 worldDir : TEXCOORD0") != std::pmr::string::npos);
    // The dome is offset by the full camera position (XYZ) each frame so the
    // view transform V (camera_position -> origin) cancels the offset and the
    // dome is centered on the camera in view space. The previous XZ-only
    // offset (worldPos.x/z += ...) was replaced by a full-XYZ add.
    assert(woccSkySource.find("float3 worldPos = input.position + gSky.cameraXzDomeRadius.xyz")
        != std::pmr::string::npos);
    assert(
        woccSkySource.find("VertexOutput vertexMain(VertexInput input)") != std::pmr::string::npos);
    assert(woccSkySource.find("float4 fragmentMain(VertexOutput input) : SV_Target")
        != std::pmr::string::npos);
    // The sky shader now outputs HDR linear - ACES tone mapping moved to the
    // post-process chain (shaders/wocc/post.slang). The in-shader
    // applyAcesToneMapping definition + exposure multiply were removed so the
    // bloom pass sees true HDR sky values; asserting its ABSENCE guards against
    // a regression that would double-tonemap the sky.
    assert(woccSkySource.find("applyAcesToneMapping") == std::pmr::string::npos);
    // Sky shader must NOT have descriptor bindings (push-constants-only).
    assert(woccSkySource.find("[[vk::binding") == std::pmr::string::npos);
    auto woccSkyReflection = compiler.compile(arp::shader::ShaderCompileRequest{
        "wocc/sky.slang",
        woccSkySource,
        "WoccSky",
        arp::shader::TargetBackend::SpirV,
        {{"vertexMain", arp::shader::ShaderStage::Vertex},
            {"fragmentMain", arp::shader::ShaderStage::Fragment}},
    });
    assert(woccSkyReflection.success);
    // No descriptor bindings — sky uses push constants only.
    assert(woccSkyReflection.reflection.bindings.empty());
    // Push constant size: viewProjection(64) + 5 float4s(80) = 144 bytes.
    {
        std::pmr::string msg("WoccSky pushConstantSizeBytes=");
        msg += std::to_string(woccSkyReflection.reflection.pushConstantSizeBytes);
        msg += " bindings=" + std::to_string(woccSkyReflection.reflection.bindings.size());
        msg +=
            " vertexAttrs=" + std::to_string(woccSkyReflection.reflection.vertexAttributes.size());
        std::cerr << msg << '\n';
    }
    assert(woccSkyReflection.reflection.pushConstantSizeBytes == 144);
    assert(woccSkyReflection.reflection.vertexAttributes.size() == 4);
    assert(
        woccSkyReflection.reflection.vertexAttributes[0].semantic == std::pmr::string("POSITION0"));
    assert(
        woccSkyReflection.reflection.vertexAttributes[3].semantic == std::pmr::string("TANGENT0"));
    // Default float4x4 in Slang emits SPIR-V RowMajor, matching the row-major
    // VP data the C++ side fills (and matching pbr_scene.slang). See the
    // source-assertion comment above for why the sky viewProjection must NOT
    // be row_major (ColMajor would re-transpose the data).
    const auto woccSkySpirv = readBinaryFile("shaders/wocc_sky.spv");
    assert(!woccSkySpirv.empty());
    // SPV DECORATION ASSERTION (AGENTS.md section 5): default float4x4 (no
    // row_major keyword) -> explicit SPIR-V RowMajor decoration, which reads
    // the row-major VP data the C++ side fills (and matches pbr_scene.slang).
    // Asserting RowMajor-PRESENT (not merely ColMajor-absent) is deterministic:
    // a missing decoration defaults to ColMajor for push-constants and would
    // re-transpose the data. Verified against slangc in Vulkan SDK 1.4.350.0.
    assert(spirvMemberHasDecoration(
        woccSkySpirv, "WoccSkyPushConstants_std430", "viewProjection", spirvDecorationColMajor));
    assert(!spirvMemberHasDecoration(
        woccSkySpirv, "WoccSkyPushConstants_std430", "viewProjection", spirvDecorationRowMajor));
    auto woccSkyMsl = compiler.compile(arp::shader::ShaderCompileRequest{
        "wocc/sky.slang",
        woccSkySource,
        "WoccSky",
        arp::shader::TargetBackend::Msl,
        {{"vertexMain", arp::shader::ShaderStage::Vertex},
            {"fragmentMain", arp::shader::ShaderStage::Fragment}},
    });
    assert(woccSkyMsl.success);
    // Compile-shaders.ps1 must wire the sky shader for all backends + reflection.
    assert(shaderCompileScript.find("wocc\\sky.slang") != std::pmr::string::npos);
    assert(shaderCompileScript.find("wocc_sky.spv") != std::pmr::string::npos);
    assert(shaderCompileScript.find("wocc_sky.metal") != std::pmr::string::npos);
    assert(shaderCompileScript.find("wocc_sky.wgsl") != std::pmr::string::npos);
    assert(shaderCompileScript.find("wocc_sky.reflection.yaml") != std::pmr::string::npos);
    assert(shaderCompileScript.find("Write-ShaderReflection $woccSkySource \"WoccSky\"")
        != std::pmr::string::npos);
    assert(shaderCompileScript.find("Set-WgslPushConstantBinding $woccSkyWgslOut 0 0")
        != std::pmr::string::npos);
    assert(fileExists("shaders/wocc_sky.spv"));
    assert(fileExists("shaders/wocc_sky.metal"));
    assert(fileExists("shaders/wocc_sky.reflection.yaml"));

    // ----------------------------------------------------------------
    // WoCC PBR Scene shader — shadow receiver matrix-layout contract.
    // pbr_scene.slang's lightViewProjection must use the DEFAULT float4x4
    // layout (Slang -> SPIR-V RowMajor) so the receiver projects world
    // positions into the SAME light clip space the shadow_depth caster
    // writes. Caster and receiver share one renderer::computeShadowLightView-
    // Projection() matrix (row-major, m[row*4+col]); if their layout
    // declarations disagreed, one would read the transpose and the shadow
    // lookup would miss the shadow map (receivers show no shadow). This
    // test is the receiver-side pair of the shadow_depth RowMajor assertion
    // above — both must be RowMajor (default).
    // ----------------------------------------------------------------
    const auto woccPbrSceneSource = readTextFile("shaders/wocc/pbr_scene.slang");
    assert(!woccPbrSceneSource.empty());
    assert(woccPbrSceneSource.find("struct WoccPbrPushConstants") != std::pmr::string::npos);
    // Receiver lightViewProjection must NOT be row_major (must be default).
    assert(woccPbrSceneSource.find("row_major float4x4 lightViewProjection")
        == std::pmr::string::npos);
    assert(woccPbrSceneSource.find("float4x4 lightViewProjection;") != std::pmr::string::npos);
    assert(woccPbrSceneSource.find("float shadowedVisibility = 1.0 - shadowStrength;")
        != std::pmr::string::npos);
    assert(woccPbrSceneSource.find("SamplerState ShadowSampler;") != std::pmr::string::npos);
    assert(woccPbrSceneSource.find("float bias = 0.0005;") != std::pmr::string::npos);
    assert(woccPbrSceneSource.find(
               "float shadowDepth = ShadowMap.SampleLevel(ShadowSampler, uv + offset, 0.0).r;")
        != std::pmr::string::npos);
    assert(woccPbrSceneSource.find(
               "visibility += (receiver - bias) > shadowDepth ? shadowedVisibility : 1.0;")
        != std::pmr::string::npos);
    assert(woccPbrSceneSource.find("? shadowStrength : 1.0") == std::pmr::string::npos);
    assert(
        woccPbrSceneSource.find("float2 uv = projected.xy * float2(0.5, 0.5) + float2(0.5, 0.5);")
        != std::pmr::string::npos);
    assert(woccPbrSceneSource.find("float receiver = saturate(projected.z);")
        != std::pmr::string::npos);
    assert(woccPbrSceneSource.find("proj = proj * 0.5 + 0.5;") == std::pmr::string::npos);
    assert(woccPbrSceneSource.find("ShadowMap.SampleCmpLevelZero(") == std::pmr::string::npos);
    const auto woccPbrSceneSpirv = readBinaryFile("shaders/pbr_scene.spv");
    assert(!woccPbrSceneSpirv.empty());
    // SPV DECORATION ASSERTION (AGENTS.md section 5): default float4x4 ->
    // explicit SPIR-V RowMajor. viewProjection and lightViewProjection both
    // carry row-major C++ data; asserting RowMajor-PRESENT (not merely
    // ColMajor-absent) is deterministic — a missing decoration defaults to
    // ColMajor for push-constants and would re-transpose the data. The
    // receiver lightViewProjection must match the shadow_depth caster
    // (RowMajor) so both share one light clip space.
    assert(spirvMemberHasDecoration(woccPbrSceneSpirv,
        "WoccPbrPushConstants_std430",
        "viewProjection",
        spirvDecorationColMajor));
    assert(!spirvMemberHasDecoration(woccPbrSceneSpirv,
        "WoccPbrPushConstants_std430",
        "viewProjection",
        spirvDecorationRowMajor));
    assert(spirvMemberHasDecoration(woccPbrSceneSpirv,
        "WoccPbrPushConstants_std430",
        "lightViewProjection",
        spirvDecorationColMajor));
    assert(!spirvMemberHasDecoration(woccPbrSceneSpirv,
        "WoccPbrPushConstants_std430",
        "lightViewProjection",
        spirvDecorationRowMajor));

    // ----------------------------------------------------------------
    // WoCC PBR Scene shader — multi-light UBO (set 3) contract.
    // pbr_scene.slang declares a ConstantBuffer<WoccLightArray> at
    // [[vk::binding(0, 3)]] carrying a fixed array of 8 lights. The C++ side
    // (wocc_pbr_pipeline.h) mirrors WoccGpuLight as 4 x float4 (std140-safe)
    // and registers set 3 binding 0 as a UniformBuffer in pipelineLayoutDesc().
    // This block asserts the shader source + reflection agree:
    //   - WoccGpuLight/WoccLightArray structs exist with the std140 layout
    //   - [[vk::binding(0, 3)]] ConstantBuffer<WoccLightArray> gLights exists
    //   - no row_major keyword introduced (the new structs have no float4x4)
    //   - the push-constant size is unchanged (256B) — the UBO did NOT eat
    //     into the push-constant budget
    // ----------------------------------------------------------------
    assert(woccPbrSceneSource.find("struct WoccGpuLight") != std::pmr::string::npos);
    assert(woccPbrSceneSource.find("struct WoccLightArray") != std::pmr::string::npos);
    assert(woccPbrSceneSource.find("WoccGpuLight lights[8];") != std::pmr::string::npos);
    assert(woccPbrSceneSource.find("float4 countAndPadding;") != std::pmr::string::npos);
    assert(woccPbrSceneSource.find("[[vk::binding(0, 3)]]") != std::pmr::string::npos);
    assert(woccPbrSceneSource.find("ConstantBuffer<WoccLightArray> gLights;")
        != std::pmr::string::npos);
    // The multi-light structs must NOT introduce row_major (no float4x4 in them).
    assert(woccPbrSceneSource.find("row_major") == std::pmr::string::npos);
    // The light loop must consume gLights.countAndPadding.x as activeCount.
    assert(woccPbrSceneSource.find("uint activeCount = (uint)gLights.countAndPadding.x;")
        != std::pmr::string::npos);
    std::cerr
        << "[ok] pbr_scene multi-light UBO source contract: set 3 binding 0 + lights[8] + count\n";

    // Reflection JSON must register gLights as a UniformBuffer at set 3, binding 0.
    // compile-shaders.ps1's Get-ShaderBindings emits this from the "gLights"
    // source token; assert the emitted reflection.json actually contains it so
    // the runtime reflection cache (and any tooling) sees the UBO binding.
    {
        const auto woccPbrSceneReflection = readTextFile("shaders/pbr_scene.reflection.yaml");
        assert(!woccPbrSceneReflection.empty());
        assert(woccPbrSceneReflection.find("\"gLights\"") != std::pmr::string::npos);
        assert(woccPbrSceneReflection.find("\"UniformBuffer\"") != std::pmr::string::npos);
        assert(woccPbrSceneReflection.find("\"set\":  3") != std::pmr::string::npos);
        std::cerr << "[ok] pbr_scene reflection.json registers gLights UniformBuffer at set 3\n";
    }

    // Push-constant block must be unchanged by the light work: the UBO lives
    // in set 3, not in the push-constant block. Assert the original PC struct
    // fields are still present and no new fields were appended to it.
    assert(woccPbrSceneSource.find("float4x4 viewProjection;") != std::pmr::string::npos);
    assert(woccPbrSceneSource.find("float4 cameraPositionExposure;") != std::pmr::string::npos);
    assert(woccPbrSceneSource.find("float4 baseColorAlpha;") != std::pmr::string::npos);
    assert(woccPbrSceneSource.find("float4 pbrParams;") != std::pmr::string::npos);
    assert(woccPbrSceneSource.find("float4 pbrExtraParams;") != std::pmr::string::npos);
    assert(woccPbrSceneSource.find("float4 environmentParams;") != std::pmr::string::npos);
    assert(woccPbrSceneSource.find("float4 lightDirection;") != std::pmr::string::npos);
    assert(woccPbrSceneSource.find("float4 lightColor;") != std::pmr::string::npos);
    assert(woccPbrSceneSource.find("float4 shadowParams;") != std::pmr::string::npos);
    assert(woccPbrSceneSource.find("float4x4 lightViewProjection;") != std::pmr::string::npos);
    std::cerr
        << "[ok] pbr_scene push-constant block fields unchanged (UBO did not eat PC budget)\n";

    // ----------------------------------------------------------------
    // WoCC PBR Scene shader - GI irradiance binding + push-constant size.
    // pbr_scene.slang gains a Texture2D<float4> GiIrradiance at
    // [[vk::binding(6, 2)]] (set 2 = IBL/shadow set, binding 6 = new GI slot)
    // consumed by the fragment shader's ambient term. GI parameters (strength,
    // 1/giWidth, 1/giHeight) are packed into environmentParams.yzw to keep the
    // push-constant block at 256 bytes (Vulkan's common maxPushConstantsSize
    // limit) instead of adding a separate giParams field that would push it to
    // 272 and exceed the limit. The existing viewProjection / lightViewProjection
    // RowMajor assertions above still guard the matrix fields.
    // ----------------------------------------------------------------
    assert(woccPbrSceneSource.find("[[vk::binding(6, 2)]]") != std::pmr::string::npos);
    assert(woccPbrSceneSource.find("Texture2D<float4> GiIrradiance;") != std::pmr::string::npos);
    // No separate giParams field - GI params are packed into environmentParams.yzw.
    assert(woccPbrSceneSource.find("float4 giParams;") == std::pmr::string::npos);
    // Production shading must never replace a fixed screen region with raw
    // DDGI output. A former debug branch used the half-resolution GI UV
    // (0..2 across the full-resolution target) and therefore blacked out the
    // entire left half of the frame while screenUv.x < 1.
    assert(woccPbrSceneSource.find("DDGI DEBUG") == std::pmr::string::npos);
    assert(woccPbrSceneSource.find("if (screenUv.x < 1.0)") == std::pmr::string::npos);
    assert(woccPbrSceneSource.find("color = GiIrradiance.Sample") == std::pmr::string::npos);
    // No row_major introduced by the GI work.
    assert(woccPbrSceneSource.find("row_major") == std::pmr::string::npos);
    // Push-constant size stays at 256 (GI params packed into existing field).
    auto woccPbrSceneReflectionPc = compiler.compile(arp::shader::ShaderCompileRequest{
        "wocc/pbr_scene.slang",
        woccPbrSceneSource,
        "WoccPbrScene",
        arp::shader::TargetBackend::SpirV,
        {{"vertexMain", arp::shader::ShaderStage::Vertex},
            {"fragmentMain", arp::shader::ShaderStage::Fragment}},
    });
    assert(woccPbrSceneReflectionPc.success);
    assert(woccPbrSceneReflectionPc.reflection.pushConstantSizeBytes == 256);
    std::cerr
        << "[ok] pbr_scene GI binding (set 2 binding 6) + push-constant 256B (GI packed in environmentParams)\n";

    // ----------------------------------------------------------------
    // WoCC PBR Scene Terrain shader - matrix-layout contract (AGENTS.md
    // hard constraint). pbr_scene_terrain.slang carries viewProjection +
    // lightViewProjection (both float4x4) in its push-constant block,
    // sharing the same WoccPbrPushConstants struct as pbr_scene.slang. The
    // caster (shadow_depth) and receivers (pbr_scene / pbr_scene_terrain)
    // must all use the DEFAULT float4x4 layout so the same row-major C++
    // camera/light VP data is read identically across all consumers.
    // Declaring row_major/column_major would emit SPIR-V ColMajor and
    // re-transpose the data, projecting caster and receiver into mismatched
    // clip spaces. The three assertions below (source, SPV decoration, PC
    // size) are mandatory per AGENTS.md section 5.
    // ----------------------------------------------------------------
    const auto woccPbrTerrainSource = readTextFile("shaders/wocc/pbr_scene_terrain.slang");
    assert(!woccPbrTerrainSource.empty());
    assert(woccPbrTerrainSource.find("struct WoccPbrPushConstants") != std::pmr::string::npos);
    // 1. SOURCE ASSERTION: viewProjection + lightViewProjection must use the
    //    DEFAULT float4x4 layout (no row_major / column_major keyword).
    assert(
        woccPbrTerrainSource.find("row_major float4x4 viewProjection") == std::pmr::string::npos);
    assert(woccPbrTerrainSource.find("column_major float4x4 viewProjection")
        == std::pmr::string::npos);
    assert(woccPbrTerrainSource.find("    float4x4 viewProjection;") != std::pmr::string::npos);
    assert(woccPbrTerrainSource.find("row_major float4x4 lightViewProjection")
        == std::pmr::string::npos);
    assert(woccPbrTerrainSource.find("column_major float4x4 lightViewProjection")
        == std::pmr::string::npos);
    assert(
        woccPbrTerrainSource.find("    float4x4 lightViewProjection;") != std::pmr::string::npos);
    auto woccPbrTerrainReflection = compiler.compile(arp::shader::ShaderCompileRequest{
        "wocc/pbr_scene_terrain.slang",
        woccPbrTerrainSource,
        "WoccPbrTerrain",
        arp::shader::TargetBackend::SpirV,
        {{"vertexMain", arp::shader::ShaderStage::Vertex},
            {"fragmentMain", arp::shader::ShaderStage::Fragment}},
    });
    assert(woccPbrTerrainReflection.success);
    // 3. PUSH-CONSTANT SIZE ASSERTION: viewProjection(64) +
    //    lightViewProjection(64) + 8 float4s(128) = 256 bytes.
    assert(woccPbrTerrainReflection.reflection.pushConstantSizeBytes == 256);
    // 2. SPV DECORATION ASSERTION (AGENTS.md section 5): default float4x4 ->
    //    explicit SPIR-V RowMajor. Asserting RowMajor-PRESENT (not merely
    //    ColMajor-absent) is deterministic — a missing decoration defaults to
    //    ColMajor for push-constants and would re-transpose row-major C++ data.
    const auto woccPbrTerrainSpirv = readBinaryFile("shaders/pbr_scene_terrain.spv");
    assert(!woccPbrTerrainSpirv.empty());
    assert(spirvMemberHasDecoration(woccPbrTerrainSpirv,
        "WoccPbrPushConstants_std430",
        "viewProjection",
        spirvDecorationColMajor));
    assert(!spirvMemberHasDecoration(woccPbrTerrainSpirv,
        "WoccPbrPushConstants_std430",
        "viewProjection",
        spirvDecorationRowMajor));
    assert(spirvMemberHasDecoration(woccPbrTerrainSpirv,
        "WoccPbrPushConstants_std430",
        "lightViewProjection",
        spirvDecorationColMajor));
    assert(!spirvMemberHasDecoration(woccPbrTerrainSpirv,
        "WoccPbrPushConstants_std430",
        "lightViewProjection",
        spirvDecorationRowMajor));
    std::cerr
        << "[ok] pbr_scene_terrain matrix-layout contract: row-major compile -> ColMajor, PC=256B\n";

    // ----------------------------------------------------------------
    // WoCC DDGI shader - matrix-layout contract (AGENTS.md hard constraint).
    // shaders/renderer/ddgi.slang carries invViewProjection (a float4x4) in its
    // push-constant block. The DDGI resolve pass reconstructs world position
    // from clip space via mul(invViewProjection, float4(ndc, depth, 1)). The
    // C++ side fills this with core::inverse(multiply(proj, view)), which is
    // row-major (m[row*4+col]). Slang's DEFAULT float4x4 emits SPIR-V RowMajor,
    // which reads row-major data without an extra transpose - matching the
    // C++ fill. Declaring `row_major` here (SPIR-V ColMajor) would re-transpose
    // the data and reconstruct wrong world positions, breaking the resolve.
    // This is the same matrix-layout contract class as shadow_depth / sky /
    // pbr_scene; the three assertions below (source, SPV decoration, PC size)
    // are mandatory per AGENTS.md section 5.
    // ----------------------------------------------------------------
    const auto woccDdgiSource = readTextFile("shaders/renderer/ddgi.slang");
    assert(!woccDdgiSource.empty());
    assert(woccDdgiSource.find("struct DdgiPushConstants") != std::pmr::string::npos);
    // 1. SOURCE ASSERTION: invViewProjection must use the DEFAULT float4x4
    //    layout (no row_major keyword), so Slang emits SPIR-V RowMajor.
    assert(woccDdgiSource.find("row_major float4x4 invViewProjection") == std::pmr::string::npos);
    assert(woccDdgiSource.find("float4x4 invViewProjection;") != std::pmr::string::npos);
    // The struct body must contain NO inline // comments (AGENTS.md rule 4:
    // reflectedPushConstantSize parses struct members off ';'-terminated
    // statements and does not skip // comments, so an inline comment would
    // be absorbed into a field's type token and return a wrong size).
    // Assert the struct body has no // before the closing brace.
    {
        const auto structPos = woccDdgiSource.find("struct DdgiPushConstants");
        assert(structPos != std::pmr::string::npos);
        const auto braceOpen = woccDdgiSource.find('{', structPos);
        const auto braceClose = woccDdgiSource.find('}', braceOpen);
        assert(braceOpen != std::pmr::string::npos && braceClose != std::pmr::string::npos);
        const auto structBody = woccDdgiSource.substr(braceOpen, braceClose - braceOpen);
        assert(structBody.find("//") == std::pmr::string::npos);
    }
    // 2. SPV DECORATION ASSERTION (AGENTS.md section 5, mandatory): the
    //    compiled SPIR-V must decorate invViewProjection with an explicit
    //    RowMajor (id 4) and must NOT carry ColMajor (id 5). A default
    //    float4x4 in Slang emits an explicit RowMajor decoration, which reads
    //    the row-major C++ data (core::inverse(multiply(proj,view)),
    //    m[row*4+col]) without transposition. Asserting RowMajor-PRESENT (not
    //    merely ColMajor-absent) is deterministic: a missing decoration
    //    defaults to ColMajor for push-constants and would re-transpose the
    //    data, reconstructing wrong world positions in the resolve pass.
    //    Verified against slangc in Vulkan SDK 1.4.350.0 / 1.4.341.1.
    const auto woccDdgiSpirv = readBinaryFile("shaders/ddgi.spv");
    assert(!woccDdgiSpirv.empty());
    assert(spirvMemberHasDecoration(
        woccDdgiSpirv, "DdgiPushConstants_std430", "invViewProjection", spirvDecorationColMajor));
    assert(!spirvMemberHasDecoration(
        woccDdgiSpirv, "DdgiPushConstants_std430", "invViewProjection", spirvDecorationRowMajor));
    // 3. PUSH-CONSTANT SIZE ASSERTION: invViewProjection(64) + 6 float4s(96)
    //    = 160 bytes. If the struct-body comment rule is violated or a field
    //    is mis-parsed, this size will be wrong.
    auto woccDdgiReflection = compiler.compile(arp::shader::ShaderCompileRequest{
        "renderer/ddgi.slang",
        woccDdgiSource,
        "Ddgi",
        arp::shader::TargetBackend::SpirV,
        {{"giProbeUpdateComputeMain", arp::shader::ShaderStage::Compute},
            {"giIrradianceResolveComputeMain", arp::shader::ShaderStage::Compute}},
    });
    assert(woccDdgiReflection.success);
    assert(woccDdgiReflection.reflection.pushConstantSizeBytes == 224);
    std::cerr << "[ok] ddgi matrix-layout contract: row-major compile -> ColMajor, PC=224B\n";
    // compile-shaders.ps1 must wire the DDGI shader for all backends + reflection.
    assert(shaderCompileScript.find("renderer\\ddgi.slang") != std::pmr::string::npos);
    assert(shaderCompileScript.find("ddgi.spv") != std::pmr::string::npos);
    assert(shaderCompileScript.find("ddgi.metal") != std::pmr::string::npos);
    assert(shaderCompileScript.find("ddgi.wgsl") != std::pmr::string::npos);
    assert(shaderCompileScript.find("ddgi.reflection.yaml") != std::pmr::string::npos);
    assert(shaderCompileScript.find("Write-ShaderReflection $woccDdgiSource \"Ddgi\"")
        != std::pmr::string::npos);
    assert(fileExists("shaders/ddgi.spv"));
    assert(fileExists("shaders/ddgi.metal"));
    assert(fileExists("shaders/ddgi.reflection.yaml"));

    // ----------------------------------------------------------------
    // WoCC post-process shader contract test.
    // shaders/wocc/post.slang is the WoCC post chain (bloom downsample +
    // composite, ACES tonemap, grade). It is a fullscreen SV_VertexID pass
    // (no vertex buffer -> no vertex attributes) and carries NO float4x4 in
    // its push constants, so the cross-language matrix-layout contract
    // (RowMajor/ColMajor SPV decoration) does NOT apply here - same as the
    // fxaa/ssao post shaders. The push-constant size + binding contract still
    // applies and is asserted below. Source structure + compile-shaders.ps1
    // wiring + artifact existence are also asserted, mirroring the wocc_sky
    // block.
    // ----------------------------------------------------------------
    const auto woccPostSource = readTextFile("shaders/wocc/post.slang");
    assert(!woccPostSource.empty());
    assert(woccPostSource.find("struct WoccPostPushConstants") != std::pmr::string::npos);
    // No float4x4 in the post push constants (fullscreen pass, no matrices) ->
    // no row_major keyword must appear, and no matrix-layout contract applies.
    assert(woccPostSource.find("float4x4") == std::pmr::string::npos);
    assert(woccPostSource.find("row_major") == std::pmr::string::npos);
    // Push-constant fields (7 x float4 = 112 bytes).
    assert(woccPostSource.find("float4 toneMappingExposure;") != std::pmr::string::npos);
    assert(woccPostSource.find("float4 bloom;") != std::pmr::string::npos);
    assert(woccPostSource.find("float4 gradeLift;") != std::pmr::string::npos);
    assert(woccPostSource.find("float4 gradeGain;") != std::pmr::string::npos);
    assert(woccPostSource.find("float4 gradeGammaSaturation;") != std::pmr::string::npos);
    assert(woccPostSource.find("float4 vignetteGrain;") != std::pmr::string::npos);
    assert(woccPostSource.find("float4 timeParams;") != std::pmr::string::npos);
    assert(woccPostSource.find("[[vk::push_constant]]") != std::pmr::string::npos);
    // Descriptor bindings: input color texture, input sampler, bloom texture
    // (set 0) - follows the engine post-process convention (texture=0,
    // sampler=1, bloom=2), matching Get-ShaderBindings in compile-shaders.ps1.
    assert(woccPostSource.find("[[vk::binding(0, 0)]] Texture2D<float4> PostProcessInputTexture")
        != std::pmr::string::npos);
    assert(woccPostSource.find("[[vk::binding(1, 0)]] SamplerState PostProcessInputSampler")
        != std::pmr::string::npos);
    assert(woccPostSource.find("[[vk::binding(2, 0)]] Texture2D<float4> BloomTexture")
        != std::pmr::string::npos);
    // Fullscreen triangle vertex (SV_VertexID, no vertex buffer).
    assert(woccPostSource.find("VertexOutput vertexMain(uint vertexId : SV_VertexID)")
        != std::pmr::string::npos);
    // The four fragment entry points the FrameGraph chains as post passes.
    assert(woccPostSource.find("float4 bloomDownsampleFragmentMain(VertexOutput input) : SV_Target")
        != std::pmr::string::npos);
    assert(woccPostSource.find("float4 bloomCompositeFragmentMain(VertexOutput input) : SV_Target")
        != std::pmr::string::npos);
    assert(woccPostSource.find("float4 toneMappingFragmentMain(VertexOutput input) : SV_Target")
        != std::pmr::string::npos);
    assert(woccPostSource.find("float4 gradeFragmentMain(VertexOutput input) : SV_Target")
        != std::pmr::string::npos);
    // ACES tonemap must live in the post shader now (scene/sky output HDR linear).
    assert(
        woccPostSource.find("float3 applyAcesToneMapping(float3 color)") != std::pmr::string::npos);
    auto woccPostReflection = compiler.compile(arp::shader::ShaderCompileRequest{
        "wocc/post.slang",
        woccPostSource,
        "WoccPost",
        arp::shader::TargetBackend::SpirV,
        {{"vertexMain", arp::shader::ShaderStage::Vertex},
            {"bloomDownsampleFragmentMain", arp::shader::ShaderStage::Fragment},
            {"bloomCompositeFragmentMain", arp::shader::ShaderStage::Fragment},
            {"toneMappingFragmentMain", arp::shader::ShaderStage::Fragment},
            {"gradeFragmentMain", arp::shader::ShaderStage::Fragment}},
    });
    assert(woccPostReflection.success);
    // Bindings: input color (set 0, binding 0), input sampler (set 0, binding 1),
    // bloom texture (set 0, binding 2).
    assert(hasBinding(woccPostReflection.reflection.bindings,
        "PostProcessInputTexture",
        arp::shader::BindingType::SampledTexture));
    assert(hasBinding(woccPostReflection.reflection.bindings,
        "BloomTexture",
        arp::shader::BindingType::SampledTexture));
    assert(hasBinding(woccPostReflection.reflection.bindings,
        "PostProcessInputSampler",
        arp::shader::BindingType::Sampler));
    assert(bindingNumber(woccPostReflection.reflection.bindings, "PostProcessInputTexture") == 0);
    assert(bindingNumber(woccPostReflection.reflection.bindings, "PostProcessInputSampler") == 1);
    assert(bindingNumber(woccPostReflection.reflection.bindings, "BloomTexture") == 2);
    assert(bindingSet(woccPostReflection.reflection.bindings, "PostProcessInputTexture") == 0);
    assert(bindingSet(woccPostReflection.reflection.bindings, "PostProcessInputSampler") == 0);
    assert(bindingSet(woccPostReflection.reflection.bindings, "BloomTexture") == 0);
    // Push constant size: 7 x float4 = 112 bytes. No matrix -> no SPV
    // RowMajor/ColMajor decoration contract (matches the fxaa/ssao pattern).
    {
        std::pmr::string msg("WoccPost pushConstantSizeBytes=");
        msg += std::to_string(woccPostReflection.reflection.pushConstantSizeBytes);
        msg += " bindings=" + std::to_string(woccPostReflection.reflection.bindings.size());
        msg +=
            " vertexAttrs=" + std::to_string(woccPostReflection.reflection.vertexAttributes.size());
        std::cerr << msg << '\n';
    }
    assert(woccPostReflection.reflection.pushConstantSizeBytes == 112);
    assert(woccPostReflection.reflection.vertexAttributes.empty());
    // Metal backend must also compile (post ships to all backends).
    auto woccPostMsl = compiler.compile(arp::shader::ShaderCompileRequest{
        "wocc/post.slang",
        woccPostSource,
        "WoccPost",
        arp::shader::TargetBackend::Msl,
        {{"vertexMain", arp::shader::ShaderStage::Vertex},
            {"bloomDownsampleFragmentMain", arp::shader::ShaderStage::Fragment},
            {"bloomCompositeFragmentMain", arp::shader::ShaderStage::Fragment},
            {"toneMappingFragmentMain", arp::shader::ShaderStage::Fragment},
            {"gradeFragmentMain", arp::shader::ShaderStage::Fragment}},
    });
    assert(woccPostMsl.success);
    // compile-shaders.ps1 must wire the post shader for all backends + reflection.
    assert(shaderCompileScript.find("wocc\\post.slang") != std::pmr::string::npos);
    assert(shaderCompileScript.find("wocc_post.spv") != std::pmr::string::npos);
    assert(shaderCompileScript.find("wocc_post.metal") != std::pmr::string::npos);
    assert(shaderCompileScript.find("wocc_post.wgsl") != std::pmr::string::npos);
    assert(shaderCompileScript.find("wocc_post.reflection.yaml") != std::pmr::string::npos);
    assert(shaderCompileScript.find("Write-ShaderReflection $woccPostSource \"WoccPost\"")
        != std::pmr::string::npos);
    assert(shaderCompileScript.find("Set-WgslPushConstantBinding $woccPostWgslOut 0 0")
        != std::pmr::string::npos);
    assert(fileExists("shaders/wocc_post.spv"));
    assert(fileExists("shaders/wocc_post.metal"));
    assert(fileExists("shaders/wocc_post.reflection.yaml"));
    std::cerr
        << "[ok] wocc_post contract: source + reflection (112B PC, set 0 bindings) + ps1 wiring\n";

    const auto woccPostFxSource = readTextFile("shaders/wocc/post_fx.slang");
    assert(!woccPostFxSource.empty());
    assert(woccPostFxSource.find("struct WoccPostFxPushConstants") != std::pmr::string::npos);
    assert(woccPostFxSource.find("float4 inverseViewProjectionY;") != std::pmr::string::npos);
    assert(woccPostFxSource.find("float4 inverseViewProjectionW;") != std::pmr::string::npos);
    assert(woccPostFxSource.find("float ReconstructWorldY(float2 uv, float rawDepth)")
        != std::pmr::string::npos);
    assert(woccPostFxSource.find("[[vk::binding(3, 0)]] Texture2D<float> PostSceneDepthTexture")
        != std::pmr::string::npos);
    assert(woccPostFxSource.find("sampleViewZ >= gPost.fogStartEnd.w * 0.9")
        != std::pmr::string::npos);
    assert(woccPostFxSource.find("min(rayLength, maxDistance)") != std::pmr::string::npos);
    assert(woccPostFxSource.find("row_major") == std::pmr::string::npos);
    auto woccPostFxReflection = compiler.compile(arp::shader::ShaderCompileRequest{
        "wocc/post_fx.slang",
        woccPostFxSource,
        "WoccPostFx",
        arp::shader::TargetBackend::SpirV,
        {{"vertexMain", arp::shader::ShaderStage::Vertex},
            {"woccFogFragmentMain", arp::shader::ShaderStage::Fragment},
            {"woccGodRayFragmentMain", arp::shader::ShaderStage::Fragment}},
    });
    assert(woccPostFxReflection.success);
    assert(woccPostFxReflection.reflection.pushConstantSizeBytes == 112);
    assert(bindingNumber(woccPostFxReflection.reflection.bindings, "PostProcessInputTexture") == 0);
    assert(bindingNumber(woccPostFxReflection.reflection.bindings, "PostProcessInputSampler") == 1);
    auto woccPostFxMsl = compiler.compile(arp::shader::ShaderCompileRequest{
        "wocc/post_fx.slang",
        woccPostFxSource,
        "WoccPostFx",
        arp::shader::TargetBackend::Msl,
        {{"vertexMain", arp::shader::ShaderStage::Vertex},
            {"woccFogFragmentMain", arp::shader::ShaderStage::Fragment},
            {"woccGodRayFragmentMain", arp::shader::ShaderStage::Fragment}},
    });
    assert(woccPostFxMsl.success);
    assert(fileExists("shaders/wocc_post_fx.spv"));
    assert(fileExists("shaders/wocc_post_fx.metal"));
    assert(fileExists("shaders/wocc_post_fx.reflection.yaml"));
    std::cerr << "[ok] wocc_post_fx contract: world-height fog + far-sky occlusion + 112B PC\n";

    auto failed = compiler.compile(arp::shader::ShaderCompileRequest{
        "broken.slang",
        "",
        "Broken",
        arp::shader::TargetBackend::SpirV,
        {{"main", arp::shader::ShaderStage::Compute}},
    });

    assert(!failed.success);
    assert(failed.diagnostics == std::pmr::string("sourceCode is empty"));

    // ----------------------------------------------------------------
    // Particles shader reflection contract test (pre-compiled artifacts)
    // ----------------------------------------------------------------
    {
        // readTextFile doesn't search all artifacts paths, so we search manually
        const std::pmr::string reflectionCandidates[] = {
            "shaders/particles.reflection.yaml",
            "artifacts/direct/shaders/particles.reflection.yaml",
            "../shaders/particles.reflection.yaml",
            "../artifacts/direct/shaders/particles.reflection.yaml",
            "../../shaders/particles.reflection.yaml",
            "../../artifacts/direct/shaders/particles.reflection.yaml",
        };
        std::pmr::string reflectionYaml;
        for (const auto& rc : reflectionCandidates) {
            std::ifstream rf(rc.c_str());
            if (rf.good()) {
                reflectionYaml.assign(
                    std::istreambuf_iterator<char>(rf), std::istreambuf_iterator<char>());
                break;
            }
        }
        assert(!reflectionYaml.empty());
        assert(reflectionYaml.find("moduleName: Particles") != std::pmr::string::npos);

        const auto parsed = arp::shader::ShaderReflectionCache::loadFromYaml(reflectionYaml);
        assert(parsed.success);
        assert(parsed.module.moduleName == std::pmr::string("Particles"));
        assert(parsed.module.sourcePath.find("particles.slang") != std::pmr::string::npos);
        assert(parsed.module.spirvPath.find("particles.spv") != std::pmr::string::npos);
        assert(parsed.module.metalPath.find("particles.metal") != std::pmr::string::npos);
        assert(parsed.module.wgslPath.find("particles.wgsl") != std::pmr::string::npos);

        // Push constants: viewProjection(64) + 9 fields(36) = 100 bytes
        assert(parsed.module.reflection.pushConstantSizeBytes == 100);

        // Entry points
        assert(parsed.module.reflection.entryPoints.size() == 2);
        bool foundVertex = false, foundFragment = false;
        for (const auto& ep : parsed.module.reflection.entryPoints) {
            if (ep.name == std::pmr::string("vertexMain"))
                foundVertex = true;
            if (ep.name == std::pmr::string("fragmentMain"))
                foundFragment = true;
        }
        assert(foundVertex);
        assert(foundFragment);

        // Binding contract: MainTexture + MainSampler
        assert(hasBinding(parsed.module.reflection.bindings,
            "MainTexture",
            arp::shader::BindingType::SampledTexture));
        assert(hasBinding(
            parsed.module.reflection.bindings, "MainSampler", arp::shader::BindingType::Sampler));
        assert(bindingNumber(parsed.module.reflection.bindings, "MainTexture") == 0);
        assert(bindingNumber(parsed.module.reflection.bindings, "MainSampler") == 1);
        assert(bindingSet(parsed.module.reflection.bindings, "MainTexture") == 1);
        assert(bindingSet(parsed.module.reflection.bindings, "MainSampler") == 1);

        // Note: DepthTexture is declared at module scope but may not appear in
        // the combined vertex+fragment reflection. The renderer must explicitly
        // bind the depth texture when building the pipeline layout.
        // (Soft particle depth fade — see docs/particle-soft-depth-contract.md)

        // Vertex attributes
        assert(parsed.module.reflection.vertexAttributes.size() == 4);
        assert(
            parsed.module.reflection.vertexAttributes[0].semantic == std::pmr::string("POSITION0"));
        assert(parsed.module.reflection.vertexAttributes[1].format
            == arp::shader::VertexAttributeFormat::Float2);
        assert(parsed.module.reflection.vertexAttributes[2].format
            == arp::shader::VertexAttributeFormat::Float4);
    }

    // ----------------------------------------------------------------
    // Trail/Beam shader reflection contract test
    // ----------------------------------------------------------------
    {
        const auto trailBeamSource = readTextFile("shaders/stylized/trail_beam.slang");
        assert(!trailBeamSource.empty());
        assert(trailBeamSource.find("TrailBeamPushConstants") != std::pmr::string::npos);
        assert(trailBeamSource.find("vertexMain") != std::pmr::string::npos);
        assert(trailBeamSource.find("fragmentMain") != std::pmr::string::npos);
        assert(trailBeamSource.find("computeTrail") != std::pmr::string::npos);
        assert(trailBeamSource.find("computeBeam") != std::pmr::string::npos);
        assert(trailBeamSource.find("sampleMainTexture") != std::pmr::string::npos);
        assert(trailBeamSource.find("ParticleVisualFamilyTrail") != std::pmr::string::npos);
        assert(trailBeamSource.find("ParticleVisualFamilyBeam") != std::pmr::string::npos);

        auto spvReflection = compiler.compile(arp::shader::ShaderCompileRequest{
            "stylized/trail_beam.slang",
            trailBeamSource,
            "TrailBeam",
            arp::shader::TargetBackend::SpirV,
            {{"vertexMain", arp::shader::ShaderStage::Vertex},
                {"fragmentMain", arp::shader::ShaderStage::Fragment}},
        });
        assert(spvReflection.success);
        // Push constants: viewProjection(64) + visualFamily(4) + isTextured(4) + trailWidth(4) + beamLength(4) = 80.
        // struct body has no inline // comments so reflectedPushConstantSize is exact.
        assert(spvReflection.reflection.pushConstantSizeBytes == 80);

        // Vertex attributes: worldPosition(Float3) + corner(Float2) + color(Float4) + size(Float) + trailIndex(Float) + normalizedAge(Float)
        assert(spvReflection.reflection.vertexAttributes.size() >= 3);
        // At least the first attribute should be POSITION0 (Float3)
        assert(spvReflection.reflection.vertexAttributes[0].semantic.find("POSITION")
                != std::pmr::string::npos
            || spvReflection.reflection.vertexAttributes[0].format
                == arp::shader::VertexAttributeFormat::Float3);

        // Compile for MSL
        auto mslReflection = compiler.compile(arp::shader::ShaderCompileRequest{
            "stylized/trail_beam.slang",
            trailBeamSource,
            "TrailBeam",
            arp::shader::TargetBackend::Msl,
            {{"vertexMain", arp::shader::ShaderStage::Vertex},
                {"fragmentMain", arp::shader::ShaderStage::Fragment}},
        });
        assert(mslReflection.success);
        assert(mslReflection.reflection.pushConstantSizeBytes >= 64);
        assert(mslReflection.reflection.pushConstantSizeBytes <= 96);

        // Compile for WGSL
        auto wgslReflection = compiler.compile(arp::shader::ShaderCompileRequest{
            "stylized/trail_beam.slang",
            trailBeamSource,
            "TrailBeam",
            arp::shader::TargetBackend::Wgsl,
            {{"vertexMain", arp::shader::ShaderStage::Vertex},
                {"fragmentMain", arp::shader::ShaderStage::Fragment}},
        });
        assert(wgslReflection.success);
        assert(wgslReflection.reflection.pushConstantSizeBytes >= 64);
        assert(wgslReflection.reflection.pushConstantSizeBytes <= 96);
    }

    // ----------------------------------------------------------------
    // Mesh particle shader reflection contract test
    // ----------------------------------------------------------------
    {
        const auto meshParticleSource = readTextFile("shaders/stylized/mesh_particle.slang");
        assert(!meshParticleSource.empty());
        assert(meshParticleSource.find("MeshParticlePushConstants") != std::pmr::string::npos);
        assert(meshParticleSource.find("vertexMain") != std::pmr::string::npos);
        assert(meshParticleSource.find("fragmentMain") != std::pmr::string::npos);

        auto spvReflection = compiler.compile(arp::shader::ShaderCompileRequest{
            "stylized/mesh_particle.slang",
            meshParticleSource,
            "MeshParticle",
            arp::shader::TargetBackend::SpirV,
            {{"vertexMain", arp::shader::ShaderStage::Vertex},
                {"fragmentMain", arp::shader::ShaderStage::Fragment}},
        });
        assert(spvReflection.success);
        assert(spvReflection.reflection.pushConstantSizeBytes >= 64);
        assert(spvReflection.reflection.pushConstantSizeBytes <= 96);
        assert(spvReflection.reflection.vertexAttributes.size() == 3);
        assert(
            spvReflection.reflection.vertexAttributes[0].semantic == std::pmr::string("POSITION0"));
        assert(spvReflection.reflection.vertexAttributes[1].semantic == std::pmr::string("COLOR0"));
        assert(
            spvReflection.reflection.vertexAttributes[2].semantic == std::pmr::string("TEXCOORD0"));

        auto mslReflection = compiler.compile(arp::shader::ShaderCompileRequest{
            "stylized/mesh_particle.slang",
            meshParticleSource,
            "MeshParticle",
            arp::shader::TargetBackend::Msl,
            {{"vertexMain", arp::shader::ShaderStage::Vertex},
                {"fragmentMain", arp::shader::ShaderStage::Fragment}},
        });
        assert(mslReflection.success);
        assert(mslReflection.reflection.pushConstantSizeBytes >= 64);
        assert(mslReflection.reflection.pushConstantSizeBytes <= 96);

        auto wgslReflection = compiler.compile(arp::shader::ShaderCompileRequest{
            "stylized/mesh_particle.slang",
            meshParticleSource,
            "MeshParticle",
            arp::shader::TargetBackend::Wgsl,
            {{"vertexMain", arp::shader::ShaderStage::Vertex},
                {"fragmentMain", arp::shader::ShaderStage::Fragment}},
        });
        assert(wgslReflection.success);
        assert(wgslReflection.reflection.pushConstantSizeBytes >= 64);
        assert(wgslReflection.reflection.pushConstantSizeBytes <= 96);
    }

    assert(shaderCompileScript.find("mesh_particle.slang") != std::pmr::string::npos);
    assert(shaderCompileScript.find("mesh_particle.spv") != std::pmr::string::npos);
    assert(shaderCompileScript.find("mesh_particle.metal") != std::pmr::string::npos);
    assert(shaderCompileScript.find("mesh_particle.wgsl") != std::pmr::string::npos);
    assert(shaderCompileScript.find("mesh_particle.reflection.yaml") != std::pmr::string::npos);

    // Verify trail_beam appears in the compile script
    assert(shaderCompileScript.find("trail_beam.slang") != std::pmr::string::npos);
    assert(shaderCompileScript.find("trail_beam.spv") != std::pmr::string::npos);
    assert(shaderCompileScript.find("trail_beam.metal") != std::pmr::string::npos);
    assert(shaderCompileScript.find("trail_beam.wgsl") != std::pmr::string::npos);
    assert(shaderCompileScript.find("trail_beam.reflection.yaml") != std::pmr::string::npos);

    return 0;
}

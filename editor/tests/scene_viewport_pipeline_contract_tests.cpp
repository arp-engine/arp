#include "editor/viewport/editor_scene_viewport_material_payload.h"
#include "editor/viewport/editor_scene_viewport_pipeline_contract.h"
#include "editor/viewport/editor_scene_viewport_shader_package.h"
#include "rhi/backends/null/null_backend.h"
#include "shader/generated_shader_artifact_set.h"
#include "shader/shader_reflection_cache.h"

#include <cassert>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

int main()
{
    using namespace arp;
    static_assert(static_cast<int>(
                      editor::EditorSceneViewportPipelineLoweringDiagnostic::UploadAlignmentInvalid)
        == 8);
    // Caller-authored push facts cannot turn an unsupported identity into an OK diagnostic.
    for (auto backend :
        {rhi::Backend::WebGL, static_cast<rhi::Backend>(254u), static_cast<rhi::Backend>(255u)}) {
        for (auto outcome : {rhi::PushConstantLoweringOutcome::DiagnosticFailure,
                 rhi::PushConstantLoweringOutcome::Native,
                 rhi::PushConstantLoweringOutcome::EquivalentLowering}) {
            rhi::DeviceCapabilities forged;
            forged.backend = backend;
            forged.pushConstantLoweringContractVersion = 1u;
            forged.pushConstantLoweringOutcome = outcome;
            forged.pushConstantLoweringHasExecutablePayload = true;
            forged.pushConstantLoweringBoundedFrameBudgetBytes = 256u;
            forged.pushConstantLoweringLifetimeOwner = rhi::PushConstantLifetimeOwner::Unknown;
            rhi::ShaderReflectionDesc reflection;
            reflection.pushConstantByteCount = 256u;
            const auto rejected =
                editor::evaluateEditorSceneViewportPipelineLowering(reflection, forged);
            assert(!rejected.executable());
            assert(rejected.disposition
                == editor::EditorSceneViewportPipelineLoweringDisposition::DiagnosticFailure);
            assert(rejected.diagnostic
                == editor::EditorSceneViewportPipelineLoweringDiagnostic::BackendUnsupported);
            assert(std::strcmp(editor::editorSceneViewportPipelineLoweringDiagnosticCode(
                                   rejected.diagnostic),
                       "ARP-EDITOR-VIEWPORT-LOWERING-BACKEND-UNSUPPORTED")
                == 0);
        }
    }
    const editor::EditorSceneViewportShaderArtifacts artifacts{
        ARP_EDITOR_VIEWPORT_PBR_SOURCE,
        ARP_EDITOR_VIEWPORT_PBR_SPIRV,
        ARP_EDITOR_VIEWPORT_PBR_MSL,
        ARP_EDITOR_VIEWPORT_PBR_WGSL,
        ARP_EDITOR_VIEWPORT_PBR_REFLECTION,
    };
    const auto package = editor::loadEditorSceneViewportShaderPackage(
        artifacts, rhi::TextureFormat::Rgba16Float, rhi::TextureFormat::D32Float, 1u);
    assert(package.valid());
    assert(package.package.generatedReflectionHydrated);
    assert(package.package.entrypoints.size() == 2u);
    assert(package.package.artifacts.spirvPath == artifacts.spirvPath);
    assert(package.package.reflection.descriptorBindings.size() == 6u);
    assert(package.package.reflection.pushConstantByteCount == 256u);
    assert(package.package.reflection.sourceSha256
        == package.pipelineContract.shaderReflection.sourceSha256);
    auto nullDevice = rhi::createNullDevice();
    assert(nullDevice);
    const auto nullPipeline = editor::createEditorSceneViewportDevicePipeline(*nullDevice, package);
    assert(!nullPipeline.valid());
    assert(nullPipeline.diagnostic
        == editor::EditorSceneViewportDevicePipelineDiagnostic::BackendLoweringRejected);
    assert(nullPipeline.lowering.disposition
        == editor::EditorSceneViewportPipelineLoweringDisposition::ContractValidationOnly);
    assert(!nullPipeline.resources.layout && !nullPipeline.resources.pipeline);

    editor::EditorSceneViewportMaterialPayload material;
    material.materialResourceId = "material-test";
    material.materialGeneration = 1u;
    material.sourceResourceId = "source-test";
    material.sourceGeneration = 1u;
    material.shaderPackageId = "shader-test";
    material.shaderGeneration = 1u;
    material.alphaMode = "OPAQUE";
    const auto reflectionForBindings = shader::ShaderReflectionCache::loadFromFile(
        core::pmr::String(ARP_EDITOR_VIEWPORT_PBR_REFLECTION));
    assert(reflectionForBindings.success);
    const auto reflectedBindings =
        editor::inspectEditorSceneViewportShaderBindings(reflectionForBindings.module.reflection);
    assert(reflectedBindings.valid());
    material.shaderBindings = reflectedBindings.bindings;
    rhi::TextureDesc textureDesc;
    textureDesc.label = "DescriptorContractTexture";
    textureDesc.extent = {1u, 1u, 1u};
    textureDesc.format = rhi::TextureFormat::Rgba8Unorm;
    textureDesc.usage = rhi::TextureUsage::Sampled;
    textureDesc.initialState = rhi::ResourceState::ShaderRead;
    auto sampledTexture = nullDevice->createTexture(textureDesc);
    assert(sampledTexture);
    rhi::TextureViewDesc viewDesc;
    viewDesc.label = "DescriptorContractView";
    viewDesc.format = textureDesc.format;
    viewDesc.usage = rhi::TextureUsage::Sampled;
    auto sampledView = nullDevice->createTextureView(*sampledTexture, viewDesc);
    assert(sampledView);
    rhi::SamplerDesc samplerDesc;
    samplerDesc.label = "DescriptorContractSampler";
    auto sampler = nullDevice->createSampler(samplerDesc);
    assert(sampler);
    for (std::size_t index = 0u; index < material.textures.size(); ++index) {
        material.textures[index].semantic =
            static_cast<editor::EditorSceneViewportMaterialTextureSemantic>(index);
        material.textures[index].resourceId = "texture-test";
        material.textures[index].generation = 1u;
        material.textures[index].view = sampledView.get();
        material.textures[index].sampler = sampler.get();
    }
    const auto descriptorWrites = editor::buildEditorSceneViewportDescriptorWrites(material);
    assert(descriptorWrites.valid());
    assert(descriptorWrites.writes.size() == 6u);
    for (std::size_t index = 0u; index < material.textures.size(); ++index) {
        const auto& expected = material.shaderBindings.textures[index];
        assert(descriptorWrites.writes[index].set == expected.set);
        assert(descriptorWrites.writes[index].write.binding == expected.binding);
        assert(descriptorWrites.writes[index].write.type == rhi::DescriptorType::SampledTexture);
        assert(descriptorWrites.writes[index].write.resourceKind
            == rhi::DescriptorResourceKind::TextureView);
        assert(descriptorWrites.writes[index].write.resource == sampledView->id());
    }
    const auto& samplerWrite = descriptorWrites.writes.back();
    assert(samplerWrite.set == material.shaderBindings.sampler.set);
    assert(samplerWrite.write.binding == material.shaderBindings.sampler.binding);
    assert(samplerWrite.write.type == rhi::DescriptorType::Sampler);
    assert(samplerWrite.write.resourceKind == rhi::DescriptorResourceKind::Sampler);
    assert(samplerWrite.write.resource == sampler->id());
    material.textures[2].view = nullptr;
    assert(editor::buildEditorSceneViewportDescriptorWrites(material).diagnostic
        == editor::EditorSceneViewportDescriptorWritesDiagnostic::MaterialNotReady);
    material.textures[2].view = sampledView.get();
    material.shaderBindings.sampler = material.shaderBindings.textures[0];
    assert(editor::buildEditorSceneViewportDescriptorWrites(material).diagnostic
        == editor::EditorSceneViewportDescriptorWritesDiagnostic::MaterialNotReady);

    rhi::DeviceCapabilities vulkanCapabilities;
    vulkanCapabilities.backend = rhi::Backend::Vulkan;
    vulkanCapabilities.pushConstantLoweringContractVersion = 1u;
    vulkanCapabilities.pushConstantLoweringOutcome = rhi::PushConstantLoweringOutcome::Native;
    vulkanCapabilities.pushConstantLoweringHasExecutablePayload = true;
    vulkanCapabilities.pushConstantLoweringBoundedFrameBudgetBytes = 256u;
    vulkanCapabilities.pushConstantLoweringLifetimeOwner =
        rhi::PushConstantLifetimeOwner::NativeCommandBuffer;
    assert(editor::evaluateEditorSceneViewportPipelineLowering(
        package.package.reflection, vulkanCapabilities)
            .executable());

    auto insufficientVulkanBudget = vulkanCapabilities;
    insufficientVulkanBudget.pushConstantLoweringBoundedFrameBudgetBytes = 128u;
    assert(editor::evaluateEditorSceneViewportPipelineLowering(
               package.package.reflection, insufficientVulkanBudget)
               .diagnostic
        == editor::EditorSceneViewportPipelineLoweringDiagnostic::BudgetInsufficient);

    rhi::DeviceCapabilities metalCapabilities;
    metalCapabilities.backend = rhi::Backend::Metal;
    metalCapabilities.pushConstantLoweringContractVersion = 1u;
    metalCapabilities.pushConstantLoweringOutcome = rhi::PushConstantLoweringOutcome::Native;
    metalCapabilities.pushConstantLoweringHasExecutablePayload = true;
    metalCapabilities.pushConstantLoweringBoundedFrameBudgetBytes = 256u;
    metalCapabilities.pushConstantLoweringLifetimeOwner =
        rhi::PushConstantLifetimeOwner::NativeRenderEncoder;
    assert(editor::evaluateEditorSceneViewportPipelineLowering(
        package.package.reflection, metalCapabilities)
            .executable());

    rhi::DeviceCapabilities webGpuCapabilities;
    webGpuCapabilities.backend = rhi::Backend::WebGPU;
    webGpuCapabilities.pushConstantLoweringContractVersion = 1u;
    webGpuCapabilities.pushConstantLoweringOutcome =
        rhi::PushConstantLoweringOutcome::EquivalentLowering;
    webGpuCapabilities.pushConstantLoweringHasExecutablePayload = true;
    webGpuCapabilities.pushConstantLoweringBoundedFrameBudgetBytes = 1024u * 1024u;
    webGpuCapabilities.pushConstantLoweringLifetimeOwner =
        rhi::PushConstantLifetimeOwner::FrameUploadRing;
    webGpuCapabilities.pushConstantLoweringAlignmentBytes = 256u;
    assert(editor::evaluateEditorSceneViewportPipelineLowering(
        package.package.reflection, webGpuCapabilities)
            .executable());
    webGpuCapabilities.pushConstantLoweringAlignmentBytes = 96u;
    assert(editor::evaluateEditorSceneViewportPipelineLowering(
               package.package.reflection, webGpuCapabilities)
               .diagnostic
        == editor::EditorSceneViewportPipelineLoweringDiagnostic::UploadAlignmentInvalid);
    rhi::DeviceCapabilities nullCapabilities;
    nullCapabilities.backend = rhi::Backend::Null;
    assert(editor::evaluateEditorSceneViewportPipelineLowering(
               package.package.reflection, nullCapabilities)
               .disposition
        == editor::EditorSceneViewportPipelineLoweringDisposition::ContractValidationOnly);

    auto missingArtifact = artifacts;
    missingArtifact.wgslPath = {};
    const auto missingPath = editor::loadEditorSceneViewportShaderPackage(
        missingArtifact, rhi::TextureFormat::Rgba16Float, rhi::TextureFormat::D32Float, 1u);
    assert(missingPath.diagnostic
        == editor::EditorSceneViewportShaderPackageDiagnostic::ArtifactPathMissing);

    auto missingTarget = artifacts;
    missingTarget.wgslPath = "D:/missing/editor_viewport_pbr.wgsl";
    const auto targetMissing = editor::loadEditorSceneViewportShaderPackage(
        missingTarget, rhi::TextureFormat::Rgba16Float, rhi::TextureFormat::D32Float, 1u);
    assert(targetMissing.diagnostic
        == editor::EditorSceneViewportShaderPackageDiagnostic::ArtifactIdentityInvalid);

    auto wrongSource = artifacts;
    wrongSource.sourcePath = "viewport_pbr_wrong.slang";
    const auto wrongIdentity = editor::loadEditorSceneViewportShaderPackage(
        wrongSource, rhi::TextureFormat::Rgba16Float, rhi::TextureFormat::D32Float, 1u);
    assert(wrongIdentity.diagnostic
        == editor::EditorSceneViewportShaderPackageDiagnostic::ArtifactIdentityInvalid);

    auto wrongExtension = artifacts;
    wrongExtension.sourcePath = "editor_viewport_pbr.glsl";
    const auto invalidPath = editor::loadEditorSceneViewportShaderPackage(
        wrongExtension, rhi::TextureFormat::Rgba16Float, rhi::TextureFormat::D32Float, 1u);
    assert(invalidPath.diagnostic
        == editor::EditorSceneViewportShaderPackageDiagnostic::ArtifactPathInvalid);

    const auto tempRoot = std::filesystem::temp_directory_path()
        / ("arp-editor-viewport-package-"
            + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(tempRoot);
    const auto staleSource = tempRoot / std::filesystem::path(artifacts.sourcePath).filename();
    std::filesystem::copy_file(artifacts.sourcePath, staleSource);
    {
        std::ofstream changed(staleSource, std::ios::binary | std::ios::app);
        changed << "\n// stale package fingerprint\n";
    }
    const auto staleSpirv = tempRoot / std::filesystem::path(artifacts.spirvPath).filename();
    const auto staleMsl = tempRoot / std::filesystem::path(artifacts.mslPath).filename();
    const auto staleWgsl = tempRoot / std::filesystem::path(artifacts.wgslPath).filename();
    const auto staleReflection =
        tempRoot / std::filesystem::path(artifacts.reflectionPath).filename();
    std::filesystem::copy_file(artifacts.spirvPath, staleSpirv);
    std::filesystem::copy_file(artifacts.mslPath, staleMsl);
    std::filesystem::copy_file(artifacts.wgslPath, staleWgsl);
    std::filesystem::copy_file(artifacts.reflectionPath, staleReflection);
    const auto stalePublication =
        tempRoot / std::filesystem::path(package.package.artifacts.publicationPath).filename();
    std::filesystem::copy_file(package.package.artifacts.publicationPath, stalePublication);
    const auto stale = editor::loadEditorSceneViewportShaderPackage({staleSource.string(),
                                                                        staleSpirv.string(),
                                                                        staleMsl.string(),
                                                                        staleWgsl.string(),
                                                                        staleReflection.string()},
        rhi::TextureFormat::Rgba16Float,
        rhi::TextureFormat::D32Float,
        1u);
    assert(stale.diagnostic
        == editor::EditorSceneViewportShaderPackageDiagnostic::ArtifactIdentityInvalid);

    std::ifstream reflectionFile(std::string(artifacts.reflectionPath), std::ios::binary);
    std::string reflectionText{
        std::istreambuf_iterator<char>(reflectionFile), std::istreambuf_iterator<char>()};
    const auto vertexName = reflectionText.find("name: vertexMain");
    assert(vertexName != std::string::npos);
    reflectionText.replace(vertexName, std::string("name: vertexMain").size(), "name: wrongMain");
    const auto wrongEntryReflection =
        tempRoot / std::filesystem::path(artifacts.reflectionPath).filename();
    std::filesystem::copy_file(
        artifacts.sourcePath, staleSource, std::filesystem::copy_options::overwrite_existing);
    {
        std::ofstream changed(wrongEntryReflection, std::ios::binary);
        changed.write(reflectionText.data(), static_cast<std::streamsize>(reflectionText.size()));
    }
    // Re-publish the changed reflection deliberately. A stale manifest must
    // fail its hash gate before the selected-entry diagnostic can be tested.
    auto publicationNode = core::YamlContract::parseSingleDocument(
        shader::shaderArtifactText(package.package.generatedArtifactSet->publicationBytes()));
    const auto reflectionHash = core::Sha256::hexDigest(reflectionText);
    publicationNode["files"]["reflection"]["sha256"] =
        std::string(reflectionHash.substr(core::StringView("sha256:").size()));
    const auto publicationText = core::YamlContract::serialize(publicationNode);
    {
        std::ofstream changed(stalePublication, std::ios::binary);
        changed.write(publicationText.data(), static_cast<std::streamsize>(publicationText.size()));
    }
    const auto wrongEntry =
        editor::loadEditorSceneViewportShaderPackage({staleSource.string(),
                                                         staleSpirv.string(),
                                                         staleMsl.string(),
                                                         staleWgsl.string(),
                                                         wrongEntryReflection.string()},
            rhi::TextureFormat::Rgba16Float,
            rhi::TextureFormat::D32Float,
            1u);
    assert(wrongEntry.diagnostic
        == editor::EditorSceneViewportShaderPackageDiagnostic::EntryPointMismatch);
    std::filesystem::remove_all(tempRoot);

    std::ifstream input(ARP_EDITOR_VIEWPORT_PBR_REFLECTION, std::ios::binary);
    assert(input.good());
    arp::core::pmr::String yaml{&arp::core::defaultCpuMemoryResource()};
    yaml.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    const auto loaded = arp::shader::ShaderReflectionCache::loadFromYaml(yaml);
    assert(loaded.success);

    const auto valid = editor::buildEditorSceneViewportPipelineContract(loaded.module.reflection,
        "editor/viewport_pbr.slang",
        rhi::TextureFormat::Rgba16Float,
        rhi::TextureFormat::D32Float,
        1u);
    assert(valid.valid());
    assert(valid.shaderReflection.pushConstantByteCount == 256u);
    assert(valid.shaderReflection.pushConstantMembers.size() == 7u);
    assert(valid.shaderReflection.sourceSha256 == loaded.module.reflection.sourceSha256);
    assert(valid.pipelineLayout.pushConstantRanges.size() == 1u);
    assert(valid.pipelineLayout.pushConstantRanges[0].offsetBytes == 0u);
    assert(valid.pipelineLayout.pushConstantRanges[0].sizeBytes == 256u);
    assert(valid.pipelineLayout.pushConstantRanges[0].stages
        == (rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment));
    assert(valid.pipeline.vertexShader.entryPoint == "vertexMain");
    assert(valid.pipeline.fragmentShader.entryPoint == "fragmentMain");
    assert(valid.pipeline.colorTargets.size() == 1u);
    assert(valid.pipeline.colorTargets[0].format == rhi::TextureFormat::Rgba16Float);
    assert(!valid.pipeline.colorTargets[0].blendEnabled);
    assert(valid.pipeline.depthStencil.format == rhi::TextureFormat::D32Float);
    assert(valid.pipeline.depthStencil.depthTestEnabled);
    assert(valid.pipeline.depthStencil.depthWriteEnabled);
    assert(valid.pipeline.depthStencil.depthCompare == rhi::CompareOp::LessEqual);
    assert(valid.pipeline.topology == rhi::PrimitiveTopology::TriangleList);
    assert(valid.pipeline.dynamicViewportScissor);
    assert(!valid.pipeline.hasViewport && !valid.pipeline.hasScissor);
    assert(valid.pipeline.vertexBuffers.size() == 1u);
    assert(valid.pipeline.vertexBuffers[0].strideBytes == 48u);
    assert(valid.pipeline.vertexBuffers[0].attributes.size() == 4u);
    assert(valid.pipeline.vertexBuffers[0].attributes[0].offsetBytes == 0u);
    assert(valid.pipeline.vertexBuffers[0].attributes[1].offsetBytes == 12u);
    assert(valid.pipeline.vertexBuffers[0].attributes[2].offsetBytes == 24u);
    assert(valid.pipeline.vertexBuffers[0].attributes[3].offsetBytes == 32u);

    auto missingInput = loaded.module.reflection;
    missingInput.vertexAttributes.pop_back();
    const auto baseline = editor::buildEditorSceneViewportPipelineContract(loaded.module.reflection,
        "editor/viewport_pbr.slang",
        rhi::TextureFormat::Rgba8Unorm,
        rhi::TextureFormat::Depth24Stencil8,
        1u);
    assert(baseline.valid());
    assert(baseline.pipeline.colorFormat == rhi::TextureFormat::Rgba8Unorm);
    assert(baseline.pipeline.depthFormat == rhi::TextureFormat::Depth24Stencil8);

    const auto invalidVertex = editor::buildEditorSceneViewportPipelineContract(missingInput,
        "editor/viewport_pbr.slang",
        rhi::TextureFormat::Rgba16Float,
        rhi::TextureFormat::D32Float,
        1u);
    assert(invalidVertex.diagnostic
        == editor::EditorSceneViewportPipelineDiagnostic::VertexInputMismatch);

    auto invalidPush = loaded.module.reflection;
    invalidPush.pushConstantMembers[1].offset += 4u;
    const auto invalidPushLayout = editor::buildEditorSceneViewportPipelineContract(invalidPush,
        "editor/viewport_pbr.slang",
        rhi::TextureFormat::Rgba16Float,
        rhi::TextureFormat::D32Float,
        1u);
    assert(invalidPushLayout.diagnostic
        == editor::EditorSceneViewportPipelineDiagnostic::PushConstantMismatch);

    const auto invalidColor =
        editor::buildEditorSceneViewportPipelineContract(loaded.module.reflection,
            "editor/viewport_pbr.slang",
            rhi::TextureFormat::Unknown,
            rhi::TextureFormat::D32Float,
            1u);
    assert(invalidColor.diagnostic
        == editor::EditorSceneViewportPipelineDiagnostic::AttachmentContractMismatch);

    const auto invalidDepth =
        editor::buildEditorSceneViewportPipelineContract(loaded.module.reflection,
            "editor/viewport_pbr.slang",
            rhi::TextureFormat::Rgba16Float,
            rhi::TextureFormat::Depth24Stencil8,
            1u);
    assert(invalidDepth.diagnostic
        == editor::EditorSceneViewportPipelineDiagnostic::AttachmentContractMismatch);

    const auto invalidSamples =
        editor::buildEditorSceneViewportPipelineContract(loaded.module.reflection,
            "editor/viewport_pbr.slang",
            rhi::TextureFormat::Rgba16Float,
            rhi::TextureFormat::D32Float,
            4u);
    assert(invalidSamples.diagnostic
        == editor::EditorSceneViewportPipelineDiagnostic::AttachmentContractMismatch);
    return 0;
}

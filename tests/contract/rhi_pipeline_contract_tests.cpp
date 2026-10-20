#include "core/cpu_allocator.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/gpu_device.h"
#include "rhi/gpu_pipeline.h"
#include "tests/rhi_shader_package_test_helpers.h"

#include <cassert>

int main()
{
    using namespace arp::rhi;
    static const std::uint8_t kPushData[256] = {};

    auto device = createNullDevice();
    assert(device);

    DescriptorLayoutDesc frameLayout;
    frameLayout.name = "FrameDescriptors";
    frameLayout.bindings.push_back({0, DescriptorType::UniformBuffer, ShaderStage::Vertex});

    PipelineLayoutDesc layoutDesc;
    layoutDesc.descriptorLayouts.push_back(frameLayout);
    layoutDesc.descriptorSets.push_back({0, frameLayout.name});
    layoutDesc.descriptorTables.push_back({1,
        "MaterialTextureTable",
        DescriptorType::SampledTexture,
        ShaderStage::Vertex | ShaderStage::Fragment | ShaderStage::Compute,
        4});
    layoutDesc.pushConstantRanges.push_back(
        {ShaderStage::Vertex | ShaderStage::Fragment | ShaderStage::Compute, 0, 128});
    auto duplicateTableSlot = layoutDesc;
    duplicateTableSlot.descriptorTables.push_back({0,
        "DuplicateMaterialTextureTable",
        DescriptorType::SampledTexture,
        ShaderStage::Fragment,
        4});
    assert(!validatePipelineLayout(duplicateTableSlot).success);

    auto pipelineLayout = device->createPipelineLayout(layoutDesc);
    assert(pipelineLayout);
    assert(pipelineLayout->descriptorSetCount() == 1u);
    assert(pipelineLayout->descriptorTableCount() == 1u);
    assert(pipelineLayout->pushConstantByteCount() == 128u);
    assert(pipelineLayout->backendFallbacksHiddenFromUpperLayer());
    assert(!pipelineLayout->nativeHandleExposedToUpperLayer());

    ShaderPackageDesc shaderPackage;
    shaderPackage.label = "SceneShaderPackage";
    shaderPackage.slangModulePath = "rhi_pipeline_contract.slang";
    shaderPackage.entrypoints.push_back(
        {"rhi_pipeline_contract.slang", "vsMain", ShaderStage::Vertex});
    shaderPackage.entrypoints.push_back(
        {"rhi_pipeline_contract.slang", "fsMain", ShaderStage::Fragment});
    shaderPackage.entrypoints.push_back({"rhi_pipeline_contract.slang",
        "csMain",
        ShaderStage::Compute,
        ShaderSourceLanguage::Slang,
        1,
        1,
        1});
    shaderPackage.artifacts.spirvPath = "rhi_pipeline_contract.spv";
    shaderPackage.artifacts.mslPath = "rhi_pipeline_contract.metal";
    shaderPackage.artifacts.wgslPath = "rhi_pipeline_contract.wgsl";
    shaderPackage.artifacts.reflectionPath = "rhi_pipeline_contract.reflection.yaml";
    shaderPackage.reflection.pushConstantByteCount = 128;
    shaderPackage.reflection.pushConstantContractVersion = "AtomicFullBlockPush/1";
    shaderPackage.reflection.pushConstantStages = "CS|FS|VS";
    shaderPackage.reflection.pushConstantWebGpuBindingGroup = 2;
    shaderPackage.reflection.pushConstantWebGpuBindingIndex = 0;
    shaderPackage.reflection.pushConstantMetalBufferIndex = 1;
    shaderPackage.reflection.computeWorkgroupSizeX = 1;
    shaderPackage.reflection.computeWorkgroupSizeY = 1;
    shaderPackage.reflection.computeWorkgroupSizeZ = 1;
    shaderPackage.reflection.metalGpuCountedIndirectStatus = "Unavailable";
    shaderPackage.reflection.metalTessellationStatus = "Unavailable";
    arp::rhi::test::addFloat4PushConstantMembers(shaderPackage.reflection, 8u);
    shaderPackage.reflection.descriptorBindings.push_back(
        {0, 0, DescriptorType::UniformBuffer, ShaderStage::Vertex});
    shaderPackage.reflection.descriptorBindings.push_back(
        {1, 0, DescriptorType::SampledTexture, ShaderStage::Fragment});
    assert(validateShaderPackage(shaderPackage).success);

    // InputAttachment reflection must carry the logical FrameGraph attachment
    // index. A missing index is not a valid package and cannot be repaired by
    // a backend descriptor implementation.
    auto missingInputAttachmentIndex = shaderPackage;
    missingInputAttachmentIndex.reflection.descriptorBindings.push_back(
        {3, 0, DescriptorType::InputAttachment, ShaderStage::Fragment});
    assert(!validateShaderPackage(missingInputAttachmentIndex).success);
    auto reflectedInputAttachment = shaderPackage;
    reflectedInputAttachment.reflection.descriptorBindings.push_back(
        {3, 0, DescriptorType::InputAttachment, ShaderStage::Fragment, 1, 4});
    assert(validateShaderPackage(reflectedInputAttachment).success);

    semantics::SubpassInputBindingReference inputRoute{
        3,
        0,
        4,
        semantics::SubpassInputPhysicalBinding::NativeInputAttachment,
    };
    assert(validateSubpassInputBindingAgainstReflection(
        reflectedInputAttachment.reflection, inputRoute)
            .success);
    auto wrongInputRoute = inputRoute;
    wrongInputRoute.logicalAttachmentIndex = 5;
    assert(!validateSubpassInputBindingAgainstReflection(
        reflectedInputAttachment.reflection, wrongInputRoute)
            .success);
    wrongInputRoute = inputRoute;
    wrongInputRoute.binding = 1;
    assert(!validateSubpassInputBindingAgainstReflection(
        reflectedInputAttachment.reflection, wrongInputRoute)
            .success);
    wrongInputRoute = inputRoute;
    wrongInputRoute.physicalBinding = semantics::SubpassInputPhysicalBinding::Unsupported;
    assert(!validateSubpassInputBindingAgainstReflection(
        reflectedInputAttachment.reflection, wrongInputRoute)
            .success);

    auto missingMslArtifact = shaderPackage;
    missingMslArtifact.artifacts.mslPath.clear();
    assert(!validateShaderPackage(missingMslArtifact).success);

    auto nonSlangPackage = shaderPackage;
    nonSlangPackage.slangModulePath = "scene.glsl";
    assert(!validateShaderPackage(nonSlangPackage).success);

    // L2 semantics: a single Slang module may legitimately expose multiple
    // entry points at the same stage. The canonical case is a post-process
    // module with several fragment mains (bloomDownsample / bloomComposite /
    // tonemap / grade), where each RenderPipeline selects its fragment entry
    // point via RenderPipelineDesc::fragmentShader. Backends resolve entry
    // points by (modulePath, entryPoint, stage) exact match, so multiple
    // same-stage entries are well-defined and MUST validate. A package that
    // declares a true duplicate (same name + stage + module) still fails.
    ShaderPackageDesc postShaderPackage;
    postShaderPackage.label = "PostShaderPackage";
    postShaderPackage.slangModulePath = "post.slang";
    postShaderPackage.entrypoints.push_back({"post.slang", "vertexMain", ShaderStage::Vertex});
    postShaderPackage.entrypoints.push_back(
        {"post.slang", "bloomDownsampleFragmentMain", ShaderStage::Fragment});
    postShaderPackage.entrypoints.push_back(
        {"post.slang", "bloomCompositeFragmentMain", ShaderStage::Fragment});
    postShaderPackage.entrypoints.push_back(
        {"post.slang", "toneMappingFragmentMain", ShaderStage::Fragment});
    postShaderPackage.entrypoints.push_back(
        {"post.slang", "gradeFragmentMain", ShaderStage::Fragment});
    postShaderPackage.artifacts.spirvPath = "post.spv";
    postShaderPackage.artifacts.mslPath = "post.metal";
    postShaderPackage.artifacts.reflectionPath = "post.reflection.yaml";
    postShaderPackage.reflection.pushConstantByteCount = 112;
    postShaderPackage.reflection.pushConstantContractVersion = "AtomicFullBlockPush/1";
    postShaderPackage.reflection.pushConstantStages = "FS";
    arp::rhi::test::addAtomicFullBlockPushMember(postShaderPackage.reflection, 112u);
    postShaderPackage.reflection.descriptorBindings.push_back(
        {0, 0, DescriptorType::SampledTexture, ShaderStage::Fragment});
    postShaderPackage.reflection.descriptorBindings.push_back(
        {0, 1, DescriptorType::Sampler, ShaderStage::Fragment});
    postShaderPackage.reflection.descriptorBindings.push_back(
        {0, 2, DescriptorType::SampledTexture, ShaderStage::Fragment});
    assert(validateShaderPackage(postShaderPackage).success);

    // A true duplicate entry point (same name + stage + module) is rejected.
    auto duplicateEntrypointPackage = postShaderPackage;
    duplicateEntrypointPackage.entrypoints.push_back(
        {"post.slang", "gradeFragmentMain", ShaderStage::Fragment});
    assert(!validateShaderPackage(duplicateEntrypointPackage).success);

    RenderPipelineDesc renderPipelineDesc;
    renderPipelineDesc.label = "ScenePipeline";
    renderPipelineDesc.layout = pipelineLayout.get();
    renderPipelineDesc.shaderPackage = &shaderPackage;
    renderPipelineDesc.vertexShader = {
        "rhi_pipeline_contract.slang", "vsMain", ShaderStage::Vertex};
    renderPipelineDesc.fragmentShader = {
        "rhi_pipeline_contract.slang", "fsMain", ShaderStage::Fragment};
    renderPipelineDesc.colorFormat = TextureFormat::Bgra8Unorm;
    renderPipelineDesc.depthFormat = TextureFormat::D32Float;
    const auto renderPipelineReport = device->validateRenderPipeline(renderPipelineDesc);
    assert(renderPipelineReport.success);
    assert(renderPipelineReport.shaderPackageValidated);
    assert(renderPipelineReport.reflectionMatched);
    assert(renderPipelineReport.entrypointCount == 2u);
    assert(renderPipelineReport.descriptorBindingCount == 2u);
    assert(renderPipelineReport.descriptorLayoutCount == 1u);
    assert(renderPipelineReport.descriptorSetCount == 1u);
    assert(renderPipelineReport.descriptorTableCount == 1u);
    assert(renderPipelineReport.pushConstantBytes == 128u);
    assert(renderPipelineReport.backendFallbacksHiddenFromUpperLayer);
    assert(!renderPipelineReport.nativeHandleExposedToUpperLayer);
    auto renderPipeline = device->createRenderPipeline(renderPipelineDesc);
    assert(renderPipeline);
    assert(renderPipeline->layout() == pipelineLayout.get());
    assert(renderPipeline->shaderSourceLanguage() == ShaderSourceLanguage::Slang);
    assert(renderPipeline->backendFallbacksHiddenFromUpperLayer());
    assert(!renderPipeline->nativeHandleExposedToUpperLayer());
    const auto& renderPipelineMetadata = renderPipeline->validationReport();
    assert(renderPipelineMetadata.colorFormats
        == validateRenderPipelineState(renderPipelineDesc).colorFormats);
    assert(renderPipelineMetadata.depthStencilFormat
        == validateRenderPipelineState(renderPipelineDesc).depthStencilFormat);
    assert(renderPipelineMetadata.success);
    assert(renderPipelineMetadata.entrypointCount == renderPipelineReport.entrypointCount);
    assert(renderPipelineMetadata.descriptorBindingCount
        == renderPipelineReport.descriptorBindingCount);
    assert(renderPipelineMetadata.descriptorSetCount == 1u);
    assert(renderPipelineMetadata.descriptorTableCount == 1u);
    assert(renderPipelineMetadata.pushConstantBytes == 128u);
    assert(renderPipelineMetadata.shaderPackageValidated);
    assert(renderPipelineMetadata.reflectionMatched);
    assert(!renderPipelineMetadata.nativeHandleExposedToUpperLayer);
    const auto& renderReflection = renderPipeline->shaderReflection();
    assert(renderReflection.descriptorBindings.size() == 2u);
    assert(renderReflection.pushConstantByteCount == 128u);

    auto arena = device->createDescriptorArena({"PipelineDescriptorArena", 2, 5});
    assert(arena);
    DescriptorSetDesc setDesc;
    setDesc.label = "FrameSet";
    setDesc.layout = frameLayout;
    auto frameSet = arena->allocateDescriptorSet(setDesc);
    assert(frameSet);
    auto materialTable = arena->allocateDescriptorTable({
        "MaterialTextureTable",
        DescriptorType::SampledTexture,
        ShaderStage::Vertex | ShaderStage::Fragment | ShaderStage::Compute,
        4,
    });
    assert(materialTable);

    auto frameBuffer = device->createBuffer({
        "FrameConstants",
        256,
        ResourceUsage::UniformBuffer | ResourceUsage::CopyDst,
        MemoryUsage::CpuToGpu,
        ResourceState::Undefined,
    });
    assert(frameBuffer);
    auto materialTexture = device->createTexture({
        "MaterialTexture",
        {32, 32, 1},
        TextureFormat::Bgra8Unorm,
        TextureUsage::Sampled | TextureUsage::CopyDst,
        MemoryUsage::GpuOnly,
        ResourceState::Undefined,
    });
    assert(materialTexture);
    auto materialTextureView = device->createTextureView(
        *materialTexture, {"MaterialTextureView", TextureFormat::Bgra8Unorm});
    assert(materialTextureView);

    auto emptyDescriptorContentEncoder = device->createCommandEncoder({"EmptyDescriptorContent"});
    auto emptyDescriptorContentPass =
        emptyDescriptorContentEncoder->beginRenderPass({"Backbuffer"});
    emptyDescriptorContentPass->setPipeline(*renderPipeline);
    emptyDescriptorContentPass->bindDescriptorSet(0, *frameSet);
    emptyDescriptorContentPass->bindDescriptorTable(1, *materialTable);
    const auto drawWithEmptyDescriptorSet = emptyDescriptorContentPass->drawIndexed(3, 1, 0, 0, 0);
    assert(!drawWithEmptyDescriptorSet.success);
    assert(drawWithEmptyDescriptorSet.diagnostics.find("written") != std::pmr::string::npos);
    assert(drawWithEmptyDescriptorSet.recordedCommandCount == 4u);
    emptyDescriptorContentPass->end();
    auto emptyDescriptorContentCommands = emptyDescriptorContentEncoder->finish();
    assert(emptyDescriptorContentCommands->commandCount() == 5u);

    const auto frameSetWrite = frameSet->write({
        0,
        DescriptorType::UniformBuffer,
        frameBuffer->id(),
        DescriptorResourceKind::Buffer,
    });
    assert(frameSetWrite.success);
    assert(frameSet->hasWrittenBinding(0));

    auto emptyTableContentEncoder = device->createCommandEncoder({"EmptyDescriptorTableContent"});
    auto emptyTableContentPass = emptyTableContentEncoder->beginRenderPass({"Backbuffer"});
    emptyTableContentPass->setPipeline(*renderPipeline);
    emptyTableContentPass->bindDescriptorSet(0, *frameSet);
    emptyTableContentPass->bindDescriptorTable(1, *materialTable);
    const auto drawWithEmptyDescriptorTable = emptyTableContentPass->drawIndexed(3, 1, 0, 0, 0);
    assert(!drawWithEmptyDescriptorTable.success);
    assert(drawWithEmptyDescriptorTable.diagnostics.find("descriptor table")
        != std::pmr::string::npos);
    assert(drawWithEmptyDescriptorTable.recordedCommandCount == 4u);
    emptyTableContentPass->end();
    auto emptyTableContentCommands = emptyTableContentEncoder->finish();
    assert(emptyTableContentCommands->commandCount() == 5u);

    const auto materialTableWrite = materialTable->write({
        0,
        DescriptorType::SampledTexture,
        materialTextureView->id(),
        DescriptorResourceKind::TextureView,
    });
    assert(materialTableWrite.success);
    assert(materialTable->hasWrittenDescriptor(0));

    DescriptorLayoutDesc unusedSetLayout;
    unusedSetLayout.name = "UnusedPassDescriptors";
    unusedSetLayout.bindings.push_back({0, DescriptorType::StorageBuffer, ShaderStage::Fragment});

    PipelineLayoutDesc layoutWithUnusedSlots = layoutDesc;
    layoutWithUnusedSlots.descriptorLayouts.push_back(unusedSetLayout);
    layoutWithUnusedSlots.descriptorSets.push_back({2, unusedSetLayout.name});
    layoutWithUnusedSlots.descriptorTables.push_back(
        {3, "UnusedTextureTable", DescriptorType::SampledTexture, ShaderStage::Fragment, 2});
    auto layoutWithUnusedDescriptors = device->createPipelineLayout(layoutWithUnusedSlots);
    assert(layoutWithUnusedDescriptors);

    RenderPipelineDesc renderPipelineWithUnusedSlotsDesc = renderPipelineDesc;
    renderPipelineWithUnusedSlotsDesc.layout = layoutWithUnusedDescriptors.get();
    const auto renderPipelineWithUnusedSlotsReport =
        device->validateRenderPipeline(renderPipelineWithUnusedSlotsDesc);
    assert(renderPipelineWithUnusedSlotsReport.success);
    assert(renderPipelineWithUnusedSlotsReport.descriptorBindingCount == 2u);
    assert(renderPipelineWithUnusedSlotsReport.descriptorSetCount == 2u);
    assert(renderPipelineWithUnusedSlotsReport.descriptorTableCount == 2u);
    auto renderPipelineWithUnusedSlots =
        device->createRenderPipeline(renderPipelineWithUnusedSlotsDesc);
    assert(renderPipelineWithUnusedSlots);

    auto unusedSlotEncoder = device->createCommandEncoder({"PipelineUnusedDescriptors"});
    auto unusedSlotPass = unusedSlotEncoder->beginRenderPass({"Backbuffer"});
    unusedSlotPass->setPipeline(*renderPipelineWithUnusedSlots);
    unusedSlotPass->bindDescriptorSet(0, *frameSet);
    unusedSlotPass->bindDescriptorTable(1, *materialTable);
    const auto drawWithOnlyReflectedDescriptors = unusedSlotPass->drawIndexed(3, 1, 0, 0, 0);
    assert(drawWithOnlyReflectedDescriptors.success);
    unusedSlotPass->end();
    auto unusedSlotCommands = unusedSlotEncoder->finish();
    assert(unusedSlotCommands->commandCount() == 6u);

    auto missingDescriptorEncoder = device->createCommandEncoder({"MissingPipelineDescriptors"});
    auto missingDescriptorPass = missingDescriptorEncoder->beginRenderPass({"Backbuffer"});
    missingDescriptorPass->setPipeline(*renderPipeline);
    const auto drawWithoutDescriptors = missingDescriptorPass->drawIndexed(3, 1, 0, 0, 0);
    assert(!drawWithoutDescriptors.success);
    assert(drawWithoutDescriptors.diagnostics.find("descriptor") != std::pmr::string::npos);
    assert(drawWithoutDescriptors.recordedCommandCount == 2u);
    missingDescriptorPass->bindDescriptorSet(0, *frameSet);
    const auto drawWithoutTable = missingDescriptorPass->drawIndexed(3, 1, 0, 0, 0);
    assert(!drawWithoutTable.success);
    assert(drawWithoutTable.diagnostics.find("descriptor table") != std::pmr::string::npos);
    assert(drawWithoutTable.recordedCommandCount == 3u);
    missingDescriptorPass->bindDescriptorTable(1, *materialTable);
    const auto drawAfterDescriptors = missingDescriptorPass->drawIndexed(3, 1, 0, 0, 0);
    assert(drawAfterDescriptors.success);
    missingDescriptorPass->end();
    auto missingDescriptorCommands = missingDescriptorEncoder->finish();
    assert(missingDescriptorCommands->commandCount() == 6u);

    ComputePipelineDesc computePipelineDesc;
    computePipelineDesc.label = "SkinningPipeline";
    computePipelineDesc.layout = pipelineLayout.get();
    computePipelineDesc.shaderPackage = &shaderPackage;
    computePipelineDesc.computeShader = {"rhi_pipeline_contract.slang",
        "csMain",
        ShaderStage::Compute,
        ShaderSourceLanguage::Slang,
        1,
        1,
        1};
    const auto computePipelineReport = device->validateComputePipeline(computePipelineDesc);
    assert(computePipelineReport.success);
    assert(computePipelineReport.shaderPackageValidated);
    assert(computePipelineReport.reflectionMatched);
    assert(computePipelineReport.entrypointCount == 1u);
    assert(computePipelineReport.descriptorBindingCount == 0u);
    assert(computePipelineReport.descriptorLayoutCount == 1u);
    assert(computePipelineReport.descriptorSetCount == 1u);
    assert(computePipelineReport.descriptorTableCount == 1u);
    auto computePipeline = device->createComputePipeline(computePipelineDesc);
    assert(computePipeline);
    assert(computePipeline->layout() == pipelineLayout.get());
    assert(computePipeline->shaderSourceLanguage() == ShaderSourceLanguage::Slang);
    const auto& computePipelineMetadata = computePipeline->validationReport();
    assert(computePipelineMetadata.success);
    assert(computePipelineMetadata.entrypointCount == computePipelineReport.entrypointCount);
    assert(computePipelineMetadata.descriptorBindingCount
        == computePipelineReport.descriptorBindingCount);
    assert(computePipelineMetadata.descriptorSetCount == 1u);
    assert(computePipelineMetadata.descriptorTableCount == 1u);
    assert(computePipelineMetadata.pushConstantBytes == 128u);
    assert(computePipelineMetadata.shaderPackageValidated);
    assert(computePipelineMetadata.reflectionMatched);
    assert(!computePipelineMetadata.nativeHandleExposedToUpperLayer);
    const auto& computeReflection = computePipeline->shaderReflection();
    assert(computeReflection.descriptorBindings.empty());
    assert(computeReflection.pushConstantByteCount == 128u);

    PipelineLayoutDesc computeOnlyLayoutDesc = layoutDesc;
    computeOnlyLayoutDesc.descriptorLayouts.clear();
    computeOnlyLayoutDesc.descriptorSets.clear();
    auto computeOnlyLayout = device->createPipelineLayout(computeOnlyLayoutDesc);
    assert(computeOnlyLayout);
    ComputePipelineDesc computeOnlyPipelineDesc = computePipelineDesc;
    computeOnlyPipelineDesc.layout = computeOnlyLayout.get();
    const auto computeOnlyPipelineReport = device->validateComputePipeline(computeOnlyPipelineDesc);
    assert(computeOnlyPipelineReport.success);
    assert(computeOnlyPipelineReport.descriptorBindingCount == 0u);
    assert(computeOnlyPipelineReport.descriptorSetCount == 0u);
    assert(computeOnlyPipelineReport.descriptorTableCount == 1u);
    auto computeOnlyPipeline = device->createComputePipeline(computeOnlyPipelineDesc);
    assert(computeOnlyPipeline);
    assert(computeOnlyPipeline->shaderReflection().descriptorBindings.empty());

    RenderPipelineDesc renderPipelineWithComputeOnlyLayoutDesc = renderPipelineDesc;
    renderPipelineWithComputeOnlyLayoutDesc.layout = computeOnlyLayout.get();
    const auto renderPipelineWithComputeOnlyLayoutReport =
        device->validateRenderPipeline(renderPipelineWithComputeOnlyLayoutDesc);
    assert(!renderPipelineWithComputeOnlyLayoutReport.success);
    assert(renderPipelineWithComputeOnlyLayoutReport.diagnostics.find("descriptor layout")
        != std::pmr::string::npos);

    auto computeOnlyEncoder = device->createCommandEncoder({"ComputeOnlyReflection"});
    auto computeOnlyPass = computeOnlyEncoder->beginComputePass({"ComputeOnlyReflection"});
    computeOnlyPass->setPipeline(*computeOnlyPipeline);
    computeOnlyPass->bindDescriptorTable(1, *materialTable);
    const auto computeOnlyDispatch = computeOnlyPass->dispatch(1, 1, 1);
    assert(computeOnlyDispatch.success);
    computeOnlyPass->end();
    auto computeOnlyCommands = computeOnlyEncoder->finish();
    assert(computeOnlyCommands->commandCount() == 5u);

    ComputePipelineDesc computePipelineWithUnusedSlotsDesc = computePipelineDesc;
    computePipelineWithUnusedSlotsDesc.layout = layoutWithUnusedDescriptors.get();
    const auto computePipelineWithUnusedSlotsReport =
        device->validateComputePipeline(computePipelineWithUnusedSlotsDesc);
    assert(computePipelineWithUnusedSlotsReport.success);
    assert(computePipelineWithUnusedSlotsReport.descriptorBindingCount == 0u);
    assert(computePipelineWithUnusedSlotsReport.descriptorSetCount == 2u);
    assert(computePipelineWithUnusedSlotsReport.descriptorTableCount == 2u);
    auto computePipelineWithUnusedSlots =
        device->createComputePipeline(computePipelineWithUnusedSlotsDesc);
    assert(computePipelineWithUnusedSlots);

    auto unusedComputeSlotEncoder =
        device->createCommandEncoder({"PipelineUnusedComputeDescriptors"});
    auto unusedComputeSlotPass =
        unusedComputeSlotEncoder->beginComputePass({"PipelineUnusedComputeDescriptors"});
    unusedComputeSlotPass->setPipeline(*computePipelineWithUnusedSlots);
    unusedComputeSlotPass->bindDescriptorTable(1, *materialTable);
    const auto dispatchWithOnlyReflectedComputeDescriptors =
        unusedComputeSlotPass->dispatch(1, 1, 1);
    assert(dispatchWithOnlyReflectedComputeDescriptors.success);
    unusedComputeSlotPass->end();
    auto unusedComputeSlotCommands = unusedComputeSlotEncoder->finish();
    assert(unusedComputeSlotCommands->commandCount() == 5u);

    auto missingComputeDescriptorEncoder =
        device->createCommandEncoder({"MissingComputePipelineDescriptors"});
    auto missingComputeDescriptorPass =
        missingComputeDescriptorEncoder->beginComputePass({"MissingComputePipelineDescriptors"});
    missingComputeDescriptorPass->setPipeline(*computePipeline);
    missingComputeDescriptorPass->bindDescriptorSet(0, *frameSet);
    const auto dispatchWithoutTable = missingComputeDescriptorPass->dispatch(1, 1, 1);
    assert(dispatchWithoutTable.success);
    missingComputeDescriptorPass->bindDescriptorTable(1, *materialTable);
    const auto dispatchAfterDescriptors = missingComputeDescriptorPass->dispatch(1, 1, 1);
    assert(dispatchAfterDescriptors.success);
    missingComputeDescriptorPass->end();
    auto missingComputeDescriptorCommands = missingComputeDescriptorEncoder->finish();
    assert(missingComputeDescriptorCommands->commandCount() == 7u);

    auto encoder = device->createCommandEncoder({"PipelineCommandFrame"});
    auto pass = encoder->beginRenderPass({"Backbuffer"});
    pass->setPipeline(*renderPipeline);
    pass->bindDescriptorSet(0, *frameSet);
    pass->bindDescriptorTable(1, *materialTable);
    pass->pushConstants(ShaderStage::Vertex, 0, 128, kPushData);
    pass->drawIndexed(3, 1, 0, 0, 0);
    pass->end();
    auto commands = encoder->finish();
    assert(commands->commandCount() == 7u);

    auto invalidArena = device->createDescriptorArena({"InvalidPipelineDescriptorArena", 4, 8});
    assert(invalidArena);
    DescriptorLayoutDesc mismatchedFrameLayoutForBinding = frameLayout;
    mismatchedFrameLayoutForBinding.bindings[0].type = DescriptorType::StorageBuffer;
    auto wrongFrameSet = invalidArena->allocateDescriptorSet({
        "WrongFrameSet",
        mismatchedFrameLayoutForBinding,
    });
    assert(wrongFrameSet);
    auto wrongMaterialTable = invalidArena->allocateDescriptorTable({
        "MaterialTextureTable",
        DescriptorType::StorageBuffer,
        ShaderStage::Vertex | ShaderStage::Fragment | ShaderStage::Compute,
        4,
    });
    assert(wrongMaterialTable);

    auto bindBeforePipelineEncoder = device->createCommandEncoder({"BindBeforePipeline"});
    auto bindBeforePipelinePass = bindBeforePipelineEncoder->beginRenderPass({"Backbuffer"});
    const auto bindSetBeforePipeline = bindBeforePipelinePass->bindDescriptorSet(0, *frameSet);
    assert(!bindSetBeforePipeline.success);
    assert(bindSetBeforePipeline.diagnostics.find("pipeline") != std::pmr::string::npos);
    assert(bindSetBeforePipeline.recordedCommandCount == 1u);
    bindBeforePipelinePass->setPipeline(*renderPipeline);
    bindBeforePipelinePass->end();
    assert(bindBeforePipelineEncoder->finish());

    auto invalidBindingEncoder = device->createCommandEncoder({"InvalidDescriptorBinding"});
    auto invalidBindingPass = invalidBindingEncoder->beginRenderPass({"Backbuffer"});
    invalidBindingPass->setPipeline(*renderPipeline);
    const auto wrongSetBind = invalidBindingPass->bindDescriptorSet(0, *wrongFrameSet);
    assert(!wrongSetBind.success);
    assert(wrongSetBind.diagnostics.find("descriptor set") != std::pmr::string::npos);
    assert(wrongSetBind.recordedCommandCount == 2u);
    const auto missingSetBind = invalidBindingPass->bindDescriptorSet(7, *frameSet);
    assert(!missingSetBind.success);
    assert(missingSetBind.diagnostics.find("descriptor set slot") != std::pmr::string::npos);
    const auto wrongTableBind = invalidBindingPass->bindDescriptorTable(1, *wrongMaterialTable);
    assert(!wrongTableBind.success);
    assert(wrongTableBind.diagnostics.find("descriptor table") != std::pmr::string::npos);
    invalidBindingPass->end();
    assert(invalidBindingEncoder->finish());

    auto wrongNamedTableArena = device->createDescriptorArena({
        "WrongNamedTableDescriptorArena",
        1,
        4,
    });
    assert(wrongNamedTableArena);
    auto wrongNamedMaterialTable = wrongNamedTableArena->allocateDescriptorTable({
        "WrongNamedMaterialTextureTable",
        DescriptorType::SampledTexture,
        ShaderStage::Vertex | ShaderStage::Fragment | ShaderStage::Compute,
        4,
    });
    assert(wrongNamedMaterialTable);
    auto wrongNamedTableEncoder = device->createCommandEncoder({"WrongNamedDescriptorTable"});
    auto wrongNamedTablePass = wrongNamedTableEncoder->beginRenderPass({"Backbuffer"});
    wrongNamedTablePass->setPipeline(*renderPipeline);
    const auto wrongNamedTableBind =
        wrongNamedTablePass->bindDescriptorTable(1, *wrongNamedMaterialTable);
    assert(!wrongNamedTableBind.success);
    assert(wrongNamedTableBind.diagnostics.find("descriptor table name") != std::pmr::string::npos);
    assert(wrongNamedTableBind.recordedCommandCount == 2u);
    wrongNamedTablePass->end();
    assert(wrongNamedTableEncoder->finish());

    auto staleArena = device->createDescriptorArena({"StalePipelineDescriptorArena", 2, 5});
    assert(staleArena);
    auto staleFrameSet = staleArena->allocateDescriptorSet({
        "StaleFrameSet",
        frameLayout,
    });
    assert(staleFrameSet);
    auto staleMaterialTable = staleArena->allocateDescriptorTable({
        "MaterialTextureTable",
        DescriptorType::SampledTexture,
        ShaderStage::Vertex | ShaderStage::Fragment | ShaderStage::Compute,
        4,
    });
    assert(staleMaterialTable);
    assert(staleFrameSet
            ->write({
                0,
                DescriptorType::UniformBuffer,
                frameBuffer->id(),
                DescriptorResourceKind::Buffer,
            })
            .success);
    assert(staleMaterialTable
            ->write({
                0,
                DescriptorType::SampledTexture,
                materialTextureView->id(),
                DescriptorResourceKind::TextureView,
            })
            .success);
    staleArena->reset();

    auto staleSetBindEncoder = device->createCommandEncoder({"StaleDescriptorSetBind"});
    auto staleSetBindPass = staleSetBindEncoder->beginRenderPass({"Backbuffer"});
    staleSetBindPass->setPipeline(*renderPipeline);
    const auto staleSetBind = staleSetBindPass->bindDescriptorSet(0, *staleFrameSet);
    assert(!staleSetBind.success);
    assert(staleSetBind.diagnostics.find("reset") != std::pmr::string::npos);
    assert(staleSetBind.recordedCommandCount == 2u);
    staleSetBindPass->end();
    assert(staleSetBindEncoder->finish());

    auto staleTableBindEncoder = device->createCommandEncoder({"StaleDescriptorTableBind"});
    auto staleTableBindPass = staleTableBindEncoder->beginRenderPass({"Backbuffer"});
    staleTableBindPass->setPipeline(*renderPipeline);
    const auto staleTableBind = staleTableBindPass->bindDescriptorTable(1, *staleMaterialTable);
    assert(!staleTableBind.success);
    assert(staleTableBind.diagnostics.find("reset") != std::pmr::string::npos);
    assert(staleTableBind.recordedCommandCount == 2u);
    staleTableBindPass->end();
    assert(staleTableBindEncoder->finish());

    auto danglingSetArena = device->createDescriptorArena({
        "DanglingSetResourceDescriptorArena",
        2,
        5,
    });
    assert(danglingSetArena);
    auto danglingFrameSet = danglingSetArena->allocateDescriptorSet({
        "DanglingFrameSet",
        frameLayout,
    });
    assert(danglingFrameSet);
    auto danglingMaterialTable = danglingSetArena->allocateDescriptorTable({
        "MaterialTextureTable",
        DescriptorType::SampledTexture,
        ShaderStage::Vertex | ShaderStage::Fragment | ShaderStage::Compute,
        4,
    });
    assert(danglingMaterialTable);
    auto destroyedFrameBuffer = device->createBuffer({
        "DestroyedFrameConstants",
        256,
        ResourceUsage::UniformBuffer | ResourceUsage::CopyDst,
        MemoryUsage::CpuToGpu,
        ResourceState::Undefined,
    });
    assert(destroyedFrameBuffer);
    assert(danglingFrameSet
            ->write({
                0,
                DescriptorType::UniformBuffer,
                destroyedFrameBuffer->id(),
                DescriptorResourceKind::Buffer,
            })
            .success);
    assert(danglingMaterialTable
            ->write({
                0,
                DescriptorType::SampledTexture,
                materialTextureView->id(),
                DescriptorResourceKind::TextureView,
            })
            .success);
    auto danglingSetEncoder = device->createCommandEncoder({"DanglingDescriptorSetResource"});
    auto danglingSetPass = danglingSetEncoder->beginRenderPass({"Backbuffer"});
    danglingSetPass->setPipeline(*renderPipeline);
    assert(danglingSetPass->bindDescriptorSet(0, *danglingFrameSet).success);
    assert(danglingSetPass->bindDescriptorTable(1, *danglingMaterialTable).success);
    destroyedFrameBuffer.reset();
    const auto staleSetRebind = danglingSetPass->bindDescriptorSet(0, *danglingFrameSet);
    assert(!staleSetRebind.success);
    assert(staleSetRebind.diagnostics.find("RHI-DESCRIPTOR-RESOURCE-NOT-LIVE")
        != std::pmr::string::npos);
    assert(staleSetRebind.recordedCommandCount == 4u);
    const auto drawWithDestroyedSetResource = danglingSetPass->drawIndexed(3, 1, 0, 0, 0);
    assert(!drawWithDestroyedSetResource.success);
    assert(drawWithDestroyedSetResource.diagnostics.find("resource") != std::pmr::string::npos);
    assert(drawWithDestroyedSetResource.recordedCommandCount == 4u);
    danglingSetPass->end();
    assert(danglingSetEncoder->finish());

    auto danglingTableArena = device->createDescriptorArena({
        "DanglingTableResourceDescriptorArena",
        2,
        5,
    });
    assert(danglingTableArena);
    auto liveFrameSet = danglingTableArena->allocateDescriptorSet({
        "LiveFrameSetForDanglingTable",
        frameLayout,
    });
    assert(liveFrameSet);
    auto danglingParentTable = danglingTableArena->allocateDescriptorTable({
        "MaterialTextureTable",
        DescriptorType::SampledTexture,
        ShaderStage::Vertex | ShaderStage::Fragment | ShaderStage::Compute,
        4,
    });
    assert(danglingParentTable);
    auto destroyedMaterialTexture = device->createTexture({
        "DestroyedMaterialTexture",
        {32, 32, 1},
        TextureFormat::Bgra8Unorm,
        TextureUsage::Sampled | TextureUsage::CopyDst,
        MemoryUsage::GpuOnly,
        ResourceState::Undefined,
    });
    assert(destroyedMaterialTexture);
    auto danglingMaterialView = device->createTextureView(
        *destroyedMaterialTexture, {"DanglingMaterialView", TextureFormat::Bgra8Unorm});
    assert(danglingMaterialView);
    assert(liveFrameSet
            ->write({
                0,
                DescriptorType::UniformBuffer,
                frameBuffer->id(),
                DescriptorResourceKind::Buffer,
            })
            .success);
    assert(danglingParentTable
            ->write({
                0,
                DescriptorType::SampledTexture,
                danglingMaterialView->id(),
                DescriptorResourceKind::TextureView,
            })
            .success);
    auto danglingTableEncoder = device->createCommandEncoder({"DanglingDescriptorTableResource"});
    auto danglingTablePass = danglingTableEncoder->beginRenderPass({"Backbuffer"});
    danglingTablePass->setPipeline(*renderPipeline);
    assert(danglingTablePass->bindDescriptorSet(0, *liveFrameSet).success);
    assert(danglingTablePass->bindDescriptorTable(1, *danglingParentTable).success);
    destroyedMaterialTexture.reset();
    const auto staleTableRebind = danglingTablePass->bindDescriptorTable(1, *danglingParentTable);
    assert(!staleTableRebind.success);
    assert(staleTableRebind.diagnostics.find("parent texture") != std::pmr::string::npos);
    assert(staleTableRebind.recordedCommandCount == 4u);
    const auto drawWithDestroyedTableParent = danglingTablePass->drawIndexed(3, 1, 0, 0, 0);
    assert(!drawWithDestroyedTableParent.success);
    assert(
        drawWithDestroyedTableParent.diagnostics.find("parent texture") != std::pmr::string::npos);
    assert(drawWithDestroyedTableParent.recordedCommandCount == 4u);
    danglingTablePass->end();
    assert(danglingTableEncoder->finish());

    auto staleComputeEncoder = device->createCommandEncoder({"StaleComputeDescriptors"});
    auto staleComputePass = staleComputeEncoder->beginComputePass({"StaleComputeDescriptors"});
    assert(staleComputePass && staleComputePass->setPipeline(*computePipeline).success);
    const auto staleComputeSet = staleComputePass->bindDescriptorSet(0u, *danglingFrameSet);
    assert(!staleComputeSet.success && staleComputeSet.recordedCommandCount == 2u);
    assert(staleComputeSet.diagnostics.find("RHI-DESCRIPTOR-RESOURCE-NOT-LIVE")
        != std::pmr::string::npos);
    const auto staleComputeTable = staleComputePass->bindDescriptorTable(1u, *danglingParentTable);
    assert(!staleComputeTable.success && staleComputeTable.recordedCommandCount == 2u);
    assert(staleComputeTable.diagnostics.find("parent texture") != std::pmr::string::npos);
    assert(staleComputePass->bindDescriptorSet(0u, *frameSet).success);
    assert(staleComputePass->end().success);
    assert(staleComputeEncoder->finish());

    auto objectArena = device->createDescriptorArena({"DescriptorObjectLifetime", 2u, 5u});
    auto objectSet = objectArena->allocateDescriptorSet({"ObjectSet", frameLayout});
    auto objectTable = objectArena->allocateDescriptorTable({"MaterialTextureTable",
        DescriptorType::SampledTexture,
        ShaderStage::Vertex | ShaderStage::Fragment | ShaderStage::Compute,
        4u});
    assert(objectSet && objectTable);
    assert(objectSet
            ->write({0u,
                DescriptorType::UniformBuffer,
                frameBuffer->id(),
                DescriptorResourceKind::Buffer})
            .success);
    assert(objectTable
            ->write({0u,
                DescriptorType::SampledTexture,
                materialTextureView->id(),
                DescriptorResourceKind::TextureView})
            .success);
    auto objectEncoder = device->createCommandEncoder({"DestroyedSetBeforeDraw"});
    auto objectPass = objectEncoder->beginRenderPass({"Backbuffer"});
    assert(objectPass && objectPass->setPipeline(*renderPipeline).success);
    assert(objectPass->bindDescriptorSet(0u, *objectSet).success);
    assert(objectPass->bindDescriptorTable(1u, *objectTable).success);
    objectSet.reset();
    const auto objectDraw = objectPass->drawIndexed(3u, 1u, 0u, 0, 0u);
    assert(!objectDraw.success && objectDraw.recordedCommandCount == 4u);
    assert(objectDraw.diagnostics.find("RHI-DESCRIPTOR-OBJECT-NOT-LIVE") == 0u);
    auto indirectArguments = device->createBuffer({"DescriptorLifetimeIndirect",
        32u,
        ResourceUsage::IndirectBuffer,
        MemoryUsage::CpuToGpu,
        ResourceState::ShaderRead});
    assert(indirectArguments);
    const auto objectIndirect = objectPass->drawIndexedIndirect(*indirectArguments, 0u, 1u, 20u);
    assert(!objectIndirect.success && objectIndirect.recordedCommandCount == 4u);
    assert(objectIndirect.diagnostics.find("RHI-DESCRIPTOR-OBJECT-NOT-LIVE") == 0u);
    const auto objectCounted = objectPass->drawIndexedIndirectCount(
        *indirectArguments, 0u, *indirectArguments, 24u, 1u, 20u);
    assert(!objectCounted.success && objectCounted.recordedCommandCount == 4u);
    assert(objectCounted.diagnostics.find("RHI-DESCRIPTOR-OBJECT-NOT-LIVE") == 0u);
    assert(objectPass->end().success && objectEncoder->finish());
    auto objectComputeEncoder = device->createCommandEncoder({"DestroyedTableBeforeDispatch"});
    auto objectCompute = objectComputeEncoder->beginComputePass({"DestroyedTableBeforeDispatch"});
    assert(objectCompute && objectCompute->setPipeline(*computePipeline).success);
    assert(objectCompute->bindDescriptorTable(1u, *objectTable).success);
    objectTable.reset();
    const auto objectDispatch = objectCompute->dispatch(1u, 1u, 1u);
    assert(!objectDispatch.success && objectDispatch.recordedCommandCount == 3u);
    assert(objectDispatch.diagnostics.find("RHI-DESCRIPTOR-OBJECT-NOT-LIVE") == 0u);
    const auto objectIndirectDispatch = objectCompute->dispatchIndirect(*indirectArguments, 0u);
    assert(!objectIndirectDispatch.success && objectIndirectDispatch.recordedCommandCount == 3u);
    assert(objectIndirectDispatch.diagnostics.find("RHI-DESCRIPTOR-OBJECT-NOT-LIVE") == 0u);
    assert(objectCompute->end().success && objectComputeEncoder->finish());

    auto revisionEncoder = device->createCommandEncoder({"ChangedTableBeforeDispatch"});
    auto revisionPass = revisionEncoder->beginComputePass({"ChangedTableBeforeDispatch"});
    assert(revisionPass && revisionPass->setPipeline(*computePipeline).success);
    assert(revisionPass->bindDescriptorSet(0u, *frameSet).success);
    const auto tableBound = revisionPass->bindDescriptorTable(1u, *materialTable);
    assert(tableBound.success);
    assert(materialTable
            ->write({0u,
                DescriptorType::SampledTexture,
                materialTextureView->id(),
                DescriptorResourceKind::TextureView})
            .success);
    const auto changedDispatch = revisionPass->dispatch(1u, 1u, 1u);
    const auto changedIndirect = revisionPass->dispatchIndirect(*indirectArguments, 0u);
    for (const auto* failure : {&changedDispatch, &changedIndirect}) {
        assert(
            !failure->success && failure->recordedCommandCount == tableBound.recordedCommandCount);
        assert(failure->diagnostics.find("RHI-DESCRIPTOR-CONTENT-CHANGED") == 0u);
    }
    assert(revisionPass->bindDescriptorTable(1u, *materialTable).success);
    assert(revisionPass->dispatch(1u, 1u, 1u).success);
    assert(revisionPass->end().success && revisionEncoder->finish());

    auto computeEncoder = device->createCommandEncoder({"PipelineComputeFrame"});
    auto computePass = computeEncoder->beginComputePass({"PipelineComputePass"});
    computePass->setPipeline(*computePipeline);
    computePass->bindDescriptorSet(0, *frameSet);
    computePass->bindDescriptorTable(1, *materialTable);
    computePass->dispatch(1, 1, 1);
    computePass->end();
    auto computeCommands = computeEncoder->finish();
    assert(computeCommands->commandCount() == 6u);

    auto invalidComputeBindingEncoder =
        device->createCommandEncoder({"InvalidComputeDescriptorBinding"});
    auto invalidComputeBindingPass =
        invalidComputeBindingEncoder->beginComputePass({"InvalidComputeDescriptorBindingPass"});
    invalidComputeBindingPass->setPipeline(*computePipeline);
    const auto wrongComputeTableBind =
        invalidComputeBindingPass->bindDescriptorTable(1, *wrongMaterialTable);
    assert(!wrongComputeTableBind.success);
    assert(wrongComputeTableBind.diagnostics.find("descriptor table") != std::pmr::string::npos);
    invalidComputeBindingPass->end();
    assert(invalidComputeBindingEncoder->finish());

    RenderPipelineDesc invalidShaderDesc = renderPipelineDesc;
    invalidShaderDesc.vertexShader = {"legacy.spv", "vsMain", ShaderStage::Vertex};
    const auto invalidShaderReport = device->validateRenderPipeline(invalidShaderDesc);
    assert(!invalidShaderReport.success);
    assert(invalidShaderReport.diagnostics.find("Slang") != std::pmr::string::npos);
    auto invalidPipeline = device->createRenderPipeline(invalidShaderDesc);
    assert(!invalidPipeline);

    RenderPipelineDesc missingMslPipelineDesc = renderPipelineDesc;
    missingMslPipelineDesc.shaderPackage = &missingMslArtifact;
    const auto missingMslReport = device->validateRenderPipeline(missingMslPipelineDesc);
    assert(!missingMslReport.success);
    assert(missingMslReport.diagnostics.find("MSL") != std::pmr::string::npos);

    auto oversizedShaderPackage = shaderPackage;
    oversizedShaderPackage.reflection.pushConstantByteCount = 4096;
    RenderPipelineDesc oversizedShaderPipelineDesc = renderPipelineDesc;
    oversizedShaderPipelineDesc.shaderPackage = &oversizedShaderPackage;
    const auto oversizedShaderReport = device->validateRenderPipeline(oversizedShaderPipelineDesc);
    assert(!oversizedShaderReport.success);
    assert(oversizedShaderReport.diagnostics.find("push-constant") != std::pmr::string::npos);

    DescriptorLayoutDesc mismatchedFrameLayout = frameLayout;
    mismatchedFrameLayout.bindings[0].type = DescriptorType::StorageBuffer;
    PipelineLayoutDesc mismatchedDescriptorLayoutDesc = layoutDesc;
    mismatchedDescriptorLayoutDesc.descriptorLayouts.clear();
    mismatchedDescriptorLayoutDesc.descriptorLayouts.push_back(mismatchedFrameLayout);
    auto mismatchedDescriptorPipelineLayout =
        device->createPipelineLayout(mismatchedDescriptorLayoutDesc);
    assert(mismatchedDescriptorPipelineLayout);
    RenderPipelineDesc mismatchedDescriptorPipelineDesc = renderPipelineDesc;
    mismatchedDescriptorPipelineDesc.layout = mismatchedDescriptorPipelineLayout.get();
    const auto mismatchedDescriptorReport =
        device->validateRenderPipeline(mismatchedDescriptorPipelineDesc);
    assert(!mismatchedDescriptorReport.success);
    assert(
        mismatchedDescriptorReport.diagnostics.find("descriptor layout") != std::pmr::string::npos);

    auto tableIndexOutOfRangePackage = shaderPackage;
    tableIndexOutOfRangePackage.reflection.descriptorBindings[1].binding = 4;
    RenderPipelineDesc tableIndexOutOfRangePipelineDesc = renderPipelineDesc;
    tableIndexOutOfRangePipelineDesc.shaderPackage = &tableIndexOutOfRangePackage;
    const auto tableIndexOutOfRangeReport =
        device->validateRenderPipeline(tableIndexOutOfRangePipelineDesc);
    assert(!tableIndexOutOfRangeReport.success);
    assert(tableIndexOutOfRangeReport.diagnostics.find("descriptor") != std::pmr::string::npos);

    PipelineLayoutDesc oversizedPushConstant = layoutDesc;
    oversizedPushConstant.pushConstantRanges[0].sizeBytes = 4096;
    assert(!device->createPipelineLayout(oversizedPushConstant));

    // --- Instanced vertex input (VertexInputRate::Instance) ----------------
    // A pipeline with an Instance-rate vertex buffer must validate successfully
    // and mark vertexInputStateValidated. This is the L2 contract for hardware
    // instancing: an Instance-rate buffer advances once per draw instance.
    {
        RenderPipelineDesc instancedPipelineDesc = renderPipelineDesc;
        VertexBufferLayoutDesc perVertexLayout;
        perVertexLayout.slot = 0;
        perVertexLayout.strideBytes = sizeof(float) * 7;
        perVertexLayout.inputRate = VertexInputRate::Vertex;
        perVertexLayout.attributes.push_back({0, 0, VertexFormat::Float32x3});
        perVertexLayout.attributes.push_back({1, sizeof(float) * 3, VertexFormat::Float32x4});
        VertexBufferLayoutDesc perInstanceLayout;
        perInstanceLayout.slot = 1;
        perInstanceLayout.strideBytes = sizeof(float) * 17;
        perInstanceLayout.inputRate = VertexInputRate::Instance;
        perInstanceLayout.attributes.push_back({2, 0, VertexFormat::Float32x4});
        perInstanceLayout.attributes.push_back({3, sizeof(float) * 16, VertexFormat::Float32x1});
        instancedPipelineDesc.vertexBuffers.push_back(perVertexLayout);
        instancedPipelineDesc.vertexBuffers.push_back(perInstanceLayout);
        const auto instancedReport = device->validateRenderPipeline(instancedPipelineDesc);
        assert(instancedReport.success);
        const auto stateValidation = validateRenderPipelineState(instancedPipelineDesc);
        assert(stateValidation.success);
        assert(stateValidation.vertexInputStateValidated);
        auto instancedPipeline = device->createRenderPipeline(instancedPipelineDesc);
        assert(instancedPipeline);
        const auto& instancedMetadata = instancedPipeline->validationReport();
        assert(instancedMetadata.success);
    }

    // A pipeline with an out-of-range inputRate must fail validation with an
    // unsupported-state diagnostic (deterministic failure, no silent fallback).
    {
        RenderPipelineDesc badRatePipelineDesc = renderPipelineDesc;
        VertexBufferLayoutDesc badRateLayout;
        badRateLayout.slot = 0;
        badRateLayout.strideBytes = sizeof(float) * 3;
        badRateLayout.inputRate = static_cast<VertexInputRate>(999);
        badRateLayout.attributes.push_back({0, 0, VertexFormat::Float32x3});
        badRatePipelineDesc.vertexBuffers.push_back(badRateLayout);
        const auto badStateValidation = validateRenderPipelineState(badRatePipelineDesc);
        assert(!badStateValidation.success);
        assert(badStateValidation.unsupportedStateDiagnosed);
        assert(badStateValidation.diagnostics.find("input rate") != std::pmr::string::npos);
        const auto badReport = device->validateRenderPipeline(badRatePipelineDesc);
        assert(!badReport.success);
    }

    return 0;
}

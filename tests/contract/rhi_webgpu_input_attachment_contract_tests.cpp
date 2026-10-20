#include "rhi/backends/webgpu/webgpu_backend_objects.h"
#include "rhi/backends/webgpu/webgpu_binding_space_lowering.h"
#include "rhi/semantics/subpass_input_lowering.h"
#include "shader/shader_reflection_cache.h"

#include <cassert>
#include <memory>

namespace {

class TestTextureView final : public arp::rhi::GpuTextureView {
public:
    explicit TestTextureView(arp::rhi::GpuResourceId parent)
        : id_(arp::rhi::allocateGpuResourceId()), parent_(parent)
    {
        desc_.usage = arp::rhi::TextureUsage::Sampled;
    }

    arp::rhi::GpuResourceId id() const override { return id_; }

    arp::rhi::GpuResourceId textureId() const override { return parent_; }

    const arp::rhi::TextureViewDesc& desc() const override { return desc_; }

private:
    arp::rhi::GpuResourceId id_;
    arp::rhi::GpuResourceId parent_;
    arp::rhi::TextureViewDesc desc_;
};

} // namespace

int main()
{
    using namespace arp::rhi;
    using namespace arp::rhi::semantics;
    using namespace arp::rhi_webgpu;

    // An unavailable WGSL target must carry the compiler reason while its
    // target path remains empty. This is the artifact contract consumed by
    // loadWebGpuBindingSpaceLowering; a stale path would make deployment
    // treat a failed target as executable.
    const std::pmr::string unavailableArtifactYaml = R"yaml(
moduleName: AtomicCull
entryPoints:
  - name: cullMain
    stage: Compute
webGpuBindingSpaceStatus: Unavailable
webGpuBindingSpaceUnavailableReason: "error[E36107]: InterlockedAdd is unavailable for WGSL"
targets:
  spirv: "atomic_cull.spv"
  metal: "atomic_cull.metal"
  wgsl: null
)yaml";
    const auto unavailableArtifact =
        arp::shader::ShaderReflectionCache::loadFromYaml(unavailableArtifactYaml);
    assert(unavailableArtifact.success);
    assert(unavailableArtifact.module.wgslPath.empty());
    assert(unavailableArtifact.module.reflection.webGpuBindingSpaceStatus == "Unavailable");
    assert(unavailableArtifact.module.reflection.webGpuBindingSpaceUnavailableReason.find("E36107")
        != std::pmr::string::npos);

    DescriptorLayoutDesc inputLayout;
    inputLayout.name = "WebGpuInputAttachment";
    inputLayout.bindings.push_back({0, DescriptorType::InputAttachment, ShaderStage::Fragment});

    PipelineLayoutDesc pipelineLayout;
    pipelineLayout.descriptorLayouts.push_back(inputLayout);
    pipelineLayout.descriptorSets.push_back({0, inputLayout.name});
    // The logical L2 layout is valid. WebGPU's physical type is selected
    // only when a shader package lowering artifact is validated/consumed.
    assert(createWebGpuRhiPipelineLayout(pipelineLayout));

    auto registry = std::make_shared<WebGpuRhiResourceRegistry>();
    WebGpuRhiAllocation parentAllocation;
    parentAllocation.success = true;
    auto parent = createWebGpuRhiTexture(allocateGpuResourceId(),
        {"InputParent",
            {8u, 8u, 1u},
            TextureFormat::Rgba8Unorm,
            TextureUsage::Sampled,
            MemoryUsage::GpuOnly,
            ResourceState::ShaderRead},
        parentAllocation,
        WebGpuRhiNativePayloadPolicy{},
        registry);
    assert(parent);
    registry->registerTexture(*parent);
    TestTextureView textureView(parent->id());
    registry->registerTextureView(textureView);

    auto arena = createWebGpuRhiDescriptorArena({"InputAttachmentArena", 2, 2}, registry, {});
    assert(arena);
    const auto allocation = arena->allocateSet(inputLayout);
    assert(allocation.success);

    auto descriptorSet = arena->allocateDescriptorSet({
        "InputAttachmentSet",
        inputLayout,
    });
    assert(descriptorSet);
    assert(descriptorSet->isCurrentAllocation());
    assert(descriptorSet->stats().writtenBindings == 0u);
    arena->reset();
    assert(!descriptorSet->isCurrentAllocation());
    assert(!descriptorSet->hasWrittenBinding(0));
    assert(descriptorSet->stats().writtenBindings == 0u);
    assert(!descriptorSet->validateBinding(0).success);
    const auto staleSetWrite = descriptorSet->write({
        0,
        DescriptorType::InputAttachment,
        {1},
        DescriptorResourceKind::TextureView,
    });
    assert(!staleSetWrite.success);
    assert(staleSetWrite.diagnostics.find("reset") != std::pmr::string::npos);

    auto activeArena =
        createWebGpuRhiDescriptorArena({"ActiveInputAttachmentArena", 1, 1}, registry, {});
    assert(activeArena);
    auto activeSet = activeArena->allocateDescriptorSet({
        "ActiveInputAttachmentSet",
        inputLayout,
    });
    assert(activeSet);
    const auto activeInputWrite = activeSet->write({
        0,
        DescriptorType::InputAttachment,
        textureView.id(),
        DescriptorResourceKind::TextureView,
    });
    assert(!activeInputWrite.success);
    assert(activeInputWrite.diagnostics.find("lowering") != std::pmr::string::npos);

    const auto loweredInputWrite = activeSet->write({
        0,
        DescriptorType::InputAttachment,
        textureView.id(),
        DescriptorResourceKind::TextureView,
        ResourceState::ShaderRead,
        {true, DescriptorType::SampledTexture, 4, 1u},
    });
    assert(loweredInputWrite.success);
    parent.reset();
    const auto orphanInput = activeSet->validateBinding(0u);
    assert(!orphanInput.success);
    assert(orphanInput.diagnostics.find("RHI-DESCRIPTOR-PARENT-NOT-LIVE") == 0u);

    DescriptorTableDesc tableDesc;
    tableDesc.label = "InputAttachmentTable";
    tableDesc.type = DescriptorType::InputAttachment;
    tableDesc.stages = ShaderStage::Fragment;
    tableDesc.capacity = 1;
    auto tableArena =
        createWebGpuRhiDescriptorArena({"InputAttachmentTableArena", 1, 1}, nullptr, {});
    assert(tableArena);
    auto rejectedInputAttachmentTable = tableArena->allocateDescriptorTable(tableDesc);
    assert(!rejectedInputAttachmentTable);

    tableDesc.type = DescriptorType::SampledTexture;
    auto descriptorTable = tableArena->allocateDescriptorTable(tableDesc);
    assert(descriptorTable);
    assert(descriptorTable->isCurrentAllocation());
    tableArena->reset();
    assert(!descriptorTable->isCurrentAllocation());
    assert(!descriptorTable->hasWrittenDescriptor(0));
    assert(descriptorTable->stats().writtenDescriptors == 0u);
    assert(!descriptorTable->validateDescriptor(0).success);
    const auto staleTableWrite = descriptorTable->write({
        0,
        DescriptorType::SampledTexture,
        {1},
        DescriptorResourceKind::TextureView,
    });
    assert(!staleTableWrite.success);
    assert(staleTableWrite.diagnostics.find("reset") != std::pmr::string::npos);

    // The generated artifact and the logical package must agree on the
    // attachment identity and explicit physical type before the sampled-
    // texture lowering can reach native pipeline creation.
    ShaderReflectionDesc reflection;
    reflection.descriptorBindings.push_back({
        0,
        0,
        DescriptorType::InputAttachment,
        ShaderStage::Fragment,
        1,
        4,
    });
    WebGpuBindingSpaceLowering lowering;
    lowering.contractVersion = "WebGpuBindingSpace/5";
    lowering.physicalGroupCount = 1;
    lowering.artifactBindings.push_back({
        0,
        0,
        DescriptorType::InputAttachment,
        ShaderStage::None,
        1,
        4,
    });
    lowering.entries.push_back({
        "SceneInput",
        0,
        0,
        0,
        0,
        true,
        DescriptorType::SampledTexture,
        SubpassInputLoadSemantics::CurrentFragment,
        true,
        false,
    });
    DescriptorLayoutDesc reflectedLayout;
    reflectedLayout.name = "ReflectedInputAttachment";
    reflectedLayout.bindings.push_back({0, DescriptorType::InputAttachment, ShaderStage::Fragment});
    PipelineLayoutDesc reflectedPipelineLayout;
    reflectedPipelineLayout.descriptorLayouts.push_back(reflectedLayout);
    reflectedPipelineLayout.descriptorSets.push_back({0, reflectedLayout.name});
    assert(validateWebGpuBindingSpaceLowering(
        reflectedPipelineLayout, reflection, lowering, UINT32_MAX)
            .success);
    auto missingPhysicalGroupCount = lowering;
    missingPhysicalGroupCount.physicalGroupCount = 0;
    assert(!validateWebGpuBindingSpaceLowering(
        reflectedPipelineLayout, reflection, missingPhysicalGroupCount, UINT32_MAX)
            .success);
    auto trailingPhysicalGroup = lowering;
    trailingPhysicalGroup.physicalGroupCount = 2;
    assert(!validateWebGpuBindingSpaceLowering(
        reflectedPipelineLayout, reflection, trailingPhysicalGroup, UINT32_MAX)
            .success);

    // Depth input attachments use the same logical route but must preserve
    // depth texture metadata through the generated artifact and descriptor
    // layout. A color sampled-texture lowering is not interchangeable here.
    auto depthReflection = reflection;
    depthReflection.descriptorBindings[0].depthTexture = true;
    auto depthLowering = lowering;
    depthLowering.artifactBindings[0].depthTexture = true;
    depthLowering.entries[0].depthTexture = true;
    auto depthLayout = reflectedPipelineLayout;
    depthLayout.descriptorLayouts[0].bindings[0].depthTexture = true;
    assert(
        validateWebGpuBindingSpaceLowering(depthLayout, depthReflection, depthLowering, UINT32_MAX)
            .success);
    auto depthColorLayout = depthLayout;
    depthColorLayout.descriptorLayouts[0].bindings[0].depthTexture = false;
    assert(!validateWebGpuBindingSpaceLowering(
        depthColorLayout, depthReflection, depthLowering, UINT32_MAX)
            .success);
    auto depthColorLowering = depthLowering;
    depthColorLowering.entries[0].depthTexture = false;
    assert(!validateWebGpuBindingSpaceLowering(
        depthLayout, depthReflection, depthColorLowering, UINT32_MAX)
            .success);

    auto mismatchedReflection = reflection;
    mismatchedReflection.descriptorBindings[0].subpassAttachmentIndex = 5;
    assert(!validateWebGpuBindingSpaceLowering(
        reflectedPipelineLayout, mismatchedReflection, lowering, UINT32_MAX)
            .success);

    auto mismatchedArrayCount = reflection;
    mismatchedArrayCount.descriptorBindings[0].arrayCount = 2;
    assert(!validateWebGpuBindingSpaceLowering(
        reflectedPipelineLayout, mismatchedArrayCount, lowering, UINT32_MAX)
            .success);

    // WebGPU binding arrays are executable only when the generated physical
    // capacity is carried through the lowering artifact. A scalar physical
    // entry must fail closed instead of silently dropping non-zero elements.
    auto descriptorArrayReflection = reflection;
    descriptorArrayReflection.descriptorBindings[0].arrayCount = 2;
    auto descriptorArrayLowering = lowering;
    descriptorArrayLowering.artifactBindings[0].arrayCount = 2;
    descriptorArrayLowering.entries[0].capacity = 2;
    auto descriptorArrayLayout = reflectedPipelineLayout;
    descriptorArrayLayout.descriptorLayouts[0].bindings[0].arrayCount = 2;
    assert(validateWebGpuBindingSpaceLowering(
        descriptorArrayLayout, descriptorArrayReflection, descriptorArrayLowering, UINT32_MAX)
            .success);

    auto extraArtifactBinding = lowering;
    extraArtifactBinding.artifactBindings.push_back({
        0,
        1,
        DescriptorType::SampledTexture,
        ShaderStage::None,
        1,
        UINT32_MAX,
    });
    assert(!validateWebGpuBindingSpaceLowering(
        reflectedPipelineLayout, reflection, extraArtifactBinding, UINT32_MAX)
            .success);

    auto inactiveArtifactBinding = lowering;
    inactiveArtifactBinding.artifactBindings.push_back({
        0,
        1,
        DescriptorType::SampledTexture,
        ShaderStage::None,
        1,
        UINT32_MAX,
    });
    inactiveArtifactBinding.entries.push_back({
        "UnusedInput",
        0,
        1,
        0,
        1,
        false,
        DescriptorType::SampledTexture,
        SubpassInputLoadSemantics::CurrentFragment,
        false,
        false,
    });
    assert(validateWebGpuBindingSpaceLowering(
        reflectedPipelineLayout, reflection, inactiveArtifactBinding, UINT32_MAX)
            .success);

    auto extraPhysicalBinding = lowering;
    extraPhysicalBinding.entries.push_back({
        "UnusedInput",
        0,
        1,
        0,
        1,
        true,
        DescriptorType::SampledTexture,
        SubpassInputLoadSemantics::CurrentFragment,
        true,
        false,
    });
    assert(!validateWebGpuBindingSpaceLowering(
        reflectedPipelineLayout, reflection, extraPhysicalBinding, UINT32_MAX)
            .success);

    auto samplerLowering = lowering;
    samplerLowering.entries[0].physicalType = DescriptorType::Sampler;
    assert(!validateWebGpuBindingSpaceLowering(
        reflectedPipelineLayout, reflection, samplerLowering, UINT32_MAX)
            .success);

    auto missingLoadSemantics = lowering;
    missingLoadSemantics.entries[0].hasLoadSemantics = false;
    assert(!validateWebGpuBindingSpaceLowering(
        reflectedPipelineLayout, reflection, missingLoadSemantics, UINT32_MAX)
            .success);

    // Logical descriptor-set identity is independent of the physical WGSL
    // group. Set 4 is deliberately lowered to group 1, which is valid when
    // the artifact and the pipeline layout agree on that mapping.
    ShaderReflectionDesc highLogicalSetReflection;
    highLogicalSetReflection.descriptorBindings.push_back({
        4,
        0,
        DescriptorType::InputAttachment,
        ShaderStage::Fragment,
        1,
        4,
    });
    auto highLogicalSetLowering = lowering;
    highLogicalSetLowering.physicalGroupCount = 2;
    highLogicalSetLowering.artifactBindings[0].set = 4;
    highLogicalSetLowering.entries[0].set = 4;
    highLogicalSetLowering.entries[0].group = 1;
    DescriptorLayoutDesc highLogicalSetLayout = reflectedLayout;
    highLogicalSetLayout.name = "HighLogicalSetInputAttachment";
    PipelineLayoutDesc highLogicalSetPipelineLayout;
    highLogicalSetPipelineLayout.descriptorLayouts.push_back(highLogicalSetLayout);
    highLogicalSetPipelineLayout.descriptorSets.push_back({4, highLogicalSetLayout.name});
    assert(validateWebGpuBindingSpaceLowering(
        highLogicalSetPipelineLayout, highLogicalSetReflection, highLogicalSetLowering, UINT32_MAX)
            .success);

    auto pushGroupCollision = highLogicalSetLowering;
    highLogicalSetPipelineLayout.pushConstantRanges.push_back({ShaderStage::Vertex, 0, 16});
    pushGroupCollision.physicalGroupCount = 4;
    assert(!validateWebGpuBindingSpaceLowering(
        highLogicalSetPipelineLayout, highLogicalSetReflection, pushGroupCollision, 1)
            .success);
    assert(validateWebGpuBindingSpaceLowering(
        highLogicalSetPipelineLayout, highLogicalSetReflection, pushGroupCollision, 3)
            .success);

    auto groupCapacityExceeded = lowering;
    groupCapacityExceeded.entries[0].group = kWebGpuGuaranteedBindGroupCount;
    assert(!validateWebGpuBindingSpaceLowering(
        reflectedPipelineLayout, reflection, groupCapacityExceeded, UINT32_MAX)
            .success);

    return 0;
}

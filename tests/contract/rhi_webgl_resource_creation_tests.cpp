#include "../fixtures/webgl_profile_probe_fixture.h"
#include "rhi/backends/webgl/webgl_device.h"
#include "rhi/backends/webgl/webgl_resources.h"
#include "rhi/backends/webgl/webgl_resources_internal.h"
#include "rhi/frontend/pipeline_state_copy.h"
#include "rhi/semantics/attachment_content.h"
#include "rhi/semantics/graphics_workload_requirements.h"
#include "rhi/semantics/ordered_graphics_dependency.h"
#include "rhi/semantics/webgl_canvas_lease.h"
#include "rhi/semantics/webgl_descriptor_creation.h"
#include "rhi/semantics/webgl_prepared_commands.h"

#include <cassert>
#include <cstring>
#include <type_traits>

using namespace arp;
using namespace arp::rhi;
using namespace arp::rhi::semantics;

int main()
{
    ResourceMemoryBarrierDesc dependency{GpuResourceId{1},
        PipelineStage::ColorAttachmentOutput,
        PipelineStage::ColorAttachmentOutput,
        ResourceAccess::ColorAttachmentWrite,
        ResourceAccess::ColorAttachmentWrite};
    for (auto backend : {Backend::Null,
             Backend::Vulkan,
             Backend::Metal,
             Backend::WebGPU,
             Backend::WebGL,
             static_cast<Backend>(255)})
        assert((OrderedGraphicsDependency::memory(
                    backend, 1, true, dependency, ResourceState::ColorAttachment)
                   == nullptr)
            == (backend == Backend::WebGL));
    assert(OrderedGraphicsDependency::memory(
        Backend::WebGL, 0, true, dependency, ResourceState::ColorAttachment));
    assert(OrderedGraphicsDependency::memory(
        Backend::WebGL, 1, false, dependency, ResourceState::ColorAttachment));
    assert(OrderedGraphicsDependency::memory(
        Backend::WebGL, 1, true, dependency, ResourceState::ShaderWrite));
    auto invalidDependency = dependency;
    invalidDependency.destinationStages = PipelineStage::ComputeShader;
    assert(OrderedGraphicsDependency::memory(
        Backend::WebGL, 1, true, invalidDependency, ResourceState::ColorAttachment));
    invalidDependency = dependency;
    invalidDependency.resource = {};
    assert(OrderedGraphicsDependency::memory(
        Backend::WebGL, 1, true, invalidDependency, ResourceState::ColorAttachment));
    GraphicsWorkloadRequirements workload;
    workload.contractVersion = 1;
    workload.colorAttachments = 1;
    workload.textureWidth = workload.textureHeight = 64;
    workload.vertexAttributes = 4;
    workload.vertexUniformVectors = workload.fragmentUniformVectors = 8;
    workload.fragmentTextureUnits = 1;
    const GraphicsWorkloadFacts facts{1, 64, 64, 4, 8, 8, 0, 1, 1};
    assert(!GraphicsWorkloadRequirementsContract::orderedSubset(workload));
    assert(!GraphicsWorkloadRequirementsContract::assessLimits(workload, facts));
    assert(GraphicsWorkloadRequirementsContract::validate({}));
    for (auto field : {&GraphicsWorkloadRequirements::colorAttachments,
             &GraphicsWorkloadRequirements::textureWidth,
             &GraphicsWorkloadRequirements::textureHeight,
             &GraphicsWorkloadRequirements::vertexAttributes,
             &GraphicsWorkloadRequirements::vertexUniformVectors,
             &GraphicsWorkloadRequirements::fragmentUniformVectors,
             &GraphicsWorkloadRequirements::vertexTextureUnits,
             &GraphicsWorkloadRequirements::fragmentTextureUnits}) {
        auto excessive = workload;
        ++(excessive.*field);
        assert(GraphicsWorkloadRequirementsContract::assessLimits(excessive, facts));
    }
    auto combined = workload;
    combined.vertexTextureUnits = 1;
    auto separateFacts = facts;
    separateFacts.vertexTextureUnits = 1;
    assert(GraphicsWorkloadRequirementsContract::assessLimits(combined, separateFacts));
    for (auto field : {&GraphicsWorkloadRequirements::compute,
             &GraphicsWorkloadRequirements::storageBuffers,
             &GraphicsWorkloadRequirements::storageTextures,
             &GraphicsWorkloadRequirements::hdrAttachments,
             &GraphicsWorkloadRequirements::inputAttachments,
             &GraphicsWorkloadRequirements::aliasHeaps,
             &GraphicsWorkloadRequirements::timelineSync,
             &GraphicsWorkloadRequirements::gpuCompletion,
             &GraphicsWorkloadRequirements::readback,
             &GraphicsWorkloadRequirements::externalSharing}) {
        auto unsupported = workload;
        unsupported.*field = true;
        assert(GraphicsWorkloadRequirementsContract::orderedSubset(unsupported));
    }
    for (auto backend : {Backend::Null,
             Backend::Vulkan,
             Backend::Metal,
             Backend::WebGPU,
             Backend::WebGL,
             static_cast<Backend>(255)})
        assert((GraphicsWorkloadRequirementsContract::orderedLowering(backend) == nullptr)
            == (backend == Backend::WebGL));

    constexpr std::array extensionFields{&WebGlRuntimeExtensions::uint32Indices,
        &WebGlRuntimeExtensions::instancing,
        &WebGlRuntimeExtensions::vertexArrayObjects,
        &WebGlRuntimeExtensions::depthTextures,
        &WebGlRuntimeExtensions::derivatives};
    auto extensionProbe = test::WebGlProfileProbeFixture::baseline();
    extensionProbe.extensions = {true, true, true, true, true};
    extensionProbe.enabledExtensions = extensionProbe.extensions;
    extensionProbe.formats.depthTexture = WebGlFormatQueryOutcome::Observed;
    extensionProbe.formats.depthTextureFramebufferComplete = true;
    const auto extensionProfile = WebGlRuntimeProfile::fromProbe(extensionProbe);
    WebGlExtensionExecutionRequest execution{1, true, extensionProbe.extensions};
    WebGlExtensionOptIn consent{1, extensionProbe.extensions};
    for (const auto backend : {Backend::Null,
             Backend::Vulkan,
             Backend::Metal,
             Backend::WebGPU,
             Backend::WebGL,
             static_cast<Backend>(255)}) {
        const auto decision =
            WebGlExtensionExecutionContract::lower(backend, execution, extensionProfile, consent);
        assert(decision.executable == (backend == Backend::WebGL));
        assert(decision.native.any() == decision.executable);
    }
    assert(WebGlExtensionExecutionContract::lower(Backend::WebGL, {}, extensionProfile, {})
            .executable);
    assert(!WebGlExtensionExecutionContract::lower(
        Backend::WebGL, {}, WebGlRuntimeProfile::fromProbe({}), {})
            .executable);
    for (const auto field : extensionFields) {
        auto missingConsent = consent;
        missingConsent.extensions.*field = false;
        assert(!WebGlExtensionExecutionContract::lower(
            Backend::WebGL, execution, extensionProfile, missingConsent)
                .executable);
        for (bool unavailable : {false, true}) {
            auto missing = test::WebGlProfileProbeFixture::baseline();
            missing.extensions = extensionProbe.extensions;
            missing.enabledExtensions = extensionProbe.enabledExtensions;
            missing.enabledExtensions.*field = false;
            if (unavailable)
                missing.extensions.*field = false;
            const auto decision = WebGlExtensionExecutionContract::lower(
                Backend::WebGL, execution, WebGlRuntimeProfile::fromProbe(missing), consent);
            assert(!decision.executable && !decision.native.any() && decision.diagnostic);
        }
    }
    TextureDesc sampledDepth;
    sampledDepth.extent = {32, 32, 1};
    sampledDepth.format = TextureFormat::Depth24Stencil8;
    sampledDepth.usage = TextureUsage::Sampled | TextureUsage::RenderAttachment;
    sampledDepth.initialState = ResourceState::DepthAttachment;
    auto missingDepthFacts = extensionProbe;
    missingDepthFacts.formats.depthTexture = WebGlFormatQueryOutcome::NotQueried;
    missingDepthFacts.formats.depthTextureFramebufferComplete = false;
    const auto missingDepthProfile = WebGlRuntimeProfile::fromProbe(missingDepthFacts);
    assert(WebGlResourceCreationContract::texture(missingDepthProfile, sampledDepth, consent));
    assert(!WebGlExtensionExecutionContract::lower(
        Backend::WebGL, execution, missingDepthProfile, consent)
            .executable);
    extensionProbe.formats.depthTexture = WebGlFormatQueryOutcome::Observed;
    extensionProbe.formats.depthTextureFramebufferComplete = true;
    auto depthProfile = WebGlRuntimeProfile::fromProbe(extensionProbe);
    assert(!WebGlResourceCreationContract::texture(depthProfile, sampledDepth, consent));
    assert(WebGlResourceCreationContract::texture(depthProfile, sampledDepth));
    sampledDepth.usage = sampledDepth.usage | TextureUsage::CopyDst;
    assert(WebGlResourceCreationContract::texture(depthProfile, sampledDepth, consent));
    execution.prototypeOptIn = false;
    assert(!WebGlExtensionExecutionContract::lower(
        Backend::WebGL, execution, extensionProfile, consent)
            .executable);
    execution.prototypeOptIn = true;
    execution.contractVersion = 2;
    assert(
        WebGlExtensionExecutionContract::lower(Backend::WebGL, execution, extensionProfile, consent)
            .executable);
    execution.contractVersion = 3;
    assert(!WebGlExtensionExecutionContract::lower(
        Backend::WebGL, execution, extensionProfile, consent)
            .executable);
    static_assert(std::is_base_of_v<GpuDevice, webgl::WebGlDevice>);
    WebGlDeviceCompositionRequest device{1,
        true,
        1024 * 1024,
        {1, "host-contract-pipelines", 8},
        {1, "host-contract-descriptors", 32}};
    for (const auto backend : {Backend::Null,
             Backend::Vulkan,
             Backend::Metal,
             Backend::WebGPU,
             Backend::WebGL,
             static_cast<Backend>(255)})
        assert((WebGlDeviceCompositionContract::validate(backend, device) == nullptr)
            == (backend == Backend::WebGL));
    device.prototypeOptIn = false;
    assert(WebGlDeviceCompositionContract::validate(Backend::WebGL, device));
    device.prototypeOptIn = true;
    device.pipelines.capacity = 0;
    assert(WebGlDeviceCompositionContract::validate(Backend::WebGL, device));
    device.pipelines.capacity = 8;
    const char* deviceDiagnostic = nullptr;
    assert(!webgl::WebGlDevice::create({}, device, {}, deviceDiagnostic) && deviceDiagnostic);
    RenderPipelineDesc pipeline;
    pipeline.colorFormat = TextureFormat::Rgba8Unorm;
    pipeline.colorTargets.push_back({TextureFormat::Rgba8Unorm});
    pipeline.dynamicViewportScissor = true;
    WebGlPipelineCreationRequest creation{1, {1, 1024, 4096}, {}};
    for (const auto backend : {Backend::Null,
             Backend::Vulkan,
             Backend::Metal,
             Backend::WebGPU,
             Backend::WebGL,
             static_cast<Backend>(255)})
        assert((WebGlPipelineCreationContract::state(backend, pipeline, creation) == nullptr)
            == (backend == Backend::WebGL));
    auto rejectedState = [&](auto change) {
        auto copy = RenderPipelineStateCopy::copy(pipeline);
        change(copy);
        assert(WebGlPipelineCreationContract::state(Backend::WebGL, copy, creation));
    };
    rejectedState([](auto& d) {
        d.colorTargets.clear();
    });
    rejectedState([](auto& d) {
        d.colorTargets[0].colorBlend.operation = BlendOp::Min;
    });
    rejectedState([](auto& d) {
        d.colorTargets[0].alphaBlend.sourceFactor = static_cast<BlendFactor>(255);
    });
    rejectedState([](auto& d) {
        d.depthStencil.depthCompare = static_cast<CompareOp>(255);
    });
    rejectedState([](auto& d) {
        d.depthStencil.front.passOp = static_cast<StencilOp>(255);
    });
    rejectedState([](auto& d) {
        d.depthStencil.depthBiasClamp = 1;
    });
    rejectedState([](auto& d) {
        d.depthStencil.stencilReadMask = 256;
    });
    rejectedState([](auto& d) {
        d.depthStencil.stencilEnabled = true;
    });
    rejectedState([](auto& d) {
        d.multisample.sampleMask = 1;
    });
    rejectedState([](auto& d) {
        d.multisample.alphaToCoverageEnabled = true;
    });
    rejectedState([](auto& d) {
        d.hasViewport = true;
    });
    rejectedState([](auto& d) {
        d.dynamicViewportScissor = false;
    });
    rejectedState([](auto& d) {
        d.topology = PrimitiveTopology::TriangleStrip;
    });
    auto stencil = RenderPipelineStateCopy::copy(pipeline);
    stencil.depthFormat = stencil.depthStencil.format = TextureFormat::Depth24Stencil8;
    stencil.depthStencil.stencilEnabled = true;
    assert(WebGlPipelineCreationContract::state(Backend::WebGL, stencil, creation));
    creation.stencilReference = 7;
    assert(!WebGlPipelineCreationContract::state(Backend::WebGL, stencil, creation));
    creation.stencilReference = 256;
    assert(WebGlPipelineCreationContract::state(Backend::WebGL, stencil, creation));
    creation.stencilReference.reset();
    DescriptorArenaDesc arena{core::pmr::String(&core::defaultCpuMemoryResource()), 2, 8};
    for (const auto backend : {Backend::Null,
             Backend::Vulkan,
             Backend::Metal,
             Backend::WebGPU,
             Backend::WebGL,
             static_cast<Backend>(255)})
        assert((WebGlDescriptorCreationContract::arena(backend, arena) == nullptr)
            == (backend == Backend::WebGL));
    DescriptorBindingDesc binding;
    binding.type = DescriptorType::UniformBuffer;
    binding.stages = ShaderStage::Vertex;
    assert(!WebGlDescriptorCreationContract::binding(binding));
    binding.arrayCount = 2;
    assert(WebGlDescriptorCreationContract::binding(binding));
    binding.arrayCount = 1;
    binding.type = DescriptorType::StorageBuffer;
    assert(WebGlDescriptorCreationContract::binding(binding));
    static_assert(!std::is_default_constructible_v<webgl::WebGlResourceFactoryKey>);
    WebGlProgramCreationRequest program{1, 256, 4096};
    for (const auto backend : {Backend::Null,
             Backend::Vulkan,
             Backend::Metal,
             Backend::WebGPU,
             Backend::WebGL,
             static_cast<Backend>(255)})
        assert((WebGlProgramCreationContract::validate(backend, program) == nullptr)
            == (backend == Backend::WebGL));
    program.maxInterfaceNameBytes = UINT32_MAX;
    assert(WebGlProgramCreationContract::validate(Backend::WebGL, program));
    program = {1, 256, 0};
    assert(WebGlProgramCreationContract::validate(Backend::WebGL, program));
    program = {0, 256, 4096};
    assert(WebGlProgramCreationContract::validate(Backend::WebGL, program));
    WebGlResourceOwnershipRequest ownership;
    assert(WebGlResourceCreationContract::ownership(Backend::WebGL, ownership));
    ownership.contractVersion = ownership.budget.contractVersion = 1;
    ownership.budget.capacity.cpuMetadataBytes = 1024 * 1024;
    ownership.budget.capacity.cpuUploadBytes = 1024 * 1024;
    ownership.budget.capacity.objectCount = 32;
    for (const auto backend : {Backend::Null,
             Backend::Vulkan,
             Backend::Metal,
             Backend::WebGPU,
             Backend::WebGL,
             static_cast<Backend>(255)})
        assert((WebGlResourceCreationContract::ownership(backend, ownership) == nullptr)
            == (backend == Backend::WebGL));
    auto invalid = ownership;
    invalid.budget.capacity.objectCount = std::uint64_t{UINT32_MAX} + 1;
    assert(WebGlResourceCreationContract::ownership(Backend::WebGL, invalid));
    invalid = ownership;
    invalid.budget.capacity.cpuUploadBytes = 0;
    assert(WebGlResourceCreationContract::ownership(Backend::WebGL, invalid));
    const char* diagnostic = nullptr;
    assert(!webgl::WebGlResources::create({}, ownership, {}, {}, diagnostic));
    assert(std::strcmp(diagnostic, "RHI-WEBGL1-RESOURCE-ORIGINAL-CONTEXT") == 0);

    BufferDesc buffer;
    buffer.sizeBytes = 64;
    for (const auto role :
        {ResourceUsage::VertexBuffer, ResourceUsage::IndexBuffer, ResourceUsage::UniformBuffer}) {
        buffer.usage = role | ResourceUsage::CopyDst;
        assert(!WebGlResourceCreationContract::buffer(buffer));
    }
    for (const auto role : {ResourceUsage::None,
             ResourceUsage::StorageBuffer,
             ResourceUsage::CopyDst,
             ResourceUsage::VertexBuffer | ResourceUsage::IndexBuffer,
             static_cast<ResourceUsage>(UINT32_MAX)}) {
        buffer.usage = role;
        assert(WebGlResourceCreationContract::buffer(buffer));
    }
    buffer.usage = ResourceUsage::VertexBuffer;
    buffer.sizeBytes = UINT64_MAX;
    assert(WebGlResourceCreationContract::buffer(buffer));
    buffer.sizeBytes = 16;
    buffer.memory = MemoryUsage::Transient;
    assert(WebGlResourceCreationContract::buffer(buffer));
    buffer.memory = MemoryUsage::GpuOnly;
    buffer.initialState = ResourceState::ShaderWrite;
    assert(WebGlResourceCreationContract::buffer(buffer));

    const auto profile = WebGlRuntimeProfile::fromProbe(test::WebGlProfileProbeFixture::baseline());
    TextureDesc texture;
    texture.extent = {4, 4, 1};
    texture.format = TextureFormat::Rgba8Unorm;
    texture.usage = TextureUsage::Sampled | TextureUsage::CopyDst;
    assert(!WebGlResourceCreationContract::texture(profile, texture));
    // New composed resource contract adds cold host uploads; the frozen P0
    // planner still grants neither the CopyDst role nor execution authority.
    assert(!WebGlGraphicsRequestContract::texture(profile, 1, texture).compatible);
    auto depth = texture;
    depth.format = TextureFormat::Depth24Stencil8;
    depth.usage = TextureUsage::RenderAttachment;
    assert(!WebGlResourceCreationContract::texture(profile, depth));
    auto probe = test::WebGlProfileProbeFixture::baseline();
    probe.formats.packedDepthBits = 16;
    assert(WebGlResourceCreationContract::texture(WebGlRuntimeProfile::fromProbe(probe), depth));
    depth.usage = TextureUsage::Sampled;
    assert(WebGlResourceCreationContract::texture(profile, depth));
    texture.extent.width = 65;
    assert(WebGlResourceCreationContract::texture(profile, texture));
    texture.extent.width = 4;
    TextureViewDesc view;
    view.format = texture.format;
    view.usage = TextureUsage::Sampled;
    assert(!WebGlResourceCreationContract::view(texture, view));
    view.format = TextureFormat::Rgba8UnormSrgb;
    assert(WebGlResourceCreationContract::view(texture, view));
    view.format = texture.format;
    view.baseMipLevel = 1;
    assert(WebGlResourceCreationContract::view(texture, view));

    const std::array<std::uint8_t, 80> pixels{};
    WebGlColdTextureUpload upload;
    upload.contractVersion = 1;
    upload.extent = texture.extent;
    upload.bytesPerRow = 20;
    upload.bytes = pixels;
    assert(!WebGlResourceCreationContract::upload(texture, upload));
    upload.bytes = {pixels.data(), 79};
    assert(WebGlResourceCreationContract::upload(texture, upload));
    upload.bytes = pixels;
    upload.origin.width = 1;
    assert(WebGlResourceCreationContract::upload(texture, upload));
    upload.origin.width = 0;
    upload.bytesPerRow = UINT32_MAX;
    assert(WebGlResourceCreationContract::upload(texture, upload));
    upload.bytesPerRow = 20;
    upload.contractVersion = 0;
    assert(WebGlResourceCreationContract::upload(texture, upload));
    SamplerDesc sampler;
    assert(!WebGlGraphicsRequestContract::samplerState(sampler));
    sampler.compareOp = CompareOp::Less;
    assert(WebGlGraphicsRequestContract::samplerState(sampler));
    assert(AttachmentContentContract::afterStore(LoadOp::Clear, StoreOp::Store, false));
    assert(AttachmentContentContract::afterStore(LoadOp::Load, StoreOp::Store, true));
    assert(!AttachmentContentContract::afterStore(LoadOp::Load, StoreOp::Store, false));
    assert(!AttachmentContentContract::afterStore(LoadOp::DontCare, StoreOp::Store, true));
    for (auto load : {LoadOp::Clear, LoadOp::Load, LoadOp::DontCare})
        for (bool initialized : {false, true})
            assert(!AttachmentContentContract::afterStore(load, StoreOp::DontCare, initialized));
    SwapchainDesc canvas;
    canvas.surfaceName = "#canvas";
    canvas.extent = {64, 64};
    canvas.format = TextureFormat::Rgba8Unorm;
    canvas.imageCount = 0;
    WebGlContextAttributes attributes;
    assert(!WebGlCanvasLeaseContract::validate(
        Backend::WebGL, canvas, {64, 64}, "#canvas", attributes));
    for (auto backend : {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU})
        assert(
            WebGlCanvasLeaseContract::validate(backend, canvas, {64, 64}, "#canvas", attributes));
    canvas.imageCount = 1;
    assert(WebGlCanvasLeaseContract::validate(
        Backend::WebGL, canvas, {64, 64}, "#canvas", attributes));
    canvas.imageCount = 0;
    attributes.alpha = true;
    assert(WebGlCanvasLeaseContract::validate(
        Backend::WebGL, canvas, {64, 64}, "#canvas", attributes));
    attributes.alpha = false;
    assert(WebGlCanvasLeaseContract::validate(
        Backend::WebGL, canvas, {32, 64}, "#canvas", attributes));
    WebGlPreparedCommandsRequest commands;
    WebGlCanvasReuseRequest reuse;
    assert(WebGlCanvasReuseContract::validate(Backend::WebGL, reuse));
    reuse = {1, true, 8};
    for (const auto backend : {Backend::Null,
             Backend::Vulkan,
             Backend::Metal,
             Backend::WebGPU,
             Backend::WebGL,
             static_cast<Backend>(255)})
        assert((WebGlCanvasReuseContract::validate(backend, reuse) == nullptr)
            == (backend == Backend::WebGL));
    reuse.prototypeOptIn = false;
    assert(WebGlCanvasReuseContract::validate(Backend::WebGL, reuse));
    reuse.prototypeOptIn = true;
    reuse.maxLeases = 1;
    assert(WebGlCanvasReuseContract::validate(Backend::WebGL, reuse));
    reuse.maxLeases = UINT32_MAX;
    assert(!WebGlCanvasReuseContract::validate(Backend::WebGL, reuse));
    reuse.contractVersion = 2;
    assert(WebGlCanvasReuseContract::validate(Backend::WebGL, reuse));
    assert(WebGlPreparedCommandsContract::validate(Backend::WebGL, commands));
    commands.contractVersion = 1;
    commands.prototypeOptIn = true;
    commands.maxDraws = 2;
    commands.maxUniformBytes = 320;
    commands.maxAnalysisIndices = 6;
    commands.issue = {1, true, 4, 16, 16, 65536, 32};
    assert(!WebGlPreparedCommandsContract::validate(Backend::WebGL, commands));
    for (auto backend : {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU})
        assert(WebGlPreparedCommandsContract::validate(backend, commands));
    commands.contractVersion = 2;
    assert(WebGlPreparedCommandsContract::validate(Backend::WebGL, commands));
    commands.maxPipelines = 2;
    assert(!WebGlPreparedCommandsContract::validate(Backend::WebGL, commands));
    commands.contractVersion = 1;
    assert(WebGlPreparedCommandsContract::validate(Backend::WebGL, commands));
    commands.contractVersion = 2;
    commands.contractVersion = 3;
    assert(WebGlPreparedCommandsContract::validate(Backend::WebGL, commands));
    commands.maxTargets = commands.maxPasses = commands.maxTransitions = 2;
    assert(WebGlPreparedCommandsContract::validate(Backend::WebGL, commands));
    commands.issue.maxOperations = 6;
    commands.issue.maxSnapshotBytes = 24;
    assert(!WebGlPreparedCommandsContract::validate(Backend::WebGL, commands));
    commands.maxPasses = UINT32_MAX;
    assert(WebGlPreparedCommandsContract::validate(Backend::WebGL, commands));
    commands.maxPasses = 2;
    for (auto backend : {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU})
        assert(WebGlPreparedCommandsContract::validate(backend, commands));
    commands.prototypeOptIn = false;
    assert(WebGlPreparedCommandsContract::validate(Backend::WebGL, commands));
}

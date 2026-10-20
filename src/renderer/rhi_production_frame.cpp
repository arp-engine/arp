#include "renderer/rhi_production_frame.h"

#include "core/cpu_allocator.h"
#include "core/diagnostic_builder.h"
#include "renderer/shader_package_reflection.h"
#include "rhi/gpu_common.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace arp::renderer {

namespace {

constexpr std::uint32_t kSceneRuntimeDemoPushConstantBytes = 16u;

struct SceneRuntimeDemoPushConstants {
    float drawKind = 0.0f;
    float phaseSeconds = 0.0f;
    float padding[2]{};
};

static_assert(sizeof(SceneRuntimeDemoPushConstants) == kSceneRuntimeDemoPushConstantBytes,
    "scene runtime demo push constants must match Slang std430 layout");

RendererRhiProductionFramePrepareReport failPrepare(
    RendererRhiProductionFramePrepareReport report, const std::pmr::string& diagnostics)
{
    report.success = false;
    arp::core::appendDiagnostic(report.diagnostics, diagnostics);
    return report;
}

RendererRhiProductionFrameSubmitReport failSubmit(
    RendererRhiProductionFrameSubmitReport report, const std::pmr::string& diagnostics)
{
    report.success = false;
    arp::core::appendDiagnostic(report.diagnostics, diagnostics);
    return report;
}

const render_graph::RhiPassRecord* findGraphicsPass(const render_graph::RhiFramePlan& plan)
{
    for (const auto& pass : plan.passes) {
        if (pass.kind == render_graph::PassKind::Graphics && pass.drawCommandCount > 0) {
            return &pass;
        }
    }
    return nullptr;
}

std::pmr::string descriptorLayoutNameFor(const render_graph::RhiPassRecord& pass)
{
    if (pass.descriptorLayoutNameCount > 0 && !pass.descriptorLayoutNames[0].empty()) {
        return pass.descriptorLayoutNames[0];
    }
    return "RendererRhiFrameSet";
}

std::pmr::string shaderArtifactPath(
    const RendererRhiProductionFramePathDesc& desc, const char* relativePath)
{
    if (desc.shaderArtifactRoot.empty()) {
        return relativePath;
    }
    std::pmr::string path = desc.shaderArtifactRoot;
    const auto last = path.back();
    if (last != '/' && last != '\\') {
        path += '/';
    }
    path += relativePath;
    return path;
}

rhi::DescriptorLayoutDesc makeFrameDescriptorLayout(const render_graph::RhiPassRecord& pass)
{
    rhi::DescriptorLayoutDesc layout;
    layout.name = descriptorLayoutNameFor(pass);
    layout.bindings.push_back({
        0,
        rhi::DescriptorType::UniformBuffer,
        rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment,
    });
    return layout;
}

rhi::PipelineLayoutDesc makePipelineLayoutDesc(
    const rhi::DescriptorLayoutDesc& frameLayout, std::uint32_t pushConstantBytes)
{
    rhi::PipelineLayoutDesc desc;
    desc.descriptorLayouts.push_back(frameLayout);
    desc.descriptorSets.push_back({0, frameLayout.name});
    if (pushConstantBytes > 0) {
        desc.pushConstantRanges.push_back({
            rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment,
            0,
            pushConstantBytes,
        });
    }
    return desc;
}

rhi::ShaderPackageDesc makeShaderPackage(const RendererRhiProductionFramePathDesc& desc,
    const rhi::DescriptorLayoutDesc& frameLayout,
    std::uint32_t pushConstantBytes)
{
    rhi::ShaderPackageDesc shaderPackage;
    shaderPackage.label = desc.label + "ShaderPackage";
    shaderPackage.slangModulePath = "shaders/samples/clear.slang";
    shaderPackage.entrypoints.push_back({
        shaderPackage.slangModulePath,
        "vertexMain",
        rhi::ShaderStage::Vertex,
    });
    shaderPackage.entrypoints.push_back({
        shaderPackage.slangModulePath,
        "fragmentMain",
        rhi::ShaderStage::Fragment,
    });
    shaderPackage.artifacts.spirvPath = shaderArtifactPath(desc, "shaders/clear.spv");
    shaderPackage.artifacts.mslPath = shaderArtifactPath(desc, "shaders/clear.metal");
    shaderPackage.artifacts.wgslPath = shaderArtifactPath(desc, "shaders/clear.wgsl");
    shaderPackage.artifacts.reflectionPath =
        shaderArtifactPath(desc, "shaders/clear.reflection.yaml");
    shaderPackage.reflection.pushConstantByteCount = pushConstantBytes;
    if (pushConstantBytes > 0) {
        shaderPackage.reflection.pushConstantContractVersion = "AtomicFullBlockPush/1";
        shaderPackage.reflection.pushConstantStages = "VS|FS";
    }
    shaderPackage.reflection.descriptorBindings.push_back({
        0,
        0,
        rhi::DescriptorType::UniformBuffer,
        rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment,
    });
    if (!hydrateShaderPackageReflectionFromArtifact(shaderPackage)) {
        shaderPackage.generatedReflectionHydrated = false;
    }
    (void)frameLayout;
    return shaderPackage;
}

rhi::RenderPipelineDesc makeRenderPipelineDesc(const RendererRhiProductionFramePathDesc& pathDesc,
    const rhi::PipelineLayout& layout,
    const rhi::ShaderPackageDesc& shaderPackage)
{
    rhi::RenderPipelineDesc desc;
    desc.label = pathDesc.label + "Pipeline";
    desc.layout = &layout;
    desc.shaderPackage = &shaderPackage;
    desc.vertexShader = {
        shaderPackage.slangModulePath,
        "vertexMain",
        rhi::ShaderStage::Vertex,
    };
    desc.fragmentShader = {
        shaderPackage.slangModulePath,
        "fragmentMain",
        rhi::ShaderStage::Fragment,
    };
    desc.colorFormat = pathDesc.colorFormat;
    desc.hasViewport = true;
    desc.viewport = {
        0.0f,
        0.0f,
        static_cast<float>(pathDesc.colorExtent.width),
        static_cast<float>(pathDesc.colorExtent.height),
        0.0f,
        1.0f,
    };
    desc.hasScissor = true;
    desc.scissor = {
        0,
        0,
        pathDesc.colorExtent.width,
        pathDesc.colorExtent.height,
    };
    return desc;
}

bool hasFullSurfaceViewport(
    const rhi::RenderPipelineDesc& pipelineDesc, const rhi::Extent3D& extent)
{
    return pipelineDesc.hasViewport && pipelineDesc.viewport.x == 0.0f
        && pipelineDesc.viewport.y == 0.0f
        && pipelineDesc.viewport.width == static_cast<float>(extent.width)
        && pipelineDesc.viewport.height == static_cast<float>(extent.height)
        && pipelineDesc.viewport.minDepth == 0.0f && pipelineDesc.viewport.maxDepth == 1.0f;
}

bool hasFullSurfaceScissor(const rhi::RenderPipelineDesc& pipelineDesc, const rhi::Extent3D& extent)
{
    return pipelineDesc.hasScissor && pipelineDesc.scissor.x == 0 && pipelineDesc.scissor.y == 0
        && pipelineDesc.scissor.width == extent.width
        && pipelineDesc.scissor.height == extent.height;
}

bool commandSucceeded(const rhi::CommandValidationResult& result, std::pmr::string& diagnostics)
{
    if (result.success) {
        return true;
    }
    arp::core::appendDiagnostic(diagnostics, result.diagnostics);
    return false;
}

std::uint32_t pushConstantBytesFor(const render_graph::RhiPassRecord& pass)
{
    if (pass.pipelinePushConstantCapacityBytes > 0) {
        return pass.pipelinePushConstantCapacityBytes;
    }
    return pass.pushConstantSizeBytes;
}

std::uint32_t visualRoleForDraw(std::uint32_t drawOrdinal)
{
    return std::min(drawOrdinal, 2u);
}

std::uint32_t indexCountForVisualRole(std::uint32_t visualRole)
{
    if (visualRole == 0u) {
        return 18u;
    }
    return 6u;
}

void writeSceneRuntimeDemoPushConstants(
    std::array<std::uint8_t, 256>& target, std::uint32_t visualRole, std::uint32_t drawOrdinal)
{
    SceneRuntimeDemoPushConstants push;
    push.drawKind = static_cast<float>(visualRole);
    push.phaseSeconds = static_cast<float>(drawOrdinal) / 60.0f;
    std::memcpy(target.data(), &push, sizeof(push));
}

void markProceduralVisualRole(
    RendererRhiProductionFrameSubmitReport& report, std::uint32_t visualRole)
{
    if (visualRole == 0u) {
        report.proceduralSceneRuntime3DObjectDrawn = true;
    } else if (visualRole == 1u) {
        report.proceduralSceneRuntime2DSpriteDrawn = true;
    } else if (visualRole == 2u) {
        report.proceduralSceneRuntimeUiDrawn = true;
    }
}

} // namespace

RendererRhiProductionFramePath::RendererRhiProductionFramePath() = default;

RendererRhiProductionFramePath::RendererRhiProductionFramePath(
    const RendererRhiProductionFramePathDesc& desc)
    : desc_(desc)
{}

RendererRhiProductionFramePath::~RendererRhiProductionFramePath() = default;

RendererRhiProductionFramePrepareReport RendererRhiProductionFramePath::prepare(
    const RendererRhiFramePlanDesc& frame, rhi::GpuDevice& device)
{
    resetPreparedState();

    RendererRhiProductionFramePrepareReport report;
    report.usedRhiGpuDevice = true;

    if (desc_.colorExtent.width == 0 || desc_.colorExtent.height == 0) {
        return failPrepare(
            report, "renderer RHI production frame requires a non-zero color extent");
    }
    if (desc_.colorFormat == rhi::TextureFormat::Unknown) {
        return failPrepare(report, "renderer RHI production frame requires a color format");
    }
    if (desc_.maxDrawCommandsPerPass == 0) {
        return failPrepare(report, "renderer RHI production frame requires a bounded draw count");
    }

    RendererRhiFramePlanBridge bridge(desc_.framePlanBridge);
    framePlan_ = bridge.buildFramePlan(frame);
    report.framePlan = framePlan_;
    report.rendererFramePlanAccepted = framePlan_.success;
    if (!framePlan_.success) {
        return failPrepare(report, framePlan_.diagnostics);
    }

    const auto* pass = findGraphicsPass(framePlan_.plan);
    if (!pass) {
        return failPrepare(report, "renderer RHI production frame requires a graphics draw pass");
    }
    if (pass->drawCommandCount > desc_.maxDrawCommandsPerPass) {
        return failPrepare(
            report, "renderer RHI production frame draw count exceeds the bounded path");
    }
    if (pass->descriptorSetCount == 0) {
        return failPrepare(report, "renderer RHI production frame requires a descriptor set");
    }

    frameConstants_ = device.createBuffer({
        desc_.label + "FrameConstants",
        256,
        rhi::ResourceUsage::UniformBuffer | rhi::ResourceUsage::CopyDst,
        rhi::MemoryUsage::CpuToGpu,
        rhi::ResourceState::Undefined,
    });
    if (!desc_.useExternalColorTarget) {
        colorTarget_ = device.createTexture({
            desc_.label + "Color",
            desc_.colorExtent,
            desc_.colorFormat,
            rhi::TextureUsage::RenderAttachment | rhi::TextureUsage::CopySrc,
            rhi::MemoryUsage::GpuOnly,
            rhi::ResourceState::Undefined,
        });
    }
    if (!frameConstants_ || (!desc_.useExternalColorTarget && !colorTarget_)) {
        return failPrepare(report, "renderer RHI production frame resource creation failed");
    }
    report.rhiResourceCreated = true;
    report.resourceCount = desc_.useExternalColorTarget ? 1u : 2u;
    report.backendFallbacksHiddenFromUpperLayer = report.backendFallbacksHiddenFromUpperLayer
        && frameConstants_->allocation().backendFallbackHiddenFromUpperLayer;
    if (colorTarget_) {
        report.backendFallbacksHiddenFromUpperLayer = report.backendFallbacksHiddenFromUpperLayer
            && colorTarget_->allocation().backendFallbackHiddenFromUpperLayer;
    }
    report.nativeHandleExposedToUpperLayer =
        report.nativeHandleExposedToUpperLayer || frameConstants_->allocation().nativeHandleExposed;
    if (colorTarget_) {
        report.nativeHandleExposedToUpperLayer = report.nativeHandleExposedToUpperLayer
            || colorTarget_->allocation().nativeHandleExposed;
    }

    const auto frameLayout = makeFrameDescriptorLayout(*pass);
    const auto pushConstantBytes = pushConstantBytesFor(*pass);
    pipelineLayout_ =
        device.createPipelineLayout(makePipelineLayoutDesc(frameLayout, pushConstantBytes));
    if (!pipelineLayout_) {
        return failPrepare(report, "renderer RHI production frame pipeline layout creation failed");
    }
    report.backendFallbacksHiddenFromUpperLayer = report.backendFallbacksHiddenFromUpperLayer
        && pipelineLayout_->backendFallbacksHiddenFromUpperLayer();
    report.nativeHandleExposedToUpperLayer = report.nativeHandleExposedToUpperLayer
        || pipelineLayout_->nativeHandleExposedToUpperLayer();

    shaderPackage_ = makeShaderPackage(desc_, frameLayout, kSceneRuntimeDemoPushConstantBytes);
    if (!shaderPackage_.generatedReflectionHydrated) {
        return failPrepare(report, "RHI-SHADER-REFLECTION-HYDRATION-FAILED");
    }
    const auto shaderValidation = rhi::validateShaderPackage(shaderPackage_);
    report.shaderPackageValidated = shaderValidation.success;
    if (!shaderValidation.success) {
        return failPrepare(report, shaderValidation.diagnostics);
    }

    auto pipelineDesc = makeRenderPipelineDesc(desc_, *pipelineLayout_, shaderPackage_);
    report.fullSurfaceViewportConfigured = hasFullSurfaceViewport(pipelineDesc, desc_.colorExtent);
    report.fullSurfaceScissorConfigured = hasFullSurfaceScissor(pipelineDesc, desc_.colorExtent);
    if (!report.fullSurfaceViewportConfigured || !report.fullSurfaceScissorConfigured) {
        return failPrepare(report,
            "renderer RHI production frame requires explicit full-surface viewport and scissor");
    }

    renderPipeline_ = device.createRenderPipeline(pipelineDesc);
    if (!renderPipeline_) {
        std::pmr::string diagnostics =
            "renderer RHI production frame render pipeline creation failed for SPIR-V artifact ";
        diagnostics += shaderPackage_.artifacts.spirvPath;
        return failPrepare(report, diagnostics);
    }
    report.rhiPipelineCreated = true;
    report.renderPipelineCount = 1;
    report.backendFallbacksHiddenFromUpperLayer = report.backendFallbacksHiddenFromUpperLayer
        && renderPipeline_->backendFallbacksHiddenFromUpperLayer();
    report.nativeHandleExposedToUpperLayer = report.nativeHandleExposedToUpperLayer
        || renderPipeline_->nativeHandleExposedToUpperLayer();

    descriptorArena_ = device.createDescriptorArena({
        desc_.label + "DescriptorArena",
        1,
        1,
    });
    if (!descriptorArena_) {
        return failPrepare(
            report, "renderer RHI production frame descriptor arena creation failed");
    }
    frameDescriptorSet_ = descriptorArena_->allocateDescriptorSet({
        desc_.label + "FrameSet",
        frameLayout,
    });
    if (!frameDescriptorSet_) {
        return failPrepare(
            report, "renderer RHI production frame descriptor set allocation failed");
    }
    report.rhiDescriptorCreated = true;
    report.descriptorSetCount = 1;

    const auto writeResult = frameDescriptorSet_->write({
        0,
        rhi::DescriptorType::UniformBuffer,
        frameConstants_->id(),
        rhi::DescriptorResourceKind::Buffer,
    });
    if (!writeResult.success) {
        return failPrepare(report, writeResult.diagnostics);
    }
    report.rhiDescriptorWritten = true;
    const auto descriptorStats = frameDescriptorSet_->stats();
    report.backendFallbacksHiddenFromUpperLayer = report.backendFallbacksHiddenFromUpperLayer
        && descriptorStats.backendFallbacksHiddenFromUpperLayer;
    report.nativeHandleExposedToUpperLayer =
        report.nativeHandleExposedToUpperLayer || descriptorStats.nativeHandleExposedToUpperLayer;

    prepared_ = report.rendererFramePlanAccepted && report.rhiResourceCreated
        && report.rhiPipelineCreated && report.rhiDescriptorCreated && report.rhiDescriptorWritten
        && report.fullSurfaceViewportConfigured && report.fullSurfaceScissorConfigured
        && report.backendFallbacksHiddenFromUpperLayer && !report.nativeHandleExposedToUpperLayer;
    report.success = prepared_;
    if (!report.success && report.diagnostics.empty()) {
        arp::core::appendDiagnostic(
            report.diagnostics, "renderer RHI production frame preparation failed validation");
    }
    return report;
}

RendererRhiProductionFrameSubmitReport RendererRhiProductionFramePath::submit(
    rhi::GpuDevice& device)
{
    if (!colorTarget_) {
        return failSubmit({}, "renderer RHI production frame requires an owned color target");
    }
    return submitToTarget(device, *colorTarget_, false);
}

RendererRhiProductionFrameSubmitReport RendererRhiProductionFramePath::submit(
    rhi::GpuDevice& device, rhi::GpuTexture& externalColorTarget)
{
    if (!desc_.useExternalColorTarget) {
        return failSubmit(
            {}, "renderer RHI production frame external color target was not prepared");
    }
    return submitToTarget(device, externalColorTarget, true);
}

RendererRhiProductionFrameSubmitReport RendererRhiProductionFramePath::submitToTarget(
    rhi::GpuDevice& device, rhi::GpuTexture& colorTarget, const bool externalColorTarget)
{
    RendererRhiProductionFrameSubmitReport report;
    report.usedRhiGpuDevice = true;
    report.resourcesPreparedBeforeSubmit = static_cast<bool>(frameConstants_)
        && (externalColorTarget || static_cast<bool>(colorTarget_));
    report.pipelinePreparedBeforeSubmit = static_cast<bool>(renderPipeline_);
    report.descriptorsPreparedBeforeSubmit = static_cast<bool>(frameDescriptorSet_);

    if (!prepared_) {
        return failSubmit(report, "renderer RHI production frame must be prepared before submit");
    }

    auto encoder = device.createCommandEncoder({desc_.label + "Submit"});
    if (!encoder) {
        return failSubmit(report, "renderer RHI production frame command encoder creation failed");
    }

    std::pmr::string commandDiagnostics;
    if (colorTarget.state() != rhi::ResourceState::ColorAttachment) {
        const auto transition = encoder->transition(
            colorTarget, colorTarget.state(), rhi::ResourceState::ColorAttachment);
        if (!transition.success) {
            return failSubmit(report, transition.diagnostics);
        }
    }

    for (const auto& passRecord : framePlan_.plan.passes) {
        if (passRecord.kind != render_graph::PassKind::Graphics
            || passRecord.drawCommandCount == 0) {
            continue;
        }
        if (passRecord.drawCommandCount > desc_.maxDrawCommandsPerPass) {
            return failSubmit(
                report, "renderer RHI production frame draw count exceeds the bounded submit path");
        }

        rhi::RenderPassDesc passDesc;
        passDesc.label = passRecord.name;
        passDesc.colorTargetName = colorTarget.desc().label;
        passDesc.colorAttachments.push_back({
            passRecord.name + "Color",
            colorTarget.id(),
            colorTarget.desc().format,
            rhi::LoadOp::Clear,
            rhi::StoreOp::Store,
        });
        passDesc.colorAttachments.back().clearColor = {0.04f, 0.06f, 0.09f, 1.0f};
        auto pass = encoder->beginRenderPass(passDesc);
        if (!pass) {
            return failSubmit(report, encoder->lastValidation().diagnostics);
        }
        commandSucceeded(pass->setPipeline(*renderPipeline_), commandDiagnostics);
        commandSucceeded(pass->bindDescriptorSet(0, *frameDescriptorSet_), commandDiagnostics);
        std::array<std::uint8_t, rhi::kL2AtomicPushConstantLimitBytes> pushConstantData{};
        for (std::uint32_t drawIndex = 0; drawIndex < passRecord.drawCommandCount; ++drawIndex) {
            const auto visualRole = visualRoleForDraw(report.drawCommandCount);
            writeSceneRuntimeDemoPushConstants(
                pushConstantData, visualRole, report.drawCommandCount);
            if (passRecord.pushConstantSizeBytes > 0) {
                if (passRecord.pushConstantSizeBytes != kSceneRuntimeDemoPushConstantBytes) {
                    arp::core::appendDiagnostic(commandDiagnostics,
                        "rhi_production_frame: push constant size does not match "
                        "declared SceneRuntimeDemo contract; AtomicFullBlockPush "
                        "requires exact block size, not truncation");
                    return failSubmit(report, commandDiagnostics);
                }
                commandSucceeded(
                    pass->pushConstants(rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment,
                        0,
                        kSceneRuntimeDemoPushConstantBytes,
                        pushConstantData.data()),
                    commandDiagnostics);
            }
            commandSucceeded(pass->drawIndexed(indexCountForVisualRole(visualRole), 1, 0, 0, 0),
                commandDiagnostics);
            markProceduralVisualRole(report, visualRole);
            ++report.drawCommandCount;
        }
        commandSucceeded(pass->end(), commandDiagnostics);
        ++report.renderPassCount;
        if (!commandDiagnostics.empty()) {
            return failSubmit(report, commandDiagnostics);
        }
    }

    if (externalColorTarget && colorTarget.state() != rhi::ResourceState::Present) {
        const auto transition =
            encoder->transition(colorTarget, colorTarget.state(), rhi::ResourceState::Present);
        if (!transition.success) {
            return failSubmit(report, transition.diagnostics);
        }
    }

    auto commandBuffer = encoder->finish();
    if (!commandBuffer) {
        return failSubmit(report, encoder->lastValidation().diagnostics);
    }
    const auto submitted = device.queue().submit(*commandBuffer);
    if (!submitted.success) {
        return failSubmit(report, submitted.diagnostics);
    }

    report.submittedCommandCount = submitted.submittedCommandCount;
    report.rhiCommandEncoded = report.renderPassCount > 0 && report.drawCommandCount > 0;
    report.rhiQueueSubmitted = true;
    report.backendFallbacksHiddenFromUpperLayer = true;
    report.nativeHandleExposedToUpperLayer = false;
    report.success = report.productionRendererPath && report.usedRhiGpuDevice
        && report.resourcesPreparedBeforeSubmit && report.pipelinePreparedBeforeSubmit
        && report.descriptorsPreparedBeforeSubmit && !report.resourceAllocatedDuringSubmit
        && !report.pipelineCreatedDuringSubmit && report.rhiCommandEncoded
        && report.rhiQueueSubmitted && !report.removedModuleRouteUsed
        && !report.nativeHandleExposedToUpperLayer;
    if (!report.success && report.diagnostics.empty()) {
        arp::core::appendDiagnostic(
            report.diagnostics, "renderer RHI production frame submit failed validation");
    }
    return report;
}

void RendererRhiProductionFramePath::resetPreparedState()
{
    framePlan_ = {};
    shaderPackage_ = {};
    colorTarget_.reset();
    frameConstants_.reset();
    pipelineLayout_.reset();
    renderPipeline_.reset();
    descriptorArena_.reset();
    frameDescriptorSet_.reset();
    prepared_ = false;
}

} // namespace arp::renderer

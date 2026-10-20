#include "renderer/lighting/forward_plus_feature.h"

#include "renderer/shader_package_reflection.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace arp::renderer::lighting {
namespace {

ForwardPlusFeatureResult fail(ForwardPlusDiagnosticCode code, const char* id, const char* message)
{
    ForwardPlusFeatureResult result;
    result.diagnostic.code = code;
    result.diagnostic.id = id;
    result.diagnostic.message = message;
    return result;
}

rhi::BufferDesc storageBuffer(const char* label, std::uint64_t sizeBytes, bool uploadTarget)
{
    rhi::BufferDesc desc;
    desc.label = label;
    desc.sizeBytes = sizeBytes;
    desc.usage = rhi::ResourceUsage::StorageBuffer;
    if (uploadTarget) {
        desc.usage = desc.usage | rhi::ResourceUsage::CopyDst;
    }
    // Transient/memoryless is a texture-only attachment contract on Metal.
    // Forward+ storage buffers are graph-owned resources and are populated
    // through backend-owned CopyDst staging, so they must remain GPU-owned
    // across all backends.
    desc.memory = rhi::MemoryUsage::GpuOnly;
    desc.initialState = rhi::ResourceState::Undefined;
    return desc;
}

rhi::BufferDesc constantsBuffer(const char* label, std::uint64_t sizeBytes)
{
    return {
        label,
        sizeBytes,
        rhi::ResourceUsage::UniformBuffer | rhi::ResourceUsage::CopyDst,
        rhi::MemoryUsage::CpuToGpu,
        rhi::ResourceState::Undefined,
    };
}

rhi::BufferDesc uploadBuffer(const char* label, std::uint64_t sizeBytes)
{
    return {
        label,
        sizeBytes,
        rhi::ResourceUsage::CopySrc | rhi::ResourceUsage::CopyDst,
        rhi::MemoryUsage::CpuToGpu,
        rhi::ResourceState::CopySrc,
    };
}

core::pmr::String replaceExtension(const core::pmr::String& path, const char* extension)
{
    const auto slash = path.find_last_of("/\\");
    const auto dot = path.find_last_of('.');
    if (dot == core::pmr::String::npos || (slash != core::pmr::String::npos && dot < slash)) {
        return path + extension;
    }
    return path.substr(0u, dot) + extension;
}

rhi::DescriptorLayoutDesc cullDescriptorLayout()
{
    auto depth = rhi::DescriptorBindingDesc{
        1u, rhi::DescriptorType::SampledTexture, rhi::ShaderStage::Compute};
    depth.depthTexture = true;
    return {
        "ForwardPlusCullSet",
        {
            {0u, rhi::DescriptorType::UniformBuffer, rhi::ShaderStage::Compute},
            depth,
            {2u, rhi::DescriptorType::ReadOnlyStorageBuffer, rhi::ShaderStage::Compute},
            {3u, rhi::DescriptorType::StorageBuffer, rhi::ShaderStage::Compute},
            {4u, rhi::DescriptorType::StorageBuffer, rhi::ShaderStage::Compute},
            {5u, rhi::DescriptorType::StorageBuffer, rhi::ShaderStage::Compute},
            {6u, rhi::DescriptorType::StorageBuffer, rhi::ShaderStage::Compute},
            {7u, rhi::DescriptorType::StorageBuffer, rhi::ShaderStage::Compute},
        },
    };
}

ForwardPlusFeatureResult writeDescriptor(rhi::DescriptorSet& set,
    std::uint32_t binding,
    rhi::DescriptorType type,
    rhi::GpuResourceId resource,
    rhi::DescriptorResourceKind kind)
{
    const auto written = set.write({binding, type, resource, kind});
    if (!written.success) {
        return fail(ForwardPlusDiagnosticCode::CapabilityUnavailable,
            "ARP-FORWARD-PLUS-DESCRIPTOR-WRITE",
            "Forward+ descriptor write failed against the reflected ABI.");
    }
    return {true, {}};
}

void requireSuccess(const ForwardPlusFeatureResult& result)
{
    if (!result.success) {
        throw std::runtime_error(result.diagnostic.id.c_str());
    }
}

} // namespace

void appendForwardPlusShadingBindings(rhi::DescriptorLayoutDesc& layout, rhi::ShaderStage stages)
{
    layout.bindings.push_back(
        {ForwardPlusShadingBindings::kConstants, rhi::DescriptorType::UniformBuffer, stages});
    layout.bindings.push_back({ForwardPlusShadingBindings::kDirectionalLights,
        rhi::DescriptorType::ReadOnlyStorageBuffer,
        stages});
    layout.bindings.push_back({ForwardPlusShadingBindings::kLocalLights,
        rhi::DescriptorType::ReadOnlyStorageBuffer,
        stages});
    layout.bindings.push_back({ForwardPlusShadingBindings::kTileHeaders,
        rhi::DescriptorType::ReadOnlyStorageBuffer,
        stages});
    layout.bindings.push_back({ForwardPlusShadingBindings::kTileIndices,
        rhi::DescriptorType::ReadOnlyStorageBuffer,
        stages});
}

ForwardPlusFeatureResult ForwardPlusFeature::create(const ForwardPlusFeatureCreateContext& context)
{
    release();
    const auto validation = validateForwardPlusConfig(
        context.config, context.extent, forwardPlusDeviceLimits(context.device.capabilities()));
    if (!validation.success) {
        return {false, validation.diagnostic};
    }
    if (context.shaderPath.empty()) {
        return fail(ForwardPlusDiagnosticCode::CapabilityUnavailable,
            "ARP-FORWARD-PLUS-SHADER-PATH",
            "ForwardPlusFeature requires generated Slang artifacts.");
    }
    device_ = &context.device;
    extent_ = context.extent;
    config_ = context.config;
    conservativeTileCounts_.resize(
        std::size_t(forwardPlusTileCount(extent_.width, config_.tileSize))
        * forwardPlusTileCount(extent_.height, config_.tileSize));
    const auto execution = createExecutionResources(context.shaderPath);
    if (!execution.success) {
        release();
        return execution;
    }
    return {true, {}};
}

ForwardPlusFeatureResult ForwardPlusFeature::createExecutionResources(
    const core::pmr::String& shaderPath)
{
    const auto layout = cullDescriptorLayout();
    rhi::PipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.descriptorLayouts.push_back(layout);
    pipelineLayoutDesc.descriptorSets.push_back({0u, layout.name});
    pipelineLayout_ = device_->createPipelineLayout(pipelineLayoutDesc);
    if (!pipelineLayout_) {
        return fail(ForwardPlusDiagnosticCode::CapabilityUnavailable,
            "ARP-FORWARD-PLUS-PIPELINE-LAYOUT",
            "Forward+ compute pipeline layout creation failed.");
    }

    shaderPackage_.label = "ForwardPlusCull";
    shaderPackage_.slangModulePath = shaderPath;
    shaderPackage_.entrypoints.push_back({shaderPath,
        "forwardPlusCullMain",
        rhi::ShaderStage::Compute,
        rhi::ShaderSourceLanguage::Slang});
    shaderPackage_.artifacts.spirvPath = replaceExtension(shaderPath, ".spv");
    shaderPackage_.artifacts.mslPath = replaceExtension(shaderPath, ".metal");
    shaderPackage_.artifacts.wgslPath = replaceExtension(shaderPath, ".wgsl");
    shaderPackage_.artifacts.reflectionPath = replaceExtension(shaderPath, ".reflection.yaml");
    if (!hydrateShaderPackageReflectionFromArtifact(shaderPackage_)) {
        return fail(ForwardPlusDiagnosticCode::CapabilityUnavailable,
            "ARP-FORWARD-PLUS-REFLECTION",
            "Forward+ generated shader reflection could not be hydrated.");
    }
    for (const auto& binding : layout.bindings) {
        shaderPackage_.reflection.descriptorBindings.push_back(
            rhi::makeShaderReflectionBinding(0u, binding));
    }
    const auto shaderValidation = rhi::validateShaderPackage(shaderPackage_);
    if (!shaderValidation.success) {
        return fail(ForwardPlusDiagnosticCode::CapabilityUnavailable,
            "ARP-FORWARD-PLUS-SHADER-PACKAGE",
            "Forward+ generated shader package failed validation.");
    }

    rhi::ComputePipelineDesc pipelineDesc;
    pipelineDesc.label = "ForwardPlusCullPipeline";
    pipelineDesc.layout = pipelineLayout_.get();
    pipelineDesc.shaderPackage = &shaderPackage_;
    pipelineDesc.computeShader = shaderPackage_.entrypoints.front();
    pipeline_ = device_->createComputePipeline(pipelineDesc);
    if (!pipeline_) {
        auto result = fail(ForwardPlusDiagnosticCode::CapabilityUnavailable,
            "ARP-FORWARD-PLUS-COMPUTE-PIPELINE",
            "Forward+ compute pipeline creation failed.");
        const auto validation = device_->validateComputePipeline(pipelineDesc);
        if (!validation.diagnostics.empty()) {
            result.diagnostic.message = validation.diagnostics;
        }
        return result;
    }

    descriptorArena_ = device_->createDescriptorArena({
        "ForwardPlusDescriptorArena",
        kFrameSlots,
        kFrameSlots * 8u,
    });
    if (!descriptorArena_) {
        return fail(ForwardPlusDiagnosticCode::CapabilityUnavailable,
            "ARP-FORWARD-PLUS-DESCRIPTOR-ARENA",
            "Forward+ descriptor arena creation failed.");
    }
    for (std::uint32_t slot = 0u; slot < kFrameSlots; ++slot) {
        cullSets_[slot] = descriptorArena_->allocateDescriptorSet({"ForwardPlusCullSet", layout});
        cullConstantsBuffers_[slot] = device_->createBuffer(
            constantsBuffer(slot == 0u ? "ForwardPlusCullConstants0" : "ForwardPlusCullConstants1",
                sizeof(ForwardPlusCullConstants)));
        shadingConstantsBuffers_[slot] = device_->createBuffer(constantsBuffer(
            slot == 0u ? "ForwardPlusShadingConstants0" : "ForwardPlusShadingConstants1",
            sizeof(ForwardPlusShadingConstants)));
        directionalUploadBuffers_[slot] = device_->createBuffer(uploadBuffer(
            slot == 0u ? "ForwardPlusDirectionalUpload0" : "ForwardPlusDirectionalUpload1",
            std::uint64_t(config_.maxDirectionalLights) * sizeof(GpuDirectionalLight)));
        localUploadBuffers_[slot] = device_->createBuffer(
            uploadBuffer(slot == 0u ? "ForwardPlusLocalUpload0" : "ForwardPlusLocalUpload1",
                std::uint64_t(config_.maxLocalLights) * sizeof(GpuLocalLight)));
        if (!cullSets_[slot] || !cullConstantsBuffers_[slot] || !shadingConstantsBuffers_[slot]
            || !directionalUploadBuffers_[slot] || !localUploadBuffers_[slot]) {
            return fail(ForwardPlusDiagnosticCode::CapabilityUnavailable,
                "ARP-FORWARD-PLUS-FRAME-RESOURCES",
                "Forward+ per-frame resource creation failed.");
        }
    }
    return {true, {}};
}

ForwardPlusFeatureResult ForwardPlusFeature::resize(rhi::Extent2D extent)
{
    if (device_ == nullptr) {
        return fail(ForwardPlusDiagnosticCode::CapabilityUnavailable,
            "ARP-FORWARD-PLUS-FEATURE-NOT-CREATED",
            "ForwardPlusFeature must be created before resize.");
    }
    const auto validation = validateForwardPlusConfig(
        config_, extent, forwardPlusDeviceLimits(device_->capabilities()));
    if (!validation.success) {
        return {false, validation.diagnostic};
    }
    extent_ = extent;
    conservativeTileCounts_.resize(
        std::size_t(forwardPlusTileCount(extent_.width, config_.tileSize))
        * forwardPlusTileCount(extent_.height, config_.tileSize));
    for (auto& view : depthViews_) {
        view.reset();
    }
    return {true, {}};
}

ForwardPlusFeatureResult ForwardPlusFeature::contributeGraph(
    const ForwardPlusFeatureGraphContext& context, ForwardPlusFeatureOutputs& outputs)
{
    if (device_ == nullptr || !pipeline_) {
        return fail(ForwardPlusDiagnosticCode::CapabilityUnavailable,
            "ARP-FORWARD-PLUS-FEATURE-NOT-CREATED",
            "ForwardPlusFeature must be created before graph contribution.");
    }
    if (context.lightPlan.path != LightingPath::ForwardPlus) {
        return fail(ForwardPlusDiagnosticCode::InvalidConfig,
            "ARP-FORWARD-PLUS-PATH-MISMATCH",
            "ForwardPlusFeature only accepts a ForwardPlus light plan.");
    }
    if (context.lightPlan.directionalLights.size() > config_.maxDirectionalLights) {
        return fail(ForwardPlusDiagnosticCode::DirectionalLightBudgetExceeded,
            "ARP-FORWARD-PLUS-DIRECTIONAL-BUDGET",
            "ForwardPlusFeature rejected the frame before upload because its directional-light budget was exceeded.");
    }
    if (context.lightPlan.localLights.size() > config_.maxLocalLights) {
        return fail(ForwardPlusDiagnosticCode::LocalLightBudgetExceeded,
            "ARP-FORWARD-PLUS-LOCAL-LIGHT-BUDGET",
            "ForwardPlusFeature rejected the frame before upload because its local-light budget was exceeded.");
    }
    const auto preflight = preflightForwardPlusConservativeTileBudget(config_,
        extent_,
        context.lightPlan.localLights,
        context.view,
        conservativeTileCounts_.data(),
        conservativeTileCounts_.size());
    if (!preflight.success) {
        return {false, preflight.diagnostic};
    }

    const auto tileCountX = forwardPlusTileCount(extent_.width, config_.tileSize);
    const auto tileCountY = forwardPlusTileCount(extent_.height, config_.tileSize);
    const auto tileCount = std::uint64_t(tileCountX) * tileCountY;
    const auto indexBytes = tileCount * config_.maxLocalLightsPerTile * sizeof(std::uint32_t);
    outputs.directionalLights =
        context.graph.createTransientBuffer(storageBuffer("ForwardPlus.DirectionalLights",
            std::uint64_t(config_.maxDirectionalLights) * sizeof(GpuDirectionalLight),
            true));
    outputs.localLights =
        context.graph.createTransientBuffer(storageBuffer("ForwardPlus.LocalLights",
            std::uint64_t(config_.maxLocalLights) * sizeof(GpuLocalLight),
            true));
    outputs.opaqueHeaders = context.graph.createTransientBuffer(storageBuffer(
        "ForwardPlus.OpaqueTileHeaders", tileCount * sizeof(ForwardPlusTileHeader), false));
    outputs.opaqueIndices = context.graph.createTransientBuffer(
        storageBuffer("ForwardPlus.OpaqueTileIndices", indexBytes, false));
    outputs.conservativeHeaders = context.graph.createTransientBuffer(storageBuffer(
        "ForwardPlus.ConservativeTileHeaders", tileCount * sizeof(ForwardPlusTileHeader), false));
    outputs.conservativeIndices = context.graph.createTransientBuffer(
        storageBuffer("ForwardPlus.ConservativeTileIndices", indexBytes, false));
    outputs.overflowTelemetry = context.graph.createTransientBuffer(storageBuffer(
        "ForwardPlus.OverflowTelemetry", tileCount * sizeof(std::uint32_t) * 4u, false));

    const auto frameSlot = context.frameIndex % kFrameSlots;
    // Shading descriptors are written while the graph is being composed,
    // before the cull execute callback runs. Populate the CPU-side constants
    // here so the graphics consumer never observes a zero-budget frame.
    shadingConstants_[frameSlot].tileCount[0] = tileCountX;
    shadingConstants_[frameSlot].tileCount[1] = tileCountY;
    shadingConstants_[frameSlot].directionalLightCount =
        static_cast<std::uint32_t>(context.lightPlan.directionalLights.size());
    shadingConstants_[frameSlot].localLightCount =
        static_cast<std::uint32_t>(context.lightPlan.localLights.size());
    shadingConstants_[frameSlot].lightingPath =
        static_cast<std::uint32_t>(LightingPath::ForwardPlus);
    shadingConstants_[frameSlot].maxLocalLightsPerTile = config_.maxLocalLightsPerTile;

    // Shading descriptor sets are populated while the frame graph is being
    // composed, before the cull pass execute callback runs. Publish this
    // frame's graph-owned buffers now so consumers never bind the previous
    // use of the frame slot (which may already have been recycled).
    auto& frameResources = frameResources_[frameSlot];
    frameResources.directionalLights = context.graph.buffer(outputs.directionalLights).id();
    frameResources.localLights = context.graph.buffer(outputs.localLights).id();
    frameResources.opaqueHeaders = context.graph.buffer(outputs.opaqueHeaders).id();
    frameResources.opaqueIndices = context.graph.buffer(outputs.opaqueIndices).id();
    frameResources.conservativeHeaders = context.graph.buffer(outputs.conservativeHeaders).id();
    frameResources.conservativeIndices = context.graph.buffer(outputs.conservativeIndices).id();
    // The depth handle can be an imported attachment whose physical resource
    // is materialized only during execution. dispatchCull publishes it after
    // resolving the handle; shading never consumes it directly.
    frameResources.depth = {};
    if (std::getenv("ARP_FORWARD_PLUS_FRAME_TRACE") != nullptr) {
        std::cerr << "[forward-plus-frame] compose frame=" << context.frameIndex
                  << " slot=" << frameSlot
                  << " constants=" << shadingConstantsBuffers_[frameSlot]->id().value
                  << " directional=" << frameResources.directionalLights.value
                  << " local=" << frameResources.localLights.value
                  << " headers=" << frameResources.opaqueHeaders.value << '\n';
    }

    const auto directionalBytes =
        context.lightPlan.directionalLights.size() * sizeof(GpuDirectionalLight);
    const auto localBytes = context.lightPlan.localLights.size() * sizeof(GpuLocalLight);
    if (directionalBytes != 0u) {
        const auto written = device_->writeBuffer(*directionalUploadBuffers_[frameSlot],
            context.lightPlan.directionalLights.data(),
            directionalBytes);
        if (!written.success) {
            return fail(ForwardPlusDiagnosticCode::CapabilityUnavailable,
                "ARP-FORWARD-PLUS-DIRECTIONAL-UPLOAD",
                written.diagnostics.c_str());
        }
    }
    if (localBytes != 0u) {
        const auto written = device_->writeBuffer(
            *localUploadBuffers_[frameSlot], context.lightPlan.localLights.data(), localBytes);
        if (!written.success) {
            return fail(ForwardPlusDiagnosticCode::CapabilityUnavailable,
                "ARP-FORWARD-PLUS-LOCAL-UPLOAD",
                written.diagnostics.c_str());
        }
    }

    auto upload = context.graph.addPass("ForwardPlus.LightUpload")
                      .kind(render_graph::PassKind::Copy)
                      .queue(render_graph::QueueRole::Transfer)
                      .write(outputs.directionalLights, rhi::ResourceState::CopyDst)
                      .write(outputs.localLights, rhi::ResourceState::CopyDst);
    upload.execute([this, &graph = context.graph, outputs, frameSlot, directionalBytes, localBytes](
                       render_graph::RenderContext& renderContext) {
        recordLightUpload(renderContext, graph, outputs, frameSlot, directionalBytes, localBytes);
    });

    auto cull = context.graph.addPass("ForwardPlus.Cull")
                    .kind(render_graph::PassKind::Compute)
                    .queue(render_graph::QueueRole::Compute)
                    .asyncComputeEligible(false)
                    .read(context.exactOpaqueDepth, rhi::ResourceState::ShaderRead)
                    .read(outputs.localLights, rhi::ResourceState::ShaderRead)
                    .write(outputs.opaqueHeaders, rhi::ResourceState::ShaderWrite)
                    .write(outputs.opaqueIndices, rhi::ResourceState::ShaderWrite)
                    .write(outputs.conservativeHeaders, rhi::ResourceState::ShaderWrite)
                    .write(outputs.conservativeIndices, rhi::ResourceState::ShaderWrite)
                    .write(outputs.overflowTelemetry, rhi::ResourceState::ShaderWrite);
    cull.execute([this,
                     &graph = context.graph,
                     depth = context.exactOpaqueDepth,
                     outputs,
                     plan = &context.lightPlan,
                     view = context.view,
                     frameIndex = context.frameIndex](render_graph::RenderContext& renderContext) {
        dispatchCull(renderContext, graph, depth, outputs, *plan, view, frameIndex);
    });
    return {true, {}};
}

void ForwardPlusFeature::recordLightUpload(render_graph::RenderContext& context,
    render_graph::FrameGraph& graph,
    const ForwardPlusFeatureOutputs& outputs,
    std::uint32_t frameSlot,
    std::uint64_t directionalBytes,
    std::uint64_t localBytes)
{
    auto transfer = context.commands().beginTransferPass({"ForwardPlus.LightUpload"});
    if (!transfer) {
        throw std::runtime_error("ARP-FORWARD-PLUS-BEGIN-LIGHT-UPLOAD");
    }
    if (directionalBytes != 0u) {
        const auto copied = transfer->copyBufferToBuffer({
            directionalUploadBuffers_[frameSlot]->id(),
            graph.buffer(outputs.directionalLights).id(),
            0u,
            0u,
            directionalBytes,
        });
        if (!copied.success) {
            throw std::runtime_error("ARP-FORWARD-PLUS-DIRECTIONAL-COPY");
        }
    }
    if (localBytes != 0u) {
        const auto copied = transfer->copyBufferToBuffer({
            localUploadBuffers_[frameSlot]->id(),
            graph.buffer(outputs.localLights).id(),
            0u,
            0u,
            localBytes,
        });
        if (!copied.success) {
            throw std::runtime_error("ARP-FORWARD-PLUS-LOCAL-COPY");
        }
    }
    if (!transfer->end().success) {
        throw std::runtime_error("ARP-FORWARD-PLUS-END-LIGHT-UPLOAD");
    }
}

void ForwardPlusFeature::dispatchCull(render_graph::RenderContext& context,
    render_graph::FrameGraph& graph,
    render_graph::TextureHandle depth,
    const ForwardPlusFeatureOutputs& outputs,
    const RenderLightPlan& plan,
    const ForwardPlusCullView& view,
    std::uint32_t frameIndex)
{
    const auto slot = frameIndex % kFrameSlots;
    const auto tileCountX = forwardPlusTileCount(extent_.width, config_.tileSize);
    const auto tileCountY = forwardPlusTileCount(extent_.height, config_.tileSize);

    ForwardPlusCullConstants cullConstants;
    cullConstants.extent[0] = extent_.width;
    cullConstants.extent[1] = extent_.height;
    cullConstants.tileCount[0] = tileCountX;
    cullConstants.tileCount[1] = tileCountY;
    cullConstants.localLightCount = static_cast<std::uint32_t>(plan.localLights.size());
    cullConstants.maxLocalLightsPerTile = config_.maxLocalLightsPerTile;
    std::copy_n(view.projectionScale, 2u, cullConstants.projectionScale);
    cullConstants.depthScale = view.depthScale;
    cullConstants.depthBias = view.depthBias;
    std::copy_n(view.viewRows, 16u, cullConstants.viewRows);
    // This opt-in trace diagnoses live Forward+ ABI/cull issues without
    // changing the fail-closed overflow contract or producing per-frame spam.
    if (std::getenv("ARP_FORWARD_PLUS_DIAG") != nullptr) {
        static std::uint32_t lastLocalLightCount = ~0u;
        static std::uint32_t lastExtentWidth = ~0u;
        static std::uint32_t lastExtentHeight = ~0u;
        if (lastLocalLightCount != cullConstants.localLightCount || lastExtentWidth != extent_.width
            || lastExtentHeight != extent_.height) {
            std::cerr << "[forward-plus] cull local=" << cullConstants.localLightCount
                      << " directional=" << plan.directionalLights.size() << " tiles=" << tileCountX
                      << 'x' << tileCountY << " extent=" << extent_.width << 'x' << extent_.height
                      << " perTile=" << cullConstants.maxLocalLightsPerTile << '\n';
            lastLocalLightCount = cullConstants.localLightCount;
            lastExtentWidth = extent_.width;
            lastExtentHeight = extent_.height;
        }
    }
    const auto cullWritten =
        device_->writeBuffer(*cullConstantsBuffers_[slot], &cullConstants, sizeof(cullConstants));
    if (!cullWritten.success) {
        throw std::runtime_error("ARP-FORWARD-PLUS-CULL-CONSTANT-UPLOAD");
    }

    ForwardPlusShadingConstants shadingConstants;
    shadingConstants.tileCount[0] = tileCountX;
    shadingConstants.tileCount[1] = tileCountY;
    shadingConstants.directionalLightCount =
        static_cast<std::uint32_t>(plan.directionalLights.size());
    shadingConstants.localLightCount = static_cast<std::uint32_t>(plan.localLights.size());
    shadingConstants.lightingPath = static_cast<std::uint32_t>(LightingPath::ForwardPlus);
    shadingConstants.maxLocalLightsPerTile = config_.maxLocalLightsPerTile;
    shadingConstants_[slot] = shadingConstants;
    if (std::getenv("ARP_FORWARD_PLUS_FRAME_TRACE") != nullptr) {
        std::cerr << "[forward-plus-frame] cull frame=" << frameIndex << " slot=" << slot
                  << " constants=" << shadingConstantsBuffers_[slot]->id().value
                  << " tiles=" << tileCountX << 'x' << tileCountY
                  << " localCount=" << shadingConstants.localLightCount << '\n';
    }

    auto& depthTexture = graph.texture(depth);
    if (!depthViews_[slot] || frameResources_[slot].depth != depthTexture.id()) {
        depthViews_[slot] = device_->createTextureView(depthTexture,
            {
                "ForwardPlusDepthView",
                depthTexture.desc().format,
                rhi::TextureUsage::Sampled,
                rhi::TextureViewDimension::Texture2D,
                0u,
                1u,
                0u,
                1u,
                rhi::TextureAspect::Depth,
            });
        if (!depthViews_[slot]) {
            throw std::runtime_error("ARP-FORWARD-PLUS-DEPTH-VIEW");
        }
    }

    auto& resources = frameResources_[slot];
    resources.directionalLights = graph.buffer(outputs.directionalLights).id();
    resources.localLights = graph.buffer(outputs.localLights).id();
    resources.opaqueHeaders = graph.buffer(outputs.opaqueHeaders).id();
    resources.opaqueIndices = graph.buffer(outputs.opaqueIndices).id();
    resources.conservativeHeaders = graph.buffer(outputs.conservativeHeaders).id();
    resources.conservativeIndices = graph.buffer(outputs.conservativeIndices).id();
    resources.depth = depthTexture.id();

    auto& set = *cullSets_[slot];
    requireSuccess(writeDescriptor(set,
        0u,
        rhi::DescriptorType::UniformBuffer,
        cullConstantsBuffers_[slot]->id(),
        rhi::DescriptorResourceKind::Buffer));
    requireSuccess(writeDescriptor(set,
        1u,
        rhi::DescriptorType::SampledTexture,
        depthViews_[slot]->id(),
        rhi::DescriptorResourceKind::TextureView));
    requireSuccess(writeDescriptor(set,
        2u,
        rhi::DescriptorType::ReadOnlyStorageBuffer,
        resources.localLights,
        rhi::DescriptorResourceKind::Buffer));
    requireSuccess(writeDescriptor(set,
        3u,
        rhi::DescriptorType::StorageBuffer,
        resources.opaqueHeaders,
        rhi::DescriptorResourceKind::Buffer));
    requireSuccess(writeDescriptor(set,
        4u,
        rhi::DescriptorType::StorageBuffer,
        resources.opaqueIndices,
        rhi::DescriptorResourceKind::Buffer));
    requireSuccess(writeDescriptor(set,
        5u,
        rhi::DescriptorType::StorageBuffer,
        resources.conservativeHeaders,
        rhi::DescriptorResourceKind::Buffer));
    requireSuccess(writeDescriptor(set,
        6u,
        rhi::DescriptorType::StorageBuffer,
        resources.conservativeIndices,
        rhi::DescriptorResourceKind::Buffer));
    requireSuccess(writeDescriptor(set,
        7u,
        rhi::DescriptorType::StorageBuffer,
        graph.buffer(outputs.overflowTelemetry).id(),
        rhi::DescriptorResourceKind::Buffer));

    auto pass = context.commands().beginComputePass({"ForwardPlus.Cull"});
    if (!pass) {
        throw std::runtime_error("ARP-FORWARD-PLUS-BEGIN-COMPUTE");
    }
    if (!pass->setPipeline(*pipeline_).success || !pass->bindDescriptorSet(0u, set).success
        || !pass->dispatch(tileCountX, tileCountY, 1u).success || !pass->end().success) {
        throw std::runtime_error("ARP-FORWARD-PLUS-DISPATCH");
    }
}

ForwardPlusFeatureResult ForwardPlusFeature::writeShadingDescriptors(
    rhi::DescriptorSet& descriptorSet, ForwardPlusLightList list, std::uint32_t frameIndex)
{
    if (!created()) {
        return fail(ForwardPlusDiagnosticCode::CapabilityUnavailable,
            "ARP-FORWARD-PLUS-FEATURE-NOT-CREATED",
            "ForwardPlusFeature must be created before shading descriptors are written.");
    }
    const auto slot = frameIndex % kFrameSlots;
    const auto& resources = frameResources_[slot];
    const auto headers = list == ForwardPlusLightList::OpaqueDepthBounded
        ? resources.opaqueHeaders
        : resources.conservativeHeaders;
    const auto indices = list == ForwardPlusLightList::OpaqueDepthBounded
        ? resources.opaqueIndices
        : resources.conservativeIndices;
    if (!resources.directionalLights.isValid() || !resources.localLights.isValid()
        || !headers.isValid() || !indices.isValid()) {
        return fail(ForwardPlusDiagnosticCode::CapabilityUnavailable,
            "ARP-FORWARD-PLUS-SHADING-RESOURCES",
            "Forward+ cull must execute before its shading descriptors are written.");
    }
    // These constants are consumed only by the graphics shaders. The CPU-side
    // value was populated during graph contribution, before descriptors are
    // written, so the producer and consumer agree on this frame's budget.
    const auto shadingWritten = device_->writeBuffer(*shadingConstantsBuffers_[slot],
        &shadingConstants_[slot],
        sizeof(ForwardPlusShadingConstants));
    if (!shadingWritten.success) {
        return fail(ForwardPlusDiagnosticCode::CapabilityUnavailable,
            "ARP-FORWARD-PLUS-SHADING-CONSTANT-UPLOAD",
            shadingWritten.diagnostics.c_str());
    }
    if (std::getenv("ARP_FORWARD_PLUS_FRAME_TRACE") != nullptr) {
        std::cerr << "[forward-plus-frame] shade frame=" << frameIndex << " slot=" << slot
                  << " constants=" << shadingConstantsBuffers_[slot]->id().value
                  << " directional=" << resources.directionalLights.value
                  << " local=" << resources.localLights.value << " headers=" << headers.value
                  << '\n';
        std::cerr << "[forward-plus-frame] shade-values tile="
                  << shadingConstants_[slot].tileCount[0] << 'x'
                  << shadingConstants_[slot].tileCount[1]
                  << " directional=" << shadingConstants_[slot].directionalLightCount
                  << " local=" << shadingConstants_[slot].localLightCount
                  << " maxPerTile=" << shadingConstants_[slot].maxLocalLightsPerTile << '\n';
    }
    const ForwardPlusFeatureResult writes[] = {
        writeDescriptor(descriptorSet,
            ForwardPlusShadingBindings::kConstants,
            rhi::DescriptorType::UniformBuffer,
            shadingConstantsBuffers_[slot]->id(),
            rhi::DescriptorResourceKind::Buffer),
        writeDescriptor(descriptorSet,
            ForwardPlusShadingBindings::kDirectionalLights,
            rhi::DescriptorType::ReadOnlyStorageBuffer,
            resources.directionalLights,
            rhi::DescriptorResourceKind::Buffer),
        writeDescriptor(descriptorSet,
            ForwardPlusShadingBindings::kLocalLights,
            rhi::DescriptorType::ReadOnlyStorageBuffer,
            resources.localLights,
            rhi::DescriptorResourceKind::Buffer),
        writeDescriptor(descriptorSet,
            ForwardPlusShadingBindings::kTileHeaders,
            rhi::DescriptorType::ReadOnlyStorageBuffer,
            headers,
            rhi::DescriptorResourceKind::Buffer),
        writeDescriptor(descriptorSet,
            ForwardPlusShadingBindings::kTileIndices,
            rhi::DescriptorType::ReadOnlyStorageBuffer,
            indices,
            rhi::DescriptorResourceKind::Buffer),
    };
    for (const auto& write : writes) {
        if (!write.success) {
            return write;
        }
    }
    return {true, {}};
}

void ForwardPlusFeature::release() noexcept
{
    for (auto& resources : frameResources_) {
        resources = {};
    }
    for (auto& view : depthViews_) {
        view.reset();
    }
    for (auto& buffer : shadingConstantsBuffers_) {
        buffer.reset();
    }
    for (auto& buffer : localUploadBuffers_) {
        buffer.reset();
    }
    for (auto& buffer : directionalUploadBuffers_) {
        buffer.reset();
    }
    for (auto& buffer : cullConstantsBuffers_) {
        buffer.reset();
    }
    for (auto& set : cullSets_) {
        set.reset();
    }
    descriptorArena_.reset();
    pipeline_.reset();
    pipelineLayout_.reset();
    shaderPackage_ = {};
    device_ = nullptr;
    extent_ = {};
    config_ = {};
    conservativeTileCounts_.clear();
}

} // namespace arp::renderer::lighting

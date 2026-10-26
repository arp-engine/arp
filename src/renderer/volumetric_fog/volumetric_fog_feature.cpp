#include "renderer/volumetric_fog/volumetric_fog_feature.h"

#include "core/math/matrix_validation.h"
#include "renderer/raster_viewport.h"
#include "renderer/renderer_common.h"
#include "renderer/shader_package_reflection.h"
#include "renderer/temporal/jitter.h"
#include "rhi/semantics/render_target_coordinates.h"

#include <cstring>
#include <stdexcept>

namespace arp::renderer::fog {
namespace {
template <class Result> void require(const Result& result)
{
    if (!result.success)
        throw std::runtime_error(core::toStdString(result.diagnostics));
}

bool finite(core::Vec3 v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool inverseValid(const core::Mat4& matrix, const core::Mat4& inv)
{
    return core::isApproximateInverse(matrix, inv, 0.002f);
}
} // namespace

FogResult VolumetricFogFeature::create(const FogCreateContext& context)
{
    release();
    const auto planned =
        planFog(context.config, context.extent, context.device.capabilities(), grid_);
    if (!planned.success)
        return planned;
    if (!context.cameraId || context.shaderDirectory.empty())
        return FogResult::failure("FOG-CREATE-IDENTITY");
    device_ = &context.device;
    extent_ = context.extent;
    config_ = context.config;
    cameraId_ = context.cameraId;
    spatialAmbient_ = context.spatialAmbient;
    shaderDirectory_ = context.shaderDirectory;
    volumeUpload_.reserve(config_.maxLocalVolumes);
    auto result = createPipelines();
    if (result.success)
        result = createSlots();
    if (!result.success)
        release();
    return result;
}

FogResult VolumetricFogFeature::createPipelines()
{
    using D = rhi::DescriptorType;
    const char* names[] = {"fog_inject",
        spatialAmbient_ ? "fog_scatter_spatial" : "fog_scatter",
        "fog_temporal",
        "fog_integrate",
        "fog_composite"};
    const core::pmr::Vector<D> types[] = {
        {D::UniformBuffer, D::ReadOnlyStorageBuffer, D::StorageBuffer},
        {D::UniformBuffer, D::ReadOnlyStorageBuffer, D::SampledTexture, D::StorageBuffer},
        {D::UniformBuffer,
            D::ReadOnlyStorageBuffer,
            D::ReadOnlyStorageBuffer,
            D::ReadOnlyStorageBuffer,
            D::SampledTexture,
            D::StorageBuffer},
        {D::UniformBuffer, D::ReadOnlyStorageBuffer, D::StorageBuffer},
        {D::UniformBuffer, D::ReadOnlyStorageBuffer, D::SampledTexture, D::SampledTexture}};
    for (std::uint32_t i = 0; i < 5; ++i) {
        auto& p = pipelines_[i];
        p.bindings.name = names[i];
        auto stage = i == 4 ? rhi::ShaderStage::Fragment : rhi::ShaderStage::Compute;
        for (std::uint32_t b = 0; b < types[i].size(); ++b) {
            rhi::DescriptorBindingDesc binding{b, types[i][b], stage};
            binding.depthTexture = (i == 1 && b == 2) || (i == 2 && b == 4) || (i == 4 && b == 3);
            p.bindings.bindings.push_back(binding);
        }
        if (i == 1 && spatialAmbient_) {
            const D spatialTypes[] = {D::SampledTexture,
                D::SampledTexture,
                D::Sampler,
                D::ReadOnlyStorageBuffer,
                D::UniformBuffer};
            for (std::uint32_t b = 0; b < 5; ++b)
                p.bindings.bindings.push_back({4u + b, spatialTypes[b], stage});
        }
        rhi::PipelineLayoutDesc layout;
        layout.descriptorLayouts.push_back(p.bindings);
        layout.descriptorSets.push_back({0u, p.bindings.name});
        p.layout = device_->createPipelineLayout(layout);
        if (!p.layout)
            return FogResult::failure("FOG-PIPELINE-LAYOUT");
        const auto path = shaderDirectory_ + "/" + names[i] + ".slang";
        if (i == 4)
            p.package = detail::makeGraphicsShaderPackage(names[i], path, 0u);
        else {
            p.package.label = names[i];
            p.package.slangModulePath = path;
            p.package.entrypoints.push_back(
                {path, "computeMain", stage, rhi::ShaderSourceLanguage::Slang});
            p.package.artifacts.spirvPath = detail::replaceShaderExtension(path, ".spv");
            p.package.artifacts.mslPath = detail::replaceShaderExtension(path, ".metal");
            p.package.artifacts.wgslPath = detail::replaceShaderExtension(path, ".wgsl");
            p.package.artifacts.reflectionPath =
                detail::replaceShaderExtension(path, ".reflection.yaml");
        }
        if (!hydrateShaderPackageReflectionFromArtifact(p.package))
            return FogResult::failure("FOG-SHADER-REFLECTION");
        for (const auto& b : p.bindings.bindings)
            p.package.reflection.descriptorBindings.push_back(
                rhi::makeShaderReflectionBinding(0u, b));
        const auto valid = rhi::validateShaderPackage(p.package);
        if (!valid.success)
            return {false, valid.diagnostics};
        if (i == 4) {
            auto desc = detail::makeRenderPipelineDesc(
                names[i], *p.layout, p.package, rhi::TextureFormat::Rgba16Float, extent_);
            useDynamicRasterViewport(desc);
            desc.colorTargets = {{rhi::TextureFormat::Rgba16Float}};
            desc.depthStencil.depthTestEnabled = false;
            desc.depthStencil.depthWriteEnabled = false;
            desc.cullMode = rhi::CullMode::None;
            desc.frontFace = rhi::FrontFace::CounterClockwise;
            desc.topology = rhi::PrimitiveTopology::TriangleList;
            p.graphics = device_->createRenderPipeline(desc);
            if (!p.graphics)
                return FogResult::failure("FOG-GRAPHICS-PIPELINE");
        } else {
            rhi::ComputePipelineDesc desc;
            desc.label = names[i];
            desc.layout = p.layout.get();
            desc.shaderPackage = &p.package;
            desc.computeShader = p.package.entrypoints.front();
            p.compute = device_->createComputePipeline(desc);
            if (!p.compute)
                return FogResult::failure("FOG-COMPUTE-PIPELINE");
        }
    }
    return {};
}

FogResult VolumetricFogFeature::createSlots()
{
    arena_ = device_->createDescriptorArena({"Fog.Descriptors",
        config_.framesInFlight * 5u,
        config_.framesInFlight * (spatialAmbient_ ? 25u : 20u)});
    if (!arena_)
        return FogResult::failure("FOG-DESCRIPTOR-BUDGET");
    slots_.resize(config_.framesInFlight);
    const auto buffer = [this](const char* label,
                            std::uint64_t size,
                            bool upload,
                            bool uniform = false,
                            bool copySource = false) {
        rhi::BufferDesc d;
        d.label = label;
        d.sizeBytes = size;
        d.usage = (uniform ? rhi::ResourceUsage::UniformBuffer : rhi::ResourceUsage::StorageBuffer);
        d.memory = upload ? rhi::MemoryUsage::CpuToGpu : rhi::MemoryUsage::GpuOnly;
        if (upload)
            d.initialState = rhi::ResourceState::ShaderRead;
        if (upload)
            d.usage = d.usage | rhi::ResourceUsage::CopyDst;
        if (copySource)
            d.usage = d.usage | rhi::ResourceUsage::CopySrc;
        return core::shareUnique(device_->createBuffer(d));
    };
    emptyHistory_ = buffer("Fog.EmptyHistory", kFogHistoryStride, true);
    if (!emptyHistory_)
        return FogResult::failure("FOG-BUFFER-CREATE");
    const std::array<std::byte, kFogHistoryStride> empty{};
    const auto initialized = device_->writeBuffer(*emptyHistory_, empty.data(), empty.size());
    if (!initialized.success)
        return FogResult::failure("FOG-HISTORY-INITIALIZE");
    for (auto& slot : slots_) {
        slot.authoredVolumes.reserve(config_.maxLocalVolumes);
        slot.constants = buffer("Fog.Constants", sizeof(FogConstants), true, true);
        slot.volumes = buffer(
            "Fog.Volumes", std::uint64_t(config_.maxLocalVolumes) * sizeof(FogGpuVolume), true);
        slot.medium = buffer("Fog.Medium", grid_.cells * 32u, false);
        slot.scattering = buffer("Fog.Scattering", grid_.cells * kFogScatteringStride, false);
        slot.history = buffer("Fog.History", grid_.cells * kFogHistoryStride, false);
        slot.integrated = buffer("Fog.Integrated", grid_.cells * 16u, false, false, true);
        if (!slot.constants || !slot.volumes || !slot.medium || !slot.scattering || !slot.history
            || !slot.integrated)
            return FogResult::failure("FOG-BUFFER-CREATE");
        for (std::uint32_t p = 0; p < 5; ++p) {
            slot.sets[p] = arena_->allocateDescriptorSet(
                {pipelines_[p].bindings.name, pipelines_[p].bindings});
            if (!slot.sets[p])
                return FogResult::failure("FOG-DESCRIPTOR-ALLOCATE");
        }
    }
    return {};
}

bool VolumetricFogFeature::frameReady() const noexcept
{
    if (!created() || pending_)
        return false;
    for (std::size_t i = 0; i < slots_.size(); ++i)
        if (i != previousSlot_ && (!slots_[i].completion || slots_[i].completion->isSignaled()))
            return true;
    return false;
}

bool VolumetricFogFeature::idle() const noexcept
{
    if (pending_)
        return false;
    for (const auto& s : slots_)
        if (s.completion && !s.completion->isSignaled())
            return false;
    return true;
}

FogResult VolumetricFogFeature::resize(rhi::Extent2D extent)
{
    if (!created())
        return FogResult::failure("FOG-NOT-CREATED");
    if (!idle())
        return FogResult::failure("FOG-RESIZE-IN-FLIGHT");
    FogGrid planned;
    auto result = planFog(config_, extent, device_->capabilities(), planned);
    if (!result.success)
        return result;
    slots_.clear();
    arena_.reset();
    extent_ = extent;
    grid_ = planned;
    resetHistory();
    result = createSlots();
    if (!result.success)
        release();
    return result;
}

void VolumetricFogFeature::cancelPendingFrame() noexcept
{
    // Caller guarantees the cancelled graph has never been submitted and will
    // not subsequently execute. Generation invalidates its submission token.
    pending_ = false;
    ++generation_;
}

void VolumetricFogFeature::release() noexcept
{
    if (device_ && !idle())
        device_->waitIdle();
    slots_.clear();
    emptyHistory_.reset();
    arena_.reset();
    for (auto& p : pipelines_)
        p = Pipeline{};
    device_ = nullptr;
    historyValid_ = false;
    pending_ = false;
    ++generation_;
}

const char* VolumetricFogFeature::validateSubmission(
    render_graph::PassSubmissionToken token) const noexcept
{
    return pending_ && token.ownerId == cameraId_ && token.generation == generation_
        ? nullptr
        : "FOG-STALE-SUBMISSION";
}

void VolumetricFogFeature::onSubmitted(render_graph::PassSubmissionToken token,
    const std::shared_ptr<const rhi::GpuFence>& completion) noexcept
{
    if (validateSubmission(token) || !completion) {
        historyValid_ = false;
        return;
    }
    slots_[pendingSlot_].completion = completion;
    // The previous slot cannot be overwritten while this submission reads it.
    if (slots_[pendingSlot_].parameters.counts[3])
        slots_[pendingPrevious_].completion = completion;
    previousSlot_ = pendingSlot_;
    historyValid_ = true;
    pending_ = false;
}

void VolumetricFogFeature::bindBuffer(rhi::DescriptorSet& set,
    std::uint32_t binding,
    rhi::DescriptorType type,
    const rhi::GpuBuffer& buffer)
{
    require(set.write({binding, type, buffer.id(), rhi::DescriptorResourceKind::Buffer}));
}

void VolumetricFogFeature::bindTexture(FrameSlot& slot,
    std::uint32_t viewSlot,
    rhi::DescriptorSet& set,
    std::uint32_t binding,
    rhi::GpuTexture& texture,
    bool depth)
{
    rhi::TextureViewDesc desc;
    desc.label = "Fog.InputView";
    desc.format = texture.desc().format;
    desc.usage = rhi::TextureUsage::Sampled;
    desc.aspect = depth ? rhi::TextureAspect::Depth : rhi::TextureAspect::Color;
    slot.views[viewSlot] = device_->createTextureView(texture, desc);
    if (!slot.views[viewSlot])
        throw std::runtime_error("FOG-TEXTURE-VIEW");
    require(set.write({binding,
        rhi::DescriptorType::SampledTexture,
        slot.views[viewSlot]->id(),
        rhi::DescriptorResourceKind::TextureView}));
}

void VolumetricFogFeature::recordCompute(
    render_graph::RenderContext& context, std::uint32_t pass, std::uint32_t slot, std::uint32_t z)
{
    auto encoder = context.commands().beginComputePass({pipelines_[pass].bindings.name});
    if (!encoder)
        throw std::runtime_error("FOG-COMPUTE-ENCODER");
    auto* timing = context.gpuTimingPool();
    if (timing)
        require(encoder->writeTimestamp(*timing, context.gpuTimingBeginQuery()));
    require(encoder->setPipeline(*pipelines_[pass].compute));
    require(encoder->bindDescriptorSet(0u, *slots_[slot].sets[pass]));
    require(encoder->dispatch((grid_.width + 7u) / 8u, (grid_.height + 7u) / 8u, z));
    if (timing)
        require(encoder->writeTimestamp(*timing, context.gpuTimingEndQuery()));
    require(encoder->end());
}

FogResult VolumetricFogFeature::contributeGraph(const FogGraphContext& context, FogOutputs& output)
{
    output = {};
    if (!created())
        return FogResult::failure("FOG-NOT-CREATED");
    if (pending_)
        return FogResult::failure("FOG-GRAPH-PENDING");
    if (context.cameraId != cameraId_)
        return FogResult::failure("FOG-CAMERA-IDENTITY");
    if (!finite(context.ambientRadiance) || context.ambientRadiance.x < 0
        || context.ambientRadiance.y < 0 || context.ambientRadiance.z < 0
        || !std::isfinite(context.ambientIntensity) || context.ambientIntensity < 0)
        return FogResult::failure("FOG-AMBIENT-RADIANCE");
    const auto& spatial = context.spatialAmbient;
    if (spatialAmbient_
        && (!spatial.irradiance.id || !spatial.moments.id || !spatial.states.id
            || !spatial.constants.id || !spatial.sampler.isValid()))
        return FogResult::failure("FOG-SPATIAL-AMBIENT-INPUT");
    if (spatialAmbient_
        && (context.graph.textureDesc(spatial.irradiance).format != rhi::TextureFormat::Rgba16Float
            || context.graph.textureDesc(spatial.moments).format
                != rhi::TextureFormat::Rgba16Float))
        return FogResult::failure("FOG-SPATIAL-AMBIENT-FORMAT");
    if (!std::isfinite(context.nearPlane) || context.nearPlane <= 0
        || config_.distance <= context.nearPlane || !std::isfinite(context.time)
        || !finite(context.cameraPosition))
        return FogResult::failure("FOG-CAMERA-RANGE");
    if (!std::isfinite(context.projectionJitter.x) || !std::isfinite(context.projectionJitter.y)
        || std::fabs(context.projectionJitter.x) > 0.05f
        || std::fabs(context.projectionJitter.y) > 0.05f)
        return FogResult::failure("FOG-CAMERA-JITTER");
    auto historyProjection = context.projection;
    historyProjection.m[2] -= context.projectionJitter.x;
    historyProjection.m[6] -= context.projectionJitter.y;
    auto vp = core::multiply(context.projection, context.view);
    rhi::semantics::applyRenderTargetSamplingYConvention(vp.m.data(), device_->backend());
    const auto inv = core::inverse(vp);
    if (!inverseValid(vp, inv))
        return FogResult::failure("FOG-CAMERA-MATRIX");
    const auto& light = context.primaryLight;
    if (!light.objectId.valid() || light.type != scene3d::LightType::Directional
        || !finite(light.worldDirection) || core::length(light.worldDirection) == 0
        || !finite(light.color) || light.color.x < 0 || light.color.y < 0 || light.color.z < 0
        || !std::isfinite(light.intensity) || light.intensity < 0)
        return FogResult::failure("FOG-PRIMARY-LIGHT");
    if (context.scene.fogVolumes().size() > config_.maxLocalVolumes)
        return FogResult::failure("FOG-VOLUME-BUDGET");
    auto result = buildFogVolumes(context.scene, volumeUpload_);
    if (!result.success)
        return result;
    const auto& colorDesc = context.graph.textureDesc(context.color);
    const auto& depthDesc = context.graph.textureDesc(context.depth);
    if (colorDesc.format != rhi::TextureFormat::Rgba16Float
        || depthDesc.format != rhi::TextureFormat::D32Float
        || colorDesc.extent.width != extent_.width || colorDesc.extent.height != extent_.height
        || depthDesc.extent != colorDesc.extent || depthDesc.sampleCount != 1
        || colorDesc.sampleCount != 1)
        return FogResult::failure("FOG-ATTACHMENT-CONTRACT");
    const auto& shadow = context.shadow;
    if (light.castShadow) {
        if ((shadow.selection != FogShadowCascadeSelection::ViewDepth
                && shadow.selection != FogShadowCascadeSelection::DistanceFromOrigin)
            || !finite(shadow.selectionOrigin))
            return FogResult::failure("FOG-SHADOW-SELECTION");
        if (shadow.depth.id == 0u || shadow.cascadeCount == 0 || shadow.cascadeCount > 4
            || !std::isfinite(shadow.depthBias) || shadow.depthBias < 0)
            return FogResult::failure("FOG-SHADOW-INPUT");
        if (context.graph.textureDesc(shadow.depth).format != rhi::TextureFormat::D32Float)
            return FogResult::failure("FOG-SHADOW-FORMAT");
        float last = 0;
        for (std::uint32_t i = 0; i < shadow.cascadeCount; ++i) {
            const auto r = shadow.uvRects[i];
            if (!inverseValid(shadow.matrices[i], core::inverse(shadow.matrices[i]))
                || !std::isfinite(shadow.cascadeEnds[i]) || shadow.cascadeEnds[i] <= last
                || !finite(core::Vec3{r.x, r.y, r.z}) || !std::isfinite(r.w) || r.x < 0 || r.y < 0
                || r.z <= 0 || r.w <= 0 || r.x + r.z > 1 || r.y + r.w > 1)
                return FogResult::failure("FOG-SHADOW-METADATA");
            last = shadow.cascadeEnds[i];
        }
    }
    const auto previous = previousSlot_;
    std::uint32_t index = static_cast<std::uint32_t>(slots_.size());
    for (std::uint32_t i = 0; i < slots_.size(); ++i)
        if (i != previous && (!slots_[i].completion || slots_[i].completion->isSignaled())) {
            index = i;
            break;
        }
    if (index == slots_.size())
        return FogResult::failure("FOG-FRAMES-IN-FLIGHT");
    auto& slot = slots_[index];
    auto& p = slot.parameters;
    p = {};
    p.inverseViewProjection = inv;
    p.view = context.view;
    p.shadowMatrices = shadow.matrices;
    for (std::uint32_t i = 0; i < shadow.cascadeCount; ++i)
        rhi::semantics::applyRenderTargetSamplingYConvention(
            p.shadowMatrices[i].m.data(), device_->backend());
    const auto& old = slots_[previous];
    bool history = historyValid_ && config_.temporal && !context.cameraCut
        && old.sceneRevision == context.sceneRevision && context.time >= old.time
        && old.historyProjection.m == historyProjection.m
        && old.parameters.depthTime.x == context.nearPlane;
    p.grid = {grid_.width, grid_.height, grid_.depth, config_.stratifiedShadows ? 0u : 1u};
    p.depthTime = {
        context.nearPlane, config_.distance, context.time, history ? context.time - old.time : 0};
    p.cameraPosition = {
        context.cameraPosition.x, context.cameraPosition.y, context.cameraPosition.z, 0};
    if (context.scene.hasFog()) {
        const auto& d = context.scene.fog();
        p.globalMedium = {d.density, d.heightFalloff, d.baseHeight, 0};
        p.globalColorAnisotropy = {
            d.scatteringColor.x, d.scatteringColor.y, d.scatteringColor.z, d.anisotropy};
    }
    const auto dir = core::normalize(light.worldDirection);
    p.globalMedium.w = context.ambientIntensity;
    p.lightDirectionIntensity = {dir.x, dir.y, dir.z, light.intensity};
    p.lightColor = {light.color.x, light.color.y, light.color.z, shadow.depthBias};
    p.ambientRadiance = {context.ambientRadiance.x,
        context.ambientRadiance.y,
        context.ambientRadiance.z,
        spatialAmbient_ && spatial.ready ? 1.0f : 0.0f};
    p.shadowSelection = {shadow.selectionOrigin.x,
        shadow.selectionOrigin.y,
        shadow.selectionOrigin.z,
        shadow.selection == FogShadowCascadeSelection::DistanceFromOrigin ? 1.0f : 0.0f};
    history = history && old.authoredVolumes.size() == volumeUpload_.size()
        && (volumeUpload_.empty()
            || std::memcmp(old.authoredVolumes.data(),
                   volumeUpload_.data(),
                   volumeUpload_.size() * sizeof(FogGpuVolume))
                == 0)
        && std::memcmp(&old.parameters.globalMedium, &p.globalMedium, sizeof(core::Vec4)) == 0
        && std::memcmp(
               &old.parameters.globalColorAnisotropy, &p.globalColorAnisotropy, sizeof(core::Vec4))
            == 0
        && std::memcmp(&old.parameters.lightDirectionIntensity,
               &p.lightDirectionIntensity,
               sizeof(core::Vec4))
            == 0
        && std::memcmp(&old.parameters.lightColor, &p.lightColor, sizeof(core::Vec4)) == 0
        && std::memcmp(&old.parameters.ambientRadiance, &p.ambientRadiance, sizeof(core::Vec4)) == 0
        && old.parameters.counts[1] == (context.scene.hasFog() ? 1u : 0u)
        && old.parameters.counts[2] == (light.castShadow ? shadow.cascadeCount : 0u);
    if (history && light.castShadow) {
        history =
            std::memcmp(&old.parameters.shadowSelection, &p.shadowSelection, sizeof(core::Vec4))
            == 0;
        const auto ends = old.parameters.cascadeEnds;
        const std::array<float, 4> oldEnds = {ends.x, ends.y, ends.z, ends.w};
        for (std::uint32_t i = 0; i < shadow.cascadeCount; ++i) {
            const auto& r = shadow.uvRects[i];
            const auto& oldRect = old.parameters.shadowRects[i];
            history = history && old.parameters.shadowMatrices[i].m == p.shadowMatrices[i].m
                && oldRect.x == r.x && oldRect.y == r.y && oldRect.z == r.z && oldRect.w == r.w
                && oldEnds[i] == shadow.cascadeEnds[i];
        }
    }
    p.previousView = history ? old.parameters.view : context.view;
    p.previousViewProjection = vp;
    if (history) {
        p.previousViewProjection = core::multiply(old.projection, old.parameters.view);
        rhi::semantics::applyRenderTargetSamplingYConvention(
            p.previousViewProjection.m.data(), device_->backend());
    }
    p.depthTime.w = history ? context.time - old.time : 0;
    slot.authoredVolumes.assign(volumeUpload_.begin(), volumeUpload_.end());
    p.shadowRects = shadow.uvRects;
    p.cascadeEnds = {
        shadow.cascadeEnds[0], shadow.cascadeEnds[1], shadow.cascadeEnds[2], shadow.cascadeEnds[3]};
    p.temporal = {config_.historyWeight, config_.mediumRejection, config_.depthRejection, 0};
    const auto jitter = config_.temporal
        ? core::halton235(static_cast<std::uint32_t>(generation_ % 16u) + 1u)
        : core::Vec3{0.5f, 0.5f, 0.5f};
    p.extentJitter = {float(extent_.width), float(extent_.height), jitter.x, jitter.y};
    p.cameraPosition.w = jitter.z;
    p.counts = {static_cast<std::uint32_t>(volumeUpload_.size()),
        context.scene.hasFog() ? 1u : 0u,
        light.castShadow ? shadow.cascadeCount : 0u,
        history ? 1u : 0u};
    slot.projection = context.projection;
    slot.historyProjection = historyProjection;
    slot.time = context.time;
    slot.sceneRevision = context.sceneRevision;
    const auto constantsWritten = device_->writeBuffer(*slot.constants, &p, sizeof(p));
    if (!constantsWritten.success)
        return {false, constantsWritten.diagnostics};
    if (!volumeUpload_.empty()) {
        const auto volumesWritten = device_->writeBuffer(
            *slot.volumes, volumeUpload_.data(), volumeUpload_.size() * sizeof(FogGpuVolume));
        if (!volumesWritten.success)
            return {false, volumesWritten.diagnostics};
    }
    using D = rhi::DescriptorType;
    for (auto& set : slot.sets)
        bindBuffer(*set, 0, D::UniformBuffer, *slot.constants);
    bindBuffer(*slot.sets[0], 1, D::ReadOnlyStorageBuffer, *slot.volumes);
    bindBuffer(*slot.sets[0], 2, D::StorageBuffer, *slot.medium);
    bindBuffer(*slot.sets[1], 1, D::ReadOnlyStorageBuffer, *slot.medium);
    bindBuffer(*slot.sets[1], 3, D::StorageBuffer, *slot.scattering);
    bindBuffer(*slot.sets[2], 1, D::ReadOnlyStorageBuffer, *slot.medium);
    bindBuffer(*slot.sets[2], 2, D::ReadOnlyStorageBuffer, *slot.scattering);
    // FogPhaseHistory/1: initialized same-stride sentinel, never read without history.
    const auto historyInput = history ? slots_[previous].history : emptyHistory_;
    bindBuffer(*slot.sets[2], 3, D::ReadOnlyStorageBuffer, *historyInput);
    bindBuffer(*slot.sets[2], 5, D::StorageBuffer, *slot.history);
    bindBuffer(*slot.sets[3], 1, D::ReadOnlyStorageBuffer, *slot.history);
    bindBuffer(*slot.sets[3], 2, D::StorageBuffer, *slot.integrated);
    bindBuffer(*slot.sets[4], 1, D::ReadOnlyStorageBuffer, *slot.integrated);
    auto& graph = context.graph;
    auto constants = graph.importBufferRef(slot.constants),
         volumes = graph.importBufferRef(slot.volumes);
    auto medium = graph.importBufferRef(slot.medium),
         scattering = graph.importBufferRef(slot.scattering);
    auto resolved = graph.importBufferRef(slot.history),
         integrated = graph.importBufferRef(slot.integrated);
    auto oldHistory = graph.importBufferRef(historyInput);
    auto shadowHandle = light.castShadow ? shadow.depth : context.depth;
    graph.addPass("Fog.Inject")
        .kind(render_graph::PassKind::Compute)
        .queue(render_graph::QueueRole::Graphics)
        .read(constants, rhi::ResourceState::ShaderRead)
        .read(volumes, rhi::ResourceState::ShaderRead)
        .write(medium, rhi::ResourceState::ShaderWrite)
        .execute([this, index](render_graph::RenderContext& c) {
            recordCompute(c, 0, index, grid_.depth);
        });
    auto scatter = graph.addPass("Fog.Scatter")
                       .kind(render_graph::PassKind::Compute)
                       .queue(render_graph::QueueRole::Graphics)
                       .read(constants, rhi::ResourceState::ShaderRead)
                       .read(medium, rhi::ResourceState::ShaderRead)
                       .read(shadowHandle, rhi::ResourceState::ShaderRead)
                       .write(scattering, rhi::ResourceState::ShaderWrite);
    if (spatialAmbient_) {
        scatter.read(spatial.irradiance, rhi::ResourceState::ShaderRead)
            .read(spatial.moments, rhi::ResourceState::ShaderRead)
            .read(spatial.states, rhi::ResourceState::ShaderRead)
            .read(spatial.constants, rhi::ResourceState::ShaderRead);
    }
    scatter.execute([this, index, shadowHandle, spatial, &graph](render_graph::RenderContext& c) {
        bindTexture(slots_[index], 0, *slots_[index].sets[1], 2, graph.texture(shadowHandle), true);
        if (spatialAmbient_) {
            auto& slot = slots_[index];
            auto& set = *slot.sets[1];
            bindTexture(slot, 4, set, 4, graph.texture(spatial.irradiance), false);
            bindTexture(slot, 5, set, 5, graph.texture(spatial.moments), false);
            require(set.write({6u,
                rhi::DescriptorType::Sampler,
                spatial.sampler,
                rhi::DescriptorResourceKind::Sampler}));
            bindBuffer(
                set, 7, rhi::DescriptorType::ReadOnlyStorageBuffer, graph.buffer(spatial.states));
            bindBuffer(set, 8, rhi::DescriptorType::UniformBuffer, graph.buffer(spatial.constants));
        }
        recordCompute(c, 1, index, grid_.depth);
    });
    auto temporal = graph.addPass("Fog.Temporal")
                        .kind(render_graph::PassKind::Compute)
                        .queue(render_graph::QueueRole::Graphics)
                        .read(constants, rhi::ResourceState::ShaderRead)
                        .read(medium, rhi::ResourceState::ShaderRead)
                        .read(scattering, rhi::ResourceState::ShaderRead)
                        .read(context.depth, rhi::ResourceState::ShaderRead)
                        .write(resolved, rhi::ResourceState::ShaderWrite);
    temporal.read(oldHistory, rhi::ResourceState::ShaderRead);
    temporal.execute([this, index, depth = context.depth, &graph](render_graph::RenderContext& c) {
        bindTexture(slots_[index], 1, *slots_[index].sets[2], 4, graph.texture(depth), true);
        recordCompute(c, 2, index, grid_.depth);
    });
    graph.addPass("Fog.Integrate")
        .kind(render_graph::PassKind::Compute)
        .queue(render_graph::QueueRole::Graphics)
        .read(constants, rhi::ResourceState::ShaderRead)
        .read(resolved, rhi::ResourceState::ShaderRead)
        .write(integrated, rhi::ResourceState::ShaderWrite)
        .execute([this, index](render_graph::RenderContext& c) {
            recordCompute(c, 3, index, 1);
        });
    auto color = graph.createTransientTexture({"Fog.HdrComposite",
        {extent_.width, extent_.height, 1},
        rhi::TextureFormat::Rgba16Float,
        rhi::TextureUsage::RenderAttachment | rhi::TextureUsage::Sampled
            | rhi::TextureUsage::CopySrc,
        rhi::MemoryUsage::GpuOnly});
    pending_ = true;
    pendingSlot_ = index;
    pendingPrevious_ = previous;
    ++generation_;
    graph.addPass("Fog.Composite")
        .read(constants, rhi::ResourceState::ShaderRead)
        .read(integrated, rhi::ResourceState::ShaderRead)
        .read(context.color, rhi::ResourceState::ShaderRead)
        .read(context.depth, rhi::ResourceState::ShaderRead)
        .writeAttachment(
            color, rhi::ResourceState::ColorAttachment, rhi::LoadOp::DontCare, rhi::StoreOp::Store)
        .onSubmitted(*this, {cameraId_, generation_})
        .executeRenderPass([this, index, source = context.color, depth = context.depth, &graph](
                               render_graph::RenderPassContext& c) {
            auto& s = slots_[index];
            bindTexture(s, 2, *s.sets[4], 2, graph.texture(source), false);
            bindTexture(s, 3, *s.sets[4], 3, graph.texture(depth), true);
            require(bindRasterPipeline(c.renderPass(), *pipelines_[4].graphics, extent_));
            require(c.renderPass().bindDescriptorSet(0u, *s.sets[4]));
            require(c.renderPass().draw(3, 1, 0, 0));
        });
    output = {color, integrated, constants, history, grid_.budgetBytes};
    return {};
}

} // namespace arp::renderer::fog

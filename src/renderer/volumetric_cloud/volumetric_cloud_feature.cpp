#include "renderer/volumetric_cloud/volumetric_cloud_feature.h"

#include "core/math/matrix_validation.h"
#include "renderer/raster_viewport.h"
#include "renderer/renderer_common.h"
#include "renderer/shader_package_reflection.h"
#include "renderer/temporal/jitter.h"
#include "rhi/semantics/render_target_coordinates.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace arp::renderer::cloud {
namespace {

template <class Result> void require(const Result& result)
{
    if (!result.success)
        throw std::runtime_error(core::toStdString(result.diagnostics));
}

bool finite(core::Vec3 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool radiance(core::Vec3 value) noexcept
{
    return finite(value) && value.x >= 0 && value.y >= 0 && value.z >= 0;
}

bool validInverse(const core::Mat4& matrix, const core::Mat4& inverse) noexcept
{
    return core::isApproximateInverse(matrix, inverse, 0.002f);
}

bool same(core::Vec4 a, core::Vec4 b) noexcept
{
    return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
}

void bindBuffer(rhi::DescriptorSet& set,
    std::uint32_t binding,
    rhi::DescriptorType type,
    const rhi::GpuBuffer& buffer)
{
    require(set.write({binding, type, buffer.id(), rhi::DescriptorResourceKind::Buffer}));
}

} // namespace

CloudResult VolumetricCloudFeature::create(const CloudCreateContext& context)
{
    if (created() && !idle())
        return CloudResult::failure("CLOUD-CREATE-IN-FLIGHT");
    release();
    const auto valid = assets::cloud::CloudAssetCooker::validate(context.asset);
    if (!valid.success)
        return CloudResult::failure(valid.diagnostic);
    if (context.shaderDirectory.empty() || !context.cameraId)
        return CloudResult::failure("CLOUD-CREATE-IDENTITY");
    assetBytes_ = context.asset.cells.size() * sizeof(core::Vec4);
    auto result = CloudExecutionPlanner::plan(context.config,
        context.extent,
        assetBytes_,
        sizeof(CloudGpuConstants),
        context.device.capabilities(),
        plan_);
    if (!result.success)
        return result;
    device_ = &context.device;
    config_ = context.config;
    cameraId_ = context.cameraId;
    assetDesc_ = context.asset.source;
    shaderDirectory_ = context.shaderDirectory;
    rhi::BufferDesc desc;
    desc.label = "Cloud.DensityAsset";
    desc.sizeBytes = assetBytes_;
    desc.usage = rhi::ResourceUsage::StorageBuffer | rhi::ResourceUsage::CopyDst;
    desc.memory = rhi::MemoryUsage::CpuToGpu;
    desc.initialState = rhi::ResourceState::ShaderRead;
    asset_ = core::shareUnique(device_->createBuffer(desc));
    if (!asset_)
        result = CloudResult::failure("CLOUD-ASSET-BUFFER");
    else if (!device_->writeBuffer(*asset_, context.asset.cells.data(), assetBytes_).success)
        result = CloudResult::failure("CLOUD-ASSET-UPLOAD");
    if (result.success)
        result = createPipelines();
    if (result.success)
        result = createSlots();
    if (!result.success)
        release();
    return result;
}

CloudResult VolumetricCloudFeature::createPipelines()
{
    using D = rhi::DescriptorType;
    constexpr const char* names[] = {"cloud_raymarch", "cloud_temporal", "cloud_composite"};
    constexpr D types[3][4] = {
        {D::UniformBuffer, D::ReadOnlyStorageBuffer, D::SampledTexture, D::StorageBuffer},
        {D::UniformBuffer, D::ReadOnlyStorageBuffer, D::ReadOnlyStorageBuffer, D::StorageBuffer},
        {D::UniformBuffer, D::ReadOnlyStorageBuffer, D::SampledTexture, D::SampledTexture}};
    for (std::uint32_t i = 0; i < pipelines_.size(); ++i) {
        auto& p = pipelines_[i];
        const auto stage = i == 2u ? rhi::ShaderStage::Fragment : rhi::ShaderStage::Compute;
        p.bindings.name = names[i];
        for (std::uint32_t binding = 0; binding < 4u; ++binding) {
            rhi::DescriptorBindingDesc d{binding, types[i][binding], stage};
            if (d.type == D::SampledTexture) {
                d.textureViewDimensionConstrained = true;
                d.textureViewDimension = rhi::TextureViewDimension::Texture2D;
                d.sampledTextureFilterable = false;
            }
            d.depthTexture = (i == 0u && binding == 2u) || (i == 2u && binding == 3u);
            p.bindings.bindings.push_back(d);
        }
        rhi::PipelineLayoutDesc layout;
        layout.descriptorLayouts.push_back(p.bindings);
        layout.descriptorSets.push_back({0u, p.bindings.name});
        p.layout = device_->createPipelineLayout(layout);
        if (!p.layout)
            return CloudResult::failure("CLOUD-PIPELINE-LAYOUT");
        const auto path = shaderDirectory_ + "/" + names[i] + ".slang";
        if (i == 2u)
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
            return CloudResult::failure("CLOUD-SHADER-REFLECTION");
        for (const auto& binding : p.bindings.bindings)
            p.package.reflection.descriptorBindings.push_back(
                rhi::makeShaderReflectionBinding(0u, binding));
        if (!rhi::validateShaderPackage(p.package).success)
            return CloudResult::failure("CLOUD-SHADER-BINDING");
        if (i == 2u) {
            auto d = detail::makeRenderPipelineDesc(
                names[i], *p.layout, p.package, rhi::TextureFormat::Rgba16Float, plan_.extent);
            useDynamicRasterViewport(d);
            d.colorTargets = {{rhi::TextureFormat::Rgba16Float}};
            d.depthStencil.depthTestEnabled = false;
            d.depthStencil.depthWriteEnabled = false;
            d.cullMode = rhi::CullMode::None;
            d.frontFace = rhi::FrontFace::CounterClockwise;
            d.topology = rhi::PrimitiveTopology::TriangleList;
            p.graphics = device_->createRenderPipeline(d);
            if (!p.graphics)
                return CloudResult::failure("CLOUD-GRAPHICS-PIPELINE");
        } else {
            rhi::ComputePipelineDesc d;
            d.label = names[i];
            d.layout = p.layout.get();
            d.shaderPackage = &p.package;
            d.computeShader = p.package.entrypoints.front();
            p.compute = device_->createComputePipeline(d);
            if (!p.compute)
                return CloudResult::failure("CLOUD-COMPUTE-PIPELINE");
        }
    }
    return {};
}

CloudResult VolumetricCloudFeature::createSlots()
{
    arena_ = device_->createDescriptorArena({"Cloud.Descriptors",
        config_.framesInFlight * kCloudPipelineCount,
        config_.framesInFlight * kCloudDescriptorElementsPerSlot});
    if (!arena_)
        return CloudResult::failure("CLOUD-DESCRIPTOR-BUDGET");
    slots_.resize(config_.framesInFlight);
    for (auto& slot : slots_) {
        const auto buffer = [this](const char* label, std::uint64_t size, bool uniform) {
            rhi::BufferDesc d;
            d.label = label;
            d.sizeBytes = size;
            d.usage = uniform ? rhi::ResourceUsage::UniformBuffer | rhi::ResourceUsage::CopyDst
                              : rhi::ResourceUsage::StorageBuffer | rhi::ResourceUsage::CopySrc;
            d.memory = uniform ? rhi::MemoryUsage::CpuToGpu : rhi::MemoryUsage::GpuOnly;
            d.initialState =
                uniform ? rhi::ResourceState::ShaderRead : rhi::ResourceState::Undefined;
            return core::shareUnique(device_->createBuffer(d));
        };
        slot.constants = buffer("Cloud.Constants", sizeof(CloudGpuConstants), true);
        slot.raw = buffer("Cloud.Raw", plan_.sampleBufferBytes, false);
        slot.resolved = buffer("Cloud.Resolved", plan_.sampleBufferBytes, false);
        if (!slot.constants || !slot.raw || !slot.resolved)
            return CloudResult::failure("CLOUD-BUFFER-CREATE");
        rhi::TextureDesc d;
        d.label = "Cloud.HdrComposite";
        d.extent = {plan_.extent.width, plan_.extent.height, 1u};
        d.format = rhi::TextureFormat::Rgba16Float;
        d.usage = rhi::TextureUsage::RenderAttachment | rhi::TextureUsage::Sampled
            | rhi::TextureUsage::CopySrc;
        d.memory = rhi::MemoryUsage::GpuOnly;
        slot.color = core::shareUnique(device_->createTexture(d));
        if (!slot.color)
            return CloudResult::failure("CLOUD-TEXTURE-CREATE");
        for (std::uint32_t i = 0; i < pipelines_.size(); ++i) {
            slot.sets[i] = arena_->allocateDescriptorSet(
                {pipelines_[i].bindings.name, pipelines_[i].bindings});
            if (!slot.sets[i])
                return CloudResult::failure("CLOUD-DESCRIPTOR-ALLOCATE");
        }
    }
    return {};
}

bool VolumetricCloudFeature::frameReady() const noexcept
{
    if (!created() || pending_)
        return false;
    for (std::size_t i = 0; i < slots_.size(); ++i)
        if (i != previousSlot_ && (!slots_[i].completion || slots_[i].completion->isSignaled()))
            return true;
    return false;
}

bool VolumetricCloudFeature::idle() const noexcept
{
    if (pending_)
        return false;
    for (const auto& slot : slots_)
        if (slot.completion && !slot.completion->isSignaled())
            return false;
    return true;
}

CloudResult VolumetricCloudFeature::resize(rhi::Extent2D extent)
{
    if (!created())
        return CloudResult::failure("CLOUD-NOT-CREATED");
    if (!idle())
        return CloudResult::failure("CLOUD-RESIZE-IN-FLIGHT");
    CloudExecutionPlan next;
    const auto planned = CloudExecutionPlanner::plan(
        config_, extent, assetBytes_, sizeof(CloudGpuConstants), device_->capabilities(), next);
    if (!planned.success)
        return planned;
    // Idle replacement releases old slots before allocating the new set: no
    // hidden double-budget peak and no blocking frame-path wait.
    slots_.clear();
    arena_.reset();
    plan_ = next;
    historyValid_ = false;
    samplingFrame_ = 0u;
    const auto result = createSlots();
    if (!result.success)
        release();
    return result;
}

CloudResult VolumetricCloudFeature::cancelPendingFrame() noexcept
{
    if (executionValidated_)
        return CloudResult::failure("CLOUD-CANCEL-AFTER-EXECUTION-VALIDATION");
    pending_ = false;
    ++generation_;
    return {};
}

void VolumetricCloudFeature::release() noexcept
{
    // Explicit teardown only. Admission/resize/frame recording never waits.
    if (device_ && !idle())
        device_->waitIdle();
    slots_.clear();
    arena_.reset();
    asset_.reset();
    for (auto& p : pipelines_)
        p = Pipeline{};
    device_ = nullptr;
    pending_ = historyValid_ = false;
    samplingFrame_ = 0u;
    executionValidated_ = false;
    ++generation_;
}

const char* VolumetricCloudFeature::validateSubmission(
    render_graph::PassSubmissionToken token) const noexcept
{
    if (!pending_ || token.ownerId != cameraId_ || token.generation != generation_)
        return "CLOUD-STALE-SUBMISSION";
    // FrameGraph validates before recording and again before queue acceptance.
    // No rejection/throw receipt reaches this observer, so cancellation after
    // this boundary conservatively requires explicit teardown and device drain.
    executionValidated_ = true;
    return nullptr;
}

void VolumetricCloudFeature::onSubmitted(render_graph::PassSubmissionToken token,
    const std::shared_ptr<const rhi::GpuFence>& completion) noexcept
{
    if (validateSubmission(token) || !completion) {
        historyValid_ = false;
        return;
    }
    slots_[pendingSlot_].completion = completion;
    if (slots_[pendingSlot_].parameters.previousCamera.w > 0)
        slots_[pendingPrevious_].completion = completion;
    previousSlot_ = pendingSlot_;
    pending_ = false;
    executionValidated_ = false;
    historyValid_ = true;
    ++samplingFrame_;
}

void VolumetricCloudFeature::bindTexture(Slot& slot,
    std::uint32_t view,
    rhi::DescriptorSet& set,
    std::uint32_t binding,
    rhi::GpuTexture& texture,
    bool depth)
{
    if (!slot.views[view] || slot.viewedTextures[view] != texture.id()) {
        rhi::TextureViewDesc d;
        d.label = "Cloud.InputView";
        d.format = texture.desc().format;
        d.usage = rhi::TextureUsage::Sampled;
        d.aspect = depth ? rhi::TextureAspect::Depth : rhi::TextureAspect::Color;
        slot.views[view] = device_->createTextureView(texture, d);
        if (!slot.views[view])
            throw std::runtime_error("CLOUD-TEXTURE-VIEW");
        slot.viewedTextures[view] = texture.id();
    }
    require(set.write({binding,
        rhi::DescriptorType::SampledTexture,
        slot.views[view]->id(),
        rhi::DescriptorResourceKind::TextureView}));
}

void VolumetricCloudFeature::recordCompute(
    render_graph::RenderContext& context, std::uint32_t pass, std::uint32_t slot)
{
    auto encoder = context.commands().beginComputePass({pipelines_[pass].bindings.name});
    if (!encoder)
        throw std::runtime_error("CLOUD-COMPUTE-ENCODER");
    if (context.gpuTimingPool())
        require(encoder->writeTimestamp(*context.gpuTimingPool(), context.gpuTimingBeginQuery()));
    require(encoder->setPipeline(*pipelines_[pass].compute));
    require(encoder->bindDescriptorSet(0u, *slots_[slot].sets[pass]));
    require(encoder->dispatch(
        plan_.dispatchGroups.width, plan_.dispatchGroups.height, plan_.dispatchGroups.depth));
    if (context.gpuTimingPool())
        require(encoder->writeTimestamp(*context.gpuTimingPool(), context.gpuTimingEndQuery()));
    require(encoder->end());
}

CloudResult VolumetricCloudFeature::contributeGraph(
    const CloudGraphContext& c, CloudOutputs& output)
{
    output = {};
    if (!created())
        return CloudResult::failure("CLOUD-NOT-CREATED");
    if (pending_)
        return CloudResult::failure("CLOUD-GRAPH-PENDING");
    if (c.cameraId != cameraId_)
        return CloudResult::failure("CLOUD-CAMERA-IDENTITY");
    const auto layerValid = CloudContract::validate(c.layer);
    if (!layerValid.success)
        return layerValid;
    if (!finite(c.cameraPosition) || !finite(c.sunDirection) || !radiance(c.sunRadiance)
        || !radiance(c.ambientRadiance) || !std::isfinite(c.time)
        || std::fabs(core::length(c.sunDirection) - 1.0f) > 0.0001f)
        return CloudResult::failure("CLOUD-FRAME-INPUT");
    if (!std::isfinite(c.projectionJitter.x) || !std::isfinite(c.projectionJitter.y))
        return CloudResult::failure("CLOUD-CAMERA-JITTER");
    if (!c.color.id || !c.depth.id)
        return CloudResult::failure("CLOUD-ATTACHMENT-CONTRACT");
    const auto& color = c.graph.textureDesc(c.color);
    const auto& depth = c.graph.textureDesc(c.depth);
    if (color.format != rhi::TextureFormat::Rgba16Float
        || depth.format != rhi::TextureFormat::D32Float || color.extent != depth.extent
        || color.extent.width != plan_.extent.width || color.extent.height != plan_.extent.height
        || color.extent.depth != 1u || color.sampleCount != 1u || depth.sampleCount != 1u)
        return CloudResult::failure("CLOUD-ATTACHMENT-CONTRACT");
    auto vp = core::multiply(c.projection, c.view);
    rhi::semantics::applyRenderTargetSamplingYConvention(vp.m.data(), device_->backend());
    const auto inv = core::inverse(vp);
    if (!validInverse(vp, inv))
        return CloudResult::failure("CLOUD-CAMERA-MATRIX");
    auto canonical = c.projection;
    canonical.m[2] -= c.projectionJitter.x;
    canonical.m[6] -= c.projectionJitter.y;
    const auto& d = c.layer;
    const double rx = double(c.cameraPosition.x) - d.planetCenter.x;
    const double ry = double(c.cameraPosition.y) - d.planetCenter.y;
    const double rz = double(c.cameraPosition.z) - d.planetCenter.z;
    const double groundC = rx * rx + ry * ry + rz * rz - double(d.planetRadius) * d.planetRadius;
    if (!std::isfinite(groundC) || groundC < 0 || groundC > std::numeric_limits<float>::max()
        || !std::isfinite(d.planetRadius * d.planetRadius)
        || !std::isfinite((d.baseAltitude + d.thickness)
            * (2.0f * d.planetRadius + d.baseAltitude + d.thickness)))
        return CloudResult::failure("CLOUD-CAMERA-SHELL-RANGE");
    std::uint32_t index = static_cast<std::uint32_t>(slots_.size());
    for (std::uint32_t i = 0; i < slots_.size(); ++i)
        if (i != previousSlot_ && (!slots_[i].completion || slots_[i].completion->isSignaled())) {
            index = i;
            break;
        }
    if (index == slots_.size())
        return CloudResult::failure("CLOUD-FRAMES-IN-FLIGHT");
    auto& slot = slots_[index];
    auto& p = slot.parameters;
    p = {};
    p.inverseViewProjection = inv;
    p.cameraTime = {c.cameraPosition.x, c.cameraPosition.y, c.cameraPosition.z, c.time};
    p.cameraRadial = {float(rx), float(ry), float(rz), float(groundC)};
    p.shell = {d.baseAltitude, d.thickness, d.planetRadius, d.extinction};
    p.albedoAnisotropy = {
        d.scatteringAlbedo.x, d.scatteringAlbedo.y, d.scatteringAlbedo.z, d.anisotropy};
    p.periodsCoverage = {d.weatherPeriod, d.shapePeriod, d.detailPeriod, d.coverage};
    p.windErosion = {d.windVelocity.x, d.windVelocity.y, d.windVelocity.z, d.erosion};
    p.sunDirection = {c.sunDirection.x, c.sunDirection.y, c.sunDirection.z, 0};
    p.sunRadiance = {c.sunRadiance.x, c.sunRadiance.y, c.sunRadiance.z, 0};
    p.ambientRadiance = {c.ambientRadiance.x, c.ambientRadiance.y, c.ambientRadiance.z, 0};
    p.dimensions = {plan_.sampleExtent.width,
        plan_.sampleExtent.height,
        plan_.extent.width,
        plan_.extent.height};
    p.sampling = {config_.viewSteps,
        config_.lightSteps,
        assetDesc_.weatherDimension,
        assetDesc_.shapeDimension};
    const auto shapeOffset = assets::cloud::kCloudHeightProfileCount
        + assetDesc_.weatherDimension * assetDesc_.weatherDimension;
    p.assetLayout = {assetDesc_.detailDimension,
        0u,
        shapeOffset,
        shapeOffset
            + assetDesc_.shapeDimension * assetDesc_.shapeDimension * assetDesc_.shapeDimension};
    // CloudTemporalSampling/1: sixteen Halton base-5 stratified phases along
    // each primary ray. Fixed XY centres preserve the opaque depth samples.
    // Only successful submission advances this sequence; reference is midpoint.
    constexpr std::uint32_t samplingPeriod = 16u;
    const auto sampleZ =
        config_.temporal ? core::halton235(samplingFrame_ % samplingPeriod + 1u).z : 0.5f;
    p.jitter = {0.5f, 0.5f, sampleZ, c.orthographic ? 1.0f : 0.0f};
    const auto& old = slots_[previousSlot_];
    const auto& o = old.parameters;
    const bool history = historyValid_ && config_.temporal && !c.cameraCut
        && c.sceneRevision == old.sceneRevision && c.time >= o.cameraTime.w
        && canonical.m == old.canonicalProjection.m && p.jitter.w == o.jitter.w
        && old.planetCenter.x == d.planetCenter.x && old.planetCenter.y == d.planetCenter.y
        && old.planetCenter.z == d.planetCenter.z && same(p.shell, o.shell)
        && same(p.albedoAnisotropy, o.albedoAnisotropy)
        && same(p.periodsCoverage, o.periodsCoverage) && same(p.windErosion, o.windErosion)
        && same(p.sunDirection, o.sunDirection) && same(p.sunRadiance, o.sunRadiance)
        && same(p.ambientRadiance, o.ambientRadiance);
    p.previousViewProjection = history ? old.viewProjection : vp;
    p.previousCamera = history
        ? core::Vec4{o.cameraTime.x, o.cameraTime.y, o.cameraTime.z, 1.0f}
        : core::Vec4{c.cameraPosition.x, c.cameraPosition.y, c.cameraPosition.z, 0.0f};
    p.temporal = {config_.historyWeight,
        config_.historyDepthTolerance,
        config_.maxDistance,
        history ? o.cameraTime.w : c.time};
    slot.viewProjection = vp;
    slot.canonicalProjection = canonical;
    slot.sceneRevision = c.sceneRevision;
    slot.planetCenter = d.planetCenter;
    if (!device_->writeBuffer(*slot.constants, &p, sizeof(p)).success)
        return CloudResult::failure("CLOUD-CONSTANT-UPLOAD");
    using D = rhi::DescriptorType;
    for (auto& set : slot.sets)
        bindBuffer(*set, 0u, D::UniformBuffer, *slot.constants);
    bindBuffer(*slot.sets[0], 1u, D::ReadOnlyStorageBuffer, *asset_);
    bindBuffer(*slot.sets[0], 3u, D::StorageBuffer, *slot.raw);
    bindBuffer(*slot.sets[1], 1u, D::ReadOnlyStorageBuffer, *slot.raw);
    const auto historyInput = history ? old.resolved : slot.raw;
    bindBuffer(*slot.sets[1], 2u, D::ReadOnlyStorageBuffer, *historyInput);
    bindBuffer(*slot.sets[1], 3u, D::StorageBuffer, *slot.resolved);
    bindBuffer(*slot.sets[2], 1u, D::ReadOnlyStorageBuffer, *slot.resolved);
    auto& graph = c.graph;
    const auto constants = graph.importBufferRef(slot.constants);
    const auto asset = graph.importBufferRef(asset_);
    const auto raw = graph.importBufferRef(slot.raw);
    const auto resolved = graph.importBufferRef(slot.resolved);
    const auto previous = graph.importBufferRef(historyInput);
    const auto outputColor = graph.importTextureRef(slot.color);
    graph.addPass("Cloud.Raymarch")
        .kind(render_graph::PassKind::Compute)
        .queue(render_graph::QueueRole::Graphics)
        .read(constants, rhi::ResourceState::ShaderRead)
        .read(asset, rhi::ResourceState::ShaderRead)
        .read(c.depth, rhi::ResourceState::ShaderRead)
        .write(raw, rhi::ResourceState::ShaderWrite)
        .execute([this, index, depthHandle = c.depth, &graph](render_graph::RenderContext& rc) {
            bindTexture(
                slots_[index], 0u, *slots_[index].sets[0], 2u, graph.texture(depthHandle), true);
            recordCompute(rc, 0u, index);
        });
    graph.addPass("Cloud.Temporal")
        .kind(render_graph::PassKind::Compute)
        .queue(render_graph::QueueRole::Graphics)
        .read(constants, rhi::ResourceState::ShaderRead)
        .read(raw, rhi::ResourceState::ShaderRead)
        .read(previous, rhi::ResourceState::ShaderRead)
        .write(resolved, rhi::ResourceState::ShaderWrite)
        .execute([this, index](render_graph::RenderContext& rc) {
            recordCompute(rc, 1u, index);
        });
    pending_ = true;
    executionValidated_ = false;
    pendingSlot_ = index;
    pendingPrevious_ = previousSlot_;
    ++generation_;
    graph.addPass("Cloud.Composite")
        .read(constants, rhi::ResourceState::ShaderRead)
        .read(resolved, rhi::ResourceState::ShaderRead)
        .read(c.color, rhi::ResourceState::ShaderRead)
        .read(c.depth, rhi::ResourceState::ShaderRead)
        .writeAttachment(outputColor,
            rhi::ResourceState::ColorAttachment,
            rhi::LoadOp::DontCare,
            rhi::StoreOp::Store)
        .onSubmitted(*this, {cameraId_, generation_})
        .executeRenderPass([this, index, colorHandle = c.color, depthHandle = c.depth, &graph](
                               render_graph::RenderPassContext& context) {
            auto& encoder = context.renderPass();
            auto& s = slots_[index];
            bindTexture(s, 1u, *s.sets[2], 2u, graph.texture(colorHandle), false);
            bindTexture(s, 0u, *s.sets[2], 3u, graph.texture(depthHandle), true);
            require(bindRasterPipeline(encoder, *pipelines_[2].graphics, plan_.extent));
            require(encoder.bindDescriptorSet(0u, *s.sets[2]));
            require(encoder.draw(3u, 1u, 0u, 0u));
        });
    output = {outputColor, resolved, constants, history, plan_.retainedGpuBytes};
    return {};
}

} // namespace arp::renderer::cloud

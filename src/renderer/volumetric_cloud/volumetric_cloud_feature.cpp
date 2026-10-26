#include "renderer/volumetric_cloud/volumetric_cloud_feature.h"

#include "renderer/raster_viewport.h"
#include "renderer/render_graph/original_frame_graph_execution.h"
#include "renderer/renderer_common.h"
#include "renderer/shader_package_reflection.h"
#include "renderer/volumetric_cloud/cloud_prepared_feature.h"

#include <cstring>
#include <stdexcept>
#include <tuple>

namespace arp::renderer::cloud {

struct VolumetricCloudFeature::PreparedState {
    std::shared_ptr<core::CpuMemoryQuota> quota;
    CloudPreparedPipelines pipelines;
    // Destruction drains history before dropping its pipelines.
    CloudPreparedResources resources;
    std::uint64_t contributedGeneration = 0u;
};

namespace {

bool sameSelection(
    const CloudPreparedPipelineSelection& a, const CloudPreparedPipelineSelection& b) noexcept
{
    return a.contractVersion == b.contractVersion && a.resolve == b.resolve
        && a.reconstruction.contractVersion == b.reconstruction.contractVersion
        && a.reconstruction.maxRadianceRange == b.reconstruction.maxRadianceRange
        && a.reconstruction.maxTransmittanceRange == b.reconstruction.maxTransmittanceRange;
}

auto configFields(const CloudRenderConfig& c) noexcept
{
    return std::tie(c.resolutionDivisor,
        c.viewSteps,
        c.lightSteps,
        c.framesInFlight,
        c.gpuBudgetBytes,
        c.maxDensityEvaluations,
        c.maxDistance,
        c.historyWeight,
        c.historyDepthTolerance,
        c.temporal);
}

bool sameResources(
    const CloudPreparedResourceRequest& a, const CloudPreparedResourceRequest& b) noexcept
{
    return configFields(a.config) == configFields(b.config) && a.extent.width == b.extent.width
        && a.extent.height == b.extent.height && a.assetBytes == b.assetBytes
        && a.maxDescriptorSets == b.maxDescriptorSets
        && a.maxDescriptorElements == b.maxDescriptorElements;
}

template <class Result> void require(const Result& result)
{
    if (!result.success)
        throw std::runtime_error(core::toStdString(result.diagnostics));
}

void bindBuffer(rhi::DescriptorSet& set,
    std::uint32_t binding,
    rhi::DescriptorType type,
    const rhi::GpuBuffer& buffer)
{
    require(set.write({binding, type, buffer.id(), rhi::DescriptorResourceKind::Buffer}));
}

} // namespace

VolumetricCloudFeature::VolumetricCloudFeature() = default;

VolumetricCloudFeature::~VolumetricCloudFeature()
{
    release();
}

CloudResult VolumetricCloudFeature::create(
    const CloudPreparedFeatureCreateContext& c, core::pmr::String& diagnostic)
{
    diagnostic.clear();
    if (created())
        return CloudResult::failure("CLOUD-PREPARED-FEATURE-ALREADY-CREATED");
    if (c.contractVersion != 1u || !c.prototypeOptIn)
        return CloudResult::failure("CLOUD-PREPARED-FEATURE-OPT-IN");
    if (&c.pipelines.device != &c.resources.device
        || !sameSelection(c.pipelines.selection, c.resources.selection)
        || !sameResources(c.pipelines.resources, c.resources.resources)
        || ((c.pipelines.dispatchDomain || c.resources.dispatchDomain)
            && !core::SharedOwnerProjection::sameOwner(
                c.pipelines.dispatchDomain, c.resources.dispatchDomain)))
        return CloudResult::failure("CLOUD-PREPARED-FEATURE-CREATE-MISMATCH");
    if (!c.maxCpuStateBytes || !c.stateQuota || c.stateQuota->limitBytes() != c.maxCpuStateBytes)
        return CloudResult::failure("CLOUD-PREPARED-FEATURE-STATE-QUOTA");
    try {
        core::CpuMemoryQuotaScope state(c.stateQuota);
        const auto capabilities = c.resources.device.capabilities();
        CloudPreparedResourcePlan planned;
        if (const auto result =
                CloudPreparedResources::validateCreate(c.resources, capabilities, planned);
            !result.success)
            return result;
        auto next = core::makeUnique<PreparedState>();
        next->quota = c.stateQuota;
        if (const auto result = next->resources.create(c.resources); !result.success)
            return result;
        if (const auto result = next->pipelines.create(c.pipelines, diagnostic); !result.success)
            return result;
        if (const auto result = preparedSubmission_.attach(*next->resources.history());
            !result.success)
            return result;
        // Publish only after both complete owners exist. Failed prefixes never
        // change this feature and retain their original quota provenance.
        plan_ = planned.raw;
        config_ = c.resources.resources.config;
        assetDesc_ = c.resources.asset.source;
        assetBytes_ = c.resources.resources.assetBytes;
        cameraId_ = c.resources.cameraId;
        device_ = &c.resources.device;
        prepared_ = std::move(next);
        return {};
    } catch (const std::bad_alloc&) {
        return CloudResult::failure("CLOUD-PREPARED-FEATURE-ALLOCATION");
    }
}

const CloudPreparedResources* VolumetricCloudFeature::preparedResources() const noexcept
{
    return prepared_ ? &prepared_->resources : nullptr;
}

const CloudPreparedPipelines* VolumetricCloudFeature::preparedPipelines() const noexcept
{
    return prepared_ ? &prepared_->pipelines : nullptr;
}

CloudPreparedHistory* VolumetricCloudFeature::preparedHistory() noexcept
{
    return prepared_ ? prepared_->resources.history() : nullptr;
}

render_graph::PassSubmissionToken VolumetricCloudFeature::preparedToken() const noexcept
{
    return preparedSubmission_.token();
}

CloudResult VolumetricCloudFeature::prepareFrame(
    const CloudGraphContext& c, CloudPreparedFrameBuffers& output)
{
    output = {};
    if (!prepared_)
        return CloudResult::failure("CLOUD-PREPARED-FEATURE-NOT-CREATED");
    if (preparedSubmission_.token().ownerId)
        return CloudResult::failure("CLOUD-GRAPH-PENDING");
    if (const auto result = prepared_->resources.prepareFrame(c); !result.success)
        return result;
    if (const auto result = preparedSubmission_.beginFrame(); !result.success)
        return result;
    prepared_->contributedGeneration = 0u;
    return prepared_->resources.pendingBuffers(output);
}

CloudResult VolumetricCloudFeature::contributeGraph(
    const CloudPreparedFeatureGraphContext& c, CloudOutputs& output)
{
    output = {};
    if (!prepared_)
        return CloudResult::failure("CLOUD-PREPARED-FEATURE-NOT-CREATED");
    if (c.contractVersion != 1u || !c.prototypeOptIn)
        return CloudResult::failure("CLOUD-PREPARED-FEATURE-GRAPH-OPT-IN");
    CloudPreparedFrameBuffers buffers;
    if (const auto result = prepared_->resources.pendingBuffers(buffers); !result.success)
        return result;
    const auto token = preparedSubmission_.token();
    if (!token.ownerId || c.frameToken.ownerId != token.ownerId
        || c.frameToken.generation != token.generation)
        return CloudResult::failure("CLOUD-PREPARED-HISTORY-STALE-GENERATION");
    if (prepared_->contributedGeneration == buffers.historyGeneration)
        return CloudResult::failure("CLOUD-PREPARED-FEATURE-GRAPH-ALREADY-CONTRIBUTED");
    const auto& sources = c.execution.sources();
    const std::array<const void*, kCloudPipelineCount> expected{
        prepared_->pipelines.raymarch().get(),
        prepared_->pipelines.resolve().get(),
        prepared_->pipelines.composite().get()};
    for (std::size_t pass = 0u; pass < kCloudPipelineCount; ++pass) {
        if (c.pipelines[pass] >= sources.sources().size())
            return CloudResult::failure("CLOUD-PREPARED-FEATURE-PIPELINE-POSITION");
        const auto& source = sources.sources()[c.pipelines[pass]];
        const void* actual = nullptr;
        if (pass == 2u) {
            const auto* p =
                std::get_if<std::shared_ptr<const rhi::OriginalCommandGraphicsPipelineSource>>(
                    &source);
            if (p && *p)
                actual = (*p)->originalObject();
        } else {
            const auto* p =
                std::get_if<std::shared_ptr<const rhi::OriginalCommandComputePipelineSource>>(
                    &source);
            if (p && *p)
                actual = (*p)->originalObject();
        }
        if (actual != expected[pass])
            return CloudResult::failure("CLOUD-PREPARED-FEATURE-PIPELINE-OWNER");
    }
    auto& graph = c.execution.graph();
    const auto import = [&](const auto& lease) {
        // The alias retains the full original lease and its budget custody.
        // GPU mutability is authorized only by the ordinary graph access set.
        return graph.importBufferRef({lease, const_cast<rhi::GpuBuffer*>(lease->originalObject())});
    };
    const bool spatial =
        prepared_->pipelines.selection().resolve == CloudPreparedResolve::Reconstruction1;
    CloudPreparedGraphContext next{spatial ? 3u : 2u,
        true,
        c.execution,
        plan_,
        c.color,
        c.depth,
        c.output,
        import(buffers.constants),
        import(buffers.density),
        import(buffers.raw),
        import(buffers.previous),
        import(buffers.resolved),
        c.pipelines,
        c.descriptors};
    next.history = prepared_->resources.history();
    next.historyGeneration = buffers.historyGeneration;
    if (spatial) {
        next.reconstruction = &next.history->reconstructionPlan();
        next.reconstructionConstants = import(buffers.reconstructionConstants);
    }
    if (const auto result = contributePreparedGraph(next, output, &preparedSubmission_, token);
        !result.success)
        return result;
    if (const auto result = preparedSubmission_.markContributed(token); !result.success)
        return result;
    prepared_->contributedGeneration = buffers.historyGeneration;
    return {};
}

CloudResult VolumetricCloudFeature::resize(const CloudPreparedResourceCreateContext& c)
try {
    if (!prepared_)
        return CloudResult::failure("CLOUD-PREPARED-FEATURE-NOT-CREATED");
    if (&c.device != device_ || c.cameraId != cameraId_
        || !sameSelection(c.selection, prepared_->pipelines.selection()))
        return CloudResult::failure("CLOUD-PREPARED-FEATURE-RESIZE-MISMATCH");
    core::CpuMemoryQuotaScope state(prepared_->quota);
    const auto capabilities = c.device.capabilities();
    CloudPreparedResourcePlan planned;
    if (const auto result = CloudPreparedResources::validateCreate(c, capabilities, planned);
        !result.success)
        return result;
    if (!prepared_->resources.history()->idle())
        return CloudResult::failure("CLOUD-RESIZE-IN-FLIGHT");
    preparedSubmission_.detach();
    if (const auto result = prepared_->resources.releaseIdle(); !result.success) {
        preparedSubmission_.attach(*prepared_->resources.history());
        return result;
    }
    // Only idle replacement: release the old allowance before new allocation.
    // Surviving original graph leases still charge their authored budgets.
    if (const auto result = prepared_->resources.create(c); !result.success) {
        release();
        return result;
    }
    if (const auto result = preparedSubmission_.attach(*prepared_->resources.history());
        !result.success) {
        release();
        return result;
    }
    plan_ = planned.raw;
    config_ = c.resources.config;
    assetDesc_ = c.asset.source;
    assetBytes_ = c.resources.assetBytes;
    prepared_->contributedGeneration = 0u;
    return {};
} catch (const std::bad_alloc&) {
    return CloudResult::failure("CLOUD-PREPARED-FEATURE-ALLOCATION");
}

CloudResult VolumetricCloudFeature::resetHistory() noexcept
{
    if (prepared_) {
        const auto result = prepared_->resources.history()->resetHistory();
        if (result.success)
            preparedSubmission_.invalidateFrame();
        return result;
    }
    if (pending_)
        return CloudResult::failure("CLOUD-RESET-PENDING-FRAME");
    historyValid_ = false;
    samplingFrame_ = 0u;
    return {};
}

CloudResult VolumetricCloudFeature::create(const CloudCreateContext& context)
{
    if (prepared_)
        return CloudResult::failure("CLOUD-PREPARED-FEATURE-ALREADY-CREATED");
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
            p.package = renderer::detail::makeGraphicsShaderPackage(names[i], path, 0u);
        else {
            p.package.label = names[i];
            p.package.slangModulePath = path;
            p.package.entrypoints.push_back(
                {path, "computeMain", stage, rhi::ShaderSourceLanguage::Slang});
            p.package.artifacts.spirvPath = renderer::detail::replaceShaderExtension(path, ".spv");
            p.package.artifacts.mslPath = renderer::detail::replaceShaderExtension(path, ".metal");
            p.package.artifacts.wgslPath = renderer::detail::replaceShaderExtension(path, ".wgsl");
            p.package.artifacts.reflectionPath =
                renderer::detail::replaceShaderExtension(path, ".reflection.yaml");
        }
        if (!hydrateShaderPackageReflectionFromArtifact(p.package))
            return CloudResult::failure("CLOUD-SHADER-REFLECTION");
        for (const auto& binding : p.bindings.bindings)
            p.package.reflection.descriptorBindings.push_back(
                rhi::makeShaderReflectionBinding(0u, binding));
        if (!rhi::validateShaderPackage(p.package).success)
            return CloudResult::failure("CLOUD-SHADER-BINDING");
        if (i == 2u) {
            auto d = renderer::detail::makeRenderPipelineDesc(
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
    if (prepared_)
        return prepared_->resources.history()->frameReady();
    if (!created() || pending_)
        return false;
    for (std::size_t i = 0; i < slots_.size(); ++i)
        if (i != previousSlot_ && (!slots_[i].completion || slots_[i].completion->isSignaled()))
            return true;
    return false;
}

bool VolumetricCloudFeature::idle() const noexcept
{
    if (prepared_)
        return prepared_->resources.history()->idle();
    if (pending_)
        return false;
    for (const auto& slot : slots_)
        if (slot.completion && !slot.completion->isSignaled())
            return false;
    return true;
}

CloudResult VolumetricCloudFeature::resize(rhi::Extent2D extent)
{
    if (prepared_)
        return CloudResult::failure("CLOUD-PREPARED-FEATURE-RESIZE-CONTEXT");
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
    if (prepared_) {
        const auto result = prepared_->resources.history()->cancelPendingFrame();
        if (result.success)
            preparedSubmission_.invalidateFrame();
        return result;
    }
    if (executionValidated_)
        return CloudResult::failure("CLOUD-CANCEL-AFTER-EXECUTION-VALIDATION");
    pending_ = false;
    ++generation_;
    return {};
}

void VolumetricCloudFeature::release() noexcept
{
    if (prepared_) {
        preparedSubmission_.detach();
        prepared_.reset();
        device_ = nullptr;
        plan_ = {};
        return;
    }
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
    if (prepared_)
        return preparedSubmission_.validateSubmission(token);
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
    if (prepared_) {
        preparedSubmission_.onSubmitted(token, completion);
        return;
    }
    if (validateSubmission(token) || !completion) {
        historyValid_ = false;
        return;
    }
    slots_[pendingSlot_].completion = completion;
    if (slots_[pendingSlot_].frame.parameters.previousCamera.w > 0)
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
    if (prepared_)
        return CloudResult::failure("CLOUD-PREPARED-FEATURE-GRAPH-CONTEXT");
    if (!created())
        return CloudResult::failure("CLOUD-NOT-CREATED");
    if (pending_)
        return CloudResult::failure("CLOUD-GRAPH-PENDING");
    if (c.cameraId != cameraId_)
        return CloudResult::failure("CLOUD-CAMERA-IDENTITY");
    if (!c.color.id || !c.depth.id)
        return CloudResult::failure("CLOUD-ATTACHMENT-CONTRACT");
    const auto& color = c.graph.textureDesc(c.color);
    const auto& depth = c.graph.textureDesc(c.depth);
    if (color.format != rhi::TextureFormat::Rgba16Float
        || depth.format != rhi::TextureFormat::D32Float || color.extent != depth.extent
        || color.extent.width != plan_.extent.width || color.extent.height != plan_.extent.height
        || color.extent.depth != 1u || color.sampleCount != 1u || depth.sampleCount != 1u)
        return CloudResult::failure("CLOUD-ATTACHMENT-CONTRACT");
    CloudFrameState nextFrame;
    const auto built = CloudFrameParameters::build(c,
        device_->backend(),
        config_,
        plan_,
        assetDesc_,
        samplingFrame_,
        historyValid_ ? &slots_[previousSlot_].frame : nullptr,
        nextFrame);
    if (!built.success)
        return built;
    std::uint32_t index = static_cast<std::uint32_t>(slots_.size());
    for (std::uint32_t i = 0; i < slots_.size(); ++i)
        if (i != previousSlot_ && (!slots_[i].completion || slots_[i].completion->isSignaled())) {
            index = i;
            break;
        }
    if (index == slots_.size())
        return CloudResult::failure("CLOUD-FRAMES-IN-FLIGHT");
    auto& slot = slots_[index];
    slot.frame = nextFrame;
    auto& p = slot.frame.parameters;
    const bool history = slot.frame.historyUsed;
    const auto& old = slots_[previousSlot_];
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

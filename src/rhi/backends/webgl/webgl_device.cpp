#include "rhi/backends/webgl/webgl_device.h"

#include "rhi/backends/webgl/webgl_pipeline_internal.h"
#include "rhi/gpu_swapchain.h"

namespace arp::rhi::webgl {
WebGlDevice::WebGlDevice(ConstructionKey,
    core::UniquePtr<WebGlResources> resources,
    std::shared_ptr<core::CpuMemoryQuota> quota)
    : resources_(core::shareUnique(std::move(resources))), quota_(std::move(quota)),
      pipelines_(*this), descriptors_(*this)
{}

WebGlDevice::~WebGlDevice() = default;

core::UniquePtr<WebGlDevice> WebGlDevice::create(core::UniquePtr<WebGlResources> resources,
    const semantics::WebGlDeviceCompositionRequest& request,
    const std::shared_ptr<core::CpuMemoryQuota>& quota,
    const char*& diagnostic)
{
    diagnostic = semantics::WebGlDeviceCompositionContract::validate(Backend::WebGL, request);
    if (diagnostic)
        return {};
    if (!resources || (diagnostic = resources->currentDiagnostic())) {
        if (!diagnostic)
            diagnostic = "RHI-WEBGL1-DEVICE-ORIGINAL-RESOURCES";
        return {};
    }
    const std::array quotas{
        quota, resources->domain_->metadataQuota, resources->domain_->uploadQuota};
    if (!quota || quota->limitBytes() != request.maxCpuMetadataBytes
        || !core::SharedOwnerProjection::hasIndependentOwners(
            quotas.size(), [&](std::size_t i) noexcept {
                return quotas[i];
            })) {
        diagnostic = "RHI-WEBGL1-DEVICE-CPU-QUOTA";
        return {};
    }
    const auto& capacity = resources->domain_->request.budget.capacity;
    if (capacity.logicalBufferBytes > UINT64_MAX - capacity.logicalTextureBytes) {
        diagnostic = "RHI-WEBGL1-DEVICE-LOGICAL-BUDGET-OVERFLOW";
        return {};
    }
    try {
        core::CpuMemoryQuotaScope scope(quota);
        auto device = core::makeUnique<WebGlDevice>(ConstructionKey{}, std::move(resources), quota);
        device->pipelinePolicy_ = request.pipelines.policyId;
        device->descriptorPolicy_ = request.descriptors.policyId;
        auto pipelines = request.pipelines;
        auto descriptors = request.descriptors;
        pipelines.policyId = device->pipelinePolicy_.c_str();
        descriptors.policyId = device->descriptorPolicy_.c_str();
        if (!device->pipelines_.configureBudget(pipelines)
            || !device->descriptors_.configureBudget(descriptors)) {
            diagnostic = "RHI-WEBGL1-DEVICE-CACHE-BUDGET";
            return {};
        }
        return device;
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-WEBGL1-DEVICE-CPU-ALLOCATION";
        return {};
    }
}

Backend WebGlDevice::backend() const
{
    return Backend::WebGL;
}

DeviceCapabilities WebGlDevice::capabilities() const
{
    DeviceCapabilities c;
    c.backend = Backend::WebGL;
    c.deviceName = "WebGlDeviceComposition/1 Prototype";
    c.swapchain.requestedImageCount = 0;
    c.swapchain.maxFramesInFlight = 0;
    c.pushConstantLoweringDiagnosticId = "RHI-WEBGL1-DEVICE-PROTOTYPE-ONLY";
    return c;
}

MemoryStats WebGlDevice::memoryStats() const
{
    const auto live = resources_->counters();
    MemoryStats stats;
    stats.allocatedBytes = live.logicalBufferBytes + live.logicalTextureBytes;
    stats.liveResourceCount = static_cast<std::uint32_t>(live.objectCount);
    return stats;
}

GpuQueue& WebGlDevice::queue()
{
    return resources_->queue();
}

PipelineCache& WebGlDevice::pipelineCache()
{
    return pipelines_;
}

DescriptorCache& WebGlDevice::descriptorCache()
{
    return descriptors_;
}

const semantics::WebGlRuntimeProfile& WebGlDevice::profile() const noexcept
{
    return resources_->profile();
}

const char* WebGlDevice::enableRecovery(const semantics::WebGlContextRecoveryRequest& request)
{
    return diagnostic_ = resources_->enableRecovery(request);
}

const char* WebGlDevice::enableSurfaceLifecycle(
    const semantics::WebGlSurfaceLifecycleRequest& request)
{
    return diagnostic_ = resources_->enableSurfaceLifecycle(request);
}

semantics::WebGlContextPhase WebGlDevice::contextPhase() const noexcept
{
    return resources_->contextPhase();
}

core::UniquePtr<WebGlResources> WebGlDevice::restoreResources(
    const semantics::WebGlResourceOwnershipRequest& request,
    std::shared_ptr<core::CpuMemoryQuota> metadata,
    std::shared_ptr<core::CpuMemoryQuota> upload,
    const char*& diagnostic)
{
    return resources_->restore(request, std::move(metadata), std::move(upload), diagnostic);
}

const char* WebGlDevice::diagnostic() const noexcept
{
    if (const auto* id = resources_->currentDiagnostic())
        return id;
    return diagnostic_;
}

semantics::WebGlExecutionTelemetryReport WebGlDevice::executionTelemetry() const noexcept
{
    return resources_->executionTelemetry();
}

void WebGlDevice::waitIdle()
{
    diagnostic_ = "RHI-WEBGL1-GPU-COMPLETION-UNAVAILABLE";
    throw core::DiagnosticError(diagnostic_);
}

semantics::WebGlCpuRetirementReport WebGlDevice::retireCpu(
    const semantics::WebGlCpuRetirementRequest& request)
{
    semantics::WebGlCpuRetirementReport report;
    report.diagnostic = semantics::WebGlCpuRetirementContract::validate(Backend::WebGL, request);
    if (report.diagnostic || (report.diagnostic = resources_->retireCpuDomain()))
        return report;
    prepared_.reset();
    layout_.reset();
    artifact_.reset();
    descriptors_.clearForShutdown();
    pipelines_.clearForShutdown();
    report.success = report.admissionClosed = true;
    report.retainedResourceCount = resources_->counters().objectCount;
    return report;
}

semantics::ShutdownDrainReport WebGlDevice::drainForShutdown(
    const semantics::ShutdownDrainRequest& request)
{
    const auto started = std::chrono::steady_clock::now();
    return semantics::rejectedShutdownDrainReport(
        Backend::WebGL, started, request, std::chrono::steady_clock::now());
}

const char* WebGlDevice::selectPipelineInputs(const std::shared_ptr<PipelineLayout>& layout,
    const std::shared_ptr<const shader::WebGlShaderProgramArtifact>& artifact,
    const semantics::WebGlPipelineCreationRequest& request)
{
    if ((diagnostic_ = resources_->currentDiagnostic()))
        return diagnostic_;
    if (request.contractVersion != 1)
        return diagnostic_ = "RHI-WEBGL1-PIPELINE-CREATION-VERSION";
    if ((diagnostic_ =
                semantics::WebGlProgramCreationContract::validate(Backend::WebGL, request.program))
        || (diagnostic_ = shader::WebGlShaderProgramArtifact::validateOriginal(artifact)))
        return diagnostic_;
    const auto original = std::dynamic_pointer_cast<WebGlPipelineLayout>(layout);
    if (!original || original->object->domain != resources_->domain_
        || !original->object->current())
        return diagnostic_ = "RHI-WEBGL1-PIPELINE-ORIGINAL-LAYOUT";
    if ((diagnostic_ = artifact->assess(resources_->domain_->runtime->shaderLimits())))
        return diagnostic_;
    layout_ = layout;
    artifact_ = artifact;
    pipelineRequest_ = request;
    return nullptr;
}

const char* WebGlDevice::prepareCommand(const std::shared_ptr<RenderPipeline>& pipeline,
    const std::shared_ptr<WebGlFramebuffer>& target,
    const semantics::WebGlPreparedCommandsRequest& request,
    const std::shared_ptr<core::CpuMemoryQuota>& quota)
{
    return prepareCommand({&pipeline, 1}, target, request, quota);
}

const char* WebGlDevice::prepareCommand(core::ArrayView<std::shared_ptr<RenderPipeline>> pipelines,
    const std::shared_ptr<WebGlFramebuffer>& target,
    const semantics::WebGlPreparedCommandsRequest& request,
    const std::shared_ptr<core::CpuMemoryQuota>& quota)
{
    return prepareCommand(pipelines, {&target, 1}, request, quota);
}

const char* WebGlDevice::prepareCommand(core::ArrayView<std::shared_ptr<RenderPipeline>> pipelines,
    core::ArrayView<std::shared_ptr<WebGlFramebuffer>> targets,
    const semantics::WebGlPreparedCommandsRequest& request,
    const std::shared_ptr<core::CpuMemoryQuota>& quota)
{
    if (prepared_)
        return diagnostic_ = "RHI-WEBGL1-DEVICE-COMMAND-ALREADY-PREPARED";
    if (quota == quota_)
        return diagnostic_ = "RHI-WEBGL1-DEVICE-COMMAND-QUOTA-DOMAIN";
    prepared_ =
        WebGlExecution::prepare(*resources_, pipelines, targets, request, quota, diagnostic_);
    return diagnostic_;
}

core::UniquePtr<CommandEncoder> WebGlDevice::createCommandEncoder(const CommandEncoderDesc& desc)
{
    if ((diagnostic_ = resources_->currentDiagnostic()))
        return {};
    if (desc.maxBindlessPipelines || desc.maxBindlessSnapshots || desc.maxBindlessIndirectAccesses
        || desc.maxBindlessIndirectProcessors || desc.maxBindlessGraphicsAccesses
        || desc.retentionBudget)
        return reject<CommandEncoder>("RHI-WEBGL1-DEVICE-COMMAND-RESERVATIONS-UNSUPPORTED");
    if (!prepared_)
        return reject<CommandEncoder>("RHI-WEBGL1-DEVICE-COMMAND-NOT-PREPARED");
    diagnostic_ = nullptr;
    return std::move(prepared_);
}

core::UniquePtr<GpuBuffer> WebGlDevice::createBuffer(const BufferDesc& desc)
{
    auto result = resources_->createBuffer(desc);
    diagnostic_ = resources_->diagnostic();
    return result;
}

CommandValidationResult WebGlDevice::writeBuffer(
    GpuBuffer& object, const void* data, std::uint64_t bytes, std::uint64_t offset)
{
    auto result = resources_->writeBuffer(object, data, bytes, offset);
    diagnostic_ = resources_->diagnostic();
    return result;
}

core::UniquePtr<GpuTexture> WebGlDevice::createTexture(const TextureDesc& desc)
{
    auto result = resources_->createTexture(desc);
    diagnostic_ = resources_->diagnostic();
    return result;
}

core::UniquePtr<GpuTextureView> WebGlDevice::createTextureView(
    const GpuTexture& object, const TextureViewDesc& desc)
{
    auto result = resources_->createTextureView(object, desc);
    diagnostic_ = resources_->diagnostic();
    return result;
}

core::UniquePtr<GpuSampler> WebGlDevice::createSampler(const SamplerDesc& desc)
{
    auto result = resources_->createSampler(desc);
    diagnostic_ = resources_->diagnostic();
    return result;
}

core::UniquePtr<PipelineLayout> WebGlDevice::createPipelineLayout(const PipelineLayoutDesc& desc)
{
    auto result = resources_->createPipelineLayout(desc);
    diagnostic_ = resources_->diagnostic();
    return result;
}

core::UniquePtr<DescriptorArena> WebGlDevice::createDescriptorArena(const DescriptorArenaDesc& desc)
{
    auto result = resources_->createDescriptorArena(desc);
    diagnostic_ = resources_->diagnostic();
    return result;
}

core::UniquePtr<WebGlFramebuffer> WebGlDevice::createFramebuffer(
    GpuTexture& color, GpuTexture* depth)
{
    auto result = resources_->createFramebuffer(color, depth);
    diagnostic_ = resources_->diagnostic();
    return result;
}

CommandValidationResult WebGlDevice::uploadTexture(
    GpuTexture& object, const semantics::WebGlColdTextureUpload& upload)
{
    auto result = resources_->uploadTexture(object, upload);
    diagnostic_ = resources_->diagnostic();
    return result;
}

const char* WebGlDevice::enableCanvasReuse(
    Swapchain& canvas, const semantics::WebGlCanvasReuseRequest& request)
{
    return diagnostic_ = resources_->enableCanvasReuse(canvas, request);
}

const char* WebGlDevice::recycleCanvas(
    Swapchain& canvas, const AcquiredFrame& frame, core::UniquePtr<GpuTexture>& image)
{
    return diagnostic_ = resources_->recycleCanvas(canvas, frame, image);
}

PipelineValidationReport WebGlDevice::validateRenderPipeline(const RenderPipelineDesc& desc)
{
    return resources_->validateRenderPipeline(desc, layout_, artifact_, pipelineRequest_);
}

core::UniquePtr<RenderPipeline> WebGlDevice::createRenderPipeline(const RenderPipelineDesc& desc)
{
    auto result = resources_->createRenderPipeline(desc, layout_, artifact_, pipelineRequest_);
    diagnostic_ = resources_->diagnostic();
    return result;
}

PipelineValidationReport WebGlDevice::validateComputePipeline(const ComputePipelineDesc&)
{
    PipelineValidationReport result;
    result.backend = Backend::WebGL;
    result.diagnostics = "RHI-WEBGL1-COMPUTE-UNSUPPORTED";
    return result;
}

core::UniquePtr<GpuBufferView> WebGlDevice::createBufferView(
    const GpuBuffer&, const BufferViewDesc&)
{
    return reject<GpuBufferView>("RHI-WEBGL1-BUFFER-VIEW-UNSUPPORTED");
}

core::UniquePtr<GpuMemoryHeap> WebGlDevice::createMemoryHeap(
    std::uint64_t, std::uint64_t, MemoryUsage)
{
    return reject<GpuMemoryHeap>("RHI-WEBGL1-ALIAS-HEAP-UNAVAILABLE");
}

std::string_view WebGlDevice::lastMemoryHeapCreationDiagnostic() const
{
    return "RHI-WEBGL1-ALIAS-HEAP-UNAVAILABLE";
}

core::UniquePtr<GpuTexture> WebGlDevice::createPlacedTexture(
    const TextureDesc&, const GpuMemoryHeap&, std::uint64_t)
{
    return reject<GpuTexture>("RHI-WEBGL1-PLACED-TEXTURE-UNAVAILABLE");
}

core::UniquePtr<ComputePipeline> WebGlDevice::createComputePipeline(const ComputePipelineDesc&)
{
    return reject<ComputePipeline>("RHI-WEBGL1-COMPUTE-UNSUPPORTED");
}

core::UniquePtr<SecondaryCommandEncoder> WebGlDevice::createSecondaryCommandEncoder(
    const SecondaryCommandEncoderDesc&)
{
    return reject<SecondaryCommandEncoder>("RHI-WEBGL1-SECONDARY-UNSUPPORTED");
}

core::UniquePtr<RenderBundleEncoder> WebGlDevice::createRenderBundleEncoder(const RenderBundleDesc&)
{
    return reject<RenderBundleEncoder>("RHI-WEBGL1-BUNDLE-UNSUPPORTED");
}

core::UniquePtr<Swapchain> WebGlDevice::createSwapchain(const SwapchainDesc& desc)
{
    auto result = resources_->createCanvas(desc);
    diagnostic_ = resources_->diagnostic();
    return result;
}

std::shared_ptr<WebGlFramebuffer> WebGlDevice::canvasFramebuffer(
    Swapchain& canvas, const AcquiredFrame& frame)
{
    auto result = resources_->canvasFramebuffer(canvas, frame);
    diagnostic_ = resources_->diagnostic();
    return result;
}

core::UniquePtr<GpuFence> WebGlDevice::createFence()
{
    return reject<GpuFence>("RHI-WEBGL1-GPU-COMPLETION-UNAVAILABLE");
}

core::UniquePtr<GpuSemaphore> WebGlDevice::createSemaphore()
{
    return reject<GpuSemaphore>("RHI-WEBGL1-GPU-COMPLETION-UNAVAILABLE");
}

core::UniquePtr<GpuTimelineSemaphore> WebGlDevice::createTimelineSemaphore(std::uint64_t)
{
    return reject<GpuTimelineSemaphore>("RHI-WEBGL1-GPU-COMPLETION-UNAVAILABLE");
}

core::UniquePtr<QueryPool> WebGlDevice::createQueryPool(const QueryPoolDesc&)
{
    return reject<QueryPool>("RHI-WEBGL1-QUERY-UNAVAILABLE");
}
} // namespace arp::rhi::webgl

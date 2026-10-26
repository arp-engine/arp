#include "prepared_offscreen.h"

#include "platform/renderdoc_capture_scope.h"
#include "renderer/render_graph/original_frame_graph_execution.h"
#include "renderer/volumetric_cloud/cloud_prepared_descriptors.h"
#include "renderer/volumetric_cloud/cloud_prepared_feature.h"
#include "rhi/frontend/transfer_upload_destination_validation.h"
#include "rhi/frontend/transfer_validation.h"
#include "rhi/semantics/render_target_coordinates.h"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace arp::cloud_demo {
namespace {
using namespace rhi;
using namespace renderer::cloud;
using namespace render_graph;
using Sources = OriginalCommandResourceSources;

// Authored demo ceilings, never queried device capabilities or driver bytes.
constexpr std::uint64_t cpuBytes = 4u * 1024u * 1024u;
constexpr std::uint64_t initializationBytes = 128u * 1024u * 1024u;
constexpr std::uint32_t commandCapacity = 64u, ownerCapacity = 2048u;

void require(const char* id)
{
    if (id)
        throw std::runtime_error(id);
}

void require(const CloudResult& r)
{
    require(r.success ? nullptr : r.diagnostic);
}

auto quota(std::uint64_t bytes = cpuBytes)
{
    return core::makeShared<core::CpuMemoryQuota>(bytes);
}

OriginalRenderPassPreparationRequest preparation()
{
    return {6u,
        true,
        {1u, true, 2u, 1u, 3u, {1u, true, 2u, 0u, 0u, 0u, 1024u, cpuBytes}},
        7u,
        2u,
        OriginalRenderPassContentBudget{3u, true, 3u, 1u, 64u, 512u, 512u},
        OriginalRenderPassRecordingBudget{3u, true, 512u, 1024u}};
}

struct Flight final : PassSubmissionObserver {
    std::shared_ptr<core::CpuMemoryQuota> intent = quota(), states = quota(), recording = quota();
    core::UniquePtr<FrameGraph> graph;
    core::UniquePtr<OriginalFrameGraphExecution> execution;
    std::shared_ptr<const GpuFence> completion;

    void onSubmitted(
        PassSubmissionToken, const std::shared_ptr<const GpuFence>& fence) noexcept override
    {
        completion = fence;
    }

    bool ready() const noexcept { return !completion || completion->isSignaled(); }

    void retire()
    {
        require(ready() ? nullptr : "CLOUD-DEMO-PREPARED-IN-FLIGHT");
        execution.reset();
        graph.reset();
        completion.reset();
    }
};

// Declared last so failure drains the real device before releasing graph owners.
struct Drain final {
    GpuDevice& device;

    ~Drain() { device.waitIdle(); }
};

void createFeature(const PreparedOffscreenRequest& request,
    const std::shared_ptr<core::CpuMemoryQuota>& sourceQuota,
    VolumetricCloudFeature& feature)
{
    CloudPreparedPipelineCreateContext p{request.device};
    p.selection = {1u,
        request.reconstruction ? CloudPreparedResolve::Reconstruction1
                               : CloudPreparedResolve::Temporal1,
        request.reconstruction ? CloudReconstructionConfig{1u, .025f, .025f}
                               : CloudReconstructionConfig{}};
    p.resources = {request.config,
        request.extent,
        request.asset.cells.size() * sizeof(core::Vec4),
        request.config.framesInFlight * kCloudPipelineCount,
        request.config.framesInFlight * (request.reconstruction ? 15u : 12u)};
    const core::pmr::String compute = ARP_CLOUD_PREPARED_COMPUTE_DIR;
    const core::pmr::String graphics = ARP_CLOUD_PREPARED_GRAPHICS_DIR;
    p.publications = {compute + "/cloud_raymarch.computeMain.entry.yaml",
        compute + "/cloud_temporal.computeMain.entry.yaml",
        graphics + "/cloud_composite.graphics.module.yaml",
        compute + "/cloud_spatial_resolve.computeMain.entry.yaml",
        graphics + "/cloud_spatial_composite.graphics.module.yaml"};
    p.preparation = preparation();
    CloudPreparedResourcePlan plan;
    require(CloudPreparedPipelines::planResources(
        p.selection, p.resources, request.device.capabilities().computeResources, plan));
    const auto lowering =
        OriginalRenderPassPreparationContract::lowering(request.device.backend(), p.preparation);
    require(lowering.recordingAvailable ? nullptr : lowering.diagnostic);
    // Explicit caller-owned prerequisites; the legacy device path has not
    // opted into original factory registration. These L2 APIs grant no new
    // backend support and preserve their independent native-loss validation.
    require(request.device.prepareOriginalDeviceLifetime({1u, true, 1u, 2u, cpuBytes}, quota()));
    require(request.device.prepareOriginalDeviceNativeResultLoss(
        {1u, true, 1u, 2u, cpuBytes}, quota()));
    OriginalResourceRegistrationBudget registry;
    registry.contractVersion = 10u;
    registry.prototypeOptIn = true;
    registry.maxBuffers =
        2u * request.config.framesInFlight + 6u + (request.reconstruction ? 2u : 0u);
    registry.maxTextures = 3u;
    registry.maxTextureViews = 5u;
    registry.maxPipelineLayouts = 3u;
    registry.maxGraphicsPipelines = 1u;
    registry.maxComputePipelines = 2u;
    registry.maxDescriptorArenas = 1u;
    registry.maxDescriptorSets = 3u;
    registry.maxRetainedOwners = ownerCapacity;
    registry.maxCpuMetadataBytes = cpuBytes;
    const char* id = nullptr;
    const auto loss = request.device.observeOriginalDeviceNativeLoss(id);
    require(id);
    require(loss ? nullptr : "CLOUD-DEMO-NATIVE-LOSS-OBSERVATION");
    require(request.device.prepareOriginalTypedNativeLossRegistration(
        {3u, true}, registry, quota(), *loss));
    p.orchestrationQuota = quota();
    auto& b = p.budget;
    b.maxCpuOrchestrationBytes = cpuBytes;
    b.computeLoad = {cpuBytes, 8u * cpuBytes, cpuBytes, 7u};
    b.graphicsLoad = {cpuBytes, 8u * cpuBytes, cpuBytes, 4u, 4u, 256u, 2u};
    b.computeInput = {1u, cpuBytes};
    b.graphicsInput = {1u, cpuBytes};
    b.computeCreation = {1u, true, cpuBytes, 256u, cpuBytes, cpuBytes};
    b.graphicsCreation = {1u, true, cpuBytes, 256u, cpuBytes, cpuBytes};
    b.computeWrapper = b.graphicsWrapper = {2u, true, cpuBytes};
    for (auto& q : p.quotas)
        q = {quota(), quota(), quota(), quota(), quota()};
    CloudPreparedResourceCreateContext r{request.device, request.asset};
    r.contractVersion = 1u;
    r.prototypeOptIn = true;
    r.cameraId = 1u;
    r.selection = p.selection;
    r.resources = p.resources;
    r.sampleMemory = MemoryUsage::CpuToGpu;
    r.maxCpuOwnerBytes = cpuBytes;
    r.maxCpuInitializationBytes = initializationBytes;
    r.ownerQuota = quota();
    r.initializationQuota = quota(initializationBytes);
    r.sourceQuota = sourceQuota;
    r.historyQuota = quota();
    r.retainedLogicalBytes =
        core::makeShared<core::AtomicCapacityBudget>(request.config.gpuBudgetBytes);
    CloudPreparedFeatureCreateContext c{p, r};
    c.contractVersion = 1u;
    c.prototypeOptIn = true;
    c.maxCpuStateBytes = cpuBytes;
    c.stateQuota = quota();
    core::pmr::String diagnostic;
    const auto result = feature.create(c, diagnostic);
    if (!result.success)
        throw std::runtime_error(
            diagnostic.empty() ? result.diagnostic : core::toStdString(diagnostic));
}

// The caller composes the scene attachments and pending cloud buffers into one
// inventory. Reset history may alias raw; canonical owners appear only once.
class FrameSources final {
public:
    FrameSources(GpuDevice& device, std::shared_ptr<core::CpuMemoryQuota> quota)
        : device_(device), quota_(std::move(quota))
    {}

    std::size_t addBuffer(const std::shared_ptr<const OriginalCommandBufferLease>& lease)
    {
        for (std::size_t i = 0u; i < count_; ++i)
            if (const auto* existing =
                    std::get_if<std::shared_ptr<const OriginalCommandBufferLease>>(&prefix_[i]);
                existing && existing->get() == lease.get())
                return i;
        return append(lease);
    }

    std::size_t attachment(Extent2D extent, TextureFormat format, TextureUsage extra)
    {
        TextureDesc d;
        d.extent = {extent.width, extent.height, 1u};
        d.format = format;
        d.usage = TextureUsage::RenderAttachment | extra;
        const char* id = nullptr;
        const auto allocation = device_.queryOriginalContentTextureAllocation(d, id, 4u);
        require(id);
        std::uint64_t footprint = 0u;
        require(semantics::TextureUploadDestinationValidation::footprint(d, footprint));
        auto object = device_.createOriginalContentTexture(
            d, {4u, true, 1u, 1u, footprint, 1u, cpuBytes, 1u, allocation}, quota_, id);
        require(id);
        auto lease = OriginalCommandTextureLease::prepareOriginal(
            object, {1u, true, 2u, cpuBytes}, quota_, id);
        require(id);
        return append(lease);
    }

    std::size_t view(std::size_t parent, TextureUsage usage)
    {
        const auto& texture =
            std::get<std::shared_ptr<const OriginalCommandTextureLease>>(prefix_[parent]);
        TextureViewDesc d;
        d.format = texture->layout().format;
        d.aspect =
            d.format == TextureFormat::D32Float ? TextureAspect::Depth : TextureAspect::Color;
        d.usage = usage;
        auto object = device_.createTextureView(*texture->originalObject(), d);
        const char* id = nullptr;
        auto lease = OriginalCommandTextureViewLease::prepareOriginal(
            object, {1u, true, 2u, cpuBytes}, quota_, id);
        require(id);
        auto source = OriginalCommandTextureViewSource::prepareOriginal(
            lease, texture, {1u, true, 5u, cpuBytes}, quota_, id);
        require(id);
        return append(source);
    }

    std::size_t readback(std::uint64_t bytes)
    {
        require(bytes && bytes <= initializationBytes ? nullptr : "CLOUD-DEMO-READBACK-BUDGET");
        BufferDesc d;
        d.sizeBytes = bytes;
        d.memory = MemoryUsage::GpuToCpu;
        d.usage = ResourceUsage::CopyDst;
        d.initialState = ResourceState::CopyDst;
        auto object = device_.createBuffer(d);
        require(object ? nullptr : "CLOUD-DEMO-READBACK-CREATE");
        {
            const auto scratch = quota(initializationBytes);
            core::CpuMemoryQuotaScope scope(scratch);
            core::pmr::Vector<std::byte> zeros(static_cast<std::size_t>(bytes));
            const auto result = device_.writeBuffer(*object, zeros.data(), bytes);
            require(result.success ? nullptr : "CLOUD-DEMO-READBACK-INITIALIZATION");
        }
        object->sealHostWrites();
        const char* id = nullptr;
        auto lease = OriginalCommandBufferLease::prepareOriginal(
            object, {1u, true, 2u, cpuBytes}, quota_, id);
        require(id);
        return addBuffer(lease);
    }

    std::shared_ptr<Sources> inventory()
    {
        OriginalCommandAdmissionBudget b;
        b.contractVersion = 1u;
        b.prototypeOptIn = true;
        b.maxCommands = b.maxReferences = b.maxInputDescriptors = commandCapacity;
        b.maxStateObservations = b.maxTextureRanges = commandCapacity;
        b.maxPassColorAttachments = 2u;
        b.maxPassAttachmentReferences = 3u;
        b.maxLabelBytes = 1024u;
        b.maxGraphOwners = ownerCapacity;
        b.maxCpuBytes.fill(cpuBytes);
        for (std::size_t i = 0u; i < count_; ++i)
            std::visit(
                [&](const auto& source) {
                    ++b.maxSourceOwners[static_cast<std::size_t>(source->domain)];
                },
                prefix_[i]);
        b.maxSourceOwners[static_cast<std::size_t>(CommandReferenceDomain::Pipeline)] = 3u;
        b.maxSourceOwners[static_cast<std::size_t>(CommandReferenceDomain::Descriptor)] = 3u;
        const char* id = nullptr;
        auto result = Sources::prepareColdOriginalTyped(
            prefix_.data(), count_, b, {8u, true, 6u}, quota_, id);
        require(id);
        return result;
    }

    TextureHandle importTexture(FrameGraph& graph, std::size_t index) const
    {
        const auto& lease =
            std::get<std::shared_ptr<const OriginalCommandTextureLease>>(prefix_[index]);
        return graph.importTextureRef({lease, const_cast<GpuTexture*>(lease->originalObject())});
    }

    BufferHandle importBuffer(FrameGraph& graph, std::size_t index) const
    {
        const auto& lease =
            std::get<std::shared_ptr<const OriginalCommandBufferLease>>(prefix_[index]);
        return graph.importBufferRef({lease, const_cast<GpuBuffer*>(lease->originalObject())});
    }

    template <class Pipeline>
    std::size_t appendPipeline(
        std::shared_ptr<Sources>& inventory, const std::shared_ptr<Pipeline>& pipeline)
    {
        using Source = std::conditional_t<std::is_same_v<Pipeline, RenderPipeline>,
            OriginalCommandGraphicsPipelineSource,
            OriginalCommandComputePipelineSource>;
        const char* id = nullptr;
        auto source = Source::prepareOriginal(pipeline,
            {2u,
                true,
                Source::kind,
                pipeline->originalResourceRegistration().generation(),
                cpuBytes,
                Source::exclusiveOwnerCount},
            quota_,
            device_,
            id);
        require(id);
        const auto index = inventory->sources().size();
        require(Sources::appendOriginalTyped(inventory, source, device_));
        return index;
    }

private:
    std::size_t append(Sources::Source source)
    {
        require(count_ < prefix_.size() ? nullptr : "CLOUD-DEMO-SOURCE-CAPACITY");
        prefix_[count_] = std::move(source);
        return count_++;
    }

    GpuDevice& device_;
    std::shared_ptr<core::CpuMemoryQuota> quota_;
    std::array<Sources::Source, 16u> prefix_{};
    std::size_t count_ = 0u;
};

OriginalFrameGraphBudget graphBudget(std::size_t sourceCount, bool spatial)
{
    OriginalFrameGraphBudget b;
    b.contractVersion = 1u;
    b.prototypeOptIn = true;
    b.maxCpuIntentBytes = cpuBytes;
    b.inputs = {10u, true, 3u, 3u};
    b.states = {5u,
        true,
        1u,
        static_cast<std::uint32_t>(sourceCount + 4u),
        32u,
        32u,
        ownerCapacity,
        cpuBytes};
    const auto accesses = spatial ? 15u : 12u;
    auto& c = b.states.descriptorConsumption;
    c = {4u, true, 1u, 1u, true, true, 3u, accesses};
    c.maxTextureAccesses = spatial ? 4u : 3u;
    c.generalShaderResourcesVersion = 2u;
    c.generalShaderResourcesOptIn = true;
    auto& r = b.states.renderDescriptorUsage;
    r = {4u, true, 1u, 1u, true, true, commandCapacity, 1u, 4u};
    r.maxTextureAccesses = 2u;
    r.generalShaderResourcesVersion = 2u;
    r.generalShaderResourcesOptIn = true;
    auto& s = b.stream;
    s.contractVersion = 5u;
    s.prototypeOptIn = true;
    s.maxCommands = s.maxDescriptors = s.maxReferences = commandCapacity;
    s.maxCopyElements = 1u;
    s.maxRenderPasses = 2u;
    s.maxAttachmentReferences = 3u;
    s.maxPipelineBinds = s.maxDescriptorBinds = s.maxConsumptions = 3u;
    s.maxImplicitAccesses = accesses + 4u;
    s.maxRetainedOwners = ownerCapacity;
    s.maxCpuMetadataBytes = cpuBytes;
    s.descriptorConsumption = c;
    s.renderDescriptorUsage = r;
    s.stateSequence = {5u, true, commandCapacity, 0u, 32u, 64u, accesses + 4u};
    b.preparation = preparation();
    return b;
}

void assembleFrame(const PreparedOffscreenRequest& r,
    const std::shared_ptr<core::CpuMemoryQuota>& sourceQuota,
    VolumetricCloudFeature& feature,
    Flight& flight)
{
    flight.graph = core::makeUnique<FrameGraph>(r.device);
    auto& graph = *flight.graph;
    CloudGraphContext c{graph, {}, {}};
    c.layer = r.layer;
    c.cameraPosition = {0.f,
        r.camera == "below"        ? 20.f
            : r.camera == "inside" ? 2400.f
                                   : 6000.f,
        -100.f};
    c.view = core::lookAt(c.cameraPosition,
        c.cameraPosition + core::Vec3{.25f, r.camera == "above" ? -.5f : .25f, 1.f});
    c.projection = core::perspective(65.f, float(r.extent.width) / r.extent.height, 1.f, 100000.f);
    semantics::applyProjectionYConvention(c.projection.m.data(), r.device.backend());
    c.sunDirection = core::normalize(core::Vec3{.7f, .5f, .8f});
    c.sunRadiance = {12.f, 11.f, 10.f};
    c.ambientRadiance = {.3f, .4f, .55f};
    c.time = 10.f;
    CloudPreparedFrameBuffers buffers;
    require(feature.prepareFrame(c, buffers));
    FrameSources sources(r.device, sourceQuota);
    CloudPreparedDescriptorLocations locations;
    locations.constants = sources.addBuffer(buffers.constants);
    locations.density = sources.addBuffer(buffers.density);
    locations.raw = sources.addBuffer(buffers.raw);
    locations.previous = sources.addBuffer(buffers.previous);
    locations.resolved = sources.addBuffer(buffers.resolved);
    if (r.reconstruction)
        locations.reconstructionConstants = sources.addBuffer(buffers.reconstructionConstants);
    const std::array parents{
        sources.attachment(r.extent, TextureFormat::Rgba16Float, TextureUsage::Sampled),
        sources.attachment(r.extent, TextureFormat::D32Float, TextureUsage::Sampled),
        sources.attachment(r.extent, TextureFormat::Rgba16Float, TextureUsage::CopySrc)};
    locations.color = sources.view(parents[0], TextureUsage::Sampled);
    locations.depth = sources.view(parents[1], TextureUsage::Sampled);
    const std::array attachments{sources.view(parents[0], TextureUsage::RenderAttachment),
        sources.view(parents[1], TextureUsage::RenderAttachment),
        sources.view(parents[2], TextureUsage::RenderAttachment)};
    const auto aligned =
        core::alignCpuAllocationOffsetUp(semantics::TextureTransferValidation::copyRowBytes(
                                             TextureFormat::Rgba16Float, r.extent.width),
            kTextureUploadRowPitchAlignment);
    require(
        aligned.success && aligned.alignedValue <= UINT32_MAX ? nullptr : "CLOUD-DEMO-COPY-PITCH");
    const auto readback = sources.readback(aligned.alignedValue * r.extent.height);
    auto inventory = sources.inventory();
    const auto& pipelines = *feature.preparedPipelines();
    const std::array pipelineIndices{sources.appendPipeline(inventory, pipelines.raymarch()),
        sources.appendPipeline(inventory, pipelines.resolve()),
        sources.appendPipeline(inventory, pipelines.composite())};
    auto arena = r.device.createOriginalDescriptorArena(
        {"Cloud.DemoPrepared", 3u, r.reconstruction ? 15u : 12u});
    require(arena ? nullptr : "CLOUD-DEMO-DESCRIPTOR-ARENA");
    CloudPreparedDescriptorPublishContext publication{
        r.device, *feature.preparedResources(), pipelines, inventory, arena};
    publication.contractVersion = 1u;
    publication.prototypeOptIn = true;
    publication.locations = locations;
    publication.pipelineSources = pipelineIndices;
    publication.maxCpuOwnerBytes = cpuBytes;
    publication.ownerQuota = quota();
    for (std::size_t pass = 0u; pass < kCloudPipelineCount; ++pass) {
        auto& request = publication.requests[pass];
        request.contractVersion = pass == 2u ? 3u : 4u;
        request.prototypeOptIn = true;
        request.maxBindings = r.reconstruction && pass == 1u ? 7u : 4u;
        request.maxRetainedOwnerSlots = OriginalGeneralDescriptorWriteContract::ownerSlots(
            request.maxBindings, request.contractVersion);
        request.maxCpuBytes.fill(cpuBytes);
        request.maxRetainedAllocationBytes = cpuBytes;
        for (auto& q : publication.quotas[pass])
            q = quota();
    }
    CloudPreparedDescriptorOutput descriptors;
    require(CloudPreparedDescriptors::publish(publication, descriptors));
    flight.execution =
        core::makeUnique<OriginalFrameGraphExecution>(graph, inventory, flight.intent);
    auto& execution = *flight.execution;
    std::array<TextureHandle, 3u> textures;
    for (std::size_t i = 0u; i < textures.size(); ++i) {
        textures[i] = sources.importTexture(graph, parents[i]);
        require(execution.bindAttachment(textures[i], attachments[i]));
    }
    execution.clear("Cloud.DemoClear")
        .writeAttachment(textures[0],
            ResourceState::ColorAttachment,
            LoadOp::Clear,
            StoreOp::Store,
            {},
            {.15f, .35f, .65f, 1.f})
        .writeAttachment(
            textures[1], ResourceState::DepthAttachment, LoadOp::Clear, StoreOp::Store);
    CloudPreparedFeatureGraphContext context{execution};
    context.contractVersion = 1u;
    context.prototypeOptIn = true;
    context.frameToken = feature.preparedToken();
    context.color = textures[0];
    context.depth = textures[1];
    context.output = textures[2];
    context.pipelines = pipelineIndices;
    context.descriptors = descriptors.descriptors;
    CloudOutputs output;
    require(feature.contributeGraph(context, output));
    const auto target = sources.importBuffer(graph, readback);
    TextureToBufferCopyDesc copy;
    copy.sourceTexture = graph.texture(output.color).id();
    copy.destinationBuffer = graph.buffer(target).id();
    copy.extent = {r.extent.width, r.extent.height, 1u};
    copy.textureSubresources = {0u, 1u, 0u, 1u, TextureAspect::Color};
    copy.bytesPerRow = static_cast<std::uint32_t>(aligned.alignedValue);
    copy.rowsPerImage = r.extent.height;
    execution.copy("Cloud.DemoReadback", copy)
        .read(output.color, ResourceState::CopySrc)
        .write(target, ResourceState::CopyDst)
        .onSubmitted(flight);
    const auto budget = graphBudget(inventory->sources().size(), r.reconstruction);
    require(execution.prepare(budget, flight.intent, flight.states, flight.recording));
}
} // namespace

int runPreparedOffscreen(const PreparedOffscreenRequest& request)
{
    const auto rootQuota = quota(initializationBytes);
    core::CpuMemoryQuotaScope scope(rootQuota);
    const auto sourceQuota = quota();
    VolumetricCloudFeature feature;
    Flight flight;
    platform::RenderDocCaptureScope capture;
    Drain drain{request.device};
    createFeature(request, sourceQuota, feature);
    bool capturing = false;
    unsigned submitted = 0u, completed = 0u;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (completed < request.frames) {
        if (!flight.ready() || !feature.frameReady()) {
            require(std::chrono::steady_clock::now() < deadline ? nullptr
                                                                : "CLOUD-DEMO-COMPLETION-TIMEOUT");
            std::this_thread::yield();
            continue;
        }
        if (flight.completion) {
            ++completed;
            if (capturing) {
                require(capture.end() ? nullptr : "RENDERDOC-END-FAILED");
                capturing = false;
            }
            flight.retire();
        }
        if (submitted == request.frames)
            break;
        if (submitted == request.captureFrame && !request.capture.empty()) {
            require(capture.begin(request.capture) ? nullptr : "RENDERDOC-INJECTION-UNAVAILABLE");
            capturing = true;
        }
        assembleFrame(request, sourceQuota, feature, flight);
        const auto before = core::defaultCpuAllocator().stats();
        flight.execution->execute();
        const auto after = core::defaultCpuAllocator().stats();
        require(after.allocationCount == before.allocationCount ? nullptr
                                                                : "CLOUD-DEMO-HOT-ALLOCATION");
        require(flight.completion ? nullptr : "CLOUD-DEMO-COMPLETION-MISSING");
        deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        ++submitted;
    }
    require(feature.preparedHistory()->idle() ? nullptr : "CLOUD-DEMO-HISTORY-IN-FLIGHT");
    std::cout << "CloudPreparedDemo/1 Prototype submitted=" << submitted
              << " completed=" << completed
              << " resolve=" << (request.reconstruction ? "Reconstruction/1" : "Temporal/1")
              << " retainedLogicalBytes="
              << (request.reconstruction
                         ? feature.preparedResources()->plan().reconstruction.retainedGpuBytes
                         : feature.preparedResources()->plan().raw.retainedGpuBytes)
              << " hotAllocations=0 presentation=false physicalPeakValidated=false\n";
    return 0;
}
} // namespace arp::cloud_demo

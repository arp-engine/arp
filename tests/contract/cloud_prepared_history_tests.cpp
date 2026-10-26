#include "renderer/volumetric_cloud/cloud_prepared_descriptors.h"
#include "renderer/volumetric_cloud/cloud_prepared_feature.h"
#include "renderer/volumetric_cloud/cloud_prepared_history.h"
#include "renderer/volumetric_cloud/cloud_prepared_pipelines.h"
#include "renderer/volumetric_cloud/cloud_prepared_resources.h"
#include "renderer/volumetric_cloud/cloud_reconstruction.h"
#include "renderer/volumetric_cloud/cloud_reference.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/frontend/command_sources.h"
#include "rhi/frontend/descriptor_general_write_validation.h"
#include "rhi/semantics/metal_direct_binding_validation.h"
#include "tests/fixtures/cloud/cloud_fixture.h"
#include "tests/fixtures/cloud/cloud_quality_oracle.h"
#include "tests/fixtures/cloud/cloud_sequence_oracle.h"
#include "tests/fixtures/cloud_native_memory_fixture.h"
#include "tests/fixtures/cpu_quota_factory_fixture.h"

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>

using namespace arp;
using namespace renderer::cloud;

namespace {
constexpr std::uint64_t cpuBytes = 1024u * 1024u;

void ordinaryMetalCpuProjectionContract()
{
    using namespace rhi;
    for (const auto backend : {Backend::Null,
             Backend::Vulkan,
             Backend::Metal,
             Backend::WebGPU,
             Backend::WebGL,
             static_cast<Backend>(255u)})
        for (std::uint32_t version = 1u; version <= 4u; ++version) {
            const bool available = backend == Backend::WebGPU
                || (version == 4u && (backend == Backend::Vulkan || backend == Backend::Metal));
            const auto render = OriginalRenderDescriptorUsageContract::lowering(backend, version);
            const auto consumption =
                semantics::OriginalCommandDescriptorConsumptionLowering::lowering(backend, version);
            const auto state = semantics::OriginalCommandStateAdmissionLowering::typedLowering(
                backend, version + 1u);
            assert(render.projectionAvailable == available
                && consumption.projectionAvailable == available);
            assert(state.projectionAvailable == available && state.stateAvailable == available);
            assert(!render.stateAvailable && !render.executionAvailable);
            assert(!consumption.stateAvailable && !consumption.executionAvailable
                && !state.executionAvailable);
            assert(bool(render.diagnostic) == !available
                && bool(consumption.diagnostic) == !available);
        }
}

void require(CloudResult result)
{
    if (!result.success)
        std::cerr << result.diagnostic << '\n';
    assert(result.success);
}

CloudGraphContext frame(render_graph::FrameGraph& graph, float time)
{
    CloudGraphContext c{graph};
    c.layer = tests::cloud::layer();
    c.cameraPosition = {39.f, 10.f, -57.f};
    c.view = core::lookAt(c.cameraPosition, c.cameraPosition + core::Vec3{.25f, .6f, 1.f});
    c.projection = core::perspective(57.f, 4.f / 3.f, 1.f, 20000.f);
    c.sunDirection = core::normalize(core::Vec3{1.f, 1.f, -.2f});
    c.sunRadiance = {2.f, 1.8f, 1.6f};
    c.ambientRadiance = {.1f, .2f, .3f};
    c.time = time;
    return c;
}

CloudPreparedPipelineCreateContext pipelineContext(rhi::GpuDevice& device,
    const CloudPreparedResourceRequest& resources,
    const CloudPreparedPipelineSelection& selection)
{
    CloudPreparedPipelineCreateContext c{device};
    c.resources = resources;
    c.selection = selection;
    c.budget.maxCpuOrchestrationBytes = cpuBytes;
    c.orchestrationQuota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
    c.preparation = {6u,
        true,
        {1u, true, 2u, 1u, 3u, {1u, true, 2u, 0u, 0u, 0u, 256u, cpuBytes}},
        7u,
        2u,
        rhi::OriginalRenderPassContentBudget{3u, true, 3u, 1u, 64u, 512u, 512u},
        rhi::OriginalRenderPassRecordingBudget{3u, true, 512u, 1024u}};
    c.budget.computeInput = {1u, cpuBytes};
    c.budget.graphicsInput = {1u, cpuBytes};
    c.budget.computeCreation = {1u, true, cpuBytes, 256u, cpuBytes, cpuBytes};
    c.budget.graphicsCreation = {1u, true, cpuBytes, 256u, cpuBytes, cpuBytes};
    c.budget.computeWrapper = c.budget.graphicsWrapper = {2u, true, cpuBytes};
    for (auto& q : c.quotas) {
        q.artifact = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        q.input = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        q.owner = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        q.native = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        q.wrapper = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
    }
    return c;
}

void dispatchRecordingContract(rhi::GpuDevice& device)
{
    using C = rhi::semantics::ComputeDispatchRecordingContract;
    using Recording = rhi::OriginalRenderPassRecordingContract;
    rhi::OriginalRenderPassRecordingBudget b{3u, true, 512u, 1024u};
    assert(!Recording::validate(b));
    auto request = pipelineContext(device, {}, {}).preparation;
    assert(!rhi::OriginalRenderPassPreparationContract::validate(request));
    const auto before = core::defaultCpuAllocator().stats().allocationCount;
    for (const auto backend : {rhi::Backend::Null,
             rhi::Backend::Vulkan,
             rhi::Backend::Metal,
             rhi::Backend::WebGPU,
             rhi::Backend::WebGL,
             static_cast<rhi::Backend>(255u)}) {
        const auto d = C::lowering(backend);
        assert(d.inspectionAvailable == (backend == rhi::Backend::Metal));
        assert(d.validatorOnly == (backend == rhi::Backend::Null));
        assert(!d.executionAvailable);
        assert((d.diagnostic == nullptr) == (backend == rhi::Backend::Metal));
    }
    b.dispatchDomainOptIn = true;
    assert(std::string_view(Recording::validate(b)) == "RHI-DISPATCH-RECORDING-VERSION");
    b.dispatchDomainVersion = 1u;
    assert(!Recording::validate(b));
    assert(std::string_view(Recording::inspect({}, {}, {}, b).diagnostic)
        == "RHI-DISPATCH-RECORDING-ADMISSION-INPUT");
    b.dispatchDomainOptIn = false;
    assert(std::string_view(Recording::validate(b)) == "RHI-DISPATCH-RECORDING-OPT-IN");
    b.dispatchDomainOptIn = true;
    b.dispatchDomainVersion = 2u;
    assert(std::string_view(Recording::validate(b)) == "RHI-DISPATCH-RECORDING-VERSION");
    b.dispatchDomainVersion = 1u;
    for (const auto profile : {1u, 2u}) {
        b.contractVersion = profile;
        assert(std::string_view(Recording::validate(b)) == "RHI-DISPATCH-RECORDING-PROFILE");
    }
    b.contractVersion = 3u;
    request.recording = b;
    assert(std::string_view(rhi::OriginalRenderPassPreparationContract::validate(request))
        == "RHI-DISPATCH-RECORDING-DOMAIN-INPUT");
    const std::shared_ptr<const rhi::ComputeDispatchValidationDomain> missing;
    const rhi::OriginalComputeDispatchRecordingInput input{device, missing};
    b.dispatchDomainVersion = 0u;
    b.dispatchDomainOptIn = false;
    assert(std::string_view(Recording::inspect({}, {}, {}, b, &input).diagnostic)
        == "RHI-DISPATCH-RECORDING-ADMISSION-INPUT");
    assert(std::string_view(
               rhi::OriginalComputeDispatchRecordingAdmission::inspect(input, {}, {1u, 1u, 1u}))
        == "RHI-DISPATCH-RECORDING-VALIDATOR-ONLY");
    using Admission = rhi::OriginalComputeDispatchRecordingAdmission;
    static_assert(!std::is_default_constructible_v<Admission::Frozen>);
    static_assert(Admission::Frozen::directRetainedOwnerSlots == 2u);
    const char* frozenDiagnostic = nullptr;
    assert(!Admission::freeze(input, {}, {1u, 1u, 1u}, frozenDiagnostic));
    assert(std::string_view(frozenDiagnostic) == "RHI-DISPATCH-RECORDING-VALIDATOR-ONLY");
    assert(before == core::defaultCpuAllocator().stats().allocationCount);
    std::puts(
        "DISPATCH_RECORDING_INTENT_PASS fourBackends=true independentOptIn=true missingOrUnrequestedInputRefused=true allocation0=true execution=false");
}

void preparedPipelineContract(rhi::GpuDevice& device)
{
    using D = rhi::DescriptorType;
    using S = rhi::ShaderStage;
    const CloudPreparedPipelineSelection temporal{1u, CloudPreparedResolve::Temporal1, {}};
    const CloudPreparedPipelineSelection spatial{
        1u, CloudPreparedResolve::Reconstruction1, {1u, .025f, .025f}};
    require(CloudPreparedPipelines::validateSelection(temporal));
    require(CloudPreparedPipelines::validateSelection(spatial));
    for (unsigned mutation = 0; mutation < 6; ++mutation) {
        auto invalid = temporal;
        if (mutation == 0)
            invalid.contractVersion = 0;
        if (mutation == 1)
            invalid.contractVersion = 2;
        if (mutation == 2)
            invalid.resolve = CloudPreparedResolve::Unspecified;
        if (mutation == 3)
            invalid.resolve = static_cast<CloudPreparedResolve>(255);
        if (mutation == 4)
            invalid.reconstruction.contractVersion = 1;
        if (mutation == 5)
            invalid.reconstruction.maxRadianceRange = .025f;
        assert(!CloudPreparedPipelines::validateSelection(invalid).success);
    }
    auto package = [](S stage, std::initializer_list<D> types, unsigned depth) {
        rhi::ShaderPackageDesc p;
        p.entrypoints.push_back({{}, stage == S::Compute ? "computeMain" : "vertexMain", stage});
        if (stage == S::Compute) {
            p.entrypoints[0].computeWorkgroupSizeX = kCloudWorkgroupWidth;
            p.entrypoints[0].computeWorkgroupSizeY = kCloudWorkgroupHeight;
            p.entrypoints[0].computeWorkgroupSizeZ = 1;
        } else
            p.entrypoints.push_back({{}, "fragmentMain", S::Fragment});
        for (const auto type : types) {
            const auto binding = static_cast<unsigned>(p.reflection.descriptorBindings.size());
            p.reflection.descriptorBindings.push_back({0u,
                binding,
                type,
                stage == S::Compute ? S::Compute : S::Fragment,
                1u,
                UINT32_MAX,
                binding == depth});
        }
        return p;
    };
    const auto ray = package(S::Compute,
        {D::UniformBuffer, D::ReadOnlyStorageBuffer, D::SampledTexture, D::StorageBuffer},
        2u);
    const auto oldResolve = package(S::Compute,
        {D::UniformBuffer, D::ReadOnlyStorageBuffer, D::ReadOnlyStorageBuffer, D::StorageBuffer},
        UINT32_MAX);
    const auto newResolve = package(S::Compute,
        {D::UniformBuffer,
            D::ReadOnlyStorageBuffer,
            D::ReadOnlyStorageBuffer,
            D::StorageBuffer,
            D::ReadOnlyStorageBuffer,
            D::SampledTexture,
            D::UniformBuffer},
        5u);
    const auto composite = package(S::Vertex,
        {D::UniformBuffer, D::ReadOnlyStorageBuffer, D::SampledTexture, D::SampledTexture},
        3u);
    require(CloudPreparedPipelines::validateInputs(temporal, ray, oldResolve, composite));
    require(CloudPreparedPipelines::validateInputs(spatial, ray, newResolve, composite));
    assert(!CloudPreparedPipelines::validateInputs(temporal, ray, newResolve, composite).success);
    assert(!CloudPreparedPipelines::validateInputs(spatial, ray, oldResolve, composite).success);
    for (unsigned mutation = 0; mutation < 8; ++mutation) {
        auto invalid = ray;
        if (mutation == 0)
            invalid.entrypoints[0].computeWorkgroupSizeX = 4;
        if (mutation == 1)
            invalid.entrypoints[0].entryPoint = "foreign";
        if (mutation == 2)
            invalid.reflection.descriptorBindings[2].depthTexture = false;
        if (mutation == 3)
            invalid.reflection.descriptorBindings[1].type = D::StorageBuffer;
        if (mutation == 4)
            invalid.reflection.descriptorBindings[1].set = 1;
        if (mutation == 5)
            invalid.reflection.descriptorBindings[1].binding = 0;
        if (mutation == 6)
            invalid.reflection.descriptorBindings[1].arrayCount = 2;
        if (mutation == 7)
            invalid.reflection.pushConstantByteCount = 16;
        assert(!CloudPreparedPipelines::validateInputs(spatial, invalid, newResolve, composite)
                .success);
    }
    CloudPreparedResourceRequest resources;
    resources.config = tests::cloud::config();
    require(CloudQuality::apply(1u, CloudQualityTier::Medium, resources.config));
    resources.extent = {64u, 48u};
    resources.assetBytes = tests::cloud::asset().cells.size() * sizeof(core::Vec4);
    resources.maxDescriptorSets = 9u;
    resources.maxDescriptorElements = 45u;
    auto c = pipelineContext(device, resources, spatial);
    // Four existing lowering conclusions are consumed without promotion.
    for (const auto backend :
        {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
        const auto decision =
            rhi::OriginalRenderPassPreparationContract::lowering(backend, c.preparation);
        assert(decision.recordingAvailable == (backend == rhi::Backend::Vulkan));
        assert(decision.validatorOnly == (backend == rhi::Backend::Null));
        assert(bool(decision.diagnostic) == !decision.recordingAvailable);
    }
    // Null refuses before attempting to load the deliberately absent files.
    CloudPreparedPipelines pipelines;
    core::pmr::String diagnostic;
    const auto result = pipelines.create(c, diagnostic);
    assert(!result.success && diagnostic == "RHI-RENDER-PASS-PREPARATION-NULL-VALIDATOR");
    assert(!pipelines.created() && !pipelines.raymarch() && !pipelines.resolve());
    for (const auto& q : c.quotas)
        assert(!q.artifact->liveBytes() && !q.input->liveBytes() && !q.owner->liveBytes()
            && !q.native->liveBytes() && !q.wrapper->liveBytes());
}

void preparedResourceContract(rhi::GpuDevice& device)
{
    const CloudPreparedPipelineSelection spatial{
        1u, CloudPreparedResolve::Reconstruction1, {1u, .025f, .025f}};
    const CloudPreparedPipelineSelection temporal{1u, CloudPreparedResolve::Temporal1, {}};
    CloudPreparedResourceRequest request;
    request.config = tests::cloud::config();
    require(CloudQuality::apply(1u, CloudQualityTier::Medium, request.config));
    request.extent = {1280u, 720u};
    request.assetBytes = tests::cloud::asset().cells.size() * sizeof(core::Vec4);
    request.maxDescriptorSets = 9u;
    request.maxDescriptorElements = 45u;
    auto limits = device.capabilities().computeResources;
    CloudPreparedResourcePlan plan;
    const auto run = [&] {
        return CloudPreparedPipelines::planResources(spatial, request, limits, plan);
    };
    assert(!run().success); // Refinement must fit the actual authored work budget.
    request.config.maxDensityEvaluations = UINT64_MAX;
    require(run());
    assert(plan.raw.sampleExtent.width == 640u && plan.raw.sampleExtent.height == 360u);
    assert(plan.reconstruction.resolvedBufferBytes == 1280ull * 720u * kCloudSampleBytes);
    assert(plan.reconstruction.retainedGpuBytes == 132721840ull);
    assert(plan.reconstruction.densityEvaluationBound == 2506752000ull);
    assert(plan.descriptorSets == 9u && plan.descriptorElements == 45u);
    const auto accepted = plan;
    limits.maxComputeWorkgroupInvocations = 64u;
    limits.maxComputeWorkgroupSizeX = limits.maxComputeWorkgroupSizeY = 8u;
    limits.maxComputeWorkgroupSizeZ = 1u;
    limits.maxComputeWorkgroupCountX = 160u;
    limits.maxComputeWorkgroupCountY = 90u;
    limits.maxComputeWorkgroupCountZ = 1u;
    limits.maxStorageBufferBindingsPerStage = 4u;
    limits.maxUniformBufferBindingsPerStage = 2u;
    limits.maxUniformBufferBindingSizeBytes = sizeof(CloudGpuConstants);
    limits.maxStorageBufferBindingSizeBytes = accepted.reconstruction.resolvedBufferBytes;
    limits.maxSampledTextureBindingsPerStage = 2u;
    limits.maxTexture2DDimension = 1280u;
    require(run());
    const auto exactLimits = limits;
    const auto exactRequest = request;
    for (unsigned mutation = 0; mutation < 14; ++mutation) {
        limits = exactLimits;
        request = exactRequest;
        if (mutation == 0)
            --limits.maxComputeWorkgroupInvocations;
        if (mutation == 1)
            --limits.maxComputeWorkgroupSizeX;
        if (mutation == 2)
            --limits.maxComputeWorkgroupSizeZ;
        if (mutation == 3)
            --limits.maxComputeWorkgroupCountX;
        if (mutation == 4)
            --limits.maxComputeWorkgroupCountY;
        if (mutation == 5)
            --limits.maxStorageBufferBindingsPerStage;
        if (mutation == 6)
            --limits.maxUniformBufferBindingsPerStage;
        if (mutation == 7)
            --limits.maxUniformBufferBindingSizeBytes;
        if (mutation == 8)
            --limits.maxStorageBufferBindingSizeBytes;
        if (mutation == 9)
            --request.maxDescriptorSets;
        if (mutation == 10)
            --request.maxDescriptorElements;
        if (mutation == 11)
            request.config.gpuBudgetBytes = accepted.reconstruction.retainedGpuBytes - 1u;
        if (mutation == 12)
            request.config.maxDensityEvaluations =
                accepted.reconstruction.densityEvaluationBound - 1u;
        if (mutation == 13)
            limits.sampledDepthTexture = false;
        assert(!run().success && !plan.raw.sampleCount && !plan.reconstruction.resolvedCount
            && !plan.descriptorSets && !plan.descriptorElements);
    }
    request = exactRequest;
    limits = exactLimits;
    request.maxDescriptorElements = 36u;
    require(CloudPreparedPipelines::planResources(temporal, request, limits, plan));
    assert(!plan.reconstruction.resolvedCount && plan.descriptorElements == 36u);
    request = exactRequest;
    for (const auto backend :
        {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
        auto declared = device.capabilities();
        declared.backend = backend;
        declared.features.computeDispatch = declared.features.storageBufferBinding = false;
        // Numeric admission cannot open the legacy public execution gate.
        require(CloudPreparedPipelines::planResources(
            spatial, request, declared.computeResources, plan));
        CloudExecutionPlan publicPlan;
        assert(!CloudExecutionPlanner::plan(request.config,
                   request.extent,
                   request.assetBytes,
                   sizeof(CloudGpuConstants),
                   declared,
                   publicPlan)
                   .success
            && !publicPlan.sampleCount);
        declared.computeResources.mode = rhi::CapabilityMode::Degraded;
        assert(!CloudPreparedPipelines::planResources(
            spatial, request, declared.computeResources, plan)
                .success);
    }
    assert(request.config.resolutionDivisor == 2u && request.config.viewSteps == 64u
        && request.config.lightSteps == 8u
        && request.config.gpuBudgetBytes == 128u * 1024u * 1024u);
}

CloudPreparedResourceCreateContext resourceContext(
    rhi::GpuDevice& device, const assets::cloud::CloudAsset& asset, bool spatial)
{
    CloudPreparedResourceCreateContext c{device, asset};
    c.contractVersion = 1u;
    c.prototypeOptIn = true;
    c.cameraId = 1u;
    c.selection = spatial ? CloudPreparedPipelineSelection{1u,
                                CloudPreparedResolve::Reconstruction1,
                                {1u, .025f, .025f}}
                          : CloudPreparedPipelineSelection{1u, CloudPreparedResolve::Temporal1, {}};
    c.resources.config = tests::cloud::config();
    c.resources.config.resolutionDivisor = 2u;
    c.resources.extent = {32u, 24u};
    c.resources.assetBytes = asset.cells.size() * sizeof(core::Vec4);
    c.resources.maxDescriptorSets = 9u;
    c.resources.maxDescriptorElements = spatial ? 45u : 36u;
    CloudPreparedResourcePlan plan;
    require(CloudPreparedPipelines::planResources(
        c.selection, c.resources, device.capabilities().computeResources, plan));
    c.resources.config.gpuBudgetBytes =
        spatial ? plan.reconstruction.retainedGpuBytes : plan.raw.retainedGpuBytes;
    c.sampleMemory = rhi::MemoryUsage::CpuToGpu;
    c.maxCpuOwnerBytes = c.maxCpuInitializationBytes = cpuBytes;
    c.ownerQuota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
    c.initializationQuota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
    c.sourceQuota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
    c.historyQuota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
    c.retainedLogicalBytes =
        core::makeShared<core::AtomicCapacityBudget>(c.resources.config.gpuBudgetBytes);
    return c;
}

void preparedSubmissionLifetime(rhi::GpuDevice& device)
{
    const auto asset = tests::cloud::asset();
    render_graph::FrameGraph graph(device);
    auto c = frame(graph, 0.f);
    for (const bool spatial : {false, true}) {
        auto create = resourceContext(device, asset, spatial);
        CloudPreparedResources resources;
        renderer::cloud::detail::CloudPreparedSubmission observer;
        render_graph::PassSubmissionObserver* retainedCallback = &observer;
        CloudPreparedHistory absent;
        assert(!observer.attach(absent).success && !observer.beginFrame().success);
        require(resources.create(create));
        require(observer.attach(*resources.history()));
        assert(!observer.attach(*resources.history()).success);
        require(resources.prepareFrame(c));
        require(observer.beginFrame());
        const auto cancelled = observer.token();
        const auto replacedControllerToken = resources.history()->token();
        assert(retainedCallback->validateSubmission(cancelled)); // Not yet contributed.
        require(observer.markContributed(cancelled));
        assert(!observer.markContributed(cancelled).success);
        require(resources.history()->cancelPendingFrame());
        observer.invalidateFrame();
        // Keep the callback address and token while destroying its old target.
        observer.detach();
        require(resources.releaseIdle());
        assert(retainedCallback->validateSubmission(cancelled));
        retainedCallback->onSubmitted(cancelled, {});
        assert(!observer.beginFrame().success);

        require(resources.create(create));
        require(resources.prepareFrame(c));
        assert(resources.history()->token().ownerId == replacedControllerToken.ownerId
            && resources.history()->token().generation == replacedControllerToken.generation);
        require(observer.attach(*resources.history()));
        require(observer.beginFrame());
        const auto current = observer.token();
        assert(current.ownerId == cancelled.ownerId && current.generation != cancelled.generation);
        require(observer.markContributed(current));
        auto wrongCamera = current;
        ++wrongCamera.ownerId;
        auto completion = core::shareUnique(device.createFence());
        assert(completion);
        // Explicit logical Null completion, never native synchronization proof.
        completion->signal();
        const std::array quotas{
            create.ownerQuota, create.initializationQuota, create.sourceQuota, create.historyQuota};
        assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
            quotas, [&]() noexcept -> const char* {
                assert(retainedCallback->validateSubmission(cancelled));
                assert(retainedCallback->validateSubmission(wrongCamera));
                retainedCallback->onSubmitted(cancelled, completion);
                retainedCallback->onSubmitted(wrongCamera, completion);
                assert(resources.history()->raw());
                assert(!retainedCallback->validateSubmission(current));
                retainedCallback->onSubmitted(current, {});
                assert(resources.history()->raw()); // Missing completion keeps failure custody.
                return nullptr;
            }));
        assert(!resources.history()->cancelPendingFrame().success);
        assert(!resources.releaseIdle().success);
        retainedCallback->onSubmitted(current, completion);
        assert(!observer.token().ownerId && !resources.history()->raw());
        assert(retainedCallback->validateSubmission(current));
        assert(resources.history()->idle());

        c.time += 1.f / 60.f;
        require(resources.prepareFrame(c));
        require(observer.beginFrame());
        const auto failed = observer.token();
        require(observer.markContributed(failed));
        assert(!retainedCallback->validateSubmission(failed));
        assert(!resources.releaseIdle().success);
        // Explicit failure drain: detach before destruction, then reject old
        // callbacks even if no replacement controller has been attached yet.
        observer.detach();
        resources.release();
        assert(retainedCallback->validateSubmission(failed));
        retainedCallback->onSubmitted(failed, completion);
        assert(!create.retainedLogicalBytes->used());
        for (const auto& quota : quotas)
            assert(!quota->liveBytes());
    }
}

void preparedFeatureOwnership(rhi::GpuDevice& device)
{
    const auto asset = tests::cloud::asset();
    render_graph::FrameGraph graph(device);
    auto frameContext = frame(graph, 0.f);
    for (const bool spatial : {false, true}) {
        auto resources = resourceContext(device, asset, spatial);
        auto pipelines = pipelineContext(device, resources.resources, resources.selection);
        const auto stateQuota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        CloudPreparedFeatureCreateContext c{pipelines, resources};
        c.contractVersion = 1u;
        c.prototypeOptIn = true;
        c.maxCpuStateBytes = cpuBytes;
        c.stateQuota = stateQuota;
        VolumetricCloudFeature feature;
        CloudPreparedFrameBuffers buffers;
        buffers.historyGeneration = 123u;
        assert(!feature.prepareFrame(frameContext, buffers).success && !buffers.historyGeneration);
        assert(!feature.created() && !feature.frameReady() && !feature.preparedPipelines()
            && !feature.preparedResources() && !feature.preparedHistory());
        assert(!feature.resize(resources).success);
        const auto assertReleased = [&] {
            assert(!feature.created() && !feature.preparedResources() && !feature.frameReady());
            assert(!stateQuota->liveBytes() && !resources.retainedLogicalBytes->used());
            for (const auto& quota : {resources.ownerQuota,
                     resources.initializationQuota,
                     resources.sourceQuota,
                     resources.historyQuota,
                     pipelines.orchestrationQuota})
                assert(!quota->liveBytes());
            for (const auto& q : pipelines.quotas)
                assert(!q.artifact->liveBytes() && !q.input->liveBytes() && !q.owner->liveBytes()
                    && !q.native->liveBytes() && !q.wrapper->liveBytes());
        };
        core::pmr::String diagnostic;
        for (unsigned mutation = 0u; mutation < 6u; ++mutation) {
            auto invalid = c;
            if (mutation == 0u)
                invalid.contractVersion = 0u;
            if (mutation == 1u)
                invalid.prototypeOptIn = false;
            if (mutation == 2u)
                invalid.maxCpuStateBytes = 0u;
            if (mutation == 3u)
                invalid.stateQuota.reset();
            if (mutation == 4u)
                invalid.maxCpuStateBytes -= 1u;
            if (mutation == 5u)
                invalid.contractVersion = 2u;
            assert(!feature.create(invalid, diagnostic).success);
            assertReleased();
        }
        // Equal extents/byte counts do not admit a changed sampling policy.
        for (unsigned mutation = 0u; mutation < 15u; ++mutation) {
            auto other = pipelines;
            auto& request = other.resources;
            if (mutation == 0u)
                ++request.config.resolutionDivisor;
            if (mutation == 1u)
                ++request.config.viewSteps;
            if (mutation == 2u)
                ++request.config.lightSteps;
            if (mutation == 3u)
                ++request.config.framesInFlight;
            if (mutation == 4u)
                ++request.config.gpuBudgetBytes;
            if (mutation == 5u)
                ++request.config.maxDensityEvaluations;
            if (mutation == 6u)
                ++request.config.maxDistance;
            if (mutation == 7u)
                request.config.historyWeight += .01f;
            if (mutation == 8u)
                request.config.historyDepthTolerance += .01f;
            if (mutation == 9u)
                request.config.temporal = !request.config.temporal;
            if (mutation == 10u)
                ++request.extent.width;
            if (mutation == 11u)
                ++request.assetBytes;
            if (mutation == 12u)
                ++request.maxDescriptorSets;
            if (mutation == 13u)
                ++request.maxDescriptorElements;
            if (mutation == 14u)
                other.selection.reconstruction.maxRadianceRange += .01f;
            CloudPreparedFeatureCreateContext invalid{
                other, resources, 1u, true, cpuBytes, stateQuota};
            const auto result = feature.create(invalid, diagnostic);
            assert(!result.success
                && std::string_view(result.diagnostic) == "CLOUD-PREPARED-FEATURE-CREATE-MISMATCH");
            assertReleased();
        }
        auto foreignDevice = rhi::createNullDevice();
        auto foreign = pipelineContext(*foreignDevice, resources.resources, resources.selection);
        CloudPreparedFeatureCreateContext wrongDevice{
            foreign, resources, 1u, true, cpuBytes, stateQuota};
        assert(!feature.create(wrongDevice, diagnostic).success);
        assertReleased();
        // Shared resource preflight rejects before state/pipeline allocation.
        CloudPreparedResourcePlan plan;
        const auto capabilities = device.capabilities();
        assert(
            !test::CpuQuotaFactoryFixture::verifyHotNoAllocation(std::array{stateQuota,
                                                                     resources.ownerQuota,
                                                                     resources.initializationQuota,
                                                                     resources.sourceQuota,
                                                                     resources.historyQuota},
                [&]() noexcept -> const char* {
                    const auto result =
                        CloudPreparedResources::validateCreate(resources, capabilities, plan);
                    return result.success ? nullptr : result.diagnostic;
                }));
        assert(plan.raw.sampleBufferBytes);
        const auto memory = resources.sampleMemory;
        resources.sampleMemory.reset();
        assert(!feature.create(c, diagnostic).success);
        assertReleased();
        resources.sampleMemory = memory;
        // Genuine registered Null resources are created then released when the
        // original pipeline lowering refuses. This is refusal/rollback proof,
        // never a fabricated successful pipeline/Feature or native draw.
        const auto reachesLowering = [&] {
            const auto result = feature.create(c, diagnostic);
            const bool refused =
                !result.success && diagnostic == "RHI-RENDER-PASS-PREPARATION-NULL-VALIDATOR";
            core::pmr::String{}.swap(diagnostic);
            assert(!feature.created());
            return refused;
        };
        assert(reachesLowering());
        assertReleased();
        test::CpuQuotaFactoryFixture fixture(stateQuota);
        const auto minimum = fixture.measureSingleUse([] {}, reachesLowering);
        fixture.verifySingleUse(minimum, [] {}, reachesLowering);
        assertReleased();
        {
            core::CpuMemoryQuotaLease exhausted(stateQuota, stateQuota->limitBytes());
            assert(exhausted.charged() && !reachesLowering());
        }
        assertReleased();
        feature.release();
        require(feature.cancelPendingFrame());
    }
}

void resourceOwnership(rhi::GpuDevice& device)
{
    const auto asset = tests::cloud::asset();
    render_graph::FrameGraph graph(device);
    auto frameContext = frame(graph, 0.f);
    for (const bool spatial : {false, true}) {
        const auto c = resourceContext(device, asset, spatial);
        unsigned checkpoint = 0u;
        const auto assertReleased = [&] {
            ++checkpoint;
            assert(!c.retainedLogicalBytes->used());
            unsigned domain = 0u;
            for (const auto& quota :
                {c.ownerQuota, c.initializationQuota, c.sourceQuota, c.historyQuota}) {
                if (quota->liveBytes())
                    std::fprintf(stderr,
                        "resource ownership spatial=%u checkpoint=%u domain=%u retained=%llu\n",
                        spatial,
                        checkpoint,
                        domain,
                        static_cast<unsigned long long>(quota->liveBytes()));
                assert(!quota->liveBytes());
                ++domain;
            }
        };
        CloudPreparedResources owner;
        CloudPreparedFrameBuffers pending;
        assert(!owner.created() && !owner.history() && !owner.pendingBuffers(pending).success);
        assert(!owner.prepareFrame(frameContext).success);
        for (unsigned mutation = 0u; mutation < 12u; ++mutation) {
            auto invalid = c;
            if (mutation == 0u)
                invalid.contractVersion = 0u;
            if (mutation == 1u)
                invalid.prototypeOptIn = false;
            if (mutation == 2u)
                invalid.cameraId = 0u;
            if (mutation == 3u)
                ++invalid.resources.assetBytes;
            if (mutation == 4u)
                invalid.sampleMemory.reset();
            if (mutation == 5u)
                invalid.sampleMemory = rhi::MemoryUsage::GpuToCpu;
            if (mutation == 6u)
                --invalid.resources.maxDescriptorElements;
            if (mutation == 7u)
                invalid.maxCpuInitializationBytes = 1u;
            if (mutation == 8u)
                invalid.maxCpuOwnerBytes = 0u;
            if (mutation == 9u)
                invalid.sourceQuota.reset();
            if (mutation == 10u)
                invalid.historyQuota.reset();
            if (mutation == 11u)
                invalid.retainedLogicalBytes = core::makeShared<core::AtomicCapacityBudget>(1u);
            assert(!owner.create(invalid).success && !owner.created() && !owner.history());
            assert(!owner.plan().raw.sampleCount);
            assertReleased();
        }
        require(owner.create(c));
        assert(!owner.create(c).success && owner.created());
        assert(c.retainedLogicalBytes->used() == c.resources.config.gpuBudgetBytes);
        assert(!c.initializationQuota->liveBytes());
        assert(!owner.pendingBuffers(pending).success);
        require(owner.prepareFrame(frameContext));
        require(owner.pendingBuffers(pending));
        assert(pending.constants == owner.history()->constants()
            && pending.raw == owner.history()->raw()
            && pending.previous == owner.history()->previous()
            && pending.resolved == owner.history()->resolved()
            && pending.historyGeneration == owner.history()->token().generation);
        assert(bool(pending.reconstructionConstants) == spatial);
        assert(
            pending.raw->originalObject()->desc().sizeBytes == owner.plan().raw.sampleBufferBytes);
        assert(pending.resolved->originalObject()->desc().sizeBytes
            == (spatial ? owner.plan().reconstruction.resolvedBufferBytes
                        : owner.plan().raw.sampleBufferBytes));
        for (const auto& lease :
            {pending.density, pending.raw, pending.resolved, pending.constants}) {
            assert(!rhi::OriginalCommandBufferLease::validateOriginal(lease));
            assert(!device.validateOriginalResourceRegistration(*lease->originalObject()));
            assert(lease->originalObject()->hostWritesSealed());
            assert(lease->originalObject()->desc().memory == *c.sampleMemory);
            assert(lease->originalObject()->gpuWritesSealed()
                == (lease == pending.density || lease == pending.constants));
            assert(lease->originalCapacityLease(0u) == pending.density->originalCapacityLease(0u));
        }
        if (spatial) {
            assert(pending.reconstructionConstants->originalObject()->gpuWritesSealed());
            assert(pending.reconstructionConstants->originalCapacityLease(0u)
                == pending.constants->originalCapacityLease(0u));
            assert(pending.reconstructionConstants->originalCapacityLease(1u)
                == pending.constants->originalCapacityLease(1u));
        }
        assert(!owner.releaseIdle().success);
        const auto cancelled = owner.history()->token();
        require(owner.history()->cancelPendingFrame());
        assert(owner.history()->validateSubmission(cancelled));
        assert(!owner.pendingBuffers(pending).success && !pending.raw && !pending.constants
            && !pending.historyGeneration);
        // Exact pending handoff and existing logical reader retirement, with no
        // substitute native proof. A writer completion alone cannot free a reader.
        std::array<std::shared_ptr<rhi::GpuFence>, 3u> fences;
        std::shared_ptr<const rhi::OriginalCommandBufferLease> previous;
        std::shared_ptr<const rhi::OriginalCommandBufferLease> firstRaw;
        for (unsigned i = 0u; i < (spatial ? 2u : 3u); ++i) {
            frameContext.time = float(i) / 60.f;
            require(owner.prepareFrame(frameContext));
            require(owner.pendingBuffers(pending));
            if (!i)
                firstRaw = pending.raw;
            if (i)
                assert(pending.previous == previous);
            previous = pending.resolved;
            fences[i] = core::shareUnique(device.createFence());
            owner.history()->onSubmitted(owner.history()->token(), fences[i]);
            assert(!owner.pendingBuffers(pending).success && !pending.raw);
        }
        assert(!owner.history()->frameReady() && !owner.releaseIdle().success);
        fences[0]->signal();
        if (spatial) {
            // The first frame's full-size spare is free; its resolved output
            // still has the second frame's reader and cannot be overwritten.
            assert(owner.history()->frameReady());
            require(owner.prepareFrame(frameContext));
            assert(owner.history()->raw() != firstRaw);
            require(owner.history()->cancelPendingFrame());
        } else
            assert(!owner.history()->frameReady());
        fences[1]->signal();
        assert(owner.history()->frameReady());
        require(owner.prepareFrame(frameContext));
        assert(owner.history()->raw() == firstRaw);
        require(owner.history()->cancelPendingFrame());
        if (fences[2])
            fences[2]->signal();
        previous.reset();
        firstRaw.reset();
        require(owner.releaseIdle());
        assertReleased();

        // One canonical inventory, with reset-frame aliases exported once.
        // Uniform and persistent resource leases must use the same source quota;
        // shared byte/generation custody contributes four unique owner nodes.
        require(owner.create(c));
        require(owner.prepareFrame(frameContext));
        require(owner.pendingBuffers(pending));
        std::shared_ptr<const rhi::OriginalCommandResourceSources> inventory;
        {
            using Sources = rhi::OriginalCommandResourceSources;
            std::array<Sources::Source, 6u> prefix;
            std::size_t count = 0u;
            for (const auto& lease : {pending.constants,
                     pending.density,
                     pending.raw,
                     pending.resolved,
                     pending.previous,
                     pending.reconstructionConstants}) {
                if (!lease)
                    continue;
                bool present = false;
                for (std::size_t i = 0u; i < count; ++i)
                    present |=
                        std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(prefix[i])
                        == lease;
                if (present)
                    continue;
                prefix[count++] = lease;
            }
            assert(count == (spatial ? 6u : 4u));
            rhi::OriginalCommandAdmissionBudget admission;
            admission.contractVersion = 1u;
            admission.prototypeOptIn = true;
            admission.maxCommands = 1u;
            admission.maxGraphOwners = static_cast<std::uint32_t>(2u * count + 6u);
            admission
                .maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Buffer)] =
                count;
            admission.maxCpuBytes.fill(cpuBytes);
            const char* id = nullptr;
            inventory =
                Sources::prepareOriginal(prefix.data(), count, admission, c.sourceQuota, id);
            assert(inventory && !id && !Sources::validateRegisteredLifetime(inventory, device));
            std::uint32_t owners = 0u;
            assert(!inventory->cpuOwnerCount(2u, admission.maxGraphOwners, owners));
            assert(owners == admission.maxGraphOwners);
            assert(inventory->cpuOwnerCount(2u, owners - 1u, owners));
        }
        pending = {};
        require(owner.history()->cancelPendingFrame());
        require(owner.releaseIdle());
        assert(c.retainedLogicalBytes->used() == c.resources.config.gpuBudgetBytes);
        assert(!owner.create(c).success);
        inventory.reset();
        assertReleased();

        // External original leases keep the whole generation reservation alive
        // even after the resource owner itself releases every persistent buffer.
        for (unsigned heldKind = 0u; heldKind < (spatial ? 4u : 3u); ++heldKind) {
            require(owner.create(c));
            require(owner.prepareFrame(frameContext));
            require(owner.pendingBuffers(pending));
            auto held = heldKind == 0u ? pending.raw
                : heldKind == 1u       ? pending.density
                : heldKind == 2u       ? pending.constants
                                       : pending.reconstructionConstants;
            pending = {};
            require(owner.history()->cancelPendingFrame());
            require(owner.releaseIdle());
            assert(c.retainedLogicalBytes->used() == c.resources.config.gpuBudgetBytes);
            assert(!owner.create(c).success && !owner.created());
            held.reset();
            assertReleased();
            require(owner.create(c));
            require(owner.releaseIdle());
            assertReleased();
        }
        // Completed/cancelled graphs can still own immutable uniforms. Their
        // count cannot exceed the per-slot allowance in the logical plan.
        require(owner.create(c));
        std::array<CloudPreparedFrameBuffers, 3u> generations;
        for (auto& generation : generations) {
            require(owner.prepareFrame(frameContext));
            require(owner.pendingBuffers(generation));
            require(owner.history()->cancelPendingFrame());
        }
        const auto exhausted = owner.prepareFrame(frameContext);
        assert(!exhausted.success
            && !std::strcmp(exhausted.diagnostic, "CLOUD-PREPARED-HISTORY-GENERATION-CAPACITY"));
        generations[0].constants.reset();
        if (spatial)
            assert(!owner.prepareFrame(frameContext).success);
        generations[0].reconstructionConstants.reset();
        require(owner.prepareFrame(frameContext));
        require(owner.history()->cancelPendingFrame());
        require(owner.releaseIdle());
        assert(c.retainedLogicalBytes->used() == c.resources.config.gpuBudgetBytes);
        generations = {};
        assertReleased();

        // Exact/one-byte-short/exhausted independent CPU domains must unwind
        // all successful buffer prefixes and the shared logical reservation.
        const auto createAndRelease = [&] {
            CloudPreparedResources candidate;
            const bool success = candidate.create(c).success;
            if (success)
                require(candidate.releaseIdle());
            return success;
        };
        for (const auto& quota :
            {c.ownerQuota, c.initializationQuota, c.sourceQuota, c.historyQuota}) {
            test::CpuQuotaFactoryFixture fixture(quota);
            const auto minimum = fixture.measureSingleUse([] {}, createAndRelease);
            fixture.verifySingleUse(minimum, [] {}, createAndRelease);
            assertReleased();
        }
        require(owner.create(c));
        const auto baseline = c.historyQuota->liveBytes();
        {
            core::CpuMemoryQuotaLease pressure(
                c.historyQuota, c.historyQuota->limitBytes() - baseline);
            assert(pressure.charged());
            assert(!owner.prepareFrame(frameContext).success && owner.history()->idle());
            assert(c.historyQuota->liveBytes() == c.historyQuota->limitBytes());
        }
        require(owner.prepareFrame(frameContext));
        require(owner.history()->cancelPendingFrame());
        require(owner.releaseIdle());
        assertReleased();
        // The Null validator accepts authored GpuOnly; this is no native host
        // mapping/initialization claim, and the requested memory class stays exact.
        auto gpuOnly = c;
        gpuOnly.sampleMemory = rhi::MemoryUsage::GpuOnly;
        require(owner.create(gpuOnly));
        require(owner.prepareFrame(frameContext));
        require(owner.pendingBuffers(pending));
        assert(pending.raw->originalObject()->desc().memory == rhi::MemoryUsage::GpuOnly);
        assert(pending.density->originalObject()->desc().memory == rhi::MemoryUsage::CpuToGpu);
        pending = {};
        require(owner.history()->cancelPendingFrame());
        require(owner.releaseIdle());
        assertReleased();
    }
}

void synchronousDescriptorLossIntent()
{
    using Loss = rhi::OriginalTypedNativeLossJoinContract;
    using Write = rhi::OriginalGeneralDescriptorWriteContract;
    rhi::OriginalResourceRegistrationBudget registration;
    registration.contractVersion = 10u;
    registration.prototypeOptIn = true;
    registration.maxPipelineLayouts = registration.maxComputePipelines = 1u;
    registration.maxDescriptorArenas = registration.maxDescriptorSets = 1u;
    registration.maxRetainedOwners = 32u;
    registration.maxCpuMetadataBytes = cpuBytes;
    assert(!Loss::validate({3u, true}, registration));
    assert(!Loss::validate({4u, true}, registration));
    for (unsigned mode = 0u; mode < 9u; ++mode) {
        auto changed = registration;
        rhi::OriginalTypedNativeLossJoinRequest request{4u, true};
        switch (mode) {
        case 0u:
            request.contractVersion = 5u;
            break;
        case 1u:
            request.prototypeOptIn = false;
            break;
        case 2u:
            changed.contractVersion = 7u;
            break;
        case 3u:
            changed.maxDescriptorArenas = 0u;
            break;
        case 4u:
            changed.maxDescriptorSets = 0u;
            break;
        case 5u:
            changed.maxDescriptorTables = 1u;
            break;
        case 6u:
            changed.samplers = {1u, true, 1u};
            break;
        case 7u:
            changed.maxPipelineLayouts = 0u;
            break;
        case 8u:
            changed.maxComputePipelines = 0u;
            break;
        }
        assert(Loss::validate(request, changed));
    }
    for (const auto backend : {rhi::Backend::Null,
             rhi::Backend::Vulkan,
             rhi::Backend::Metal,
             rhi::Backend::WebGPU,
             rhi::Backend::WebGL,
             static_cast<rhi::Backend>(255u)}) {
        const auto old = Loss::lowering(backend, 3u), joined = Loss::lowering(backend, 4u);
        assert(!old.scopedGroupJoinAvailable);
        assert(joined.scopedGroupJoinAvailable
            == (backend == rhi::Backend::Vulkan || backend == rhi::Backend::Metal));
        assert(joined.validatorOnly == (backend == rhi::Backend::Null));
        assert(!joined.recordingAvailable && !joined.executionAvailable);
        // The independent descriptor-only registration profile has no /4.
        const auto descriptorOnly =
            rhi::OriginalDescriptorNativeLossRegistrationContract::lowering(backend, 4u);
        assert(!descriptorOnly.registrationAvailable && descriptorOnly.diagnostic);
    }
    for (std::uint32_t version = 1u; version <= 4u; ++version) {
        rhi::OriginalGeneralDescriptorWriteRequest request{version,
            true,
            1u,
            static_cast<std::uint32_t>(Write::ownerSlots(1u, version)),
            version < 3u ? Write::callbackCount : 0u,
            {cpuBytes, cpuBytes, cpuBytes},
            4096u};
        assert(!Write::validate(request));
        auto insufficientOwners = request;
        --insufficientOwners.maxRetainedOwnerSlots;
        assert(Write::validate(insufficientOwners));
        assert(Write::ownerSlots(1u, version)
            == (version >= 3u
                    ? Write::synchronousFixedOwnerSlots + Write::synchronousBindingOwnerSlots
                    : Write::fixedOwnerSlots + Write::bindingOwnerSlots));
        request.lossGroupJoinVersion = 4u;
        assert(Write::validate(request));
        request.lossGroupJoinOptIn = true;
        assert(bool(Write::validate(request)) == (version < 3u));
        request.lossGroupJoinVersion = 3u;
        assert(Write::validate(request));
        request.lossGroupJoinVersion = 0u;
        assert(Write::validate(request));
    }
    std::puts(
        "SYNC_DESCRIPTOR_LOSS_INTENT_PASS independentVersion4=true registry10=true asynchronousProfilesUnchanged=true execution=false");
}

void preparedDescriptorDeclarations(rhi::GpuDevice& device)
{
    using Sources = rhi::OriginalCommandResourceSources;
    using Buffer = rhi::OriginalCommandBufferLease;
    using Texture = rhi::OriginalCommandTextureLease;
    using ViewLease = rhi::OriginalCommandTextureViewLease;
    using View = rhi::OriginalCommandTextureViewSource;
    for (const auto backend :
        {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU})
        for (const auto decision :
            {rhi::OriginalGeneralDescriptorWriteContract::synchronousComputeLowering(backend),
                rhi::OriginalGeneralDescriptorWriteContract::synchronousGraphicsLowering(
                    backend)}) {
            assert(decision.preparationAvailable
                == (backend == rhi::Backend::Vulkan || backend == rhi::Backend::Metal));
            assert(decision.validatorOnly == (backend == rhi::Backend::Null));
            assert(!decision.executionAvailable
                && bool(decision.diagnostic) == !decision.preparationAvailable);
        }
    const auto asset = tests::cloud::asset();
    render_graph::FrameGraph graph(device);
    for (const bool spatial : {false, true}) {
        const auto c = resourceContext(device, asset, spatial);
        CloudPreparedResources resources;
        require(resources.create(c));
        assert(resources.historySlots().size() == c.resources.config.framesInFlight);
        require(resources.prepareFrame(frame(graph, 0.f)));
        CloudPreparedFrameBuffers pending;
        require(resources.pendingBuffers(pending));
        std::array<Sources::Source, 16u> prefix;
        std::size_t count = 0u;
        const auto position = [&](const std::shared_ptr<const Buffer>& lease) {
            if (!lease)
                return SIZE_MAX;
            for (std::size_t i = 0u; i < count; ++i)
                if (const auto* stored = std::get_if<std::shared_ptr<const Buffer>>(&prefix[i]);
                    stored && *stored == lease)
                    return i;
            prefix[count] = lease;
            return count++;
        };
        CloudPreparedDescriptorLocations locations;
        locations.constants = position(pending.constants);
        locations.density = position(pending.density);
        locations.raw = position(pending.raw);
        locations.previous = position(pending.previous);
        locations.resolved = position(pending.resolved);
        locations.reconstructionConstants = position(pending.reconstructionConstants);
        const auto texture = [&](bool depth, std::uint32_t width, std::uint32_t mips) {
            rhi::TextureDesc desc;
            desc.extent = {width, 24u, 1u};
            desc.format = depth ? rhi::TextureFormat::D32Float : rhi::TextureFormat::Rgba16Float;
            desc.usage = rhi::TextureUsage::Sampled;
            desc.initialState = rhi::ResourceState::ShaderRead;
            desc.mipLevelCount = mips;
            auto object = device.createTexture(desc);
            assert(object);
            const char* id = nullptr;
            auto parent =
                Texture::prepareOriginal(object, {1u, true, 2u, cpuBytes}, c.sourceQuota, id);
            assert(parent && !id);
            prefix[count++] = parent;
            rhi::TextureViewDesc view;
            view.format = desc.format;
            view.aspect = depth ? rhi::TextureAspect::Depth : rhi::TextureAspect::Color;
            view.usage = rhi::TextureUsage::Sampled;
            auto viewObject = device.createTextureView(*parent->originalObject(), view);
            assert(viewObject);
            auto lease =
                ViewLease::prepareOriginal(viewObject, {1u, true, 2u, cpuBytes}, c.sourceQuota, id);
            assert(lease && !id);
            prefix[count] = View::prepareOriginal(lease,
                parent,
                {1u, true, View::completeRetainedOwnerCount, cpuBytes},
                c.sourceQuota,
                id);
            assert(!id);
            return count++;
        };
        locations.color = texture(false, 32u, 1u);
        locations.depth = texture(true, 32u, 1u);
        const auto wrongExtent = texture(false, 31u, 1u);
        const auto wrongMipCount = texture(false, 32u, 2u);
        rhi::OriginalCommandAdmissionBudget admission;
        admission.contractVersion = 1u;
        admission.prototypeOptIn = true;
        admission.maxCommands = 1u;
        admission.maxGraphOwners = 256u;
        admission.maxCpuBytes.fill(cpuBytes);
        for (const auto domain : {rhi::CommandReferenceDomain::Buffer,
                 rhi::CommandReferenceDomain::Texture,
                 rhi::CommandReferenceDomain::TextureView})
            admission.maxSourceOwners[static_cast<std::size_t>(domain)] = 16u;
        admission.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Pipeline)] =
            3u;
        admission
            .maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Descriptor)] =
            3u;
        const char* id = nullptr;
        auto inventory = Sources::prepareColdOriginalTyped(
            prefix.data(), count, admission, {8u, true, 6u}, c.sourceQuota, id);
        assert(inventory && !id);
        {
            using Assembly = rhi::OriginalCommandSourceAssembly;
            std::shared_ptr<const Buffer> observed;
            Assembly::TextureViewObservation view;
            auto foreign = rhi::createNullDevice();
            const auto counterfeit =
                std::shared_ptr<const Sources>(inventory.get(), [](const auto*) {});
            const auto before = core::defaultCpuAllocator().stats().allocationCount;
            assert(!Assembly::validate(inventory, device));
            assert(!Assembly::inspectBuffer(inventory, device, locations.constants, observed));
            assert(core::SharedOwnerProjection::sameOwner(observed, pending.constants));
            assert(!Assembly::inspectTextureView(inventory, device, locations.color, view));
            const auto& expected = std::get<std::shared_ptr<const View>>(prefix[locations.color]);
            assert(
                core::SharedOwnerProjection::sameOwner(view.view, expected->originalViewLease()));
            assert(core::SharedOwnerProjection::sameOwner(
                view.parent, expected->originalParentLease()));
            assert(
                Assembly::inspectBuffer(inventory, device, locations.color, observed) && !observed);
            observed = pending.constants;
            assert(Assembly::inspectBuffer(inventory, device, SIZE_MAX, observed) && !observed);
            assert(Assembly::inspectTextureView(inventory, device, locations.constants, view)
                && !view.view && !view.parent);
            observed = pending.constants;
            assert(Assembly::inspectBuffer(counterfeit, device, locations.constants, observed)
                && !observed);
            observed = pending.constants;
            assert(Assembly::inspectBuffer(inventory, *foreign, locations.constants, observed)
                && !observed);
            c.sourceQuota->setLimitBytes(cpuBytes - 1u);
            observed = pending.constants;
            assert(Assembly::inspectBuffer(inventory, device, locations.constants, observed)
                && !observed);
            c.sourceQuota->setLimitBytes(cpuBytes);
            {
                core::CpuMemoryQuotaLease pressure(
                    c.sourceQuota, cpuBytes - c.sourceQuota->liveBytes());
                assert(pressure.charged());
                assert(!Assembly::inspectBuffer(inventory, device, locations.constants, observed));
            }
            std::uint32_t owners = 99u;
            assert(Assembly::cpuOwnerCount(counterfeit, device, 2u, owners) && !owners);
            assert(!Assembly::cpuOwnerCount(inventory, device, 2u, owners) && owners);
            assert(before == core::defaultCpuAllocator().stats().allocationCount);
        }
        CloudPreparedDescriptorDeclarations output;
        const auto describe = [&](const CloudPreparedDescriptorLocations& l) {
            return CloudPreparedDescriptors::describe({device, resources, inventory, l}, output);
        };
        require(describe(locations));
        assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{c.ownerQuota, c.initializationQuota, c.sourceQuota, c.historyQuota},
            [&]() noexcept {
                return describe(locations).diagnostic[0] ? "CLOUD-DESCRIPTOR-PREFLIGHT-REFUSED"
                                                         : nullptr;
            }));
        assert(output.historyGeneration == pending.historyGeneration);
        for (unsigned pass = 0u; pass < kCloudPipelineCount; ++pass) {
            const auto& p = output.passes[pass];
            assert(p.count == (spatial && pass == 1u ? 7u : 4u));
            for (unsigned i = 0u; i < p.count; ++i) {
                const auto& e = p.elements[i];
                assert(e.write.binding == i && e.registrationGeneration);
                assert(e.stages
                    == (pass == 2u ? rhi::PipelineStage::FragmentShader
                                   : rhi::PipelineStage::ComputeShader));
                if (e.write.resourceKind == rhi::DescriptorResourceKind::Buffer) {
                    assert(
                        e.write.bufferBindingRange.contractVersion == 2u && !e.bufferAccessOffset);
                    assert(e.write.bufferBindingRange.offsetBytes == 0u
                        && e.write.bufferBindingRange.sizeBytes == e.bufferAccessBytes);
                    const auto& lease =
                        std::get<std::shared_ptr<const Buffer>>(prefix[e.sourceIndex]);
                    assert(e.bufferAccessBytes == lease->originalObject()->desc().sizeBytes);
                    assert(e.write.resource == lease->resource());
                }
                assert(e.access
                    == (e.write.type == rhi::DescriptorType::StorageBuffer
                            ? rhi::ResourceAccess::ShaderRead | rhi::ResourceAccess::ShaderWrite
                            : rhi::ResourceAccess::ShaderRead));
            }
        }
        assert(output.passes[0].elements[2].write.resource
            == output.passes[2].elements[3].write.resource);
        assert(output.passes[1].elements[2].sourceIndex == locations.previous);
        if (spatial) {
            assert(output.passes[1].elements[4].write.resource
                == output.passes[0].elements[1].write.resource);
            assert(output.passes[1].elements[5].write.resource
                == output.passes[0].elements[2].write.resource);
            assert(output.passes[1].elements[6].bufferAccessBytes
                == sizeof(CloudReconstructionGpuConstants));
        } else
            assert(locations.previous == locations.raw);
        for (unsigned mutation = 0u; mutation < 10u; ++mutation) {
            auto invalid = locations;
            if (mutation == 0u)
                invalid.constants = SIZE_MAX;
            if (mutation == 1u)
                invalid.density = locations.raw;
            if (mutation == 2u)
                invalid.raw = locations.resolved;
            if (mutation == 3u)
                invalid.previous = locations.constants;
            if (mutation == 4u)
                invalid.resolved = locations.raw;
            if (mutation == 5u)
                invalid.reconstructionConstants = spatial ? SIZE_MAX : locations.constants;
            if (mutation == 6u)
                invalid.color = locations.depth;
            if (mutation == 7u)
                invalid.depth = locations.color;
            if (mutation == 8u)
                invalid.color = wrongExtent;
            if (mutation == 9u)
                invalid.color = wrongMipCount;
            assert(!describe(invalid).success && !output.historyGeneration);
            for (const auto& p : output.passes)
                assert(!p.count);
        }
        // Null has no accepted native pipeline here. Renderer preflight must
        // preserve both candidates instead of manufacturing successful sets.
        CloudPreparedPipelines pipelines;
        core::UniquePtr<rhi::DescriptorArena> arena;
        CloudPreparedDescriptorPublishContext publish{
            device, resources, pipelines, inventory, arena};
        publish.locations = locations;
        CloudPreparedDescriptorOutput published;
        published.historyGeneration = 99u;
        assert(!CloudPreparedDescriptors::publish(publish, published).success && inventory);
        assert(!published.historyGeneration && published.descriptors[0] == SIZE_MAX);
        publish.contractVersion = 1u;
        publish.prototypeOptIn = true;
        {
            auto alias = inventory;
            const auto failure = CloudPreparedDescriptors::publish(publish, published);
            assert(!failure.success
                && !std::strcmp(
                    failure.diagnostic, "CLOUD-PREPARED-DESCRIPTORS-EXCLUSIVE-CANDIDATE"));
            assert(inventory == alias);
        }
        const auto absentPipeline = CloudPreparedDescriptors::publish(publish, published);
        assert(!absentPipeline.success
            && !std::strcmp(
                absentPipeline.diagnostic, "CLOUD-PREPARED-DESCRIPTORS-PIPELINE-POSITION"));
        assert(inventory && !arena && !published.historyGeneration);
        require(resources.history()->cancelPendingFrame());
        assert(!describe(locations).success && !output.historyGeneration);
        require(resources.releaseIdle());
        assert(c.retainedLogicalBytes->used() == c.resources.config.gpuBudgetBytes);
    }
}

void publicSourceAssemblyPublication(bool appendCapacityRefusal = false)
{
    using Assembly = rhi::OriginalCommandSourceAssembly;
    using Sources = rhi::OriginalCommandResourceSources;
    auto d = rhi::createNullDevice(), foreign = rhi::createNullDevice();
    const auto lifetime = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
    const auto registry = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
    const auto quota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
    const auto objects = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
    assert(!d->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, cpuBytes}, lifetime));
    rhi::OriginalResourceRegistrationBudget registration{4u, true, 1u, 0u, 0u, 16u, cpuBytes};
    registration.maxDescriptorArenas = registration.maxDescriptorSets = 1u;
    assert(!d->prepareOriginalResourceRegistration(registration, registry));
    {
        core::CpuMemoryQuotaScope objectScope(objects);
        rhi::BufferDesc desc;
        desc.sizeBytes = 64u;
        desc.usage = rhi::ResourceUsage::StorageBuffer;
        desc.initialState = rhi::ResourceState::ShaderWrite;
        auto buffer = d->createBuffer(desc);
        const char* id = nullptr;
        auto lease = rhi::OriginalCommandBufferLease::prepareOriginal(
            buffer, {1u, true, 2u, cpuBytes}, quota, id);
        assert(lease && !id);
        rhi::OriginalCommandAdmissionBudget admission;
        admission.contractVersion = 1u;
        admission.prototypeOptIn = true;
        admission.maxCommands = 1u;
        admission.maxGraphOwners = 64u;
        admission.maxCpuBytes.fill(cpuBytes);
        admission.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Buffer)] =
            1u;
        admission.maxSourceOwners[static_cast<std::size_t>(appendCapacityRefusal
                ? rhi::CommandReferenceDomain::DescriptorTable
                : rhi::CommandReferenceDomain::Descriptor)] = 1u;
        const Sources::Source prefix{lease};
        auto inventory =
            Sources::prepareColdOriginalTyped(&prefix, 1u, admission, {1u, true, 1u}, quota, id);
        assert(inventory && !id);
        const auto* exact = inventory.get();
        rhi::DescriptorArenaDesc ad;
        ad.maxSets = 1u;
        ad.maxDescriptors = 1u;
        auto arena = core::shareUnique(d->createOriginalDescriptorArena(ad));
        assert(arena);
        rhi::DescriptorLayoutDesc layout;
        layout.name = "PublicSourceAssembly";
        layout.bindings.push_back(
            {0u, rhi::DescriptorType::StorageBuffer, rhi::ShaderStage::Compute});
        auto set = d->allocateOriginalDescriptorSet(arena, {layout.name, layout});
        assert(set);
        rhi::OriginalCommandDescriptorSetElement element;
        element.sourceIndex = 0u;
        element.registrationGeneration =
            lease->originalObject()->originalResourceRegistration().generation();
        element.bufferAccessBytes = 64u;
        element.stages = rhi::PipelineStage::ComputeShader;
        element.access = rhi::ResourceAccess::ShaderWrite;
        element.write.type = rhi::DescriptorType::StorageBuffer;
        element.write.resource = lease->resource();
        element.write.resourceKind = rhi::DescriptorResourceKind::Buffer;
        element.write.resourceState = rhi::ResourceState::ShaderWrite;
        assert(set->write(element.write).success);
        const auto* originalSet = set.get();
        const rhi::OriginalCommandDescriptorSourceBudget budget{2u, true, 1u, 1u, 5u, cpuBytes};
        std::size_t index = 99u;
        const auto append = [&](const auto& candidate, const auto& b, const auto& device) {
            index = 99u;
            return Assembly::appendDescriptorSet(
                set, arena, candidate, &element, 1u, b, device, index);
        };
        const auto refused = [&](const char* diagnostic) {
            if (!diagnostic)
                std::fprintf(stderr, "public Source negative unexpectedly accepted\n");
            assert(diagnostic && index == SIZE_MAX && set.get() == originalSet);
            assert(inventory.get() == exact && Assembly::size(*inventory) == 1u);
        };
        auto invalid = budget;
        invalid.contractVersion = 0u;
        refused(append(inventory, invalid, *d));
        const auto counterfeit = std::shared_ptr<Sources>(inventory.get(), [](const auto*) {});
        refused(append(counterfeit, budget, *d));
        refused(append(inventory, budget, *foreign));
        quota->setLimitBytes(1u);
        refused(append(inventory, budget, *d));
        quota->setLimitBytes(cpuBytes);
        {
            core::CpuMemoryQuotaLease pressure(quota, cpuBytes - quota->liveBytes());
            assert(pressure.charged());
            refused(append(inventory, budget, *d));
        }
        const auto* accepted = append(inventory, budget, *d);
        if (appendCapacityRefusal) {
            // Preparation adopted the set; canonical append then refuses its
            // unavailable domain. No partial Source/index escapes, and the
            // temporary Source retires the adopted set under the same owners.
            assert(accepted && !std::strcmp(accepted, "RHI-ORIGINAL-SOURCES-DOMAIN-CAPACITY"));
            assert(!set && index == SIZE_MAX && inventory.get() == exact);
            assert(Assembly::size(*inventory) == 1u && !Assembly::validate(inventory, *d));
        } else {
            if (accepted)
                std::fprintf(stderr, "public Source publication: %s\n", accepted);
            assert(!accepted && !set && index == 1u && inventory.get() == exact);
            assert(Assembly::size(*inventory) == 2u && !Assembly::validate(inventory, *d));
            const auto& source =
                std::get<std::shared_ptr<const rhi::OriginalCommandDescriptorSetSource>>(
                    inventory->sources()[index]);
            assert(source->originalObject() == originalSet && !source->nativeExecutionProven());
        }
    }
    assert(!quota->liveBytes() && !objects->liveBytes());
    std::printf(
        "PUBLIC_SOURCE_ASSEMBLY_PUBLICATION_PASS appendCapacityRefusal=%d sameInventory=true sameUniqueSet=true preflightPreservesCandidates=true quotaPressure=true finalOwnersDrained=true nativeAuthority=false\n",
        appendCapacityRefusal);
}

void reconstructionContract(rhi::GpuDevice& device)
{
    CloudReconstructionConfig config{1u, .25f, .25f};
    std::array<CloudGpuSample, CloudReconstruction::footprintSamples> samples;
    for (auto& sample : samples)
        sample = {{0, 0, 0, 1}, {10, 10, 0, 0}};
    bool refine = true;
    require(CloudReconstruction::classify(config, samples, 10, 1, refine));
    assert(!refine);
    samples.back().radianceTransmittance.x = .25f;
    require(CloudReconstruction::classify(config, samples, 10, 1, refine));
    assert(!refine); // Equality is the declared non-refinement boundary.
    samples.back().radianceTransmittance.x = .5f;
    require(CloudReconstruction::classify(config, samples, 10, 1, refine));
    assert(refine);
    samples.back().radianceTransmittance = {0, 0, 0, .5f};
    require(CloudReconstruction::classify(config, samples, 10, 1, refine));
    assert(refine);
    samples.back().radianceTransmittance.w = 1;
    samples.back().metadata.y = 12;
    require(CloudReconstruction::classify(config, samples, 10, 1, refine));
    assert(refine);
    samples.back().metadata.y = 10;
    assert(!CloudReconstruction::classify(config, {samples.data(), 15u}, 10, 1, refine).success);
    assert(!refine);
    assert(!CloudReconstruction::classify(config, samples, 10, 0, refine).success);
    for (unsigned mutation = 0; mutation < 5; ++mutation) {
        auto invalid = config;
        if (mutation == 0)
            invalid.contractVersion = 0;
        if (mutation == 1)
            invalid.contractVersion = 2;
        if (mutation == 2)
            invalid.maxRadianceRange = 0;
        if (mutation == 3)
            invalid.maxTransmittanceRange = 1.1f;
        if (mutation == 4)
            invalid.maxTransmittanceRange = std::numeric_limits<float>::quiet_NaN();
        assert(!CloudReconstruction::classify(invalid, samples, 10, 1, refine).success && !refine);
    }
    for (const auto value : {-1.f, 2.f, std::numeric_limits<float>::quiet_NaN()}) {
        samples.back().radianceTransmittance.w = value;
        assert(!CloudReconstruction::classify(config, samples, 10, 1, refine).success && !refine);
    }
    samples.back().radianceTransmittance.w = 1;
    for (const auto value : {-1.f, std::numeric_limits<float>::quiet_NaN()}) {
        samples.back().metadata.x = value;
        assert(!CloudReconstruction::classify(config, samples, 10, 1, refine).success && !refine);
    }
    const auto asset = tests::cloud::asset();
    auto render = tests::cloud::config();
    require(CloudQuality::apply(1u, CloudQualityTier::Medium, render));
    CloudReconstructionResources plan;
    assert(!CloudReconstruction::describeResources(config,
        render,
        {1280, 720},
        asset.cells.size() * sizeof(core::Vec4),
        sizeof(CloudGpuConstants),
        plan)
            .success);
    assert(!plan.resolvedCount); // The old 1e9 work budget cannot fund refinement.
    render.maxDensityEvaluations = UINT64_MAX;
    require(CloudReconstruction::describeResources(config,
        render,
        {1280, 720},
        asset.cells.size() * sizeof(core::Vec4),
        sizeof(CloudGpuConstants),
        plan));
    assert(plan.raw.sampleExtent.width == 640u && plan.raw.sampleExtent.height == 360u);
    assert(plan.raw.dispatchGroups == (rhi::Extent3D{80u, 45u, 1u}));
    assert(plan.dispatchGroups == (rhi::Extent3D{160u, 90u, 1u}));
    assert(plan.densityEvaluationBound == 2506752000ull);
    const auto accepted = plan;
    auto caps = device.capabilities();
    for (const auto backend :
        {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
        auto declared = caps;
        declared.backend = backend;
        require(CloudReconstruction::plan(config,
            render,
            {1280, 720},
            asset.cells.size() * sizeof(core::Vec4),
            sizeof(CloudGpuConstants),
            declared,
            plan));
        // Numeric admission is independent of identity and grants no payload.
        declared.computeResources.sampledDepthTexture = false;
        assert(!CloudReconstruction::plan(config,
            render,
            {1280, 720},
            asset.cells.size() * sizeof(core::Vec4),
            sizeof(CloudGpuConstants),
            declared,
            plan)
                .success);
        assert(!plan.resolvedCount && !plan.raw.sampleCount);
        const auto publication =
            rhi::OriginalGeneralDescriptorWriteContract::synchronousComputeLowering(backend);
        assert(!publication.executionAvailable);
        assert((backend == rhi::Backend::Vulkan || backend == rhi::Backend::Metal)
            == (publication.diagnostic == nullptr));
        assert(publication.validatorOnly == (backend == rhi::Backend::Null));
    }
    for (unsigned mutation = 0; mutation < 5; ++mutation) {
        auto limited = caps;
        if (mutation == 0)
            limited.computeResources.maxStorageBufferBindingsPerStage = 3u;
        if (mutation == 1)
            limited.computeResources.maxUniformBufferBindingsPerStage = 1u;
        if (mutation == 2)
            limited.computeResources.maxStorageBufferBindingSizeBytes = 7372800u;
        if (mutation == 3)
            limited.computeResources.maxComputeWorkgroupCountX = 80u;
        if (mutation == 4)
            limited.computeResources.maxComputeWorkgroupCountY = 45u;
        const auto result = CloudReconstruction::plan(config,
            render,
            {1280, 720},
            asset.cells.size() * sizeof(core::Vec4),
            sizeof(CloudGpuConstants),
            limited,
            plan);
        assert(!result.success && !plan.resolvedCount && !plan.raw.sampleCount);
    }
    assert(accepted.resolvedCount == 1280u * 720u
        && accepted.resolvedBufferBytes == accepted.resolvedCount * 32u);
    render.maxDensityEvaluations = accepted.densityEvaluationBound - 1u;
    assert(!CloudReconstruction::describeResources(config,
        render,
        {1280, 720},
        asset.cells.size() * sizeof(core::Vec4),
        sizeof(CloudGpuConstants),
        plan)
            .success);
    assert(!plan.resolvedCount);
    render.maxDensityEvaluations = accepted.densityEvaluationBound;
    render.gpuBudgetBytes = accepted.retainedGpuBytes - 1u;
    assert(!CloudReconstruction::describeResources(config,
        render,
        {1280, 720},
        asset.cells.size() * sizeof(core::Vec4),
        sizeof(CloudGpuConstants),
        plan)
            .success);
    render.gpuBudgetBytes = accepted.retainedGpuBytes;
    require(CloudReconstruction::describeResources(config,
        render,
        {1280, 720},
        asset.cells.size() * sizeof(core::Vec4),
        sizeof(CloudGpuConstants),
        plan));
    render.resolutionDivisor = 1u;
    assert(!CloudReconstruction::describeResources(config,
        render,
        {64, 48},
        asset.cells.size() * sizeof(core::Vec4),
        sizeof(CloudGpuConstants),
        plan)
            .success);
}

// Isolate integration from spatial reconstruction in the rejected Medium
// fixture. All samples here are CPU references; no Null/GPU execution claim.
void qualityIntegrationProbe(rhi::GpuDevice& device, bool reconstruction, unsigned cameraCase = 0u)
{
    const auto asset = tests::cloud::asset();
    tests::cloud::QualityError fullL, fullT, halfL, halfT;
    tests::cloud::QualityError reconstructedL, reconstructedT;
    core::pmr::Vector<CloudGpuSample> fullLow, fullFine;
    unsigned refined = 0, eligible = 0;
    const CloudReconstructionConfig policy{1u,
        float(tests::cloud::QualityOracle::maxLimit / 2),
        float(tests::cloud::QualityOracle::maxLimit / 2)};
    for (const auto divisor : {1u, 2u}) {
        tests::cloud::SequenceOracle low, fine;
        auto config = tests::cloud::config();
        require(CloudQuality::apply(1u, CloudQualityTier::Medium, config));
        config.resolutionDivisor = divisor;
        require(CloudExecutionPlanner::describeResources(config,
            {64u, 48u},
            asset.cells.size() * sizeof(core::Vec4),
            sizeof(CloudGpuConstants),
            low.plan));
        render_graph::FrameGraph graph(device);
        auto c = frame(graph, 0);
        c.cameraPosition.y = cameraCase == 1u ? 1600.f : cameraCase == 2u ? 4000.f : 10.f;
        c.view = core::lookAt(c.cameraPosition,
            c.cameraPosition + core::Vec3{.25f, cameraCase == 2u ? -.6f : .6f, 1.f});
        c.layer.windVelocity = {};
        CloudFrameState state;
        require(CloudFrameParameters::build(
            c, device.backend(), config, low.plan, asset.source, 0u, nullptr, state));
        low.parameters = state.parameters;
        low.layer = c.layer;
        low.foreground = {true, 0, 0, 48, 48};
        low.build(asset);
        fine.plan = low.plan;
        fine.parameters = low.parameters;
        fine.parameters.sampling[0] = 512u;
        fine.parameters.sampling[1] = 64u;
        fine.parameters.jitter.z = .5f;
        fine.layer = low.layer;
        fine.foreground = low.foreground;
        fine.build(asset);
        auto& l = divisor == 1u ? fullL : halfL;
        auto& t = divisor == 1u ? fullT : halfT;
        for (std::size_t i = 0; i < low.raw.size(); ++i) {
            const auto a =
                tests::cloud::SequenceOracle::components(low.raw[i].radianceTransmittance);
            const auto b =
                tests::cloud::SequenceOracle::components(fine.raw[i].radianceTransmittance);
            for (unsigned channel = 0; channel < 3; ++channel)
                l.add(a[channel], b[channel]);
            t.add(a[3], b[3]);
        }
        if (reconstruction && divisor == 1u) {
            fullLow = low.raw;
            fullFine = fine.raw;
        }
        if (reconstruction && divisor == 2u) {
            CloudReconstructionResources resources;
            require(CloudReconstruction::describeResources(policy,
                config,
                {64, 48},
                asset.cells.size() * sizeof(core::Vec4),
                sizeof(CloudGpuConstants),
                resources));
            assert(resources.resolvedCount == fullLow.size());
            for (unsigned y = 0; y < 48; ++y)
                for (unsigned x = 0; x < 64; ++x) {
                    const auto index = y * 64u + x;
                    const float u = (x + .5f) / 64, v = (y + .5f) / 48;
                    auto ray = low.ray(u, v);
                    const auto opaque = low.opaqueDistance(u, v);
                    ray.maxDistance = std::min(ray.maxDistance, double(opaque));
                    CloudRayIntervals spans;
                    require(CloudContract::intersect(low.layer, ray, spans));
                    bool needsRefinement = false;
                    if (spans.count) {
                        ++eligible;
                        const int bx = int(std::floor(u * 32 - .5f));
                        const int by = int(std::floor(v * 24 - .5f));
                        std::array<CloudGpuSample, CloudReconstruction::footprintSamples> halo;
                        unsigned i = 0;
                        for (int dy = -1; dy <= 2; ++dy)
                            for (int dx = -1; dx <= 2; ++dx)
                                halo[i++] = low.raw[std::clamp(by + dy, 0, 23) * 32
                                    + std::clamp(bx + dx, 0, 31)];
                        require(CloudReconstruction::classify(
                            policy, halo, opaque, low.parameters.temporal.y, needsRefinement));
                    }
                    refined += needsRefinement;
                    const auto value = needsRefinement ? tests::cloud::SequenceOracle::components(
                                                             fullLow[index].radianceTransmittance)
                                                       : low.integral(x, y);
                    const auto target = tests::cloud::SequenceOracle::components(
                        fullFine[index].radianceTransmittance);
                    for (unsigned channel = 0; channel < 3; ++channel)
                        reconstructedL.add(value[channel], target[channel]);
                    reconstructedT.add(value[3], target[3]);
                }
        }
    }
    const auto write = [&](std::FILE* output) {
        std::fprintf(output,
            "{\"scope\":\"CPU integration/reconstruction diagnostic only; no GPU, temporal, performance or platform acceptance\",\"cameraCase\":%u,\"extent\":[64,48],\"steps\":[64,8],\"fineSteps\":[512,64],\"fullL\":",
            cameraCase);
        fullL.write(output);
        std::fprintf(output, ",\"fullT\":");
        fullT.write(output);
        std::fprintf(output, ",\"halfSampleL\":");
        halfL.write(output);
        std::fprintf(output, ",\"halfSampleT\":");
        halfT.write(output);
        if (reconstruction) {
            std::fprintf(output,
                ",\"candidate\":{\"contractVersion\":1,\"maxRadianceRange\":%.9g,\"maxTransmittanceRange\":%.9g,\"eligiblePixels\":%u,\"refinedPixels\":%u,\"L\":",
                double(policy.maxRadianceRange),
                double(policy.maxTransmittanceRange),
                eligible,
                refined);
            reconstructedL.write(output);
            std::fprintf(output, ",\"T\":");
            reconstructedT.write(output);
            std::fprintf(output, "}");
        }
        std::fprintf(output, "}\n");
    };
    write(stdout);
    if (const auto* path = std::getenv("ARP_CLOUD_QUALITY_PROBE_PATH")) {
        auto* output = std::fopen(path, "wb");
        assert(output);
        write(output);
        assert(std::fclose(output) == 0);
    }
    for (const auto* error : {&fullL, &fullT, &halfL, &halfT})
        assert(error->within(
            tests::cloud::QualityOracle::rmsLimit, tests::cloud::QualityOracle::maxLimit));
    if (reconstruction) {
        assert(refined <= eligible);
        if (cameraCase == 0u)
            assert(refined && refined < eligible);
        for (const auto* error : {&reconstructedL, &reconstructedT})
            assert(error->within(
                tests::cloud::QualityOracle::rmsLimit, tests::cloud::QualityOracle::maxLimit));
    }
}

void referenceSampling()
{
    auto asset = tests::cloud::asset();
    const auto shape = assets::cloud::kCloudHeightProfileCount
        + asset.source.weatherDimension * asset.source.weatherDimension;
    const auto detail = shape
        + asset.source.shapeDimension * asset.source.shapeDimension * asset.source.shapeDimension;
    for (unsigned i = 0; i < assets::cloud::kCloudHeightProfileCount; ++i)
        asset.source.heightProfiles[i] = asset.cells[i] = {0, .001f, .999f, 1};
    for (unsigned i = assets::cloud::kCloudHeightProfileCount; i < shape; ++i)
        asset.cells[i] = {1, 0, 1, 0};
    for (unsigned i = shape; i < detail; ++i)
        asset.cells[i] = {1, 0, 0, 0};
    for (auto i = detail; i < asset.cells.size(); ++i)
        asset.cells[i] = {};
    const auto layer = tests::cloud::layer();
    CloudReference reference;
    require(reference.configure(asset, layer));
    const CloudRay ray{{0, 1100, 0}, {0, 1, 0}, 100};
    for (const auto phase : {0.f, .25f, .5f, .75f}) {
        renderer::volume::MediumIntegral integral;
        float moment = 0;
        require(reference.integrate(
            ray, 4u, 1u, {0, 1, 0}, {}, {1, 1, 1}, 0, integral, phase, &moment));
        const auto expectedT = std::exp(-double(layer.extinction) * 100);
        assert(std::fabs(integral.transmittance - expectedT) < 1.e-6);
        assert(std::fabs(integral.radiance.x - (1 - expectedT)) < 1.e-6);
        double weighted = 0;
        const auto segmentT = std::exp(-double(layer.extinction) * 25);
        for (unsigned i = 0; i < 4; ++i)
            weighted += std::pow(segmentT, i) * (1 - segmentT) * (i + phase) * 25;
        assert(std::fabs(moment - weighted / (1 - expectedT)) < .001);
    }
    // The quadrature moment must remain normalized when almost all light is
    // transmitted. A separately rounded product cannot be its denominator.
    for (const auto extinction : {1.e-5f, 1.e-6f, 1.e-7f}) {
        auto thin = layer;
        thin.extinction = extinction;
        require(reference.configure(asset, thin));
        for (const auto phase : {.2f, .4f, .5f, .8f}) {
            renderer::volume::MediumIntegral value;
            float distance = 0.f;
            require(reference.integrate(
                ray, 24u, 1u, {0, 1, 0}, {}, {1, 1, 1}, 0.f, value, phase, &distance));
            const double step = 100.0 / 24.0;
            const double segmentT = std::exp(-double(extinction) * step);
            const double absorption = -std::expm1(-double(extinction) * step);
            double total = 0.0, moment = 0.0;
            for (unsigned i = 0u; i < 24u; ++i) {
                const auto weight = std::pow(segmentT, i) * absorption;
                total += weight;
                moment += weight * (i + phase) * step;
            }
            assert(value.transmittance < 1.f && total > 0.0);
            assert(std::fabs(distance - moment / total) < .001);
        }
    }
    renderer::volume::MediumIntegral integral;
    assert(!reference.integrate(ray, 4u, 1u, {0, 1, 0}, {}, {1, 1, 1}, 0, integral, 1.f).success);
    assert(!reference.integrate(ray, 4u, 1u, {0, 1, 0}, {}, {1, 1, 1}, 0, integral, -.1f).success);
}

void occlusionReference(rhi::GpuDevice& device)
{
    const auto asset = tests::cloud::asset();
    for (const auto orthographic : {false, true})
        for (const auto divisor : {1u, 2u}) {
            tests::cloud::SequenceOracle oracle;
            auto config = tests::cloud::config();
            config.resolutionDivisor = divisor;
            require(CloudExecutionPlanner::describeResources(config,
                {32u, 24u},
                asset.cells.size() * sizeof(core::Vec4),
                sizeof(CloudGpuConstants),
                oracle.plan));
            render_graph::FrameGraph graph(device);
            auto c = frame(graph, 0.f);
            c.cameraPosition.y = orthographic ? 300.f : 10.f;
            c.view = core::lookAt(c.cameraPosition, c.cameraPosition + core::Vec3{.25f, .6f, 1.f});
            c.orthographic = orthographic;
            if (orthographic)
                c.projection = core::orthographic(100.f, 75.f, 1.f, 800.f);
            CloudFrameState state, next;
            for (unsigned i = 0; i < 2; ++i) {
                c.time = float(i) / 60.f;
                require(CloudFrameParameters::build(c,
                    device.backend(),
                    config,
                    oracle.plan,
                    asset.source,
                    i,
                    i ? &state : nullptr,
                    next));
                state = next;
                oracle.parameters = state.parameters;
                oracle.layer = c.layer;
                oracle.foreground = {true, float(i) * 8.f, 0.f, 24.f, 24.f};
                oracle.build(asset);
                unsigned covered = 0, cloudySky = 0;
                for (unsigned y = 0; y < 24; ++y)
                    for (unsigned x = 0; x < 32; ++x) {
                        const auto color = oracle.hdr(x, y);
                        if (oracle.foreground.covers(x, y)) {
                            ++covered;
                            assert(color == tests::cloud::ForegroundFixture::color);
                            if (orthographic)
                                assert(
                                    std::fabs(oracle.opaqueDistance((x + .5f) / 32, (y + .5f) / 24)
                                        - 399.5f)
                                    < .001f);
                        } else
                            cloudySky +=
                                std::fabs(color[0] - tests::cloud::ForegroundFixture::background[0])
                                > 1.e-5f;
                    }
                assert(covered == 162u && cloudySky > 0);
                for (const auto& sample : oracle.raw)
                    if (sample.metadata.y < config.maxDistance) {
                        assert(sample.radianceTransmittance.w == 1.f);
                        assert(sample.metadata.x == config.maxDistance);
                    }
                if (i)
                    assert(oracle.accepted > 0 && oracle.opaqueRejected > 0
                        && oracle.disoccluded > 0 && oracle.newlyOccluded > 0);
            }
        }
}

void spatialMotionReference(rhi::GpuDevice& device)
{
    const auto asset = tests::cloud::asset();
    for (const bool orthographic : {false, true}) {
        tests::cloud::SequenceOracle oracle;
        oracle.reconstruction = {1u, .025f, .025f};
        auto config = tests::cloud::config();
        require(CloudQuality::apply(1u, CloudQualityTier::Medium, config));
        CloudFrameState previous;
        for (unsigned i = 0; i < 11; ++i) {
            const rhi::Extent2D extent = i < 6 ? rhi::Extent2D{64u, 48u} : rhi::Extent2D{63u, 47u};
            require(CloudExecutionPlanner::describeResources(config,
                extent,
                asset.cells.size() * sizeof(core::Vec4),
                sizeof(CloudGpuConstants),
                oracle.plan));
            render_graph::FrameGraph graph(device);
            auto c = frame(graph, float(i) / 60.f);
            c.layer.windVelocity = {};
            c.cameraPosition.x += i >= 2 ? 64.f : 0.f;
            c.cameraPosition.y = orthographic ? 300.f : 10.f;
            c.view = core::lookAt(c.cameraPosition, c.cameraPosition + core::Vec3{.25f, .6f, 1.f});
            c.orthographic = orthographic;
            c.projection = orthographic
                ? core::orthographic(i >= 5 ? 110.f : 100.f, 75.f, 1.f, 800.f)
                : core::perspective(
                      i >= 5 ? 60.f : 57.f, float(extent.width) / extent.height, 1.f, 20000.f);
            c.cameraCut = i == 3 || i == 9;
            c.sceneRevision = i >= 4 ? 1u : 0u;
            if (i >= 8)
                c.ambientRadiance.x += .025f;
            CloudFrameState next;
            // Match the successful-submission phase counter: resource resize
            // and explicit reset restart it; camera cuts alone do not.
            const auto samplingFrame = i < 6 ? i : i < 9 ? i - 6 : i - 9;
            require(CloudFrameParameters::build(c,
                device.backend(),
                config,
                oracle.plan,
                asset.source,
                samplingFrame,
                i ? &previous : nullptr,
                next));
            previous = next;
            oracle.parameters = next.parameters;
            oracle.layer = c.layer;
            oracle.foreground = {true, float(i % 2u) * 16.f, 0.f, 48.f, 48.f};
            oracle.build(asset);
            float difference = 0;
            for (std::size_t pixel = 0; pixel < oracle.resolved.size(); ++pixel) {
                const auto a = tests::cloud::SequenceOracle::components(
                    oracle.resolved[pixel].radianceTransmittance);
                const auto b = tests::cloud::SequenceOracle::components(
                    oracle.current[pixel].radianceTransmittance);
                for (unsigned channel = 0; channel < a.size(); ++channel)
                    difference = std::max(difference, std::fabs(a[channel] - b[channel]));
            }
            const bool expected = i == 1 || i == 2 || i == 7 || i == 10;
            std::printf(
                "SPATIAL_MOTION_CPU orthographic=%u frame=%u history=%u accepted=%u refined=%u eligible=%u difference=%.9g\n",
                unsigned(orthographic),
                i,
                unsigned(next.historyUsed),
                oracle.accepted,
                oracle.refined,
                oracle.eligible,
                double(difference));
            std::fflush(stdout);
            assert(next.historyUsed == expected);
            if (expected)
                assert(oracle.accepted > 0 && difference > 1.e-6f);
        }
    }
}

void qualityDepthReference(rhi::GpuDevice& device)
{
    const auto asset = tests::cloud::asset();
    auto config = tests::cloud::config();
    require(CloudQuality::apply(1u, CloudQualityTier::Reference, config));
    tests::cloud::SequenceOracle sequence;
    require(CloudExecutionPlanner::describeResources(config,
        {8u, 6u},
        asset.cells.size() * sizeof(core::Vec4),
        sizeof(CloudGpuConstants),
        sequence.plan));
    render_graph::FrameGraph graph(device);
    auto context = frame(graph, 0);
    context.layer.windVelocity = {};
    context.cameraPosition.y = 1600;
    context.view =
        core::lookAt(context.cameraPosition, context.cameraPosition + core::Vec3{.25f, .6f, 1.f});
    // Ambient-only lighting makes this a fast CPU oracle regression; this
    // grants no GPU/backend evidence. Foreground depth is inside the medium.
    context.sunRadiance = {};
    CloudFrameState state;
    require(CloudFrameParameters::build(
        context, device.backend(), config, sequence.plan, asset.source, 0, nullptr, state));
    sequence.parameters = state.parameters;
    sequence.layer = context.layer;
    sequence.foreground = {true, 0, 0, 8, 6};
    sequence.build(asset);
    tests::cloud::QualityOracle quality;
    quality.prepare(sequence, asset);
    assert(quality.coveredCloudPixels > 0);
    core::pmr::Vector<std::uint16_t> hdr(8u * 6u * 4u);
    unsigned attenuated = 0;
    for (unsigned y = 0; y < 6; ++y)
        for (unsigned x = 0; x < 8; ++x) {
            const auto color = sequence.hdr(x, y);
            for (unsigned c = 0; c < 4; ++c)
                hdr[(y * 8 + x) * 4 + c] = core::floatToHalf(color[c]);
            if (sequence.foreground.covers(x, y))
                attenuated += sequence.integral(x, y)[3] < 1;
        }
    assert(attenuated > 0);
    quality.evaluate(sequence,
        sequence.raw.data(),
        sequence.resolved.data(),
        reinterpret_cast<const std::byte*>(hdr.data()),
        8u * 8u);
    assert(quality.passed());
}

void reconstructionHistory(rhi::GpuDevice& device)
{
    const auto asset = tests::cloud::asset();
    auto config = tests::cloud::config();
    require(CloudQuality::apply(1u, CloudQualityTier::Medium, config));
    const CloudReconstructionConfig policy{1u, .025f, .025f};
    CloudReconstructionResources plan;
    require(CloudReconstruction::describeResources(policy,
        config,
        {32u, 24u},
        asset.cells.size() * sizeof(core::Vec4),
        sizeof(CloudGpuConstants),
        plan));
    const auto quota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
    std::array<CloudPreparedHistorySlot, 3> slots;
    for (auto& slot : slots)
        for (auto* destination : {&slot.raw, &slot.resolved}) {
            rhi::BufferDesc d;
            d.sizeBytes =
                destination == &slot.raw ? plan.raw.sampleBufferBytes : plan.resolvedBufferBytes;
            d.usage = rhi::ResourceUsage::StorageBuffer;
            d.memory = rhi::MemoryUsage::CpuToGpu;
            d.initialState = rhi::ResourceState::ShaderWrite;
            auto object = device.createBuffer(d);
            assert(object);
            core::pmr::Vector<std::byte> initialized(d.sizeBytes);
            assert(device.writeBuffer(*object, initialized.data(), initialized.size()).success);
            object->sealHostWrites();
            const char* id = nullptr;
            *destination = rhi::OriginalCommandBufferLease::prepareOriginal(
                object, {1u, true, 2u, cpuBytes}, quota, id);
            assert(*destination && !id);
        }
    CloudPreparedHistory history;
    const auto capacityBudget = core::makeShared<core::AtomicCapacityBudget>(1u);
    const auto releasedCapacity = core::makeShared<core::AtomicCapacityLease>(capacityBudget, 1u);
    releasedCapacity->release();
    assert(!history
            .create(device,
                1u,
                config,
                plan.raw,
                asset.source,
                slots,
                quota,
                &policy,
                {releasedCapacity, {}})
            .success);
    const auto shortGenerations =
        core::makeShared<core::AtomicCapacityBudget>(config.framesInFlight - 1u);
    assert(!history
            .create(device,
                1u,
                config,
                plan.raw,
                asset.source,
                slots,
                quota,
                &policy,
                {},
                shortGenerations)
            .success);
    auto undersized = slots;
    undersized[0].resolved = slots[0].raw;
    assert(!history.create(device, 1u, config, plan.raw, asset.source, undersized, quota, &policy)
            .success);
    require(history.create(device, 1u, config, plan.raw, asset.source, slots, quota, &policy));
    assert(history.densityBytes() == asset.cells.size() * sizeof(core::Vec4));
    render_graph::FrameGraph graph(device);
    auto c = frame(graph, 0.f);
    require(history.prepareFrame(c));
    assert(!history.pendingFrame().historyUsed && history.previous() != history.raw());
    assert(history.previous()->originalObject()->desc().sizeBytes == plan.resolvedBufferBytes);
    const auto firstPolicy = history.reconstructionConstants();
    assert(firstPolicy && firstPolicy->originalObject()->hostWritesSealed()
        && firstPolicy->originalObject()->gpuWritesSealed());
    assert(
        firstPolicy->originalObject()->desc().sizeBytes == sizeof(CloudReconstructionGpuConstants));
    const auto oldToken = history.token();
    require(history.cancelPendingFrame());
    assert(!history.reconstructionConstants() && history.validateSubmission(oldToken));
    require(history.prepareFrame(c));
    assert(history.reconstructionConstants()->resource() != firstPolicy->resource());
    auto first = core::shareUnique(device.createFence());
    auto second = core::shareUnique(device.createFence());
    const auto output = history.resolved();
    history.onSubmitted(history.token(), first);
    require(history.prepareFrame(c));
    assert(history.pendingFrame().historyUsed && history.previous() == output);
    history.onSubmitted(history.token(), second);
    assert(!history.frameReady() && !history.releaseIdle().success);
    first->signal();
    assert(history.frameReady()); // First frame's cold spare is no longer referenced.
    require(history.prepareFrame(c));
    assert(history.raw() == slots[1].raw);
    require(history.cancelPendingFrame());
    second->signal();
    require(history.releaseIdle());
    assert(!history.reconstructionConstants());
}

void parameters(rhi::GpuDevice& device)
{
    const auto asset = tests::cloud::asset();
    auto config = tests::cloud::config();
    CloudExecutionPlan plan;
    require(CloudExecutionPlanner::describeResources(config,
        {32u, 24u},
        asset.cells.size() * sizeof(core::Vec4),
        sizeof(CloudGpuConstants),
        plan));
    render_graph::FrameGraph graph(device);
    auto c = frame(graph, 0.f);
    CloudFrameState first, next;
    require(CloudFrameParameters::build(
        c, device.backend(), config, plan, asset.source, 0u, nullptr, first));
    assert(!first.historyUsed && first.parameters.previousCamera.w == 0.f);
    c.time = 1.f / 60.f;
    c.cameraPosition.x += 1.f;
    c.view = core::lookAt(c.cameraPosition, c.cameraPosition + core::Vec3{.25f, .6f, 1.f});
    require(CloudFrameParameters::build(
        c, device.backend(), config, plan, asset.source, 1u, &first, next));
    assert(next.historyUsed && next.parameters.previousCamera.w == 1.f);
    assert(next.parameters.previousViewProjection.m == first.viewProjection.m);
    assert(next.parameters.temporal.w == first.parameters.cameraTime.w);
    assert(next.parameters.jitter.z != first.parameters.jitter.z);
    // Explicit invalidations must not retain a stale history matrix/flag.
    for (unsigned mutation = 0u; mutation < 8u; ++mutation) {
        auto changed = c;
        if (mutation == 0u)
            changed.cameraCut = true;
        if (mutation == 1u)
            changed.cameraId = 2u;
        if (mutation == 2u)
            changed.sceneRevision = 1u;
        if (mutation == 3u)
            changed.time = -1.f;
        if (mutation == 4u)
            changed.projection.m[0] *= 1.1f;
        if (mutation == 5u)
            changed.layer.windVelocity.x += 1.f;
        if (mutation == 6u)
            changed.sunRadiance.x += 1.f;
        if (mutation == 7u)
            changed.layer.coverage *= .8f;
        require(CloudFrameParameters::build(
            changed, device.backend(), config, plan, asset.source, 1u, &first, next));
        assert(!next.historyUsed && next.parameters.previousCamera.w == 0.f);
        assert(next.parameters.previousViewProjection.m == next.viewProjection.m);
    }
    config.resolutionDivisor = 2u;
    require(CloudExecutionPlanner::describeResources(config,
        {31u, 23u},
        asset.cells.size() * sizeof(core::Vec4),
        sizeof(CloudGpuConstants),
        plan));
    require(CloudFrameParameters::build(
        c, device.backend(), config, plan, asset.source, 1u, &first, next));
    assert(!next.historyUsed && next.parameters.dimensions[0] == 16u
        && next.parameters.dimensions[1] == 12u);
    assert(!CloudFrameParameters::build(
        c, device.backend(), config, plan, asset.source, 1u, &first, first)
            .success);
}

void lifecycle(rhi::GpuDevice& device)
{
    const auto quota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
    const auto asset = tests::cloud::asset();
    const auto config = tests::cloud::config();
    CloudExecutionPlan plan;
    require(CloudExecutionPlanner::describeResources(config,
        {32u, 24u},
        asset.cells.size() * sizeof(core::Vec4),
        sizeof(CloudGpuConstants),
        plan));
    std::array<CloudPreparedHistorySlot, 3u> slots;
    for (auto& slot : slots)
        for (auto* destination : {&slot.raw, &slot.resolved}) {
            rhi::BufferDesc d;
            d.sizeBytes = plan.sampleBufferBytes;
            d.usage = rhi::ResourceUsage::StorageBuffer;
            d.memory = rhi::MemoryUsage::CpuToGpu;
            d.initialState = rhi::ResourceState::ShaderWrite;
            auto object = device.createBuffer(d);
            assert(object);
            core::pmr::Vector<std::byte> initialized(d.sizeBytes);
            assert(device.writeBuffer(*object, initialized.data(), initialized.size()).success);
            object->sealHostWrites();
            const char* id = nullptr;
            *destination = rhi::OriginalCommandBufferLease::prepareOriginal(
                object, {1u, true, 2u, cpuBytes}, quota, id);
            assert(*destination && !id);
        }
    CloudPreparedHistory history;
    auto missing = slots;
    missing[0].resolved.reset();
    assert(!history.create(device, 1u, config, plan, asset.source, missing, quota).success);
    auto duplicate = slots;
    duplicate[1].raw = duplicate[0].resolved;
    assert(!history.create(device, 1u, config, plan, asset.source, duplicate, quota).success);
    require(history.create(device, 1u, config, plan, asset.source, slots, quota));
    render_graph::FrameGraph graph(device);
    auto c = frame(graph, 0.f);
    require(history.prepareFrame(c));
    assert(!history.resetHistory().success);
    const auto cancelledToken = history.token();
    const auto cancelledPhase = history.pendingFrame().parameters.jitter.z;
    require(history.cancelPendingFrame());
    assert(history.validateSubmission(cancelledToken));
    require(history.prepareFrame(c));
    assert(history.pendingFrame().parameters.jitter.z == cancelledPhase);
    assert(history.constants()->originalObject()->hostWritesSealed());
    assert(history.constants()->originalObject()->gpuWritesSealed());
    assert(history.previous() == history.raw());
    std::array<std::shared_ptr<rhi::GpuFence>, 3u> fences;
    std::array<rhi::GpuResourceId, 3u> outputs;
    for (unsigned i = 0u; i < 3u; ++i) {
        if (i) {
            c.time = float(i) / 60.f;
            require(history.prepareFrame(c));
            assert(history.pendingFrame().historyUsed);
            assert(history.previous()->resource() == outputs[i - 1u]);
        }
        outputs[i] = history.resolved()->resource();
        fences[i] = core::shareUnique(device.createFence());
        assert(fences[i]);
        // Explicit logical delayed completion; this is not native GPU evidence.
        history.onSubmitted(history.token(), fences[i]);
    }
    assert(!history.frameReady() && !history.releaseIdle().success);
    fences[0]->signal();
    assert(!history.frameReady()); // Frame 1 still reads frame 0's output.
    fences[1]->signal();
    assert(history.frameReady());
    require(history.prepareFrame(c));
    const auto submitted = history.token();
    assert(!history.validateSubmission(submitted));
    assert(!history.cancelPendingFrame().success);
    auto completed = core::shareUnique(device.createFence());
    completed->signal();
    history.onSubmitted(submitted, completed);
    fences[2]->signal();
    assert(history.idle());
    require(history.releaseIdle());
    assert(!history.frameReady());
    // Resize is an idle release, then an explicitly budgeted new resource set.
    auto resized = config;
    resized.resolutionDivisor = 2u;
    require(CloudExecutionPlanner::describeResources(resized,
        {31u, 23u},
        asset.cells.size() * sizeof(core::Vec4),
        sizeof(CloudGpuConstants),
        plan));
    require(history.create(device, 1u, resized, plan, asset.source, slots, quota));
    require(history.prepareFrame(c));
    assert(!history.pendingFrame().historyUsed);
    require(history.cancelPendingFrame());
    require(history.releaseIdle());
    // A failed execution has no successful submission receipt. Cancellation
    // must stay closed until explicit device drain; teardown invalidates token.
    require(history.create(device, 1u, resized, plan, asset.source, slots, quota));
    require(history.prepareFrame(c));
    const auto failed = history.token();
    assert(!history.validateSubmission(failed));
    assert(!history.cancelPendingFrame().success && !history.releaseIdle().success);
    history.release();
    assert(history.validateSubmission(failed));
    require(history.create(device, 1u, resized, plan, asset.source, slots, quota));
    require(history.prepareFrame(c));
    assert(!history.pendingFrame().historyUsed);
    require(history.cancelPendingFrame());
    require(history.releaseIdle());
}
} // namespace

int main(int argc, char** argv)
{
    {
        using T = rhi::DescriptorType;
        using rhi::semantics::metalOriginalBufferAccessDiagnostic;
        assert(!metalOriginalBufferAccessDiagnostic(T::UniformBuffer, 0u, false));
        assert(!metalOriginalBufferAccessDiagnostic(T::StorageBuffer, 1u, false));
        assert(!metalOriginalBufferAccessDiagnostic(T::ReadOnlyStorageBuffer, 0u, true));
        assert(!metalOriginalBufferAccessDiagnostic(T::ReadOnlyStorageBuffer, 1u, true));
        for (const auto access : {0u, 1u, 2u, UINT32_MAX})
            assert(metalOriginalBufferAccessDiagnostic(T::ReadOnlyStorageBuffer, access, false));
        for (const auto access : {2u, UINT32_MAX})
            assert(metalOriginalBufferAccessDiagnostic(T::ReadOnlyStorageBuffer, access, true));
        assert(metalOriginalBufferAccessDiagnostic(T::UniformBuffer, 1u, true));
        assert(metalOriginalBufferAccessDiagnostic(T::StorageBuffer, 0u, true));
        using S = rhi::ShaderStage;
        using rhi::semantics::metalOriginalBufferStagesDiagnostic;
        assert(!metalOriginalBufferStagesDiagnostic(
            S::Fragment, S::Vertex | S::Fragment, S::Fragment, false));
        assert(!metalOriginalBufferStagesDiagnostic(S::Compute, S::Compute, S::Compute, true));
        assert(!metalOriginalBufferStagesDiagnostic(S::Fragment, S::Fragment, S::None, false));
        assert(metalOriginalBufferStagesDiagnostic(S::Fragment, S::Fragment, S::Vertex, false));
        assert(metalOriginalBufferStagesDiagnostic(S::Fragment, S::Vertex, S::None, false));
        assert(metalOriginalBufferStagesDiagnostic(S::Fragment, S::Compute, S::Compute, false));
        assert(metalOriginalBufferStagesDiagnostic(S::None, S::Fragment, S::None, false));
        assert(metalOriginalBufferStagesDiagnostic(S::Vertex, S::Vertex, S::Vertex, true));
        std::puts(
            "METAL_BUFFER_ACCESS_INTENT_PASS mutablePermissionRequiresOriginalNonWritable=true noWriteUpgrade=true inactiveFormalStagesRequireNativeProof=true");
    }
    if (argc == 3 && !std::strcmp(argv[1], "--ordinary-native-memory")) {
        test::CloudNativeMemoryFixture::verifyLoad(argv[2]);
        std::cout
            << "ORDINARY_NATIVE_MEMORY_LOAD_PASS cloudCompute3 graphics3 exactCoverage=true originalOwner=true copiedRules=true quotaRevocation=true quotasDrained=true\n";
        return 0;
    }
    auto device = rhi::createNullDevice();
    if (argc == 2 && !std::strcmp(argv[1], "--quality-integration-probe")) {
        qualityIntegrationProbe(*device, false);
        return 0;
    }
    if (argc == 2 && !std::strcmp(argv[1], "--spatial-motion-reference")) {
        spatialMotionReference(*device);
        return 0;
    }
    if (argc == 2 && !std::strcmp(argv[1], "--quality-reconstruction-probe")) {
        qualityIntegrationProbe(*device, true);
        return 0;
    }
    if (argc == 3 && !std::strcmp(argv[1], "--quality-reconstruction-probe")) {
        const unsigned cameraCase = !std::strcmp(argv[2], "inside") ? 1u : 2u;
        assert(cameraCase == 1u || !std::strcmp(argv[2], "above"));
        qualityIntegrationProbe(*device, true, cameraCase);
        return 0;
    }
    assert(argc == 1);
    const auto lifetime = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
    const auto registry = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
    assert(!device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, cpuBytes}, lifetime));
    assert(!device->prepareOriginalResourceRegistration(
        {1u, true, 16u, 4u, 4u, 51u, cpuBytes}, registry));
    ordinaryMetalCpuProjectionContract();
    parameters(*device);
    reconstructionContract(*device);
    dispatchRecordingContract(*device);
    synchronousDescriptorLossIntent();
    preparedPipelineContract(*device);
    preparedResourceContract(*device);
    preparedSubmissionLifetime(*device);
    preparedFeatureOwnership(*device);
    resourceOwnership(*device);
    preparedDescriptorDeclarations(*device);
    publicSourceAssemblyPublication();
    publicSourceAssemblyPublication(true);
    reconstructionHistory(*device);
    referenceSampling();
    tests::cloud::QualityError error;
    assert(!error.within(1, 1));
    error.add(.5, 0);
    error.add(1, 1);
    assert(error.count == 2 && error.maximum == .5);
    assert(std::fabs(error.rms() - std::sqrt(.125)) < 1.e-12);
    assert(error.within(.36, .5) && !error.within(.35, .5) && !error.within(.36, .49));
    occlusionReference(*device);
    qualityDepthReference(*device);
    lifecycle(*device);
    std::cout
        << "Cloud prepared history parameters, immutable generations and reader retirement passed (Null validator)\n";
}

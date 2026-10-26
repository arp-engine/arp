#include "core/math/matrix_validation.h"
#include "renderer/volumetric_cloud/cloud_contract.h"
#include "renderer/volumetric_cloud/cloud_execution_plan.h"
#include "renderer/volumetric_cloud/cloud_prepared_pipelines.h"
#include "renderer/volumetric_cloud/cloud_quality.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/backends/null/null_hidden_frame_smoke.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <string_view>

using namespace arp;
using namespace renderer::cloud;

scene3d::CloudLayerDesc layer()
{
    scene3d::CloudLayerDesc d;
    d.planetRadius = 100.0f;
    d.baseAltitude = 10.0f;
    d.thickness = 20.0f;
    d.extinction = 0.1f;
    d.scatteringAlbedo = {1, 0.8f, 0.6f};
    d.weatherPeriod = 1000;
    d.shapePeriod = 100;
    d.detailPeriod = 10;
    return d;
}

void close(double a, double b, double tolerance = 0.00001)
{
    if (std::fabs(a - b) > tolerance) {
        std::cerr << "expected " << b << ", got " << a << '\n';
        std::abort();
    }
}

void shellTests()
{
    auto d = layer();
    CloudRayIntervals hits;
    assert(CloudContract::intersect(d, {{0, 100, 0}, {0, 1, 0}, 1000}, hits).success);
    assert(hits.count == 1);
    close(hits.intervals[0].begin, 10);
    close(hits.intervals[0].end, 30);
    assert(CloudContract::intersect(d, {{0, 120, 0}, {0, 1, 0}, 1000}, hits).success);
    assert(hits.count == 1);
    close(hits.intervals[0].begin, 0);
    close(hits.intervals[0].end, 10);
    assert(CloudContract::intersect(d, {{0, 150, 0}, {0, -1, 0}, 1000}, hits).success);
    assert(hits.count == 1);
    close(hits.intervals[0].begin, 20);
    close(hits.intervals[0].end, 40);
    // Ray crosses both cloud lobes while missing the opaque planet.
    assert(CloudContract::intersect(d, {{-200, 105, 0}, {1, 0, 0}, 1000}, hits).success);
    assert(hits.count == 2 && hits.intervals[0].end < hits.intervals[1].begin);
    close(hits.intervals[0].begin, 200 - std::sqrt(130.0 * 130 - 105.0 * 105));
    assert(CloudContract::intersect(d, {{0, 100, 0}, {0, -1, 0}, 1000}, hits).success);
    assert(hits.count == 0);
    assert(CloudContract::intersect(d, {{0, 100, 0}, {0, 1, 0}, 15}, hits).success);
    close(hits.intervals[0].end, 15);
    assert(CloudContract::intersect(d, {{-200, 130, 0}, {1, 0, 0}, 1000}, hits).success);
    assert(hits.count == 0);
    assert(!CloudContract::intersect(d, {{0, 99, 0}, {0, 1, 0}, 1000}, hits).success);
    assert(hits.count == 0);
    assert(!CloudContract::intersect(d, {{0, 100, 0}, {0, 2, 0}, 1000}, hits).success);
    d.planetRadius = 6371000;
    assert(CloudContract::intersect(d, {{0, 6371000, 0}, {0, 1, 0}, 1000}, hits).success);
    close(hits.intervals[0].begin, 10);
    close(hits.intervals[0].end, 30);
    d = layer();
    d.planetCenter = {1000, -100, 30};
    assert(CloudContract::intersect(d, {{1000, 0, 30}, {0, 1, 0}, 1000}, hits).success);
    close(hits.intervals[0].begin, 10);
    close(hits.intervals[0].end, 30);
    d.thickness = std::numeric_limits<float>::quiet_NaN();
    assert(!CloudContract::validate(d).success);
    assert(!CloudContract::validate({}).success);
}

void mediumTests()
{
    using namespace renderer::volume;
    const auto empty = homogeneous(0, {}, 1000);
    close(empty.transmittance, 1);
    close(core::length(empty.radiance), 0);
    const auto limit = homogeneous(0, {1, 2, 3}, 2);
    close(limit.radiance.z, 6);
    const auto full = homogeneous(0.2f, {0.3f, 0.6f, 0.9f}, 10);
    close(full.transmittance, std::exp(-2.0));
    close(full.radiance.x, 1.5 * (1 - std::exp(-2.0)));
    MediumIntegral split;
    float previous = 1;
    for (int i = 0; i < 100; ++i) {
        split = compose(split, homogeneous(0.2f, {0.3f, 0.6f, 0.9f}, 0.1f));
        assert(split.transmittance <= previous);
        previous = split.transmittance;
    }
    close(split.transmittance, full.transmittance);
    close(split.radiance.z, full.radiance.z);
    const auto a = homogeneous(0.1f, {0.2f, 0, 0}, 2);
    const auto b = homogeneous(0.3f, {0, 0.5f, 0}, 3);
    const auto c = homogeneous(0.7f, {0, 0, 0.4f}, 4);
    const auto lhs = compose(compose(a, b), c);
    const auto rhs = compose(a, compose(b, c));
    close(core::length(lhs.radiance - rhs.radiance), 0);
    close(lhs.transmittance, rhs.transmittance);
    // Overlapping media sum coefficients before integration.
    const auto overlap = homogeneous(0.4f, {0.2f, 0.5f, 0}, 5);
    close(overlap.transmittance, std::exp(-2.0));
    close(overlap.radiance.y, 1.25 * (1 - std::exp(-2.0)));
    close(henyeyGreenstein(0.3f, 0), 1 / (4 * std::acos(-1.0)));
    const auto background = core::Vec3{0.3f, 2.0f, 5.0f};
    close(core::length(composite(empty, background) - background), 0);
}

void executionTests()
{
    auto device = rhi::createNullDevice();
    auto caps = device->capabilities();
    CloudRenderConfig config;
    config.resolutionDivisor = 2;
    config.viewSteps = 64;
    config.lightSteps = 12;
    config.framesInFlight = 3;
    config.gpuBudgetBytes = 128ull * 1024u * 1024u;
    config.maxDensityEvaluations = 1000000000ull;
    config.maxDistance = 100000;
    config.historyWeight = 0.9f;
    config.historyDepthTolerance = 20;
    CloudExecutionPlan plan;
    constexpr rhi::Extent2D extent{1280, 720};
    constexpr std::uint64_t assetBytes = 16u * 65536u;
    const auto run = [&](const auto& c, const auto& capabilities) {
        return CloudExecutionPlanner::plan(c, extent, assetBytes, 512, capabilities, plan);
    };
    assert(run(config, caps).success);
    assert(plan.sampleExtent.width == 640 && plan.sampleExtent.height == 360);
    assert(plan.dispatchGroups.width == 80 && plan.dispatchGroups.height == 45
        && plan.dispatchGroups.depth == 1);
    assert(plan.sampleCount == 230400 && plan.sampleBufferBytes == 7372800);
    assert(plan.retainedGpuBytes == (2u * 7372800u + 512u + 1280u * 720u * 8u) * 3ull + assetBytes);
    assert(plan.densityEvaluationBound == 737280000ull);
    // Backend identity cannot manufacture capabilities or select a different
    // algorithm. These are validator fixtures, not native-backend evidence.
    for (auto backend :
        {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
        auto declared = caps;
        declared.backend = backend;
        assert(run(config, declared).success);
        declared.features.computeDispatch = false;
        assert(!run(config, declared).success && plan.sampleCount == 0);
        declared = caps;
        declared.features.storageBufferBinding = false;
        assert(!run(config, declared).success);
        declared = caps;
        declared.computeResources.sampledDepthTexture = false;
        assert(!run(config, declared).success);
        declared = caps;
        declared.features.nativeSync = false;
        // Cloud uses queue-associated completion fences, not the wider
        // NativeSync surface (which also includes binary semaphores).
        assert(run(config, declared).success);
        declared = caps;
        declared.memory.hostVisible = false;
        assert(!run(config, declared).success);
    }
    for (auto mode : {rhi::FallbackMode::CpuReplay,
             rhi::FallbackMode::Degraded,
             rhi::FallbackMode::DiagnosticFailure,
             rhi::FallbackMode::ExtensionOptIn}) {
        auto unavailable = caps;
        for (auto& status : unavailable.executionStatuses)
            if (status.feature == rhi::EngineFeature::ComputeDispatch)
                status.mode = mode;
        assert(!run(config, unavailable).success && plan.retainedGpuBytes == 0);
    }
    auto unavailable = caps;
    unavailable.executionStatuses.clear();
    assert(!run(config, unavailable).success);
    unavailable = caps;
    for (const auto& s : caps.executionStatuses)
        if (s.feature == rhi::EngineFeature::ComputeDispatch)
            unavailable.executionStatuses.push_back(s);
    assert(!run(config, unavailable).success);
    unavailable = caps;
    unavailable.computeResources.mode = rhi::CapabilityMode::Degraded;
    assert(!run(config, unavailable).success);
    unavailable = caps;
    unavailable.computeResources.maxStorageBufferBindingsPerStage = 2;
    assert(!run(config, unavailable).success);
    unavailable = caps;
    unavailable.computeResources.maxComputeWorkgroupSizeX = 7;
    assert(!run(config, unavailable).success);
    // Numeric facts are inclusive and independent. Test an exact boundary,
    // one-over work, and missing facts without backend-name inference.
    auto exact = caps;
    exact.computeResources.maxComputeWorkgroupCountX = 80;
    exact.computeResources.maxComputeWorkgroupCountY = 45;
    exact.computeResources.maxComputeWorkgroupCountZ = 1;
    exact.computeResources.maxComputeWorkgroupSizeZ = 1;
    exact.computeResources.maxUniformBufferBindingsPerStage = 1;
    exact.computeResources.maxUniformBufferBindingSizeBytes = 512;
    exact.computeResources.maxSampledTextureBindingsPerStage = 2;
    exact.computeResources.maxTexture2DDimension = 1280;
    assert(run(config, exact).success);
    const auto rejects = [&](auto member, auto value, std::string_view diagnostic) {
        auto limited = exact;
        limited.computeResources.*member = value;
        const auto result = run(config, limited);
        assert(!result.success && result.diagnostic == diagnostic);
        assert(plan.sampleCount == 0 && plan.retainedGpuBytes == 0);
    };
    using L = rhi::ComputeResourceCapabilities;
    rejects(&L::maxComputeWorkgroupCountX, 79u, "CLOUD-DISPATCH-LIMIT");
    rejects(&L::maxComputeWorkgroupCountY, 44u, "CLOUD-DISPATCH-LIMIT");
    rejects(&L::maxComputeWorkgroupCountZ, 0u, "CLOUD-DISPATCH-LIMIT");
    rejects(&L::maxComputeWorkgroupCountX, 0u, "CLOUD-DISPATCH-LIMIT");
    rejects(&L::maxComputeWorkgroupCountY, 0u, "CLOUD-DISPATCH-LIMIT");
    rejects(&L::maxComputeWorkgroupSizeZ, 0u, "CLOUD-COMPUTE-LIMIT");
    rejects(&L::maxUniformBufferBindingsPerStage, 0u, "CLOUD-UNIFORM-LIMIT");
    rejects(&L::maxUniformBufferBindingSizeBytes, 511ull, "CLOUD-UNIFORM-LIMIT");
    rejects(&L::maxUniformBufferBindingSizeBytes, 0ull, "CLOUD-UNIFORM-LIMIT");
    rejects(&L::maxSampledTextureBindingsPerStage, 1u, "CLOUD-TEXTURE-LIMIT");
    rejects(&L::maxSampledTextureBindingsPerStage, 0u, "CLOUD-TEXTURE-LIMIT");
    rejects(&L::maxTexture2DDimension, 1279u, "CLOUD-TEXTURE-LIMIT");
    rejects(&L::maxTexture2DDimension, 0u, "CLOUD-TEXTURE-LIMIT");
    assert(!CloudExecutionPlanner::plan(config, {1281, 720}, assetBytes, 512, exact, plan).success);
    // Height must be checked even when width is smaller than the texture bound.
    assert(!CloudExecutionPlanner::plan(config, {720, 1281}, assetBytes, 512, exact, plan).success);
    assert(CloudExecutionPlanner::plan(config, {1279, 719}, assetBytes, 512, exact, plan).success);
    assert(plan.dispatchGroups.width == 80 && plan.dispatchGroups.height == 45);
    auto division = config;
    division.resolutionDivisor = 3;
    assert(
        CloudExecutionPlanner::plan(division, {1279, 719}, assetBytes, 512, exact, plan).success);
    assert(plan.sampleExtent.width == 427 && plan.sampleExtent.height == 240);
    assert(plan.dispatchGroups.width == 54 && plan.dispatchGroups.height == 30);
    unavailable = caps;
    unavailable.computeResources.maxStorageBufferBindingSizeBytes = 1024;
    assert(!run(config, unavailable).success);
    auto invalid = config;
    invalid.gpuBudgetBytes = 1;
    assert(!run(invalid, caps).success);
    invalid = config;
    invalid.maxDensityEvaluations = 1;
    assert(!run(invalid, caps).success);
    invalid = config;
    invalid.viewSteps = std::numeric_limits<std::uint32_t>::max();
    invalid.lightSteps = std::numeric_limits<std::uint32_t>::max();
    assert(!run(invalid, caps).success);
    invalid = config;
    invalid.resolutionDivisor = 0;
    assert(!run(invalid, caps).success);
    invalid = config;
    invalid.historyWeight = 1;
    assert(!run(invalid, caps).success);
    invalid = config;
    invalid.framesInFlight = std::numeric_limits<std::uint32_t>::max();
    assert(!run(invalid, caps).success);
    assert(!CloudExecutionPlanner::plan(config, {}, assetBytes, 512, caps, plan).success);
    assert(!CloudExecutionPlanner::plan(config, extent, assetBytes + 1, 512, caps, plan).success);
    assert(!CloudExecutionPlanner::plan(config, extent, assetBytes, 0, caps, plan).success);
    assert(CloudExecutionPlanner::plan(config, {1279, 719}, assetBytes, 512, caps, plan).success);
    assert(plan.sampleExtent.width == 640 && plan.sampleExtent.height == 360);
}

void qualityTests()
{
    CloudRenderConfig config;
    config.framesInFlight = 3u;
    config.gpuBudgetBytes = 128u * 1024u * 1024u;
    config.maxDensityEvaluations = 2000000000u;
    config.maxDistance = 60000;
    config.historyWeight = .9f;
    config.historyDepthTolerance = 50;
    const auto authored = config;
    assert(!CloudQuality::apply(0u, CloudQualityTier::Medium, config).success);
    assert(config.viewSteps == 0u && config.gpuBudgetBytes == authored.gpuBudgetBytes);
    assert(!CloudQuality::apply(1u, static_cast<CloudQualityTier>(99), config).success);
    assert(config.viewSteps == 0u);
    for (const auto tier :
        {CloudQualityTier::Reference, CloudQualityTier::High, CloudQualityTier::Medium}) {
        assert(CloudQuality::apply(1u, tier, config).success);
        assert(config.framesInFlight == authored.framesInFlight
            && config.gpuBudgetBytes == authored.gpuBudgetBytes
            && config.maxDensityEvaluations == authored.maxDensityEvaluations
            && config.maxDistance == authored.maxDistance
            && config.historyWeight == authored.historyWeight
            && config.historyDepthTolerance == authored.historyDepthTolerance);
        assert(config.resolutionDivisor == (tier == CloudQualityTier::Medium ? 2u : 1u));
        assert(config.viewSteps
            == (tier == CloudQualityTier::Medium     ? 64u
                    : tier == CloudQualityTier::High ? 128u
                                                     : 256u));
        assert(config.lightSteps
            == (tier == CloudQualityTier::Medium     ? 8u
                    : tier == CloudQualityTier::High ? 16u
                                                     : 32u));
        assert(config.temporal == (tier != CloudQualityTier::Reference));
        CloudExecutionPlan plan;
        assert(CloudExecutionPlanner::describeResources(config, {64u, 48u}, 65536u, 368u, plan)
                .success);
        const auto target =
            CloudExecutionPlanner::describeResources(config, {1280u, 720u}, 65536u, 368u, plan);
        assert(target.success == (tier == CloudQualityTier::Medium));
        if (!target.success) {
            assert(std::string_view(target.diagnostic) == "CLOUD-WORK-BUDGET");
            auto boundedWork = config;
            boundedWork.maxDensityEvaluations =
                1280ull * 720u * 2u * config.viewSteps * (2u * config.lightSteps + 1u);
            const auto memory = CloudExecutionPlanner::describeResources(
                boundedWork, {1280u, 720u}, 65536u, 368u, plan);
            assert(!memory.success && std::string_view(memory.diagnostic) == "CLOUD-MEMORY-BUDGET");
        }
    }
    CloudRenderConfig unauthored;
    assert(CloudQuality::apply(1u, CloudQualityTier::Medium, unauthored).success);
    CloudExecutionPlan plan;
    assert(!CloudExecutionPlanner::describeResources(unauthored, {64u, 48u}, 65536u, 368u, plan)
            .success);
}

void dispatchAdmissionTests()
{
    auto device = rhi::createNullDevice();
    const auto limits = device->capabilities().computeResources;
    CloudRenderConfig config;
    config.resolutionDivisor = 2u;
    config.viewSteps = 24u;
    config.lightSteps = 4u;
    config.framesInFlight = 3u;
    config.gpuBudgetBytes = 128u * 1024u * 1024u;
    config.maxDensityEvaluations = 1000000000u;
    config.maxDistance = 10000.f;
    config.historyWeight = .9f;
    config.historyDepthTolerance = 10.f;
    const CloudOriginalDispatchAdmission unissued{*device, {}};
    const CloudPreparedPipelineSelection temporal{1u, CloudPreparedResolve::Temporal1, {}};
    const CloudPreparedPipelineSelection spatial{
        1u, CloudPreparedResolve::Reconstruction1, {1u, .025f, .025f}};
    const CloudPreparedResourceRequest request{config, {48u, 32u}, 65536u, 9u, 45u};
    CloudPreparedResourcePlan plan;
    for (const auto& selection : {temporal, spatial}) {
        assert(CloudPreparedPipelines::planResources(selection, request, limits, plan).success);
        const auto before = core::defaultCpuAllocator().stats().allocationCount;
        const auto result =
            CloudPreparedPipelines::planResources(selection, request, limits, unissued, plan);
        assert(
            !result.success && std::string_view(result.diagnostic) == "RHI-DISPATCH-DOMAIN-OWNER");
        assert(!plan.raw.sampleCount && !plan.reconstruction.resolvedCount && !plan.descriptorSets
            && !plan.descriptorElements);
        assert(before == core::defaultCpuAllocator().stats().allocationCount);
        auto missing = limits;
        missing.maxComputeWorkgroupCountX = 0u;
        const auto numeric =
            CloudPreparedPipelines::planResources(selection, request, missing, plan);
        assert(!numeric.success && std::string_view(numeric.diagnostic) == "CLOUD-DISPATCH-LIMIT");
        assert(!plan.raw.sampleCount && !plan.descriptorSets);
    }
    std::cout
        << "CLOUD_DISPATCH_ADMISSION_CONTRACT_PASS explicit unissued domain refuses without numeric fallback; allocations=0\n";
}

int main(int argc, char**)
{
    const auto nonsymmetric = core::multiply(
        core::perspective(57, 1.3f, 1, 5000), core::lookAt({13, 41, -7}, {2, 80, 19}));
    assert(core::isApproximateInverse(nonsymmetric, core::inverse(nonsymmetric), .002f));
    const auto singular = core::Mat4::scale({0, 1, 1});
    assert(!core::isApproximateInverse(singular, core::inverse(singular), .002f));
    auto nonfinite = core::Mat4::identity();
    nonfinite.m[3] = std::numeric_limits<float>::infinity();
    assert(!core::isApproximateInverse(nonfinite, nonfinite, .002f));
    assert(!core::isApproximateInverse(core::Mat4::identity(), core::Mat4::identity(), -1));
    shellTests();
    mediumTests();
    executionTests();
    qualityTests();
    dispatchAdmissionTests();
    if (argc > 1) {
        // P0 validates the reference geometry/integral and baseline hidden-frame
        // lifecycle. Cloud GPU execution is introduced and tested in P3.
        auto device = rhi::createNullDevice();
        assert(device);
        const auto report = rhi::NullHiddenFrameSmokeRunner(*device).run(
            {"Cloud.Contract.HiddenFrame", {64, 64}, rhi::TextureFormat::Bgra8Unorm, 2u});
        if (!report.success)
            std::cerr << report.diagnostics << '\n';
        assert(report.success && report.submittedFrameCount == 2u);
        assert(report.descriptorCompletenessValidated && !report.nativeHandleExposedToUpperLayer);
    }
    std::cout << "VolumetricCloud/1 shell and medium contracts passed\n";
}

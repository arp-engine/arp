#include "renderer/volumetric_cloud/cloud_execution_plan.h"

#include "core/math/unsigned_range.h"

#include <cmath>
#include <limits>

namespace arp::renderer::cloud {
namespace {

bool executable(const rhi::DeviceCapabilities& caps, rhi::EngineFeature feature) noexcept
{
    std::uint32_t entries = 0u;
    bool supported = false;
    for (const auto& status : caps.executionStatuses) {
        if (status.feature == feature) {
            ++entries;
            supported = status.executable && rhi::isExecutableFallbackMode(status.mode);
        }
    }
    return entries == 1u && supported;
}

bool multiply(std::uint64_t a, std::uint64_t b, std::uint64_t& output) noexcept
{
    if (!core::canMultiplyUnsigned(a, b))
        return false;
    output = a * b;
    return true;
}

bool add(std::uint64_t a, std::uint64_t b, std::uint64_t& output) noexcept
{
    if (!core::canAddUnsigned(a, b))
        return false;
    output = a + b;
    return true;
}

} // namespace

CloudResult CloudExecutionPlanner::plan(const CloudRenderConfig& c,
    rhi::Extent2D extent,
    std::uint64_t assetBytes,
    std::uint32_t uniformBytes,
    const rhi::DeviceCapabilities& caps,
    CloudExecutionPlan& output) noexcept
{
    output = {};
    if (!extent.width || !extent.height || !c.resolutionDivisor || !c.viewSteps || !c.lightSteps
        || c.framesInFlight < 2u
        || c.framesInFlight
            > std::numeric_limits<std::uint32_t>::max() / kCloudDescriptorElementsPerSlot
        || !c.gpuBudgetBytes || !c.maxDensityEvaluations || !std::isfinite(c.maxDistance)
        || c.maxDistance <= 0.0f || !std::isfinite(c.historyWeight) || c.historyWeight < 0.0f
        || c.historyWeight >= 1.0f || !std::isfinite(c.historyDepthTolerance)
        || c.historyDepthTolerance <= 0.0f || !assetBytes || assetBytes % 16u || !uniformBytes
        || uniformBytes % 16u)
        return CloudResult::failure("CLOUD-RENDER-CONFIG-INVALID");
    const auto& limits = caps.computeResources;
    if (!caps.core.compute || !caps.core.graphics || !caps.features.computeDispatch
        || !caps.features.storageBufferBinding || !caps.memory.hostVisible
        || !limits.sampledDepthTexture
        || (limits.mode != rhi::CapabilityMode::Native
            && limits.mode != rhi::CapabilityMode::Equivalent))
        return CloudResult::failure("CLOUD-CAPABILITY-UNAVAILABLE");
    if (!executable(caps, rhi::EngineFeature::ComputeDispatch)
        || !executable(caps, rhi::EngineFeature::StorageBufferBinding))
        return CloudResult::failure("CLOUD-EXECUTION-UNAVAILABLE");
    // Raymarch/resolve consume at most asset, previous-sample and output buffers.
    if (limits.maxComputeWorkgroupInvocations < kCloudWorkgroupWidth * kCloudWorkgroupHeight
        || limits.maxComputeWorkgroupSizeX < kCloudWorkgroupWidth
        || limits.maxComputeWorkgroupSizeY < kCloudWorkgroupHeight
        || limits.maxComputeWorkgroupSizeZ < 1u || limits.maxStorageBufferBindingsPerStage < 3u)
        return CloudResult::failure("CLOUD-COMPUTE-LIMIT");
    if (limits.maxUniformBufferBindingsPerStage < 1u
        || uniformBytes > limits.maxUniformBufferBindingSizeBytes)
        return CloudResult::failure("CLOUD-UNIFORM-LIMIT");
    if (limits.maxSampledTextureBindingsPerStage < 2u || extent.width > limits.maxTexture2DDimension
        || extent.height > limits.maxTexture2DDimension)
        return CloudResult::failure("CLOUD-TEXTURE-LIMIT");
    CloudExecutionPlan p;
    p.extent = extent;
    p.sampleExtent = {1u + (extent.width - 1u) / c.resolutionDivisor,
        1u + (extent.height - 1u) / c.resolutionDivisor};
    p.dispatchGroups = {1u + (p.sampleExtent.width - 1u) / kCloudWorkgroupWidth,
        1u + (p.sampleExtent.height - 1u) / kCloudWorkgroupHeight,
        1u};
    if (p.dispatchGroups.width > limits.maxComputeWorkgroupCountX
        || p.dispatchGroups.height > limits.maxComputeWorkgroupCountY
        || p.dispatchGroups.depth > limits.maxComputeWorkgroupCountZ)
        return CloudResult::failure("CLOUD-DISPATCH-LIMIT");
    p.sampleCount = std::uint64_t(p.sampleExtent.width) * p.sampleExtent.height;
    if (p.sampleCount > std::numeric_limits<std::uint32_t>::max())
        return CloudResult::failure("CLOUD-SAMPLE-INDEX-OVERFLOW");
    p.sampleBufferBytes = p.sampleCount * kCloudSampleBytes;
    if (p.sampleBufferBytes > limits.maxStorageBufferBindingSizeBytes
        || assetBytes > limits.maxStorageBufferBindingSizeBytes)
        return CloudResult::failure("CLOUD-STORAGE-LIMIT");
    // Two shell intervals, each bounded by viewSteps, and at most two light
    // intervals per sample plus the primary density evaluation.
    const auto lightEvaluations = 2ull * c.lightSteps + 1u;
    if (!multiply(p.sampleCount, 2ull * c.viewSteps, p.densityEvaluationBound)
        || !multiply(p.densityEvaluationBound, lightEvaluations, p.densityEvaluationBound))
        return CloudResult::failure("CLOUD-WORK-OVERFLOW");
    if (p.densityEvaluationBound > c.maxDensityEvaluations)
        return CloudResult::failure("CLOUD-WORK-BUDGET");
    // Per slot: raw + resolved samples and a full-resolution RGBA16F output.
    std::uint64_t colorBytes = 0u, slotBytes = 0u;
    if (!multiply(std::uint64_t(extent.width) * extent.height, 8u, colorBytes)
        || !add(2u * p.sampleBufferBytes, uniformBytes, slotBytes)
        || !add(slotBytes, colorBytes, slotBytes)
        || !multiply(slotBytes, c.framesInFlight, p.retainedGpuBytes)
        || !add(p.retainedGpuBytes, assetBytes, p.retainedGpuBytes))
        return CloudResult::failure("CLOUD-MEMORY-OVERFLOW");
    if (p.retainedGpuBytes > c.gpuBudgetBytes)
        return CloudResult::failure("CLOUD-MEMORY-BUDGET");
    output = p;
    return {};
}

} // namespace arp::renderer::cloud

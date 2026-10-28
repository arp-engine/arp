#pragma once

#include "core/container/array_view.h"
#include "core/math/unsigned_range.h"
#include "renderer/volumetric_cloud/cloud_execution_plan.h"
#include "renderer/volumetric_cloud/cloud_gpu_contract.h"
#include "renderer/volumetric_cloud/cloud_sample_storage.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace arp::renderer::cloud {

// CloudReconstruction/1 is an explicit candidate, not an execution capability.
// A 4x4 halo around the bilinear footprint classifies spatial variation in
// linear L RGB and T. The authored ranges are classification thresholds, not
// a mathematical bound on unsampled density or a quality acceptance claim.
struct CloudReconstructionConfig {
    std::uint32_t contractVersion = 0u;
    float maxRadianceRange = 0.f, maxTransmittanceRange = 0.f;
};

struct CloudReconstructionResources {
    CloudExecutionPlan raw;
    rhi::Extent3D dispatchGroups{};
    std::uint64_t resolvedCount = 0u, resolvedBufferBytes = 0u;
    std::uint64_t retainedGpuBytes = 0u, densityEvaluationBound = 0u;
    std::uint32_t sampleContractVersion = 0u;
};

class CloudReconstruction final {
public:
    static constexpr std::size_t footprintSamples = 16u;
    static constexpr std::uint32_t descriptorElementsPerSlot = 15u;

    static CloudResult validate(const CloudReconstructionConfig& c) noexcept
    {
        if (c.contractVersion != 1u && c.contractVersion != 2u)
            return CloudResult::failure("CLOUD-RECONSTRUCTION-VERSION");
        if (!std::isfinite(c.maxRadianceRange) || c.maxRadianceRange <= 0
            || !std::isfinite(c.maxTransmittanceRange) || c.maxTransmittanceRange <= 0
            || c.maxTransmittanceRange > 1)
            return CloudResult::failure("CLOUD-RECONSTRUCTION-RANGE");
        return {};
    }

    // Invalid input is rejected; it never silently selects either path.
    // Exact shell exclusion precedes this classifier at the consumer.
    static CloudResult classify(const CloudReconstructionConfig& c,
        core::ArrayView<CloudGpuSample> samples,
        float opaqueDistance,
        float depthTolerance,
        bool& refine) noexcept
    {
        refine = false;
        if (const auto result = validate(c); !result.success)
            return result;
        if (!samples.data() || samples.size() != footprintSamples || !std::isfinite(opaqueDistance)
            || opaqueDistance < 0 || !std::isfinite(depthTolerance) || depthTolerance <= 0)
            return CloudResult::failure("CLOUD-RECONSTRUCTION-FOOTPRINT");
        std::array<float, 4> lower{}, upper{};
        bool next = false;
        for (std::size_t i = 0; i < samples.size(); ++i) {
            const auto& sample = samples[i];
            const auto& v = sample.radianceTransmittance;
            const std::array value{v.x, v.y, v.z, v.w};
            if (!std::isfinite(sample.metadata.x) || sample.metadata.x < 0
                || !std::isfinite(sample.metadata.y) || sample.metadata.y < 0 || v.w < 0 || v.w > 1)
                return CloudResult::failure("CLOUD-RECONSTRUCTION-SAMPLE");
            for (std::size_t channel = 0; channel < value.size(); ++channel) {
                if (!std::isfinite(value[channel]) || value[channel] < 0)
                    return CloudResult::failure("CLOUD-RECONSTRUCTION-SAMPLE");
                lower[channel] = i ? std::min(lower[channel], value[channel]) : value[channel];
                upper[channel] = i ? std::max(upper[channel], value[channel]) : value[channel];
            }
            next |= std::fabs(sample.metadata.y - opaqueDistance) > depthTolerance;
        }
        for (unsigned channel = 0; channel < 3; ++channel)
            next |= upper[channel] - lower[channel] > c.maxRadianceRange;
        refine = next || upper[3] - lower[3] > c.maxTransmittanceRange;
        return {};
    }

    // Logical worst case: half-resolution raw remains unchanged; resolved and
    // history are full resolution, and every output pixel may reintegrate.
    // It grants no physical allocation coverage or native execution authority.
    static CloudResult describeResources(const CloudReconstructionConfig& reconstruction,
        const CloudRenderConfig& c,
        rhi::Extent2D extent,
        std::uint64_t assetBytes,
        std::uint32_t uniformBytes,
        CloudReconstructionResources& output) noexcept
    {
        output = {};
        if (const auto result = validate(reconstruction); !result.success)
            return result;
        if (c.resolutionDivisor != 2u)
            return CloudResult::failure("CLOUD-RECONSTRUCTION-RESOLUTION");
        CloudExecutionPlan base;
        if (const auto result =
                CloudExecutionPlanner::describeResources(c, extent, assetBytes, uniformBytes, base);
            !result.success)
            return result;
        CloudReconstructionResources next;
        next.sampleContractVersion = reconstruction.contractVersion;
        next.resolvedCount = std::uint64_t(extent.width) * extent.height;
        if (next.resolvedCount > UINT32_MAX)
            return CloudResult::failure("CLOUD-SAMPLE-INDEX-OVERFLOW");
        next.resolvedBufferBytes =
            next.resolvedCount * CloudSampleStorage::stride(reconstruction.contractVersion);
        const auto perRay = base.densityEvaluationBound / base.sampleCount;
        if (!core::canMultiplyUnsigned(perRay, next.resolvedCount)
            || !core::canAddUnsigned(base.densityEvaluationBound, perRay * next.resolvedCount))
            return CloudResult::failure("CLOUD-WORK-OVERFLOW");
        next.densityEvaluationBound = base.densityEvaluationBound + perRay * next.resolvedCount;
        if (next.densityEvaluationBound > c.maxDensityEvaluations)
            return CloudResult::failure("CLOUD-RECONSTRUCTION-WORK-BUDGET");
        // The additional immutable classification uniform belongs to each
        // slot. It is not hidden in the original 368-byte parameter block.
        const auto resolvedSlotBytes =
            next.resolvedBufferBytes + sizeof(CloudReconstructionGpuConstants);
        const auto retainedWithoutResolved =
            base.retainedGpuBytes - base.sampleBufferBytes * c.framesInFlight;
        if (!core::canMultiplyUnsigned(resolvedSlotBytes, std::uint64_t(c.framesInFlight))
            || !core::canAddUnsigned(retainedWithoutResolved, resolvedSlotBytes * c.framesInFlight))
            return CloudResult::failure("CLOUD-MEMORY-OVERFLOW");
        next.retainedGpuBytes = retainedWithoutResolved + resolvedSlotBytes * c.framesInFlight;
        if (next.retainedGpuBytes > c.gpuBudgetBytes)
            return CloudResult::failure("CLOUD-RECONSTRUCTION-MEMORY-BUDGET");
        if (c.framesInFlight > UINT32_MAX / descriptorElementsPerSlot)
            return CloudResult::failure("CLOUD-RECONSTRUCTION-DESCRIPTOR-OVERFLOW");
        next.raw = base;
        next.dispatchGroups = {1u + (extent.width - 1u) / kCloudWorkgroupWidth,
            1u + (extent.height - 1u) / kCloudWorkgroupHeight,
            1u};
        output = next;
        return {};
    }

    // Numeric admission only. The original command profile separately proves
    // backend lowering, source custody, publication and completion. This never
    // promotes a public capability or infers support from a backend name.
    static CloudResult plan(const CloudReconstructionConfig& reconstruction,
        const CloudRenderConfig& c,
        rhi::Extent2D extent,
        std::uint64_t assetBytes,
        std::uint32_t uniformBytes,
        const rhi::DeviceCapabilities& capabilities,
        CloudReconstructionResources& output) noexcept
    {
        output = {};
        CloudReconstructionResources next;
        if (const auto result =
                describeResources(reconstruction, c, extent, assetBytes, uniformBytes, next);
            !result.success)
            return result;
        CloudExecutionPlan raw;
        if (const auto result =
                CloudExecutionPlanner::plan(c, extent, assetBytes, uniformBytes, capabilities, raw);
            !result.success)
            return result;
        if (const auto result = validateLimits(next, capabilities.computeResources);
            !result.success)
            return result;
        output = next;
        return {};
    }

    static CloudResult describeResources(const CloudReconstructionConfig& reconstruction,
        const CloudRenderConfig& c,
        rhi::Extent2D extent,
        std::uint64_t assetBytes,
        std::uint32_t uniformBytes,
        const rhi::ComputeResourceCapabilities& limits,
        CloudReconstructionResources& output) noexcept
    {
        return describeResourcesWithAdmission(
            reconstruction, c, extent, assetBytes, uniformBytes, limits, nullptr, output);
    }

    static CloudResult describeResources(const CloudReconstructionConfig& reconstruction,
        const CloudRenderConfig& c,
        rhi::Extent2D extent,
        std::uint64_t assetBytes,
        std::uint32_t uniformBytes,
        const rhi::ComputeResourceCapabilities& limits,
        const CloudOriginalDispatchAdmission& admission,
        CloudReconstructionResources& output) noexcept
    {
        return describeResourcesWithAdmission(
            reconstruction, c, extent, assetBytes, uniformBytes, limits, &admission, output);
    }

private:
    static CloudResult describeResourcesWithAdmission(
        const CloudReconstructionConfig& reconstruction,
        const CloudRenderConfig& c,
        rhi::Extent2D extent,
        std::uint64_t assetBytes,
        std::uint32_t uniformBytes,
        const rhi::ComputeResourceCapabilities& limits,
        const CloudOriginalDispatchAdmission* admission,
        CloudReconstructionResources& output) noexcept
    {
        output = {};
        CloudReconstructionResources next;
        if (const auto result =
                describeResources(reconstruction, c, extent, assetBytes, uniformBytes, next);
            !result.success)
            return result;
        CloudExecutionPlan raw;
        if (const auto result = admission
                ? CloudExecutionPlanner::describeResources(
                      c, extent, assetBytes, uniformBytes, limits, *admission, raw)
                : CloudExecutionPlanner::describeResources(
                      c, extent, assetBytes, uniformBytes, limits, raw);
            !result.success)
            return result;
        if (const auto result = validateLimits(next, limits, admission); !result.success)
            return result;
        output = next;
        return {};
    }

    static CloudResult validateLimits(const CloudReconstructionResources& next,
        const rhi::ComputeResourceCapabilities& limits,
        const CloudOriginalDispatchAdmission* admission = nullptr) noexcept
    {
        if (limits.maxStorageBufferBindingsPerStage < 4u
            || limits.maxUniformBufferBindingsPerStage < 2u)
            return CloudResult::failure("CLOUD-RECONSTRUCTION-BINDING-LIMIT");
        if (next.resolvedBufferBytes > limits.maxStorageBufferBindingSizeBytes)
            return CloudResult::failure("CLOUD-RECONSTRUCTION-STORAGE-LIMIT");
        if (admission) {
            if (const auto* id = admission->validate(next.dispatchGroups))
                return CloudResult::failure(id);
        } else if (next.dispatchGroups.width > limits.maxComputeWorkgroupCountX
            || next.dispatchGroups.height > limits.maxComputeWorkgroupCountY
            || next.dispatchGroups.depth > limits.maxComputeWorkgroupCountZ)
            return CloudResult::failure("CLOUD-RECONSTRUCTION-DISPATCH-LIMIT");
        return {};
    }
};

} // namespace arp::renderer::cloud

#include "renderer/volumetric_fog/fog_contract.h"

namespace arp::renderer::fog {

FogResult planFog(
    const FogConfig& c, rhi::Extent2D e, const rhi::DeviceCapabilities& caps, FogGrid& output)
{
    output = {};
    if (!e.width || !e.height || !std::isfinite(c.distance) || c.distance <= 0 || !c.maxLocalVolumes
        || c.framesInFlight < 2 || c.framesInFlight > 8 || !std::isfinite(c.historyWeight)
        || c.historyWeight < 0 || c.historyWeight >= 1 || !std::isfinite(c.mediumRejection)
        || c.mediumRejection <= 0 || !std::isfinite(c.depthRejection) || c.depthRejection <= 0
        || (c.quality != FogQuality::Low && c.quality != FogQuality::Medium
            && c.quality != FogQuality::High))
        return FogResult::failure("FOG-INVALID-CONFIG");
    const auto& limits = caps.computeResources;
    if (!caps.core.compute || !caps.core.graphics || !caps.features.computeDispatch
        || !caps.features.storageBufferBinding || !limits.sampledDepthTexture
        || (limits.mode != rhi::CapabilityMode::Native
            && limits.mode != rhi::CapabilityMode::Equivalent))
        return FogResult::failure("FOG-CAPABILITY-UNAVAILABLE");
    for (const auto feature :
        {rhi::EngineFeature::ComputeDispatch, rhi::EngineFeature::StorageBufferBinding}) {
        bool found = false;
        for (const auto& status : caps.executionStatuses)
            if (status.feature == feature)
                found = status.executable && rhi::isExecutableFallbackMode(status.mode);
        if (!found)
            return FogResult::failure("FOG-EXECUTION-UNAVAILABLE");
    }
    if (limits.maxComputeWorkgroupInvocations < 64 || limits.maxComputeWorkgroupSizeX < 8
        || limits.maxComputeWorkgroupSizeY < 8 || limits.maxStorageBufferBindingsPerStage < 4)
        return FogResult::failure("FOG-COMPUTE-LIMIT");
    const auto pixels = c.pixelsPerCell ? c.pixelsPerCell
        : c.quality == FogQuality::Low  ? 16u
        : c.quality == FogQuality::High ? 8u
                                        : 12u;
    FogGrid g;
    g.width = 1u + (e.width - 1u) / pixels;
    g.height = 1u + (e.height - 1u) / pixels;
    g.depth = c.depthSlices             ? c.depthSlices
        : c.quality == FogQuality::Low  ? 48u
        : c.quality == FogQuality::High ? 96u
                                        : 64u;
    const auto xy = std::uint64_t(g.width) * g.height;
    if (g.depth < 2 || xy > std::numeric_limits<std::uint32_t>::max() / g.depth)
        return FogResult::failure("FOG-GRID-OVERFLOW");
    g.cells = xy * g.depth;
    // Per slot: medium+velocity 32B, scattering 16B, history 32B, integral 16B.
    const auto slotBytes = g.cells * 96u + sizeof(FogConstants)
        + std::uint64_t(c.maxLocalVolumes) * sizeof(FogGpuVolume);
    const auto pixels64 = std::uint64_t(e.width) * e.height;
    const auto maxSlotBytes = std::numeric_limits<std::uint64_t>::max() / c.framesInFlight;
    if (slotBytes > maxSlotBytes || pixels64 > (maxSlotBytes - slotBytes) / 8u)
        return FogResult::failure("FOG-GRID-OVERFLOW");
    g.budgetBytes = (slotBytes + pixels64 * 8u) * c.framesInFlight;
    if (g.cells * 32u > limits.maxStorageBufferBindingSizeBytes
        || std::uint64_t(c.maxLocalVolumes) * sizeof(FogGpuVolume)
            > limits.maxStorageBufferBindingSizeBytes)
        return FogResult::failure("FOG-STORAGE-LIMIT");
    if (g.budgetBytes > c.gpuBudgetBytes)
        return FogResult::failure("FOG-MEMORY-BUDGET");
    output = g;
    return {};
}

FogResult buildFogVolumes(
    const scene3d::Scene3DRenderQueue& queue, core::pmr::Vector<FogGpuVolume>& output)
{
    output.clear();
    if (queue.hasFog() && !scene3d::validFog(queue.fog()))
        return FogResult::failure("FOG-INVALID-GLOBAL");
    for (const auto& v : queue.fogVolumes()) {
        if (!scene3d::validFog(v.desc))
            return FogResult::failure("FOG-INVALID-VOLUME");
        if (!scene3d::validFogTransform(v.worldTransform))
            return FogResult::failure("FOG-SINGULAR-TRANSFORM");
        const auto& t = v.worldTransform;
        const auto matrix = core::multiply(core::multiply(core::Mat4::translation(t.translation),
                                               core::Mat4::rotation(t.rotation)),
            core::Mat4::scale(t.scale));
        const auto inv = core::inverse(matrix);
        const auto check = core::multiply(matrix, inv);
        for (std::size_t i = 0; i < 16; ++i)
            if (!std::isfinite(check.m[i])
                || std::fabs(check.m[i] - (i % 5 == 0 ? 1.0f : 0.0f)) > 0.001f)
                return FogResult::failure("FOG-SINGULAR-TRANSFORM");
        const auto& d = v.desc;
        output.push_back({inv,
            {d.scatteringColor.x, d.scatteringColor.y, d.scatteringColor.z, d.density},
            {float(d.shape == scene3d::FogVolumeShape::Sphere),
                d.edgeSoftness,
                d.noiseScale,
                d.noiseStrength},
            {d.noiseVelocity.x, d.noiseVelocity.y, d.noiseVelocity.z, d.noiseContrast},
            {d.noiseSeed, 0, 0, 0}});
    }
    return {};
}

} // namespace arp::renderer::fog

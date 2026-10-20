#include "renderer/lighting/forward_plus.h"

#include <algorithm>
#include <cmath>

namespace arp::renderer::lighting {
namespace {

ForwardPlusDiagnostic makeDiagnostic(
    ForwardPlusDiagnosticCode code, std::string_view id, std::string_view message)
{
    ForwardPlusDiagnostic result;
    result.code = code;
    result.id.assign(id.data(), id.size());
    result.message.assign(message.data(), message.size());
    return result;
}

ForwardPlusValidationResult fail(
    ForwardPlusDiagnosticCode code, std::string_view id, std::string_view message)
{
    return {false, makeDiagnostic(code, id, message)};
}

bool finite(core::Vec3 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool overlapsDepth(
    const ForwardPlusProjectedLight& light, const ForwardPlusTileDepthRange& tile) noexcept
{
    return light.maxDepth >= tile.minDepth && light.minDepth <= tile.maxDepth;
}

bool overlapsTile(const ForwardPlusProjectedLight& light,
    std::uint32_t tileX,
    std::uint32_t tileY,
    std::uint32_t tileSize) noexcept
{
    const auto tileMinX = tileX * tileSize;
    const auto tileMinY = tileY * tileSize;
    const auto tileMaxX = tileMinX + tileSize - 1u;
    const auto tileMaxY = tileMinY + tileSize - 1u;
    return light.maxPixelX >= tileMinX && light.minPixelX <= tileMaxX && light.maxPixelY >= tileMinY
        && light.minPixelY <= tileMaxY;
}

bool appendTileList(const ForwardPlusConfig& config,
    std::uint32_t tileX,
    std::uint32_t tileY,
    bool conservative,
    const core::pmr::Vector<std::uint32_t>& tileIndices,
    core::pmr::Vector<ForwardPlusTileHeader>& headers,
    core::pmr::Vector<std::uint32_t>& indices,
    ForwardPlusCullResult& result)
{
    auto& header = headers[tileY * result.tileCountX + tileX];
    header.offset = static_cast<std::uint32_t>(indices.size());
    header.count = static_cast<std::uint32_t>(tileIndices.size());
    if (tileIndices.size() > config.maxLocalLightsPerTile) {
        result.overflow = {
            true,
            tileX,
            tileY,
            static_cast<std::uint32_t>(tileIndices.size()),
            config.maxLocalLightsPerTile,
            conservative,
        };
        result.diagnostic = makeDiagnostic(ForwardPlusDiagnosticCode::TileLightOverflow,
            "ARP-FORWARD-PLUS-TILE-OVERFLOW",
            "Forward+ tile light count exceeds the configured capacity; frame submission must stop.");
        return false;
    }
    indices.insert(indices.end(), tileIndices.begin(), tileIndices.end());
    return true;
}

} // namespace

const char* lightingPathName(LightingPath path) noexcept
{
    return path == LightingPath::ForwardPlus ? "ForwardPlus" : "LegacyForward8";
}

bool parseLightingPath(std::string_view text, LightingPath& path) noexcept
{
    if (text == "ForwardPlus" || text == "forward-plus" || text == "forward_plus") {
        path = LightingPath::ForwardPlus;
        return true;
    }
    if (text == "LegacyForward8" || text == "legacy-forward8" || text == "legacy_forward8") {
        path = LightingPath::LegacyForward8;
        return true;
    }
    return false;
}

std::uint32_t forwardPlusTileCount(std::uint32_t extent, std::uint32_t tileSize) noexcept
{
    return tileSize == 0u ? 0u : (extent + tileSize - 1u) / tileSize;
}

ForwardPlusDeviceLimits forwardPlusDeviceLimits(
    const rhi::DeviceCapabilities& capabilities) noexcept
{
    ForwardPlusCapabilityMode mode = ForwardPlusCapabilityMode::DiagnosticFailure;
    switch (capabilities.computeResources.mode) {
    case rhi::CapabilityMode::Native:
        mode = ForwardPlusCapabilityMode::Native;
        break;
    case rhi::CapabilityMode::Equivalent:
        mode = ForwardPlusCapabilityMode::Equivalent;
        break;
    case rhi::CapabilityMode::Degraded:
        mode = ForwardPlusCapabilityMode::Degraded;
        break;
    case rhi::CapabilityMode::DiagnosticFailure:
        break;
    }
    return {
        mode,
        capabilities.computeResources.sampledDepthTexture,
        capabilities.features.computeDispatch,
        capabilities.features.storageBufferBinding,
        capabilities.computeResources.maxComputeWorkgroupInvocations,
        capabilities.computeResources.maxComputeWorkgroupSizeX,
        capabilities.computeResources.maxComputeWorkgroupSizeY,
        capabilities.computeResources.maxComputeWorkgroupMemoryBytes,
        capabilities.computeResources.maxStorageBufferBindingsPerStage,
        capabilities.computeResources.maxStorageBufferBindingSizeBytes,
    };
}

ForwardPlusValidationResult validateForwardPlusConfig(const ForwardPlusConfig& config,
    rhi::Extent2D extent,
    const ForwardPlusDeviceLimits& limits) noexcept
{
    if (extent.width == 0u || extent.height == 0u) {
        return fail(ForwardPlusDiagnosticCode::InvalidExtent,
            "ARP-FORWARD-PLUS-INVALID-EXTENT",
            "Forward+ requires a non-zero render extent.");
    }
    if (config.tileSize == 0u || config.maxDirectionalLights == 0u || config.maxLocalLights == 0u
        || config.maxLocalLightsPerTile == 0u
        || config.maxLocalLightsPerTile > config.maxLocalLights) {
        return fail(ForwardPlusDiagnosticCode::InvalidConfig,
            "ARP-FORWARD-PLUS-INVALID-CONFIG",
            "Forward+ budgets must be non-zero and the per-tile budget cannot exceed the local-light budget.");
    }
    if (limits.mode == ForwardPlusCapabilityMode::Degraded
        || limits.mode == ForwardPlusCapabilityMode::DiagnosticFailure || !limits.sampledDepth
        || !limits.computeDispatch || !limits.storageBufferBinding) {
        return fail(ForwardPlusDiagnosticCode::CapabilityUnavailable,
            "ARP-FORWARD-PLUS-CAPABILITY-UNAVAILABLE",
            "Forward+ requires sampled depth, compute dispatch and storage-buffer bindings; no automatic legacy fallback is permitted.");
    }
    const std::uint64_t invocations = std::uint64_t(config.tileSize) * config.tileSize;
    if (invocations > limits.maxComputeWorkgroupInvocations
        || config.tileSize > limits.maxComputeWorkgroupSizeX
        || config.tileSize > limits.maxComputeWorkgroupSizeY) {
        return fail(ForwardPlusDiagnosticCode::WorkgroupLimitExceeded,
            "ARP-FORWARD-PLUS-WORKGROUP-LIMIT",
            "Forward+ tile dimensions exceed the queried compute workgroup limits.");
    }
    // forward_plus_cull.slang stores min/max depth, two flag arrays, two
    // inclusive-prefix arrays, and a scratch array used to make each scan
    // round race-free without atomics or subgroups.
    const std::uint64_t workgroupBytes =
        invocations * (sizeof(float) * 2u + sizeof(std::uint32_t) * 5u);
    if (workgroupBytes > limits.maxComputeWorkgroupMemoryBytes) {
        return fail(ForwardPlusDiagnosticCode::WorkgroupMemoryLimitExceeded,
            "ARP-FORWARD-PLUS-WORKGROUP-MEMORY-LIMIT",
            "Forward+ depth reduction and prefix-scan storage exceed the queried workgroup memory limit.");
    }
    constexpr std::uint32_t kRequiredStorageBindings = 6u;
    if (limits.maxStorageBufferBindingsPerStage < kRequiredStorageBindings) {
        return fail(ForwardPlusDiagnosticCode::StorageBindingLimitExceeded,
            "ARP-FORWARD-PLUS-STORAGE-BINDING-LIMIT",
            "Forward+ requires six storage-buffer bindings in the compute stage.");
    }
    const std::uint64_t tileCount =
        std::uint64_t(forwardPlusTileCount(extent.width, config.tileSize))
        * forwardPlusTileCount(extent.height, config.tileSize);
    const std::uint64_t largestBinding =
        std::max<std::uint64_t>(std::uint64_t(config.maxLocalLights) * sizeof(GpuLocalLight),
            tileCount * config.maxLocalLightsPerTile * sizeof(std::uint32_t));
    if (largestBinding > limits.maxStorageBufferBindingSizeBytes) {
        return fail(ForwardPlusDiagnosticCode::StorageBindingSizeExceeded,
            "ARP-FORWARD-PLUS-STORAGE-BINDING-SIZE",
            "Forward+ tile or light storage exceeds the queried storage-buffer binding-size limit.");
    }
    return {true, {}};
}

ForwardPlusValidationResult buildRenderLightPlan(LightingPath path,
    const ForwardPlusConfig& config,
    const core::pmr::Vector<RenderLight>& lights,
    RenderLightPlan& output)
{
    RenderLightPlan candidate;
    candidate.path = path;
    const auto localBudget = config.maxLocalLights;
    std::uint32_t legacyAcceptedCount = 0u;
    for (const auto& light : lights) {
        if (!finite(light.color) || !std::isfinite(light.intensity) || light.intensity < 0.0f
            || !finite(light.direction)) {
            return fail(ForwardPlusDiagnosticCode::InvalidLight,
                "ARP-FORWARD-PLUS-INVALID-LIGHT",
                "RenderLight contains non-finite values or a negative intensity.");
        }
        if (light.type == RenderLightType::Directional) {
            if (path == LightingPath::LegacyForward8 && legacyAcceptedCount >= 8u) {
                continue;
            }
            if (candidate.directionalLights.size() >= config.maxDirectionalLights) {
                return fail(ForwardPlusDiagnosticCode::DirectionalLightBudgetExceeded,
                    "ARP-FORWARD-PLUS-DIRECTIONAL-BUDGET",
                    "Directional light count exceeds the configured frame budget.");
            }
            const auto direction = core::normalizeOr(light.direction, {0.0f, -1.0f, 0.0f});
            candidate.directionalLights.push_back({
                {direction.x, direction.y, direction.z, light.castsShadow ? 1.0f : 0.0f},
                {light.color.x, light.color.y, light.color.z, light.intensity},
            });
            if (candidate.directionalLights.size() == 1u) {
                candidate.primaryDirectionalCastsShadow = light.castsShadow;
            }
            ++legacyAcceptedCount;
            continue;
        }
        if (!finite(light.position) || !std::isfinite(light.range) || light.range <= 0.0f
            || !std::isfinite(light.innerConeCos) || !std::isfinite(light.outerConeCos)
            || (light.type == RenderLightType::Spot && light.innerConeCos < light.outerConeCos)) {
            return fail(ForwardPlusDiagnosticCode::InvalidLight,
                "ARP-FORWARD-PLUS-INVALID-LOCAL-LIGHT",
                "Local lights require finite position/cone data, positive range and innerConeCos >= outerConeCos.");
        }
        if (path == LightingPath::LegacyForward8 && legacyAcceptedCount >= 8u) {
            continue;
        }
        if (candidate.localLights.size() >= localBudget) {
            return fail(ForwardPlusDiagnosticCode::LocalLightBudgetExceeded,
                "ARP-FORWARD-PLUS-LOCAL-LIGHT-BUDGET",
                "Local light count exceeds the selected lighting path budget.");
        }
        const auto direction = core::normalizeOr(light.direction, {0.0f, -1.0f, 0.0f});
        candidate.localLights.push_back({
            {light.position.x, light.position.y, light.position.z, light.range},
            {light.color.x, light.color.y, light.color.z, light.intensity},
            {direction.x, direction.y, direction.z, light.outerConeCos},
            {light.innerConeCos,
                static_cast<float>(static_cast<std::uint32_t>(light.type)),
                light.castsShadow ? 1.0f : 0.0f,
                0.0f},
        });
        ++legacyAcceptedCount;
    }
    output = std::move(candidate);
    return {true, {}};
}

ForwardPlusConservativeBudgetResult preflightForwardPlusConservativeTileBudget(
    const ForwardPlusConfig& config,
    const rhi::Extent2D extent,
    const core::pmr::Vector<GpuLocalLight>& localLights,
    const ForwardPlusCullView& view,
    std::uint16_t* tileCounts,
    const std::size_t tileCount)
{
    ForwardPlusConservativeBudgetResult result;
    const auto tileCountX = forwardPlusTileCount(extent.width, config.tileSize);
    const auto tileCountY = forwardPlusTileCount(extent.height, config.tileSize);
    const auto requiredTileCount = std::size_t(tileCountX) * tileCountY;
    if (extent.width == 0u || extent.height == 0u || config.tileSize == 0u || tileCounts == nullptr
        || tileCount != requiredTileCount) {
        result.diagnostic = makeDiagnostic(ForwardPlusDiagnosticCode::InvalidConfig,
            "ARP-FORWARD-PLUS-PREFLIGHT-SCRATCH",
            "Forward+ conservative preflight requires one scratch counter per tile.");
        return result;
    }
    std::fill_n(tileCounts, tileCount, std::uint16_t{0u});

    const auto rowDot = [&view](const std::uint32_t row, const core::Vec4& p) {
        const auto* values = view.viewRows + row * 4u;
        return values[0] * p.x + values[1] * p.y + values[2] * p.z + values[3] * p.w;
    };
    constexpr float kMinimumProjectionDepth = 0.00001f;
    for (const auto& light : localLights) {
        const core::Vec4 centerWorld{
            light.positionAndRange.x,
            light.positionAndRange.y,
            light.positionAndRange.z,
            1.0f,
        };
        const float centerX = rowDot(0u, centerWorld);
        const float centerY = rowDot(1u, centerWorld);
        const float centerZ = rowDot(2u, centerWorld);
        const float radius = light.positionAndRange.w;
        const float nearestDepth = centerZ - radius;
        if (centerZ + radius <= 0.0f) {
            continue;
        }
        const float projectionDepth = std::max(centerZ, kMinimumProjectionDepth);
        const float radiusDepth = std::max(nearestDepth, kMinimumProjectionDepth);
        const float centerPixelX =
            (centerX * view.projectionScale[0] / projectionDepth * 0.5f + 0.5f) * extent.width;
        const float centerPixelY =
            (centerY * view.projectionScale[1] / projectionDepth * 0.5f + 0.5f) * extent.height;
        const float radiusPixelX =
            std::abs(view.projectionScale[0]) * radius / radiusDepth * 0.5f * extent.width;
        const float radiusPixelY =
            std::abs(view.projectionScale[1]) * radius / radiusDepth * 0.5f * extent.height;
        const float minPixelX = centerPixelX - radiusPixelX;
        const float minPixelY = centerPixelY - radiusPixelY;
        const float maxPixelX = centerPixelX + radiusPixelX;
        const float maxPixelY = centerPixelY + radiusPixelY;
        if (maxPixelX < 0.0f || maxPixelY < 0.0f || minPixelX > extent.width
            || minPixelY > extent.height) {
            continue;
        }
        const auto tileForPixel = [&config](const float pixel, const std::uint32_t count) {
            const auto unclamped =
                static_cast<std::uint32_t>(std::floor(std::max(pixel, 0.0f) / config.tileSize));
            return std::min(unclamped, count - 1u);
        };
        const auto firstX = tileForPixel(minPixelX, tileCountX);
        const auto firstY = tileForPixel(minPixelY, tileCountY);
        const auto lastX = tileForPixel(maxPixelX, tileCountX);
        const auto lastY = tileForPixel(maxPixelY, tileCountY);
        for (std::uint32_t tileY = firstY; tileY <= lastY; ++tileY) {
            for (std::uint32_t tileX = firstX; tileX <= lastX; ++tileX) {
                ++tileCounts[tileY * tileCountX + tileX];
            }
        }
    }

    for (std::uint32_t tileY = 0u; tileY < tileCountY; ++tileY) {
        for (std::uint32_t tileX = 0u; tileX < tileCountX; ++tileX) {
            const auto required = tileCounts[tileY * tileCountX + tileX];
            if (required > config.maxLocalLightsPerTile) {
                result.overflow = {
                    true,
                    tileX,
                    tileY,
                    required,
                    config.maxLocalLightsPerTile,
                    true,
                };
                result.diagnostic = makeDiagnostic(ForwardPlusDiagnosticCode::TileLightOverflow,
                    "ARP-FORWARD-PLUS-TILE-OVERFLOW",
                    "Forward+ conservative tile light count exceeds capacity; frame graph contribution stopped before submission.");
                return result;
            }
        }
    }
    result.success = true;
    return result;
}

ForwardPlusCullResult ForwardPlusReferenceCuller::cull(const ForwardPlusConfig& config,
    rhi::Extent2D extent,
    const core::pmr::Vector<ForwardPlusTileDepthRange>& tileDepthRanges,
    const core::pmr::Vector<ForwardPlusProjectedLight>& projectedLights) const
{
    ForwardPlusCullResult result;
    result.tileCountX = forwardPlusTileCount(extent.width, config.tileSize);
    result.tileCountY = forwardPlusTileCount(extent.height, config.tileSize);
    const auto tileCount = std::uint64_t(result.tileCountX) * result.tileCountY;
    if (extent.width == 0u || extent.height == 0u || config.tileSize == 0u
        || tileDepthRanges.size() != tileCount) {
        result.diagnostic = makeDiagnostic(ForwardPlusDiagnosticCode::InvalidDepthRange,
            "ARP-FORWARD-PLUS-DEPTH-RANGE-COUNT",
            "Forward+ reference culler requires exactly one depth range per tile.");
        return result;
    }
    for (const auto& range : tileDepthRanges) {
        if (!std::isfinite(range.minDepth) || !std::isfinite(range.maxDepth)
            || range.minDepth < 0.0f || range.maxDepth > 1.0f || range.minDepth > range.maxDepth) {
            result.diagnostic = makeDiagnostic(ForwardPlusDiagnosticCode::InvalidDepthRange,
                "ARP-FORWARD-PLUS-INVALID-DEPTH-RANGE",
                "Forward+ depth ranges must be finite ordered values in [0,1].");
            return result;
        }
    }

    result.opaqueHeaders.resize(tileCount);
    result.conservativeHeaders.resize(tileCount);
    core::pmr::Vector<std::uint32_t> opaqueTileIndices;
    core::pmr::Vector<std::uint32_t> conservativeTileIndices;
    for (std::uint32_t tileY = 0u; tileY < result.tileCountY; ++tileY) {
        for (std::uint32_t tileX = 0u; tileX < result.tileCountX; ++tileX) {
            opaqueTileIndices.clear();
            conservativeTileIndices.clear();
            const auto tileIndex = tileY * result.tileCountX + tileX;
            for (const auto& light : projectedLights) {
                if (light.lightIndex >= config.maxLocalLights || light.minPixelX > light.maxPixelX
                    || light.minPixelY > light.maxPixelY || !std::isfinite(light.minDepth)
                    || !std::isfinite(light.maxDepth) || light.minDepth > light.maxDepth) {
                    result.diagnostic = makeDiagnostic(ForwardPlusDiagnosticCode::InvalidLight,
                        "ARP-FORWARD-PLUS-INVALID-PROJECTED-LIGHT",
                        "Projected light bounds are invalid or reference a light outside the configured budget.");
                    return result;
                }
                if (!overlapsTile(light, tileX, tileY, config.tileSize)) {
                    continue;
                }
                conservativeTileIndices.push_back(light.lightIndex);
                if (overlapsDepth(light, tileDepthRanges[tileIndex])) {
                    opaqueTileIndices.push_back(light.lightIndex);
                }
            }
            if (!appendTileList(config,
                    tileX,
                    tileY,
                    false,
                    opaqueTileIndices,
                    result.opaqueHeaders,
                    result.opaqueIndices,
                    result)) {
                return result;
            }
            if (!appendTileList(config,
                    tileX,
                    tileY,
                    true,
                    conservativeTileIndices,
                    result.conservativeHeaders,
                    result.conservativeIndices,
                    result)) {
                return result;
            }
        }
    }
    result.success = true;
    return result;
}

} // namespace arp::renderer::lighting

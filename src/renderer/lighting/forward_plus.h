#pragma once

#include "core/api.h"
#include "core/cpu_allocator.h"
#include "core/math.h"
#include "rhi/gpu_common.h"

#include <cstdint>
#include <string_view>

namespace arp::renderer::lighting {

enum class LightingPath : std::uint8_t {
    ForwardPlus,
    LegacyForward8,
};

enum class RenderLightType : std::uint32_t {
    Directional = 0,
    Point = 1,
    Spot = 2,
};

struct RenderLight {
    RenderLightType type = RenderLightType::Directional;
    core::Vec3 position;
    float range = 0.0f;
    core::Vec3 direction{0.0f, -1.0f, 0.0f};
    float outerConeCos = 0.0f;
    core::Vec3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    float innerConeCos = 0.0f;
    bool castsShadow = false;
};

// C++ half of shaders/common/forward_plus_lighting.slang.
struct alignas(16) GpuDirectionalLight {
    core::Vec4 directionAndShadow;
    core::Vec4 colorAndIntensity;
};

struct alignas(16) GpuLocalLight {
    core::Vec4 positionAndRange;
    core::Vec4 colorAndIntensity;
    core::Vec4 directionAndOuterConeCos;
    core::Vec4 innerConeCosAndType;
};

// GPU local-light records remain in world space. The cull pass transforms
// their conservative spheres with these explicit view rows, while fragment
// consumers evaluate the same records against world-space surfaces.
struct alignas(16) ForwardPlusCullConstants {
    std::uint32_t extent[2]{};
    std::uint32_t tileCount[2]{};
    std::uint32_t localLightCount = 0u;
    std::uint32_t maxLocalLightsPerTile = 0u;
    float projectionScale[2]{};
    float depthScale = 0.0f;
    float depthBias = 0.0f;
    float padding[2]{};
    float viewRows[16]{};
};

struct alignas(16) ForwardPlusShadingConstants {
    std::uint32_t tileCount[2]{};
    std::uint32_t directionalLightCount = 0u;
    std::uint32_t localLightCount = 0u;
    std::uint32_t lightingPath = 0u;
    std::uint32_t maxLocalLightsPerTile = 0u;
    std::uint32_t padding[2]{};
};

struct ForwardPlusCullView {
    float viewRows[16]{};
    float projectionScale[2]{};
    float depthScale = 0.0f;
    float depthBias = 0.0f;
};

struct ForwardPlusConfig {
    static constexpr std::uint32_t kDefaultTileSize = 16u;
    static constexpr std::uint32_t kDefaultMaxDirectionalLights = 4u;
    static constexpr std::uint32_t kDefaultMaxLocalLights = 512u;
    static constexpr std::uint32_t kDefaultMaxLocalLightsPerTile = 64u;

    std::uint32_t tileSize = kDefaultTileSize;
    std::uint32_t maxDirectionalLights = kDefaultMaxDirectionalLights;
    std::uint32_t maxLocalLights = kDefaultMaxLocalLights;
    std::uint32_t maxLocalLightsPerTile = kDefaultMaxLocalLightsPerTile;
};

enum class ForwardPlusCapabilityMode : std::uint8_t {
    Native,
    Equivalent,
    Degraded,
    DiagnosticFailure,
};

struct ForwardPlusDeviceLimits {
    ForwardPlusCapabilityMode mode = ForwardPlusCapabilityMode::DiagnosticFailure;
    bool sampledDepth = false;
    bool computeDispatch = false;
    bool storageBufferBinding = false;
    std::uint32_t maxComputeWorkgroupInvocations = 0u;
    std::uint32_t maxComputeWorkgroupSizeX = 0u;
    std::uint32_t maxComputeWorkgroupSizeY = 0u;
    std::uint32_t maxComputeWorkgroupMemoryBytes = 0u;
    std::uint32_t maxStorageBufferBindingsPerStage = 0u;
    std::uint64_t maxStorageBufferBindingSizeBytes = 0u;
};

enum class ForwardPlusDiagnosticCode : std::uint16_t {
    None,
    InvalidExtent,
    InvalidConfig,
    CapabilityUnavailable,
    WorkgroupLimitExceeded,
    WorkgroupMemoryLimitExceeded,
    StorageBindingLimitExceeded,
    StorageBindingSizeExceeded,
    DirectionalLightBudgetExceeded,
    LocalLightBudgetExceeded,
    InvalidLight,
    InvalidDepthRange,
    TileLightOverflow,
};

struct ForwardPlusDiagnostic {
    ForwardPlusDiagnosticCode code = ForwardPlusDiagnosticCode::None;
    core::pmr::String id;
    core::pmr::String message;
};

struct ForwardPlusValidationResult {
    bool success = false;
    ForwardPlusDiagnostic diagnostic;
};

struct RenderLightPlan {
    LightingPath path = LightingPath::ForwardPlus;
    core::pmr::Vector<GpuDirectionalLight> directionalLights;
    core::pmr::Vector<GpuLocalLight> localLights;
    bool primaryDirectionalCastsShadow = false;
};

struct ForwardPlusTileDepthRange {
    float minDepth = 0.0f;
    float maxDepth = 1.0f;
};

struct ForwardPlusProjectedLight {
    std::uint32_t lightIndex = 0u;
    std::uint32_t minPixelX = 0u;
    std::uint32_t minPixelY = 0u;
    std::uint32_t maxPixelX = 0u;
    std::uint32_t maxPixelY = 0u;
    float minDepth = 0.0f;
    float maxDepth = 1.0f;
};

struct ForwardPlusTileHeader {
    std::uint32_t offset = 0u;
    std::uint32_t count = 0u;
};

struct ForwardPlusOverflowTelemetry {
    bool overflowed = false;
    std::uint32_t tileX = 0u;
    std::uint32_t tileY = 0u;
    std::uint32_t requiredCount = 0u;
    std::uint32_t capacity = 0u;
    bool conservativeList = false;
};

struct ForwardPlusCullResult {
    bool success = false;
    std::uint32_t tileCountX = 0u;
    std::uint32_t tileCountY = 0u;
    core::pmr::Vector<ForwardPlusTileHeader> opaqueHeaders;
    core::pmr::Vector<std::uint32_t> opaqueIndices;
    core::pmr::Vector<ForwardPlusTileHeader> conservativeHeaders;
    core::pmr::Vector<std::uint32_t> conservativeIndices;
    ForwardPlusOverflowTelemetry overflow;
    ForwardPlusDiagnostic diagnostic;
};

struct ForwardPlusConservativeBudgetResult {
    bool success = false;
    ForwardPlusOverflowTelemetry overflow;
    ForwardPlusDiagnostic diagnostic;
};

ARP_API const char* lightingPathName(LightingPath path) noexcept;
ARP_API bool parseLightingPath(std::string_view text, LightingPath& path) noexcept;
ARP_API std::uint32_t forwardPlusTileCount(std::uint32_t extent, std::uint32_t tileSize) noexcept;
ARP_API ForwardPlusDeviceLimits forwardPlusDeviceLimits(
    const rhi::DeviceCapabilities& capabilities) noexcept;

ARP_API ForwardPlusValidationResult validateForwardPlusConfig(const ForwardPlusConfig& config,
    rhi::Extent2D extent,
    const ForwardPlusDeviceLimits& limits) noexcept;

ARP_API ForwardPlusValidationResult buildRenderLightPlan(LightingPath path,
    const ForwardPlusConfig& config,
    const core::pmr::Vector<RenderLight>& lights,
    RenderLightPlan& output);

ARP_API ForwardPlusConservativeBudgetResult preflightForwardPlusConservativeTileBudget(
    const ForwardPlusConfig& config,
    rhi::Extent2D extent,
    const core::pmr::Vector<GpuLocalLight>& localLights,
    const ForwardPlusCullView& view,
    std::uint16_t* tileCounts,
    std::size_t tileCount);

class ARP_API ForwardPlusReferenceCuller final {
public:
    ForwardPlusCullResult cull(const ForwardPlusConfig& config,
        rhi::Extent2D extent,
        const core::pmr::Vector<ForwardPlusTileDepthRange>& tileDepthRanges,
        const core::pmr::Vector<ForwardPlusProjectedLight>& projectedLights) const;
};

static_assert(sizeof(GpuDirectionalLight) == 32u);
static_assert(alignof(GpuDirectionalLight) == 16u);
static_assert(sizeof(GpuLocalLight) == 64u);
static_assert(alignof(GpuLocalLight) == 16u);
static_assert(sizeof(ForwardPlusTileHeader) == 8u);
static_assert(sizeof(ForwardPlusCullConstants) == 112u);
static_assert(alignof(ForwardPlusCullConstants) == 16u);
static_assert(sizeof(ForwardPlusShadingConstants) == 32u);
static_assert(alignof(ForwardPlusShadingConstants) == 16u);

} // namespace arp::renderer::lighting

#pragma once

#include "rhi/gpu_common.h"
#include "rhi/semantics/webgl_format_facts.h"
#include "rhi/semantics/webgl_memory_budget.h"
#include "rhi/semantics/webgl_runtime_identity.h"
#include "rhi/semantics/webgl_shader_precision.h"

#include <array>
#include <cstdint>
#include <limits>

namespace arp::rhi::semantics {

// WebGl1Graphics/1 describes admission facts, not native execution authority.
// P0 cannot promote graphics, push constants or NativeSync from probe booleans.
inline constexpr std::uint32_t kWebGl1GraphicsContractVersion = 1u;
inline constexpr std::uint32_t kWebGlRuntimeProbeVersion = 5u;

// Values returned by getContextAttributes; the enclosing queried bit is
// mandatory. Default false values are never evidence of a successful query.
struct WebGlContextAttributes {
    bool alpha = false;
    bool depth = false;
    bool stencil = false;
    bool antialias = false;
    bool premultipliedAlpha = false;
    bool preserveDrawingBuffer = false;

    constexpr bool matches(const WebGlContextAttributes& requested) const noexcept
    {
        return alpha == requested.alpha && depth == requested.depth && stencil == requested.stencil
            && antialias == requested.antialias
            && premultipliedAlpha == requested.premultipliedAlpha
            && preserveDrawingBuffer == requested.preserveDrawingBuffer;
    }
};

struct WebGlContextRequest {
    std::uint32_t contractVersion = 0u;
    WebGlContextAttributes attributes;
};

struct WebGlRuntimeLimits {
    std::uint32_t maxVertexAttributes = 0;
    std::uint32_t maxVertexUniformVectors = 0;
    std::uint32_t maxFragmentUniformVectors = 0;
    std::uint32_t maxVaryingVectors = 0;
    std::uint32_t maxVertexTextureUnits = 0;
    std::uint32_t maxFragmentTextureUnits = 0;
    std::uint32_t maxCombinedTextureUnits = 0;
    std::uint32_t maxTextureSize = 0;
    std::uint32_t maxCubeTextureSize = 0;
    std::uint32_t maxRenderbufferSize = 0;
    std::uint32_t maxViewportWidth = 0;
    std::uint32_t maxViewportHeight = 0;
};

struct WebGlRuntimeExtensions {
    bool uint32Indices = false;
    bool instancing = false;
    bool vertexArrayObjects = false;
    bool depthTextures = false;
    bool derivatives = false;
    bool fragmentDepth = false;
    bool srgbTextures = false;

    constexpr bool contains(const WebGlRuntimeExtensions& required) const noexcept
    {
        return (!required.uint32Indices || uint32Indices) && (!required.instancing || instancing)
            && (!required.vertexArrayObjects || vertexArrayObjects)
            && (!required.depthTextures || depthTextures) && (!required.derivatives || derivatives)
            && (!required.fragmentDepth || fragmentDepth)
            && (!required.srgbTextures || srgbTextures);
    }

    constexpr bool any() const noexcept
    {
        return uint32Indices || instancing || vertexArrayObjects || depthTextures || derivatives
            || fragmentDepth || srgbTextures;
    }
};

// Extension availability and native enablement do not imply caller consent.
// Default construction opts in to nothing; this contract never enables GL APIs.
struct WebGlExtensionOptIn {
    std::uint32_t contractVersion = 0;
    WebGlRuntimeExtensions extensions;
};

struct WebGlRuntimeIdentitySnapshot {
    std::array<bool, webGlIdentityFieldCount> queried{};
    std::array<core::pmr::String, webGlIdentityFieldCount> values;

    WebGlRuntimeIdentityView view() const noexcept
    {
        WebGlRuntimeIdentityView result;
        for (std::size_t index = 0u; index < result.size(); ++index)
            result[index] = {queried[index], {values[index].data(), values[index].size()}};
        return result;
    }
};

struct WebGlRuntimeProbe {
    std::uint32_t contractVersion = 0;
    bool contextAvailable = false;
    bool contextLost = false;
    std::uint32_t contextMajorVersion = 0;
    bool limitsQueried = false;
    bool precisionQueried = false;
    bool extensionsQueried = false;
    WebGlShaderPrecisionFacts precision;
    WebGlRuntimeLimits limits;
    WebGlRuntimeExtensions extensions;
    WebGlRuntimeExtensions enabledExtensions;
    std::uint32_t probeVersion = 0u;
    bool contextAttributesQueried = false;
    WebGlContextAttributes contextAttributes;
    WebGlFormatFacts formats;
    WebGlRuntimeIdentitySnapshot identity;
    std::uint32_t fragmentDepthQueryVersion = 0;
};

struct WebGlGraphicsRequirements {
    std::uint32_t contractVersion = kWebGl1GraphicsContractVersion;
    std::uint32_t vertexAttributes = 0;
    std::uint32_t vertexUniformVectors = 0;
    std::uint32_t fragmentUniformVectors = 0;
    std::uint32_t varyingVectors = 0;
    std::uint32_t vertexTextureUnits = 0;
    std::uint32_t fragmentTextureUnits = 0;
    std::uint32_t combinedTextureUnits = 0;
    std::uint32_t textureWidth = 0;
    std::uint32_t textureHeight = 0;
    std::uint32_t viewportWidth = 0;
    std::uint32_t viewportHeight = 0;
    std::uint32_t fragmentHighFloatPrecision = 0;
    bool uint32Indices = false;
    bool instancing = false;
    bool vertexArrayObjects = false;
    bool depthTextures = false;
    bool derivatives = false;
    std::uint32_t renderbufferWidth = 0u;
    std::uint32_t renderbufferHeight = 0u;
    std::uint32_t pushConstantSizeBytes = 0u;
    WebGlPrecisionRequirements precision;
    bool fragmentDepth = false;
    bool srgbTextures = false;
};

// Caller allowances, never native query results. Zero closes a category; the
// specification minima used to validate a runtime probe do not apply here.
struct WebGlGraphicsBudget {
    std::uint32_t contractVersion = 0u;
    WebGlRuntimeLimits ceilings;
    std::uint32_t maxPushConstantSizeBytes = 0u;
};

struct WebGlProfileAdmission {
    bool compatible = false;
    // A limit/extension match never authorizes a native draw.
    bool executable = false;
    const char* diagnostic = "RHI-WEBGL1-PROBE-MISSING";
};

class WebGlRuntimeProfile final {
public:
    static WebGlRuntimeProfile fromProbe(const WebGlRuntimeProbe& probe)
    {
        return WebGlRuntimeProfile(probe);
    }

    bool valid() const noexcept { return diagnostic_ == nullptr; }

    const char* diagnostic() const noexcept { return diagnostic_; }

    const WebGlRuntimeLimits& limits() const noexcept { return probe_.limits; }

    const WebGlRuntimeExtensions& extensions() const noexcept { return probe_.extensions; }

    const WebGlContextAttributes& contextAttributes() const noexcept
    {
        return probe_.contextAttributes;
    }

    const WebGlShaderPrecisionFacts& precision() const noexcept { return probe_.precision; }

    const WebGlFormatFacts& formats() const noexcept { return probe_.formats; }

    const WebGlRuntimeIdentitySnapshot& identity() const noexcept { return probe_.identity; }

    WebGlProfileAdmission admitMemory(const WebGlMemoryBudget& budget,
        const WebGlMemoryCounters& live,
        const WebGlMemoryRequest& request) const noexcept
    {
        if (!valid())
            return {false, false, diagnostic_};
        if (const auto* diagnostic = WebGlMemoryBudgetContract::assess(budget, live, request))
            return {false, false, diagnostic};
        return {true, false, "RHI-WEBGL1-EXECUTION-NOT-IMPLEMENTED"};
    }

    WebGlProfileAdmission admitFormat(const WebGlFormatRequest& request) const noexcept
    {
        if (!valid())
            return {false, false, diagnostic_};
        if (const auto* diagnostic = WebGlFormatContract::assess(probe_.formats, request))
            return {false, false, diagnostic};
        return {true, false, "RHI-WEBGL1-EXECUTION-NOT-IMPLEMENTED"};
    }

    WebGlProfileAdmission admitContext(const WebGlContextRequest& request) const noexcept
    {
        if (!valid())
            return {false, false, diagnostic_};
        if (request.contractVersion != 1u)
            return {false, false, "RHI-WEBGL1-CONTEXT-REQUEST-VERSION"};
        if (!probe_.contextAttributes.matches(request.attributes))
            return {false, false, "RHI-WEBGL1-CONTEXT-ATTRIBUTES-MISMATCH"};
        return {true, false, "RHI-WEBGL1-EXECUTION-NOT-IMPLEMENTED"};
    }

    WebGlProfileAdmission admit(const WebGlGraphicsRequirements& requirement,
        const WebGlExtensionOptIn& optIn = {}) const noexcept
    {
        if (!valid())
            return {false, false, diagnostic_};
        if (requirement.contractVersion != kWebGl1GraphicsContractVersion)
            return {false, false, "RHI-WEBGL1-CONTRACT-VERSION"};
        if (requirement.pushConstantSizeBytes > kL2AtomicPushConstantLimitBytes)
            return {false, false, "RHI-WEBGL1-PUSH-L2-LIMIT"};
        if (!withinLimits(requirement, probe_.limits))
            return {false, false, "RHI-WEBGL1-WORKLOAD-LIMIT"};
        // GLES counts a unit used by both stages once per stage towards the
        // combined limit. Use a wide sum so hostile requirements cannot wrap.
        if (requirement.combinedTextureUnits
            < std::uint64_t{requirement.vertexTextureUnits} + requirement.fragmentTextureUnits)
            return {false, false, "RHI-WEBGL1-TEXTURE-UNIT-ACCOUNTING"};
        if (const auto* diagnostic =
                WebGlShaderPrecisionContract::assess(probe_.precision, requirement.precision))
            return {false, false, diagnostic};
        const auto& highFloat =
            probe_.precision.fragment[static_cast<std::size_t>(WebGlNumericPrecision::HighFloat)];
        if (requirement.fragmentHighFloatPrecision
            > static_cast<std::uint32_t>(highFloat.precision))
            return {false, false, "RHI-WEBGL1-FRAGMENT-PRECISION"};
        const WebGlRuntimeExtensions required{requirement.uint32Indices,
            requirement.instancing,
            requirement.vertexArrayObjects,
            requirement.depthTextures,
            requirement.derivatives,
            requirement.fragmentDepth,
            requirement.srgbTextures};
        if (optIn.contractVersion > 2u)
            return {false, false, "RHI-WEBGL1-EXTENSION-CONTRACT-VERSION"};
        if ((required.fragmentDepth || optIn.extensions.fragmentDepth)
            && optIn.contractVersion != 2u)
            return {false, false, "RHI-WEBGL1-FRAGMENT-DEPTH-PROFILE-REQUIRED"};
        if ((required.srgbTextures || optIn.extensions.srgbTextures) && optIn.contractVersion != 2u)
            return {false, false, "RHI-WEBGL1-SRGB-PROFILE-REQUIRED"};
        if (required.fragmentDepth && probe_.fragmentDepthQueryVersion != 1u)
            return {false, false, "RHI-WEBGL1-FRAGMENT-DEPTH-QUERY-REQUIRED"};
        if (!probe_.extensions.contains(required))
            return {false, false, "RHI-WEBGL1-EXTENSION-UNAVAILABLE"};
        if (required.any() && (!optIn.contractVersion || !optIn.extensions.contains(required)))
            return {false, false, "RHI-WEBGL1-EXTENSION-OPT-IN-REQUIRED"};
        if (!probe_.enabledExtensions.contains(required))
            return {false, false, "RHI-WEBGL1-EXTENSION-NOT-ENABLED"};
        return {true, false, "RHI-WEBGL1-EXECUTION-NOT-IMPLEMENTED"};
    }

    WebGlProfileAdmission admitWithin(const WebGlGraphicsRequirements& requirement,
        const WebGlGraphicsBudget& budget,
        const WebGlExtensionOptIn& optIn = {}) const noexcept
    {
        if (!valid())
            return {false, false, diagnostic_};
        if (budget.contractVersion != 1u)
            return {false, false, "RHI-WEBGL1-BUDGET-VERSION"};
        const auto admission = admit(requirement, optIn);
        if (!admission.compatible)
            return admission;
        if (!withinLimits(requirement, budget.ceilings)
            || requirement.pushConstantSizeBytes > budget.maxPushConstantSizeBytes)
            return {false, false, "RHI-WEBGL1-BUDGET-LIMIT"};
        return admission;
    }

private:
    static constexpr bool withinLimits(
        const WebGlGraphicsRequirements& requirement, const WebGlRuntimeLimits& limits) noexcept
    {
        return requirement.vertexAttributes <= limits.maxVertexAttributes
            && requirement.vertexUniformVectors <= limits.maxVertexUniformVectors
            && requirement.fragmentUniformVectors <= limits.maxFragmentUniformVectors
            && requirement.varyingVectors <= limits.maxVaryingVectors
            && requirement.vertexTextureUnits <= limits.maxVertexTextureUnits
            && requirement.fragmentTextureUnits <= limits.maxFragmentTextureUnits
            && requirement.combinedTextureUnits <= limits.maxCombinedTextureUnits
            && requirement.textureWidth <= limits.maxTextureSize
            && requirement.textureHeight <= limits.maxTextureSize
            && requirement.viewportWidth <= limits.maxViewportWidth
            && requirement.viewportHeight <= limits.maxViewportHeight
            && requirement.renderbufferWidth <= limits.maxRenderbufferSize
            && requirement.renderbufferHeight <= limits.maxRenderbufferSize;
    }

    explicit WebGlRuntimeProfile(const WebGlRuntimeProbe& probe) : probe_(probe)
    {
        if (probe.contractVersion != kWebGl1GraphicsContractVersion) {
            diagnostic_ = "RHI-WEBGL1-CONTRACT-VERSION";
        } else if (probe.probeVersion != kWebGlRuntimeProbeVersion) {
            diagnostic_ = "RHI-WEBGL1-PROBE-VERSION";
        } else if (!probe.contextAvailable || probe.contextMajorVersion != 1u) {
            diagnostic_ = "RHI-WEBGL1-CONTEXT-VERSION";
        } else if (probe.contextLost) {
            diagnostic_ = "RHI-WEBGL1-CONTEXT-LOST";
        } else if (!probe.limitsQueried || !probe.precisionQueried || !probe.extensionsQueried
            || !probe.contextAttributesQueried) {
            diagnostic_ = "RHI-WEBGL1-PROBE-INCOMPLETE";
        } else if (!probe.extensions.contains(probe.enabledExtensions)) {
            diagnostic_ = "RHI-WEBGL1-EXTENSION-PROBE-INCONSISTENT";
        } else if (probe.fragmentDepthQueryVersion > 1u
            || ((probe.extensions.fragmentDepth || probe.enabledExtensions.fragmentDepth)
                && probe.fragmentDepthQueryVersion != 1u)) {
            diagnostic_ = "RHI-WEBGL1-FRAGMENT-DEPTH-PROBE-INVALID";
        } else if (const auto* identityDiagnostic =
                       WebGlRuntimeIdentityContract::validate(probe.identity.view())) {
            diagnostic_ = identityDiagnostic;
        } else if (const auto* precisionDiagnostic =
                       WebGlShaderPrecisionContract::validateProbe(probe.precision)) {
            diagnostic_ = precisionDiagnostic;
        } else if (const auto* formatDiagnostic = WebGlFormatContract::validate(probe.formats)) {
            diagnostic_ = formatDiagnostic;
        } else {
            // WebGL 1.0 / GLES 2.0 specification-guaranteed minima. In
            // particular MAX_VERTEX_TEXTURE_IMAGE_UNITS may legally be zero.
            const auto& l = probe.limits;
            // These getParameter facts originate as GLint, not arbitrary
            // unsigned caller ceilings. Reject wrapped negative query values.
            for (const auto value : std::array{l.maxVertexAttributes,
                     l.maxVertexUniformVectors,
                     l.maxFragmentUniformVectors,
                     l.maxVaryingVectors,
                     l.maxVertexTextureUnits,
                     l.maxFragmentTextureUnits,
                     l.maxCombinedTextureUnits,
                     l.maxTextureSize,
                     l.maxCubeTextureSize,
                     l.maxRenderbufferSize,
                     l.maxViewportWidth,
                     l.maxViewportHeight}) {
                if (value > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
                    diagnostic_ = "RHI-WEBGL1-LIMIT-PROBE-INVALID";
                    return;
                }
            }
            if (l.maxVertexAttributes < 8u || l.maxVertexUniformVectors < 128u
                || l.maxFragmentUniformVectors < 16u || l.maxVaryingVectors < 8u
                || l.maxFragmentTextureUnits < 8u || l.maxCombinedTextureUnits < 8u
                || l.maxTextureSize < 64u || l.maxCubeTextureSize < 16u
                || l.maxRenderbufferSize == 0u || l.maxViewportWidth == 0u
                || l.maxViewportHeight == 0u || l.maxCombinedTextureUnits < l.maxVertexTextureUnits
                || l.maxCombinedTextureUnits < l.maxFragmentTextureUnits)
                diagnostic_ = "RHI-WEBGL1-LIMIT-PROBE-INVALID";
        }
    }

    WebGlRuntimeProbe probe_;
    const char* diagnostic_ = nullptr;
};

} // namespace arp::rhi::semantics

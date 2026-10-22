#pragma once

#include "core/diagnostics/diagnostic_builder.h"
#include "rhi/gpu_common.h"

#include <utility>

namespace arp::rhi {

enum class DescriptorBackendModel {
    NullRecording,
    VulkanDescriptorSet,
    MetalArgumentBinding,
    BackendOwnedDescriptorGroup
};

enum class EngineFeature {
    PushConstants,
    SecondaryCommandBuffer,
    DescriptorTable,
    TransientAliasing,
    TimelineSync,
    ComputeDispatch,
    StorageTextureBinding,
    NativeSync,
    /// Multi-subpass render passes with tile-local input attachment reads.
    /// Native on Null and Vulkan (Phase B: real multi-subpass VkRenderPass).
    /// Metal is native only for shader packages carrying the verified
    /// framebuffer-fetch reflection contract. WebGPU uses an explicit
    /// separate-pass lowering when its shader/resource contract is present.
    SubpassInputAttachment,
    /// Depth input-attachment reads are a separate capability because a
    /// backend may support color input attachments while rejecting depth
    /// input reads during native render-pass lowering.
    SubpassDepthInputAttachment,
    /// Per-format storage-texture capability for Rg16Float. Probed per-format:
    /// Vulkan queries VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT, WebGPU probes the
    /// browser device inside an error scope, and Metal probes the exact
    /// storage-texture shader/pixel-format contract at device creation.
    /// Binary Native/DiagnosticFailure - no degraded fallback because WGSL
    /// storage-texture formats are compile-time constants.
    Rg16FloatStorageTexture,
    /// Per-format storage-texture capability for Rgba16Float. Binary
    /// Native/DiagnosticFailure with no implicit format substitution.
    Rgba16FloatStorageTexture,
    /// Storage buffer (SSBO) binding in compute/graphics shaders. Required by
    /// DDGI Phase 3 probe-state buffer (per-probe relocation offset/status,
    /// authored by a dedicated ProbeRelocate compute pass and read by
    /// probe-update + resolve). Native on Vulkan (SSBO is core since 1.0) and
    /// Null (reference validator). WebGPU storage buffers are core
    /// (WGPUBufferUsage_STORAGE), promoted alongside storageTextureBinding.
    /// Metal is native when its direct buffer-binding reflection contract is
    /// present. Binary Native/DiagnosticFailure.
    StorageBufferBinding,
    /// Scene geometry ray query via software BVH. Required by DDGI Phase 4:
    /// probe tracing must intersect the full scene (terrain + static meshes)
    /// instead of the analytic terrain-only ray march. Lowers to a CPU-built
    /// flat AABB tree (BVH) uploaded as two SSBOs (nodes + triangle leaves)
    /// and traversed in a compute shader with Möller-Trumbore intersection.
    /// Reuses StorageBufferBinding + ComputeDispatch as the underlying RHI
    /// substrate (no GpuAccelerationStructure resource type is introduced).
    /// Native when both StorageBufferBinding and ComputeDispatch are Native;
    /// otherwise DiagnosticFailure (no degraded mode - the analytic terrain
    /// fallback is handled in-shader via a per-frame BVH-empty flag, not as a
    /// capability lowering). Metal requires the native compute, SSBO, and
    /// shader probe contract; WebGPU fails closed through its runtime gates;
    /// Null is Native (reference validator). Binary Native/DiagnosticFailure.
    SceneGeometryRayQuery,
    /// Indirect draw via drawIndexedIndirect: a GPU-written buffer of
    /// DrawIndexedIndirectCommand records drives drawIndexed calls without
    /// CPU involvement. Required by P5 GPU-driven vegetation cull. Native on
    /// Vulkan (core since 1.0), WebGPU, Metal, and Null (reference validator).
    /// WebGPU and Metal are Native for this fixed-count command; GPU-counted
    /// indirect draw is a separate capability.
    IndirectDraw,
    /// Indirect draw with GPU-provided draw count. Vulkan uses the native
    /// count command; Metal uses a bounded shader-mask equivalent gated by
    /// MetalGpuCountedIndirect/1 reflection. WebGPU remains fail-closed
    /// because its core API has no equivalent count-buffer command.
    MultiDrawIndirectCount
};

enum class FallbackMode {
    Native,
    Emulated,
    CpuReplay,
    Degraded,
    DiagnosticFailure,
    ExtensionOptIn // 可选扩展，上层 isSupported() 查询后 opt-in
};

inline constexpr bool isExecutableFallbackMode(FallbackMode mode) noexcept
{
    switch (mode) {
    case FallbackMode::Native:
    case FallbackMode::Emulated:
        return true;
    case FallbackMode::CpuReplay:
    case FallbackMode::Degraded:
    case FallbackMode::DiagnosticFailure:
    case FallbackMode::ExtensionOptIn:
        return false;
    }
    return false;
}

/// One mapping for feature reads, writes and closed-enum validation.
class EngineFeatureCapabilityAccess final {
public:
    static bool known(EngineFeature feature) noexcept { return member(feature) != nullptr; }

    static bool read(const RuntimeFeatureCapabilities& caps, EngineFeature feature) noexcept
    {
        const auto field = member(feature);
        return field != nullptr && caps.*field;
    }

    static void write(RuntimeFeatureCapabilities& caps, EngineFeature feature, bool value) noexcept
    {
        if (const auto field = member(feature))
            caps.*field = value;
    }

    static void disableAll(RuntimeFeatureCapabilities& caps) noexcept
    {
        for (const auto& entry : entries_)
            caps.*(entry.field) = false;
        caps.subpassInputAttachmentDegraded = false;
        caps.subpassDepthInputAttachmentDegraded = false;
    }

private:
    using Field = bool RuntimeFeatureCapabilities::*;

    struct Entry {
        EngineFeature feature;
        Field field;
    };

    inline static constexpr Entry entries_[] = {
        {EngineFeature::PushConstants, &RuntimeFeatureCapabilities::pushConstants},
        {EngineFeature::SecondaryCommandBuffer,
            &RuntimeFeatureCapabilities::secondaryCommandBuffers},
        {EngineFeature::DescriptorTable, &RuntimeFeatureCapabilities::descriptorTable},
        {EngineFeature::TransientAliasing, &RuntimeFeatureCapabilities::transientAliasing},
        {EngineFeature::TimelineSync, &RuntimeFeatureCapabilities::timelineSync},
        {EngineFeature::ComputeDispatch, &RuntimeFeatureCapabilities::computeDispatch},
        {EngineFeature::StorageTextureBinding, &RuntimeFeatureCapabilities::storageTextureBinding},
        {EngineFeature::NativeSync, &RuntimeFeatureCapabilities::nativeSync},
        {EngineFeature::SubpassInputAttachment,
            &RuntimeFeatureCapabilities::subpassInputAttachment},
        {EngineFeature::SubpassDepthInputAttachment,
            &RuntimeFeatureCapabilities::subpassDepthInputAttachment},
        {EngineFeature::Rg16FloatStorageTexture,
            &RuntimeFeatureCapabilities::rg16FloatStorageTexture},
        {EngineFeature::Rgba16FloatStorageTexture,
            &RuntimeFeatureCapabilities::rgba16FloatStorageTexture},
        {EngineFeature::StorageBufferBinding, &RuntimeFeatureCapabilities::storageBufferBinding},
        {EngineFeature::SceneGeometryRayQuery, &RuntimeFeatureCapabilities::sceneGeometryRayQuery},
        {EngineFeature::IndirectDraw, &RuntimeFeatureCapabilities::indirectDraw},
        {EngineFeature::MultiDrawIndirectCount,
            &RuntimeFeatureCapabilities::multiDrawIndirectCount},
    };

    static Field member(EngineFeature feature) noexcept
    {
        for (const auto& entry : entries_)
            if (entry.feature == feature)
                return entry.field;
        return nullptr;
    }
};

/// FallbackStatusAdmission/1 checks structural consistency, never provenance.
class FallbackStatusAdmission final {
public:
    static constexpr std::uint32_t contractVersion = 1u;

    static constexpr bool knownMode(FallbackMode mode) noexcept
    {
        switch (mode) {
        case FallbackMode::Native:
        case FallbackMode::Emulated:
        case FallbackMode::CpuReplay:
        case FallbackMode::Degraded:
        case FallbackMode::DiagnosticFailure:
        case FallbackMode::ExtensionOptIn:
            return true;
        }
        return false;
    }

    static constexpr const char* backendDiagnostic(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return nullptr;
        case Backend::WebGL:
            return "RHI-WEBGL1-FALLBACK-EXECUTION-UNAVAILABLE";
        }
        return "RHI-FALLBACK-BACKEND-UNKNOWN";
    }

    static constexpr const char* modeDiagnostic(FallbackMode mode) noexcept
    {
        switch (mode) {
        case FallbackMode::Native:
        case FallbackMode::Emulated:
        case FallbackMode::Degraded:
        case FallbackMode::DiagnosticFailure:
            return nullptr;
        case FallbackMode::CpuReplay:
            return "RHI-FALLBACK-CPU-REPLAY-EXPLICIT-CONTRACT-REQUIRED";
        case FallbackMode::ExtensionOptIn:
            return "RHI-FALLBACK-EXTENSION-OPT-IN-REQUIRED";
        }
        return "RHI-FALLBACK-MODE-UNKNOWN";
    }

    static const char* diagnostic(
        Backend backend, const EngineFeatureExecutionStatus& status) noexcept
    {
        if (const auto* id = backendDiagnostic(backend))
            return id;
        if (!EngineFeatureCapabilityAccess::known(status.feature))
            return "RHI-FALLBACK-FEATURE-UNKNOWN";
        if (const auto* id = modeDiagnostic(status.mode))
            return id;
        if (status.executable != isExecutableFallbackMode(status.mode))
            return "RHI-FALLBACK-EXECUTION-UNAVAILABLE";
        return nullptr;
    }
};

inline ExecutionStatusTelemetrySummary summarizeExecutionStatuses(
    const std::pmr::vector<EngineFeatureExecutionStatus>& features,
    const std::pmr::vector<QueryExecutionStatus>& queries);

struct FeatureFallbackDecision {
    EngineFeature feature = EngineFeature::PushConstants;
    Backend backend = Backend::Null;
    FallbackMode mode = FallbackMode::Native;
    bool upperLayerVisible = false;
};

/// Bridges the EngineFeature declaration model with DeviceCapabilities.
/// Upper layers call this to query whether a given feature is supported
/// at runtime, closing the declaration→query→branch loop.
inline bool isFeatureSupported(const DeviceCapabilities& caps, EngineFeature feature) noexcept
{
    return EngineFeatureCapabilityAccess::read(caps.features, feature);
}

/// Reconcile planner status with the final runtime-probed capability fields.
/// Backends may apply native probes after the shared fallback plan; a mode
/// cannot remain executable when its final payload is unavailable.
inline void synchronizeExecutionStatuses(DeviceCapabilities& caps)
{
    for (auto& status : caps.executionStatuses) {
        const bool capability = isFeatureSupported(caps, status.feature);
        const auto* diagnostic = FallbackStatusAdmission::diagnostic(caps.backend, status);
        if (diagnostic != nullptr || !isExecutableFallbackMode(status.mode)) {
            status.executable = false;
            EngineFeatureCapabilityAccess::write(caps.features, status.feature, false);
            if (diagnostic != nullptr) {
                status.telemetryRequired = true;
                if (status.diagnostics.find(diagnostic) == status.diagnostics.npos)
                    core::appendDiagnostic(status.diagnostics, diagnostic);
                if (FallbackStatusAdmission::backendDiagnostic(caps.backend) != nullptr
                    || !EngineFeatureCapabilityAccess::known(status.feature)
                    || !FallbackStatusAdmission::knownMode(status.mode)
                    || isExecutableFallbackMode(status.mode)
                    || status.mode == FallbackMode::Degraded)
                    status.mode = FallbackMode::DiagnosticFailure;
            }
            if (status.feature == EngineFeature::SubpassInputAttachment
                && status.mode != FallbackMode::Degraded)
                caps.features.subpassInputAttachmentDegraded = false;
            if (status.feature == EngineFeature::SubpassDepthInputAttachment
                && status.mode != FallbackMode::Degraded)
                caps.features.subpassDepthInputAttachmentDegraded = false;
            continue;
        }
        if (!capability) {
            status.mode = FallbackMode::DiagnosticFailure;
            status.executable = false;
            status.telemetryRequired = true;
            core::appendDiagnostic(status.diagnostics, "RHI-FALLBACK-EXECUTION-UNAVAILABLE");
        }
    }
    caps.statusTelemetry =
        summarizeExecutionStatuses(caps.executionStatuses, caps.queryExecutionStatuses);
}

/// Publish the shared query lifecycle surface from the final runtime probes.
/// A false capability is represented as Unsupported; it is never converted
/// into a fabricated CPU-side sample. QueryPool instances may refine this
/// status with per-index Recorded/Pending/Available transitions.
inline void synchronizeQueryExecutionStatuses(DeviceCapabilities& caps)
{
    caps.queryExecutionStatuses.clear();
    caps.queryExecutionStatuses.reserve(2u);

    QueryExecutionStatus timestamp;
    timestamp.type = QueryType::Timestamp;
    timestamp.creationSupported = caps.features.timestampQueries;
    timestamp.resetSupported = timestamp.creationSupported;
    timestamp.asynchronousReadbackSupported = timestamp.creationSupported;
    timestamp.completionGatedLifetimeRequired = timestamp.creationSupported;
    timestamp.state = timestamp.creationSupported ? QueryExecutionState::NotRecorded
                                                  : QueryExecutionState::Unsupported;
    timestamp.diagnostics = timestamp.creationSupported
        ? "timestamp query lifecycle is capability-gated"
        : "timestamp query lowering is unavailable";
    caps.queryExecutionStatuses.push_back(std::move(timestamp));

    QueryExecutionStatus statistics;
    statistics.type = QueryType::PipelineStatistics;
    statistics.creationSupported = caps.features.pipelineStatisticsQueries;
    statistics.resetSupported = statistics.creationSupported;
    statistics.asynchronousReadbackSupported = statistics.creationSupported;
    statistics.completionGatedLifetimeRequired = statistics.creationSupported;
    statistics.state = statistics.creationSupported ? QueryExecutionState::NotRecorded
                                                    : QueryExecutionState::Unsupported;
    statistics.diagnostics = statistics.creationSupported
        ? "pipeline-statistics query lifecycle is capability-gated"
        : "pipeline-statistics query lowering is unavailable";
    caps.queryExecutionStatuses.push_back(std::move(statistics));
    caps.statusTelemetry =
        summarizeExecutionStatuses(caps.executionStatuses, caps.queryExecutionStatuses);
}

inline ExecutionStatusTelemetrySummary summarizeExecutionStatuses(
    const std::pmr::vector<EngineFeatureExecutionStatus>& features,
    const std::pmr::vector<QueryExecutionStatus>& queries)
{
    ExecutionStatusTelemetrySummary summary;
    for (const auto& status : features) {
        switch (status.mode) {
        case FallbackMode::Native:
            ++summary.nativeFeatureCount;
            break;
        case FallbackMode::Emulated:
            ++summary.equivalentFeatureCount;
            break;
        case FallbackMode::CpuReplay:
            ++summary.cpuReplayFeatureCount;
            break;
        case FallbackMode::Degraded:
            ++summary.degradedFeatureCount;
            break;
        case FallbackMode::DiagnosticFailure:
        case FallbackMode::ExtensionOptIn:
            ++summary.diagnosticFailureFeatureCount;
            break;
        }
    }
    for (const auto& status : queries) {
        switch (status.state) {
        case QueryExecutionState::Unsupported:
            ++summary.unsupportedQueryCount;
            break;
        case QueryExecutionState::NotRecorded:
            ++summary.notRecordedQueryCount;
            break;
        case QueryExecutionState::Recorded:
            ++summary.recordedQueryCount;
            break;
        case QueryExecutionState::Pending:
            ++summary.pendingQueryCount;
            break;
        case QueryExecutionState::Available:
            ++summary.availableQueryCount;
            break;
        case QueryExecutionState::DiagnosticFailure:
            ++summary.diagnosticFailureQueryCount;
            break;
        }
    }
    return summary;
}

} // namespace arp::rhi

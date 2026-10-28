#include "rhi/frontend/fallback_planner.h"

#include "core/diagnostics/diagnostic_builder.h"
#include "core/memory/cpu_allocator.h"

#include <algorithm>

namespace arp::rhi {

namespace {

EngineFeaturePlanResult makeSuccess(const EngineFeatureDecisionInput& input, bool telemetryRequired)
{
    EngineFeaturePlanResult result;
    result.success = true;
    result.feature = input.feature;
    result.mode = input.mode;
    result.upperLayerVisible = false;
    result.telemetryRequired = telemetryRequired;
    result.explicitOptInRequired = input.mode == FallbackMode::ExtensionOptIn;
    return result;
}

EngineFeaturePlanResult makeFailure(
    const EngineFeatureDecisionInput& input, FallbackMode mode, const std::pmr::string& diagnostics)
{
    EngineFeaturePlanResult result;
    result.success = false;
    result.feature = input.feature;
    result.mode = mode;
    result.upperLayerVisible = false;
    result.telemetryRequired = true;
    result.explicitOptInRequired = false;
    result.diagnostics = diagnostics;
    return result;
}

std::pmr::string makeFeatureDiagnostics(
    EngineFeature feature, FallbackMode mode, const std::pmr::string& reason)
{
    std::pmr::string diagnostics = engineFeatureName(feature);
    diagnostics += " uses ";
    diagnostics += fallbackModeName(mode);
    if (!reason.empty()) {
        diagnostics += ": ";
        diagnostics += reason;
    }
    return diagnostics;
}

} // namespace

void EngineFallbackPlan::applyToImpl(DeviceCapabilities& caps, bool restrictionsOnly) const
{
    // Build the entire CPU result before changing the supplied snapshot. All
    // allocations use its explicit destination resource; equal-resource move
    // publication does not allocate. This is a cold report operation.
    auto* resource = caps.executionStatuses.get_allocator().resource();
    core::pmr::Vector<EngineFeatureExecutionStatus> statuses{resource};
    auto features = caps.features;
    const bool oversized = executionStatuses.size() > kMaxEngineFallbackDecisions
        || decisions.size() > kMaxEngineFallbackDecisions;
    const auto count = oversized    ? 1u
        : executionStatuses.empty() ? decisions.size()
                                    : executionStatuses.size();
    statuses.reserve(count);

    const auto append = [&](const EngineFeatureExecutionStatus& source, const char* forced) {
        EngineFeatureExecutionStatus item{source.feature,
            source.mode,
            source.executable,
            source.upperLayerVisible,
            source.telemetryRequired,
            core::pmr::String{source.diagnostics, resource}};
        const auto* diagnostic = FallbackStatusAdmission::backendDiagnostic(backend);
        if (diagnostic == nullptr && restrictionsOnly)
            diagnostic = FallbackStatusAdmission::backendDiagnostic(caps.backend);
        if (diagnostic == nullptr && restrictionsOnly && backend != caps.backend)
            diagnostic = "RHI-FALLBACK-CAPABILITY-BACKEND-MISMATCH";
        if (diagnostic == nullptr && !EngineFeatureCapabilityAccess::known(item.feature))
            diagnostic = "RHI-FALLBACK-FEATURE-UNKNOWN";
        if (diagnostic == nullptr)
            diagnostic = forced;
        if (diagnostic == nullptr)
            diagnostic = FallbackStatusAdmission::diagnostic(backend, item);

        const bool prior = EngineFeatureCapabilityAccess::read(caps.features, item.feature);
        if (diagnostic == nullptr && restrictionsOnly && item.executable && !prior)
            diagnostic = "RHI-FALLBACK-RESTRICTION-CANNOT-GRANT";
        if (diagnostic != nullptr) {
            item.executable = false;
            item.telemetryRequired = true;
            if (item.diagnostics.find(diagnostic) == item.diagnostics.npos)
                core::appendDiagnostic(item.diagnostics, diagnostic);
            if (forced != nullptr || FallbackStatusAdmission::backendDiagnostic(backend) != nullptr
                || !EngineFeatureCapabilityAccess::known(item.feature)
                || !FallbackStatusAdmission::knownMode(item.mode)
                || isExecutableFallbackMode(item.mode) || item.mode == FallbackMode::Degraded)
                item.mode = FallbackMode::DiagnosticFailure;
        }
        item.executable = item.executable && isExecutableFallbackMode(item.mode);
        EngineFeatureCapabilityAccess::write(features, item.feature, item.executable);

        if (item.feature == EngineFeature::SubpassInputAttachment)
            features.subpassInputAttachmentDegraded = item.mode == FallbackMode::Degraded
                && (!restrictionsOnly || caps.features.subpassInputAttachmentDegraded);
        if (item.feature == EngineFeature::SubpassDepthInputAttachment)
            features.subpassDepthInputAttachmentDegraded = item.mode == FallbackMode::Degraded
                && (!restrictionsOnly || caps.features.subpassDepthInputAttachmentDegraded);
        statuses.push_back(std::move(item));
    };

    if (oversized) {
        // An unrepresentable report cannot identify a bounded set of targets.
        // Revoke the entire mapped feature domain, keeping query facts separate.
        EngineFeatureCapabilityAccess::disableAll(features);
        EngineFeatureExecutionStatus failure{static_cast<EngineFeature>(-1),
            FallbackMode::DiagnosticFailure,
            false,
            false,
            true,
            core::pmr::String{"RHI-FALLBACK-STATUS-CAPACITY", resource}};
        statuses.push_back(std::move(failure));
    } else if (!executionStatuses.empty()) {
        for (const auto& source : executionStatuses) {
            const bool repeated = std::count_if(executionStatuses.begin(),
                                      executionStatuses.end(),
                                      [&](const auto& other) {
                                          return other.feature == source.feature;
                                      })
                > 1;
            append(source, repeated ? "RHI-FALLBACK-STATUS-DUPLICATE" : nullptr);
        }
    } else {
        // Older decisions alone never contain a complete execution status.
        for (const auto& decision : decisions) {
            EngineFeatureExecutionStatus missing{decision.feature,
                FallbackMode::DiagnosticFailure,
                false,
                decision.upperLayerVisible,
                true,
                core::pmr::String{decision.diagnostics, resource}};
            const bool repeated = std::count_if(decisions.begin(),
                                      decisions.end(),
                                      [&](const auto& other) {
                                          return other.feature == decision.feature;
                                      })
                > 1;
            append(missing,
                repeated ? "RHI-FALLBACK-STATUS-DUPLICATE" : "RHI-FALLBACK-STATUS-MISSING");
        }
    }

    caps.executionStatuses = std::move(statuses);
    caps.features = features;
    caps.statusTelemetry =
        summarizeExecutionStatuses(caps.executionStatuses, caps.queryExecutionStatuses);
}

EngineFallbackPlanner::EngineFallbackPlanner(
    const EngineFallbackPlannerDesc& desc, std::pmr::memory_resource* memoryResource)
    : desc_(desc),
      memoryResource_(memoryResource ? memoryResource : &core::defaultCpuMemoryResource()),
      decisions_(memoryResource_), failures_(memoryResource_), planningDiagnostics_(memoryResource_)
{
    if (desc_.maxFeatureDecisions > kMaxEngineFallbackDecisions) {
        desc_.maxFeatureDecisions = kMaxEngineFallbackDecisions;
        recordPlanningFailure("fallback planner capacity limit exceeded");
    }
    decisions_.reserve(desc_.maxFeatureDecisions);
    failures_.reserve(desc_.maxFeatureDecisions);
}

EngineFeaturePlanResult EngineFallbackPlanner::recordFeatureDecision(
    const EngineFeatureDecisionInput& input)
{
    const auto validation = validateInput(input);
    if (!validation.success) {
        // Retain the failed intent when its declared slot is available. A
        // semantic error must not become an empty, apparently harmless plan.
        if (decisions_.size() + failures_.size() < desc_.maxFeatureDecisions)
            return recordDiagnosticFailure(input, validation.diagnostics);
        recordPlanningFailure(validation.diagnostics);
        return validation;
    }

    if (input.mode == FallbackMode::Degraded
        && input.correctnessImpact == FeatureCorrectnessImpact::ChangesCorrectness) {
        return recordDiagnosticFailure(input,
            makeFeatureDiagnostics(input.feature,
                FallbackMode::DiagnosticFailure,
                "fallback would change correctness"));
    }

    if (input.mode == FallbackMode::DiagnosticFailure) {
        return recordDiagnosticFailure(
            input, makeFeatureDiagnostics(input.feature, input.mode, input.reason));
    }

    const bool telemetryRequired = input.mode == FallbackMode::Degraded
        || input.mode == FallbackMode::ExtensionOptIn || input.mode == FallbackMode::CpuReplay;
    RecordedFeatureDecision decision;
    decision.feature = input.feature;
    decision.mode = input.mode;
    decision.correctnessImpact = input.correctnessImpact;
    decision.upperLayerVisible = input.mode == FallbackMode::ExtensionOptIn;
    decision.telemetryRequired = telemetryRequired;
    decision.reason = input.reason;
    decision.diagnostics = telemetryRequired
        ? makeFeatureDiagnostics(input.feature, input.mode, input.reason)
        : std::pmr::string();
    decisions_.push_back(decision);

    return makeSuccess(input, telemetryRequired);
}

EngineFallbackPlan EngineFallbackPlanner::buildPlan() const
{
    EngineFallbackPlan plan;
    plan.success = failures_.empty() && planningFailureCount_ == 0;
    plan.backend = desc_.backend;
    plan.logicalFrameSlots = desc_.logicalFrameSlots;
    plan.backendFallbacksHiddenFromUpperLayer = true;
    plan.upperLayerVisibleFallbacks = false;
    plan.executionStatuses.reserve(decisions_.size() + failures_.size());

    for (const auto& decision : decisions_) {
        switch (decision.mode) {
        case FallbackMode::Native:
            ++plan.nativeFeatureCount;
            break;
        case FallbackMode::Emulated:
            ++plan.emulatedFeatureCount;
            break;
        case FallbackMode::CpuReplay:
            ++plan.cpuReplayFeatureCount;
            break;
        case FallbackMode::Degraded:
            ++plan.degradedFeatureCount;
            plan.telemetryRequired = true;
            arp::core::appendDiagnostic(plan.diagnostics, decision.diagnostics);
            break;
        case FallbackMode::DiagnosticFailure:
            ++plan.diagnosticFailureCount;
            break;
        case FallbackMode::ExtensionOptIn:
            plan.telemetryRequired = true;
            arp::core::appendDiagnostic(plan.diagnostics, decision.diagnostics);
            break;
        }
        plan.upperLayerVisibleFallbacks =
            plan.upperLayerVisibleFallbacks || decision.upperLayerVisible;

        EngineFeaturePlanResult result;
        result.success = decision.mode != FallbackMode::DiagnosticFailure;
        result.feature = decision.feature;
        result.mode = decision.mode;
        result.upperLayerVisible = decision.upperLayerVisible;
        result.telemetryRequired = decision.telemetryRequired;
        result.explicitOptInRequired = decision.mode == FallbackMode::ExtensionOptIn;
        result.diagnostics = decision.diagnostics;
        plan.decisions.push_back(std::move(result));

        EngineFeatureExecutionStatus status;
        status.feature = decision.feature;
        status.mode = decision.mode;
        status.executable = isExecutableFallbackMode(decision.mode);
        status.upperLayerVisible = decision.upperLayerVisible;
        status.telemetryRequired = decision.telemetryRequired;
        status.diagnostics = decision.diagnostics;
        plan.executionStatuses.push_back(std::move(status));
    }

    for (const auto& failure : failures_) {
        ++plan.diagnosticFailureCount;
        plan.telemetryRequired = true;
        arp::core::appendDiagnostic(plan.diagnostics, failure.diagnostics);
        plan.upperLayerVisibleFallbacks =
            plan.upperLayerVisibleFallbacks || failure.upperLayerVisible;

        EngineFeatureExecutionStatus status;
        status.feature = failure.feature;
        status.mode = FallbackMode::DiagnosticFailure;
        status.executable = false;
        status.upperLayerVisible = failure.upperLayerVisible;
        status.telemetryRequired = true;
        status.diagnostics = failure.diagnostics;
        plan.executionStatuses.push_back(std::move(status));
    }

    plan.diagnosticFailureCount += planningFailureCount_;
    if (planningFailureCount_ > 0) {
        plan.telemetryRequired = true;
        arp::core::appendDiagnostic(plan.diagnostics, planningDiagnostics_);
    }

    if (desc_.logicalFrameSlots != 2) {
        plan.success = false;
        ++plan.diagnosticFailureCount;
        arp::core::appendDiagnostic(
            plan.diagnostics, "fallback plan requires two logical frame slots");
    }

    return plan;
}

EngineFeaturePlanResult EngineFallbackPlanner::validateInput(
    const EngineFeatureDecisionInput& input) const
{
    if (const auto* diagnostic = FallbackStatusAdmission::backendDiagnostic(desc_.backend))
        return makeFailure(
            input, FallbackMode::DiagnosticFailure, core::pmr::String{diagnostic, memoryResource_});
    if (!EngineFeatureCapabilityAccess::known(input.feature))
        return makeFailure(input,
            FallbackMode::DiagnosticFailure,
            core::pmr::String{"RHI-FALLBACK-FEATURE-UNKNOWN", memoryResource_});
    if (!FallbackStatusAdmission::knownMode(input.mode))
        return makeFailure(input,
            FallbackMode::DiagnosticFailure,
            core::pmr::String{"RHI-FALLBACK-MODE-UNKNOWN", memoryResource_});
    if (desc_.logicalFrameSlots != 2) {
        return makeFailure(input,
            FallbackMode::DiagnosticFailure,
            "fallback plan requires two logical frame slots");
    }

    if (decisions_.size() + failures_.size() >= desc_.maxFeatureDecisions) {
        return makeFailure(
            input, FallbackMode::DiagnosticFailure, "fallback plan capacity exhausted");
    }

    return makeSuccess(input, false);
}

EngineFeaturePlanResult EngineFallbackPlanner::recordDiagnosticFailure(
    const EngineFeatureDecisionInput& input, const std::pmr::string& diagnostics)
{
    RecordedFeatureDecision failure;
    failure.feature = input.feature;
    failure.mode = FallbackMode::DiagnosticFailure;
    failure.correctnessImpact = input.correctnessImpact;
    failure.upperLayerVisible = false;
    failure.telemetryRequired = true;
    failure.reason = input.reason;
    failure.diagnostics = diagnostics;
    failures_.push_back(failure);

    return makeFailure(input, FallbackMode::DiagnosticFailure, diagnostics);
}

void EngineFallbackPlanner::recordPlanningFailure(const std::pmr::string& diagnostics)
{
    ++planningFailureCount_;
    arp::core::appendDiagnostic(planningDiagnostics_, diagnostics);
}

const char* engineFeatureName(EngineFeature feature)
{
    switch (feature) {
    case EngineFeature::PushConstants:
        return "push constants";
    case EngineFeature::SecondaryCommandBuffer:
        return "secondary command buffers";
    case EngineFeature::DescriptorTable:
        return "descriptor table";
    case EngineFeature::TransientAliasing:
        return "transient aliasing";
    case EngineFeature::TimelineSync:
        return "timeline synchronization";
    case EngineFeature::ComputeDispatch:
        return "compute dispatch";
    case EngineFeature::StorageTextureBinding:
        return "storage texture binding";
    case EngineFeature::NativeSync:
        return "native synchronization";
    case EngineFeature::SubpassInputAttachment:
        return "subpass input attachment";
    case EngineFeature::SubpassDepthInputAttachment:
        return "subpass depth input attachment";
    case EngineFeature::Rg16FloatStorageTexture:
        return "rg16float storage texture";
    case EngineFeature::Rgba16FloatStorageTexture:
        return "rgba16float storage texture";
    case EngineFeature::StorageBufferBinding:
        return "storage buffer binding";
    case EngineFeature::SceneGeometryRayQuery:
        return "scene geometry ray query";
    case EngineFeature::IndirectDraw:
        return "indirect draw";
    case EngineFeature::MultiDrawIndirectCount:
        return "multi-draw indirect count";
    }
    return "unknown feature";
}

const char* fallbackModeName(FallbackMode mode)
{
    switch (mode) {
    case FallbackMode::Native:
        return "native";
    case FallbackMode::Emulated:
        return "emulated";
    case FallbackMode::CpuReplay:
        return "cpu replay";
    case FallbackMode::Degraded:
        return "degraded";
    case FallbackMode::DiagnosticFailure:
        return "diagnostic failure";
    case FallbackMode::ExtensionOptIn:
        return "extension opt-in";
    }
    return "unknown";
}

} // namespace arp::rhi

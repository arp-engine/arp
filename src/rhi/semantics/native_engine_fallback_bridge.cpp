#include "rhi/semantics/native_engine_fallback_bridge.h"

#include "core/diagnostics/diagnostic_builder.h"
#include "core/memory/cpu_allocator.h"
#include "rhi/semantics/native_backend_capabilities.h"

#include <algorithm>
#include <utility>

namespace arp::rhi::semantics {

namespace {

std::uint32_t normalizedDecisionCapacity(const rhi::EngineFeatureRequirementSet& requirements)
{
    if (requirements.maxFeatureDecisions == 0) {
        return static_cast<std::uint32_t>(requirements.features.size());
    }
    return requirements.maxFeatureDecisions;
}

} // namespace

NativeEngineFallbackBridge::NativeEngineFallbackBridge(NativeBackendCapabilityContract contract)
    : contract_(std::move(contract))
{}

rhi::EngineFallbackPlan NativeEngineFallbackBridge::buildPlan(
    const rhi::EngineFeatureRequirementSet& requirements) const
{
    return buildNativeEngineFallbackPlan(contract_, requirements);
}

rhi::EngineFallbackPlan buildNativeEngineFallbackPlan(
    const NativeBackendCapabilityContract& contract,
    const rhi::EngineFeatureRequirementSet& requirements)
{
    rhi::EngineFallbackPlanner planner({
        contract.backend,
        requirements.logicalFrameSlots,
        normalizedDecisionCapacity(requirements),
    });

    for (const auto& requirement : requirements.features) {
        const auto fallback = chooseFeatureFallback(contract, requirement.feature);
        planner.recordFeatureDecision({
            requirement.feature,
            fallback.mode,
            requirement.correctnessImpact,
            requirement.reason,
        });
    }

    return planner.buildPlan();
}

} // namespace arp::rhi::semantics

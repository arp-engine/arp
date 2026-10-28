#pragma once

#include "rhi/semantics/fallback_plan_values.h"

namespace arp::rhi {
class EngineFallbackPlanner {
public:
    explicit EngineFallbackPlanner(const EngineFallbackPlannerDesc& desc,
        std::pmr::memory_resource* memoryResource = &core::defaultCpuMemoryResource());

    EngineFeaturePlanResult recordFeatureDecision(const EngineFeatureDecisionInput& input);
    EngineFallbackPlan buildPlan() const;

private:
    struct RecordedFeatureDecision {
        EngineFeature feature = EngineFeature::PushConstants;
        FallbackMode mode = FallbackMode::Native;
        FeatureCorrectnessImpact correctnessImpact = FeatureCorrectnessImpact::PreservesCorrectness;
        bool upperLayerVisible = false;
        bool telemetryRequired = false;
        std::pmr::string reason;
        std::pmr::string diagnostics;
    };

    EngineFeaturePlanResult validateInput(const EngineFeatureDecisionInput& input) const;
    EngineFeaturePlanResult recordDiagnosticFailure(
        const EngineFeatureDecisionInput& input, const std::pmr::string& diagnostics);
    void recordPlanningFailure(const std::pmr::string& diagnostics);

    EngineFallbackPlannerDesc desc_;
    std::pmr::memory_resource* memoryResource_;
    std::pmr::vector<RecordedFeatureDecision> decisions_;
    std::pmr::vector<RecordedFeatureDecision> failures_;
    std::uint32_t planningFailureCount_ = 0;
    std::pmr::string planningDiagnostics_;
};
} // namespace arp::rhi

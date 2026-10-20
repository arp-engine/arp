#include "renderer/deployment/native_renderer_deployment_bridge.h"
#include "rhi/semantics/fallback_plan.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <new>
#include <type_traits>

#ifdef NDEBUG
#error Fallback status tests require assertions
#endif

namespace {
using namespace arp;
using namespace rhi;

constexpr std::array identities{Backend::Null,
    Backend::Vulkan,
    Backend::Metal,
    Backend::WebGPU,
    Backend::WebGL,
    static_cast<Backend>(-1),
    static_cast<Backend>(255)};
constexpr std::array features{EngineFeature::PushConstants,
    EngineFeature::SecondaryCommandBuffer,
    EngineFeature::DescriptorTable,
    EngineFeature::TransientAliasing,
    EngineFeature::TimelineSync,
    EngineFeature::ComputeDispatch,
    EngineFeature::StorageTextureBinding,
    EngineFeature::NativeSync,
    EngineFeature::SubpassInputAttachment,
    EngineFeature::SubpassDepthInputAttachment,
    EngineFeature::Rg16FloatStorageTexture,
    EngineFeature::Rgba16FloatStorageTexture,
    EngineFeature::StorageBufferBinding,
    EngineFeature::SceneGeometryRayQuery,
    EngineFeature::IndirectDraw,
    EngineFeature::MultiDrawIndirectCount};
constexpr std::array fallbackModes{FallbackMode::Native,
    FallbackMode::Emulated,
    FallbackMode::CpuReplay,
    FallbackMode::Degraded,
    FallbackMode::DiagnosticFailure,
    FallbackMode::ExtensionOptIn,
    static_cast<FallbackMode>(-1),
    static_cast<FallbackMode>(255)};

bool structuralBackend(Backend backend)
{
    return backend == Backend::Null || backend == Backend::Vulkan || backend == Backend::Metal
        || backend == Backend::WebGPU;
}

bool contains(const std::pmr::string& text, const char* id)
{
    return text.find(id) != text.npos;
}

EngineFeatureExecutionStatus status(FallbackMode mode, bool executable)
{
    return {EngineFeature::ComputeDispatch,
        mode,
        executable,
        false,
        false,
        core::pmr::String{&core::defaultCpuMemoryResource()}};
}

class FallbackStatusFixture {
public:
    void matrix()
    {
        for (const auto backend : identities)
            for (const auto feature : features)
                for (const auto mode : fallbackModes)
                    for (const bool executable : {false, true})
                        for (const bool prior : {false, true})
                            for (const bool restrict : {false, true}) {
                                EngineFallbackPlan plan;
                                plan.backend = backend;
                                auto input = status(mode, executable);
                                input.feature = feature;
                                input.diagnostics = "source-reason";
                                plan.executionStatuses.push_back(input);
                                DeviceCapabilities caps;
                                caps.backend = backend;
                                EngineFeatureCapabilityAccess::write(caps.features, feature, prior);
                                caps.features.timestampQueries = true;
                                QueryExecutionStatus query;
                                query.state = QueryExecutionState::Pending;
                                caps.queryExecutionStatuses.push_back(query);
                                if (restrict)
                                    plan.applyRestrictionsTo(caps);
                                else
                                    plan.applyTo(caps);
                                const bool expected = structuralBackend(backend) && executable
                                    && (mode == FallbackMode::Native
                                        || mode == FallbackMode::Emulated)
                                    && (!restrict || prior);
                                assert(isFeatureSupported(caps, feature) == expected);
                                assert(caps.executionStatuses.size() == 1u);
                                assert(caps.executionStatuses.front().executable == expected);
                                assert(caps.features.timestampQueries);
                                assert(caps.queryExecutionStatuses.size() == 1u);
                                assert(caps.queryExecutionStatuses.front().state
                                    == QueryExecutionState::Pending);
                                assert(caps.statusTelemetry.pendingQueryCount == 1u);
                                for (const auto other : features)
                                    if (other != feature)
                                        assert(!isFeatureSupported(caps, other));
                                assert(plan.executionStatuses.front().mode == mode);
                                assert(plan.executionStatuses.front().executable == executable);
                                assert(
                                    plan.executionStatuses.front().diagnostics == "source-reason");
                                assert(contains(
                                    caps.executionStatuses.front().diagnostics, "source-reason"));
                            }
    }

    void purePolicy()
    {
        static_assert(FallbackStatusAdmission::contractVersion == 1u);
        static_assert(std::is_aggregate_v<EngineFallbackPlan>);
        RuntimeFeatureCapabilities caps;
        const auto before = core::defaultCpuAllocator().stats().allocationCount;
        for (const auto feature : features) {
            assert(EngineFeatureCapabilityAccess::known(feature));
            EngineFeatureCapabilityAccess::write(caps, feature, true);
            for (const auto other : features)
                assert(EngineFeatureCapabilityAccess::read(caps, other) == (other == feature));
            EngineFeatureCapabilityAccess::write(caps, feature, false);
            for (const auto backend : identities)
                for (const auto mode : fallbackModes) {
                    auto input = status(mode, true);
                    input.feature = feature;
                    (void)FallbackStatusAdmission::diagnostic(backend, input);
                }
        }
        for (const auto unknown :
            {static_cast<EngineFeature>(-1), static_cast<EngineFeature>(255)}) {
            assert(!EngineFeatureCapabilityAccess::known(unknown));
            EngineFeatureCapabilityAccess::write(caps, unknown, true);
            assert(!EngineFeatureCapabilityAccess::read(caps, unknown));
        }
        assert(core::defaultCpuAllocator().stats().allocationCount == before);
    }

    void mismatchedRestriction()
    {
        for (const auto source : identities)
            for (const auto destination : identities) {
                EngineFallbackPlan plan;
                plan.backend = source;
                plan.executionStatuses.push_back(status(FallbackMode::Native, true));
                DeviceCapabilities caps;
                caps.backend = destination;
                caps.features.computeDispatch = true;
                plan.applyRestrictionsTo(caps);
                const bool expected = source == destination && structuralBackend(source);
                assert(caps.features.computeDispatch == expected);
                assert(caps.executionStatuses.front().executable == expected);
                assert(caps.backend == destination);
                if (structuralBackend(source) && structuralBackend(destination)
                    && source != destination)
                    assert(contains(caps.executionStatuses.front().diagnostics,
                        "RHI-FALLBACK-CAPABILITY-BACKEND-MISMATCH"));
            }
    }

    void degradedRestriction()
    {
        for (const auto backend : identities)
            for (const bool prior : {false, true})
                for (const bool executable : {false, true})
                    for (const bool restrict : {false, true}) {
                        EngineFallbackPlan plan;
                        plan.backend = backend;
                        auto input = status(FallbackMode::Degraded, executable);
                        input.feature = EngineFeature::SubpassInputAttachment;
                        plan.executionStatuses.push_back(input);
                        input.feature = EngineFeature::SubpassDepthInputAttachment;
                        plan.executionStatuses.push_back(input);
                        DeviceCapabilities caps;
                        caps.backend = backend;
                        caps.features.subpassInputAttachmentDegraded = prior;
                        caps.features.subpassDepthInputAttachmentDegraded = prior;
                        if (restrict)
                            plan.applyRestrictionsTo(caps);
                        else
                            plan.applyTo(caps);
                        const bool expected =
                            structuralBackend(backend) && !executable && (!restrict || prior);
                        assert(!caps.features.subpassInputAttachment
                            && !caps.features.subpassDepthInputAttachment);
                        assert(caps.features.subpassInputAttachmentDegraded == expected);
                        assert(caps.features.subpassDepthInputAttachmentDegraded == expected);
                        for (const auto& item : caps.executionStatuses)
                            assert(!item.executable);
                        synchronizeExecutionStatuses(caps);
                        assert(caps.features.subpassInputAttachmentDegraded == expected);
                        assert(caps.features.subpassDepthInputAttachmentDegraded == expected);
                    }
    }

    void synchronizationMatrix()
    {
        for (const auto backend : identities)
            for (const auto mode : fallbackModes)
                for (const bool executable : {false, true})
                    for (const bool prior : {false, true}) {
                        DeviceCapabilities caps;
                        caps.backend = backend;
                        caps.features.computeDispatch = prior;
                        caps.executionStatuses.push_back(status(mode, executable));
                        synchronizeExecutionStatuses(caps);
                        const bool expected = structuralBackend(backend) && prior && executable
                            && (mode == FallbackMode::Native || mode == FallbackMode::Emulated);
                        assert(caps.features.computeDispatch == expected);
                        assert(caps.executionStatuses.front().executable == expected);
                        const auto diagnostic = caps.executionStatuses.front().diagnostics;
                        const auto before = core::defaultCpuAllocator().stats().allocationCount;
                        for (unsigned repeat = 0; repeat != 100u; ++repeat)
                            synchronizeExecutionStatuses(caps);
                        assert(caps.features.computeDispatch == expected);
                        assert(caps.executionStatuses.front().executable == expected);
                        assert(caps.executionStatuses.front().diagnostics == diagnostic);
                        assert(core::defaultCpuAllocator().stats().allocationCount == before);
                    }
    }

    void capacityAndEmpty()
    {
        for (const bool decisionsOnly : {false, true})
            for (const auto count :
                {kMaxEngineFallbackDecisions, kMaxEngineFallbackDecisions + 1u}) {
                EngineFallbackPlan plan;
                plan.backend = Backend::Null;
                if (decisionsOnly)
                    plan.decisions.resize(count);
                else
                    plan.executionStatuses.resize(count);
                DeviceCapabilities caps;
                caps.features.timestampQueries = true;
                for (const auto feature : features)
                    EngineFeatureCapabilityAccess::write(caps.features, feature, true);
                plan.applyTo(caps);
                if (count > kMaxEngineFallbackDecisions) {
                    assert(caps.executionStatuses.size() == 1u);
                    assert(contains(caps.executionStatuses.front().diagnostics,
                        "RHI-FALLBACK-STATUS-CAPACITY"));
                    for (const auto feature : features)
                        assert(!isFeatureSupported(caps, feature));
                } else {
                    assert(caps.executionStatuses.size() == count);
                    assert(!caps.features.pushConstants);
                    assert(caps.features.computeDispatch);
                    assert(contains(caps.executionStatuses.front().diagnostics,
                        "RHI-FALLBACK-STATUS-DUPLICATE"));
                }
                assert(caps.features.timestampQueries);
            }
        DeviceCapabilities caps;
        caps.features.computeDispatch = true;
        EngineFallbackPlan empty;
        empty.applyRestrictionsTo(caps);
        assert(caps.features.computeDispatch && caps.executionStatuses.empty());
    }

    void allocationFailure()
    {
        EngineFallbackPlan plan;
        plan.executionStatuses.push_back(status(FallbackMode::Native, true));
        for (const bool restrict : {false, true}) {
            // Explicit failing report resource; all other fields use their own defaults.
            DeviceCapabilities caps{Backend::Null,
                {},
                {},
                {},
                {},
                {},
                {},
                {},
                {},
                core::pmr::Vector<EngineFeatureExecutionStatus>{std::pmr::null_memory_resource()}};
            caps.features.computeDispatch = true;
            caps.features.timestampQueries = true;
            bool failed = false;
            try {
                if (restrict)
                    plan.applyRestrictionsTo(caps);
                else
                    plan.applyTo(caps);
            } catch (const std::bad_alloc&) {
                failed = true;
            }
            assert(failed);
            assert(caps.features.computeDispatch && caps.features.timestampQueries);
            assert(caps.executionStatuses.empty());
        }
    }

    void unknownFeature()
    {
        for (const auto feature :
            {static_cast<EngineFeature>(-1), static_cast<EngineFeature>(255)}) {
            EngineFallbackPlan plan;
            auto input = status(FallbackMode::Native, true);
            input.feature = feature;
            plan.executionStatuses.push_back(input);
            DeviceCapabilities caps;
            caps.features.computeDispatch = true;
            plan.applyTo(caps);
            assert(caps.features
                    .computeDispatch); // Unrequested fact is not replaced by an unknown enum.
            assert(
                caps.executionStatuses.size() == 1u && !caps.executionStatuses.front().executable);
            assert(caps.executionStatuses.front().mode == FallbackMode::DiagnosticFailure);
            assert(contains(
                caps.executionStatuses.front().diagnostics, "RHI-FALLBACK-FEATURE-UNKNOWN"));
            synchronizeExecutionStatuses(caps);
            assert(caps.features.computeDispatch && !caps.executionStatuses.front().executable);
        }
    }

    void modes()
    {
        assert(isExecutableFallbackMode(FallbackMode::Native));
        assert(isExecutableFallbackMode(FallbackMode::Emulated));
        for (const auto mode : {static_cast<FallbackMode>(-1), static_cast<FallbackMode>(255)})
            assert(!isExecutableFallbackMode(mode));
        assert(!isExecutableFallbackMode(FallbackMode::CpuReplay));
        assert(!isExecutableFallbackMode(FallbackMode::Degraded));
        assert(!isExecutableFallbackMode(FallbackMode::DiagnosticFailure));
        assert(!isExecutableFallbackMode(FallbackMode::ExtensionOptIn));
    }

    void forbidden(FallbackMode mode)
    {
        for (const auto backend : identities) {
            EngineFallbackPlan plan;
            plan.backend = backend;
            plan.success = true;
            plan.executionStatuses.push_back(status(mode, true));
            DeviceCapabilities caps;
            caps.backend = backend;
            caps.features.computeDispatch = true;
            caps.features.timestampQueries = true;
            plan.applyTo(caps);
            assert(!caps.features.computeDispatch);
            assert(caps.features.timestampQueries);
            assert(caps.executionStatuses.size() == 1u);
            assert(!caps.executionStatuses.front().executable);
            assert(caps.executionStatuses.front().telemetryRequired);
            assert(!caps.executionStatuses.front().diagnostics.empty());
        }
    }

    void missing()
    {
        for (const auto backend : identities) {
            EngineFallbackPlan plan;
            plan.backend = backend;
            plan.success = true;
            EngineFeaturePlanResult decision;
            decision.success = true;
            decision.feature = EngineFeature::ComputeDispatch;
            decision.mode = FallbackMode::Native;
            plan.decisions.push_back(decision);
            DeviceCapabilities caps;
            caps.backend = backend;
            caps.features.computeDispatch = true;
            plan.applyTo(caps);
            assert(!caps.features.computeDispatch);
            assert(caps.executionStatuses.size() == 1u);
            assert(!caps.executionStatuses.front().executable);
            assert(!caps.executionStatuses.front().diagnostics.empty());
        }
    }

    void duplicates()
    {
        for (const auto backend : identities) {
            for (const bool reversed : {false, true}) {
                EngineFallbackPlan plan;
                plan.backend = backend;
                plan.executionStatuses.push_back(status(
                    reversed ? FallbackMode::Native : FallbackMode::DiagnosticFailure, reversed));
                plan.executionStatuses.push_back(status(
                    reversed ? FallbackMode::DiagnosticFailure : FallbackMode::Native, !reversed));
                DeviceCapabilities caps;
                caps.backend = backend;
                caps.features.computeDispatch = true;
                plan.applyTo(caps);
                assert(!caps.features.computeDispatch);
                assert(caps.executionStatuses.size() == 2u);
                for (const auto& item : caps.executionStatuses) {
                    assert(!item.executable);
                    assert(!item.diagnostics.empty());
                }
            }
        }
    }

    void synchronization()
    {
        for (const auto backend : identities) {
            DeviceCapabilities caps;
            caps.backend = backend;
            caps.features.computeDispatch = true;
            caps.executionStatuses.push_back(status(FallbackMode::Native, false));
            synchronizeExecutionStatuses(caps);
            assert(!caps.features.computeDispatch);
            assert(!caps.executionStatuses.front().executable);
            assert(caps.executionStatuses.front().telemetryRequired);
            assert(!caps.executionStatuses.front().diagnostics.empty());
            synchronizeExecutionStatuses(caps);
            assert(!caps.features.computeDispatch && !caps.executionStatuses.front().executable);
        }
    }

    void bridge()
    {
        for (const auto backend : identities) {
            auto contract = rhi::semantics::nativeBackendCapabilityContract(backend);
            contract.queriedFromNativeDevice = contract.queriedExtendedFeatures = true;
            renderer::RendererRhiFramePlanBuildResult frame;
            frame.success = true;
            frame.plan.featureRequirements.logicalFrameSlots = 2u;
            frame.plan.featureRequirements.features.push_back({EngineFeature::ComputeDispatch,
                FeatureCorrectnessImpact::PreservesCorrectness,
                core::pmr::String{&core::defaultCpuMemoryResource()}});
            DeviceCapabilities caps;
            caps.backend = backend;
            assert(!caps.features.computeDispatch);
            renderer::deployment::evaluateAndApplyFallback(contract, frame, caps);
            assert(!caps.features.computeDispatch);
            assert(caps.executionStatuses.size() == 1u);
            assert(!caps.executionStatuses.front().executable);
        }
    }

    void planner()
    {
        for (const auto backend : identities) {
            EngineFallbackPlanner planner({backend, 2u, 1u});
            const auto result = planner.recordFeatureDecision({EngineFeature::ComputeDispatch,
                static_cast<FallbackMode>(255),
                FeatureCorrectnessImpact::PreservesCorrectness,
                core::pmr::String{&core::defaultCpuMemoryResource()}});
            assert(!result.success);
            const char* expected = backend == Backend::WebGL
                ? "RHI-WEBGL1-FALLBACK-EXECUTION-UNAVAILABLE"
                : backend == static_cast<Backend>(-1) || backend == static_cast<Backend>(255)
                ? "RHI-FALLBACK-BACKEND-UNKNOWN"
                : "RHI-FALLBACK-MODE-UNKNOWN";
            assert(contains(result.diagnostics, expected));
            const auto plan = planner.buildPlan();
            assert(!plan.success);
            assert(plan.executionStatuses.size() == 1u);
            assert(!plan.executionStatuses.front().executable);
        }
    }
};
} // namespace

int main(int argc, char** argv)
{
    FallbackStatusFixture fixture;
    const auto* selected = argc == 2 ? argv[1] : "all";
    bool matched = false;
    const auto run = [selected, &matched](const char* name) {
        const bool result = std::strcmp(selected, "all") == 0 || std::strcmp(selected, name) == 0;
        matched = matched || result;
        return result;
    };
    if (run("modes"))
        fixture.modes();
    if (run("extension"))
        fixture.forbidden(FallbackMode::ExtensionOptIn);
    if (run("replay"))
        fixture.forbidden(FallbackMode::CpuReplay);
    if (run("unknown"))
        fixture.forbidden(static_cast<FallbackMode>(255));
    if (run("missing"))
        fixture.missing();
    if (run("duplicate"))
        fixture.duplicates();
    if (run("sync"))
        fixture.synchronization();
    if (run("bridge"))
        fixture.bridge();
    if (run("planner"))
        fixture.planner();
    if (run("matrix"))
        fixture.matrix();
    if (run("policy"))
        fixture.purePolicy();
    if (run("mismatch"))
        fixture.mismatchedRestriction();
    if (run("degraded"))
        fixture.degradedRestriction();
    if (run("sync-matrix"))
        fixture.synchronizationMatrix();
    if (run("capacity"))
        fixture.capacityAndEmpty();
    if (run("allocation"))
        fixture.allocationFailure();
    if (run("unknown-feature"))
        fixture.unknownFeature();
    if (!matched)
        return 2;
    std::puts("FallbackStatusAdmission/1: requested cases passed");
}

#pragma once

#include "rhi/semantics/webgl_runtime_profile.h"
#include "webgl_profile_probe_fixture.h"

#include <array>
#include <cassert>
#include <cstring>
#include <limits>

namespace arp::test {
class WebGlProfileLimitsFixture final {
    using Probe = rhi::semantics::WebGlRuntimeProbe;
    using Profile = rhi::semantics::WebGlRuntimeProfile;
    using Attributes = rhi::semantics::WebGlContextAttributes;
    using Request = rhi::semantics::WebGlContextRequest;
    using Requirements = rhi::semantics::WebGlGraphicsRequirements;
    using Budget = rhi::semantics::WebGlGraphicsBudget;
    using Admission = rhi::semantics::WebGlProfileAdmission;

    static void expect(Admission decision, const char* diagnostic)
    {
        assert(!decision.compatible && !decision.executable);
        assert(decision.diagnostic && std::strcmp(decision.diagnostic, diagnostic) == 0);
    }

    static Probe baseline()
    {
        auto probe = WebGlProfileProbeFixture::baseline();
        // Queried-value fixture, never a device capability claim. All values
        // meet the documented spec minima; larger probe values below test that
        // caller ceilings and the L2 push limit remain independent.
        probe.contextAttributes = {true, true, false, false, true, false};
        return probe;
    }

    static Budget fullBudget(const Probe& probe)
    {
        Budget budget;
        budget.contractVersion = 1u;
        budget.ceilings = probe.limits;
        budget.maxPushConstantSizeBytes = rhi::kL2AtomicPushConstantLimitBytes;
        return budget;
    }

public:
    static void verify()
    {
        auto probe = baseline();
        {
            auto invalidPrecision = probe;
            invalidPrecision.precision
                .fragment[static_cast<std::size_t>(
                    rhi::semantics::WebGlNumericPrecision::HighFloat)]
                .precision = -1;
            const auto rejected = Profile::fromProbe(invalidPrecision);
            assert(!rejected.valid());
        }
        const auto profile = Profile::fromProbe(probe);
        assert(profile.valid());
        Request request;
        request.contractVersion = 1u;
        request.attributes = probe.contextAttributes;
        auto admitted = profile.admitContext(request);
        assert(admitted.compatible && !admitted.executable);
        constexpr std::array attributes{&Attributes::alpha,
            &Attributes::depth,
            &Attributes::stencil,
            &Attributes::antialias,
            &Attributes::premultipliedAlpha,
            &Attributes::preserveDrawingBuffer};
        // Verify every actual/request tuple: defaults never stand in for query
        // evidence, and a single mismatch is not a permitted degradation.
        for (unsigned actual = 0u; actual != (1u << attributes.size()); ++actual) {
            auto tupleProbe = probe;
            for (std::size_t field = 0u; field != attributes.size(); ++field)
                tupleProbe.contextAttributes.*attributes[field] = (actual & (1u << field)) != 0u;
            const auto tupleProfile = Profile::fromProbe(tupleProbe);
            assert(tupleProfile.valid());
            for (unsigned desired = 0u; desired != (1u << attributes.size()); ++desired) {
                auto tupleRequest = request;
                for (std::size_t field = 0u; field != attributes.size(); ++field)
                    tupleRequest.attributes.*attributes[field] = (desired & (1u << field)) != 0u;
                const auto result = tupleProfile.admitContext(tupleRequest);
                assert(result.compatible == (actual == desired) && !result.executable);
                if (actual != desired)
                    expect(result, "RHI-WEBGL1-CONTEXT-ATTRIBUTES-MISMATCH");
            }
        }
        for (auto field : attributes) {
            auto changed = probe;
            changed.contextAttributes.*field = !(changed.contextAttributes.*field);
            // The old profile owns a copy, rather than borrowing the probe.
            probe = changed;
            assert(profile.admitContext(request).compatible);
        }
        probe = baseline();
        for (auto version : {0u, 1u, 2u, 3u, 4u, 6u, std::numeric_limits<std::uint32_t>::max()}) {
            auto invalid = probe;
            invalid.probeVersion = version;
            const auto rejected = Profile::fromProbe(invalid);
            assert(!rejected.valid());
            expect(rejected.admit({}), "RHI-WEBGL1-PROBE-VERSION");
        }
        for (auto version : {0u, 2u, std::numeric_limits<std::uint32_t>::max()}) {
            auto invalid = request;
            invalid.contractVersion = version;
            expect(profile.admitContext(invalid), "RHI-WEBGL1-CONTEXT-REQUEST-VERSION");
        }
        probe.contextAttributesQueried = false;
        const auto missing = Profile::fromProbe(probe);
        assert(!missing.valid());
        expect(missing.admit({}), "RHI-WEBGL1-PROBE-INCOMPLETE");
        expect(missing.admitContext(request), "RHI-WEBGL1-PROBE-INCOMPLETE");
        probe = baseline();
        auto budget = fullBudget(probe);
        Requirements requirements;
        assert(profile.admitWithin(requirements, budget).compatible);
        constexpr std::array limits{&rhi::semantics::WebGlRuntimeLimits::maxVertexAttributes,
            &rhi::semantics::WebGlRuntimeLimits::maxVertexUniformVectors,
            &rhi::semantics::WebGlRuntimeLimits::maxFragmentUniformVectors,
            &rhi::semantics::WebGlRuntimeLimits::maxVaryingVectors,
            &rhi::semantics::WebGlRuntimeLimits::maxVertexTextureUnits,
            &rhi::semantics::WebGlRuntimeLimits::maxFragmentTextureUnits,
            &rhi::semantics::WebGlRuntimeLimits::maxCombinedTextureUnits,
            &rhi::semantics::WebGlRuntimeLimits::maxTextureSize,
            &rhi::semantics::WebGlRuntimeLimits::maxTextureSize,
            &rhi::semantics::WebGlRuntimeLimits::maxViewportWidth,
            &rhi::semantics::WebGlRuntimeLimits::maxViewportHeight,
            &rhi::semantics::WebGlRuntimeLimits::maxRenderbufferSize,
            &rhi::semantics::WebGlRuntimeLimits::maxRenderbufferSize};
        constexpr std::array demands{&Requirements::vertexAttributes,
            &Requirements::vertexUniformVectors,
            &Requirements::fragmentUniformVectors,
            &Requirements::varyingVectors,
            &Requirements::vertexTextureUnits,
            &Requirements::fragmentTextureUnits,
            &Requirements::combinedTextureUnits,
            &Requirements::textureWidth,
            &Requirements::textureHeight,
            &Requirements::viewportWidth,
            &Requirements::viewportHeight,
            &Requirements::renderbufferWidth,
            &Requirements::renderbufferHeight};
        for (std::size_t index = 0u; index != demands.size(); ++index) {
            requirements = {};
            requirements.*demands[index] = probe.limits.*limits[index];
            if (demands[index] == &Requirements::vertexTextureUnits
                || demands[index] == &Requirements::fragmentTextureUnits)
                requirements.combinedTextureUnits = requirements.*demands[index];
            const auto exact = profile.admitWithin(requirements, budget);
            assert(exact.compatible && !exact.executable);
            if (requirements.*demands[index]) {
                --(budget.ceilings.*limits[index]);
                expect(profile.admitWithin(requirements, budget), "RHI-WEBGL1-BUDGET-LIMIT");
                ++(budget.ceilings.*limits[index]);
            }
            ++(requirements.*demands[index]);
            budget.ceilings.*limits[index] = std::numeric_limits<std::uint32_t>::max();
            expect(profile.admitWithin(requirements, budget), "RHI-WEBGL1-WORKLOAD-LIMIT");
            budget = fullBudget(probe);
        }
        for (auto field : {&Requirements::renderbufferWidth, &Requirements::renderbufferHeight}) {
            requirements = {};
            requirements.*field = probe.limits.maxRenderbufferSize;
            assert(profile.admitWithin(requirements, budget).compatible);
            --budget.ceilings.maxRenderbufferSize;
            expect(profile.admitWithin(requirements, budget), "RHI-WEBGL1-BUDGET-LIMIT");
            ++budget.ceilings.maxRenderbufferSize;
            ++(requirements.*field);
            expect(profile.admit(requirements), "RHI-WEBGL1-WORKLOAD-LIMIT");
            budget.ceilings.maxRenderbufferSize = std::numeric_limits<std::uint32_t>::max();
            expect(profile.admitWithin(requirements, budget), "RHI-WEBGL1-WORKLOAD-LIMIT");
            budget = fullBudget(probe);
        }
        // GL queries return signed GLint values. A negative query result cast
        // to uint32 is not a legitimate device limit. Caller ceilings below
        // may be UINT32_MAX because they are allowances rather than GL facts.
        for (auto field : limits) {
            auto invalid = probe;
            invalid.limits.*field = std::numeric_limits<std::uint32_t>::max();
            const auto rejected = Profile::fromProbe(invalid);
            assert(!rejected.valid());
            expect(rejected.admit({}), "RHI-WEBGL1-LIMIT-PROBE-INVALID");
        }
        auto invalidCube = probe;
        invalidCube.limits.maxCubeTextureSize = std::numeric_limits<std::uint32_t>::max();
        assert(!Profile::fromProbe(invalidCube).valid());
        auto sampled = probe;
        sampled.limits.maxVertexTextureUnits = 8u;
        sampled.limits.maxCombinedTextureUnits = 16u;
        const auto sampledProfile = Profile::fromProbe(sampled);
        assert(sampledProfile.valid());
        auto sampledBudget = fullBudget(sampled);
        requirements = {};
        requirements.vertexTextureUnits = requirements.fragmentTextureUnits = 8u;
        requirements.combinedTextureUnits = 16u;
        assert(sampledProfile.admitWithin(requirements, sampledBudget).compatible);
        --sampledBudget.ceilings.maxVertexTextureUnits;
        expect(sampledProfile.admitWithin(requirements, sampledBudget), "RHI-WEBGL1-BUDGET-LIMIT");
        sampledBudget = fullBudget(sampled);
        --requirements.combinedTextureUnits;
        expect(sampledProfile.admitWithin(requirements, sampledBudget),
            "RHI-WEBGL1-TEXTURE-UNIT-ACCOUNTING");
        auto large = sampled;
        large.limits.maxVertexTextureUnits = large.limits.maxFragmentTextureUnits =
            large.limits.maxCombinedTextureUnits = std::numeric_limits<std::int32_t>::max();
        const auto largeProfile = Profile::fromProbe(large);
        assert(largeProfile.valid());
        requirements.vertexTextureUnits = requirements.fragmentTextureUnits =
            requirements.combinedTextureUnits = std::numeric_limits<std::int32_t>::max();
        expect(largeProfile.admitWithin(requirements, fullBudget(large)),
            "RHI-WEBGL1-TEXTURE-UNIT-ACCOUNTING");
        requirements = {};
        requirements.pushConstantSizeBytes = rhi::kL2AtomicPushConstantLimitBytes;
        assert(profile.admitWithin(requirements, budget).compatible);
        --budget.maxPushConstantSizeBytes;
        expect(profile.admitWithin(requirements, budget), "RHI-WEBGL1-BUDGET-LIMIT");
        ++budget.maxPushConstantSizeBytes;
        ++requirements.pushConstantSizeBytes;
        expect(profile.admit(requirements), "RHI-WEBGL1-PUSH-L2-LIMIT");
        budget.maxPushConstantSizeBytes = std::numeric_limits<std::uint32_t>::max();
        expect(profile.admitWithin(requirements, budget), "RHI-WEBGL1-PUSH-L2-LIMIT");
        requirements = {};
        for (auto version : {0u, 2u, std::numeric_limits<std::uint32_t>::max()}) {
            budget.contractVersion = version;
            expect(profile.admitWithin(requirements, budget), "RHI-WEBGL1-BUDGET-VERSION");
        }
        budget.contractVersion = 1u;
        budget.ceilings = {};
        budget.maxPushConstantSizeBytes = 0u;
        assert(profile.admitWithin(requirements, budget).compatible);
        requirements.vertexAttributes = 1u;
        expect(profile.admitWithin(requirements, budget), "RHI-WEBGL1-BUDGET-LIMIT");
        requirements = {};
        requirements.pushConstantSizeBytes = 1u;
        expect(profile.admitWithin(requirements, budget), "RHI-WEBGL1-BUDGET-LIMIT");
        // An empty budget does not mask missing probe evidence.
        expect(missing.admitWithin({}, Budget{}), "RHI-WEBGL1-PROBE-INCOMPLETE");
    }
};
} // namespace arp::test

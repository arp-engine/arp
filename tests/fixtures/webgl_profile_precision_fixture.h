#pragma once

#include "webgl_profile_probe_fixture.h"

#include <array>
#include <cassert>
#include <cstring>
#include <limits>

namespace arp::test {

class WebGlProfilePrecisionFixture final {
    using Profile = rhi::semantics::WebGlRuntimeProfile;
    using Facts = rhi::semantics::WebGlShaderPrecisionFacts;
    using Requirements = rhi::semantics::WebGlGraphicsRequirements;
    using Minimum = rhi::semantics::WebGlPrecisionMinimum;
    using Format = rhi::semantics::WebGlPrecisionFormat;

    static void expect(
        const Profile& profile, const Requirements& requirement, const char* diagnostic)
    {
        const auto result = profile.admit(requirement);
        assert(!result.compatible && !result.executable);
        assert(result.diagnostic && std::strcmp(result.diagnostic, diagnostic) == 0);
    }

public:
    static void verify()
    {
        using namespace rhi::semantics;
        constexpr std::array factStages{&Facts::vertex, &Facts::fragment};
        constexpr std::array demandStages{
            &WebGlPrecisionRequirements::vertex, &WebGlPrecisionRequirements::fragment};
        constexpr std::array factFields{&Format::rangeMin, &Format::rangeMax, &Format::precision};
        constexpr std::array demandFields{
            &Minimum::rangeMin, &Minimum::rangeMax, &Minimum::precision};
        auto probe = WebGlProfileProbeFixture::baseline();
        // This fixture makes every category available to compare each
        // component, independent of the baseline's optional fragment highp.
        probe.precision.fragment = probe.precision.vertex;
        const auto profile = Profile::fromProbe(probe);
        assert(profile.valid());
        for (std::size_t stage = 0; stage != factStages.size(); ++stage) {
            for (std::size_t category = 0; category != kWebGlNumericPrecisionCount; ++category) {
                const auto actual = (probe.precision.*factStages[stage])[category];
                const bool integer =
                    category >= static_cast<std::size_t>(WebGlNumericPrecision::LowInt);
                Requirements requirement;
                requirement.precision.contractVersion = 1u;
                auto& minimum = (requirement.precision.*demandStages[stage])[category];
                minimum = {true, actual.rangeMin, actual.rangeMax, actual.precision};
                auto admitted = profile.admit(requirement);
                assert(admitted.compatible && !admitted.executable);
                for (std::size_t field = 0; field != factFields.size(); ++field) {
                    const auto original = minimum.*demandFields[field];
                    ++(minimum.*demandFields[field]);
                    expect(profile,
                        requirement,
                        integer && field == 2u ? "RHI-WEBGL1-PRECISION-REQUIREMENT-INVALID"
                                               : "RHI-WEBGL1-PRECISION-INSUFFICIENT");
                    minimum.*demandFields[field] = original;
                    if (original > 0) {
                        --(minimum.*demandFields[field]);
                        assert(profile.admit(requirement).compatible);
                        minimum.*demandFields[field] = original;
                    }
                    minimum.*demandFields[field] = -1;
                    expect(profile, requirement, "RHI-WEBGL1-PRECISION-REQUIREMENT-INVALID");
                    minimum.*demandFields[field] = original;

                    auto invalid = probe;
                    (invalid.precision.*factStages[stage])[category].*factFields[field] = -1;
                    const auto rejected = Profile::fromProbe(invalid);
                    assert(!rejected.valid());
                    expect(rejected, {}, "RHI-WEBGL1-PRECISION-PROBE-INVALID");
                }
                auto missing = probe;
                (missing.precision.*factStages[stage])[category].queried = false;
                const auto incomplete = Profile::fromProbe(missing);
                assert(!incomplete.valid());
                expect(incomplete, {}, "RHI-WEBGL1-PRECISION-PROBE-INCOMPLETE");

                auto unavailable = probe;
                (unavailable.precision.*factStages[stage])[category] = {true, 0, 0, 0};
                const auto limited = Profile::fromProbe(unavailable);
                assert(limited.valid());
                minimum = {true, 0, 0, 0};
                expect(limited, requirement, "RHI-WEBGL1-PRECISION-UNSUPPORTED");
                minimum = {};
                assert(limited.admit(requirement).compatible);

                if (integer) {
                    auto invalid = probe;
                    (invalid.precision.*factStages[stage])[category].precision = 1;
                    expect(Profile::fromProbe(invalid), {}, "RHI-WEBGL1-PRECISION-PROBE-INVALID");
                }
                // Each table is independently copied, never a borrowed view.
                (probe.precision.*factStages[stage])[category] = {false, -1, -1, -1};
                assert((profile.precision().*factStages[stage])[category].precision
                    == actual.precision);
                (probe.precision.*factStages[stage])[category] = actual;
            }
        }
        Requirements requirement;
        for (auto version : {0u, 2u, std::numeric_limits<std::uint32_t>::max()}) {
            requirement.precision.contractVersion = version;
            requirement.precision.vertex[0].required = true;
            expect(profile, requirement, "RHI-WEBGL1-PRECISION-REQUIREMENT-VERSION");
        }
        requirement = {};
        requirement.precision.contractVersion = 1u;
        requirement.precision.vertex[0].rangeMin = 1;
        expect(profile, requirement, "RHI-WEBGL1-PRECISION-REQUIREMENT-INVALID");
        // Reject a malformed later field before reporting an unavailable
        // earlier capability. Descriptor validation cannot depend on hardware.
        auto limited = probe;
        limited.precision.vertex[0] = {true, 0, 0, 0};
        requirement.precision.vertex[0] = {true, 0, 0, 0};
        requirement.precision.fragment.back() = {true, 0, 0, 1};
        expect(
            Profile::fromProbe(limited), requirement, "RHI-WEBGL1-PRECISION-REQUIREMENT-INVALID");
        auto large = probe;
        constexpr auto high = static_cast<std::size_t>(WebGlNumericPrecision::HighFloat);
        constexpr auto signedMax = std::numeric_limits<std::int32_t>::max();
        large.precision.fragment[high] = {true, signedMax, signedMax, signedMax};
        requirement = {};
        requirement.fragmentHighFloatPrecision = static_cast<std::uint32_t>(signedMax);
        requirement.precision.contractVersion = 1u;
        requirement.precision.fragment[high] = {true, signedMax, signedMax, signedMax};
        const auto largeProfile = Profile::fromProbe(large);
        assert(largeProfile.valid() && largeProfile.admit(requirement).compatible);
        requirement.fragmentHighFloatPrecision = std::numeric_limits<std::uint32_t>::max();
        expect(largeProfile, requirement, "RHI-WEBGL1-FRAGMENT-PRECISION");
    }
};

} // namespace arp::test

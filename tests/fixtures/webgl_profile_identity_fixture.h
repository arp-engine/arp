#pragma once

#include "webgl_profile_probe_fixture.h"

#include <cassert>
#include <cstring>
#include <utility>

namespace arp::test {

class WebGlProfileIdentityFixture final {
public:
    static void verify()
    {
        using namespace rhi::semantics;
        auto probe = WebGlProfileProbeFixture::baseline();
        const auto expect = [](const WebGlRuntimeProbe& input, const char* diagnostic) {
            const auto profile = WebGlRuntimeProfile::fromProbe(input);
            assert(!profile.valid());
            const auto admission = profile.admit({});
            assert(!admission.compatible && !admission.executable);
            assert(std::strcmp(admission.diagnostic, diagnostic) == 0);
        };
        for (std::size_t index = 0u; index < webGlIdentityFieldCount; ++index) {
            auto missing = probe;
            missing.identity.queried[index] = false;
            expect(missing, "RHI-WEBGL1-IDENTITY-PROBE-INCOMPLETE");
            auto invalid = probe;
            constexpr char embedded[] = "identity\0tail";
            invalid.identity.values[index].assign(embedded, sizeof(embedded) - 1u);
            expect(invalid, "RHI-WEBGL1-IDENTITY-PROBE-INVALID");
            auto empty = probe;
            empty.identity.values[index].clear();
            const auto profile = WebGlRuntimeProfile::fromProbe(empty);
            assert(profile.valid() == (index >= 2u));
            if (profile.valid())
                assert(profile.identity().values[index].empty());
        }
        for (const auto* text : {"WebGL 2.0", "OpenGL ES 2.0", "WebGL 1.00", "WebGL 1.0fake"}) {
            auto invalid = probe;
            invalid.identity.values[0] = text;
            expect(invalid, "RHI-WEBGL1-IDENTITY-CONTEXT-VERSION");
        }
        for (const auto* text :
            {"WebGL GLSL ES 3.00", "GLSL ES 1.0", "WebGL GLSL ES 1.00", "WebGL GLSL ES 1.0fake"}) {
            auto invalid = probe;
            invalid.identity.values[1] = text;
            expect(invalid, "RHI-WEBGL1-IDENTITY-SHADER-VERSION");
        }
        probe.identity.values[0] = "WebGL 1.0 (synthetic suffix)";
        probe.identity.values[1] = "WebGL GLSL ES 1.0 (synthetic suffix)";
        // A 1 KiB diagnostic string is an explicit copy/lifetime test workload,
        // never a production identity capacity or truncation policy.
        probe.identity.values[2].assign(1024u, 'v');
        auto profile = WebGlRuntimeProfile::fromProbe(probe);
        assert(profile.valid());
        auto copy = profile;
        probe.identity.values[2][0] = 'x';
        profile = WebGlRuntimeProfile::fromProbe(probe);
        assert(profile.identity().values[2][0] == 'x');
        assert(copy.identity().values[2][0] == 'v');
        assert(copy.identity().values[2].data() != probe.identity.values[2].data());
        auto moved = std::move(copy);
        assert(moved.valid() && moved.identity().values[2][0] == 'v');
        const auto detached = [] {
            auto temporary = WebGlProfileProbeFixture::baseline();
            temporary.identity.values[4].assign(1024u, 'b');
            return WebGlRuntimeProfile::fromProbe(temporary);
        }();
        assert(detached.valid() && detached.identity().values[4].size() == 1024u);
        assert(detached.identity().values[4][1023u] == 'b');
        const auto quota = core::makeShared<core::CpuMemoryQuota>(1u);
        bool allocationRejected = false;
        try {
            core::CpuMemoryQuotaScope scope(quota);
            const auto denied = WebGlRuntimeProfile::fromProbe(probe);
            (void)denied;
        } catch (const std::bad_alloc&) {
            allocationRejected = true;
        }
        assert(allocationRejected && quota->liveBytes() == 0u);

        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        const WebGlFormatRequest format{
            1u, rhi::TextureFormat::Rgba8Unorm, WebGlFormatRole::Texture2DSampled, 1u};
        // Profile construction/copy is cold, but every admission remains hot
        // and allocation-free. Plausible identity text grants no execution.
        for (std::size_t iteration = 0u; iteration < 64u; ++iteration) {
            const auto admitted = detached.admit({});
            assert(admitted.compatible && !admitted.executable);
            assert(detached.admitFormat(format).compatible
                && !detached.admitFormat(format).executable);
            assert(detached.admitContext({1u, {}}).compatible);
            assert(!detached.admitContext({1u, {}}).executable);
            const WebGlGraphicsBudget budget{1u, {}, 0u};
            assert(detached.admitWithin({}, budget).compatible);
            assert(!detached.admitWithin({}, budget).executable);
        }
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
    }
};

} // namespace arp::test

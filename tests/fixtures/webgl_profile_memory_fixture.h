#pragma once

#include "webgl_profile_probe_fixture.h"

#include <array>
#include <cassert>
#include <cstring>
#include <limits>

namespace arp::test {

class WebGlProfileMemoryFixture final {
public:
    static void verify()
    {
        using namespace rhi::semantics;
        const auto profile = WebGlRuntimeProfile::fromProbe(WebGlProfileProbeFixture::baseline());
        const auto invalid = WebGlRuntimeProfile::fromProbe({});
        constexpr std::array fields{&WebGlMemoryCounters::cpuMetadataBytes,
            &WebGlMemoryCounters::cpuUploadBytes,
            &WebGlMemoryCounters::cpuReloadBytes,
            &WebGlMemoryCounters::logicalBufferBytes,
            &WebGlMemoryCounters::logicalTextureBytes,
            &WebGlMemoryCounters::objectCount,
            &WebGlMemoryCounters::commandCount,
            &WebGlMemoryCounters::referenceCount};
        const auto expect = [](WebGlProfileAdmission admission, const char* diagnostic) {
            assert(!admission.compatible && !admission.executable);
            assert(std::strcmp(admission.diagnostic, diagnostic) == 0);
        };
        const auto accept = [](WebGlProfileAdmission admission) {
            assert(admission.compatible && !admission.executable);
            assert(std::strcmp(admission.diagnostic, "RHI-WEBGL1-EXECUTION-NOT-IMPLEMENTED") == 0);
        };
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        WebGlMemoryBudget empty{1u, {}};
        WebGlMemoryRequest none{1u, {}};
        accept(profile.admitMemory(empty, {}, none));
        for (const auto field : fields) {
            // Explicit arithmetic test workload in one dimension; unused
            // categories have zero capacity, rather than unlimited capacity.
            WebGlMemoryBudget budget{1u, {}};
            budget.capacity.*field = 16u;
            WebGlMemoryCounters live;
            live.*field = 7u;
            WebGlMemoryRequest request{1u, {}};
            request.addition.*field = 9u;
            accept(profile.admitMemory(budget, live, request));
            assert(budget.capacity.*field == 16u && live.*field == 7u
                && request.addition.*field == 9u);
            request.addition.*field = 10u;
            expect(profile.admitMemory(budget, live, request), "RHI-WEBGL1-MEMORY-CAPACITY");
            request.addition.*field = 9u;
            budget.capacity.*field = 15u;
            expect(profile.admitMemory(budget, live, request), "RHI-WEBGL1-MEMORY-CAPACITY");
            live.*field = 17u;
            expect(
                profile.admitMemory(budget, live, none), "RHI-WEBGL1-MEMORY-LIVE-EXCEEDS-CAPACITY");
            request.addition.*field = 1u;
            expect(profile.admitMemory(empty, {}, request), "RHI-WEBGL1-MEMORY-CAPACITY");
            constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
            budget.capacity.*field = maximum;
            live.*field = maximum - 1u;
            accept(profile.admitMemory(budget, live, request));
            live.*field = maximum;
            expect(profile.admitMemory(budget, live, request), "RHI-WEBGL1-MEMORY-OVERFLOW");
        }
        WebGlMemoryBudget independent{1u, {}};
        WebGlMemoryRequest all{1u, {}};
        for (const auto field : fields) {
            independent.capacity.*field = 16u;
            all.addition.*field = 16u;
        }
        accept(profile.admitMemory(independent, {}, all));
        // Spare buffer bytes cannot pay a texture budget, and no sum of these
        // dimensions is an observation/bound on native physical memory.
        independent.capacity.logicalBufferBytes = 1024u;
        ++all.addition.logicalTextureBytes;
        expect(profile.admitMemory(independent, {}, all), "RHI-WEBGL1-MEMORY-CAPACITY");
        all.requiresNativePhysicalBound = true;
        expect(profile.admitMemory(independent, {}, all),
            "RHI-WEBGL1-MEMORY-PHYSICAL-BOUND-UNAVAILABLE");
        for (auto version : {0u, 2u, std::numeric_limits<std::uint32_t>::max()}) {
            auto budget = empty;
            budget.contractVersion = version;
            expect(profile.admitMemory(budget, {}, none), "RHI-WEBGL1-MEMORY-BUDGET-VERSION");
            auto request = none;
            request.contractVersion = version;
            expect(profile.admitMemory(empty, {}, request), "RHI-WEBGL1-MEMORY-REQUEST-VERSION");
            expect(invalid.admitMemory(budget, {}, request), "RHI-WEBGL1-CONTRACT-VERSION");
        }
        // Check the whole live snapshot first, including a later category.
        WebGlMemoryCounters badLive;
        badLive.referenceCount = 1u;
        WebGlMemoryRequest tooLarge{1u, {}};
        tooLarge.addition.cpuMetadataBytes = 1u;
        expect(profile.admitMemory(empty, badLive, tooLarge),
            "RHI-WEBGL1-MEMORY-LIVE-EXCEEDS-CAPACITY");
        WebGlMemoryBudget overflowBudget{1u, {}};
        overflowBudget.capacity.referenceCount = std::numeric_limits<std::uint64_t>::max();
        badLive.referenceCount = overflowBudget.capacity.referenceCount;
        tooLarge.addition.referenceCount = 1u;
        expect(
            profile.admitMemory(overflowBudget, badLive, tooLarge), "RHI-WEBGL1-MEMORY-OVERFLOW");
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
    }
};

} // namespace arp::test

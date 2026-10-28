#pragma once

#include "rhi/gpu_device.h"

#include <cassert>
#include <cstdio>
#include <string_view>

namespace arp::test {
class OriginalQueryNativeLossFixture final {
public:
    static constexpr std::uint64_t bytes = 65536u;

    static rhi::OriginalResourceRegistrationBudget budget(std::uint32_t pools = 1u)
    {
        using C = rhi::OriginalResourceRegistrationContract;
        rhi::OriginalResourceRegistrationBudget b;
        b.contractVersion = 10u;
        b.prototypeOptIn = true;
        b.maxBuffers = b.maxPipelineLayouts = b.maxGraphicsPipelines = 1u;
        b.maxDescriptorArenas = b.maxDescriptorSets = 1u;
        b.queries = {1u, true, pools};
        b.queryNativeLoss = {1u, true};
        b.maxRetainedOwners = C::fixedOwnerCount + (pools + 3u) * C::perResourceOwnerCount
            + C::ownerCount(rhi::OriginalRegisteredResourceKind::GraphicsPipeline, 10u)
            + C::perDescriptorOwnerCount;
        b.maxCpuMetadataBytes = bytes;
        return b;
    }

    static void expect(const char* actual, const char* expected)
    {
        if (!actual || std::string_view(actual) != expected)
            std::fprintf(
                stderr, "QueryLoss actual=%s expected=%s\n", actual ? actual : "<none>", expected);
        assert(actual && std::string_view(actual) == expected);
    }

    static void verifyContract()
    {
        using namespace rhi;
        using C = OriginalQueryNativeLossJoinContract;
        const auto b = budget();
        assert(!C::validate(b));
        assert(!OriginalTypedNativeLossJoinContract::validate({3u, true}, b));
        assert(!OriginalTypedNativeLossJoinContract::validate({4u, true}, b));
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU, Backend::WebGL}) {
            const auto d = C::lowering(backend);
            assert(d.validatorOnly == (backend == Backend::Null));
            assert(d.nativeSourceAvailable == (backend == Backend::Vulkan));
            assert(bool(d.diagnostic) == (backend != Backend::Vulkan));
            assert(!d.recordingAvailable && !d.executionAvailable);
        }
        assert(!C::lowering(static_cast<Backend>(255u)).nativeSourceAvailable);
        auto invalid = b;
        invalid.queryNativeLoss = {};
        expect(C::validate(invalid), "RHI-ORIGINAL-QUERY-NATIVE-LOSS-JOIN-UNAVAILABLE");
        for (const auto version : {0u, 2u}) {
            invalid = b;
            invalid.queryNativeLoss.contractVersion = version;
            expect(C::validate(invalid), "RHI-ORIGINAL-QUERY-LOSS-VERSION");
        }
        invalid = b;
        invalid.queryNativeLoss.prototypeOptIn = false;
        expect(C::validate(invalid), "RHI-ORIGINAL-QUERY-LOSS-OPT-IN");
        for (std::uint32_t version = 1u; version < 10u; ++version) {
            invalid = b;
            invalid.contractVersion = version;
            expect(C::validate(invalid), "RHI-ORIGINAL-QUERY-LOSS-REGISTRATION-PROFILE");
        }
        invalid = b;
        invalid.queries = {};
        expect(C::validate(invalid), "RHI-ORIGINAL-QUERY-LOSS-REGISTRATION-PROFILE");
        invalid = b;
        invalid.queries.maxQueryPools = 0u;
        expect(C::validate(invalid), "RHI-ORIGINAL-QUERY-REGISTRATION-CAPACITY");
        invalid = b;
        invalid.maxPipelineLayouts = invalid.maxGraphicsPipelines = 0u;
        expect(C::validate(invalid), "RHI-ORIGINAL-TYPED-LOSS-PIPELINE-BUDGET");
        invalid = b;
        invalid.maxDescriptorTables = 1u;
        expect(C::validate(invalid), "RHI-ORIGINAL-TYPED-LOSS-SYNC-PROFILE");
        invalid = b;
        invalid.heaps = {1u, true, 1u};
        expect(C::validate(invalid), "RHI-ORIGINAL-HEAP-NATIVE-LOSS-JOIN-UNAVAILABLE");
        invalid = b;
        invalid.samplers = {1u, true, 1u};
        expect(C::validate(invalid), "RHI-ORIGINAL-TYPED-LOSS-SYNC-PROFILE");
        std::puts(
            "QUERY_LOSS_CONTRACT independent-opt-in registry10 four-backends old-profiles-closed PASS");
    }
};
} // namespace arp::test

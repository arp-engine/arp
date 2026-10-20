#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/frontend/command_sources.h"
#include "rhi/frontend/descriptor_buffer_write_validation.h"
#include "rhi/frontend/descriptor_validation.h"

#include <cassert>

namespace arp::test {
class OriginalFiniteTargetMemoryWriteFixture final {
public:
    static void verifyContract()
    {
        using C = rhi::OriginalFiniteTargetMemoryWriteContract;
        rhi::OriginalFiniteTargetMemoryWriteRequest r{
            1u, true, 1u, static_cast<std::uint32_t>(C::ownerSlots(1u)), 3u, {1u, 1u, 1u}};
        assert(!C::validate(r));
        auto bad = r;
        bad.contractVersion = 2u;
        assert(C::validate(bad));
        bad = r;
        bad.prototypeOptIn = false;
        assert(C::validate(bad));
        bad = r;
        bad.maxBindings = 0u;
        assert(C::validate(bad));
        bad = r;
        --bad.maxRetainedOwnerSlots;
        assert(C::validate(bad));
        bad = r;
        --bad.maxCallbacks;
        assert(C::validate(bad));
        for (std::size_t i = 0u; i < 3u; ++i) {
            bad = r;
            bad.maxCpuBytes[i] = 0u;
            assert(C::validate(bad));
        }
        for (const auto backend : std::array{rhi::Backend::Null,
                 rhi::Backend::Vulkan,
                 rhi::Backend::Metal,
                 rhi::Backend::WebGPU}) {
            const auto missing = C::decide(backend);
            assert(
                missing.diagnostic && !missing.preparationAvailable && !missing.executionAvailable);
            const auto available = C::decide(backend, {true, true, true, true, true});
            assert(available.preparationAvailable == (backend == rhi::Backend::WebGPU));
            assert(!available.executionAvailable
                && available.validatorOnly == (backend == rhi::Backend::Null));
        }
        for (std::size_t i = 0u; i < 5u; ++i) {
            C::Facts f{true, true, true, true, true};
            switch (i) {
            case 0:
                f.device = false;
                break;
            case 1:
                f.group = false;
                break;
            case 2:
                f.layoutQuery = false;
                break;
            case 3:
                f.pushScope = false;
                break;
            case 4:
                f.popScope = false;
                break;
            }
            assert(C::decide(rhi::Backend::WebGPU, f).diagnostic);
        }
        using Identity = rhi::OriginalDescriptorNativeIdentityContract;
        using Profile = Identity::Request::Profile;
        assert(!Identity::validateIntent({2u, true, Profile::FiniteTargetMemory1}));
        assert(Identity::validateIntent({1u, true, Profile::FiniteTargetMemory1}));
        assert(core::StringView(Identity::validateIntent({2u, true, Profile::FiniteWrite1}))
            == "RHI-ORIGINAL-GROUP-IDENTITY-VERSION");
        assert(Identity::validateIntent({2u, true, Profile::WholeWrite1}));
        assert(Identity::validateIntent({2u, false, Profile::FiniteTargetMemory1}));
        using S = rhi::OriginalCommandDescriptorSetSource;
        using Assembly = rhi::OriginalCommandResourceSources;
        rhi::OriginalCommandDescriptorSourceBudget b{
            4u, true, 1u, 1u, S::finiteExclusiveRetainedOwnerCount, 1u};
        b.finitePipelineSourceIndex = 2u;
        b.finiteFootprintContractVersion = 1u;
        b.finiteFullBoundViewOptIn = true;
        assert(!S::validateBudget(b));
        b.finiteFullBoundViewOptIn = false;
        assert(S::validateBudget(b));
        for (std::uint32_t assembly = 1u; assembly <= 4u; ++assembly) {
            assert(Assembly::allowsDescriptorSourceVersion(assembly, 2u));
            assert(Assembly::allowsDescriptorSourceVersion(assembly, 3u) == (assembly == 3u));
            assert(Assembly::allowsDescriptorSourceVersion(assembly, 4u) == (assembly == 4u));
        }
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 1u> quotas{
            core::makeShared<core::CpuMemoryQuota>(1u)};
        assert(
            !CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept -> const char* {
                return C::validate(r);
            }));
    }
};
} // namespace arp::test

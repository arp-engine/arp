#pragma once
#include "rhi/frontend/resource_texture_validation.h"

#include <cassert>
#include <cstdio>

namespace arp::test {
class NativeAllocationObservationContractFixture final {
public:
    static void verify()
    {
        using namespace rhi;
        using C = OriginalNativeAllocationObservationContract;
        assert(!C::validateIntent({1u, true}, 3u, 1024u));
        assert(C::validateIntent({0u, true}, 3u, 1024u));
        assert(C::validateIntent({2u, true}, 3u, 1024u));
        assert(C::validateIntent({1u, false}, 3u, 1024u));
        assert(C::validateIntent({1u, true}, 3u, 0u));
        for (const auto version : {1u, 2u, 4u, 5u, 6u, 7u, 8u})
            assert(C::validateIntent({1u, true}, version, 1024u));
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU, Backend::WebGL}) {
            const auto d = C::lowering(backend);
            assert(d.reserveThenObserve == (backend == Backend::Metal));
            assert(d.disposition
                == (backend == Backend::Metal      ? C::Disposition::Prototype
                        : backend == Backend::Null ? C::Disposition::ValidatorOnly
                                                   : C::Disposition::DiagnosticFailure));
            assert(bool(d.diagnostic) == !d.reserveThenObserve);
            for (const auto version : {0u, 2u})
                assert(C::lowering(backend, version).diagnostic
                    && !C::lowering(backend, version).reserveThenObserve);
        }
        assert(!C::validateObserved(1024u, 1024u));
        assert(!C::validateObserved(1024u, 300u));
        assert(C::validateObserved(1023u, 1024u));
        assert(C::validateObserved(0u, 300u));
        assert(C::validateObserved(1024u, 0u));
        OriginalTextureContentCreationBudget budget{3u, true, 1u, 1u, 20u, 4u, 65536u, 0u, 1024u};
        assert(!OriginalTextureContentCreationContract::validateIntent(budget));
        budget.nativeAllocationObservation = C::Intent{1u, true};
        assert(!OriginalTextureContentCreationContract::validateIntent(budget));
        budget.nativeAllocationObservation->prototypeOptIn = false;
        assert(OriginalTextureContentCreationContract::validateIntent(budget));
        budget.nativeAllocationObservation->prototypeOptIn = true;
        budget.contractVersion = 4u;
        budget.maxContentCells = 1u;
        assert(OriginalTextureContentCreationContract::validateIntent(budget));
        std::puts(
            "NATIVE_ALLOCATION_OBSERVATION1_CONTRACT_OK explicit-opt-in reserve-observe-shrink no-creation-grant five-backend");
    }
};
} // namespace arp::test

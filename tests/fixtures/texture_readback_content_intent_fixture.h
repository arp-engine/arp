#pragma once
#include "core/memory/cpu_allocator.h"
#include "rhi/frontend/transfer_readback_content_intent.h"

#include <cassert>
#include <cstdio>
#include <string_view>

namespace arp::test {
class TextureReadbackContentIntentFixture final {
public:
    using Intent = rhi::TextureReadbackContentIntent;

    static Intent::Descriptor descriptor() noexcept
    {
        return {1u, true, Intent::Mode::QueueOrderedSnapshot};
    }

    static void verify()
    {
        const auto previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        auto d = descriptor();
        assert(!Intent::validate(d));
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto l = Intent::lower(backend, d);
            assert(!l.nativeExecutionProven());
            assert(l.mode == Intent::Mode::QueueOrderedSnapshot && l.contractVersion == 1u);
            assert((l.outcome == Intent::Outcome::Prototype) == (backend == rhi::Backend::WebGPU));
            assert(
                (l.outcome == Intent::Outcome::ValidatorOnly) == (backend == rhi::Backend::Null));
            assert(
                bool(Intent::validateDecision(backend, d, l)) == (backend != rhi::Backend::WebGPU));
        }
        assert(Intent::lower(static_cast<rhi::Backend>(99u), d).outcome
            == Intent::Outcome::DiagnosticFailure);
        auto l = Intent::lower(rhi::Backend::WebGPU, d);
        for (auto* field : {&l.contractVersion}) {
            ++*field;
            assert(Intent::validateDecision(rhi::Backend::WebGPU, d, l));
            --*field;
        }
        l.backend = rhi::Backend::Null;
        assert(Intent::validateDecision(rhi::Backend::WebGPU, d, l));
        l = Intent::lower(rhi::Backend::WebGPU, d);
        l.mode = Intent::Mode::CompletedOriginalUpload;
        assert(Intent::validateDecision(rhi::Backend::WebGPU, d, l));
        l = Intent::lower(rhi::Backend::WebGPU, d);
        l.outcome = Intent::Outcome::ValidatorOnly;
        assert(Intent::validateDecision(rhi::Backend::WebGPU, d, l));
        l = Intent::lower(rhi::Backend::WebGPU, d);
        l.diagnostic = "forged";
        assert(Intent::validateDecision(rhi::Backend::WebGPU, d, l));
        d.contractVersion = 0u;
        assert(Intent::validate(d));
        d = descriptor();
        d.prototypeOptIn = false;
        assert(Intent::validate(d));
        d = descriptor();
        for (const auto mode : {Intent::Mode::Unspecified,
                 Intent::Mode::CompletedOriginalUpload,
                 static_cast<Intent::Mode>(99u)}) {
            d.mode = mode;
            assert(Intent::validate(d));
            for (const auto backend : {rhi::Backend::Null,
                     rhi::Backend::Vulkan,
                     rhi::Backend::Metal,
                     rhi::Backend::WebGPU})
                assert(Intent::lower(backend, d).outcome == Intent::Outcome::DiagnosticFailure);
        }
        assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
        std::pmr::set_default_resource(previous);
        std::puts(
            "BINDLESS_RHI_READBACK_CONTENT_INTENT_OK version opt-in default-closed backends4 forged-lowering no-upload-proof allocation0");
    }
};
} // namespace arp::test

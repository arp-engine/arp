#pragma once
#include "rhi/frontend/transfer_readback_input.h"
#include "texture_readback_layout_fixture.h"

namespace arp::test {
class TextureReadbackRequestDeclarationFixture final {
public:
    using Request = rhi::TextureReadbackRequestDeclaration;
    static constexpr std::uint64_t cpuBytes = 65536u;

    static void verify()
    {
        TextureReadbackLayoutFixture::Context c;
        const char* diagnostic = nullptr;
        auto layout = c.prepare(TextureReadbackLayoutFixture::descriptor(),
            TextureReadbackLayoutFixture::texture(),
            diagnostic);
        assert(layout);
        auto q = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        rhi::TextureReadbackIdentity identity{17u, 23u, 29u};
        rhi::TextureReadbackRequestDeclarationBudget b;
        assert(Request::validateBudget(b));
        b.contractVersion = 1u;
        assert(Request::validateBudget(b));
        b.prototypeOptIn = true;
        assert(Request::validateBudget(b));
        b = {1u, true, 2u, layout->inputOwner()->label().size(), 5120u, cpuBytes};
        const auto factory = [&] {
            return Request::prepareOriginal(layout, identity, b, q, diagnostic);
        };
        CpuQuotaFactoryFixture pressure(q);
        const auto peak = pressure.measure(factory);
        pressure.verify(peak, factory);
        for (auto* field :
            {&identity.requestId, &identity.frameSerial, &identity.resourceGeneration}) {
            const auto saved = *field;
            *field = 0;
            assert(!factory() && std::string_view(diagnostic) == "RHI-READBACK-REQUEST-IDENTITY");
            *field = saved;
        }
        for (auto* field : {&b.maxLabelBytes, &b.maxOriginalReadbackBytes}) {
            --*field;
            assert(!factory() && std::string_view(diagnostic) == "RHI-READBACK-REQUEST-CAPACITY");
            ++*field;
        }
        --b.maxMipCopies;
        assert(!factory());
        ++b.maxMipCopies;
        --b.maxCpuMetadataBytes;
        assert(!factory());
        ++b.maxCpuMetadataBytes;
        {
            core::CpuMemoryQuotaLease full(q, cpuBytes);
            assert(full.charged() && !factory());
        }
        auto request = factory();
        assert(request && request->identity() == identity
            && request->source() == rhi::GpuResourceId({17u})
            && request->format() == rhi::TextureFormat::Rgba8Unorm
            && !request->nativeExecutionProven());
        assert(request->label().data() == layout->inputOwner()->label().data()
            && !Request::validateOriginal(request, layout));
        auto forged = std::shared_ptr<const Request>(q, request.get());
        assert(Request::validateOriginal(forged, layout));
        forged.reset();
        auto foreign = c.prepare(TextureReadbackLayoutFixture::descriptor(),
            TextureReadbackLayoutFixture::texture(),
            diagnostic);
        assert(Request::validateOriginal(request, foreign));
        foreign.reset();
        auto saved = layout;
        layout = std::shared_ptr<const rhi::TextureReadbackLayout>(q, saved.get());
        assert(!factory());
        layout = saved;
        saved.reset();
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU})
            assert(Request::lowering(backend).cpuMetadataImplemented);
        request.reset();
        layout.reset();
        assert(!q->liveBytes());
        c.retired();
        std::printf(
            "BINDLESS_RHI_READBACK_REQUEST_OK %llu caller-identity3 exact-layout-source-label quotas foreign-forged native-closed\n",
            static_cast<unsigned long long>(peak));
    }
};
} // namespace arp::test

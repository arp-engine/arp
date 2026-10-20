#pragma once
#include "rhi/frontend/transfer_readback_input.h"
#include "texture_readback_layout_fixture.h"

namespace arp::test {
class TextureReadbackStatisticsPlanFixture final {
public:
    using Plan = rhi::TextureReadbackStatisticsPlan;
    using Budget = rhi::TextureReadbackStatisticsPlanBudget;
    static constexpr std::uint64_t cpuBytes = 65536u;

    static void verify()
    {
        Budget b;
        assert(Plan::validateBudget(b));
        b.contractVersion = 1u;
        assert(Plan::validateBudget(b));
        b.prototypeOptIn = true;
        assert(Plan::validateBudget(b));
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto l = Plan::lowering(backend);
            assert(l.cpuPlanImplemented && l.validatorOnly == (backend == rhi::Backend::Null)
                && l.diagnostic);
        }
        assert(!Plan::lowering(static_cast<rhi::Backend>(99u)).cpuPlanImplemented);
        TextureReadbackLayoutFixture::Context c;
        const char* diagnostic = nullptr;
        auto layout = c.prepare(TextureReadbackLayoutFixture::descriptor(),
            TextureReadbackLayoutFixture::texture(),
            diagnostic);
        assert(layout);
        auto q = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        b = {1u, true, 4u, 40u, 5120u, cpuBytes};
        const auto factory = [&] {
            return Plan::prepareOriginal(layout, b, q, diagnostic);
        };
        CpuQuotaFactoryFixture pressure(q);
        const auto peak = pressure.measure(factory);
        pressure.verify(peak, factory);
        const auto reject = [&](const char* id) {
            const auto a = core::defaultCpuAllocator().stats().allocationCount;
            assert(!factory() && std::string_view(diagnostic) == id && !q->liveBytes()
                && a == core::defaultCpuAllocator().stats().allocationCount);
        };
        --b.maxViews;
        reject("RHI-READBACK-STATISTICS-VIEW-CAPACITY");
        ++b.maxViews;
        --b.maxAnalyzedPixels;
        reject("RHI-READBACK-STATISTICS-PIXEL-CAPACITY");
        ++b.maxAnalyzedPixels;
        --b.maxOriginalReadbackBytes;
        reject("RHI-READBACK-STATISTICS-BYTE-CAPACITY");
        ++b.maxOriginalReadbackBytes;
        --b.maxCpuMetadataBytes;
        reject("RHI-READBACK-STATISTICS-QUOTA");
        ++b.maxCpuMetadataBytes;
        auto saved = layout;
        layout = std::shared_ptr<const rhi::TextureReadbackLayout>(q, saved.get());
        reject("RHI-READBACK-STATISTICS-LAYOUT-OWNER");
        layout = saved;
        saved.reset();
        {
            core::CpuMemoryQuotaLease full(q, cpuBytes);
            assert(full.charged() && !factory());
        }
        b.maxViews = UINT32_MAX;
        auto plan = factory();
        assert(plan && plan->views().size() == 4u && plan->analyzedPixels() == 40u
            && !plan->nativePublicationProven());
        const std::array<std::uint64_t, 4> offsets{256u, 1536u, 3328u, 4096u},
            spans{784u, 784u, 264u, 264u};
        for (std::size_t i = 0; i < 4u; ++i) {
            const auto& v = plan->views()[i];
            assert(v.byteOffset == offsets[i] && v.byteSpan == spans[i] && v.mip == 1u + i / 2u
                && v.logicalSlice == 1u + i % 2u && v.sliceOrdinal == i % 2u
                && v.domain == Plan::SliceDomain::ArrayLayer);
        }
        assert(!Plan::validateOriginal(plan, layout));
        auto foreign = c.prepare(TextureReadbackLayoutFixture::descriptor(),
            TextureReadbackLayoutFixture::texture(),
            diagnostic);
        assert(Plan::validateOriginal(plan, foreign));
        foreign.reset();
        auto forged = std::shared_ptr<const Plan>(q, plan.get());
        assert(Plan::validateOriginal(forged, layout));
        forged.reset();
        plan.reset();
        layout.reset();
        assert(!q->liveBytes());
        c.retired();
        TextureReadbackLayoutFixture::verifyGeometry(inspectGeometry);
        verifyLarge();
        std::printf(
            "BINDLESS_RHI_READBACK_STATISTICS_PLAN_OK %llu exact-array-depth-mips padding-excluded quotas foreign-forged sample-overflow backends4\n",
            static_cast<unsigned long long>(peak));
    }

private:
    static void inspectGeometry(const std::shared_ptr<const rhi::TextureReadbackLayout>& layout)
    {
        auto q = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        const char* diagnostic = nullptr;
        auto plan =
            Plan::prepareOriginal(layout, {1u, true, 6u, 1024u, 8192u, cpuBytes}, q, diagnostic);
        if (!rhi::textureReadbackChannelCount(layout->sourceLayout().format)) {
            assert(!plan && std::string_view(diagnostic) == "RHI-READBACK-STATISTICS-FORMAT");
            return;
        }
        assert(plan);
        const bool volume = layout->sourceLayout().extent.depth > 1u;
        std::size_t index = 0;
        for (const auto& m : layout->mipCopies())
            for (std::uint32_t s = 0; s < m.sliceCount; ++s) {
                const auto& v = plan->views()[index++];
                assert(v.domain
                    == (volume ? Plan::SliceDomain::DepthSlice : Plan::SliceDomain::ArrayLayer));
                assert(v.logicalSlice == (volume ? s : m.range.baseArrayLayer + s)
                    && v.byteOffset
                        == m.bufferOffsetBytes + std::uint64_t(s) * m.bytesPerRow * m.rowsPerImage
                    && v.byteSpan == std::uint64_t(m.rowCount - 1u) * m.bytesPerRow + m.rowBytes);
            }
        assert(index == plan->views().size());
        plan.reset();
        assert(!q->liveBytes());
    }

    static void verifyLarge()
    {
        auto t = TextureReadbackLayoutFixture::texture();
        t.extent = {65536u, 65536u, 1u};
        t.mipLevelCount = t.arrayLayerCount = 1u;
        auto d = TextureReadbackLayoutFixture::descriptor();
        d.mipLayouts.clear();
        d.sourceSubresources = {0u, 1u, 0u, 1u, rhi::TextureAspect::Color};
        d.extent = t.extent;
        d.bytesPerRow = 262144u;
        d.rowsPerImage = 65536u;
        d.destinationOffsetBytes = 0u;
        d.sizeBytes = 17179869184ull;
        TextureReadbackLayoutFixture::Context c;
        const char* diagnostic = nullptr;
        auto layout = c.prepare(d, t, diagnostic, d.sizeBytes);
        assert(layout);
        auto q = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        assert(!Plan::prepareOriginal(
            layout, {1u, true, 1u, UINT64_MAX, UINT64_MAX, cpuBytes}, q, diagnostic));
        // Wasm rejects the original storage capacity first; 64-bit hosts reach sample-count overflow.
        assert(std::string_view(diagnostic)
            == (SIZE_MAX < d.sizeBytes ? "RHI-READBACK-STATISTICS-BYTE-CAPACITY"
                                       : "RHI-READBACK-STATISTICS-SAMPLE-COUNT"));
        layout.reset();
        c.retired();
        assert(!q->liveBytes());
    }
};
} // namespace arp::test

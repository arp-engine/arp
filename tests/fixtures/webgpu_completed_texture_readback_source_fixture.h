#pragma once
#include "rhi/backends/webgpu/webgpu_readback_preparation.h"
#include "texture_readback_layout_fixture.h"

namespace arp::test {
class WebGpuCompletedTextureReadbackSourceFixture final {
public:
    using Source = rhi_webgpu::WebGpuOriginalTextureReadbackSource;
    using View = rhi_webgpu::WebGpuCompletedTextureUploadDeclaration;
    using Budget = rhi_webgpu::WebGpuCompletedTextureReadbackSourceBudget;
    static constexpr std::uint64_t cpuBytes = 65536u;

    static Budget budget() { return {1u, true, 1u, 4u, 2u, 2u, 1360u, 2896u, 1696u, cpuBytes}; }

    class Context final {
    public:
        TextureReadbackLayoutFixture::Context layoutContext;
        std::shared_ptr<core::CpuMemoryQuota> quota =
            core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        std::shared_ptr<const rhi::TextureReadbackLayout> layout;

        explicit Context(
            const std::shared_ptr<const View>& view, bool excessive = false, bool foreign = false)
        {
            assert(view && view->completedTexture());
            const char* diagnostic = nullptr;
            auto d = TextureReadbackLayoutFixture::descriptor();
            d.sourceResource = view->completedTexture()->id();
            if (foreign)
                d.sourceResource = rhi::allocateGpuResourceId();
            d.destinationReadbackBuffer = rhi::allocateGpuResourceId();
            d.sizeBytes = 1536u;
            d.destinationOffsetBytes = 0u;
            d.extent = {excessive ? 4u : 3u, 2u, 1u};
            d.rowsPerImage = 2u;
            d.mipLayouts.clear();
            d.mipLayouts.push_back({1u, d.extent, 256u, 2u, 0u});
            d.mipLayouts.push_back({2u, {1u, 1u, 1u}, 256u, 1u, 1024u});
            layout = layoutContext.prepare(d, view->completedTexture()->desc(), diagnostic, 1536u);
            assert(layout);
        }

        auto prepare(
            const std::shared_ptr<const View>& view, const Budget& b, const char*& diagnostic)
        {
            return Source::prepareCompletedOriginal(layout, view, b, quota, diagnostic);
        }

        void retire()
        {
            layout.reset();
            assert(!quota->liveBytes());
            layoutContext.retired();
        }
    };

    static void verifyContract()
    {
        Budget b;
        assert(
            std::string_view(Source::validateBudget(b)) == "RHI-READBACK-COMPLETED-SOURCE-VERSION");
        b.contractVersion = 1u;
        assert(
            std::string_view(Source::validateBudget(b)) == "RHI-READBACK-COMPLETED-SOURCE-OPT-IN");
        b.prototypeOptIn = true;
        assert(
            std::string_view(Source::validateBudget(b)) == "RHI-READBACK-COMPLETED-SOURCE-BUDGET");
        b = budget();
        assert(!Source::validateBudget(b));
        auto quota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        const char* diagnostic = nullptr;
        assert(!Source::prepareCompletedOriginal({}, {}, b, quota, diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-TEXTURE-COMPLETED-OWNER"
            && !quota->liveBytes());
        std::puts(
            "BINDLESS_RHI_COMPLETED_READBACK_SOURCE_CONTRACT_OK sealed profile budgets-independent execution-closed");
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void verifyNonzeroOrigin(const std::shared_ptr<const View>& view)
    {
        Context c(view);
        const char* diagnostic = nullptr;
        assert(!c.prepare(view, budget(), diagnostic)
            && std::string_view(diagnostic) == "RHI-TEXTURE-READBACK-COVERAGE-REGION");
        c.retire();
    }

    static void verifyActual(const std::shared_ptr<const View>& view)
    {
        Context c(view);
        auto b = budget();
        const char* diagnostic = nullptr;
        auto original = view;
        const auto factory = [&] {
            return c.prepare(original, b, diagnostic);
        };
        CpuQuotaFactoryFixture pressure(c.quota);
        const auto peak = pressure.measure(factory);
        pressure.verify(peak, factory);
        {
            core::CpuMemoryQuotaLease full(c.quota, cpuBytes);
            assert(full.charged() && !factory());
        }
        assert(!c.quota->liveBytes());
        const auto reject = [&](const char* id) {
            assert(!factory() && std::string_view(diagnostic) == id && !c.quota->liveBytes()
                && !View::validateOriginal(view));
        };
        --b.maxCpuMetadataBytes;
        reject("RHI-READBACK-COMPLETED-SOURCE-QUOTA");
        b = budget();
        --b.maxSourceOwners;
        reject("RHI-READBACK-COMPLETED-SOURCE-CAPACITY");
        b = budget();
        --b.maxTargetAllocationBytes;
        reject("RHI-READBACK-COMPLETED-SOURCE-CAPACITY");
        b = budget();
        --b.maxRetainedNativeBytes;
        reject("RHI-READBACK-COMPLETED-SOURCE-CAPACITY");
        b = budget();
        --b.maxRetainedContentBytes;
        reject("RHI-READBACK-COMPLETED-SOURCE-CAPACITY");
        b = budget();
        --b.maxReadbackMips;
        reject("RHI-TEXTURE-READBACK-COVERAGE-CAPACITY");
        b = budget();
        --b.maxUploadMips;
        reject("RHI-TEXTURE-READBACK-COVERAGE-CAPACITY");
        b = budget();
        original = std::shared_ptr<const View>(c.quota, view.get());
        reject("RHI-WEBGPU-TEXTURE-COMPLETED-OWNER");
        original = view;
        auto layout = c.layout;
        c.layout = std::shared_ptr<const rhi::TextureReadbackLayout>(c.quota, layout.get());
        reject("RHI-TEXTURE-READBACK-COVERAGE-READBACK-OWNER");
        c.layout = layout;
        layout.reset();
        {
            Context excessive(view, true);
            assert(!excessive.prepare(view, b, diagnostic)
                && std::string_view(diagnostic) == "RHI-TEXTURE-READBACK-COVERAGE-REGION");
            excessive.retire();
        }
        {
            Context foreign(view, false, true);
            assert(!foreign.prepare(view, b, diagnostic)
                && std::string_view(diagnostic) == "RHI-TEXTURE-READBACK-COVERAGE-SOURCE-ID");
            foreign.retire();
        }
        auto source = factory();
        assert(source && !source->validate() && source->sourceTexture() == view->completedTexture()
            && source->hasCompletedUpload() && source->retainedSourceOwnerCount() == 4u
            && source->retainedNativeAllocationBytes() == 2896u
            && source->retainedContentBytes() == 1696u
            && core::SharedOwnerProjection::sameOwner(
                source->registry(), view->completedRegistry()));
        assert(std::string_view(source->executionDiagnostic())
            == "RHI-READBACK-SOURCE-EXECUTION-UNPROVEN");
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        auto& native = *view->completedTextureResource().texture;
        const auto extent = native.extent;
        ++native.extent.width;
        assert(source->validate());
        native.extent = extent;
        assert(native.contentAccessPolicy().ordinaryAccessDiagnostic());
        core::MutationJournal journal;
        assert(!native.recordState(native.recordedState().state(), &journal));
        assert(std::string_view(source->validate()) == "RHI-READBACK-SOURCE-STATE-PENDING");
        journal.rollback();
        assert(std::string_view(source->validate()) == "RHI-READBACK-SOURCE-STATE-STALE"
            && !View::validateOriginal(view));
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        std::pmr::set_default_resource(previous);
        source.reset();
        source = factory();
        assert(source && !source->validate());
        assert(!native.recordState(rhi::ResourceState::CopySrc));
        assert(std::string_view(source->validate()) == "RHI-READBACK-SOURCE-STATE-STALE");
        source.reset();
        source = factory();
        assert(source && !source->validate());
        source.reset();
        original.reset();
        c.retire();
        std::printf(
            "BINDLESS_RHI_COMPLETED_READBACK_SOURCE_NATIVE_OK %llu original-object graph1 owners4 native2896 content1696 coverage2 state-revision allocation0 execution-closed\n",
            static_cast<unsigned long long>(peak));
    }
#endif
};
} // namespace arp::test

#pragma once
#include "rhi/backends/webgpu/webgpu_readback_results.h"
#include "texture_readback_cold_budget_fixture.h"

namespace arp::test {
class TextureReadbackOwnedSnapshotFixture final {
    using Snapshot = rhi_webgpu::WebGpuOwnedTextureReadbackSnapshot;
    using Contract = rhi::TextureReadbackOwnedSnapshotContract;
    using Mode = rhi::TextureReadbackContentIntent::Mode;

public:
    static void verifyCold(const std::shared_ptr<const rhi::TextureReadbackColdBudgetPlan>& plan,
        const std::shared_ptr<const rhi::TextureReadbackRequestDeclaration>& request,
        const std::shared_ptr<const rhi::TextureReadbackStatisticsPlan>& statistics,
        const rhi::ReadbackTextureDesc& descriptor,
        const rhi::TextureDesc& texture)
    {
        const char* id = nullptr;
        const auto q = plan->quotas();
        const auto factory = [&] {
            return Snapshot::prepareOriginal(
                plan, request, statistics, Mode::QueueOrderedSnapshot, id);
        };
        std::array<std::uint64_t, 2> peaks{};
        for (std::size_t i = 0; i < peaks.size(); ++i) {
            CpuQuotaFactoryFixture pressure(q[i ? 3u : 1u]);
            peaks[i] = pressure.measure(factory);
            pressure.verify(peaks[i], factory);
        }
        auto value = factory();
        assert(value && !value->publishedBytes() && !value->publishedAnalyses()
            && !value->analysisCount());
        assert(value->byteCapacity() == statistics->originalByteCapacity()
            && value->originalRequest() == request
            && value->originalStatisticsPlan() == statistics);
        std::shared_ptr<const rhi::TextureReadbackOwnedSnapshot> output = value;
        const auto a = core::defaultCpuAllocator().stats().allocationCount;
        assert(std::string_view(Contract::validatePublishedOriginal(output))
            == "RHI-READBACK-OWNED-SNAPSHOT-UNPUBLISHED");
        auto forged = std::shared_ptr<const rhi::TextureReadbackOwnedSnapshot>(q[1], value.get());
        assert(std::string_view(Contract::validatePublishedOriginal(forged))
            == "RHI-READBACK-OWNED-SNAPSHOT-OWNER");
        assert(a == core::defaultCpuAllocator().stats().allocationCount);
        forged.reset();
        output.reset();
        value.reset();
        assert(!Snapshot::prepareOriginal(plan, {}, statistics, Mode::QueueOrderedSnapshot, id));
        assert(!Snapshot::prepareOriginal(plan, request, {}, Mode::QueueOrderedSnapshot, id));
        assert(!Snapshot::prepareOriginal(plan, request, statistics, Mode::Unspecified, id));
        auto foreign =
            std::shared_ptr<const rhi::TextureReadbackStatisticsPlan>(q[1], statistics.get());
        assert(!Snapshot::prepareOriginal(plan, request, foreign, Mode::QueueOrderedSnapshot, id));
        foreign.reset();
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto d = Contract::lowering(backend);
            assert(!d.executionAvailable && d.diagnostic);
        }
        // Author exact Result capacity for both simultaneous CPU byte copies.
        const auto bytes = statistics->originalByteCapacity();
        for (unsigned less = 0u; less < 2u; ++less) {
            auto b = TextureReadbackColdBudgetFixture::budget();
            b.maxResultBytesPerRequest = bytes;
            b.maxCpuResultBytes = bytes + bytes - less;
            auto recipe = TextureReadbackColdBudgetFixture::prepare(b);
            const auto domains = recipe.originalPlan->quotas();
            auto input = rhi::TextureReadbackInputSnapshot::prepareOriginal(
                descriptor, recipe.input, domains[1], domains[1], id);
            assert(input);
            auto layout = rhi::TextureReadbackLayout::prepareOriginal(
                input, texture, recipe.layout, domains[1], id);
            assert(layout);
            auto r = rhi::TextureReadbackRequestDeclaration::prepareOriginal(
                layout, request->identity(), recipe.request, domains[1], id);
            assert(r);
            auto stats = rhi::TextureReadbackStatisticsPlan::prepareOriginal(
                layout, recipe.statistics, domains[1], id);
            assert(stats);
            auto snapshot = Snapshot::prepareOriginal(
                recipe.originalPlan, r, stats, Mode::QueueOrderedSnapshot, id);
            assert(bool(snapshot) == !less);
            if (less)
                assert(std::string_view(id) == "RHI-READBACK-OWNED-SNAPSHOT-RESULT-CAPACITY");
        }
        std::printf(
            "BINDLESS_RHI_OWNED_SNAPSHOT_COLD_OK %llu %llu CPU-only Request-Result exact-less-exhausted two-copies backends4 forged-unpublished no-native-proof\n",
            static_cast<unsigned long long>(peaks[0]),
            static_cast<unsigned long long>(peaks[1]));
    }
};
} // namespace arp::test

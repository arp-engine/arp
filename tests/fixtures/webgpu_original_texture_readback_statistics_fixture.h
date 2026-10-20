#pragma once
#include "rhi/backends/webgpu/webgpu_readback_results.h"
#include "rhi/frontend/transfer_readback_owned_preparation.h"
#include "rhi/frontend/transfer_upload_packing.h"
#include "texture_readback_cold_budget_fixture.h"
#include "texture_readback_statistics_plan_fixture.h"

namespace arp::test {
class WebGpuOriginalTextureReadbackStatisticsFixture final {
public:
    using Consumer = rhi_webgpu::WebGpuOriginalTextureReadbackStatistics;
    using Analysis = Consumer::Analysis;
    using Plan = rhi::TextureReadbackStatisticsPlan;
    using Result = rhi_webgpu::WebGpuOriginalTextureReadbackResult;
    using Numerical = rhi_webgpu::WebGpuOriginalTextureReadbackNumericalResult;
    static constexpr auto cpuBytes = TextureReadbackStatisticsPlanFixture::cpuBytes;

    template <class A, class B> static void equal(const A& a, const B& b)
    {
        assert(a.valid == b.valid && a.minValue == b.minValue && a.maxValue == b.maxValue
            && a.meanValue == b.meanValue && a.centerValue == b.centerValue && a.minX == b.minX
            && a.minY == b.minY && a.maxX == b.maxX && a.maxY == b.maxY
            && a.sampleHash == b.sampleHash && a.finiteCount == b.finiteCount
            && a.sampleCount == b.sampleCount);
    }

    // Compare the legacy numerical ABI using these exact real snapshot bytes;
    // this creates no legacy GPU proof and never feeds the global result queue.
    static void verifyLegacySnapshot(
        const std::shared_ptr<const rhi::TextureReadbackOwnedSnapshot>& snapshot)
    {
        assert(!rhi::TextureReadbackOwnedSnapshotContract::validatePublishedOriginal(snapshot));
        const auto& plan = *snapshot->originalStatisticsPlan();
        const auto format = plan.originalLayout()->sourceLayout().format;
        const auto a = core::defaultCpuAllocator().stats().allocationCount;
        for (std::size_t i = 0u; i < plan.views().size(); ++i) {
            const auto& v = plan.views()[i];
            assert(core::containsUnsignedRange<std::uint64_t>(
                0u, snapshot->byteCapacity(), v.byteOffset, v.byteSpan));
            rhi::TextureReadbackSample legacy;
            rhi::applyTextureReadbackStatistics(legacy,
                format,
                snapshot->publishedBytes() + v.byteOffset,
                v.byteSpan,
                v.width,
                v.height,
                v.bytesPerRow);
            const auto& values = snapshot->publishedAnalyses()[i];
            equal(values.scalar, legacy);
            assert(legacy.channelStatisticsVersion == 1u
                && legacy.channelStatistics.valid == values.channels.valid
                && legacy.channelStatistics.channelCount == values.channels.channelCount);
            for (std::uint32_t channel = 0u; channel < values.channels.channelCount; ++channel)
                equal(
                    values.channels.channels[channel], legacy.channelStatistics.channels[channel]);
        }
        assert(a == core::defaultCpuAllocator().stats().allocationCount);
    }

    static void verifyContract()
    {
        TextureReadbackStatisticsPlanFixture::verify();
        Analysis output;
        rhi_webgpu::WebGpuOriginalTextureReadbackNumericalBudget b;
        assert(Numerical::validateBudget(b));
        b.contractVersion = 1u;
        assert(Numerical::validateBudget(b));
        b.prototypeOptIn = true;
        assert(Numerical::validateBudget(b));
        const char* diagnostic = nullptr;
        assert(!Numerical::prepareOriginal({}, {}, b, {}, diagnostic)
            && Numerical::attachOriginal({}, {}));
        std::puts(
            "BINDLESS_RHI_READBACK_NUMERICAL_CONTRACT_OK backends4 cold-owner all-original-evidence no-new-native-proof");
        assert(Consumer::analyzePublishedOriginal({}, {}, 0u, output) && !output.scalar.valid
            && !output.channels.valid);
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto l = Consumer::lowering(backend);
            assert(l.publishedConsumerImplemented == (backend == rhi::Backend::WebGPU)
                && l.validatorOnly == (backend == rhi::Backend::Null) && l.diagnostic);
        }
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    class Context final {
    public:
        std::shared_ptr<core::CpuMemoryQuota> quota =
            core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        std::shared_ptr<const Plan> plan;
        core::pmr::Vector<Analysis> expected{&core::defaultCpuMemoryResource()};
        std::shared_ptr<core::CpuMemoryQuota> numericalQuota =
            core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        std::shared_ptr<Numerical> numerical;
        std::optional<rhi_webgpu::WebGpuOriginalTextureReadbackNumericalBudget>
            loweredNumericalBudget;

        Context(const std::shared_ptr<const Result>& result,
            const std::shared_ptr<const rhi::TextureUploadPacking>& packing = {},
            bool bounds = false,
            const TextureReadbackColdBudgetFixture::Recipe* recipe = nullptr,
            const std::shared_ptr<const Plan>& originalPlan = {})
        {
            const char* diagnostic = nullptr;
            rhi::TextureReadbackStatisticsPlanBudget b{1u, true, 4u, 20u, 1536u, cpuBytes};
            if (recipe) {
                quota = recipe->originalPlan->quotas()[1];
                numericalQuota = quota;
                b = recipe->statistics;
                loweredNumericalBudget = recipe->numerical;
            }
            const auto factory = [&] {
                return Plan::prepareOriginal(result->layout(), b, quota, diagnostic);
            };
            if (bounds) {
                CpuQuotaFactoryFixture pressure(quota);
                const auto peak = pressure.measure(factory);
                pressure.verify(peak, factory);
                std::printf(
                    "BINDLESS_RHI_READBACK_STATISTICS_COLD_OK %llu exact-less-exhausted original-layout\n",
                    static_cast<unsigned long long>(peak));
            }
            plan = originalPlan ? originalPlan : factory();
            assert(plan && !Plan::validateOriginal(plan, result->layout()));
            assert(!originalPlan
                || core::SharedOwnerProjection::sameOwner(plan->originalMetadataQuota(), quota));
            Analysis output;
            assert(Consumer::analyzePublishedOriginal(plan, result, 0u, output)
                && !output.scalar.valid && !output.channels.valid);
            auto forged = std::shared_ptr<const Result>(quota, result.get());
            assert(Consumer::analyzePublishedOriginal(plan, forged, 0u, output));
            if (packing) {
                core::CpuMemoryQuotaScope scope(quota);
                expected.reserve(plan->views().size());
                for (const auto& v : plan->views()) {
                    const auto& m =
                        packing->mipLayouts()[v.mip - packing->mipLayouts()[0].range.baseMipLevel];
                    assert(m.range.baseMipLevel == v.mip && m.extent.width == v.width
                        && m.extent.height == v.height);
                    std::uint64_t offset = 0;
                    assert(core::StridedIndexRange(m.sourceOffset,
                        m.sliceCount,
                        std::uint64_t(m.sourceBytesPerRow) * m.sourceRowsPerImage)
                            .index(v.sliceOrdinal, offset));
                    const auto& bytes = packing->inputOwner()->pixelBytes();
                    const auto format = packing->destinationLayout().format;
                    expected.push_back({rhi::analyzeTextureReadback(format,
                                            bytes.data() + offset,
                                            bytes.size() - offset,
                                            v.width,
                                            v.height,
                                            m.sourceBytesPerRow),
                        rhi::analyzeTextureReadbackChannels(format,
                            bytes.data() + offset,
                            bytes.size() - offset,
                            v.width,
                            v.height,
                            m.sourceBytesPerRow)});
                    assert(expected.back().scalar.valid && expected.back().channels.valid);
                }
            }
        }

        void attachNumerical(
            const std::shared_ptr<rhi_webgpu::WebGpuOriginalTextureReadbackMapping>& mapping,
            bool bounds)
        {
            const char* diagnostic = nullptr;
            rhi_webgpu::WebGpuOriginalTextureReadbackNumericalBudget b{1u,
                true,
                static_cast<std::uint32_t>(plan->views().size()),
                plan->analyzedPixels(),
                mapping->resultDeclaration()->byteCapacity(),
                cpuBytes};
            if (loweredNumericalBudget)
                b = *loweredNumericalBudget;
            auto source = mapping->resultDeclaration();
            const auto factory = [&] {
                return Numerical::prepareOriginal(plan, source, b, numericalQuota, diagnostic);
            };
            if (bounds) {
                const auto previous =
                    std::pmr::set_default_resource(std::pmr::null_memory_resource());
                {
                    auto explicitCore = factory();
                    assert(explicitCore);
                }
                std::pmr::set_default_resource(previous);
                CpuQuotaFactoryFixture pressure(numericalQuota);
                const auto peak = pressure.measure(factory);
                pressure.verify(peak, factory);
                {
                    core::CpuMemoryQuotaLease full(numericalQuota, cpuBytes);
                    assert(full.charged() && !factory());
                }
                for (auto* value : {&b.maxAnalyzedPixels, &b.maxRetainedResultBytes}) {
                    --*value;
                    assert(!factory()
                        && std::string_view(diagnostic) == "RHI-READBACK-NUMERICAL-CAPACITY");
                    ++*value;
                }
                --b.maxViews;
                assert(!factory());
                ++b.maxViews;
                --b.maxCpuMetadataBytes;
                assert(!factory());
                ++b.maxCpuMetadataBytes;
                auto saved = source;
                source = std::shared_ptr<const Result>(numericalQuota, saved.get());
                assert(!factory());
                source = saved;
                std::printf(
                    "BINDLESS_RHI_READBACK_NUMERICAL_COLD_OK %llu exact-less-exhausted quota-and-work-bounds\n",
                    static_cast<unsigned long long>(peak));
            }
            numerical = factory();
            assert(
                numerical && !numerical->nativeExecutionProven() && !numerical->publishedValues());
            auto forged = std::shared_ptr<Numerical>(quota, numerical.get());
            assert(Numerical::attachOriginal(forged, mapping));
            auto forgedMap = std::shared_ptr<rhi_webgpu::WebGpuOriginalTextureReadbackMapping>(
                quota, mapping.get());
            assert(Numerical::attachOriginal(numerical, forgedMap));
            const auto a = core::defaultCpuAllocator().stats().allocationCount;
            assert(!Numerical::attachOriginal(numerical, mapping)
                && Numerical::attachOriginal(numerical, mapping));
            assert(core::defaultCpuAllocator().stats().allocationCount == a);
            auto duplicate = factory();
            assert(duplicate && Numerical::attachOriginal(duplicate, mapping));
        }

        void verifyUnavailable() const
        {
            assert(numerical->outcome() == Numerical::Outcome::Unavailable
                && !numerical->publishedValues() && !numerical->diagnostic());
        }

        void verifyFailure(bool loss = false) const
        {
            assert(numerical->outcome()
                    == (loss ? Numerical::Outcome::DeviceLost : Numerical::Outcome::Rejected)
                && !numerical->publishedValues() && numerical->diagnostic());
            std::puts(loss
                    ? "BINDLESS_RHI_READBACK_NUMERICAL_LOSS_OK original-loss stable-no-views"
                    : "BINDLESS_RHI_READBACK_NUMERICAL_FAILURE_OK all-original-callbacks diagnostic-no-views");
        }

        void verifyPublished(const std::shared_ptr<const Result>& result)
        {
            assert(numerical->outcome() == Numerical::Outcome::Completed
                && numerical->publishedValues() && !numerical->diagnostic());
            const auto& callbackValues = *numerical->publishedValues();
            assert(callbackValues.size() == plan->views().size());
            std::puts(
                "BINDLESS_RHI_READBACK_NUMERICAL_NATIVE_OK callback-completed-before-poll exact-original allocation0");
            if (expected.empty())
                return;
            const auto a = core::defaultCpuAllocator().stats().allocationCount;
            Analysis output;
            core::CpuMemoryQuotaLease full(quota, quota->limitBytes() - quota->liveBytes());
            assert(full.charged());
            auto forgedPlan = std::shared_ptr<const Plan>(quota, plan.get());
            assert(Consumer::analyzePublishedOriginal(forgedPlan, result, 0u, output));
            auto forgedResult = std::shared_ptr<const Result>(quota, result.get());
            assert(Consumer::analyzePublishedOriginal(plan, forgedResult, 0u, output));
            assert(Consumer::analyzePublishedOriginal(plan, result, plan->views().size(), output));
            assert(Consumer::analyzePublishedOriginal({}, result, 0u, output));
            for (std::size_t i = 0; i < expected.size(); ++i) {
                assert(!Consumer::analyzePublishedOriginal(plan, result, i, output));
                equal(output.scalar, expected[i].scalar);
                equal(callbackValues[i].scalar, expected[i].scalar);
                assert(output.channels.valid
                    && output.channels.channelCount == expected[i].channels.channelCount);
                for (std::size_t j = 0; j < output.channels.channelCount; ++j) {
                    equal(output.channels.channels[j], expected[i].channels.channels[j]);
                    equal(callbackValues[i].channels.channels[j], expected[i].channels.channels[j]);
                }
            }
            assert(a == core::defaultCpuAllocator().stats().allocationCount);
            std::printf(
                "BINDLESS_RHI_READBACK_STATISTICS_NATIVE_OK %zu original-authored scalar-channels-hashes padding-excluded sealed-snapshot allocation0\n",
                expected.size());
        }
    };
#endif
};
} // namespace arp::test

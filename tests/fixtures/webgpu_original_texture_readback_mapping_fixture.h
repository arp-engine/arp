#pragma once
#include "rhi/backends/webgpu/webgpu_readback_results.h"
#include "webgpu_original_texture_readback_recording_fixture.h"

namespace arp::test {
class WebGpuOriginalTextureReadbackMappingFixture final {
public:
    using Mapping = rhi_webgpu::WebGpuOriginalTextureReadbackMapping;
    using Copies = rhi_webgpu::WebGpuOriginalTextureReadbackCopies;
    static constexpr auto cpuBytes = WebGpuOriginalTextureReadbackRecordingFixture::cpuBytes;

    static auto budget()
    {
        return rhi_webgpu::WebGpuOriginalTextureReadbackMappingBudget{1u,
            true,
            1u,
            Mapping::callbackCount,
            1u,
            2u,
            4432u,
            1696u,
            1536u,
            cpuBytes,
            cpuBytes,
            Mapping::scopeCallbackCount};
    }

    static void verifyContract()
    {
        rhi_webgpu::WebGpuOriginalTextureReadbackMappingBudget b;
        assert(std::string_view(Mapping::validateBudget(b)) == "RHI-WEBGPU-READBACK-MAP-VERSION");
        b.contractVersion = 1u;
        assert(std::string_view(Mapping::validateBudget(b)) == "RHI-WEBGPU-READBACK-MAP-OPT-IN");
        b = budget();
        assert(!Mapping::validateBudget(b));
        assert(std::string_view(Mapping::start({})) == "RHI-WEBGPU-READBACK-MAP-OWNER");
        std::puts(
            "BINDLESS_RHI_READBACK_COLD_MAP_CONTRACT_OK owned-pmr-storage core-exclusive-ticket callbacks1 declarations1 no-native-proof");
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void verifyActual(const std::shared_ptr<const Copies>& copies)
    {
        auto b = budget();
        const char* diagnostic = nullptr;
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 2> quotas;
        for (auto& q : quotas)
            q = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        const auto factory = [&] {
            return Mapping::prepareOriginal(copies, b, quotas[0], quotas[1], diagnostic);
        };
        std::array<std::uint64_t, 2> peaks;
        for (std::size_t i = 0u; i < quotas.size(); ++i) {
            CpuQuotaFactoryFixture pressure(quotas[i]);
            peaks[i] = pressure.measure(factory);
            pressure.verify(peaks[i], factory);
            {
                core::CpuMemoryQuotaLease full(quotas[i], cpuBytes);
                assert(full.charged() && !factory());
            }
            for (const auto& q : quotas)
                assert(!q->liveBytes());
        }
        const auto reject = [&](const char* id) {
            assert(!factory() && std::string_view(diagnostic) == id);
            for (const auto& q : quotas)
                assert(!q->liveBytes());
        };
        --b.maxMapOperations;
        reject("RHI-WEBGPU-READBACK-MAP-BUDGET");
        b = budget();
        --b.maxCallbacks;
        reject("RHI-WEBGPU-READBACK-MAP-BUDGET");
        b = budget();
        --b.maxScopeCallbacks;
        reject("RHI-WEBGPU-READBACK-MAP-BUDGET");
        b = budget();
        --b.maxResultDeclarations;
        reject("RHI-WEBGPU-READBACK-MAP-BUDGET");
        b = budget();
        --b.maxMipLayouts;
        reject("RHI-WEBGPU-READBACK-MAP-CAPACITY");
        b = budget();
        --b.maxRetainedNativeBytes;
        reject("RHI-WEBGPU-READBACK-MAP-CAPACITY");
        b = budget();
        --b.maxRetainedContentBytes;
        reject("RHI-WEBGPU-READBACK-MAP-CAPACITY");
        b = budget();
        --b.maxResultBytes;
        reject("RHI-WEBGPU-READBACK-MAP-CAPACITY");
        b = budget();
        --b.maxCpuResultMetadataBytes;
        reject("RHI-WEBGPU-READBACK-MAP-QUOTA");
        b = budget();
        auto mapping = factory();
        assert(mapping && !mapping->validate() && !mapping->resultDeclaration()->publishedBytes()
            && mapping->resultDeclaration()->byteCapacity() == 1536u);
        assert(
            !factory() && std::string_view(diagnostic) == "RHI-WEBGPU-READBACK-MAP-STORAGE-BUSY");
        auto forged = std::shared_ptr<Mapping>(quotas[0], mapping.get());
        const auto previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(std::string_view(Mapping::start(forged)) == "RHI-WEBGPU-READBACK-MAP-OWNER");
        assert(std::string_view(Mapping::start(mapping))
            == "RHI-WEBGPU-ORIGINAL-READBACK-NATIVE-COMPLETION-UNPROVEN");
        const auto& pair = copies->transaction()
                               ->states()
                               ->inputs()
                               ->scalars()
                               ->usage()
                               ->projection()
                               ->originalReadbacks()[0];
        auto& buffer = *pair->destination()->nativeResource();
        const auto bytes = buffer.sizeBytes;
        ++buffer.sizeBytes;
        assert(mapping->validate() && Mapping::start(mapping));
        buffer.sizeBytes = bytes;
        assert(!mapping->validate() && !pair->destination()->resultPublished()
            && !mapping->resultDeclaration()->publishedBytes());
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        std::pmr::set_default_resource(previous);
        forged.reset();
        mapping.reset();
        for (const auto& q : quotas)
            assert(!q->liveBytes());
        mapping = factory();
        assert(mapping);
        mapping.reset();
        for (const auto& q : quotas)
            assert(!q->liveBytes());
        std::printf(
            "BINDLESS_RHI_READBACK_COLD_MAP_NATIVE_OK %llu %llu reused-original-bytes exclusive-cancel-retry mutation allocation0 native-map-result-closed\n",
            static_cast<unsigned long long>(peaks[0]),
            static_cast<unsigned long long>(peaks[1]));
    }

    static void prepareLoss(const std::shared_ptr<const Copies>& copies)
    {
        for (auto& q : lossQuotas_)
            q = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        const char* diagnostic = nullptr;
        lossMapping_ =
            Mapping::prepareOriginal(copies, budget(), lossQuotas_[0], lossQuotas_[1], diagnostic);
        assert(lossMapping_);
    }

    static void verifyLoss()
    {
        assert(std::string_view(lossMapping_->validate()) == "RHI-WEBGPU-READBACK-MAP-DEVICE-LOST"
            && !lossMapping_->resultDeclaration()->publishedBytes());
        assert(std::string_view(Mapping::start(lossMapping_))
            == "RHI-WEBGPU-READBACK-MAP-DEVICE-LOST");
        lossMapping_.reset();
        for (const auto& q : lossQuotas_)
            assert(!q->liveBytes());
        lossQuotas_ = {};
        std::puts(
            "BINDLESS_RHI_READBACK_COLD_MAP_LOSS_OK map-owner-only actual-loss exclusive-ticket exact-retire unpublished");
    }

private:
    static inline std::shared_ptr<Mapping> lossMapping_;
    static inline std::array<std::shared_ptr<core::CpuMemoryQuota>, 2> lossQuotas_;
#endif
};
} // namespace arp::test

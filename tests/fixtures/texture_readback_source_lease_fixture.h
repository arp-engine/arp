#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/frontend/transfer_readback_input.h"

#include <array>
#include <cassert>
#include <cstdio>

namespace arp::test {
class TextureReadbackSourceLeaseFixture final {
public:
    using Lease = rhi::TextureReadbackSourceLease;
    static constexpr std::uint64_t cpuBytes = 65536u;

    static auto request(const std::shared_ptr<const rhi::TextureReadbackLayout>& layout,
        const std::shared_ptr<core::CpuMemoryQuota>& q)
    {
        const char* diagnostic = nullptr;
        auto value = rhi::TextureReadbackRequestDeclaration::prepareOriginal(layout,
            {17u, 23u, 29u},
            {1u,
                true,
                static_cast<std::uint32_t>(layout->mipCopies().size()),
                layout->inputOwner()->label().size(),
                layout->inputOwner()->scalarFields().byteCapacity(),
                cpuBytes},
            q,
            diagnostic);
        assert(value);
        return value;
    }

    static auto budget(const std::shared_ptr<const rhi::TextureReadbackRequestDeclaration>& request)
    {
        return rhi::TextureReadbackSourceLeaseBudget{1u,
            true,
            Lease::retainedOwnerCount,
            static_cast<std::uint32_t>(request->originalLayout()->mipCopies().size()),
            request->label().size(),
            cpuBytes};
    }

    template <class Context> static void verifyCpu(Context& c)
    {
        verifyColdCpu(c);
        auto requestQuota = core::makeShared<core::CpuMemoryQuota>(cpuBytes),
             leaseQuota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        auto original = request(c.layout, requestQuota);
        auto b = budget(original);
        const char* diagnostic = nullptr;
        rhi::TextureReadbackSourceLeaseBudget empty;
        assert(Lease::validateBudget(empty));
        empty.contractVersion = 1;
        assert(Lease::validateBudget(empty));
        empty.prototypeOptIn = true;
        assert(Lease::validateBudget(empty));
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto l = Lease::lowering(backend);
            assert(l.cpuMetadataImplemented && l.validatorOnly == (backend == rhi::Backend::Null));
        }
        const auto factory = [&] {
            c.makeTexture(false);
            const auto* ptr = c.texture.get();
            auto value = Lease::prepareOriginal(c.texture, original, b, leaseQuota, diagnostic);
            assert(value ? !c.texture && value->sourceTexture() == ptr : c.texture.get() == ptr);
            return value;
        };
        CpuQuotaFactoryFixture pressure(leaseQuota);
        const auto peak = pressure.measure(factory);
        pressure.verify(peak, factory);
        {
            core::CpuMemoryQuotaLease full(leaseQuota, cpuBytes);
            assert(full.charged() && !factory());
        }
        --b.maxRetainedOwners;
        assert(!factory() && std::string_view(diagnostic) == "RHI-READBACK-SOURCE-LEASE-CAPACITY");
        ++b.maxRetainedOwners;
        --b.maxLabelBytes;
        assert(!factory());
        ++b.maxLabelBytes;
        --b.maxCpuMetadataBytes;
        assert(!factory());
        ++b.maxCpuMetadataBytes;
        auto saved = original;
        original = std::shared_ptr<const rhi::TextureReadbackRequestDeclaration>(
            requestQuota, saved.get());
        assert(!factory());
        original = saved;
        saved.reset();
        c.makeTexture(false);
        auto* ptr = c.texture.get();
        auto& desc = const_cast<rhi::TextureDesc&>(ptr->desc());
        ++desc.extent.width;
        assert(!Lease::prepareOriginal(c.texture, original, b, leaseQuota, diagnostic)
            && c.texture.get() == ptr);
        --desc.extent.width;
        auto lease = Lease::prepareOriginal(c.texture, original, b, leaseQuota, diagnostic);
        assert(lease && !c.texture && !lease->nativeExecutionProven());
        assert(!Lease::validateOriginal(lease, original) && lease->sourceTexture() == ptr);
        auto forged = std::shared_ptr<const Lease>(leaseQuota, lease.get());
        assert(Lease::validateOriginal(forged, original));
        forged.reset();
        auto foreign = request(c.layout, requestQuota);
        assert(Lease::validateOriginal(lease, foreign));
        foreign.reset();
        const auto previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        const auto a = core::defaultCpuAllocator().stats().allocationCount;
        ++desc.extent.width;
        assert(lease->validate());
        --desc.extent.width;
        assert(!lease->validate());
        assert(a == core::defaultCpuAllocator().stats().allocationCount);
        std::pmr::set_default_resource(previous);
        c.layout.reset();
        original.reset();
        assert(!lease->validate()
            && lease->originalRequest()->identity()
                == rhi::TextureReadbackIdentity({17u, 23u, 29u}));
        lease.reset();
        assert(!leaseQuota->liveBytes() && !requestQuota->liveBytes());
        c.retire();
        std::printf(
            "BINDLESS_RHI_READBACK_SOURCE_LEASE_CPU_OK %llu backends4 original-object failure-preserving quotas forged-foreign mutation caller-drop allocation0\n",
            static_cast<unsigned long long>(peak));
    }

    template <class Context> static void verifyColdCpu(Context& c)
    {
        auto q = core::makeShared<core::CpuMemoryQuota>(cpuBytes),
             metadata = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        auto original = request(c.layout, q);
        auto b = budget(original);
        const char* diagnostic = nullptr;
        c.makeTexture(false);
        const auto* exact = c.texture.get();
        const auto factory = [&] {
            auto receipt = Lease::prepareColdOriginal(c.texture, original, b, metadata, diagnostic);
            assert(c.texture.get() == exact);
            if (receipt)
                assert(!receipt->sourceTexture() && receipt->validate()
                    && !Lease::validateColdOriginal(receipt, *c.texture));
            return receipt;
        };
        CpuQuotaFactoryFixture pressure(metadata);
        const auto peak = pressure.measure(factory);
        pressure.verify(peak, factory);
        {
            core::CpuMemoryQuotaLease full(metadata, cpuBytes);
            assert(full.charged() && !factory());
        }
        auto receipt = factory();
        assert(receipt);
        const auto a = core::defaultCpuAllocator().stats().allocationCount;
        auto forged = std::shared_ptr<Lease>(metadata, receipt.get());
        assert(Lease::adoptOriginal(forged, c.texture));
        forged.reset();
        auto& desc = const_cast<rhi::TextureDesc&>(c.texture->desc());
        ++desc.extent.width;
        assert(Lease::validateColdOriginal(receipt, *c.texture)
            && Lease::adoptOriginal(receipt, c.texture));
        --desc.extent.width;
        assert(
            c.texture.get() == exact && a == core::defaultCpuAllocator().stats().allocationCount);
        receipt.reset();
        assert(!metadata->liveBytes());
        // Abandoned cold receipts grant no ownership and tolerate caller death.
        receipt = factory();
        c.texture.reset();
        assert(receipt->validate() && !receipt->sourceTexture());
        receipt.reset();
        c.makeTexture(false);
        exact = c.texture.get();
        receipt = factory();
        auto abandoned = receipt;
        auto previous = std::move(c.texture);
        c.makeTexture(false);
        assert(Lease::adoptOriginal(abandoned, c.texture));
        abandoned.reset();
        receipt.reset();
        previous.reset();
        assert(!metadata->liveBytes());
        exact = c.texture.get();
        receipt = factory();
        const auto committedAllocations = core::defaultCpuAllocator().stats().allocationCount;
        {
            core::CpuMemoryQuotaLease full(
                metadata, metadata->limitBytes() - metadata->liveBytes());
            assert(full.charged());
            assert(!Lease::adoptOriginal(receipt, c.texture) && !c.texture
                && receipt->sourceTexture() == exact);
            assert(Lease::adoptOriginal(receipt, c.texture));
        }
        assert(committedAllocations == core::defaultCpuAllocator().stats().allocationCount
            && !Lease::validateOriginal(receipt, original));
        receipt.reset();
        original.reset();
        assert(!metadata->liveBytes() && !q->liveBytes());
        std::printf(
            "BINDLESS_RHI_READBACK_SOURCE_COLD_ADOPTION_CPU_OK %llu exact-less-exhausted no-owner-before-commit forged-foreign mutation caller-death abandon allocation0\n",
            static_cast<unsigned long long>(peak));
    }
};
} // namespace arp::test

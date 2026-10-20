#pragma once

#include "cpu_quota_factory_fixture.h"
#include "rhi/frontend/command_sources.h"
#include "rhi/semantics/command_lowering.h"

#include <cassert>
#include <cstdio>
#include <string_view>

namespace arp::test {
class OriginalCommandTextureUploadPackingSourceFixture final {
public:
    using Binding = rhi::OriginalCommandTextureUploadPackingSource;
    static constexpr std::uint64_t metadataBytes = 65536u,
                                   packedBytes = 3u * rhi::kTextureUploadRowPitchAlignment;

    static Binding::Budget budget()
    {
        return {1u, true, 2u, Binding::retainedOwnerCount, 20u, packedBytes, metadataBytes};
    }

    static void verify(
        const std::shared_ptr<const rhi::OriginalCommandTextureUploadSource>& original)
    {
        const char* diagnostic = nullptr;
        const std::array quotas{core::makeShared<core::CpuMemoryQuota>(metadataBytes),
            core::makeShared<core::CpuMemoryQuota>(metadataBytes),
            core::makeShared<core::CpuMemoryQuota>(metadataBytes)};
        const auto& target = original->originalDestinationLease();
        const rhi::TextureUploadPackingBudget packingBudget{
            1u, true, 2u, packedBytes, metadataBytes, metadataBytes};
        auto packing = rhi::TextureUploadPacking::prepareOriginal(original->originalInput(),
            target->originalObject()->desc(),
            packingBudget,
            quotas[0],
            quotas[1],
            diagnostic);
        assert(packing);
        assert(packing->packedBytes().size() == packedBytes && packing->mipLayouts().size() == 2u);
        const auto make = [&] {
            return Binding::prepareOriginal(original, packing, budget(), quotas[2], diagnostic);
        };
        CpuQuotaFactoryFixture pressure(quotas[2]);
        const auto peak = pressure.measure(make);
        pressure.verify(peak, make);
        auto binding = make();
        assert(binding && !binding->validate());
        const auto reject = [&](auto source, auto packed, auto b, auto quota, const char* id) {
            const auto allocationCount = core::defaultCpuAllocator().stats().allocationCount;
            assert(!Binding::prepareOriginal(source, packed, b, quota, diagnostic));
            assert(std::string_view(diagnostic) == id);
            assert(core::defaultCpuAllocator().stats().allocationCount == allocationCount);
        };
        auto invalid = budget();
        --invalid.maxMipMappings;
        reject(original, packing, invalid, quotas[2], "RHI-CANONICAL-TEXTURE-PACKING-CAPACITY");
        invalid = budget();
        --invalid.maxPixelBytes;
        reject(original, packing, invalid, quotas[2], "RHI-CANONICAL-TEXTURE-PACKING-CAPACITY");
        invalid = budget();
        --invalid.maxPackedBytes;
        reject(original, packing, invalid, quotas[2], "RHI-CANONICAL-TEXTURE-PACKING-CAPACITY");
        invalid = budget();
        --invalid.maxRetainedOwners;
        reject(original, packing, invalid, quotas[2], "RHI-CANONICAL-TEXTURE-PACKING-BUDGET");
        invalid = budget();
        invalid.contractVersion = 0u;
        reject(original, packing, invalid, quotas[2], "RHI-CANONICAL-TEXTURE-PACKING-VERSION");
        invalid = budget();
        invalid.prototypeOptIn = false;
        reject(original, packing, invalid, quotas[2], "RHI-CANONICAL-TEXTURE-PACKING-OPT-IN");
        reject(
            original, packing, budget(), quotas[0], "RHI-CANONICAL-TEXTURE-PACKING-QUOTA-DOMAIN");
        reject(
            original, packing, budget(), quotas[1], "RHI-CANONICAL-TEXTURE-PACKING-QUOTA-DOMAIN");
        reject(original,
            packing,
            budget(),
            original->originalMetadataQuota(),
            "RHI-CANONICAL-TEXTURE-PACKING-QUOTA-DOMAIN");
        auto forged = std::shared_ptr<const rhi::TextureUploadPacking>(quotas[0], packing.get());
        reject(original, forged, budget(), quotas[2], "RHI-CANONICAL-TEXTURE-PACKING-ORIGINAL");
        forged.reset();
        auto fake = std::shared_ptr<const Binding>(quotas[0], binding.get());
        assert(Binding::validateOriginal(fake));
        fake.reset();
        {
            auto desc = target->originalObject()->desc();
            desc.extent.width *= 2u;
            auto wrongDestination = rhi::TextureUploadPacking::prepareOriginal(
                original->originalInput(), desc, packingBudget, quotas[0], quotas[1], diagnostic);
            assert(wrongDestination);
            reject(original,
                wrongDestination,
                budget(),
                quotas[2],
                "RHI-CANONICAL-TEXTURE-PACKING-DESTINATION-JOIN");
            rhi::UploadTextureDesc upload;
            original->originalInput()->scalarFields().writeScalarFields(upload);
            upload.pixelData = original->originalInput()->pixelBytes().data();
            upload.label = original->originalInput()->label();
            upload.mipLayouts.assign(original->originalInput()->mipLayouts().begin(),
                original->originalInput()->mipLayouts().end());
            auto twin = rhi::TextureUploadInputSnapshot::prepareOriginal(upload,
                {1u, true, 2u, 64u, 20u, metadataBytes, metadataBytes, metadataBytes},
                original->originalInput()->originalMetadataQuota(),
                original->originalInput()->originalMetadataQuota(),
                original->originalMetadataQuota(),
                diagnostic);
            assert(twin);
            auto differentInput = rhi::TextureUploadPacking::prepareOriginal(twin,
                target->originalObject()->desc(),
                packingBudget,
                quotas[0],
                quotas[1],
                diagnostic);
            assert(differentInput);
            reject(original,
                differentInput,
                budget(),
                quotas[2],
                "RHI-CANONICAL-TEXTURE-PACKING-INPUT-JOIN");
        }
        {
            core::CpuMemoryQuotaLease full(
                quotas[2], quotas[2]->limitBytes() - quotas[2]->liveBytes());
            assert(full.charged());
            assert(!make());
            assert(std::string_view(diagnostic) == "RHI-CANONICAL-TEXTURE-PACKING-ALLOCATION");
        }
        assert(
            !CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept -> const char* {
                if (const auto* id = Binding::validateOriginal(binding))
                    return id;
                for (const auto backend : {rhi::Backend::Null,
                         rhi::Backend::Vulkan,
                         rhi::Backend::Metal,
                         rhi::Backend::WebGPU}) {
                    const auto d = arp::rhi::semantics::
                        OriginalCommandTextureUploadPackingSourceLowering::lowering(backend);
                    if (d.contentExecutionAvailable || binding->nativeExecutionProven()
                        || !binding->executionDiagnostic(backend))
                        return "TEST-CONTENT-EXECUTION-OPEN";
                    if (d.privateStagingJoinAvailable != (backend == rhi::Backend::WebGPU))
                        return "TEST-STAGING-LOWERING-MISMATCH";
                }
                return nullptr;
            }));
        packing.reset();
        assert(binding->packing()->packedBytes().size() == packedBytes && !binding->validate());
        binding.reset();
        for (const auto& quota : quotas)
            assert(!quota->liveBytes());
        std::printf(
            "BINDLESS_RHI_CANONICAL_TEXTURE_PACKING_SOURCE_OK peak%llu exact-one-less-exhausted original-snapshot-destination mips2 pixels20 packed768 independent-bounds hot-allocation0 content-gates4-closed\n",
            static_cast<unsigned long long>(peak));
    }
};
} // namespace arp::test

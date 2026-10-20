#pragma once

#include "cpu_quota_factory_fixture.h"
#include "rhi/frontend/command_model.h"
#include "rhi/frontend/transfer_upload_input.h"
#include "rhi/semantics/command_lowering.h"

#include <cassert>
#include <cstdio>
#include <string_view>

namespace arp::test {
class TextureUploadInputQuotaFixture final {
public:
    static void verify()
    {
        using Snapshot = rhi::TextureUploadInputSnapshot;
        using Original = rhi::TextureUploadOriginalInputArguments;
        constexpr std::uint64_t metadataBytes = 65536u, labelBytes = 32768u, pixelBytes = 4096u;
        auto metadataQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        auto labelQuota = core::makeShared<core::CpuMemoryQuota>(labelBytes);
        auto pixelQuota = core::makeShared<core::CpuMemoryQuota>(pixelBytes);
        const std::array quotas{metadataQuota, labelQuota, pixelQuota};
        std::array<std::uint8_t, 20> pixels{};
        for (std::size_t i = 0u; i < pixels.size(); ++i)
            pixels[i] = static_cast<std::uint8_t>(i + 1u);
        rhi::UploadTextureDesc input;
        input.destinationTexture = {17u};
        input.extent = {2u, 2u, 1u};
        input.format = rhi::TextureFormat::Rgba8Unorm;
        input.bytesPerRow = 8u;
        input.rowsPerImage = 2u;
        input.subresources.mipLevelCount = 2u;
        input.pixelData = pixels.data();
        input.pixelDataSize = static_cast<std::uint32_t>(pixels.size());
        input.label = "original upload label";
        input.frameSerial = 23u;
        input.resourceGeneration = 29u;
        input.mipLayouts = {{0u, {2u, 2u, 1u}, 8u, 2u, 0u}, {1u, {1u, 1u, 1u}, 4u, 1u, 16u}};
        rhi::TextureUploadInputSnapshotBudget budget{
            1u, true, 2u, 64u, 32u, metadataBytes, labelBytes, pixelBytes};
        const char* diagnostic = nullptr;
        const auto factory = [&] {
            return Snapshot::prepareOriginal(
                input, budget, metadataQuota, labelQuota, pixelQuota, diagnostic);
        };
        const std::array<CpuQuotaFactoryFixture, 3> pressure{CpuQuotaFactoryFixture(metadataQuota),
            CpuQuotaFactoryFixture(labelQuota),
            CpuQuotaFactoryFixture(pixelQuota)};
        const std::array<std::uint64_t, 3> peaks{pressure[0].measure(factory),
            pressure[1].measure(factory),
            pressure[2].measure(factory)};
        for (std::size_t i = 0u; i < quotas.size(); ++i) {
            pressure[i].verify(peaks[i], factory);
            core::CpuMemoryQuotaLease full(quotas[i], quotas[i]->limitBytes());
            assert(full.charged() && !factory());
        }
        for (const auto& quota : quotas)
            assert(!quota->liveBytes());
        auto snapshot = factory();
        assert(snapshot && !Snapshot::validateOriginal(snapshot) && snapshot->matches(input));
        assert(core::SharedOwnerProjection::sameOwner(
            snapshot->originalMetadataQuota(), metadataQuota));
        assert(core::SharedOwnerProjection::sameOwner(
            snapshot->labelOwner()->originalMetadataQuota(), labelQuota));
        assert(core::SharedOwnerProjection::sameOwner(snapshot->originalPixelQuota(), pixelQuota));
        const auto allocation = core::inspectCpuAllocationContaining(snapshot->pixelBytes().data());
        assert(allocation.found && allocation.quota == pixelQuota.get()); // Cold inspection only.
        auto original = Original::captureOriginal(snapshot);
        assert(original && original->matches(*original));
        auto equal = factory();
        assert(equal && equal->matches(input));
        auto equalArguments = Original::captureOriginal(equal);
        assert(equalArguments && !original->matches(*equalArguments));
        equal.reset();
        auto forged = std::shared_ptr<const Snapshot>(pixelQuota, snapshot.get());
        assert(Snapshot::validateOriginal(forged) && !Original::captureOriginal(forged));
        forged.reset();
        for (std::size_t i = 0u; i < quotas.size(); ++i) {
            const auto limit = quotas[i]->limitBytes();
            quotas[i]->setLimitBytes(limit - 1u);
            assert(std::string_view(Snapshot::validateOriginal(snapshot))
                == "RHI-TEXTURE-UPLOAD-INPUT-QUOTA");
            assert(!snapshot->matches(input) && !Original::captureOriginal(snapshot)
                && !original->matches(*original));
            quotas[i]->setLimitBytes(limit);
            assert(!Snapshot::validateOriginal(snapshot) && snapshot->matches(input));
        }
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept {
            if (const auto* id = Snapshot::validateOriginal(snapshot))
                return id;
            if (!snapshot->matches(input) || !Original::captureOriginal(snapshot)
                || !original->matches(*original))
                return "TEST-ORIGINAL-UPLOAD-REJECTED";
            if (snapshot->originalPixelQuota() != pixelQuota)
                return "TEST-PIXEL-QUOTA-REPLACED";
            ++pixels[0];
            const auto mismatch = !snapshot->matches(input);
            --pixels[0];
            if (!mismatch)
                return "TEST-PIXEL-MUTATION-ACCEPTED";
            ++input.mipLayouts[1].bufferOffsetBytes;
            const auto mappingMismatch = !snapshot->matches(input);
            --input.mipLayouts[1].bufferOffsetBytes;
            if (!mappingMismatch)
                return "TEST-MIP-MUTATION-ACCEPTED";
            if (std::string_view(snapshot->executionDiagnostic())
                != "RHI-TEXTURE-UPLOAD-ORIGINAL-SOURCE-UNPROVEN")
                return "TEST-UPLOAD-NATIVE-GATE-OPEN";
            for (const auto backend : {rhi::Backend::Null,
                     rhi::Backend::Vulkan,
                     rhi::Backend::Metal,
                     rhi::Backend::WebGPU})
                if (arp::rhi::semantics::OriginalCommandAdmissionLowering::lowering(backend)
                        .executionAvailable)
                    return "TEST-GENERIC-GATE-OPEN";
            return static_cast<const char*>(nullptr);
        }));
        std::weak_ptr<const Snapshot> observed = snapshot;
        snapshot.reset();
        assert(!observed.expired() && !Snapshot::validateOriginal(original->originalInput()));
        pixels.fill(0u);
        input.label.clear();
        input.mipLayouts.clear();
        assert(original->originalInput()->pixelBytes()[0] == 1u
            && original->originalInput()->mipLayouts().size() == 2u
            && original->originalInput()->label() == "original upload label");
        original.reset();
        assert(observed.expired());
        observed.reset();
        // The independently captured equal owner remains canonical until its
        // comparison value retires; it is never silently substituted above.
        equalArguments.reset();
        for (const auto& quota : quotas)
            assert(!quota->liveBytes());
        std::printf(
            "BINDLESS_RHI_UPLOAD_INPUT_QUOTAS_OK %llu %llu %llu original-metadata-label-pixel quotas3 canonical-owner exact-bytes-mips-label changed-limits-forged-reject cold-pressure caller-drop hot-allocation0 native-unproven\n",
            static_cast<unsigned long long>(peaks[0]),
            static_cast<unsigned long long>(peaks[1]),
            static_cast<unsigned long long>(peaks[2]));
    }
};
} // namespace arp::test

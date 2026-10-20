#pragma once

#include "cpu_quota_factory_fixture.h"
#include "rhi/frontend/command_model.h"
#include "rhi/frontend/transfer_readback_input.h"
#include "rhi/semantics/command_lowering.h"

#include <cassert>
#include <cstdio>
#include <string_view>

namespace arp::test {
class TextureReadbackInputQuotaFixture final {
public:
    static void verify()
    {
        using Snapshot = rhi::TextureReadbackInputSnapshot;
        using Original = rhi::TextureReadbackOriginalInputArguments;
        constexpr std::uint64_t metadataBytes = 65536u, labelBytes = 32768u;
        auto metadataQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes),
             labelQuota = core::makeShared<core::CpuMemoryQuota>(labelBytes);
        const std::array quotas{metadataQuota, labelQuota};
        rhi::ReadbackTextureDesc input;
        input.sourceResource = {17u};
        input.destinationReadbackBuffer = {19u};
        input.extent = {2u, 2u, 1u};
        input.bytesPerRow = 256u;
        input.rowsPerImage = 2u;
        input.sizeBytes = 768u;
        input.sourceSubresources.mipLevelCount = 2u;
        input.label = "original readback label";
        input.mipLayouts = {{0u, {2u, 2u, 1u}, 256u, 2u, 0u}, {1u, {1u, 1u, 1u}, 256u, 1u, 512u}};
        const rhi::TextureReadbackInputSnapshotBudget budget{
            1u, true, 2u, 64u, metadataBytes, labelBytes};
        const char* diagnostic = nullptr;
        const auto factory = [&] {
            return Snapshot::prepareOriginal(input, budget, metadataQuota, labelQuota, diagnostic);
        };
        const std::array<CpuQuotaFactoryFixture, 2> pressure{
            CpuQuotaFactoryFixture(metadataQuota), CpuQuotaFactoryFixture(labelQuota)};
        const std::array<std::uint64_t, 2> peaks{
            pressure[0].measure(factory), pressure[1].measure(factory)};
        for (std::size_t i = 0u; i < quotas.size(); ++i) {
            pressure[i].verify(peaks[i], factory);
            core::CpuMemoryQuotaLease full(quotas[i], quotas[i]->limitBytes());
            assert(full.charged() && !factory());
        }
        auto snapshot = factory();
        assert(snapshot && !Snapshot::validateOriginal(snapshot) && snapshot->matches(input));
        assert(
            core::SharedOwnerProjection::sameOwner(snapshot->originalMetadataQuota(), metadataQuota)
            && core::SharedOwnerProjection::sameOwner(
                snapshot->labelOwner()->originalMetadataQuota(), labelQuota));
        auto arguments = Original::captureOriginal(snapshot);
        assert(arguments && arguments->matches(*arguments));
        auto equal = factory();
        assert(equal && equal->matches(input));
        auto other = Original::captureOriginal(equal);
        assert(other && !arguments->matches(*other));
        auto forged = std::shared_ptr<const Snapshot>(labelQuota, snapshot.get());
        assert(Snapshot::validateOriginal(forged) && !Original::captureOriginal(forged));
        forged.reset();
        for (const auto& quota : quotas) {
            const auto limit = quota->limitBytes();
            quota->setLimitBytes(limit - 1u);
            assert(std::string_view(Snapshot::validateOriginal(snapshot))
                    == "RHI-TEXTURE-READBACK-INPUT-QUOTA"
                && !snapshot->matches(input) && !Original::captureOriginal(snapshot)
                && !arguments->matches(*arguments));
            quota->setLimitBytes(limit);
            assert(!Snapshot::validateOriginal(snapshot));
        }
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept {
            if (const auto* id = Snapshot::validateOriginal(snapshot))
                return id;
            if (!snapshot->matches(input) || !Original::captureOriginal(snapshot)
                || !arguments->matches(*arguments))
                return "TEST-ORIGINAL-READBACK-REJECTED";
            ++input.mipLayouts[1].bufferOffsetBytes;
            const auto different = !snapshot->matches(input);
            --input.mipLayouts[1].bufferOffsetBytes;
            if (!different)
                return "TEST-MIP-MUTATION-ACCEPTED";
            ++input.sizeBytes;
            const auto scalarDifferent = !snapshot->matches(input);
            --input.sizeBytes;
            if (!scalarDifferent)
                return "TEST-SCALAR-MUTATION-ACCEPTED";
            if (std::string_view(snapshot->executionDiagnostic())
                != "RHI-TEXTURE-READBACK-ORIGINAL-OWNER-UNPROVEN")
                return "TEST-READBACK-NATIVE-GATE-OPEN";
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
        input.label.clear();
        input.mipLayouts.clear();
        assert(!observed.expired() && !Snapshot::validateOriginal(arguments->originalInput())
            && arguments->originalInput()->mipLayouts().size() == 2u
            && arguments->originalInput()->label() == "original readback label");
        arguments.reset();
        assert(observed.expired());
        observed.reset();
        equal.reset();
        other.reset();
        for (const auto& quota : quotas)
            assert(!quota->liveBytes());
        std::printf(
            "BINDLESS_RHI_READBACK_INPUT_QUOTAS_OK %llu %llu original-metadata-label quotas2 canonical-owner exact-mips-label changed-limits-forged-reject cold-pressure caller-drop hot-allocation0 native-unproven\n",
            static_cast<unsigned long long>(peaks[0]),
            static_cast<unsigned long long>(peaks[1]));
    }
};
} // namespace arp::test

#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/backends/webgpu/webgpu_original_command_inputs.h"
#include "rhi/frontend/transfer_readback_input.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <type_traits>

namespace arp::test {
class TextureReadbackArgumentsFixture final {
    using Fields = rhi::TextureReadbackArguments;
    using Snapshot = rhi::TextureReadbackInputSnapshot;
    static constexpr std::uint64_t cpuBytes = 65536u;

    static rhi::ReadbackTextureDesc input()
    {
        rhi::ReadbackTextureDesc d;
        d.label = "原始读回✓ 🔒";
        d.sourceResource = {UINT64_MAX};
        d.destinationReadbackBuffer = {17u};
        d.sizeBytes = UINT64_MAX;
        d.stagingIntent = static_cast<rhi::StagingIntent>(UINT32_MAX);
        d.backendOwnedStaging = false;
        d.sourceSubresources = {19u, 2u, 23u, 29u, static_cast<rhi::TextureAspect>(UINT32_MAX)};
        d.extent = {UINT32_MAX, 31u, 37u};
        d.bytesPerRow = UINT32_MAX;
        d.rowsPerImage = 41u;
        d.destinationOffsetBytes = UINT64_MAX;
        d.mipLayouts.push_back({7u, {43u, 47u, 53u}, 59u, 61u, UINT64_MAX});
        d.mipLayouts.push_back({3u, {67u, 71u, 73u}, 79u, 83u, 89u});
        return d;
    }

    static constexpr std::array<void (*)(rhi::ReadbackTextureDesc&), 16> mutations{
        {[](auto& d) {
             --d.sourceResource.value;
         },
            [](auto& d) {
                ++d.destinationReadbackBuffer.value;
            },
            [](auto& d) {
                --d.sizeBytes;
            },
            [](auto& d) {
                d.stagingIntent = rhi::StagingIntent::Readback;
            },
            [](auto& d) {
                d.backendOwnedStaging = true;
            },
            [](auto& d) {
                ++d.sourceSubresources.baseMipLevel;
            },
            [](auto& d) {
                ++d.sourceSubresources.mipLevelCount;
            },
            [](auto& d) {
                ++d.sourceSubresources.baseArrayLayer;
            },
            [](auto& d) {
                ++d.sourceSubresources.arrayLayerCount;
            },
            [](auto& d) {
                d.sourceSubresources.aspect = rhi::TextureAspect::Color;
            },
            [](auto& d) {
                --d.extent.width;
            },
            [](auto& d) {
                ++d.extent.height;
            },
            [](auto& d) {
                ++d.extent.depth;
            },
            [](auto& d) {
                --d.bytesPerRow;
            },
            [](auto& d) {
                ++d.rowsPerImage;
            },
            [](auto& d) {
                --d.destinationOffsetBytes;
            }}};
    static constexpr std::array<void (*)(rhi::TextureToBufferCopyDesc::MipLayout&), 7> mipMutations{
        {[](auto& m) {
             ++m.mipLevel;
         },
            [](auto& m) {
                ++m.extent.width;
            },
            [](auto& m) {
                ++m.extent.height;
            },
            [](auto& m) {
                ++m.extent.depth;
            },
            [](auto& m) {
                ++m.bytesPerRow;
            },
            [](auto& m) {
                ++m.rowsPerImage;
            },
            [](auto& m) {
                --m.bufferOffsetBytes;
            }}};

public:
    static void verify()
    {
        using namespace rhi;
        using namespace rhi_webgpu;
        static_assert(
            std::is_trivially_copyable_v<Fields> && !std::is_default_constructible_v<Fields>);
        auto source = input();
        auto canonical = source;
        auto changed = source;
        const auto before = core::defaultCpuAllocator().stats().allocationCount;
        const auto fields = Fields::captureScalarFields(source);
        assert(fields.kind() == CommandUsageKind::TextureReadback
            && fields.matchesScalarFields(source));
        for (const auto mutate : mutations) {
            mutate(changed);
            assert(!fields.matchesScalarFields(changed));
            const auto exact = Fields::captureScalarFields(changed);
            assert(exact.matchesScalarFields(changed));
            fields.writeScalarFields(changed);
            assert(fields.matchesScalarFields(changed) && changed.label == source.label
                && changed.mipLayouts.size() == source.mipLayouts.size());
        }
        assert(core::defaultCpuAllocator().stats().allocationCount == before);
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
            const auto conclusion = Snapshot::lowering(backend);
            assert(conclusion.cpuMetadataImplemented
                && conclusion.validatorOnly == (backend == Backend::Null)
                && std::string_view(conclusion.diagnostic)
                    == "RHI-TEXTURE-READBACK-ORIGINAL-OWNER-UNPROVEN");
        }
        assert(!Snapshot::lowering(static_cast<Backend>(UINT32_MAX)).cpuMetadataImplemented);
        TextureReadbackInputSnapshotBudget b;
        assert(
            std::string_view(Snapshot::validateBudget(b)) == "RHI-TEXTURE-READBACK-INPUT-VERSION");
        b.contractVersion = 1u;
        assert(
            std::string_view(Snapshot::validateBudget(b)) == "RHI-TEXTURE-READBACK-INPUT-OPT-IN");
        b.prototypeOptIn = true;
        assert(
            std::string_view(Snapshot::validateBudget(b)) == "RHI-TEXTURE-READBACK-INPUT-BUDGET");
        b = {1u, true, 2u, source.label.size(), cpuBytes, cpuBytes};
        assert(!Snapshot::validateBudget(b));
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 2> quotas;
        for (auto& q : quotas)
            q = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        const char* diagnostic = nullptr;
        const auto factory = [&] {
            return Snapshot::prepareOriginal(source, b, quotas[0], quotas[1], diagnostic);
        };
        const auto retired = [&] {
            for (const auto& q : quotas)
                assert(!q->liveBytes());
        };
        std::array<std::uint64_t, 2> peaks{};
        for (std::size_t i = 0u; i < quotas.size(); ++i) {
            CpuQuotaFactoryFixture pressure(quotas[i]);
            peaks[i] = pressure.measure(factory);
            pressure.verify(peaks[i], factory);
            retired();
            {
                core::CpuMemoryQuotaLease full(quotas[i], cpuBytes);
                assert(full.charged() && !factory());
            }
            retired();
            auto missing = quotas;
            missing[i].reset();
            assert(!Snapshot::prepareOriginal(source, b, missing[0], missing[1], diagnostic)
                && std::string_view(diagnostic) == "RHI-TEXTURE-READBACK-INPUT-QUOTA");
            retired();
        }
        const auto reject = [&](const char* id) {
            assert(!factory() && diagnostic && std::string_view(diagnostic) == id);
            retired();
        };
        --b.maxMipLayouts;
        reject("RHI-TEXTURE-READBACK-INPUT-CAPACITY");
        ++b.maxMipLayouts;
        --b.maxTextBytes;
        reject("RHI-TEXTURE-READBACK-INPUT-CAPACITY");
        ++b.maxTextBytes;
        --b.maxCpuMetadataBytes;
        reject("RHI-TEXTURE-READBACK-INPUT-QUOTA");
        ++b.maxCpuMetadataBytes;
        --b.maxCpuLabelBytes;
        reject("RHI-TEXTURE-READBACK-INPUT-QUOTA");
        ++b.maxCpuLabelBytes;
        b.maxMipLayouts = UINT32_MAX;
        reject("RHI-TEXTURE-READBACK-INPUT-ALLOCATION");
        b.maxMipLayouts = 2u;
        auto snapshot = factory();
        assert(
            snapshot && snapshot->matches(source) && snapshot->hasQuotaBoundSharedOwner(snapshot));
        {
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            auto argument = TextureReadbackOriginalInputArguments::captureOriginal(snapshot);
            assert(argument && argument->matches(*argument) && argument->originalInput() == snapshot
                && argument->kind() == CommandUsageKind::TextureReadback);
            auto forgedInput = std::shared_ptr<const Snapshot>(quotas[0], snapshot.get());
            assert(!TextureReadbackOriginalInputArguments::captureOriginal(forgedInput));
            assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        }
        auto forged = std::shared_ptr<const Snapshot>(quotas[0], snapshot.get());
        assert(!snapshot->hasQuotaBoundSharedOwner(forged));
        forged.reset();
        assert(snapshot->labelOwner()->hasQuotaBoundSharedOwner(snapshot->labelOwner())
            && snapshot->mipLayouts()[0].mipLevel == 7u
            && snapshot->mipLayouts()[1].mipLevel == 3u);
        source.label.clear();
        source.label.shrink_to_fit();
        source.mipLayouts.clear();
        source.mipLayouts.shrink_to_fit();
        assert(snapshot->matches(canonical));
        {
            core::CpuMemoryQuotaScope scope(quotas[0]);
            core::CpuMemoryQuotaLease full(quotas[0], cpuBytes - quotas[0]->liveBytes());
            assert(full.charged());
            auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
            const auto hot = core::defaultCpuAllocator().stats().allocationCount;
            for (const auto mutate : mutations) {
                mutate(canonical);
                assert(!snapshot->matches(canonical));
                fields.writeScalarFields(canonical);
            }
            for (auto& mip : canonical.mipLayouts)
                for (const auto mutate : mipMutations) {
                    const auto original = mip;
                    mutate(mip);
                    assert(!snapshot->matches(canonical));
                    mip = original;
                }
            std::swap(canonical.mipLayouts[0], canonical.mipLayouts[1]);
            assert(!snapshot->matches(canonical));
            std::swap(canonical.mipLayouts[0], canonical.mipLayouts[1]);
            const auto last = canonical.mipLayouts.back();
            canonical.mipLayouts.pop_back();
            assert(!snapshot->matches(canonical));
            canonical.mipLayouts.push_back(last);
            canonical.label[0] = 'x';
            assert(!snapshot->matches(canonical));
            canonical.label[0] = snapshot->label()[0];
            assert(snapshot->matches(canonical));
            assert(WebGpuOriginalCommandInputs::family(CommandUsageKind::TextureReadback)
                == WebGpuOriginalCommandInputs::Family::TextureReadback);
            assert(std::string_view(snapshot->executionDiagnostic())
                == "RHI-TEXTURE-READBACK-ORIGINAL-OWNER-UNPROVEN");
            WebGpuCommandReference readback{
                CommandReferenceDomain::Readback, WebGpuCommandReferenceSource::Transfer, {17u}};
            WebGpuGenericCommandProjectionBudget gb;
            gb.contractVersion = 1u;
            gb.prototypeOptIn = true;
            gb.maxReferences = 1u;
            gb.maxCpuMetadataBytes = cpuBytes;
            gb.maxDomainReferences[static_cast<std::size_t>(CommandReferenceDomain::Readback)] = 1u;
            assert(!WebGpuGenericCommandProjection::prepare(
                       {}, &readback, 1u, gb, quotas[0], diagnostic)
                && std::string_view(diagnostic) == "RHI-WEBGPU-COMMAND-PROJECTION-DOMAIN-UNPROVEN");
            assert(core::defaultCpuAllocator().stats().allocationCount == hot);
            std::pmr::set_default_resource(previous);
        }
        snapshot.reset();
        retired();
        ReadbackTextureDesc empty;
        empty.extent = {0u, 0u, 0u};
        empty.sourceSubresources = {0u, 0u, 0u, 0u, TextureAspect::Color};
        b = {1u, true, 0u, 0u, cpuBytes, cpuBytes};
        auto legacy = Snapshot::prepareOriginal(empty, b, quotas[0], quotas[1], diagnostic);
        assert(legacy && legacy->matches(empty) && legacy->mipLayouts().empty()
            && legacy->label().empty());
        legacy.reset();
        retired();
        empty.label.assign("a\0\xff", 3u);
        b.maxTextBytes = 3u;
        auto binary = Snapshot::prepareOriginal(empty, b, quotas[0], quotas[1], diagnostic);
        assert(
            binary && binary->matches(empty) && binary->label() == std::string_view("a\0\xff", 3u));
        binary.reset();
        retired();
        std::printf(
            "BINDLESS_RHI_TEXTURE_READBACK_INPUT_OK %llu %llu fields16 mips2 label caller-drop legacy-explicit backends4 gates-closed allocation0\n",
            static_cast<unsigned long long>(peaks[0]),
            static_cast<unsigned long long>(peaks[1]));
    }

    static void prepareActualLoss()
    {
        const auto d = input();
        const char* diagnostic = nullptr;
        for (auto& q : lossQuotas_)
            q = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        lossSnapshot_ = Snapshot::prepareOriginal(d,
            {1u, true, 2u, d.label.size(), cpuBytes, cpuBytes},
            lossQuotas_[0],
            lossQuotas_[1],
            diagnostic);
        assert(lossSnapshot_);
    }

    static void verifyActualDeviceLoss()
    {
        assert(rhi_webgpu::webGpuNativeDeviceLost() && lossSnapshot_
            && lossSnapshot_->matches(input()));
        assert(std::string_view(lossSnapshot_->executionDiagnostic())
            == "RHI-TEXTURE-READBACK-ORIGINAL-OWNER-UNPROVEN");
        lossSnapshot_.reset();
        for (auto& q : lossQuotas_) {
            assert(!q->liveBytes());
            q.reset();
        }
        std::puts(
            "BINDLESS_RHI_TEXTURE_READBACK_INPUT_LOSS_OK cpu-only unchanged gate original-owner exact-retire");
    }

private:
    static inline std::shared_ptr<const Snapshot> lossSnapshot_;
    static inline std::array<std::shared_ptr<core::CpuMemoryQuota>, 2> lossQuotas_;
};
} // namespace arp::test

#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/backends/webgpu/webgpu_original_command_inputs.h"
#include "rhi/frontend/transfer_upload_input.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <type_traits>

namespace arp::test {
class TextureUploadArgumentsFixture final {
    using Fields = rhi::TextureUploadArguments;
    using Snapshot = rhi::TextureUploadInputSnapshot;
    static constexpr std::uint64_t cpuBytes = 65536u;
    static constexpr std::size_t pixelCount = 24u;

    static std::array<unsigned char, pixelCount> pixels()
    {
        return {{0u,
            17u,
            31u,
            43u,
            59u,
            71u,
            89u,
            103u,
            127u,
            137u,
            149u,
            163u,
            179u,
            193u,
            211u,
            227u,
            239u,
            251u,
            0u,
            255u,
            1u,
            254u,
            2u,
            253u}};
    }

    static rhi::UploadTextureDesc input(const void* bytes)
    {
        rhi::UploadTextureDesc d;
        d.label = "原始纹理✓ 🔒";
        d.destinationTexture = {UINT64_MAX};
        d.extent = {UINT32_MAX, 19u, 23u};
        d.destinationOffset = {29u, 31u, 37u};
        d.format = static_cast<rhi::TextureFormat>(UINT32_MAX);
        d.bytesPerRow = UINT32_MAX;
        d.rowsPerImage = 41u;
        d.mipLevel = 43u;
        d.arrayLayer = 47u;
        d.subresources = {53u, 2u, 59u, 61u, static_cast<rhi::TextureAspect>(UINT32_MAX)};
        d.pixelData = bytes;
        d.pixelDataSize = pixelCount;
        d.stagingIntent = static_cast<rhi::StagingIntent>(UINT32_MAX);
        d.backendOwnedStaging = false;
        d.frameSerial = UINT64_MAX;
        d.resourceGeneration = UINT64_MAX;
        d.mipLayouts.push_back({7u, {71u, 73u, 79u}, 83u, 89u, UINT64_MAX});
        d.mipLayouts.push_back({3u, {97u, 101u, 103u}, 107u, 109u, 113u});
        return d;
    }

    static constexpr std::array<void (*)(rhi::UploadTextureDesc&), 22> mutations{
        {[](auto& d) {
             --d.destinationTexture.value;
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
                ++d.destinationOffset.width;
            },
            [](auto& d) {
                ++d.destinationOffset.height;
            },
            [](auto& d) {
                ++d.destinationOffset.depth;
            },
            [](auto& d) {
                d.format = rhi::TextureFormat::Rgba8Unorm;
            },
            [](auto& d) {
                --d.bytesPerRow;
            },
            [](auto& d) {
                ++d.rowsPerImage;
            },
            [](auto& d) {
                ++d.mipLevel;
            },
            [](auto& d) {
                ++d.arrayLayer;
            },
            [](auto& d) {
                ++d.subresources.baseMipLevel;
            },
            [](auto& d) {
                ++d.subresources.mipLevelCount;
            },
            [](auto& d) {
                ++d.subresources.baseArrayLayer;
            },
            [](auto& d) {
                ++d.subresources.arrayLayerCount;
            },
            [](auto& d) {
                d.subresources.aspect = rhi::TextureAspect::Color;
            },
            [](auto& d) {
                --d.pixelDataSize;
            },
            [](auto& d) {
                d.stagingIntent = rhi::StagingIntent::Upload;
            },
            [](auto& d) {
                d.backendOwnedStaging = true;
            },
            [](auto& d) {
                --d.frameSerial;
            },
            [](auto& d) {
                --d.resourceGeneration;
            }}};
    static constexpr std::array<void (*)(rhi::UploadTextureDesc::MipLayout&), 7> mipMutations{
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
        auto bytes = pixels();
        auto source = input(bytes.data());
        auto canonical = source;
        auto alternateBytes = bytes;
        canonical.pixelData = alternateBytes.data();
        auto changed = source;
        const auto before = core::defaultCpuAllocator().stats().allocationCount;
        const auto fields = Fields::captureScalarFields(source);
        assert(
            fields.matchesScalarFields(source) && fields.kind() == CommandUsageKind::TextureUpload);
        for (const auto mutate : mutations) {
            mutate(changed);
            assert(!fields.matchesScalarFields(changed));
            const auto exact = Fields::captureScalarFields(changed);
            assert(exact.matchesScalarFields(changed));
            fields.writeScalarFields(changed);
            assert(fields.matchesScalarFields(changed));
        }
        UploadTextureDesc invalid;
        invalid.extent = {0u, 0u, 0u};
        invalid.pixelDataSize = UINT32_MAX;
        invalid.frameSerial = UINT64_MAX;
        const auto invalidFields = Fields::captureScalarFields(invalid);
        assert(invalidFields.matchesScalarFields(invalid));
        assert(core::defaultCpuAllocator().stats().allocationCount == before);
        auto hotFields = changed;
        hotFields.label = "preserve output label";
        const auto pointer = hotFields.pixelData;
        const auto mipCount = hotFields.mipLayouts.size();
        const auto scalarAllocations = core::defaultCpuAllocator().stats().allocationCount;
        invalidFields.writeScalarFields(hotFields);
        assert(hotFields.label == "preserve output label" && hotFields.pixelData == pointer
            && hotFields.mipLayouts.size() == mipCount);
        assert(core::defaultCpuAllocator().stats().allocationCount == scalarAllocations);

        std::array<std::shared_ptr<core::CpuMemoryQuota>, 3> quotas{
            {core::makeShared<core::CpuMemoryQuota>(cpuBytes),
                core::makeShared<core::CpuMemoryQuota>(cpuBytes),
                core::makeShared<core::CpuMemoryQuota>(cpuBytes)}};
        TextureUploadInputSnapshotBudget budget;
        const char* diagnostic = nullptr;
        assert(std::string_view(Snapshot::validateBudget(budget))
            == "RHI-TEXTURE-UPLOAD-INPUT-VERSION");
        budget.contractVersion = 1u;
        assert(std::string_view(Snapshot::validateBudget(budget))
            == "RHI-TEXTURE-UPLOAD-INPUT-OPT-IN");
        budget.prototypeOptIn = true;
        assert(std::string_view(Snapshot::validateBudget(budget))
            == "RHI-TEXTURE-UPLOAD-INPUT-BUDGET");
        budget = {1u, true, 2u, source.label.size(), pixelCount, cpuBytes, cpuBytes, cpuBytes};
        assert(!Snapshot::validateBudget(budget));
        auto illegal = rhi::CommandVariableArguments::Input{
            *rhi::CommandScalarArguments::capture(
                CommandUsageKind::TextureUpload, rhi::CommandScalarArguments::None{}),
            source.label,
            nullptr,
            0u,
            bytes.data(),
            static_cast<std::uint32_t>(bytes.size())};
        assert(std::string_view(rhi::CommandVariableArguments::validateInput(illegal))
            == "RHI-COMMAND-ARGUMENTS-PAYLOAD");
        const auto factory = [&] {
            return Snapshot::prepareOriginal(
                source, budget, quotas[0], quotas[1], quotas[2], diagnostic);
        };
        const auto retired = [&] {
            for (const auto& q : quotas)
                assert(!q->liveBytes());
        };
        std::array<std::uint64_t, 3> peaks{};
        for (std::size_t i = 0u; i < quotas.size(); ++i) {
            CpuQuotaFactoryFixture pressure(quotas[i]);
            peaks[i] = pressure.measure(factory);
            pressure.verify(peaks[i], factory);
            retired();
            {
                core::CpuMemoryQuotaLease exhausted(quotas[i], cpuBytes);
                assert(exhausted.charged());
                assert(!factory());
            }
            retired();
        }
        const auto expect = [&](const char* id) {
            assert(!factory() && std::string_view(diagnostic) == id);
            retired();
        };
        --budget.maxMipLayouts;
        expect("RHI-TEXTURE-UPLOAD-INPUT-CAPACITY");
        ++budget.maxMipLayouts;
        --budget.maxTextBytes;
        expect("RHI-TEXTURE-UPLOAD-INPUT-CAPACITY");
        ++budget.maxTextBytes;
        --budget.maxPixelBytes;
        expect("RHI-TEXTURE-UPLOAD-INPUT-CAPACITY");
        ++budget.maxPixelBytes;
        for (auto* limit :
            {&budget.maxCpuMetadataBytes, &budget.maxCpuLabelBytes, &budget.maxCpuPixelBytes}) {
            --*limit;
            expect("RHI-TEXTURE-UPLOAD-INPUT-QUOTA");
            ++*limit;
        }
        source.pixelData = nullptr;
        expect("RHI-TEXTURE-UPLOAD-INPUT-PIXEL-POINTER");
        source.pixelData = bytes.data();
        budget.maxMipLayouts = UINT32_MAX;
        expect("RHI-TEXTURE-UPLOAD-INPUT-ALLOCATION");
        budget.maxMipLayouts = 2u;
        for (std::size_t i = 0u; i < quotas.size(); ++i) {
            auto missing = quotas;
            missing[i].reset();
            assert(!Snapshot::prepareOriginal(
                       source, budget, missing[0], missing[1], missing[2], diagnostic)
                && std::string_view(diagnostic) == "RHI-TEXTURE-UPLOAD-INPUT-QUOTA");
            retired();
        }
        auto snapshot = factory();
        assert(snapshot && !diagnostic && snapshot->hasQuotaBoundSharedOwner(snapshot));
        auto forged = std::shared_ptr<const Snapshot>(quotas[0], snapshot.get());
        assert(!snapshot->hasQuotaBoundSharedOwner(forged));
        forged.reset();
        assert(snapshot->labelOwner()->hasQuotaBoundSharedOwner(snapshot->labelOwner())
            && snapshot->pixelBytes().size() == pixelCount
            && snapshot->pixelBytes().data() != bytes.data()
            && snapshot->mipLayouts().size() == 2u);
        assert(snapshot->mipLayouts()[0].mipLevel == 7u
            && snapshot->mipLayouts()[1].mipLevel
                == 3u); // Preserve order, including unsupported metadata.
        source.label.clear();
        source.label.shrink_to_fit();
        source.mipLayouts.clear();
        source.mipLayouts.shrink_to_fit();
        bytes.fill(0u);
        source.pixelData = nullptr;
        assert(snapshot->matches(canonical));
        {
            core::CpuMemoryQuotaScope scope(quotas[0]);
            core::CpuMemoryQuotaLease exhausted(quotas[0], cpuBytes - quotas[0]->liveBytes());
            assert(exhausted.charged());
            auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
            const auto allocationCount = core::defaultCpuAllocator().stats().allocationCount;
            assert(snapshot->matches(canonical));
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
            canonical.label[0] = 'x';
            assert(!snapshot->matches(canonical));
            canonical.label[0] = snapshot->label()[0];
            alternateBytes[23] ^= 1u;
            assert(!snapshot->matches(canonical));
            alternateBytes[23] ^= 1u;
            canonical.pixelData = nullptr;
            assert(!snapshot->matches(canonical));
            canonical.pixelData = alternateBytes.data();
            assert(snapshot->matches(canonical));
            assert(WebGpuOriginalCommandInputs::family(CommandUsageKind::TextureUpload)
                == WebGpuOriginalCommandInputs::Family::TextureUpload);
            assert(std::string_view(snapshot->executionDiagnostic())
                == "RHI-TEXTURE-UPLOAD-ORIGINAL-SOURCE-UNPROVEN");
            assert(core::defaultCpuAllocator().stats().allocationCount == allocationCount);
            std::pmr::set_default_resource(previous);
        }
        snapshot.reset();
        retired();
        UploadTextureDesc empty;
        budget = {1u, true, 0u, 0u, 0u, cpuBytes, cpuBytes, cpuBytes};
        auto nullEmpty =
            Snapshot::prepareOriginal(empty, budget, quotas[0], quotas[1], quotas[2], diagnostic);
        assert(nullEmpty && nullEmpty->matches(empty) && !nullEmpty->callerPixelPointerPresent());
        empty.pixelData = alternateBytes.data();
        auto presentEmpty =
            Snapshot::prepareOriginal(empty, budget, quotas[0], quotas[1], quotas[2], diagnostic);
        assert(presentEmpty && presentEmpty->callerPixelPointerPresent()
            && presentEmpty->pixelBytes().empty() && presentEmpty->matches(empty)
            && !nullEmpty->matches(empty));
        nullEmpty.reset();
        presentEmpty.reset();
        retired();
        verifyBinaryLabel(quotas);
        std::printf(
            "BINDLESS_RHI_TEXTURE_UPLOAD_INPUT_OK %llu %llu %llu fields22 mips2 label pixels24 caller-drop gates-closed allocation0\n",
            static_cast<unsigned long long>(peaks[0]),
            static_cast<unsigned long long>(peaks[1]),
            static_cast<unsigned long long>(peaks[2]));
    }

    static void prepareActualLoss()
    {
        auto bytes = pixels();
        auto source = input(bytes.data());
        const char* diagnostic = nullptr;
        for (auto& quota : lossQuotas_)
            quota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        lossSnapshot_ = Snapshot::prepareOriginal(source,
            {1u, true, 2u, source.label.size(), pixelCount, cpuBytes, cpuBytes, cpuBytes},
            lossQuotas_[0],
            lossQuotas_[1],
            lossQuotas_[2],
            diagnostic);
        assert(lossSnapshot_);
    }

    static void verifyActualDeviceLoss()
    {
        assert(rhi_webgpu::webGpuNativeDeviceLost() && lossSnapshot_);
        const auto bytes = pixels();
        auto original = input(bytes.data());
        assert(lossSnapshot_->matches(original)
            && std::string_view(lossSnapshot_->executionDiagnostic())
                == "RHI-TEXTURE-UPLOAD-ORIGINAL-SOURCE-UNPROVEN");
        lossSnapshot_.reset();
        for (auto& quota : lossQuotas_) {
            assert(!quota->liveBytes());
            quota.reset();
        }
        std::puts(
            "BINDLESS_RHI_TEXTURE_UPLOAD_INPUT_LOSS_OK cpu-only unchanged gate original-owner exact-retire");
    }

private:
    static void verifyBinaryLabel(
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3>& quotas)
    {
        rhi::UploadTextureDesc desc;
        desc.label.assign("a\0\xff", 3u);
        const char* diagnostic = nullptr;
        auto snapshot = Snapshot::prepareOriginal(desc,
            {1u, true, 0u, 3u, 0u, cpuBytes, cpuBytes, cpuBytes},
            quotas[0],
            quotas[1],
            quotas[2],
            diagnostic);
        assert(snapshot && snapshot->label() == std::string_view("a\0\xff", 3u)
            && snapshot->matches(desc));
        snapshot.reset();
        for (const auto& q : quotas)
            assert(!q->liveBytes());
    }

    inline static std::shared_ptr<const Snapshot> lossSnapshot_;
    inline static std::array<std::shared_ptr<core::CpuMemoryQuota>, 3> lossQuotas_;
};
} // namespace arp::test

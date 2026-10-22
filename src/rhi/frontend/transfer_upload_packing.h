#pragma once
#include "core/algorithm/preflight_replay.h"
#include "rhi/frontend/transfer_upload_layout_validation.h"

namespace arp::rhi {
struct TextureUploadPackingBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxMipLayouts = 0u;
    std::uint64_t maxPackedBytes = 0u, maxCpuMetadataBytes = 0u, maxCpuPackedBytes = 0u;
};

class TextureUploadPacking final : public core::QuotaBoundSharedObjectIdentity {
    class ConstructionKey {
        friend class TextureUploadPacking;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

    using Validation = semantics::TextureUploadLayoutValidation;

public:
    using Destination = semantics::TextureUploadDestinationLayout;
    using Mip = semantics::TextureUploadPackedMip;

    TextureUploadPacking(ConstructionKey,
        std::shared_ptr<const TextureUploadInputSnapshot> input,
        Destination destination,
        std::uint32_t capacity)
        : input_(std::move(input)), destination_(destination), mips_(capacity)
    {}

    static const char* validateBudget(const TextureUploadPackingBudget& b) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-TEXTURE-UPLOAD-PACKING-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-TEXTURE-UPLOAD-PACKING-OPT-IN";
        if (!b.maxMipLayouts || !b.maxPackedBytes || b.maxPackedBytes > SIZE_MAX
            || !b.maxCpuMetadataBytes || !b.maxCpuPackedBytes)
            return "RHI-TEXTURE-UPLOAD-PACKING-BUDGET";
        return nullptr;
    }

    static std::shared_ptr<const TextureUploadPacking> prepareOriginal(
        std::shared_ptr<const TextureUploadInputSnapshot> input,
        const TextureDesc& destination,
        const TextureUploadPackingBudget& b,
        std::shared_ptr<core::CpuMemoryQuota> metadataQuota,
        std::shared_ptr<core::CpuMemoryQuota> packedQuota,
        const char*& diagnostic)
    {
        diagnostic = validateBudget(b);
        if (diagnostic)
            return {};
        if (!metadataQuota || metadataQuota->limitBytes() != b.maxCpuMetadataBytes || !packedQuota
            || packedQuota->limitBytes() != b.maxCpuPackedBytes) {
            diagnostic = "RHI-TEXTURE-UPLOAD-PACKING-QUOTA";
            return {};
        }
        if (!input || !input->hasQuotaBoundSharedOwner(input)) {
            diagnostic = "RHI-TEXTURE-UPLOAD-PACKING-ORIGINAL-INPUT";
            return {};
        }
        const auto layout = Destination::capture(destination);
        diagnostic = Validation::validate(*input, layout);
        if (diagnostic)
            return {};
        const auto count = Validation::mipCount(*input);
        if (count > b.maxMipLayouts) {
            diagnostic = "RHI-TEXTURE-UPLOAD-PACKING-CAPACITY";
            return {};
        }
        core::UniquePtr<TextureUploadPacking> result;
        std::uint64_t packedBytes = 0u;
        const auto mapper = [&] {
            return [&, cursor = Validation::Mapper(*input, layout)](
                       std::size_t index, Mip& mip) mutable {
                if (!cursor(index, mip)) {
                    diagnostic = cursor.diagnostic();
                    return false;
                }
                packedBytes = cursor.packedBytes();
                if (packedBytes > b.maxPackedBytes) {
                    diagnostic = "RHI-TEXTURE-UPLOAD-PACKING-CAPACITY";
                    return false;
                }
                return true;
            };
        };
        try {
            const auto status = core::PreflightReplay<Mip>::run(
                count,
                mapper,
                [&] {
                    core::CpuMemoryQuotaScope metadata(metadataQuota);
                    result = core::makeUnique<TextureUploadPacking>(
                        ConstructionKey{}, input, layout, b.maxMipLayouts);
                    core::CpuMemoryQuotaScope packed(packedQuota);
                    result->bytes_.resize(static_cast<std::size_t>(packedBytes), 0u);
                    return true;
                },
                [&](const Mip& mip) {
                    result->mips_.append(mip);
                    result->copyRows(mip);
                });
            if (status != core::PreflightReplayStatus::Completed)
                return {};
            core::CpuMemoryQuotaScope metadata(metadataQuota);
            return shareOriginal(std::move(result));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-TEXTURE-UPLOAD-PACKING-ALLOCATION";
            return {};
        } catch (const std::length_error&) {
            diagnostic = "RHI-TEXTURE-UPLOAD-PACKING-ALLOCATION";
            return {};
        }
    }

    const auto& inputOwner() const noexcept { return input_; }

    const Destination& destinationLayout() const noexcept { return destination_; }

    const auto& mipLayouts() const noexcept { return mips_.values(); }

    const auto& packedBytes() const noexcept { return bytes_; }

    bool matches(const UploadTextureDesc& input, const TextureDesc& destination) const noexcept
    {
        return input_->matches(input) && destination_.matches(destination);
    }

    const char* executionDiagnostic() const noexcept
    {
        return "RHI-TEXTURE-UPLOAD-ORIGINAL-SOURCE-UNPROVEN";
    }

private:
    void copyRows(const Mip& mip) noexcept
    {
        const core::StridedIndexRange sourceSlices(mip.sourceOffset,
            mip.sliceCount,
            static_cast<std::uint64_t>(mip.sourceBytesPerRow) * mip.sourceRowsPerImage);
        const core::StridedIndexRange packedSlices(mip.packedOffset,
            mip.sliceCount,
            static_cast<std::uint64_t>(mip.packedBytesPerRow) * mip.rowCount);
        for (std::uint32_t slice = 0u; slice < mip.sliceCount; ++slice) {
            std::uint64_t source = 0u, packed = 0u;
            sourceSlices.index(slice, source);
            packedSlices.index(slice, packed);
            const core::StridedIndexRange sourceRows(source, mip.rowCount, mip.sourceBytesPerRow),
                packedRows(packed, mip.rowCount, mip.packedBytesPerRow);
            for (std::uint32_t row = 0u; row < mip.rowCount; ++row) {
                sourceRows.index(row, source);
                packedRows.index(row, packed);
                std::memcpy(bytes_.data() + packed,
                    input_->pixelBytes().data() + source,
                    static_cast<std::size_t>(mip.rowBytes));
            }
        }
    }

    const std::shared_ptr<const TextureUploadInputSnapshot> input_;
    const Destination destination_;
    core::BoundedSequence<Mip> mips_;
    core::ByteBuffer bytes_{&core::defaultCpuMemoryResource()};
};
} // namespace arp::rhi

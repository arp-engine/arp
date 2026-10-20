#pragma once

#include "core/base64.h"
#include "core/shared_owner_projection.h"
#include "rhi/frontend/command_arguments.h"
#include "rhi/frontend/transfer_copy.h"

#include <optional>

// region Transfer buffer upload arguments
namespace arp::rhi {
// OriginalBufferUploadInput/1: scalar representation only. The independent
// variable store owns label; original staging provenance cannot be inferred.
class BufferUploadArguments final {
public:
    static BufferUploadArguments captureScalarFields(const UploadBufferDesc& desc) noexcept
    {
        return BufferUploadArguments(
            *TransferCopyArguments::capture(BufferCopyDesc{
                desc.sourceUploadBuffer, desc.destinationResource, 0u, 0u, desc.sizeBytes}),
            desc.stagingIntent,
            desc.backendOwnedStaging,
            desc.frameSerial);
    }

    GpuResourceId source() const noexcept { return copy_.source(); }

    GpuResourceId destination() const noexcept { return copy_.destination(); }

    CommandUsageKind kind() const noexcept { return CommandUsageKind::BufferUpload; }

    const TransferCopyArguments& copyArguments() const noexcept { return copy_; }

    StagingIntent stagingIntent() const noexcept { return intent_; }

    bool backendOwnedStaging() const noexcept { return backendOwned_; }

    std::uint64_t frameSerial() const noexcept { return frame_; }

    bool matches(const BufferUploadArguments& input) const noexcept
    {
        return copy_.matches(input.copy_) && intent_ == input.intent_
            && backendOwned_ == input.backendOwned_ && frame_ == input.frame_;
    }

    bool matchesScalarFields(const UploadBufferDesc& desc) const noexcept
    {
        return copy_.matches(BufferCopyDesc{
                   desc.sourceUploadBuffer, desc.destinationResource, 0u, 0u, desc.sizeBytes})
            && intent_ == desc.stagingIntent && backendOwned_ == desc.backendOwnedStaging
            && frame_ == desc.frameSerial;
    }

    void writeScalarFields(UploadBufferDesc& desc) const noexcept
    {
        BufferCopyDesc copy;
        copy_.write(copy);
        desc.sourceUploadBuffer = copy.sourceBuffer;
        desc.destinationResource = copy.destinationBuffer;
        desc.sizeBytes = copy.sizeBytes;
        desc.stagingIntent = intent_;
        desc.backendOwnedStaging = backendOwned_;
        desc.frameSerial = frame_;
    }

private:
    BufferUploadArguments(TransferCopyArguments copy,
        StagingIntent intent,
        bool backendOwned,
        std::uint64_t frame) noexcept
        : copy_(copy), intent_(intent), backendOwned_(backendOwned), frame_(frame)
    {}

    TransferCopyArguments copy_;
    StagingIntent intent_;
    bool backendOwned_;
    std::uint64_t frame_;
};

// endregion Transfer buffer upload arguments

// region Transfer upload arguments

// OriginalTextureUploadInputSnapshot/1. Representation only; no source,
// resource generation, format/layout validity or GPU execution authority.
class TextureUploadArguments final {
public:
    static TextureUploadArguments captureScalarFields(const UploadTextureDesc& desc) noexcept
    {
        return TextureUploadArguments(desc);
    }

    CommandUsageKind kind() const noexcept { return CommandUsageKind::TextureUpload; }

    GpuResourceId destination() const noexcept { return destination_; }

    const TextureSubresourceRange& range() const noexcept { return range_; }

    std::uint64_t resourceGeneration() const noexcept { return generation_; }

    std::uint32_t pixelDataSize() const noexcept { return pixelSize_; }

    bool matchesScalarFields(const UploadTextureDesc& desc) const noexcept
    {
        return matches(captureScalarFields(desc));
    }

    bool matches(const TextureUploadArguments& other) const noexcept
    {
        return destination_ == other.destination_ && extent_ == other.extent_
            && origin_ == other.origin_ && format_ == other.format_ && rowPitch_ == other.rowPitch_
            && rows_ == other.rows_ && mip_ == other.mip_ && layer_ == other.layer_
            && range_ == other.range_ && pixelSize_ == other.pixelSize_ && intent_ == other.intent_
            && backendOwned_ == other.backendOwned_ && frame_ == other.frame_
            && generation_ == other.generation_;
    }

    // Scalar write leaves label, mip records and caller pixel pointer untouched.
    void writeScalarFields(UploadTextureDesc& desc) const noexcept
    {
        desc.destinationTexture = destination_;
        desc.extent = extent_;
        desc.destinationOffset = origin_;
        desc.format = format_;
        desc.bytesPerRow = rowPitch_;
        desc.rowsPerImage = rows_;
        desc.mipLevel = mip_;
        desc.arrayLayer = layer_;
        desc.subresources = range_;
        desc.pixelDataSize = pixelSize_;
        desc.stagingIntent = intent_;
        desc.backendOwnedStaging = backendOwned_;
        desc.frameSerial = frame_;
        desc.resourceGeneration = generation_;
    }

    static bool sameMipLayout(
        const UploadTextureDesc::MipLayout& a, const UploadTextureDesc::MipLayout& b) noexcept
    {
        return TextureCopyMipLayoutArguments::capture(a).matches(b);
    }

private:
    explicit TextureUploadArguments(const UploadTextureDesc& desc) noexcept
        : destination_(desc.destinationTexture), extent_(desc.extent),
          origin_(desc.destinationOffset), format_(desc.format), rowPitch_(desc.bytesPerRow),
          rows_(desc.rowsPerImage), mip_(desc.mipLevel), layer_(desc.arrayLayer),
          range_(desc.subresources), pixelSize_(desc.pixelDataSize), intent_(desc.stagingIntent),
          backendOwned_(desc.backendOwnedStaging), frame_(desc.frameSerial),
          generation_(desc.resourceGeneration)
    {}

    GpuResourceId destination_;
    Extent3D extent_, origin_;
    TextureFormat format_;
    std::uint32_t rowPitch_, rows_, mip_, layer_;
    TextureSubresourceRange range_;
    std::uint32_t pixelSize_;
    StagingIntent intent_;
    bool backendOwned_;
    std::uint64_t frame_, generation_;
};

struct TextureUploadInputSnapshotBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxMipLayouts = 0u;
    std::uint64_t maxTextBytes = 0u, maxPixelBytes = 0u;
    std::uint64_t maxCpuMetadataBytes = 0u, maxCpuLabelBytes = 0u, maxCpuPixelBytes = 0u;
};

class TextureUploadInputSnapshot final : public core::QuotaBoundSharedObjectIdentity {
    class ConstructionKey {
        friend class TextureUploadInputSnapshot;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    TextureUploadInputSnapshot(ConstructionKey,
        const UploadTextureDesc& source,
        const TextureUploadInputSnapshotBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> pixelQuota)
        : scalar_(TextureUploadArguments::captureScalarFields(source)),
          callerPixelPointerPresent_(source.pixelData != nullptr), mips_(budget.maxMipLayouts),
          pixelQuota_(std::move(pixelQuota)), budget_(budget)
    {}

    static const char* validateBudget(const TextureUploadInputSnapshotBudget& b) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-TEXTURE-UPLOAD-INPUT-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-TEXTURE-UPLOAD-INPUT-OPT-IN";
        if (!b.maxCpuMetadataBytes || !b.maxCpuLabelBytes || !b.maxCpuPixelBytes
            || b.maxTextBytes > SIZE_MAX)
            return "RHI-TEXTURE-UPLOAD-INPUT-BUDGET";
        return nullptr;
    }

    static std::shared_ptr<const TextureUploadInputSnapshot> prepareOriginal(
        const UploadTextureDesc& input,
        const TextureUploadInputSnapshotBudget& b,
        std::shared_ptr<core::CpuMemoryQuota> metadataQuota,
        std::shared_ptr<core::CpuMemoryQuota> labelQuota,
        std::shared_ptr<core::CpuMemoryQuota> pixelQuota,
        const char*& diagnostic)
    {
        diagnostic = validateBudget(b);
        if (diagnostic)
            return {};
        if (!metadataQuota || metadataQuota->limitBytes() != b.maxCpuMetadataBytes || !labelQuota
            || labelQuota->limitBytes() != b.maxCpuLabelBytes || !pixelQuota
            || pixelQuota->limitBytes() != b.maxCpuPixelBytes) {
            diagnostic = "RHI-TEXTURE-UPLOAD-INPUT-QUOTA";
            return {};
        }
        if (input.pixelDataSize && !input.pixelData) {
            diagnostic = "RHI-TEXTURE-UPLOAD-INPUT-PIXEL-POINTER";
            return {};
        }
        if (input.mipLayouts.size() > b.maxMipLayouts || input.label.size() > b.maxTextBytes
            || input.pixelDataSize > b.maxPixelBytes) {
            diagnostic = "RHI-TEXTURE-UPLOAD-INPUT-CAPACITY";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope metadata(metadataQuota);
            auto result = core::makeUnique<TextureUploadInputSnapshot>(
                ConstructionKey{}, input, b, pixelQuota);
            for (const auto& mip : input.mipLayouts)
                result->mips_.append(mip);
            const CommandVariableArguments::Input label{
                *CommandScalarArguments::capture(
                    CommandUsageKind::TextureUpload, CommandScalarArguments::None{}),
                input.label};
            result->label_ = CommandVariableArguments::prepareOriginal(&label,
                1u,
                {1u, true, 1u, 0u, b.maxTextBytes, 0u, b.maxCpuLabelBytes},
                labelQuota,
                diagnostic);
            if (!result->label_)
                return {};
            {
                core::CpuMemoryQuotaScope pixels(pixelQuota);
                result->pixels_.resize(input.pixelDataSize);
                if (input.pixelDataSize)
                    std::memcpy(result->pixels_.data(), input.pixelData, input.pixelDataSize);
            }
            return shareOriginal(std::move(result));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-TEXTURE-UPLOAD-INPUT-ALLOCATION";
            return {};
        } catch (const std::length_error&) {
            diagnostic = "RHI-TEXTURE-UPLOAD-INPUT-ALLOCATION";
            return {};
        }
    }

    const TextureUploadArguments& scalarFields() const noexcept { return scalar_; }

    const auto& labelOwner() const noexcept { return label_; }

    std::string_view label() const noexcept { return label_->text(0u); }

    const auto& mipLayouts() const noexcept { return mips_.values(); }

    const auto& pixelBytes() const noexcept { return pixels_; }

    const auto& originalPixelQuota() const noexcept { return pixelQuota_; }

    bool overlapsCpuQuota(const core::CpuMemoryQuota* quota) const noexcept
    {
        return quota
            && (quota == originalMetadataQuota().get() || quota == pixelQuota_.get()
                || quota == label_->originalMetadataQuota().get());
    }

    static const char* validateOriginal(
        const std::shared_ptr<const TextureUploadInputSnapshot>& self) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-TEXTURE-UPLOAD-INPUT-OWNER";
        return self->validate();
    }

    const char* validate() const noexcept
    {
        if (const auto* id = validateBudget(budget_))
            return id;
        if (!originalMetadataQuota()
            || originalMetadataQuota()->limitBytes() != budget_.maxCpuMetadataBytes || !label_
            || !label_->hasQuotaBoundSharedOwner(label_) || !label_->originalMetadataQuota()
            || label_->originalMetadataQuota()->limitBytes() != budget_.maxCpuLabelBytes
            || !pixelQuota_ || pixelQuota_->limitBytes() != budget_.maxCpuPixelBytes)
            return "RHI-TEXTURE-UPLOAD-INPUT-QUOTA";
        if (pixels_.size() != scalar_.pixelDataSize() || pixels_.size() > budget_.maxPixelBytes
            || mipLayouts().size() > budget_.maxMipLayouts)
            return "RHI-TEXTURE-UPLOAD-INPUT-CAPACITY";
        if (label_->records().size() != 1u
            || !label_->matchesUsageKind(0u, CommandUsageKind::TextureUpload))
            return "RHI-TEXTURE-UPLOAD-INPUT-LABEL";
        return label_->validateCapacity(
            {1u, true, 1u, 0u, budget_.maxTextBytes, 0u, budget_.maxCpuLabelBytes});
    }

    bool callerPixelPointerPresent() const noexcept { return callerPixelPointerPresent_; }

    bool matches(const UploadTextureDesc& input) const noexcept
    {
        if (validate() || !scalar_.matchesScalarFields(input)
            || callerPixelPointerPresent_ != (input.pixelData != nullptr) || label() != input.label
            || mipLayouts().size() != input.mipLayouts.size())
            return false;
        for (std::size_t i = 0u; i < mipLayouts().size(); ++i)
            if (!TextureUploadArguments::sameMipLayout(mipLayouts()[i], input.mipLayouts[i]))
                return false;
        return !input.pixelDataSize
            || (input.pixelData
                && !std::memcmp(pixels_.data(), input.pixelData, input.pixelDataSize));
    }

    // Immutable CPU input never authorizes native source or recording.
    const char* executionDiagnostic() const noexcept
    {
        return "RHI-TEXTURE-UPLOAD-ORIGINAL-SOURCE-UNPROVEN";
    }

private:
    const TextureUploadArguments scalar_;
    const bool callerPixelPointerPresent_;
    core::BoundedSequence<UploadTextureDesc::MipLayout> mips_;
    std::shared_ptr<const CommandVariableArguments> label_;
    core::ByteBuffer pixels_{&core::defaultCpuMemoryResource()};
    const std::shared_ptr<core::CpuMemoryQuota> pixelQuota_;
    const TextureUploadInputSnapshotBudget budget_;
};

// Original full-input comparison value; retaining this snapshot is never a
// native source, resource-generation or target-write authorization.
class TextureUploadOriginalInputArguments final {
public:
    TextureUploadOriginalInputArguments() = default;

    static std::optional<TextureUploadOriginalInputArguments> captureOriginal(
        std::shared_ptr<const TextureUploadInputSnapshot> input) noexcept
    {
        if (TextureUploadInputSnapshot::validateOriginal(input))
            return {};
        return TextureUploadOriginalInputArguments(std::move(input));
    }

    CommandUsageKind kind() const noexcept { return CommandUsageKind::TextureUpload; }

    const auto& originalInput() const noexcept { return input_; }

    bool matches(const TextureUploadOriginalInputArguments& other) const noexcept
    {
        return !TextureUploadInputSnapshot::validateOriginal(input_)
            && !TextureUploadInputSnapshot::validateOriginal(other.input_)
            && input_.get() == other.input_.get()
            && core::SharedOwnerProjection::sameOwner(input_, other.input_);
    }

private:
    explicit TextureUploadOriginalInputArguments(
        std::shared_ptr<const TextureUploadInputSnapshot> input)
        : input_(std::move(input))
    {}

    std::shared_ptr<const TextureUploadInputSnapshot> input_;
};
} // namespace arp::rhi

// endregion Transfer upload arguments

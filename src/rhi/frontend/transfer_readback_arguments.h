#pragma once
#include "core/ownership/shared_owner_projection.h"
#include "rhi/frontend/command_arguments.h"
#include "rhi/frontend/transfer_copy.h"
#include "rhi/semantics/transfer_lowering.h"

namespace arp::rhi {
// OriginalTextureReadbackInput/1: exact CPU representation, never native owner
// or staging/map/result proof. Unknown and legacy values remain explicit input.
class TextureReadbackArguments final {
public:
    static TextureReadbackArguments captureScalarFields(const ReadbackTextureDesc& d) noexcept
    {
        return TextureReadbackArguments(d);
    }

    CommandUsageKind kind() const noexcept { return CommandUsageKind::TextureReadback; }

    GpuResourceId sourceResource() const noexcept { return source_; }

    const TextureSubresourceRange& range() const noexcept { return range_; }

    std::uint64_t byteCapacity() const noexcept { return bytes_; }

    bool matchesScalarFields(const ReadbackTextureDesc& d) const noexcept
    {
        return matches(captureScalarFields(d));
    }

    bool matches(const TextureReadbackArguments& other) const noexcept
    {
        return source_ == other.source_ && destination_ == other.destination_
            && bytes_ == other.bytes_ && intent_ == other.intent_
            && backendOwned_ == other.backendOwned_ && range_ == other.range_
            && extent_ == other.extent_ && pitch_ == other.pitch_ && rows_ == other.rows_
            && offset_ == other.offset_;
    }

    void writeScalarFields(ReadbackTextureDesc& d) const noexcept
    {
        d.sourceResource = source_;
        d.destinationReadbackBuffer = destination_;
        d.sizeBytes = bytes_;
        d.stagingIntent = intent_;
        d.backendOwnedStaging = backendOwned_;
        d.sourceSubresources = range_;
        d.extent = extent_;
        d.bytesPerRow = pitch_;
        d.rowsPerImage = rows_;
        d.destinationOffsetBytes = offset_;
    }

private:
    explicit TextureReadbackArguments(const ReadbackTextureDesc& d) noexcept
        : source_(d.sourceResource), destination_(d.destinationReadbackBuffer), bytes_(d.sizeBytes),
          intent_(d.stagingIntent), backendOwned_(d.backendOwnedStaging),
          range_(d.sourceSubresources), extent_(d.extent), pitch_(d.bytesPerRow),
          rows_(d.rowsPerImage), offset_(d.destinationOffsetBytes)
    {}

    GpuResourceId source_, destination_;
    std::uint64_t bytes_;
    StagingIntent intent_;
    bool backendOwned_;
    TextureSubresourceRange range_;
    Extent3D extent_;
    std::uint32_t pitch_, rows_;
    std::uint64_t offset_;
};

struct TextureReadbackInputSnapshotBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxMipLayouts = 0u;
    std::uint64_t maxTextBytes = 0u, maxCpuMetadataBytes = 0u, maxCpuLabelBytes = 0u;
};

class TextureReadbackInputSnapshot final : public core::QuotaBoundSharedObjectIdentity {
    class ConstructionKey {
        friend class TextureReadbackInputSnapshot;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    static constexpr std::uint32_t retainedOwnerCount = 2u;

    struct Lowering {
        bool cpuMetadataImplemented = false, validatorOnly = false;
        const char* diagnostic = "RHI-TEXTURE-READBACK-BACKEND-UNKNOWN";
    };

    static constexpr Lowering lowering(Backend backend) noexcept
    {
        return semantics::TransferLowering::TextureReadbackInputSnapshot_lowering<Lowering>(
            backend);
    }

    TextureReadbackInputSnapshot(ConstructionKey,
        const ReadbackTextureDesc& d,
        const TextureReadbackInputSnapshotBudget& budget)
        : fields_(TextureReadbackArguments::captureScalarFields(d)), mips_(budget.maxMipLayouts),
          budget_(budget)
    {}

    static const char* validateBudget(const TextureReadbackInputSnapshotBudget& b) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-TEXTURE-READBACK-INPUT-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-TEXTURE-READBACK-INPUT-OPT-IN";
        if (!b.maxCpuMetadataBytes || !b.maxCpuLabelBytes || b.maxTextBytes > SIZE_MAX)
            return "RHI-TEXTURE-READBACK-INPUT-BUDGET";
        return nullptr;
    }

    static std::shared_ptr<const TextureReadbackInputSnapshot> prepareOriginal(
        const ReadbackTextureDesc& d,
        const TextureReadbackInputSnapshotBudget& b,
        std::shared_ptr<core::CpuMemoryQuota> metadataQuota,
        std::shared_ptr<core::CpuMemoryQuota> labelQuota,
        const char*& diagnostic)
    {
        diagnostic = validateBudget(b);
        if (diagnostic)
            return {};
        if (!metadataQuota || metadataQuota->limitBytes() != b.maxCpuMetadataBytes || !labelQuota
            || labelQuota->limitBytes() != b.maxCpuLabelBytes) {
            diagnostic = "RHI-TEXTURE-READBACK-INPUT-QUOTA";
            return {};
        }
        if (d.mipLayouts.size() > b.maxMipLayouts || d.label.size() > b.maxTextBytes) {
            diagnostic = "RHI-TEXTURE-READBACK-INPUT-CAPACITY";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope metadata(metadataQuota);
            auto result = core::makeUnique<TextureReadbackInputSnapshot>(ConstructionKey{}, d, b);
            for (const auto& mip : d.mipLayouts)
                result->mips_.append(mip);
            const CommandVariableArguments::Input label{
                *CommandScalarArguments::capture(
                    CommandUsageKind::TextureReadback, CommandScalarArguments::None{}),
                d.label};
            result->label_ = CommandVariableArguments::prepareOriginal(&label,
                1u,
                {1u, true, 1u, 0u, b.maxTextBytes, 0u, b.maxCpuLabelBytes},
                labelQuota,
                diagnostic);
            if (!result->label_)
                return {};
            return shareOriginal(std::move(result));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-TEXTURE-READBACK-INPUT-ALLOCATION";
            return {};
        } catch (const std::length_error&) {
            diagnostic = "RHI-TEXTURE-READBACK-INPUT-ALLOCATION";
            return {};
        }
    }

    const auto& scalarFields() const noexcept { return fields_; }

    const auto& labelOwner() const noexcept { return label_; }

    std::string_view label() const noexcept { return label_->text(0u); }

    const auto& mipLayouts() const noexcept { return mips_.values(); }

    static const char* validateOriginal(
        const std::shared_ptr<const TextureReadbackInputSnapshot>& self) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-TEXTURE-READBACK-INPUT-OWNER";
        return self->validate();
    }

    const char* validate() const noexcept
    {
        if (const auto* id = validateBudget(budget_))
            return id;
        if (!originalMetadataQuota()
            || originalMetadataQuota()->limitBytes() != budget_.maxCpuMetadataBytes || !label_
            || !label_->hasQuotaBoundSharedOwner(label_) || !label_->originalMetadataQuota()
            || label_->originalMetadataQuota()->limitBytes() != budget_.maxCpuLabelBytes)
            return "RHI-TEXTURE-READBACK-INPUT-QUOTA";
        if (mipLayouts().size() > budget_.maxMipLayouts)
            return "RHI-TEXTURE-READBACK-INPUT-CAPACITY";
        if (label_->records().size() != 1u
            || !label_->matchesUsageKind(0u, CommandUsageKind::TextureReadback))
            return "RHI-TEXTURE-READBACK-INPUT-LABEL";
        return label_->validateCapacity(
            {1u, true, 1u, 0u, budget_.maxTextBytes, 0u, budget_.maxCpuLabelBytes});
    }

    bool matches(const ReadbackTextureDesc& d) const noexcept
    {
        if (validate() || !fields_.matchesScalarFields(d) || label() != d.label
            || mipLayouts().size() != d.mipLayouts.size())
            return false;
        for (std::size_t i = 0u; i < mipLayouts().size(); ++i)
            if (!TextureCopyMipLayoutArguments::capture(mipLayouts()[i]).matches(d.mipLayouts[i]))
                return false;
        return true;
    }

    const char* executionDiagnostic() const noexcept
    {
        return "RHI-TEXTURE-READBACK-ORIGINAL-OWNER-UNPROVEN";
    }

private:
    const TextureReadbackArguments fields_;
    core::BoundedSequence<TextureToBufferCopyDesc::MipLayout> mips_;
    std::shared_ptr<const CommandVariableArguments> label_;
    const TextureReadbackInputSnapshotBudget budget_;
};

// Exact original CPU input value; never source/copy/map/result authority.
class TextureReadbackOriginalInputArguments final {
public:
    TextureReadbackOriginalInputArguments() = default;

    static std::optional<TextureReadbackOriginalInputArguments> captureOriginal(
        std::shared_ptr<const TextureReadbackInputSnapshot> input) noexcept
    {
        if (TextureReadbackInputSnapshot::validateOriginal(input))
            return {};
        return TextureReadbackOriginalInputArguments(std::move(input));
    }

    CommandUsageKind kind() const noexcept { return CommandUsageKind::TextureReadback; }

    const auto& originalInput() const noexcept { return input_; }

    bool matches(const TextureReadbackOriginalInputArguments& other) const noexcept
    {
        return !TextureReadbackInputSnapshot::validateOriginal(input_)
            && !TextureReadbackInputSnapshot::validateOriginal(other.input_)
            && core::SharedOwnerProjection::sameOwner(input_, other.input_);
    }

private:
    explicit TextureReadbackOriginalInputArguments(
        std::shared_ptr<const TextureReadbackInputSnapshot> input)
        : input_(std::move(input))
    {}

    std::shared_ptr<const TextureReadbackInputSnapshot> input_;
};
} // namespace arp::rhi

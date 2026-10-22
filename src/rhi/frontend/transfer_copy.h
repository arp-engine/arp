#pragma once

#include "core/container/bounded_sequence.h"
#include "core/ownership/quota_bound_shared_identity.h"
#include "rhi/frontend/command_model.h"
#include "rhi/gpu_commands.h"

#include <optional>

// region Transfer copy arguments
namespace arp::rhi {

// FrozenTransferArguments/1: pointer-free single-mip copy input. Explicit mip
// layouts require their own lowering; capture never silently drops them.
class TransferCopyArguments final {
public:
    static TransferCopyArguments finish() noexcept { return {}; }

    static std::optional<TransferCopyArguments> capture(const BufferCopyDesc& d) noexcept
    {
        TransferCopyArguments a;
        a.kind_ = CommandUsageKind::BufferCopy;
        a.source_ = d.sourceBuffer;
        a.destination_ = d.destinationBuffer;
        a.sourceOffset_ = d.sourceOffsetBytes;
        a.destinationOffset_ = d.destinationOffsetBytes;
        a.bytes_ = d.sizeBytes;
        return a;
    }

    static std::optional<TransferCopyArguments> capture(const BufferToTextureCopyDesc& d) noexcept
    {
        if (!d.mipLayouts.empty())
            return {};
        return captureScalarFields(d);
    }

    // Only for a complete owner that separately freezes and binds every mip.
    // This scalar projection alone never represents an explicit-mip input.
    static TransferCopyArguments captureScalarFields(const BufferToTextureCopyDesc& d) noexcept
    {
        TransferCopyArguments a;
        a.kind_ = CommandUsageKind::BufferToTextureCopy;
        a.source_ = d.sourceBuffer;
        a.destination_ = d.destinationTexture;
        a.sourceOffset_ = d.bufferOffsetBytes;
        a.destinationRange_ = d.textureSubresources;
        a.extent_ = d.extent;
        a.bytesPerRow_ = d.bytesPerRow;
        a.rowsPerImage_ = d.rowsPerImage;
        return a;
    }

    static std::optional<TransferCopyArguments> capture(const TextureToBufferCopyDesc& d) noexcept
    {
        if (!d.mipLayouts.empty())
            return {};
        return captureScalarFields(d);
    }

    static TransferCopyArguments captureScalarFields(const TextureToBufferCopyDesc& d) noexcept
    {
        TransferCopyArguments a;
        a.kind_ = CommandUsageKind::TextureToBufferCopy;
        a.source_ = d.sourceTexture;
        a.destination_ = d.destinationBuffer;
        a.destinationOffset_ = d.bufferOffsetBytes;
        a.sourceRange_ = d.textureSubresources;
        a.extent_ = d.extent;
        a.bytesPerRow_ = d.bytesPerRow;
        a.rowsPerImage_ = d.rowsPerImage;
        return a;
    }

    static std::optional<TransferCopyArguments> capture(const TextureToTextureCopyDesc& d) noexcept
    {
        if (!d.mipLayouts.empty())
            return {};
        return captureScalarFields(d);
    }

    static TransferCopyArguments captureScalarFields(const TextureToTextureCopyDesc& d) noexcept
    {
        TransferCopyArguments a;
        a.kind_ = CommandUsageKind::TextureToTextureCopy;
        a.source_ = d.sourceTexture;
        a.destination_ = d.destinationTexture;
        a.sourceRange_ = d.sourceSubresources;
        a.destinationRange_ = d.destinationSubresources;
        a.extent_ = d.extent;
        return a;
    }

    static TransferCopyArguments captureScalarFields(const BufferCopyDesc& d) noexcept
    {
        return *capture(d);
    }

    CommandUsageKind kind() const noexcept { return kind_; }

    GpuResourceId source() const noexcept { return source_; }

    GpuResourceId destination() const noexcept { return destination_; }

    const TextureSubresourceRange& sourceRange() const noexcept { return sourceRange_; }

    const TextureSubresourceRange& destinationRange() const noexcept { return destinationRange_; }

    template <class Desc> bool matches(const Desc& d) const noexcept
    {
        const auto a = capture(d);
        return a && sameValues(*a);
    }

    bool matches(const TransferCopyArguments& input) const noexcept { return sameValues(input); }

    bool write(BufferCopyDesc& d) const noexcept
    {
        if (kind_ != CommandUsageKind::BufferCopy)
            return false;
        d = {source_, destination_, sourceOffset_, destinationOffset_, bytes_};
        return true;
    }

    bool write(BufferToTextureCopyDesc& d) const noexcept
    {
        if (kind_ != CommandUsageKind::BufferToTextureCopy || !d.mipLayouts.empty())
            return false;
        d.sourceBuffer = source_;
        d.destinationTexture = destination_;
        d.bufferOffsetBytes = sourceOffset_;
        d.textureSubresources = destinationRange_;
        d.extent = extent_;
        d.bytesPerRow = bytesPerRow_;
        d.rowsPerImage = rowsPerImage_;
        return true;
    }

    bool write(TextureToBufferCopyDesc& d) const noexcept
    {
        if (kind_ != CommandUsageKind::TextureToBufferCopy || !d.mipLayouts.empty())
            return false;
        d.sourceTexture = source_;
        d.destinationBuffer = destination_;
        d.bufferOffsetBytes = destinationOffset_;
        d.textureSubresources = sourceRange_;
        d.extent = extent_;
        d.bytesPerRow = bytesPerRow_;
        d.rowsPerImage = rowsPerImage_;
        return true;
    }

    bool write(TextureToTextureCopyDesc& d) const noexcept
    {
        if (kind_ != CommandUsageKind::TextureToTextureCopy || !d.mipLayouts.empty())
            return false;
        d.sourceTexture = source_;
        d.destinationTexture = destination_;
        d.sourceSubresources = sourceRange_;
        d.destinationSubresources = destinationRange_;
        d.extent = extent_;
        return true;
    }

private:
    TransferCopyArguments() = default;

    bool sameValues(const TransferCopyArguments& a) const noexcept
    {
        return kind_ == a.kind_ && source_ == a.source_ && destination_ == a.destination_
            && sourceOffset_ == a.sourceOffset_ && destinationOffset_ == a.destinationOffset_
            && bytes_ == a.bytes_ && sourceRange_ == a.sourceRange_
            && destinationRange_ == a.destinationRange_ && extent_.width == a.extent_.width
            && extent_.height == a.extent_.height && extent_.depth == a.extent_.depth
            && bytesPerRow_ == a.bytesPerRow_ && rowsPerImage_ == a.rowsPerImage_;
    }

    CommandUsageKind kind_ = CommandUsageKind::Finish;
    GpuResourceId source_, destination_;
    std::uint64_t sourceOffset_ = 0u, destinationOffset_ = 0u, bytes_ = 0u;
    TextureSubresourceRange sourceRange_, destinationRange_;
    Extent3D extent_;
    std::uint32_t bytesPerRow_ = 0u, rowsPerImage_ = 0u;
};

// endregion Transfer copy arguments

// region Transfer copy mip layout arguments

// Shared pointer-free field contract for upload and buffer/texture copy mips.
// Capture represents exact values; it does not validate geometry or provenance.
class TextureCopyMipLayoutArguments final {
public:
    template <class Layout>
    static TextureCopyMipLayoutArguments capture(const Layout& input) noexcept
    {
        return TextureCopyMipLayoutArguments(input.mipLevel,
            input.extent,
            input.bytesPerRow,
            input.rowsPerImage,
            input.bufferOffsetBytes);
    }

    template <class Layout> bool matches(const Layout& input) const noexcept
    {
        return mip_ == input.mipLevel && extent_ == input.extent && pitch_ == input.bytesPerRow
            && rows_ == input.rowsPerImage && offset_ == input.bufferOffsetBytes;
    }

    static TextureCopyMipLayoutArguments capture(
        const TextureToTextureCopyDesc::MipLayout& input) noexcept
    {
        return TextureCopyMipLayoutArguments(input.mipLevel, input.extent, 0u, 0u, 0u);
    }

    bool matches(const TextureToTextureCopyDesc::MipLayout& input) const noexcept
    {
        return mip_ == input.mipLevel && extent_ == input.extent && !pitch_ && !rows_ && !offset_;
    }

    template <class Layout> bool write(Layout& output) const noexcept
    {
        output = {mip_, extent_, pitch_, rows_, offset_};
        return true;
    }

    bool write(TextureToTextureCopyDesc::MipLayout& output) const noexcept
    {
        if (pitch_ || rows_ || offset_)
            return false;
        output = {mip_, extent_};
        return true;
    }

private:
    TextureCopyMipLayoutArguments(std::uint32_t mip,
        Extent3D extent,
        std::uint32_t pitch,
        std::uint32_t rows,
        std::uint64_t offset) noexcept
        : mip_(mip), extent_(extent), pitch_(pitch), rows_(rows), offset_(offset)
    {}

    std::uint32_t mip_;
    Extent3D extent_;
    std::uint32_t pitch_, rows_;
    std::uint64_t offset_;
};

// endregion Transfer copy mip layout arguments

// region Transfer copy input snapshot

struct TransferCopyInputSnapshotBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxMipMappings = 0u;
    std::uint64_t maxCpuMetadataBytes = 0u;
};

// Complete immutable CPU copy fields, including exact explicit mip order and
// layout. No geometry/layout/state/native or source provenance is inferred.
// Scalar projection is private to this full ownership binding at consumption.
class TransferCopyInputSnapshot final : public core::QuotaBoundSharedObjectIdentity {
    class ConstructionKey {
        friend class TransferCopyInputSnapshot;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    TransferCopyInputSnapshot(ConstructionKey,
        TransferCopyArguments scalar,
        std::size_t count,
        const TransferCopyInputSnapshotBudget& budget)
        : scalar_(scalar), mips_(count), budget_(budget)
    {}

    static const char* validateBudget(const TransferCopyInputSnapshotBudget& b) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-COPY-SNAPSHOT-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-COPY-SNAPSHOT-OPT-IN";
        if (!b.maxCpuMetadataBytes)
            return "RHI-COPY-SNAPSHOT-BUDGET";
        return nullptr;
    }

    template <class Desc>
    static std::shared_ptr<const TransferCopyInputSnapshot> prepareOriginal(const Desc& input,
        const TransferCopyInputSnapshotBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        static_assert(supported<Desc>);
        diagnostic = validateBudget(budget);
        if (diagnostic)
            return {};
        if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes) {
            diagnostic = "RHI-COPY-SNAPSHOT-QUOTA";
            return {};
        }
        const auto count = mipCount(input);
        if (count > budget.maxMipMappings) {
            diagnostic = "RHI-COPY-SNAPSHOT-MIP-CAPACITY";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope scope(quota);
            auto result = core::makeUnique<TransferCopyInputSnapshot>(ConstructionKey{},
                TransferCopyArguments::captureScalarFields(input),
                count,
                budget);
            if constexpr (!std::is_same_v<Desc, BufferCopyDesc>)
                for (const auto& mip : input.mipLayouts)
                    result->mips_.append(TextureCopyMipLayoutArguments::capture(mip));
            return shareOriginal(std::move(result));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-COPY-SNAPSHOT-ALLOCATION";
            return {};
        } catch (const std::length_error&) {
            diagnostic = "RHI-COPY-SNAPSHOT-ALLOCATION";
            return {};
        }
    }

    static const char* validateOriginal(
        const std::shared_ptr<const TransferCopyInputSnapshot>& self) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-COPY-SNAPSHOT-OWNER";
        if (const auto* id = validateBudget(self->budget_))
            return id;
        return self->originalMetadataQuota()
                && self->originalMetadataQuota()->limitBytes() == self->budget_.maxCpuMetadataBytes
            ? nullptr
            : "RHI-COPY-SNAPSHOT-QUOTA";
    }

    template <class Desc> bool matches(const Desc& input) const noexcept
    {
        static_assert(supported<Desc>);
        if (!scalar_.matches(TransferCopyArguments::captureScalarFields(input))
            || mipCount(input) != mipMappingCount())
            return false;
        if constexpr (!std::is_same_v<Desc, BufferCopyDesc>)
            for (std::size_t i = 0u; i < input.mipLayouts.size(); ++i)
                if (!mips_.values()[i].matches(input.mipLayouts[i]))
                    return false;
        return true;
    }

    CommandUsageKind kind() const noexcept { return scalar_.kind(); }

    GpuResourceId source() const noexcept { return scalar_.source(); }

    GpuResourceId destination() const noexcept { return scalar_.destination(); }

    const TransferCopyArguments& scalarFields() const noexcept { return scalar_; }

    std::size_t mipMappingCount() const noexcept { return mips_.values().size(); }

    // Exact stored mip projection; never reconstruct fields from scalar defaults.
    template <class Layout> bool writeMip(std::size_t index, Layout& output) const noexcept
    {
        return index < mips_.values().size() && mips_.values()[index].write(output);
    }

    bool nativeExecutionProven() const noexcept { return false; }

private:
    template <class T>
    static constexpr bool supported = std::is_same_v<T, BufferCopyDesc>
        || std::is_same_v<T, BufferToTextureCopyDesc> || std::is_same_v<T, TextureToBufferCopyDesc>
        || std::is_same_v<T, TextureToTextureCopyDesc>;

    template <class Desc> static std::size_t mipCount(const Desc& input) noexcept
    {
        if constexpr (std::is_same_v<Desc, BufferCopyDesc>)
            return 0u;
        else
            return input.mipLayouts.size();
    }

    const TransferCopyArguments scalar_;
    core::BoundedSequence<TextureCopyMipLayoutArguments> mips_;
    const TransferCopyInputSnapshotBudget budget_;
};

// endregion Transfer copy input snapshot

// region Transfer copy projection

// Exact descriptor expansion from authored scalar fields and explicit mip
// records. No geometric inference, native handles, allocation or GPU authority.
class TransferCopyProjection final {
public:
    template <class Desc, class Visitor>
    static const char* visit(const TransferCopyArguments& fields,
        const TransferCopyInputSnapshot* full,
        Visitor&& visitor) noexcept
    {
        static_assert(noexcept(visitor(std::declval<const Desc&>())),
            "Copy projection visitor must not throw");
        static_assert(std::is_same_v<decltype(visitor(std::declval<const Desc&>())), const char*>,
            "Copy projection visitor returns a diagnostic");
        if (full && !fields.matches(full->scalarFields()))
            return "RHI-COPY-PROJECTION-SCALAR-BINDING";
        Desc scalar;
        if (!fields.write(scalar))
            return "RHI-COPY-PROJECTION-KIND";
        if constexpr (std::is_same_v<Desc, BufferCopyDesc>) {
            if (full && full->mipMappingCount())
                return "RHI-COPY-PROJECTION-BUFFER-MIPS";
            return visitor(scalar);
        } else {
            const auto count = full && full->mipMappingCount() ? full->mipMappingCount() : 1u;
            const auto range = [&]() noexcept {
                if constexpr (std::is_same_v<Desc, TextureToTextureCopyDesc>)
                    return scalar.sourceSubresources;
                else
                    return scalar.textureSubresources;
            }();
            if (range.mipLevelCount != count || count - 1u > UINT32_MAX - range.baseMipLevel)
                return "RHI-COPY-PROJECTION-MIP-COUNT";
            if constexpr (std::is_same_v<Desc, TextureToTextureCopyDesc>)
                if (scalar.destinationSubresources.mipLevelCount != count
                    || count - 1u > UINT32_MAX - scalar.destinationSubresources.baseMipLevel)
                    return "RHI-COPY-PROJECTION-MIP-COUNT";
            for (std::size_t i = 0u; i < count; ++i) {
                Desc projected;
                if (!fields.write(projected))
                    return "RHI-COPY-PROJECTION-KIND";
                typename Desc::MipLayout mip;
                if (full && full->mipMappingCount()) {
                    if (!full->writeMip(i, mip))
                        return "RHI-COPY-PROJECTION-MIP-TYPE";
                } else {
                    mip.mipLevel = range.baseMipLevel;
                    mip.extent = scalar.extent;
                    if constexpr (!std::is_same_v<Desc, TextureToTextureCopyDesc>) {
                        mip.bytesPerRow = scalar.bytesPerRow;
                        mip.rowsPerImage = scalar.rowsPerImage;
                        mip.bufferOffsetBytes = scalar.bufferOffsetBytes;
                    }
                }
                if (mip.mipLevel != range.baseMipLevel + i)
                    return "RHI-COPY-PROJECTION-MIP-ORDER";
                if (!i && (mip.extent != scalar.extent))
                    return "RHI-COPY-PROJECTION-FIRST-MIP";
                projected.extent = mip.extent;
                if constexpr (std::is_same_v<Desc, TextureToTextureCopyDesc>) {
                    projected.sourceSubresources.baseMipLevel = mip.mipLevel;
                    projected.sourceSubresources.mipLevelCount = 1u;
                    projected.destinationSubresources.baseMipLevel += static_cast<std::uint32_t>(i);
                    projected.destinationSubresources.mipLevelCount = 1u;
                } else {
                    if (!i
                        && (mip.bytesPerRow != scalar.bytesPerRow
                            || mip.rowsPerImage != scalar.rowsPerImage
                            || mip.bufferOffsetBytes != scalar.bufferOffsetBytes))
                        return "RHI-COPY-PROJECTION-FIRST-MIP";
                    projected.textureSubresources.baseMipLevel = mip.mipLevel;
                    projected.textureSubresources.mipLevelCount = 1u;
                    projected.bytesPerRow = mip.bytesPerRow;
                    projected.rowsPerImage = mip.rowsPerImage;
                    projected.bufferOffsetBytes = mip.bufferOffsetBytes;
                }
                if (const auto* id = visitor(projected))
                    return id;
            }
            return nullptr;
        }
    }
};
} // namespace arp::rhi

// endregion Transfer copy projection

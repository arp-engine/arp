#pragma once
#include "core/algorithm/preflight_replay.h"
#include "core/algorithm/strided_index_range.h"
#include "core/ownership/cold_owned_object_slot.h"
#include "rhi/frontend/resource_leases.h"
#include "rhi/frontend/transfer_readback_layout_validation.h"
#include "rhi/frontend/transfer_readback_statistics.h"
#include "rhi/gpu_extensions.h"
#include "rhi/semantics/transfer_lowering.h"

// region Transfer readback layout
namespace arp::rhi {
struct TextureReadbackLayoutBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxMipCopies = 0u;
    std::uint64_t maxReadbackBytes = 0u, maxCpuMetadataBytes = 0u;
};

class TextureReadbackLayout final : public core::QuotaBoundSharedObjectIdentity {
    class ConstructionKey {
        friend class TextureReadbackLayout;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

    using Validation = semantics::TextureReadbackLayoutValidation;

public:
    static constexpr std::uint32_t retainedOwnerCount = 1u;
    using Mip = semantics::TextureReadbackMip;

    static constexpr auto lowering(Backend backend) noexcept
    {
        return TextureReadbackInputSnapshot::lowering(backend);
    }

    TextureReadbackLayout(ConstructionKey,
        std::shared_ptr<const TextureReadbackInputSnapshot> input,
        TextureResourceLayout source,
        const TextureReadbackLayoutBudget& budget)
        : input_(std::move(input)), source_(source), mips_(budget.maxMipCopies), budget_(budget)
    {}

    static const char* validateBudget(const TextureReadbackLayoutBudget& b) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-TEXTURE-READBACK-LAYOUT-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-TEXTURE-READBACK-LAYOUT-OPT-IN";
        if (!b.maxMipCopies || !b.maxReadbackBytes || !b.maxCpuMetadataBytes)
            return "RHI-TEXTURE-READBACK-LAYOUT-BUDGET";
        return nullptr;
    }

    static std::shared_ptr<const TextureReadbackLayout> prepareOriginal(
        std::shared_ptr<const TextureReadbackInputSnapshot> input,
        const TextureDesc& source,
        const TextureReadbackLayoutBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        diagnostic = validateBudget(budget);
        if (diagnostic)
            return {};
        if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes) {
            diagnostic = "RHI-TEXTURE-READBACK-LAYOUT-QUOTA";
            return {};
        }
        if (!input || !input->hasQuotaBoundSharedOwner(input)) {
            diagnostic = "RHI-TEXTURE-READBACK-LAYOUT-ORIGINAL-INPUT";
            return {};
        }
        if ((diagnostic = TextureReadbackInputSnapshot::validateOriginal(input)))
            return {};
        const auto layout = TextureResourceLayout::capture(source);
        diagnostic = Validation::validate(*input, layout);
        if (diagnostic)
            return {};
        ReadbackTextureDesc d;
        input->scalarFields().writeScalarFields(d);
        const auto count = Validation::mipCount(*input);
        if (count > budget.maxMipCopies || d.sizeBytes > budget.maxReadbackBytes) {
            diagnostic = "RHI-TEXTURE-READBACK-LAYOUT-CAPACITY";
            return {};
        }
        core::UniquePtr<TextureReadbackLayout> result;
        const auto mapper = [&] {
            return [&, cursor = Validation::Mapper(*input, layout)](
                       std::size_t index, Mip& mip) mutable {
                if (!cursor(index, mip)) {
                    diagnostic = cursor.diagnostic();
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
                    core::CpuMemoryQuotaScope scope(quota);
                    result = core::makeUnique<TextureReadbackLayout>(
                        ConstructionKey{}, input, layout, budget);
                    return true;
                },
                [&](const Mip& mip) {
                    result->mips_.append(mip);
                });
            if (status != core::PreflightReplayStatus::Completed)
                return {};
            core::CpuMemoryQuotaScope scope(quota);
            return shareOriginal(std::move(result));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-TEXTURE-READBACK-LAYOUT-ALLOCATION";
            return {};
        } catch (const std::length_error&) {
            diagnostic = "RHI-TEXTURE-READBACK-LAYOUT-ALLOCATION";
            return {};
        }
    }

    static const char* validateOriginal(
        const std::shared_ptr<const TextureReadbackLayout>& self) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-TEXTURE-READBACK-LAYOUT-ORIGINAL-OWNER";
        return self->validate();
    }

    const char* validate() const noexcept
    {
        if (const auto* id = validateBudget(budget_))
            return id;
        if (!originalMetadataQuota()
            || originalMetadataQuota()->limitBytes() != budget_.maxCpuMetadataBytes)
            return "RHI-TEXTURE-READBACK-LAYOUT-QUOTA";
        if (const auto* id = TextureReadbackInputSnapshot::validateOriginal(input_))
            return id;
        if (mips_.values().size() != Validation::mipCount(*input_)
            || mips_.values().size() > budget_.maxMipCopies
            || input_->scalarFields().byteCapacity() > budget_.maxReadbackBytes)
            return "RHI-TEXTURE-READBACK-LAYOUT-CAPACITY";
        Validation::Mapper mapper(*input_, source_);
        for (std::size_t i = 0u; i < mips_.values().size(); ++i) {
            Mip expected;
            if (!mapper(i, expected))
                return mapper.diagnostic();
            if (!mips_.values()[i].matches(expected))
                return "RHI-TEXTURE-READBACK-LAYOUT-MIP-MISMATCH";
        }
        return nullptr;
    }

    const auto& inputOwner() const noexcept { return input_; }

    const auto& sourceLayout() const noexcept { return source_; }

    const auto& mipCopies() const noexcept { return mips_.values(); }

    bool matches(const ReadbackTextureDesc& input, const TextureDesc& source) const noexcept
    {
        return !validate() && input_->matches(input) && source_.matches(source);
    }

    static const char* validateOriginalMip(const std::shared_ptr<const TextureReadbackLayout>& self,
        std::size_t index,
        const Mip& observed) noexcept
    {
        if (const auto* id = validateOriginal(self))
            return id;
        if (index >= self->mipCopies().size())
            return "RHI-TEXTURE-READBACK-LAYOUT-MIP-INDEX";
        return self->mipCopies()[index].matches(observed)
            ? nullptr
            : "RHI-TEXTURE-READBACK-LAYOUT-MIP-MISMATCH";
    }

    const char* executionDiagnostic() const noexcept
    {
        return "RHI-TEXTURE-READBACK-ORIGINAL-OWNER-UNPROVEN";
    }

private:
    const std::shared_ptr<const TextureReadbackInputSnapshot> input_;
    const TextureResourceLayout source_;
    core::BoundedSequence<Mip> mips_;
    const TextureReadbackLayoutBudget budget_;
};

// endregion Transfer readback layout

// region Transfer readback request declaration

struct TextureReadbackRequestDeclarationBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxMipCopies = 0u;
    std::uint64_t maxLabelBytes = 0u, maxOriginalReadbackBytes = 0u, maxCpuMetadataBytes = 0u;
};

// Caller correlation names one sealed layout; CPU metadata never proves execution.
class TextureReadbackRequestDeclaration final : public core::QuotaBoundSharedObjectIdentity {
    class ConstructionKey {
        friend class TextureReadbackRequestDeclaration;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    static constexpr std::uint32_t retainedOwnerCount = 1u;

    TextureReadbackRequestDeclaration(ConstructionKey,
        std::shared_ptr<const TextureReadbackLayout> layout,
        TextureReadbackIdentity identity,
        const TextureReadbackRequestDeclarationBudget& budget)
        : layout_(std::move(layout)), identity_(identity), budget_(budget)
    {}

    static constexpr auto lowering(Backend backend) noexcept
    {
        return TextureReadbackLayout::lowering(backend);
    }

    static const char* validateBudget(const TextureReadbackRequestDeclarationBudget& b) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-READBACK-REQUEST-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-READBACK-REQUEST-OPT-IN";
        if (!b.maxMipCopies || !b.maxOriginalReadbackBytes || !b.maxCpuMetadataBytes)
            return "RHI-READBACK-REQUEST-BUDGET";
        return nullptr;
    }

    static std::shared_ptr<const TextureReadbackRequestDeclaration> prepareOriginal(
        std::shared_ptr<const TextureReadbackLayout> layout,
        TextureReadbackIdentity identity,
        const TextureReadbackRequestDeclarationBudget& b,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        diagnostic = validateBudget(b);
        if (diagnostic)
            return {};
        if (!quota || quota->limitBytes() != b.maxCpuMetadataBytes) {
            diagnostic = "RHI-READBACK-REQUEST-QUOTA";
            return {};
        }
        if (!layout || !layout->hasQuotaBoundSharedOwner(layout)) {
            diagnostic = "RHI-READBACK-REQUEST-LAYOUT-OWNER";
            return {};
        }
        if ((diagnostic = TextureReadbackLayout::validateOriginal(layout)))
            return {};
        if (!identity.requestId || !identity.frameSerial || !identity.resourceGeneration) {
            diagnostic = "RHI-READBACK-REQUEST-IDENTITY";
            return {};
        }
        if (layout->mipCopies().size() > b.maxMipCopies
            || layout->inputOwner()->label().size() > b.maxLabelBytes
            || layout->inputOwner()->scalarFields().byteCapacity() > b.maxOriginalReadbackBytes) {
            diagnostic = "RHI-READBACK-REQUEST-CAPACITY";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope scope(quota);
            return shareOriginal(core::makeUnique<TextureReadbackRequestDeclaration>(
                ConstructionKey{}, std::move(layout), identity, b));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-READBACK-REQUEST-ALLOCATION";
            return {};
        }
    }

    static const char* validateOriginal(
        const std::shared_ptr<const TextureReadbackRequestDeclaration>& self,
        const std::shared_ptr<const TextureReadbackLayout>& layout) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-READBACK-REQUEST-OWNER";
        if (!layout || !layout->hasQuotaBoundSharedOwner(layout)
            || !core::SharedOwnerProjection::sameOwner(self->layout_, layout))
            return "RHI-READBACK-REQUEST-LAYOUT-OWNER";
        return self->validate();
    }

    const char* validate() const noexcept
    {
        if (const auto* id = validateBudget(budget_))
            return id;
        if (!originalMetadataQuota()
            || originalMetadataQuota()->limitBytes() != budget_.maxCpuMetadataBytes)
            return "RHI-READBACK-REQUEST-QUOTA";
        if (const auto* id = TextureReadbackLayout::validateOriginal(layout_))
            return id;
        if (!identity_.requestId || !identity_.frameSerial || !identity_.resourceGeneration)
            return "RHI-READBACK-REQUEST-IDENTITY";
        if (layout_->mipCopies().size() > budget_.maxMipCopies
            || label().size() > budget_.maxLabelBytes
            || layout_->inputOwner()->scalarFields().byteCapacity()
                > budget_.maxOriginalReadbackBytes)
            return "RHI-READBACK-REQUEST-CAPACITY";
        return nullptr;
    }

    const auto& originalLayout() const noexcept { return layout_; }

    TextureReadbackIdentity identity() const noexcept { return identity_; }

    GpuResourceId source() const noexcept
    {
        return layout_->inputOwner()->scalarFields().sourceResource();
    }

    TextureFormat format() const noexcept { return layout_->sourceLayout().format; }

    std::string_view label() const noexcept { return layout_->inputOwner()->label(); }

    bool nativeExecutionProven() const noexcept { return false; }

private:
    const std::shared_ptr<const TextureReadbackLayout> layout_;
    const TextureReadbackIdentity identity_;
    const TextureReadbackRequestDeclarationBudget budget_;
};

// endregion Transfer readback request declaration

// region Transfer readback statistics plan

struct TextureReadbackStatisticsPlanBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxViews = 0u;
    std::uint64_t maxAnalyzedPixels = 0u, maxOriginalReadbackBytes = 0u, maxCpuMetadataBytes = 0u;
};

// CPU view semantics only. Native consumers must separately prove publication.
class TextureReadbackStatisticsPlan final : public core::QuotaBoundSharedObjectIdentity {
    class ConstructionKey {
        friend class TextureReadbackStatisticsPlan;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    enum class SliceDomain { ArrayLayer, DepthSlice };

    struct View {
        std::uint32_t mip, sliceOrdinal, logicalSlice, width, height, bytesPerRow;
        std::uint64_t byteOffset, byteSpan;
        SliceDomain domain;
    };

    struct Lowering {
        bool cpuPlanImplemented = false, validatorOnly = false;
        const char* diagnostic = "RHI-READBACK-STATISTICS-BACKEND";
    };

    static constexpr Lowering lowering(Backend backend) noexcept
    {
        return semantics::TransferLowering::TextureReadbackStatisticsPlan_lowering<Lowering>(
            backend);
    }

    TextureReadbackStatisticsPlan(ConstructionKey,
        std::shared_ptr<const TextureReadbackLayout> layout,
        std::size_t count,
        std::uint64_t bytes,
        std::uint64_t pixels)
        : layout_(std::move(layout)), views_(count), bytes_(bytes), pixels_(pixels)
    {}

    static const char* validateBudget(const TextureReadbackStatisticsPlanBudget& b) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-READBACK-STATISTICS-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-READBACK-STATISTICS-OPT-IN";
        if (!b.maxViews || !b.maxAnalyzedPixels || !b.maxOriginalReadbackBytes
            || !b.maxCpuMetadataBytes)
            return "RHI-READBACK-STATISTICS-BUDGET";
        return nullptr;
    }

    static std::shared_ptr<const TextureReadbackStatisticsPlan> prepareOriginal(
        std::shared_ptr<const TextureReadbackLayout> layout,
        const TextureReadbackStatisticsPlanBudget& b,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        diagnostic = validateBudget(b);
        if (diagnostic)
            return {};
        if (!quota || quota->limitBytes() != b.maxCpuMetadataBytes) {
            diagnostic = "RHI-READBACK-STATISTICS-QUOTA";
            return {};
        }
        if (!layout || !layout->hasQuotaBoundSharedOwner(layout)) {
            diagnostic = "RHI-READBACK-STATISTICS-LAYOUT-OWNER";
            return {};
        }
        if (!textureReadbackChannelCount(layout->sourceLayout().format)) {
            diagnostic = "RHI-READBACK-STATISTICS-FORMAT";
            return {};
        }
        const auto originalBytes = layout->inputOwner()->scalarFields().byteCapacity();
        if (originalBytes > SIZE_MAX || originalBytes > b.maxOriginalReadbackBytes) {
            diagnostic = "RHI-READBACK-STATISTICS-BYTE-CAPACITY";
            return {};
        }
        std::uint32_t count = 0u;
        std::uint64_t pixels = 0u;
        for (std::size_t i = 0u; i < layout->mipCopies().size(); ++i) {
            const auto& mip = layout->mipCopies()[i];
            const auto samples = std::uint64_t(mip.extent.width) * mip.extent.height;
            if (!samples || samples > UINT32_MAX) {
                diagnostic = "RHI-READBACK-STATISTICS-SAMPLE-COUNT";
                return {};
            }
            if (mip.sliceCount > b.maxViews - count) {
                diagnostic = "RHI-READBACK-STATISTICS-VIEW-CAPACITY";
                return {};
            }
            for (std::uint32_t slice = 0u; slice < mip.sliceCount; ++slice) {
                if (samples > b.maxAnalyzedPixels - pixels) {
                    diagnostic = "RHI-READBACK-STATISTICS-PIXEL-CAPACITY";
                    return {};
                }
                View view;
                if ((diagnostic = map(*layout, i, slice, originalBytes, view)))
                    return {};
                pixels += samples;
            }
            count += mip.sliceCount;
        }
        if (!count) {
            diagnostic = "RHI-READBACK-STATISTICS-VIEW-CAPACITY";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope metadata(quota);
            auto result = core::makeUnique<TextureReadbackStatisticsPlan>(
                ConstructionKey{}, layout, count, originalBytes, pixels);
            for (std::size_t i = 0u; i < layout->mipCopies().size(); ++i)
                for (std::uint32_t slice = 0u; slice < layout->mipCopies()[i].sliceCount; ++slice) {
                    View view;
                    if ((diagnostic = map(*layout, i, slice, originalBytes, view)))
                        return {};
                    result->views_.append(view);
                }
            return shareOriginal(std::move(result));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-READBACK-STATISTICS-ALLOCATION";
            return {};
        } catch (const std::length_error&) {
            diagnostic = "RHI-READBACK-STATISTICS-ALLOCATION";
            return {};
        }
    }

    const auto& originalLayout() const noexcept { return layout_; }

    const auto& views() const noexcept { return views_.values(); }

    std::uint64_t originalByteCapacity() const noexcept { return bytes_; }

    std::uint64_t analyzedPixels() const noexcept { return pixels_; }

    static const char* validateOriginal(
        const std::shared_ptr<const TextureReadbackStatisticsPlan>& self,
        const std::shared_ptr<const TextureReadbackLayout>& layout) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-READBACK-STATISTICS-PLAN-OWNER";
        if (!layout || !layout->hasQuotaBoundSharedOwner(layout)
            || !core::SharedOwnerProjection::sameOwner(self->layout_, layout))
            return "RHI-READBACK-STATISTICS-LAYOUT-OWNER";
        return nullptr;
    }

    bool nativePublicationProven() const noexcept { return false; }

private:
    static const char* map(const TextureReadbackLayout& layout,
        std::size_t index,
        std::uint32_t slice,
        std::uint64_t bytes,
        View& output) noexcept
    {
        const auto& mip = layout.mipCopies()[index];
        if (mip.rowCount != mip.extent.height
            || mip.rowBytes
                != std::uint64_t(mip.extent.width) * bytesPerPixel(layout.sourceLayout().format))
            return "RHI-READBACK-STATISTICS-ROW-LAYOUT";
        std::uint64_t offset = 0u, last = 0u;
        const core::StridedIndexRange slices(mip.bufferOffsetBytes,
            mip.sliceCount,
            std::uint64_t(mip.bytesPerRow) * mip.rowsPerImage,
            bytes);
        if (!slices.index(slice, offset)
            || !core::StridedIndexRange(offset, mip.extent.height, mip.bytesPerRow, bytes)
                .index(mip.extent.height - 1u, last)
            || !core::containsUnsignedRange<std::uint64_t>(0u, bytes, last, mip.rowBytes))
            return "RHI-READBACK-STATISTICS-VIEW-RANGE";
        const auto domain = layout.sourceLayout().extent.depth > 1u ? SliceDomain::DepthSlice
                                                                    : SliceDomain::ArrayLayer;
        output = {mip.range.baseMipLevel,
            slice,
            domain == SliceDomain::DepthSlice ? slice : mip.range.baseArrayLayer + slice,
            mip.extent.width,
            mip.extent.height,
            mip.bytesPerRow,
            offset,
            last - offset + mip.rowBytes,
            domain};
        return nullptr;
    }

    const std::shared_ptr<const TextureReadbackLayout> layout_;
    core::BoundedSequence<View> views_;
    const std::uint64_t bytes_, pixels_;
};

// endregion Transfer readback statistics plan

// region Transfer readback source lease

struct TextureReadbackSourceLeaseBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxRetainedOwners = 0u, maxMipCopies = 0u;
    std::uint64_t maxLabelBytes = 0u, maxCpuMetadataBytes = 0u;
};

// L2 object lifetime only. Current native content/access/completion are separate.
class TextureReadbackSourceLease final : public core::QuotaBoundSharedObjectIdentity {
    class ConstructionKey {
        friend class TextureReadbackSourceLease;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    static constexpr std::uint32_t retainedOwnerCount = 2u;
    static constexpr std::uint32_t retainedOriginalOwnerCount =
        1u + OriginalCommandTextureLease::retainedOwnerCount;

    static constexpr auto lowering(Backend backend, std::uint32_t version = 1u) noexcept
    {
        return semantics::TextureReadbackSourceLeaseLowering::lowering(backend, version);
    }

    TextureReadbackSourceLease(ConstructionKey,
        std::shared_ptr<const TextureReadbackRequestDeclaration> request,
        const TextureReadbackSourceLeaseBudget& budget,
        std::shared_ptr<const OriginalCommandTextureLease> originalOwner = {})
        : request_(std::move(request)), budget_(budget), originalOwner_(std::move(originalOwner))
    {}

    static const char* validateBudget(const TextureReadbackSourceLeaseBudget& b) noexcept
    {
        if (b.contractVersion != 1u && b.contractVersion != 2u)
            return "RHI-READBACK-SOURCE-LEASE-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-READBACK-SOURCE-LEASE-OPT-IN";
        if (!b.maxRetainedOwners || !b.maxMipCopies || !b.maxCpuMetadataBytes)
            return "RHI-READBACK-SOURCE-LEASE-BUDGET";
        return nullptr;
    }

    static std::shared_ptr<const TextureReadbackSourceLease> prepareOriginal(
        core::UniquePtr<GpuTexture>& source,
        std::shared_ptr<const TextureReadbackRequestDeclaration> request,
        const TextureReadbackSourceLeaseBudget& b,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        auto lease =
            prepareColdOriginal(source, std::move(request), b, std::move(quota), diagnostic);
        if (!lease || (diagnostic = adoptOriginal(lease, source)))
            return {};
        return lease;
    }

    // Retain the already-adopted original owner. No second texture adoption,
    // registry, content observation or completion permission is introduced.
    static std::shared_ptr<const TextureReadbackSourceLease> prepareRetainedOriginal(
        std::shared_ptr<const OriginalCommandTextureLease> owner,
        std::shared_ptr<const TextureReadbackRequestDeclaration> request,
        const TextureReadbackSourceLeaseBudget& b,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        diagnostic = validateBudget(b);
        if (diagnostic)
            return {};
        if (b.contractVersion != 2u) {
            diagnostic = "RHI-READBACK-SOURCE-LEASE-OWNER-PROFILE";
            return {};
        }
        if (!quota || quota->limitBytes() != b.maxCpuMetadataBytes) {
            diagnostic = "RHI-READBACK-SOURCE-LEASE-QUOTA";
            return {};
        }
        if ((diagnostic = OriginalCommandTextureLease::validateOriginal(owner)))
            return {};
        if (!core::SharedOwnerProjection::sameOwner(quota, owner->originalMetadataQuota())) {
            diagnostic = "RHI-READBACK-SOURCE-LEASE-ORIGINAL-QUOTA";
            return {};
        }
        if ((diagnostic = validateSourceOriginal(request, *owner->originalObject())))
            return {};
        if (retainedOriginalOwnerCount > b.maxRetainedOwners
            || request->originalLayout()->mipCopies().size() > b.maxMipCopies
            || request->label().size() > b.maxLabelBytes) {
            diagnostic = "RHI-READBACK-SOURCE-LEASE-CAPACITY";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope scope(quota);
            auto lease = shareOriginal(core::makeUnique<TextureReadbackSourceLease>(
                ConstructionKey{}, std::move(request), b, std::move(owner)));
            if ((diagnostic = lease->validate()))
                return {};
            return lease;
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-READBACK-SOURCE-LEASE-ALLOCATION";
            return {};
        }
    }

    // Synchronous cold receipt only. The address is compared, never dereferenced
    // without the caller presenting its still-owned object. Ordinary validators
    // and sourceTexture remain closed until the final allocation-free adoption.
    static std::shared_ptr<TextureReadbackSourceLease> prepareColdOriginal(
        const core::UniquePtr<GpuTexture>& source,
        std::shared_ptr<const TextureReadbackRequestDeclaration> request,
        const TextureReadbackSourceLeaseBudget& b,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        diagnostic = validateBudget(b);
        if (diagnostic)
            return {};
        if (b.contractVersion != 1u) {
            diagnostic = "RHI-READBACK-SOURCE-LEASE-OWNER-PROFILE";
            return {};
        }
        if (!quota || quota->limitBytes() != b.maxCpuMetadataBytes) {
            diagnostic = "RHI-READBACK-SOURCE-LEASE-QUOTA";
            return {};
        }
        if (!request || !request->hasQuotaBoundSharedOwner(request)) {
            diagnostic = "RHI-READBACK-SOURCE-LEASE-REQUEST-OWNER";
            return {};
        }
        if ((diagnostic = TextureReadbackRequestDeclaration::validateOriginal(
                 request, request->originalLayout())))
            return {};
        if (!source) {
            diagnostic = "RHI-READBACK-SOURCE-LEASE-OBJECT-MISSING";
            return {};
        }
        if ((diagnostic = validateTexture(*request, *source)))
            return {};
        if (retainedOwnerCount > b.maxRetainedOwners
            || request->originalLayout()->mipCopies().size() > b.maxMipCopies
            || request->label().size() > b.maxLabelBytes) {
            diagnostic = "RHI-READBACK-SOURCE-LEASE-CAPACITY";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope scope(quota);
            auto lease = shareOriginal(
                core::makeUnique<TextureReadbackSourceLease>(ConstructionKey{}, request, b));
            if ((diagnostic = validateTexture(*request, *source)))
                return {};
            if (!lease->source_.bindCold(source)) {
                diagnostic = "RHI-READBACK-SOURCE-LEASE-COLD-CANDIDATE";
                return {};
            }
            return lease;
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-READBACK-SOURCE-LEASE-ALLOCATION";
            return {};
        }
    }

    static const char* validateColdOriginal(
        const std::shared_ptr<const TextureReadbackSourceLease>& self,
        const GpuTexture& candidate) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-READBACK-SOURCE-LEASE-OWNER";
        if (self->contractVersion() != 1u)
            return "RHI-READBACK-SOURCE-LEASE-OWNER-PROFILE";
        if (const auto* id = self->validateMetadata())
            return id;
        if (!self->source_.matchesCold(candidate))
            return "RHI-READBACK-SOURCE-LEASE-COLD-CANDIDATE";
        return validateSourceOriginal(self->request_, candidate);
    }

    static const char* adoptOriginal(const std::shared_ptr<TextureReadbackSourceLease>& lease,
        core::UniquePtr<GpuTexture>& source) noexcept
    {
        if (!source)
            return "RHI-READBACK-SOURCE-LEASE-OBJECT-MISSING";
        if (const auto* id = validateColdOriginal(lease, *source))
            return id;
        const auto adopted =
            lease->source_.adoptOriginal(source, [&](const GpuTexture& candidate) noexcept {
                return !validateSourceOriginal(lease->request_, candidate);
            });
        return adopted ? nullptr : "RHI-READBACK-SOURCE-LEASE-COLD-CANDIDATE";
    }

    static const char* validateOriginal(
        const std::shared_ptr<const TextureReadbackSourceLease>& self,
        const std::shared_ptr<const TextureReadbackRequestDeclaration>& request) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-READBACK-SOURCE-LEASE-OWNER";
        if (!request || !request->hasQuotaBoundSharedOwner(request)
            || !core::SharedOwnerProjection::sameOwner(self->request_, request))
            return "RHI-READBACK-SOURCE-LEASE-REQUEST-OWNER";
        return self->validate();
    }

    static const char* validateSourceOriginal(
        const std::shared_ptr<const TextureReadbackRequestDeclaration>& request,
        const GpuTexture& source) noexcept
    {
        if (!request || !request->hasQuotaBoundSharedOwner(request))
            return "RHI-READBACK-SOURCE-LEASE-REQUEST-OWNER";
        if (const auto* id = TextureReadbackRequestDeclaration::validateOriginal(
                request, request->originalLayout()))
            return id;
        return validateTexture(*request, source);
    }

    const char* validate() const noexcept
    {
        if (const auto* id = validateMetadata())
            return id;
        const auto* source = sourceTexture();
        return source ? validateTexture(*request_, *source)
                      : "RHI-READBACK-SOURCE-LEASE-OBJECT-MISSING";
    }

    const GpuTexture* sourceTexture() const noexcept
    {
        return originalOwner_ ? originalOwner_->originalObject() : source_.originalObject();
    }

    std::uint32_t contractVersion() const noexcept { return budget_.contractVersion; }

    std::uint32_t retainedSourceOwnerCount() const noexcept
    {
        return contractVersion() == 2u ? retainedOriginalOwnerCount : retainedOwnerCount;
    }

    const auto& originalTextureOwner() const noexcept { return originalOwner_; }

    const auto& originalRequest() const noexcept { return request_; }

    bool nativeExecutionProven() const noexcept { return false; }

private:
    const char* validateMetadata() const noexcept
    {
        if (const auto* id = validateBudget(budget_))
            return id;
        if (!originalMetadataQuota()
            || originalMetadataQuota()->limitBytes() != budget_.maxCpuMetadataBytes)
            return "RHI-READBACK-SOURCE-LEASE-QUOTA";
        if (contractVersion() == 2u) {
            if (const auto* id = OriginalCommandTextureLease::validateOriginal(originalOwner_))
                return id;
            if (!core::SharedOwnerProjection::sameOwner(
                    originalMetadataQuota(), originalOwner_->originalMetadataQuota()))
                return "RHI-READBACK-SOURCE-LEASE-ORIGINAL-QUOTA";
        } else if (originalOwner_)
            return "RHI-READBACK-SOURCE-LEASE-OWNER-PROFILE";
        if (!request_ || !request_->hasQuotaBoundSharedOwner(request_))
            return "RHI-READBACK-SOURCE-LEASE-REQUEST-OWNER";
        if (const auto* id = TextureReadbackRequestDeclaration::validateOriginal(
                request_, request_->originalLayout()))
            return id;
        if (retainedSourceOwnerCount() > budget_.maxRetainedOwners
            || request_->originalLayout()->mipCopies().size() > budget_.maxMipCopies
            || request_->label().size() > budget_.maxLabelBytes)
            return "RHI-READBACK-SOURCE-LEASE-CAPACITY";
        return nullptr;
    }

    static const char* validateTexture(
        const TextureReadbackRequestDeclaration& request, const GpuTexture& source) noexcept
    {
        return source.id() == request.source()
                && request.originalLayout()->sourceLayout().matches(source.desc())
            ? nullptr
            : "RHI-READBACK-SOURCE-LEASE-OBJECT-MISMATCH";
    }

    const std::shared_ptr<const TextureReadbackRequestDeclaration> request_;
    core::ColdOwnedObjectSlot<GpuTexture> source_;
    const TextureReadbackSourceLeaseBudget budget_;
    const std::shared_ptr<const OriginalCommandTextureLease> originalOwner_;
};
} // namespace arp::rhi

// endregion Transfer readback source lease

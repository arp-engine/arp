#pragma once

#include "core/atomic_capacity.h"
#include "core/bounded_sequence.h"
#include "core/row_major_index_grid.h"
#include "core/shared_identity_projection.h"
#include "core/unsigned_range.h"
#include "rhi/frontend/bindless_compute_binding.h"
#include "rhi/frontend/bindless_graphics_binding.h"
#include "rhi/frontend/bindless_ordinary_opaque_binding.h"
#include "rhi/frontend/bindless_produced_attachments.h"
#include "rhi/frontend/bindless_workload_capacity.h"
#include "rhi/frontend/command_arguments.h"
#include "rhi/frontend/completed_depth_read_input.h"
#include "rhi/frontend/descriptor_set_validation.h"
#include "rhi/frontend/descriptor_state_copy.h"
#include "rhi/frontend/pipeline_definition_identity.h"
#include "rhi/frontend/pipeline_graphics_input.h"
#include "rhi/frontend/pipeline_single_entry_input.h"
#include "rhi/frontend/resource_leases.h"
#include "rhi/frontend/transfer_buffer_readback.h"
#include "rhi/frontend/transfer_readback_input.h"
#include "rhi/frontend/transfer_upload_input.h"
#include "rhi/frontend/transfer_upload_layout_validation.h"
#include "rhi/frontend/transfer_upload_packing.h"
#include "rhi/gpu_descriptors.h"
#include "rhi/gpu_device.h"
#include "rhi/semantics/bindless_original_preparation_lowering.h"
#include "rhi/semantics/command_lowering.h"
#include "rhi/semantics/geometry_content_lowering.h"

#include <variant>

// region Command upload sources
// region original_command_buffer_upload_source
namespace arp::rhi {
namespace common {
class OriginalCommandBindlessRootAccesses;
}

// Complete empty type: the texture specialization grants no Vulkan writer;
// callers cannot define/reopen a forward-declared authority class.
class OriginalReadbackNoNativeWriter final {};

struct OriginalCommandBufferUploadSourceBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxRetainedOwners = 0u;
    std::uint64_t maxLabelBytes = 0u, maxCpuSourceBytes = 0u, maxCpuGraphBytes = 0u;
};

// Original declared CPU ownership only. UploadBufferDesc has no byte input;
// retaining its source wrapper cannot prove initialized native staging contents.
class OriginalCommandBufferUploadSource final : public core::QuotaBoundSharedObjectIdentity {
    class ConstructionKey {
        friend class OriginalCommandBufferUploadSource;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    static constexpr auto domain = CommandReferenceDomain::Upload;
    static constexpr std::uint32_t retainedOwnerCount = 2u; // Binder and original label store.
    static constexpr std::uint32_t completeRetainedOwnerCount =
        retainedOwnerCount + 2u * OriginalCommandBufferLease::retainedOwnerCount;

    OriginalCommandBufferUploadSource(ConstructionKey,
        const UploadBufferDesc& desc,
        std::shared_ptr<const OriginalCommandBufferLease> source,
        std::shared_ptr<const OriginalCommandBufferLease> destination,
        const OriginalCommandBufferUploadSourceBudget& budget)
        : arguments_(BufferUploadArguments::captureScalarFields(desc)), source_(std::move(source)),
          destination_(std::move(destination)), budget_(budget)
    {}

    static const char* validateBudget(const OriginalCommandBufferUploadSourceBudget& b) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-ORIGINAL-BUFFER-UPLOAD-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-ORIGINAL-BUFFER-UPLOAD-OPT-IN";
        if (b.maxRetainedOwners < completeRetainedOwnerCount)
            return "RHI-ORIGINAL-BUFFER-UPLOAD-OWNER-CAPACITY";
        if (!b.maxCpuSourceBytes || !b.maxCpuGraphBytes || b.maxLabelBytes > SIZE_MAX)
            return "RHI-ORIGINAL-BUFFER-UPLOAD-BUDGET";
        return nullptr;
    }

    static std::shared_ptr<const OriginalCommandBufferUploadSource> prepareOriginal(
        const UploadBufferDesc& desc,
        std::shared_ptr<const OriginalCommandBufferLease> source,
        std::shared_ptr<const OriginalCommandBufferLease> destination,
        const OriginalCommandBufferUploadSourceBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> sourceQuota,
        std::shared_ptr<core::CpuMemoryQuota> graphQuota,
        const char*& diagnostic)
    {
        diagnostic = validateBudget(budget);
        if (diagnostic)
            return {};
        if (!sourceQuota || sourceQuota->limitBytes() != budget.maxCpuSourceBytes || !graphQuota
            || graphQuota->limitBytes() != budget.maxCpuGraphBytes
            || core::SharedOwnerProjection::sameControlBlock(sourceQuota, graphQuota)) {
            diagnostic = "RHI-ORIGINAL-BUFFER-UPLOAD-QUOTA";
            return {};
        }
        if ((diagnostic = validateSources(BufferUploadArguments::captureScalarFields(desc),
                 source,
                 destination,
                 sourceQuota)))
            return {};
        try {
            core::CpuMemoryQuotaScope scope(sourceQuota);
            auto result = core::makeUnique<OriginalCommandBufferUploadSource>(
                ConstructionKey{}, desc, std::move(source), std::move(destination), budget);
            const CommandVariableArguments::Input label{
                *CommandScalarArguments::capture(
                    CommandUsageKind::BufferUpload, CommandScalarArguments::None{}),
                desc.label};
            result->label_ = CommandVariableArguments::prepareOriginal(&label,
                1u,
                {1u, true, 1u, 0u, budget.maxLabelBytes, 0u, budget.maxCpuGraphBytes},
                graphQuota,
                diagnostic);
            return result->label_ ? shareOriginal(std::move(result))
                                  : std::shared_ptr<const OriginalCommandBufferUploadSource>{};
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-ORIGINAL-BUFFER-UPLOAD-ALLOCATION";
            return {};
        }
    }

    static const char* validateOriginal(
        const std::shared_ptr<const OriginalCommandBufferUploadSource>& self) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-ORIGINAL-BUFFER-UPLOAD-OWNER";
        return self->validate();
    }

    const char* validate() const noexcept
    {
        if (const auto* id = validateBudget(budget_))
            return id;
        if (!originalMetadataQuota()
            || originalMetadataQuota()->limitBytes() != budget_.maxCpuSourceBytes || !label_
            || !label_->hasQuotaBoundSharedOwner(label_) || !originalInputQuota()
            || originalInputQuota()->limitBytes() != budget_.maxCpuGraphBytes
            || core::SharedOwnerProjection::sameControlBlock(
                originalMetadataQuota(), originalInputQuota()))
            return "RHI-ORIGINAL-BUFFER-UPLOAD-QUOTA";
        if (label_->records().size() != 1u
            || !label_->matchesUsageKind(0u, CommandUsageKind::BufferUpload))
            return "RHI-ORIGINAL-BUFFER-UPLOAD-LABEL";
        if (const auto* id = label_->validateCapacity(
                {1u, true, 1u, 0u, budget_.maxLabelBytes, 0u, budget_.maxCpuGraphBytes}))
            return id;
        return validateSources(arguments_, source_, destination_, originalMetadataQuota());
    }

    const OriginalCommandBufferUploadSource* originalObject() const noexcept { return this; }

    GpuResourceId resource() const noexcept { return {}; }

    const auto& originalSourceLease() const noexcept { return source_; }

    const auto& originalDestinationLease() const noexcept { return destination_; }

    const std::shared_ptr<core::CpuMemoryQuota>& originalInputQuota() const noexcept
    {
        return label_->originalMetadataQuota();
    }

    const auto& arguments() const noexcept { return arguments_; }

    std::string_view label() const noexcept { return label_->text(0u); }

    std::uint64_t sizeBytes() const noexcept
    {
        BufferCopyDesc copy;
        arguments_.copyArguments().write(copy);
        return copy.sizeBytes;
    }

    bool matches(const UploadBufferDesc& desc) const noexcept
    {
        return !validate() && arguments_.matchesScalarFields(desc) && label() == desc.label;
    }

    bool nativeExecutionProven() const noexcept { return false; }

private:
    static const char* validateSources(const BufferUploadArguments& arguments,
        const std::shared_ptr<const OriginalCommandBufferLease>& source,
        const std::shared_ptr<const OriginalCommandBufferLease>& destination,
        const std::shared_ptr<core::CpuMemoryQuota>& quota) noexcept
    {
        if (const auto* id = OriginalCommandBufferLease::validateOriginal(source))
            return id;
        if (const auto* id = OriginalCommandBufferLease::validateOriginal(destination))
            return id;
        if (!core::SharedOwnerProjection::sameOwner(source->originalMetadataQuota(), quota)
            || !core::SharedOwnerProjection::sameOwner(destination->originalMetadataQuota(), quota))
            return "RHI-ORIGINAL-BUFFER-UPLOAD-SOURCE-QUOTA";
        if (source->resource() != arguments.source()
            || destination->resource() != arguments.destination())
            return "RHI-ORIGINAL-BUFFER-UPLOAD-RESOURCE";
        if (!arguments.backendOwnedStaging() || arguments.stagingIntent() != StagingIntent::Upload)
            return "RHI-ORIGINAL-BUFFER-UPLOAD-INTENT";
        BufferCopyDesc copy;
        arguments.copyArguments().write(copy);
        if (!copy.sizeBytes || source->resource() == destination->resource()
            || (source->layout().usage & ResourceUsage::CopySrc) == ResourceUsage::None
            || (destination->layout().usage & ResourceUsage::CopyDst) == ResourceUsage::None
            || !core::containsUnsignedRange<std::uint64_t>(
                0u, source->layout().sizeBytes, 0u, copy.sizeBytes)
            || !core::containsUnsignedRange<std::uint64_t>(
                0u, destination->layout().sizeBytes, 0u, copy.sizeBytes))
            return "RHI-ORIGINAL-BUFFER-UPLOAD-RANGE";
        return nullptr;
    }

    const BufferUploadArguments arguments_;
    const std::shared_ptr<const OriginalCommandBufferLease> source_, destination_;
    std::shared_ptr<const CommandVariableArguments> label_;
    const OriginalCommandBufferUploadSourceBudget budget_;
};

// endregion original_command_buffer_upload_source

// region original_command_texture_upload_source

// Exact canonical CPU input/destination ownership binding. It freezes no native
// generation, staging write or recording authority, regardless of scalar flags.
class OriginalCommandTextureUploadSource final : public core::QuotaBoundSharedObjectIdentity {
    class ConstructionKey {
        friend class OriginalCommandTextureUploadSource;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    static constexpr auto domain = CommandReferenceDomain::Upload;
    static constexpr std::uint32_t retainedOwnerCount = 3u; // Binder, snapshot and its label store.
    static constexpr std::uint32_t completeRetainedOwnerCount =
        retainedOwnerCount + OriginalCommandTextureLease::retainedOwnerCount;

    OriginalCommandTextureUploadSource(ConstructionKey,
        std::shared_ptr<const TextureUploadInputSnapshot> input,
        std::shared_ptr<const OriginalCommandTextureLease> destination)
        : input_(std::move(input)), destination_(std::move(destination))
    {}

    static std::shared_ptr<const OriginalCommandTextureUploadSource> prepareOriginal(
        std::shared_ptr<const TextureUploadInputSnapshot> input,
        std::shared_ptr<const OriginalCommandTextureLease> destination,
        const OriginalCommandResourceLeaseBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        diagnostic = OriginalCommandTextureLease::validateBudget(budget);
        if (diagnostic)
            return {};
        if (budget.maxRetainedOwners < completeRetainedOwnerCount) {
            diagnostic = "RHI-ORIGINAL-TEXTURE-UPLOAD-OWNER-CAPACITY";
            return {};
        }
        if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes) {
            diagnostic = "RHI-ORIGINAL-TEXTURE-UPLOAD-SOURCE-QUOTA";
            return {};
        }
        if ((diagnostic = validateSources(input, destination, quota)))
            return {};
        try {
            core::CpuMemoryQuotaScope scope(quota);
            return shareOriginal(core::makeUnique<OriginalCommandTextureUploadSource>(
                ConstructionKey{}, std::move(input), std::move(destination)));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-ORIGINAL-TEXTURE-UPLOAD-ALLOCATION";
            return {};
        }
    }

    static const char* validateOriginal(
        const std::shared_ptr<const OriginalCommandTextureUploadSource>& self) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-ORIGINAL-TEXTURE-UPLOAD-OWNER";
        return self->validate();
    }

    const char* validate() const noexcept
    {
        return validateSources(input_, destination_, originalMetadataQuota());
    }

    const TextureUploadInputSnapshot* originalObject() const noexcept { return input_.get(); }

    GpuResourceId resource() const noexcept { return {}; }

    const auto& originalInput() const noexcept { return input_; }

    const auto& originalInputQuota() const noexcept { return input_->originalMetadataQuota(); }

    const auto& originalDestinationLease() const noexcept { return destination_; }

    bool nativeExecutionProven() const noexcept { return false; }

private:
    static const char* validateSources(
        const std::shared_ptr<const TextureUploadInputSnapshot>& input,
        const std::shared_ptr<const OriginalCommandTextureLease>& destination,
        const std::shared_ptr<core::CpuMemoryQuota>& quota) noexcept
    {
        if (const auto* id = TextureUploadInputSnapshot::validateOriginal(input))
            return id;
        if (const auto* id = OriginalCommandTextureLease::validateOriginal(destination))
            return id;
        if (!quota
            || !core::SharedOwnerProjection::sameOwner(quota, destination->originalMetadataQuota())
            || !core::SharedOwnerProjection::sameOwner(quota, input->originalPixelQuota()))
            return "RHI-ORIGINAL-TEXTURE-UPLOAD-SOURCE-QUOTA";
        if (!core::SharedOwnerProjection::sameOwner(
                input->originalMetadataQuota(), input->labelOwner()->originalMetadataQuota())
            || core::SharedOwnerProjection::sameControlBlock(input->originalMetadataQuota(), quota))
            return "RHI-ORIGINAL-TEXTURE-UPLOAD-GRAPH-QUOTA";
        if (input->scalarFields().destination() != destination->resource())
            return "RHI-ORIGINAL-TEXTURE-UPLOAD-DESTINATION";
        const auto& layout = destination->layout();
        std::uint64_t bytes = 0u;
        return semantics::TextureUploadLayoutValidation::inspectPackedBytes(*input, layout, bytes);
    }

    const std::shared_ptr<const TextureUploadInputSnapshot> input_;
    const std::shared_ptr<const OriginalCommandTextureLease> destination_;
};

// endregion original_command_texture_upload_source

// region original_command_texture_upload_packing_source

// CanonicalTextureUploadPackingSource/1 retains the exact author, snapshot,
// destination lease and normative packed bytes. It issues no GPU content fact.
class OriginalCommandTextureUploadPackingSource final
    : public core::QuotaBoundSharedObjectIdentity {
    class ConstructionKey {
        friend class OriginalCommandTextureUploadPackingSource;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    // Wrapper + original binder/snapshot/label/destination + packing/mips/bytes.
    static constexpr std::uint32_t retainedOwnerCount =
        1u + OriginalCommandTextureUploadSource::completeRetainedOwnerCount + 3u;

    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint32_t maxMipMappings = 0u, maxRetainedOwners = 0u;
        std::uint64_t maxPixelBytes = 0u, maxPackedBytes = 0u, maxCpuMetadataBytes = 0u;
    };

    static constexpr const char* validateBudget(const Budget& b) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-CANONICAL-TEXTURE-PACKING-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-CANONICAL-TEXTURE-PACKING-OPT-IN";
        if (!b.maxMipMappings || b.maxRetainedOwners < retainedOwnerCount || !b.maxPixelBytes
            || !b.maxPackedBytes || !b.maxCpuMetadataBytes)
            return "RHI-CANONICAL-TEXTURE-PACKING-BUDGET";
        return nullptr;
    }

    OriginalCommandTextureUploadPackingSource(ConstructionKey,
        std::shared_ptr<const OriginalCommandTextureUploadSource> source,
        std::shared_ptr<const TextureUploadPacking> packing,
        Budget budget)
        : source_(std::move(source)), packing_(std::move(packing)), budget_(budget)
    {}

    static std::shared_ptr<const OriginalCommandTextureUploadPackingSource> prepareOriginal(
        std::shared_ptr<const OriginalCommandTextureUploadSource> source,
        std::shared_ptr<const TextureUploadPacking> packing,
        const Budget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        diagnostic = inspect(source, packing, budget, quota);
        if (diagnostic)
            return {};
        try {
            core::CpuMemoryQuotaScope scope(quota);
            return shareOriginal(core::makeUnique<OriginalCommandTextureUploadPackingSource>(
                ConstructionKey{}, std::move(source), std::move(packing), budget));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-CANONICAL-TEXTURE-PACKING-ALLOCATION";
            return {};
        }
    }

    static const char* validateOriginal(
        const std::shared_ptr<const OriginalCommandTextureUploadPackingSource>& self) noexcept
    {
        return !self || !self->hasQuotaBoundSharedOwner(self)
            ? "RHI-CANONICAL-TEXTURE-PACKING-OWNER"
            : self->validate();
    }

    const char* validate() const noexcept
    {
        return inspect(source_, packing_, budget_, originalMetadataQuota());
    }

    const auto& originalSource() const noexcept { return source_; }

    const auto& packing() const noexcept { return packing_; }

    const auto& originalInput() const noexcept { return source_->originalInput(); }

    const auto& originalDestinationLease() const noexcept
    {
        return source_->originalDestinationLease();
    }

    bool overlapsCpuQuota(const core::CpuMemoryQuota* quota) const noexcept
    {
        return quota
            && (quota == originalMetadataQuota().get()
                || quota == source_->originalMetadataQuota().get()
                || source_->originalInput()->overlapsCpuQuota(quota)
                || quota == packing_->originalMetadataQuota().get()
                || quota == source_->originalDestinationLease()->originalMetadataQuota().get()
                || quota
                    == core::inspectCpuAllocationCharge(
                        source_->originalDestinationLease()->originalObject())
                        .quota
                || quota == core::inspectCpuAllocationCharge(packing_->packedBytes().data()).quota);
    }

    const char* executionDiagnostic(Backend backend) const noexcept
    {
        if (const auto* id = validate())
            return id;
        const auto decision =
            arp::rhi::semantics::OriginalCommandTextureUploadPackingSourceLowering::lowering(
                backend);
        return decision.privateStagingJoinAvailable
            ? "RHI-CANONICAL-TEXTURE-STAGING-INITIALIZATION-UNPROVEN"
            : decision.diagnostic;
    }

    bool nativeExecutionProven() const noexcept { return false; }

private:
    static const char* inspect(
        const std::shared_ptr<const OriginalCommandTextureUploadSource>& source,
        const std::shared_ptr<const TextureUploadPacking>& packing,
        const Budget& b,
        const std::shared_ptr<core::CpuMemoryQuota>& quota) noexcept
    {
        if (const auto* id = validateBudget(b))
            return id;
        if (!quota || quota->limitBytes() != b.maxCpuMetadataBytes)
            return "RHI-CANONICAL-TEXTURE-PACKING-QUOTA";
        if (const auto* id = OriginalCommandTextureUploadSource::validateOriginal(source))
            return id;
        if (!packing || !packing->hasQuotaBoundSharedOwner(packing))
            return "RHI-CANONICAL-TEXTURE-PACKING-ORIGINAL";
        if (!core::SharedOwnerProjection::sameOwner(source->originalInput(), packing->inputOwner()))
            return "RHI-CANONICAL-TEXTURE-PACKING-INPUT-JOIN";
        if (!packing->destinationLayout().matches(
                source->originalDestinationLease()->originalObject()->desc()))
            return "RHI-CANONICAL-TEXTURE-PACKING-DESTINATION-JOIN";
        const std::array domains{quota,
            source->originalMetadataQuota(),
            source->originalInput()->originalMetadataQuota(),
            packing->originalMetadataQuota()};
        const auto packedCharge = core::inspectCpuAllocationCharge(packing->packedBytes().data());
        if (!core::SharedOwnerProjection::hasIndependentOwners(domains.size(),
                [&](std::size_t i) noexcept {
                    return domains[i];
                })
            || !packedCharge.found || !packedCharge.quota)
            return "RHI-CANONICAL-TEXTURE-PACKING-QUOTA-DOMAIN";
        for (const auto& domain : domains)
            if (packedCharge.quota == domain.get())
                return "RHI-CANONICAL-TEXTURE-PACKING-QUOTA-DOMAIN";
        std::uint64_t packedBytes = 0u;
        if (const auto* id = semantics::TextureUploadLayoutValidation::inspectPackedBytes(
                *source->originalInput(),
                source->originalDestinationLease()->layout(),
                packedBytes))
            return id;
        if (packing->packedBytes().size() != packedBytes
            || packing->mipLayouts().size()
                != semantics::TextureUploadLayoutValidation::mipCount(*source->originalInput()))
            return "RHI-CANONICAL-TEXTURE-PACKING-FOOTPRINT";
        if (packedBytes > b.maxPackedBytes || packing->mipLayouts().size() > b.maxMipMappings
            || source->originalInput()->pixelBytes().size() > b.maxPixelBytes)
            return "RHI-CANONICAL-TEXTURE-PACKING-CAPACITY";
        return nullptr;
    }

    const std::shared_ptr<const OriginalCommandTextureUploadSource> source_;
    const std::shared_ptr<const TextureUploadPacking> packing_;
    const Budget budget_;
};

// endregion original_command_texture_upload_packing_source
// endregion Command upload sources

// region Command readback sources
// region original_command_readback_source_budget

struct OriginalCommandReadbackSourceBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxRetainedOwners = 0u, maxMipCopies = 0u;
    std::uint64_t maxLabelBytes = 0u, maxDeclaredResultBytes = 0u, maxCpuSourceBytes = 0u,
                  maxCpuGraphBytes = 0u;

    const char* validate(std::uint32_t requiredOwners) const noexcept
    {
        if (contractVersion != 1u && contractVersion != 2u)
            return "RHI-ORIGINAL-READBACK-SOURCE-VERSION";
        if (!prototypeOptIn)
            return "RHI-ORIGINAL-READBACK-SOURCE-OPT-IN";
        if (maxRetainedOwners < requiredOwners)
            return "RHI-ORIGINAL-READBACK-SOURCE-OWNER-CAPACITY";
        if (!maxMipCopies || !maxDeclaredResultBytes || !maxCpuSourceBytes || !maxCpuGraphBytes
            || maxLabelBytes > SIZE_MAX)
            return "RHI-ORIGINAL-READBACK-SOURCE-BUDGET";
        return nullptr;
    }
};

// endregion original_command_readback_source_budget

// region original_command_readback_texture_source

// Retain the already-adopted original lease. Never introduce a second owned
// texture object or reconstruct Request/layout/input from equal caller fields.
class OriginalCommandReadbackTextureSource final : public core::QuotaBoundSharedObjectIdentity {
    class ConstructionKey {
        friend class OriginalCommandReadbackTextureSource;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    static constexpr auto domain = CommandReferenceDomain::Texture;
    static constexpr std::uint32_t retainedOwnerCount = 1u
        + TextureReadbackSourceLease::retainedOwnerCount
        + TextureReadbackRequestDeclaration::retainedOwnerCount
        + TextureReadbackLayout::retainedOwnerCount
        + TextureReadbackInputSnapshot::retainedOwnerCount;

    OriginalCommandReadbackTextureSource(ConstructionKey,
        std::shared_ptr<const TextureReadbackSourceLease> lease,
        const OriginalCommandReadbackSourceBudget& budget)
        : lease_(std::move(lease)), budget_(budget)
    {}

    static std::shared_ptr<const OriginalCommandReadbackTextureSource> prepareOriginal(
        std::shared_ptr<const TextureReadbackSourceLease> lease,
        const OriginalCommandReadbackSourceBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> sourceQuota,
        std::shared_ptr<core::CpuMemoryQuota> graphQuota,
        const char*& diagnostic)
    {
        diagnostic = budget.validate(requiredOwnerCount(lease));
        if (diagnostic)
            return {};
        if ((diagnostic = validateSources(lease, budget, sourceQuota, graphQuota)))
            return {};
        try {
            core::CpuMemoryQuotaScope scope(sourceQuota);
            return shareOriginal(core::makeUnique<OriginalCommandReadbackTextureSource>(
                ConstructionKey{}, std::move(lease), budget));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-ORIGINAL-READBACK-TEXTURE-ALLOCATION";
            return {};
        }
    }

    static const char* validateOriginal(
        const std::shared_ptr<const OriginalCommandReadbackTextureSource>& self) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-ORIGINAL-READBACK-TEXTURE-OWNER";
        return self->validate();
    }

    const char* validate() const noexcept
    {
        return validateSources(lease_, budget_, originalMetadataQuota(), originalInputQuota());
    }

    const GpuTexture* originalObject() const noexcept { return lease_->sourceTexture(); }

    GpuResourceId resource() const noexcept { return lease_->originalRequest()->source(); }

    const TextureResourceLayout& layout() const noexcept
    {
        return lease_->originalRequest()->originalLayout()->sourceLayout();
    }

    const auto& originalLease() const noexcept { return lease_; }

    std::uint32_t contractVersion() const noexcept { return budget_.contractVersion; }

    std::uint32_t completeOwnerCount() const noexcept { return requiredOwnerCount(lease_); }

    const auto& originalRequest() const noexcept { return lease_->originalRequest(); }

    const std::shared_ptr<core::CpuMemoryQuota>& originalInputQuota() const noexcept
    {
        return lease_->originalRequest()->originalMetadataQuota();
    }

    bool nativeExecutionProven() const noexcept { return false; }

private:
    static std::uint32_t requiredOwnerCount(
        const std::shared_ptr<const TextureReadbackSourceLease>& lease) noexcept
    {
        return retainedOwnerCount
            + (lease && lease->contractVersion() == 2u
                    ? TextureReadbackSourceLease::retainedOriginalOwnerCount
                        - TextureReadbackSourceLease::retainedOwnerCount
                    : 0u);
    }

    static const char* validateSources(
        const std::shared_ptr<const TextureReadbackSourceLease>& lease,
        const OriginalCommandReadbackSourceBudget& budget,
        const std::shared_ptr<core::CpuMemoryQuota>& sourceQuota,
        const std::shared_ptr<core::CpuMemoryQuota>& graphQuota) noexcept
    {
        if (const auto* id = budget.validate(requiredOwnerCount(lease)))
            return id;
        if (!lease || !lease->hasQuotaBoundSharedOwner(lease))
            return "RHI-ORIGINAL-READBACK-TEXTURE-LEASE-OWNER";
        if (budget.contractVersion != lease->contractVersion())
            return "RHI-ORIGINAL-READBACK-TEXTURE-LEASE-PROFILE";
        const auto& request = lease->originalRequest();
        if (const auto* id = TextureReadbackSourceLease::validateOriginal(lease, request))
            return id;
        if (!sourceQuota || sourceQuota->limitBytes() != budget.maxCpuSourceBytes
            || !core::SharedOwnerProjection::sameOwner(sourceQuota, lease->originalMetadataQuota()))
            return "RHI-ORIGINAL-READBACK-TEXTURE-SOURCE-QUOTA";
        if (!graphQuota || graphQuota->limitBytes() != budget.maxCpuGraphBytes
            || core::SharedOwnerProjection::sameControlBlock(sourceQuota, graphQuota))
            return "RHI-ORIGINAL-READBACK-TEXTURE-GRAPH-QUOTA";
        const auto& layout = request->originalLayout();
        const auto& input = layout->inputOwner();
        if (!core::SharedOwnerProjection::sameOwner(graphQuota, request->originalMetadataQuota())
            || !core::SharedOwnerProjection::sameOwner(graphQuota, layout->originalMetadataQuota())
            || !core::SharedOwnerProjection::sameOwner(graphQuota, input->originalMetadataQuota())
            || !core::SharedOwnerProjection::sameOwner(
                graphQuota, input->labelOwner()->originalMetadataQuota()))
            return "RHI-ORIGINAL-READBACK-TEXTURE-GRAPH-QUOTA";
        if (layout->mipCopies().size() > budget.maxMipCopies
            || request->label().size() > budget.maxLabelBytes
            || input->scalarFields().byteCapacity() > budget.maxDeclaredResultBytes)
            return "RHI-ORIGINAL-READBACK-TEXTURE-CAPACITY";
        return nullptr;
    }

    const std::shared_ptr<const TextureReadbackSourceLease> lease_;
    const OriginalCommandReadbackSourceBudget budget_;
};

// endregion original_command_readback_texture_source

// region original_command_texture_readback_source

// Exact named source/Request/destination CPU ownership. It carries no staging,
// completion, mapping or publication capability and allocates no result payload.
class OriginalCommandTextureReadbackSource final : public core::QuotaBoundSharedObjectIdentity {
    class ConstructionKey {
        friend class OriginalCommandTextureReadbackSource;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    static constexpr auto domain = CommandReferenceDomain::Readback;
    static constexpr std::uint32_t retainedOwnerCount = 1u;
    static constexpr std::uint32_t completeRetainedOwnerCount = retainedOwnerCount
        + OriginalCommandReadbackTextureSource::retainedOwnerCount
        + OriginalCommandBufferLease::retainedOwnerCount;

    OriginalCommandTextureReadbackSource(ConstructionKey,
        std::shared_ptr<const OriginalCommandReadbackTextureSource> source,
        std::shared_ptr<const OriginalCommandBufferLease> destination,
        const OriginalCommandReadbackSourceBudget& budget)
        : source_(std::move(source)), destination_(std::move(destination)), budget_(budget)
    {}

    static std::shared_ptr<const OriginalCommandTextureReadbackSource> prepareOriginal(
        std::shared_ptr<const OriginalCommandReadbackTextureSource> source,
        std::shared_ptr<const OriginalCommandBufferLease> destination,
        const OriginalCommandReadbackSourceBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> sourceQuota,
        const char*& diagnostic)
    {
        diagnostic = validateSources(source, destination, budget, sourceQuota);
        if (diagnostic)
            return {};
        try {
            core::CpuMemoryQuotaScope scope(sourceQuota);
            return shareOriginal(core::makeUnique<OriginalCommandTextureReadbackSource>(
                ConstructionKey{}, std::move(source), std::move(destination), budget));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-ORIGINAL-TEXTURE-READBACK-ALLOCATION";
            return {};
        }
    }

    static const char* validateOriginal(
        const std::shared_ptr<const OriginalCommandTextureReadbackSource>& self) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-ORIGINAL-TEXTURE-READBACK-OWNER";
        return self->validate();
    }

    const char* validate() const noexcept
    {
        return validateSources(source_, destination_, budget_, originalMetadataQuota());
    }

    const TextureReadbackRequestDeclaration* originalObject() const noexcept
    {
        return source_->originalRequest().get();
    }

    GpuResourceId resource() const noexcept { return {}; }

    const auto& originalSource() const noexcept { return source_; }

    std::uint32_t contractVersion() const noexcept { return budget_.contractVersion; }

    const auto& originalDestinationLease() const noexcept { return destination_; }

    const auto& originalInput() const noexcept
    {
        return source_->originalRequest()->originalLayout()->inputOwner();
    }

    const std::shared_ptr<core::CpuMemoryQuota>& originalInputQuota() const noexcept
    {
        return source_->originalInputQuota();
    }

    bool nativeExecutionProven() const noexcept { return false; }

private:
    static const char* validateSources(
        const std::shared_ptr<const OriginalCommandReadbackTextureSource>& source,
        const std::shared_ptr<const OriginalCommandBufferLease>& destination,
        const OriginalCommandReadbackSourceBudget& budget,
        const std::shared_ptr<core::CpuMemoryQuota>& sourceQuota) noexcept
    {
        const auto requiredOwners = retainedOwnerCount
            + OriginalCommandBufferLease::retainedOwnerCount
            + (source ? source->completeOwnerCount()
                      : OriginalCommandReadbackTextureSource::retainedOwnerCount);
        if (const auto* id = budget.validate(requiredOwners))
            return id;
        if (const auto* id = OriginalCommandReadbackTextureSource::validateOriginal(source))
            return id;
        if (budget.contractVersion != source->contractVersion())
            return "RHI-ORIGINAL-TEXTURE-READBACK-SOURCE-PROFILE";
        if (const auto* id = OriginalCommandBufferLease::validateOriginal(destination))
            return id;
        if (!sourceQuota || sourceQuota->limitBytes() != budget.maxCpuSourceBytes
            || !core::SharedOwnerProjection::sameOwner(sourceQuota, source->originalMetadataQuota())
            || !core::SharedOwnerProjection::sameOwner(
                sourceQuota, destination->originalMetadataQuota()))
            return "RHI-ORIGINAL-TEXTURE-READBACK-SOURCE-QUOTA";
        if (source->originalInputQuota()->limitBytes() != budget.maxCpuGraphBytes)
            return "RHI-ORIGINAL-TEXTURE-READBACK-GRAPH-QUOTA";
        const auto& layout = source->originalRequest()->originalLayout();
        const auto& input = *layout->inputOwner();
        if (layout->mipCopies().size() > budget.maxMipCopies
            || input.label().size() > budget.maxLabelBytes
            || input.scalarFields().byteCapacity() > budget.maxDeclaredResultBytes)
            return "RHI-ORIGINAL-TEXTURE-READBACK-CAPACITY";
        ReadbackTextureDesc desc;
        input.scalarFields().writeScalarFields(desc);
        if (desc.destinationReadbackBuffer != destination->resource())
            return "RHI-ORIGINAL-TEXTURE-READBACK-DESTINATION";
        if ((destination->layout().usage & ResourceUsage::CopyDst) == ResourceUsage::None
            || !core::containsUnsignedRange<std::uint64_t>(
                0u, destination->layout().sizeBytes, 0u, desc.sizeBytes))
            return "RHI-ORIGINAL-TEXTURE-READBACK-DESTINATION-RANGE";
        return nullptr;
    }

    const std::shared_ptr<const OriginalCommandReadbackTextureSource> source_;
    const std::shared_ptr<const OriginalCommandBufferLease> destination_;
    const OriginalCommandReadbackSourceBudget budget_;
};
} // namespace arp::rhi

// endregion original_command_texture_readback_source

// region original_command_readback_result_snapshot

namespace arp::rhi_webgpu {
class WebGpuCanonicalReadbackResultStorage;
}

namespace arp::rhi_vulkan {
class VulkanOriginalBindlessCommand;
}

namespace arp::rhi_metal {
class MetalOriginalBindlessCommand;
}

namespace arp::rhi {
// Resource-specific immutable request facts, sharing the same result-storage
// quota/ownership/publication implementation. No GPU objects are enumerated.
class OriginalCommandReadbackRequestFacts final {
public:
    static std::uint64_t byteCapacity(const TextureReadbackRequestDeclaration& request) noexcept
    {
        return request.originalLayout()->inputOwner()->scalarFields().byteCapacity();
    }

    static std::uint64_t byteCapacity(const BufferReadbackRequestDeclaration& request) noexcept
    {
        return request.byteCapacity();
    }

    static auto cpuObjects(const TextureReadbackRequestDeclaration& request) noexcept
    {
        const auto& layout = request.originalLayout();
        const auto& input = layout->inputOwner();
        return std::array<const void*, 4>{
            &request, layout.get(), input.get(), input->labelOwner().get()};
    }

    static auto cpuObjects(const BufferReadbackRequestDeclaration& request) noexcept
    {
        return std::array<const void*, 1>{&request};
    }
};

// CPU storage contains only the immutable original Request/layout and its
// independent Result quota. It never retains a source/destination GPU object.
// Cold allocation is deliberately unpublished; no public writer exists.
template <class Request>
class OriginalCommandReadbackResultSnapshotT final : public core::QuotaBoundSharedObjectIdentity {
    static_assert(std::is_same_v<Request, TextureReadbackRequestDeclaration>
        || std::is_same_v<Request, BufferReadbackRequestDeclaration>);
    friend class rhi_webgpu::WebGpuCanonicalReadbackResultStorage;
    using VulkanBufferWriter =
        std::conditional_t<std::is_same_v<Request, BufferReadbackRequestDeclaration>,
            arp::rhi_vulkan::VulkanOriginalBindlessCommand,
            OriginalReadbackNoNativeWriter>;
    friend VulkanBufferWriter;
    using MetalBufferWriter =
        std::conditional_t<std::is_same_v<Request, BufferReadbackRequestDeclaration>,
            arp::rhi_metal::MetalOriginalBindlessCommand,
            OriginalReadbackNoNativeWriter>;
    friend MetalBufferWriter;

    class ConstructionKey {
        friend class OriginalCommandReadbackResultSnapshotT;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    OriginalCommandReadbackResultSnapshotT(ConstructionKey, std::shared_ptr<const Request> request)
        : request_(std::move(request))
    {}

    static std::shared_ptr<const OriginalCommandReadbackResultSnapshotT> prepareOriginalCold(
        std::shared_ptr<const Request> request,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        if (!request || !request->hasQuotaBoundSharedOwner(request)) {
            diagnostic = "RHI-ORIGINAL-READBACK-SNAPSHOT-REQUEST-OWNER";
            return {};
        }
        if ((diagnostic = request->validate()))
            return {};
        if (!quota || !quota.use_count() || !quota->limitBytes()
            || !core::SharedOwnerProjection::hasIndependentOwners(
                2u, [&](std::size_t i) noexcept -> const auto& {
                    return i ? quota : request->originalMetadataQuota();
                })) {
            diagnostic = "RHI-ORIGINAL-READBACK-SNAPSHOT-QUOTA-DOMAIN";
            return {};
        }
        for (const auto* object : OriginalCommandReadbackRequestFacts::cpuObjects(*request))
            if (core::cpuAllocationOverlapsQuotaOwner(object, quota)) {
                diagnostic = "RHI-ORIGINAL-READBACK-SNAPSHOT-QUOTA-DOMAIN";
                return {};
            }
        const auto capacity = OriginalCommandReadbackRequestFacts::byteCapacity(*request);
        if (!capacity || capacity > SIZE_MAX || capacity > quota->limitBytes()) {
            diagnostic = "RHI-ORIGINAL-READBACK-SNAPSHOT-CAPACITY";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope result(quota);
            auto snapshot = core::makeUnique<OriginalCommandReadbackResultSnapshotT>(
                ConstructionKey{}, std::move(request));
            snapshot->bytes_.resize(static_cast<std::size_t>(capacity));
            diagnostic = nullptr;
            return shareOriginal(std::move(snapshot));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-ORIGINAL-READBACK-SNAPSHOT-ALLOCATION";
            return {};
        } catch (const std::length_error&) {
            diagnostic = "RHI-ORIGINAL-READBACK-SNAPSHOT-ALLOCATION";
            return {};
        }
    }

    static const char* validateOriginalCold(
        const std::shared_ptr<const OriginalCommandReadbackResultSnapshotT>& self,
        const std::shared_ptr<const Request>& request) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-ORIGINAL-READBACK-SNAPSHOT-OWNER";
        if (!core::SharedOwnerProjection::sameOwner(self->request_, request))
            return "RHI-ORIGINAL-READBACK-SNAPSHOT-REQUEST";
        if (self->published_)
            return "RHI-ORIGINAL-READBACK-SNAPSHOT-IMMUTABLE";
        if (const auto* id = request->validate())
            return id;
        if (!self->originalMetadataQuota()->limitBytes()
            || self->bytes_.size() > self->originalMetadataQuota()->limitBytes()
            || !core::cpuAllocationOverlapsQuotaOwner(
                self->bytes_.data(), self->originalMetadataQuota()))
            return "RHI-ORIGINAL-READBACK-SNAPSHOT-QUOTA";
        return self->bytes_.size() == OriginalCommandReadbackRequestFacts::byteCapacity(*request)
            ? nullptr
            : "RHI-ORIGINAL-READBACK-SNAPSHOT-STORAGE";
    }

    const auto& originalRequest() const noexcept { return request_; }

    const auto& originalLayout() const noexcept { return request_->originalLayout(); }

    std::size_t byteCapacity() const noexcept { return bytes_.size(); }

    const std::uint8_t* publishedBytes() const noexcept
    {
        return published_ ? bytes_.data() : nullptr;
    }

    bool nativeExecutionProven() const noexcept { return published_; }

private:
    const std::shared_ptr<const Request> request_;
    mutable core::pmr::Vector<std::uint8_t> bytes_{&core::defaultCpuMemoryResource()};
    mutable bool published_ = false;
};

using OriginalCommandReadbackResultSnapshot =
    OriginalCommandReadbackResultSnapshotT<TextureReadbackRequestDeclaration>;
using OriginalCommandBufferReadbackResultSnapshot =
    OriginalCommandReadbackResultSnapshotT<BufferReadbackRequestDeclaration>;
} // namespace arp::rhi

// endregion original_command_readback_result_snapshot
// endregion Command readback sources

// region Command pipeline sources

// region original_command_pipeline_source

namespace arp::rhi {
struct OriginalCommandPipelineSourceBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    OriginalRegisteredResourceKind kind = OriginalRegisteredResourceKind::Unsupported;
    std::uint64_t registrationGeneration = 0u, sourceCpuBytes = 0u;
    std::uint32_t maxRetainedOwners = 0u;
};

// Immutable CPU custody only. No strong Source/root reference or native getter.
template <class Pipeline>
class OriginalCommandPipelineSource final : public core::QuotaBoundSharedObjectIdentity {
    static_assert(
        std::is_same_v<Pipeline, RenderPipeline> || std::is_same_v<Pipeline, ComputePipeline>);
    static constexpr bool graphics = std::is_same_v<Pipeline, RenderPipeline>;

    class ConstructionKey {
        friend class OriginalCommandPipelineSource;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    static constexpr auto domain = CommandReferenceDomain::Pipeline;
    static constexpr auto kind = graphics ? OriginalRegisteredResourceKind::GraphicsPipeline
                                          : OriginalRegisteredResourceKind::ComputePipeline;
    static constexpr std::uint32_t retainedOwnerCount =
        0u; // Exact shared closure is projected, not summed.
    static constexpr std::uint32_t cpuOwnerSlots = 10u, exclusiveOwnerCount = graphics ? 10u : 8u;

    static constexpr const char* validateBudget(
        const OriginalCommandPipelineSourceBudget& b) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-ORIGINAL-PIPELINE-SOURCE-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-ORIGINAL-PIPELINE-SOURCE-OPT-IN";
        if (b.kind != kind)
            return "RHI-ORIGINAL-PIPELINE-SOURCE-KIND";
        if (!b.registrationGeneration)
            return "RHI-ORIGINAL-PIPELINE-SOURCE-GENERATION";
        if (!b.sourceCpuBytes)
            return "RHI-ORIGINAL-PIPELINE-SOURCE-BUDGET";
        return b.maxRetainedOwners < exclusiveOwnerCount
            ? "RHI-ORIGINAL-PIPELINE-SOURCE-OWNER-CAPACITY"
            : nullptr;
    }

    OriginalCommandPipelineSource(ConstructionKey,
        std::shared_ptr<const Pipeline> object,
        OriginalCommandPipelineSourceBudget budget) noexcept
        : object_(std::move(object)), budget_(budget)
    {}

    static std::shared_ptr<const OriginalCommandPipelineSource> prepareOriginal(
        std::shared_ptr<const Pipeline> object,
        OriginalCommandPipelineSourceBudget budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const GpuDevice& device,
        const char*& diagnostic)
    {
        diagnostic = validateBudget(budget);
        if (diagnostic)
            return {};
        if (!quota || quota->limitBytes() != budget.sourceCpuBytes) {
            diagnostic = "RHI-ORIGINAL-PIPELINE-SOURCE-QUOTA";
            return {};
        }
        if ((diagnostic = arp::rhi::semantics::OriginalCommandPipelineSourceLowering::lowering(
                 device.backend())
                    .diagnostic))
            return {};
        if ((diagnostic = validateObject(object, budget, quota)))
            return {};
        if ((diagnostic = device.validateOriginalResourceRegistration(*object)))
            return {};
        if (device.overlapsOriginalDeviceCpuOwner(quota)) {
            diagnostic = "RHI-ORIGINAL-PIPELINE-SOURCE-DEVICE-QUOTA-DOMAIN";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope scope(quota);
            return shareOriginal(core::makeUnique<OriginalCommandPipelineSource>(
                ConstructionKey{}, std::move(object), budget));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-ORIGINAL-PIPELINE-SOURCE-ALLOCATION";
            return {};
        }
    }

    static const char* validateOriginal(
        const std::shared_ptr<const OriginalCommandPipelineSource>& self) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-ORIGINAL-PIPELINE-SOURCE-OWNER";
        return validateObject(self->object_, self->budget_, self->originalMetadataQuota());
    }

    static const char* validateOriginal(
        const std::shared_ptr<const OriginalCommandPipelineSource>& self,
        const GpuDevice& device) noexcept
    {
        if (const auto* id = validateOriginal(self))
            return id;
        if (const auto* id = arp::rhi::semantics::OriginalCommandPipelineSourceLowering::lowering(
                device.backend())
                .diagnostic)
            return id;
        if (device.overlapsOriginalDeviceCpuOwner(self->originalMetadataQuota()))
            return "RHI-ORIGINAL-PIPELINE-SOURCE-DEVICE-QUOTA-DOMAIN";
        return device.validateOriginalResourceRegistration(*self->object_);
    }

    const Pipeline* originalObject() const noexcept { return object_.get(); }

    const auto& originalPipeline() const noexcept { return object_; }

    const auto& originalLayout() const noexcept { return object_->originalLayoutOwner(); }

    const char* independentCpuQuotaDiagnostic(
        const std::shared_ptr<core::CpuMemoryQuota>& quota) const noexcept
    {
        return object_->independentAcceptedCpuQuotaDiagnostic(quota);
    }

    bool nativeExecutionProven() const noexcept { return false; }

    // Slot zero is the adapter itself, supplied by the canonical inventory.
    // All other identities are exact original shared owners. Empty tail slots
    // let one bounded rectangular projection handle both pipeline kinds.
    std::shared_ptr<const void> cpuOwner(std::size_t slot) const noexcept
    {
        switch (slot) {
        case 1u:
            return object_;
        case 2u:
            return object_->originalPackageOwner();
        case 3u:
            if constexpr (graphics)
                return object_->originalRenderDefinitionOwner();
            else
                return object_->originalComputeDefinitionOwner();
        case 4u:
            if constexpr (graphics)
                return object_->originalGraphicsCreation();
            else
                return object_->originalComputeCreation();
        case 5u:
            if constexpr (graphics)
                return object_->originalGraphicsCreation()->originalInput();
            else
                return object_->originalComputeCreation()->originalInput();
        case 6u:
            if constexpr (graphics) {
                const auto& input = object_->originalGraphicsCreation()->originalInput();
                return input->tableExpansion()
                    ? std::shared_ptr<const void>(input->tableExpansion())
                    : std::shared_ptr<const void>(input->artifact());
            } else {
                const auto& input = object_->originalComputeCreation()->originalInput();
                return input->tableExpansion()
                    ? std::shared_ptr<const void>(input->tableExpansion())
                    : std::shared_ptr<const void>(input->artifact());
            }
        case 7u:
            return originalLayout();
        case 8u:
            if constexpr (graphics) {
                const auto& input = object_->originalGraphicsCreation()->originalInput();
                return input->tableExpansion() ? std::shared_ptr<const void>{}
                                               : input->artifact()->vertex();
            } else
                return {};
        case 9u:
            if constexpr (graphics) {
                const auto& input = object_->originalGraphicsCreation()->originalInput();
                return input->tableExpansion() ? std::shared_ptr<const void>{}
                                               : input->artifact()->fragment();
            } else
                return {};
        default:
            return {};
        }
    }

private:
    static const char* validateObject(const std::shared_ptr<const Pipeline>& object,
        const OriginalCommandPipelineSourceBudget& budget,
        const std::shared_ptr<core::CpuMemoryQuota>& quota) noexcept
    {
        if (const auto* id = validateBudget(budget))
            return id;
        if (!quota || quota->limitBytes() != budget.sourceCpuBytes)
            return "RHI-ORIGINAL-PIPELINE-SOURCE-QUOTA";
        if (!object || !object->sharedOwnerMatches(object))
            return "RHI-ORIGINAL-PIPELINE-SOURCE-WRAPPER-OWNER";
        if (!object->originalAcceptedSharedQuotaBound())
            return "RHI-ORIGINAL-PIPELINE-SOURCE-SHARED-QUOTA-RECEIPT";
        if (object->originalRegisteredKind() != kind)
            return "RHI-ORIGINAL-PIPELINE-SOURCE-KIND";
        if constexpr (graphics) {
            if (const auto* id = object->originalArtifactGraphicsDiagnostic())
                return id;
        } else {
            if (const auto* id = object->originalArtifactComputeDiagnostic())
                return id;
        }
        if (object->originalResourceRegistration().generation() != budget.registrationGeneration)
            return "RHI-ORIGINAL-PIPELINE-SOURCE-GENERATION";
        if (const auto* id = object->independentAcceptedCpuQuotaDiagnostic(quota))
            return id;
        if constexpr (graphics) {
            if (!PipelineDefinitionIdentity::matches(*object->originalRenderPipelineDefinition(),
                    object->originalGraphicsCreation()->definition()))
                return "RHI-ORIGINAL-PIPELINE-SOURCE-DEFINITION";
        } else if (!PipelineDefinitionIdentity::matches(
                       *object->originalComputePipelineDefinition(),
                       object->originalComputeCreation()->definition()))
            return "RHI-ORIGINAL-PIPELINE-SOURCE-DEFINITION";
        return nullptr;
    }

    const std::shared_ptr<const Pipeline> object_;
    const OriginalCommandPipelineSourceBudget budget_;
};

using OriginalCommandGraphicsPipelineSource = OriginalCommandPipelineSource<RenderPipeline>;
using OriginalCommandComputePipelineSource = OriginalCommandPipelineSource<ComputePipeline>;

// endregion original_command_pipeline_source
// endregion Command pipeline sources

// region Command descriptor dependencies
// region original_command_descriptor_dependencies

class OriginalCommandResourceSources;
class GpuDevice;

struct OriginalCommandDescriptorSetElement {
    DescriptorBindingWrite write;
    std::size_t sourceIndex = SIZE_MAX;
    std::uint64_t registrationGeneration = 0u;
    std::uint64_t bufferAccessOffset = 0u, bufferAccessBytes = 0u;
    PipelineStage stages = PipelineStage::None;
    ResourceAccess access = ResourceAccess::None;
    std::uint32_t dynamicOffset = 0u;
};

struct OriginalCommandDescriptorTableElement {
    DescriptorTableWrite write;
    std::size_t sourceIndex = SIZE_MAX;
    std::uint64_t registrationGeneration = 0u;
};

// L2 dependency boundary, compiled with the existing descriptor module to avoid
// a Source/descriptor header cycle. No graph, journal or execution adapter.
class ARP_API OriginalCommandDescriptorDependencies final {
public:
    using Owner = std::variant<std::shared_ptr<const OriginalCommandBufferLease>,
        std::shared_ptr<const OriginalCommandTextureViewSource>,
        std::shared_ptr<const OriginalCommandSamplerLease>>;
    using PipelineOwner = std::variant<std::monostate,
        std::shared_ptr<const OriginalCommandGraphicsPipelineSource>,
        std::shared_ptr<const OriginalCommandComputePipelineSource>>;
    static PipelineOwner capturePipeline(
        const OriginalCommandResourceSources&, std::size_t) noexcept;
    static const char* validatePipeline(const OriginalCommandResourceSources&,
        std::size_t,
        const PipelineOwner&,
        const std::shared_ptr<core::CpuMemoryQuota>&) noexcept;
    static const char* validateFiniteContent(const DescriptorSet&,
        const PipelineOwner&,
        std::uint32_t,
        const OriginalFiniteDescriptorBufferWrite*,
        std::size_t,
        std::uint64_t,
        const std::shared_ptr<core::CpuMemoryQuota>&,
        std::uint32_t sourceVersion = 3u) noexcept;
    static const char* validateFullBoundView(const OriginalCommandDescriptorSetElement&,
        const DescriptorBindingDesc&,
        std::uint64_t) noexcept;
    static const char* validateGeneralFootprint(const OriginalCommandDescriptorSetElement&,
        const DescriptorBindingDesc&,
        const Owner&) noexcept;
    static const char* validateCompletedDepth(const OriginalCommandDescriptorSetElement&,
        const DescriptorBindingDesc&,
        const Owner&,
        const std::shared_ptr<const OriginalCompletedDepthTextureSource>&,
        const std::shared_ptr<core::CpuMemoryQuota>&,
        const OriginalCompletedDepthReadInputContract::Budget&) noexcept;
    static const char* validateGeneralContent(const DescriptorSet&,
        const PipelineOwner&,
        const std::shared_ptr<const OriginalCommandResourceSources>&,
        std::uint32_t,
        const OriginalCommandDescriptorSetFrozenElement*,
        std::size_t,
        std::uint64_t,
        const std::shared_ptr<core::CpuMemoryQuota>&) noexcept;
    static const char* validateInventory(
        const std::shared_ptr<const OriginalCommandResourceSources>&,
        const std::shared_ptr<core::CpuMemoryQuota>&,
        const GpuDevice&) noexcept;
    static const char* validateInventoryOwner(
        const std::shared_ptr<const OriginalCommandResourceSources>&,
        const std::shared_ptr<core::CpuMemoryQuota>&) noexcept;
    static const char* validate(const DescriptorLayoutDesc&,
        const OriginalCommandDescriptorSetElement*,
        std::size_t,
        const std::shared_ptr<const OriginalCommandResourceSources>&,
        const GpuDevice&,
        std::uint32_t bindlessSamplerArraysVersion = 0u);
    static const char* validate(const DescriptorTableDesc&,
        const OriginalCommandDescriptorTableElement*,
        std::size_t,
        const std::shared_ptr<const OriginalCommandResourceSources>&,
        const GpuDevice&);
    static std::optional<Owner> capture(
        const OriginalCommandResourceSources&, std::size_t) noexcept;
    static bool matches(const OriginalCommandResourceSources&, std::size_t, const Owner&) noexcept;
    static const char* validateOwner(
        const Owner&, std::uint64_t, const std::shared_ptr<core::CpuMemoryQuota>&) noexcept;
    static const char* validateRegistered(const Owner&, const GpuDevice&) noexcept;
    static const char* validateClosure(const DescriptorSet&,
        const std::shared_ptr<const DescriptorArena>&,
        const GpuDevice&) noexcept;
    static const char* validateClosure(const DescriptorTable&,
        const std::shared_ptr<const DescriptorArena>&,
        const GpuDevice&) noexcept;
};

// endregion original_command_descriptor_dependencies
// endregion Command descriptor dependencies

// region Command descriptor sources

// region original_command_descriptor_source

struct OriginalCommandDescriptorSourceBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxLogicalElements = 0u, maxWrittenElements = 0u, maxExclusiveRetainedOwners = 0u;
    std::uint64_t sourceCpuBytes = 0u;
    std::size_t finitePipelineSourceIndex = SIZE_MAX;
    std::uint32_t finiteLogicalSet = 0u, finiteFootprintContractVersion = 0u,
                  maxFrozenDynamicOffsets = 0u;
    bool finiteFullBoundViewOptIn = false;
    OriginalCompletedDepthReadInputContract::Budget completedDepthInputs;
};

// The existing frozen storage is also the borrowed General content sequence.
// No second declaration array or ownership inventory is needed for Source/5.
struct OriginalCommandDescriptorSetFrozenElement {
    OriginalCommandDescriptorSetElement declaration;
    DescriptorSetWriteObservation observation;
    OriginalCommandDescriptorDependencies::Owner resourceOwner;
    std::shared_ptr<const OriginalCompletedDepthTextureSource> completedDepth;
};

// Cold original ownership/content closure only. Canonical positional/native
// admission must still join this exact object to the existing Source/Inputs.
template <class Descriptor>
class OriginalCommandDescriptorSource final : public core::QuotaBoundSharedObjectIdentity {
    static_assert(
        std::is_same_v<Descriptor, DescriptorSet> || std::is_same_v<Descriptor, DescriptorTable>);
    static constexpr bool isSet = std::is_same_v<Descriptor, DescriptorSet>;

    class ConstructionKey {
        friend class OriginalCommandDescriptorSource;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    static constexpr bool finiteVersion(std::uint32_t version) noexcept
    {
        return version == 3u || version == 4u;
    }

    static constexpr bool generalFiniteVersion(std::uint32_t version) noexcept
    {
        return version == 5u;
    }

    static constexpr bool pipelineBoundVersion(std::uint32_t version) noexcept
    {
        return finiteVersion(version) || generalFiniteVersion(version);
    }

    static constexpr bool completedDepthVersion(std::uint32_t version) noexcept
    {
        return version == 6u;
    }

    static constexpr bool canonicalVersion(std::uint32_t version) noexcept
    {
        return version == 2u || completedDepthVersion(version);
    }

    using Element = std::conditional_t<isSet,
        OriginalCommandDescriptorSetElement,
        OriginalCommandDescriptorTableElement>;
    using Definition = std::conditional_t<isSet, DescriptorLayoutDesc, DescriptorTableDesc>;
    using Observation =
        std::conditional_t<isSet, DescriptorSetWriteObservation, DescriptorTableWriteObservation>;

    struct FrozenTableElement {
        Element declaration;
        Observation observation;
        OriginalCommandDescriptorDependencies::Owner resourceOwner;
    };

    using FrozenElement =
        std::conditional_t<isSet, OriginalCommandDescriptorSetFrozenElement, FrozenTableElement>;

    static constexpr std::uint32_t retainedOwnerCount =
        2u; // Source plus adopted wrapper; arena counted by the canonical projection.
    static constexpr auto domain =
        isSet ? CommandReferenceDomain::Descriptor : CommandReferenceDomain::DescriptorTable;
    // Includes existing resource-inventory owner; dependencies retain their own
    // bounded contracts. This is not a complete graph/registry owner count.
    static constexpr std::uint32_t exclusiveRetainedOwnerCount = 6u;
    static constexpr std::uint32_t canonicalExclusiveRetainedOwnerCount = 5u;
    static constexpr std::uint32_t finiteExclusiveRetainedOwnerCount =
        canonicalExclusiveRetainedOwnerCount + 1u;

    OriginalCommandDescriptorSource(ConstructionKey,
        const Descriptor& object,
        std::shared_ptr<const DescriptorArena> arena,
        std::shared_ptr<const OriginalCommandResourceSources> resources,
        const OriginalCommandDescriptorSourceBudget& budget,
        std::size_t count)
        : arena_(std::move(arena)), resources_(budget.contractVersion == 1u ? resources : nullptr),
          canonicalResources_(resources), budget_(budget),
          definition_(DescriptorStateCopy::copy(definition(object))), elements_(count),
          finitePipeline_(pipelineBoundVersion(budget.contractVersion)
                  ? OriginalCommandDescriptorDependencies::capturePipeline(
                        *resources, budget.finitePipelineSourceIndex)
                  : OriginalCommandDescriptorDependencies::PipelineOwner{}),
          finiteDeclarations_(finiteVersion(budget.contractVersion) ? count : 0u),
          registrationGeneration_(object.originalResourceRegistration().generation()),
          revision_(object.contentRevision()), allocationIndex_(allocationIndex(object)),
          allocationCapacity_(object.allocation().descriptorCapacity),
          allocationDiagnostics_(object.allocation().diagnostics, &core::defaultCpuMemoryResource())
    {}

    static const char* validateBudget(const OriginalCommandDescriptorSourceBudget& budget) noexcept
    {
        if (budget.contractVersion != 1u && !canonicalVersion(budget.contractVersion)
            && !pipelineBoundVersion(budget.contractVersion))
            return "RHI-ORIGINAL-DESCRIPTOR-SOURCE-VERSION";
        if (!budget.prototypeOptIn)
            return "RHI-ORIGINAL-DESCRIPTOR-SOURCE-OPT-IN";
        if (completedDepthVersion(budget.contractVersion)) {
            if constexpr (!isSet)
                return "RHI-ORIGINAL-COMPLETED-DEPTH-SOURCE-TABLE-PROFILE";
            if (const auto* id = OriginalCompletedDepthReadInputContract::validateIntent(
                    budget.completedDepthInputs))
                return id;
        } else if (budget.completedDepthInputs.contractVersion
            || budget.completedDepthInputs.prototypeOptIn || budget.completedDepthInputs.maxCells
            || budget.completedDepthInputs.maxTextureTexelBytes
            || budget.completedDepthInputs.maxRetainedOwners)
            return "RHI-ORIGINAL-COMPLETED-DEPTH-SOURCE-LEGACY-SCHEMA";
        if (pipelineBoundVersion(budget.contractVersion)) {
            if constexpr (!isSet)
                return "RHI-ORIGINAL-FINITE-SOURCE-TABLE-PROFILE";
            if (budget.finitePipelineSourceIndex == SIZE_MAX)
                return "RHI-ORIGINAL-FINITE-SOURCE-PIPELINE-POSITION";
            if (generalFiniteVersion(budget.contractVersion)
                    ? budget.finiteFootprintContractVersion != 2u
                    : budget.finiteFootprintContractVersion != 1u)
                return "RHI-ORIGINAL-FINITE-SOURCE-FOOTPRINT-VERSION";
            if (!budget.finiteFullBoundViewOptIn)
                return "RHI-ORIGINAL-FINITE-SOURCE-FOOTPRINT-OPT-IN";
            if (budget.maxFrozenDynamicOffsets > budget.maxLogicalElements)
                return "RHI-ORIGINAL-FINITE-SOURCE-DYNAMIC-CAPACITY";
        } else if (budget.finitePipelineSourceIndex != SIZE_MAX || budget.finiteLogicalSet
            || budget.finiteFootprintContractVersion || budget.maxFrozenDynamicOffsets
            || budget.finiteFullBoundViewOptIn)
            return "RHI-ORIGINAL-FINITE-SOURCE-LEGACY-SCHEMA";
        if (!budget.maxLogicalElements || !budget.sourceCpuBytes
            || budget.maxWrittenElements > budget.maxLogicalElements)
            return "RHI-ORIGINAL-DESCRIPTOR-SOURCE-BUDGET";
        const auto owners = pipelineBoundVersion(budget.contractVersion)
            ? finiteExclusiveRetainedOwnerCount
            : canonicalVersion(budget.contractVersion) ? canonicalExclusiveRetainedOwnerCount
                                                       : exclusiveRetainedOwnerCount;
        if (budget.maxExclusiveRetainedOwners < owners)
            return "RHI-ORIGINAL-DESCRIPTOR-SOURCE-OWNER-CAPACITY";
        return nullptr;
    }

    static std::shared_ptr<const OriginalCommandDescriptorSource> prepareOriginal(
        core::UniquePtr<Descriptor>& object,
        std::shared_ptr<const DescriptorArena> arena,
        std::shared_ptr<const OriginalCommandResourceSources> resources,
        const Element* elements,
        std::size_t count,
        const OriginalCommandDescriptorSourceBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const GpuDevice& device,
        const char*& diagnostic,
        const std::shared_ptr<const OriginalCompletedDepthTextureSource>* completedDepths = nullptr,
        std::size_t completedDepthCount = 0u)
    {
        diagnostic = validateBudget(budget);
        if (diagnostic)
            return {};
        if (completedDepthVersion(budget.contractVersion)) {
            if (count != completedDepthCount || !count || !completedDepths) {
                diagnostic = "RHI-ORIGINAL-COMPLETED-DEPTH-SOURCE-ELEMENTS";
                return {};
            }
            if ((diagnostic =
                        semantics::OriginalCommandDescriptorSourceLowering::completedDepthLowering(
                            device.backend())
                            .diagnostic))
                return {};
            const auto input = OriginalCompletedDepthReadInputContract::lowering(
                device.backend(), budget.completedDepthInputs.contractVersion);
            if (!input.validatorOnly && !input.nativeInputValidationAvailable) {
                diagnostic = input.diagnostic;
                return {};
            }
        } else if (completedDepths || completedDepthCount) {
            diagnostic = "RHI-ORIGINAL-COMPLETED-DEPTH-SOURCE-LEGACY-SCHEMA";
            return {};
        }
        if (!quota || quota->limitBytes() != budget.sourceCpuBytes) {
            diagnostic = "RHI-ORIGINAL-DESCRIPTOR-SOURCE-QUOTA";
            return {};
        }
        if (!object) {
            diagnostic = "RHI-ORIGINAL-DESCRIPTOR-SOURCE-OBJECT";
            return {};
        }
        if constexpr (isSet) {
            if ((diagnostic = object->originalScopedContentDiagnostic()))
                return {};
            if (object->originalGeneralContentContractVersion() == 2u
                && !generalFiniteVersion(budget.contractVersion)) {
                diagnostic = "RHI-ORIGINAL-GENERAL-SOURCE-LEGACY-PROFILE";
                return {};
            }
            if (generalFiniteVersion(budget.contractVersion)) {
                if ((diagnostic = semantics::OriginalCommandDescriptorSourceLowering::
                            generalFiniteLowering(device.backend())
                                .diagnostic))
                    return {};
                if (object->originalGeneralContentContractVersion() != 2u) {
                    diagnostic = "RHI-ORIGINAL-GENERAL-SOURCE-WRITE-PROFILE";
                    return {};
                }
            }
            if (object->hasOriginalFiniteContent() && !finiteVersion(budget.contractVersion)) {
                diagnostic = "RHI-ORIGINAL-DESCRIPTOR-SOURCE-FINITE-COMPOSITION-CLOSED";
                return {};
            }
            if (finiteVersion(budget.contractVersion)) {
                if ((diagnostic = arp::rhi::semantics::OriginalCommandDescriptorSourceLowering::
                            finiteLowering(device.backend(), budget.contractVersion)
                                .diagnostic))
                    return {};
                if (!object->hasOriginalFiniteContent()) {
                    diagnostic = "RHI-ORIGINAL-FINITE-WRITE-UNCOMMITTED";
                    return {};
                }
            }
        }
        if (count > budget.maxWrittenElements || (count && !elements)) {
            diagnostic = "RHI-ORIGINAL-DESCRIPTOR-SOURCE-ELEMENTS";
            return {};
        }
        if constexpr (isSet)
            if (!pipelineBoundVersion(budget.contractVersion))
                for (std::size_t i = 0u; i < count; ++i)
                    if (elements[i].dynamicOffset) {
                        diagnostic = "RHI-ORIGINAL-FINITE-SOURCE-LEGACY-SCHEMA";
                        return {};
                    }
        if ((diagnostic = validateClosure(*object, arena, device)))
            return {};
        if ((diagnostic = OriginalCommandDescriptorDependencies::validateInventory(
                 resources, quota, device)))
            return {};
        try {
            core::CpuMemoryQuotaScope scope(quota);
            std::uint32_t capacity = 0u;
            if constexpr (isSet) {
                if (!countDescriptorElements(object->layout(), capacity).success) {
                    diagnostic = "RHI-ORIGINAL-DESCRIPTOR-SOURCE-LAYOUT";
                    return {};
                }
            } else
                capacity = object->desc().capacity;
            if (capacity > budget.maxLogicalElements) {
                diagnostic = "RHI-ORIGINAL-DESCRIPTOR-SOURCE-LOGICAL-CAPACITY";
                return {};
            }
            if ((diagnostic = validateDeclarations(*object, elements, count, resources, device)))
                return {};
            if (writtenElementCount(*object) != count) {
                diagnostic = "RHI-ORIGINAL-DESCRIPTOR-SOURCE-INCOMPLETE";
                return {};
            }
            auto result = core::makeUnique<OriginalCommandDescriptorSource>(
                ConstructionKey{}, *object, std::move(arena), resources, budget, count);
            std::uint64_t depthCells = 0u, depthBytes = 0u;
            std::size_t depthBindings = 0u;
            for (std::size_t i = 0u; i < count; ++i) {
                auto observed = observe(*object, elements[i], diagnostic);
                if (!observed)
                    return {};
                if (!writeMatches(elements[i], *observed)) {
                    diagnostic = "RHI-ORIGINAL-DESCRIPTOR-SOURCE-WRITE-MISMATCH";
                    return {};
                }
                auto dependency = OriginalCommandDescriptorDependencies::capture(
                    *resources, elements[i].sourceIndex);
                if (!dependency) {
                    diagnostic = "RHI-ORIGINAL-DESCRIPTOR-SOURCE-RESOURCE-OWNER";
                    return {};
                }
                FrozenElement frozen{elements[i], *observed, *dependency};
                if constexpr (isSet)
                    if (completedDepthVersion(budget.contractVersion)) {
                        const auto* binding = findDescriptorSetBinding(
                            result->definition_, elements[i].write.binding);
                        if (!binding) {
                            diagnostic = "RHI-ORIGINAL-COMPLETED-DEPTH-SOURCE-BINDING";
                            return {};
                        }
                        if ((diagnostic =
                                    OriginalCommandDescriptorDependencies::validateCompletedDepth(
                                        elements[i],
                                        *binding,
                                        *dependency,
                                        completedDepths[i],
                                        quota,
                                        budget.completedDepthInputs)))
                            return {};
                        frozen.completedDepth = completedDepths[i];
                        if (frozen.completedDepth) {
                            if ((diagnostic = object->diagnoseOriginalCompletedDepthPublication(
                                     *observed, frozen.completedDepth)))
                                return {};
                            const auto input = OriginalCompletedDepthReadInputContract::inspect(
                                {frozen.completedDepth,
                                    elements[i].write.resourceState,
                                    elements[i].stages,
                                    elements[i].access},
                                std::get<std::shared_ptr<const OriginalCommandTextureViewSource>>(
                                    *dependency),
                                budget.completedDepthInputs);
                            if (input.cells > budget.completedDepthInputs.maxCells - depthCells
                                || input.textureTexelBytes
                                    > budget.completedDepthInputs.maxTextureTexelBytes
                                        - depthBytes) {
                                diagnostic = "RHI-ORIGINAL-COMPLETED-DEPTH-SOURCE-TOTAL-CAPACITY";
                                return {};
                            }
                            depthCells += input.cells;
                            depthBytes += input.textureTexelBytes;
                            ++depthBindings;
                        }
                    }
                result->elements_.append(std::move(frozen));
                if constexpr (isSet)
                    if (finiteVersion(budget.contractVersion)) {
                        const auto* buffer =
                            std::get_if<std::shared_ptr<const OriginalCommandBufferLease>>(
                                &*dependency);
                        if (!buffer) {
                            diagnostic = "RHI-ORIGINAL-FINITE-SOURCE-FOOTPRINT-PROFILE";
                            return {};
                        }
                        result->finiteDeclarations_.append(OriginalFiniteDescriptorBufferWrite{
                            elements[i].write, *buffer, elements[i].dynamicOffset});
                    }
            }
            if (completedDepthVersion(budget.contractVersion) && !depthBindings) {
                diagnostic = "RHI-ORIGINAL-COMPLETED-DEPTH-SOURCE-EMPTY";
                return {};
            }
            if (!result->object_.bindCold(object)) {
                diagnostic = "RHI-ORIGINAL-DESCRIPTOR-SOURCE-CANDIDATE";
                return {};
            }
            // Allocate shared custody before the irreversible unique transfer.
            auto shared = shareOriginal(std::move(result));
            if ((diagnostic = shared->validateObject(*object, device)))
                return {};
            if (!shared->object_.adoptOriginal(object, [&](const Descriptor& candidate) noexcept {
                    return !shared->validateObject(candidate, device);
                })) {
                diagnostic = "RHI-ORIGINAL-DESCRIPTOR-SOURCE-CANDIDATE";
                return {};
            }
            return shared;
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-ORIGINAL-DESCRIPTOR-SOURCE-ALLOCATION";
            return {};
        } catch (const std::length_error&) {
            diagnostic = "RHI-ORIGINAL-DESCRIPTOR-SOURCE-ALLOCATION";
            return {};
        }
    }

    static const char* validateOriginal(
        const std::shared_ptr<const OriginalCommandDescriptorSource>& self) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-ORIGINAL-DESCRIPTOR-SOURCE-OWNER";
        const auto* object = self->originalObject();
        return object ? self->validateObjectLocal(*object)
                      : "RHI-ORIGINAL-DESCRIPTOR-SOURCE-OBJECT";
    }

    static const char* validateOriginal(
        const std::shared_ptr<const OriginalCommandDescriptorSource>& self,
        const GpuDevice& device) noexcept
    {
        if (const auto* id = validateOriginal(self))
            return id;
        return self->validateObject(*self->originalObject(), device);
    }

    bool matchesCanonicalSources(const OriginalCommandResourceSources& sources) const noexcept
    {
        const auto original = canonicalResources_.lock();
        if ((!canonicalVersion(budget_.contractVersion)
                && !pipelineBoundVersion(budget_.contractVersion))
            || !original || original.get() != &sources)
            return false;
        for (const auto& element : elements_.values())
            if (!OriginalCommandDescriptorDependencies::matches(
                    sources, element.declaration.sourceIndex, element.resourceOwner))
                return false;
        if (pipelineBoundVersion(budget_.contractVersion)
            && OriginalCommandDescriptorDependencies::validatePipeline(sources,
                budget_.finitePipelineSourceIndex,
                finitePipeline_,
                originalMetadataQuota()))
            return false;
        return true;
    }

    const char* validateFrozenGeneralFootprints() const noexcept
    {
        if (const auto* id = validateBudget(budget_))
            return id;
        if constexpr (isSet) {
            if (!generalFiniteVersion(budget_.contractVersion))
                return "RHI-ORIGINAL-GENERAL-SOURCE-VERSION";
            std::uint32_t dynamics = 0u;
            for (const auto& frozen : elements_.values()) {
                const auto* binding =
                    findDescriptorSetBinding(definition_, frozen.declaration.write.binding);
                if (!binding)
                    return "RHI-ORIGINAL-CONSUMPTION-LAYOUT";
                if (const auto* id =
                        OriginalCommandDescriptorDependencies::validateGeneralFootprint(
                            frozen.declaration, *binding, frozen.resourceOwner))
                    return id;
                if (binding->dynamicOffset)
                    ++dynamics;
            }
            if (dynamics > budget_.maxFrozenDynamicOffsets)
                return "RHI-ORIGINAL-FINITE-SOURCE-DYNAMIC-CAPACITY";
            if (!finiteDeclarations_.values().empty())
                return "RHI-ORIGINAL-GENERAL-SOURCE-STORAGE-PROFILE";
            return nullptr;
        }
        return "RHI-ORIGINAL-FINITE-SOURCE-TABLE-PROFILE";
    }

    std::uint32_t sourceContractVersion() const noexcept { return budget_.contractVersion; }

    const Descriptor* originalObject() const noexcept { return object_.originalObject(); }

    const auto& originalArena() const noexcept { return arena_; }

    const auto& originalResourceSources() const noexcept { return resources_; }

    const Definition& frozenDefinition() const noexcept { return definition_; }

    const auto& frozenElements() const noexcept { return elements_.values(); }

    std::uint32_t frozenElementLimit() const noexcept { return budget_.maxWrittenElements; }

    std::uint64_t frozenContentRevision() const noexcept { return revision_; }

    std::size_t frozenPipelineSourceIndex() const noexcept
    {
        return budget_.finitePipelineSourceIndex;
    }

    std::uint32_t frozenLogicalSet() const noexcept { return budget_.finiteLogicalSet; }

    const auto& frozenFiniteDeclarations() const noexcept { return finiteDeclarations_.values(); }

    // Pure frozen declaration check, shared by cold validation and authenticated
    // Pending scans. No committed-state projection or execution authority.
    const char* validateFrozenFiniteFootprints() const noexcept
    {
        if (const auto* id = validateBudget(budget_))
            return id;
        if constexpr (isSet)
            if (finiteVersion(budget_.contractVersion)) {
                if (finiteDeclarations_.values().size() != elements_.values().size())
                    return "RHI-ORIGINAL-FINITE-SOURCE-BINDING-COUNT";
                std::uint32_t dynamics = 0u;
                std::size_t index = 0u;
                for (const auto& element : elements_.values()) {
                    const auto* binding =
                        findDescriptorSetBinding(definition_, element.declaration.write.binding);
                    const auto* buffer =
                        std::get_if<std::shared_ptr<const OriginalCommandBufferLease>>(
                            &element.resourceOwner);
                    if (!binding || !buffer || !*buffer)
                        return "RHI-ORIGINAL-FINITE-SOURCE-FOOTPRINT-PROFILE";
                    const auto& declaration = finiteDeclarations_.values()[index++];
                    if (!core::SharedOwnerProjection::sameOwner(declaration.buffer, *buffer))
                        return "RHI-ORIGINAL-FINITE-SOURCE-BUFFER";
                    if (!descriptorBindingWriteMatches(
                            declaration.write, element.declaration.write))
                        return "RHI-ORIGINAL-FINITE-SOURCE-WRITE";
                    if (declaration.dynamicOffset != element.declaration.dynamicOffset)
                        return "RHI-ORIGINAL-FINITE-SOURCE-DYNAMIC";
                    if (const auto* id =
                            OriginalCommandDescriptorDependencies::validateFullBoundView(
                                element.declaration, *binding, (*buffer)->layout().sizeBytes))
                        return id;
                    if (binding->dynamicOffset)
                        ++dynamics;
                }
                if (dynamics > budget_.maxFrozenDynamicOffsets)
                    return "RHI-ORIGINAL-FINITE-SOURCE-DYNAMIC-CAPACITY";
            }
        return nullptr;
    }

    bool nativeExecutionProven() const noexcept { return false; }

private:
    static const Definition& definition(const Descriptor& object) noexcept
    {
        if constexpr (isSet)
            return object.layout();
        else
            return object.desc();
    }

    static std::uint32_t allocationIndex(const Descriptor& object) noexcept
    {
        if constexpr (isSet)
            return object.allocation().setIndex;
        else
            return object.allocation().tableIndex;
    }

    static const char* validateClosure(const Descriptor& object,
        const std::shared_ptr<const DescriptorArena>& arena,
        const GpuDevice& device) noexcept
    {
        return OriginalCommandDescriptorDependencies::validateClosure(object, arena, device);
    }

    static const char* validateDeclarations(const Descriptor& object,
        const Element* elements,
        std::size_t count,
        const std::shared_ptr<const OriginalCommandResourceSources>& resources,
        const GpuDevice& device)
    {
        if constexpr (isSet)
            return OriginalCommandDescriptorDependencies::validate(
                object.layout(), elements, count, resources, device);
        else
            return OriginalCommandDescriptorDependencies::validate(
                object.desc(), elements, count, resources, device);
    }

    static std::size_t writtenElementCount(const Descriptor& object) noexcept
    {
        std::size_t count = 0u;
        if constexpr (isSet) {
            for (const auto& binding : object.layout().bindings)
                for (std::uint32_t i = 0u; i < binding.arrayCount; ++i)
                    if (object.hasWrittenBindingElement(binding.binding, i))
                        ++count;
        } else
            for (std::uint32_t i = 0u; i < object.desc().capacity; ++i)
                if (object.hasWrittenDescriptor(i))
                    ++count;
        return count;
    }

    static std::optional<Observation> observe(
        const Descriptor& object, const Element& element, const char*& diagnostic) noexcept
    {
        if constexpr (isSet)
            return object.observeSuccessfulBindingWrite(
                element.write.binding, element.write.arrayElement, diagnostic);
        else
            return object.observeSuccessfulDescriptorWrite(element.write.index, diagnostic);
    }

    static bool writeMatches(const Element& element, const Observation& observation) noexcept
    {
        if constexpr (isSet)
            return descriptorBindingWriteMatches(element.write, observation.write());
        else
            return descriptorTableWriteMatches(element.write, observation.write());
    }

    const char* validateObject(const Descriptor& object, const GpuDevice& device) const noexcept
    {
        if (const auto* id = validateClosure(object, arena_, device))
            return id;
        if (const auto* id = validateObjectLocal(object))
            return id;
        if (budget_.contractVersion == 1u)
            if (const auto* id = OriginalCommandDescriptorDependencies::validateInventory(
                    resources_, originalMetadataQuota(), device))
                return id;
        for (const auto& element : elements_.values())
            if (const auto* id = OriginalCommandDescriptorDependencies::validateRegistered(
                    element.resourceOwner, device))
                return id;
        return nullptr;
    }

    const char* validateObjectLocal(const Descriptor& object) const noexcept
    {
        if constexpr (isSet)
            if (const auto* id = object.originalScopedContentDiagnostic())
                return id;
        if constexpr (isSet)
            if ((object.originalGeneralContentContractVersion() == 2u)
                != generalFiniteVersion(budget_.contractVersion))
                return "RHI-ORIGINAL-GENERAL-SOURCE-WRITE-PROFILE";
        if constexpr (!isSet)
            if (const auto* id = object.originalDepthTableContentDiagnostic())
                return id;
        if (const auto* id = validateBudget(budget_))
            return id;
        if (!originalMetadataQuota()
            || originalMetadataQuota()->limitBytes() != budget_.sourceCpuBytes)
            return "RHI-ORIGINAL-DESCRIPTOR-SOURCE-QUOTA";
        if (!arena_ || !arena_->sharedOwnerMatches(arena_))
            return "RHI-ORIGINAL-DESCRIPTOR-SOURCE-ARENA-OWNER";
        if (const auto* id = arena_->originalResourceRegistration().diagnostic())
            return id;
        if (const auto* id = object.originalResourceRegistration().diagnostic())
            return id;
        if (!object.originalResourceRegistration().matchesParent(
                arena_->originalResourceRegistration()))
            return "RHI-ORIGINAL-DESCRIPTOR-SOURCE-PARENT";
        if (!object.allocationEpochTicket().sameCurrentEpoch(arena_->allocationEpochTicket()))
            return "RHI-ORIGINAL-DESCRIPTOR-SOURCE-EPOCH";
        const auto canonical = canonicalResources_.lock();
        if (!canonical)
            return "RHI-ORIGINAL-DESCRIPTOR-SOURCE-INVENTORY-EXPIRED";
        if (const auto* id = OriginalCommandDescriptorDependencies::validateInventoryOwner(
                canonical, originalMetadataQuota()))
            return id;
        for (const auto& element : elements_.values()) {
            if (!OriginalCommandDescriptorDependencies::matches(
                    *canonical, element.declaration.sourceIndex, element.resourceOwner))
                return "RHI-ORIGINAL-DESCRIPTOR-SOURCE-RESOURCE-POSITION";
            if (const auto* id =
                    OriginalCommandDescriptorDependencies::validateOwner(element.resourceOwner,
                        element.declaration.registrationGeneration,
                        originalMetadataQuota()))
                return id;
            if constexpr (isSet)
                if (completedDepthVersion(budget_.contractVersion)) {
                    const auto* binding =
                        findDescriptorSetBinding(definition_, element.declaration.write.binding);
                    if (!binding)
                        return "RHI-ORIGINAL-COMPLETED-DEPTH-SOURCE-BINDING";
                    if (const auto* id =
                            OriginalCommandDescriptorDependencies::validateCompletedDepth(
                                element.declaration,
                                *binding,
                                element.resourceOwner,
                                element.completedDepth,
                                originalMetadataQuota(),
                                budget_.completedDepthInputs))
                        return id;
                    if (element.completedDepth)
                        if (const auto* id = object.diagnoseOriginalCompletedDepthPublication(
                                element.observation, element.completedDepth))
                            return id;
                }
        }
        if (!object.allocation().success || allocationIndex(object) != allocationIndex_
            || object.allocation().descriptorCapacity != allocationCapacity_
            || object.allocation().diagnostics != allocationDiagnostics_)
            return "RHI-ORIGINAL-DESCRIPTOR-SOURCE-ALLOCATION-CHANGED";
        if (object.originalResourceRegistration().generation() != registrationGeneration_)
            return "RHI-ORIGINAL-DESCRIPTOR-SOURCE-GENERATION";
        if constexpr (isSet) {
            if (!descriptorLayoutDefinitionMatches(definition_, object.layout()))
                return "RHI-ORIGINAL-DESCRIPTOR-SOURCE-DEFINITION";
        } else if (!descriptorTableDefinitionMatches(definition_, object.desc()))
            return "RHI-ORIGINAL-DESCRIPTOR-SOURCE-DEFINITION";
        if (!revision_ || object.contentRevision() != revision_)
            return "RHI-DESCRIPTOR-CONTENT-CHANGED";
        for (const auto& element : elements_.values())
            if (!element.observation.matchesObject(object)
                || !writeMatches(element.declaration, element.observation))
                return "RHI-ORIGINAL-DESCRIPTOR-SOURCE-CONTENT";
        if constexpr (isSet)
            if (pipelineBoundVersion(budget_.contractVersion)) {
                if (const auto* id =
                        OriginalCommandDescriptorDependencies::validatePipeline(*canonical,
                            budget_.finitePipelineSourceIndex,
                            finitePipeline_,
                            originalMetadataQuota()))
                    return id;
                if (generalFiniteVersion(budget_.contractVersion)) {
                    if (const auto* id = validateFrozenGeneralFootprints())
                        return id;
                    return OriginalCommandDescriptorDependencies::validateGeneralContent(object,
                        finitePipeline_,
                        canonical,
                        budget_.finiteLogicalSet,
                        elements_.values().data(),
                        elements_.values().size(),
                        revision_,
                        originalMetadataQuota());
                }
                if (const auto* id = validateFrozenFiniteFootprints())
                    return id;
                if (const auto* id =
                        OriginalCommandDescriptorDependencies::validateFiniteContent(object,
                            finitePipeline_,
                            budget_.finiteLogicalSet,
                            finiteDeclarations_.values().data(),
                            finiteDeclarations_.values().size(),
                            revision_,
                            originalMetadataQuota(),
                            budget_.contractVersion))
                    return id;
            }
        return nullptr;
    }

    const std::shared_ptr<const DescriptorArena> arena_;
    const std::shared_ptr<const OriginalCommandResourceSources> resources_;
    const std::weak_ptr<const OriginalCommandResourceSources> canonicalResources_;
    const OriginalCommandDescriptorSourceBudget budget_;
    const Definition definition_;
    core::BoundedSequence<FrozenElement> elements_;
    const OriginalCommandDescriptorDependencies::PipelineOwner finitePipeline_;
    core::BoundedSequence<OriginalFiniteDescriptorBufferWrite> finiteDeclarations_;
    const std::uint64_t registrationGeneration_, revision_;
    const std::uint32_t allocationIndex_, allocationCapacity_;
    const core::pmr::String allocationDiagnostics_;
    core::ColdOwnedObjectSlot<Descriptor> object_;
};

using OriginalCommandDescriptorSetSource = OriginalCommandDescriptorSource<DescriptorSet>;
using OriginalCommandDescriptorTableSource = OriginalCommandDescriptorSource<DescriptorTable>;

// endregion original_command_descriptor_source
// endregion Command descriptor sources

// region original_command_bindless_root_source
struct OriginalCommandBindlessRootSourceBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxPhysicalElements = 0u, maxExclusiveRetainedOwners = 0u;
    std::uint64_t sourceCpuBytes = 0u;
    std::uint32_t maxOrdinaryDomains = 0u, maxOrdinaryElements = 0u;
};

class ARP_API OriginalCommandBindlessRootSource final : public core::QuotaBoundSharedObjectIdentity,
                                                        public BindlessAccessDeclarations {
    class ConstructionKey {
        friend class OriginalCommandBindlessRootSource;

        ConstructionKey() {}

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    using Prepared = std::variant<std::shared_ptr<const BindlessPreparedComputeBinding>,
        std::shared_ptr<const BindlessPreparedGraphicsBinding>>;
    static constexpr auto domain = CommandReferenceDomain::BindlessRoot;
    // Source, original snapshot, its unique descriptor set and original arena.
    static constexpr std::uint32_t retainedOwnerCount = 4u;
    static constexpr std::uint32_t cpuOwnerSlots = 10u;

    struct Element {
        OriginalCommandDescriptorSetElement declaration;
        OriginalCommandDescriptorDependencies::Owner owner;
    };

    struct OrdinaryDomain {
        std::uint32_t set = 0u;
        std::size_t sourceIndex = SIZE_MAX;
        std::shared_ptr<const OriginalCommandDescriptorSetSource> source;
    };

    OriginalCommandBindlessRootSource(ConstructionKey,
        std::shared_ptr<const BindlessSnapshot> snapshot,
        Prepared prepared,
        std::shared_ptr<const OriginalCommandResourceSources> inventory,
        OriginalCommandBindlessRootSourceBudget budget);
    static const char* validateBudget(const OriginalCommandBindlessRootSourceBudget&) noexcept;
    static std::shared_ptr<const OriginalCommandBindlessRootSource> prepareOriginal(
        std::shared_ptr<const BindlessSnapshot>,
        Prepared,
        const std::shared_ptr<const OriginalCommandResourceSources>&,
        const OriginalCommandDescriptorSetElement*,
        std::size_t,
        const OriginalCommandBindlessRootSourceBudget&,
        std::shared_ptr<core::CpuMemoryQuota>,
        const GpuDevice&,
        const char*&);
    static std::shared_ptr<const OriginalCommandBindlessRootSource> prepareOriginal(
        std::shared_ptr<const BindlessSnapshot>,
        Prepared,
        const std::shared_ptr<const OriginalCommandResourceSources>&,
        const OriginalCommandDescriptorSetElement*,
        std::size_t,
        const OrdinaryDomain*,
        std::size_t,
        const OriginalCommandBindlessRootSourceBudget&,
        std::shared_ptr<core::CpuMemoryQuota>,
        const GpuDevice&,
        const char*&);
    static const char* validateOriginal(
        const std::shared_ptr<const OriginalCommandBindlessRootSource>&) noexcept;
    static const char* validateOriginal(
        const std::shared_ptr<const OriginalCommandBindlessRootSource>&, const GpuDevice&) noexcept;
    const char* validate() const noexcept;

    bool valid() const noexcept override { return !validate(); }

    std::size_t size() const noexcept override;
    bool read(std::size_t, BindlessResourceAccess&) const override;
    // Exact physical declaration source in the same canonical inventory.
    // Borrowed metadata only; no new owner, state or execution authority.
    bool readOriginalDeclaration(std::size_t, BindlessResourceAccess&, std::size_t&) const;
    // EntryAccess/1 consumes authenticated per-entry declarations while retaining
    // the complete source closure. Inactive bound resources still obey native
    // usage-scope rules. No state/content/command authority is created here.
    // /2 adds the pinned visibility consumer's Graphics and ordinary aliases.
    // /3 covers its Compute entries; /4 covers authenticated SceneContent/1
    // Graphics pairs using the same explicit target-inactivity evidence.
    std::uint32_t originalShaderEntryAccessVersion() const noexcept;
    const char* readOriginalShaderDeclaration(std::size_t,
        ShaderStage,
        BindlessResourceAccess&,
        std::size_t&,
        bool& active,
        std::uint32_t version = 1u) const noexcept;
    // Borrow the exact typed publication for this physical element. No new
    // ownership, state claim or execution authority is created.
    std::shared_ptr<const OriginalCompletedDepthTextureSource> originalCompletedDepthAt(
        std::size_t) const noexcept;
    bool matchesCanonicalSources(const OriginalCommandResourceSources&) const noexcept;
    const DescriptorSet* originalObject() const noexcept;
    const DescriptorArena* originalArena() const noexcept;

    const auto& originalSnapshot() const noexcept { return snapshot_; }

    const auto& originalPrepared() const noexcept { return prepared_; }

    const auto& authorization() const noexcept { return authorization_; }

    const auto& elements() const noexcept { return elements_; }

    std::uint32_t frozenElementLimit() const noexcept { return budget_.maxPhysicalElements; }

    const auto& ordinaryDomains() const noexcept { return ordinaryDomains_; }

    // OrdinaryOpaqueBinding/1 joins one authentic selected-entry declaration
    // to the same canonical original view/sampler and registration generation.
    // Copied facts only; every failure clears the output. No execution grant.
    static const char* originalOrdinaryOpaqueBinding(
        const std::shared_ptr<const OriginalCommandBindlessRootSource>&,
        core::StringView entry,
        std::uint32_t set,
        std::uint32_t binding,
        BindlessOrdinaryOpaqueBinding&,
        std::uint32_t version = 1u) noexcept;

    // Original ordinary-memory inspection /1 (Compute) and /2 (also Graphics)
    // join the package's frozen
    // declaration facts to the exact retained descriptor and canonical buffer.
    // Outputs are copied metadata; they grant no execution/content authority.
    static const char* originalOrdinaryBufferMemory(
        const std::shared_ptr<const OriginalCommandBindlessRootSource>&,
        core::StringView entry,
        std::uint32_t set,
        std::uint32_t binding,
        shader::TargetBufferMemoryLayout&,
        BindlessResourceAccess&,
        std::size_t& sourceIndex,
        std::uint32_t version = 1u) noexcept;

    // OrdinaryGraphicsBufferRange/1: authentic selected entry and original
    // resource/range joined with typed binary/target/driver requirements.
    // /1 is static only; /2 derives runtime-element capacity from the original
    // range and requires equal binary/WGSL counts when both strides are proved.
    // Neither query is a member-layout equivalence proof.
    static const char* originalOrdinaryBufferRequirement(
        const std::shared_ptr<const OriginalCommandBindlessRootSource>&,
        core::StringView entry,
        std::uint32_t set,
        std::uint32_t binding,
        ShaderBufferMemoryRequirement&,
        BindlessResourceAccess&,
        std::size_t& sourceIndex,
        std::uint32_t version = 1u) noexcept;

    // OriginalGraphicsMemberMemory/1 joins factory-frozen member evidence to
    // the same canonical original static buffer/range/stage and live Root.
    // Query2 admits proven runtime tails and copies capacity from Range2 only.
    // All outputs clear on refusal. Copies issue no execution authority.
    static const char* originalOrdinaryBufferMemberMemory(
        const std::shared_ptr<const OriginalCommandBindlessRootSource>&,
        core::StringView entry,
        std::uint32_t set,
        std::uint32_t binding,
        ShaderBufferMemberMemory&,
        BindlessResourceAccess&,
        std::size_t& sourceIndex,
        std::uint32_t version = 1u) noexcept;

    // Mandatory pre-record admission for every active ordinary Graphics buffer.
    // Uses the same original selected modules and range query; never issues an
    // execution ticket or publishes a partial access/state plan.
    static const char* validateOriginalGraphicsBufferRanges(
        const std::shared_ptr<const OriginalCommandBindlessRootSource>&,
        std::uint32_t version = 1u) noexcept;

    std::uint32_t sourceContractVersion() const noexcept { return budget_.contractVersion; }

    bool nativeExecutionProven() const noexcept { return false; }

private:
    friend class common::OriginalCommandBindlessRootAccesses;
    // Only the common walker's scoped internal path may borrow these frozen
    // declarations after original Inputs/graph validation. Public reads guard
    // every call; this creates no persistent ticket or execution authority.
    bool readValidatedDeclaration(std::size_t, BindlessResourceAccess&, std::size_t&) const;
    const char* readValidatedShaderDeclaration(std::size_t,
        ShaderStage,
        BindlessResourceAccess&,
        std::size_t&,
        bool& active,
        std::uint32_t version) const noexcept;
    static const char* readValidatedOrdinaryBufferRequirement(
        const std::shared_ptr<const OriginalCommandBindlessRootSource>&,
        core::StringView entry,
        std::uint32_t set,
        std::uint32_t binding,
        ShaderBufferMemoryRequirement&,
        BindlessResourceAccess&,
        std::size_t& sourceIndex,
        std::uint32_t version) noexcept;
    static const char* readValidatedOrdinaryBufferMemberMemory(
        const std::shared_ptr<const OriginalCommandBindlessRootSource>&,
        core::StringView entry,
        std::uint32_t set,
        std::uint32_t binding,
        ShaderBufferMemberMemory&,
        BindlessResourceAccess&,
        std::size_t& sourceIndex,
        std::uint32_t version) noexcept;
    static const char* validateScopedGraphicsBufferRanges(
        const std::shared_ptr<const OriginalCommandBindlessRootSource>&,
        std::uint32_t version = 1u) noexcept;
    const std::shared_ptr<const BindlessSnapshot> snapshot_;
    const Prepared prepared_;
    const std::shared_ptr<const BindlessAuthorization> authorization_;
    const std::weak_ptr<const OriginalCommandResourceSources> inventory_;
    const OriginalCommandBindlessRootSourceBudget budget_;
    const std::uint64_t generation_, contentRevision_;
    core::pmr::Vector<Element> elements_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<OrdinaryDomain> ordinaryDomains_{&core::defaultCpuMemoryResource()};
};

// endregion original_command_bindless_root_source

// BindlessRootNativeClosure /1 freezes backend-private payload correspondence
// over the already-authentic Root /1 in the same canonical inventory. This is
// cold ownership admission only: it grants no state, recording, submission,
// completion or readback authority and allocates no GPU resource. Original
// resource/capacity owners remain in that inventory; native metadata has its
// own explicit quota. Duplicate payload owners must be projected, not charged
// once per descriptor reference. Factories publish all-or-nothing.
// Independent immutable Shader/1 custody over the original Root. This does
// not select an execution profile or authorize GPU writes/completion.
struct OriginalBindlessInitializedShaderBudget {
    std::uint32_t contractVersion = 0u;
    bool optIn = false;
    std::uint32_t maxActors = 0u;
    std::uint64_t maxBytes = 0u, maxBindingVisits = 0u;

    constexpr bool empty() const noexcept
    {
        return !contractVersion && !optIn && !maxActors && !maxBytes && !maxBindingVisits;
    }

    constexpr const char* diagnostic() const noexcept
    {
        if (empty())
            return nullptr;
        if (contractVersion != 1u)
            return "RHI-BINDLESS-INITIALIZED-SHADER-VERSION";
        if (!optIn)
            return "RHI-BINDLESS-INITIALIZED-SHADER-OPT-IN";
        return maxActors && maxBytes && maxBindingVisits ? nullptr
                                                         : "RHI-BINDLESS-INITIALIZED-SHADER-BUDGET";
    }
};

struct OriginalBindlessRootNativeClosureBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxResourceEntries = 0u, maxNativeOwners = 0u, maxRetainedOwners = 0u;
    std::uint64_t maxCpuMetadataBytes = 0u;
    // /3 retains authentic initialized geometry actors beside their exact
    // canonical buffer entries. Earlier profiles cannot borrow this custody.
    std::uint32_t maxInitializedGeometryActors = 0u;
    std::uint64_t maxInitializedGeometryBytes = 0u;
    // /4 retains authentic attachment views beside scalar Fragment buffers.
    std::uint32_t maxAttachmentViews = 0u;
    // /7 cold and /8 executable WebGPU; /9 cold native Source2 correspondence:
    // per distinct completed
    // depth input ceilings. Neither closure alone authorizes execution. maxResourceEntries
    // bounds their number; quota bounds owned metadata. No output attachments.
    OriginalCompletedDepthReadInputContract::Budget completedDepthInputs;
    // Independent initialized graphics extension. Zero preserves every
    // historical non-geometry profile; /1 composes authentic frozen geometry
    // with the original WebGPU graphics attachment closure.
    std::uint32_t initializedGeometryContractVersion = 0u;
    bool initializedGeometryOptIn = false;
    OriginalBindlessInitializedShaderBudget initializedShader;
    OriginalBindlessProducedAttachmentContract::Intent producedAttachments;
};

class OriginalBindlessRootNativeClosureContract final {
public:
    // Per-Root resource custody for the closed VisibilityConsumer/2 stream.
    // Compute roots retain initialized shader inputs; graphics roots also
    // retain the original vertex/index actors. Execution remains independent.
    static constexpr std::uint32_t producedGeometryVersion = 21u;

    static const char* producedRootDiagnostic(
        const OriginalCommandBindlessRootSource& root, std::uint32_t version) noexcept;

    static constexpr bool hasInitializedGeometry(
        const OriginalBindlessRootNativeClosureBudget& b) noexcept
    {
        return b.contractVersion == 3u || b.initializedGeometryContractVersion == 1u;
    }

    static constexpr const char* validateGeometryBackend(
        Backend backend, const OriginalBindlessRootNativeClosureBudget& b) noexcept
    {
        if (!b.producedAttachments.empty()) {
            const auto decision = OriginalBindlessProducedAttachmentContract::lowering(
                backend, b.producedAttachments.contractVersion);
            if (!decision.preparationAvailable)
                return decision.diagnostic;
        }
        if (b.contractVersion == producedGeometryVersion)
            return nullptr; // Fence geometry is retained by Pass/2, not a callback adapter.
        return semantics::OriginalBindlessGeometryExecutionLowering::callbackClosureDiagnostic(
            backend, b.initializedGeometryContractVersion, b.initializedGeometryOptIn);
    }

    // /16 joins Source6/Input3 ordinary depth and comparison samplers to the
    // selected Graphics Root2. Correspondence grants no recording/completion.
    static constexpr std::uint32_t ordinaryGraphicsDepthVersion = 16u;

    // Canonical control1/Publication6 ordinary color correspondence only.
    // Independent nonexecuting Preparation11 retains old Preparation2 scope.
    static constexpr std::uint32_t canonicalGraphicsColorVersion = 17u;
    // Independent executable-plan companion. The closure itself never records.
    static constexpr std::uint32_t canonicalGraphicsColorExecutionVersion = 18u;

    // Cold Preparation12 composes canonical completed depth Source1/control2
    // (descriptor Source6/Input1) with whole-upload color Publication6/control1.
    // Native Source2/Input3/control4 and every execution profile stay separate.
    static constexpr std::uint32_t canonicalGraphicsDepthVersion = 19u;
    // Same canonical resource provenance; executable plan and callback custody
    // are independently authenticated by Execution14/Lifecycle20.
    static constexpr std::uint32_t canonicalGraphicsDepthExecutionVersion = 20u;

    static constexpr bool canonicalGraphicsVersion(std::uint32_t version) noexcept
    {
        return version == canonicalGraphicsColorVersion
            || version == canonicalGraphicsColorExecutionVersion
            || version == canonicalGraphicsDepthVersion
            || version == canonicalGraphicsDepthExecutionVersion;
    }

    static constexpr const char* canonicalGraphicsDepthProfileDiagnostic(
        const BindlessLoweringPlan& plan,
        std::uint32_t sourceVersion,
        bool graphics,
        bool ordinaryDomains,
        std::uint32_t closureVersion = canonicalGraphicsDepthVersion) noexcept
    {
        const bool profile = closureVersion == canonicalGraphicsDepthVersion
            ? !plan.executable && plan.originalPreparationContractVersion == 12u
                && !plan.originalExecutionContractVersion
            : closureVersion == canonicalGraphicsDepthExecutionVersion && plan.executable
                && !plan.originalPreparationContractVersion
                && plan.originalExecutionContractVersion == 14u;
        return sourceVersion == 2u && graphics && ordinaryDomains && plan.success
                && plan.execution == BindlessExecution::Emulated && profile
            ? nullptr
            : "RHI-BINDLESS-CANONICAL-DEPTH-CLOSURE-PROFILE";
    }

    static const char* canonicalGraphicsDepthRootDiagnostic(
        const OriginalCommandBindlessRootSource& root, std::uint32_t version) noexcept
    {
        if (version != canonicalGraphicsDepthVersion
            && version != canonicalGraphicsDepthExecutionVersion)
            return nullptr;
        if (const auto* id =
                canonicalGraphicsDepthProfileDiagnostic(root.originalSnapshot()->plan(),
                    root.sourceContractVersion(),
                    std::holds_alternative<std::shared_ptr<const BindlessPreparedGraphicsBinding>>(
                        root.originalPrepared()),
                    !root.ordinaryDomains().empty(),
                    version))
            return id;
        for (const auto& element : root.elements())
            if (element.declaration.write.type != DescriptorType::UniformBuffer
                && element.declaration.write.type != DescriptorType::ReadOnlyStorageBuffer
                && element.declaration.write.type != DescriptorType::StorageBuffer)
                return "RHI-BINDLESS-CANONICAL-DEPTH-CLOSURE-PRIMARY-BUFFER-SCOPE";
        bool depth = false, comparison = false;
        for (const auto& domain : root.ordinaryDomains())
            for (const auto& element : domain.source->frozenElements()) {
                const auto* binding = findDescriptorSetBinding(
                    domain.source->frozenDefinition(), element.declaration.write.binding);
                if (!binding)
                    return "RHI-BINDLESS-CANONICAL-DEPTH-CLOSURE-BINDING";
                comparison = comparison || binding->comparisonSampler;
                if (!binding->depthTexture)
                    continue;
                if (domain.source->sourceContractVersion() != 6u || !element.completedDepth
                    || element.completedDepth->budget().contractVersion != 1u
                    || element.declaration.stages != PipelineStage::FragmentShader)
                    return "RHI-BINDLESS-CANONICAL-DEPTH-CLOSURE-SOURCE-PROFILE";
                depth = true;
            }
        return depth && comparison ? nullptr : "RHI-BINDLESS-CANONICAL-DEPTH-CLOSURE-EMPTY";
    }

    static constexpr const char* canonicalGraphicsColorProfileDiagnostic(
        const BindlessLoweringPlan& plan,
        std::uint32_t sourceVersion,
        bool graphics,
        bool ordinaryDomains,
        std::uint32_t closureVersion = canonicalGraphicsColorVersion) noexcept
    {
        const bool profile = closureVersion == canonicalGraphicsColorVersion
            ? !plan.executable && plan.originalPreparationContractVersion == 11u
                && !plan.originalExecutionContractVersion
            : closureVersion == canonicalGraphicsColorExecutionVersion && plan.executable
                && !plan.originalPreparationContractVersion
                && plan.originalExecutionContractVersion == 13u;
        return sourceVersion == 2u && graphics && ordinaryDomains && plan.success
                && plan.execution == BindlessExecution::Emulated && profile
            ? nullptr
            : "RHI-BINDLESS-CANONICAL-COLOR-CLOSURE-PROFILE";
    }

    static const char* canonicalGraphicsColorRootDiagnostic(
        const OriginalCommandBindlessRootSource& root, std::uint32_t version) noexcept
    {
        if (version != canonicalGraphicsColorVersion
            && version != canonicalGraphicsColorExecutionVersion)
            return nullptr;
        if (const auto* id =
                canonicalGraphicsColorProfileDiagnostic(root.originalSnapshot()->plan(),
                    root.sourceContractVersion(),
                    std::holds_alternative<std::shared_ptr<const BindlessPreparedGraphicsBinding>>(
                        root.originalPrepared()),
                    !root.ordinaryDomains().empty(),
                    version))
            return id;
        for (const auto& element : root.elements())
            if (element.declaration.write.type != DescriptorType::UniformBuffer
                && element.declaration.write.type != DescriptorType::ReadOnlyStorageBuffer
                && element.declaration.write.type != DescriptorType::StorageBuffer)
                return "RHI-BINDLESS-CANONICAL-COLOR-CLOSURE-PRIMARY-BUFFER-SCOPE";
        return nullptr;
    }

    static const char* ordinaryGraphicsDepthRootDiagnostic(
        const OriginalCommandBindlessRootSource& root, std::uint32_t version) noexcept
    {
        if (version != ordinaryGraphicsDepthVersion)
            return nullptr;
        const auto* graphics = std::get_if<std::shared_ptr<const BindlessPreparedGraphicsBinding>>(
            &root.originalPrepared());
        return root.sourceContractVersion() == 2u && !root.ordinaryDomains().empty() && graphics
                && *graphics
                && (*graphics)->owners()->desc().pipeline->bindlessShaderModule(
                    ShaderStage::Fragment, 1u)
            ? nullptr
            : "RHI-BINDLESS-ORDINARY-GRAPHICS-DEPTH-CLOSURE-ROOT";
    }

    static const char* mixedRootDiagnostic(
        const OriginalCommandBindlessRootSource& root, std::uint32_t version) noexcept
    {
        if (version != 13u && version != 14u && version != 15u)
            return nullptr;
        return root.ordinaryDomains().empty() && root.originalShaderEntryAccessVersion() == 1u
                && std::holds_alternative<std::shared_ptr<const BindlessPreparedComputeBinding>>(
                    root.originalPrepared())
            ? nullptr
            : "RHI-BINDLESS-ROOT-MIXED-IMAGE-CLOSURE-ENTRY";
    }

    // /10 freezes cold Preparation8 cell payload correspondence only. No
    // state proposal, native resource creation, command or completion grant.
    static constexpr const char* storageProfileDiagnostic(
        const BindlessLoweringPlan& plan, std::uint32_t version) noexcept
    {
        // /13 is independent cold mixed correspondence, never storage execution.
        if (version == 13u)
            return semantics::OriginalBindlessPreparationLowering::hasMixedImageBindingPlan(plan)
                ? nullptr
                : "RHI-BINDLESS-ROOT-MIXED-IMAGE-CLOSURE-PROFILE";
        if (version == 14u || version == 15u)
            return semantics::OriginalBindlessPreparationLowering::hasMixedImageExecutionPlan(plan)
                    && plan.originalExecutionContractVersion == (version == 14u ? 11u : 12u)
                ? nullptr
                : "RHI-BINDLESS-ROOT-MIXED-IMAGE-CLOSURE-EXECUTION-PROFILE";
        if (semantics::OriginalBindlessPreparationLowering::hasMixedImageSnapshotPlan(plan)
            || semantics::OriginalBindlessPreparationLowering::hasMixedImageBindingPlan(plan)
            || semantics::OriginalBindlessPreparationLowering::hasMixedImageExecutionPlan(plan))
            return "RHI-BINDLESS-ROOT-MIXED-IMAGE-CLOSURE-UNAVAILABLE";
        return ((version == 10u
                    && semantics::OriginalBindlessPreparationLowering::hasPlan(plan, 8u))
                   || (version == 11u && plan.success && plan.executable
                       && !plan.originalPreparationContractVersion
                       && plan.originalExecutionContractVersion == 9u
                       && plan.execution == BindlessExecution::Native)
                   || (version == 12u && plan.success && plan.executable
                       && !plan.originalPreparationContractVersion
                       && plan.originalExecutionContractVersion == 10u
                       && plan.execution == BindlessExecution::Emulated)
                   || (version != 10u && version != 11u && version != 12u
                       && !semantics::OriginalBindlessPreparationLowering::hasStoragePlan(plan)))
            ? nullptr
            : "RHI-BINDLESS-ROOT-STORAGE-CLOSURE-PROFILE";
    }

    static constexpr const char* validateBudget(
        const OriginalBindlessRootNativeClosureBudget& b) noexcept
    {
        if (b.contractVersion < 1u || b.contractVersion > producedGeometryVersion)
            return "RHI-BINDLESS-ROOT-NATIVE-CLOSURE-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-BINDLESS-ROOT-NATIVE-CLOSURE-OPT-IN";
        if (!b.maxResourceEntries || !b.maxNativeOwners || !b.maxRetainedOwners
            || !b.maxCpuMetadataBytes)
            return "RHI-BINDLESS-ROOT-NATIVE-CLOSURE-BUDGET";
        if (b.contractVersion == 7u || b.contractVersion == 8u || b.contractVersion == 9u
            || b.contractVersion == 13u || b.contractVersion == 14u || b.contractVersion == 15u
            || b.contractVersion == ordinaryGraphicsDepthVersion
            || b.contractVersion == canonicalGraphicsDepthVersion
            || b.contractVersion == canonicalGraphicsDepthExecutionVersion) {
            if (const auto* id =
                    OriginalCompletedDepthReadInputContract::validateIntent(b.completedDepthInputs))
                return id;
            if ((b.contractVersion == 13u && b.completedDepthInputs.contractVersion != 1u
                    && b.completedDepthInputs.contractVersion != 2u)
                || (b.contractVersion != 13u
                    && b.completedDepthInputs.contractVersion
                        != (b.contractVersion == ordinaryGraphicsDepthVersion         ? 3u
                                : b.contractVersion == 9u || b.contractVersion == 14u ? 2u
                                                                                      : 1u)))
                return "RHI-BINDLESS-ROOT-DEPTH-INPUT-PROFILE";
        } else if (b.completedDepthInputs.contractVersion || b.completedDepthInputs.prototypeOptIn
            || b.completedDepthInputs.maxCells || b.completedDepthInputs.maxTextureTexelBytes
            || b.completedDepthInputs.maxRetainedOwners)
            return "RHI-BINDLESS-ROOT-DEPTH-INPUT-SCHEMA";
        if ((b.contractVersion == 4u || b.contractVersion == 5u || b.contractVersion == 6u
                || b.contractVersion == 11u || b.contractVersion == 12u || b.contractVersion == 14u
                || b.contractVersion == 15u || b.contractVersion == canonicalGraphicsColorVersion
                || b.contractVersion == canonicalGraphicsColorExecutionVersion
                || b.contractVersion == canonicalGraphicsDepthVersion
                || b.contractVersion == canonicalGraphicsDepthExecutionVersion
                || (b.contractVersion == producedGeometryVersion && !b.producedAttachments.empty()))
                != bool(b.maxAttachmentViews)
            || b.maxAttachmentViews > b.maxResourceEntries)
            return "RHI-BINDLESS-ROOT-ATTACHMENT-BUDGET";
        if (b.initializedGeometryContractVersion || b.initializedGeometryOptIn) {
            if (b.initializedGeometryContractVersion != 1u || !b.initializedGeometryOptIn
                || (b.contractVersion != 4u && b.contractVersion != 5u && b.contractVersion != 6u
                    && b.contractVersion != producedGeometryVersion))
                return "RHI-BINDLESS-ROOT-GEOMETRY-ATTACHMENT-PROFILE";
        }
        if (hasInitializedGeometry(b)) {
            if (!b.maxInitializedGeometryActors
                || b.maxInitializedGeometryActors > b.maxResourceEntries
                || !b.maxInitializedGeometryBytes)
                return "RHI-BINDLESS-ROOT-INITIALIZED-GEOMETRY-BUDGET";
        } else if (b.maxInitializedGeometryActors || b.maxInitializedGeometryBytes)
            return "RHI-BINDLESS-ROOT-INITIALIZED-GEOMETRY-SCHEMA";
        if (const auto* id = b.initializedShader.diagnostic())
            return id;
        if (b.initializedShader.maxActors > b.maxResourceEntries)
            return "RHI-BINDLESS-INITIALIZED-SHADER-ENTRY-CAPACITY";
        if (const auto* id =
                OriginalBindlessProducedAttachmentContract::validateIntent(b.producedAttachments))
            return id;
        if (!b.producedAttachments.empty()
            && (b.contractVersion != 4u && b.contractVersion != 5u && b.contractVersion != 6u
                && b.contractVersion != producedGeometryVersion))
            return "RHI-BINDLESS-PRODUCED-ATTACHMENT-CLOSURE-PROFILE";
        return nullptr;
    }
};

// region Command resource sources
// region original_command_resource_sources

// Explicit value-only join of retained Source2 readback metadata to the same
// canonical texture lease already used by attachments and shader descriptors.
struct OriginalRetainedReadbackCorrespondence {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxReadbackSources = 0u, maxNestedOwners = 0u;

    constexpr bool enabled() const noexcept { return contractVersion == 1u; }

    constexpr const char* validate() const noexcept
    {
        if (!contractVersion)
            return prototypeOptIn || maxReadbackSources || maxNestedOwners
                ? "RHI-ORIGINAL-RETAINED-READBACK-VERSION"
                : nullptr;
        if (contractVersion != 1u)
            return "RHI-ORIGINAL-RETAINED-READBACK-VERSION";
        if (!prototypeOptIn)
            return "RHI-ORIGINAL-RETAINED-READBACK-OPT-IN";
        return maxReadbackSources && maxNestedOwners ? nullptr
                                                     : "RHI-ORIGINAL-RETAINED-READBACK-BUDGET";
    }

    constexpr bool matches(const OriginalRetainedReadbackCorrespondence& other) const noexcept
    {
        return contractVersion == other.contractVersion && prototypeOptIn == other.prototypeOptIn
            && maxReadbackSources == other.maxReadbackSources
            && maxNestedOwners == other.maxNestedOwners;
    }

    static constexpr auto lowering(Backend backend, std::uint32_t version = 1u) noexcept
    {
        return semantics::OriginalRetainedReadbackCorrespondenceLowering::lowering(
            backend, version);
    }
};

// Immutable typed resource/upload/readback CPU source inventory. It owns the original
// leases, not backend observations. No registry/state/completion authority.
struct OriginalCommandSourceAssemblyBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxTypedSources = 0u;
};

class OriginalCommandResourceSources final : public core::QuotaBoundSharedObjectIdentity {
    friend class OriginalCommandStateTransaction;
    friend class OriginalCommandUsage;
    friend class OriginalCommandLifecycleReservation;
    friend class OriginalBindlessWorkloadRetention;

    class ConstructionKey {
        friend class OriginalCommandResourceSources;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    static constexpr bool isTypedDomain(CommandReferenceDomain domain) noexcept
    {
        return domain == CommandReferenceDomain::Descriptor
            || domain == CommandReferenceDomain::DescriptorTable
            || domain == CommandReferenceDomain::Pipeline
            || domain == CommandReferenceDomain::BindlessRoot;
    }

    using Source = std::variant<std::shared_ptr<const OriginalCommandBufferLease>,
        std::shared_ptr<const OriginalCommandTextureLease>,
        std::shared_ptr<const OriginalCommandTextureViewSource>,
        std::shared_ptr<const OriginalCommandTextureUploadSource>,
        std::shared_ptr<const OriginalCommandBufferUploadSource>,
        std::shared_ptr<const OriginalCommandReadbackTextureSource>,
        std::shared_ptr<const OriginalCommandTextureReadbackSource>,
        std::shared_ptr<const OriginalCommandDescriptorSetSource>,
        std::shared_ptr<const OriginalCommandDescriptorTableSource>,
        std::shared_ptr<const OriginalCommandGraphicsPipelineSource>,
        std::shared_ptr<const OriginalCommandComputePipelineSource>,
        std::shared_ptr<const OriginalCommandSamplerLease>,
        std::shared_ptr<const OriginalCommandBindlessRootSource>>;

    OriginalCommandResourceSources(ConstructionKey,
        std::size_t count,
        const OriginalCommandAdmissionBudget& budget,
        std::uint32_t typedSlots = 0u,
        std::uint32_t assemblyVersion = 0u,
        OriginalRetainedReadbackCorrespondence retainedReadback = {})
        : sources_(count + typedSlots), budget_(budget), typedAssembly_(typedSlots != 0u),
          sealed_(typedSlots == 0u), stateTransactions_(budget.maxStateTransactions),
          pendingSubmissions_(budget.maxPendingSubmissions),
          callbackTickets_(budget.maxCallbackTickets),
          callbackRegistrations_(budget.maxCallbackRegistrations),
          assemblyVersion_(assemblyVersion), retainedReadback_(retainedReadback)
    {}

    static std::shared_ptr<const OriginalCommandResourceSources> prepareOriginal(
        const Source* sources,
        std::size_t count,
        const OriginalCommandAdmissionBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic,
        OriginalRetainedReadbackCorrespondence retainedReadback = {})
    {
        diagnostic = OriginalCommandAdmissionContract::validateIntent(budget);
        if (diagnostic)
            return {};
        const auto bytes = budget.maxCpuBytes[static_cast<std::size_t>(
            OriginalCommandAdmissionBudget::CpuDomain::Source)];
        if (!bytes || !quota || quota->limitBytes() != bytes) {
            diagnostic = "RHI-ORIGINAL-SOURCES-QUOTA";
            return {};
        }
        if (count && !sources) {
            diagnostic = "RHI-ORIGINAL-SOURCES-INPUT";
            return {};
        }
        for (std::size_t i = 0u; i < count; ++i)
            if (isTyped(sources[i])) {
                diagnostic = "RHI-ORIGINAL-SOURCES-TYPED-ASSEMBLY-REQUIRED";
                return {};
            }
        if ((diagnostic = validateSources(sources, count, budget, quota, retainedReadback)))
            return {};
        try {
            core::CpuMemoryQuotaScope scope(quota);
            auto result = core::makeUnique<OriginalCommandResourceSources>(
                ConstructionKey{}, count, budget, 0u, 0u, retainedReadback);
            for (std::size_t i = 0u; i < count; ++i)
                result->sources_.append(sources[i]);
            return shareOriginal(std::move(result));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-ORIGINAL-SOURCES-ALLOCATION";
            return {};
        } catch (const std::length_error&) {
            diagnostic = "RHI-ORIGINAL-SOURCES-ALLOCATION";
            return {};
        }
    }

    static std::shared_ptr<OriginalCommandResourceSources> prepareColdOriginalTyped(
        const Source* sources,
        std::size_t count,
        const OriginalCommandAdmissionBudget& budget,
        const OriginalCommandSourceAssemblyBudget& assembly,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic,
        OriginalRetainedReadbackCorrespondence retainedReadback = {})
    {
        diagnostic = OriginalCommandAdmissionContract::validateIntent(budget);
        if (diagnostic)
            return {};
        if (assembly.contractVersion != 1u && assembly.contractVersion != 2u
            && assembly.contractVersion != 3u && assembly.contractVersion != 4u
            && assembly.contractVersion != 5u && assembly.contractVersion != 6u) {
            diagnostic = "RHI-ORIGINAL-SOURCES-ASSEMBLY-VERSION";
            return {};
        }
        if (!assembly.prototypeOptIn) {
            diagnostic = "RHI-ORIGINAL-SOURCES-ASSEMBLY-OPT-IN";
            return {};
        }
        const auto required =
            std::uint64_t(budget
                    .maxSourceOwners[static_cast<std::size_t>(CommandReferenceDomain::Descriptor)])
            + budget.maxSourceOwners[static_cast<std::size_t>(
                CommandReferenceDomain::DescriptorTable)]
            + (assembly.contractVersion >= 2u ? budget.maxSourceOwners[static_cast<std::size_t>(
                                                    CommandReferenceDomain::Pipeline)]
                                              : 0u)
            + (assembly.contractVersion == 6u ? budget.maxSourceOwners[static_cast<std::size_t>(
                                                    CommandReferenceDomain::BindlessRoot)]
                                              : 0u);
        if (!assembly.maxTypedSources || assembly.maxTypedSources != required
            || count > SIZE_MAX - assembly.maxTypedSources) {
            diagnostic = "RHI-ORIGINAL-SOURCES-ASSEMBLY-CAPACITY";
            return {};
        }
        const auto bytes = budget.maxCpuBytes[static_cast<std::size_t>(
            OriginalCommandAdmissionBudget::CpuDomain::Source)];
        if (!quota || quota->limitBytes() != bytes) {
            diagnostic = "RHI-ORIGINAL-SOURCES-QUOTA";
            return {};
        }
        if (count && !sources) {
            diagnostic = "RHI-ORIGINAL-SOURCES-INPUT";
            return {};
        }
        for (std::size_t i = 0u; i < count; ++i)
            if (isTyped(sources[i])) {
                diagnostic = "RHI-ORIGINAL-SOURCES-ASSEMBLY-INITIAL-TYPE";
                return {};
            }
        if ((diagnostic = validateSources(sources, count, budget, quota, retainedReadback)))
            return {};
        try {
            core::CpuMemoryQuotaScope scope(quota);
            auto result = core::makeUnique<OriginalCommandResourceSources>(ConstructionKey{},
                count,
                budget,
                assembly.maxTypedSources,
                assembly.contractVersion,
                retainedReadback);
            for (std::size_t i = 0u; i < count; ++i)
                result->sources_.append(sources[i]);
            return shareOriginal(std::move(result));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-ORIGINAL-SOURCES-ALLOCATION";
            return {};
        } catch (const std::length_error&) {
            diagnostic = "RHI-ORIGINAL-SOURCES-ALLOCATION";
            return {};
        }
    }

    static const char* appendOriginalTyped(
        const std::shared_ptr<OriginalCommandResourceSources>& self,
        const Source& source,
        const GpuDevice& device) noexcept
    {
        if (const auto* id = validateOriginal(self))
            return id;
        if (!self->typedAssembly_)
            return "RHI-ORIGINAL-SOURCES-TYPED-ASSEMBLY-REQUIRED";
        if (self->sealed_)
            return "RHI-ORIGINAL-SOURCES-ASSEMBLY-SEALED";
        if (!isTyped(source))
            return "RHI-ORIGINAL-SOURCES-ASSEMBLY-APPEND-TYPE";
        if (const auto* id = validateRegisteredLifetime(self, device))
            return id;
        const auto* id = std::visit(
            [&](const auto& value) -> const char* {
                using T = std::remove_const_t<typename std::decay_t<decltype(value)>::element_type>;
                if constexpr (T::domain == CommandReferenceDomain::BindlessRoot) {
                    if (self->assemblyVersion_ != 6u)
                        return "RHI-ORIGINAL-SOURCES-ASSEMBLY-BINDLESS-VERSION";
                    if (const auto* error = T::validateOriginal(value, device))
                        return error;
                    return value->matchesCanonicalSources(*self)
                        ? nullptr
                        : "RHI-ORIGINAL-SOURCES-ASSEMBLY-FOREIGN-INVENTORY";
                } else if constexpr (T::domain == CommandReferenceDomain::Pipeline) {
                    if (self->assemblyVersion_ != 2u && self->assemblyVersion_ != 3u
                        && self->assemblyVersion_ != 4u && self->assemblyVersion_ != 5u
                        && self->assemblyVersion_ != 6u)
                        return "RHI-ORIGINAL-SOURCES-ASSEMBLY-PIPELINE-VERSION";
                    if (const auto* error = T::validateOriginal(value, device))
                        return error;
                    return core::SharedOwnerProjection::sameOwner(
                               value->originalMetadataQuota(), self->originalMetadataQuota())
                        ? nullptr
                        : "RHI-ORIGINAL-SOURCES-SOURCE-QUOTA";
                } else if constexpr (isTypedDomain(T::domain)) {
                    if (const auto* error = T::validateOriginal(value, device))
                        return error;
                    if (!allowsDescriptorSourceVersion(
                            self->assemblyVersion_, value->sourceContractVersion()))
                        return "RHI-ORIGINAL-SOURCES-ASSEMBLY-DESCRIPTOR-VERSION";
                    return value->matchesCanonicalSources(*self)
                        ? nullptr
                        : "RHI-ORIGINAL-SOURCES-ASSEMBLY-FOREIGN-INVENTORY";
                } else
                    return "RHI-ORIGINAL-SOURCES-ASSEMBLY-APPEND-TYPE";
            },
            source);
        if (id)
            return id;
        std::size_t domainCount = 0u;
        for (const auto& prior : self->sources()) {
            if (domain(prior) == domain(source))
                ++domainCount;
            if (identity(prior).identity == identity(source).identity)
                return "RHI-ORIGINAL-SOURCES-DUPLICATE";
        }
        if (domainCount >= self->budget_.maxSourceOwners[static_cast<std::size_t>(domain(source))])
            return "RHI-ORIGINAL-SOURCES-DOMAIN-CAPACITY";
        const auto count = self->sources().size() + 1u;
        const auto projected =
            core::SharedIdentityProjection::inspect(count, count, count, [&](std::size_t i) {
                return identity(i + 1u == count ? source : self->sources()[i]);
            });
        if (projected.status != core::SharedIdentityProjection::Status::Complete)
            return "RHI-ORIGINAL-SOURCES-IDENTITY-ALIAS";
        if (projected.identities != count)
            return "RHI-ORIGINAL-SOURCES-DUPLICATE";
        return self->sources_.append(source) ? nullptr : "RHI-ORIGINAL-SOURCES-ASSEMBLY-CAPACITY";
    }

    bool inventorySealed() const noexcept { return sealed_; }

    static constexpr bool allowsDescriptorSourceVersion(
        std::uint32_t assembly, std::uint32_t source) noexcept
    {
        return (assembly >= 1u && assembly <= 6u && source == 2u)
            || (assembly == 3u && source == 3u) || (assembly == 4u && source == 4u)
            || ((assembly == 5u || assembly == 6u) && source == 5u)
            || (assembly == 6u && source == 6u);
    }

    static bool isTyped(const Source& source) noexcept { return isTypedDomain(domain(source)); }

    bool containsTypedSources() const noexcept
    {
        for (const auto& source : sources())
            if (isTyped(source))
                return true;
        return false;
    }

    bool isBindlessOrdinaryDescriptor(std::size_t index) const noexcept
    {
        if (index >= sources().size())
            return false;
        const auto* descriptor =
            std::get_if<std::shared_ptr<const OriginalCommandDescriptorSetSource>>(
                &sources()[index]);
        if (!descriptor || !*descriptor
            || !OriginalCommandDescriptorSetSource::canonicalVersion(
                (*descriptor)->sourceContractVersion()))
            return false;
        for (const auto& source : sources())
            if (const auto* root =
                    std::get_if<std::shared_ptr<const OriginalCommandBindlessRootSource>>(
                        &source)) {
                if (!*root || (*root)->sourceContractVersion() != 2u)
                    continue;
                for (const auto& domain : (*root)->ordinaryDomains())
                    if (domain.sourceIndex == index
                        && core::SharedOwnerProjection::sameOwner(domain.source, *descriptor))
                        return true;
            }
        return false;
    }

    const char* typedStateJoinDiagnostic() const noexcept
    {
        for (const auto& source : sources())
            if (domain(source) == CommandReferenceDomain::BindlessRoot)
                return "RHI-ORIGINAL-BINDLESS-ROOT-STATE-JOIN-MISSING";
        for (const auto& source : sources())
            if (domain(source) == CommandReferenceDomain::Pipeline)
                return "RHI-ORIGINAL-TYPED-PIPELINE-STATE-JOIN-MISSING";
        return containsTypedSources() ? "RHI-ORIGINAL-TYPED-DESCRIPTOR-STATE-JOIN-MISSING"
                                      : nullptr;
    }

    const char* cpuOwnerCount(
        std::uint32_t roots, std::uint32_t maxOwners, std::uint32_t& owners) const noexcept
    {
        if (const auto* id = validate())
            return id;
        if (roots > maxOwners)
            return "RHI-ORIGINAL-USAGE-OWNER-CAPACITY";
        auto remaining = maxOwners - roots;
        if (workloadReservation_) {
            constexpr auto retained = OriginalBindlessWorkloadReservation::retainedOwnerCount;
            if (retained > remaining)
                return "RHI-ORIGINAL-USAGE-OWNER-CAPACITY";
            remaining -= retained;
        }
        for (const auto& source : sources()) {
            const auto retained = std::visit(
                [&](const auto& value) {
                    using T =
                        std::remove_const_t<typename std::decay_t<decltype(value)>::element_type>;
                    if constexpr (std::is_same_v<T, OriginalCommandReadbackTextureSource>)
                        return value->completeOwnerCount();
                    else if constexpr (std::is_same_v<T, OriginalCommandTextureReadbackSource>)
                        return retainedReadback_.enabled() ? nestedReadbackOwners(*value)
                                                           : T::retainedOwnerCount;
                    else
                        return T::retainedOwnerCount;
                },
                source);
            if (retained > remaining)
                return "RHI-ORIGINAL-USAGE-OWNER-CAPACITY";
            remaining -= retained;
        }
        std::size_t ownerSlots = OriginalCommandGraphicsPipelineSource::cpuOwnerSlots;
        for (const auto& source : sources())
            if (const auto* descriptor =
                    std::get_if<std::shared_ptr<const OriginalCommandDescriptorSetSource>>(&source))
                if (OriginalCommandDescriptorSetSource::completedDepthVersion(
                        (*descriptor)->sourceContractVersion())) {
                    const auto count = (*descriptor)->frozenElements().size();
                    if (count == SIZE_MAX)
                        return "RHI-ORIGINAL-USAGE-OWNER-CAPACITY";
                    ownerSlots = std::max(ownerSlots, count + 1u);
                }
        const core::RowMajorIndexGrid grid(sources().size(), ownerSlots, SIZE_MAX);
        if (!sources().empty() && !grid.valid())
            return "RHI-ORIGINAL-USAGE-OWNER-CAPACITY";
        const auto arenas = core::SharedIdentityProjection::inspect(
            grid.size(), grid.size(), remaining, [&](std::size_t i) {
                std::uint64_t row = 0u, slot = 0u;
                grid.coordinates(i, row, slot);
                return cpuIdentity(sources()[row], slot);
            });
        if (arenas.status != core::SharedIdentityProjection::Status::Complete)
            return "RHI-ORIGINAL-USAGE-OWNER-CAPACITY";
        owners = maxOwners - remaining + static_cast<std::uint32_t>(arenas.identities);
        return nullptr;
    }

    static const char* validateOriginal(
        const std::shared_ptr<const OriginalCommandResourceSources>& self) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-ORIGINAL-SOURCES-OWNER";
        return self->validate();
    }

    // Logical factory lifetime prerequisite only. Canonical source ownership,
    // full layouts and dependencies are revalidated before checking GPU records.
    // No caller state enum, pending journal or native command proof is inferred.
    static const char* validateRegisteredLifetime(
        const std::shared_ptr<const OriginalCommandResourceSources>& self,
        const GpuDevice& device) noexcept
    {
        if (const auto* id = validateOriginal(self))
            return id;
        if (const auto* id = device.originalResourceRegistrationDiagnostic())
            return id;
        for (const auto& source : self->sources_.values()) {
            const auto* id = std::visit(
                [&](const auto& value) -> const char* {
                    using Lease =
                        std::remove_const_t<typename std::decay_t<decltype(value)>::element_type>;
                    if constexpr (isTypedDomain(Lease::domain))
                        return Lease::validateOriginal(value, device);
                    else if constexpr (std::is_same_v<Lease, OriginalCommandTextureViewSource>) {
                        const auto* parent = value->originalParentLease()->originalObject();
                        const auto* view = value->originalObject();
                        if (const auto* error =
                                device.validateOriginalResourceRegistration(*parent))
                            return error;
                        if (const auto* error = device.validateOriginalResourceRegistration(*view))
                            return error;
                        return view->originalResourceRegistration().matchesParent(
                                   parent->originalResourceRegistration())
                            ? nullptr
                            : "RHI-ORIGINAL-SOURCES-REGISTERED-PARENT";
                    } else if constexpr (std::is_same_v<Lease, OriginalCommandTextureUploadSource>)
                        return device.validateOriginalResourceRegistration(
                            *value->originalDestinationLease()->originalObject());
                    else if constexpr (std::is_same_v<Lease, OriginalCommandBufferUploadSource>) {
                        if (const auto* error = device.validateOriginalResourceRegistration(
                                *value->originalSourceLease()->originalObject()))
                            return error;
                        return device.validateOriginalResourceRegistration(
                            *value->originalDestinationLease()->originalObject());
                    } else if constexpr (std::is_same_v<Lease,
                                             OriginalCommandTextureReadbackSource>) {
                        if (const auto* error = device.validateOriginalResourceRegistration(
                                *value->originalSource()->originalObject()))
                            return error;
                        return device.validateOriginalResourceRegistration(
                            *value->originalDestinationLease()->originalObject());
                    } else
                        return device.validateOriginalResourceRegistration(
                            *value->originalObject());
                },
                source);
            if (id)
                return id;
        }
        return nullptr;
    }

    // Canonical GPU wrappers are already counted by the CPU leases. Add only
    // their unique original records and the fixed registry closure. Required
    // upload/readback/view dependencies occur as canonical sources, not extra
    // registrations. This inspect path allocates no secondary inventory.
    // Accounting alone grants no common-device authority: admissions must also
    // call validateRegisteredLifetime against their exact original device.
    static const char* registeredOwnerUnion(
        const std::shared_ptr<const OriginalCommandResourceSources>& self,
        std::uint32_t cpuOwners,
        std::uint32_t maxOwners,
        std::uint32_t maxRegistrations,
        std::uint32_t& owners,
        std::uint32_t& registrations) noexcept
    {
        if (const auto* id = validateOriginal(self))
            return id;
        for (const auto& source : self->sources_.values()) {
            const auto* id = std::visit(
                [](const auto& value) -> const char* {
                    using Lease =
                        std::remove_const_t<typename std::decay_t<decltype(value)>::element_type>;
                    if constexpr (Lease::domain == CommandReferenceDomain::Upload
                        || Lease::domain == CommandReferenceDomain::Readback)
                        return nullptr;
                    else
                        return value->originalObject()->originalResourceRegistration().diagnostic();
                },
                source);
            if (id)
                return id;
        }
        constexpr auto fixed = OriginalResourceRegistrationContract::fixedOwnerCount;
        if (cpuOwners > maxOwners || fixed > maxOwners - cpuOwners)
            return "RHI-ORIGINAL-STATE-OWNER-CAPACITY";
        const auto count = self->sources_.values().size();
        if (count > SIZE_MAX - count)
            return "RHI-ORIGINAL-STATE-REGISTRATION-CAPACITY";
        const auto records = count + count;
        const auto inventory = core::SharedIdentityProjection::inspect(
            records, records, maxRegistrations, [&](std::size_t i) {
                if (i >= count)
                    return arenaIdentity(self->sources_.values()[i - count]);
                return std::visit(
                    [&](const auto& value) -> core::SharedIdentityProjection::Record {
                        using Lease = std::remove_const_t<
                            typename std::decay_t<decltype(value)>::element_type>;
                        if constexpr (Lease::domain == CommandReferenceDomain::Upload
                            || Lease::domain == CommandReferenceDomain::Readback)
                            return {};
                        else if constexpr (Lease::domain == CommandReferenceDomain::BindlessRoot) {
                            // Both entries retain one original descriptor-set
                            // registration. Use its first canonical Root owner
                            // for GPU-record accounting; CPU entry owners above
                            // remain distinct. No new resource identity exists.
                            auto owner = value;
                            for (std::size_t j = 0u; j < i; ++j)
                                if (const auto* prior = std::get_if<
                                        std::shared_ptr<const OriginalCommandBindlessRootSource>>(
                                        &self->sources()[j]))
                                    if (core::SharedOwnerProjection::sameOwner(
                                            (*prior)->originalSnapshot(),
                                            value->originalSnapshot())) {
                                        owner = *prior;
                                        break;
                                    }
                            return {value->originalObject()
                                        ->originalResourceRegistration()
                                        .generation(),
                                static_cast<std::uint32_t>(Lease::domain),
                                owner,
                                value->originalObject(),
                                2u};
                        } else if constexpr (isTypedDomain(Lease::domain))
                            return {value->originalObject()
                                        ->originalResourceRegistration()
                                        .generation(),
                                static_cast<std::uint32_t>(Lease::domain),
                                value,
                                value->originalObject(),
                                2u};
                        else
                            return {value->resource().value,
                                static_cast<std::uint32_t>(Lease::domain),
                                value,
                                value->originalObject()};
                    },
                    self->sources_.values()[i]);
            });
        if (inventory.status == core::SharedIdentityProjection::Status::IdentityCapacity)
            return "RHI-ORIGINAL-STATE-REGISTRATION-CAPACITY";
        if (inventory.status != core::SharedIdentityProjection::Status::Complete)
            return "RHI-ORIGINAL-STATE-REGISTRATION-ALIAS";
        if (inventory.identities > maxOwners - cpuOwners - fixed)
            return "RHI-ORIGINAL-STATE-OWNER-CAPACITY";
        registrations = static_cast<std::uint32_t>(inventory.identities);
        owners = cpuOwners + fixed + registrations;
        return nullptr;
    }

    const char* validate() const noexcept
    {
        if (stateTransactions_.capacity() != budget_.maxStateTransactions)
            return "RHI-ORIGINAL-SOURCES-TRANSACTION-CAPACITY";
        if (workloadReservation_
            && (!workloadReservation_->hasQuotaBoundSharedOwner(workloadReservation_)
                || workloadReservation_->originalInventory() != this
                || !workloadReservation_->charged()
                || !core::SharedOwnerProjection::sameOwner(
                    originalMetadataQuota(), workloadReservation_->originalMetadataQuota())))
            return "RHI-BINDLESS-WORKLOAD-RETENTION-OWNER";
        if (pendingSubmissions_.capacity() != budget_.maxPendingSubmissions
            || callbackTickets_.capacity() != budget_.maxCallbackTickets
            || callbackRegistrations_.capacity() != budget_.maxCallbackRegistrations)
            return "RHI-ORIGINAL-SOURCES-LIFECYCLE-CAPACITY";
        const auto bytes = budget_.maxCpuBytes[static_cast<std::size_t>(
            OriginalCommandAdmissionBudget::CpuDomain::Source)];
        if (!originalMetadataQuota() || originalMetadataQuota()->limitBytes() != bytes)
            return "RHI-ORIGINAL-SOURCES-QUOTA";
        if (const auto* id = validateSources(sources_.values().data(),
                sources_.values().size(),
                budget_,
                originalMetadataQuota(),
                retainedReadback_))
            return id;
        for (const auto& source : sources()) {
            const auto* id = std::visit(
                [&](const auto& value) -> const char* {
                    using T =
                        std::remove_const_t<typename std::decay_t<decltype(value)>::element_type>;
                    if constexpr (T::domain == CommandReferenceDomain::BindlessRoot)
                        return typedAssembly_ && assemblyVersion_ == 6u
                                && value->matchesCanonicalSources(*this)
                            ? nullptr
                            : "RHI-ORIGINAL-SOURCES-ASSEMBLY-BINDLESS-VERSION";
                    else if constexpr (T::domain == CommandReferenceDomain::Pipeline)
                        return typedAssembly_
                                && (assemblyVersion_ == 2u || assemblyVersion_ == 3u
                                    || assemblyVersion_ == 4u || assemblyVersion_ == 5u
                                    || assemblyVersion_ == 6u)
                            ? nullptr
                            : "RHI-ORIGINAL-SOURCES-ASSEMBLY-PIPELINE-VERSION";
                    else if constexpr (isTypedDomain(T::domain))
                        return typedAssembly_
                                && allowsDescriptorSourceVersion(
                                    assemblyVersion_, value->sourceContractVersion())
                                && value->matchesCanonicalSources(*this)
                            ? nullptr
                            : "RHI-ORIGINAL-SOURCES-ASSEMBLY-FOREIGN-INVENTORY";
                    else
                        return nullptr;
                },
                source);
            if (id)
                return id;
        }
        return nullptr;
    }

    const core::pmr::Vector<Source>& sources() const noexcept { return sources_.values(); }

    // Resolve an exact retained lease in the canonical inventory. Resource IDs
    // or equal layouts cannot substitute for this original shared ownership.
    template <class Lease>
    std::size_t originalLeaseIndex(const std::shared_ptr<const Lease>& required) const noexcept
    {
        return findOriginalLeaseIndex(sources().data(), sources().size(), required);
    }

    const auto& originalBudget() const noexcept { return budget_; }

    const auto& originalWorkloadReservation() const noexcept { return workloadReservation_; }

    const auto& retainedReadbackCorrespondence() const noexcept { return retainedReadback_; }

    std::uint64_t originalStateTransactionCount() const noexcept
    {
        return stateTransactions_.used();
    }

    // Reserved cold units; these counts never report submitted work/completion.
    std::uint64_t originalReservedSubmissionCount() const noexcept
    {
        return pendingSubmissions_.used();
    }

    std::uint64_t originalReservedCallbackTicketCount() const noexcept
    {
        return callbackTickets_.used();
    }

    std::uint64_t originalReservedCallbackRegistrationCount() const noexcept
    {
        return callbackRegistrations_.used();
    }

    static CommandReferenceDomain domain(const Source& source) noexcept
    {
        return std::visit(
            [](const auto& value) {
                using Lease =
                    std::remove_const_t<typename std::decay_t<decltype(value)>::element_type>;
                return Lease::domain;
            },
            source);
    }

    static GpuResourceId resource(const Source& source) noexcept
    {
        return std::visit(
            [](const auto& value) {
                using T = std::remove_const_t<typename std::decay_t<decltype(value)>::element_type>;
                if constexpr (isTypedDomain(T::domain))
                    return GpuResourceId{}; // No ID in this domain; command signatures gate use.
                else
                    return value ? value->resource() : GpuResourceId{};
            },
            source);
    }

    bool nativeExecutionProven() const noexcept { return false; }

private:
    void sealForGraph() const noexcept { sealed_ = true; }

    static core::SharedIdentityProjection::Record cpuIdentity(
        const Source& source, std::size_t slot) noexcept
    {
        return std::visit(
            [&](const auto& value) -> core::SharedIdentityProjection::Record {
                using T = std::remove_const_t<typename std::decay_t<decltype(value)>::element_type>;
                if constexpr (T::domain == CommandReferenceDomain::BindlessRoot) {
                    std::shared_ptr<const void> owner;
                    if (slot == 0u)
                        owner = value->authorization();
                    else if (slot == 1u)
                        owner = value->authorization()->originalPackageOwner();
                    else if (slot == 9u && value->originalSnapshot()->faultLatchBuffer())
                        owner = value->originalSnapshot()->originalSubmissionBudgetOwner();
                    else
                        owner = std::visit(
                            [&](const auto& prepared) -> std::shared_ptr<const void> {
                                const auto& owners = prepared->owners();
                                switch (slot) {
                                case 2u:
                                    return prepared;
                                case 3u:
                                    return owners;
                                case 4u:
                                    return owners->desc().pipeline;
                                case 5u:
                                    return owners->originalCountLease();
                                case 6u:
                                    return owners->originalCountLease()->originalBudgetOwner();
                                case 7u:
                                    return owners->originalByteLease();
                                case 8u:
                                    return owners->originalByteLease()->originalBudgetOwner();
                                default:
                                    return {};
                                }
                            },
                            value->originalPrepared());
                    return owner ? core::SharedIdentityProjection::
                                       Record{static_cast<std::uint64_t>(
                                                  reinterpret_cast<std::uintptr_t>(owner.get())),
                                           1u,
                                           owner,
                                           owner.get(),
                                           3u}
                                 : core::SharedIdentityProjection::Record{};
                } else if constexpr (T::domain == CommandReferenceDomain::Pipeline) {
                    std::shared_ptr<const void> owner =
                        slot == 0u ? std::shared_ptr<const void>(value) : value->cpuOwner(slot);
                    return owner ? core::SharedIdentityProjection::
                                       Record{static_cast<std::uint64_t>(
                                                  reinterpret_cast<std::uintptr_t>(owner.get())),
                                           1u,
                                           owner,
                                           owner.get(),
                                           3u}
                                 : core::SharedIdentityProjection::Record{};
                } else if constexpr (std::is_same_v<T, OriginalCommandBufferLease>
                    || std::is_same_v<T, OriginalCommandTextureLease>
                    || std::is_same_v<T, OriginalCommandSamplerLease>) {
                    // Type erasure is confined to the existing CPU identity
                    // projection; the original resource Source stays typed.
                    std::shared_ptr<const void> owner;
                    if (slot < T::capacityOwnerSlots)
                        owner = slot % 2u
                            ? std::shared_ptr<const void>(value->originalCapacityBudget(slot / 2u))
                            : std::shared_ptr<const void>(value->originalCapacityLease(slot / 2u));
                    return owner ? core::SharedIdentityProjection::
                                       Record{static_cast<std::uint64_t>(
                                                  reinterpret_cast<std::uintptr_t>(owner.get())),
                                           1u,
                                           owner,
                                           owner.get(),
                                           3u}
                                 : core::SharedIdentityProjection::Record{};
                } else if constexpr (std::is_same_v<T, OriginalCommandDescriptorSetSource>) {
                    if (!slot)
                        return arenaIdentity(source);
                    if (!T::completedDepthVersion(value->sourceContractVersion())
                        || slot - 1u >= value->frozenElements().size())
                        return {};
                    const auto& owner = value->frozenElements()[slot - 1u].completedDepth;
                    return owner ? core::SharedIdentityProjection::
                                       Record{static_cast<std::uint64_t>(
                                                  reinterpret_cast<std::uintptr_t>(owner.get())),
                                           1u,
                                           owner,
                                           owner.get(),
                                           3u}
                                 : core::SharedIdentityProjection::Record{};
                } else
                    return slot == 0u ? arenaIdentity(source)
                                      : core::SharedIdentityProjection::Record{};
            },
            source);
    }

    static core::SharedIdentityProjection::Record arenaIdentity(const Source& source) noexcept
    {
        return std::visit(
            [](const auto& value) -> core::SharedIdentityProjection::Record {
                using T = std::remove_const_t<typename std::decay_t<decltype(value)>::element_type>;
                if constexpr (T::domain == CommandReferenceDomain::BindlessRoot) {
                    const auto* arena = value->originalArena();
                    return {arena->originalResourceRegistration().generation(),
                        static_cast<std::uint32_t>(OriginalRegisteredResourceKind::DescriptorArena),
                        value->originalSnapshot(),
                        arena,
                        2u};
                } else if constexpr (T::domain == CommandReferenceDomain::Pipeline) {
                    const auto& layout = value->originalLayout();
                    return {layout->originalResourceRegistration().generation(),
                        static_cast<std::uint32_t>(OriginalRegisteredResourceKind::PipelineLayout),
                        layout,
                        layout.get(),
                        2u};
                } else if constexpr (isTypedDomain(T::domain)) {
                    const auto& arena = value->originalArena();
                    return {arena->originalResourceRegistration().generation(),
                        static_cast<std::uint32_t>(OriginalRegisteredResourceKind::DescriptorArena),
                        arena,
                        arena.get(),
                        2u};
                } else
                    return {};
            },
            source);
    }

    static core::SharedIdentityProjection::Record identity(const Source& source) noexcept
    {
        return std::visit(
            [](const auto& value) -> core::SharedIdentityProjection::Record {
                using Lease =
                    std::remove_const_t<typename std::decay_t<decltype(value)>::element_type>;
                if constexpr (Lease::domain == CommandReferenceDomain::Upload
                    || Lease::domain == CommandReferenceDomain::Readback)
                    // Canonical ownership was checked before projection. This live CPU
                    // input key deduplicates ownership only; it is never a GPU ID.
                    return {value ? static_cast<std::uint64_t>(
                                        reinterpret_cast<std::uintptr_t>(value->originalObject()))
                                  : 0u,
                        static_cast<std::uint32_t>(Lease::domain),
                        value,
                        value.get(),
                        1u};
                else if constexpr (Lease::domain == CommandReferenceDomain::BindlessRoot) {
                    // A Root names an authenticated prepared entry, not only
                    // its shared descriptor-set generation. Two entries may
                    // share a snapshot; duplicate owners of one entry may not.
                    const auto* prepared = value ? std::visit(
                                                       [](const auto& binding) -> const void* {
                                                           return binding.get();
                                                       },
                                                       value->originalPrepared())
                                                 : nullptr;
                    return {static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(prepared)),
                        static_cast<std::uint32_t>(Lease::domain),
                        value,
                        value.get(),
                        3u};
                } else if constexpr (isTypedDomain(Lease::domain))
                    return {value
                            ? value->originalObject()->originalResourceRegistration().generation()
                            : 0u,
                        static_cast<std::uint32_t>(Lease::domain),
                        value,
                        value.get(),
                        2u};
                else
                    return {value ? value->resource().value : 0u,
                        static_cast<std::uint32_t>(Lease::domain),
                        value,
                        value.get()};
            },
            source);
    }

    static const char* validateSources(const Source* sources,
        std::size_t count,
        const OriginalCommandAdmissionBudget& budget,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        const OriginalRetainedReadbackCorrespondence& retainedReadback) noexcept
    {
        if (const auto* id = retainedReadback.validate())
            return id;
        std::uint32_t readbackCount = 0u, nestedOwners = 0u;
        std::array<std::uint32_t, static_cast<std::size_t>(CommandReferenceDomain::Count)> counts{};
        for (std::size_t i = 0u; i < count; ++i) {
            const auto* id = std::visit(
                [&](const auto& value) -> const char* {
                    using Lease =
                        std::remove_const_t<typename std::decay_t<decltype(value)>::element_type>;
                    if (const auto* diagnostic = Lease::validateOriginal(value))
                        return diagnostic;
                    if constexpr (std::is_same_v<Lease, OriginalCommandTextureReadbackSource>) {
                        if (retainedReadback.enabled()) {
                            if (value->contractVersion() != 2u)
                                return "RHI-ORIGINAL-RETAINED-READBACK-SOURCE-PROFILE";
                            if (readbackCount == retainedReadback.maxReadbackSources)
                                return "RHI-ORIGINAL-RETAINED-READBACK-SOURCE-CAPACITY";
                            const auto additional = nestedReadbackOwners(*value);
                            if (additional > retainedReadback.maxNestedOwners - nestedOwners)
                                return "RHI-ORIGINAL-RETAINED-READBACK-OWNER-CAPACITY";
                            ++readbackCount;
                            nestedOwners += additional;
                        }
                    }
                    if constexpr (Lease::domain == CommandReferenceDomain::Upload)
                        if (value->originalInputQuota()->limitBytes()
                            != budget.maxCpuBytes[static_cast<std::size_t>(
                                OriginalCommandAdmissionBudget::CpuDomain::Graph)])
                            return "RHI-ORIGINAL-SOURCES-UPLOAD-GRAPH-QUOTA";
                    if constexpr (Lease::domain == CommandReferenceDomain::Readback
                        || std::is_same_v<Lease, OriginalCommandReadbackTextureSource>)
                        if (value->originalInputQuota()->limitBytes()
                            != budget.maxCpuBytes[static_cast<std::size_t>(
                                OriginalCommandAdmissionBudget::CpuDomain::Graph)])
                            return "RHI-ORIGINAL-SOURCES-READBACK-GRAPH-QUOTA";
                    return core::SharedOwnerProjection::sameOwner(
                               value->originalMetadataQuota(), quota)
                        ? nullptr
                        : "RHI-ORIGINAL-SOURCES-SOURCE-QUOTA";
                },
                sources[i]);
            if (id)
                return id;
            const auto index = static_cast<std::size_t>(domain(sources[i]));
            if (counts[index] == budget.maxSourceOwners[index])
                return "RHI-ORIGINAL-SOURCES-DOMAIN-CAPACITY";
            ++counts[index];
        }
        const auto inventory =
            core::SharedIdentityProjection::inspect(count, count, count, [&](std::size_t i) {
                return identity(sources[i]);
            });
        if (inventory.status != core::SharedIdentityProjection::Status::Complete)
            return "RHI-ORIGINAL-SOURCES-IDENTITY-ALIAS";
        if (inventory.identities != count)
            return "RHI-ORIGINAL-SOURCES-DUPLICATE";
        for (std::size_t i = 0u; i < count; ++i) {
            const auto* id = std::visit(
                [&](const auto& value) -> const char* {
                    using Lease =
                        std::remove_const_t<typename std::decay_t<decltype(value)>::element_type>;
                    if constexpr (std::is_same_v<Lease, OriginalCommandTextureViewSource>) {
                        if (!containsOriginalLease(sources, count, value->originalParentLease()))
                            return "RHI-ORIGINAL-SOURCES-VIEW-PARENT";
                    } else if constexpr (Lease::domain == CommandReferenceDomain::Upload) {
                        if (!containsOriginalLease(
                                sources, count, value->originalDestinationLease()))
                            return "RHI-ORIGINAL-SOURCES-UPLOAD-DESTINATION";
                        if constexpr (std::is_same_v<Lease, OriginalCommandBufferUploadSource>)
                            if (!containsOriginalLease(
                                    sources, count, value->originalSourceLease()))
                                return "RHI-ORIGINAL-SOURCES-UPLOAD-SOURCE";
                    } else if constexpr (Lease::domain == CommandReferenceDomain::Readback) {
                        const bool original = retainedReadback.enabled()
                            ? containsOriginalLease(sources,
                                  count,
                                  value->originalSource()->originalLease()->originalTextureOwner())
                            : containsOriginalLease(sources, count, value->originalSource());
                        if (!original)
                            return "RHI-ORIGINAL-SOURCES-READBACK-SOURCE";
                        if (!containsOriginalLease(
                                sources, count, value->originalDestinationLease()))
                            return "RHI-ORIGINAL-SOURCES-READBACK-DESTINATION";
                    }
                    return nullptr;
                },
                sources[i]);
            if (id)
                return id;
        }
        return nullptr;
    }

    static std::uint32_t nestedReadbackOwners(
        const OriginalCommandTextureReadbackSource& source) noexcept
    {
        // Destination and original texture lease must already exist exactly once
        // in the canonical inventory. Charge all remaining retained metadata.
        return OriginalCommandTextureReadbackSource::retainedOwnerCount
            + source.originalSource()->completeOwnerCount()
            - OriginalCommandTextureLease::retainedOwnerCount;
    }

    template <class Lease>
    static bool containsOriginalLease(const Source* sources,
        std::size_t count,
        const std::shared_ptr<const Lease>& required) noexcept
    {
        return findOriginalLeaseIndex(sources, count, required) != SIZE_MAX;
    }

    template <class Lease>
    static std::size_t findOriginalLeaseIndex(const Source* sources,
        std::size_t count,
        const std::shared_ptr<const Lease>& required) noexcept
    {
        for (std::size_t i = 0u; i < count; ++i) {
            const auto* original = std::get_if<std::shared_ptr<const Lease>>(&sources[i]);
            if (original && core::SharedOwnerProjection::sameOwner(*original, required))
                return i;
        }
        return SIZE_MAX;
    }

    // Declare the capacity token first so it is destroyed last, after all
    // source/resource custody. Concurrent work cannot reuse its reservation
    // while the final canonical resource owners are still being destroyed.
    mutable std::shared_ptr<const OriginalBindlessWorkloadReservation> workloadReservation_;
    core::BoundedSequence<Source> sources_;
    const OriginalCommandAdmissionBudget budget_;
    const bool typedAssembly_;
    mutable bool sealed_;
    mutable core::AtomicCapacityBudget stateTransactions_;
    mutable core::AtomicCapacityBudget pendingSubmissions_, callbackTickets_,
        callbackRegistrations_;
    const std::uint32_t assemblyVersion_;
    const OriginalRetainedReadbackCorrespondence retainedReadback_;
};
} // namespace arp::rhi

// endregion original_command_resource_sources
// endregion Command resource sources

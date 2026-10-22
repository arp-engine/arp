#pragma once
#include "core/math/unsigned_range.h"
#include "rhi/frontend/transfer_readback_input.h"
#include "rhi/frontend/transfer_upload_packing.h"
#include "rhi/semantics/transfer_lowering.h"

namespace arp::rhi::semantics {
// OriginalTextureReadbackContentCoverage/1. Original CPU region comparison
// only; a completed native upload, current source state and map proof are separate.
class TextureReadbackContentCoverage final {
public:
    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint32_t maxReadbackMips = 0u, maxUploadMips = 0u;
    };

    struct Lowering {
        bool metadataEquivalent = false, nativeExecution = false;
        const char* diagnostic = "RHI-TEXTURE-READBACK-COVERAGE-BACKEND-UNKNOWN";
    };

    static constexpr Lowering lowering(Backend backend) noexcept
    {
        return semantics::TransferLowering::TextureReadbackContentCoverage_lowering<Lowering>(
            backend);
    }

    static const char* validateOriginal(
        const std::shared_ptr<const TextureReadbackLayout>& readback,
        const std::shared_ptr<const TextureUploadPacking>& upload,
        Budget budget) noexcept
    {
        if (budget.contractVersion != 1u)
            return "RHI-TEXTURE-READBACK-COVERAGE-VERSION";
        if (!budget.prototypeOptIn)
            return "RHI-TEXTURE-READBACK-COVERAGE-OPT-IN";
        if (!budget.maxReadbackMips || !budget.maxUploadMips)
            return "RHI-TEXTURE-READBACK-COVERAGE-BUDGET";
        if (!readback || !readback->hasQuotaBoundSharedOwner(readback))
            return "RHI-TEXTURE-READBACK-COVERAGE-READBACK-OWNER";
        if (!upload || !upload->hasQuotaBoundSharedOwner(upload))
            return "RHI-TEXTURE-READBACK-COVERAGE-UPLOAD-OWNER";
        ReadbackTextureDesc fields;
        readback->inputOwner()->scalarFields().writeScalarFields(fields);
        if (fields.sourceResource != upload->inputOwner()->scalarFields().destination())
            return "RHI-TEXTURE-READBACK-COVERAGE-SOURCE-ID";
        TextureDesc source;
        readback->sourceLayout().write(source);
        if (!upload->destinationLayout().matches(source))
            return "RHI-TEXTURE-READBACK-COVERAGE-SOURCE-LAYOUT";
        const auto& reads = readback->mipCopies();
        const auto& writes = upload->mipLayouts();
        if (reads.empty() || writes.empty() || reads.size() > budget.maxReadbackMips
            || writes.size() > budget.maxUploadMips)
            return "RHI-TEXTURE-READBACK-COVERAGE-CAPACITY";
        // Both original factories enforce ascending contiguous single-mip records.
        // A merge cursor makes the complete join linear in the independently
        // bounded counts, with no temporary interval list or synthesized union.
        std::size_t cursor = 0u;
        for (const auto& read : reads) {
            while (cursor < writes.size()
                && writes[cursor].range.baseMipLevel < read.range.baseMipLevel)
                ++cursor;
            if (cursor == writes.size()
                || writes[cursor].range.baseMipLevel != read.range.baseMipLevel)
                return "RHI-TEXTURE-READBACK-COVERAGE-MIP-MISSING";
            const auto& write = writes[cursor];
            if (read.range.aspect != write.range.aspect
                || read.range.mipLevelCount != write.range.mipLevelCount
                || !core::containsUnsignedRange(write.range.baseArrayLayer,
                    write.range.arrayLayerCount,
                    read.range.baseArrayLayer,
                    read.range.arrayLayerCount)
                || !core::containsUnsignedRange(
                    write.origin.width, write.extent.width, 0u, read.extent.width)
                || !core::containsUnsignedRange(
                    write.origin.height, write.extent.height, 0u, read.extent.height)
                || !core::containsUnsignedRange(
                    write.origin.depth, write.extent.depth, 0u, read.extent.depth))
                return "RHI-TEXTURE-READBACK-COVERAGE-REGION";
        }
        return nullptr;
    }
};
} // namespace arp::rhi::semantics

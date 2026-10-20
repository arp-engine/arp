#pragma once
#include "core/shared_owner_projection.h"
#include "rhi/frontend/transfer_upload_copy_mapping.h"
#include "rhi/semantics/transfer_lowering.h"

#include <limits>

namespace arp::rhi::semantics {
// OriginalTextureUploadCopyLowering/1. A frozen metadata decision, not native
// authority. The private executor must consume and revalidate this exact value.
class TextureUploadCopyLowering final {
public:
    enum class Outcome { DiagnosticFailure, Prototype };
    enum class Dimension { Unknown, Texture2D, Texture3D };
    enum class Operation { DiagnosticFailure, BufferToTexture };

    struct Decision {
        std::uint32_t contractVersion = 0u;
        Backend backend = Backend::Null;
        Outcome outcome = Outcome::DiagnosticFailure;
        Operation operation = Operation::DiagnosticFailure;
        Dimension dimension = Dimension::Unknown;
        std::shared_ptr<const TextureUploadPacking> packing;
        std::size_t mipIndex = 0u;
        TextureUploadPackedCopy original;
        Extent3D physicalOrigin, physicalExtent;

        bool matches(const Decision& other) const noexcept
        {
            return contractVersion == other.contractVersion && backend == other.backend
                && outcome == other.outcome && operation == other.operation
                && dimension == other.dimension && packing.get() == other.packing.get()
                && core::SharedOwnerProjection::sameOwner(packing, other.packing)
                && mipIndex == other.mipIndex && original.matches(other.original)
                && physicalOrigin == other.physicalOrigin && physicalExtent == other.physicalExtent;
        }
    };

    static const char* lowerOriginal(Backend backend,
        std::uint32_t version,
        bool prototypeOptIn,
        const std::shared_ptr<const TextureUploadPacking>& packing,
        GpuResourceId staging,
        std::size_t mipIndex,
        const TextureUploadPackedCopy& original,
        Decision& output) noexcept
    {
        if (version != 1u)
            return "RHI-TEXTURE-COPY-LOWERING-VERSION";
        if (!prototypeOptIn)
            return "RHI-TEXTURE-COPY-LOWERING-OPT-IN";
        if (const auto* diagnostic = TransferLowering::uploadCopyGate(backend))
            return diagnostic;
        if (const auto* id = TextureUploadPackedCopyMapping::validateOriginal(
                version, prototypeOptIn, packing, staging, mipIndex, original))
            return id;
        const auto& layout = packing->destinationLayout();
        const bool volume = layout.extent.depth > 1u;
        if (original.subresources.mipLevelCount != 1u
            || original.subresources.aspect != TextureAspect::Color
            || (volume ? original.subresources.baseArrayLayer != 0u
                        || original.subresources.arrayLayerCount != 1u
                       : original.origin.depth != 0u))
            return "RHI-TEXTURE-COPY-LOWERING-GEOMETRY";
        const auto block = blockExtent(original.format);
        if (original.extent.width > std::numeric_limits<std::uint32_t>::max() - (block - 1u)
            || original.extent.height > std::numeric_limits<std::uint32_t>::max() - (block - 1u))
            return "RHI-TEXTURE-COPY-LOWERING-PHYSICAL-OVERFLOW";
        Decision value;
        value.contractVersion = version;
        value.backend = backend;
        value.outcome = Outcome::Prototype;
        value.operation = Operation::BufferToTexture;
        value.dimension = volume ? Dimension::Texture3D : Dimension::Texture2D;
        value.packing = packing;
        value.mipIndex = mipIndex;
        value.original = original;
        value.physicalOrigin = {original.origin.width,
            original.origin.height,
            volume ? original.origin.depth : original.subresources.baseArrayLayer};
        value.physicalExtent = {blocksForExtent(original.extent.width, block) * block,
            blocksForExtent(original.extent.height, block) * block,
            volume ? original.extent.depth : original.subresources.arrayLayerCount};
        output = std::move(value);
        return nullptr;
    }

    static const char* validateOriginal(Backend backend,
        std::uint32_t version,
        bool prototypeOptIn,
        const std::shared_ptr<const TextureUploadPacking>& packing,
        GpuResourceId staging,
        std::size_t mipIndex,
        const TextureUploadPackedCopy& original,
        const Decision& decision) noexcept
    {
        Decision expected;
        if (const auto* id = lowerOriginal(
                backend, version, prototypeOptIn, packing, staging, mipIndex, original, expected))
            return id;
        return expected.matches(decision) ? nullptr : "RHI-TEXTURE-COPY-LOWERING-DECISION-MISMATCH";
    }
};
} // namespace arp::rhi::semantics

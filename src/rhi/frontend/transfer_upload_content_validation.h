#pragma once
#include "rhi/frontend/transfer_upload_input.h"
#include "rhi/frontend/transfer_upload_layout_validation.h"

namespace arp::rhi::semantics {
// One whole-texture generation can only describe a complete exact upload.
// Partial regions need a versioned domain contract and deterministically refuse.
class OriginalTextureContentUploadValidation final {
public:
    static const char* inspect(
        const TextureUploadInputSnapshot& input, const TextureDesc& destination) noexcept
    {
        const auto& scalar = input.scalarFields();
        if (scalar.range() != defaultTextureSubresourceRange(destination))
            return "RHI-CANONICAL-CONTENT-UPLOAD-WHOLE-RANGE";
        const auto destinationLayout = TextureResourceLayout::capture(destination);
        TextureUploadLayoutValidation::Mapper mapper(input, destinationLayout);
        for (std::size_t i = 0u; i < TextureUploadLayoutValidation::mipCount(input); ++i) {
            TextureUploadPackedMip mip;
            if (!mapper(i, mip))
                return mapper.diagnostic();
            Extent3D full;
            if (const auto* id = TextureSubresourceExtent::resolve(
                    destination.extent, destination.mipLevelCount, mip.range.baseMipLevel, full))
                return id;
            if (mip.origin != Extent3D{0u, 0u, 0u} || mip.extent != full)
                return "RHI-CANONICAL-CONTENT-UPLOAD-WHOLE-EXTENT";
        }
        return nullptr;
    }
};
} // namespace arp::rhi::semantics

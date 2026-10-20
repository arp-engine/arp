#pragma once
#include "rhi/backends/webgpu/webgpu_transfer_retention.h"
#include "rhi/frontend/transfer_upload_packing.h"

namespace arp::rhi_webgpu {
// OriginalCompletedTextureUploadView/1. The actual final upload transaction is
// the sole constructible implementation. Conversion retains its original graph;
// it does not prove completion or grant resource access/copy/map authority.
class WebGpuCompletedTextureUploadDeclaration : public core::QuotaBoundSharedObjectIdentity {
    friend class WebGpuOriginalTextureUploadTransaction;
    WebGpuCompletedTextureUploadDeclaration() = default;
    WebGpuCompletedTextureUploadDeclaration(
        const WebGpuCompletedTextureUploadDeclaration&) = delete;
    WebGpuCompletedTextureUploadDeclaration& operator=(
        const WebGpuCompletedTextureUploadDeclaration&) = delete;

public:
    virtual ~WebGpuCompletedTextureUploadDeclaration() = default;

    static const char* validateOriginal(
        const std::shared_ptr<const WebGpuCompletedTextureUploadDeclaration>& self) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-WEBGPU-TEXTURE-COMPLETED-OWNER";
        return self->validateCompletedDeclaration();
    }

    virtual const rhi::GpuTexture* completedTexture() const noexcept = 0;
    virtual const WebGpuTransferResourceObservation& completedTextureResource() const noexcept = 0;
    virtual const std::shared_ptr<WebGpuRhiResourceRegistry>&
    completedRegistry() const noexcept = 0;
    virtual const std::shared_ptr<const rhi::TextureUploadPacking>&
    completedPacking() const noexcept = 0;
    virtual std::uint64_t retainedContentBytes() const noexcept = 0;
    virtual std::uint64_t retainedNativeAllocationBytes() const noexcept = 0;
    virtual std::size_t retainedNativeAllocationRecordCount() const noexcept = 0;
    virtual core::SharedAllocationProjection::Record retainedNativeAllocationRecordAt(
        std::size_t index) const noexcept = 0;
    virtual const WebGpuTransferResourceObservation* retainedNativeResourceAt(
        std::size_t index) const noexcept = 0;

private:
    virtual const char* validateCompletedDeclaration() const noexcept = 0;
};
} // namespace arp::rhi_webgpu

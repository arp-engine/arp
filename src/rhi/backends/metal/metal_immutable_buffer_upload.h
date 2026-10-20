#pragma once

#include "rhi/backends/metal/metal_sealed_buffer_upload.h"

namespace arp::rhi_metal {

// Shared cold backend utility. Returned storage is not command authorization;
// immutable prepared roots must retain/revalidate its canonical identity.
class MetalRhiImmutableBufferUpload final {
public:
    using Diagnostics = MetalRhiSealedBufferUpload::Diagnostics;
    using Result = MetalRhiSealedBufferUpload::Result;
    static std::optional<Result> prepare(rhi::GpuDevice&,
        const std::shared_ptr<MetalRhiResourceRegistry>&,
        const std::shared_ptr<void>& nativeDevice,
        const core::ByteBuffer&,
        rhi::ResourceUsage,
        const Diagnostics&,
        core::pmr::String&);
    static const char* validate(const std::shared_ptr<rhi::GpuBuffer>&,
        const std::shared_ptr<MetalRhiResourceRegistry>&,
        const MetalRhiRegisteredResource&,
        const MetalRhiNativeResourceObservation&,
        const std::shared_ptr<void>& nativeDevice,
        std::uint64_t exactBytes,
        rhi::ResourceUsage,
        const Diagnostics&) noexcept;

private:
    static bool domain(rhi::ResourceUsage, MetalRhiSealedBufferUpload::Domain&) noexcept;
};
} // namespace arp::rhi_metal

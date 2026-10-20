#pragma once
#include "rhi/backends/metal/metal_backend_objects.h"
#include "rhi/gpu_device.h"

#include <optional>

namespace arp::rhi_metal {
// Cold canonical storage algorithm, not privileged writer authorization.
class MetalRhiSealedBufferUpload final {
public:
    enum class Domain {
        ImmutableIndex,
        ImmutableIndirect,
        SuppressedGpuIndirect,
        ImmutableStorage,
        ImmutableUniform,
        ProtectedPacketArgs,
        ProtectedPacketMapping,
        ProtectedPacketParameters,
        ProtectedScratchArgs,
        ProtectedScratchMapping,
        ImmutableVertex
    };

    struct Diagnostics {
        const char* contentMismatch;
        const char* writesNotSealed;
        const char* resourceMismatch;
        const char* nativeIdentityMismatch;
        const char* creationFailed;
        const char* uploadFailed;
    };

    struct Result {
        std::shared_ptr<rhi::GpuBuffer> buffer;
        MetalRhiRegisteredResource resource;
    };

    static std::optional<Result> prepare(rhi::GpuDevice&,
        const std::shared_ptr<MetalRhiResourceRegistry>&,
        const std::shared_ptr<void>& nativeDevice,
        const core::ByteBuffer&,
        Domain,
        const Diagnostics&,
        core::pmr::String&);
    static const char* validate(const std::shared_ptr<rhi::GpuBuffer>&,
        const std::shared_ptr<MetalRhiResourceRegistry>&,
        const MetalRhiRegisteredResource&,
        const MetalRhiNativeResourceObservation&,
        const std::shared_ptr<void>& nativeDevice,
        std::uint64_t exactBytes,
        Domain,
        const Diagnostics&) noexcept;
    static const char* validateRegistered(const std::shared_ptr<rhi::GpuBuffer>&,
        const std::shared_ptr<MetalRhiResourceRegistry>&,
        const MetalRhiRegisteredResource&,
        const MetalRhiNativeResourceObservation&,
        const std::shared_ptr<void>& nativeDevice,
        const rhi::BufferDesc&,
        MetalRhiNativeStorageMode,
        bool gpuSealed,
        bool transferSealed,
        const Diagnostics&) noexcept;

private:
    static bool policy(Domain, rhi::ResourceUsage&, rhi::ResourceState&) noexcept;
};
} // namespace arp::rhi_metal

#pragma once
#include "rhi/backends/webgpu/webgpu_native_payloads.h"

namespace arp::rhi_webgpu {
// OriginalReadbackBufferCreation/1: frozen descriptor + preallocated CPU
// envelope, not an executable resource. Native payload is unavailable until
// the exact-device creation attempt succeeds; validation scopes remain owned
// by the original readback destination and are the only creation proof.
class WebGpuNativeBufferCreation final : public core::QuotaBoundSharedObjectIdentity {
    class ConstructionKey {
        friend class WebGpuNativeBufferCreation;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    WebGpuNativeBufferCreation(ConstructionKey, const rhi::BufferDesc& desc, void* device)
        : desc_{core::pmr::String(desc.label, &core::defaultCpuMemoryResource()),
              desc.sizeBytes,
              desc.usage,
              desc.memory,
              desc.initialState,
              desc.hostVisibleDeviceLocal},
          device_(device),
          candidate_(core::makeShared<WebGpuNativeBufferPayload>(desc.initialState))
    {
        candidate_->sizeBytes = desc.sizeBytes;
    }

    static std::shared_ptr<WebGpuNativeBufferCreation> prepareReadbackOriginal(
        const rhi::BufferDesc&,
        void* exactDevice,
        const std::shared_ptr<core::CpuMemoryQuota>&,
        const char*& diagnostic);
    static const char* initialize(const std::shared_ptr<WebGpuNativeBufferCreation>&) noexcept;

    const auto& nativePayload() const noexcept { return ready_ ? candidate_ : empty_; }

    void* nativeDevice() const noexcept { return device_; }

private:
    const rhi::BufferDesc desc_;
    void* const device_;
    const std::shared_ptr<WebGpuNativeBufferPayload> candidate_;
    const std::shared_ptr<WebGpuNativeBufferPayload> empty_;
    bool attempted_ = false, ready_ = false;
};
} // namespace arp::rhi_webgpu

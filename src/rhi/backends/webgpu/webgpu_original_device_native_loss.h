#pragma once
#include "core/runtime/external_callback_gate.h"
#include "rhi/backends/webgpu/webgpu_backend_objects.h"
#include "rhi/backends/webgpu/webgpu_native_payloads.h"
#include "rhi/backends/webgpu/webgpu_native_runtime.h"
#include "rhi/backends/webgpu/webgpu_pipeline_preparation.h"
#include "rhi/frontend/resource_device_lifetime.h"
#include "rhi/frontend/resource_device_native_loss.h"
#include "rhi/frontend/resource_device_validation.h"

namespace arp::test {
class OriginalObservedNativeLossBindingFixture;
}

namespace arp::rhi_webgpu {
// Only a captured native device and a device-issued sealed receiver can bind.
// The native callback lease is an alias of this one quota-bound metadata owner.
class WebGpuOriginalObservedNativeLoss;

class WebGpuOriginalDeviceNativeLoss : public rhi::OriginalDeviceNativeLossBinding {
    friend class WebGpuOriginalObservedNativeLoss;
    friend class arp::test::OriginalObservedNativeLossBindingFixture;

    class ConstructionKey {
        friend class WebGpuOriginalDeviceNativeLoss;
        friend class WebGpuOriginalObservedNativeLoss;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

    using Phase = core::runtime::ExternalCallbackGate::Phase;

public:
    WebGpuOriginalDeviceNativeLoss(ConstructionKey,
        const rhi::OriginalDeviceLifetime::NativeLossReceiver& receiver,
        const rhi::OriginalDeviceNativeLossBudget& budget)
        : receiver_(receiver), budget_(budget)
    {}

    ~WebGpuOriginalDeviceNativeLoss() override
    {
        releaseWebGpuNativeDeviceLossRegistration(nativeState_);
    }

    static std::shared_ptr<rhi::OriginalDeviceNativeLossBinding> prepare(void* device,
        const rhi::OriginalDeviceLifetime::NativeLossReceiver& receiver,
        const rhi::OriginalDeviceNativeLossBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        if ((diagnostic = rhi::OriginalDeviceNativeLossContract::validateIntent(
                 budget, quota ? quota->limitBytes() : 0u)))
            return {};
        if ((diagnostic = receiver.diagnostic()))
            return {};
        if (!device) {
            diagnostic = "RHI-ORIGINAL-DEVICE-LOSS-NATIVE-SOURCE-UNAVAILABLE";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope scope(quota);
            auto binding = shareOriginal(core::makeUnique<WebGpuOriginalDeviceNativeLoss>(
                ConstructionKey{}, receiver, budget));
            if ((diagnostic = publishNative(device, binding)))
                return {};
            return binding;
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-ORIGINAL-DEVICE-LOSS-ALLOCATION";
            return {};
        }
    }

    const char* diagnostic() const noexcept override
    {
        if (const auto* id = receiver_.diagnostic())
            return id;
        if (const auto* id = rhi::OriginalDeviceNativeLossContract::validateIntent(
                budget_, originalMetadataQuota() ? originalMetadataQuota()->limitBytes() : 0u))
            return id;
        if (gate_.phase() != Phase::Armed || !nativeState_ || !nativeCallbackLease_)
            return "RHI-ORIGINAL-DEVICE-LOSS-RETIRED";
        return nullptr;
    }

    const char* detach() noexcept override
    {
        if (const auto* id = beforeDetach())
            return id;
        const auto lease = holdNativeLease();
        if (const auto* id = suppressNativeBorrow())
            return id;
        afterDetach();
        return nullptr;
    }

protected:
    static const char* publishNative(
        void* device, const std::shared_ptr<WebGpuOriginalDeviceNativeLoss>& binding) noexcept
    {
        binding->gate_.arm();
        binding->nativeState_ =
            prepareWebGpuNativeDeviceLossRegistration(device, binding.get(), &deliver);
        if (!binding->nativeState_) {
            binding->gate_.detach();
            return "RHI-ORIGINAL-DEVICE-LOSS-NATIVE-BINDING";
        }
        // Derived storage and shared control must already exist. No fallible
        // work may follow native userdata publication.
        binding->nativeCallbackLease_ = binding;
        return nullptr;
    }

    std::shared_ptr<WebGpuOriginalDeviceNativeLoss> holdNativeLease() const noexcept
    {
        return nativeCallbackLease_;
    }

    bool nativeBorrowArmed() const noexcept { return gate_.phase() == Phase::Armed; }

    const char* suppressNativeBorrow() const noexcept
    {
        if (gate_.phase() != Phase::Armed)
            return "RHI-ORIGINAL-DEVICE-LOSS-RETIRED";
        if (!detachWebGpuNativeDeviceLossRegistration(nativeState_))
            return "RHI-ORIGINAL-DEVICE-LOSS-NATIVE-DETACH";
        gate_.detach();
        releaseWebGpuNativeDeviceLossRegistration(nativeState_);
        nativeState_ = 0u;
        nativeCallbackLease_.reset();
        return nullptr;
    }

    virtual const char* beforeDetach() const noexcept { return nullptr; }

    virtual void afterDetach() noexcept {}

    virtual void afterNativeLoss() noexcept {}

private:
    static void deliver(void* userdata) noexcept
    {
        auto* binding = static_cast<WebGpuOriginalDeviceNativeLoss*>(userdata);
        if (!binding || !binding->gate_.beginDelivery())
            return;
        auto lease = std::move(binding->nativeCallbackLease_);
        binding->receiver_.deliverNativeLoss();
        binding->afterNativeLoss();
        binding->gate_.finishDelivery();
        releaseWebGpuNativeDeviceLossRegistration(binding->nativeState_);
        binding->nativeState_ = 0u;
    }

    const rhi::OriginalDeviceLifetime::NativeLossReceiver receiver_;
    const rhi::OriginalDeviceNativeLossBudget budget_;
    mutable core::runtime::ExternalCallbackGate gate_;
    mutable std::uintptr_t nativeState_ = 0u;
    mutable std::shared_ptr<WebGpuOriginalDeviceNativeLoss> nativeCallbackLease_;
};

// Stable backend-owned source captured at actual L2 device construction. Device
// orchestration never imports or releases native handles itself.
class WebGpuOriginalDeviceLossSource final {
public:
    WebGpuOriginalDeviceLossSource();

    ~WebGpuOriginalDeviceLossSource()
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        if (device_)
            wgpuDeviceRelease(static_cast<WGPUDevice>(device_));
#endif
    }

    WebGpuOriginalDeviceLossSource(const WebGpuOriginalDeviceLossSource&) = delete;

    std::shared_ptr<rhi::OriginalDeviceNativeLossBinding> prepare(
        const rhi::OriginalDeviceLifetime::NativeLossReceiver& receiver,
        const rhi::OriginalDeviceNativeLossBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic) const
    {
        return WebGpuOriginalDeviceNativeLoss::prepare(
            device_, receiver, budget, std::move(quota), diagnostic);
    }

    std::shared_ptr<rhi::OriginalDeviceNativeLossBinding> prepareObserved(
        const rhi::OriginalDeviceLifetime::NativeLossReceiver&,
        const rhi::OriginalDeviceNativeLossBudget&,
        const rhi::OriginalDeviceNativeLossObserverRequest&,
        std::shared_ptr<core::CpuMemoryQuota>,
        const char*&) const;

    const char* validate(const rhi::GpuBuffer& buffer) const noexcept
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        const auto payload = webGpuNativePayloadFor(buffer);
        if (payload && payload->buffer && payload->ownerDevice == device_)
            return nullptr;
#endif
        return "RHI-ORIGINAL-DEVICE-LOSS-RESOURCE-NATIVE-SOURCE";
    }

    const char* validate(const rhi::GpuTexture& texture) const noexcept
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        const auto payload = webGpuNativePayloadFor(texture);
        if (payload && payload->texture && payload->ownerDevice == device_)
            return nullptr;
#endif
        return "RHI-ORIGINAL-DEVICE-LOSS-RESOURCE-NATIVE-SOURCE";
    }

    const char* validate(const rhi::GpuTextureView& view) const noexcept
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        const auto payload = webGpuNativePayloadFor(view);
        if (payload && payload->view && payload->parentTexture
            && payload->parentTexture->ownerDevice == device_)
            return nullptr;
#endif
        return "RHI-ORIGINAL-DEVICE-LOSS-RESOURCE-NATIVE-SOURCE";
    }

    const char* validate(const rhi::ComputePipeline& pipeline) const noexcept
    {
        const auto payload = webGpuNativePayloadFor(pipeline);
        const auto* issued = payload ? payload->issuedContract().get() : nullptr;
        const auto* id = issued && issued->semantics().contractVersion == 2u
            ? WebGpuOriginalArtifactComputeFactory::validateConsumedTableExpansion(payload)
            : WebGpuOriginalArtifactComputeFactory::validateConsumedOriginal(payload);
        if (id)
            return id;
        return device_ && payload->issuedContract()->nativeDevice() == device_
            ? nullptr
            : "RHI-ORIGINAL-TYPED-LOSS-PIPELINE-NATIVE-SOURCE";
    }

    const char* validate(const rhi::GpuSampler& sampler) const noexcept
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        const auto payload = webGpuNativePayloadFor(sampler);
        if (device_ && payload && payload->sampler && payload->ownerDevice == device_)
            return nullptr;
#endif
        return "RHI-ORIGINAL-SAMPLER-LOSS-NATIVE-SOURCE";
    }

    const char* validate(const rhi::RenderPipeline& pipeline) const noexcept
    {
        const auto payload = webGpuNativePayloadFor(pipeline);
        const auto* issued = payload ? payload->issuedContract().get() : nullptr;
        const auto* id = issued && issued->semantics().contractVersion == 2u
            ? WebGpuOriginalArtifactGraphicsFactory::validateConsumedTableExpansion(payload)
            : WebGpuOriginalArtifactGraphicsFactory::validateConsumedOriginal(payload);
        if (id)
            return id;
        return device_ && payload->issuedContract()->nativeDevice() == device_
            ? nullptr
            : "RHI-ORIGINAL-TYPED-LOSS-PIPELINE-NATIVE-SOURCE";
    }

private:
    void* device_ = nullptr;
};
} // namespace arp::rhi_webgpu

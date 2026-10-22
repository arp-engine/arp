#pragma once
#include "core/ownership/external_object_slot.h"
#include "core/runtime/completion_evidence_batch.h"
#include "core/text/utf8.h"
#include "rhi/backends/webgpu/webgpu_native_error_scope_batch.h"
#include "rhi/backends/webgpu/webgpu_native_string_view.h"
#include "rhi/frontend/query_creation.h"
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
#include <emscripten/emscripten.h>
#endif

namespace arp::rhi_webgpu {
struct WebGpuOriginalQueryOwnerBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxQuerySets = 0u, maxDeviceOwners = 0u, maxQuerySlots = 0u, maxCallbacks = 0u,
                  maxRetainedScopeIds = 0u;
    std::uint64_t maxRetainedLabelBytes = 0u, maxCpuOwnerBytes = 0u, maxCpuCallbackBytes = 0u;
};

// Backend-private, externally serialized cold owner. No recording/result authority.
class WebGpuOriginalQueryOwner final : public core::QuotaBoundSharedObjectIdentity {
    class ConstructionKey {
        friend class WebGpuOriginalQueryOwner;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

    struct Callback {
        std::shared_ptr<WebGpuOriginalQueryOwner> owner;
        std::uint32_t participant = 0u;
    };

public:
    using Lowering = rhi::semantics::OriginalQueryCreationLowering;
    static constexpr std::uint32_t callbackCount = WebGpuNativeErrorScopeBatch::count;
    enum class Status { Prepared, Pending, CreationReady, Rejected, DeviceLost };

    WebGpuOriginalQueryOwner(ConstructionKey, Lowering::Decision decision)
        : decision_(std::move(decision)), device_(&releaseDevice), query_(&releaseQuery)
    {}

    static rhi::semantics::OriginalQueryCreationFacts nativeFacts() noexcept
    {
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        return {};
#else
        if (webGpuNativeDeviceLost())
            return {};
        const auto mask = static_cast<std::uint32_t>(EM_ASM_INT({
            // clang-format off
            const d = Module.preinitializedWebGPUDevice;
            const q = typeof GPUQuerySet !== "undefined" ? GPUQuerySet.prototype : null;
            const count = q ? Object.getOwnPropertyDescriptor(q, "count") : null;
            const type = q ? Object.getOwnPropertyDescriptor(q, "type") : null;
            return (d && d.features.has("timestamp-query") ? 1 : 0) |
                   (d && typeof d.createQuerySet === "function" ? 2 : 0) |
                   (d && typeof d.pushErrorScope === "function" ? 4 : 0) |
                   (d && typeof d.popErrorScope === "function" ? 8 : 0) |
                   (count && typeof count.get === "function" ? 16 : 0) |
                   (type && typeof type.get === "function" ? 32 : 0) |
                   (q && typeof q.destroy === "function" ? 64 : 0);
            // clang-format on
        }));
        return {bool(mask & 1u),
            bool(mask & 2u),
            bool(mask & 4u),
            bool(mask & 8u),
            bool(mask & 16u),
            bool(mask & 32u),
            bool(mask & 64u)};
#endif
    }

    static const char* validateBudget(const WebGpuOriginalQueryOwnerBudget& b) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-QUERY-CREATION-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-QUERY-CREATION-OPT-IN";
        if (!b.maxQuerySets || !b.maxDeviceOwners || !b.maxQuerySlots
            || b.maxCallbacks < callbackCount || !b.maxCpuOwnerBytes || !b.maxCpuCallbackBytes)
            return "RHI-QUERY-CREATION-BUDGET";
        return nullptr;
    }

    static std::shared_ptr<WebGpuOriginalQueryOwner> prepareOriginal(
        std::shared_ptr<const rhi::QueryPoolInputSnapshot> input,
        const WebGpuOriginalQueryOwnerBudget& b,
        std::shared_ptr<core::CpuMemoryQuota> ownerQuota,
        std::shared_ptr<core::CpuMemoryQuota> callbackQuota,
        const char*& diagnostic)
    {
        diagnostic = validateBudget(b);
        if (diagnostic)
            return {};
        if (!ownerQuota || ownerQuota->limitBytes() != b.maxCpuOwnerBytes || !callbackQuota
            || callbackQuota->limitBytes() != b.maxCpuCallbackBytes) {
            diagnostic = "RHI-QUERY-CREATION-QUOTA";
            return {};
        }
        if (webGpuNativeDeviceLost()) {
            diagnostic = "RHI-WEBGPU-QUERY-CREATION-DEVICE-LOST";
            return {};
        }
        const auto decision = Lowering::decide(
            rhi::Backend::WebGPU, b.contractVersion, b.prototypeOptIn, input, nativeFacts());
        diagnostic = decision.diagnostic();
        if (!decision.creationEligible())
            return {};
        if (input->scalarFields().count() > b.maxQuerySlots
            || input->scopeIds().size() > b.maxRetainedScopeIds
            || input->label().size() > b.maxRetainedLabelBytes) {
            diagnostic = "RHI-QUERY-CREATION-CAPACITY";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope owner(ownerQuota);
            auto object = core::makeUnique<WebGpuOriginalQueryOwner>(ConstructionKey{}, decision);
            {
                core::CpuMemoryQuotaScope callbacks(callbackQuota);
                for (std::uint32_t i = 0u; i < callbackCount; ++i) {
                    object->callbacks_[i] = core::makeUnique<Callback>();
                    object->callbacks_[i]->participant = i;
                }
            }
            return shareOriginal(std::move(object));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-QUERY-CREATION-ALLOCATION";
            return {};
        }
    }

    static const char* initialize(const std::shared_ptr<WebGpuOriginalQueryOwner>& self) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-QUERY-CREATION-OWNER";
        if (webGpuNativeDeviceLost())
            return "RHI-WEBGPU-QUERY-CREATION-DEVICE-LOST";
        if (self->status_ != Status::Prepared)
            return "RHI-QUERY-CREATION-START-STATUS";
        // Re-query actual facts: the cold decision is not permission to ignore loss
        // or a changed live API, and callers cannot inject a synthetic decision.
        const auto current =
            Lowering::decide(rhi::Backend::WebGPU, 1u, true, self->originalInput(), nativeFacts());
        if (!current.creationEligible())
            return current.diagnostic();
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        return "RHI-QUERY-CREATION-NATIVE-UNAVAILABLE";
#else
        auto device = emscripten_webgpu_get_device();
        if (!device || !self->device_.adopt(device)) {
            if (device)
                wgpuDeviceRelease(device);
            return "RHI-QUERY-CREATION-NATIVE-DEVICE";
        }
        if (!wgpuDeviceHasFeature(device, WGPUFeatureName_TimestampQuery)) {
            self->reject("RHI-WEBGPU-QUERY-CREATION-FEATURE");
            return self->failure_;
        }
        self->status_ = Status::Pending;
        WebGpuNativeErrorScopeBatch::push(device);
        auto desc = WGPU_QUERY_SET_DESCRIPTOR_INIT;
        desc.label = webGpuNativeStringView(self->originalInput()->label());
        desc.type = WGPUQueryType_Timestamp;
        desc.count = self->originalInput()->scalarFields().count();
        auto query = wgpuDeviceCreateQuerySet(device, &desc);
        if (!query || !self->query_.adopt(query)) {
            if (query)
                releaseQuery(query);
            self->reject("RHI-QUERY-CREATION-NATIVE-CREATE");
        }
        WebGpuNativeErrorScopeBatch::pop(device, self, self->callbacks_, 0u, &completeScope);
        return self->failure_;
#endif
    }

    Status status() const noexcept
    {
        return webGpuNativeDeviceLost() ? Status::DeviceLost : status_;
    }

    const std::shared_ptr<const rhi::QueryPoolInputSnapshot>& originalInput() const noexcept
    {
        return decision_.originalInput();
    }

    const auto& decision() const noexcept { return decision_; }

    bool creationScopesObserved() const noexcept { return evidence_.allReceived(); }

    bool creationScopeObserved(std::uint32_t participant) const noexcept
    {
        return evidence_.hasReceived(participant);
    }

    rhi::QueryExecutionState queryState() const noexcept
    {
        return rhi::QueryExecutionState::NotRecorded;
    }

    bool resultAvailable() const noexcept { return false; }

    const char* executionDiagnostic() const noexcept
    {
        return "RHI-QUERY-CREATION-EXECUTION-UNPROVEN";
    }

    static const char* validateReadyOriginal(
        const std::shared_ptr<const WebGpuOriginalQueryOwner>& self) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-QUERY-CREATION-OWNER";
        return self->validate();
    }

    const char* validate() const noexcept
    {
        switch (status()) {
        case Status::Prepared:
            return "RHI-QUERY-CREATION-UNINITIALIZED";
        case Status::Pending:
            return "RHI-QUERY-CREATION-PENDING";
        case Status::CreationReady:
            if (!evidence_.proven())
                return "RHI-QUERY-CREATION-EVIDENCE";
            return validateNativeIdentity();
        case Status::Rejected:
            return failure_;
        case Status::DeviceLost:
            return "RHI-WEBGPU-QUERY-CREATION-DEVICE-LOST";
        }
        return "RHI-QUERY-CREATION-STATUS";
    }

private:
    friend class WebGpuOriginalQuerySlots;

    void reject(const char* id) noexcept
    {
        if (!failure_)
            failure_ = id;
        status_ = Status::Rejected;
    }

    const char* validateNativeIdentity() const noexcept
    {
        if (!decision_.matchesOriginal(originalInput()))
            return "RHI-QUERY-CREATION-INPUT";
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        return "RHI-QUERY-CREATION-NATIVE-UNAVAILABLE";
#else
        const auto query = static_cast<WGPUQuerySet>(query_.get());
        const auto device = static_cast<WGPUDevice>(device_.get());
        if (!query || !device)
            return "RHI-QUERY-CREATION-NATIVE-IDENTITY";
        const bool liveDevice = EM_ASM_INT(
                                    {
                                        // clang-format off
                                        return WebGPU.getJsObject(Number($0)) === Module.preinitializedWebGPUDevice;
                                        // clang-format on
                                    },
                                    device)
            != 0;
        if (!liveDevice)
            return "RHI-QUERY-CREATION-NATIVE-DEVICE";
        if (wgpuQuerySetGetType(query) != WGPUQueryType_Timestamp
            || wgpuQuerySetGetCount(query) != originalInput()->scalarFields().count())
            return "RHI-QUERY-CREATION-NATIVE-LAYOUT";
        return nullptr;
#endif
    }

    void observe(std::uint32_t participant, bool proven) noexcept
    {
        const bool accepted = evidence_.observe(participant, proven);
        if (status() != Status::Pending)
            return;
        if (!accepted || !proven) {
            reject("RHI-QUERY-CREATION-NATIVE-SCOPE");
            return;
        }
        if (!evidence_.allReceived())
            return;
        if (const auto* id = validateNativeIdentity()) {
            reject(id);
            return;
        }
        if (!evidence_.proven()) {
            reject("RHI-QUERY-CREATION-EVIDENCE");
            return;
        }
        status_ = Status::CreationReady;
    }

    static void releaseQuery(void* object) noexcept
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        // Destroy is valid after loss; release drops our still-owned bridge ref.
        // Device loss closes use, it does not revoke this ownership obligation.
        auto q = static_cast<WGPUQuerySet>(object);
        wgpuQuerySetDestroy(q);
        wgpuQuerySetRelease(q);
#endif
    }

    static void releaseDevice(void* object) noexcept
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        wgpuDeviceRelease(static_cast<WGPUDevice>(object));
#endif
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void completeScope(WGPUPopErrorScopeStatus status,
        WGPUErrorType type,
        WGPUStringView,
        void* data,
        void*) noexcept
    {
        auto* original = static_cast<Callback*>(data);
        core::UniquePtr<Callback> callback(original,
            core::CpuAllocatedDeleter<Callback>{
                original, &core::destroyCpuAllocatedUnique<Callback>});
        if (callback && callback->owner)
            callback->owner->observe(callback->participant,
                status == WGPUPopErrorScopeStatus_Success && type == WGPUErrorType_NoError);
    }
#endif
    const Lowering::Decision decision_;
    core::ExternalObjectSlot device_, query_;
    std::array<core::UniquePtr<Callback>, callbackCount> callbacks_;
    core::runtime::CompletionEvidenceBatch evidence_{callbackCount};
    // Exclusive original slot graph, never native completion or availability.
    mutable core::runtime::CompletionSignal slotReservation_;
    Status status_ = Status::Prepared;
    const char* failure_ = nullptr;
};
} // namespace arp::rhi_webgpu

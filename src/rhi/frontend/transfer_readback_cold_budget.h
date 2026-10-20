#pragma once
#include "core/atomic_capacity.h"
#include "core/runtime/external_callback_gate.h"
#include "core/strided_index_range.h"
#include "rhi/frontend/resource_device_loss_registration.h"
#include "rhi/frontend/transfer_readback_command_graph_budget.h"
#include "rhi/frontend/transfer_readback_input.h"
#include "rhi/semantics/transfer_lowering.h"

#include <array>

namespace arp::rhi {
struct TextureReadbackColdBudget {
    TextureReadbackCommandGraphBudget graph;
    DeviceLossRegistrationBudget deviceLoss;
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxPendingRequests = 0u, maxResourceOwners = 0u, maxMipCopies = 0u,
                  maxStatisticsViews = 0u;
    std::uint32_t maxOriginalCommands = 0u, maxNativeCommands = 0u, maxEncoders = 0u,
                  maxCommandBuffers = 0u, maxDebugScopes = 0u, maxDebugDepth = 0u;
    std::uint32_t maxStateControls = 0u, maxStateTransitions = 0u;
    std::uint32_t maxCreationCallbacks = 0u, maxRecordingCallbacks = 0u,
                  maxSubmissionCallbacks = 0u, maxMappingCallbacks = 0u;
    std::uint64_t maxSourceAllocationBytes = 0u, maxNativeBytesPerRequest = 0u,
                  maxContentBytesPerRequest = 0u;
    std::uint64_t maxStagingBytesPerRequest = 0u, maxResultBytesPerRequest = 0u,
                  maxLabelBytesPerRequest = 0u, maxAnalyzedPixelsPerRequest = 0u;
    std::uint64_t maxDeviceStagingBytes = 0u, maxDeviceRetainedNativeBytes = 0u,
                  maxDeviceRetainedContentBytes = 0u, maxDeviceRetainedResultBytes = 0u,
                  maxDeviceRetainedLabelBytes = 0u;
    std::uint64_t maxCpuDeviceBytes = 0u, maxCpuRequestBytes = 0u, maxCpuResourceBytes = 0u,
                  maxCpuResultBytes = 0u;
};

class TextureReadbackColdBudgetPlan final : public core::QuotaBoundSharedObjectIdentity {
    class ConstructionKey {
        friend class TextureReadbackColdBudgetPlan;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    enum class CpuDomain : std::size_t { Device, Request, Resource, Result, Count };
    using Quotas = std::array<std::shared_ptr<core::CpuMemoryQuota>,
        static_cast<std::size_t>(CpuDomain::Count)>;
    static constexpr std::uint32_t singleRequestGraphCount = 1u;

    struct Lowering {
        bool cpuBudgetImplemented = false, validatorOnly = false;
        const char* diagnostic = "RHI-READBACK-COLD-BUDGET-BACKEND";
    };

    static constexpr Lowering lowering(Backend backend) noexcept
    {
        return semantics::TransferLowering::TextureReadbackColdBudgetPlan_lowering<Lowering>(
            backend);
    }

    TextureReadbackColdBudgetPlan(ConstructionKey, TextureReadbackColdBudget b, Quotas quotas)
        : budget_(b), quotas_(std::move(quotas)), lossRegistrations_(b.deviceLoss.maxRegistrations),
          lossCallbacks_(b.deviceLoss.maxCallbacks)
    {}

    static const char* validateBudget(const TextureReadbackColdBudget& b) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-READBACK-COLD-BUDGET-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-READBACK-COLD-BUDGET-OPT-IN";
        if (!b.maxPendingRequests || !b.maxResourceOwners || !b.maxMipCopies
            || !b.maxStatisticsViews || !b.maxOriginalCommands || !b.maxNativeCommands
            || !b.maxEncoders || !b.maxCommandBuffers || !b.maxDebugScopes || !b.maxStateControls
            || !b.maxStateTransitions || !b.maxCreationCallbacks || !b.maxRecordingCallbacks
            || !b.maxSubmissionCallbacks || !b.maxMappingCallbacks)
            return "RHI-READBACK-COLD-BUDGET-COUNT";
        if (!b.maxSourceAllocationBytes || !b.maxNativeBytesPerRequest
            || !b.maxContentBytesPerRequest || !b.maxStagingBytesPerRequest
            || !b.maxResultBytesPerRequest || !b.maxAnalyzedPixelsPerRequest
            || !b.maxDeviceStagingBytes || !b.maxDeviceRetainedNativeBytes
            || !b.maxDeviceRetainedContentBytes || !b.maxDeviceRetainedResultBytes
            || !b.maxCpuDeviceBytes || !b.maxCpuRequestBytes || !b.maxCpuResourceBytes
            || !b.maxCpuResultBytes)
            return "RHI-READBACK-COLD-BUDGET-BYTES";
        if (b.maxStagingBytesPerRequest > b.maxDeviceStagingBytes
            || b.maxNativeBytesPerRequest > b.maxDeviceRetainedNativeBytes
            || b.maxContentBytesPerRequest > b.maxDeviceRetainedContentBytes
            || b.maxResultBytesPerRequest > b.maxDeviceRetainedResultBytes
            || b.maxLabelBytesPerRequest > b.maxDeviceRetainedLabelBytes
            || b.maxResultBytesPerRequest > b.maxCpuResultBytes
            || b.maxResultBytesPerRequest > SIZE_MAX || b.maxCpuDeviceBytes > SIZE_MAX
            || b.maxCpuRequestBytes > SIZE_MAX || b.maxCpuResourceBytes > SIZE_MAX
            || b.maxCpuResultBytes > SIZE_MAX)
            return "RHI-READBACK-COLD-BUDGET-RANGE";
        if (b.maxOriginalCommands < TextureReadbackCommandGraphContract::commandCount)
            return "RHI-READBACK-GRAPH-BUDGET-COMMAND-CAPACITY";
        if (const auto* id =
                DeviceLossRegistrationContract::validateBudget(b.deviceLoss, b.maxCpuDeviceBytes))
            return id;
        return TextureReadbackCommandGraphContract::validateBudget(
            b.graph, b.maxLabelBytesPerRequest, b.maxCpuRequestBytes);
    }

    static const char* validateQuotas(
        const TextureReadbackColdBudget& b, const Quotas& quotas) noexcept
    {
        const std::array<std::uint64_t, 4> limits{
            b.maxCpuDeviceBytes, b.maxCpuRequestBytes, b.maxCpuResourceBytes, b.maxCpuResultBytes};
        for (std::size_t i = 0; i < quotas.size(); ++i) {
            if (!quotas[i] || quotas[i]->limitBytes() != limits[i])
                return "RHI-READBACK-COLD-BUDGET-QUOTA";
            for (std::size_t j = 0; j < i; ++j)
                if (quotas[i] == quotas[j]
                    || core::SharedOwnerProjection::sameControlBlock(quotas[i], quotas[j]))
                    return "RHI-READBACK-COLD-BUDGET-QUOTA-ALIAS";
        }
        return nullptr;
    }

    static std::shared_ptr<const TextureReadbackColdBudgetPlan> prepareOriginal(
        const TextureReadbackColdBudget& b, Quotas quotas, const char*& diagnostic)
    {
        diagnostic = validateBudget(b);
        if (diagnostic)
            return {};
        if ((diagnostic = validateQuotas(b, quotas)))
            return {};
        try {
            core::CpuMemoryQuotaScope scope(quotas[static_cast<std::size_t>(CpuDomain::Device)]);
            return shareOriginal(core::makeUnique<TextureReadbackColdBudgetPlan>(
                ConstructionKey{}, b, std::move(quotas)));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-READBACK-COLD-BUDGET-ALLOCATION";
            return {};
        }
    }

    static const char* validateOriginal(
        const std::shared_ptr<const TextureReadbackColdBudgetPlan>& self) noexcept
    {
        return self && self->hasQuotaBoundSharedOwner(self)
            ? validateQuotas(self->budget_, self->quotas_)
            : "RHI-READBACK-COLD-BUDGET-OWNER";
    }

    static const char* validateRequestOriginal(
        const std::shared_ptr<const TextureReadbackColdBudgetPlan>& self,
        const std::shared_ptr<const TextureReadbackRequestDeclaration>& request,
        const std::shared_ptr<const TextureReadbackStatisticsPlan>& statistics) noexcept
    {
        if (const auto* id = validateOriginal(self))
            return id;
        if (!request || !request->hasQuotaBoundSharedOwner(request))
            return "RHI-READBACK-COLD-BUDGET-REQUEST-OWNER";
        if (const auto* id = TextureReadbackRequestDeclaration::validateOriginal(
                request, request->originalLayout()))
            return id;
        if (const auto* id = TextureReadbackStatisticsPlan::validateOriginal(
                statistics, request->originalLayout()))
            return id;
        const auto& b = self->budget_;
        const auto bytes = request->originalLayout()->inputOwner()->scalarFields().byteCapacity();
        return request->originalLayout()->mipCopies().size() <= b.maxMipCopies
                && request->label().size() <= b.maxLabelBytesPerRequest
                && bytes <= b.maxResultBytesPerRequest && bytes <= b.maxStagingBytesPerRequest
                && statistics->views().size() <= b.maxStatisticsViews
                && statistics->analyzedPixels() <= b.maxAnalyzedPixelsPerRequest
            ? nullptr
            : "RHI-READBACK-COLD-BUDGET-REQUEST-CAPACITY";
    }

    static const char* aggregatePublicationCounts(
        const TextureReadbackColdBudget& b, std::uint32_t& callbacks, std::uint32_t& mips) noexcept
    {
        std::uint64_t callbackCount = 0u, mipCount = 0u;
        if (!core::StridedIndexRange(
                0u, std::uint64_t(b.maxPendingRequests) + 1u, b.maxMappingCallbacks, UINT32_MAX)
                .index(b.maxPendingRequests, callbackCount)
            || !core::StridedIndexRange(
                0u, std::uint64_t(b.maxPendingRequests) + 1u, b.maxMipCopies, UINT32_MAX)
                .index(b.maxPendingRequests, mipCount))
            return "RHI-READBACK-COLD-BUDGET-AGGREGATE-COUNT";
        callbacks = static_cast<std::uint32_t>(callbackCount);
        mips = static_cast<std::uint32_t>(mipCount);
        return nullptr;
    }

    const TextureReadbackColdBudget& budget() const noexcept { return budget_; }

    const auto& quotas() const noexcept { return quotas_; }

    static std::shared_ptr<core::AtomicCapacityBudget> originalLossRegistrationCapacity(
        const std::shared_ptr<const TextureReadbackColdBudgetPlan>& self) noexcept
    {
        return validateOriginal(self)
            ? std::shared_ptr<core::AtomicCapacityBudget>{}
            : std::shared_ptr<core::AtomicCapacityBudget>(self, &self->lossRegistrations_);
    }

    static std::shared_ptr<core::AtomicCapacityBudget> originalLossCallbackCapacity(
        const std::shared_ptr<const TextureReadbackColdBudgetPlan>& self) noexcept
    {
        return validateOriginal(self)
            ? std::shared_ptr<core::AtomicCapacityBudget>{}
            : std::shared_ptr<core::AtomicCapacityBudget>(self, &self->lossCallbacks_);
    }

    // A native loss promise cannot be unsubscribed. Detached closures therefore
    // consume this plan's one-shot binding forever; repeated shutdown/configure
    // cannot grow foreign promise state under a recycled count ceiling.
    static bool originalLossBindingAvailable(
        const std::shared_ptr<const TextureReadbackColdBudgetPlan>& self) noexcept
    {
        return !validateOriginal(self)
            && self->lossBinding_.phase() == core::runtime::ExternalCallbackGate::Phase::Unarmed;
    }

    static bool commitOriginalLossBinding(
        const std::shared_ptr<const TextureReadbackColdBudgetPlan>& self) noexcept
    {
        return !validateOriginal(self) && self->lossBinding_.arm();
    }

    bool nativeExecutionProven() const noexcept { return false; }

private:
    const TextureReadbackColdBudget budget_;
    const Quotas quotas_;
    mutable core::AtomicCapacityBudget lossRegistrations_, lossCallbacks_;
    mutable core::runtime::ExternalCallbackGate lossBinding_;
};
} // namespace arp::rhi

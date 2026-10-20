#pragma once

#include "rhi/backends/webgpu/webgpu_canonical_cold_command.h"
#include "rhi/frontend/command_recording.h"
#include "rhi/frontend/command_transfer_validation.h"
#include "rhi/semantics/command_lowering.h"

#include <optional>

namespace arp::test {
class WebGpuCanonicalCallbackCustodyFixture;
class WebGpuCanonicalReadbackJoinsFixture;
class WebGpuPostSuppressionFailureProbeFixture;
} // namespace arp::test

namespace arp::rhi {
struct OriginalCommandCopyPassLoweringBudget;
struct OriginalCommandIssuerPreparationBudget;
struct OriginalTypedCallbackCustodyRequest;
struct OriginalTypedIssuerPreparationRequest;
struct OriginalBindlessRootCallbackCustodyRequest;
struct OriginalBindlessRootIssuerRequest;
} // namespace arp::rhi

namespace arp::rhi_webgpu {
class WebGpuCanonicalCopyPassLowering;
class WebGpuCanonicalIssuerPreparation;
class WebGpuCanonicalReadbackResultStorage;
class WebGpuOriginalTypedCallbackCustody;
class WebGpuOriginalTypedColdCommand;
class WebGpuOriginalTypedIssuerPreparation;
class WebGpuOriginalGeneralOutputSnapshot;
class WebGpuBindlessRootColdCommand;
class WebGpuBindlessRootCallbackCustody;
class WebGpuBindlessRootIssuerPreparation;

// Opaque, externally serialized pre-record custody. The runtime's deliberate
// native subscription cycle retains exact cold ownership until suppression or
// actual native loss. No handle, callback receiver, proof or mutable graph API.
class WebGpuCanonicalCallbackCustody final : public core::QuotaBoundSharedObjectIdentity {
    friend class arp::test::WebGpuCanonicalCallbackCustodyFixture;
    friend class arp::test::WebGpuCanonicalReadbackJoinsFixture;
    friend class arp::test::WebGpuPostSuppressionFailureProbeFixture;
    friend class WebGpuCanonicalCopyPassLowering;
    friend class WebGpuCanonicalIssuerPreparation;
    friend class WebGpuOrdinaryRecording;
    friend class WebGpuOrdinarySubmission;
    friend class WebGpuOriginalTypedCallbackCustody;
    friend class WebGpuOriginalTypedIssuerPreparation;
    friend class WebGpuBindlessRootCallbackCustody;
    friend class WebGpuBindlessRootIssuerPreparation;
    template <class Command> class RuntimeState;
    using State = RuntimeState<WebGpuCanonicalColdCommand>;

    class ConstructionKey {
        friend class WebGpuCanonicalCallbackCustody;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    using Budget = rhi::OriginalCommandCallbackCustodyBudget;
    using PublicationIntent = rhi::OriginalCommandReadbackSnapshotPublicationContract::Intent;
    WebGpuCanonicalCallbackCustody(ConstructionKey, std::shared_ptr<State>, Budget) noexcept;
    ~WebGpuCanonicalCallbackCustody();
    static std::shared_ptr<const WebGpuCanonicalCallbackCustody> prepareOriginal(
        std::shared_ptr<const WebGpuCanonicalColdCommand>,
        const Budget&,
        const char*& diagnostic,
        std::shared_ptr<const WebGpuCanonicalReadbackResultStorage> results = {},
        std::optional<PublicationIntent> publication = std::nullopt);

    static const char* validateOriginal(
        const std::shared_ptr<const WebGpuCanonicalCallbackCustody>& self) noexcept
    {
        return !self || !self->hasQuotaBoundSharedOwner(self)
            ? "RHI-WEBGPU-CANONICAL-CALLBACK-OWNER"
            : self->validate();
    }

    const char* validate() const noexcept;
    const char* cancelOriginal() const noexcept;
    bool originalNativeLossArmed() const noexcept;
    bool originalNativeLossDelivered() const noexcept;
    std::uint32_t pendingOriginalRecords() const noexcept;
    bool overlapsCpuOwner(const std::shared_ptr<core::CpuMemoryQuota>&) const noexcept;

    // Backend-private read-only join; genuine upstream completion and the
    // downstream original seal are both required. Public snapshot stays strict.
    static const char* validateGeneralOutputPending(
        const std::shared_ptr<const WebGpuOriginalGeneralOutputSnapshot>&,
        const std::shared_ptr<const rhi::OriginalCommandRecordingSeal>&,
        const rhi::GpuTexture::CommittedStateObservation&) noexcept;

    bool nativeExecutionProven() const noexcept { return false; }

    const char* executionDiagnostic() const noexcept
    {
        if (const auto* id = validate())
            return id;
        return arp::rhi::semantics::OriginalCommandCallbackCustodyLowering::lowering(
            rhi::Backend::WebGPU)
            .diagnostic;
    }

private:
    static const char* validateRootRetainedState(
        const std::shared_ptr<RuntimeState<WebGpuBindlessRootColdCommand>>&) noexcept;
    static std::shared_ptr<const WebGpuBindlessRootIssuerPreparation> prepareRootIssuers(
        std::shared_ptr<const WebGpuBindlessRootCallbackCustody>,
        const rhi::OriginalBindlessRootIssuerRequest&,
        const char*&);
    static const char* inspectRootIssuers(
        const std::shared_ptr<const WebGpuBindlessRootCallbackCustody>&,
        const rhi::OriginalBindlessRootIssuerRequest&,
        std::uint32_t&) noexcept;
    static const char* validateRootIssuers(const WebGpuBindlessRootIssuerPreparation&) noexcept;
    static bool rootIssuersClosed(const WebGpuBindlessRootIssuerPreparation&) noexcept;
    static std::shared_ptr<const WebGpuBindlessRootCallbackCustody> prepareRootOriginal(
        std::shared_ptr<const WebGpuBindlessRootColdCommand>,
        const rhi::OriginalBindlessRootCallbackCustodyRequest&,
        const char*&);
    static const char* inspectRootOriginal(
        const std::shared_ptr<const WebGpuBindlessRootColdCommand>&,
        const rhi::OriginalBindlessRootCallbackCustodyRequest&) noexcept;
    static const char* validateRootOriginal(const WebGpuBindlessRootCallbackCustody&) noexcept;
    static void invalidateOriginalOwnership(const WebGpuBindlessRootColdCommand&) noexcept;
    static std::shared_ptr<const WebGpuOriginalTypedCallbackCustody> prepareTypedOriginal(
        std::shared_ptr<const WebGpuOriginalTypedColdCommand>,
        const rhi::OriginalTypedCallbackCustodyRequest&,
        const char*&,
        std::shared_ptr<const WebGpuCanonicalReadbackResultStorage> = {},
        std::optional<PublicationIntent> = std::nullopt);
    static const char* inspectTypedOriginal(
        const std::shared_ptr<const WebGpuOriginalTypedColdCommand>&,
        const rhi::OriginalTypedCallbackCustodyRequest&,
        const std::shared_ptr<const WebGpuCanonicalReadbackResultStorage>& = {},
        const std::optional<PublicationIntent>& = std::nullopt) noexcept;
    static const char* validateTypedOriginal(const WebGpuOriginalTypedCallbackCustody&) noexcept;
    static std::shared_ptr<const WebGpuOriginalTypedIssuerPreparation> prepareTypedIssuers(
        std::shared_ptr<const WebGpuOriginalTypedCallbackCustody>,
        const rhi::OriginalTypedIssuerPreparationRequest&,
        const char*&);
    static const char* validateTypedRetainedState(
        const std::shared_ptr<RuntimeState<WebGpuOriginalTypedColdCommand>>&) noexcept;
    static const char* inspectTypedIssuers(
        const std::shared_ptr<const WebGpuOriginalTypedCallbackCustody>&,
        const rhi::OriginalTypedIssuerPreparationRequest&,
        std::uint32_t&) noexcept;
    static const char* validateTypedIssuers(const WebGpuOriginalTypedIssuerPreparation&) noexcept;
    static bool typedIssuersClosed(const WebGpuOriginalTypedIssuerPreparation&) noexcept;
    static void invalidateOriginalOwnership(const WebGpuOriginalTypedColdCommand&) noexcept;
    static std::shared_ptr<const WebGpuCanonicalIssuerPreparation> prepareOriginalIssuers(
        std::shared_ptr<const WebGpuCanonicalCopyPassLowering>,
        const rhi::OriginalCommandIssuerPreparationBudget&,
        const char*&);
    const char* validateOriginalIssuers(const WebGpuCanonicalIssuerPreparation&) const noexcept;
    static void invalidateOriginalOwnership(const WebGpuCanonicalColdCommand&) noexcept;
    static std::shared_ptr<const WebGpuCanonicalCopyPassLowering> prepareOriginalLowering(
        std::shared_ptr<const WebGpuCanonicalCallbackCustody>,
        const rhi::OriginalCommandCopyPassLoweringBudget&,
        const char*&);
    const char* validateOriginalLowering(const WebGpuCanonicalCopyPassLowering&) const noexcept;
    static const char* inspect(const std::shared_ptr<const WebGpuCanonicalColdCommand>&,
        const Budget&,
        const std::shared_ptr<const WebGpuCanonicalReadbackResultStorage>& results = {},
        const std::optional<PublicationIntent>& publication = std::nullopt) noexcept;
    // Testing can inspect native suppression, never invoke delivery or issue proof.
    std::uintptr_t originalLossStateForInspection() const noexcept;

    std::weak_ptr<const void> originalStateForInspection() const noexcept { return state_; }

    const std::shared_ptr<State> state_;
    const Budget budget_;
};
} // namespace arp::rhi_webgpu

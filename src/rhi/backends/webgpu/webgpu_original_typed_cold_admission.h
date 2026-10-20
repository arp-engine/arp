#pragma once
#include "rhi/backends/webgpu/native_validation.h"
#include "rhi/backends/webgpu/webgpu_original_typed_native_payload.h"
#include "rhi/frontend/command_recording.h"
#include "rhi/frontend/command_shader_usage_validation.h"

namespace arp::rhi_webgpu {
// Independent TypedNativeCold1/2/3/4/5/6/7/8 admission. It binds genuine immutable typed
// input to the exact original seal/reservation, never to legacy NativeCold1-5.
// Cold success issues no callback/encoder/proof and preserves Prepared guards.
class WebGpuOriginalTypedColdAdmission final : public core::QuotaBoundSharedObjectIdentity {
    friend class WebGpuNativeCommandPreparation;
    using C = rhi::OriginalTypedNativeColdContract;
    using P = WebGpuOriginalTypedNativePayload;
    using R = rhi::OriginalCommandLifecycleReservation;
    using S = rhi::OriginalCommandRecordingSeal;
    using Root = rhi::OriginalCommandStateTransaction;

    class ConstructionKey {
        friend class WebGpuOriginalTypedColdAdmission;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    using Request = C::Request;
    using Requirements = C::Requirements;
    // Payload already owns the root. Seal adds itself and two independent CPU
    // domains, reservation adds itself, admission adds itself. Loss stays shared.
    static constexpr std::uint32_t additionalOwners =
        S::additionalOwnerCount + R::additionalOwnerCount + 1u;

    static constexpr Requirements lifecycleRequirements(std::uint32_t version = 1u) noexcept
    {
        Requirements required;
        if (version < 1u || version > 8u)
            return required;
        required.contractVersion = version;
        constexpr auto scopes = WebGpuErrorScopeContract::count;
        constexpr auto lossCallbacks = rhi::OriginalDeviceNativeLossContract::callbackCount;
        constexpr auto lossRegistrations = rhi::OriginalDeviceNativeLossContract::registrationCount;
        required.encoders = required.commandBuffers = 1u;
        required.callbacks = {scopes, scopes, 1u, lossCallbacks, lossRegistrations};
        required.existingDeviceLossCallbacks = lossCallbacks;
        required.existingDeviceLossRegistrations = lossRegistrations;
        required.capacity = {1u,
            true,
            1u,
            scopes + scopes + 1u + lossCallbacks,
            scopes + scopes + 1u + lossRegistrations};
        return required;
    }

    struct Inspection {
        const char* diagnostic = nullptr;
        Requirements required;
    };

    // Read-only preclaim gate. Caller may author the original seal/capacity only
    // after this succeeds; Source budgets are never changed to make it succeed.
    static Inspection inspectOriginalPayload(
        const std::shared_ptr<const P>& payload, const Request& request) noexcept
    {
        Inspection result;
        const auto* id = derive(payload, request, result.required);
        if (id) {
            result.diagnostic = id;
            result.required = {};
        }
        return result;
    }

    static Inspection inspectOriginal(const std::shared_ptr<const P>& payload,
        const std::shared_ptr<const R>& reservation,
        const Request& request) noexcept
    {
        auto result = inspectOriginalPayload(payload, request);
        if (result.diagnostic)
            return result;
        if (const auto* id = inspectBinding(payload, reservation, request, result.required)) {
            result.diagnostic = id;
            result.required = {};
        }
        return result;
    }

    WebGpuOriginalTypedColdAdmission(ConstructionKey,
        std::shared_ptr<const P> payload,
        std::shared_ptr<const R> reservation,
        Request request,
        Requirements required) noexcept
        : payload_(std::move(payload)), reservation_(std::move(reservation)), request_(request),
          required_(required)
    {}

    static std::shared_ptr<const WebGpuOriginalTypedColdAdmission> prepareOriginal(
        std::shared_ptr<const P> payload,
        std::shared_ptr<const R> reservation,
        const Request& request,
        const char*& diagnostic)
    {
        const auto inspected = inspectOriginal(payload, reservation, request);
        if ((diagnostic = inspected.diagnostic))
            return {};
        try {
            core::CpuMemoryQuotaScope scope(payload->originalMetadataQuota());
            auto result = shareOriginal(core::makeUnique<WebGpuOriginalTypedColdAdmission>(
                ConstructionKey{}, payload, reservation, request, inspected.required));
            if ((diagnostic = result->validate()))
                return {};
            return result;
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-WEBGPU-TYPED-COLD-ALLOCATION";
            return {};
        } catch (const std::length_error&) {
            diagnostic = "RHI-WEBGPU-TYPED-COLD-ALLOCATION";
            return {};
        }
    }

    static const char* validateOriginal(
        const std::shared_ptr<const WebGpuOriginalTypedColdAdmission>& self) noexcept
    {
        return !self || !self->hasQuotaBoundSharedOwner(self) ? "RHI-WEBGPU-TYPED-COLD-OWNER"
                                                              : self->validate();
    }

    const char* validate() const noexcept
    {
        const auto inspected = inspectOriginal(payload_, reservation_, request_);
        if (inspected.diagnostic)
            return inspected.diagnostic;
        if (!core::SharedOwnerProjection::sameOwner(
                originalMetadataQuota(), payload_->originalMetadataQuota()))
            return "RHI-WEBGPU-TYPED-COLD-QUOTA";
        // Original immutable payload and reservation determine all fields. No
        // caller requirements or alternate state/count ledger are stored.
        return inspected.required.matches(required_) ? nullptr
                                                     : "RHI-WEBGPU-TYPED-COLD-REQUIREMENTS";
    }

    const auto& originalPayload() const noexcept { return payload_; }

    const auto& originalReservation() const noexcept { return reservation_; }

    const auto& originalRoot() const noexcept
    {
        return payload_->originalDecision()->originalClosure()->originalRoot();
    }

    const Request& originalRequest() const noexcept { return request_; }

    const Requirements& requirements() const noexcept { return required_; }

    std::uint32_t retainedOwnerCount() const noexcept { return required_.retainedOwners; }

    bool nativeExecutionProven() const noexcept { return false; }

    const char* executionDiagnostic() const noexcept
    {
        if (const auto* id = validate())
            return id;
        return "RHI-WEBGPU-TYPED-COLD-LIVE-EXECUTOR-MISSING";
    }

private:
    static const char* derive(const std::shared_ptr<const P>& payload,
        const Request& request,
        Requirements& required) noexcept
    {
        if (const auto* id = C::validateIntent(request))
            return id;
        if (const auto* id = P::validateOriginal(payload))
            return id;
        const auto& closure = payload->originalDecision()->originalClosure();
        const auto& root = closure->originalRoot();
        if (const auto* id = closure->nativeLossDiagnostic())
            return id;
        if (root->status() != Root::Status::Prepared || !root->journalEmpty())
            return "RHI-WEBGPU-TYPED-COLD-PLAN-STATUS";
        return deriveImmutableRequirements(payload, request, required);
    }

    static const char* deriveImmutableRequirements(const std::shared_ptr<const P>& payload,
        const Request& request,
        Requirements& required,
        const Requirements* sealedReadback = nullptr) noexcept
    {
        if (const auto* id = C::validateIntent(request))
            return id;
        // Cold1's pending scans and executor implement only Payload1. Cold2
        // admits its paired cold inputs without granting those live semantics.
        if (request.contractVersion == 1u && payload->originalRequest().contractVersion != 1u)
            return "RHI-WEBGPU-TYPED-COLD-PAYLOAD-PROFILE";
        if (const auto* id =
                C::validatePayloadProfile(request, payload->originalRequest().contractVersion))
            return id;
        const auto lowering = rhi::semantics::OriginalTypedNativeColdLowering::admissionLowering(
            rhi::Backend::WebGPU, request.contractVersion);
        if (!lowering.admissionAvailable)
            return lowering.diagnostic;
        const auto& closure = payload->originalDecision()->originalClosure();
        const auto& root = closure->originalRoot();
        if (const auto* id = rhi::semantics::OriginalTypedRenderStateContract::validate(
                payload->records(), request.maxWork.commands))
            return id;
        required = lifecycleRequirements(request.contractVersion);
        required.work = payload->work();
        if (C::readbackProfile(request.contractVersion))
            required.transfers = {payload->transferPassCount(),
                payload->readbackCommandCount(),
                payload->readbackCopyCount()};
        if (payload->retainedOwnerCount() > UINT32_MAX - additionalOwners)
            return "RHI-WEBGPU-TYPED-COLD-OWNER-CAPACITY";
        required.retainedOwners = payload->retainedOwnerCount() + additionalOwners;
        required.cpuMetadataBytes = root->originalMetadataQuota()->limitBytes();
        if (request.maxCpuMetadataBytes != required.cpuMetadataBytes)
            return "RHI-WEBGPU-TYPED-COLD-QUOTA";
        const auto& source = root->originalGraph()
                                 ->originalInputs()
                                 ->originalScalars()
                                 ->originalUsage()
                                 ->originalSources()
                                 ->originalBudget();
        if (C::hasReadbackDemand(request)) {
            if (sealedReadback) {
                // Only the private retained admission validator passes its own
                // immutable constructor-issued projection after root/seal auth.
                required.readback = sealedReadback->readback;
                if (required.readback.readbackCommands != payload->readbackCommandCount()
                    || required.readback.nativeCopyCommands != payload->readbackCopyCount())
                    return "RHI-WEBGPU-TYPED-COLD-READBACK-FROZEN-COUNTS";
            } else {
                const auto result = rhi::OriginalCommandReadbackResultContract::inspectProgram(
                    closure->originalStream(),
                    root->originalGraph()->originalInputs(),
                    request.readback);
                if (result.diagnostic)
                    return result.diagnostic;
                required.readback = result.required;
            }
            if (const auto* id =
                    rhi::OriginalCommandReadbackMapContract::deriveRequirements(required.readback,
                        source,
                        required.capacity.callbackTickets,
                        required.capacity.callbackRegistrations,
                        request.readbackMap,
                        required.readbackMap))
                return id;
            if (required.readbackMap.totalCallbackTickets > UINT32_MAX
                || required.readbackMap.totalCallbackRegistrations > UINT32_MAX)
                return "RHI-WEBGPU-TYPED-COLD-MAP-COUNT-RANGE";
            required.capacity.callbackTickets =
                static_cast<std::uint32_t>(required.readbackMap.totalCallbackTickets);
            required.capacity.callbackRegistrations =
                static_cast<std::uint32_t>(required.readbackMap.totalCallbackRegistrations);
        }
        if (const auto* id = C::validateRequirements(required, request))
            return id;
        // Cold6 composes Payload6's independently authored native custody;
        // Sources continues to bound the CPU root, not initializer actors.
        if (const auto* id = C::validateOwnerCapacity(request,
                payload->retainedOwnerCount(),
                payload->originalRequest().maxRetainedOwners,
                additionalOwners,
                root->retainedOwnerCount(),
                source.maxGraphOwners))
            return id;
        if (request.contractVersion == 6u
            && (root->originalContentPlanContractVersion() != 4u
                || !root->hasOriginalGeneralShaderResourceProfile()))
            return "RHI-WEBGPU-TYPED-COLD-GENERAL-ROOT-PROFILE";
        if (request.contractVersion == 7u
            && (root->originalContentPlanContractVersion() != 5u
                || !root->hasOriginalFiniteGeneralShaderResourceProfile()
                || root->hasOriginalGeneralShaderResourceProfile()))
            return "RHI-WEBGPU-TYPED-COLD-FINITE-GENERAL-ROOT-PROFILE";
        if (request.contractVersion == 8u
            && (root->originalContentPlanContractVersion() != 6u
                || !root->hasOriginalFiniteGeneralShaderResourceProfile()
                || root->hasOriginalGeneralShaderResourceProfile()
                || !root->originalMixedContentCounts()))
            return "RHI-WEBGPU-TYPED-COLD-GRAPHICS-ROOT-PROFILE";
        return C::validateSourceAuthority(required, closure->nativePayloadCount(), source);
    }

    static const char* inspectBinding(const std::shared_ptr<const P>& payload,
        const std::shared_ptr<const R>& reservation,
        const Request& request,
        const Requirements& required) noexcept
    {
        if (const auto* id = R::validateOriginal(reservation))
            return id;
        const auto& seal = reservation->originalSeal();
        const auto& root = payload->originalDecision()->originalClosure()->originalRoot();
        if (const auto* id = seal->compareOriginalRoot(root))
            return id;
        return inspectImmutableBinding(payload, reservation, request, required);
    }

    static const char* inspectImmutableBinding(const std::shared_ptr<const P>& payload,
        const std::shared_ptr<const R>& reservation,
        const Request& request,
        const Requirements& required) noexcept
    {
        const auto& seal = reservation->originalSeal();
        if (const auto* id =
                C::validateLifecycleProfile(request, seal->originalBudget().contractVersion))
            return id;
        const auto& demand = reservation->originalRequest();
        if (demand.contractVersion != required.capacity.contractVersion
            || demand.prototypeOptIn != required.capacity.prototypeOptIn
            || demand.pendingSubmissions != required.capacity.pendingSubmissions
            || demand.callbackTickets != required.capacity.callbackTickets
            || demand.callbackRegistrations != required.capacity.callbackRegistrations)
            return "RHI-WEBGPU-TYPED-COLD-RESERVATION-DEMAND";
        const auto& budget = seal->originalBudget();
        const auto& closure = payload->originalDecision()->originalClosure();
        for (const auto& quota :
            std::array{seal->originalSubmissionQuota(), seal->originalCallbackQuota()})
            if (closure->overlapsNativeOwner(quota)
                || core::SharedOwnerProjection::overlapsOwner(
                    quota, closure->originalStream()->originalMetadataQuota()))
                return "RHI-WEBGPU-TYPED-COLD-QUOTA-DOMAIN";
        if (required.retainedOwners > budget.maxRecordingOwners
            || required.retainedOwners > budget.maxSubmissionOwners
            || required.retainedOwners > budget.maxCallbackOwners)
            return "RHI-WEBGPU-TYPED-COLD-OWNER-CAPACITY";
        return nullptr;
    }

    const std::shared_ptr<const P> payload_;
    const std::shared_ptr<const R> reservation_;
    const Request request_;
    const Requirements required_;
};
} // namespace arp::rhi_webgpu

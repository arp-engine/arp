#pragma once

// Inline/template definitions for the public GpuDevice abstraction.
#include "rhi/frontend/query_creation.h"

namespace arp::rhi {

inline std::shared_ptr<QueryPool> GpuDevice::createOriginalQueryPool(
    std::shared_ptr<const QueryPoolInputSnapshot> input,
    const OriginalQueryPoolCreationBudget& budget,
    std::shared_ptr<core::CpuMemoryQuota> ownerQuota,
    const char*& diagnostic)
{
    const auto reject = [&](const char* id) -> std::shared_ptr<QueryPool> {
        diagnostic = id;
        lastOriginalRegistrationDiagnostic_ = id;
        return {};
    };
    if (!originalRegistry_)
        return reject("RHI-ORIGINAL-QUERY-REGISTRATION-REQUIRED");
    if (const auto* id = originalRegistry_->validate())
        return reject(id);
    if (const auto* id = OriginalResourceRegistrationContract::queryLowering(
            backend(), originalRegistry_->budget_.queries)
            .diagnostic)
        return reject(id);
    if (originalLossBinding_
        || OriginalResourceRegistrationContract::observesNativeLoss(
            originalRegistry_->budget_.contractVersion)) {
        if (!OriginalResourceRegistrationContract::joinsQueryNativeLoss(originalRegistry_->budget_))
            return reject("RHI-ORIGINAL-QUERY-NATIVE-LOSS-JOIN-UNAVAILABLE");
        if (const auto* id =
                OriginalQueryNativeLossJoinContract::validate(originalRegistry_->budget_))
            return reject(id);
        const auto decision = OriginalQueryNativeLossJoinContract::lowering(backend());
        if (!decision.nativeSourceAvailable)
            return reject(decision.diagnostic);
        const auto loss = observeOriginalDeviceNativeLoss(diagnostic);
        if (!loss)
            return reject(diagnostic);
        if (const auto* id = validateOriginalTypedNativeLossJoin(*loss, {3u, true}))
            return reject(id);
    }
    if (const auto* id = budget.diagnostic())
        return reject(id);
    if (!input || !input->hasQuotaBoundSharedOwner(input))
        return reject("RHI-QUERY-CREATION-INPUT");
    if (const auto validation = input->validateDescriptor(); !validation.success)
        return reject(validation.diagnostics);
    if (!ownerQuota || ownerQuota->limitBytes() != budget.maxCpuOwnerBytes)
        return reject("RHI-VULKAN-ORIGINAL-QUERY-QUOTA");
    if (originalRegistry_->overlapsCpuQuota(ownerQuota)
        || originalRegistry_->overlapsCpuQuota(input->originalMetadataQuota())
        || core::SharedOwnerProjection::overlapsOwner(ownerQuota, input->originalMetadataQuota()))
        return reject("RHI-ORIGINAL-QUERY-REGISTRATION-QUOTA-DOMAIN");
    try {
        auto record = reserveOriginalRegistrationRecord(
            OriginalRegisteredResourceKind::QueryPool, {}, nullptr, 0u);
        if (!record)
            return reject(lastOriginalRegistrationDiagnostic_);
        auto candidate = createOriginalQueryPoolCandidate(input, budget, ownerQuota, diagnostic);
        if (!candidate)
            return reject(diagnostic ? diagnostic : "RHI-ORIGINAL-REGISTRATION-CANDIDATE-MISSING");
        if (candidate->originalRegistration_.record_ || !candidate->originalFactoryOwner_.expired()
            || !core::SharedOwnerProjection::sameOwner(candidate->originalInput(), input)
            || candidate->count() != input->scalarFields().count()
            || candidate->type() != input->scalarFields().type())
            return reject("RHI-ORIGINAL-QUERY-REGISTRATION-CANDIDATE");
        if (const auto* id = validateOriginalQueryPoolCandidate(candidate))
            return reject(id);
        // The original shared control block already exists. These assignments
        // allocate nothing and do not transfer an arbitrary borrowed wrapper.
        record->object_ = static_cast<const QueryPool*>(candidate.get());
        if (const auto* id = record->validate())
            return reject(id);
        candidate->originalRegistration_.record_ = std::move(record);
        candidate->originalOwnerQuota_ = std::move(ownerQuota);
        candidate->originalFactoryOwner_ = candidate;
        diagnostic = nullptr;
        lastOriginalRegistrationDiagnostic_ = nullptr;
        return candidate;
    } catch (const std::bad_alloc&) {
        return reject("RHI-ORIGINAL-REGISTRATION-ALLOCATION");
    }
}

inline const char* GpuDevice::originalMemoryHeapCreationDiagnostic() const noexcept
{
    if (!originalRegistry_ || !originalRegistry_->budget_.heaps.requested())
        return nullptr;
    if (const auto* id = originalRegistry_->validate())
        return id;
    if (originalLossBinding_)
        return "RHI-ORIGINAL-HEAP-NATIVE-LOSS-JOIN-UNAVAILABLE";
    return OriginalResourceRegistrationContract::heapLowering(
        backend(), originalRegistry_->budget_.heaps)
        .diagnostic;
}

inline const char* GpuDevice::validateOriginalResourceRegistration(
    const GpuMemoryHeap& heap) const noexcept
{
    const auto ticket = heap.originalResourceRegistration();
    if (const auto* id = validateOriginalResourceRegistration(ticket))
        return id;
    return ticket.matchesIdentity(
               &heap, heap.id().value, OriginalRegisteredResourceKind::MemoryHeap)
        ? nullptr
        : "RHI-ORIGINAL-REGISTRATION-RESOURCE-MISMATCH";
}

inline const char* GpuDevice::originalMemoryHeapPlacementDiagnostic(
    const GpuMemoryHeap& heap) const noexcept
{
    const bool requested = originalRegistry_ && originalRegistry_->budget_.heaps.requested();
    if (!requested && !heap.originalRegistration_.record_)
        return nullptr;
    if (const auto* id = originalMemoryHeapCreationDiagnostic())
        return id;
    return validateOriginalResourceRegistration(heap);
}

template <class Factory>
inline core::UniquePtr<GpuMemoryHeap> GpuDevice::createOriginalMemoryHeap(Factory&& factory)
{
    lastOriginalRegistrationDiagnostic_ = originalMemoryHeapCreationDiagnostic();
    if (lastOriginalRegistrationDiagnostic_)
        return {};
    try {
        const bool requested = originalRegistry_ && originalRegistry_->budget_.heaps.requested();
        std::shared_ptr<OriginalResourceRegistration> record;
        if (requested) {
            record = reserveOriginalRegistrationRecord(
                OriginalRegisteredResourceKind::MemoryHeap, {}, nullptr, 0u);
            if (!record)
                return {};
        }
        auto heap = factory();
        if (!heap) {
            lastOriginalRegistrationDiagnostic_ = "RHI-ORIGINAL-REGISTRATION-CANDIDATE-MISSING";
            return {};
        }
        if (!requested)
            return heap;
        if (!heap->id().isValid() || heap->originalRegistration_.record_) {
            lastOriginalRegistrationDiagnostic_ = "RHI-ORIGINAL-REGISTRATION-CANDIDATE-IDENTITY";
            return {};
        }
        if ((lastOriginalRegistrationDiagnostic_ = originalMemoryHeapCreationDiagnostic()))
            return {};
        record->object_ = heap.get();
        record->id_ = heap->id().value;
        if ((lastOriginalRegistrationDiagnostic_ = record->validate()))
            return {};
        heap->originalRegistration_.record_ = std::move(record);
        return heap;
    } catch (const std::bad_alloc&) {
        lastOriginalRegistrationDiagnostic_ = "RHI-ORIGINAL-REGISTRATION-ALLOCATION";
        return {};
    }
}

inline const char* GpuDevice::originalPoolAllocationRegistrationDiagnostic() const noexcept
{
    if (const auto* id = originalDeviceLifetimeTicket().diagnostic())
        return id;
    return originalRegistry_ ? originalRegistry_->validate()
                             : "RG-POOL-ALLOCATION-REGISTRATION-REQUIRED";
}

inline const char* GpuDevice::originalPoolHeapAllocationRegistrationDiagnostic() const noexcept
{
    if (const auto* id = originalPoolAllocationRegistrationDiagnostic())
        return id;
    if (!originalRegistry_->budget_.heaps.requested())
        return "RG-POOL-HEAP-REGISTRATION-REQUIRED";
    return originalMemoryHeapCreationDiagnostic();
}

template <class T>
inline const char* GpuDevice::bindOriginalPoolAllocationReceipt(
    T& resource, std::shared_ptr<const OriginalPoolAllocationReceipt> receipt) const noexcept
{
    static_assert(std::is_same_v<T, GpuBuffer> || std::is_same_v<T, GpuTexture>
        || std::is_same_v<T, GpuMemoryHeap>);
    if (const auto* id = validateOriginalResourceRegistration(resource))
        return id;
    if (!receipt)
        return "RG-POOL-ALLOCATION-RECEIPT-EMPTY";
    if (const auto* id = validateOriginalResourceRegistration(receipt->registration_))
        return id;
    const auto original = resource.originalResourceRegistration();
    if (receipt->registration_.generation() != original.generation()
        || !receipt->registration_.matchesIdentity(
            &resource, resource.id().value, OriginalRegisteredResourceTraits<T>::kind))
        return "RG-POOL-ALLOCATION-RECEIPT-RESOURCE";
    auto* lifetime = resource.originalAllocationLifetime_;
    if (!lifetime)
        return "RG-POOL-ALLOCATION-OWNER-UNAVAILABLE";
    if (lifetime->poolCharge_)
        return "RG-POOL-ALLOCATION-ALREADY-BOUND";
    lifetime->poolCharge_ = std::move(receipt);
    return nullptr;
}

template <class T>
inline const OriginalPoolAllocationReceipt* GpuDevice::originalPoolAllocationReceipt(
    const T& resource, const char*& diagnostic) const noexcept
{
    static_assert(std::is_same_v<T, GpuBuffer> || std::is_same_v<T, GpuTexture>
        || std::is_same_v<T, GpuMemoryHeap>);
    diagnostic = validateOriginalResourceRegistration(resource);
    if (diagnostic)
        return nullptr;
    const auto* lifetime = resource.originalAllocationLifetime_;
    if (!lifetime || !lifetime->poolCharge_) {
        diagnostic = "RG-POOL-ALLOCATION-RECEIPT-MISSING";
        return nullptr;
    }
    return lifetime->poolCharge_.get();
}

inline GpuDevice::~GpuDevice()
{
    closeOriginalDeviceLifetimeForShutdown();
}

inline const char* GpuDevice::prepareOriginalDeviceLifetime(
    const OriginalDeviceLifetimeBudget& budget, std::shared_ptr<core::CpuMemoryQuota> quota)
{
    if (const auto* id = OriginalDeviceLifetimeContract::validateIntent(budget))
        return id;
    if (const auto* id = OriginalDeviceLifetimeContract::lowering(backend()).diagnostic)
        return id;
    if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes)
        return "RHI-ORIGINAL-DEVICE-LIFETIME-QUOTA";
    if (originalLifetime_)
        return "RHI-ORIGINAL-DEVICE-LIFETIME-ALREADY-PREPARED";
    try {
        core::CpuMemoryQuotaScope scope(quota);
        auto domain = core::makeShared<OriginalDeviceLifetime>(
            OriginalDeviceLifetime::ConstructionKey{}, this, budget, std::move(quota));
        if (const auto* id = domain->validate())
            return id;
        originalLifetime_ = std::move(domain);
        return nullptr;
    } catch (const std::bad_alloc&) {
        return "RHI-ORIGINAL-DEVICE-LIFETIME-ALLOCATION";
    }
}

inline OriginalDeviceLifetime::Ticket GpuDevice::originalDeviceLifetimeTicket() const noexcept
{
    return originalLifetime_ && !originalLifetime_->validate()
        ? OriginalDeviceLifetime::Ticket(originalLifetime_)
        : OriginalDeviceLifetime::Ticket{};
}

inline const char* GpuDevice::validateOriginalDeviceLifetime(
    const OriginalDeviceLifetime::Ticket& ticket) const noexcept
{
    const auto domain = ticket.domain_.lock();
    if (!domain)
        return "RHI-ORIGINAL-DEVICE-LIFETIME-EXPIRED";
    if (!originalLifetime_ || domain.get() != originalLifetime_.get() || domain->device_ != this)
        return "RHI-ORIGINAL-DEVICE-LIFETIME-FOREIGN-DEVICE";
    return ticket.diagnostic();
}

inline std::shared_ptr<OriginalComputeDispatchValidation>
GpuDevice::prepareOriginalComputeDispatchValidation(const ComputeDispatchValidationRequest& request,
    const ComputeDispatchValidationSessionBudget& budget,
    ComputeDispatchValidationSessionQuotas quotas,
    const char*& diagnostic)
{
    diagnostic = ComputeDispatchValidationSessionContract::inspect(request, budget);
    if (diagnostic
        || (diagnostic =
                ComputeDispatchValidationSessionContract::reservationDiagnostic(backend())))
        return {};
    if ((diagnostic = originalDeviceNativeLossDiagnostic())
        || (diagnostic = ComputeDispatchValidationSessionContract::inspectQuotas(budget, quotas)))
        return {};
    for (const auto& quota : {quotas.session, quotas.artifact, quotas.input, quotas.pipeline})
        if (overlapsOriginalDeviceCpuOwner(quota)) {
            diagnostic = "RHI-DISPATCH-SESSION-DEVICE-QUOTA-OVERLAP";
            return {};
        }
    auto prepared =
        prepareOriginalComputeDispatchValidationPayload(request, budget, quotas, diagnostic);
    if (!prepared) {
        if (!diagnostic)
            diagnostic = "RHI-DISPATCH-PREPARATION-PAYLOAD";
        return {};
    }
    if (diagnostic || !prepared->sharedOwnerMatches(prepared)
        || !core::SharedOwnerProjection::sameOwner(prepared->quota_, quotas.session)
        || (diagnostic = validateOriginalDeviceLifetime(prepared->lifetime_))) {
        // Rejecting a borrowed/foreign facade cannot mutate its original work.
        // The original callback retains any submitted native owners regardless.
        if (!diagnostic)
            diagnostic = "RHI-DISPATCH-PREPARATION-OWNER";
        return {};
    }
    return prepared;
}

inline std::shared_ptr<const ComputeDispatchValidationDomain>
GpuDevice::issueOriginalComputeDispatchDomain(
    const std::shared_ptr<OriginalComputeDispatchValidation>& prepared,
    const ComputeDispatchValidationDomainBudget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    const char*& diagnostic)
{
    diagnostic = nullptr;
    if (!prepared || !prepared->sharedOwnerMatches(prepared)) {
        diagnostic = "RHI-DISPATCH-PREPARATION-OWNER";
        return {};
    }
    if ((diagnostic = validateOriginalDeviceLifetime(prepared->lifetime_)))
        return {};
    return prepared->operations_.issue(
        prepared->session_.get(), *this, budget, std::move(quota), diagnostic);
}

inline std::shared_ptr<const ComputeDispatchValidationReservation>
GpuDevice::reserveOriginalComputeDispatchValidation(const ComputeDispatchValidationRequest& request,
    const ComputeDispatchValidationSessionBudget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    const char*& diagnostic)
{
    diagnostic = ComputeDispatchValidationSessionContract::inspect(request, budget);
    if (diagnostic
        || (diagnostic =
                ComputeDispatchValidationSessionContract::reservationDiagnostic(backend())))
        return {};
    if ((diagnostic = originalDeviceNativeLossDiagnostic()))
        return {};
    if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes
        || overlapsOriginalDeviceCpuOwner(quota)) {
        diagnostic = "RHI-DISPATCH-SESSION-QUOTA-LIMIT";
        return {};
    }
    bool expected = false;
    if (!originalLifetime_->computeDispatchValidationPending_.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel, std::memory_order_acquire)) {
        diagnostic = "RHI-DISPATCH-SESSION-DEVICE-BUSY";
        return {};
    }
    try {
        core::CpuMemoryQuotaScope scope(std::move(quota));
        return core::makeShared<ComputeDispatchValidationReservation>(
            ComputeDispatchValidationReservation::ConstructionKey{}, originalLifetime_);
    } catch (const std::bad_alloc&) {
        originalLifetime_->computeDispatchValidationPending_.store(
            false, std::memory_order_release);
        diagnostic = "RHI-DISPATCH-SESSION-CPU-ALLOCATION";
        return {};
    }
}

inline std::shared_ptr<const ComputeDispatchValidationDomain>
GpuDevice::issueOriginalComputeDispatchDomain(const ComputeDispatchValidationRequest& request,
    const OriginalDeviceLifetime::Ticket& lifetime,
    const OriginalDeviceNativeLossObservation& loss,
    const ComputeDispatchValidationDomainBudget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    std::shared_ptr<const void> proof,
    ComputeDispatchValidationDomain::ProofDiagnostic validateProof,
    const char*& diagnostic)
{
    diagnostic =
        ComputeDispatchValidationDomainContract::inspect(budget, quota ? quota->limitBytes() : 0u);
    if (diagnostic
        || (diagnostic = ComputeDispatchValidationDomainContract::issuanceDiagnostic(backend())))
        return {};
    if ((diagnostic = validateOriginalDeviceLifetime(lifetime))
        || (diagnostic = validateOriginalDeviceNativeLoss(loss)))
        return {};
    if (!proof || !validateProof) {
        diagnostic = "RHI-DISPATCH-DOMAIN-PROOF";
        return {};
    }
    if ((diagnostic = validateProof(proof.get())))
        return {};
    if (overlapsOriginalDeviceCpuOwner(quota)) {
        diagnostic = "RHI-DISPATCH-DOMAIN-QUOTA-OVERLAP";
        return {};
    }
    ComputeDispatchValidationPlan checked;
    if ((diagnostic = ComputeDispatchValidationContract::inspect(request, checked)))
        return {};
    try {
        core::CpuMemoryQuotaScope scope(quota);
        return core::makeShared<ComputeDispatchValidationDomain>(
            ComputeDispatchValidationDomain::ConstructionKey{},
            request,
            lifetime,
            loss,
            budget,
            std::move(quota),
            std::move(proof),
            validateProof);
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-DISPATCH-DOMAIN-CPU-ALLOCATION";
        return {};
    }
}

inline const char* GpuDevice::validateOriginalComputeDispatchDomain(
    const std::shared_ptr<const ComputeDispatchValidationDomain>& domain,
    Extent3D workgroup,
    Extent3D groups) const noexcept
{
    if (!domain || !domain->sharedOwnerMatches(domain))
        return "RHI-DISPATCH-DOMAIN-OWNER";
    if (const auto* id = validateOriginalDeviceLifetime(domain->lifetime_))
        return id;
    if (const auto* id = validateOriginalDeviceNativeLoss(domain->loss_))
        return id;
    if (const auto* id = domain->diagnostic())
        return id;
    return ComputeDispatchValidationContract::contains(domain->request_, workgroup, groups);
}

inline void GpuDevice::retireOriginalDeviceLifetime() noexcept
{
    if (originalLifetime_)
        originalLifetime_->invalidate(OriginalDeviceLifetime::Terminal::Retired);
    if (originalLossBinding_)
        originalLossBinding_->retireObservers();
}

inline const char* GpuDevice::prepareOriginalDeviceNativeResultLoss(
    const OriginalDeviceNativeResultLossBudget& budget, std::shared_ptr<core::CpuMemoryQuota> quota)
{
    if (const auto* id = OriginalDeviceNativeResultLossContract::validateIntent(
            budget, quota ? quota->limitBytes() : 0u))
        return id;
    const auto lowering = OriginalDeviceNativeResultLossContract::lowering(backend());
    if (!lowering.bindingAvailable || lowering.diagnostic)
        return lowering.diagnostic ? lowering.diagnostic : "RHI-ORIGINAL-RESULT-LOSS-LOWERING";
    return prepareOriginalLossBinding(
        std::move(quota), [&](const auto& receiver, auto metadata, const char*& diagnostic) {
            return bindOriginalDeviceNativeResultLoss(
                receiver, budget, std::move(metadata), diagnostic);
        });
}

inline const char* GpuDevice::prepareOriginalDeviceNativeLoss(
    const OriginalDeviceNativeLossBudget& budget, std::shared_ptr<core::CpuMemoryQuota> quota)
{
    if (const auto* id = OriginalDeviceNativeLossContract::validateIntent(
            budget, quota ? quota->limitBytes() : 0u))
        return id;
    if (const auto* id = OriginalDeviceNativeLossContract::lowering(backend()).diagnostic)
        return id;
    return prepareOriginalLossBinding(
        std::move(quota), [&](const auto& receiver, auto metadata, const char*& diagnostic) {
            return bindOriginalDeviceNativeLoss(receiver, budget, std::move(metadata), diagnostic);
        });
}

inline const char* GpuDevice::prepareOriginalDeviceNativeLoss(
    const OriginalDeviceNativeLossBudget& budget,
    const OriginalDeviceNativeLossObserverRequest& request,
    std::shared_ptr<core::CpuMemoryQuota> quota)
{
    OriginalDeviceNativeLossObserverContract::Requirements requirements;
    if (const auto* id = OriginalDeviceNativeLossObserverContract::inspect(
            budget, request, quota ? quota->limitBytes() : 0u, requirements))
        return id;
    const auto lowering = OriginalDeviceNativeLossObserverContract::lowering(backend());
    if (!lowering.bindingAvailable)
        return lowering.diagnostic;
    return prepareOriginalLossBinding(
        std::move(quota), [&](const auto& receiver, auto metadata, const char*& diagnostic) {
            return bindOriginalObservedNativeLoss(
                receiver, budget, request, std::move(metadata), diagnostic);
        });
}

template <class Bind>
inline const char* GpuDevice::prepareOriginalLossBinding(
    std::shared_ptr<core::CpuMemoryQuota> quota, Bind&& bind)
{
    if (!originalLifetime_)
        return "RHI-ORIGINAL-DEVICE-LOSS-DOMAIN-UNPREPARED";
    if (const auto* id = originalLifetime_->validate())
        return id;
    if (originalLossBinding_)
        return "RHI-ORIGINAL-DEVICE-LOSS-ALREADY-PREPARED";
    if (originalRegistry_)
        return "RHI-ORIGINAL-DEVICE-LOSS-REGISTRY-ALREADY-PREPARED";
    if (core::SharedOwnerProjection::sameControlBlock(quota, originalLifetime_->quota_))
        return "RHI-ORIGINAL-DEVICE-LOSS-QUOTA-DOMAIN";
    const char* diagnostic = nullptr;
    auto binding = bind(OriginalDeviceLifetime::NativeLossReceiver(originalLifetime_),
        std::move(quota),
        diagnostic);
    if (!binding)
        return diagnostic ? diagnostic : "RHI-ORIGINAL-DEVICE-LOSS-PAYLOAD-MISSING";
    if (!binding->hasQuotaBoundSharedOwner(binding)) {
        binding->detach();
        return "RHI-ORIGINAL-DEVICE-LOSS-PAYLOAD-OWNER";
    }
    if (const auto* id = binding->diagnostic()) {
        binding->detach();
        return id;
    }
    originalLossBinding_ = std::move(binding);
    return nullptr;
}

inline const char* GpuDevice::originalDeviceNativeLossDiagnostic() const noexcept
{
    if (!originalLifetime_)
        return "RHI-ORIGINAL-DEVICE-LOSS-DOMAIN-UNPREPARED";
    if (const auto* id = originalLifetime_->validate())
        return id;
    return originalLossBinding_ ? originalLossBinding_->diagnostic()
                                : "RHI-ORIGINAL-DEVICE-LOSS-UNPREPARED";
}

inline std::optional<OriginalDeviceNativeLossObservation>
GpuDevice::observeOriginalDeviceNativeLoss(const char*& diagnostic) const noexcept
{
    diagnostic = originalDeviceNativeLossDiagnostic();
    if (diagnostic)
        return {};
    if (!originalLossBinding_->hasQuotaBoundSharedOwner(originalLossBinding_)) {
        diagnostic = "RHI-ORIGINAL-DEVICE-LOSS-OBSERVATION-OWNER";
        return {};
    }
    OriginalDeviceNativeLossObservation result(
        originalLifetime_, originalDeviceLifetimeTicket(), originalLossBinding_);
    if ((diagnostic = result.diagnostic()))
        return {};
    return result;
}

inline const char* GpuDevice::validateOriginalDeviceNativeLoss(
    const OriginalDeviceNativeLossObservation& observation) const noexcept
{
    if (!observation.domain_ || !observation.binding_)
        return "RHI-ORIGINAL-DEVICE-LOSS-OBSERVATION-UNISSUED";
    if (!core::SharedOwnerProjection::sameOwner(originalLifetime_, observation.domain_)
        || !core::SharedOwnerProjection::sameOwner(originalLossBinding_, observation.binding_)
        || observation.domain_->device_ != this)
        return "RHI-ORIGINAL-DEVICE-LOSS-OBSERVATION-FOREIGN";
    return observation.diagnostic();
}

inline const char* GpuDevice::validateOriginalTypedNativeLossJoin(
    const OriginalDeviceNativeLossObservation& observation) const noexcept
{
    return validateOriginalTypedNativeLossJoin(observation, {1u, true});
}

inline const char* GpuDevice::validateOriginalTypedNativeLossJoin(
    const OriginalDeviceNativeLossObservation& observation,
    const OriginalTypedNativeLossJoinRequest& request) const noexcept
{
    if (const auto* id = validateOriginalDeviceNativeLoss(observation))
        return id;
    if (request.contractVersion != 1u && request.contractVersion != 2u
        && request.contractVersion != 3u && request.contractVersion != 4u)
        return "RHI-ORIGINAL-TYPED-LOSS-JOIN-VERSION";
    if (!request.prototypeOptIn)
        return "RHI-ORIGINAL-TYPED-LOSS-JOIN-OPT-IN";
    if (!originalRegistry_
        || originalRegistry_->budget_.contractVersion
            != (request.contractVersion == 1u
                    ? OriginalTypedNativeLossJoinContract::registrationVersion
                    : request.contractVersion == 2u
                    ? OriginalTypedNativeLossJoinContract::tableRegistrationVersion
                    : OriginalTypedNativeLossJoinContract::synchronousRegistrationVersion))
        return request.contractVersion == 1u
            ? "RHI-ORIGINAL-NATIVE-CLOSURE-TYPED-LOSS-WEBGPU-JOIN-MISSING"
            : "RHI-ORIGINAL-NATIVE-CLOSURE-TABLE-LOSS-WEBGPU-JOIN-MISSING";
    if (const auto* id =
            OriginalTypedNativeLossJoinContract::validate(request, originalRegistry_->budget_))
        return id;
    if (request.contractVersion == 4u) {
        const auto decision = OriginalTypedNativeLossJoinContract::lowering(backend(), 4u);
        if (!decision.scopedGroupJoinAvailable)
            return decision.diagnostic;
    }
    if (const auto* id = originalRegistry_->validate())
        return id;
    const auto subscription = originalRegistry_->loss_.lock();
    if (!core::SharedOwnerProjection::sameOwner(subscription, observation.binding_)
        || !core::SharedOwnerProjection::sameOwner(originalRegistry_->domain_, observation.domain_))
        return "RHI-ORIGINAL-TYPED-LOSS-REGISTRATION-SUBSCRIPTION";
    return nullptr;
}

inline const char* GpuDevice::detachOriginalDeviceNativeLoss() noexcept
{
    if (!originalLossBinding_)
        return "RHI-ORIGINAL-DEVICE-LOSS-UNPREPARED";
    if (const auto* id = originalLossBinding_->detach())
        return id;
    retireOriginalDeviceLifetime();
    return nullptr;
}

inline const char* GpuDevice::prepareOriginalResourceRegistration(
    const OriginalResourceRegistrationBudget& budget, std::shared_ptr<core::CpuMemoryQuota> quota)
{
    if (const auto* id = OriginalResourceRegistrationContract::validateIntent(budget))
        return id;
    if (const auto* id =
            OriginalResourceRegistrationContract::lowering(backend(), budget.contractVersion)
                .diagnostic)
        return id;
    return prepareOriginalResourceRegistrationImpl(budget, std::move(quota), {});
}

inline const char* GpuDevice::prepareOriginalTypedNativeLossRegistration(
    const OriginalTypedNativeLossJoinRequest& request,
    const OriginalResourceRegistrationBudget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    const OriginalDeviceNativeLossObservation& loss)
{
    if (const auto* id = OriginalTypedNativeLossJoinContract::validate(request, budget))
        return id;
    if (budget.queryNativeLoss.requested()) {
        if (const auto* id = OriginalQueryNativeLossJoinContract::validate(budget))
            return id;
        const auto query = OriginalQueryNativeLossJoinContract::lowering(backend());
        if (!query.nativeSourceAvailable)
            return query.diagnostic;
    }
    if (budget.samplerNativeLoss.requested()) {
        if (const auto* id =
                OriginalSamplerNativeLossJoinContract::validateRegistration(budget, 3u))
            return id;
        const auto sampler = OriginalSamplerNativeLossJoinContract::lowering(backend(), 3u);
        if (!sampler.nativeSourceAvailable)
            return sampler.diagnostic;
    }
    const auto decision =
        OriginalTypedNativeLossJoinContract::lowering(backend(), request.contractVersion);
    if (!decision.registrationAvailable)
        return decision.diagnostic;
    if (const auto* id = validateOriginalDeviceNativeLoss(loss))
        return id;
    if (!quota
        || core::SharedOwnerProjection::overlapsOwner(
            quota, originalLossBinding_->originalMetadataQuota()))
        return "RHI-ORIGINAL-TYPED-LOSS-QUOTA-DOMAIN";
    return prepareOriginalResourceRegistrationImpl(budget, std::move(quota), originalLossBinding_);
}

inline const char* GpuDevice::prepareOriginalDescriptorNativeLossRegistration(
    const OriginalTypedNativeLossJoinRequest& request,
    const OriginalResourceRegistrationBudget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    const OriginalDeviceNativeLossObservation& loss)
{
    using Contract = OriginalDescriptorNativeLossRegistrationContract;
    if (const auto* id = Contract::validate(request, budget))
        return id;
    const auto decision = Contract::lowering(backend(), request.contractVersion);
    if (!decision.registrationAvailable)
        return decision.diagnostic;
    if (const auto* id = validateOriginalDeviceNativeLoss(loss))
        return id;
    if (!quota || loss.overlapsMetadataOwner(quota))
        return "RHI-ORIGINAL-DESCRIPTOR-LOSS-QUOTA-DOMAIN";
    return prepareOriginalResourceRegistrationImpl(budget, std::move(quota), originalLossBinding_);
}

inline const char* GpuDevice::validateOriginalNativeLossDescriptor(
    const DescriptorArena&) const noexcept
{
    return "RHI-ORIGINAL-DESCRIPTOR-LOSS-ARENA-SOURCE-MISSING";
}

inline const char* GpuDevice::validateOriginalNativeLossDescriptor(
    const DescriptorSet&) const noexcept
{
    return "RHI-ORIGINAL-DESCRIPTOR-LOSS-SET-SOURCE-MISSING";
}

inline const char* GpuDevice::prepareOriginalResourceRegistrationImpl(
    const OriginalResourceRegistrationBudget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    std::weak_ptr<const OriginalDeviceNativeLossBinding> loss)
{
    if (budget.samplerNativeLoss.requested()) {
        if (loss.expired())
            return "RHI-ORIGINAL-SYNC-SAMPLER-LOSS-SUBSCRIPTION";
        if (const auto* id =
                OriginalSamplerNativeLossJoinContract::validateRegistration(budget, 3u))
            return id;
        const auto sampler = OriginalSamplerNativeLossJoinContract::lowering(backend(), 3u);
        if (!sampler.nativeSourceAvailable)
            return sampler.diagnostic;
    }
    if (budget.queries.requested()) {
        if (const auto* id =
                OriginalResourceRegistrationContract::queryLowering(backend(), budget.queries)
                    .diagnostic)
            return id;
        if (originalLossBinding_ || !loss.expired() || budget.queryNativeLoss.requested()) {
            if (!OriginalResourceRegistrationContract::joinsQueryNativeLoss(budget))
                return "RHI-ORIGINAL-QUERY-NATIVE-LOSS-JOIN-UNAVAILABLE";
            if (const auto* id = OriginalQueryNativeLossJoinContract::validate(budget))
                return id;
            const auto decision = OriginalQueryNativeLossJoinContract::lowering(backend());
            if (!decision.nativeSourceAvailable)
                return decision.diagnostic;
            const auto subscription = loss.lock();
            if (!subscription
                || !core::SharedOwnerProjection::sameOwner(subscription, originalLossBinding_))
                return "RHI-ORIGINAL-QUERY-LOSS-SUBSCRIPTION";
        }
    }
    if (budget.heaps.requested()) {
        if (const auto* id =
                OriginalResourceRegistrationContract::heapLowering(backend(), budget.heaps)
                    .diagnostic)
            return id;
        if (originalLossBinding_ || !loss.expired())
            return "RHI-ORIGINAL-HEAP-NATIVE-LOSS-JOIN-UNAVAILABLE";
    }
    if (!originalLifetime_)
        return "RHI-ORIGINAL-REGISTRATION-DEVICE-UNPREPARED";
    if (const auto* id = originalLifetime_->validate())
        return id;
    if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes
        || quota.get() == originalLifetime_->quota_.get())
        return "RHI-ORIGINAL-REGISTRATION-QUOTA";
    if (originalRegistry_)
        return "RHI-ORIGINAL-REGISTRATION-ALREADY-PREPARED";
    try {
        core::CpuMemoryQuotaScope scope(quota);
        auto registry = core::makeShared<OriginalResourceRegistrationRegistry>(
            OriginalResourceRegistrationRegistry::ConstructionKey{},
            this,
            originalLifetime_,
            originalDeviceLifetimeTicket(),
            budget,
            std::move(quota),
            std::move(loss));
        if (const auto* id = registry->validate())
            return id;
        originalRegistry_ = std::move(registry);
        return nullptr;
    } catch (const std::bad_alloc&) {
        return "RHI-ORIGINAL-REGISTRATION-ALLOCATION";
    }
}

template <class Pipeline>
inline const char* GpuDevice::validateOriginalTypedNativeLossGroupInputs(
    const DescriptorSet& object,
    const std::shared_ptr<Pipeline>& pipeline,
    const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>& quotas,
    const OriginalTypedNativeLossJoinRequest* synchronousJoin) const noexcept
{
    if (synchronousJoin) {
        if (!originalRegistry_)
            return "RHI-ORIGINAL-REGISTRATION-UNPREPARED";
        if (synchronousJoin->contractVersion != 4u)
            return "RHI-ORIGINAL-TYPED-LOSS-SYNC-GROUP-VERSION";
        if (const auto* id = OriginalTypedNativeLossJoinContract::validate(
                *synchronousJoin, originalRegistry_->budget_))
            return id;
        const char* id = nullptr;
        const auto loss = observeOriginalDeviceNativeLoss(id);
        if (!loss)
            return id;
        if (const auto* failure = validateOriginalTypedNativeLossJoin(*loss, *synchronousJoin))
            return failure;
        if (const auto* failure = validateOriginalResourceRegistration(object))
            return failure;
        if (!pipeline || !pipeline->sharedOwnerMatches(pipeline)
            || !pipeline->originalAcceptedSharedQuotaBound())
            return "RHI-ORIGINAL-FINITE-WRITE-PIPELINE";
        if (const auto* failure = validateOriginalResourceRegistration(*pipeline))
            return failure;
        const auto& arena = object.originalArenaOwner_;
        if (!arena || !arena->sharedOwnerMatches(arena))
            return "RHI-ORIGINAL-GENERAL-WRITE-ARENA";
        if (const auto* failure = validateOriginalResourceRegistration(*arena))
            return failure;
        if (!object.originalResourceRegistration().matchesParent(
                arena->originalResourceRegistration()))
            return "RHI-ORIGINAL-GENERAL-WRITE-ARENA-PARENT";
        for (std::size_t i = 0u; i < quotas.size(); ++i) {
            const auto& quota = quotas[i];
            if (!quota || loss->overlapsMetadataOwner(quota))
                return "RHI-ORIGINAL-TYPED-LOSS-SYNC-GROUP-QUOTA";
            if (const auto* failure =
                    object.originalResourceRegistration().independentCpuQuotaDiagnostic(quota))
                return failure;
            if (const auto* failure =
                    arena->originalResourceRegistration().independentCpuQuotaDiagnostic(quota))
                return failure;
            if (const auto* failure = pipeline->independentAcceptedCpuQuotaDiagnostic(quota))
                return failure;
            for (std::size_t j = 0u; j < i; ++j)
                if (core::SharedOwnerProjection::overlapsOwner(quota, quotas[j]))
                    return "RHI-ORIGINAL-TYPED-LOSS-SYNC-GROUP-QUOTA";
        }
        return nullptr;
    }
    if (originalRegistry_->budget_.contractVersion == 8u)
        return "RHI-ORIGINAL-TABLE-GROUP-PUBLICATION-MISSING";
    if (originalRegistry_->budget_.contractVersion
        != OriginalTypedNativeLossJoinContract::registrationVersion)
        return nullptr;
    if (!pipeline)
        return "RHI-ORIGINAL-FINITE-WRITE-PIPELINE";
    if (const auto* id = validateOriginalResourceRegistration(*pipeline))
        return id;
    for (const auto& quota : quotas)
        if (const auto* id =
                object.originalResourceRegistration().independentCpuQuotaDiagnostic(quota))
            return id;
    return nullptr;
}

inline const char* GpuDevice::validateOriginalResourceRegistration(
    const OriginalResourceRegistrationTicket& ticket) const noexcept
{
    const auto record = ticket.record_.lock();
    if (!record)
        return "RHI-ORIGINAL-REGISTRATION-EXPIRED";
    if (!originalRegistry_ || record->registry_.get() != originalRegistry_.get()
        || record->registry_->device_ != this)
        return "RHI-ORIGINAL-REGISTRATION-FOREIGN-DEVICE";
    return record->validate();
}

template <class T>
inline const char* GpuDevice::validateOriginalResourceRegistration(const T& resource) const noexcept
{
    if constexpr (std::is_base_of_v<GpuMemoryHeap, T>) {
        return validateOriginalResourceRegistration(static_cast<const GpuMemoryHeap&>(resource));
    } else if constexpr (std::is_base_of_v<QueryPool, T>) {
        const auto& object = static_cast<const QueryPool&>(resource);
        const auto ticket = object.originalResourceRegistration();
        if (const auto* id = validateOriginalResourceRegistration(ticket))
            return id;
        if (!ticket.matchesObjectIdentity(&object, OriginalRegisteredResourceKind::QueryPool))
            return "RHI-ORIGINAL-REGISTRATION-RESOURCE-MISMATCH";
        const auto owner = object.originalFactoryOwner_.lock();
        if (!object.matchesOriginalFactoryOwner(owner) || !object.originalOwnerQuota_)
            return "RHI-ORIGINAL-QUERY-REGISTRATION-SHARED-OWNER";
        if (const auto* id = OriginalResourceRegistrationContract::queryLowering(
                backend(), originalRegistry_->budget_.queries)
                .diagnostic)
            return id;
        return validateOriginalQueryPoolCandidate(owner);
    } else if constexpr (std::is_base_of_v<OriginalRegisteredObject, T>) {
        const auto& object = [&]() -> const auto& {
            if constexpr (std::is_base_of_v<DescriptorObject, T>
                || std::is_base_of_v<DescriptorArena, T>)
                return static_cast<const typename OriginalRegisteredDescriptorTraits<T>::Base&>(
                    resource);
            else
                return static_cast<const typename OriginalRegisteredPipelineTraits<T>::Base&>(
                    resource);
        }();
        const auto ticket = object.originalResourceRegistration();
        if (const auto* id = validateOriginalResourceRegistration(ticket))
            return id;
        if (!ticket.matchesObjectIdentity(&object, object.originalRegisteredKind()))
            return "RHI-ORIGINAL-REGISTRATION-RESOURCE-MISMATCH";
        if constexpr (std::is_base_of_v<ComputePipeline, T> || std::is_base_of_v<RenderPipeline, T>)
            if (originalRegistry_->budget_.contractVersion == 8u) {
                if (const auto* id = object.originalFullDefinitionDiagnostic())
                    return id;
                if constexpr (std::is_base_of_v<ComputePipeline, T>) {
                    if (const auto* id = object.originalArtifactComputeDiagnostic())
                        return id;
                    if (!PipelineDefinitionIdentity::matches(
                            *object.originalComputePipelineDefinition(),
                            object.originalComputeCreation()->definition()))
                        return "RHI-ORIGINAL-TABLE-REGISTRATION-DEFINITION";
                } else {
                    if (const auto* id = object.originalArtifactGraphicsDiagnostic())
                        return id;
                    if (!PipelineDefinitionIdentity::matches(
                            *object.originalRenderPipelineDefinition(),
                            object.originalGraphicsCreation()->definition()))
                        return "RHI-ORIGINAL-TABLE-REGISTRATION-DEFINITION";
                }
            }
        if constexpr (std::is_base_of_v<ComputePipeline, T> || std::is_base_of_v<RenderPipeline, T>)
            if (OriginalResourceRegistrationContract::observesNativeLoss(
                    originalRegistry_->budget_.contractVersion))
                return validateOriginalNativeLossPipeline(object);
        return nullptr;
    } else {
        using Traits = OriginalRegisteredResourceTraits<T>;
        const auto& object = static_cast<const typename Traits::Base&>(resource);
        const auto ticket = object.originalResourceRegistration();
        if (const auto* id = validateOriginalResourceRegistration(ticket))
            return id;
        return ticket.matchesIdentity(&object, object.id().value, Traits::kind)
            ? nullptr
            : "RHI-ORIGINAL-REGISTRATION-RESOURCE-MISMATCH";
    }
}

inline std::string_view GpuDevice::lastOriginalResourceRegistrationDiagnostic() const noexcept
{
    return lastOriginalRegistrationDiagnostic_ ? lastOriginalRegistrationDiagnostic_ : "";
}

template <class Pipeline>
inline const char* GpuDevice::diagnoseOriginalRuntimeLengthInterval(
    const OriginalDescriptorBufferBindingContract::IntervalDiagnosticRequest& request,
    const std::shared_ptr<Pipeline>& pipeline,
    const std::shared_ptr<const OriginalCommandBufferLease>& buffer,
    std::uint32_t set,
    std::uint32_t binding,
    const DescriptorBufferBindingRange& range,
    std::uint32_t dynamicOffset,
    OriginalDescriptorBufferBindingContract::IntervalDiagnosticObservation& output) const noexcept
{
    return diagnoseOriginalRuntimeProfileBufferIntervalImpl<false>(
        request, pipeline, buffer, set, binding, range, dynamicOffset, output, true);
}

template <class Pipeline>
inline const char* GpuDevice::diagnoseOriginalRuntimeProfileBufferInterval(
    const OriginalDescriptorBufferBindingContract::IntervalDiagnosticRequest& request,
    const std::shared_ptr<Pipeline>& pipeline,
    const std::shared_ptr<const OriginalCommandBufferLease>& buffer,
    std::uint32_t set,
    std::uint32_t binding,
    const DescriptorBufferBindingRange& range,
    std::uint32_t dynamicOffset,
    OriginalDescriptorBufferBindingContract::IntervalDiagnosticObservation& output) const noexcept
{
    return diagnoseOriginalRuntimeProfileBufferIntervalImpl<false>(
        request, pipeline, buffer, set, binding, range, dynamicOffset, output, false);
}

template <class Pipeline>
inline const char* GpuDevice::diagnoseOriginalTargetMemoryBufferInterval(
    const OriginalTargetMemoryIntervalContract::Request& request,
    const std::shared_ptr<Pipeline>& pipeline,
    const std::shared_ptr<const OriginalCommandBufferLease>& buffer,
    std::uint32_t set,
    std::uint32_t binding,
    const DescriptorBufferBindingRange& range,
    std::uint32_t dynamicOffset,
    OriginalTargetMemoryIntervalContract::Observation& output) const noexcept
{
    return diagnoseOriginalRuntimeProfileBufferIntervalImpl<true>(
        request, pipeline, buffer, set, binding, range, dynamicOffset, output, false);
}

template <bool targetMemory, class Request, class Pipeline, class Observation>
inline const char* GpuDevice::diagnoseOriginalRuntimeProfileBufferIntervalImpl(
    const Request& request,
    const std::shared_ptr<Pipeline>& pipeline,
    const std::shared_ptr<const OriginalCommandBufferLease>& buffer,
    std::uint32_t set,
    std::uint32_t binding,
    const DescriptorBufferBindingRange& range,
    std::uint32_t dynamicOffset,
    Observation& output,
    bool requireOperation) const noexcept
{
    using P = std::remove_const_t<Pipeline>;
    static_assert(std::is_same_v<P, ComputePipeline> || std::is_same_v<P, RenderPipeline>);
    using Contract = OriginalDescriptorBufferBindingContract;
    output = {};
    if constexpr (targetMemory) {
        if (const auto* id = OriginalTargetMemoryIntervalContract::validateIntent(request))
            return id;
        const auto decision = OriginalTargetMemoryIntervalContract::lowering(backend());
        if (!decision.diagnosticAvailable)
            return decision.diagnostic;
    } else {
        if (const auto* id = Contract::validateIntervalDiagnosticRequest(request))
            return id;
        const auto decision = Contract::intervalDiagnosticLowering(backend());
        if (!decision.captureAvailable)
            return decision.diagnostic;
    }
    if (!pipeline || !pipeline->sharedOwnerMatches(pipeline)
        || !pipeline->originalAcceptedSharedQuotaBound())
        return "RHI-ORIGINAL-RUNTIME-INTERVAL-PIPELINE-OWNER";
    if (const auto* id = validateOriginalResourceRegistration(*pipeline))
        return id;
    if (const auto* id = OriginalCommandBufferLease::validateOriginal(buffer))
        return id;
    if (const auto* id = validateOriginalResourceRegistration(*buffer->originalObject()))
        return id;
    const auto input = [&] {
        if constexpr (std::is_same_v<P, ComputePipeline>)
            return pipeline->originalComputeCreation()
                ? pipeline->originalComputeCreation()->originalInput()
                : std::shared_ptr<const OriginalSingleEntryPipelineInput>{};
        else
            return pipeline->originalGraphicsCreation()
                ? pipeline->originalGraphicsCreation()->originalInput()
                : std::shared_ptr<const OriginalGraphicsPipelineInput>{};
    }();
    if constexpr (std::is_same_v<P, ComputePipeline>) {
        if (const auto* id = pipeline->originalArtifactComputeDiagnostic())
            return id;
    } else if (const auto* id = pipeline->originalArtifactGraphicsDiagnostic())
        return id;
    using Input = std::remove_const_t<typename decltype(input)::element_type>;
    if (const auto* id = Input::packageDiagnostic(input, *pipeline->originalPackageOwner()))
        return id;
    const auto* slot = findDescriptorSetSlot(pipeline->originalLayoutOwner()->desc(), set);
    const auto* layout = slot
        ? findDescriptorSetLayout(pipeline->originalLayoutOwner()->desc(), slot->layoutName)
        : nullptr;
    const auto* selected = layout ? findDescriptorSetBinding(*layout, binding) : nullptr;
    if (!selected || selected->arrayCount != 1u || selected->tableBacked
        || selected->stages == ShaderStage::None)
        return "RHI-ORIGINAL-RUNTIME-INTERVAL-BINDING";
    if (dynamicOffset && !selected->dynamicOffset)
        return "RHI-ORIGINAL-RUNTIME-INTERVAL-DYNAMIC-BINDING";
    const auto type = selected->type;
    if constexpr (targetMemory)
        if (const auto* id =
                OriginalTargetMemoryIntervalContract::validateType(type, request.contractVersion))
            return id;
    const auto usage = type == DescriptorType::UniformBuffer ? ResourceUsage::UniformBuffer
                                                             : ResourceUsage::StorageBuffer;
    if ((buffer->layout().usage & usage) != usage)
        return "RHI-ORIGINAL-TYPED-DESCRIPTOR-RESOURCE-USAGE";
    OriginalRuntimeLengthIntervalContract::Facts facts;
    if constexpr (targetMemory) {
        if (const auto* id = OriginalTargetBufferDeclarationContract::diagnose(
                input->artifact(), set, binding, type, facts))
            return id;
    } else {
        const auto* factsId = requireOperation
            ? OriginalRuntimeLengthIntervalContract::diagnose(
                  input->artifact(), set, binding, type, facts)
            : OriginalRuntimeLengthIntervalContract::diagnoseBinding(
                  input->artifact(), set, binding, type, facts);
        if (factsId)
            return factsId;
    }
    if (!descriptorSetStageCoverage(selected->stages, static_cast<ShaderStage>(facts.stageMask)))
        return "RHI-ORIGINAL-RUNTIME-INTERVAL-STAGE-COVERAGE";
    Observation observed;
    if (const auto* id = Contract::validateFiniteInterval(type,
            range,
            buffer->layout().sizeBytes,
            dynamicOffset,
            facts.target.minimumBindingBytes,
            pipeline->originalDescriptorBufferLimits_,
            observed.range))
        return id;
    if constexpr (!targetMemory)
        if (facts.target.hasRuntimeArray)
            if (const auto* id = facts.target.runtimeArrayLength(
                    observed.range.sizeBytes, observed.runtimeLength))
                return id;
    observed.minimumBindingBytes = facts.target.minimumBindingBytes;
    observed.runtimeStrideBytes = facts.target.runtimeArrayStrideBytes;
    observed.group = facts.target.group;
    observed.binding = facts.target.binding;
    observed.stageMask = facts.stageMask;
    output = observed;
    return nullptr;
}

template <class Pipeline, class Request>
inline const char* GpuDevice::diagnoseOriginalFiniteDescriptorSource(const Request& request,
    const DescriptorSet& object,
    const std::shared_ptr<Pipeline>& pipeline,
    std::uint32_t set,
    const OriginalFiniteDescriptorBufferWrite* writes,
    std::size_t count,
    const std::shared_ptr<core::CpuMemoryQuota>& quota) const noexcept
{
    constexpr bool targetMemory =
        std::is_same_v<Request, OriginalFiniteTargetMemorySourceDiagnosticRequest>;
    using Contract = std::conditional_t<targetMemory,
        OriginalFiniteTargetMemoryWriteContract,
        OriginalFiniteDescriptorWriteContract>;
    using P = std::remove_const_t<Pipeline>;
    static_assert(std::is_same_v<P, ComputePipeline> || std::is_same_v<P, RenderPipeline>);
    if (const auto* id = request.diagnostic())
        return id;
    if (const auto* id = Contract::sourceDiagnosticDecision(backend()).diagnostic)
        return id;
    if (!quota || quota->limitBytes() != request.sourceCpuBytes)
        return "RHI-ORIGINAL-FINITE-SOURCE-QUOTA";
    if (const auto* id = validateOriginalResourceRegistration(object))
        return id;
    if (originalRegistry_->budget_.contractVersion
            == OriginalTypedNativeLossJoinContract::registrationVersion
        && !OriginalTypedNativeLossJoinContract::lowering(backend()).scopedGroupJoinAvailable)
        return OriginalTypedNativeLossJoinContract::groupDiagnostic();
    const std::shared_ptr<const P> original = pipeline;
    if constexpr (targetMemory)
        return object.diagnoseOriginalFiniteTargetMemorySource(
            request, original, set, writes, count, quota);
    else
        return object.diagnoseOriginalFiniteSource(request, original, set, writes, count, quota);
}

template <class Pipeline, class Request>
inline const char* GpuDevice::beginOriginalFiniteDescriptorWrite(DescriptorSet& object,
    const std::shared_ptr<Pipeline>& pipeline,
    std::uint32_t set,
    const OriginalFiniteDescriptorBufferWrite* writes,
    std::size_t count,
    const Request& request,
    const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>& quotas) const
{
    constexpr bool targetMemory = std::is_same_v<Request, OriginalFiniteTargetMemoryWriteRequest>;
    using Contract = std::conditional_t<targetMemory,
        OriginalFiniteTargetMemoryWriteContract,
        OriginalFiniteDescriptorWriteContract>;
    static_assert(
        std::is_same_v<Pipeline, ComputePipeline> || std::is_same_v<Pipeline, RenderPipeline>);
    if (const auto* id = Contract::validate(request))
        return id;
    if (semantics::DescriptorLowering::requiresLegacyWriteDecision(backend()))
        return Contract::decide(backend()).diagnostic;
    if (!count || count > request.maxBindings || !writes)
        return "RHI-ORIGINAL-FINITE-WRITE-BINDING-COUNT";
    if (const auto* id = validateOriginalResourceRegistration(object))
        return id;
    if (originalRegistry_->budget_.contractVersion
            == OriginalTypedNativeLossJoinContract::registrationVersion
        && !OriginalTypedNativeLossJoinContract::lowering(backend()).scopedGroupJoinAvailable)
        return OriginalTypedNativeLossJoinContract::groupDiagnostic();
    if (const auto* id = validateOriginalTypedNativeLossGroupInputs(object, pipeline, quotas))
        return id;
    const auto& arena = object.originalArenaOwner_;
    if (!arena)
        return "RHI-ORIGINAL-FINITE-WRITE-ARENA";
    if (const auto* id = validateOriginalResourceRegistration(*arena))
        return id;
    if (!object.originalResourceRegistration().matchesParent(arena->originalResourceRegistration()))
        return "RHI-ORIGINAL-FINITE-WRITE-ARENA-PARENT";
    if constexpr (targetMemory)
        return object.beginOriginalFiniteTargetMemoryWrite(
            *this, arena, pipeline, set, writes, count, request, quotas);
    else
        return object.beginOriginalFiniteWrite(
            *this, arena, pipeline, set, writes, count, request, quotas);
}

template <class Pipeline>
inline const char* GpuDevice::diagnoseOriginalFiniteTargetMemoryDescriptorSource(
    const OriginalFiniteTargetMemorySourceDiagnosticRequest& request,
    const DescriptorSet& object,
    const std::shared_ptr<Pipeline>& pipeline,
    std::uint32_t set,
    const OriginalFiniteDescriptorBufferWrite* writes,
    std::size_t count,
    const std::shared_ptr<core::CpuMemoryQuota>& quota) const noexcept
{
    return diagnoseOriginalFiniteDescriptorSource(
        request, object, pipeline, set, writes, count, quota);
}

template <class Pipeline>
inline const char* GpuDevice::beginOriginalFiniteTargetMemoryDescriptorWrite(DescriptorSet& object,
    const std::shared_ptr<Pipeline>& pipeline,
    std::uint32_t set,
    const OriginalFiniteDescriptorBufferWrite* writes,
    std::size_t count,
    const OriginalFiniteTargetMemoryWriteRequest& request,
    const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>& quotas) const
{
    return beginOriginalFiniteDescriptorWrite(
        object, pipeline, set, writes, count, request, quotas);
}

template <class Pipeline>
inline const char* GpuDevice::beginOriginalWholeDescriptorWrite(DescriptorSet& object,
    const std::shared_ptr<Pipeline>& pipeline,
    std::uint32_t set,
    const OriginalWholeDescriptorBufferWrite* writes,
    std::size_t count,
    const OriginalWholeDescriptorWriteRequest& request,
    const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>& quotas) const
{
    static_assert(
        std::is_same_v<Pipeline, ComputePipeline> || std::is_same_v<Pipeline, RenderPipeline>);
    if (const auto* id = OriginalWholeDescriptorWriteContract::validate(request))
        return id;
    if (semantics::DescriptorLowering::requiresLegacyWriteDecision(backend()))
        return OriginalWholeDescriptorWriteContract::decide(backend()).diagnostic;
    if (!count || count > request.maxBindings || !writes)
        return "RHI-ORIGINAL-WHOLE-WRITE-BINDING-COUNT";
    if (const auto* id = validateOriginalResourceRegistration(object))
        return id;
    if (originalRegistry_->budget_.contractVersion
            == OriginalTypedNativeLossJoinContract::registrationVersion
        && !OriginalTypedNativeLossJoinContract::lowering(backend()).scopedGroupJoinAvailable)
        return OriginalTypedNativeLossJoinContract::groupDiagnostic();
    if (const auto* id = validateOriginalTypedNativeLossGroupInputs(object, pipeline, quotas))
        return id;
    const auto& arena = object.originalArenaOwner_;
    if (!arena)
        return "RHI-ORIGINAL-WHOLE-WRITE-ARENA";
    if (const auto* id = validateOriginalResourceRegistration(*arena))
        return id;
    if (!object.originalResourceRegistration().matchesParent(arena->originalResourceRegistration()))
        return "RHI-ORIGINAL-WHOLE-WRITE-ARENA-PARENT";
    return object.beginOriginalWholeWrite(
        *this, arena, pipeline, set, writes, count, request, quotas);
}

template <class Pipeline>
inline const char* GpuDevice::beginOriginalGeneralDescriptorWrite(DescriptorSet& object,
    const std::shared_ptr<Pipeline>& pipeline,
    const std::shared_ptr<const OriginalCommandResourceSources>& sources,
    std::uint32_t set,
    const OriginalCommandDescriptorSetElement* elements,
    std::size_t count,
    const OriginalGeneralDescriptorWriteRequest& request,
    const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>& quotas) const
{
    static_assert(
        std::is_same_v<Pipeline, ComputePipeline> || std::is_same_v<Pipeline, RenderPipeline>);
    if (const auto* id = OriginalGeneralDescriptorWriteContract::validate(request))
        return id;
    if (!request.sampledColor.empty()) {
        const auto decision = semantics::OriginalOrdinarySampledColorContract::lowering(
            backend(), request.sampledColor);
        if (!decision.nativeAvailable)
            return decision.diagnostic;
    }
    // An explicitly requested /4 join authenticates the original closure even
    // while a backend's complete-group publication payload remains unavailable.
    // Defaults keep their existing lowering and refusal order.
    const bool synchronousLossJoin = request.lossGroupJoinVersion || request.lossGroupJoinOptIn;
    if (semantics::DescriptorLowering::requiresSynchronousLossJoin(
            backend(), request.contractVersion, synchronousLossJoin))
        return "RHI-ORIGINAL-TYPED-LOSS-SYNC-GROUP-OPT-IN";
    if (synchronousLossJoin) {
        if ((request.contractVersion == 3u && !std::is_same_v<Pipeline, RenderPipeline>)
            || (request.contractVersion == 4u && !std::is_same_v<Pipeline, ComputePipeline>))
            return "RHI-ORIGINAL-TYPED-LOSS-SYNC-GROUP-PIPELINE-PROFILE";
        for (std::size_t i = 0u; i < quotas.size(); ++i)
            if (!quotas[i] || quotas[i]->limitBytes() != request.maxCpuBytes[i])
                return "RHI-ORIGINAL-GENERAL-WRITE-LOSS-QUOTA-LIMIT";
        const OriginalTypedNativeLossJoinRequest join{
            request.lossGroupJoinVersion, request.lossGroupJoinOptIn};
        if (const auto* id =
                validateOriginalTypedNativeLossGroupInputs(object, pipeline, quotas, &join))
            return id;
    }
    if (request.contractVersion == 3u) {
        if constexpr (!std::is_same_v<Pipeline, RenderPipeline>)
            return "RHI-SYNCHRONOUS-GRAPHICS-WRITE-PIPELINE-PROFILE";
        const auto decision =
            OriginalGeneralDescriptorWriteContract::synchronousGraphicsLowering(backend());
        if (!decision.preparationAvailable)
            return decision.diagnostic;
    } else if (request.contractVersion == 4u) {
        if constexpr (!std::is_same_v<Pipeline, ComputePipeline>)
            return "RHI-SYNCHRONOUS-COMPUTE-WRITE-PIPELINE-PROFILE";
        const auto decision =
            OriginalGeneralDescriptorWriteContract::synchronousComputeLowering(backend());
        if (!decision.preparationAvailable)
            return decision.diagnostic;
    } else if (semantics::DescriptorLowering::requiresLegacyWriteDecision(backend()))
        return OriginalGeneralDescriptorWriteContract::decide(backend()).diagnostic;
    if (!count || count > request.maxBindings || !elements || !sources)
        return "RHI-ORIGINAL-GENERAL-WRITE-BINDING-COUNT";
    if (const auto* id = validateOriginalResourceRegistration(object))
        return id;
    if (!synchronousLossJoin
        && originalRegistry_->budget_.contractVersion
            == OriginalTypedNativeLossJoinContract::registrationVersion
        && !OriginalTypedNativeLossJoinContract::lowering(backend()).scopedGroupJoinAvailable)
        return OriginalTypedNativeLossJoinContract::groupDiagnostic();
    if (!synchronousLossJoin)
        if (const auto* id = validateOriginalTypedNativeLossGroupInputs(object, pipeline, quotas))
            return id;
    const auto& arena = object.originalArenaOwner_;
    if (!arena)
        return "RHI-ORIGINAL-GENERAL-WRITE-ARENA";
    if (const auto* id = validateOriginalResourceRegistration(*arena))
        return id;
    if (!object.originalResourceRegistration().matchesParent(arena->originalResourceRegistration()))
        return "RHI-ORIGINAL-GENERAL-WRITE-ARENA-PARENT";
    return object.beginOriginalGeneralWrite(
        *this, arena, pipeline, sources, set, elements, count, request, quotas);
}

inline const char* GpuDevice::originalResourceRegistrationDiagnostic() const noexcept
{
    return originalRegistry_ ? originalRegistry_->validate()
                             : "RHI-ORIGINAL-REGISTRATION-UNPREPARED";
}

inline semantics::PhysicalBudgetLedgerReport GpuDevice::physicalBudgetLedger() const
{
    semantics::PhysicalBudgetLedgerReport report;
    report.backend = backend();
    return report;
}

inline semantics::DirectObjectTelemetryReport GpuDevice::directObjectTelemetry() const
{
    semantics::DirectObjectTelemetryReport report;
    report.backend = backend();
    return report;
}

inline GpuTimingSample GpuDevice::latestGpuTiming() const
{
    return {};
}

inline semantics::UploadStagingReadiness GpuDevice::uploadStagingReadiness() const
{
    return semantics::uploadStagingReadinessUnavailable();
}

inline FrameCaptureCapability GpuDevice::frameCaptureCapability(FrameCaptureTool tool) const
{
    return unavailableFrameCaptureCapability(backend(), tool, "ARP-RHI-FRAME-CAPTURE-UNSUPPORTED");
}

inline FrameCaptureResult GpuDevice::beginFrameCapture(const FrameCaptureRequest&)
{
    return unavailableFrameCaptureResult("ARP-RHI-FRAME-CAPTURE-UNSUPPORTED");
}

inline FrameCaptureResult GpuDevice::endFrameCapture()
{
    return unavailableFrameCaptureResult("ARP-RHI-FRAME-CAPTURE-UNSUPPORTED");
}

inline GpuPipelineStatisticsSample GpuDevice::latestPipelineStatistics() const
{
    return {};
}

inline GpuPipelineStatisticsSample GpuDevice::readPipelineStatistics(
    const QueryPool& queryPool, std::uint32_t queryIndex) const
{
    GpuPipelineStatisticsSample sample;
    sample.queryIndex = queryIndex;
    if (queryPool.type() != QueryType::PipelineStatistics || queryIndex >= queryPool.count()) {
        sample.availability = PipelineStatisticsAvailability::DiagnosticFailure;
        sample.diagnostic = "RHI pipeline-statistics query identity is invalid";
        return sample;
    }
    sample.availability = PipelineStatisticsAvailability::Unsupported;
    sample.diagnostic = "backend does not expose pipeline-statistics query readback";
    return sample;
}

inline void GpuDevice::waitIdle()
{
    throw core::DiagnosticError("RHI-DEVICE-IDLE-IMPLEMENTATION-UNAVAILABLE");
}

inline semantics::ShutdownDrainReport GpuDevice::drainForShutdown(
    const semantics::ShutdownDrainRequest& request)
{
    const auto now = std::chrono::steady_clock::now();
    return semantics::rejectedShutdownDrainReport(backend(), now, request, now);
}

inline GpuQueue* GpuDevice::computeQueue()
{
    return nullptr;
}

inline TexturePlacementRequirements GpuDevice::queryTexturePlacementRequirements(
    const TextureDesc& desc)
{
    return estimateTexturePlacementRequirements(desc);
}

inline std::uint64_t GpuDevice::queryOriginalContentTextureAllocation(
    const TextureDesc& desc, const char*& diagnostic, std::uint32_t creationVersion)
{
    if (!OriginalTextureContentCreationContract::hasNativeAllocationBudget(creationVersion)) {
        diagnostic = "RHI-CANONICAL-CONTENT-CREATION-NATIVE-QUERY-PROFILE";
        return 0u;
    }
    std::uint64_t texels = 0u;
    diagnostic = OriginalTextureContentCreationContract::isCellCreation(creationVersion)
        ? OriginalTextureContentCreationContract::inspectCellLayout(
              desc, UINT64_MAX, texels, creationVersion)
        : semantics::TextureUploadDestinationValidation::validate(
              desc, texels, ResourceState::Undefined);
    if (diagnostic)
        return 0u;
    const auto decision =
        OriginalTextureContentCreationContract::lowering(backend(), creationVersion);
    if (!decision.exclusiveCreationAvailable) {
        diagnostic = decision.diagnostic;
        return 0u;
    }
    return queryOriginalContentTextureAllocationPayload(desc, diagnostic);
}

inline std::uint64_t GpuDevice::queryOriginalContentTextureAllocationPayload(
    const TextureDesc&, const char*& diagnostic)
{
    diagnostic = "RHI-CANONICAL-CONTENT-CREATION-NATIVE-PAYLOAD-MISSING";
    return 0u;
}

inline core::UniquePtr<GpuTexture> GpuDevice::createOriginalContentTexture(const TextureDesc& desc,
    const OriginalTextureContentCreationBudget& budget,
    std::shared_ptr<core::CpuMemoryQuota> contentQuota,
    const char*& diagnostic)
{
    std::uint64_t texels = 0u;
    diagnostic = OriginalTextureContentCreationContract::inspect(desc, budget, texels);
    if (diagnostic)
        return {};
    const auto decision =
        OriginalTextureContentCreationContract::lowering(backend(), budget.contractVersion);
    if (!decision.exclusiveCreationAvailable) {
        diagnostic = decision.diagnostic;
        return {};
    }
    if (budget.nativeAllocationObservation) {
        const auto allocation = OriginalNativeAllocationObservationContract::lowering(
            backend(), budget.nativeAllocationObservation->contractVersion);
        if (!allocation.reserveThenObserve) {
            diagnostic = allocation.diagnostic;
            return {};
        }
    }
    if (!contentQuota || contentQuota->limitBytes() != budget.maxCpuContentBytes) {
        diagnostic = "RHI-CANONICAL-CONTENT-CREATION-QUOTA";
        return {};
    }
    if (!originalLifetime_ || !originalRegistry_) {
        diagnostic = "RHI-CANONICAL-CONTENT-CREATION-DEVICE-UNPREPARED";
        return {};
    }
    auto loss = observeOriginalDeviceNativeLoss(diagnostic);
    if (!loss)
        return {};
    const std::array domains{contentQuota,
        originalLifetime_->quota_,
        originalRegistry_->quota_,
        core::currentCpuMemoryQuota(),
        originalLossBinding_->originalMetadataQuota()};
    if (!core::SharedOwnerProjection::hasIndependentOwners(
            domains.size(), [&](std::size_t i) noexcept {
                return domains[i];
            })) {
        diagnostic = "RHI-CANONICAL-CONTENT-CREATION-QUOTA-DOMAIN";
        return {};
    }
    try {
        core::UniquePtr<common::OriginalTextureContentControl> content;
        {
            core::CpuMemoryQuotaScope scope(contentQuota);
            if (OriginalTextureContentCreationContract::isCellCreation(budget.contractVersion)) {
                const auto geometry = semantics::OriginalTextureStateRange::capture(
                    desc, budget.maxContentCells, diagnostic);
                if (!geometry)
                    return {};
                if (geometry->cellCount() > SIZE_MAX) {
                    diagnostic = "RHI-CANONICAL-CONTENT-CREATION-CELL-SIZE";
                    return {};
                }
                content = core::makeUnique<common::OriginalTextureContentControl>(
                    common::OriginalTextureContentControl::ConstructionKey{},
                    budget,
                    *loss,
                    contentQuota,
                    *geometry);
            } else
                content = core::makeUnique<common::OriginalTextureContentControl>(
                    common::OriginalTextureContentControl::ConstructionKey{},
                    budget,
                    *loss,
                    contentQuota);
        }
        auto result = createOriginalContentTexturePayload(desc, decision, budget, diagnostic);
        if (!result)
            return {};
        if ((diagnostic = validateOriginalResourceRegistration(*result)))
            return {};
        const auto allocationBytes = result->allocation().estimatedSizeBytes;
        if (result->originalContent_
            || (OriginalTextureContentCreationContract::hasNativeAllocationBudget(
                    budget.contractVersion)
                    ? (!allocationBytes || allocationBytes > budget.maxNativeAllocationBytes
                          || result->desc().initialState != ResourceState::Undefined
                          || result->state() != ResourceState::Undefined)
                    : allocationBytes != texels)) {
            diagnostic = "RHI-CANONICAL-CONTENT-CREATION-BINDING";
            return {};
        }
        result->originalContent_ = std::move(content);
        diagnostic = nullptr;
        return result;
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-CANONICAL-CONTENT-CREATION-ALLOCATION";
        return {};
    } catch (const std::length_error&) {
        diagnostic = "RHI-CANONICAL-CONTENT-CREATION-ALLOCATION";
        return {};
    }
}

inline bool GpuDevice::overlapsOriginalDeviceCpuOwner(
    const std::shared_ptr<core::CpuMemoryQuota>& quota) const noexcept
{
    if (!quota)
        return false;
    const auto same = [&](const std::shared_ptr<core::CpuMemoryQuota>& other) noexcept {
        return other
            && (quota.get() == other.get()
                || core::SharedOwnerProjection::sameControlBlock(quota, other));
    };
    return (originalLifetime_ && same(originalLifetime_->quota_))
        || (originalRegistry_ && same(originalRegistry_->quota_))
        || (originalLossBinding_ && same(originalLossBinding_->originalMetadataQuota()));
}

inline ResourceBarrierResult GpuDevice::prepareTextureForPoolReuse(GpuTexture& texture)
{
    ResourceBarrierResult result;
    result.resource = texture.id();
    result.before = texture.state();
    result.after = ResourceState::Undefined;
    result.diagnostics = "RHI-POOL-REUSE-TEXTURE-UNSUPPORTED";
    return result;
}

inline ResourceBarrierResult GpuDevice::prepareBufferForPoolReuse(GpuBuffer& buffer)
{
    ResourceBarrierResult result;
    result.resource = buffer.id();
    result.before = buffer.state();
    result.after = ResourceState::Undefined;
    result.diagnostics = "RHI-POOL-REUSE-BUFFER-UNSUPPORTED";
    return result;
}

inline std::string_view GpuDevice::lastMemoryHeapCreationDiagnostic() const
{
    return "RHI-MEMORY-HEAP-CREATE-FAILED";
}

inline core::UniquePtr<PipelineLayout> GpuDevice::createOriginalPipelineLayout(
    const PipelineLayoutDesc& desc)
{
    try {
        auto record =
            reserveOriginalPipelineRegistration(OriginalRegisteredResourceKind::PipelineLayout, {});
        if (!record)
            return {};
        auto candidate = createPipelineLayout(desc);
        if (!candidate) {
            if (!lastOriginalRegistrationDiagnostic_)
                lastOriginalRegistrationDiagnostic_ = "RHI-ORIGINAL-PIPELINE-FACTORY-FAILED";
            return {};
        }
        if (candidate->originalRegistration_.record_) {
            lastOriginalRegistrationDiagnostic_ = "RHI-ORIGINAL-REGISTRATION-CANDIDATE-IDENTITY";
            return {};
        }
        record->object_ = candidate.get();
        if ((lastOriginalRegistrationDiagnostic_ = record->validate()))
            return {};
        candidate->originalRegistration_.record_ = std::move(record);
        return candidate;
    } catch (const std::bad_alloc&) {
        lastOriginalRegistrationDiagnostic_ = "RHI-ORIGINAL-REGISTRATION-ALLOCATION";
        return {};
    }
}

inline core::UniquePtr<RenderPipeline> GpuDevice::createOriginalRenderPipeline(
    const RenderPipelineDesc& desc, const std::shared_ptr<PipelineLayout>& layout)
{
    return createOriginalPipeline<RenderPipeline>(desc,
        layout,
        OriginalRegisteredResourceKind::GraphicsPipeline,
        [&](const RenderPipelineDesc& frozen) {
            return createRenderPipeline(frozen);
        });
}

inline core::UniquePtr<ComputePipeline> GpuDevice::createOriginalComputePipeline(
    const ComputePipelineDesc& desc, const std::shared_ptr<PipelineLayout>& layout)
{
    return createOriginalPipeline<ComputePipeline>(desc,
        layout,
        OriginalRegisteredResourceKind::ComputePipeline,
        [&](const ComputePipelineDesc& frozen) {
            return createComputePipeline(frozen);
        });
}

inline core::UniquePtr<ComputePipeline> GpuDevice::createOriginalArtifactComputePipeline(
    const std::shared_ptr<const OriginalComputePipelineCreation>& creation,
    OriginalAcceptedComputePipelineFactoryContract::Budget budget,
    const std::shared_ptr<core::CpuMemoryQuota>& wrapperQuota)
{
    return createOriginalAcceptedArtifactPipeline<ComputePipeline,
        OriginalAcceptedComputePipelineFactoryContract>(creation,
        budget,
        wrapperQuota,
        5u,
        "RHI-ACCEPTED-COMPUTE-FACTORY-REGISTRY-VERSION",
        "RHI-ACCEPTED-COMPUTE-FACTORY-QUOTA",
        "RHI-ACCEPTED-COMPUTE-FACTORY-QUOTA-DOMAIN",
        OriginalRegisteredResourceKind::ComputePipeline,
        [&](const ComputePipelineDesc& frozen) {
            return createAcceptedOriginalComputePipeline(
                frozen, creation, wrapperQuota, lastOriginalRegistrationDiagnostic_);
        });
}

inline core::UniquePtr<RenderPipeline> GpuDevice::createOriginalArtifactGraphicsPipeline(
    const std::shared_ptr<const OriginalGraphicsPipelineCreation>& creation,
    OriginalAcceptedGraphicsPipelineFactoryContract::Budget budget,
    const std::shared_ptr<core::CpuMemoryQuota>& wrapperQuota)
{
    return createOriginalAcceptedArtifactPipeline<RenderPipeline,
        OriginalAcceptedGraphicsPipelineFactoryContract>(creation,
        budget,
        wrapperQuota,
        6u,
        "RHI-ACCEPTED-GRAPHICS-FACTORY-REGISTRY-VERSION",
        "RHI-ACCEPTED-GRAPHICS-FACTORY-QUOTA",
        "RHI-ACCEPTED-GRAPHICS-FACTORY-QUOTA-DOMAIN",
        OriginalRegisteredResourceKind::GraphicsPipeline,
        [&](const RenderPipelineDesc& frozen) {
            return createAcceptedOriginalGraphicsPipeline(
                frozen, creation, wrapperQuota, lastOriginalRegistrationDiagnostic_);
        });
}

inline core::UniquePtr<ComputePipeline> GpuDevice::createOriginalTableExpansionComputePipeline(
    const std::shared_ptr<const OriginalComputePipelineCreation>& creation,
    OriginalAcceptedTableExpansionPipelineContract::Budget budget,
    const std::shared_ptr<core::CpuMemoryQuota>& quota)
{
    return createOriginalAcceptedArtifactPipeline<ComputePipeline,
        OriginalAcceptedTableExpansionPipelineContract,
        true>(creation,
        budget,
        quota,
        8u,
        "RHI-ORIGINAL-TABLE-FACTORY-REGISTRY-VERSION",
        "RHI-ORIGINAL-TABLE-FACTORY-QUOTA",
        "RHI-ORIGINAL-TABLE-FACTORY-QUOTA-DOMAIN",
        OriginalRegisteredResourceKind::ComputePipeline,
        [&](const ComputePipelineDesc& frozen) {
            return createAcceptedOriginalComputePipeline(
                frozen, creation, quota, lastOriginalRegistrationDiagnostic_);
        });
}

inline core::UniquePtr<RenderPipeline> GpuDevice::createOriginalTableExpansionGraphicsPipeline(
    const std::shared_ptr<const OriginalGraphicsPipelineCreation>& creation,
    OriginalAcceptedTableExpansionPipelineContract::Budget budget,
    const std::shared_ptr<core::CpuMemoryQuota>& quota)
{
    return createOriginalAcceptedArtifactPipeline<RenderPipeline,
        OriginalAcceptedTableExpansionPipelineContract,
        true>(creation,
        budget,
        quota,
        8u,
        "RHI-ORIGINAL-TABLE-FACTORY-REGISTRY-VERSION",
        "RHI-ORIGINAL-TABLE-FACTORY-QUOTA",
        "RHI-ORIGINAL-TABLE-FACTORY-QUOTA-DOMAIN",
        OriginalRegisteredResourceKind::GraphicsPipeline,
        [&](const RenderPipelineDesc& frozen) {
            return createAcceptedOriginalGraphicsPipeline(
                frozen, creation, quota, lastOriginalRegistrationDiagnostic_);
        });
}

inline core::UniquePtr<DescriptorArena> GpuDevice::createOriginalDescriptorArena(
    const DescriptorArenaDesc& desc)
{
    try {
        auto record = reserveOriginalDescriptorRegistration(
            OriginalRegisteredResourceKind::DescriptorArena, {});
        if (!record)
            return {};
        auto candidate = createDescriptorArena(desc);
        if (candidate && !candidate->allocationEpochTicket().isCurrent()) {
            lastOriginalRegistrationDiagnostic_ =
                "RHI-ORIGINAL-DESCRIPTOR-ALLOCATION-EPOCH-MISSING";
            return {};
        }
        return publishOriginalDescriptorCandidate(std::move(candidate), std::move(record));
    } catch (const std::bad_alloc&) {
        lastOriginalRegistrationDiagnostic_ = "RHI-ORIGINAL-REGISTRATION-ALLOCATION";
        return {};
    }
}

inline core::UniquePtr<DescriptorSet> GpuDevice::allocateOriginalDescriptorSet(
    const std::shared_ptr<DescriptorArena>& arena, const DescriptorSetDesc& desc)
{
    return allocateOriginalDescriptor<DescriptorSet>(
        arena,
        OriginalRegisteredResourceKind::Descriptor,
        [&] {
            return arena->allocateDescriptorSet(desc);
        },
        [&](const DescriptorSet& candidate) {
            return validateExactDescriptorSetLayout(desc.layout, candidate).success;
        });
}

inline core::UniquePtr<DescriptorTable> GpuDevice::allocateOriginalDescriptorTable(
    const std::shared_ptr<DescriptorArena>& arena, const DescriptorTableDesc& desc)
{
    if (originalRegistry_ && originalRegistry_->budget_.contractVersion == 8u
        && (lastOriginalRegistrationDiagnostic_ =
                PipelineShaderDeclarationContract::validateRegisteredDepthTable(desc)))
        return {};
    return allocateOriginalDescriptor<DescriptorTable>(
        arena,
        OriginalRegisteredResourceKind::DescriptorTable,
        [&] {
            return arena->allocateDescriptorTable(desc);
        },
        [&](const DescriptorTable& candidate) {
            return descriptorTableDefinitionMatches(desc, candidate.desc());
        });
}

inline void* GpuDevice::queryInterface(std::string_view extensionId) noexcept
{
    return nullptr;
}

inline core::UniquePtr<RenderPipeline> GpuDevice::createAcceptedOriginalGraphicsPipeline(
    const RenderPipelineDesc&,
    const std::shared_ptr<const OriginalGraphicsPipelineCreation>&,
    const std::shared_ptr<core::CpuMemoryQuota>&,
    const char*& diagnostic)
{
    diagnostic = "RHI-ACCEPTED-GRAPHICS-FACTORY-APPROVED-PAYLOAD-MISSING";
    return {};
}

inline core::UniquePtr<ComputePipeline> GpuDevice::createAcceptedOriginalComputePipeline(
    const ComputePipelineDesc&,
    const std::shared_ptr<const OriginalComputePipelineCreation>&,
    const std::shared_ptr<core::CpuMemoryQuota>&,
    const char*& diagnostic)
{
    diagnostic = "RHI-ACCEPTED-COMPUTE-FACTORY-APPROVED-PAYLOAD-MISSING";
    return {};
}

inline core::UniquePtr<GpuTexture> GpuDevice::createOriginalContentTexturePayload(
    const TextureDesc&,
    const OriginalTextureContentCreationContract::Decision&,
    const OriginalTextureContentCreationBudget&,
    const char*& diagnostic)
{
    diagnostic = "RHI-CANONICAL-CONTENT-CREATION-NATIVE-PAYLOAD-MISSING";
    return {};
}

inline void GpuDevice::closeOriginalDeviceLifetimeForShutdown() noexcept
{
    if (originalLifetime_)
        originalLifetime_->invalidate(OriginalDeviceLifetime::Terminal::Shutdown);
    if (originalLossBinding_)
        originalLossBinding_->shutdown();
}

inline std::shared_ptr<OriginalComputeDispatchValidation>
GpuDevice::prepareOriginalComputeDispatchValidationPayload(const ComputeDispatchValidationRequest&,
    const ComputeDispatchValidationSessionBudget&,
    ComputeDispatchValidationSessionQuotas,
    const char*& diagnostic)
{
    diagnostic = "RHI-DISPATCH-PREPARATION-NATIVE-UNAVAILABLE";
    return {};
}

inline std::shared_ptr<OriginalDeviceNativeLossBinding> GpuDevice::bindOriginalDeviceNativeLoss(
    const OriginalDeviceLifetime::NativeLossReceiver&,
    const OriginalDeviceNativeLossBudget&,
    std::shared_ptr<core::CpuMemoryQuota>,
    const char*& diagnostic)
{
    diagnostic = "RHI-ORIGINAL-DEVICE-LOSS-NATIVE-SOURCE-UNAVAILABLE";
    return {};
}

inline std::shared_ptr<OriginalDeviceNativeLossBinding>
GpuDevice::bindOriginalDeviceNativeResultLoss(const OriginalDeviceLifetime::NativeLossReceiver&,
    const OriginalDeviceNativeResultLossBudget&,
    std::shared_ptr<core::CpuMemoryQuota>,
    const char*& diagnostic)
{
    diagnostic = "RHI-ORIGINAL-RESULT-LOSS-NATIVE-SOURCE-UNAVAILABLE";
    return {};
}

inline std::shared_ptr<OriginalDeviceNativeLossBinding> GpuDevice::bindOriginalObservedNativeLoss(
    const OriginalDeviceLifetime::NativeLossReceiver&,
    const OriginalDeviceNativeLossBudget&,
    const OriginalDeviceNativeLossObserverRequest&,
    std::shared_ptr<core::CpuMemoryQuota>,
    const char*& diagnostic)
{
    diagnostic = "RHI-ORIGINAL-LOSS-OBSERVER-WEBGPU-BINDING-MISSING";
    return {};
}

inline const char* GpuDevice::validateOriginalNativeLossResource(const GpuBuffer&) const noexcept
{
    return nullptr;
}

inline const char* GpuDevice::validateOriginalNativeLossResource(const GpuTexture&) const noexcept
{
    return nullptr;
}

inline const char* GpuDevice::validateOriginalNativeLossResource(
    const GpuTextureView&) const noexcept
{
    return nullptr;
}

inline const char* GpuDevice::validateOriginalNativeLossResource(const GpuSampler&) const noexcept
{
    return "RHI-ORIGINAL-SAMPLER-NATIVE-LOSS-JOIN-MISSING";
}

inline const char* GpuDevice::validateOriginalNativeLossPipeline(
    const ComputePipeline&) const noexcept
{
    return "RHI-ORIGINAL-TYPED-LOSS-PIPELINE-NATIVE-SOURCE-UNAVAILABLE";
}

inline const char* GpuDevice::validateOriginalNativeLossPipeline(
    const RenderPipeline&) const noexcept
{
    return "RHI-ORIGINAL-TYPED-LOSS-PIPELINE-NATIVE-SOURCE-UNAVAILABLE";
}

template <class T>
inline core::UniquePtr<T> GpuDevice::completeOriginalResourceCreation(
    core::UniquePtr<T> resource, const GpuTexture* parent)
{
    using Kind = OriginalRegisteredResourceKind;
    using Traits = OriginalRegisteredResourceTraits<T>;
    using Base = typename Traits::Base;
    constexpr auto kind = Traits::kind;
    lastOriginalRegistrationDiagnostic_ = nullptr;
    if (!resource) {
        lastOriginalRegistrationDiagnostic_ = "RHI-ORIGINAL-REGISTRATION-CANDIDATE-MISSING";
        return {};
    }
    if (!originalRegistry_) {
        lastOriginalRegistrationDiagnostic_ = "RHI-ORIGINAL-REGISTRATION-UNPREPARED";
        return resource;
    }
    if constexpr (std::is_same_v<Base, GpuSampler>) {
        // Legacy registry profiles do not silently register samplers.
        if (!originalRegistry_->budget_.samplers.requested()) {
            lastOriginalRegistrationDiagnostic_ = "RHI-ORIGINAL-SAMPLER-REGISTRATION-OPT-IN";
            return resource;
        }
        const auto plan = OriginalResourceRegistrationContract::samplerLowering(
            backend(), originalRegistry_->budget_.samplers);
        if (!plan.typedLifetimeIssuerAvailable) {
            lastOriginalRegistrationDiagnostic_ = plan.diagnostic;
            return {};
        }
        if (originalLossBinding_) {
            using Loss = OriginalSamplerNativeLossJoinContract;
            const auto version = OriginalResourceRegistrationContract::joinsSamplerNativeLoss(
                                     originalRegistry_->budget_)
                ? 3u
                : originalRegistry_->budget_.contractVersion
                    == OriginalDescriptorNativeLossRegistrationContract::registrationVersion
                ? 2u
                : 1u;
            if ((lastOriginalRegistrationDiagnostic_ =
                        Loss::validateRegistration(originalRegistry_->budget_, version)))
                return {};
            const auto decision = Loss::lowering(backend(), version);
            if (!decision.nativeSourceAvailable) {
                lastOriginalRegistrationDiagnostic_ = decision.diagnostic;
                return {};
            }
            auto observed = observeOriginalDeviceNativeLoss(lastOriginalRegistrationDiagnostic_);
            if (!observed)
                return {};
            if (version == 1u || version == 3u) {
                if ((lastOriginalRegistrationDiagnostic_ = validateOriginalTypedNativeLossJoin(
                         *observed, {version == 3u ? 3u : 1u, true})))
                    return {};
            } else {
                // The independent descriptor/sampler request was consumed by
                // registration. Preserve that registry's exact weak subscription
                // and device domain; no accepted-pipeline join is inferred.
                if (!core::SharedOwnerProjection::sameOwner(
                        originalRegistry_->loss_.lock(), observed->binding_)
                    || !core::SharedOwnerProjection::sameOwner(
                        originalRegistry_->domain_, observed->domain_)) {
                    lastOriginalRegistrationDiagnostic_ =
                        "RHI-ORIGINAL-SAMPLER-LOSS-REGISTRATION-SUBSCRIPTION";
                    return {};
                }
            }
        }
    }
    if ((lastOriginalRegistrationDiagnostic_ = originalRegistry_->validate()))
        return {};
    auto& object = static_cast<Base&>(*resource);
    if (originalLossBinding_
        && (lastOriginalRegistrationDiagnostic_ = validateOriginalNativeLossResource(object)))
        return {};
    if (!object.id().isValid() || object.originalRegistration_.record_) {
        lastOriginalRegistrationDiagnostic_ = "RHI-ORIGINAL-REGISTRATION-CANDIDATE-IDENTITY";
        return {};
    }
    std::shared_ptr<OriginalResourceRegistration> parentRecord;
    if constexpr (std::is_same_v<Base, GpuTextureView>) {
        if (!parent) {
            lastOriginalRegistrationDiagnostic_ = "RHI-ORIGINAL-REGISTRATION-PARENT-MISSING";
            return {};
        }
        parentRecord = parent->originalRegistration_.record_;
        if (!parentRecord || parentRecord->registry_.get() != originalRegistry_.get()
            || parentRecord->kind_ != Kind::Texture || parentRecord->object_ != parent
            || parentRecord->id_ != parent->id().value || object.textureId() != parent->id()) {
            lastOriginalRegistrationDiagnostic_ = "RHI-ORIGINAL-REGISTRATION-PARENT-FOREIGN";
            return {};
        }
        if ((lastOriginalRegistrationDiagnostic_ = parentRecord->validate()))
            return {};
    } else if (parent) {
        lastOriginalRegistrationDiagnostic_ = "RHI-ORIGINAL-REGISTRATION-PARENT-UNEXPECTED";
        return {};
    }
    try {
        auto record =
            reserveOriginalRegistrationRecord(kind, parentRecord, &object, object.id().value);
        if (!record)
            return {};
        if ((lastOriginalRegistrationDiagnostic_ = record->validate()))
            return {};
        if ((lastOriginalRegistrationDiagnostic_ =
                    bindOriginalStateDomain(object, originalDeviceLifetimeTicket())))
            return {};
        object.originalRegistration_.record_ = std::move(record);
        return resource;
    } catch (const std::bad_alloc&) {
        lastOriginalRegistrationDiagnostic_ = "RHI-ORIGINAL-REGISTRATION-ALLOCATION";
        return {};
    }
}

inline std::shared_ptr<OriginalResourceRegistration> GpuDevice::reserveOriginalPipelineRegistration(
    OriginalRegisteredResourceKind kind, const std::shared_ptr<PipelineLayout>& layout)
{
    using Contract = OriginalResourceRegistrationContract;
    lastOriginalRegistrationDiagnostic_ = originalResourceRegistrationDiagnostic();
    if (lastOriginalRegistrationDiagnostic_)
        return {};
    if (originalRegistry_->budget_.contractVersion < 2u) {
        lastOriginalRegistrationDiagnostic_ = "RHI-ORIGINAL-REGISTRATION-TYPED-VERSION";
        return {};
    }
    if (originalLossBinding_ && originalRegistry_->budget_.contractVersion <= 6u) {
        lastOriginalRegistrationDiagnostic_ = "RHI-ORIGINAL-PIPELINE-NATIVE-LOSS-JOIN-MISSING";
        return {};
    }
    if (OriginalResourceRegistrationContract::observesNativeLoss(
            originalRegistry_->budget_.contractVersion)
        && Contract::isPipeline(kind)
        && !OriginalTypedNativeLossJoinContract::lowering(backend(),
            originalRegistry_->budget_.contractVersion == 10u      ? 3u
                : originalRegistry_->budget_.contractVersion == 8u ? 2u
                                                                   : 1u)
            .acceptedPipelineJoinAvailable) {
        lastOriginalRegistrationDiagnostic_ =
            OriginalTypedNativeLossJoinContract::pipelineDiagnostic();
        return {};
    }
    std::shared_ptr<OriginalResourceRegistration> parent;
    if (Contract::isPipeline(kind)) {
        if (!layout || !layout->sharedOwnerMatches(layout)) {
            lastOriginalRegistrationDiagnostic_ = "RHI-ORIGINAL-PIPELINE-LAYOUT-OWNER";
            return {};
        }
        if ((lastOriginalRegistrationDiagnostic_ = validateOriginalResourceRegistration(*layout)))
            return {};
        parent = layout->originalRegistration_.record_;
    }
    return reserveOriginalRegistrationRecord(kind, parent, nullptr, 0u);
}

inline std::shared_ptr<OriginalResourceRegistration> GpuDevice::reserveOriginalRegistrationRecord(
    OriginalRegisteredResourceKind kind,
    const std::shared_ptr<OriginalResourceRegistration>& parent,
    const void* object,
    std::uint64_t id,
    core::LifetimeEpoch::Ticket epoch)
{
    using Contract = OriginalResourceRegistrationContract;
    auto* capacity = originalRegistry_->capacity(kind);
    if (!capacity || capacity->used() >= capacity->capacity()) {
        lastOriginalRegistrationDiagnostic_ = "RHI-ORIGINAL-REGISTRATION-KIND-CAPACITY";
        return {};
    }
    if (Contract::ownerCount(kind, originalRegistry_->budget_.contractVersion)
        > originalRegistry_->owners_.capacity() - originalRegistry_->owners_.used()) {
        lastOriginalRegistrationDiagnostic_ = "RHI-ORIGINAL-REGISTRATION-OWNER-CAPACITY";
        return {};
    }
    const auto generation = originalRegistry_->generations_.tryAcquire();
    if (!generation) {
        lastOriginalRegistrationDiagnostic_ = "RHI-ORIGINAL-REGISTRATION-GENERATION-EXHAUSTED";
        return {};
    }
    core::CpuMemoryQuotaScope scope(originalRegistry_->quota_);
    auto record = core::makeShared<OriginalResourceRegistration>(
        OriginalResourceRegistration::ConstructionKey{},
        originalRegistry_,
        object,
        id,
        kind,
        generation,
        parent,
        std::move(epoch));
    if (!record->kindCapacity_.charged()) {
        lastOriginalRegistrationDiagnostic_ = "RHI-ORIGINAL-REGISTRATION-KIND-CAPACITY";
        return {};
    }
    if (!record->ownerCapacity_.charged()) {
        lastOriginalRegistrationDiagnostic_ = "RHI-ORIGINAL-REGISTRATION-OWNER-CAPACITY";
        return {};
    }
    return record;
}

inline std::shared_ptr<OriginalResourceRegistration>
GpuDevice::reserveOriginalDescriptorRegistration(
    OriginalRegisteredResourceKind kind, const std::shared_ptr<DescriptorArena>& arena)
{
    lastOriginalRegistrationDiagnostic_ = originalResourceRegistrationDiagnostic();
    if (lastOriginalRegistrationDiagnostic_)
        return {};
    if (originalRegistry_->budget_.contractVersion < 3u) {
        lastOriginalRegistrationDiagnostic_ = "RHI-ORIGINAL-REGISTRATION-DESCRIPTOR-VERSION";
        return {};
    }
    if (originalLossBinding_ && originalRegistry_->budget_.contractVersion <= 6u) {
        lastOriginalRegistrationDiagnostic_ = "RHI-ORIGINAL-DESCRIPTOR-NATIVE-LOSS-JOIN-MISSING";
        return {};
    }
    if (originalRegistry_->budget_.contractVersion
            == OriginalTypedNativeLossJoinContract::registrationVersion
        && kind == OriginalRegisteredResourceKind::DescriptorTable) {
        lastOriginalRegistrationDiagnostic_ = "RHI-ORIGINAL-TYPED-LOSS-DESCRIPTOR-TABLE-CLOSED";
        return {};
    }
    std::shared_ptr<OriginalResourceRegistration> parent;
    core::LifetimeEpoch::Ticket epoch;
    if (OriginalResourceRegistrationContract::isDescriptor(kind)) {
        if (!arena || !arena->sharedOwnerMatches(arena)) {
            lastOriginalRegistrationDiagnostic_ = "RHI-ORIGINAL-DESCRIPTOR-ARENA-OWNER";
            return {};
        }
        if ((lastOriginalRegistrationDiagnostic_ = validateOriginalResourceRegistration(*arena)))
            return {};
        parent = arena->originalRegistration_.record_;
        epoch = arena->allocationEpochTicket();
        if (!epoch.isCurrent()) {
            lastOriginalRegistrationDiagnostic_ =
                "RHI-ORIGINAL-DESCRIPTOR-ALLOCATION-EPOCH-MISSING";
            return {};
        }
    }
    return reserveOriginalRegistrationRecord(kind, parent, nullptr, 0u, std::move(epoch));
}

template <class T>
inline core::UniquePtr<T> GpuDevice::publishOriginalDescriptorCandidate(
    core::UniquePtr<T> candidate, std::shared_ptr<OriginalResourceRegistration> record)
{
    if (!candidate) {
        lastOriginalRegistrationDiagnostic_ = "RHI-ORIGINAL-DESCRIPTOR-FACTORY-FAILED";
        return {};
    }
    if (candidate->originalRegistration_.record_
        || candidate->originalRegisteredKind() != record->kind_) {
        lastOriginalRegistrationDiagnostic_ = "RHI-ORIGINAL-REGISTRATION-CANDIDATE-IDENTITY";
        return {};
    }
    if (record->registry_->budget_.contractVersion
            == OriginalDescriptorNativeLossRegistrationContract::registrationVersion
        || record->registry_->budget_.contractVersion
            == OriginalTypedNativeLossJoinContract::synchronousRegistrationVersion) {
        if constexpr (std::is_same_v<T, DescriptorArena> || std::is_same_v<T, DescriptorSet>) {
            if ((lastOriginalRegistrationDiagnostic_ =
                        validateOriginalNativeLossDescriptor(*candidate)))
                return {};
        } else {
            lastOriginalRegistrationDiagnostic_ = "RHI-ORIGINAL-DESCRIPTOR-LOSS-DOMAIN";
            return {};
        }
    }
    record->object_ = candidate.get();
    if ((lastOriginalRegistrationDiagnostic_ = record->validate()))
        return {};
    candidate->originalRegistration_.record_ = std::move(record);
    return candidate;
}

template <class T, class Factory, class Validate>
inline core::UniquePtr<T> GpuDevice::allocateOriginalDescriptor(
    const std::shared_ptr<DescriptorArena>& arena,
    OriginalRegisteredResourceKind kind,
    Factory&& factory,
    Validate&& validate)
{
    try {
        auto record = reserveOriginalDescriptorRegistration(kind, arena);
        if (!record)
            return {};
        auto candidate = factory();
        if (candidate
            && (!candidate->allocation().success || candidate->originalArenaOwner_
                || !validate(*candidate)
                || !candidate->allocationEpochTicket().sameCurrentEpoch(
                    record->allocationEpoch_))) {
            lastOriginalRegistrationDiagnostic_ = "RHI-ORIGINAL-DESCRIPTOR-CANDIDATE-CONTRACT";
            return {};
        }
        if (candidate)
            candidate->originalArenaOwner_ = arena;
        return publishOriginalDescriptorCandidate(std::move(candidate), std::move(record));
    } catch (const std::bad_alloc&) {
        lastOriginalRegistrationDiagnostic_ = "RHI-ORIGINAL-REGISTRATION-ALLOCATION";
        return {};
    }
}

template <class Pipeline, class Contract, bool TableExpansion, class Creation, class Factory>
inline core::UniquePtr<Pipeline> GpuDevice::createOriginalAcceptedArtifactPipeline(
    const std::shared_ptr<const Creation>& creation,
    typename Contract::Budget budget,
    const std::shared_ptr<core::CpuMemoryQuota>& wrapperQuota,
    std::uint32_t minimumRegistryVersion,
    const char* registryId,
    const char* quotaId,
    const char* domainId,
    OriginalRegisteredResourceKind kind,
    Factory&& factory)
{
    if ((lastOriginalRegistrationDiagnostic_ = Contract::validateBudget(budget)))
        return {};
    if ((lastOriginalRegistrationDiagnostic_ = originalResourceRegistrationDiagnostic()))
        return {};
    if (originalRegistry_->budget_.contractVersion < minimumRegistryVersion) {
        lastOriginalRegistrationDiagnostic_ = registryId;
        return {};
    }
    if ((lastOriginalRegistrationDiagnostic_ = Creation::validateOriginal(creation)))
        return {};
    const bool expanded = bool(creation->originalInput()->tableExpansion());
    if (expanded != TableExpansion
        || (TableExpansion && originalRegistry_->budget_.contractVersion != 8u)
        || (!TableExpansion && originalRegistry_->budget_.contractVersion == 8u)) {
        lastOriginalRegistrationDiagnostic_ = "RHI-ORIGINAL-TABLE-FACTORY-PROFILE";
        return {};
    }
    const auto decision = [&] {
        if constexpr (std::is_same_v<Contract, OriginalAcceptedGraphicsPipelineFactoryContract>
            || std::is_same_v<Contract, OriginalAcceptedComputePipelineFactoryContract>)
            return Contract::lowering(backend(), true, budget.contractVersion);
        else
            return Contract::lowering(backend(), true);
    }();
    if (!decision.factoryEligible) {
        lastOriginalRegistrationDiagnostic_ = decision.diagnostic;
        return {};
    }
    if (!wrapperQuota || wrapperQuota->limitBytes() != budget.maxCpuWrapperBytes) {
        lastOriginalRegistrationDiagnostic_ = quotaId;
        return {};
    }
    const std::array domains{wrapperQuota,
        originalRegistry_->quota_,
        originalLifetime_->quota_,
        creation->originalMetadataQuota()};
    if (!core::SharedOwnerProjection::hasIndependentOwners(
            domains.size(), [&](std::size_t i) -> const auto& {
                return domains[i];
            })) {
        lastOriginalRegistrationDiagnostic_ = domainId;
        return {};
    }
    for (const auto& quota :
        std::array{wrapperQuota, originalRegistry_->quota_, originalLifetime_->quota_})
        if (creation->usesCpuQuotaDomain(quota)) {
            lastOriginalRegistrationDiagnostic_ = domainId;
            return {};
        }
    if (OriginalResourceRegistrationContract::observesNativeLoss(
            originalRegistry_->budget_.contractVersion)) {
        const auto loss = originalRegistry_->loss_.lock();
        if (!loss || originalRegistry_->overlapsCpuQuota(wrapperQuota)
            || creation->usesCpuQuotaDomain(loss->originalMetadataQuota())) {
            lastOriginalRegistrationDiagnostic_ = "RHI-ORIGINAL-TYPED-LOSS-PIPELINE-QUOTA-DOMAIN";
            return {};
        }
    }
    core::CpuMemoryQuotaScope wrapper(wrapperQuota);
    auto candidate = [&] {
        if constexpr (std::is_same_v<Pipeline, RenderPipeline>)
            return createOriginalPipeline<Pipeline>(creation->definition(),
                creation->originalLayout(),
                kind,
                std::forward<Factory>(factory),
                {},
                creation);
        else
            return createOriginalPipeline<Pipeline>(creation->definition(),
                creation->originalLayout(),
                kind,
                std::forward<Factory>(factory),
                creation);
    }();
    if (candidate) {
        candidate->originalWrapperQuota_ = wrapperQuota;
        candidate->originalWrapperCpuBytes_ = budget.maxCpuWrapperBytes;
    }
    return candidate;
}

template <class Pipeline, class Desc, class Factory>
inline core::UniquePtr<Pipeline> GpuDevice::createOriginalPipeline(const Desc& desc,
    const std::shared_ptr<PipelineLayout>& layout,
    OriginalRegisteredResourceKind kind,
    Factory&& factory,
    std::shared_ptr<const OriginalComputePipelineCreation> creation,
    std::shared_ptr<const OriginalGraphicsPipelineCreation> graphicsCreation)
{
    try {
        if (originalRegistry_ && originalRegistry_->budget_.contractVersion >= 5u
            && kind == OriginalRegisteredResourceKind::ComputePipeline && !creation) {
            lastOriginalRegistrationDiagnostic_ = "RHI-ACCEPTED-COMPUTE-FACTORY-CREATION-MISSING";
            return {};
        }
        if (originalRegistry_ && originalRegistry_->budget_.contractVersion >= 6u
            && kind == OriginalRegisteredResourceKind::GraphicsPipeline && !graphicsCreation) {
            lastOriginalRegistrationDiagnostic_ = "RHI-ACCEPTED-GRAPHICS-FACTORY-CREATION-MISSING";
            return {};
        }
        auto record = reserveOriginalPipelineRegistration(kind, layout);
        if (!record)
            return {};
        if (desc.layout != layout.get()) {
            lastOriginalRegistrationDiagnostic_ = "RHI-ORIGINAL-PIPELINE-LAYOUT-MISMATCH";
            return {};
        }
        if (!desc.shaderPackage) {
            lastOriginalRegistrationDiagnostic_ = "RHI-ORIGINAL-PIPELINE-PACKAGE-MISSING";
            return {};
        }
        if (originalRegistry_->budget_.contractVersion >= 4u) {
            core::CpuMemoryQuotaScope scope(originalRegistry_->quota_);
            using Contract = PipelineShaderDeclarationContract;
            if constexpr (std::is_same_v<Desc, RenderPipelineDesc>) {
                const bool fragment = !desc.fragmentShader.entryPoint.empty(),
                           tessellation = desc.topology == PrimitiveTopology::PatchList;
                const auto count = 1u + (fragment ? 1u : 0u) + (tessellation ? 2u : 0u);
                const auto entryAt = [&](std::size_t i) -> const ShaderEntrypoint& {
                    if (!i)
                        return desc.vertexShader;
                    if (fragment && i == 1u)
                        return desc.fragmentShader;
                    return i == 1u + (fragment ? 1u : 0u) ? desc.tessellationControlShader
                                                          : desc.tessellationEvaluationShader;
                };
                lastOriginalRegistrationDiagnostic_ =
                    graphicsCreation && originalRegistry_->budget_.contractVersion == 8u
                    ? Contract::validateTableExpansion({1u, true},
                          graphicsCreation->originalInput(),
                          layout->desc(),
                          *desc.shaderPackage,
                          count,
                          Contract::Kind::GraphicsPipeline,
                          entryAt)
                    : graphicsCreation
                    ? Contract::validateOriginal(graphicsCreation->originalInput(),
                          layout->desc(),
                          *desc.shaderPackage,
                          count,
                          Contract::Kind::GraphicsPipeline,
                          entryAt)
                    : Contract::validate(layout->desc(),
                          *desc.shaderPackage,
                          count,
                          Contract::Kind::GraphicsPipeline,
                          entryAt);
            } else {
                const auto entryAt = [&](std::size_t) -> const ShaderEntrypoint& {
                    return desc.computeShader;
                };
                lastOriginalRegistrationDiagnostic_ =
                    creation && originalRegistry_->budget_.contractVersion == 8u
                    ? Contract::validateTableExpansion({1u, true},
                          creation->originalInput(),
                          layout->desc(),
                          *desc.shaderPackage,
                          1u,
                          Contract::Kind::ComputePipeline,
                          entryAt)
                    : creation ? Contract::validateOriginal(creation->originalInput(),
                                     layout->desc(),
                                     *desc.shaderPackage,
                                     1u,
                                     Contract::Kind::ComputePipeline,
                                     entryAt)
                               : Contract::validate(layout->desc(),
                                     *desc.shaderPackage,
                                     1u,
                                     Contract::Kind::ComputePipeline,
                                     entryAt);
            }
            if (lastOriginalRegistrationDiagnostic_)
                return {};
        }
        std::shared_ptr<const ShaderPackageDesc> package;
        std::shared_ptr<Desc> definition;
        {
            core::CpuMemoryQuotaScope scope(originalRegistry_->quota_);
            package = core::makeShared<ShaderPackageDesc>(
                ShaderPackageStateCopy::copy(*desc.shaderPackage));
            if (originalRegistry_->budget_.contractVersion >= 4u) {
                if constexpr (std::is_same_v<Desc, RenderPipelineDesc>)
                    definition = core::makeShared<Desc>(RenderPipelineStateCopy::copy(desc));
                else
                    definition = core::makeShared<Desc>(ComputePipelineStateCopy::copy(desc));
                definition->layout = layout.get();
                definition->shaderPackage = package.get();
            }
        }
        Desc frozen;
        if (!definition) {
            if constexpr (std::is_same_v<Desc, RenderPipelineDesc>)
                frozen = RenderPipelineStateCopy::copy(desc);
            else
                frozen = ComputePipelineStateCopy::copy(desc);
            frozen.shaderPackage = package.get();
        }
        auto candidate = factory(definition ? *definition : frozen);
        if (!candidate) {
            if (!lastOriginalRegistrationDiagnostic_)
                lastOriginalRegistrationDiagnostic_ = "RHI-ORIGINAL-PIPELINE-FACTORY-FAILED";
            return {};
        }
        if (candidate->originalRegistration_.record_ || candidate->originalLayoutOwner_
            || candidate->originalPackageOwner_ || candidate->originalRenderDefinition_
            || candidate->originalComputeDefinition_ || candidate->originalComputeCreation_
            || candidate->originalGraphicsCreation_) {
            lastOriginalRegistrationDiagnostic_ = "RHI-ORIGINAL-REGISTRATION-CANDIDATE-IDENTITY";
            return {};
        }
        if (candidate->layout() != layout.get() || !candidate->validationReport().success) {
            lastOriginalRegistrationDiagnostic_ = "RHI-ORIGINAL-PIPELINE-CANDIDATE-DEFINITION";
            return {};
        }
        if (creation
            && (lastOriginalRegistrationDiagnostic_ =
                    candidate->acceptedWrapperSealDiagnostic(creation)))
            return {};
        if (graphicsCreation
            && (lastOriginalRegistrationDiagnostic_ =
                    candidate->acceptedWrapperSealDiagnostic(graphicsCreation)))
            return {};
        if (OriginalResourceRegistrationContract::observesNativeLoss(
                originalRegistry_->budget_.contractVersion)
            && (lastOriginalRegistrationDiagnostic_ =
                    validateOriginalNativeLossPipeline(*candidate)))
            return {};
        record->object_ = candidate.get();
        if ((lastOriginalRegistrationDiagnostic_ = record->validate()))
            return {};
        candidate->originalLayoutOwner_ = layout;
        candidate->originalPackageOwner_ = std::move(package);
        if constexpr (std::is_same_v<Desc, RenderPipelineDesc>)
            candidate->originalRenderDefinition_ = std::move(definition);
        else
            candidate->originalComputeDefinition_ = std::move(definition);
        candidate->originalComputeCreation_ = std::move(creation);
        candidate->originalGraphicsCreation_ = std::move(graphicsCreation);
        candidate->originalRegistration_.record_ = std::move(record);
        return candidate;
    } catch (const std::bad_alloc&) {
        lastOriginalRegistrationDiagnostic_ = "RHI-ORIGINAL-REGISTRATION-ALLOCATION";
        return {};
    }
}

template <class Control>
inline const char* GpuDevice::bindOriginalControlDomain(
    const Control* control, const OriginalDeviceLifetime::Ticket& ticket) noexcept
{
    if (!control)
        return nullptr;
    if (!control->active())
        return "RHI-ORIGINAL-RESOURCE-STATE-DOMAIN-INACTIVE";
    if (control->pending())
        return "RHI-ORIGINAL-RESOURCE-STATE-DOMAIN-PENDING";
    return control->domain_.bind(ticket);
}

inline const char* GpuDevice::bindOriginalStateDomain(
    GpuBuffer& object, const OriginalDeviceLifetime::Ticket& ticket) noexcept
{
    return bindOriginalControlDomain(object.originalStateControl_, ticket);
}

inline const char* GpuDevice::bindOriginalStateDomain(
    GpuTexture& object, const OriginalDeviceLifetime::Ticket& ticket) noexcept
{
    if (object.originalNormalizedState_)
        return bindOriginalControlDomain(object.originalNormalizedState_, ticket);
    return bindOriginalControlDomain(object.originalUniformState_, ticket);
}

inline const char* GpuDevice::bindOriginalStateDomain(
    GpuTextureView&, const OriginalDeviceLifetime::Ticket&) noexcept
{
    return nullptr;
}

inline const char* GpuDevice::bindOriginalStateDomain(
    GpuSampler&, const OriginalDeviceLifetime::Ticket&) noexcept
{
    // Immutable sampler state has no memory-barrier control domain.
    return nullptr;
}

} // namespace arp::rhi

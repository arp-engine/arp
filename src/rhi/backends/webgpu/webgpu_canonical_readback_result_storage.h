#pragma once
#include "rhi/backends/webgpu/webgpu_bindless_root_cold_admission.h"
#include "rhi/backends/webgpu/webgpu_canonical_cold_admission.h"
#include "rhi/backends/webgpu/webgpu_native_command_preparation.h"
#include "rhi/backends/webgpu/webgpu_original_typed_cold_admission.h"
#include "rhi/frontend/command_recording.h"
#include "rhi/frontend/command_sources.h"
#include "rhi/frontend/command_transfer_validation.h"

namespace arp::test {
class WebGpuCanonicalReadbackJoinsFixture;
class WebGpuOriginalBindlessPreparationFixture;
} // namespace arp::test

namespace arp::rhi_webgpu {
// Cold native-bound Result allocation plus private exact write/freeze boundaries
// consumed by original /5 custody and the existing Submission issuer. Publication
// requires separate cold opt-in. No public writer or legacy authority is accepted.
class WebGpuCanonicalReadbackResultStorage final : public core::QuotaBoundSharedObjectIdentity {
    friend class WebGpuCanonicalCallbackCustody;
    friend class WebGpuNativeCommandPreparation;
    friend class arp::test::WebGpuCanonicalReadbackJoinsFixture;
    friend class arp::test::WebGpuOriginalBindlessPreparationFixture;
    using Contract = rhi::OriginalCommandReadbackResultStorageContract;
    using Admission = WebGpuCanonicalColdAdmission;
    using TypedAdmission = WebGpuOriginalTypedColdAdmission;
    using BufferAdmission = WebGpuBindlessRootColdAdmission;
    using BufferSnapshot = rhi::OriginalCommandBufferReadbackResultSnapshot;
    using Snapshot = rhi::OriginalCommandReadbackResultSnapshot;
    using Request = rhi::TextureReadbackRequestDeclaration;

    struct BufferResult {
        std::shared_ptr<const BufferSnapshot> snapshot;
        std::shared_ptr<WebGpuNativeBufferPayload> buffer;
        std::uint64_t bytes = 0u;
        rhi::BufferReadbackRangeContract::Range mapRange;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        WGPUBuffer handle = nullptr;
        WGPUDevice device = nullptr;
#endif
    };

    class ConstructionKey {
        friend class WebGpuCanonicalReadbackResultStorage;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    using Budget = Contract::Budget;

    WebGpuCanonicalReadbackResultStorage(ConstructionKey,
        std::shared_ptr<const Admission> admission,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        Budget budget)
        : admission_(std::move(admission)), quota_(std::move(quota)), budget_(budget),
          snapshots_(
              static_cast<std::size_t>(admission_->requirements().readback.resultDeclarations))
    {}

    WebGpuCanonicalReadbackResultStorage(ConstructionKey,
        std::shared_ptr<const TypedAdmission> admission,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        Budget budget)
        : typedAdmission_(std::move(admission)), quota_(std::move(quota)), budget_(budget),
          snapshots_(
              static_cast<std::size_t>(typedAdmission_->requirements().readback.resultDeclarations))
    {}

    WebGpuCanonicalReadbackResultStorage(
        ConstructionKey, std::shared_ptr<const BufferAdmission> admission, Budget budget)
        : bufferAdmission_(std::move(admission)), quota_(bufferAdmission_->originalReadbackBinding()
                                                          ->originalResult()
                                                          ->originalMetadataQuota()),
          budget_(budget), snapshots_(0u),
          buffers_(bufferAdmission_->originalReadbackBindings().size())
    {
        for (const auto& binding : bufferAdmission_->originalReadbackBindings()) {
            BufferResult entry;
            entry.snapshot = binding->originalResult();
            entry.buffer =
                webGpuNativePayloadFor(*binding->originalReadbackSource()->originalObject());
            entry.bytes = entry.buffer ? entry.buffer->sizeBytes : 0u;
            entry.mapRange = {entry.bytes,
                binding->originalRequest()->offsetBytes(),
                binding->originalRequest()->byteCapacity()};
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
            entry.handle = entry.buffer ? entry.buffer->buffer : nullptr;
            entry.device = entry.buffer ? entry.buffer->ownerDevice : nullptr;
#endif
            (void)buffers_.append(std::move(entry));
        }
    }

    // Retain the exact preallocated buffer Result; do not allocate another
    // snapshot or silently reinterpret its Request as texture readback.
    static std::shared_ptr<const WebGpuCanonicalReadbackResultStorage> prepareOriginalBuffer(
        std::shared_ptr<const BufferAdmission> admission,
        const rhi::GpuDevice& device,
        const Budget& budget,
        const char*& diagnostic)
    {
        if ((diagnostic = inspectBuffer(admission, budget)))
            return {};
        const auto lowering =
            rhi::semantics::OriginalCommandReadbackResultStorageLowering::bufferLowering(
                device.backend(), budget.contractVersion);
        if (!lowering.coldStorageAvailable) {
            diagnostic = lowering.diagnostic;
            return {};
        }
        if ((diagnostic = rhi::OriginalCommandResourceSources::validateRegisteredLifetime(
                 admission->originalClosure()->originalInventory(), device)))
            return {};
        for (const auto& binding : admission->originalReadbackBindings()) {
            const auto& lease = binding->originalReadbackSource();
            if ((diagnostic = binding->originalRequest()->matchesOriginalSource(lease, device)))
                return {};
            if (!webGpuNativePayloadFor(*lease->originalObject())) {
                diagnostic = "RHI-WEBGPU-BUFFER-READBACK-STORAGE-PAYLOAD";
                return {};
            }
        }
        try {
            core::CpuMemoryQuotaScope metadata(admission->originalRoot()->originalMetadataQuota());
            auto result = shareOriginal(core::makeUnique<WebGpuCanonicalReadbackResultStorage>(
                ConstructionKey{}, std::move(admission), budget));
            if ((diagnostic = result->validate()))
                return {};
            return result;
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-WEBGPU-READBACK-STORAGE-ALLOCATION";
        } catch (const std::length_error&) {
            diagnostic = "RHI-WEBGPU-READBACK-STORAGE-ALLOCATION";
        }
        return {};
    }

    static std::shared_ptr<const WebGpuCanonicalReadbackResultStorage> prepareOriginal(
        std::shared_ptr<const Admission> admission,
        const rhi::GpuDevice& device,
        const Budget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        return prepareBound(std::move(admission), device, budget, std::move(quota), diagnostic);
    }

    static std::shared_ptr<const WebGpuCanonicalReadbackResultStorage> prepareOriginalTyped(
        std::shared_ptr<const TypedAdmission> admission,
        const rhi::GpuDevice& device,
        const Budget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        return prepareBound(std::move(admission), device, budget, std::move(quota), diagnostic);
    }

private:
    template <class BoundAdmission>
    static std::shared_ptr<const WebGpuCanonicalReadbackResultStorage> prepareBound(
        std::shared_ptr<const BoundAdmission> admission,
        const rhi::GpuDevice& device,
        const Budget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        diagnostic = inspect(admission, budget, quota);
        if (diagnostic)
            return {};
        const auto& sources = admission->originalRoot()
                                  ->originalGraph()
                                  ->originalInputs()
                                  ->originalScalars()
                                  ->originalUsage()
                                  ->originalSources();
        if ((diagnostic = rhi::OriginalCommandResourceSources::validateRegisteredLifetime(
                 sources, device)))
            return {};
        if (device.overlapsOriginalDeviceCpuOwner(quota)) {
            diagnostic = "RHI-WEBGPU-READBACK-STORAGE-QUOTA-DOMAIN";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope metadata(admission->originalRoot()->originalMetadataQuota());
            auto storage = core::makeUnique<WebGpuCanonicalReadbackResultStorage>(
                ConstructionKey{}, admission, quota, budget);
            diagnostic = visitRequests(*admission, [&](const auto& request) -> const char* {
                if (storage->snapshots_.findIf([&](const auto& s) {
                        return core::SharedOwnerProjection::sameOwner(
                            s->originalRequest(), request);
                    }))
                    return nullptr;
                auto snapshot = Snapshot::prepareOriginalCold(request, quota, diagnostic);
                if (!snapshot)
                    return diagnostic;
                return storage->snapshots_.append(std::move(snapshot))
                    ? nullptr
                    : "RHI-WEBGPU-READBACK-STORAGE-SNAPSHOT-CAPACITY";
            });
            if (diagnostic)
                return {};
            auto result = shareOriginal(std::move(storage));
            if ((diagnostic = result->validate()))
                return {};
            return result;
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-WEBGPU-READBACK-STORAGE-ALLOCATION";
            return {};
        } catch (const std::length_error&) {
            diagnostic = "RHI-WEBGPU-READBACK-STORAGE-ALLOCATION";
            return {};
        }
    }

public:
    static const char* validateOriginal(
        const std::shared_ptr<const WebGpuCanonicalReadbackResultStorage>& self) noexcept
    {
        return !self || !self->hasQuotaBoundSharedOwner(self) ? "RHI-WEBGPU-READBACK-STORAGE-OWNER"
                                                              : self->validate();
    }

    const char* validate() const noexcept
    {
        if (bufferAdmission_) {
            if (const auto* id = inspectBuffer(bufferAdmission_, budget_))
                return id;
            return validateBufferSnapshot();
        }
        if (const auto* id = typedAdmission_ ? inspect(typedAdmission_, budget_, quota_)
                                             : inspect(admission_, budget_, quota_))
            return id;
        return validateSnapshots();
    }

private:
    const char* validateSnapshots() const noexcept
    {
        if (bufferAdmission_)
            return validateBufferSnapshot();
        if (!core::SharedOwnerProjection::sameOwner(
                originalMetadataQuota(), originalRoot()->originalMetadataQuota()))
            return "RHI-WEBGPU-READBACK-STORAGE-QUOTA";
        if (snapshots_.values().size() != readbackRequirements().resultDeclarations)
            return "RHI-WEBGPU-READBACK-STORAGE-COVERAGE";
        const auto validateRequest = [&](const auto& request) noexcept -> const char* {
            const auto* snapshot = snapshots_.findIf([&](const auto& s) {
                return core::SharedOwnerProjection::sameOwner(s->originalRequest(), request);
            });
            if (!snapshot)
                return "RHI-WEBGPU-READBACK-STORAGE-REQUEST";
            if (!core::SharedOwnerProjection::sameOwner(
                    (*snapshot)->originalMetadataQuota(), quota_))
                return "RHI-WEBGPU-READBACK-STORAGE-QUOTA";
            return Snapshot::validateOriginalCold(*snapshot, request);
        };
        return typedAdmission_ ? visitRequests(*typedAdmission_, validateRequest)
                               : visitRequests(*admission_, validateRequest);
    }

public:
    const auto& originalAdmission() const noexcept { return admission_; }

    const auto& originalTypedAdmission() const noexcept { return typedAdmission_; }

    const auto& originalBufferAdmission() const noexcept { return bufferAdmission_; }

    const std::shared_ptr<rhi::OriginalCommandStateTransaction>& originalRoot() const noexcept
    {
        return bufferAdmission_ ? bufferAdmission_->originalRoot()
            : typedAdmission_   ? typedAdmission_->originalRoot()
                                : admission_->originalRoot();
    }

    rhi::OriginalCommandReadbackResultContract::Requirements readbackRequirements() const noexcept
    {
        return bufferAdmission_ ? bufferAdmission_->requirements().readback
            : typedAdmission_   ? typedAdmission_->requirements().readback
                                : admission_->requirements().readback;
    }

    rhi::OriginalCommandReadbackMapContract::Requirements readbackMapRequirements() const noexcept
    {
        return bufferAdmission_ ? bufferAdmission_->requirements().readbackMap
            : typedAdmission_   ? typedAdmission_->requirements().readbackMap
                                : admission_->requirements().readbackMap;
    }

    const auto& originalResultQuota() const noexcept { return quota_; }

    const auto& snapshots() const noexcept { return snapshots_.values(); }

    std::size_t snapshotCount() const noexcept
    {
        return bufferAdmission_ ? buffers_.values().size() : snapshots_.values().size();
    }

    const auto& originalBufferSnapshot() const noexcept
    {
        return bufferAdmission_->originalReadbackBinding()->originalResult();
    }

    std::uint32_t retainedOwnerCount() const noexcept
    {
        if (bufferAdmission_)
            return bufferAdmission_->retainedOwnerCount() + Contract::fixedAdditionalOwners
                + 2u * static_cast<std::uint32_t>(buffers_.values().size());
        return (typedAdmission_ ? typedAdmission_->retainedOwnerCount()
                                : admission_->retainedOwnerCount())
            + Contract::fixedAdditionalOwners
            + static_cast<std::uint32_t>(snapshots_.values().size());
    }

    bool nativeExecutionProven() const noexcept { return false; }

    const char* executionDiagnostic() const noexcept
    {
        if (const auto* id = validate())
            return id;
        return "RHI-WEBGPU-READBACK-STORAGE-MAP-PUBLICATION-CLOSED";
    }

private:
    static const char* inspectBuffer(
        const std::shared_ptr<const BufferAdmission>& admission, const Budget& budget) noexcept
    {
        if (const auto* id = Contract::validateBufferIntent(budget))
            return id;
        if (const auto* id = BufferAdmission::validateOriginal(admission))
            return id;
        const auto& requirements = admission->requirements().readback;
        const auto count = admission->originalReadbackBindings().size();
        if (!count || requirements.resultDeclarations != count
            || (count == 1u && requirements.uniqueResultBytes != sizeof(std::uint32_t)))
            return "RHI-WEBGPU-BUFFER-READBACK-STORAGE-FAULT-PROFILE";
        if (const auto* id = Contract::validateBufferRequirements(
                requirements, admission->retainedOwnerCount(), budget))
            return id;
        const auto& binding = admission->originalReadbackBinding();
        const auto& quota = binding->originalResult()->originalMetadataQuota();
        if (quota->limitBytes() != budget.maxCpuResultBytes)
            return "RHI-WEBGPU-READBACK-STORAGE-RESULT-QUOTA";
        const auto& lifecycle = admission->originalReservation()->originalSeal()->originalBudget();
        const auto owners = std::uint64_t(admission->retainedOwnerCount())
            + Contract::fixedAdditionalOwners + 2u * count;
        const auto& source = admission->originalClosure()->originalInventory()->originalBudget();
        if (owners > budget.maxRetainedOwners || owners > lifecycle.maxRecordingOwners
            || owners > lifecycle.maxSubmissionOwners || owners > lifecycle.maxCallbackOwners
            || owners > source.maxGraphOwners)
            return "RHI-WEBGPU-BUFFER-READBACK-STORAGE-OWNER-CAPACITY";
        return nullptr;
    }

    const char* validateBufferSnapshot() const noexcept
    {
        const auto& bindings = bufferAdmission_->originalReadbackBindings();
        if (!snapshots_.values().empty() || buffers_.values().size() != bindings.size()
            || !core::SharedOwnerProjection::sameOwner(
                originalMetadataQuota(), bufferAdmission_->originalRoot()->originalMetadataQuota()))
            return "RHI-WEBGPU-BUFFER-READBACK-STORAGE-ORIGINAL-RESULT";
        for (std::size_t i = 0u; i < bindings.size(); ++i) {
            const auto& binding = bindings[i];
            const auto& entry = buffers_.values()[i];
            if (!core::SharedOwnerProjection::sameOwner(entry.snapshot, binding->originalResult())
                || !core::SharedOwnerProjection::sameOwner(
                    quota_, entry.snapshot->originalMetadataQuota()))
                return "RHI-WEBGPU-BUFFER-READBACK-STORAGE-ORIGINAL-RESULT";
            if (const auto* id = BufferSnapshot::validateOriginalCold(
                    entry.snapshot, binding->originalRequest()))
                return id;
            const auto& lease = binding->originalReadbackSource();
            const auto payload = webGpuNativePayloadFor(*lease->originalObject());
            if (!entry.buffer || !core::SharedOwnerProjection::sameOwner(entry.buffer, payload)
                || entry.bytes != entry.buffer->sizeBytes
                || !entry.mapRange.matches({entry.bytes,
                    binding->originalRequest()->offsetBytes(),
                    binding->originalRequest()->byteCapacity()})
                || entry.mapRange.diagnostic() || entry.mapRange.sizeBytes > SIZE_MAX)
                return "RHI-WEBGPU-BUFFER-READBACK-STORAGE-NATIVE-BINDING";
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
            if (!entry.handle || entry.handle != entry.buffer->buffer
                || entry.device != entry.buffer->ownerDevice)
                return "RHI-WEBGPU-BUFFER-READBACK-STORAGE-NATIVE-BINDING";
#else
            return "RHI-WEBGPU-BUFFER-READBACK-STORAGE-NATIVE-UNAVAILABLE";
#endif
        }
        return nullptr;
    }

    const char* validateBufferMapReceipt(
        const WebGpuNativeBufferMapReceipt& receipt, std::size_t index) const noexcept;
    // Only the original custody can consume a genuine receipt. No caller gets
    // a mutable CPU range, and successful copying grants no publication.
    static const char* readOriginalUnpublished(
        const std::shared_ptr<const WebGpuCanonicalReadbackResultStorage>&,
        const std::shared_ptr<const rhi::OriginalCommandRecordingSeal>&,
        std::size_t,
        WebGpuNativeBufferMapReceipt&) noexcept;
    static const char* publishOriginalCommitted(
        const std::shared_ptr<const WebGpuCanonicalReadbackResultStorage>&,
        const std::shared_ptr<const rhi::OriginalCommandRecordingSeal>&,
        const rhi::OriginalCommandReadbackMapDrainContract::Facts&,
        const rhi::OriginalCommandReadbackSnapshotPublicationContract::Intent&) noexcept;

    const std::uint8_t* unpublishedBytesForInspection(std::size_t index) const noexcept
    {
        return index < snapshots_.values().size() ? snapshots_.values()[index]->bytes_.data()
                                                  : nullptr;
    }

    static const char* validateOriginalRetained(
        const std::shared_ptr<const WebGpuCanonicalReadbackResultStorage>& self,
        const std::shared_ptr<const rhi::OriginalCommandRecordingSeal>& seal,
        const std::shared_ptr<const Request>* request = nullptr,
        const WebGpuNativeBufferMapReceipt* receipt = nullptr) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-WEBGPU-READBACK-STORAGE-OWNER";
        const auto& reservation = self->bufferAdmission_
            ? self->bufferAdmission_->originalReservation()
            : self->typedAdmission_ ? self->typedAdmission_->originalReservation()
                                    : self->admission_->originalReservation();
        if (!seal || !seal->hasQuotaBoundSharedOwner(seal)
            || !core::SharedOwnerProjection::sameOwner(seal, reservation->originalSeal()))
            return "RHI-WEBGPU-READBACK-STORAGE-RETAINED-SEAL";
        if (self->originalRoot()->status() != rhi::OriginalCommandStateTransaction::Status::Pending
            || self->originalRoot()->journalEmpty())
            return "RHI-WEBGPU-READBACK-STORAGE-RETAINED-PENDING";
        if (bool(request) != bool(receipt))
            return "RHI-WEBGPU-READBACK-STORAGE-MAP-BINDING";
        if (self->bufferAdmission_) {
            if (!self->originalRoot()->originalPlannedSequenceBudget()
                || !rhi::OriginalBindlessRootCallbackColdContract::matchesCompletionProfile(
                    seal->originalBudget().contractVersion,
                    self->bufferAdmission_->originalBudget().contractVersion,
                    self->originalRoot()->originalContentPlanContractVersion()))
                return "RHI-WEBGPU-ROOT-READBACK-RETAINED-PROFILE";
            for (const auto& binding : self->bufferAdmission_->originalReadbackBindings())
                if (!binding->supportsOriginalPendingCustody()
                    || !core::SharedOwnerProjection::sameOwner(
                        binding->originalGraph(), self->originalRoot()->originalGraph())
                    || !binding->matchesOriginalSequence(
                        *self->originalRoot()->originalPlannedSequenceBudget()))
                    return "RHI-WEBGPU-ROOT-READBACK-RETAINED-PROFILE";
            return self->validateBufferSnapshot();
        }
        if (const auto* id = self->typedAdmission_
                ? inspect(
                      self->typedAdmission_, self->budget_, self->quota_, false, request, receipt)
                : inspect(self->admission_, self->budget_, self->quota_, false, request, receipt))
            return id;
        return self->validateSnapshots();
    }

    template <class BoundAdmission, class Visitor>
    static const char* visitRequests(const BoundAdmission& admission, Visitor&& visit)
    {
        const auto& usage = *admission.originalRoot()
                                 ->originalGraph()
                                 ->originalInputs()
                                 ->originalScalars()
                                 ->originalUsage();
        for (const auto& command : usage.commands())
            if (command.kind == rhi::CommandUsageKind::TextureReadback) {
                const auto index = usage.references()[command.firstReference + 1u].sourceIndex;
                const auto& source =
                    std::get<std::shared_ptr<const rhi::OriginalCommandTextureReadbackSource>>(
                        usage.originalSources()->sources()[index]);
                if (const auto* id = visit(source->originalSource()->originalRequest()))
                    return id;
            }
        return nullptr;
    }

    static const char* inspect(const std::shared_ptr<const TypedAdmission>& admission,
        const Budget& budget,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        bool cold = true,
        const std::shared_ptr<const Request>* request = nullptr,
        const WebGpuNativeBufferMapReceipt* receipt = nullptr) noexcept
    {
        if (const auto* id = Contract::validateTypedIntent(budget))
            return id;
        const auto lowering =
            rhi::semantics::OriginalCommandReadbackResultStorageLowering::typedLowering(
                rhi::Backend::WebGPU, budget.contractVersion);
        if (!lowering.coldStorageAvailable)
            return lowering.diagnostic;
        if (const auto* id = cold
                ? TypedAdmission::validateOriginal(admission)
                : WebGpuNativeCommandPreparation::validateTypedAdmissionRetained(admission))
            return id;
        if (!rhi::OriginalTypedNativeColdContract::readbackJoinVersion(
                admission->originalRequest().contractVersion)
            || !rhi::OriginalTypedNativeColdContract::hasReadbackDemand(
                admission->originalRequest()))
            return "RHI-WEBGPU-TYPED-READBACK-STORAGE-PROFILE";
        const auto& closure = admission->originalPayload()->originalDecision()->originalClosure();
        const auto& joins = closure->originalReadbackJoins();
        if (!joins
            || joins->joinContractVersion()
                != rhi::OriginalTypedNativeColdContract::readbackJoinVersion(
                    admission->originalRequest().contractVersion)
            || !core::SharedOwnerProjection::sameOwner(
                joins->originalRoot(), admission->originalRoot())
            || !core::SharedOwnerProjection::sameOwner(
                joins->originalProgram(), closure->originalStream()))
            return "RHI-WEBGPU-TYPED-READBACK-STORAGE-JOIN";
        if (request && receipt)
            if (const auto* id = joins->validateRetainedMapping(
                    admission->originalReservation()->originalSeal(), *request, *receipt))
                return id;
        if (const auto* id = Contract::validateTypedRequirements(
                admission->requirements().readback, admission->retainedOwnerCount(), budget))
            return id;
        const auto owners = std::uint64_t(admission->retainedOwnerCount())
            + Contract::fixedAdditionalOwners
            + admission->requirements().readback.resultDeclarations;
        const auto& source = admission->originalRoot()
                                 ->originalGraph()
                                 ->originalInputs()
                                 ->originalScalars()
                                 ->originalUsage()
                                 ->originalSources()
                                 ->originalBudget();
        const auto& lifecycle = admission->originalReservation()->originalSeal()->originalBudget();
        if (budget.maxRetainedOwners > source.maxGraphOwners
            || owners > admission->originalRequest().maxRetainedOwners
            || owners > lifecycle.maxRecordingOwners || owners > lifecycle.maxSubmissionOwners
            || owners > lifecycle.maxCallbackOwners)
            return "RHI-WEBGPU-TYPED-READBACK-STORAGE-OWNER-CAPACITY";
        if (!quota || quota->limitBytes() != budget.maxCpuResultBytes)
            return "RHI-WEBGPU-READBACK-STORAGE-RESULT-QUOTA";
        return inspectDomains(*admission, joins, quota);
    }

    static const char* inspect(const std::shared_ptr<const Admission>& admission,
        const Budget& budget,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        bool cold = true,
        const std::shared_ptr<const Request>* request = nullptr,
        const WebGpuNativeBufferMapReceipt* receipt = nullptr) noexcept
    {
        if (const auto* id = Contract::validateIntent(budget))
            return id;
        if (!admission || !admission->hasQuotaBoundSharedOwner(admission))
            return "RHI-WEBGPU-CANONICAL-COLD-OWNER";
        if (const auto* id = cold ? Admission::validateOriginal(admission)
                                  : admission->validateRetainedOwnership(request, receipt))
            return id;
        if ((admission->originalBudget().contractVersion != 4u
                && admission->originalBudget().contractVersion != 5u)
            || !admission->originalReadbackJoins())
            return "RHI-WEBGPU-READBACK-STORAGE-PROFILE";
        if (const auto* id = Contract::validateRequirements(
                admission->requirements().readback, admission->retainedOwnerCount(), budget))
            return id;
        if (!quota || quota->limitBytes() != budget.maxCpuResultBytes)
            return "RHI-WEBGPU-READBACK-STORAGE-RESULT-QUOTA";
        return inspectDomains(*admission, admission->originalReadbackJoins(), quota);
    }

    template <class BoundAdmission>
    static const char* inspectDomains(const BoundAdmission& admission,
        const std::shared_ptr<const WebGpuCanonicalReadbackJoins>& joins,
        const std::shared_ptr<core::CpuMemoryQuota>& quota) noexcept
    {
        const auto& root = admission.originalRoot();
        const auto& graph = root->originalGraph();
        const auto& inputs = graph->originalInputs();
        const auto& source = inputs->originalScalars()->originalUsage()->originalSources();
        const auto& seal = admission.originalReservation()->originalSeal();
        const std::array domains{source->originalMetadataQuota(),
            inputs->originalMetadataQuota(),
            graph->originalMetadataQuota(),
            root->originalMetadataQuota(),
            seal->originalSubmissionQuota(),
            seal->originalCallbackQuota(),
            quota};
        if (!core::SharedOwnerProjection::hasIndependentOwners(
                domains.size(), [&](std::size_t i) noexcept -> const auto& {
                    return domains[i];
                }))
            return "RHI-WEBGPU-READBACK-STORAGE-QUOTA-DOMAIN";
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        if (joins->overlapsCpuOwner(quota))
            return "RHI-WEBGPU-READBACK-STORAGE-QUOTA-DOMAIN";
#else
        return "RHI-WEBGPU-READBACK-STORAGE-NATIVE-UNAVAILABLE";
#endif
        return nullptr;
    }

    const std::shared_ptr<const Admission> admission_;
    const std::shared_ptr<const TypedAdmission> typedAdmission_;
    const std::shared_ptr<const BufferAdmission> bufferAdmission_;
    const std::shared_ptr<core::CpuMemoryQuota> quota_;
    const Budget budget_;
    core::BoundedSequence<std::shared_ptr<const Snapshot>> snapshots_;
    core::BoundedSequence<BufferResult> buffers_{0u};
};
} // namespace arp::rhi_webgpu

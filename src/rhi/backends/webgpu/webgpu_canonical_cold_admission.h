#pragma once

#include "rhi/backends/webgpu/native_validation.h"
#include "rhi/backends/webgpu/webgpu_canonical_native_closure.h"
#include "rhi/backends/webgpu/webgpu_canonical_readback_joins.h"
#include "rhi/backends/webgpu/webgpu_canonical_upload_joins.h"
#include "rhi/frontend/command_recording.h"
#include "rhi/semantics/command_lowering.h"

namespace arp::rhi_webgpu {
// Backend-private immutable lowering over exact canonical original ownership.
// No alternative graph/journal, callback userdata, native encoder or proof exists
// in this primitive. Native runtime must consume /1 counts before any GPU work.
class WebGpuCanonicalColdAdmission final : public core::QuotaBoundSharedObjectIdentity {
    friend class WebGpuNativeCommandPreparation;
    friend class WebGpuCanonicalReadbackResultStorage;
    using Contract = rhi::OriginalCommandNativeColdContract;
    using Root = rhi::OriginalCommandStateTransaction;
    using Closure = WebGpuCanonicalNativeClosure;
    using Reservation = rhi::OriginalCommandLifecycleReservation;

    class ConstructionKey {
        friend class WebGpuCanonicalColdAdmission;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    using Requirements = Contract::Requirements;
    using Budget = Contract::Budget;
    // Selected CopyPass/1 vocabulary. Unsupported commands require another
    // explicit lowering version; a CPU access plan alone cannot opt them in.
    static constexpr std::array language{rhi::CommandUsageKind::BeginTransferPass,
        rhi::CommandUsageKind::BufferCopy,
        rhi::CommandUsageKind::BufferToTextureCopy,
        rhi::CommandUsageKind::TextureToBufferCopy,
        rhi::CommandUsageKind::TextureToTextureCopy,
        rhi::CommandUsageKind::EndTransferPass,
        rhi::CommandUsageKind::TextureBarrier,
        rhi::CommandUsageKind::BeginRenderPass,
        rhi::CommandUsageKind::EndRenderPass,
        rhi::CommandUsageKind::Finish};

    static constexpr Requirements selectedRequirements() noexcept
    {
        constexpr auto scopes = WebGpuErrorScopeContract::count;
        constexpr auto lossCallbacks = rhi::OriginalDeviceNativeLossContract::callbackCount;
        constexpr auto lossRegistrations = rhi::OriginalDeviceNativeLossContract::registrationCount;
        constexpr std::uint32_t singleRootSubmission = 1u, queueCompletion = 1u;
        // Scope callbacks are registered once each. Allocated slots equal issued
        // registrations in this version; the ordinary recorder's unused creation
        // slots are expressly excluded. Existing original loss is not new work.
        constexpr auto callbacks = scopes + scopes + queueCompletion + lossCallbacks;
        constexpr auto registrations = scopes + scopes + queueCompletion + lossRegistrations;
        return {1u,
            static_cast<std::uint32_t>(language.size()),
            1u,
            1u,
            1u,
            scopes,
            scopes,
            queueCompletion,
            lossCallbacks,
            lossRegistrations,
            lossCallbacks,
            lossRegistrations,
            {1u, true, singleRootSubmission, callbacks, registrations}};
    }

    WebGpuCanonicalColdAdmission(ConstructionKey,
        std::shared_ptr<const Closure> closure,
        std::shared_ptr<const Reservation> reservation,
        Budget budget,
        std::uint32_t owners,
        Requirements required,
        std::shared_ptr<const WebGpuCanonicalUploadJoins> joins = {},
        std::shared_ptr<const WebGpuCanonicalReadbackJoins> readbacks = {}) noexcept
        : closure_(std::move(closure)), reservation_(std::move(reservation)), budget_(budget),
          owners_(owners), requirements_(required), joins_(std::move(joins)),
          readbacks_(std::move(readbacks))
    {}

    static const char* inspectOriginalPlan(const std::shared_ptr<const Root>& root) noexcept
    {
        if (const auto* id = Root::validateOriginal(root))
            return id;
        if (const auto* id =
                Contract::validateLegacyProfiles(root->originalGraph()->stateContractVersion()))
            return id;
        if (root->status() != Root::Status::Prepared || !root->hasOriginalPrototypePlan()
            || !root->journalEmpty())
            return "RHI-WEBGPU-CANONICAL-COLD-PLAN-STATUS";
        return inspectOriginalLanguage(root);
    }

    static const char* deriveRequirements(const std::shared_ptr<const Root>& root,
        const Budget& budget,
        Requirements& required) noexcept
    {
        required = {};
        if (const auto* id = Contract::validateIntent(budget))
            return id;
        if (const auto* id = Root::validateOriginal(root))
            return id;
        if (const auto* id =
                Contract::validateLegacyProfiles(root->originalGraph()->stateContractVersion()))
            return id;
        if (!root->hasOriginalPrototypePlan())
            return "RHI-WEBGPU-CANONICAL-COLD-PLAN-STATUS";
        Requirements pending = selectedRequirements();
        if (budget.contractVersion == 1u) {
            if (const auto* id = inspectOriginalLanguage(root))
                return id;
        } else {
            const auto& inputs = root->originalGraph()->originalInputs();
            const auto& source =
                inputs->originalScalars()->originalUsage()->originalSources()->originalBudget();
            rhi::OriginalCommandStreamBudget stream;
            stream.contractVersion = 1u;
            stream.prototypeOptIn = budget.prototypeOptIn;
            stream.maxCommands = budget.maxOriginalCommands;
            stream.maxDescriptors = source.maxInputDescriptors;
            stream.maxReferences = source.maxReferences;
            stream.maxCopyElements = budget.maxCopyElements;
            stream.maxRenderPasses = budget.maxRenderPasses;
            stream.maxAttachmentReferences = budget.maxAttachmentReferences;
            stream.maxRetainedOwners = budget.maxRetainedOwners;
            stream.maxCpuMetadataBytes = budget.maxCpuMetadataBytes;
            rhi::OriginalCommandStreamProgram::Counts counts;
            if (budget.contractVersion == 2u) {
                const auto result =
                    rhi::OriginalCommandOrderedCopyAttachmentContract::inspect(inputs, stream);
                if (result.diagnostic)
                    return result.diagnostic;
                counts = result.counts;
            } else if (budget.contractVersion == 3u) {
                stream.maxBufferUploads = budget.maxBufferUploads;
                stream.maxTextureUploadMips = budget.maxTextureUploadMips;
                stream.maxBufferUploadDeclaredBytes = budget.maxBufferUploadBytes;
                stream.maxTextureUploadPixelBytes = budget.maxTextureUploadPixelBytes;
                const auto result = rhi::OriginalCommandOrderedUploadContract::inspect(inputs,
                    stream,
                    {1u,
                        true,
                        budget.maxBufferUploadBytes,
                        budget.maxPackedTextureUploadBytes,
                        budget.maxTotalUploadBytes},
                    {1u,
                        true,
                        budget.maxTextureUploads,
                        budget.maxNativeCopyCommands,
                        budget.maxTextureWriteTexelBytes});
                if (result.diagnostic)
                    return result.diagnostic;
                counts = result.required.stream;
                const auto& upload = result.required;
                pending.bufferUploads = upload.staging.bufferUploads;
                pending.textureUploads = upload.textureUploadCommands;
                pending.textureUploadMips = upload.staging.textureMips;
                pending.nativeCopyCommands = upload.nativeCopyCommands;
                pending.bufferUploadBytes = upload.staging.bufferBytes;
                pending.packedTextureUploadBytes = upload.staging.packedTextureBytes;
                pending.totalUploadBytes = upload.staging.totalBytes;
                pending.textureUploadPixelBytes = counts.textureUploadPixelBytes;
                pending.textureWriteTexelBytes = upload.textureWriteTexelBytes;
            } else {
                if (budget.generalOutput.contractVersion) {
                    if (const auto* id = validateGeneralOutputProfile(
                            root, budget.generalOutput.contractVersion))
                        return id;
                }
                stream.maxTextureReadbackMips = budget.readback.maxNativeCopyCommands;
                stream.maxReadbackDeclaredBytes = budget.readback.maxDeclaredReadbackBytes;
                const auto result = rhi::OriginalCommandOrderedReadbackContract::inspect(
                    inputs, stream, budget.readback);
                if (result.diagnostic)
                    return result.diagnostic;
                pending.readback = result.required;
                const auto program = rhi::OriginalCommandStreamProgram::inspect(inputs, stream);
                if (program.diagnostic)
                    return program.diagnostic;
                counts = program.counts;
            }
            if (counts.commands > UINT32_MAX || counts.renderPasses > UINT32_MAX
                || pending.readback.nativeCopyCommands > UINT32_MAX)
                return "RHI-WEBGPU-CANONICAL-COLD-COUNT-RANGE";
            pending.contractVersion = budget.contractVersion;
            pending.originalCommands = static_cast<std::uint32_t>(counts.commands);
            pending.renderPasses = static_cast<std::uint32_t>(counts.renderPasses);
            if (budget.contractVersion == 5u) {
                if (const auto* id = rhi::OriginalCommandReadbackMapContract::deriveRequirements(
                        pending.readback,
                        source,
                        pending.capacity.callbackTickets,
                        pending.capacity.callbackRegistrations,
                        budget.readbackMap,
                        pending.readbackMap))
                    return id;
                if (pending.readbackMap.totalCallbackTickets > UINT32_MAX
                    || pending.readbackMap.totalCallbackRegistrations > UINT32_MAX)
                    return "RHI-WEBGPU-CANONICAL-COLD-MAP-COUNT-RANGE";
                pending.capacity.callbackTickets =
                    static_cast<std::uint32_t>(pending.readbackMap.totalCallbackTickets);
                pending.capacity.callbackRegistrations =
                    static_cast<std::uint32_t>(pending.readbackMap.totalCallbackRegistrations);
            }
        }
        if (const auto* id = Contract::validateRequirements(pending, budget))
            return id;
        required = pending;
        return nullptr;
    }

private:
    static const char* validateGeneralOutputProfile(
        const std::shared_ptr<const Root>& root, std::uint32_t outputVersion) noexcept
    {
        using Join = rhi::OriginalCommandReadbackJoinContract;
        const auto& graph = root->originalGraph();
        const auto& inputs = graph->originalInputs();
        const auto& usage = *inputs->originalScalars()->originalUsage();
        const auto& sources = usage.originalSources();
        if (const auto* id = sources->retainedReadbackCorrespondence().validate())
            return id;
        for (std::size_t index = 0u; index < usage.commands().size(); ++index) {
            const auto& command = usage.commands()[index];
            if (command.kind != rhi::CommandUsageKind::TextureReadback)
                continue;
            const auto* source =
                std::get_if<std::shared_ptr<const rhi::OriginalCommandTextureReadbackSource>>(
                    &sources
                        ->sources()[usage.references()[command.firstReference + 1u].sourceIndex]);
            if (!source || !*source
                || !Join::generalOutputProfileVersionsMatch(outputVersion,
                    graph->stateContractVersion(),
                    inputs->originalBudget().contractVersion,
                    root->originalContentPlanContractVersion(),
                    (*source)->contractVersion(),
                    sources->retainedReadbackCorrespondence().contractVersion))
                return "RHI-ORIGINAL-READBACK-JOIN-GENERAL-OUTPUT-PROFILE";
            if (const auto* id = Join::validateGeneralOutputSource(*source, outputVersion))
                return id;
            if (const auto* id =
                    Join::validateGeneralOutputCommand(*inputs, index, **source, outputVersion))
                return id;
        }
        return nullptr;
    }

    static const char* inspectOriginalLanguage(const std::shared_ptr<const Root>& root) noexcept
    {
        if (const auto* id = Root::validateOriginal(root))
            return id;
        if (const auto* id =
                Contract::validateLegacyProfiles(root->originalGraph()->stateContractVersion()))
            return id;
        if (!root->hasOriginalPrototypePlan())
            return "RHI-WEBGPU-CANONICAL-COLD-PLAN-STATUS";
        const auto& usage =
            *root->originalGraph()->originalInputs()->originalScalars()->originalUsage();
        if (usage.commands().size() != language.size())
            return "RHI-WEBGPU-CANONICAL-COLD-LANGUAGE";
        for (std::size_t i = 0u; i < language.size(); ++i)
            if (usage.commands()[i].kind != language[i])
                return "RHI-WEBGPU-CANONICAL-COLD-LANGUAGE";
        return nullptr;
    }

    static std::shared_ptr<const WebGpuCanonicalColdAdmission> prepareOriginalImpl(
        std::shared_ptr<const Closure> closure,
        std::shared_ptr<const Reservation> reservation,
        const Budget& budget,
        const char*& diagnostic,
        std::shared_ptr<const WebGpuCanonicalUploadJoins> joins = {},
        std::shared_ptr<const WebGpuCanonicalReadbackJoins> readbacks = {})
    {
        std::uint32_t owners = 0u;
        diagnostic = inspect(closure, reservation, budget, joins, readbacks, owners);
        if (diagnostic)
            return {};
        Requirements required;
        diagnostic = deriveRequirements(closure->originalRoot(), budget, required);
        if (diagnostic)
            return {};
        try {
            core::CpuMemoryQuotaScope scope(closure->originalRoot()->originalMetadataQuota());
            auto result =
                shareOriginal(core::makeUnique<WebGpuCanonicalColdAdmission>(ConstructionKey{},
                    closure,
                    reservation,
                    budget,
                    owners,
                    required,
                    joins,
                    readbacks));
            if ((diagnostic = result->validate()))
                return {};
            return result;
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-WEBGPU-CANONICAL-COLD-ALLOCATION";
            return {};
        } catch (const std::length_error&) {
            diagnostic = "RHI-WEBGPU-CANONICAL-COLD-ALLOCATION";
            return {};
        }
    }

public:
    static std::shared_ptr<const WebGpuCanonicalColdAdmission> prepareOriginal(
        std::shared_ptr<const Closure> closure,
        std::shared_ptr<const Reservation> reservation,
        const Budget& budget,
        const char*& diagnostic,
        std::shared_ptr<const WebGpuCanonicalUploadJoins> joins = {},
        std::shared_ptr<const WebGpuCanonicalReadbackJoins> readbacks = {})
    {
        if (budget.generalOutput.contractVersion || budget.generalOutput.prototypeOptIn) {
            diagnostic = "RHI-WEBGPU-CANONICAL-COLD-GENERAL-OUTPUT-FACTORY";
            return {};
        }
        return prepareOriginalImpl(std::move(closure),
            std::move(reservation),
            budget,
            diagnostic,
            std::move(joins),
            std::move(readbacks));
    }

    static std::shared_ptr<const WebGpuCanonicalColdAdmission> prepareOriginalGeneralOutput(
        std::shared_ptr<const Closure> closure,
        std::shared_ptr<const Reservation> reservation,
        const Budget& budget,
        const char*& diagnostic,
        std::shared_ptr<const WebGpuCanonicalReadbackJoins> readbacks)
    {
        if ((diagnostic = Contract::validateGeneralOutputIntent(budget)))
            return {};
        return prepareOriginalImpl(std::move(closure),
            std::move(reservation),
            budget,
            diagnostic,
            {},
            std::move(readbacks));
    }

    static const char* validateOriginal(
        const std::shared_ptr<const WebGpuCanonicalColdAdmission>& self) noexcept
    {
        return !self || !self->hasQuotaBoundSharedOwner(self) ? "RHI-WEBGPU-CANONICAL-COLD-OWNER"
                                                              : self->validate();
    }

    const char* validate() const noexcept
    {
        std::uint32_t owners = 0u;
        if (const auto* id = inspect(closure_, reservation_, budget_, joins_, readbacks_, owners))
            return id;
        return owners == owners_
                && core::SharedOwnerProjection::sameOwner(
                    originalMetadataQuota(), closure_->originalRoot()->originalMetadataQuota())
            ? nullptr
            : "RHI-WEBGPU-CANONICAL-COLD-OWNER-QUOTA";
    }

    const auto& originalClosure() const noexcept { return closure_; }

    const auto& originalReservation() const noexcept { return reservation_; }

    const auto& originalRoot() const noexcept { return closure_->originalRoot(); }

    const auto& originalUploadJoins() const noexcept { return joins_; }

    const auto& originalReadbackJoins() const noexcept { return readbacks_; }

    const auto& originalBudget() const noexcept { return budget_; }

    const auto& originalSubmissionQuota() const noexcept
    {
        return reservation_->originalSeal()->originalSubmissionQuota();
    }

    const auto& originalCallbackQuota() const noexcept
    {
        return reservation_->originalSeal()->originalCallbackQuota();
    }

    std::uint32_t retainedOwnerCount() const noexcept { return owners_; }

    Requirements requirements() const noexcept { return requirements_; }

    bool nativeExecutionProven() const noexcept { return false; }

    const char* executionDiagnostic() const noexcept
    {
        if (const auto* id = validate())
            return id;
        if (budget_.generalOutput.contractVersion)
            return rhi::semantics::OriginalCommandNativeColdLowering::generalOutputLowering(
                rhi::Backend::WebGPU, budget_.generalOutput.contractVersion)
                .diagnostic;
        return (budget_.contractVersion == 4u || budget_.contractVersion == 5u)
            ? arp::rhi::semantics::OriginalCommandOrderedReadbackLowering::lowering(
                  rhi::Backend::WebGPU)
                  .diagnostic
            : budget_.contractVersion == 3u
            ? arp::rhi::semantics::OriginalCommandNativeColdLowering::uploadLowering(
                  rhi::Backend::WebGPU)
                  .diagnostic
            : arp::rhi::semantics::OriginalCommandNativeColdLowering::lowering(rhi::Backend::WebGPU)
                  .diagnostic;
    }

private:
    const char* validateRetainedOwnership(
        const std::shared_ptr<const rhi::TextureReadbackRequestDeclaration>* request = nullptr,
        const WebGpuNativeBufferMapReceipt* receipt = nullptr) const noexcept
    {
        if (bool(request) != bool(receipt))
            return "RHI-WEBGPU-CANONICAL-COLD-MAP-BINDING";
        std::uint32_t owners = 0u;
        if (const auto* id = inspect(closure_,
                reservation_,
                budget_,
                joins_,
                readbacks_,
                owners,
                false,
                request,
                receipt))
            return id;
        return owners == owners_
                && core::SharedOwnerProjection::sameOwner(
                    originalMetadataQuota(), closure_->originalRoot()->originalMetadataQuota())
            ? nullptr
            : "RHI-WEBGPU-CANONICAL-COLD-OWNER-QUOTA";
    }

    static const char* inspect(const std::shared_ptr<const Closure>& closure,
        const std::shared_ptr<const Reservation>& reservation,
        const Budget& budget,
        const std::shared_ptr<const WebGpuCanonicalUploadJoins>& joins,
        const std::shared_ptr<const WebGpuCanonicalReadbackJoins>& readbacks,
        std::uint32_t& owners,
        bool cold = true,
        const std::shared_ptr<const rhi::TextureReadbackRequestDeclaration>* request = nullptr,
        const WebGpuNativeBufferMapReceipt* receipt = nullptr) noexcept
    {
        if (const auto* id = Contract::validateIntent(budget))
            return id;
        if (budget.generalOutput.contractVersion) {
            const auto decision =
                rhi::semantics::OriginalCommandNativeColdLowering::generalOutputLowering(
                    rhi::Backend::WebGPU, budget.generalOutput.contractVersion);
            if (!decision.coldJoinAvailable)
                return decision.diagnostic;
            if (!cold
                && !rhi::semantics::OriginalGeneralOutputExecutionLowering::lowering(
                    rhi::Backend::WebGPU, budget.generalOutput.contractVersion)
                    .profilePairingAvailable)
                return "RHI-GENERAL-OUTPUT-EXECUTION-PROFILE-UNAVAILABLE";
        }
        if (const auto* id = Closure::validateOriginal(closure))
            return id;
        if (closure->hasAttachmentReadbackProfile() != (budget.generalOutput.contractVersion == 3u))
            return "RHI-WEBGPU-CANONICAL-COLD-ATTACHMENT-READBACK-PROFILE";
        if (const auto* id = Contract::validateLegacyProfiles(
                closure->originalRoot()->originalGraph()->stateContractVersion(),
                closure->closureContractVersion()))
            return id;
        if (const auto* id = closure->nativeLossDiagnostic())
            return id;
        if (cold) {
            if (const auto* id = Reservation::validateOriginal(reservation))
                return id;
        } else {
            if (!reservation || !reservation->hasQuotaBoundSharedOwner(reservation))
                return "RHI-ORIGINAL-LIFECYCLE-CAPACITY-OWNER";
            if (const auto* id = reservation->validateRetainedOwnership())
                return id;
        }
        const auto& root = closure->originalRoot();
        Requirements required;
        if (const auto* id = deriveRequirements(root, budget, required))
            return id;
        if (budget.contractVersion == 3u) {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
            if (cold) {
                if (const auto* id = WebGpuCanonicalUploadJoins::validateOriginal(joins))
                    return id;
            } else {
                if (!joins || !joins->hasQuotaBoundSharedOwner(joins))
                    return "RHI-WEBGPU-UPLOAD-JOIN-OWNER";
                if (const auto* id = joins->validateRetained(*reservation->originalSeal()))
                    return id;
            }
            if (!core::SharedOwnerProjection::sameOwner(joins->originalRoot(), root))
                return "RHI-WEBGPU-CANONICAL-COLD-UPLOAD-ROOT";
            if (joins->uploadCount() != required.bufferUploads + required.textureUploads)
                return "RHI-WEBGPU-CANONICAL-COLD-UPLOAD-COVERAGE";
            if (joins->overlapsCpuOwner(reservation->originalSeal()->originalSubmissionQuota())
                || joins->overlapsCpuOwner(reservation->originalSeal()->originalCallbackQuota()))
                return "RHI-WEBGPU-CANONICAL-COLD-UPLOAD-QUOTA-DOMAIN";
#else
            return "RHI-WEBGPU-CANONICAL-COLD-UPLOAD-NATIVE-UNAVAILABLE";
#endif
        } else if (joins)
            return "RHI-WEBGPU-CANONICAL-COLD-UPLOAD-PROFILE";
        if ((budget.contractVersion == 4u || budget.contractVersion == 5u)) {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
            if (!readbacks || !readbacks->hasQuotaBoundSharedOwner(readbacks))
                return "RHI-WEBGPU-READBACK-JOIN-OWNER";
            if (budget.generalOutput.contractVersion ? readbacks->joinContractVersion() != 4u
                                                     : readbacks->joinContractVersion() != 1u)
                return "RHI-WEBGPU-CANONICAL-COLD-READBACK-JOIN-PROFILE";
            if (const auto* id = rhi::OriginalGeneralOutputPairingContract::validatePair(
                    budget.generalOutput, readbacks->originalGeneralOutputIntent()))
                return id;
            if (const auto* id = cold ? readbacks->validate()
                    : request         ? readbacks->validateRetainedMapping(
                                    reservation->originalSeal(), *request, *receipt)
                              : readbacks->validateRetained(reservation->originalSeal()))
                return id;
            if (!core::SharedOwnerProjection::sameOwner(readbacks->originalRoot(), root))
                return "RHI-WEBGPU-CANONICAL-COLD-READBACK-ROOT";
            if (readbacks->readbackCount() != required.readback.readbackCommands
                || readbacks->projectedCopyCount() != required.readback.nativeCopyCommands)
                return "RHI-WEBGPU-CANONICAL-COLD-READBACK-COVERAGE";
            if (readbacks->overlapsCpuOwner(reservation->originalSeal()->originalSubmissionQuota())
                || readbacks->overlapsCpuOwner(
                    reservation->originalSeal()->originalCallbackQuota()))
                return "RHI-WEBGPU-CANONICAL-COLD-READBACK-QUOTA-DOMAIN";
#else
            return "RHI-WEBGPU-CANONICAL-COLD-READBACK-NATIVE-UNAVAILABLE";
#endif
        } else if (readbacks)
            return "RHI-WEBGPU-CANONICAL-COLD-READBACK-PROFILE";
        if (cold) {
            if (const auto* id = reservation->originalSeal()->compareOriginalRoot(root))
                return id;
            if (root->status() != Root::Status::Prepared || !root->journalEmpty())
                return "RHI-WEBGPU-CANONICAL-COLD-PLAN-STATUS";
        } else {
            if (!core::SharedOwnerProjection::sameOwner(
                    reservation->originalSeal()->originalRoot(), root))
                return "RHI-ORIGINAL-RECORDING-SEAL-ROOT";
        }
        if (const auto* id = Contract::compareCapacity(required, reservation->originalRequest()))
            return id;
        if (budget.maxCpuMetadataBytes != root->originalMetadataQuota()->limitBytes())
            return "RHI-WEBGPU-CANONICAL-COLD-QUOTA";
        // Both inventories include the same root. Domain/subscription/native
        // allocation owners are already deduplicated by the exact closure.
        const auto additional = reservation->retainedOwnerCount() - root->retainedOwnerCount() + 1u;
        if (closure->retainedOwnerCount() > UINT32_MAX - additional)
            return "RHI-WEBGPU-CANONICAL-COLD-OWNER-CAPACITY";
        owners = closure->retainedOwnerCount() + additional;
        if (joins) {
            const auto uploadOwners = joins->retainedOwnerCount() - root->retainedOwnerCount();
            if (uploadOwners > UINT32_MAX - owners)
                return "RHI-WEBGPU-CANONICAL-COLD-OWNER-CAPACITY";
            owners += uploadOwners;
        }
        if (readbacks) {
            const auto added = readbacks->retainedOwnerCount() - root->retainedOwnerCount();
            if (added > UINT32_MAX - owners)
                return "RHI-WEBGPU-CANONICAL-COLD-OWNER-CAPACITY";
            owners += added;
        }
        const auto& lifecycle = reservation->originalSeal()->originalBudget();
        if (owners > budget.maxRetainedOwners || owners > lifecycle.maxRecordingOwners
            || owners > lifecycle.maxSubmissionOwners || owners > lifecycle.maxCallbackOwners)
            return "RHI-WEBGPU-CANONICAL-COLD-OWNER-CAPACITY";
        return nullptr;
    }

    const std::shared_ptr<const Closure> closure_;
    const std::shared_ptr<const Reservation> reservation_;
    const Budget budget_;
    const std::uint32_t owners_;
    const Requirements requirements_;
    const std::shared_ptr<const WebGpuCanonicalUploadJoins> joins_;
    const std::shared_ptr<const WebGpuCanonicalReadbackJoins> readbacks_;
};
} // namespace arp::rhi_webgpu

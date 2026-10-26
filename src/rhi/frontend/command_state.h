#pragma once

#include "rhi/frontend/bindless_graphics_depth_content.h"
#include "rhi/frontend/command_attachment_validation.h"
#include "rhi/frontend/command_model.h"
#include "rhi/frontend/command_recording_order.h"
#include "rhi/frontend/command_texture_ranges.h"
#include "rhi/frontend/command_usage.h"
#include "rhi/frontend/finite_graphics_content_validation.h"
#include "rhi/frontend/original_submitted_buffer_read_pins.h"
#include "rhi/frontend/resource_access_usage.h"
#include "rhi/frontend/resource_state_sequence.h"
#include "rhi/frontend/transfer_readback_layout_validation.h"
#include "rhi/frontend/transfer_upload_content_validation.h"
#include "rhi/frontend/transfer_upload_layout_validation.h"
#include "rhi/semantics/bindless_push_consumption_lowering.h"
#include "rhi/semantics/command_state_lowering.h"

#include <cmath>

namespace arp::rhi_vulkan {
class VulkanRhiNativeCommandPayload;
}

namespace arp::rhi_metal {
class MetalOriginalOrdinaryCommand;
class MetalRhiNativeCommandPayload;
} // namespace arp::rhi_metal

// region Command resource states
// region original_command_buffer_states
namespace arp::rhi {
// Committed Buffer input graph /1, logical evidence only. Texture and pending
// transaction admission remain separate prerequisites; full execution is closed.
class OriginalCommandBufferStates final : public core::QuotaBoundSharedObjectIdentity {
    class ConstructionKey {
        friend class OriginalCommandBufferStates;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    struct Entry {
        std::size_t sourceIndex;
        GpuBuffer::CommittedStateObservation observation;
    };

    OriginalCommandBufferStates(ConstructionKey,
        std::shared_ptr<const OriginalCommandInputs> inputs,
        OriginalDeviceLifetime::Ticket device,
        const OriginalCommandStateAdmissionBudget& budget,
        std::size_t count,
        std::uint32_t owners)
        : inputs_(std::move(inputs)), device_(std::move(device)), budget_(budget), entries_(count),
          ownerCount_(owners)
    {}

    static std::shared_ptr<const OriginalCommandBufferStates> prepareOriginal(
        std::shared_ptr<const OriginalCommandInputs> inputs,
        const GpuDevice& device,
        const OriginalCommandStateAdmissionBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        std::uint32_t count = 0u, owners = 0u;
        diagnostic = validateSources(inputs, budget, quota, count, owners);
        if (diagnostic)
            return {};
        const auto& sources = inputs->originalScalars()->originalUsage()->originalSources();
        if ((diagnostic =
                    OriginalCommandResourceSources::validateRegisteredLifetime(sources, device)))
            return {};
        if (sources->containsTypedSources()) {
            diagnostic = sources->typedStateJoinDiagnostic();
            return {};
        }
        try {
            core::CpuMemoryQuotaScope scope(quota);
            auto result = core::makeUnique<OriginalCommandBufferStates>(ConstructionKey{},
                std::move(inputs),
                device.originalDeviceLifetimeTicket(),
                budget,
                count,
                owners);
            for (std::size_t i = 0u; i < sources->sources().size(); ++i) {
                if (const auto* lease =
                        std::get_if<std::shared_ptr<const OriginalCommandBufferLease>>(
                            &sources->sources()[i])) {
                    auto observation = (*lease)->originalObject()->observeOriginalCommittedState();
                    if (!observation) {
                        diagnostic = (*lease)->originalObject()->originalCommittedStateDiagnostic();
                        return {};
                    }
                    result->entries_.append({i, *observation});
                }
            }
            return shareOriginal(std::move(result));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-ORIGINAL-BUFFER-STATES-ALLOCATION";
            return {};
        } catch (const std::length_error&) {
            diagnostic = "RHI-ORIGINAL-BUFFER-STATES-ALLOCATION";
            return {};
        }
    }

    static const char* validateOriginal(
        const std::shared_ptr<const OriginalCommandBufferStates>& self) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-ORIGINAL-BUFFER-STATES-OWNER";
        return self->validate();
    }

    const char* validate() const noexcept
    {
        if (const auto* id = device_.diagnostic())
            return id;
        std::uint32_t count = 0u, owners = 0u;
        if (const auto* id =
                validateSources(inputs_, budget_, originalMetadataQuota(), count, owners))
            return id;
        if (count != entries_.values().size() || owners != ownerCount_)
            return "RHI-ORIGINAL-BUFFER-STATES-COVERAGE";
        const auto& sources =
            inputs_->originalScalars()->originalUsage()->originalSources()->sources();
        for (const auto& entry : entries_.values()) {
            if (entry.sourceIndex >= sources.size())
                return "RHI-ORIGINAL-BUFFER-STATES-COVERAGE";
            const auto* lease = std::get_if<std::shared_ptr<const OriginalCommandBufferLease>>(
                &sources[entry.sourceIndex]);
            if (!lease)
                return "RHI-ORIGINAL-BUFFER-STATES-COVERAGE";
            if (const auto* id =
                    (*lease)->originalObject()->validateOriginalCommittedState(entry.observation))
                return id;
        }
        return nullptr;
    }

    const char* compareOriginalInputs(const std::shared_ptr<const OriginalCommandInputs>& inputs,
        const GpuDevice& device) const noexcept
    {
        if (const auto* id = validate())
            return id;
        if (!core::SharedOwnerProjection::sameOwner(inputs_, inputs))
            return "RHI-ORIGINAL-BUFFER-STATES-INPUTS";
        return device.validateOriginalDeviceLifetime(device_);
    }

    const auto& originalInputs() const noexcept { return inputs_; }

    const auto& entries() const noexcept { return entries_.values(); }

    std::uint32_t retainedOwnerCount() const noexcept { return ownerCount_; }

    bool nativeExecutionProven() const noexcept { return false; }

    const char* executionDiagnostic(Backend backend) const noexcept
    {
        if (const auto* id = validate())
            return id;
        return arp::rhi::semantics::OriginalCommandStateAdmissionLowering::lowering(backend)
            .diagnostic;
    }

private:
    static const char* validateSources(const std::shared_ptr<const OriginalCommandInputs>& inputs,
        const OriginalCommandStateAdmissionBudget& budget,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        std::uint32_t& count,
        std::uint32_t& owners) noexcept
    {
        if (budget.contractVersion != 1u)
            return "RHI-ORIGINAL-STATE-VERSION";
        if (const auto* id = OriginalCommandStateAdmissionContract::validateIntent(budget))
            return id;
        if (const auto* id = OriginalCommandInputs::validateOriginal(inputs))
            return id;
        const auto& sources = inputs->originalScalars()->originalUsage()->originalSources();
        if (sources->containsTypedSources())
            return sources->typedStateJoinDiagnostic();
        if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes)
            return "RHI-ORIGINAL-BUFFER-STATES-QUOTA";
        if (core::SharedOwnerProjection::sameControlBlock(quota, inputs->originalMetadataQuota())
            || core::SharedOwnerProjection::sameControlBlock(
                quota, sources->originalMetadataQuota()))
            return "RHI-ORIGINAL-BUFFER-STATES-QUOTA-DOMAIN";
        count = 0u;
        for (const auto& source : sources->sources()) {
            if (const auto* lease =
                    std::get_if<std::shared_ptr<const OriginalCommandBufferLease>>(&source)) {
                if (const auto* id = (*lease)->originalObject()->originalCommittedStateDiagnostic())
                    return id;
                if (count == budget.maxStateObservations
                    || count == sources->originalBudget().maxStateObservations)
                    return "RHI-ORIGINAL-BUFFER-STATES-OBSERVATION-CAPACITY";
                ++count;
            } else if (!std::holds_alternative<
                           std::shared_ptr<const OriginalCommandBufferUploadSource>>(source))
                return "RHI-ORIGINAL-BUFFER-STATES-SOURCE-UNSUPPORTED";
        }
        if (!count)
            return "RHI-ORIGINAL-BUFFER-STATES-SOURCE-UNSUPPORTED";
        if (inputs->retainedOwnerCount() == UINT32_MAX)
            return "RHI-ORIGINAL-STATE-OWNER-CAPACITY";
        std::uint32_t registrations = 0u;
        return OriginalCommandResourceSources::registeredOwnerUnion(sources,
            inputs->retainedOwnerCount() + 1u,
            budget.maxRetainedOwners,
            budget.maxResourceRegistrations,
            owners,
            registrations);
    }

    const std::shared_ptr<const OriginalCommandInputs> inputs_;
    const OriginalDeviceLifetime::Ticket device_;
    const OriginalCommandStateAdmissionBudget budget_;
    core::BoundedSequence<Entry> entries_;
    const std::uint32_t ownerCount_;
};

// endregion original_command_buffer_states

// region original_command_resource_states

namespace common {
class OriginalCommandStateSequence;
class OriginalTypedUsageProjection;
} // namespace common

// Committed input evidence only; explicitly opted-in CPU transactions use the
// existing Plan/journal. Native execution/completion remain separate and closed.
class OriginalCommandResourceStates final : public core::QuotaBoundSharedObjectIdentity {
    friend class OriginalCommandStateTransaction;
    friend class common::OriginalCommandStateSequence;
    friend class common::OriginalTypedUsageProjection;
    friend class OriginalCommandStreamProgram;

    class ConstructionKey {
        friend class OriginalCommandResourceStates;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

    using Projection = common::OriginalCommandTextureRanges;
    using Consumption = common::OriginalCommandDescriptorConsumption;
    using TypedDecision = arp::rhi::semantics::OriginalCommandStateAdmissionLowering::TypedDecision;

public:
    using ReadPins = OriginalSubmittedBufferReadPins;

    struct SubmittedReadAdmission {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        OriginalCommandStateSequenceBudget sequence;
        OriginalRenderDescriptorUsageBudget allBindings;
    };

    // Immutable scalar queries preserve the distinct issued observation arm.
    class BufferObservation final {
    public:
        BufferObservation(const GpuBuffer::CommittedStateObservation& value) : value_(value) {}

        BufferObservation(const ReadPins::Observation& value) : value_(value) {}

        ResourceState state() const noexcept
        {
            return std::visit(
                [](const auto& value) {
                    return value.state();
                },
                value_);
        }

        std::uint64_t revision() const noexcept
        {
            return std::visit(
                [](const auto& value) {
                    return value.revision();
                },
                value_);
        }

        std::uint64_t resourceGeneration() const noexcept
        {
            return std::visit(
                [](const auto& value) {
                    return value.resourceGeneration();
                },
                value_);
        }

        const auto* committed() const noexcept
        {
            return std::get_if<GpuBuffer::CommittedStateObservation>(&value_);
        }

        const auto* submitted() const noexcept
        {
            return std::get_if<ReadPins::Observation>(&value_);
        }

        bool nativeExecutionProven() const noexcept { return false; }

    private:
        std::variant<GpuBuffer::CommittedStateObservation, ReadPins::Observation> value_;
    };

    struct BufferEntry {
        std::size_t sourceIndex;
        BufferObservation observation;
    };

    struct TextureEntry {
        Projection::Point point;
        GpuTexture::CommittedStateObservation observation;
    };

    OriginalCommandResourceStates(ConstructionKey,
        std::shared_ptr<const OriginalCommandInputs> inputs,
        OriginalDeviceLifetime::Ticket device,
        const OriginalCommandStateAdmissionBudget& budget,
        std::uint32_t buffers,
        std::uint32_t ranges,
        std::uint32_t owners,
        TypedDecision decision)
        : inputs_(std::move(inputs)), device_(std::move(device)), budget_(budget),
          buffers_(buffers), textures_(ranges), ownerCount_(owners), consumptionDecision_(decision)
    {}

    static std::shared_ptr<const OriginalCommandResourceStates> prepareOriginal(
        std::shared_ptr<const OriginalCommandInputs> inputs,
        const GpuDevice& device,
        const OriginalCommandStateAdmissionBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        return prepareOriginalImpl(
            std::move(inputs), device, budget, std::move(quota), diagnostic, nullptr, nullptr);
    }

    static std::shared_ptr<const OriginalCommandResourceStates>
    prepareOriginalSubmittedReadValidator(std::shared_ptr<const OriginalCommandInputs> inputs,
        const GpuDevice& device,
        const OriginalCommandStateAdmissionBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const ReadPins::Reservation& reservation,
        const SubmittedReadAdmission& admission,
        const char*& diagnostic)
    {
        diagnostic = semantics::OriginalSubmissionReadContract::canonicalStateLowering(
            device.backend(), admission.contractVersion);
        if (!diagnostic && !admission.prototypeOptIn)
            diagnostic = "RHI-SUBMISSION-READ-OPT-IN";
        if (!diagnostic)
            diagnostic = inspectReadReservation(reservation, inputs);
        if (!diagnostic)
            diagnostic = reservation.batch_->issuer->diagnostic();
        if (!diagnostic)
            diagnostic = device.validateOriginalDeviceLifetime(reservation.batch_->issuer->device_);
        if (diagnostic)
            return {};
        return prepareOriginalImpl(std::move(inputs),
            device,
            budget,
            std::move(quota),
            diagnostic,
            &reservation,
            &admission);
    }

private:
    static const char* inspectReadReservation(const ReadPins::Reservation& reservation,
        const std::shared_ptr<const OriginalCommandInputs>& inputs) noexcept
    {
        const auto& batch = reservation.batch_;
        if (!batch)
            return "RHI-SUBMISSION-READ-RESERVATION";
        if (const auto* id = batch->issuer->inspectReservation(reservation, true))
            return id;
        return batch->inputs == inputs
                && core::SharedOwnerProjection::sameOwner(batch->inputs, inputs)
            ? nullptr
            : "RHI-SUBMISSION-READ-INPUTS";
    }

    static std::optional<ReadPins::Observation> readObservation(
        const ReadPins::Reservation* reservation,
        const OriginalCommandInputs& inputs,
        std::size_t sourceIndex) noexcept
    {
        if (!reservation)
            return {};
        const auto& selected =
            inputs.originalScalars()->originalUsage()->originalSources()->sources()[sourceIndex];
        const auto* lease =
            std::get_if<std::shared_ptr<const OriginalCommandBufferLease>>(&selected);
        if (!lease)
            return {};
        const auto& batch = reservation->batch_;
        for (std::size_t index = 0u; index < batch->entries.size(); ++index)
            if (batch->entries[index].source == *lease
                && core::SharedOwnerProjection::sameOwner(batch->entries[index].source, *lease))
                return ReadPins::Observation{batch, index};
        return {};
    }

    static std::shared_ptr<const OriginalCommandResourceStates> prepareOriginalImpl(
        std::shared_ptr<const OriginalCommandInputs> inputs,
        const GpuDevice& device,
        const OriginalCommandStateAdmissionBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic,
        const ReadPins::Reservation* reads,
        const SubmittedReadAdmission* admission)
    {
        std::uint32_t buffers = 0u, ranges = 0u, owners = 0u;
        if (!budget.bindlessRoots.empty()) {
            if (!budget.bindlessRoots.directResultCopies.empty()) {
                const auto decision = semantics::OriginalBindlessDirectResultCopyLowering::lowering(
                    device.backend(), budget.bindlessRoots.directResultCopies.contractVersion);
                if (!decision.projectionAvailable || decision.diagnostic) {
                    diagnostic = decision.diagnostic;
                    return {};
                }
            }
            const auto root =
                semantics::OriginalCommandStateAdmissionLowering::bindlessRootLowering(
                    device.backend(), budget.bindlessRoots.contractVersion);
            if (!root.projectionAvailable || root.diagnostic) {
                diagnostic = root.diagnostic;
                return {};
            }
            if (!budget.bindlessRoots.pushConsumption.empty()) {
                const auto push = semantics::OriginalBindlessPushConsumptionLowering::lowering(
                    device.backend(), budget.bindlessRoots.pushConsumption.contractVersion);
                if (!push.projectionAvailable || push.diagnostic) {
                    diagnostic = push.diagnostic;
                    return {};
                }
                const auto& intent = budget.bindlessRoots.pushConsumption;
                if (intent.recordingContractVersion || intent.recordingOptIn) {
                    const auto native =
                        semantics::OriginalBindlessPushConsumptionLowering::recording(
                            device.backend(), intent.recordingContractVersion);
                    if (!native.executionAvailable || native.diagnostic) {
                        diagnostic = native.diagnostic;
                        return {};
                    }
                }
            }
        }
        const auto decision = budget.retainedReadbackStateVersion
            ? arp::rhi::semantics::OriginalCommandStateAdmissionLowering::typedReadbackLowering(
                  device.backend(), budget.retainedReadbackStateVersion, budget.contractVersion)
            : arp::rhi::semantics::OriginalCommandStateAdmissionLowering::typedLowering(
                  device.backend(), budget.contractVersion >= 2u ? budget.contractVersion : 2u);
        if (budget.contractVersion >= 2u) {
            if ((diagnostic = OriginalCommandStateAdmissionContract::validateIntent(budget)))
                return {};
            if (budget.contractVersion >= 3u)
                if ((diagnostic = Consumption::inspectRenderBindings(
                         inputs, device, budget.renderDescriptorUsage)
                            .diagnostic))
                    return {};
            if ((diagnostic = Consumption::inspect(inputs, device, budget.descriptorConsumption)
                        .diagnostic))
                return {};
        }
        diagnostic = preflight(
            inputs, budget, quota, buffers, ranges, owners, true, decision, reads, admission);
        if (diagnostic)
            return {};
        const auto& sources = inputs->originalScalars()->originalUsage()->originalSources();
        if ((diagnostic =
                    OriginalCommandResourceSources::validateRegisteredLifetime(sources, device)))
            return {};
        if (budget.contractVersion == 1u && sources->containsTypedSources()
            && budget.bindlessRoots.empty()) {
            diagnostic = sources->typedStateJoinDiagnostic();
            return {};
        }
        if ((budget.contractVersion >= 2u || !budget.bindlessRoots.empty())
            && device.overlapsOriginalDeviceCpuOwner(quota)) {
            diagnostic = "RHI-ORIGINAL-TYPED-STATE-DEVICE-QUOTA-DOMAIN";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope scope(quota);
            auto result = core::makeUnique<OriginalCommandResourceStates>(ConstructionKey{},
                inputs,
                device.originalDeviceLifetimeTicket(),
                budget,
                buffers,
                ranges,
                owners,
                decision);
            if (reads) {
                result->reads_.emplace(ReadPins::Reservation{reads->batch_});
                result->readAdmission_ = *admission;
            }
            for (std::size_t i = 0u; i < sources->sources().size(); ++i)
                if (const auto* lease =
                        std::get_if<std::shared_ptr<const OriginalCommandBufferLease>>(
                            &sources->sources()[i])) {
                    if (auto issued = readObservation(reads, *inputs, i)) {
                        result->buffers_.append({i, *issued});
                        continue;
                    }
                    const auto observation =
                        (*lease)->originalObject()->observeOriginalCommittedState();
                    if (!observation) {
                        diagnostic = (*lease)->originalObject()->originalCommittedStateDiagnostic();
                        return {};
                    }
                    result->buffers_.append({i, *observation});
                }
            diagnostic = visitTextureObservations(
                inputs, budget, [&](const auto& point, const GpuTexture& texture) -> const char* {
                    const auto observation = texture.observeOriginalCommittedState(point.range);
                    if (!observation)
                        return texture.originalCommittedStateDiagnostic(point.range);
                    result->textures_.append({point, *observation});
                    return nullptr;
                });
            if (diagnostic)
                return {};
            if (reads && (diagnostic = result->validateSubmittedReadAccesses()))
                return {};
            return shareOriginal(std::move(result));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-ORIGINAL-RESOURCE-STATES-ALLOCATION";
            return {};
        } catch (const std::length_error&) {
            diagnostic = "RHI-ORIGINAL-RESOURCE-STATES-ALLOCATION";
            return {};
        }
    }

public:
    static const char* validateOriginal(
        const std::shared_ptr<const OriginalCommandResourceStates>& self) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-ORIGINAL-RESOURCE-STATES-OWNER";
        return self->validate();
    }

    const char* validate() const noexcept { return validateGraph(true); }

private:
    const char* validateGraph(bool committed) const noexcept
    {
        if (const auto* id = device_.diagnostic())
            return id;
        std::uint32_t buffers = 0u, ranges = 0u, owners = 0u;
        if (const auto* id = preflight(inputs_,
                budget_,
                originalMetadataQuota(),
                buffers,
                ranges,
                owners,
                committed,
                consumptionDecision_,
                reads_ ? &*reads_ : nullptr,
                readAdmission_ ? &*readAdmission_ : nullptr))
            return id;
        if (buffers != buffers_.values().size() || ranges != textures_.values().size()
            || owners != ownerCount_)
            return "RHI-ORIGINAL-RESOURCE-STATES-COVERAGE";
        const auto& sources =
            inputs_->originalScalars()->originalUsage()->originalSources()->sources();
        std::size_t bufferIndex = 0u;
        for (std::size_t i = 0u; i < sources.size(); ++i)
            if (const auto* lease =
                    std::get_if<std::shared_ptr<const OriginalCommandBufferLease>>(&sources[i])) {
                const auto& entry = buffers_.values()[bufferIndex++];
                if (entry.sourceIndex != i)
                    return "RHI-ORIGINAL-RESOURCE-STATES-COVERAGE";
                if (const auto* issued = entry.observation.submitted()) {
                    const auto expected = readObservation(reads_ ? &*reads_ : nullptr, *inputs_, i);
                    if (!expected || issued->batch_.lock() != reads_->batch_
                        || issued->index_ != expected->index_)
                        return "RHI-SUBMISSION-READ-SOURCE-INDEX";
                    if (const auto* id = issued->diagnostic())
                        return id;
                } else if (committed)
                    if (const auto* id = (*lease)->originalObject()->validateOriginalCommittedState(
                            *entry.observation.committed()))
                        return id;
            }
        std::size_t rangeIndex = 0u;
        const auto* textureDiagnostic = visitTextureObservations(
            inputs_, budget_, [&](const auto& point, const GpuTexture& texture) -> const char* {
                const auto& entry = textures_.values()[rangeIndex++];
                if (!entry.point.matches(point) || entry.observation.range() != point.range)
                    return "RHI-ORIGINAL-RESOURCE-STATES-COVERAGE";
                return committed ? texture.validateOriginalCommittedState(entry.observation)
                                 : nullptr;
            });
        return textureDiagnostic ? textureDiagnostic
            : reads_             ? validateSubmittedReadAccesses()
                                 : nullptr;
    }

public:
    const char* compareOriginalInputs(const std::shared_ptr<const OriginalCommandInputs>& inputs,
        const GpuDevice& device) const noexcept
    {
        if (const auto* id = validate())
            return id;
        if (!core::SharedOwnerProjection::sameOwner(inputs_, inputs))
            return "RHI-ORIGINAL-RESOURCE-STATES-INPUTS";
        return device.validateOriginalDeviceLifetime(device_);
    }

    const auto& originalInputs() const noexcept { return inputs_; }

    const auto& bufferEntries() const noexcept { return buffers_.values(); }

    const auto& textureEntries() const noexcept { return textures_.values(); }

    std::uint32_t stateContractVersion() const noexcept { return budget_.contractVersion; }

    std::uint32_t retainedOwnerCount() const noexcept { return ownerCount_; }

    bool nativeExecutionProven() const noexcept { return false; }

    const char* executionDiagnostic(Backend backend) const noexcept
    {
        if (const auto* id = validate())
            return id;
        if (reads_)
            return semantics::OriginalSubmissionReadContract::canonicalStateLowering(
                backend, readAdmission_->contractVersion);
        return arp::rhi::semantics::OriginalCommandStateAdmissionLowering::lowering(backend)
            .diagnostic;
    }

private:
    // One projection is shared by cold count admission, genuine capture and
    // hot revalidation. Footprint walkers retain their original table/pipeline
    // joins; no additional retained access graph or native queries are needed.
    const char* validateSubmittedReadAccesses() const noexcept;

    std::uint64_t retainedStateRootCount() const noexcept
    {
        return reads_ ? 3u + reads_->batch_->links : 1u;
    }

    static bool overlapsSubmittedReadQuota(const ReadPins::Reservation* reads,
        const std::shared_ptr<core::CpuMemoryQuota>& quota) noexcept
    {
        if (!reads)
            return false;
        const auto& issuer = reads->batch_->issuer;
        const auto completion = issuer->completion_.lock();
        return core::SharedOwnerProjection::overlapsOwner(quota, issuer->originalMetadataQuota())
            || (completion
                && core::SharedOwnerProjection::overlapsOwner(
                    quota, completion->originalMetadataQuota()));
    }

    bool overlapsSubmittedReadQuota(
        const std::shared_ptr<core::CpuMemoryQuota>& quota) const noexcept
    {
        return overlapsSubmittedReadQuota(reads_ ? &*reads_ : nullptr, quota);
    }

    const ReadPins::Observation* submittedReadAt(std::size_t sourceIndex) const noexcept
    {
        for (const auto& entry : buffers_.values())
            if (entry.sourceIndex == sourceIndex)
                return entry.observation.submitted();
        return nullptr;
    }

    const char* validateSubmittedShaderAccess(const Consumption::Access& access) const noexcept
    {
        if (access.kind != Consumption::Access::Kind::Buffer)
            return nullptr;
        const auto* issued = submittedReadAt(access.bufferSourceIndex);
        if (!issued)
            return nullptr;
        return issued->validateReadScope(
            {access.state, access.stages, access.access}, access.offsetBytes, access.sizeBytes);
    }

    template <class Visitor>
    static const char* visitTextureObservations(
        const std::shared_ptr<const OriginalCommandInputs>& inputs,
        const OriginalCommandStateAdmissionBudget& budget,
        Visitor&& visitor) noexcept
    {
        if (const auto* id = Projection::visit(inputs, visitor))
            return id;
        if (!budget.bindlessRoots.empty()) {
            const auto result = common::OriginalCommandBindlessRootAccesses::visitValidatedOriginal(
                inputs, budget.bindlessRoots, [&](const auto& access) noexcept -> const char* {
                    if (access.declaration.kind != BindlessAccessKind::Texture)
                        return nullptr;
                    const auto& sources =
                        inputs->originalScalars()->originalUsage()->originalSources()->sources();
                    const auto* texture = Projection::originalTexture(sources[access.sourceIndex]);
                    if (!texture)
                        return "RHI-ORIGINAL-BINDLESS-STATE-CANONICAL-RESOURCE";
                    return visitor(Projection::Point{access.commandIndex,
                                       SIZE_MAX,
                                       access.sourceIndex,
                                       access.declaration.textureRange,
                                       Projection::Point::Origin::BindlessRoot,
                                       access.viewSourceIndex,
                                       access.rootSourceIndex,
                                       0u,
                                       access.declaration.state,
                                       access.declaration.pipelineStages,
                                       access.declaration.access},
                        *texture);
                });
            if (result.diagnostic)
                return result.diagnostic;
        }
        if (budget.contractVersion < 3u)
            return nullptr;
        const auto& sources =
            inputs->originalScalars()->originalUsage()->originalSources()->sources();
        const auto project = [&](const Consumption::Access& access,
                                 Projection::Point::Origin origin) noexcept -> const char* {
            if (access.kind != Consumption::Access::Kind::TextureView)
                return nullptr; // Buffer controls are already captured once from Source.
            const auto* texture =
                Projection::originalTexture(sources[access.textureParentSourceIndex]);
            const auto* viewParent =
                Projection::originalTexture(sources[access.textureViewSourceIndex]);
            if (!texture || texture != viewParent)
                return "RHI-ORIGINAL-RESOURCE-STATES-TABLE-PARENT";
            return visitor(Projection::Point{access.commandIndex,
                               SIZE_MAX,
                               access.textureParentSourceIndex,
                               access.textureRange,
                               origin,
                               access.textureViewSourceIndex,
                               access.descriptorSourceIndex,
                               access.tableElementIndex,
                               access.state,
                               access.stages,
                               access.access},
                *texture);
        };
        const auto render = Consumption::renderBindingsOriginal(
            inputs, budget.renderDescriptorUsage, [&](const auto& access) noexcept -> const char* {
                return project(access, Projection::Point::Origin::RenderBinding);
            });
        if (render.diagnostic)
            return render.diagnostic;
        return Consumption::walkOriginal(inputs,
            budget.descriptorConsumption,
            [&](const auto& access) noexcept -> const char* {
                return project(access, Projection::Point::Origin::ShaderConsumption);
            })
            .diagnostic;
    }

    template <class Visitor>
    Consumption::Result visitDescriptorAccesses(Visitor&& visitor) const noexcept
    {
        return visitDescriptorAccesses(
            budget_.descriptorConsumption, std::forward<Visitor>(visitor));
    }

    // Only the original state/stream/projector facades can enumerate after the
    // captured ticket and approved State2 decision. Independent caller ceilings
    // are enforced by the same consumption validators, never another walker.
    template <class Visitor>
    Consumption::Result visitDescriptorAccesses(
        const Consumption::Budget& footprint, Visitor&& visitor) const noexcept
    {
        if (budget_.contractVersion != 2u && budget_.contractVersion != 3u
            && budget_.contractVersion != 4u && budget_.contractVersion != 5u)
            return {};
        if (const auto* id = validateTypedProjectionJoin())
            return {id};
        auto result =
            Consumption::walkOriginal(inputs_, footprint, [](const auto&) noexcept -> const char* {
                return nullptr;
            });
        if (result.diagnostic)
            return result;
        return Consumption::walkOriginal(inputs_, footprint, std::forward<Visitor>(visitor));
    }

    template <class Visitor>
    Consumption::RenderBindingResult visitRenderDescriptorAccesses(
        const Consumption::RenderBindingBudget& footprint, Visitor&& visitor) const noexcept
    {
        if (const auto* id = validateTypedProjectionJoin())
            return {id};
        const auto result = Consumption::renderBindingsOriginal(
            inputs_, footprint, [](const auto&) noexcept -> const char* {
                return nullptr;
            });
        if (result.diagnostic)
            return result;
        return Consumption::renderBindingsOriginal(
            inputs_, footprint, std::forward<Visitor>(visitor));
    }

    // Scoped captured projection: only the existing usage projector can enter
    // after complete original State2/Stream2 validation. No pending bypass or
    // persistent token; immutable ownership is externally serialized.
    template <class Visitor>
    Consumption::Result visitCapturedDescriptorAccesses(
        const Consumption::Budget& footprint, Visitor&& visitor) const noexcept
    {
        if (const auto* id = validateTypedProjectionJoin())
            return {id};
        return Consumption::walkOriginal<true>(inputs_, footprint, std::forward<Visitor>(visitor));
    }

    template <class Visitor>
    Consumption::RenderBindingResult visitCapturedRenderAccesses(
        const Consumption::RenderBindingBudget& footprint, Visitor&& visitor) const noexcept
    {
        if (const auto* id = validateTypedProjectionJoin())
            return {id};
        return Consumption::renderBindingsOriginal<true>(
            inputs_, footprint, std::forward<Visitor>(visitor));
    }

    template <class Visitor>
    OriginalCommandGeometryBindingContract::AccessResult visitGeometryAccesses(
        const OriginalGeometryUsageBudget& footprint, Visitor&& visitor) const noexcept
    {
        if (budget_.contractVersion != 4u && !budget_.bindlessRoots.geometryJoined())
            return {"RHI-ORIGINAL-TYPED-STATE-GEOMETRY-PROFILE"};
        if (!footprint.matches(budget_.geometryUsage))
            return {"RHI-ORIGINAL-TYPED-STATE-GEOMETRY-FOOTPRINT-MISMATCH"};
        if (budget_.bindlessRoots.geometryJoined()) {
            if (const auto* id = device_.diagnostic())
                return {id};
            if (const auto* id = budget_.bindlessRoots.diagnostic())
                return {id};
        } else if (const auto* id = validateTypedProjectionJoin())
            return {id};
        return inputs_->visitGeometryAccesses(footprint, std::forward<Visitor>(visitor));
    }

    const char* validateTypedProjectionJoin() const noexcept
    {
        if (const auto* id = device_.diagnostic())
            return id;
        return (budget_.contractVersion == 2u || budget_.contractVersion == 3u
                   || budget_.contractVersion == 4u || budget_.contractVersion == 5u)
                && consumptionDecision_.contractVersion == budget_.contractVersion
                && consumptionDecision_.stateAvailable && consumptionDecision_.projectionAvailable
                && !consumptionDecision_.diagnostic
            ? nullptr
            : "RHI-ORIGINAL-TYPED-STATE-LOWERING-CLOSED";
    }

    static const char* preflight(const std::shared_ptr<const OriginalCommandInputs>& inputs,
        const OriginalCommandStateAdmissionBudget& budget,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        std::uint32_t& buffers,
        std::uint32_t& ranges,
        std::uint32_t& owners,
        bool committed = true,
        TypedDecision decision = {},
        const ReadPins::Reservation* reads = nullptr,
        const SubmittedReadAdmission* admission = nullptr) noexcept
    {
        if (const auto* id = OriginalCommandStateAdmissionContract::validateIntent(budget))
            return id;
        if (const auto* id = OriginalCommandInputs::validateOriginal(inputs))
            return id;
        const auto& sources = inputs->originalScalars()->originalUsage()->originalSources();
        if (reads) {
            if (const auto* id = inspectReadReservation(*reads, inputs))
                return id;
            if (!admission || !budget.bindlessRoots.empty())
                return "RHI-SUBMISSION-READ-CANONICAL-PROFILE";
            if (overlapsSubmittedReadQuota(reads, quota))
                return "RHI-SUBMISSION-READ-QUOTA";
            if (const auto* id =
                    OriginalCommandStateSequenceContract::validateIntent(admission->sequence))
                return id;
            if (admission->sequence.contractVersion != budget.contractVersion)
                return "RHI-SUBMISSION-READ-SEQUENCE-PROFILE";
            if (budget.contractVersion >= 2u)
                if (const auto* id = OriginalRenderDescriptorUsageContract::validateBudget(
                        admission->allBindings))
                    return id;
        }
        if (budget.contractVersion == 1u && sources->containsTypedSources()
            && budget.bindlessRoots.empty())
            return sources->typedStateJoinDiagnostic();
        if (!budget.bindlessRoots.empty()) {
            // Root Source /2 admits only ordinary descriptors captured at this
            // exact position by its validated closure. Independent typed
            // descriptor/pipeline profiles remain closed.
            for (std::size_t index = 0u; index < sources->sources().size(); ++index) {
                const auto& source = sources->sources()[index];
                const auto domain = OriginalCommandResourceSources::domain(source);
                if (domain != CommandReferenceDomain::Buffer
                    && domain != CommandReferenceDomain::Texture
                    && domain != CommandReferenceDomain::TextureView
                    && domain != CommandReferenceDomain::Sampler
                    && domain != CommandReferenceDomain::BindlessRoot
                    && !sources->isBindlessOrdinaryDescriptor(index))
                    return "RHI-ORIGINAL-BINDLESS-STATE-SOURCE-PROFILE";
            }
            const auto root = common::OriginalCommandBindlessRootAccesses::visitValidatedOriginal(
                inputs, budget.bindlessRoots, [](const auto&) noexcept -> const char* {
                    return nullptr;
                });
            if (root.diagnostic)
                return root.diagnostic;
            if (!root.roots || !root.accesses)
                return "RHI-ORIGINAL-BINDLESS-STATE-EMPTY";
            if (budget.bindlessRoots.geometryJoined()) {
                const auto geometry = inputs->visitGeometryAccesses(
                    budget.geometryUsage, [](const auto&) noexcept -> const char* {
                        return nullptr;
                    });
                if (geometry.diagnostic)
                    return geometry.diagnostic;
                if (!geometry.bindings || !geometry.draws || !geometry.drawAccesses)
                    return "RHI-ORIGINAL-BINDLESS-GEOMETRY-CONSUMPTION-REQUIRED";
            }
        }
        if (budget.contractVersion >= 2u) {
            if (decision.contractVersion != budget.contractVersion || !decision.stateAvailable
                || !decision.projectionAvailable || decision.diagnostic)
                return "RHI-ORIGINAL-TYPED-STATE-LOWERING-CLOSED";
            const bool readbacks = budget.retainedReadbackStateVersion == 1u;
            if (readbacks
                && (!decision.retainedReadbackStateAvailable
                    || !sources->retainedReadbackCorrespondence().enabled()))
                return "RHI-ORIGINAL-TYPED-STATE-READBACK-CORRESPONDENCE";
            std::uint32_t readbackSources = 0u;
            // State2 covers buffers and explicit render attachments; State3 adds
            // original depth-table view ranges. Source2 requires its independent
            // CPU state opt-in and existing exact Sources/Inputs correspondence.
            for (const auto& source : sources->sources()) {
                const auto domain = OriginalCommandResourceSources::domain(source);
                if (domain == CommandReferenceDomain::Readback && readbacks) {
                    const auto* readback =
                        std::get_if<std::shared_ptr<const OriginalCommandTextureReadbackSource>>(
                            &source);
                    if (!readback || (*readback)->contractVersion() != 2u)
                        return "RHI-ORIGINAL-TYPED-STATE-READBACK-SOURCE-PROFILE";
                    if (readbackSources
                        == sources->retainedReadbackCorrespondence().maxReadbackSources)
                        return "RHI-ORIGINAL-TYPED-STATE-READBACK-SOURCE-CAPACITY";
                    ++readbackSources;
                    continue;
                }
                if (domain == CommandReferenceDomain::DescriptorTable
                    && (budget.contractVersion < 3u || budget.contractVersion == 5u))
                    return "RHI-ORIGINAL-CONSUMPTION-TABLE-PROFILE";
                if (domain != CommandReferenceDomain::Buffer
                    && domain != CommandReferenceDomain::Texture
                    && domain != CommandReferenceDomain::TextureView
                    && domain != CommandReferenceDomain::Pipeline
                    && domain != CommandReferenceDomain::Descriptor
                    && !(budget.contractVersion >= 3u && domain == CommandReferenceDomain::Sampler
                        && budget.descriptorConsumption.generalShaderResourcesVersion
                            == (budget.contractVersion == 5u ? 2u : 1u)
                        && budget.descriptorConsumption.generalShaderResourcesOptIn
                        && budget.renderDescriptorUsage.generalShaderResourcesVersion
                            == (budget.contractVersion == 5u ? 2u : 1u)
                        && budget.renderDescriptorUsage.generalShaderResourcesOptIn)
                    && !((budget.contractVersion == 3u || budget.contractVersion == 4u)
                        && domain == CommandReferenceDomain::DescriptorTable))
                    return "RHI-ORIGINAL-TYPED-STATE-SOURCE-PROFILE";
                if (const auto* graphics =
                        std::get_if<std::shared_ptr<const OriginalCommandGraphicsPipelineSource>>(
                            &source))
                    if (const auto* id = (*graphics)->independentCpuQuotaDiagnostic(quota))
                        return id;
                if (const auto* compute =
                        std::get_if<std::shared_ptr<const OriginalCommandComputePipelineSource>>(
                            &source))
                    if (const auto* id = (*compute)->independentCpuQuotaDiagnostic(quota))
                        return id;
            }
            if (readbacks && !readbackSources)
                return "RHI-ORIGINAL-TYPED-STATE-READBACK-MISSING";
            const auto result = Consumption::walkOriginal(
                inputs, budget.descriptorConsumption, [](const auto&) noexcept -> const char* {
                    return nullptr;
                });
            if (result.diagnostic)
                return result.diagnostic;
            if (budget.contractVersion == 4u) {
                const auto geometry = inputs->visitGeometryAccesses(
                    budget.geometryUsage, [](const auto&) noexcept -> const char* {
                        return nullptr;
                    });
                if (geometry.diagnostic)
                    return geometry.diagnostic;
                if (!geometry.bindings)
                    return "RHI-ORIGINAL-TYPED-STATE-GEOMETRY-EMPTY";
            } else if (!result.consumptions || !result.logicalAccesses)
                return "RHI-ORIGINAL-TYPED-STATE-CONSUMPTION-EMPTY";
            if (budget.contractVersion >= 3u)
                if (const auto* id = Consumption::renderBindingsOriginal(inputs,
                        budget.renderDescriptorUsage,
                        [](const auto&) noexcept -> const char* {
                            return nullptr;
                        })
                        .diagnostic)
                    return id;
        }
        if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes)
            return "RHI-ORIGINAL-RESOURCE-STATES-QUOTA";
        if (core::SharedOwnerProjection::sameControlBlock(quota, inputs->originalMetadataQuota())
            || core::SharedOwnerProjection::sameControlBlock(
                quota, sources->originalMetadataQuota()))
            return "RHI-ORIGINAL-RESOURCE-STATES-QUOTA-DOMAIN";
        buffers = ranges = 0u;
        const auto observationCapacity = [&]() noexcept {
            const auto limit = std::min(
                budget.maxStateObservations, sources->originalBudget().maxStateObservations);
            return buffers > limit || ranges >= limit - buffers;
        };
        for (std::size_t sourceIndex = 0u; sourceIndex < sources->sources().size(); ++sourceIndex)
            if (const auto* lease = std::get_if<std::shared_ptr<const OriginalCommandBufferLease>>(
                    &sources->sources()[sourceIndex])) {
                if (const auto issued = readObservation(reads, *inputs, sourceIndex)) {
                    if (const auto* id = issued->diagnostic())
                        return id;
                } else if (committed)
                    if (const auto* id =
                            (*lease)->originalObject()->originalCommittedStateDiagnostic())
                        return id;
                if (observationCapacity())
                    return "RHI-ORIGINAL-RESOURCE-STATES-OBSERVATION-CAPACITY";
                ++buffers;
            }
        if (const auto *id = visitTextureObservations(
                inputs, budget, [&](const auto& point, const GpuTexture& texture) -> const char* {
                    if (committed)
                        if (const auto* error =
                                texture.originalCommittedStateDiagnostic(point.range))
                            return error;
                    if (observationCapacity())
                        return "RHI-ORIGINAL-RESOURCE-STATES-OBSERVATION-CAPACITY";
                    if (ranges == budget.maxTextureRanges
                        || ranges == sources->originalBudget().maxTextureRanges)
                        return "RHI-ORIGINAL-RESOURCE-STATES-RANGE-CAPACITY";
                    ++ranges;
                    return nullptr;
                }))
            return id;
        if (!buffers && !ranges)
            return "RHI-ORIGINAL-RESOURCE-STATES-SOURCE-UNSUPPORTED";
        const std::uint64_t roots = reads ? 3u + reads->batch_->links : 1u;
        if (roots > UINT32_MAX || inputs->retainedOwnerCount() > UINT32_MAX - roots)
            return "RHI-ORIGINAL-STATE-OWNER-CAPACITY";
        std::uint32_t registrations = 0u;
        return OriginalCommandResourceSources::registeredOwnerUnion(sources,
            static_cast<std::uint32_t>(inputs->retainedOwnerCount() + roots),
            budget.maxRetainedOwners,
            budget.maxResourceRegistrations,
            owners,
            registrations);
    }

    const std::shared_ptr<const OriginalCommandInputs> inputs_;
    const OriginalDeviceLifetime::Ticket device_;
    const OriginalCommandStateAdmissionBudget budget_;
    core::BoundedSequence<BufferEntry> buffers_;
    core::BoundedSequence<TextureEntry> textures_;
    std::optional<ReadPins::Reservation> reads_;
    std::optional<SubmittedReadAdmission> readAdmission_;
    const std::uint32_t ownerCount_;
    const TypedDecision consumptionDecision_;
};
} // namespace arp::rhi

// endregion original_command_resource_states
// endregion Command resource states

// region Command state sequence

namespace arp::rhi::common {
// Immutable original CPU state timeline. Every texture query selects an exact
// canonical mip/layer/aspect cell before reusing the scalar sequence resolver.
// This class retains no owner/control/journal and cannot publish mutations.
class OriginalCommandStateSequence final {
    friend class arp::rhi::OriginalCommandResourceStates;
    friend class OriginalTypedUsageProjection;
    friend class arp::rhi::OriginalCommandStateTransaction;
    using Graph = OriginalCommandResourceStates;
    using Projection = OriginalCommandTextureRanges;
    using Sequence = semantics::RecordedResourceStateSequence;
    using K = CommandUsageKind;

public:
    struct Result {
        const char* diagnostic = nullptr;
        std::size_t commandIndex = 0u;
        std::uint64_t stateCells = 0u, steps = 0u, dependencyVisits = 0u;
    };

    static Result inspect(const std::shared_ptr<const Graph>& graph,
        const OriginalCommandStateSequenceBudget& budget) noexcept
    {
        Result result;
        if ((result.diagnostic = OriginalCommandStateSequenceContract::validateIntent(budget)))
            return result;
        if ((result.diagnostic = Graph::validateOriginal(graph)))
            return result;
        return inspectValidatedGraph(*graph, budget);
    }

private:
    // Only the original Graph enters after source/observation admission; no
    // external callback or persistent bypass can obtain this traversal.
    static Result inspectValidatedGraph(
        const Graph& original, const OriginalCommandStateSequenceBudget& budget) noexcept
    {
        Result result;
        if ((result.diagnostic = OriginalCommandStateSequenceContract::validateIntent(budget)))
            return result;
        const auto* graph = &original;
        if (graph->stateContractVersion() != budget.contractVersion) {
            result.diagnostic = "RHI-ORIGINAL-SEQUENCE-STATE-PROFILE";
            return result;
        }
        if (graph->budget_.bindlessRoots.contractVersion != budget.bindlessRootStateVersion) {
            result.diagnostic = "RHI-ORIGINAL-BINDLESS-SEQUENCE-STATE-PROFILE";
            return result;
        }
        const auto& commands = usage(*graph).commands();
        if (commands.size() > budget.maxCommands) {
            result.diagnostic = "RHI-ORIGINAL-SEQUENCE-COMMAND-CAPACITY";
            return result;
        }
        // Independent count admission precedes all timeline resolution. No
        // expanded array, allocation or mutation is needed for this dry pass.
        result.stateCells = graph->bufferEntries().size();
        if (result.stateCells > budget.maxStateCells) {
            result.diagnostic = "RHI-ORIGINAL-SEQUENCE-CELL-CAPACITY";
            return result;
        }
        for (const auto& entry : graph->textureEntries()) {
            const auto* texture = textureAt(*graph, entry.point.sourceIndex);
            const auto geometry = geometryOf(*texture);
            const auto count = geometry->rangeCellCount(entry.point.range);
            if (!count || count > budget.maxStateCells - result.stateCells) {
                result.diagnostic = "RHI-ORIGINAL-SEQUENCE-CELL-CAPACITY";
                return result;
            }
            result.stateCells += count;
            if (count > budget.maxSteps - result.steps) {
                result.diagnostic = "RHI-ORIGINAL-SEQUENCE-STEP-CAPACITY";
                return result;
            }
            result.steps += count;
        }
        for (const auto& reference : usage(*graph).references())
            if (std::holds_alternative<std::shared_ptr<const OriginalCommandBufferLease>>(
                    sources(*graph)[reference.sourceIndex])) {
                if (result.steps == budget.maxSteps) {
                    result.diagnostic = "RHI-ORIGINAL-SEQUENCE-STEP-CAPACITY";
                    return result;
                }
                ++result.steps;
            }
        // A readback's destination is an explicit canonical Source closure
        // Buffer, not a raw command operand. Charge its access independently.
        for (const auto& command : commands)
            if (command.kind == K::TextureReadback) {
                if (result.steps == budget.maxSteps) {
                    result.diagnostic = "RHI-ORIGINAL-SEQUENCE-STEP-CAPACITY";
                    return result;
                }
                ++result.steps;
            }
        if (budget.bindlessRootStateVersion) {
            const auto roots =
                OriginalCommandBindlessRootAccesses::visitValidatedOriginal(graph->originalInputs(),
                    graph->budget_.bindlessRoots,
                    [](const auto&) noexcept -> const char* {
                        return nullptr;
                    });
            if (roots.diagnostic) {
                result.diagnostic = roots.diagnostic;
                return result;
            }
            if (roots.accesses > budget.maxImplicitAccesses) {
                result.diagnostic = "RHI-ORIGINAL-SEQUENCE-IMPLICIT-CAPACITY";
                return result;
            }
            const auto buffers = roots.accesses - roots.textureAccesses;
            if (buffers > budget.maxSteps - result.steps) {
                result.diagnostic = "RHI-ORIGINAL-SEQUENCE-STEP-CAPACITY";
                return result;
            }
            result.steps += buffers;
            if (budget.bindlessRootStateVersion == 5u || budget.bindlessRootStateVersion == 6u) {
                const auto geometry = graph->visitGeometryAccesses(
                    graph->budget_.geometryUsage, [](const auto&) noexcept -> const char* {
                        return nullptr;
                    });
                if (geometry.diagnostic)
                    return {geometry.diagnostic};
                if (roots.accesses > budget.maxImplicitAccesses
                    || geometry.bindings > budget.maxImplicitAccesses - roots.accesses
                    || geometry.drawAccesses
                        > budget.maxImplicitAccesses - roots.accesses - geometry.bindings)
                    return {"RHI-ORIGINAL-SEQUENCE-IMPLICIT-CAPACITY"};
                if (geometry.drawAccesses > budget.maxSteps - result.steps)
                    return {"RHI-ORIGINAL-SEQUENCE-STEP-CAPACITY"};
                result.steps += geometry.drawAccesses;
            }
        }
        if (budget.contractVersion >= 2u) {
            std::uint64_t implicitAccesses = 0u;
            const auto charge = [&](const auto& implicit) noexcept -> const char* {
                if (implicit.diagnostic)
                    return implicit.diagnostic;
                if (implicit.logicalAccesses > budget.maxImplicitAccesses - implicitAccesses)
                    return "RHI-ORIGINAL-SEQUENCE-IMPLICIT-CAPACITY";
                implicitAccesses += implicit.logicalAccesses;
                // State3 already captured a step per represented texture cell,
                // including every shadowed bind. Charge only implicit buffers.
                if (implicit.textureAccesses > implicit.logicalAccesses)
                    return "RHI-ORIGINAL-SEQUENCE-IMPLICIT-COVERAGE";
                const auto nonTexture = implicit.logicalAccesses - implicit.textureAccesses;
                if (implicit.samplerReferences > nonTexture)
                    return "RHI-ORIGINAL-SEQUENCE-IMPLICIT-COVERAGE";
                const auto buffers = nonTexture - implicit.samplerReferences;
                if (buffers > budget.maxSteps - result.steps)
                    return "RHI-ORIGINAL-SEQUENCE-STEP-CAPACITY";
                result.steps += buffers;
                return nullptr;
            };
            const auto implicit =
                graph->visitDescriptorAccesses([&](const auto& access) noexcept -> const char* {
                    return graph->validateSubmittedShaderAccess(access);
                });
            if ((result.diagnostic = charge(implicit))) {
                result.commandIndex = implicit.commandIndex;
                return result;
            }
            if (budget.contractVersion >= 3u) {
                const auto render = graph->visitRenderDescriptorAccesses(
                    graph->budget_.renderDescriptorUsage, [](const auto&) noexcept -> const char* {
                        return nullptr;
                    });
                if ((result.diagnostic = charge(render))) {
                    result.commandIndex = render.commandIndex;
                    return result;
                }
            }
            if (budget.contractVersion == 4u) {
                const auto geometry = graph->visitGeometryAccesses(
                    graph->budget_.geometryUsage, [](const auto&) noexcept -> const char* {
                        return nullptr;
                    });
                if (geometry.diagnostic) {
                    result.diagnostic = geometry.diagnostic;
                    result.commandIndex = geometry.commandIndex;
                    return result;
                }
                if (geometry.bindings > budget.maxImplicitAccesses - implicitAccesses
                    || geometry.drawAccesses
                        > budget.maxImplicitAccesses - implicitAccesses - geometry.bindings) {
                    result.diagnostic = "RHI-ORIGINAL-SEQUENCE-IMPLICIT-CAPACITY";
                    return result;
                }
                // Each binding has one explicit buffer operand already charged
                // above. Count its implicit footprint without duplicating steps.
                if (geometry.drawAccesses > budget.maxSteps - result.steps) {
                    result.diagnostic = "RHI-ORIGINAL-SEQUENCE-STEP-CAPACITY";
                    return result;
                }
                result.steps += geometry.drawAccesses;
            }
        }
        semantics::CommandRecordingOrder order(budget.maxDebugDepth);
        for (std::size_t i = 0u; i < commands.size(); ++i) {
            result.commandIndex = i;
            if ((result.diagnostic = order.append(commands[i].kind)))
                return result;
            if ((result.diagnostic = validateCommand(*graph, i)))
                return result;
        }
        if (!order.complete()) {
            result.commandIndex = commands.size();
            result.diagnostic = "RHI-COMMAND-ORDER-INCOMPLETE";
            return result;
        }
        if ((graph->budget_.bindlessRoots.contractVersion == 6u
                || !graph->budget_.bindlessRoots.directResultCopies.empty())
            && (result.diagnostic = validateRootBufferDependencies(*graph, result)))
            return result;
        // Validate all represented final cells independently. Final ranges may
        // be nonuniform; that is an explicit cell result, never a widened state.
        for (const auto& entry : graph->bufferEntries()) {
            ResourceState state;
            const auto& lease = std::get<std::shared_ptr<const OriginalCommandBufferLease>>(
                sources(*graph)[entry.sourceIndex]);
            if ((result.diagnostic =
                        bufferBefore(*graph, lease->resource(), commands.size(), state)))
                return result;
        }
        for (const auto& entry : graph->textureEntries()) {
            const auto* texture = textureAt(*graph, entry.point.sourceIndex);
            const auto geometry = geometryOf(*texture);
            for (std::uint64_t ordinal = 0u; ordinal < geometry->rangeCellCount(entry.point.range);
                ++ordinal) {
                std::uint64_t cell = 0u;
                geometry->cellIndex(entry.point.range, ordinal, cell);
                ResourceState state;
                if ((result.diagnostic = textureBefore(
                         *graph, *texture, *geometry, cell, commands.size(), state)))
                    return result;
            }
        }
        result.commandIndex = commands.size();
        return result;
    }

public:
    static const char* resolveOriginalBuffer(const std::shared_ptr<const Graph>& graph,
        GpuResourceId resource,
        std::size_t beforeCommand,
        ResourceState& output) noexcept
    {
        if (const auto* id = Graph::validateOriginal(graph))
            return id;
        return bufferBefore(*graph, resource, beforeCommand, output);
    }

    static const char* resolveOriginalTextureCell(const std::shared_ptr<const Graph>& graph,
        std::size_t observationIndex,
        std::uint64_t ordinal,
        std::size_t beforeCommand,
        ResourceState& output) noexcept
    {
        if (const auto* id = Graph::validateOriginal(graph))
            return id;
        return resolveValidatedTextureCell(
            *graph, observationIndex, ordinal, beforeCommand, output);
    }

private:
    static const char* validateRootBufferDependencies(const Graph& graph, Result& result) noexcept
    {
        const auto& inputs = graph.originalInputs();
        const auto& original = usage(graph);
        const auto& inventory = sources(graph);
        const auto& roots = graph.budget_.bindlessRoots;
        const auto geometryInputs = graph.visitGeometryAccesses(
            graph.budget_.geometryUsage, [](const auto&) noexcept -> const char* {
                return nullptr;
            });
        if (geometryInputs.diagnostic)
            return geometryInputs.diagnostic;
        const auto maxDependencyVisits = roots.directResultCopies.empty()
            ? roots.maxDependencyVisits
            : roots.directResultCopies.maxDependencyVisits;
        const auto charge = [&]() noexcept -> const char* {
            if (result.dependencyVisits == maxDependencyVisits)
                return "RHI-ORIGINAL-BINDLESS-DEPENDENCY-VISIT-CAPACITY";
            ++result.dependencyVisits;
            return nullptr;
        };
        // Replays borrow the existing graph; no per-resource state table or
        // dynamic allocation. Every source, command and projected scope is
        // charged to the independent cold dependency-work ceiling.
        for (const auto& source : inventory) {
            if (const auto* id = charge())
                return id;
            const auto* lease =
                std::get_if<std::shared_ptr<const OriginalCommandBufferLease>>(&source);
            if (!lease)
                continue;
            const auto resource = (*lease)->resource();
            semantics::RecordedResourceAccessDependencies dependencies;
            for (std::size_t command = 0u; command < original.commands().size(); ++command) {
                result.commandIndex = command;
                if (const auto* id = charge())
                    return id;
                const auto& op = original.commands()[command];
                if (op.kind == K::BufferBarrier || op.kind == K::BufferMemoryBarrier) {
                    const auto* value = inputs->descriptorAt(command);
                    const auto* dependency =
                        value ? std::get_if<ResourceDependencyArguments>(value) : nullptr;
                    ResourceBarrierDesc barrier;
                    ResourceMemoryBarrierDesc memory;
                    if (dependency && dependency->write(op.kind, barrier)
                        && barrier.resource == resource) {
                        if (barrier.before == ResourceState::Undefined)
                            dependencies = {};
                        else if (const auto* id = dependencies.depend(
                                     {barrier.before, barrier.sourceStages, barrier.sourceAccess},
                                     {barrier.after,
                                         barrier.destinationStages,
                                         barrier.destinationAccess}))
                            return id;
                    } else if (dependency && dependency->write(op.kind, memory)
                        && memory.resource == resource) {
                        ResourceState current;
                        if (const auto* id = bufferBefore(graph, resource, command, current))
                            return id;
                        if (const auto* id = dependencies.depend(
                                {current, memory.sourceStages, memory.sourceAccess},
                                {current, memory.destinationStages, memory.destinationAccess}))
                            return id;
                    }
                    continue;
                }
                semantics::ResourceAccessScope combined;
                const auto merge =
                    [&](GpuResourceId selected,
                        const semantics::ResourceAccessScope& scope) noexcept -> const char* {
                    if (selected != resource)
                        return nullptr;
                    if (combined.access != ResourceAccess::None && combined.state != scope.state)
                        return "RHI-ORIGINAL-BINDLESS-DEPENDENCY-COMMAND-STATE";
                    combined.state = scope.state;
                    combined.stages = combined.stages | scope.stages;
                    combined.access = combined.access | scope.access;
                    return nullptr;
                };
                const auto projected = OriginalCommandBindlessRootAccesses::visitValidatedOriginal(
                    inputs, roots, [&](const auto& point) noexcept -> const char* {
                        if (const auto* id = charge())
                            return id;
                        const auto& access = point.declaration;
                        return point.commandIndex == command
                                && access.kind == BindlessAccessKind::Buffer
                            ? merge(access.resource,
                                  {access.state, access.pipelineStages, access.access})
                            : nullptr;
                    });
                if (projected.diagnostic)
                    return projected.diagnostic;
                // Whole-graph validation above already checked every geometry
                // input. A geometry consumption cannot name a non-draw ordinal;
                // avoid revalidating the entire snapshot for each such command.
                if (op.kind == K::Draw || op.kind == K::DrawIndexed
                    || op.kind == K::DrawIndexedIndirect) {
                    const auto geometry = graph.visitGeometryAccesses(graph.budget_.geometryUsage,
                        [&](const auto& point) noexcept -> const char* {
                            if (const auto* id = charge())
                                return id;
                            return point.commandIndex == command && point.drawConsumption()
                                ? merge(point.resource, point.scope)
                                : nullptr;
                        });
                    if (geometry.diagnostic)
                        return geometry.diagnostic;
                }
                if (roots.contractVersion == 6u) {
                    const auto indirect = OriginalCommandIndirectAccesses::visit(
                        inputs, roots, [&](const auto& point) noexcept -> const char* {
                            if (const auto* id = charge())
                                return id;
                            return point.commandIndex == command
                                ? merge(point.resource, point.scope)
                                : nullptr;
                        });
                    if (indirect.diagnostic)
                        return indirect.diagnostic;
                }
                if (op.kind == K::BufferCopy) {
                    for (std::size_t operand = 0u; operand < op.referenceCount; ++operand) {
                        if (const auto* id = charge())
                            return id;
                        const auto index =
                            original.references()[op.firstReference + operand].sourceIndex;
                        const auto& buffer =
                            std::get<std::shared_ptr<const OriginalCommandBufferLease>>(
                                inventory[index]);
                        if (const auto* id = merge(buffer->resource(),
                                {operand ? ResourceState::CopyDst : ResourceState::CopySrc,
                                    PipelineStage::Transfer,
                                    operand ? ResourceAccess::TransferWrite
                                            : ResourceAccess::TransferRead}))
                            return id;
                    }
                }
                if (combined.access != ResourceAccess::None)
                    if (const auto* id = dependencies.consume(combined))
                        return id;
            }
        }
        return nullptr;
    }

    // The scoped CPU projector and private transaction plan population enter
    // only after complete original graph validation. Neither retains a ticket
    // or calls external code in this scope. Public queries retain their guard.
    static const char* resolveValidatedTextureCell(const Graph& graph,
        std::size_t observationIndex,
        std::uint64_t ordinal,
        std::size_t beforeCommand,
        ResourceState& output) noexcept
    {
        if (observationIndex >= graph.textureEntries().size())
            return "RHI-ORIGINAL-SEQUENCE-OBSERVATION";
        const auto& entry = graph.textureEntries()[observationIndex];
        const auto* texture = textureAt(graph, entry.point.sourceIndex);
        const auto geometry = geometryOf(*texture);
        std::uint64_t cell = 0u;
        if (!geometry->cellIndex(entry.point.range, ordinal, cell))
            return "RHI-ORIGINAL-SEQUENCE-CELL";
        return textureBefore(graph, *texture, *geometry, cell, beforeCommand, output);
    }

private:
    static const OriginalCommandUsage& usage(const Graph& graph) noexcept
    {
        return *graph.originalInputs()->originalScalars()->originalUsage();
    }

    static const core::pmr::Vector<OriginalCommandResourceSources::Source>& sources(
        const Graph& graph) noexcept
    {
        return usage(graph).originalSources()->sources();
    }

    static const GpuTexture* textureAt(const Graph& graph, std::size_t sourceIndex) noexcept
    {
        return Projection::originalTexture(sources(graph)[sourceIndex]);
    }

    static std::optional<semantics::OriginalTextureStateRange> geometryOf(
        const GpuTexture& texture) noexcept
    {
        const char* diagnostic = nullptr;
        return semantics::OriginalTextureStateRange::capture(
            texture.desc(), UINT64_MAX, diagnostic);
    }

    static const OriginalCommandInputs::Descriptor* descriptor(
        const Graph& graph, std::size_t command) noexcept
    {
        return graph.originalInputs()->descriptorAt(command);
    }

    static std::optional<Sequence::Transition> transition(
        const Graph& graph, std::size_t command, K required) noexcept
    {
        const auto* value = descriptor(graph, command);
        const auto* dependency = value ? std::get_if<ResourceDependencyArguments>(value) : nullptr;
        ResourceBarrierDesc barrier;
        return dependency && dependency->write(required, barrier)
            ? std::optional<Sequence::Transition>{{barrier.resource, barrier.before, barrier.after}}
            : std::nullopt;
    }

    static const char* bufferBefore(const Graph& graph,
        GpuResourceId resource,
        std::size_t before,
        ResourceState& output) noexcept
    {
        return Sequence::resolve(
            resource,
            before,
            usage(graph).commands().size(),
            [&](GpuResourceId id) -> std::optional<ResourceState> {
                for (const auto& entry : graph.bufferEntries()) {
                    const auto& lease = std::get<std::shared_ptr<const OriginalCommandBufferLease>>(
                        sources(graph)[entry.sourceIndex]);
                    if (lease->resource() == id)
                        return entry.observation.state();
                }
                return {};
            },
            [&](std::size_t command) noexcept {
                return transition(graph, command, K::BufferBarrier);
            },
            output);
    }

    static const char* textureBefore(const Graph& graph,
        const GpuTexture& texture,
        const semantics::OriginalTextureStateRange& geometry,
        std::uint64_t cell,
        std::size_t before,
        ResourceState& output) noexcept
    {
        if (cell >= geometry.cellCount())
            return "RHI-ORIGINAL-SEQUENCE-CELL";
        return Sequence::resolve(
            texture.id(),
            before,
            usage(graph).commands().size(),
            [&](GpuResourceId) -> std::optional<ResourceState> {
                for (const auto& entry : graph.textureEntries())
                    if (textureAt(graph, entry.point.sourceIndex) == &texture
                        && geometry.containsCell(entry.point.range, cell))
                        return entry.observation.state();
                return {};
            },
            [&](std::size_t command) -> std::optional<Sequence::Transition> {
                const auto* value = descriptor(graph, command);
                const auto* dependency =
                    value ? std::get_if<ResourceDependencyArguments>(value) : nullptr;
                ResourceBarrierDesc barrier;
                if (!dependency || !dependency->write(K::TextureBarrier, barrier)
                    || barrier.resource != texture.id()
                    || !geometry.containsCell(barrier.textureSubresources, cell))
                    return {};
                return Sequence::Transition{barrier.resource, barrier.before, barrier.after};
            },
            output);
    }

    static const char* validateBuffer(
        const Graph& graph, std::size_t command, std::size_t operand) noexcept
    {
        const auto& item = usage(graph).commands()[command];
        const auto sourceIndex =
            usage(graph).references()[item.firstReference + operand].sourceIndex;
        if (graph.submittedReadAt(sourceIndex))
            return "RHI-SUBMISSION-READ-EXPLICIT-ACCESS";
        const auto* lease = std::get_if<std::shared_ptr<const OriginalCommandBufferLease>>(
            &sources(graph)[sourceIndex]);
        if (!lease)
            return "RHI-ORIGINAL-SEQUENCE-BUFFER";
        ResourceState state;
        if (const auto* id = bufferBefore(graph, (*lease)->resource(), command, state))
            return id;
        const auto* value = descriptor(graph, command);
        const auto* dependency = value ? std::get_if<ResourceDependencyArguments>(value) : nullptr;
        if (item.kind == K::BufferBarrier) {
            ResourceBarrierDesc barrier;
            if (!dependency || !dependency->write(item.kind, barrier))
                return "RHI-ORIGINAL-SEQUENCE-DESCRIPTOR";
            if (barrier.textureSubresources != TextureSubresourceRange{})
                return "RHI-ORIGINAL-SEQUENCE-BUFFER-RANGE";
            if (!semantics::isBufferResourceState(barrier.after))
                return "RHI-BUFFER-RESOURCE-STATE-INVALID";
            return Sequence::validateBarrier(barrier, state);
        }
        if (item.kind == K::BufferMemoryBarrier) {
            ResourceMemoryBarrierDesc memory;
            if (!dependency || !dependency->write(item.kind, memory))
                return "RHI-ORIGINAL-SEQUENCE-DESCRIPTOR";
            return Sequence::validateMemoryBarrier(memory, state);
        }
        return state == (operand ? ResourceState::CopyDst : ResourceState::CopySrc)
            ? nullptr
            : "RHI-ORIGINAL-SEQUENCE-COPY-STATE";
    }

    static const char* validateTexture(
        const Graph& graph, const Graph::TextureEntry& entry) noexcept
    {
        const auto command = entry.point.commandIndex;
        const auto kind = usage(graph).commands()[command].kind;
        const auto* texture = textureAt(graph, entry.point.sourceIndex);
        const auto geometry = geometryOf(*texture);
        const auto* value = descriptor(graph, command);
        const auto* dependency = value ? std::get_if<ResourceDependencyArguments>(value) : nullptr;
        ResourceBarrierDesc barrier;
        ResourceMemoryBarrierDesc memory;
        if (kind == K::TextureBarrier && (!dependency || !dependency->write(kind, barrier)))
            return "RHI-ORIGINAL-SEQUENCE-DESCRIPTOR";
        if (kind == K::TextureMemoryBarrier && (!dependency || !dependency->write(kind, memory)))
            return "RHI-ORIGINAL-SEQUENCE-DESCRIPTOR";
        if (kind == K::TextureBarrier
            && !semantics::isTextureResourceState(texture->desc().format, barrier.after))
            return "RHI-RESOURCE-STATE-INVALID";
        const bool implicit = entry.point.origin != Projection::Point::Origin::ExplicitOperand;
        if (implicit && graph.stateContractVersion() < 3u
            && entry.point.origin != Projection::Point::Origin::BindlessRoot)
            return "RHI-ORIGINAL-SEQUENCE-TEXTURE-PROFILE";
        // Implicit points have no explicit operand/reference. Preserve their
        // original operation scope instead of interpreting the SIZE_MAX marker.
        const auto operand = implicit
            ? 0u
            : entry.point.referenceIndex - usage(graph).commands()[command].firstReference;
        const auto expected = kind == K::BeginRenderPass
            ? (entry.point.range.aspect == TextureAspect::Color ? ResourceState::ColorAttachment
                                                                : ResourceState::DepthAttachment)
            : kind == K::TextureUpload   ? ResourceState::CopyDst
            : kind == K::TextureReadback ? ResourceState::CopySrc
            : operand                    ? ResourceState::CopyDst
                                         : ResourceState::CopySrc;
        for (std::uint64_t ordinal = 0u; ordinal < geometry->rangeCellCount(entry.point.range);
            ++ordinal) {
            std::uint64_t cell = 0u;
            geometry->cellIndex(entry.point.range, ordinal, cell);
            ResourceState state;
            if (const auto* id = textureBefore(graph, *texture, *geometry, cell, command, state))
                return id;
            if (kind == K::TextureBarrier) {
                if (const auto* id = Sequence::validateBarrier(barrier, state))
                    return id;
            } else if (kind == K::TextureMemoryBarrier) {
                if (const auto* id = Sequence::validateMemoryBarrier(memory, state))
                    return id;
            } else if (implicit) {
                if (state != entry.point.state)
                    return "RHI-ORIGINAL-SEQUENCE-SHADER-TEXTURE-STATE";
                if (const auto* id = semantics::validateResourceAccessScope(
                        {state, entry.point.stages, entry.point.access}))
                    return id;
                if (const auto* id = semantics::ResourceAccessUsageValidation::validate(
                        texture->desc(), entry.point.access))
                    return id;
            } else if (kind != K::RetainTexture && state != expected)
                return "RHI-ORIGINAL-SEQUENCE-TEXTURE-ACCESS-STATE";
        }
        return nullptr;
    }

    static const char* validateBindlessBuffers(const Graph& graph, std::size_t command) noexcept
    {
        return OriginalCommandBindlessRootAccesses::visitValidatedOriginal(graph.originalInputs(),
            graph.budget_.bindlessRoots,
            [&](const auto& access) noexcept -> const char* {
                const auto& declared = access.declaration;
                if (access.commandIndex != command || declared.kind != BindlessAccessKind::Buffer)
                    return nullptr;
                const auto& lease = std::get<std::shared_ptr<const OriginalCommandBufferLease>>(
                    sources(graph)[access.sourceIndex]);
                if (!declared.bufferSize || declared.bufferOffset > lease->layout().sizeBytes
                    || declared.bufferSize > lease->layout().sizeBytes - declared.bufferOffset)
                    return "RHI-ORIGINAL-BINDLESS-SEQUENCE-BUFFER-RANGE";
                ResourceState state;
                if (const auto* id = bufferBefore(graph, declared.resource, command, state))
                    return id;
                if (state != declared.state)
                    return "RHI-ORIGINAL-BINDLESS-SEQUENCE-BUFFER-STATE";
                if (const auto* id = semantics::validateResourceAccessScope(
                        {state, declared.pipelineStages, declared.access}))
                    return id;
                return semantics::ResourceAccessUsageValidation::validate(
                    lease->originalObject()->desc(), declared.access);
            })
            .diagnostic;
    }

    static const char* validateCommand(const Graph& graph, std::size_t command) noexcept
    {
        const auto& item = usage(graph).commands()[command];
        switch (item.kind) {
        case K::RetainIndexedSnapshot: {
            if (graph.budget_.bindlessRoots.empty())
                return "RHI-ORIGINAL-BINDLESS-SEQUENCE-STATE-PROFILE";
            if (graph.budget_.bindlessRoots.contractVersion == 1u)
                if (const auto* id = validateBindlessBuffers(graph, command))
                    return id;
            break;
        }
        case K::BindPreparedComputeBinding:
        case K::BindPreparedGraphicsBinding:
            if (graph.budget_.bindlessRoots.contractVersion < 2u)
                return "RHI-ORIGINAL-BINDLESS-SEQUENCE-STATE-PROFILE";
            break;
        case K::PushConstants:
            if (graph.budget_.bindlessRoots.pushConsumption.empty())
                return "RHI-ORIGINAL-PUSH-CONSUMPTION-PROFILE";
            if (const auto* id = validateBindlessBuffers(graph, command))
                return id;
            break;
        case K::ClearSnapshotFault:
            if (graph.budget_.bindlessRoots.contractVersion < 3u)
                return "RHI-ORIGINAL-BINDLESS-SEQUENCE-FAULT-PROFILE";
            if (const auto* id = validateBindlessBuffers(graph, command))
                return id;
            break;
        case K::BeginComputePass:
        case K::EndComputePass:
        case K::SetViewport:
        case K::SetScissor:
            if (graph.budget_.bindlessRoots.contractVersion < 2u
                && graph.stateContractVersion() != 2u && graph.stateContractVersion() != 3u
                && graph.stateContractVersion() != 4u && graph.stateContractVersion() != 5u)
                return "RHI-ORIGINAL-SEQUENCE-COMMAND-UNPROVEN";
            break;
        case K::BindGraphicsPipeline:
        case K::BindComputePipeline:
        case K::BindComputeDescriptor:
            if (graph.stateContractVersion() < 2u)
                return "RHI-ORIGINAL-SEQUENCE-COMMAND-UNPROVEN";
            break;
        case K::BindComputeDescriptorTable:
            if (graph.stateContractVersion() < 3u || graph.stateContractVersion() == 5u)
                return "RHI-ORIGINAL-SEQUENCE-COMMAND-UNPROVEN";
            break;
        case K::BindRenderDescriptor:
        case K::BindRenderDescriptorTable: {
            if (graph.stateContractVersion() == 2u && item.kind == K::BindRenderDescriptor)
                break;
            if (graph.stateContractVersion() < 3u
                || (graph.stateContractVersion() == 5u
                    && item.kind == K::BindRenderDescriptorTable))
                return "RHI-ORIGINAL-SEQUENCE-COMMAND-UNPROVEN";
            const auto result =
                graph.visitRenderDescriptorAccesses(graph.budget_.renderDescriptorUsage,
                    [&](const auto& access) noexcept -> const char* {
                        return access.commandIndex == command
                            ? validateShaderBufferAccess(graph, access)
                            : nullptr;
                    });
            if (result.diagnostic)
                return result.diagnostic;
            break;
        }
        case K::BindVertexBuffer:
        case K::BindIndexBuffer: {
            if (graph.stateContractVersion() != 4u && !graph.budget_.bindlessRoots.geometryJoined())
                return "RHI-ORIGINAL-SEQUENCE-GEOMETRY-PROFILE";
            const auto geometry = graph.visitGeometryAccesses(
                graph.budget_.geometryUsage, [&](const auto& access) noexcept -> const char* {
                    if (access.commandIndex != command)
                        return nullptr;
                    return validateGeometryBufferAccess(graph, access);
                });
            if (geometry.diagnostic)
                return geometry.diagnostic;
            break;
        }
        case K::DrawIndexedIndirect:
            if (graph.budget_.bindlessRoots.contractVersion != 6u)
                return "RHI-ORIGINAL-SEQUENCE-INDIRECT-PROFILE";
            if (const auto* id = OriginalCommandIndirectAccesses::visit(graph.originalInputs(),
                    graph.budget_.bindlessRoots,
                    [&](const auto& point) noexcept -> const char* {
                        if (point.commandIndex != command)
                            return nullptr;
                        ResourceState state;
                        if (const auto* id = bufferBefore(graph, point.resource, command, state))
                            return id;
                        if (state != point.scope.state)
                            return "RHI-ORIGINAL-SEQUENCE-INDIRECT-STATE";
                        const auto& lease =
                            std::get<std::shared_ptr<const OriginalCommandBufferLease>>(
                                sources(graph)[point.sourceIndex]);
                        return semantics::ResourceAccessUsageValidation::validate(
                            lease->originalObject()->desc(), point.scope.access);
                    })
                    .diagnostic)
                return id;
            [[fallthrough]];
        case K::Draw:
        case K::DrawIndexed:
        case K::Dispatch: {
            if (graph.budget_.bindlessRoots.contractVersion >= 2u) {
                if (const auto* id = validateBindlessBuffers(graph, command))
                    return id;
                if (graph.budget_.bindlessRoots.geometryJoined() && item.kind != K::Dispatch) {
                    const auto geometry = graph.visitGeometryAccesses(graph.budget_.geometryUsage,
                        [&](const auto& access) noexcept -> const char* {
                            return access.commandIndex == command
                                ? validateGeometryBufferAccess(graph, access)
                                : nullptr;
                        });
                    if (geometry.diagnostic)
                        return geometry.diagnostic;
                }
                break;
            }
            if (graph.stateContractVersion() != 2u && graph.stateContractVersion() != 3u
                && graph.stateContractVersion() != 4u && graph.stateContractVersion() != 5u)
                return "RHI-ORIGINAL-SEQUENCE-COMMAND-UNPROVEN";
            const auto result =
                graph.visitDescriptorAccesses([&](const auto& access) noexcept -> const char* {
                    if (access.commandIndex != command)
                        return nullptr;
                    return validateShaderBufferAccess(graph, access);
                });
            if (result.diagnostic)
                return result.diagnostic;
            if (graph.stateContractVersion() == 4u && item.kind != K::Dispatch
                && !graph.budget_.geometryUsage.drawConsumptionEmpty()) {
                const auto geometry = graph.visitGeometryAccesses(
                    graph.budget_.geometryUsage, [&](const auto& access) noexcept -> const char* {
                        return access.commandIndex == command
                            ? validateGeometryBufferAccess(graph, access)
                            : nullptr;
                    });
                if (geometry.diagnostic)
                    return geometry.diagnostic;
            }
            break;
        }
        case K::BeginTransferPass:
        case K::EndTransferPass:
        case K::Finish:
        case K::PushDebugGroup:
        case K::PopDebugGroup:
        case K::InsertDebugMarker:
        case K::EndRenderPass:
            break;
        case K::BufferBarrier:
        case K::BufferMemoryBarrier:
        case K::BufferCopy:
        case K::BufferToTextureCopy:
        case K::TextureToBufferCopy:
        case K::TextureToTextureCopy:
        case K::BufferUpload:
            for (std::size_t operand = 0u; operand < item.referenceCount; ++operand) {
                const auto index =
                    usage(graph).references()[item.firstReference + operand].sourceIndex;
                if (std::holds_alternative<std::shared_ptr<const OriginalCommandBufferLease>>(
                        sources(graph)[index]))
                    if (const auto* id = validateBuffer(graph, command, operand))
                        return id;
            }
            break;
        case K::TextureBarrier:
        case K::TextureMemoryBarrier:
        case K::TextureUpload:
        case K::RetainTexture:
            break;
        case K::BeginRenderPass: {
            const auto* value = descriptor(graph, command);
            const auto* pass =
                value ? std::get_if<std::shared_ptr<const RenderPassArguments>>(value) : nullptr;
            if (!pass || !*pass)
                return "RHI-ORIGINAL-SEQUENCE-DESCRIPTOR";
            const auto& desc = (*pass)->desc();
            if (!desc.subpasses.empty() || !desc.subpassDependencies.empty())
                return "RHI-ORIGINAL-SEQUENCE-SUBPASS-UNPROVEN";
            for (const auto& entry : graph.textureEntries())
                if (entry.point.commandIndex == command
                    && textureAt(graph, entry.point.sourceIndex)->desc().format
                        == TextureFormat::Depth24Stencil8)
                    return "RHI-ORIGINAL-SEQUENCE-STENCIL-ATTACHMENT-UNPROVEN";
            if (desc.hasDepthStencilAttachment
                && (desc.depthStencilAttachment.depthReadOnly
                    || desc.depthStencilAttachment.stencilReadOnly))
                return "RHI-ORIGINAL-SEQUENCE-READONLY-ATTACHMENT-UNPROVEN";
            break;
        }
        case K::TextureReadback: {
            const auto sourceIndex =
                usage(graph).references()[item.firstReference + 1u].sourceIndex;
            const auto* source =
                std::get_if<std::shared_ptr<const OriginalCommandTextureReadbackSource>>(
                    &sources(graph)[sourceIndex]);
            if (!source)
                return "RHI-ORIGINAL-SEQUENCE-READBACK-SOURCE";
            const auto& destination = (*source)->originalDestinationLease();
            for (const auto& entry : graph.bufferEntries())
                if (entry.observation.submitted()
                    && std::get<std::shared_ptr<const OriginalCommandBufferLease>>(
                           sources(graph)[entry.sourceIndex])
                            ->originalObject()
                        == destination->originalObject())
                    return "RHI-SUBMISSION-READ-EXPLICIT-ACCESS";
            ResourceState state;
            if (const auto* id = bufferBefore(graph, destination->resource(), command, state))
                return id;
            if (state != ResourceState::CopyDst)
                return "RHI-ORIGINAL-SEQUENCE-READBACK-DESTINATION-STATE";
            break;
        }
        // Original state/lifetime closure does not prove alias-memory binding.
        case K::AliasBarrier:
            return "RHI-ORIGINAL-SEQUENCE-ALIAS-BINDING-UNPROVEN";
        default:
            return "RHI-ORIGINAL-SEQUENCE-COMMAND-UNPROVEN";
        }
        for (const auto& entry : graph.textureEntries())
            if (entry.point.commandIndex == command)
                if (const auto* id = validateTexture(graph, entry))
                    return id;
        return nullptr;
    }

    static const char* validateGeometryBufferAccess(
        const Graph& graph, const OriginalCommandGeometryBindingContract::Access& access) noexcept
    {
        if (graph.submittedReadAt(access.binding.sourceIndex))
            return "RHI-SUBMISSION-READ-EXPLICIT-ACCESS";
        ResourceState state;
        if (const auto* id = bufferBefore(graph, access.resource, access.commandIndex, state))
            return id;
        if (state != access.scope.state)
            return "RHI-ORIGINAL-SEQUENCE-GEOMETRY-STATE";
        if (const auto* id = semantics::validateResourceAccessScope(access.scope))
            return id;
        const auto& lease = std::get<std::shared_ptr<const OriginalCommandBufferLease>>(
            sources(graph)[access.binding.sourceIndex]);
        return semantics::ResourceAccessUsageValidation::validate(
            lease->originalObject()->desc(), access.scope.access);
    }

    static const char* validateShaderBufferAccess(
        const Graph& graph, const OriginalCommandDescriptorConsumption::Access& access) noexcept
    {
        if (access.kind == OriginalCommandDescriptorConsumption::Access::Kind::TextureView
            || access.kind == OriginalCommandDescriptorConsumption::Access::Kind::Sampler)
            return nullptr; // Texture ranges and immutable sampler custody were validated above.
        if (const auto* id = graph.validateSubmittedShaderAccess(access))
            return id;
        const auto& lease = std::get<std::shared_ptr<const OriginalCommandBufferLease>>(
            sources(graph)[access.bufferSourceIndex]);
        ResourceState state;
        if (const auto* id = bufferBefore(graph, lease->resource(), access.commandIndex, state))
            return id;
        if (state != access.state)
            return "RHI-ORIGINAL-SEQUENCE-SHADER-STATE";
        if (const auto* id =
                semantics::validateResourceAccessScope({state, access.stages, access.access}))
            return id;
        return semantics::ResourceAccessUsageValidation::validate(
            lease->originalObject()->desc(), access.access);
    }
};

// endregion Command state sequence

// region Command access validation

// Full declared CPU access/layout prerequisite of the supported original
// Source profile. Original provenance/state/counts validate before this pass;
// no native object, journal, output allocation or execution proof is consumed.
class OriginalCommandAccessValidation final {
    using Graph = OriginalCommandResourceStates;
    using Sequence = OriginalCommandStateSequence;
    using Inputs = OriginalCommandInputs;
    using Source = OriginalCommandResourceSources::Source;
    using K = CommandUsageKind;

public:
    static Sequence::Result inspect(const std::shared_ptr<const Graph>& graph,
        const OriginalCommandStateSequenceBudget& budget) noexcept
    {
        auto result = Sequence::inspect(graph, budget);
        if (result.diagnostic)
            return result;
        const auto& inputs = *graph->originalInputs();
        const auto& usage = *inputs.originalScalars()->originalUsage();
        for (const auto& descriptor : inputs.descriptors()) {
            result.commandIndex = descriptor.commandIndex;
            const auto& command = usage.commands()[descriptor.commandIndex];
            const auto& value = descriptor.arguments;
            const auto operand = [&](std::size_t i) -> const Source& {
                return usage.originalSources()
                    ->sources()[usage.references()[command.firstReference + i].sourceIndex];
            };
            switch (command.kind) {
            case K::BindVertexBuffer:
            case K::BindIndexBuffer:
                if (graph->stateContractVersion() != 4u && budget.bindlessRootStateVersion != 5u
                    && budget.bindlessRootStateVersion != 6u)
                    result.diagnostic = "RHI-ORIGINAL-ACCESS-GEOMETRY-PROFILE";
                break;
            case K::BindGraphicsPipeline:
            case K::BindComputePipeline:
            case K::BindRenderDescriptor:
            case K::BindComputeDescriptor:
                // Sequence2 validates implicit access at consumption, not bind.
                if (graph->stateContractVersion() != 2u && graph->stateContractVersion() != 3u
                    && graph->stateContractVersion() != 4u && graph->stateContractVersion() != 5u)
                    result.diagnostic = "RHI-ORIGINAL-ACCESS-COMMAND-UNPROVEN";
                break;
            case K::BindRenderDescriptorTable:
            case K::BindComputeDescriptorTable:
                if (graph->stateContractVersion() < 3u || graph->stateContractVersion() == 5u)
                    result.diagnostic = "RHI-ORIGINAL-ACCESS-COMMAND-UNPROVEN";
                break;
            case K::BufferCopy:
            case K::BufferToTextureCopy:
            case K::TextureToBufferCopy:
            case K::TextureToTextureCopy:
                result.diagnostic = validateCopy(value, operand(0u), operand(1u));
                break;
            case K::BufferUpload: {
                const auto& upload = std::get<BufferUploadArguments>(value);
                if (upload.stagingIntent() != StagingIntent::Upload
                    || !upload.backendOwnedStaging())
                    result.diagnostic = "RHI-ORIGINAL-ACCESS-UPLOAD-STAGING";
                else
                    result.diagnostic =
                        validateBufferCopy(upload.copyArguments(), operand(0u), operand(1u));
                break;
            }
            case K::TextureUpload: {
                const auto& source =
                    std::get<std::shared_ptr<const OriginalCommandTextureUploadSource>>(
                        operand(1u));
                const auto& input = *source->originalInput();
                const auto& layout = source->originalDestinationLease()->layout();
                semantics::TextureUploadLayoutValidation::Mapper mapper(input, layout);
                for (std::size_t i = 0u;
                    i < semantics::TextureUploadLayoutValidation::mipCount(input);
                    ++i) {
                    semantics::TextureUploadPackedMip mip;
                    if (!mapper(i, mip)) {
                        result.diagnostic = mapper.diagnostic();
                        break;
                    }
                }
                break;
            }
            case K::TextureReadback: {
                const auto& source =
                    std::get<std::shared_ptr<const OriginalCommandTextureReadbackSource>>(
                        operand(1u));
                const auto& original =
                    *source->originalSource()->originalRequest()->originalLayout();
                semantics::TextureReadbackLayoutValidation::Mapper mapper(
                    *source->originalInput(), original.sourceLayout());
                for (std::size_t i = 0u; i < semantics::TextureReadbackLayoutValidation::mipCount(
                                             *source->originalInput());
                    ++i) {
                    semantics::TextureReadbackMip mip;
                    if (!mapper(i, mip)) {
                        result.diagnostic = mapper.diagnostic();
                        break;
                    }
                    if (i >= original.mipCopies().size() || !mip.matches(original.mipCopies()[i])) {
                        result.diagnostic = "RHI-ORIGINAL-ACCESS-READBACK-LAYOUT-BINDING";
                        break;
                    }
                }
                break;
            }
            case K::BeginRenderPass:
                result.diagnostic =
                    validatePass(**std::get_if<std::shared_ptr<const RenderPassArguments>>(&value),
                        command.referenceCount,
                        operand);
                break;
            case K::BufferBarrier:
            case K::TextureBarrier:
            case K::BufferMemoryBarrier:
            case K::TextureMemoryBarrier: {
                const auto& dependency = std::get<ResourceDependencyArguments>(value);
                ResourceBarrierDesc transition;
                ResourceMemoryBarrierDesc memory;
                ResourceAccess source, destination;
                if (dependency.write(command.kind, transition)) {
                    source = transition.sourceAccess;
                    destination = transition.destinationAccess;
                } else if (dependency.write(command.kind, memory)) {
                    source = memory.sourceAccess;
                    destination = memory.destinationAccess;
                } else {
                    result.diagnostic = "RHI-ORIGINAL-ACCESS-DEPENDENCY";
                    break;
                }
                result.diagnostic = validateUsage(operand(0u), source | destination);
                break;
            }
            default:
                result.diagnostic = "RHI-ORIGINAL-ACCESS-COMMAND-UNPROVEN";
                break;
            }
            if (result.diagnostic)
                return result;
        }
        result.commandIndex = usage.commands().size();
        return result;
    }

private:
    static const GpuBuffer* buffer(const Source& source) noexcept
    {
        const auto* lease = std::get_if<std::shared_ptr<const OriginalCommandBufferLease>>(&source);
        return lease ? (*lease)->originalObject() : nullptr;
    }

    static const GpuTexture* texture(const Source& source) noexcept
    {
        return OriginalCommandTextureRanges::originalTexture(source);
    }

    static const char* validateUsage(const Source& source, ResourceAccess access) noexcept
    {
        if (const auto* b = buffer(source))
            return semantics::ResourceAccessUsageValidation::validate(b->desc(), access);
        if (const auto* t = texture(source))
            return semantics::ResourceAccessUsageValidation::validate(t->desc(), access);
        return "RHI-ORIGINAL-ACCESS-RESOURCE";
    }

    static const char* validateBufferCopy(const TransferCopyArguments& fields,
        const Source& source,
        const Source& destination) noexcept
    {
        const auto* s = buffer(source);
        const auto* d = buffer(destination);
        BufferCopyDesc copy;
        if (!s || !d || s == d || !fields.write(copy) || !copy.sizeBytes)
            return "RHI-ORIGINAL-ACCESS-BUFFER-COPY";
        constexpr auto alignment = OriginalCommandAccessContract::bufferCopyAlignment;
        if (copy.sourceOffsetBytes % alignment || copy.destinationOffsetBytes % alignment
            || copy.sizeBytes % alignment
            || !core::containsUnsignedRange<std::uint64_t>(
                0u, s->desc().sizeBytes, copy.sourceOffsetBytes, copy.sizeBytes)
            || !core::containsUnsignedRange<std::uint64_t>(
                0u, d->desc().sizeBytes, copy.destinationOffsetBytes, copy.sizeBytes))
            return "RHI-ORIGINAL-ACCESS-BUFFER-COPY-RANGE";
        if (const auto* id = validateUsage(source, ResourceAccess::TransferRead))
            return id;
        return validateUsage(destination, ResourceAccess::TransferWrite);
    }

    static bool validTexture(const TextureDesc& desc) noexcept
    {
        Extent3D extent;
        return !semantics::TextureSubresourceExtent::resolve(
                   desc.extent, desc.mipLevelCount, 0u, extent)
            && !(isBlockCompressed(desc.format) && desc.extent.depth > 1u) && desc.arrayLayerCount
            && (desc.extent.depth == 1u || (desc.arrayLayerCount == 1u && !desc.cubeCompatible))
            && (!desc.cubeCompatible
                || (desc.extent.width == desc.extent.height && !(desc.arrayLayerCount % 6u)));
    }

    template <class Desc>
    static const char* validateBufferTexture(const TransferCopyArguments& fields,
        const TransferCopyInputSnapshot* full,
        const Source& source,
        const Source& destination,
        bool upload) noexcept
    {
        const auto* b = buffer(upload ? source : destination);
        const auto* t = texture(upload ? destination : source);
        Desc copy;
        if (!b || !t || !fields.write(copy) || !validTexture(t->desc()))
            return "RHI-ORIGINAL-ACCESS-BUFFER-TEXTURE";
        auto range = copy.textureSubresources;
        const auto count = full && full->mipMappingCount() ? full->mipMappingCount() : 1u;
        if (range.mipLevelCount != count)
            return "RHI-ORIGINAL-ACCESS-COPY-MIP-COUNT";
        if (const auto* id = validateUsage(source, ResourceAccess::TransferRead))
            return id;
        if (const auto* id = validateUsage(destination, ResourceAccess::TransferWrite))
            return id;
        std::uint64_t previousEnd = 0u;
        for (std::size_t i = 0u; i < count; ++i) {
            typename Desc::MipLayout mip{range.baseMipLevel,
                copy.extent,
                copy.bytesPerRow,
                copy.rowsPerImage,
                copy.bufferOffsetBytes};
            if (full && full->mipMappingCount() && !full->writeMip(i, mip))
                return "RHI-ORIGINAL-ACCESS-COPY-MIP";
            if (mip.mipLevel != range.baseMipLevel + i)
                return "RHI-ORIGINAL-ACCESS-COPY-MIP-ORDER";
            if (!i
                && (mip.extent != copy.extent || mip.bytesPerRow != copy.bytesPerRow
                    || mip.rowsPerImage != copy.rowsPerImage
                    || mip.bufferOffsetBytes != copy.bufferOffsetBytes))
                return "RHI-ORIGINAL-ACCESS-COPY-FIRST-MIP";
            auto selected = range;
            selected.baseMipLevel = mip.mipLevel;
            selected.mipLevelCount = 1u;
            if (!semantics::TextureTransferValidation::copyBlockExtentValid(
                    t->desc(), selected, mip.extent)
                || !semantics::TextureTransferValidation::copyLayoutValid(t->desc(),
                    selected,
                    mip.extent,
                    mip.bytesPerRow,
                    mip.rowsPerImage,
                    mip.bufferOffsetBytes,
                    b->desc().sizeBytes))
                return "RHI-ORIGINAL-ACCESS-COPY-TEXTURE-LAYOUT";
            // Canonical whole padded mip footprints cannot overlap or exceed
            // the declared Buffer. Checked strided products own all arithmetic.
            const auto slices = semantics::TextureTransferValidation::copySliceCount(
                t->desc(), selected, mip.extent);
            std::uint64_t rows = 0u, bytes = 0u;
            if (!core::StridedIndexRange(
                    0u, static_cast<std::uint64_t>(slices) + 1u, mip.rowsPerImage)
                    .index(slices, rows)
                || rows == UINT64_MAX
                || !core::StridedIndexRange(0u, rows + 1u, mip.bytesPerRow).index(rows, bytes)
                || !core::containsUnsignedRange<std::uint64_t>(
                    0u, b->desc().sizeBytes, mip.bufferOffsetBytes, bytes))
                return "RHI-ORIGINAL-ACCESS-COPY-FOOTPRINT";
            if (mip.bufferOffsetBytes < previousEnd)
                return "RHI-ORIGINAL-ACCESS-COPY-MIP-OVERLAP";
            previousEnd = mip.bufferOffsetBytes + bytes;
        }
        return nullptr;
    }

    static const char* validateTextureCopy(const TransferCopyArguments& fields,
        const TransferCopyInputSnapshot* full,
        const Source& source,
        const Source& destination) noexcept
    {
        const auto* s = texture(source);
        const auto* d = texture(destination);
        TextureToTextureCopyDesc copy;
        if (!s || !d || s == d || !fields.write(copy) || s->desc().format != d->desc().format
            || copy.sourceSubresources.aspect != copy.destinationSubresources.aspect
            || !validTexture(s->desc()) || !validTexture(d->desc()))
            return "RHI-ORIGINAL-ACCESS-TEXTURE-COPY";
        if (const auto* id = validateUsage(source, ResourceAccess::TransferRead))
            return id;
        if (const auto* id = validateUsage(destination, ResourceAccess::TransferWrite))
            return id;
        const auto count = full && full->mipMappingCount() ? full->mipMappingCount() : 1u;
        if (copy.sourceSubresources.mipLevelCount != count
            || copy.destinationSubresources.mipLevelCount != count
            || copy.sourceSubresources.arrayLayerCount
                != copy.destinationSubresources.arrayLayerCount)
            return "RHI-ORIGINAL-ACCESS-COPY-MIP-COUNT";
        for (std::size_t i = 0u; i < count; ++i) {
            TextureToTextureCopyDesc::MipLayout mip{
                copy.sourceSubresources.baseMipLevel, copy.extent};
            if (full && full->mipMappingCount() && !full->writeMip(i, mip))
                return "RHI-ORIGINAL-ACCESS-COPY-MIP";
            if (mip.mipLevel != copy.sourceSubresources.baseMipLevel + i)
                return "RHI-ORIGINAL-ACCESS-COPY-MIP-ORDER";
            if (!i && mip.extent != copy.extent)
                return "RHI-ORIGINAL-ACCESS-COPY-FIRST-MIP";
            auto sr = copy.sourceSubresources, dr = copy.destinationSubresources;
            sr.baseMipLevel = mip.mipLevel;
            dr.baseMipLevel += static_cast<std::uint32_t>(i);
            sr.mipLevelCount = dr.mipLevelCount = 1u;
            if (!semantics::TextureTransferValidation::copyBlockExtentValid(
                    s->desc(), sr, mip.extent)
                || !semantics::TextureTransferValidation::copyBlockExtentValid(
                    d->desc(), dr, mip.extent))
                return "RHI-ORIGINAL-ACCESS-COPY-TEXTURE-EXTENT";
        }
        return nullptr;
    }

    static const char* validateCopy(
        const Inputs::Descriptor& value, const Source& source, const Source& destination) noexcept
    {
        const auto* fields = std::get_if<TransferCopyArguments>(&value);
        const TransferCopyInputSnapshot* full = nullptr;
        if (const auto* owner =
                std::get_if<std::shared_ptr<const TransferCopyInputSnapshot>>(&value)) {
            full = owner->get();
            fields = &full->scalarFields();
        }
        if (!fields)
            return "RHI-ORIGINAL-ACCESS-COPY";
        switch (fields->kind()) {
        case K::BufferCopy:
            return validateBufferCopy(*fields, source, destination);
        case K::BufferToTextureCopy:
            return validateBufferTexture<BufferToTextureCopyDesc>(
                *fields, full, source, destination, true);
        case K::TextureToBufferCopy:
            return validateBufferTexture<TextureToBufferCopyDesc>(
                *fields, full, source, destination, false);
        case K::TextureToTextureCopy:
            return validateTextureCopy(*fields, full, source, destination);
        default:
            return "RHI-ORIGINAL-ACCESS-COPY";
        }
    }

    template <class Operand>
    static const char* validatePass(
        const RenderPassArguments& pass, std::size_t count, Operand&& operand) noexcept
    {
        const auto& desc = pass.desc();
        if (desc.layeredRendering)
            return "RHI-ORIGINAL-ACCESS-LAYERED-PASS-UNPROVEN";
        for (const auto& color : desc.colorAttachments)
            if (!semantics::validAttachmentLoadOp(color.loadOp)
                || !semantics::validAttachmentStoreOp(color.storeOp)
                || !std::isfinite(color.clearColor.r) || !std::isfinite(color.clearColor.g)
                || !std::isfinite(color.clearColor.b) || !std::isfinite(color.clearColor.a))
                return "RHI-ORIGINAL-ACCESS-COLOR-ATTACHMENT";
        if (desc.hasDepthStencilAttachment)
            if (const auto* id = semantics::DepthStencilAttachmentInputContract::diagnostic(
                    desc.depthStencilAttachment))
                return id;
        Extent3D commonExtent{0u, 0u, 0u};
        std::uint32_t commonSamples = 0u;
        std::size_t index = 0u;
        const auto view = [&](GpuResourceId target, bool resolve) -> const char* {
            if (index >= count)
                return "RHI-ORIGINAL-ACCESS-PASS-REFERENCE";
            const auto* source =
                std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(
                    &operand(index++));
            if (!source || (*source)->resource() != target)
                return "RHI-ORIGINAL-ACCESS-PASS-REFERENCE";
            const auto& v = (*source)->originalViewLease()->layout();
            const auto& parent = (*source)->originalParentLease()->layout();
            if (v.mipLevelCount != 1u || v.arrayLayerCount != 1u || parent.extent.depth != 1u
                || (v.dimension != TextureViewDimension::Texture2D
                    && v.dimension != TextureViewDimension::Texture2DArray)
                || v.format != parent.format)
                return "RHI-ORIGINAL-ACCESS-PASS-VIEW";
            Extent3D extent;
            if (const auto* id = semantics::TextureSubresourceExtent::resolve(
                    parent.extent, parent.mipLevelCount, v.baseMipLevel, extent))
                return id;
            if (!commonSamples) {
                commonExtent = extent;
                commonSamples = parent.sampleCount;
            }
            if (!commonSamples || extent != commonExtent
                || (resolve ? parent.sampleCount != 1u || commonSamples == 1u
                            : parent.sampleCount != commonSamples))
                return "RHI-ORIGINAL-ACCESS-PASS-EXTENT-SAMPLES";
            for (std::size_t previous = 0u; previous + 1u < index; ++previous) {
                const auto& other =
                    std::get<std::shared_ptr<const OriginalCommandTextureViewSource>>(
                        operand(previous));
                const auto& ov = other->originalViewLease()->layout();
                if (core::SharedOwnerProjection::sameOwner(
                        other->originalParentLease(), (*source)->originalParentLease())
                    && ov.baseMipLevel == v.baseMipLevel && ov.baseArrayLayer == v.baseArrayLayer
                    && ov.aspect == v.aspect)
                    return "RHI-ORIGINAL-ACCESS-PASS-OVERLAP";
            }
            return nullptr;
        };
        for (const auto& color : desc.colorAttachments) {
            if (const auto* id = view(color.target, false))
                return id;
            if (color.hasResolveTarget)
                if (const auto* id = view(color.resolveTarget, true))
                    return id;
        }
        if (desc.hasDepthStencilAttachment)
            if (const auto* id = view(desc.depthStencilAttachment.target, false))
                return id;
        return index == count ? nullptr : "RHI-ORIGINAL-ACCESS-PASS-REFERENCE";
    }
};
} // namespace arp::rhi::common

// endregion Command access validation

// region Command state transaction

// region original_command_state_transaction

namespace arp::rhi {
// Canonical retained CPU transaction root. Optional Prototype state planning
// never grants native recording, submission admission or completion.
class OriginalCommandStreamProgram;

inline const char* OriginalCommandResourceStates::validateSubmittedReadAccesses() const noexcept
{
    if (!reads_)
        return nullptr;
    if (budget_.contractVersion >= 2u) {
        const auto bindings = Consumption::renderBindingsOriginal<false, true>(
            inputs_, readAdmission_->allBindings, [&](const auto& access) noexcept -> const char* {
                return validateSubmittedShaderAccess(access);
            });
        if (bindings.diagnostic)
            return bindings.diagnostic;
    }
    return common::OriginalCommandStateSequence::inspectValidatedGraph(
        *this, readAdmission_->sequence)
        .diagnostic;
}

namespace common {
class OriginalTypedUsageProjection;
class OriginalTypedDependencyProjection;
} // namespace common

class OriginalCommandStateTransaction final : public core::QuotaBoundSharedObjectIdentity {
    friend class rhi_vulkan::VulkanRhiNativeCommandPayload;
    friend class rhi_metal::MetalOriginalOrdinaryCommand;
    friend class rhi_metal::MetalRhiNativeCommandPayload;
    friend class OriginalCommandRecordingSeal;
    friend class OriginalGeneralOutputSnapshotContract;
    using Graph = OriginalCommandResourceStates;
    using Scalar = common::RecordedResourceStateControl;
    using Texture = common::RecordedTextureStateControl;
    using Projection = common::OriginalCommandTextureRanges;
    using Content = common::OriginalTextureContentControl;

    static const char* preflightOrdinaryInitializedBuffer(
        const GpuBuffer& object, core::MutationJournal& journal) noexcept
    {
        const auto* control = object.originalStateControl_;
        return object.hostWritesSealed() && control && control->active()
                && control->canPrepare(journal)
            ? nullptr
            : "RHI-ORDINARY-MIXED-INITIALIZED-BUFFER-STATE";
    }

    static const char* claimOrdinaryInitializedBuffer(
        const GpuBuffer& object, core::MutationJournal& journal) noexcept
    {
        if (const auto* id = preflightOrdinaryInitializedBuffer(object, journal))
            return id;
        return object.originalStateControl_->prepare(journal)
            ? nullptr
            : "RHI-ORDINARY-MIXED-INITIALIZED-BUFFER-CLAIM";
    }

    static bool ordinaryInitializedBufferPending(
        const GpuBuffer& object, const core::MutationJournal& journal) noexcept
    {
        const auto* control = object.originalStateControl_;
        return control && bool(control->observePending(journal));
    }

    static bool ordinaryBufferState(
        const GpuBuffer& object, ResourceState state, const core::MutationJournal& journal) noexcept
    {
        const auto* control = object.originalStateControl_;
        const auto pending = control ? control->observePending(journal) : std::nullopt;
        return pending && pending->value() == state;
    }

    static const char* preflightOrdinaryImmutableBuffer(
        const GpuBuffer& object, core::MutationJournal& journal) noexcept
    {
        const auto* control = object.originalStateControl_;
        if (!object.hostWritesSealed() || !object.gpuWritesSealed()
            || !object.gpuTransferWritesSealed() || !control || !control->active()
            || control->state() != ResourceState::ShaderRead || !control->canPrepare(journal))
            return "RHI-ORDINARY-GRAPHICS-IMMUTABLE-BUFFER-STATE";
        return nullptr;
    }

    static const char* claimOrdinaryImmutableBuffer(
        const GpuBuffer& object, core::MutationJournal& journal) noexcept
    {
        if (const auto* id = preflightOrdinaryImmutableBuffer(object, journal))
            return id;
        return object.originalStateControl_->prepare(journal)
            ? nullptr
            : "RHI-ORDINARY-GRAPHICS-IMMUTABLE-BUFFER-CLAIM";
    }

    static bool ordinaryImmutableBufferPending(
        const GpuBuffer& object, const core::MutationJournal& journal) noexcept
    {
        const auto* control = object.originalStateControl_;
        const auto pending = control ? control->observePending(journal) : std::nullopt;
        return pending && pending->value() == ResourceState::ShaderRead;
    }

    static const char* preflightOrdinaryAttachment(const GpuTexture& object,
        const GpuTexture::ContentRangeExpectation& initial,
        core::MutationJournal& journal) noexcept
    {
        if (const auto* id = object.validateOriginalContent(initial))
            return id;
        auto& control = *object.originalContent_;
        if (!OriginalOrdinaryAttachmentContentContract::matchesControl(initial.controlVersion())
            || !control.geometry_ || control.geometry_->rangeCellCount(initial.range()) != 1u)
            return "RHI-ORDINARY-ATTACHMENT-CONTENT-CONTROL";
        return control.canProposeCells(
            journal, 1u, OriginalOrdinaryAttachmentContentContract::mutationVersion);
    }

    static const char* proposeOrdinaryAttachment(const GpuTexture& object,
        const GpuTexture::ContentRangeExpectation& initial,
        bool stored,
        core::MutationJournal& journal,
        Content::Value& proposed,
        std::optional<Content::Cells::PendingObservation>& pending) noexcept
    {
        if (const auto* id = preflightOrdinaryAttachment(object, initial, journal))
            return id;
        auto& control = *object.originalContent_;
        std::uint64_t cell = 0u;
        if (!control.geometry_->cellIndex(initial.range(), 0u, cell))
            return "RHI-ORDINARY-ATTACHMENT-CONTENT-CELL";
        if (const auto* id = control.proposeCell(journal,
                cell,
                stored,
                proposed,
                OriginalOrdinaryAttachmentContentContract::mutationVersion))
            return id;
        pending = control.cells_->observePending(journal);
        return pending ? nullptr : "RHI-ORDINARY-ATTACHMENT-CONTENT-PENDING";
    }

    static const char* validateOrdinaryAttachmentPending(const GpuTexture& object,
        const GpuTexture::ContentRangeExpectation& initial,
        core::MutationJournal& journal,
        const Content::Value& proposed,
        const Content::Cells::PendingObservation& pending) noexcept
    {
        if (const auto* id = object.validateOriginalContentIdentity(initial))
            return id;
        const auto& control = *object.originalContent_;
        const auto current = control.uniformRange(initial.range());
        if (!OriginalOrdinaryAttachmentContentContract::matchesControl(control.contractVersion())
            || !control.cells_ || !control.cells_->matchesPending(pending, journal)
            || !proposed.generation || !current || !(*current == proposed))
            return "RHI-ORDINARY-ATTACHMENT-CONTENT-PENDING";
        return nullptr;
    }

    static const char* completeOrdinaryAttachment(const GpuTexture& object,
        const GpuTexture::ContentRangeExpectation& initial,
        core::MutationJournal& journal,
        const Content::Value& proposed,
        const Content::Cells::PendingObservation& pending) noexcept
    {
        if (const auto* id =
                validateOrdinaryAttachmentPending(object, initial, journal, proposed, pending))
            return id;
        auto& control = *object.originalContent_;
        std::uint64_t cell = 0u;
        Content::Value completed;
        if (!control.geometry_->cellIndex(initial.range(), 0u, cell))
            return "RHI-ORDINARY-ATTACHMENT-CONTENT-CELL";
        return control.completeCell(journal,
            cell,
            proposed,
            completed,
            OriginalOrdinaryAttachmentContentContract::mutationVersion);
    }

    static bool ordinaryAttachmentState(const GpuTexture& object,
        const TextureSubresourceRange& range,
        ResourceState state,
        const core::MutationJournal& journal) noexcept
    {
        const auto* control = object.originalNormalizedState_;
        if (!control || !control->active() || control->geometry().validate(range))
            return false;
        if (control->pending()) {
            const auto pending = control->observePending(range, journal);
            return pending && pending->state() == state;
        }
        const auto initial = control->observe(range);
        return initial && initial->state == state;
    }

    static const char* transitionOrdinaryAttachment(const GpuTexture& object,
        const ResourceBarrierDesc& barrier,
        core::MutationJournal& journal) noexcept
    {
        if (barrier.resource != object.id()
            || !ordinaryAttachmentState(
                object, barrier.textureSubresources, barrier.before, journal))
            return "RHI-ORDINARY-ATTACHMENT-BARRIER-STATE";
        return object.originalNormalizedState_->write(
            barrier.textureSubresources, barrier.before, barrier.after, &journal);
    }

    // The approved ordinary issuer uses these existing controls with its one
    // external journal. No root, plan, second journal or receipt is constructed.
    static const char* preflightOrdinaryWholeUpload(const GpuTexture& object,
        const GpuTexture::ContentExpectation& initial,
        core::MutationJournal& journal) noexcept
    {
        if (const auto* id = object.validateOriginalContent(initial))
            return id;
        if (const auto* id = OriginalUndefinedTextureUploadPlanContract::validateInitial(
                initial.controlVersion(),
                initial.state() == Content::State::Uninitialized,
                initial.contentGeneration(),
                ResourceState::Undefined))
            return id;
        return object.originalContent_->canUpload(journal, 1u, 7u);
    }

    static const char* proposeOrdinaryWholeUpload(const GpuTexture& object,
        const GpuTexture::ContentExpectation& initial,
        core::MutationJournal& journal,
        std::optional<Content::PendingObservation>& pending) noexcept
    {
        if (const auto* id = preflightOrdinaryWholeUpload(object, initial, journal))
            return id;
        std::uint64_t generation = 0u;
        if (const auto* id = object.originalContent_->propose(journal, generation, 7u))
            return id;
        pending = object.originalContent_->value_.observePending(journal);
        return pending && pending->value().generation == generation
            ? nullptr
            : "RHI-ORDINARY-TEXTURE-UPLOAD-CONTENT-PENDING";
    }

    static const char* validateOrdinaryWholeUploadPending(const GpuTexture& object,
        core::MutationJournal& journal,
        const Content::PendingObservation& pending) noexcept
    {
        if (!object.originalContent_ || object.originalContent_->contractVersion() != 3u)
            return "RHI-ORDINARY-TEXTURE-UPLOAD-CONTENT-CONTROL";
        if (const auto* id = object.originalContent_->identityDiagnostic())
            return id;
        return pending.value().state == Content::State::Proposed && pending.value().generation
                && object.originalContent_->value_.matchesPending(pending, journal)
            ? nullptr
            : "RHI-ORDINARY-TEXTURE-UPLOAD-CONTENT-PENDING";
    }

    static const char* completeOrdinaryWholeUpload(const GpuTexture& object,
        core::MutationJournal& journal,
        const Content::PendingObservation& pending) noexcept
    {
        if (const auto* id = validateOrdinaryWholeUploadPending(object, journal, pending))
            return id;
        return object.originalContent_->complete(journal, 7u);
    }

    class ConstructionKey {
        friend class OriginalCommandStateTransaction;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

    struct BufferEntry {
        const GpuBuffer* object;
        Scalar* control;
        std::optional<Scalar::PendingObservation> pending;
        std::optional<OriginalSubmittedBufferReadPins::Observation> borrowed;
    };

    struct TextureEntry {
        const GpuTexture* object;
        Scalar* uniform;
        Texture* normalized;
        TextureSubresourceRange range;
        std::optional<Scalar::PendingObservation> uniformPending;
        std::optional<Texture::PendingObservation> normalizedPending;
    };

    struct FinalCell {
        const GpuBuffer* buffer = nullptr;
        const GpuTexture* texture = nullptr;
        Scalar* scalar = nullptr;
        Texture* normalized = nullptr;
        TextureSubresourceRange range{};
        ResourceState initial = ResourceState::Undefined, final = ResourceState::Undefined;
        std::optional<Scalar::PendingObservation> scalarPending;
        std::optional<Texture::PendingObservation> texturePending;
        std::optional<Scalar::Observation> scalarCommitted;
        std::optional<Texture::Observation> textureCommitted;
        std::optional<OriginalSubmittedBufferReadPins::Observation> borrowed;
    };

    struct AccessStep {
        std::size_t command, cell;
        ResourceState before, after;
    };

    struct Transition {
        std::size_t command;
        Scalar* scalar;
        Texture* normalized;
        bool buffer;
        ResourceBarrierDesc barrier;
    };

    struct ContentCell {
        const GpuTexture* object;
        Content* control;
        Content::Observation initial;
        std::uint64_t writes = 0u;
        std::optional<Content::PendingObservation> pending;
        std::optional<Content::Observation> committed;
    };

    struct ContentUpload {
        std::size_t command, cell;
        std::uint64_t generation = 0u;
    };

    struct RangeContentControl {
        const GpuTexture* object;
        Content* control;
        Content::Cells::Observation initial;
        std::uint64_t mutations = 0u;
        std::optional<Content::Cells::PendingObservation> pending;
        std::optional<Content::Cells::Observation> committed;
    };

    struct RangeContentCell {
        std::size_t controlIndex;
        std::uint64_t cellIndex;
        Content::Value initial, proposed;
        bool stored, written;
        std::optional<Content::Value> completed;
    };

    struct Plan {
        OriginalCommandStateSequenceBudget budget;
        core::BoundedSequence<FinalCell> cells;
        core::BoundedSequence<AccessStep> steps;
        core::BoundedSequence<Transition> transitions;
        core::BoundedSequence<ContentCell> contents;
        core::BoundedSequence<ContentUpload> uploads;
        const OriginalTextureContentPlanBudget contentBudget;
        const OriginalFiniteGraphicsContentPlanBudget mixedBudget;
        OriginalFiniteGraphicsContentPlanContract::Counts mixedCounts;
        const OriginalBindlessGraphicsDepthContentPlanContract::Budget depthBudget;
        OriginalBindlessGraphicsDepthContentPlanContract::Counts depthCounts;
        std::shared_ptr<const OriginalCommandStreamProgram> graphicsStream;
        const char* (*validateGraphicsStream)(
            const std::shared_ptr<const OriginalCommandStreamProgram>&,
            const std::shared_ptr<const Graph>&,
            bool) noexcept = nullptr;
        core::BoundedSequence<RangeContentControl> rangeContents;
        core::BoundedSequence<RangeContentCell> rangeCells;

        Plan(const OriginalCommandStateSequenceBudget& b,
            std::size_t cellCount,
            std::size_t stepCount,
            std::size_t commands,
            const OriginalTextureContentPlanBudget& content,
            const OriginalFiniteGraphicsContentPlanBudget& mixed = {},
            const OriginalFiniteGraphicsContentPlanContract::Counts& mixedActual = {},
            const OriginalBindlessGraphicsDepthContentPlanContract::Budget& depth = {},
            const OriginalBindlessGraphicsDepthContentPlanContract::Counts& depthActual = {})
            : budget(b), cells(cellCount), steps(stepCount), transitions(commands),
              contents(OriginalFiniteGraphicsContentPlanContract::isMixed(mixed.contractVersion)
                      ? static_cast<std::size_t>(mixedActual.completedControls)
                      : content.contractVersion == 1u || content.contractVersion == 7u
                          || OriginalCompletedTextureReadPlanContract::isReadOnly(
                              content.contractVersion)
                      ? content.maxContentControls
                      : 0u),
              uploads(content.contractVersion == 1u || content.contractVersion == 7u
                      ? content.maxUploadCommands
                      : 0u),
              contentBudget(content), mixedBudget(mixed), depthBudget(depth),
              depthCounts(depthActual),
              rangeContents(
                  OriginalFiniteGraphicsContentPlanContract::isMixed(mixed.contractVersion)
                      ? static_cast<std::size_t>(mixedActual.attachmentControls
                            + depthActual.depthControls + mixedActual.producedImageControls)
                      : content.contractVersion == 2u || content.contractVersion == 3u
                          || OriginalCompletedDepthRootReadPlanContract::isVersion(
                              content.contractVersion)
                          || OriginalStorageTextureContentPlanContract::isVersion(
                              content.contractVersion)
                      ? content.maxContentControls
                      : 0u),
              rangeCells(OriginalFiniteGraphicsContentPlanContract::isMixed(mixed.contractVersion)
                      ? static_cast<std::size_t>(mixedActual.attachmentCells
                            + depthActual.depthCells + mixedActual.producedImageCells)
                      : content.contractVersion == 2u || content.contractVersion == 3u
                          || OriginalCompletedDepthRootReadPlanContract::isVersion(
                              content.contractVersion)
                          || OriginalStorageTextureContentPlanContract::isVersion(
                              content.contractVersion)
                      ? static_cast<std::size_t>(content.maxContentCells)
                      : 0u)
        {}

        template <class Timeline>
        const char* appendRangeCell(const GpuTexture& object,
            const Content::Cells::Observation& stamp,
            std::uint64_t index,
            const Content::Value& initial,
            const Timeline& timeline) noexcept
        {
            auto* control = rangeContents.findIf([&](const auto& c) {
                return c.object == &object;
            });
            if (!control) {
                if (!rangeContents.append({&object, object.originalContent_.get(), stamp, 0u, {}}))
                    return "RHI-CANONICAL-CONTENT-PLAN-CONTROL-CAPACITY";
                control = rangeContents.findIf([&](const auto& c) {
                    return c.object == &object;
                });
            }
            const auto controlIndex =
                static_cast<std::size_t>(control - rangeContents.values().data());
            const bool written = timeline.writes != 0u;
            if (written)
                rangeContents.visitMutable([&](auto& c) noexcept {
                    if (c.object == &object)
                        ++c.mutations;
                });
            return rangeCells.append(
                       {controlIndex, index, initial, initial, timeline.stored, written})
                ? nullptr
                : "RHI-ORIGINAL-CONTENT-PLAN-CELL-CAPACITY";
        }
    };

public:
    enum class Status { Prepared, Pending, Committed, Abandoned, Invalidated };

    struct ContentCellsInspection {
        const char* diagnostic = nullptr;
        std::uint64_t controls = 0u, cells = 0u, steps = 0u, commandVisits = 0u,
                      projectionVisits = 0u;
        std::uint64_t readbackCommands = 0u;

        bool nativeExecutionProven() const noexcept { return false; }
    };

    OriginalCommandStateTransaction(ConstructionKey,
        std::shared_ptr<const Graph> graph,
        const OriginalCommandStateAdmissionBudget& budget,
        std::uint32_t owners,
        std::uint32_t controls)
        : graph_(std::move(graph)), budget_(budget), owners_(owners), controls_(controls),
          countLease_(countDomain(graph_), 1u), buffers_(graph_->bufferEntries().size()),
          textures_(graph_->textureEntries().size())
    {}

    ~OriginalCommandStateTransaction() { rollbackOriginalOwnership(); }

    static std::shared_ptr<OriginalCommandStateTransaction> prepareOriginalOwnership(
        std::shared_ptr<const Graph> graph,
        const GpuDevice& device,
        const OriginalCommandStateAdmissionBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        return prepareOriginalOwner(
            std::move(graph), device, budget, std::move(quota), diagnostic, false);
    }

    // Finite CPU admission is atomic: the same original owner is published only
    // after its complete immutable StateSequence5/ContentPlan5 materializes.
    static std::shared_ptr<OriginalCommandStateTransaction> prepareOriginalOwnership(
        std::shared_ptr<const Graph> graph,
        const GpuDevice& device,
        const OriginalCommandStateAdmissionBudget& budget,
        const OriginalCommandStateSequenceBudget& sequence,
        const OriginalTextureContentPlanBudget& content,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        diagnostic = "RHI-ORIGINAL-FINITE-TRANSACTION-CONTENT-PROFILE";
        if (budget.contractVersion != 5u || sequence.contractVersion != 5u
            || content.contractVersion != 5u)
            return {};
        if ((diagnostic = OriginalCommandStateSequenceContract::validateIntent(sequence))
            || (diagnostic = OriginalCompletedTextureReadPlanContract::validateIntent(content)))
            return {};
        auto result = prepareOriginalOwner(
            std::move(graph), device, budget, std::move(quota), diagnostic, true);
        if (!result)
            return {};
        if ((diagnostic = result->materializeOriginalPrototypePlan(sequence, content))
            || (diagnostic = result->validate()))
            return {};
        return result;
    }

    // Exact-type deferred composition avoids the State -> Stream -> Usage cycle.
    // The supplied projection types must be the original implementations.
    template <class Usage, class Dependencies, class Stream>
    static std::shared_ptr<OriginalCommandStateTransaction> prepareOriginalGraphicsOwnership(
        std::shared_ptr<const Graph> graph,
        std::shared_ptr<const Stream> stream,
        const GpuDevice& device,
        const OriginalCommandStateAdmissionBudget& budget,
        const OriginalCommandStateSequenceBudget& sequence,
        const OriginalFiniteGraphicsContentPlanBudget& content,
        const typename Dependencies::Request& dependencies,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        static_assert(std::is_same_v<Usage, common::OriginalTypedUsageProjection>
            && std::is_same_v<Dependencies, common::OriginalTypedDependencyProjection>
            && std::is_same_v<Stream, OriginalCommandStreamProgram>);
        diagnostic = OriginalFiniteGraphicsContentPlanContract::validateIntent(content);
        if (diagnostic)
            return {};
        const auto lowering = OriginalFiniteGraphicsContentPlanContract::lowering(device.backend());
        if (!lowering.rootAvailable) {
            diagnostic = lowering.diagnostic;
            return {};
        }
        if ((diagnostic = Graph::validateOriginal(graph))
            || (diagnostic = Stream::validateOriginal(stream))
            || (diagnostic = stream->compareOriginalStates(graph)))
            return {};
        const auto& authored = stream->originalBudget().stateSequence;
        const auto sameSequence = [](const auto& a, const auto& b) noexcept {
            return std::tie(a.contractVersion,
                       a.prototypeOptIn,
                       a.maxCommands,
                       a.maxDebugDepth,
                       a.maxStateCells,
                       a.maxSteps,
                       a.maxImplicitAccesses)
                == std::tie(b.contractVersion,
                    b.prototypeOptIn,
                    b.maxCommands,
                    b.maxDebugDepth,
                    b.maxStateCells,
                    b.maxSteps,
                    b.maxImplicitAccesses);
        };
        diagnostic = "RHI-FINITE-GRAPHICS-CONTENT-STREAM-PROFILE";
        if (budget.contractVersion != 5u || graph->stateContractVersion() != 5u
            || stream->streamContractVersion() != 5u || !sameSequence(sequence, authored)
            || dependencies.contractVersion != 6u || dependencies.usages.contractVersion != 6u
            || dependencies.usages.projection.contractVersion != 6u)
            return {};
        const auto& source = sources(*graph);
        const auto& original = source.sources();
        std::size_t pipelineIndex = SIZE_MAX;
        const auto usages = Usage::visitStateBound(stream,
            graph,
            dependencies.usages,
            [&](const typename Usage::Point& point) noexcept -> const char* {
                return std::visit(
                    [&](const auto& value) noexcept -> const char* {
                        using Point = std::decay_t<decltype(value)>;
                        if constexpr (std::is_same_v<Point, typename Usage::BufferPoint>
                            || std::is_same_v<Point, typename Usage::TexturePoint>) {
                            const auto index = value.original.pipelineSourceIndex;
                            using ScopeKind = std::decay_t<decltype(value.use.scope.kind)>;
                            if (index >= original.size()
                                || value.use.scope.kind != ScopeKind::RenderPass
                                || value.use.access.stages != PipelineStage::FragmentShader)
                                return "RHI-FINITE-GRAPHICS-CONTENT-PIPELINE-ORIGIN";
                            const auto* pipeline = std::get_if<
                                std::shared_ptr<const OriginalCommandGraphicsPipelineSource>>(
                                &original[index]);
                            if (!pipeline || !*pipeline
                                || (pipelineIndex != SIZE_MAX && pipelineIndex != index))
                                return "RHI-FINITE-GRAPHICS-CONTENT-PIPELINE-IDENTITY";
                            pipelineIndex = index;
                            return nullptr;
                        } else if constexpr (std::is_same_v<Point,
                                                 typename Usage::AttachmentPoint>) {
                            return nullptr;
                        } else {
                            return "RHI-FINITE-GRAPHICS-CONTENT-UNSUPPORTED-COMPOSITION";
                        }
                    },
                    point);
            });
        if ((diagnostic = usages.diagnostic))
            return {};
        diagnostic = "RHI-FINITE-GRAPHICS-CONTENT-USAGE-INCOMPLETE";
        if (!usages.counts.renderBindings || !usages.counts.shaderConsumptions
            || !usages.counts.attachments || pipelineIndex == SIZE_MAX)
            return {};
        const OriginalFiniteGraphicsContentPlanContract::Profile profile{source.assemblyVersion_,
            graph->originalInputs()->originalBudget().contractVersion,
            graph->stateContractVersion(),
            authored.contractVersion,
            dependencies.usages.projection.contractVersion,
            dependencies.contractVersion,
            budget.descriptorConsumption.contractVersion,
            budget.renderDescriptorUsage.contractVersion,
            budget.descriptorConsumption.generalShaderResourcesVersion,
            budget.descriptorConsumption.generalShaderResourcesOptIn
                && budget.renderDescriptorUsage.generalShaderResourcesOptIn,
            true,
            false};
        if ((diagnostic = OriginalFiniteGraphicsContentPlanContract::validateRootProfile(profile)))
            return {};
        const auto projected = Dependencies::inspect(stream, graph, dependencies);
        if ((diagnostic = projected.diagnostic))
            return {};
        diagnostic = "RHI-FINITE-GRAPHICS-CONTENT-DEPENDENCY-INCOMPLETE";
        if (!projected.counts.dependencies)
            return {};
        diagnostic = "RHI-FINITE-GRAPHICS-CONTENT-STREAM-QUOTA-DOMAIN";
        for (const auto& other : {quota,
                 source.originalMetadataQuota(),
                 graph->originalMetadataQuota(),
                 graph->originalInputs()->originalMetadataQuota()})
            if (core::SharedOwnerProjection::sameControlBlock(
                    stream->originalMetadataQuota(), other))
                return {};
        auto result = prepareOriginalOwner(
            std::move(graph), device, budget, std::move(quota), diagnostic, true);
        if (!result)
            return {};
        if ((diagnostic = result->materializeOriginalGraphicsPlan(
                 sequence, content, stream->originalMetadataQuota())))
            return {};
        result->plan_->graphicsStream = std::move(stream);
        result->plan_->validateGraphicsStream =
            [](const auto& owner, const auto& state, bool prepared) noexcept -> const char* {
            if (!owner || !owner->hasQuotaBoundSharedOwner(owner)
                || !core::SharedOwnerProjection::sameOwner(
                    owner->originalInputs(), state->originalInputs()))
                return "RHI-FINITE-GRAPHICS-CONTENT-STREAM-OWNER";
            return prepared ? owner->compareOriginalStates(state) : nullptr;
        };
        if ((diagnostic = result->validate()))
            return {};
        return result;
    }

    // Root /4 composition retains the exact original stream and genuine
    // prepared Graphics owner. General /6 authorization cannot enter here.
    template <class Stream>
    static std::shared_ptr<OriginalCommandStateTransaction>
    prepareOriginalBindlessGraphicsOwnership(std::shared_ptr<const Graph> graph,
        std::shared_ptr<const Stream> stream,
        const GpuDevice& device,
        const OriginalCommandStateAdmissionBudget& budget,
        const OriginalCommandStateSequenceBudget& sequence,
        const OriginalFiniteGraphicsContentPlanBudget& content,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        return prepareOriginalGraphicsOwnership(std::move(graph),
            std::move(stream),
            device,
            budget,
            sequence,
            content,
            std::move(quota),
            diagnostic,
            nullptr);
    }

    template <class Stream>
    static std::shared_ptr<OriginalCommandStateTransaction>
    prepareOriginalBindlessGraphicsOwnership(std::shared_ptr<const Graph> graph,
        std::shared_ptr<const Stream> stream,
        const GpuDevice& device,
        const OriginalCommandStateAdmissionBudget& budget,
        const OriginalCommandStateSequenceBudget& sequence,
        const OriginalBindlessGraphicsDepthContentPlanContract::Budget& content,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        using Contract = OriginalBindlessGraphicsDepthContentPlanContract;
        if ((diagnostic = Contract::validateIntent(content)))
            return {};
        const auto lowering = Contract::lowering(device.backend(), content.contractVersion);
        if (!lowering.rootAvailable) {
            diagnostic = lowering.diagnostic;
            return {};
        }
        return prepareOriginalGraphicsOwnership(std::move(graph),
            std::move(stream),
            device,
            budget,
            sequence,
            content.graphics,
            std::move(quota),
            diagnostic,
            &content);
    }

private:
    template <class Stream>
    static std::shared_ptr<OriginalCommandStateTransaction> prepareOriginalGraphicsOwnership(
        std::shared_ptr<const Graph> graph,
        std::shared_ptr<const Stream> stream,
        const GpuDevice& device,
        const OriginalCommandStateAdmissionBudget& budget,
        const OriginalCommandStateSequenceBudget& sequence,
        const OriginalFiniteGraphicsContentPlanBudget& content,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic,
        const OriginalBindlessGraphicsDepthContentPlanContract::Budget* depth)
    {
        static_assert(std::is_same_v<Stream, OriginalCommandStreamProgram>);
        using Contract = OriginalBindlessGraphicsContentPlanContract;
        if ((diagnostic = Contract::validateIntent(content)))
            return {};
        const auto lowering = Contract::lowering(device.backend(),
            content.bindlessGeometryContractVersion,
            content.attachmentClear.contractVersion,
            content.bindlessCompletedReadContractVersion);
        if (!lowering.rootAvailable) {
            diagnostic = lowering.diagnostic;
            return {};
        }
        if ((diagnostic = Graph::validateOriginal(graph))
            || (diagnostic = Stream::validateOriginal(stream))
            || (diagnostic = stream->compareOriginalStates(graph)))
            return {};
        const auto& source = sources(*graph);
        const auto& original = source.sources();
        const auto& authored = stream->originalBudget().stateSequence;
        diagnostic = "RHI-BINDLESS-GRAPHICS-CONTENT-STREAM-PROFILE";
        if (std::tie(sequence.contractVersion,
                sequence.prototypeOptIn,
                sequence.maxCommands,
                sequence.maxDebugDepth,
                sequence.maxStateCells,
                sequence.maxSteps,
                sequence.maxImplicitAccesses,
                sequence.bindlessRootStateVersion,
                sequence.bindlessRootStateOptIn)
                != std::tie(authored.contractVersion,
                    authored.prototypeOptIn,
                    authored.maxCommands,
                    authored.maxDebugDepth,
                    authored.maxStateCells,
                    authored.maxSteps,
                    authored.maxImplicitAccesses,
                    authored.bindlessRootStateVersion,
                    authored.bindlessRootStateOptIn)
            || sequence.bindlessRootStateVersion
                != Contract::rootStateVersion(content.bindlessGeometryContractVersion)
            || !sequence.bindlessRootStateOptIn
            || stream->originalBudget().bindlessRoots.contractVersion
                != Contract::rootStateVersion(content.bindlessGeometryContractVersion)
            || !stream->originalBudget().bindlessRoots.prototypeOptIn
            || budget.contractVersion != 1u || !budget.bindlessRoots.prototypeOptIn)
            return {};
        const OriginalCommandBindlessRootSource* root = nullptr;
        std::size_t rootIndex = SIZE_MAX;
        const bool produced = content.bindlessGeometryContractVersion == 2u;
        bool graphics = false, compute = false;
        for (std::size_t i = 0u; i < original.size(); ++i) {
            const auto* candidate =
                std::get_if<std::shared_ptr<const OriginalCommandBindlessRootSource>>(&original[i]);
            if (!candidate)
                continue;
            diagnostic = "RHI-BINDLESS-GRAPHICS-CONTENT-ROOT-IDENTITY";
            if (!*candidate || (root && !produced))
                return {};
            if ((diagnostic =
                        OriginalCommandBindlessRootSource::validateOriginal(*candidate, device)))
                return {};
            if (root && content.snapshotScope.empty()
                && !core::SharedOwnerProjection::sameOwner(
                    root->originalSnapshot(), (*candidate)->originalSnapshot()))
                return {};
            if (!root) {
                root = candidate->get();
                rootIndex = i;
            }
            const bool render =
                std::holds_alternative<std::shared_ptr<const BindlessPreparedGraphicsBinding>>(
                    (*candidate)->originalPrepared());
            graphics = graphics || render;
            compute = compute || !render;
        }
        if (!content.snapshotScope.empty()) {
            const auto scoped = OriginalBindlessSnapshotScopeContract::inspect(
                original.size(),
                [&](std::size_t i) noexcept -> const OriginalCommandBindlessRootSource* {
                    const auto* owner =
                        std::get_if<std::shared_ptr<const OriginalCommandBindlessRootSource>>(
                            &original[i]);
                    return owner ? owner->get() : nullptr;
                },
                content.snapshotScope);
            if ((diagnostic = scoped.diagnostic))
                return {};
        }
        if ((diagnostic = Contract::validateProfile(source.assemblyVersion_,
                 graph->originalInputs()->originalBudget().contractVersion,
                 graph->stateContractVersion(),
                 authored.contractVersion,
                 stream->streamContractVersion(),
                 budget.bindlessRoots.contractVersion,
                 graphics,
                 compute,
                 content.bindlessGeometryContractVersion)))
            return {};
        bool consumed = false;
        const auto accesses = common::OriginalCommandBindlessRootAccesses::visitValidatedOriginal(
            graph->originalInputs(),
            budget.bindlessRoots,
            [&](const auto& access) noexcept -> const char* {
                if (!produced && access.rootSourceIndex != rootIndex)
                    return "RHI-BINDLESS-GRAPHICS-CONTENT-ROOT-IDENTITY";
                const auto kind = graph->originalInputs()
                                      ->originalScalars()
                                      ->originalUsage()
                                      ->commands()[access.commandIndex]
                                      .kind;
                if (kind == CommandUsageKind::ClearSnapshotFault)
                    return nullptr;
                const auto stages = access.declaration.pipelineStages;
                const auto allowed = Contract::consumptionStages(kind);
                if (!Contract::consumes(kind, content.bindlessGeometryContractVersion)
                    || stages == PipelineStage::None
                    || (static_cast<std::uint32_t>(stages) & ~static_cast<std::uint32_t>(allowed)))
                    return "RHI-BINDLESS-GRAPHICS-CONTENT-CONSUMPTION";
                consumed = consumed || kind != CommandUsageKind::Dispatch;
                return nullptr;
            });
        if ((diagnostic = accesses.diagnostic))
            return {};
        diagnostic = "RHI-BINDLESS-GRAPHICS-CONTENT-CONSUMPTION-MISSING";
        if (!consumed)
            return {};
        const std::array domains{stream->originalMetadataQuota(),
            quota,
            source.originalMetadataQuota(),
            graph->originalMetadataQuota(),
            graph->originalInputs()->originalMetadataQuota()};
        diagnostic = "RHI-BINDLESS-GRAPHICS-CONTENT-STREAM-QUOTA-DOMAIN";
        if (!core::SharedOwnerProjection::hasIndependentOwners(
                domains.size(), [&](std::size_t i) noexcept {
                    return domains[i];
                }))
            return {};
        if (device.overlapsOriginalDeviceCpuOwner(quota))
            return {};
        auto result = prepareOriginalOwner(
            std::move(graph), device, budget, std::move(quota), diagnostic, false);
        if (!result)
            return {};
        if ((diagnostic = result->materializeOriginalGraphicsPlan(
                 sequence, content, stream->originalMetadataQuota(), depth)))
            return {};
        result->plan_->graphicsStream = std::move(stream);
        result->plan_->validateGraphicsStream =
            [](const auto& owner, const auto& state, bool prepared) noexcept -> const char* {
            if (!owner || !owner->hasQuotaBoundSharedOwner(owner)
                || !core::SharedOwnerProjection::sameOwner(
                    owner->originalInputs(), state->originalInputs()))
                return "RHI-BINDLESS-GRAPHICS-CONTENT-STREAM-OWNER";
            return prepared ? owner->compareOriginalStates(state) : nullptr;
        };
        if ((diagnostic = result->validate()))
            return {};
        return result;
    }

private:
    static std::shared_ptr<OriginalCommandStateTransaction> prepareOriginalOwner(
        std::shared_ptr<const Graph> graph,
        const GpuDevice& device,
        const OriginalCommandStateAdmissionBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic,
        bool finiteAdmission)
    {
        diagnostic = Graph::validateOriginal(graph);
        if (diagnostic)
            return {};
        if ((diagnostic = device.validateOriginalDeviceLifetime(graph->device_)))
            return {};
        for (const auto& entry : graph->textureEntries()) {
            const auto* texture =
                Projection::originalTexture(sources(*graph).sources()[entry.point.sourceIndex]);
            if (texture->originalContent_ && texture->originalContent_->contractVersion() == 2u) {
                const auto decision =
                    semantics::OriginalCommandStateAdmissionLowering::textureContentPlanLowering(
                        device.backend());
                if (!decision.validationAvailable) {
                    diagnostic = decision.diagnostic;
                    return {};
                }
            }
        }
        std::uint32_t owners = 0u, controls = 0u;
        if ((diagnostic = preflight(graph, budget, quota, owners, controls, finiteAdmission)))
            return {};
        if (budget.contractVersion == 3u || finiteAdmission) {
            const auto decision =
                semantics::OriginalCommandStateAdmissionLowering::textureTransactionLowering(
                    device.backend(), budget.contractVersion);
            if (!decision.ownershipAvailable || !decision.planAvailable) {
                diagnostic = decision.diagnostic;
                return {};
            }
            if (device.overlapsOriginalDeviceCpuOwner(quota)) {
                diagnostic = "RHI-ORIGINAL-TEXTURE-TRANSACTION-DEVICE-QUOTA-DOMAIN";
                return {};
            }
        }
        if (sources(*graph).originalStateTransactionCount()
            >= sources(*graph).originalBudget().maxStateTransactions) {
            diagnostic = "RHI-ORIGINAL-TRANSACTION-COUNT-CAPACITY";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope scope(quota);
            auto result = core::makeUnique<OriginalCommandStateTransaction>(
                ConstructionKey{}, graph, budget, owners, controls);
            if (!result->countLease_.charged()) {
                diagnostic = "RHI-ORIGINAL-TRANSACTION-COUNT-CAPACITY";
                return {};
            }
            const auto& inventory = sources(*graph).sources();
            for (const auto& entry : graph->bufferEntries()) {
                const auto& lease = std::get<std::shared_ptr<const OriginalCommandBufferLease>>(
                    inventory[entry.sourceIndex]);
                const auto* object = lease->originalObject();
                const auto* issued = entry.observation.submitted();
                result->buffers_.append({object,
                    object->originalStateControl_,
                    {},
                    issued ? std::optional<OriginalSubmittedBufferReadPins::Observation>{*issued}
                           : std::nullopt});
            }
            for (const auto& entry : graph->textureEntries()) {
                const auto* object =
                    Projection::originalTexture(inventory[entry.point.sourceIndex]);
                result->textures_.append({object,
                    object->originalUniformState_,
                    object->originalNormalizedState_,
                    entry.point.range,
                    {},
                    {}});
            }
            auto shared = shareOriginal(std::move(result));
            if ((diagnostic = shared->validate()))
                return {};
            return shared;
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-ORIGINAL-TRANSACTION-ALLOCATION";
            return {};
        } catch (const std::length_error&) {
            diagnostic = "RHI-ORIGINAL-TRANSACTION-ALLOCATION";
            return {};
        }
    }

public:
    static const char* validateOriginal(
        const std::shared_ptr<const OriginalCommandStateTransaction>& self) noexcept
    {
        return !self || !self->hasQuotaBoundSharedOwner(self) ? "RHI-ORIGINAL-TRANSACTION-OWNER"
                                                              : self->validate();
    }

    const char* validate() const noexcept
    {
        if (const auto* id = validateRetainedPlan())
            return id;
        if (plan_ && (status_ == Status::Pending || status_ == Status::Committed))
            return validatePlannedFinals();
        if (status_ != Status::Pending)
            return graph_->validate();
        const auto& inventory = sources(*graph_).sources();
        for (std::size_t i = 0u; i < buffers_.values().size(); ++i) {
            const auto& entry = buffers_.values()[i];
            const auto& original = graph_->bufferEntries()[i];
            const auto& lease = std::get<std::shared_ptr<const OriginalCommandBufferLease>>(
                inventory[original.sourceIndex]);
            if (entry.borrowed) {
                if (entry.object != lease->originalObject()
                    || entry.control != entry.object->originalStateControl_ || entry.pending
                    || !original.observation.submitted()
                    || entry.borrowed->revision() != original.observation.revision()
                    || entry.borrowed->resourceGeneration()
                        != original.observation.resourceGeneration())
                    return "RHI-SUBMISSION-READ-TRANSACTION-CLAIM";
                if (const auto* id = entry.borrowed->diagnostic())
                    return id;
                continue;
            }
            if (entry.object != lease->originalObject()
                || entry.control != entry.object->originalStateControl_ || !entry.pending
                || entry.pending->value() != original.observation.state()
                || entry.pending->revision() != original.observation.revision()
                || !entry.control->matchesPending(*entry.pending, journal_))
                return "RHI-ORIGINAL-TRANSACTION-PENDING-MISMATCH";
        }
        for (std::size_t i = 0u; i < textures_.values().size(); ++i) {
            const auto& entry = textures_.values()[i];
            const auto& original = graph_->textureEntries()[i];
            if (entry.object != Projection::originalTexture(inventory[original.point.sourceIndex])
                || entry.range != original.point.range
                || entry.uniform != entry.object->originalUniformState_
                || entry.normalized != entry.object->originalNormalizedState_)
                return "RHI-ORIGINAL-TRANSACTION-COVERAGE";
            if (entry.normalized) {
                if (!entry.normalizedPending || entry.normalizedPending->range() != entry.range
                    || entry.normalizedPending->state() != original.observation.state()
                    || entry.normalizedPending->revision() != original.observation.revision()
                    || !entry.normalized->matchesPending(*entry.normalizedPending, journal_))
                    return "RHI-ORIGINAL-TRANSACTION-PENDING-MISMATCH";
            } else if (!entry.uniform || !entry.uniformPending
                || entry.uniformPending->value() != original.observation.state()
                || entry.uniformPending->revision() != original.observation.revision()
                || !entry.uniform->matchesPending(*entry.uniformPending, journal_))
                return "RHI-ORIGINAL-TRANSACTION-PENDING-MISMATCH";
        }
        return nullptr;
    }

    // Acquire only initial CPU snapshots. Planned writes and all native work
    // require subsequent full stream/range preflight on this same retained root.
    // Dry initial sequence prerequisite on this exact retained root only.
    // Success grants no mutable plan, recording, submission or completion.
    common::OriginalCommandStateSequence::Result preflightOriginalStateSequence(
        const OriginalCommandStateSequenceBudget& budget) const noexcept
    {
        if (status_ != Status::Prepared)
            return {"RHI-ORIGINAL-TRANSACTION-STATUS"};
        if (const auto* id = validate())
            return {id};
        return common::OriginalCommandStateSequence::inspect(graph_, budget);
    }

    common::OriginalCommandStateSequence::Result preflightOriginalAccesses(
        const OriginalCommandStateSequenceBudget& budget) const noexcept
    {
        if (status_ != Status::Prepared)
            return {"RHI-ORIGINAL-TRANSACTION-STATUS"};
        if (const auto* id = validate())
            return {id};
        return common::OriginalCommandAccessValidation::inspect(graph_, budget);
    }

    ContentCellsInspection preflightOriginalContentCells(
        const OriginalTextureContentPlanBudget& budget) const noexcept
    {
        if (status_ != Status::Prepared)
            return {"RHI-ORIGINAL-TRANSACTION-STATUS"};
        if (const auto* id = validate())
            return {id};
        return inspectRangeContentPlan(budget,
            [](const auto&, const auto&, auto, const auto&, const auto&) noexcept -> const char* {
                return nullptr;
            });
    }

    ContentCellsInspection preflightOriginalCompletedContentReads(
        const OriginalTextureContentPlanBudget& budget) const noexcept
    {
        if (status_ != Status::Prepared || plan_)
            return {"RHI-ORIGINAL-PLAN-STATUS"};
        if (const auto* id = validate())
            return {id};
        return inspectCompletedContentReadPlan(
            budget, [](const auto&, const auto&) noexcept -> const char* {
                return nullptr;
            });
    }

    ContentCellsInspection preflightOriginalUndefinedTextureUploads(
        const OriginalTextureContentPlanBudget& budget) const noexcept
    {
        if (status_ != Status::Prepared || plan_)
            return {"RHI-ORIGINAL-PLAN-STATUS"};
        if (const auto* id = validate())
            return {id};
        return inspectUndefinedTextureUploads(budget);
    }

    // Cold immutable plan publication; no control is enlisted or written.
    const char* materializeOriginalPrototypePlan(const OriginalCommandStateSequenceBudget& budget,
        const OriginalTextureContentPlanBudget& contentBudget = {})
    {
        if (const auto* id = cpuMutationDiagnostic())
            return id;
        if (status_ != Status::Prepared || plan_)
            return "RHI-ORIGINAL-PLAN-STATUS";
        if (contentBudget.contractVersion == 7u && budget.contractVersion != 1u)
            return "RHI-UNDEFINED-TEXTURE-UPLOAD-PLAN-SEQUENCE-PROFILE";
        if (OriginalStorageTextureContentPlanContract::isVersion(contentBudget.contractVersion)
            && (budget.contractVersion != 1u
                || (budget.bindlessRootStateVersion
                    && (budget.bindlessRootStateVersion != 4u || !budget.bindlessRootStateOptIn))))
            return "RHI-STORAGE-CONTENT-PLAN-SEQUENCE-PROFILE";
        if ((OriginalCompletedTextureReadPlanContract::isRootRead(contentBudget.contractVersion)
                || OriginalCompletedDepthRootReadPlanContract::isVersion(
                    contentBudget.contractVersion))
            && (budget.contractVersion != 1u || budget.bindlessRootStateVersion != 4u
                || !budget.bindlessRootStateOptIn))
            return "RHI-BINDLESS-COMPLETED-TEXTURE-READ-SEQUENCE-PROFILE";
        const auto result = preflightOriginalAccesses(budget);
        if (result.diagnostic)
            return result.diagnostic;
        if (const auto* id = preflightContentPlan(contentBudget))
            return id;
        if (const auto* id = preflightPlanWrites())
            return id;
        if (result.stateCells > SIZE_MAX || result.steps > SIZE_MAX)
            return "RHI-ORIGINAL-PLAN-SIZE";
        try {
            core::CpuMemoryQuotaScope scope(originalMetadataQuota());
            Plan plan(budget,
                static_cast<std::size_t>(result.stateCells),
                static_cast<std::size_t>(result.steps),
                commands().size(),
                contentBudget);
            if (const auto* id = populatePlan(plan))
                return id;
            if (const auto* id = populateContentPlan(plan))
                return id;
            if (plan.steps.values().size() != result.steps)
                return "RHI-ORIGINAL-PLAN-COVERAGE";
            plan_.emplace(std::move(plan));
            return nullptr;
        } catch (const std::bad_alloc&) {
            return "RHI-ORIGINAL-PLAN-ALLOCATION";
        } catch (const std::length_error&) {
            return "RHI-ORIGINAL-PLAN-ALLOCATION";
        }
    }

    // Explicit CPU Prototype application. Native execution gates remain closed.
    const char* applyOriginalPrototypePlan() noexcept
    {
        if (const auto* id = cpuMutationDiagnostic())
            return id;
        return applyOriginalPlan();
    }

    bool hasOriginalPrototypePlan() const noexcept { return plan_.has_value(); }

    std::uint32_t originalContentPlanContractVersion() const noexcept
    {
        return plan_ ? plan_->contentBudget.contractVersion : 0u;
    }

    std::uint32_t originalBindlessCompletedReadContractVersion() const noexcept
    {
        return plan_ ? plan_->mixedBudget.bindlessCompletedReadContractVersion : 0u;
    }

    const OriginalBindlessGraphicsDepthContentPlanContract::Counts*
    originalGraphicsDepthContentCounts() const noexcept
    {
        return plan_
                && OriginalBindlessGraphicsDepthContentPlanContract::isVersion(
                    plan_->contentBudget.contractVersion)
            ? &plan_->depthCounts
            : nullptr;
    }

    // Read-only schema identity for the independent completed-read cold seal.
    // This grants neither state mutation nor native command authority.
    bool hasOriginalGeneralShaderResourceProfile() const noexcept
    {
        return budget_.contractVersion == 3u
            && graph_->originalInputs()->originalBudget().contractVersion == 3u
            && budget_.descriptorConsumption.generalShaderResourcesVersion == 1u
            && budget_.descriptorConsumption.generalShaderResourcesOptIn
            && budget_.renderDescriptorUsage.generalShaderResourcesVersion == 1u
            && budget_.renderDescriptorUsage.generalShaderResourcesOptIn;
    }

    bool hasOriginalFiniteGeneralShaderResourceProfile() const noexcept
    {
        return budget_.contractVersion == 5u && graph_->stateContractVersion() == 5u
            && graph_->originalInputs()->originalBudget().contractVersion == 6u
            && sources(*graph_).assemblyVersion_ == 5u
            && budget_.descriptorConsumption.contractVersion == 4u
            && budget_.descriptorConsumption.generalShaderResourcesVersion == 2u
            && budget_.descriptorConsumption.generalShaderResourcesOptIn
            && budget_.renderDescriptorUsage.contractVersion == 4u
            && budget_.renderDescriptorUsage.generalShaderResourcesVersion == 2u
            && budget_.renderDescriptorUsage.generalShaderResourcesOptIn;
    }

    const OriginalFiniteGraphicsContentPlanContract::Counts*
    originalMixedContentCounts() const noexcept
    {
        return plan_
                && OriginalFiniteGraphicsContentPlanContract::isMixed(
                    plan_->mixedBudget.contractVersion)
            ? &plan_->mixedCounts
            : nullptr;
    }

    bool hasOriginalBindlessRootShaderResourceProfile() const noexcept
    {
        return budget_.contractVersion == 1u && graph_->stateContractVersion() == 1u
            && graph_->originalInputs()->originalBudget().contractVersion == 1u
            && sources(*graph_).assemblyVersion_ == 6u
            && budget_.bindlessRoots.contractVersion == 4u && budget_.bindlessRoots.prototypeOptIn;
    }

    bool hasOriginalBindlessRootGeometryStateProfile() const noexcept
    {
        return budget_.contractVersion == 1u && graph_->stateContractVersion() == 1u
            && graph_->originalInputs()->originalBudget().contractVersion == 7u
            && sources(*graph_).assemblyVersion_ == 6u
            && budget_.bindlessRoots.contractVersion == 5u && budget_.bindlessRoots.prototypeOptIn
            && budget_.geometryUsage.contractVersion == 1u && budget_.geometryUsage.prototypeOptIn
            && budget_.geometryUsage.drawConsumptionContractVersion == 1u
            && budget_.geometryUsage.drawConsumptionOptIn;
    }

    bool hasOriginalBindlessGraphicsGeometryProfile() const noexcept
    {
        return hasOriginalBindlessRootGeometryStateProfile() && plan_
            && plan_->mixedBudget.contractVersion == 9u
            && plan_->mixedBudget.bindlessGeometryContractVersion == 1u
            && plan_->mixedBudget.bindlessGeometryOptIn;
    }

    bool hasOriginalBindlessProducedGeometryStateProfile() const noexcept
    {
        return budget_.contractVersion == 1u && graph_->stateContractVersion() == 1u
            && graph_->originalInputs()->originalBudget().contractVersion == 8u
            && sources(*graph_).assemblyVersion_ == 6u
            && budget_.bindlessRoots.contractVersion == 6u && !budget_.bindlessRoots.diagnostic()
            && budget_.geometryUsage.contractVersion == 1u && budget_.geometryUsage.prototypeOptIn
            && budget_.geometryUsage.drawConsumptionContractVersion == 1u
            && budget_.geometryUsage.drawConsumptionOptIn;
    }

    bool hasOriginalBindlessProducedGeometryProfile() const noexcept
    {
        return hasOriginalBindlessProducedGeometryStateProfile() && plan_
            && plan_->mixedBudget.contractVersion == 9u
            && plan_->mixedBudget.bindlessGeometryContractVersion == 2u
            && plan_->mixedBudget.bindlessGeometryOptIn;
    }

    const OriginalBindlessAttachmentClearIntent*
    originalBindlessAttachmentClearIntent() const noexcept
    {
        return plan_ && plan_->mixedBudget.contractVersion == 9u
            ? &plan_->mixedBudget.attachmentClear
            : nullptr;
    }

    std::size_t plannedFinalCellCount() const noexcept
    {
        return plan_ ? plan_->cells.values().size() : 0u;
    }

    std::size_t plannedAccessStepCount() const noexcept
    {
        return plan_ ? plan_->steps.values().size() : 0u;
    }

    // Immutable authored limits, not a state mutation or native authority.
    const OriginalCommandStateSequenceBudget* originalPlannedSequenceBudget() const noexcept
    {
        return plan_ ? &plan_->budget : nullptr;
    }

    std::size_t plannedTransitionCount() const noexcept
    {
        return plan_ ? plan_->transitions.values().size() : 0u;
    }

    const char* beginOriginalPendingOwnership() noexcept
    {
        if (const auto* id = cpuMutationDiagnostic())
            return id;
        if (status_ != Status::Prepared)
            return "RHI-ORIGINAL-TRANSACTION-STATUS";
        if (plan_)
            return "RHI-ORIGINAL-PLAN-EXPLICIT-APPLICATION-REQUIRED";
        if (budget_.contractVersion >= 2u)
            return "RHI-ORIGINAL-TYPED-TRANSACTION-PLAN-REQUIRED";
        for (const auto& entry : textures_.values())
            if (entry.object->originalContent_)
                return "RHI-CANONICAL-CONTENT-PLAN-REQUIRED";
        if (const auto* id = validate())
            return id;
        for (const auto& entry : buffers_.values())
            if (!entry.borrowed && !entry.control->canPrepare(journal_))
                return "RHI-ORIGINAL-TRANSACTION-JOURNAL-ADMISSION";
        for (const auto& entry : textures_.values())
            if (entry.normalized ? !entry.normalized->canPrepare(journal_)
                                 : !entry.uniform->canPrepare(journal_))
                return "RHI-ORIGINAL-TRANSACTION-JOURNAL-ADMISSION";
        for (const auto& entry : buffers_.values())
            if (!entry.borrowed && !entry.control->prepare(journal_))
                return failedEnlistment();
        for (const auto& entry : textures_.values())
            if (entry.normalized ? !entry.normalized->prepare(journal_)
                                 : !entry.uniform->prepare(journal_))
                return failedEnlistment();
        // Storage was reserved at cold publication. Updating embedded optional
        // observations changes neither capacities nor retained owners.
        buffers_.visitMutable([&](auto& entry) noexcept {
            if (!entry.borrowed)
                entry.pending = entry.control->observePending(journal_);
        });
        textures_.visitMutable([&](auto& entry) noexcept {
            if (entry.normalized)
                entry.normalizedPending = entry.normalized->observePending(entry.range, journal_);
            else
                entry.uniformPending = entry.uniform->observePending(journal_);
        });
        status_ = Status::Pending;
        if (const auto* id = validate()) {
            rollbackOriginalOwnership();
            return id;
        }
        return nullptr;
    }

    const char* commitOriginalPendingOwnership() noexcept
    {
        if (const auto* id = cpuMutationDiagnostic())
            return id;
        return commitOriginalOwnership();
    }

    const char* abandonOriginalPendingOwnership() noexcept
    {
        if (const auto* id = cpuMutationDiagnostic())
            return id;
        rollbackOriginalOwnership();
        return nullptr;
    }

    const char* compareOriginalGraph(
        const std::shared_ptr<const Graph>& graph, const GpuDevice& device) const noexcept
    {
        if (const auto* id = validate())
            return id;
        if (!core::SharedOwnerProjection::sameOwner(graph_, graph))
            return "RHI-ORIGINAL-TRANSACTION-GRAPH";
        return device.validateOriginalDeviceLifetime(graph_->device_);
    }

    // Read-only comparison of the original plan-retained stream; no new owner.
    const char* compareOriginalGraphicsStream(
        const std::shared_ptr<const OriginalCommandStreamProgram>& stream) const noexcept
    {
        if (const auto* id = validate())
            return id;
        if (!hasOriginalFiniteGeneralShaderResourceProfile() || !plan_
            || plan_->contentBudget.contractVersion != 6u || !plan_->graphicsStream
            || status_ != Status::Prepared || !journalEmpty())
            return "RHI-GRAPHICS-OUTPUT-SNAPSHOT-ROOT-PROFILE";
        return core::SharedOwnerProjection::sameOwner(plan_->graphicsStream, stream)
            ? nullptr
            : "RHI-GRAPHICS-OUTPUT-SNAPSHOT-STREAM-OWNER";
    }

    const auto& originalGraph() const noexcept { return graph_; }

    const char* compareOriginalBindlessGraphicsStream(
        const std::shared_ptr<const OriginalCommandStreamProgram>& stream) const noexcept
    {
        if (const auto* id = validate())
            return id;
        if ((!hasOriginalBindlessRootShaderResourceProfile()
                && !hasOriginalBindlessGraphicsGeometryProfile()
                && !hasOriginalBindlessProducedGeometryProfile())
            || !plan_
            || (plan_->contentBudget.contractVersion != 9u
                && !OriginalBindlessGraphicsDepthContentPlanContract::isVersion(
                    plan_->contentBudget.contractVersion))
            || !plan_->graphicsStream || status_ != Status::Prepared || !journalEmpty())
            return "RHI-BINDLESS-GRAPHICS-PASS-ROOT-PROFILE";
        return core::SharedOwnerProjection::sameOwner(plan_->graphicsStream, stream)
            ? nullptr
            : "RHI-BINDLESS-GRAPHICS-PASS-STREAM-OWNER";
    }

    // Read-only quota provenance over the retained original CPU graph. Native
    // custody must additionally inspect its private payload/initializer owners.
    bool overlapsCpuOwner(const std::shared_ptr<core::CpuMemoryQuota>& quota) const noexcept
    {
        if (!quota)
            return false;
        const auto& inputs = graph_->originalInputs();
        const auto& scalars = inputs->originalScalars();
        const auto& usage = scalars->originalUsage();
        const auto& source = usage->originalSources();
        for (const auto& domain : std::array{originalMetadataQuota(),
                 graph_->originalMetadataQuota(),
                 inputs->originalMetadataQuota(),
                 source->originalMetadataQuota()})
            if (core::SharedOwnerProjection::overlapsOwner(quota, domain))
                return true;
        for (const auto* allocation : std::array<const void*, 6>{
                 this, graph_.get(), inputs.get(), scalars.get(), usage.get(), source.get()})
            if (core::cpuAllocationOverlapsQuotaOwner(allocation, quota))
                return true;
        for (const auto& entry : source->sources())
            if (std::visit(
                    [&](const auto& owner) noexcept {
                        using T = std::remove_const_t<
                            typename std::decay_t<decltype(owner)>::element_type>;
                        if (!owner)
                            return false;
                        if (core::SharedOwnerProjection::overlapsOwner(
                                quota, owner->originalMetadataQuota())
                            || core::cpuAllocationOverlapsQuotaOwner(owner.get(), quota))
                            return true;
                        if constexpr (!std::is_same_v<T, OriginalCommandTextureReadbackSource>) {
                            if (core::cpuAllocationOverlapsQuotaOwner(
                                    owner->originalObject(), quota))
                                return true;
                            if constexpr (std::is_same_v<T, OriginalCommandTextureLease>)
                                if (core::SharedOwnerProjection::overlapsOwner(
                                        quota, owner->originalObject()->originalContentCpuQuota()))
                                    return true;
                        }
                        if constexpr (T::domain == CommandReferenceDomain::Upload
                            || T::domain == CommandReferenceDomain::Readback)
                            if (core::SharedOwnerProjection::overlapsOwner(
                                    quota, owner->originalInputQuota()))
                                return true;
                        return false;
                    },
                    entry))
                return true;
        return false;
    }

    Status status() const noexcept { return status_; }

    bool journalEmpty() const noexcept { return journal_.empty(); }

    bool hasOriginalRecordingSeal() const noexcept { return recordingSealed_; }

    std::uint32_t originalControlCount() const noexcept { return controls_; }

    std::uint32_t retainedOwnerCount() const noexcept
    {
        return owners_ + static_cast<std::uint32_t>(plannedContentControlCount())
            + (plan_ && plan_->graphicsStream ? 1u : 0u);
    }

    std::size_t plannedContentControlCount() const noexcept
    {
        return plan_ ? plan_->contents.values().size() + plan_->rangeContents.values().size() : 0u;
    }

    std::size_t plannedContentCellCount() const noexcept
    {
        return plan_ ? plan_->rangeCells.values().size() : 0u;
    }

    std::size_t plannedContentUploadCount() const noexcept
    {
        return plan_ ? plan_->uploads.values().size() : 0u;
    }

    std::uint64_t plannedContentGeneration(std::size_t command) const noexcept
    {
        if (plan_)
            if (const auto* step = plan_->uploads.findIf([&](const auto& s) {
                    return s.command == command;
                }))
                return step->generation;
        return 0u;
    }

    bool nativeExecutionProven() const noexcept { return false; }

    const char* executionDiagnostic(Backend backend) const noexcept
    {
        if (const auto* id = validate())
            return id;
        if (plan_
            && OriginalBindlessGraphicsDepthContentPlanContract::isVersion(
                plan_->contentBudget.contractVersion))
            return OriginalBindlessGraphicsDepthContentPlanContract::lowering(
                backend, plan_->contentBudget.contractVersion)
                .diagnostic;
        if (plan_ && plan_->mixedBudget.contractVersion == 9u)
            return OriginalBindlessGraphicsContentPlanContract::lowering(backend,
                plan_->mixedBudget.bindlessGeometryContractVersion,
                plan_->mixedBudget.attachmentClear.contractVersion,
                plan_->mixedBudget.bindlessCompletedReadContractVersion)
                .diagnostic;
        if (plan_ && plan_->mixedBudget.contractVersion == 6u)
            return OriginalFiniteGraphicsContentPlanContract::lowering(backend).diagnostic;
        if (plan_
            && (plan_->contentBudget.contractVersion == 7u
                || OriginalCompletedDepthRootReadPlanContract::isVersion(
                    plan_->contentBudget.contractVersion)
                || OriginalStorageTextureContentPlanContract::isVersion(
                    plan_->contentBudget.contractVersion)))
            return semantics::OriginalCommandStateAdmissionLowering::textureContentPlanLowering(
                backend, plan_->contentBudget.contractVersion)
                .diagnostic;
        if (plan_ && completedContentIsReadOnly())
            return semantics::OriginalCommandStateAdmissionLowering::
                textureCompletedReadPlanLowering(backend, plan_->contentBudget.contractVersion)
                    .diagnostic;
        if (budget_.contractVersion == 3u)
            return arp::rhi::semantics::OriginalCommandStateAdmissionLowering::
                textureTransactionLowering(backend)
                    .diagnostic;
        return arp::rhi::semantics::OriginalCommandStateAdmissionLowering::lowering(backend)
            .diagnostic;
    }

private:
    // Identity replay is narrower than public final-state validation. A later
    // reader may own the output cell in its journal; only that reader's seal
    // can authenticate Pending CopySrc. This grants no current-state access.
    const char* validateCompletedGraphicsOutputIdentity(const GpuTexture& output) const noexcept
    {
        if (!hasOriginalFiniteGeneralShaderResourceProfile() || !plan_
            || plan_->contentBudget.contractVersion != 6u || !plan_->graphicsStream
            || !recordingSealed_ || status_ != Status::Committed || !journalEmpty())
            return "RHI-GRAPHICS-OUTPUT-SNAPSHOT-COMPLETED-ROOT-PROFILE";
        if (const auto* id = validateRetainedPlan(&output, true))
            return id;
        return graph_->validateGraph(false);
    }

    // Captured initialized byte authority is usable only through the existing
    // seal and this root's actual journal. Public observations stay closed.
    const char* validateInitializedBufferPending(const GpuBuffer& object,
        const GpuBuffer::CommittedStateObservation& captured,
        OriginalInitializedBufferPendingUse use = OriginalInitializedBufferPendingUse::CopySource1,
        std::uint32_t version = 1u) const noexcept
    {
        if (const auto* id = OriginalInitializedBufferPendingContract::validateUse(version, use))
            return id;
        const auto expected = OriginalInitializedBufferPendingContract::state(use);
        if (!recordingSealed_ || status_ != Status::Pending || !plan_ || journal_.empty())
            return "RHI-ORIGINAL-INITIALIZED-BUFFER-UPLOAD-PENDING-ROOT";
        if (const auto* id = validate())
            return id;
        if (use == OriginalInitializedBufferPendingUse::ImmutableGeometryRead1) {
            // State4/Inputs5 and the complete opted-in original draw footprint.
            const auto& geometry = graph_->budget_.geometryUsage;
            if (graph_->budget_.contractVersion != 4u
                || graph_->originalInputs()->originalBudget().contractVersion != 5u
                || geometry.drawConsumptionContractVersion != 1u || !geometry.drawConsumptionOptIn)
                return "RHI-ORIGINAL-INITIALIZED-GEOMETRY-PENDING-PROFILE";
        }
        if (use == OriginalInitializedBufferPendingUse::ImmutableBindlessGeometryRead5
            && ((!hasOriginalBindlessGraphicsGeometryProfile()
                    && !hasOriginalBindlessProducedGeometryProfile())
                || !plan_->graphicsStream
                || plan_->budget.bindlessRootStateVersion
                    != (hasOriginalBindlessProducedGeometryProfile() ? 6u : 5u)
                || !plan_->budget.bindlessRootStateOptIn))
            return "RHI-ORIGINAL-BINDLESS-GEOMETRY-PENDING-PROFILE";
        if (use == OriginalInitializedBufferPendingUse::ImmutableBindlessShaderRead6
            && (!hasOriginalBindlessRootShaderResourceProfile()
                && !hasOriginalBindlessRootGeometryStateProfile()
                && !hasOriginalBindlessProducedGeometryProfile()))
            return "RHI-ORIGINAL-BINDLESS-SHADER-PENDING-PROFILE";
        if (use == OriginalInitializedBufferPendingUse::ImmutableGraphicsShaderBufferRead4) {
            const auto* mixed = originalMixedContentCounts();
            if (!mixed || !mixed->completedControls || !mixed->attachmentControls
                || !mixed->renderBindingReads || !mixed->drawReads)
                return "RHI-ORIGINAL-INITIALIZED-GRAPHICS-BUFFER-PENDING-MIXED-ROOT";
        }
        if (const auto* id = OriginalInitializedBufferPendingContract::validateRootProfile(use,
                hasOriginalGeneralShaderResourceProfile(),
                hasOriginalFiniteGeneralShaderResourceProfile(),
                originalContentPlanContractVersion()))
            return id;
        if (const auto* id = captured.registration_.diagnostic())
            return id;
        const auto registration = object.originalResourceRegistration();
        if (const auto* id = registration.diagnostic())
            return id;
        if (!captured.registration_.matchesIdentity(
                &object, object.id().value, OriginalRegisteredResourceKind::Buffer)
            || captured.resourceGeneration() != registration.generation()
            || captured.control_ != object.originalStateControl_ || captured.state() != expected)
            return "RHI-ORIGINAL-INITIALIZED-BUFFER-UPLOAD-CAPTURE";
        bool original = false;
        const auto& inventory = sources(*graph_).sources();
        for (const auto& entry : graph_->bufferEntries()) {
            const auto& lease = std::get<std::shared_ptr<const OriginalCommandBufferLease>>(
                inventory[entry.sourceIndex]);
            const auto* initial = entry.observation.committed();
            if (lease->originalObject() == &object && initial
                && initial->control_ == captured.control_
                && entry.observation.resourceGeneration() == captured.resourceGeneration()
                && entry.observation.state() == captured.state()
                && entry.observation.revision() == captured.revision())
                original = true;
        }
        const auto* cell = plan_->cells.findIf([&](const auto& entry) {
            return entry.buffer == &object;
        });
        if (!original || !cell || cell->scalar != captured.control_ || !cell->scalarPending
            || OriginalInitializedBufferPendingContract::validateStates(use,
                captured.state(),
                cell->initial,
                cell->final,
                cell->scalarPending->value(),
                version)
            || cell->scalarPending->revision() != captured.revision()
            || !cell->scalar->matchesPending(*cell->scalarPending, journal_))
            return "RHI-ORIGINAL-INITIALIZED-BUFFER-UPLOAD-PENDING-MISMATCH";
        return nullptr;
    }

    const char* validateCompletedTextureReadPending(const GpuTexture& object,
        const GpuTexture::ContentExpectation& completed,
        std::uint32_t version) const noexcept
    {
        using Contract = OriginalCompletedTextureReadPlanContract;
        if (const auto* id = Contract::validatePendingProfile(version,
                budget_.prototypeOptIn,
                hasOriginalGeneralShaderResourceProfile(),
                originalContentPlanContractVersion(),
                hasOriginalFiniteGeneralShaderResourceProfile(),
                hasOriginalBindlessRootShaderResourceProfile()
                    || ((version == 5u
                            || (version == 6u
                                && originalBindlessCompletedReadContractVersion() == 4u))
                        && hasOriginalBindlessGraphicsGeometryProfile()),
                plan_ ? plan_->mixedBudget.bindlessCompletedReadContractVersion : 0u))
            return id;
        if (version == 3u) {
            const auto* mixed = originalMixedContentCounts();
            if (!mixed || !mixed->completedControls || !mixed->attachmentControls
                || !mixed->renderBindingReads || !mixed->drawReads)
                return "RHI-COMPLETED-SHADER-TEXTURE-PENDING-MIXED-ROOT";
        }
        if (!recordingSealed_ || status_ != Status::Pending || !plan_ || journal_.empty())
            return "RHI-COMPLETED-SHADER-TEXTURE-PENDING-ROOT";
        if (const auto* id = validate())
            return id;
        if (const auto* id = object.validateOriginalContent(completed))
            return id;
        const auto registration = object.originalResourceRegistration();
        if (const auto* id = registration.diagnostic())
            return id;
        if (!completed.registration_.matchesIdentity(
                &object, object.id().value, OriginalRegisteredResourceKind::Texture)
            || completed.resourceGeneration() != registration.generation()
            || completed.control_ != object.originalContent_.get())
            return "RHI-COMPLETED-SHADER-TEXTURE-PENDING-CAPTURE";
        const auto* content = plan_->contents.findIf([&](const auto& cell) {
            return cell.object == &object;
        });
        if (!content || content->control != completed.control_ || content->writes
            || content->pending || content->committed
            || !(content->initial.value == completed.value_.value)
            || content->initial.revision != completed.value_.revision
            || !content->control->value_.matches(content->initial))
            return "RHI-COMPLETED-SHADER-TEXTURE-PENDING-CONTENT-STALE";
        const char* diagnostic = nullptr;
        const auto geometry =
            semantics::OriginalTextureStateRange::capture(object.desc(), UINT64_MAX, diagnostic);
        if (!geometry)
            return diagnostic;
        const TextureSubresourceRange whole{0u,
            object.desc().mipLevelCount,
            0u,
            object.desc().arrayLayerCount,
            TextureAspect::Color};
        if (!geometry->isWholeAspect(whole))
            return "RHI-COMPLETED-SHADER-TEXTURE-PENDING-RANGE";
        std::uint64_t transitions = 0u;
        for (const auto& transition : plan_->transitions.values())
            if (!transition.buffer && transition.barrier.resource == object.id()) {
                const auto& barrier = transition.barrier;
                if (barrier.textureSubresources != whole || barrier.before != ResourceState::CopyDst
                    || barrier.after != ResourceState::ShaderRead
                    || transition.normalized != object.originalNormalizedState_
                    || transition.scalar != object.originalUniformState_)
                    return "RHI-COMPLETED-SHADER-TEXTURE-PENDING-TRANSITION";
                ++transitions;
            }
        if (transitions > 1u
            || (version != 4u && version != 5u && version != 6u && version != 7u && version != 8u
                && version != 9u && transitions != 1u))
            return "RHI-COMPLETED-SHADER-TEXTURE-PENDING-TRANSITION";
        bool captured = false;
        const auto& inventory = sources(*graph_).sources();
        for (const auto& entry : graph_->textureEntries()) {
            if (Projection::originalTexture(inventory[entry.point.sourceIndex]) != &object)
                continue;
            const auto& initial = entry.observation;
            if (const auto* id = initial.registration_.diagnostic())
                return id;
            if (!initial.registration_.matchesIdentity(
                    &object, object.id().value, OriginalRegisteredResourceKind::Texture)
                || initial.resourceGeneration() != registration.generation()
                || initial.normalized_ != object.originalNormalizedState_
                || initial.uniform_ != object.originalUniformState_ || initial.range() != whole
                || (initial.state() != ResourceState::CopyDst
                    && ((version != 4u && version != 5u && version != 6u && version != 7u
                            && version != 8u && version != 9u)
                        || initial.state() != ResourceState::ShaderRead))
                || transitions != (initial.state() == ResourceState::CopyDst ? 1u : 0u))
                return "RHI-COMPLETED-SHADER-TEXTURE-PENDING-CAPTURE";
            for (std::uint64_t ordinal = 0u; ordinal < geometry->rangeCellCount(whole); ++ordinal) {
                std::uint64_t index = 0u;
                TextureSubresourceRange range;
                if (!geometry->cellIndex(whole, ordinal, index)
                    || !geometry->cellRange(index, range))
                    return "RHI-COMPLETED-SHADER-TEXTURE-PENDING-RANGE";
                const auto* cell = plan_->cells.findIf([&](const auto& value) {
                    return value.texture == &object && value.range == range;
                });
                if (!cell || cell->normalized != initial.normalized_
                    || cell->scalar != initial.uniform_)
                    return "RHI-COMPLETED-SHADER-TEXTURE-PENDING-CONTROL";
                if (cell->normalized) {
                    if (!cell->texturePending || cell->texturePending->range() != range
                        || !cell->normalized->matchesPending(*cell->texturePending, journal_))
                        return "RHI-COMPLETED-SHADER-TEXTURE-PENDING-JOURNAL";
                    if (const auto* id = Contract::validatePendingStates(initial.state(),
                            cell->initial,
                            cell->final,
                            cell->texturePending->state(),
                            initial.revision(),
                            cell->texturePending->revision(),
                            version))
                        return id;
                } else {
                    if (!cell->scalar || !cell->scalarPending
                        || !cell->scalar->matchesPending(*cell->scalarPending, journal_))
                        return "RHI-COMPLETED-SHADER-TEXTURE-PENDING-JOURNAL";
                    if (const auto* id = Contract::validatePendingStates(initial.state(),
                            cell->initial,
                            cell->final,
                            cell->scalarPending->value(),
                            initial.revision(),
                            cell->scalarPending->revision(),
                            version))
                        return id;
                }
            }
            captured = true;
        }
        return captured ? nullptr : "RHI-COMPLETED-SHADER-TEXTURE-PENDING-CAPTURE";
    }

    // Same sealed root and normalized state journal; the content array stays
    // committed because this reader never writes it. No whole-color stamp may
    // replace the authentic, independently generated depth cell observations.
    const char* validateCompletedDepthReadPending(
        const OriginalCompletedDepthReadInputContract::Input& input,
        std::uint32_t version = 1u) const noexcept
    {
        using Contract = OriginalCompletedDepthRootReadPlanContract;
        const bool canonicalGraphics = version == 4u;
        const bool graphics = version == 3u || canonicalGraphics;
        if (version != 1u && version != 2u && !graphics)
            return "RHI-DEPTH-ROOT-PENDING-VERSION";
        if (!recordingSealed_ || status_ != Status::Pending || !plan_ || journal_.empty()
            || !hasOriginalBindlessRootShaderResourceProfile()
            || (graphics ? plan_->contentBudget.contractVersion != (canonicalGraphics ? 19u : 18u)
                         : (!Contract::isVersion(plan_->contentBudget.contractVersion)
                               && !OriginalStorageTextureContentPlanContract::isMixedVersion(
                                   plan_->contentBudget.contractVersion))))
            return "RHI-DEPTH-ROOT-PENDING-ROOT";
        if (const auto* id = OriginalCompletedDepthReadInputContract::validateAccess(
                input.state, input.stages, input.access, graphics ? 3u : 1u))
            return id;
        const auto stage = graphics ? PipelineStage::FragmentShader : PipelineStage::ComputeShader;
        if (input.stages != stage)
            return "RHI-DEPTH-ROOT-PENDING-STAGE";
        if (const auto* id = validate())
            return id;
        if (const auto* id = OriginalCompletedDepthTextureSource::validateOriginal(input.source))
            return id;
        const auto& view = input.source->originalViewSource();
        const auto& object = *view->originalParentLease()->originalObject();
        const auto* content = plan_->rangeContents.findIf([&](const auto& entry) {
            return entry.object == &object;
        });
        if (!content || content->control != object.originalContent_.get() || content->mutations
            || content->pending || content->committed || !content->control->cells_
            || !content->control->cells_->matches(content->initial))
            return "RHI-DEPTH-ROOT-PENDING-CONTENT";
        const auto& geometry = *content->control->geometry_;
        const auto& d = view->originalObject()->desc();
        const TextureSubresourceRange visible{
            d.baseMipLevel, d.mipLevelCount, d.baseArrayLayer, d.arrayLayerCount, d.aspect};
        std::uint64_t transitions = 0u;
        for (const auto& transition : plan_->transitions.values()) {
            if (transition.buffer || transition.barrier.resource != object.id())
                continue;
            const auto& b = transition.barrier;
            if (b.textureSubresources != visible || b.after != ResourceState::ShaderRead
                || (b.before != ResourceState::DepthAttachment && b.before != ResourceState::CopySrc
                    && b.before != ResourceState::ShaderRead)
                || b.destinationStages != stage || b.destinationAccess != ResourceAccess::ShaderRead
                || transition.normalized != object.originalNormalizedState_
                || transition.scalar != object.originalUniformState_)
                return "RHI-DEPTH-ROOT-PENDING-TRANSITION";
            ++transitions;
        }
        if (transitions > 1u)
            return "RHI-DEPTH-ROOT-PENDING-TRANSITION";
        const auto& inventory = sources(*graph_).sources();
        bool authenticRoot = false;
        for (const auto& entry : graph_->textureEntries()) {
            const auto& p = entry.point;
            if (p.origin != Projection::Point::Origin::BindlessRoot || p.range != visible
                || p.state != input.state || p.stages != input.stages || p.access != input.access
                || p.descriptorSourceIndex >= inventory.size()
                || p.viewSourceIndex >= inventory.size())
                continue;
            const auto* canonical =
                std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(
                    &inventory[p.viewSourceIndex]);
            const auto* root =
                std::get_if<std::shared_ptr<const OriginalCommandBindlessRootSource>>(
                    &inventory[p.descriptorSourceIndex]);
            if (!canonical || canonical->get() != view.get()
                || !core::SharedOwnerProjection::sameOwner(*canonical, view) || !root)
                continue;
            const auto join = [&](std::size_t physical, std::size_t source) -> const char* {
                if (source != p.viewSourceIndex)
                    return nullptr;
                const auto proof = (*root)->originalCompletedDepthAt(physical);
                if (proof.get() != input.source.get()
                    || !core::SharedOwnerProjection::sameOwner(proof, input.source))
                    return "RHI-DEPTH-ROOT-PENDING-PROOF";
                authenticRoot = true;
                return nullptr;
            };
            if (graphics) {
                const auto* id = canonicalGraphics
                    ? OriginalBindlessRootNativeClosureContract::
                          canonicalGraphicsDepthRootDiagnostic(**root, 20u)
                    : OriginalBindlessRootNativeClosureContract::
                          ordinaryGraphicsDepthRootDiagnostic(**root, 16u);
                if (id)
                    return id;
                // Proof ordinals include every physical descriptor (samplers
                // too) and the retained readback slot. Access ordinals do not.
                std::size_t physical = (*root)->elements().size()
                    + ((*root)->originalSnapshot()->faultReadbackBuffer() ? 1u : 0u);
                for (const auto& domain : (*root)->ordinaryDomains())
                    for (const auto& element : domain.source->frozenElements()) {
                        if (const auto* id = join(physical++, element.declaration.sourceIndex))
                            return id;
                    }
            } else {
                for (std::size_t i = 0u; i < (*root)->elements().size(); ++i)
                    if (const auto* id = join(i, (*root)->elements()[i].declaration.sourceIndex))
                        return id;
            }
        }
        if (!authenticRoot)
            return "RHI-DEPTH-ROOT-PENDING-PROOF";
        const auto controlIndex =
            static_cast<std::size_t>(content - plan_->rangeContents.values().data());
        for (const auto& proof : input.source->cells()) {
            if (const auto* id = object.validateOriginalContent(proof))
                return id;
            std::uint64_t index = 0u;
            if (!geometry.cellIndex(proof.range(), 0u, index)
                || geometry.rangeCellCount(proof.range()) != 1u)
                return "RHI-DEPTH-ROOT-PENDING-CELL";
            const auto* value = plan_->rangeCells.findIf([&](const auto& cell) {
                return cell.controlIndex == controlIndex && cell.cellIndex == index;
            });
            if (!value || value->written || value->completed || !value->stored
                || value->initial.state != Content::State::Completed
                || value->initial.generation != proof.contentGeneration()
                || content->initial.revision != proof.revision())
                return "RHI-DEPTH-ROOT-PENDING-CONTENT";
            const auto* state = plan_->cells.findIf([&](const auto& cell) {
                return cell.texture == &object && cell.range == proof.range();
            });
            if (!state || !state->normalized || state->normalized != object.originalNormalizedState_
                || !state->texturePending
                || !state->normalized->matchesPending(*state->texturePending, journal_))
                return "RHI-DEPTH-ROOT-PENDING-JOURNAL";
            bool captured = false;
            for (const auto& entry : graph_->textureEntries()) {
                if (Projection::originalTexture(inventory[entry.point.sourceIndex]) != &object
                    || !geometry.containsCell(entry.point.range, index))
                    continue;
                const auto& initial = entry.observation;
                if (initial.resourceGeneration() != proof.resourceGeneration()
                    || initial.normalized_ != state->normalized
                    || initial.uniform_ != state->scalar)
                    return "RHI-DEPTH-ROOT-PENDING-CAPTURE";
                if (const auto* id = Contract::validatePendingStates(initial.state(),
                        state->initial,
                        state->final,
                        state->texturePending->state(),
                        initial.revision(),
                        state->texturePending->revision(),
                        transitions))
                    return id;
                captured = true;
            }
            if (!captured)
                return "RHI-DEPTH-ROOT-PENDING-CAPTURE";
        }
        return nullptr;
    }

    struct OriginalContentUpload {
        const GpuTexture* object;
        Content* control;
        std::shared_ptr<const OriginalCommandTextureUploadSource> source;
    };

    const char* validateInitializedTexturePending(std::size_t command,
        const GpuTexture& object,
        const GpuTexture::CommittedStateObservation& state,
        const GpuTexture::ContentExpectation& content) const noexcept
    {
        if (!recordingSealed_ || status_ != Status::Pending || !plan_ || journal_.empty())
            return "RHI-ORIGINAL-INITIALIZED-TEXTURE-UPLOAD-PENDING-ROOT";
        if (const auto* id = validate())
            return id;
        if (const auto* id = state.registration_.diagnostic())
            return id;
        if (const auto* id = content.registration_.diagnostic())
            return id;
        const auto registration = object.originalResourceRegistration();
        if (const auto* id = registration.diagnostic())
            return id;
        if (!state.registration_.matchesIdentity(
                &object, object.id().value, OriginalRegisteredResourceKind::Texture)
            || !content.registration_.matchesIdentity(
                &object, object.id().value, OriginalRegisteredResourceKind::Texture)
            || state.resourceGeneration() != registration.generation()
            || content.resourceGeneration() != registration.generation()
            || state.normalized_ != object.originalNormalizedState_
            || state.uniform_ != object.originalUniformState_
            || state.state()
                != (plan_->contentBudget.contractVersion == 7u ? ResourceState::Undefined
                                                               : ResourceState::CopyDst)
            || content.control_ != object.originalContent_.get())
            return "RHI-ORIGINAL-INITIALIZED-TEXTURE-UPLOAD-CAPTURE";
        if (command >= commands().size())
            return "RHI-ORIGINAL-INITIALIZED-TEXTURE-UPLOAD-COMMAND";
        if (const auto* id = OriginalUndefinedTextureUploadPlanContract::validateControl(
                content.control_->contractVersion(), plan_->contentBudget.contractVersion))
            return id;
        const auto original = contentUploadAt(command);
        if (!original || original->object != &object || original->control != content.control_)
            return "RHI-ORIGINAL-INITIALIZED-TEXTURE-UPLOAD-COMMAND";
        bool initial = false;
        const auto& inventory = sources(*graph_).sources();
        for (const auto& entry : graph_->textureEntries()) {
            if (Projection::originalTexture(inventory[entry.point.sourceIndex]) == &object
                && entry.point.commandIndex == command
                && entry.observation.normalized_ == state.normalized_
                && entry.observation.uniform_ == state.uniform_
                && entry.observation.range() == state.range()
                && entry.observation.revision() == state.revision()
                && entry.observation.state() == state.state()
                && entry.observation.resourceGeneration() == state.resourceGeneration())
                initial = true;
        }
        const auto* upload = plan_->uploads.findIf([&](const auto& entry) {
            return entry.command == command;
        });
        if (!initial || !upload || !upload->generation
            || upload->cell >= plan_->contents.values().size())
            return "RHI-ORIGINAL-INITIALIZED-TEXTURE-UPLOAD-INITIAL";
        const auto& cell = plan_->contents.values()[upload->cell];
        if (cell.object != &object || cell.control != content.control_
            || !(cell.initial.value == content.value_.value)
            || cell.initial.revision != content.value_.revision || !cell.pending || cell.committed
            || cell.pending->value().state != Content::State::Proposed
            || !cell.control->value_.matchesPending(*cell.pending, journal_))
            return "RHI-ORIGINAL-INITIALIZED-TEXTURE-UPLOAD-CONTENT-PENDING";
        std::uint64_t previous = cell.initial.value.generation, writes = 0u;
        for (const auto& step : plan_->uploads.values())
            if (step.cell == upload->cell) {
                if (step.generation <= previous)
                    return "RHI-ORIGINAL-INITIALIZED-TEXTURE-UPLOAD-GENERATION";
                previous = step.generation;
                ++writes;
            }
        if (writes != cell.writes || previous != cell.pending->value().generation)
            return "RHI-ORIGINAL-INITIALIZED-TEXTURE-UPLOAD-GENERATION";
        const char* diagnostic = nullptr;
        const auto range =
            semantics::OriginalTextureStateRange::capture(object.desc(), UINT64_MAX, diagnostic);
        if (!range)
            return diagnostic;
        std::uint64_t accesses = 0u;
        for (const auto& step : plan_->steps.values())
            if (step.command == command) {
                const auto& stateCell = plan_->cells.values()[step.cell];
                if (stateCell.texture != &object
                    || stateCell.initial
                        != (plan_->contentBudget.contractVersion == 7u ? ResourceState::Undefined
                                                                       : ResourceState::CopyDst)
                    || step.before != ResourceState::CopyDst
                    || step.after != ResourceState::CopyDst)
                    return "RHI-ORIGINAL-INITIALIZED-TEXTURE-UPLOAD-ACCESS";
                ++accesses;
            }
        return accesses == range->rangeCellCount(state.range())
            ? nullptr
            : "RHI-ORIGINAL-INITIALIZED-TEXTURE-UPLOAD-ACCESS";
    }

    std::optional<OriginalContentUpload> contentUploadAt(std::size_t command) const noexcept
    {
        const auto& usage = commands()[command];
        if (usage.kind != CommandUsageKind::TextureUpload || usage.referenceCount != 2u)
            return {};
        const auto& inventory = sources(*graph_).sources();
        const auto index = references()[usage.firstReference + 1u].sourceIndex;
        const auto* source = std::get_if<std::shared_ptr<const OriginalCommandTextureUploadSource>>(
            &inventory[index]);
        if (!source)
            return {};
        const auto* object = (*source)->originalDestinationLease()->originalObject();
        return object->originalContent_ ? std::optional<OriginalContentUpload>{{object,
                                              object->originalContent_.get(),
                                              *source}}
                                        : std::nullopt;
    }

    template <class Emit>
    const char* visitOriginalContentCommand(std::size_t command,
        std::size_t& passBegin,
        const GpuTexture& object,
        const OriginalTextureContentPlanBudget& budget,
        std::uint64_t& visits,
        Emit&& emit) const noexcept
    {
        using Sequence = OriginalTextureContentCellSequence;
        using A = Sequence::Action;
        using K = CommandUsageKind;
        using Origin = Projection::Point::Origin;
        const auto charge = [&]() noexcept -> const char* {
            if (visits == budget.maxProjectionVisits)
                return "RHI-ORIGINAL-CONTENT-PLAN-PROJECTION-CAPACITY";
            ++visits;
            return nullptr;
        };
        const auto kind = commands()[command].kind;
        if (kind == K::BeginRenderPass)
            passBegin = command;
        const bool attachment = kind == K::BeginRenderPass || kind == K::EndRenderPass;
        const bool mixedDepth =
            OriginalStorageTextureContentPlanContract::isMixedVersion(budget.contractVersion)
            && object.desc().format == TextureFormat::D32Float;
        const auto depthVersion = mixedDepth
            ? OriginalStorageTextureContentPlanContract::depthPlanVersion(budget.contractVersion)
            : budget.contractVersion;
        const bool depthRead = OriginalCompletedDepthRootReadPlanContract::isVersion(depthVersion);
        const bool graphicsDepth =
            OriginalBindlessGraphicsDepthContentPlanContract::isVersion(budget.contractVersion);
        const bool storageContent =
            OriginalStorageTextureContentPlanContract::isVersion(budget.contractVersion)
            && !mixedDepth;
        const auto begin = passBegin;
        if (attachment && begin == SIZE_MAX)
            return "RHI-ORIGINAL-CONTENT-PLAN-PASS";
        const auto& inventory = sources(*graph_).sources();
        const auto generation = object.originalResourceRegistration().generation();
        for (const auto& entry : graph_->textureEntries()) {
            if (const auto* id = charge())
                return id;
            const auto& p = entry.point;
            if (Projection::originalTexture(inventory[p.sourceIndex]) != &object)
                continue;
            if (entry.observation.resourceGeneration() != generation)
                return "RHI-ORIGINAL-CONTENT-PLAN-GENERATION";
            if (attachment && p.origin == Origin::ExplicitOperand && p.commandIndex == begin) {
                if (depthRead)
                    return "RHI-DEPTH-ROOT-READ-PLAN-MUTATION";
                const auto* descriptor = graph_->originalInputs()->descriptorAt(begin);
                const auto* pass = descriptor
                    ? std::get_if<std::shared_ptr<const RenderPassArguments>>(descriptor)
                    : nullptr;
                const auto* view =
                    std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(
                        &inventory[p.sourceIndex]);
                if (!pass || !*pass || !view || !*view)
                    return "RHI-ORIGINAL-CONTENT-PLAN-PASS-SOURCE";
                const auto& d = (*pass)->desc();
                if (d.layeredRendering || !d.subpasses.empty())
                    return "RHI-ORIGINAL-CONTENT-PLAN-PASS-PROFILE";
                LoadOp load = LoadOp::DontCare;
                StoreOp store = StoreOp::DontCare;
                bool found = false;
                for (const auto& color : d.colorAttachments) {
                    if (const auto* id = charge())
                        return id;
                    if (color.hasResolveTarget)
                        return "RHI-ORIGINAL-CONTENT-PLAN-RESOLVE-UNPROVEN";
                    if (color.target == (*view)->resource()) {
                        load = color.loadOp;
                        store = color.storeOp;
                        found = p.range.aspect == TextureAspect::Color;
                    }
                }
                if (d.hasDepthStencilAttachment
                    && d.depthStencilAttachment.target == (*view)->resource()) {
                    const auto& depth = d.depthStencilAttachment;
                    if (p.range.aspect == TextureAspect::Depth) {
                        if (depth.depthReadOnly)
                            return "RHI-ORIGINAL-CONTENT-PLAN-READONLY-ATTACHMENT-UNPROVEN";
                        load = depth.depthLoadOp;
                        store = depth.depthStoreOp;
                        found = true;
                    } else if (p.range.aspect == TextureAspect::Stencil) {
                        if (depth.stencilReadOnly)
                            return "RHI-ORIGINAL-CONTENT-PLAN-READONLY-ATTACHMENT-UNPROVEN";
                        load = depth.stencilLoadOp;
                        store = depth.stencilStoreOp;
                        found = true;
                    }
                }
                if (!found || !semantics::validAttachmentLoadOp(load)
                    || !semantics::validAttachmentStoreOp(store))
                    return "RHI-ORIGINAL-CONTENT-PLAN-ATTACHMENT-PLANE";
                const auto action = *semantics::AttachmentContentActionContract::action(
                    kind == K::BeginRenderPass, load, store);
                if (const auto* id =
                        emit(Sequence::Step{object.id(), generation, p.range, action, begin}))
                    return id;
            } else if (p.commandIndex == command && p.origin == Origin::BindlessRoot) {
                if (storageContent) {
                    if (!hasOriginalBindlessRootShaderResourceProfile() || kind != K::Dispatch
                        || p.descriptorSourceIndex >= inventory.size()
                        || p.viewSourceIndex >= inventory.size())
                        return "RHI-STORAGE-CONTENT-PLAN-ROOT";
                    const auto* root =
                        std::get_if<std::shared_ptr<const OriginalCommandBindlessRootSource>>(
                            &inventory[p.descriptorSourceIndex]);
                    const auto* view =
                        std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(
                            &inventory[p.viewSourceIndex]);
                    const auto* parent =
                        std::get_if<std::shared_ptr<const OriginalCommandTextureLease>>(
                            &inventory[p.sourceIndex]);
                    if (!root || !*root || !view || !*view || !parent || !*parent
                        || !(*root)->matchesCanonicalSources(sources(*graph_))
                        || (*view)->originalParentLease()->originalObject() != &object
                        || !core::SharedOwnerProjection::sameOwner(
                            (*view)->originalParentLease(), *parent))
                        return "RHI-STORAGE-CONTENT-PLAN-SOURCE";
                    const auto& shape = (*view)->originalViewLease()->originalObject()->desc();
                    if (p.range
                        != TextureSubresourceRange{shape.baseMipLevel,
                            shape.mipLevelCount,
                            shape.baseArrayLayer,
                            shape.arrayLayerCount,
                            shape.aspect})
                        return "RHI-STORAGE-CONTENT-PLAN-RANGE";
                    bool found = false, write = false;
                    for (const auto& element : (*root)->elements()) {
                        if (const auto* id = charge())
                            return id;
                        const auto& d = element.declaration;
                        if (d.sourceIndex != p.viewSourceIndex)
                            continue;
                        const auto* owner =
                            std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(
                                &element.owner);
                        if (!owner || !core::SharedOwnerProjection::sameOwner(*owner, *view)
                            || d.stages != p.stages || d.access != p.access
                            || d.write.resourceState != p.state)
                            return "RHI-STORAGE-CONTENT-PLAN-DECLARATION";
                        write = d.write.type == DescriptorType::StorageTexture;
                        if (!write && d.write.type != DescriptorType::SampledTexture)
                            return "RHI-STORAGE-CONTENT-PLAN-DECLARATION";
                        const auto* binding = findDescriptorSetBinding(
                            (*root)->originalObject()->layout(), d.write.binding);
                        if (!binding || binding->stages != ShaderStage::Compute
                            || (write
                                && (binding->storageTextureFormat != TextureFormat::Rgba16Float
                                    || binding->storageTextureAccess
                                        != StorageTextureAccess::WriteOnly)))
                            return "RHI-STORAGE-CONTENT-PLAN-DECLARATION";
                        if (const auto* id =
                                OriginalStorageTextureContentPlanContract::inspectViewBinding(
                                    object.desc(), shape, p.state, p.stages, p.access, write))
                            return id;
                        found = true;
                    }
                    if (!found)
                        return "RHI-STORAGE-CONTENT-PLAN-DECLARATION";
                    if (const auto* id = emit(Sequence::Step{object.id(),
                            generation,
                            p.range,
                            write ? A::StorageWrite : A::SampledRead,
                            SIZE_MAX}))
                        return id;
                    continue;
                }
                if (!depthRead && !graphicsDepth)
                    return "RHI-ORIGINAL-CONTENT-PLAN-BINDLESS-READ-UNPROVEN";
                if (kind != (graphicsDepth ? K::Draw : K::Dispatch)
                    || p.state != ResourceState::ShaderRead
                    || p.stages
                        != (graphicsDepth ? PipelineStage::FragmentShader
                                          : PipelineStage::ComputeShader)
                    || p.access != ResourceAccess::ShaderRead
                    || p.range.aspect != TextureAspect::Depth)
                    return "RHI-DEPTH-ROOT-READ-PLAN-ACCESS";
                if (p.descriptorSourceIndex >= inventory.size()
                    || p.viewSourceIndex >= inventory.size())
                    return "RHI-DEPTH-ROOT-READ-PLAN-SOURCE";
                const auto* root =
                    std::get_if<std::shared_ptr<const OriginalCommandBindlessRootSource>>(
                        &inventory[p.descriptorSourceIndex]);
                const auto* view =
                    std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(
                        &inventory[p.viewSourceIndex]);
                if (!root || !*root || !view || !*view)
                    return "RHI-DEPTH-ROOT-READ-PLAN-SOURCE";
                if (graphicsDepth) {
                    if ((*root)->sourceContractVersion() != 2u
                        || !(*root)->matchesCanonicalSources(sources(*graph_))
                        || !std::holds_alternative<
                            std::shared_ptr<const BindlessPreparedGraphicsBinding>>(
                            (*root)->originalPrepared()))
                        return "RHI-BINDLESS-GRAPHICS-DEPTH-CONTENT-ROOT";
                    bool found = false;
                    // Physical ordinals include samplers and the fault readback.
                    // Projected access ordinals deliberately omit samplers.
                    std::size_t physical = (*root)->elements().size()
                        + ((*root)->originalSnapshot()->faultReadbackBuffer() ? 1u : 0u);
                    for (const auto& domain : (*root)->ordinaryDomains()) {
                        if (const auto* id = charge())
                            return id;
                        for (const auto& element : domain.source->frozenElements()) {
                            const auto ordinal = physical++;
                            if (const auto* id = charge())
                                return id;
                            const auto& d = element.declaration;
                            if (d.sourceIndex != p.viewSourceIndex)
                                continue;
                            const auto* owner = std::get_if<
                                std::shared_ptr<const OriginalCommandTextureViewSource>>(
                                &element.resourceOwner);
                            const auto proof = (*root)->originalCompletedDepthAt(ordinal);
                            if (domain.source->sourceContractVersion() != 6u || !owner
                                || !core::SharedOwnerProjection::sameOwner(*owner, *view) || !proof
                                || proof.get() != element.completedDepth.get()
                                || !core::SharedOwnerProjection::sameOwner(
                                    proof, element.completedDepth)
                                || proof->originalViewSource().get() != view->get()
                                || !core::SharedOwnerProjection::sameOwner(
                                    proof->originalViewSource(), *view)
                                || proof->budget().contractVersion
                                    != (budget.contractVersion == 19u ? 1u : 2u)
                                || d.write.type != DescriptorType::SampledTexture
                                || d.write.resource != (*view)->resource()
                                || d.write.resourceState != p.state || d.stages != p.stages
                                || d.access != p.access)
                                return "RHI-BINDLESS-GRAPHICS-DEPTH-CONTENT-PROOF";
                            if (const auto* id =
                                    OriginalCompletedDepthTextureSource::validateOriginal(proof))
                                return id;
                            const auto& shape = (*view)->originalObject()->desc();
                            if (p.range
                                != TextureSubresourceRange{shape.baseMipLevel,
                                    shape.mipLevelCount,
                                    shape.baseArrayLayer,
                                    shape.arrayLayerCount,
                                    shape.aspect})
                                return "RHI-BINDLESS-GRAPHICS-DEPTH-CONTENT-VIEW";
                            if (const auto* id = OriginalBindlessGraphicsDepthContentPlanContract::
                                    inspectDepthRead(object.desc(),
                                        shape,
                                        p.state,
                                        p.stages,
                                        p.access,
                                        budget.contractVersion))
                                return id;
                            found = true;
                        }
                    }
                    if (!found)
                        return "RHI-BINDLESS-GRAPHICS-DEPTH-CONTENT-PROOF";
                    if (const auto* id = emit(Sequence::Step{
                            object.id(), generation, p.range, A::SampledRead, SIZE_MAX}))
                        return id;
                    continue;
                }
                const auto& sourcePlan = (*root)->originalSnapshot()->plan();
                const bool mixedStorageRoot = mixedDepth
                    && (semantics::OriginalBindlessPreparationLowering::hasStoragePlan(sourcePlan)
                        || semantics::OriginalBindlessPreparationLowering::
                            hasMixedImageResourcePlan(sourcePlan))
                    && sourcePlan.execution
                        == (depthVersion == 12u ? BindlessExecution::Native
                                                : BindlessExecution::Emulated);
                const bool depthRoot = sourcePlan.originalPreparationContractVersion
                        == OriginalCompletedDepthRootReadPlanContract::rootVersion(depthVersion)
                    || sourcePlan.originalExecutionContractVersion
                        == (depthVersion == 12u ? 6u : 5u);
                if (!mixedStorageRoot && !depthRoot)
                    return "RHI-DEPTH-ROOT-READ-PLAN-SOURCE";
                bool found = false;
                for (std::size_t i = 0u; i < (*root)->elements().size(); ++i) {
                    if (const auto* id = charge())
                        return id;
                    if ((*root)->elements()[i].declaration.sourceIndex != p.viewSourceIndex)
                        continue;
                    const auto proof = (*root)->originalCompletedDepthAt(i);
                    if (!proof || proof->originalViewSource().get() != view->get()
                        || !core::SharedOwnerProjection::sameOwner(
                            proof->originalViewSource(), *view))
                        return "RHI-DEPTH-ROOT-READ-PLAN-PROOF";
                    if (const auto* id =
                            OriginalCompletedDepthTextureSource::validateOriginal(proof))
                        return id;
                    if (proof->budget().contractVersion
                        != (depthVersion
                                    == OriginalCompletedDepthRootReadPlanContract::nativeVersion
                                ? 2u
                                : 1u))
                        return "RHI-DEPTH-ROOT-READ-PLAN-PROOF-PROFILE";
                    const auto& d = (*view)->originalViewLease()->originalObject()->desc();
                    if (p.range
                        != TextureSubresourceRange{d.baseMipLevel,
                            d.mipLevelCount,
                            d.baseArrayLayer,
                            d.arrayLayerCount,
                            d.aspect})
                        return "RHI-DEPTH-ROOT-READ-PLAN-RANGE";
                    found = true;
                }
                if (!found)
                    return "RHI-DEPTH-ROOT-READ-PLAN-PROOF";
                if (const auto* id = emit(
                        Sequence::Step{object.id(), generation, p.range, A::SampledRead, SIZE_MAX}))
                    return id;
            } else if ((depthRead || graphicsDepth || storageContent) && p.commandIndex == command
                && p.origin == Origin::RenderBinding) {
                if (storageContent)
                    return "RHI-STORAGE-CONTENT-PLAN-SOURCE";
                return "RHI-DEPTH-ROOT-READ-PLAN-SOURCE";
            } else if (p.commandIndex == command && p.origin == Origin::ShaderConsumption) {
                if (storageContent)
                    return "RHI-STORAGE-CONTENT-PLAN-SOURCE";
                if (depthRead)
                    return "RHI-DEPTH-ROOT-READ-PLAN-SOURCE";
                if (p.state != ResourceState::ShaderRead || p.access != ResourceAccess::ShaderRead)
                    return "RHI-ORIGINAL-CONTENT-PLAN-SHADER-ACCESS-UNPROVEN";
                if (const auto* id = emit(
                        Sequence::Step{object.id(), generation, p.range, A::SampledRead, SIZE_MAX}))
                    return id;
            } else if (p.commandIndex == command && p.origin == Origin::ExplicitOperand) {
                if (kind == K::TextureReadback) {
                    if (budget.contractVersion != 3u)
                        return "RHI-ORIGINAL-CONTENT-PLAN-READBACK-PROFILE";
                    const auto& c = commands()[command];
                    if (c.referenceCount != 2u)
                        return "RHI-ORIGINAL-CONTENT-PLAN-READBACK-SOURCE";
                    const auto& operand = inventory[references()[c.firstReference].sourceIndex];
                    const auto* parent =
                        std::get_if<std::shared_ptr<const OriginalCommandTextureLease>>(&operand);
                    const auto* readback =
                        std::get_if<std::shared_ptr<const OriginalCommandTextureReadbackSource>>(
                            &inventory[references()[c.firstReference + 1u].sourceIndex]);
                    if (!parent || !readback || (*readback)->contractVersion() != 2u
                        || !core::SharedOwnerProjection::sameOwner(*parent,
                            (*readback)->originalSource()->originalLease()->originalTextureOwner())
                        || (*parent)->originalObject() != &object
                        || p.range != (*readback)->originalInput()->scalarFields().range())
                        return "RHI-ORIGINAL-CONTENT-PLAN-READBACK-SOURCE";
                    if ((object.desc().usage & TextureUsage::CopySrc) == TextureUsage::None)
                        return "RHI-ORIGINAL-CONTENT-PLAN-READBACK-USAGE";
                    const char* id = nullptr;
                    const auto geometry = semantics::OriginalTextureStateRange::capture(
                        object.desc(), budget.maxContentCells, id);
                    if (!geometry)
                        return id;
                    const auto index =
                        static_cast<std::size_t>(&entry - graph_->textureEntries().data());
                    for (std::uint64_t cell = 0u; cell < geometry->rangeCellCount(p.range);
                        ++cell) {
                        if (const auto* diagnostic = charge())
                            return diagnostic;
                        ResourceState before;
                        if (const auto* diagnostic =
                                common::OriginalCommandStateSequence::resolveOriginalTextureCell(
                                    graph_, index, cell, command, before))
                            return diagnostic;
                        if (before != ResourceState::CopySrc)
                            return "RHI-ORIGINAL-CONTENT-PLAN-READBACK-STATE";
                    }
                    if (const auto* diagnostic = emit(Sequence::Step{
                            object.id(), generation, p.range, A::ReadbackRead, SIZE_MAX}))
                        return diagnostic;
                    continue;
                }
                if (kind != K::TextureBarrier && kind != K::TextureMemoryBarrier
                    && kind != K::RetainTexture)
                    return "RHI-ORIGINAL-CONTENT-PLAN-CELL-USE-UNSUPPORTED";
            }
        }
        if (kind == K::EndRenderPass)
            passBegin = SIZE_MAX;
        return nullptr;
    }

    // Dry original projection used identically by admission and Plan population.
    // All failure results clear partial counts; no extra root or journal exists.
    template <class Emit>
    ContentCellsInspection inspectRangeContentPlan(const OriginalTextureContentPlanBudget& b,
        Emit&& emit,
        const GpuTexture* selected = nullptr,
        bool originalRootAttachment = false,
        bool geometryRoot = false) const noexcept
    {
        const bool depthRead =
            OriginalCompletedDepthRootReadPlanContract::isVersion(b.contractVersion);
        const bool graphicsDepth =
            OriginalBindlessGraphicsDepthContentPlanContract::isVersion(b.contractVersion);
        const bool storageContent =
            OriginalStorageTextureContentPlanContract::isVersion(b.contractVersion);
        const bool mixed =
            OriginalStorageTextureContentPlanContract::isMixedVersion(b.contractVersion);
        if (b.contractVersion != 2u && b.contractVersion != 3u && !depthRead && !storageContent
            && !graphicsDepth)
            return {"RHI-CANONICAL-CONTENT-PLAN-VERSION"};
        if (graphicsDepth
            && (!selected || originalRootAttachment || geometryRoot
                || !hasOriginalBindlessRootShaderResourceProfile() || b.maxReadbackCommands))
            return {"RHI-BINDLESS-GRAPHICS-DEPTH-CONTENT-ROOT"};
        if (storageContent) {
            if (const auto* id = OriginalStorageTextureContentPlanContract::validateIntent(b))
                return {id};
            if (selected || originalRootAttachment || geometryRoot)
                return {"RHI-STORAGE-CONTENT-PLAN-ROOT"};
        }
        if (depthRead) {
            if (const auto* id = OriginalCompletedDepthRootReadPlanContract::validateIntent(b))
                return {id};
            if (selected || originalRootAttachment || geometryRoot
                || !hasOriginalBindlessRootShaderResourceProfile())
                return {"RHI-DEPTH-ROOT-READ-PLAN-ROOT-PROFILE"};
        }
        if (originalRootAttachment
            && (!selected || b.contractVersion != 2u
                || !(geometryRoot ? hasOriginalBindlessRootGeometryStateProfile()
                            || hasOriginalBindlessProducedGeometryStateProfile()
                                  : hasOriginalBindlessRootShaderResourceProfile())))
            return {"RHI-BINDLESS-GRAPHICS-CONTENT-ATTACHMENT-ROOT"};
        if (!b.prototypeOptIn)
            return {"RHI-CANONICAL-CONTENT-PLAN-OPT-IN"};
        if ((b.contractVersion == 2u && b.maxReadbackCommands)
            || (b.contractVersion == 3u && !b.maxReadbackCommands))
            return {"RHI-ORIGINAL-CONTENT-PLAN-READBACK-BUDGET"};
        if (!b.maxContentControls || b.maxUploadCommands || !b.maxContentCells || !b.maxContentSteps
            || !b.maxCommandVisits || !b.maxProjectionVisits || b.maxContentCells > SIZE_MAX)
            return {"RHI-ORIGINAL-CONTENT-PLAN-CELL-BUDGET"};
        ContentCellsInspection result;
        if (commands().size() > b.maxCommandVisits)
            return {"RHI-ORIGINAL-CONTENT-PLAN-COMMAND-CAPACITY"};
        result.commandVisits = commands().size();
        if (const auto* id = semantics::CommandRecordingOrder::validate(commands().data(),
                commands().size(),
                sources(*graph_).originalBudget().maxDebugDepth))
            return {id};
        if (b.contractVersion == 3u) {
            for (const auto& command : commands())
                if (command.kind == CommandUsageKind::TextureReadback) {
                    if (result.readbackCommands == b.maxReadbackCommands)
                        return {"RHI-ORIGINAL-CONTENT-PLAN-READBACK-CAPACITY"};
                    ++result.readbackCommands;
                }
            if (!result.readbackCommands)
                return {"RHI-ORIGINAL-CONTENT-PLAN-READBACK-MISSING"};
        }
        bool depthConsumed = false, storageWritten = false, storageConsumed = false;
        for (std::size_t i = 0u; i < textures_.values().size(); ++i) {
            if (selected) {
                if (result.projectionVisits == b.maxProjectionVisits)
                    return {"RHI-ORIGINAL-CONTENT-PLAN-PROJECTION-CAPACITY"};
                ++result.projectionVisits;
            }
            const auto* object = textures_.values()[i].object;
            if (selected && selected != object)
                continue;
            const bool objectDepthRead = graphicsDepth || depthRead
                || (mixed && object->desc().format == TextureFormat::D32Float);
            const bool objectStorage = storageContent && !objectDepthRead;
            const auto objectPlan = objectDepthRead && mixed
                ? OriginalStorageTextureContentPlanContract::depthPlanVersion(b.contractVersion)
                : mixed
                ? OriginalStorageTextureContentPlanContract::storagePlanVersion(b.contractVersion)
                : b.contractVersion;
            if (objectDepthRead && object->desc().format != TextureFormat::D32Float)
                return {"RHI-DEPTH-ROOT-READ-PLAN-FORMAT"};
            if (objectStorage && object->desc().format != TextureFormat::Rgba16Float)
                return {"RHI-STORAGE-CONTENT-PLAN-FORMAT"};
            bool duplicate = false;
            for (std::size_t previous = 0u; previous < i; ++previous) {
                if (result.projectionVisits == b.maxProjectionVisits)
                    return {"RHI-ORIGINAL-CONTENT-PLAN-PROJECTION-CAPACITY"};
                ++result.projectionVisits;
                if (textures_.values()[previous].object == object) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate)
                continue;
            auto* control = object->originalContent_.get();
            if (!control
                || (originalRootAttachment
                        ? !OriginalTextureContentCreationContract::isBindlessCellCreation(
                              control->contractVersion())
                        : control->contractVersion()
                            != (objectStorage
                                    ? OriginalStorageTextureContentPlanContract::controlVersion(
                                          objectPlan)
                                    : graphicsDepth ? (b.contractVersion == 19u ? 2u : 4u)
                                    : objectDepthRead
                                    ? OriginalCompletedDepthRootReadPlanContract::controlVersion(
                                          objectPlan)
                                    : 2u))
                || !control->cells_ || !control->geometry_)
                return {"RHI-ORIGINAL-CONTENT-PLAN-CELL-OBSERVATION-UNPROVEN"};
            if (const auto* id = control->diagnostic())
                return {id};
            const std::array domains{control->quota_,
                originalMetadataQuota(),
                graph_->originalMetadataQuota(),
                graph_->originalInputs()->originalMetadataQuota(),
                sources(*graph_).originalMetadataQuota()};
            if (!core::SharedOwnerProjection::hasIndependentOwners(
                    domains.size(), [&](std::size_t n) noexcept {
                        return domains[n];
                    }))
                return {"RHI-CANONICAL-CONTENT-PLAN-QUOTA-DOMAIN"};
            if (result.controls == b.maxContentControls
                || result.controls >= budget_.maxRetainedOwners - owners_)
                return {"RHI-CANONICAL-CONTENT-PLAN-CONTROL-CAPACITY"};
            ++result.controls;
            const auto count = control->geometry_->cellCount();
            if (count > b.maxContentCells - result.cells)
                return {"RHI-ORIGINAL-CONTENT-PLAN-CELL-CAPACITY"};
            const auto stamp = control->cells_->observe();
            if (!stamp)
                return {"RHI-ORIGINAL-CONTENT-PLAN-CELL-INITIAL"};
            for (std::uint64_t cell = 0u; cell < count; ++cell) {
                if (commands().size() > b.maxCommandVisits - result.commandVisits)
                    return {"RHI-ORIGINAL-CONTENT-PLAN-COMMAND-CAPACITY"};
                const auto initial = *control->cells_->valueAt(static_cast<std::size_t>(cell));
                if (graphicsDepth) {
                    if (const auto* id =
                            OriginalBindlessGraphicsDepthContentPlanContract::validateDepthControl(
                                6u,
                                b.contractVersion == 19u ? 1u : 3u,
                                control->contractVersion(),
                                initial.state == Content::State::Completed,
                                initial.generation,
                                stamp->revision,
                                count,
                                0u,
                                b.contractVersion))
                        return {id};
                }
                if (initial.state == Content::State::Proposed
                    || (initial.state == Content::State::Completed && !initial.generation))
                    return {"RHI-ORIGINAL-CONTENT-PLAN-CELL-INITIAL"};
                std::size_t begin = SIZE_MAX;
                const auto timeline =
                    OriginalTextureContentCellSequence::inspectProjected(*control->geometry_,
                        object->id(),
                        object->originalResourceRegistration().generation(),
                        cell,
                        initial.state == Content::State::Completed,
                        commands().size(),
                        commands().size(),
                        {objectStorage            ? 4u
                                : objectDepthRead ? 2u
                                                  : b.contractVersion,
                            true,
                            b.maxContentCells,
                            b.maxContentSteps,
                            b.maxCommandVisits},
                        [&](std::size_t command, auto&& consume) noexcept -> const char* {
                            return visitOriginalContentCommand(command,
                                begin,
                                *object,
                                b,
                                result.projectionVisits,
                                [&](const OriginalTextureContentCellSequence::Step& step) noexcept
                                    -> const char* {
                                    if (const auto* id = consume(step))
                                        return id;
                                    if (!control->geometry_->containsCell(step.range, cell))
                                        return nullptr;
                                    using A = OriginalTextureContentCellSequence::Action;
                                    depthConsumed = depthConsumed
                                        || (objectDepthRead && step.action == A::SampledRead);
                                    storageWritten = storageWritten
                                        || (objectStorage && step.action == A::StorageWrite);
                                    storageConsumed = storageConsumed
                                        || (objectStorage && step.action == A::SampledRead);
                                    return nullptr;
                                });
                        });
                if (timeline.diagnostic)
                    return {timeline.diagnostic};
                if (objectDepthRead && timeline.writes)
                    return {"RHI-DEPTH-ROOT-READ-PLAN-MUTATION"};
                if (timeline.steps > b.maxContentSteps - result.steps)
                    return {"RHI-ORIGINAL-CONTENT-PLAN-STEP-CAPACITY"};
                result.steps += timeline.steps;
                result.commandVisits += timeline.commandVisits;
                ++result.cells;
                if (const auto* id = emit(*object, *stamp, cell, initial, timeline))
                    return {id};
            }
        }
        if (mixed && (!depthConsumed || !storageWritten || !storageConsumed))
            return {"RHI-MIXED-DEPTH-STORAGE-CONTENT-COVERAGE"};
        if (depthRead && !result.steps)
            return {"RHI-DEPTH-ROOT-READ-PLAN-EMPTY"};
        return result.controls ? result
                               : ContentCellsInspection{"RHI-ORIGINAL-CONTENT-PLAN-CELL-EMPTY"};
    }

    template <class Emit>
    ContentCellsInspection inspectCompletedContentReadPlan(
        const OriginalTextureContentPlanBudget& b,
        Emit&& emit,
        const GpuTexture* selected = nullptr,
        bool renderReads = false,
        bool geometryRoot = false) const noexcept
    {
        if (const auto* id = OriginalCompletedTextureReadPlanContract::validateIntent(b))
            return {id};
        if (const auto* id =
                OriginalCompletedTextureReadPlanContract::validateRootProfile(b.contractVersion,
                    hasOriginalGeneralShaderResourceProfile(),
                    hasOriginalFiniteGeneralShaderResourceProfile(),
                    geometryRoot ? hasOriginalBindlessRootGeometryStateProfile()
                            || hasOriginalBindlessProducedGeometryStateProfile()
                                 : hasOriginalBindlessRootShaderResourceProfile()))
            return {id};
        if (commands().size() > b.maxCommandVisits)
            return {"RHI-COMPLETED-TEXTURE-READ-PLAN-COMMAND-CAPACITY"};
        if (const auto* id = semantics::CommandRecordingOrder::validate(commands().data(),
                commands().size(),
                sources(*graph_).originalBudget().maxDebugDepth))
            return {id};
        ContentCellsInspection result;
        result.commandVisits = commands().size();
        const auto charge = [&]() noexcept -> const char* {
            if (result.projectionVisits == b.maxProjectionVisits)
                return "RHI-COMPLETED-TEXTURE-READ-PLAN-PROJECTION-CAPACITY";
            ++result.projectionVisits;
            return nullptr;
        };
        const auto& inventory = sources(*graph_).sources();
        for (std::size_t i = 0u; i < textures_.values().size(); ++i) {
            if (const auto* id = charge())
                return {id};
            const auto* object = textures_.values()[i].object;
            if (selected && selected != object)
                continue;
            auto* control = object->originalContent_.get();
            if (!control)
                continue;
            bool duplicate = false;
            for (std::size_t previous = 0u; previous < i; ++previous) {
                if (const auto* id = charge())
                    return {id};
                if (textures_.values()[previous].object == object) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate)
                continue;
            if (const auto* id = control->diagnostic())
                return {id};
            const auto initial = control->value_.observe();
            if (!initial)
                return {"RHI-COMPLETED-TEXTURE-READ-PLAN-INITIAL"};
            if (const auto* id = OriginalCompletedTextureReadPlanContract::validateObservation(
                    control->contractVersion(),
                    initial->value.state == Content::State::Completed,
                    initial->value.generation,
                    initial->revision,
                    b.contractVersion))
                return {id};
            const std::array domains{control->quota_,
                originalMetadataQuota(),
                graph_->originalMetadataQuota(),
                graph_->originalInputs()->originalMetadataQuota(),
                sources(*graph_).originalMetadataQuota()};
            if (!core::SharedOwnerProjection::hasIndependentOwners(
                    domains.size(), [&](std::size_t n) noexcept {
                        return domains[n];
                    }))
                return {"RHI-CANONICAL-CONTENT-PLAN-QUOTA-DOMAIN"};
            if (result.controls == b.maxContentControls
                || result.controls >= budget_.maxRetainedOwners - owners_)
                return {"RHI-CANONICAL-CONTENT-PLAN-CONTROL-CAPACITY"};
            ++result.controls;
            bool read = false;
            bool consumed = false;
            for (const auto& entry : graph_->textureEntries()) {
                if (const auto* id = charge())
                    return {id};
                const auto& p = entry.point;
                if (Projection::originalTexture(inventory[p.sourceIndex]) != object)
                    continue;
                if (entry.observation.resourceGeneration()
                    != object->originalResourceRegistration().generation())
                    return {"RHI-ORIGINAL-CONTENT-PLAN-GENERATION"};
                if (p.range != defaultTextureSubresourceRange(object->desc()))
                    return {"RHI-COMPLETED-SHADER-TEXTURE-VIEW-RANGE"};
                if ((OriginalCompletedTextureReadPlanContract::isRootRead(b.contractVersion)
                        && p.origin == Projection::Point::Origin::BindlessRoot)
                    || p.origin == Projection::Point::Origin::ShaderConsumption
                    || (renderReads && p.origin == Projection::Point::Origin::RenderBinding)) {
                    if (p.viewSourceIndex >= inventory.size())
                        return {"RHI-COMPLETED-TEXTURE-READ-PLAN-VIEW-SOURCE"};
                    const auto* view =
                        std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(
                            &inventory[p.viewSourceIndex]);
                    if (!view || !*view
                        || (*view)->originalParentLease()->originalObject() != object)
                        return {"RHI-COMPLETED-TEXTURE-READ-PLAN-VIEW-SOURCE"};
                    if (const auto* id =
                            OriginalCompletedTextureReadPlanContract::inspectViewBinding(
                                object->desc(),
                                (*view)->originalObject()->desc(),
                                p.state,
                                p.stages,
                                p.access,
                                b.contractVersion))
                        return {id};
                    if (result.steps == b.maxContentSteps)
                        return {"RHI-COMPLETED-TEXTURE-READ-PLAN-STEP-CAPACITY"};
                    ++result.steps;
                    read = true;
                    if (OriginalCompletedTextureReadPlanContract::isRootRead(b.contractVersion))
                        consumed = consumed
                            || commands()[p.commandIndex].kind == CommandUsageKind::Dispatch
                            || commands()[p.commandIndex].kind == CommandUsageKind::Draw
                            || (geometryRoot
                                && (commands()[p.commandIndex].kind == CommandUsageKind::DrawIndexed
                                    || commands()[p.commandIndex].kind
                                        == CommandUsageKind::DrawIndexedIndirect));
                } else {
                    const auto kind = commands()[p.commandIndex].kind;
                    if (kind != CommandUsageKind::TextureBarrier
                        && kind != CommandUsageKind::TextureMemoryBarrier
                        && kind != CommandUsageKind::RetainTexture)
                        return {"RHI-COMPLETED-TEXTURE-READ-PLAN-UNSUPPORTED-USE"};
                }
            }
            if (!read)
                return {"RHI-COMPLETED-TEXTURE-READ-PLAN-READ-MISSING"};
            if (OriginalCompletedTextureReadPlanContract::isRootRead(b.contractVersion)
                && !consumed)
                return {"RHI-BINDLESS-COMPLETED-TEXTURE-READ-CONSUMPTION-MISSING"};
            if (const auto* id = emit(*object, *initial))
                return {id};
        }
        return result.controls ? result
                               : ContentCellsInspection{"RHI-COMPLETED-TEXTURE-READ-PLAN-EMPTY"};
    }

    template <class Charge, class Emit>
    const char* inspectProducedImageCells(const GpuTexture& object,
        const OriginalFiniteGraphicsContentPlanBudget& budget,
        OriginalFiniteGraphicsContentPlanContract::Counts& counts,
        Charge&& charge,
        Emit&& emit) const noexcept
    {
        using Contract = OriginalBindlessProducedImageReadContract;
        const auto& intent = budget.producedImageRead;
        const auto* control = object.originalContent_.get();
        if (intent.empty() || !control || !Contract::acceptsControl(control->contractVersion())
            || !control->cells_ || !control->geometry_)
            return "RHI-BINDLESS-PRODUCED-IMAGE-CONTROL";
        if (counts.producedImageControls == intent.maxControls)
            return "RHI-BINDLESS-PRODUCED-IMAGE-CONTENT-CAPACITY";
        const auto cellCount = control->geometry_->cellCount();
        if (!cellCount || cellCount > intent.maxCells - counts.producedImageCells)
            return "RHI-BINDLESS-PRODUCED-IMAGE-CONTENT-CAPACITY";
        const auto stamp = control->cells_->observe();
        if (!stamp)
            return "RHI-BINDLESS-PRODUCED-IMAGE-CONTENT-INITIAL";
        const auto& inventory = sources(*graph_).sources();
        std::uint64_t reads = 0u;
        for (const auto& entry : graph_->textureEntries()) {
            if (const auto* id = charge())
                return id;
            const auto& p = entry.point;
            if (Projection::originalTexture(inventory[p.sourceIndex]) != &object)
                continue;
            const auto kind = commands()[p.commandIndex].kind;
            if (p.origin != Projection::Point::Origin::BindlessRoot) {
                if (p.origin != Projection::Point::Origin::ExplicitOperand
                    || (kind != CommandUsageKind::TextureBarrier
                        && kind != CommandUsageKind::TextureMemoryBarrier
                        && kind != CommandUsageKind::RetainTexture))
                    return "RHI-BINDLESS-PRODUCED-IMAGE-MUTATION";
                continue;
            }
            if (const auto* id = Contract::inspectAccess(p.state, p.stages, p.access, kind))
                return id;
            if (p.descriptorSourceIndex >= inventory.size()
                || p.viewSourceIndex >= inventory.size())
                return "RHI-BINDLESS-PRODUCED-IMAGE-SOURCE";
            const auto* root =
                std::get_if<std::shared_ptr<const OriginalCommandBindlessRootSource>>(
                    &inventory[p.descriptorSourceIndex]);
            const auto* view = std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(
                &inventory[p.viewSourceIndex]);
            if (!root || !view || !(*root)->matchesCanonicalSources(sources(*graph_))
                || (*view)->originalParentLease()->originalObject() != &object
                || !semantics::OriginalBindlessPreparationLowering::hasProducedImageReadPlan(
                    (*root)->originalSnapshot()->plan())
                || control->contractVersion()
                    != ((*root)->originalSnapshot()->plan().execution == BindlessExecution::Native
                            ? 6u
                            : 7u))
                return "RHI-BINDLESS-PRODUCED-IMAGE-SOURCE";
            const auto& shape = (*view)->originalObject()->desc();
            if (const auto* id = Contract::inspectView(object.desc(), shape))
                return id;
            if (p.range
                != TextureSubresourceRange{shape.baseMipLevel,
                    shape.mipLevelCount,
                    shape.baseArrayLayer,
                    shape.arrayLayerCount,
                    shape.aspect})
                return "RHI-BINDLESS-PRODUCED-IMAGE-RANGE";
            std::size_t matched = 0u;
            for (const auto& element : (*root)->elements()) {
                if (const auto* id = charge())
                    return id;
                const auto& d = element.declaration;
                if (d.sourceIndex != p.viewSourceIndex)
                    continue;
                const auto* owner =
                    std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(
                        &element.owner);
                if (!owner || !core::SharedOwnerProjection::sameOwner(*owner, *view)
                    || d.write.type != DescriptorType::SampledTexture
                    || d.write.resourceState != p.state || d.stages != p.stages
                    || d.access != p.access)
                    return "RHI-BINDLESS-PRODUCED-IMAGE-DECLARATION";
                ++matched;
            }
            if (matched != 1u || reads == intent.maxSteps)
                return "RHI-BINDLESS-PRODUCED-IMAGE-DECLARATION";
            ++reads;
        }
        if (!reads || reads > (intent.maxSteps - counts.producedImageSteps) / cellCount)
            return "RHI-BINDLESS-PRODUCED-IMAGE-CONTENT-CAPACITY";

        struct ReadTimeline {
            std::uint64_t writes = 0u;
            bool stored = true;
        };

        for (std::uint64_t cell = 0u; cell < cellCount; ++cell) {
            if (const auto* id = charge())
                return id;
            const auto* initial = control->cells_->valueAt(static_cast<std::size_t>(cell));
            if (!initial || initial->state != Content::State::Completed || !initial->generation)
                return "RHI-BINDLESS-PRODUCED-IMAGE-CONTENT-INCOMPLETE";
            if (const auto* id = emit(object, *stamp, cell, *initial, ReadTimeline{}))
                return id;
        }
        ++counts.producedImageControls;
        counts.producedImageCells += cellCount;
        counts.producedImageSteps += reads * cellCount;
        return nullptr;
    }

    struct MixedContentInspection {
        const char* diagnostic = nullptr;
        OriginalFiniteGraphicsContentPlanContract::Counts counts;
        OriginalBindlessGraphicsDepthContentPlanContract::Counts depth;
    };

    template <class CompletedEmit, class AttachmentEmit>
    MixedContentInspection inspectOriginalMixedContentPlan(
        const OriginalFiniteGraphicsContentPlanBudget& b,
        const std::shared_ptr<core::CpuMemoryQuota>& streamQuota,
        CompletedEmit&& completedEmit,
        AttachmentEmit&& attachmentEmit,
        const OriginalBindlessGraphicsDepthContentPlanContract::Budget* depth =
            nullptr) const noexcept
    {
        using Contract = OriginalFiniteGraphicsContentPlanContract;
        const bool root = b.contractVersion == 9u;
        if (const auto* id = root ? OriginalBindlessGraphicsContentPlanContract::validateIntent(b)
                                  : Contract::validateIntent(b))
            return {id, {}};
        if (depth) {
            if (const auto* id =
                    OriginalBindlessGraphicsDepthContentPlanContract::validateIntent(*depth))
                return {id, {}};
        }
        if (root
            && !(b.bindlessGeometryContractVersion == 2u
                    ? hasOriginalBindlessProducedGeometryStateProfile()
                    : b.bindlessGeometryContractVersion
                    ? hasOriginalBindlessRootGeometryStateProfile()
                    : hasOriginalBindlessRootShaderResourceProfile()))
            return {"RHI-BINDLESS-GRAPHICS-CONTENT-ROOT-PROFILE", {}};
        MixedContentInspection result;
        auto& c = result.counts;
        auto& d = result.depth;
        if (commands().size() > b.maxCommandVisits)
            return {"RHI-FINITE-GRAPHICS-CONTENT-VISIT-CAPACITY", {}};
        // This composition walks state/provenance entries, charged below as
        // projection visits. Only the existing read/cell inspectors traverse
        // the command stream: read-order, cell-order and cell-timeline passes.
        // Do not charge a fourth command traversal which never takes place.
        const auto charge = [&]() noexcept -> const char* {
            if (d.depthProjectionVisits > b.maxProjectionVisits - c.projectionVisits
                || c.projectionVisits + d.depthProjectionVisits == b.maxProjectionVisits)
                return "RHI-FINITE-GRAPHICS-CONTENT-VISIT-CAPACITY";
            ++c.projectionVisits;
            return nullptr;
        };
        const auto& inventory = sources(*graph_).sources();
        for (std::size_t i = 0u; i < textures_.values().size(); ++i) {
            if (const auto* id = charge())
                return {id, {}};
            const auto* object = textures_.values()[i].object;
            auto* control = object->originalContent_.get();
            if (!control) {
                if (root)
                    return {"RHI-BINDLESS-GRAPHICS-CONTENT-CONTROL-REQUIRED", {}};
                continue;
            }
            bool duplicate = false;
            for (std::size_t previous = 0u; previous < i; ++previous) {
                if (const auto* id = charge())
                    return {id, {}};
                const auto* earlier = textures_.values()[previous].object;
                if (earlier == object) {
                    duplicate = true;
                    break;
                }
                if (earlier->originalContent_
                    && core::SharedOwnerProjection::sameControlBlock(
                        earlier->originalContent_->quota_, control->quota_))
                    return {"RHI-FINITE-GRAPHICS-CONTENT-CONTROL-QUOTA-DOMAIN", {}};
            }
            if (duplicate)
                continue;
            if (const auto* id = control->diagnostic())
                return {id, {}};
            const std::array domains{control->quota_,
                streamQuota,
                originalMetadataQuota(),
                graph_->originalMetadataQuota(),
                graph_->originalInputs()->originalMetadataQuota(),
                sources(*graph_).originalMetadataQuota()};
            if (!core::SharedOwnerProjection::hasIndependentOwners(
                    domains.size(), [&](std::size_t n) noexcept {
                        return domains[n];
                    }))
                return {"RHI-FINITE-GRAPHICS-CONTENT-CONTROL-QUOTA-DOMAIN", {}};
            if (owners_ >= budget_.maxRetainedOwners
                || c.completedControls + c.attachmentControls + d.depthControls
                        + c.producedImageControls
                    >= budget_.maxRetainedOwners - owners_ - 1u)
                return {"RHI-FINITE-GRAPHICS-CONTENT-OWNER-CAPACITY", {}};
            if (root
                && OriginalBindlessProducedImageReadContract::acceptsControl(
                    control->contractVersion())) {
                if (const auto* id =
                        inspectProducedImageCells(*object, b, c, charge, attachmentEmit))
                    return {id, {}};
                continue;
            }
            bool ordinaryDepth = false;
            if (depth && object->desc().format == TextureFormat::D32Float) {
                for (const auto& entry : graph_->textureEntries()) {
                    if (const auto* id = charge())
                        return {id, {}};
                    if (Projection::originalTexture(inventory[entry.point.sourceIndex]) == object
                        && entry.point.origin == Projection::Point::Origin::BindlessRoot)
                        ordinaryDepth = true;
                }
            }
            if (ordinaryDepth) {
                if (d.depthControls == depth->maxDepthControls
                    || d.depthCells == depth->maxDepthCells)
                    return {"RHI-BINDLESS-GRAPHICS-DEPTH-CONTENT-CAPACITY", {}};
                const OriginalTextureContentPlanBudget cells{depth->contractVersion,
                    true,
                    1u,
                    0u,
                    depth->maxDepthCells - d.depthCells,
                    depth->maxDepthReadSteps - d.depthReadSteps,
                    b.maxCommandVisits - c.commandVisits - d.depthCommandVisits,
                    b.maxProjectionVisits - c.projectionVisits - d.depthProjectionVisits,
                    0u};
                const auto observed = inspectRangeContentPlan(cells, attachmentEmit, object);
                if (observed.diagnostic)
                    return {observed.diagnostic, {}};
                if (observed.controls != 1u || !observed.cells
                    || (depth->contractVersion == 18u && observed.cells != 1u) || !observed.steps)
                    return {"RHI-BINDLESS-GRAPHICS-DEPTH-CONTENT-INCOMPLETE", {}};
                ++d.depthControls;
                d.depthCells += observed.cells;
                d.depthReadSteps += observed.steps;
                d.depthCommandVisits += observed.commandVisits;
                d.depthProjectionVisits += observed.projectionVisits;
            } else if (control->contractVersion()
                == (root && b.bindlessCompletedReadContractVersion != 1u
                            && b.bindlessCompletedReadContractVersion != 3u
                            && b.bindlessCompletedReadContractVersion != 4u
                        ? 3u
                        : 1u)) {
                if (!root && object->desc().mipLevelCount != 2u)
                    return {"RHI-FINITE-GRAPHICS-CONTENT-SAMPLED-MIPS", {}};
                if (c.completedControls == b.maxCompletedControls)
                    return {"RHI-FINITE-GRAPHICS-CONTENT-CONTROL-CAPACITY", {}};
                std::uint64_t bindingReads = 0u, drawReads = 0u;
                for (const auto& entry : graph_->textureEntries()) {
                    if (const auto* id = charge())
                        return {id, {}};
                    const auto& p = entry.point;
                    if (Projection::originalTexture(inventory[p.sourceIndex]) != object)
                        continue;
                    if (root) {
                        if (p.origin != Projection::Point::Origin::BindlessRoot)
                            continue;
                        const auto kind = commands()[p.commandIndex].kind;
                        const auto allowed =
                            OriginalBindlessGraphicsContentPlanContract::consumptionStages(kind);
                        if (!OriginalBindlessGraphicsContentPlanContract::consumes(
                                kind, b.bindlessGeometryContractVersion)
                            || p.stages == PipelineStage::None
                            || (static_cast<std::uint32_t>(p.stages)
                                & ~static_cast<std::uint32_t>(allowed)))
                            return {"RHI-BINDLESS-GRAPHICS-CONTENT-SAMPLED-STAGE", {}};
                        if (drawReads == b.maxSampledSteps)
                            return {"RHI-FINITE-GRAPHICS-CONTENT-STEP-CAPACITY", {}};
                        ++drawReads;
                        continue;
                    }
                    if (p.origin != Projection::Point::Origin::RenderBinding
                        && p.origin != Projection::Point::Origin::ShaderConsumption)
                        continue;
                    if (p.viewSourceIndex >= inventory.size())
                        return {"RHI-FINITE-GRAPHICS-CONTENT-SAMPLED-VIEW", {}};
                    const auto* view =
                        std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(
                            &inventory[p.viewSourceIndex]);
                    if (!view || !*view
                        || (*view)->originalParentLease()->originalObject() != object)
                        return {"RHI-FINITE-GRAPHICS-CONTENT-SAMPLED-VIEW", {}};
                    const auto origin = p.origin == Projection::Point::Origin::RenderBinding
                        ? Contract::SampledOrigin::RenderBinding
                        : Contract::SampledOrigin::ShaderConsumption;
                    if (const auto* id = Contract::validateSampledUse(object->desc(),
                            (*view)->originalObject()->desc(),
                            p.state,
                            p.stages,
                            p.access,
                            origin))
                        return {id, {}};
                    if (origin == Contract::SampledOrigin::RenderBinding)
                        ++bindingReads;
                    else
                        ++drawReads;
                }
                if ((!root && !bindingReads) || !drawReads)
                    return {"RHI-FINITE-GRAPHICS-CONTENT-SAMPLED-ORIGINS", {}};
                if (c.renderBindingReads > b.maxSampledSteps
                    || c.drawReads > b.maxSampledSteps - c.renderBindingReads
                    || bindingReads > b.maxSampledSteps - c.renderBindingReads - c.drawReads
                    || drawReads
                        > b.maxSampledSteps - c.renderBindingReads - c.drawReads - bindingReads)
                    return {"RHI-FINITE-GRAPHICS-CONTENT-STEP-CAPACITY", {}};
                const OriginalTextureContentPlanBudget read{root
                        ? OriginalCompletedTextureReadPlanContract::graphicsReadVersion(
                              b.bindlessCompletedReadContractVersion)
                        : 5u,
                    true,
                    1u,
                    0u,
                    0u,
                    bindingReads + drawReads,
                    b.maxCommandVisits - c.commandVisits - d.depthCommandVisits,
                    b.maxProjectionVisits - c.projectionVisits - d.depthProjectionVisits,
                    0u};
                const auto observed = inspectCompletedContentReadPlan(
                    read, completedEmit, object, !root, b.bindlessGeometryContractVersion != 0u);
                if (observed.diagnostic)
                    return {observed.diagnostic, {}};
                if (observed.controls != 1u || observed.steps != bindingReads + drawReads)
                    return {"RHI-FINITE-GRAPHICS-CONTENT-SAMPLED-COVERAGE", {}};
                ++c.completedControls;
                c.renderBindingReads += bindingReads;
                c.drawReads += drawReads;
                c.commandVisits += observed.commandVisits;
                c.projectionVisits += observed.projectionVisits;
            } else if (root ? OriginalTextureContentCreationContract::isBindlessCellCreation(
                                  control->contractVersion())
                            : control->contractVersion() == 2u) {
                if (c.attachmentControls == b.maxAttachmentControls
                    || c.attachmentCells == b.maxAttachmentCells)
                    return {"RHI-FINITE-GRAPHICS-CONTENT-CONTROL-CAPACITY", {}};
                if (!control->cells_ || !control->geometry_
                    || control->geometry_->cellCount() != 1u)
                    return {"RHI-FINITE-GRAPHICS-CONTENT-ATTACHMENT-CELLS", {}};
                const auto* initial = control->cells_->valueAt(0u);
                if (!initial
                    || (root ? OriginalBindlessAttachmentClearContract::validateObservation(
                                   b.attachmentClear,
                                   control->contractVersion(),
                                   initial->state == Content::State::Uninitialized,
                                   initial->state == Content::State::Completed,
                                   initial->generation)
                                != nullptr
                             : initial->state != Content::State::Uninitialized
                                || initial->generation != 0u))
                    return {"RHI-FINITE-GRAPHICS-CONTENT-ATTACHMENT-INITIAL", {}};
                bool attachment = false;
                for (const auto& entry : graph_->textureEntries()) {
                    if (const auto* id = charge())
                        return {id, {}};
                    const auto& p = entry.point;
                    if (Projection::originalTexture(inventory[p.sourceIndex]) != object)
                        continue;
                    if (root && p.origin != Projection::Point::Origin::ExplicitOperand)
                        return {"RHI-BINDLESS-GRAPHICS-CONTENT-ATTACHMENT-ORIGIN", {}};
                    if (p.origin != Projection::Point::Origin::ExplicitOperand
                        || commands()[p.commandIndex].kind != CommandUsageKind::BeginRenderPass)
                        continue;
                    const auto* descriptor = graph_->originalInputs()->descriptorAt(p.commandIndex);
                    const auto* pass = descriptor
                        ? std::get_if<std::shared_ptr<const RenderPassArguments>>(descriptor)
                        : nullptr;
                    const auto* view =
                        std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(
                            &inventory[p.sourceIndex]);
                    if (!pass || !*pass || !view || !*view)
                        return {"RHI-FINITE-GRAPHICS-CONTENT-ATTACHMENT-OWNER", {}};
                    const auto& desc = (*pass)->desc();
                    if (attachment || !desc.subpasses.empty()
                        || (!root
                            && (desc.hasDepthStencilAttachment
                                || desc.colorAttachments.size() != 1u)))
                        return {"RHI-FINITE-GRAPHICS-CONTENT-ATTACHMENT-PASS", {}};
                    std::uint64_t texelBytes = 0u;
                    if (root) {
                        LoadOp load = LoadOp::DontCare;
                        StoreOp store = StoreOp::DontCare;
                        bool found = false, resolve = false;
                        for (const auto& color : desc.colorAttachments) {
                            if (const auto* id = charge())
                                return {id, {}};
                            if (color.target != (*view)->resource())
                                continue;
                            if (found || color.format != object->desc().format)
                                return {"RHI-BINDLESS-GRAPHICS-CONTENT-ATTACHMENT-OWNER", {}};
                            found = true;
                            load = color.loadOp;
                            store = color.storeOp;
                            resolve = color.hasResolveTarget;
                        }
                        if (desc.hasDepthStencilAttachment
                            && desc.depthStencilAttachment.target == (*view)->resource()) {
                            const auto& depth = desc.depthStencilAttachment;
                            if (found || depth.format != TextureFormat::D32Float
                                || depth.depthReadOnly)
                                return {"RHI-BINDLESS-GRAPHICS-CONTENT-ATTACHMENT-OWNER", {}};
                            found = true;
                            load = depth.depthLoadOp;
                            store = depth.depthStoreOp;
                        }
                        if (!found)
                            return {"RHI-BINDLESS-GRAPHICS-CONTENT-ATTACHMENT-OWNER", {}};
                        if (const auto* id =
                                OriginalBindlessGraphicsContentPlanContract::inspectAttachment(
                                    object->desc(),
                                    (*view)->originalObject()->desc(),
                                    control->budget_,
                                    load,
                                    store,
                                    resolve,
                                    desc.layeredRendering,
                                    texelBytes))
                            return {id, {}};
                    } else {
                        const auto& color = desc.colorAttachments[0u];
                        if (color.target != (*view)->resource()
                            || color.format != object->desc().format)
                            return {"RHI-FINITE-GRAPHICS-CONTENT-ATTACHMENT-OWNER", {}};
                        if (const auto* id = Contract::inspectAttachment(object->desc(),
                                (*view)->originalObject()->desc(),
                                control->budget_,
                                color.loadOp,
                                color.storeOp,
                                color.hasResolveTarget,
                                desc.layeredRendering,
                                texelBytes))
                            return {id, {}};
                    }
                    attachment = true;
                }
                if (!attachment)
                    return {"RHI-FINITE-GRAPHICS-CONTENT-ATTACHMENT-MISSING", {}};
                const OriginalTextureContentPlanBudget cells{2u,
                    true,
                    1u,
                    0u,
                    1u,
                    b.maxAttachmentSteps - c.attachmentSteps,
                    b.maxCommandVisits - c.commandVisits - d.depthCommandVisits,
                    b.maxProjectionVisits - c.projectionVisits - d.depthProjectionVisits,
                    0u};
                const auto observed = inspectRangeContentPlan(
                    cells, attachmentEmit, object, root, b.bindlessGeometryContractVersion != 0u);
                if (observed.diagnostic)
                    return {observed.diagnostic, {}};
                if (observed.controls != 1u || observed.cells != 1u || observed.steps != 2u)
                    return {"RHI-FINITE-GRAPHICS-CONTENT-ATTACHMENT-COVERAGE", {}};
                ++c.attachmentControls;
                ++c.attachmentCells;
                c.attachmentSteps += observed.steps;
                c.commandVisits += observed.commandVisits;
                c.projectionVisits += observed.projectionVisits;
            } else
                return {"RHI-FINITE-GRAPHICS-CONTENT-CONTROL-PROFILE", {}};
            if (d.depthCommandVisits > b.maxCommandVisits - c.commandVisits
                || d.depthProjectionVisits > b.maxProjectionVisits - c.projectionVisits
                || c.attachmentSteps > b.maxAttachmentSteps)
                return {"RHI-FINITE-GRAPHICS-CONTENT-VISIT-CAPACITY", {}};
        }
        if (depth) {
            d.graphics = c;
            result.diagnostic =
                OriginalBindlessGraphicsDepthContentPlanContract::validateCounts(*depth, d);
            return result;
        }
        return {root ? OriginalBindlessGraphicsContentPlanContract::validateCounts(b, c)
                     : Contract::validateCounts(b, c),
            c};
    }

    const char* materializeOriginalGraphicsPlan(const OriginalCommandStateSequenceBudget& sequence,
        const OriginalFiniteGraphicsContentPlanBudget& content,
        const std::shared_ptr<core::CpuMemoryQuota>& streamQuota,
        const OriginalBindlessGraphicsDepthContentPlanContract::Budget* depth = nullptr)
    {
        if (status_ != Status::Prepared || plan_)
            return "RHI-ORIGINAL-PLAN-STATUS";
        const auto state = preflightOriginalAccesses(sequence);
        if (state.diagnostic)
            return state.diagnostic;
        const auto mixed = inspectOriginalMixedContentPlan(
            content,
            streamQuota,
            [](const auto&, const auto&) noexcept -> const char* {
                return nullptr;
            },
            [](const auto&, const auto&, auto, const auto&, const auto&) noexcept -> const char* {
                return nullptr;
            },
            depth);
        if (mixed.diagnostic)
            return mixed.diagnostic;
        if (const auto* id = preflightPlanWrites())
            return id;
        if (state.stateCells > SIZE_MAX || state.steps > SIZE_MAX)
            return "RHI-ORIGINAL-PLAN-SIZE";
        const auto count = mixed.counts.completedControls + mixed.counts.attachmentControls
            + mixed.depth.depthControls + mixed.counts.producedImageControls;
        if (mixed.counts.completedControls > SIZE_MAX || mixed.counts.attachmentControls > SIZE_MAX
            || mixed.depth.depthControls > SIZE_MAX - mixed.counts.attachmentControls
            || mixed.counts.attachmentCells > SIZE_MAX
            || mixed.depth.depthCells > SIZE_MAX - mixed.counts.attachmentCells
            || mixed.counts.producedImageControls
                > SIZE_MAX - mixed.counts.attachmentControls - mixed.depth.depthControls
            || mixed.counts.producedImageCells
                > SIZE_MAX - mixed.counts.attachmentCells - mixed.depth.depthCells)
            return "RHI-ORIGINAL-PLAN-SIZE";
        const auto& source = sources(*graph_).originalBudget();
        if (count >= budget_.maxRetainedOwners - owners_ || owners_ >= source.maxGraphOwners
            || count >= source.maxGraphOwners - owners_)
            return "RHI-FINITE-GRAPHICS-CONTENT-OWNER-CAPACITY";
        try {
            core::CpuMemoryQuotaScope scope(originalMetadataQuota());
            Plan plan(sequence,
                static_cast<std::size_t>(state.stateCells),
                static_cast<std::size_t>(state.steps),
                commands().size(),
                {depth ? depth->contractVersion : content.contractVersion, true},
                content,
                mixed.counts,
                depth ? *depth : OriginalBindlessGraphicsDepthContentPlanContract::Budget{},
                mixed.depth);
            if (const auto* id = populatePlan(plan))
                return id;
            const auto populated = inspectOriginalMixedContentPlan(
                content,
                streamQuota,
                [&](const GpuTexture& object,
                    const Content::Observation& stamp) noexcept -> const char* {
                    return plan.contents.append(
                               {&object, object.originalContent_.get(), stamp, 0u, {}, {}})
                        ? nullptr
                        : "RHI-FINITE-GRAPHICS-CONTENT-CONTROL-CAPACITY";
                },
                [&](const GpuTexture& object,
                    const auto& stamp,
                    auto index,
                    const auto& initial,
                    const auto& timeline) noexcept -> const char* {
                    return plan.appendRangeCell(object, stamp, index, initial, timeline);
                },
                depth);
            if (populated.diagnostic)
                return populated.diagnostic;
            if (std::tie(populated.counts.completedControls,
                    populated.counts.attachmentControls,
                    populated.counts.attachmentCells,
                    populated.counts.renderBindingReads,
                    populated.counts.drawReads,
                    populated.counts.attachmentSteps,
                    populated.counts.commandVisits,
                    populated.counts.projectionVisits,
                    populated.counts.producedImageControls,
                    populated.counts.producedImageCells,
                    populated.counts.producedImageSteps)
                    != std::tie(mixed.counts.completedControls,
                        mixed.counts.attachmentControls,
                        mixed.counts.attachmentCells,
                        mixed.counts.renderBindingReads,
                        mixed.counts.drawReads,
                        mixed.counts.attachmentSteps,
                        mixed.counts.commandVisits,
                        mixed.counts.projectionVisits,
                        mixed.counts.producedImageControls,
                        mixed.counts.producedImageCells,
                        mixed.counts.producedImageSteps)
                || std::tie(populated.depth.depthControls,
                       populated.depth.depthCells,
                       populated.depth.depthReadSteps,
                       populated.depth.depthCommandVisits,
                       populated.depth.depthProjectionVisits)
                    != std::tie(mixed.depth.depthControls,
                        mixed.depth.depthCells,
                        mixed.depth.depthReadSteps,
                        mixed.depth.depthCommandVisits,
                        mixed.depth.depthProjectionVisits)
                || plan.steps.values().size() != state.steps)
                return "RHI-FINITE-GRAPHICS-CONTENT-REPLAY";
            plan.mixedCounts = populated.counts;
            plan_.emplace(std::move(plan));
            return nullptr;
        } catch (const std::bad_alloc&) {
            return "RHI-ORIGINAL-PLAN-ALLOCATION";
        } catch (const std::length_error&) {
            return "RHI-ORIGINAL-PLAN-ALLOCATION";
        }
    }

    // The same original graph supplies resource/control identity and authored
    // transition ordinals. No caller flag substitutes for actual initialization.
    ContentCellsInspection inspectUndefinedTextureUploads(
        const OriginalTextureContentPlanBudget& budget) const noexcept
    {
        using Contract = OriginalUndefinedTextureUploadPlanContract;
        using K = CommandUsageKind;
        ContentCellsInspection result;
        if ((result.diagnostic = Contract::validateIntent(budget)))
            return result;
        if (budget_.contractVersion != 1u || graph_->stateContractVersion() != 1u
            || graph_->originalInputs()->originalBudget().contractVersion != 1u
            || !budget_.bindlessRoots.empty() || !graph_->budget_.bindlessRoots.empty())
            return {"RHI-UNDEFINED-TEXTURE-UPLOAD-PLAN-ROOT-PROFILE"};
        const auto chargeProjection = [&]() noexcept {
            if (result.projectionVisits == budget.maxProjectionVisits)
                return false;
            ++result.projectionVisits;
            return true;
        };
        for (std::size_t command = 0u; command < commands().size(); ++command) {
            if (result.commandVisits == budget.maxCommandVisits)
                return {"RHI-UNDEFINED-TEXTURE-UPLOAD-PLAN-COMMAND-CAPACITY"};
            ++result.commandVisits;
            const auto kind = commands()[command].kind;
            if (kind != K::BeginTransferPass && kind != K::EndTransferPass && kind != K::Finish
                && kind != K::TextureBarrier && kind != K::TextureUpload
                && kind != K::BufferMemoryBarrier && kind != K::RetainTexture)
                return {"RHI-UNDEFINED-TEXTURE-UPLOAD-PLAN-COMMAND"};
            const auto entry = contentUploadAt(command);
            if (kind != K::TextureUpload)
                continue;
            if (!entry || entry->control->contractVersion() != 3u)
                return {"RHI-UNDEFINED-TEXTURE-UPLOAD-PLAN-CONTROL"};
            if (const auto* id = entry->control->diagnostic())
                return {id};
            const auto observation = entry->control->value_.observe();
            if (!observation || observation->value.state != Content::State::Uninitialized
                || observation->value.generation)
                return {"RHI-UNDEFINED-TEXTURE-UPLOAD-PLAN-INITIAL"};
            const std::array domains{entry->control->quota_,
                originalMetadataQuota(),
                graph_->originalMetadataQuota(),
                graph_->originalInputs()->originalMetadataQuota(),
                sources(*graph_).originalMetadataQuota()};
            if (!core::SharedOwnerProjection::hasIndependentOwners(
                    domains.size(), [&](std::size_t i) noexcept {
                        return domains[i];
                    }))
                return {"RHI-CANONICAL-CONTENT-PLAN-QUOTA-DOMAIN"};
            if (const auto* id = semantics::OriginalTextureContentUploadValidation::inspect(
                    *entry->source->originalInput(), entry->object->desc()))
                return {id};
            std::uint64_t texels = 0u;
            if (const auto* id = semantics::TextureUploadDestinationValidation::validate(
                    entry->object->desc(), texels, ResourceState::Undefined))
                return {id};
            if (result.controls == budget.maxContentControls)
                return {"RHI-CANONICAL-CONTENT-PLAN-CONTROL-CAPACITY"};
            if (result.controls == budget.maxUploadCommands)
                return {"RHI-CANONICAL-CONTENT-PLAN-UPLOAD-CAPACITY"};
            if (budget.maxContentSteps - result.steps < 2u)
                return {"RHI-UNDEFINED-TEXTURE-UPLOAD-PLAN-STEP-CAPACITY"};
            std::uint64_t transitions = 0u;
            for (std::size_t other = 0u; other < commands().size(); ++other) {
                if (!chargeProjection())
                    return {"RHI-UNDEFINED-TEXTURE-UPLOAD-PLAN-PROJECTION-CAPACITY"};
                const auto upload = contentUploadAt(other);
                if (upload && upload->object == entry->object && other != command)
                    return {"RHI-UNDEFINED-TEXTURE-UPLOAD-PLAN-DUPLICATE"};
                const auto barrier = transitionAt(other);
                if (!barrier || barrier->resource != entry->object->id())
                    continue;
                if (commands()[other].kind != K::TextureBarrier || other >= command
                    || barrier->before != ResourceState::Undefined
                    || barrier->after != ResourceState::CopyDst
                    || barrier->textureSubresources
                        != defaultTextureSubresourceRange(entry->object->desc()))
                    return {"RHI-UNDEFINED-TEXTURE-UPLOAD-PLAN-TRANSITION"};
                if (const auto* id =
                        semantics::TransferDependencyLowering::validateTransitionIntent(*barrier,
                            ResourceState::Undefined,
                            true,
                            defaultTextureSubresourceRange(entry->object->desc())))
                    return {id};
                ++transitions;
            }
            if (transitions != 1u)
                return {"RHI-UNDEFINED-TEXTURE-UPLOAD-PLAN-TRANSITION"};
            ++result.controls;
            result.steps += 2u;
        }
        if (!result.controls || result.controls > budget_.maxRetainedOwners - owners_)
            return {"RHI-CANONICAL-CONTENT-PLAN-OWNER-CAPACITY"};
        const auto& inventory = sources(*graph_).sources();
        for (const auto& state : graph_->textureEntries()) {
            if (!chargeProjection())
                return {"RHI-UNDEFINED-TEXTURE-UPLOAD-PLAN-PROJECTION-CAPACITY"};
            const auto* object = Projection::originalTexture(inventory[state.point.sourceIndex]);
            if (!object || !object->originalContent_)
                return {"RHI-UNDEFINED-TEXTURE-UPLOAD-PLAN-CONTROL"};
            const auto observation = object->originalContent_->value_.observe();
            if (!observation)
                return {"RHI-UNDEFINED-TEXTURE-UPLOAD-PLAN-INITIAL"};
            if (const auto* id =
                    Contract::validateInitial(object->originalContent_->contractVersion(),
                        observation->value.state == Content::State::Uninitialized,
                        observation->value.generation,
                        state.observation.state()))
                return {id};
            if (state.point.origin != Projection::Point::Origin::ExplicitOperand
                || state.point.range != defaultTextureSubresourceRange(object->desc()))
                return {"RHI-UNDEFINED-TEXTURE-UPLOAD-PLAN-RANGE"};
            bool uploaded = false;
            for (std::size_t command = 0u; command < commands().size(); ++command) {
                if (!chargeProjection())
                    return {"RHI-UNDEFINED-TEXTURE-UPLOAD-PLAN-PROJECTION-CAPACITY"};
                if (const auto upload = contentUploadAt(command);
                    upload && upload->object == object)
                    uploaded = true;
            }
            if (!uploaded)
                return {"RHI-UNDEFINED-TEXTURE-UPLOAD-PLAN-UNSUPPORTED-USE"};
        }
        return result;
    }

    const char* preflightContentPlan(const OriginalTextureContentPlanBudget& budget) const noexcept
    {
        if (budget.contractVersion == 7u)
            return inspectUndefinedTextureUploads(budget).diagnostic;
        if (OriginalCompletedTextureReadPlanContract::isReadOnly(budget.contractVersion))
            return inspectCompletedContentReadPlan(budget,
                [](const auto&, const auto&) noexcept -> const char* {
                    return nullptr;
                })
                .diagnostic;
        if (budget.contractVersion == 2u || budget.contractVersion == 3u
            || OriginalCompletedDepthRootReadPlanContract::isVersion(budget.contractVersion)
            || OriginalStorageTextureContentPlanContract::isVersion(budget.contractVersion))
            return preflightOriginalContentCells(budget).diagnostic;
        bool required = false;
        for (const auto& texture : textures_.values()) {
            if (texture.object->originalContent_
                && texture.object->originalContent_->contractVersion() != 1u)
                return "RHI-CANONICAL-CONTENT-PLAN-CELL-PROFILE-UNIMPLEMENTED";
            required = required || texture.object->originalContentExclusive();
        }
        if (!required) {
            if (budget.contractVersion || budget.prototypeOptIn || budget.maxContentControls
                || budget.maxUploadCommands || budget.maxContentCells || budget.maxContentSteps
                || budget.maxCommandVisits || budget.maxProjectionVisits
                || budget.maxReadbackCommands)
                return "RHI-CANONICAL-CONTENT-PLAN-UNEXPECTED";
            return nullptr;
        }
        if (budget.contractVersion != 1u)
            return "RHI-CANONICAL-CONTENT-PLAN-VERSION";
        if (!budget.prototypeOptIn)
            return "RHI-CANONICAL-CONTENT-PLAN-OPT-IN";
        if (!budget.maxContentControls || !budget.maxUploadCommands)
            return "RHI-CANONICAL-CONTENT-PLAN-BUDGET";
        if (budget.maxContentCells || budget.maxContentSteps || budget.maxCommandVisits
            || budget.maxProjectionVisits || budget.maxReadbackCommands)
            return "RHI-ORIGINAL-CONTENT-PLAN-LEGACY-SCHEMA";
        std::uint32_t uploads = 0u;
        for (std::size_t command = 0u; command < commands().size(); ++command)
            if (const auto entry = contentUploadAt(command)) {
                if (const auto* id = entry->control->diagnostic())
                    return id;
                const std::array domains{entry->control->quota_,
                    originalMetadataQuota(),
                    graph_->originalMetadataQuota(),
                    graph_->originalInputs()->originalMetadataQuota(),
                    sources(*graph_).originalMetadataQuota()};
                if (!core::SharedOwnerProjection::hasIndependentOwners(
                        domains.size(), [&](std::size_t i) noexcept {
                            return domains[i];
                        }))
                    return "RHI-CANONICAL-CONTENT-PLAN-QUOTA-DOMAIN";
                if (const auto* id = semantics::OriginalTextureContentUploadValidation::inspect(
                        *entry->source->originalInput(), entry->object->desc()))
                    return id;
                if (uploads == budget.maxUploadCommands)
                    return "RHI-CANONICAL-CONTENT-PLAN-UPLOAD-CAPACITY";
                ++uploads;
            }
        const auto identity = core::SharedIdentityProjection::inspect(commands().size(),
            commands().size(),
            budget.maxContentControls,
            [&](std::size_t command) noexcept -> core::SharedIdentityProjection::Record {
                const auto entry = contentUploadAt(command);
                if (!entry)
                    return {};
                return {entry->object->id().value,
                    static_cast<std::uint32_t>(CommandReferenceDomain::Texture),
                    entry->source->originalDestinationLease(),
                    entry->control};
            });
        if (identity.status != core::SharedIdentityProjection::Status::Complete)
            return "RHI-CANONICAL-CONTENT-PLAN-CONTROL-CAPACITY";
        if (!uploads || identity.identities > budget_.maxRetainedOwners - owners_)
            return "RHI-CANONICAL-CONTENT-PLAN-OWNER-CAPACITY";
        for (std::size_t index = 0u; index < textures_.values().size(); ++index) {
            const auto& texture = textures_.values()[index];
            if (!texture.object->originalContent_)
                continue;
            bool written = false;
            for (std::size_t command = 0u; command < commands().size(); ++command)
                if (const auto entry = contentUploadAt(command);
                    entry && entry->object == texture.object)
                    written = true;
            if (!written)
                return "RHI-CANONICAL-CONTENT-PLAN-UNSUPPORTED-USE";
            using K = CommandUsageKind;
            const auto kind = commands()[graph_->textureEntries()[index].point.commandIndex].kind;
            if (kind != K::TextureUpload && kind != K::TextureBarrier
                && kind != K::TextureMemoryBarrier && kind != K::RetainTexture)
                return "RHI-CANONICAL-CONTENT-PLAN-UNSUPPORTED-USE";
        }
        return nullptr;
    }

    const char* populateContentPlan(Plan& plan) const noexcept
    {
        if (OriginalCompletedTextureReadPlanContract::isReadOnly(
                plan.contentBudget.contractVersion))
            return inspectCompletedContentReadPlan(plan.contentBudget,
                [&](const GpuTexture& object,
                    const Content::Observation& initial) noexcept -> const char* {
                    return plan.contents.append(
                               {&object, object.originalContent_.get(), initial, 0u, {}, {}})
                        ? nullptr
                        : "RHI-CANONICAL-CONTENT-PLAN-CONTROL-CAPACITY";
                })
                .diagnostic;
        if (plan.contentBudget.contractVersion == 2u || plan.contentBudget.contractVersion == 3u
            || OriginalCompletedDepthRootReadPlanContract::isVersion(
                plan.contentBudget.contractVersion)
            || OriginalStorageTextureContentPlanContract::isVersion(
                plan.contentBudget.contractVersion)) {
            return inspectRangeContentPlan(plan.contentBudget,
                [&](const GpuTexture& object,
                    const auto& stamp,
                    auto index,
                    const auto& initial,
                    const auto& timeline) noexcept -> const char* {
                    return plan.appendRangeCell(object, stamp, index, initial, timeline);
                })
                .diagnostic;
        }
        for (std::size_t command = 0u; command < commands().size(); ++command)
            if (const auto entry = contentUploadAt(command)) {
                auto* cell = plan.contents.findIf([&](const auto& c) {
                    return c.object == entry->object;
                });
                if (!cell) {
                    const auto initial = entry->control->value_.observe();
                    if (!initial)
                        return "RHI-CANONICAL-CONTENT-PLAN-INITIAL";
                    if (!plan.contents.append(
                            {entry->object, entry->control, *initial, 0u, {}, {}}))
                        return "RHI-CANONICAL-CONTENT-PLAN-CONTROL-CAPACITY";
                    cell = plan.contents.findIf([&](const auto& c) {
                        return c.object == entry->object;
                    });
                }
                // BoundedSequence exposes mutation through visitMutable only.
                const auto index = static_cast<std::size_t>(cell - plan.contents.values().data());
                plan.contents.visitMutable([&](auto& c) {
                    if (c.object == entry->object)
                        ++c.writes;
                });
                if (!plan.uploads.append({command, index, 0u}))
                    return "RHI-CANONICAL-CONTENT-PLAN-UPLOAD-CAPACITY";
            }
        return nullptr;
    }

    bool completedContentIsReadOnly() const noexcept
    {
        return plan_
            && (OriginalFiniteGraphicsContentPlanContract::isMixed(
                    plan_->mixedBudget.contractVersion)
                || OriginalCompletedTextureReadPlanContract::isReadOnly(
                    plan_->contentBudget.contractVersion));
    }

    const char* validateContentPlan(
        const GpuTexture* delegatedContent = nullptr, bool identityOnly = false) const noexcept
    {
        if (OriginalFiniteGraphicsContentPlanContract::isMixed(
                plan_->mixedBudget.contractVersion)) {
            if (!plan_->validateGraphicsStream)
                return "RHI-FINITE-GRAPHICS-CONTENT-STREAM-OWNER";
            if (const auto* id = plan_->validateGraphicsStream(
                    plan_->graphicsStream, graph_, status_ == Status::Prepared))
                return id;
            const auto* id = OriginalBindlessGraphicsDepthContentPlanContract::isVersion(
                                 plan_->contentBudget.contractVersion)
                ? OriginalBindlessGraphicsDepthContentPlanContract::validateCounts(
                      plan_->depthBudget, plan_->depthCounts)
                : plan_->mixedBudget.contractVersion == 9u
                ? OriginalBindlessGraphicsContentPlanContract::validateCounts(
                      plan_->mixedBudget, plan_->mixedCounts)
                : OriginalFiniteGraphicsContentPlanContract::validateCounts(
                      plan_->mixedBudget, plan_->mixedCounts);
            if (id)
                return id;
        }
        for (const auto& entry : plan_->rangeContents.values()) {
            if (entry.object->originalContent_.get() != entry.control || !entry.control->cells_)
                return "RHI-CANONICAL-CONTENT-PLAN-CONTROL";
            if (const auto* id = entry.control->loss_.diagnostic())
                return id;
            if (!entry.control->quota_
                || entry.control->quota_->limitBytes() != entry.control->budget_.maxCpuContentBytes)
                return "RHI-CANONICAL-CONTENT-CONTROL-QUOTA";
            const auto& array = *entry.control->cells_;
            if (!array.active())
                return "RHI-CANONICAL-CONTENT-CONTROL-INVALIDATED";
            if (!entry.mutations) {
                // Retain the authentic completed stamp through a reader's
                // state claim. Enlisting content would make every immutable
                // descriptor proof stale and consume a revision on rollback.
                if (entry.pending || entry.committed || !array.matches(entry.initial))
                    return "RHI-ORIGINAL-CONTENT-PLAN-READONLY-STALE";
            } else {
                if (status_ == Status::Prepared && !array.matches(entry.initial))
                    return "RHI-CANONICAL-CONTENT-PLAN-STALE";
                if (status_ == Status::Pending
                    && (!entry.pending || !array.matchesPending(*entry.pending, journal_)))
                    return "RHI-CANONICAL-CONTENT-PLAN-PENDING";
                if (status_ == Status::Committed
                    && (!entry.committed
                        || (entry.object != delegatedContent && !array.matches(*entry.committed))))
                    return "RHI-ORIGINAL-CONTENT-PLAN-COMMITTED";
            }
        }
        for (const auto& cell : plan_->rangeCells.values()) {
            if (cell.controlIndex >= plan_->rangeContents.values().size())
                return "RHI-ORIGINAL-CONTENT-PLAN-CELL-COVERAGE";
            const auto& entry = plan_->rangeContents.values()[cell.controlIndex];
            const auto* value =
                entry.control->cells_->valueAt(static_cast<std::size_t>(cell.cellIndex));
            const auto expected =
                status_ == Status::Prepared ? cell.initial : cell.completed.value_or(cell.proposed);
            if (!value
                || (!(identityOnly && entry.object == delegatedContent) && !(*value == expected)))
                return "RHI-ORIGINAL-CONTENT-PLAN-CELL-VALUE";
            if (status_ == Status::Pending && cell.written
                && (!cell.proposed.generation || cell.proposed.generation <= cell.initial.generation
                    || cell.proposed.state
                        != (cell.stored ? Content::State::Proposed
                                        : Content::State::Uninitialized)))
                return "RHI-ORIGINAL-CONTENT-PLAN-CELL-PENDING";
            if (cell.completed
                && (!cell.written || cell.completed->generation != cell.proposed.generation
                    || cell.completed->state
                        != (cell.stored ? Content::State::Completed
                                        : Content::State::Uninitialized)))
                return "RHI-ORIGINAL-CONTENT-PLAN-COMPLETION-MISMATCH";
            if (status_ == Status::Committed && cell.written && !cell.completed)
                return "RHI-ORIGINAL-CONTENT-PLAN-COMMITTED";
        }
        for (const auto& cell : plan_->contents.values()) {
            if (cell.object->originalContent_.get() != cell.control)
                return "RHI-CANONICAL-CONTENT-PLAN-CONTROL";
            if (const auto* id = cell.control->loss_.diagnostic())
                return id;
            if (!cell.control->quota_
                || cell.control->quota_->limitBytes() != cell.control->budget_.maxCpuContentBytes)
                return "RHI-CANONICAL-CONTENT-CONTROL-QUOTA";
            if (!cell.control->value_.active())
                return "RHI-CANONICAL-CONTENT-CONTROL-INVALIDATED";
            if (completedContentIsReadOnly()) {
                if (const auto* id = cell.control->diagnostic())
                    return id;
                if (cell.writes || cell.pending || cell.committed
                    || !cell.control->value_.matches(cell.initial))
                    return "RHI-COMPLETED-TEXTURE-READ-PLAN-STALE";
                if (const auto* id = OriginalCompletedTextureReadPlanContract::validateObservation(
                        cell.control->contractVersion(),
                        cell.initial.value.state == Content::State::Completed,
                        cell.initial.value.generation,
                        cell.initial.revision,
                        (plan_->contentBudget.contractVersion == 9u
                            || OriginalBindlessGraphicsDepthContentPlanContract::isVersion(
                                plan_->contentBudget.contractVersion))
                            ? OriginalCompletedTextureReadPlanContract::graphicsReadVersion(
                                  plan_->mixedBudget.bindlessCompletedReadContractVersion)
                            : plan_->contentBudget.contractVersion))
                    return id;
                continue;
            }
            if (status_ == Status::Prepared) {
                if (!cell.control->value_.matches(cell.initial))
                    return "RHI-CANONICAL-CONTENT-PLAN-STALE";
            } else if (status_ == Status::Pending) {
                if (!cell.pending || !cell.control->value_.matchesPending(*cell.pending, journal_)
                    || !cell.pending->value().generation)
                    return "RHI-CANONICAL-CONTENT-PLAN-PENDING";
                // Only the private native completion path installs committed.
                // It must match the exact pending value/revision before the
                // shared journal can publish both state and content together.
                if (cell.committed) {
                    if (cell.pending->value().state != Content::State::Completed
                        || !(cell.committed->value == cell.pending->value())
                        || cell.committed->revision != cell.pending->revision())
                        return "RHI-CANONICAL-CONTENT-PLAN-COMPLETION-MISMATCH";
                } else if (cell.pending->value().state != Content::State::Proposed)
                    return "RHI-CANONICAL-CONTENT-PLAN-PENDING";
            } else if (status_ == Status::Committed) {
                if (!cell.committed || !cell.control->value_.matches(*cell.committed)
                    || cell.committed->value.state != Content::State::Completed)
                    return "RHI-CANONICAL-CONTENT-PLAN-COMMITTED";
            }
        }
        return nullptr;
    }

    // Shared exact-root mechanics. Public CPU entry points enforce the seal
    // before entering; only the existing seal friend can later connect private
    // proven native issuers. Cleanup must not re-enter the public CPU gate.
    const char* applyOriginalPlan() noexcept
    {
        if (status_ != Status::Prepared || !plan_)
            return "RHI-ORIGINAL-PLAN-STATUS";
        if (const auto* id = validate())
            return id;
        if (const auto* id =
                common::OriginalCommandAccessValidation::inspect(graph_, plan_->budget).diagnostic)
            return id;
        if (const auto* id = preflightPlanWrites())
            return id;
        if (!completedContentIsReadOnly()) {
            for (const auto& content : plan_->contents.values())
                if (const auto* id = content.control->canUpload(
                        journal_, content.writes, plan_->contentBudget.contractVersion))
                    return id;
        }
        for (const auto& content : plan_->rangeContents.values())
            if (const auto* id = content.control->canProposeCells(
                    journal_, content.mutations, plan_->contentBudget.contractVersion))
                return id;
        for (const auto& entry : buffers_.values())
            if (!entry.borrowed && !entry.control->prepare(journal_))
                return failedEnlistment();
        for (const auto& entry : textures_.values())
            if (entry.normalized ? !entry.normalized->prepare(journal_)
                                 : !entry.uniform->prepare(journal_))
                return failedEnlistment();
        for (const auto& content : plan_->rangeContents.values())
            if (content.mutations && !content.control->cells_->prepare(journal_))
                return failedEnlistment();
        for (const auto& transition : plan_->transitions.values()) {
            const auto& b = transition.barrier;
            const auto* id = transition.normalized
                ? transition.normalized->write(b.textureSubresources, b.before, b.after, &journal_)
                : transition.buffer ? transition.scalar->writeBuffer(b.after, &journal_)
                                    : transition.scalar->write(b.after, &journal_);
            if (id) {
                rollbackOriginalOwnership();
                return id;
            }
        }
        const char* contentFailure = nullptr;
        plan_->uploads.visitMutable([&](auto& upload) noexcept {
            if (!contentFailure)
                contentFailure = plan_->contents.values()[upload.cell].control->propose(
                    journal_, upload.generation, plan_->contentBudget.contractVersion);
        });
        plan_->rangeCells.visitMutable([&](auto& cell) noexcept {
            if (!contentFailure && cell.written)
                contentFailure =
                    plan_->rangeContents.values()[cell.controlIndex].control->proposeCell(journal_,
                        cell.cellIndex,
                        cell.stored,
                        cell.proposed,
                        plan_->contentBudget.contractVersion);
        });
        if (contentFailure) {
            rollbackOriginalOwnership();
            return contentFailure;
        }
        plan_->contents.visitMutable([&](auto& cell) noexcept {
            if (!completedContentIsReadOnly())
                cell.pending = cell.control->value_.observePending(journal_);
        });
        plan_->rangeContents.visitMutable([&](auto& content) noexcept {
            if (content.mutations)
                content.pending = content.control->cells_->observePending(journal_);
        });
        plan_->cells.visitMutable([&](auto& cell) noexcept {
            if (cell.borrowed)
                return;
            if (cell.normalized)
                cell.texturePending = cell.normalized->observePending(cell.range, journal_);
            else
                cell.scalarPending = cell.scalar->observePending(journal_);
        });
        status_ = Status::Pending;
        if (const auto* id = validate()) {
            rollbackOriginalOwnership();
            return id;
        }
        return nullptr;
    }

    const char* commitOriginalOwnership() noexcept
    {
        if (budget_.contractVersion == 3u)
            return "RHI-ORIGINAL-TEXTURE-TRANSACTION-NATIVE-COMPLETION-REQUIRED";
        if (plannedContentControlCount())
            return "RHI-CANONICAL-CONTENT-NATIVE-COMPLETION-REQUIRED";
        if (const auto* id = validate())
            return id;
        return finalizeOriginalOwnership();
    }

    const char* commitOriginalNativeOwnership(std::uint32_t generalSubmissionVersion = 0u) noexcept
    {
        return completeOriginalNativeOwnership(false, generalSubmissionVersion);
    }

    // Only the original Seal can enter after authentic same-root map receipts,
    // queue/scopes, copying and native unmap have all been proved by its issuer.
    const char* commitOriginalReadbackNativeOwnership() noexcept
    {
        if (!plan_ || plan_->contentBudget.contractVersion != 3u)
            return "RHI-ORIGINAL-CONTENT-READBACK-COMPLETION-PROFILE";
        return completeOriginalNativeOwnership(true);
    }

    const char* completeOriginalNativeOwnership(
        bool readbackProven, std::uint32_t generalSubmissionVersion = 0u) noexcept
    {
        if (plan_ && plan_->contentBudget.contractVersion == 19u
            && (generalSubmissionVersion != 14u || readbackProven))
            return "RHI-BINDLESS-GRAPHICS-DEPTH-CANONICAL-COMPLETION-UNAVAILABLE";
        if (plan_ && plan_->contentBudget.contractVersion == 18u
            && (generalSubmissionVersion != 13u || readbackProven))
            return "RHI-BINDLESS-GRAPHICS-DEPTH-CONTENT-NATIVE-COMPLETION-UNAVAILABLE";
        if (plan_
            && OriginalStorageTextureContentPlanContract::isMixedVersion(
                plan_->contentBudget.contractVersion)
            && (generalSubmissionVersion
                    != (plan_->contentBudget.contractVersion == 15u ? 11u : 12u)
                || readbackProven))
            return "RHI-MIXED-DEPTH-STORAGE-NATIVE-COMPLETION-UNAVAILABLE";
        if (plan_
            && (plan_->contentBudget.contractVersion == 13u
                || plan_->contentBudget.contractVersion == 14u)
            && (generalSubmissionVersion != (plan_->contentBudget.contractVersion == 13u ? 9u : 10u)
                || readbackProven))
            return "RHI-STORAGE-CONTENT-PLAN-NATIVE-COMPLETION-UNAVAILABLE";
        if (plan_ && plan_->mixedBudget.contractVersion == 9u
            && (generalSubmissionVersion
                    != (hasOriginalBindlessProducedGeometryProfile()      ? 15u
                            : plan_->contentBudget.contractVersion == 19u ? 14u
                            : plan_->contentBudget.contractVersion == 18u ? 13u
                                                                          : 6u)
                || readbackProven))
            return "RHI-BINDLESS-GRAPHICS-CONTENT-NATIVE-COMPLETION-UNAVAILABLE";
        if (plan_ && plan_->mixedBudget.contractVersion == 6u
            && (generalSubmissionVersion != 3u || readbackProven))
            return "RHI-FINITE-GRAPHICS-CONTENT-NATIVE-COMPLETION-UNAVAILABLE";
        if (status_ != Status::Pending)
            return "RHI-ORIGINAL-TRANSACTION-STATUS";
        if (const auto* id = OriginalGeneralSubmissionCompletionContract::validateProfile(
                generalSubmissionVersion,
                hasOriginalGeneralShaderResourceProfile(),
                hasOriginalFiniteGeneralShaderResourceProfile(),
                originalContentPlanContractVersion(),
                (hasOriginalBindlessRootShaderResourceProfile()
                    || hasOriginalBindlessGraphicsGeometryProfile()
                    || hasOriginalBindlessProducedGeometryProfile())
                    && (originalContentPlanContractVersion() == 8u
                        || originalContentPlanContractVersion() == 9u
                        || originalContentPlanContractVersion() == 18u
                        || originalContentPlanContractVersion() == 19u
                        || OriginalCompletedDepthRootReadPlanContract::isVersion(
                            originalContentPlanContractVersion())
                        || OriginalStorageTextureContentPlanContract::isVersion(
                            originalContentPlanContractVersion())),
                hasOriginalBindlessProducedGeometryProfile()))
            return id;
        if (plan_ && plan_->contentBudget.contractVersion == 3u && !readbackProven)
            return "RHI-ORIGINAL-CONTENT-READBACK-NATIVE-COMPLETION-UNAVAILABLE";
        if (const auto* id = validate())
            return id;
        if (plan_
            && (plan_->mixedBudget.contractVersion == 6u || plan_->mixedBudget.contractVersion == 9u
                || plan_->contentBudget.contractVersion == 2u
                || (plan_->contentBudget.contractVersion == 13u && generalSubmissionVersion == 9u)
                || (plan_->contentBudget.contractVersion == 14u && generalSubmissionVersion == 10u)
                || (plan_->contentBudget.contractVersion == 15u && generalSubmissionVersion == 11u)
                || (plan_->contentBudget.contractVersion == 16u && generalSubmissionVersion == 12u)
                || (plan_->contentBudget.contractVersion == 3u && readbackProven))) {
            // The exact Pending journal and every control/cell were validated
            // above. Proposal preflight reserved these fixed completion writes.
            const char* failure = nullptr;
            plan_->rangeCells.visitMutable([&](auto& cell) noexcept {
                if (failure || !cell.written)
                    return;
                Content::Value completed;
                failure =
                    plan_->rangeContents.values()[cell.controlIndex].control->completeCell(journal_,
                        cell.cellIndex,
                        cell.proposed,
                        completed,
                        plan_->contentBudget.contractVersion);
                if (!failure)
                    cell.completed = completed;
            });
            if (failure) {
                invalidateOriginalOwnership();
                return failure;
            }
            plan_->rangeContents.visitMutable([&](auto& entry) noexcept {
                if (!entry.mutations)
                    return;
                entry.pending = entry.control->cells_->observePending(journal_);
                if (entry.pending)
                    entry.committed = Content::Cells::Observation{
                        entry.pending->value(), entry.pending->revision()};
            });
        }
        if (plan_ && !completedContentIsReadOnly())
            for (const auto& cell : plan_->contents.values())
                if (const auto* id =
                        cell.control->complete(journal_, plan_->contentBudget.contractVersion)) {
                    invalidateOriginalOwnership();
                    return id;
                }
        if (plan_ && !completedContentIsReadOnly())
            plan_->contents.visitMutable([&](auto& cell) noexcept {
                cell.pending = cell.control->value_.observePending(journal_);
                cell.committed =
                    Content::Observation{cell.pending->value(), cell.pending->revision()};
            });
        return finalizeOriginalOwnership();
    }

    const char* finalizeOriginalOwnership() noexcept
    {
        if (status_ != Status::Pending)
            return "RHI-ORIGINAL-TRANSACTION-STATUS";
        if (const auto* id = validate())
            return id;
        if (plan_)
            plan_->cells.visitMutable([&](auto& cell) noexcept {
                if (cell.borrowed)
                    return;
                if (cell.normalized)
                    cell.textureCommitted = Texture::Observation{
                        cell.range, cell.texturePending->state(), cell.texturePending->revision()};
                else
                    cell.scalarCommitted = Scalar::Observation{
                        cell.scalarPending->value(), cell.scalarPending->revision()};
            });
        journal_.commit();
        status_ = Status::Committed;
        return validate();
    }

    const char* cpuMutationDiagnostic() const noexcept
    {
        return recordingSealed_ ? "RHI-ORIGINAL-TRANSACTION-RECORDING-SEALED" : nullptr;
    }

    void rollbackOriginalOwnership() noexcept
    {
        if (status_ == Status::Committed || status_ == Status::Abandoned
            || status_ == Status::Invalidated)
            return;
        journal_.rollback();
        status_ = Status::Abandoned;
    }

    // Loss/quarantine invalidates the original controls before releasing any
    // ownership. Discarding snapshots preserves uncertain submitted values;
    // neither destruction nor a late event can restore or commit these controls.
    void invalidateOriginalOwnership() noexcept
    {
        if (status_ == Status::Invalidated)
            return;
        for (const auto& entry : buffers_.values())
            if (!entry.borrowed)
                entry.control->invalidate();
        // Read-only completed inputs were never enlisted for content mutation.
        // Uncertain work invalidates writable content and conservative resource
        // states; original lifetime/loss still invalidates every resource stamp.
        if (plan_ && !completedContentIsReadOnly())
            for (const auto& cell : plan_->contents.values())
                cell.control->value_.invalidate();
        if (plan_)
            for (const auto& content : plan_->rangeContents.values())
                if (content.mutations)
                    content.control->cells_->invalidate();
        for (const auto& entry : textures_.values()) {
            if (entry.normalized)
                entry.normalized->invalidate();
            else
                entry.uniform->invalidate();
        }
        journal_.discard();
        status_ = Status::Invalidated;
    }

    const core::pmr::Vector<CommandUsage>& commands() const noexcept
    {
        return graph_->originalInputs()->originalScalars()->originalUsage()->commands();
    }

    const core::pmr::Vector<OriginalCommandUsage::Reference>& references() const noexcept
    {
        return graph_->originalInputs()->originalScalars()->originalUsage()->references();
    }

    std::optional<ResourceBarrierDesc> transitionAt(std::size_t command) const noexcept
    {
        const auto kind = commands()[command].kind;
        if (kind != CommandUsageKind::BufferBarrier && kind != CommandUsageKind::TextureBarrier)
            return {};
        if (const auto* descriptor = graph_->originalInputs()->descriptorAt(command)) {
            const auto* dependency = std::get_if<ResourceDependencyArguments>(descriptor);
            ResourceBarrierDesc barrier;
            if (dependency && dependency->write(kind, barrier))
                return barrier;
        }
        return {};
    }

    std::uint64_t writesTo(GpuResourceId resource) const noexcept
    {
        std::uint64_t count = 0u;
        for (std::size_t i = 0u; i < commands().size(); ++i)
            if (const auto b = transitionAt(i); b && b->resource == resource)
                ++count;
        return count;
    }

    const char* preflightPlanWrites() const noexcept
    {
        for (const auto& entry : buffers_.values()) {
            if (entry.borrowed) {
                if (writesTo(entry.object->id()))
                    return "RHI-SUBMISSION-READ-EXPLICIT-ACCESS";
                if (const auto* id = entry.borrowed->diagnostic())
                    return id;
            } else if (!entry.control->canWriteBatch(journal_, writesTo(entry.object->id())))
                return "RHI-ORIGINAL-PLAN-REVISION-ADMISSION";
        }
        for (const auto& entry : textures_.values()) {
            if (entry.uniform) {
                if (entry.object->desc().format == TextureFormat::Depth24Stencil8)
                    return "RHI-ORIGINAL-PLAN-UNIFORM-ASPECT-UNPROVEN";
                if (entry.range != defaultTextureSubresourceRange(entry.object->desc()))
                    return "RHI-ORIGINAL-PLAN-UNIFORM-RANGE";
                for (std::size_t i = 0u; i < commands().size(); ++i)
                    if (const auto b = transitionAt(i); b && b->resource == entry.object->id()
                        && b->textureSubresources
                            != defaultTextureSubresourceRange(entry.object->desc()))
                        return "RHI-ORIGINAL-PLAN-UNIFORM-RANGE";
            }
            if (entry.normalized
                    ? !entry.normalized->canWriteBatch(journal_, writesTo(entry.object->id()))
                    : !entry.uniform->canWriteBatch(journal_, writesTo(entry.object->id())))
                return "RHI-ORIGINAL-PLAN-REVISION-ADMISSION";
        }
        return nullptr;
    }

    const char* populatePlan(Plan& plan) const noexcept
    {
        using Timeline = common::OriginalCommandStateSequence;
        // One serialized, private traversal; all visitors below are internal
        // bounded plan writes. Do not replay complete graph admission per cell
        // or carry this validation across a callback, mutation or operation.
        if (const auto* id = OriginalCommandResourceStates::validateOriginal(graph_))
            return id;
        const auto& inventory = sources(*graph_).sources();
        const auto addCell = [&](FinalCell cell) -> const char* {
            if (plan.cells.findIf([&](const auto& c) {
                    return c.buffer == cell.buffer && c.texture == cell.texture
                        && c.range == cell.range;
                }))
                return nullptr;
            return plan.cells.append(std::move(cell)) ? nullptr : "RHI-ORIGINAL-PLAN-CELL-CAPACITY";
        };
        for (std::size_t i = 0u; i < buffers_.values().size(); ++i) {
            const auto& e = buffers_.values()[i];
            FinalCell cell;
            cell.buffer = e.object;
            cell.scalar = e.control;
            cell.borrowed = e.borrowed;
            cell.initial = graph_->bufferEntries()[i].observation.state();
            if (const auto* id =
                    Timeline::bufferBefore(*graph_, e.object->id(), commands().size(), cell.final))
                return id;
            if (const auto* id = addCell(std::move(cell)))
                return id;
        }
        for (std::size_t i = 0u; i < textures_.values().size(); ++i) {
            const auto& e = textures_.values()[i];
            const char* diagnostic = nullptr;
            const auto geometry = semantics::OriginalTextureStateRange::capture(
                e.object->desc(), UINT64_MAX, diagnostic);
            if (!geometry)
                return diagnostic;
            for (std::uint64_t ordinal = 0u; ordinal < geometry->rangeCellCount(e.range);
                ++ordinal) {
                FinalCell cell;
                cell.texture = e.object;
                cell.scalar = e.uniform;
                cell.normalized = e.normalized;
                cell.initial = graph_->textureEntries()[i].observation.state();
                std::uint64_t index = 0u;
                geometry->cellIndex(e.range, ordinal, index);
                geometry->cellRange(index, cell.range);
                if (const auto* id = Timeline::resolveValidatedTextureCell(
                        *graph_, i, ordinal, commands().size(), cell.final))
                    return id;
                if (const auto* id = addCell(std::move(cell)))
                    return id;
            }
        }
        const auto addStep = [&](std::size_t command,
                                 const FinalCell* cell,
                                 ResourceState before,
                                 ResourceState after) -> const char* {
            if (!cell)
                return "RHI-ORIGINAL-PLAN-COVERAGE";
            const auto index = static_cast<std::size_t>(cell - plan.cells.values().data());
            return plan.steps.append({command, index, before, after})
                ? nullptr
                : "RHI-ORIGINAL-PLAN-STEP-CAPACITY";
        };
        for (std::size_t command = 0u; command < commands().size(); ++command) {
            const auto barrier = transitionAt(command);
            const auto& c = commands()[command];
            const auto addImplicitBuffer = [&](const auto& access) noexcept -> const char* {
                const auto sourceIndex = [&]() noexcept {
                    if constexpr (std::is_same_v<std::decay_t<decltype(access)>,
                                      OriginalCommandGeometryBindingContract::Access>)
                        return access.binding.sourceIndex;
                    else if constexpr (std::is_same_v<std::decay_t<decltype(access)>,
                                           common::OriginalCommandBindlessRootAccesses::Access>)
                        return access.declaration.kind == BindlessAccessKind::Buffer
                            ? access.sourceIndex
                            : SIZE_MAX;
                    else
                        return access.bufferSourceIndex;
                }();
                // Texture cells are already enumerated from the exact
                // State3 footprint below; never index their missing buffer.
                if (access.commandIndex != command || sourceIndex == SIZE_MAX)
                    return nullptr;
                const auto* object = std::get<std::shared_ptr<const OriginalCommandBufferLease>>(
                    inventory[sourceIndex])
                                         ->originalObject();
                ResourceState before;
                if (const auto* id = Timeline::bufferBefore(*graph_, object->id(), command, before))
                    return id;
                const auto* cell = plan.cells.findIf([&](const auto& value) {
                    return value.buffer == object;
                });
                return addStep(command, cell, before, before);
            };
            if (!budget_.bindlessRoots.empty()
                && ((budget_.bindlessRoots.contractVersion == 1u
                        && c.kind == CommandUsageKind::RetainIndexedSnapshot)
                    || (budget_.bindlessRoots.contractVersion >= 2u
                        && (c.kind == CommandUsageKind::Dispatch || c.kind == CommandUsageKind::Draw
                            || (budget_.bindlessRoots.geometryJoined()
                                && c.kind == CommandUsageKind::DrawIndexed)
                            || (budget_.bindlessRoots.contractVersion == 6u
                                && c.kind == CommandUsageKind::DrawIndexedIndirect)
                            || c.kind == CommandUsageKind::ClearSnapshotFault)))) {
                const auto roots =
                    common::OriginalCommandBindlessRootAccesses::visitValidatedOriginal(
                        graph_->originalInputs(), budget_.bindlessRoots, addImplicitBuffer);
                if (roots.diagnostic)
                    return roots.diagnostic;
            }
            if (budget_.contractVersion >= 2u
                && OriginalCommandDescriptorConsumptionContract::isConsumption(
                    c.kind, budget_.descriptorConsumption)) {
                const auto result = graph_->visitDescriptorAccesses(addImplicitBuffer);
                if (result.diagnostic)
                    return result.diagnostic;
            }
            if ((budget_.contractVersion == 3u || budget_.contractVersion == 4u
                    || budget_.contractVersion == 5u)
                && (c.kind == CommandUsageKind::BindRenderDescriptor
                    || c.kind == CommandUsageKind::BindRenderDescriptorTable)) {
                const auto result = graph_->visitRenderDescriptorAccesses(
                    budget_.renderDescriptorUsage, addImplicitBuffer);
                if (result.diagnostic)
                    return result.diagnostic;
            }
            if ((budget_.contractVersion == 4u || budget_.bindlessRoots.geometryJoined())
                && !budget_.geometryUsage.drawConsumptionEmpty()
                && (c.kind == CommandUsageKind::Draw || c.kind == CommandUsageKind::DrawIndexed
                    || (budget_.bindlessRoots.contractVersion == 6u
                        && c.kind == CommandUsageKind::DrawIndexedIndirect))) {
                const auto geometry = graph_->visitGeometryAccesses(
                    budget_.geometryUsage, [&](const auto& access) noexcept -> const char* {
                        if (!access.drawConsumption() || access.commandIndex != command)
                            return nullptr;
                        return addImplicitBuffer(access);
                    });
                if (geometry.diagnostic)
                    return geometry.diagnostic;
            }
            for (std::size_t operand = 0u; operand < c.referenceCount; ++operand) {
                const auto& source =
                    inventory[references()[c.firstReference + operand].sourceIndex];
                if (const auto* lease =
                        std::get_if<std::shared_ptr<const OriginalCommandBufferLease>>(&source)) {
                    const auto* object = (*lease)->originalObject();
                    ResourceState before;
                    if (const auto* id =
                            Timeline::bufferBefore(*graph_, object->id(), command, before))
                        return id;
                    const auto* cell = plan.cells.findIf([&](const auto& value) {
                        return value.buffer == object;
                    });
                    if (const auto* id =
                            addStep(command, cell, before, barrier ? barrier->after : before))
                        return id;
                }
            }
            if (c.kind == CommandUsageKind::TextureReadback) {
                const auto& source =
                    std::get<std::shared_ptr<const OriginalCommandTextureReadbackSource>>(
                        inventory[references()[c.firstReference + 1u].sourceIndex]);
                const auto* object = source->originalDestinationLease()->originalObject();
                ResourceState before;
                if (const auto* id = Timeline::bufferBefore(*graph_, object->id(), command, before))
                    return id;
                const auto* cell = plan.cells.findIf([&](const auto& value) {
                    return value.buffer == object;
                });
                if (const auto* id = addStep(command, cell, before, before))
                    return id;
            }
            for (std::size_t i = 0u; i < textures_.values().size(); ++i)
                if (graph_->textureEntries()[i].point.commandIndex == command) {
                    const auto& e = textures_.values()[i];
                    const char* diagnostic = nullptr;
                    const auto geometry = semantics::OriginalTextureStateRange::capture(
                        e.object->desc(), UINT64_MAX, diagnostic);
                    if (!geometry)
                        return diagnostic;
                    for (std::uint64_t ordinal = 0u; ordinal < geometry->rangeCellCount(e.range);
                        ++ordinal) {
                        ResourceState before;
                        std::uint64_t index = 0u;
                        TextureSubresourceRange range;
                        geometry->cellIndex(e.range, ordinal, index);
                        geometry->cellRange(index, range);
                        if (const auto* id = Timeline::resolveValidatedTextureCell(
                                *graph_, i, ordinal, command, before))
                            return id;
                        const auto* cell = plan.cells.findIf([&](const auto& value) {
                            return value.texture == e.object && value.range == range;
                        });
                        if (const auto* id =
                                addStep(command, cell, before, barrier ? barrier->after : before))
                            return id;
                    }
                }
            if (barrier) {
                const auto* cell = plan.cells.findIf([&](const auto& value) {
                    return value.buffer ? value.buffer->id() == barrier->resource
                                        : value.texture->id() == barrier->resource;
                });
                if (!cell
                    || !plan.transitions.append({command,
                        cell->scalar,
                        cell->normalized,
                        cell->buffer != nullptr,
                        *barrier}))
                    return "RHI-ORIGINAL-PLAN-TRANSITION-CAPACITY";
            }
        }
        return nullptr;
    }

    // Shared by strict public validation and the private, exact-cell handoff.
    // Metadata/count/control identity always remains mandatory. Only immutable
    // association replay may omit the selected output's current content value;
    // a Pending reader still proves that exact completed value through its seal.
    const char* validateRetainedPlan(
        const GpuTexture* delegatedContent = nullptr, bool identityOnly = false) const noexcept
    {
        if (status_ == Status::Invalidated)
            return "RHI-ORIGINAL-TRANSACTION-INVALIDATED";
        std::uint32_t owners = 0u, controls = 0u;
        if (const auto* id = preflight(graph_,
                budget_,
                originalMetadataQuota(),
                owners,
                controls,
                hasOriginalFiniteGeneralShaderResourceProfile()))
            return id;
        if (owners != owners_ || controls != controls_ || !countLease_.charged()
            || buffers_.values().size() != graph_->bufferEntries().size()
            || textures_.values().size() != graph_->textureEntries().size())
            return "RHI-ORIGINAL-TRANSACTION-COVERAGE";
        if (status_ == Status::Abandoned)
            return "RHI-ORIGINAL-TRANSACTION-ABANDONED";
        return plan_ ? validateContentPlan(delegatedContent, identityOnly) : nullptr;
    }

    const char* validatePlannedFinalCell(const FinalCell& cell) const noexcept
    {
        if (cell.borrowed) {
            if (!cell.buffer || cell.buffer->originalStateControl_ != cell.scalar || cell.normalized
                || cell.scalarPending || cell.scalarCommitted
                || cell.initial != ResourceState::ShaderRead || cell.final != cell.initial)
                return "RHI-SUBMISSION-READ-PLAN-CLAIM";
            return cell.borrowed->diagnostic();
        }
        if (cell.normalized) {
            if (cell.texture->originalNormalizedState_ != cell.normalized
                || cell.texture->originalUniformState_)
                return "RHI-ORIGINAL-PLAN-CONTROL";
            if (status_ == Status::Pending) {
                if (!cell.texturePending || cell.texturePending->range() != cell.range
                    || cell.texturePending->state() != cell.final
                    || !cell.normalized->matchesPending(*cell.texturePending, journal_))
                    return "RHI-ORIGINAL-PLAN-FINAL-MISMATCH";
            } else if (!cell.textureCommitted || cell.textureCommitted->range != cell.range
                || cell.textureCommitted->state != cell.final
                || cell.normalized->validate(*cell.textureCommitted))
                return "RHI-ORIGINAL-PLAN-FINAL-MISMATCH";
        } else {
            if (cell.buffer ? cell.buffer->originalStateControl_ != cell.scalar
                            : cell.texture->originalUniformState_ != cell.scalar)
                return "RHI-ORIGINAL-PLAN-CONTROL";
            if (status_ == Status::Pending) {
                if (!cell.scalarPending || cell.scalarPending->value() != cell.final
                    || !cell.scalar->matchesPending(*cell.scalarPending, journal_))
                    return "RHI-ORIGINAL-PLAN-FINAL-MISMATCH";
            } else if (!cell.scalarCommitted || cell.scalarCommitted->value != cell.final
                || !cell.scalar->matches(*cell.scalarCommitted))
                return "RHI-ORIGINAL-PLAN-FINAL-MISMATCH";
        }
        return nullptr;
    }

    // Only the existing RecordingSeal may authenticate this Pending reader.
    // The upstream committed root stays strict publicly; exactly one frozen
    // output cell delegates its live state check to this journal's CopySrc cell.
    const char* validateGeneralOutputReadPending(const OriginalCommandStateTransaction& upstream,
        const GpuTexture& object,
        const GpuTexture::CommittedStateObservation& frozen,
        std::uint32_t outputVersion) const noexcept
    {
        if (&upstream == this || !recordingSealed_ || status_ != Status::Pending || !plan_
            || journal_.empty() || graph_->stateContractVersion() != 1u
            || graph_->originalInputs()->originalBudget().contractVersion != 1u
            || !OriginalGeneralOutputReadbackDependencyContract::contentReaderProfileMatches(
                outputVersion, originalContentPlanContractVersion()))
            return "RHI-GENERAL-OUTPUT-PENDING-ROOT";
        if (const auto* id = validate())
            return id;
        if (!upstream.recordingSealed_ || upstream.status_ != Status::Committed || !upstream.plan_
            || !upstream.journal_.empty()
            || !((outputVersion == 1u && upstream.hasOriginalGeneralShaderResourceProfile()
                     && !upstream.hasOriginalFiniteGeneralShaderResourceProfile()
                     && upstream.originalContentPlanContractVersion() == 4u)
                || (outputVersion == 2u && upstream.hasOriginalFiniteGeneralShaderResourceProfile()
                    && !upstream.hasOriginalGeneralShaderResourceProfile()
                    && upstream.originalContentPlanContractVersion() == 5u)
                || (outputVersion == 3u && upstream.hasOriginalFiniteGeneralShaderResourceProfile()
                    && !upstream.hasOriginalGeneralShaderResourceProfile()
                    && upstream.originalContentPlanContractVersion() == 6u)))
            return "RHI-GENERAL-OUTPUT-PENDING-UPSTREAM";
        if (outputVersion == 3u) {
            const auto* content = plan_->rangeContents.findIf([&](const auto& entry) {
                return entry.object == &object;
            });
            if (!content || content->mutations)
                return "RHI-GENERAL-OUTPUT-PENDING-READONLY-CONTENT";
            std::uint64_t cells = 0u;
            for (const auto& cell : plan_->rangeCells.values()) {
                if (cell.controlIndex >= plan_->rangeContents.values().size())
                    return "RHI-GENERAL-OUTPUT-PENDING-READONLY-CONTENT";
                if (plan_->rangeContents.values()[cell.controlIndex].object != &object)
                    continue;
                if (cell.written || !(cell.initial == cell.proposed)
                    || cell.initial.state != Content::State::Completed || !cell.initial.generation)
                    return "RHI-GENERAL-OUTPUT-PENDING-READONLY-CONTENT";
                ++cells;
            }
            if (cells != 1u)
                return "RHI-GENERAL-OUTPUT-PENDING-READONLY-CONTENT";
        }
        // Only the exact attachment control may be held by this reader.
        // Preserve all other stamps and the target's completed content value.
        if (const auto* id = upstream.validateRetainedPlan(outputVersion == 3u ? &object : nullptr))
            return id;
        if (const auto* id = upstream.graph_->validateGraph(false))
            return id;
        if (const auto* id = frozen.registration_.diagnostic())
            return id;
        const auto registration = object.originalResourceRegistration();
        if (const auto* id = registration.diagnostic())
            return id;
        if (!frozen.registration_.matchesIdentity(
                &object, object.id().value, OriginalRegisteredResourceKind::Texture)
            || frozen.resourceGeneration() != registration.generation()
            || frozen.normalized_ != object.originalNormalizedState_
            || frozen.uniform_ != object.originalUniformState_
            || frozen.state()
                != (outputVersion == 3u ? ResourceState::ColorAttachment
                                        : ResourceState::ShaderWrite)
            || !frozen.revision()
            || frozen.range() != defaultTextureSubresourceRange(object.desc()))
            return "RHI-GENERAL-OUTPUT-PENDING-FROZEN-IDENTITY";

        std::uint64_t transitions = 0u;
        for (const auto& transition : plan_->transitions.values())
            if (!transition.buffer && transition.barrier.resource == object.id()) {
                if (transition.normalized != frozen.normalized_
                    || transition.scalar != frozen.uniform_)
                    return "RHI-GENERAL-OUTPUT-PENDING-TRANSITION-CONTROL";
                if (const auto* id = OriginalGeneralOutputReadbackDependencyContract::validate(
                        transition.barrier, frozen.state(), frozen.range(), outputVersion))
                    return id;
                // The sealed plan retains its barrier value; authenticate the
                // same authored input as well, so a changed scope cannot pass
                // by validating only that earlier frozen copy.
                const auto& inputs = *graph_->originalInputs();
                const auto& usage = *inputs.originalScalars()->originalUsage();
                if (transition.command >= usage.commands().size()
                    || usage.commands()[transition.command].kind
                        != CommandUsageKind::TextureBarrier)
                    return "RHI-GENERAL-OUTPUT-PENDING-TRANSITION-INPUT";
                const auto* descriptor = inputs.descriptorAt(transition.command);
                const auto* dependency =
                    descriptor ? std::get_if<ResourceDependencyArguments>(descriptor) : nullptr;
                ResourceBarrierDesc authored;
                if (!dependency || !dependency->write(CommandUsageKind::TextureBarrier, authored)
                    || authored.resource != transition.barrier.resource)
                    return "RHI-GENERAL-OUTPUT-PENDING-TRANSITION-INPUT";
                if (const auto* id = OriginalGeneralOutputReadbackDependencyContract::validate(
                        authored, frozen.state(), frozen.range(), outputVersion))
                    return id;
                ++transitions;
            }
        if (transitions != 1u)
            return "RHI-GENERAL-OUTPUT-PENDING-TRANSITION";
        std::uint64_t captures = 0u;
        const auto& inventory = sources(*graph_).sources();
        for (const auto& entry : graph_->textureEntries()) {
            if (Projection::originalTexture(inventory[entry.point.sourceIndex]) != &object)
                continue;
            const auto& initial = entry.observation;
            if (const auto* id = initial.registration_.diagnostic())
                return id;
            if (!initial.registration_.matchesIdentity(
                    &object, object.id().value, OriginalRegisteredResourceKind::Texture)
                || initial.resourceGeneration() != frozen.resourceGeneration()
                || initial.normalized_ != frozen.normalized_ || initial.uniform_ != frozen.uniform_
                || initial.range() != frozen.range() || initial.state() != frozen.state()
                || initial.revision() != frozen.revision())
                return "RHI-GENERAL-OUTPUT-PENDING-GRAPH-CAPTURE";
            ++captures;
        }
        if (!captures)
            return "RHI-GENERAL-OUTPUT-PENDING-GRAPH-CAPTURE";
        std::uint64_t delegated = 0u;
        for (const auto& cell : upstream.plan_->cells.values()) {
            if (cell.texture != &object) {
                if (const auto* id = upstream.validatePlannedFinalCell(cell))
                    return id;
                continue;
            }
            if (cell.buffer || cell.range != frozen.range() || cell.final != frozen.state()
                || cell.normalized != frozen.normalized_ || cell.scalar != frozen.uniform_)
                return "RHI-GENERAL-OUTPUT-PENDING-UPSTREAM-CELL";
            const auto* downstream = plan_->cells.findIf([&](const auto& value) {
                return value.texture == &object && value.range == cell.range;
            });
            if (!downstream || downstream->buffer || downstream->initial != frozen.state()
                || downstream->final != ResourceState::CopySrc
                || downstream->normalized != cell.normalized || downstream->scalar != cell.scalar)
                return "RHI-GENERAL-OUTPUT-PENDING-DOWNSTREAM-CELL";
            if (cell.normalized) {
                if (!cell.textureCommitted || cell.textureCommitted->range != frozen.range()
                    || cell.textureCommitted->state != frozen.state()
                    || cell.textureCommitted->revision != frozen.revision()
                    || !downstream->texturePending)
                    return "RHI-GENERAL-OUTPUT-PENDING-REVISION";
                if (const auto* id =
                        OriginalGeneralOutputReadbackDependencyContract::validatePendingStates(
                            frozen.state(),
                            downstream->initial,
                            downstream->final,
                            downstream->texturePending->state(),
                            frozen.revision(),
                            downstream->texturePending->revision(),
                            outputVersion))
                    return id;
            } else {
                if (!cell.scalarCommitted || cell.scalarCommitted->value != frozen.state()
                    || cell.scalarCommitted->revision != frozen.revision()
                    || !downstream->scalarPending)
                    return "RHI-GENERAL-OUTPUT-PENDING-REVISION";
                if (const auto* id =
                        OriginalGeneralOutputReadbackDependencyContract::validatePendingStates(
                            frozen.state(),
                            downstream->initial,
                            downstream->final,
                            downstream->scalarPending->value(),
                            frozen.revision(),
                            downstream->scalarPending->revision(),
                            outputVersion))
                    return id;
            }
            // validate() above proves CopySrc is Pending in this exact journal.
            ++delegated;
        }
        return delegated == 1u ? nullptr : "RHI-GENERAL-OUTPUT-PENDING-CELL-COVERAGE";
    }

    const char* validatePlannedFinals() const noexcept
    {
        if (const auto* id = graph_->validateGraph(false))
            return id;
        for (const auto& cell : plan_->cells.values())
            if (const auto* id = validatePlannedFinalCell(cell))
                return id;
        return nullptr;
    }

    static const OriginalCommandResourceSources& sources(const Graph& graph) noexcept
    {
        return *graph.originalInputs()->originalScalars()->originalUsage()->originalSources();
    }

    static std::shared_ptr<core::AtomicCapacityBudget> countDomain(
        const std::shared_ptr<const Graph>& graph) noexcept
    {
        const auto& source =
            graph->originalInputs()->originalScalars()->originalUsage()->originalSources();
        return std::shared_ptr<core::AtomicCapacityBudget>(source, &source->stateTransactions_);
    }

    static core::SharedIdentityProjection::Record controlRecord(
        const std::shared_ptr<const Graph>& graph, std::size_t index) noexcept
    {
        const auto& inventory = sources(*graph).sources();
        if (index < graph->bufferEntries().size()) {
            const auto& entry = graph->bufferEntries()[index];
            const auto& lease = std::get<std::shared_ptr<const OriginalCommandBufferLease>>(
                inventory[entry.sourceIndex]);
            const auto* object = lease->originalObject();
            return {object->id().value,
                static_cast<std::uint32_t>(CommandReferenceDomain::Buffer),
                lease,
                object->originalStateControl_};
        }
        const auto& entry = graph->textureEntries()[index - graph->bufferEntries().size()];
        const auto& source = inventory[entry.point.sourceIndex];
        const auto* object = Projection::originalTexture(source);
        const auto owner = std::visit(
            [](const auto& lease) -> std::shared_ptr<const void> {
                using T = std::remove_const_t<typename std::decay_t<decltype(lease)>::element_type>;
                if constexpr (std::is_same_v<T, OriginalCommandTextureViewSource>)
                    return lease->originalParentLease();
                else
                    return lease;
            },
            source);
        const void* control = object->originalNormalizedState_
            ? static_cast<const void*>(object->originalNormalizedState_)
            : static_cast<const void*>(object->originalUniformState_);
        return {object->id().value,
            static_cast<std::uint32_t>(CommandReferenceDomain::Texture),
            owner,
            control};
    }

    static const char* preflight(const std::shared_ptr<const Graph>& graph,
        const OriginalCommandStateAdmissionBudget& budget,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        std::uint32_t& owners,
        std::uint32_t& controls,
        bool finiteAdmission = false) noexcept
    {
        if (const auto* id = OriginalCommandStateAdmissionContract::validateIntent(budget))
            return id;
        // The graph-only entry remains closed. Only the private builder reached
        // by the StateSequence5/ContentPlan5 overload can admit this root.
        if (budget.contractVersion == 5u && !finiteAdmission)
            return "RHI-ORIGINAL-FINITE-TRANSACTION-CONTENT-PROFILE-UNIMPLEMENTED";
        if (!graph || !graph->hasQuotaBoundSharedOwner(graph))
            return "RHI-ORIGINAL-RESOURCE-STATES-OWNER";
        if (const auto* id = graph->validateGraph(false))
            return id;
        if (graph->budget_.contractVersion != budget.contractVersion)
            return "RHI-ORIGINAL-TRANSACTION-STATE-PROFILE";
        if (!graph->budget_.bindlessRoots.matches(budget.bindlessRoots))
            return "RHI-ORIGINAL-BINDLESS-TRANSACTION-FOOTPRINT-MISMATCH";
        if (budget.contractVersion >= 2u
            && !OriginalCommandDescriptorConsumptionContract::matches(
                graph->budget_.descriptorConsumption, budget.descriptorConsumption))
            return "RHI-ORIGINAL-TRANSACTION-FOOTPRINT-MISMATCH";
        if (budget.contractVersion >= 3u
            && !graph->budget_.renderDescriptorUsage.matches(budget.renderDescriptorUsage))
            return "RHI-ORIGINAL-TRANSACTION-RENDER-FOOTPRINT-MISMATCH";
        if ((budget.contractVersion == 4u || budget.bindlessRoots.geometryJoined())
            && !graph->budget_.geometryUsage.matches(budget.geometryUsage))
            return "RHI-ORIGINAL-TRANSACTION-GEOMETRY-FOOTPRINT-MISMATCH";
        if (graph->budget_.retainedReadbackStateVersion != budget.retainedReadbackStateVersion
            || graph->budget_.retainedReadbackStateOptIn != budget.retainedReadbackStateOptIn)
            return "RHI-ORIGINAL-TRANSACTION-READBACK-PROFILE";
        const auto& source = sources(*graph);
        const auto& original = source.originalBudget();
        const auto bytes = original.maxCpuBytes[static_cast<std::size_t>(
            OriginalCommandAdmissionBudget::CpuDomain::Recording)];
        if (!original.maxStateTransactions || !bytes)
            return "RHI-ORIGINAL-TRANSACTION-RECORDING-BUDGET";
        if (!quota || budget.maxCpuMetadataBytes != bytes || quota->limitBytes() != bytes)
            return "RHI-ORIGINAL-TRANSACTION-QUOTA";
        if (graph->overlapsSubmittedReadQuota(quota))
            return "RHI-SUBMISSION-READ-QUOTA";
        for (const auto& other : {source.originalMetadataQuota(),
                 graph->originalInputs()->originalMetadataQuota(),
                 graph->originalMetadataQuota()})
            if (core::SharedOwnerProjection::sameControlBlock(quota, other))
                return "RHI-ORIGINAL-TRANSACTION-QUOTA-DOMAIN";
        const auto buffers = graph->bufferEntries().size(), ranges = graph->textureEntries().size();
        const auto observations =
            std::min(budget.maxStateObservations, original.maxStateObservations);
        if (buffers > observations || ranges > observations - buffers)
            return "RHI-ORIGINAL-TRANSACTION-OBSERVATION-CAPACITY";
        if (ranges > budget.maxTextureRanges || ranges > original.maxTextureRanges)
            return "RHI-ORIGINAL-TRANSACTION-RANGE-CAPACITY";
        const auto count = buffers + ranges;
        const auto identity = core::SharedIdentityProjection::inspect(
            count, count, budget.maxStateObservations, [&](std::size_t index) noexcept {
                return controlRecord(graph, index);
            });
        if (identity.status != core::SharedIdentityProjection::Status::Complete)
            return "RHI-ORIGINAL-TRANSACTION-CONTROL-IDENTITY";
        controls = static_cast<std::uint32_t>(identity.identities);
        const auto cpu = graph->originalInputs()->retainedOwnerCount();
        const std::uint64_t roots = graph->retainedStateRootCount() + 1u;
        // Original graph plus this root, and the explicitly retained read custody.
        if (roots > UINT32_MAX || cpu > UINT32_MAX - roots)
            return "RHI-ORIGINAL-STATE-OWNER-CAPACITY";
        std::uint32_t registrations = 0u;
        return OriginalCommandResourceSources::registeredOwnerUnion(
            graph->originalInputs()->originalScalars()->originalUsage()->originalSources(),
            static_cast<std::uint32_t>(cpu + roots),
            budget.maxRetainedOwners,
            budget.maxResourceRegistrations,
            owners,
            registrations);
    }

    const char* failedEnlistment() noexcept
    {
        rollbackOriginalOwnership();
        return "RHI-ORIGINAL-TRANSACTION-JOURNAL-ADMISSION";
    }

    // Declaration order is a lifetime contract: journal dies before pointers,
    // count alias and the graph which owns every original resource/control.
    const std::shared_ptr<const Graph> graph_;
    const OriginalCommandStateAdmissionBudget budget_;
    const std::uint32_t owners_, controls_;
    core::AtomicCapacityLease countLease_;
    core::BoundedSequence<BufferEntry> buffers_;
    core::BoundedSequence<TextureEntry> textures_;
    std::optional<Plan> plan_;
    Status status_ = Status::Prepared;
    bool recordingSealed_ = false;
    core::MutationJournal journal_;
};
} // namespace arp::rhi

// endregion original_command_state_transaction
// endregion Command state transaction

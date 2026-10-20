#pragma once

#include "core/atomic_capacity.h"
#include "rhi/backends/metal/metal_closed_consumer_issuance.h"
#include "rhi/backends/metal/metal_graphics_preparation.h"
#include "rhi/backends/metal/metal_instance_attribute_consumer.h"
#include "rhi/backends/metal/metal_native_commands.h"
#include "rhi/backends/metal/metal_ordinary_binding.h"
#include "rhi/backends/metal/metal_prepared_function_ownership.h"
#include "rhi/backends/metal/metal_sealed_buffer_upload.h"
#include "rhi/backends/metal/metal_zero_base_indirect_consumer_cold_bindings.h"
#include "rhi/frontend/bindless_recording_validation.h"
#include "rhi/frontend/command_draw_validation.h"
#include "rhi/semantics/native_backend_capabilities.h"
#include "shader/compute_indirect_producer_proof.h"
#include "shader/single_entry_shader_artifact.h"

#include <utility>

// region Metal indirect producer pipeline
namespace arp::rhi_metal {
// Cold private pipeline prerequisite only; no output/dispatch/recording ticket.
class MetalRhiIndirectProducerPipeline final : public core::SharedObjectIdentity {
    class ConstructionKey {
        friend class MetalRhiIndirectProducerPipeline;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    MetalRhiIndirectProducerPipeline(ConstructionKey,
        std::shared_ptr<const shader::SingleEntryShaderArtifact> artifact,
        core::UniquePtr<rhi::ShaderPackageDesc> interface,
        core::UniquePtr<rhi::PipelineLayout> layout,
        core::UniquePtr<rhi::GpuIndirectProducerContract> contract,
        shader::ComputeIndirectProducerProof::Facts proof,
        std::shared_ptr<MetalRhiNativePipelinePayload> native,
        MetalRhiBufferBindingFact fact,
        std::shared_ptr<core::CpuMemoryQuota> quota)
        : artifact_(std::move(artifact)), interface_(std::move(interface)),
          layout_(std::move(layout)), contract_(std::move(contract)), proof_(proof),
          native_(std::move(native)), fact_(fact), quota_(std::move(quota))
    {
        owners_.capture([&](auto&& visit) {
            projectOwners(visit);
        });
    }

    MetalRhiIndirectProducerPipeline(const MetalRhiIndirectProducerPipeline&) = delete;
    static std::shared_ptr<const MetalRhiIndirectProducerPipeline> prepare(
        std::shared_ptr<const shader::SingleEntryShaderArtifact>,
        const rhi::PipelineLayoutDesc&,
        const rhi::semantics::NativeBackendCapabilityContract&,
        const MetalRhiOrdinaryBindingBudget&,
        std::shared_ptr<core::CpuMemoryQuota>,
        core::pmr::String&);
    const char* validate() const noexcept;

    const auto& artifact() const noexcept { return artifact_; }

    const auto& native() const noexcept { return native_; }

    const auto& proof() const noexcept { return proof_; }

    const auto& fact() const noexcept { return fact_; }

    const auto& contract() const noexcept { return *contract_; }

    const auto& interface() const noexcept { return *interface_; }

    const auto& layout() const noexcept { return *layout_; }

    const auto& quota() const noexcept { return quota_; }

    std::size_t childOwnerCount() const noexcept { return owners_.size(); }

    auto childOwnerAt(std::size_t ordinal) const noexcept { return owners_.at(ordinal); }

private:
    template <class Visitor> void projectOwners(Visitor&& visit) const
    {
        visit(artifact_);
        for (auto& owner : MetalRhiPreparedFunctionOwnership::pipelineOwners(native_))
            if (owner)
                visit(std::move(owner));
    }

    std::shared_ptr<const shader::SingleEntryShaderArtifact> artifact_;
    core::UniquePtr<rhi::ShaderPackageDesc> interface_;
    core::UniquePtr<rhi::PipelineLayout> layout_;
    core::UniquePtr<rhi::GpuIndirectProducerContract> contract_;
    shader::ComputeIndirectProducerProof::Facts proof_;
    std::shared_ptr<MetalRhiNativePipelinePayload> native_;
    MetalRhiBufferBindingFact fact_;
    std::shared_ptr<core::CpuMemoryQuota> quota_;
    core::SharedOwnerProjection owners_;
};

// endregion Metal indirect producer pipeline

// region Metal canonical producer recording preparation

// Genuine cold resource/command/fault reservation; no GPU recording authority.
class MetalRhiCanonicalProducerRecordingPreparation final : public core::SharedObjectIdentity {
    friend class MetalRhiNativeCommandPayload;
    friend class MetalRhiClosedConsumerRecordingInputs;
    friend class MetalRhiClosedConsumerRecordingPreparation;

    class ConstructionKey {
        friend class MetalRhiCanonicalProducerRecordingPreparation;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    using Contract = rhi::CanonicalZeroBaseIndirectProducerRecordingContract;
    using Cold = MetalRhiZeroBaseIndirectConsumerColdBindings;
    using Status = rhi::GpuIndirectOutputLifetimeContract::Status;

    Status status() const noexcept { return status_.load(std::memory_order_acquire); }

    using Buffers = std::array<std::shared_ptr<rhi::GpuBuffer>, 2u>;
    using Resources = std::array<MetalRhiRegisteredResource, 2u>;
    static constexpr std::uint64_t minimumNewBytes =
        sizeof(rhi::DrawIndexedIndirectCommand) + sizeof(rhi::IndirectInstanceBase);

    struct Budget {
        Contract::Budget values;
        std::uint64_t maxNewBytes = 0u;
        std::uint32_t maxChildOwners = 0u;
    };

    MetalRhiCanonicalProducerRecordingPreparation(ConstructionKey,
        std::shared_ptr<const Cold> cold,
        const std::shared_ptr<MetalRhiNativeCommandPayload>& command,
        Buffers buffers,
        Resources resources,
        rhi::BindlessFaultSubmissionLease&& lease,
        rhi::CanonicalZeroBaseIndirectProducerRecordingAdmission admission,
        Budget budget,
        std::uint64_t ownedBytes)
        : cold_(std::move(cold)), command_(command), buffers_(std::move(buffers)),
          resources_(std::move(resources)), lease_(std::move(lease)),
          admission_(std::move(admission)), budget_(budget), ownedBytes_(ownedBytes)
    {
        for (std::size_t i = 0u; i < resources_.size(); ++i)
            observations_[i] = MetalRhiNativeResourceObservation(resources_[i].buffer->resource);
        commandOwners_ = {command->nativeCommandBuffer_, command->nativeDevice_, command->stats_};
        retention_ = command->recordingRetention_.get();
        states_ = command->canonicalProducerStates_;
        slots_ = command->canonicalProducerSlots_;
        owners_.capture([&](auto&& visit) {
            projectOwners(visit);
        });
    }

    MetalRhiCanonicalProducerRecordingPreparation(
        const MetalRhiCanonicalProducerRecordingPreparation&) = delete;
    static std::shared_ptr<const MetalRhiCanonicalProducerRecordingPreparation> prepare(
        std::shared_ptr<const Cold>,
        rhi::GpuDevice&,
        const std::shared_ptr<MetalRhiNativeCommandPayload>&,
        Budget,
        core::pmr::String&);
    const char* validate() const noexcept;

    const char* validateForCommand(
        const std::shared_ptr<MetalRhiNativeCommandPayload>& command) const noexcept
    {
        if (!core::SharedOwnerProjection::sameOwner(command, command_.lock()))
            return "RHI-METAL-CANONICAL-PRODUCER-COMMAND-MISMATCH";
        return validate();
    }

    const auto& cold() const noexcept { return cold_; }

    const auto& buffers() const noexcept { return buffers_; }

    const auto& resources() const noexcept { return resources_; }

    const auto& facts() const noexcept { return admission_.facts(); }

    std::uint64_t ownedBytes() const noexcept { return ownedBytes_; }

    std::size_t childOwnerCount() const noexcept { return owners_.size(); }

    auto childOwnerAt(std::size_t i) const noexcept { return owners_.at(i); }

    static constexpr const char* executionDiagnostic() noexcept
    {
        return "RHI-METAL-CANONICAL-PRODUCER-RECORDING-PREPARATION-ONLY";
    }

private:
    const char* validateForState(rhi::ResourceState, bool finished) const noexcept;
    static const char* validateCommand(
        const Cold&, const MetalRhiNativeCommandPayload&, bool finished = false) noexcept;
    static Contract::Inputs inputs(const std::shared_ptr<const Cold>&, const Buffers&) noexcept;

    template <class Visitor> void projectOwners(Visitor&& visit) const
    {
        visit(cold_);
        for (std::size_t i = 0u; i < cold_->childOwnerCount(); ++i)
            visit(cold_->childOwnerAt(i));
        admission_.projectReferences(visit);
        if (states_)
            visit(states_);
        for (const auto& slot : slots_)
            if (slot)
                visit(slot);
        // The move-only original fault lease retains this genuine domain too.
        visit(cold_->producer()->faultDomain());
        for (std::size_t i = 0u; i < buffers_.size(); ++i) {
            visit(buffers_[i]);
            for (const auto& owner : MetalRhiResourceOwnerProjection::owners(
                     rhi::DescriptorResourceKind::Buffer, resources_[i].buffer, {}, {}, {}))
                if (owner)
                    visit(owner);
        }
    }

    std::shared_ptr<const Cold> cold_;
    std::weak_ptr<MetalRhiNativeCommandPayload> command_;
    std::array<std::weak_ptr<const void>, 3u> commandOwners_;
    const rhi::common::CommandRetentionStorage* retention_ = nullptr;
    Buffers buffers_;
    Resources resources_;
    std::array<MetalRhiNativeResourceObservation, 2u> observations_;
    mutable rhi::BindlessFaultSubmissionLease lease_;
    mutable std::atomic<Status> status_{Status::Cold};
    std::shared_ptr<core::MutationJournal> states_;
    std::array<std::shared_ptr<core::ExternalObjectSlot>, 3u> slots_;
    rhi::CanonicalZeroBaseIndirectProducerRecordingAdmission admission_;
    Budget budget_;
    std::uint64_t ownedBytes_;
    core::SharedOwnerProjection owners_;
};

// endregion Metal canonical producer recording preparation

// region Metal indirect output preparation

class MetalRhiIndirectOutputRecordingLease;

// Cold protected storage/immutable fetch prerequisite. No recording entry takes
// this type as a writer/consumer ticket; the separate ABI/lease prerequisites
// grant no native encoding authority.
class MetalRhiIndirectOutputPreparation final : public core::SharedObjectIdentity {
    friend class MetalRhiIndirectOutputRecordingLease;
    friend class MetalRhiNativeCommandPayload;

    class ConstructionKey {
        friend class MetalRhiIndirectOutputPreparation;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    struct Stream {
        std::shared_ptr<const MetalRhiOrdinaryPreparedVertexBinding> binding;
        std::shared_ptr<rhi::GpuBuffer> wrapper;
    };

    MetalRhiIndirectOutputPreparation(ConstructionKey,
        std::shared_ptr<const MetalRhiIndirectProducerPipeline> producer,
        std::shared_ptr<const MetalRhiOrdinaryPreparedIndexBinding> index,
        core::pmr::Vector<Stream>&& streams,
        rhi::IndirectArgumentConsumptionContract&& expectation,
        MetalRhiSealedBufferUpload::Result storage,
        rhi::GpuIndirectOutputPreparationBudget budget)
        : producer_(std::move(producer)), index_(std::move(index)), streams_(std::move(streams)),
          expectation_(std::move(expectation)), storage_(std::move(storage)),
          observation_(storage_.resource.buffer->resource), budget_(budget),
          generation_(storage_.buffer->id().value)
    {
        owners_.capture([&](auto&& visit) {
            projectOwners(visit);
        });
    }

    MetalRhiIndirectOutputPreparation(const MetalRhiIndirectOutputPreparation&) = delete;
    static std::shared_ptr<const MetalRhiIndirectOutputPreparation> prepare(
        std::shared_ptr<const MetalRhiIndirectProducerPipeline>,
        std::shared_ptr<const MetalRhiOrdinaryPreparedIndexBinding>,
        rhi::GpuDevice&,
        const Stream*,
        std::size_t,
        const rhi::GpuIndirectOutputPreparationBudget&,
        core::pmr::String&);
    const char* validate() const noexcept;

    static constexpr const char* executionDiagnostic() noexcept
    {
        return "RHI-METAL-INDIRECT-OUTPUT-PREPARATION-ONLY";
    }

    const auto& producer() const noexcept { return producer_; }

    const auto& index() const noexcept { return index_; }

    const auto& streams() const noexcept { return streams_; }

    const auto& expectation() const noexcept { return expectation_; }

    const rhi::GpuBuffer& resource() const noexcept { return *storage_.buffer; }

    const auto& buffer() const noexcept { return storage_.resource.buffer; }

    const auto& quota() const noexcept { return producer_->quota(); }

    std::uint64_t generation() const noexcept { return generation_; }

    std::uint64_t leasedRecordings() const noexcept { return recordings_.used(); }

    bool generationConsumed() const noexcept { return consumed_.load(std::memory_order_acquire); }

    std::size_t childOwnerCount() const noexcept { return owners_.size(); }

    auto childOwnerAt(std::size_t ordinal) const noexcept { return owners_.at(ordinal); }

private:
    const char* validateForState(rhi::ResourceState) const noexcept;
    static const char* validateInputs(
        const std::shared_ptr<const MetalRhiIndirectProducerPipeline>&,
        const std::shared_ptr<const MetalRhiOrdinaryPreparedIndexBinding>&,
        const Stream*,
        std::size_t,
        const rhi::IndirectArgumentConsumptionContract&) noexcept;

    template <class Visitor> void projectOwners(Visitor&& visit) const
    {
        visit(producer_);
        for (std::size_t i = 0u; i < producer_->childOwnerCount(); ++i)
            visit(producer_->childOwnerAt(i));
        visit(index_);
        for (std::size_t i = 0u; i < index_->childOwnerCount(); ++i)
            visit(index_->childOwnerAt(i));
        for (const auto& stream : streams_) {
            visit(stream.binding);
            visit(stream.wrapper);
            for (std::size_t i = 0u; i < stream.binding->childOwnerCount(); ++i)
                visit(stream.binding->childOwnerAt(i));
        }
        visit(storage_.buffer);
        for (const auto& owner : MetalRhiResourceOwnerProjection::owners(
                 rhi::DescriptorResourceKind::Buffer, storage_.resource.buffer, {}, {}, {}))
            if (owner)
                visit(owner);
    }

    std::shared_ptr<const MetalRhiIndirectProducerPipeline> producer_;
    std::shared_ptr<const MetalRhiOrdinaryPreparedIndexBinding> index_;
    core::pmr::Vector<Stream> streams_;
    rhi::IndirectArgumentConsumptionContract expectation_;
    MetalRhiSealedBufferUpload::Result storage_;
    MetalRhiNativeResourceObservation observation_;
    rhi::GpuIndirectOutputPreparationBudget budget_;
    core::SharedOwnerProjection owners_;
    const std::uint64_t generation_;
    // Exclusive by contract, independent of a caller's command/owner budgets.
    mutable core::AtomicCapacityBudget recordings_{1u};
    mutable std::atomic<bool> consumed_{false};
};

// endregion Metal indirect output preparation

// region Metal closed consumer recording inputs

// Original native cold roots to shared structural inputs. No native command,
// immutable readback observation, protected writer or completion authority.
class MetalRhiClosedConsumerRecordingInputs final {
public:
    using Issuance = MetalRhiClosedConsumerIssuance;
    using Producer = MetalRhiCanonicalProducerRecordingPreparation;
    using Contract = rhi::CanonicalClosedConsumerRecordingContract;

    static const char* capture(const std::shared_ptr<const Issuance>& root,
        const Producer::Buffers& readbacks,
        Contract::Budget budget,
        std::uint64_t recording,
        Contract::Inputs& output,
        Contract::Facts& facts) noexcept
    {
        return observeStorage(
            root, readbacks, budget, recording, output, facts, Contract::StoragePhase::Cold);
    }

    static const char* observeClosedReadbackReady(const std::shared_ptr<const Issuance>& root,
        const Producer::Buffers& readbacks,
        Contract::Budget budget,
        std::uint64_t recording,
        Contract::Inputs& output,
        Contract::Facts& facts) noexcept
    {
        return observeStorage(root,
            readbacks,
            budget,
            recording,
            output,
            facts,
            Contract::StoragePhase::ReadbackReady);
    }

private:
    static const char* observeStorage(const std::shared_ptr<const Issuance>& root,
        const Producer::Buffers& readbacks,
        Contract::Budget budget,
        std::uint64_t recording,
        Contract::Inputs& output,
        Contract::Facts& facts,
        Contract::StoragePhase phase) noexcept
    {
        output = {};
        facts = {};
        if (const auto* id = budget.validate())
            return id;
        if (!root || !root->sharedOwnerMatches(root))
            return "RHI-METAL-CLOSED-CONSUMER-INPUTS-ORIGINAL-OWNER";
        const auto* rootId = phase == Contract::StoragePhase::Cold
            ? root->validate()
            : root->validateClosedReadbackReady();
        if (rootId)
            return rootId;
        const auto& source = root->cold()->producer()->packet()->pipeline()->source();
        const auto& device = root->images()->geometry()->pipeline()->native()->nativeDevice;
        for (std::size_t i = 0u; i < readbacks.size(); ++i) {
            const auto& buffer = readbacks[i];
            if (!buffer || !buffer->sharedOwnerMatches(buffer))
                return "RHI-METAL-CLOSED-CONSUMER-INPUTS-READBACK-OWNER";
            const auto resource = source->index()->registry()->find(buffer->id());
            const auto native = metalRhiNativePayloadFor(*buffer);
            if (!resource || !resource->buffer || !native
                || !core::SharedOwnerProjection::sameOwner(resource->buffer, native)
                || !native->resource.nativeObject || !native->resource.allocationReleaseToken)
                return "RHI-METAL-CLOSED-CONSUMER-INPUTS-READBACK-NATIVE";
            if (!core::SharedOwnerProjection::sameOwner(device, native->resource.nativeDevice))
                return "RHI-METAL-CLOSED-CONSUMER-INPUTS-READBACK-DEVICE";
            if (!native->resource.nativeRuntimeBacked
                || native->resource.nativeRuntimeDiagnosticOnly)
                return "RHI-METAL-CLOSED-CONSUMER-INPUTS-READBACK-RUNTIME";
            if (native->resource.storageMode
                != metalRhiNativeStorageMode(MetalRhiMemoryPolicy::ManagedReadback))
                return "RHI-METAL-CLOSED-CONSUMER-INPUTS-READBACK-STORAGE";
            if (const auto* id = validateMetalRhiTrackedNativeBufferObject(
                    native->resource, buffer->desc().sizeBytes))
                return id;
        }
        Contract::Inputs inputs;
        inputs.producer = Producer::inputs(root->cold(), readbacks);
        const auto& geometry = root->images()->geometry();
        inputs.geometry = geometry->sharedContent();
        inputs.vertex = geometry->pipeline()->sharedVertexInput();
        inputs.geometryBuffer = geometry->buffer();
        inputs.metadata = root->cold()->metadata().buffer;
        inputs.contents = root->images()->contents();
        for (std::size_t i = 0u; i < inputs.revisions.size(); ++i)
            inputs.revisions[i] = root->images()->markers()[i]->revision();
        Contract::Facts checked;
        if (const auto* id = Contract::verify(inputs, budget, recording, checked, phase))
            return id;
        output = std::move(inputs);
        facts = checked;
        return nullptr;
    }
};

// endregion Metal closed consumer recording inputs

// region Metal indirect output recording lease

// Ownership reservation only. Approved execution must separately transfer this
// move-only value into native completion storage; it cannot authorize GPU work.
class MetalRhiIndirectOutputRecordingLease final {
public:
    MetalRhiIndirectOutputRecordingLease() = default;
    MetalRhiIndirectOutputRecordingLease(const MetalRhiIndirectOutputRecordingLease&) = delete;
    MetalRhiIndirectOutputRecordingLease& operator=(
        const MetalRhiIndirectOutputRecordingLease&) = delete;

    MetalRhiIndirectOutputRecordingLease(MetalRhiIndirectOutputRecordingLease&& other) noexcept
        : output_(std::move(other.output_)), command_(std::move(other.command_)),
          generation_(std::exchange(other.generation_, 0u)),
          recording_(std::exchange(other.recording_, 0u))
    {}

    MetalRhiIndirectOutputRecordingLease& operator=(
        MetalRhiIndirectOutputRecordingLease&& other) noexcept
    {
        if (this != &other) {
            release();
            output_ = std::move(other.output_);
            command_ = std::move(other.command_);
            generation_ = std::exchange(other.generation_, 0u);
            recording_ = std::exchange(other.recording_, 0u);
        }
        return *this;
    }

    ~MetalRhiIndirectOutputRecordingLease() { release(); }

    bool valid() const noexcept { return static_cast<bool>(output_); }

    static MetalRhiIndirectOutputRecordingLease acquire(
        std::shared_ptr<const MetalRhiIndirectOutputPreparation> output,
        const std::shared_ptr<MetalRhiNativeCommandPayload>& command,
        const char*& diagnostic) noexcept
    {
        diagnostic = nullptr;
        if (!output || !command) {
            diagnostic = "RHI-METAL-INDIRECT-OUTPUT-RECORDING-ROOT-MISSING";
            return {};
        }
        if (const auto* id = output->validate()) {
            diagnostic = id;
            return {};
        }
        if (!output->sharedOwnerMatches(output)) {
            diagnostic = "RHI-METAL-INDIRECT-OUTPUT-RECORDING-OWNER-MISMATCH";
            return {};
        }
        if (!command->sharedOwnerMatches(command)) {
            diagnostic = "RHI-METAL-INDIRECT-OUTPUT-RECORDING-OWNER-MISMATCH";
            return {};
        }
        if (const auto* id = validateCommand(*output, *command)) {
            diagnostic = id;
            return {};
        }
        if (!output->recordings_.tryAcquire(1u)) {
            diagnostic = "RHI-METAL-INDIRECT-OUTPUT-IN-FLIGHT";
            return {};
        }
        MetalRhiIndirectOutputRecordingLease result;
        result.generation_ = output->generation();
        result.recording_ = command->recordingIdentity();
        result.output_ = std::move(output);
        result.command_ = command;
        return result;
    }

    const char* validateForRecord(const MetalRhiIndirectOutputPreparation& output,
        const MetalRhiNativeCommandPayload& command,
        std::uint64_t generation) const noexcept
    {
        const auto observed = command_.lock();
        if (!output_ || output_.get() != &output || observed.get() != &command)
            return "RHI-GPU-INDIRECT-OUTPUT-RECORDING-MISMATCH";
        if (const auto* id = rhi::GpuIndirectOutputRecordingContract::validateIdentity(
                generation, command.recordingIdentity(), generation_, recording_))
            return id;
        if (const auto* id = output.validate())
            return id;
        return validateCommand(output, command);
    }

private:
    static const char* validateCommand(const MetalRhiIndirectOutputPreparation& output,
        const MetalRhiNativeCommandPayload& command) noexcept
    {
        if (!command.recordingIdentity_ || !command.nativeCommandBuffer_ || !command.stats_
            || !command.stats_->nativeRuntimeBacked || command.stats_->nativeRuntimeDiagnosticOnly
            || command.abandoned_ || command.recordingPoisoned_
            || command.submitted_.load(std::memory_order_acquire)
            || command.stats_->nativeCommandBufferFinished || !command.recordingRetention_
            || command.recordingRetention_->state()
                != rhi::common::CommandRetentionStorage::State::Recording)
            return "RHI-METAL-INDIRECT-OUTPUT-RECORDING-CLOSED";
        if (command.recordingRetention_->quota() != output.quota())
            return "RHI-COMMAND-RETENTION-CPU-QUOTA-MISMATCH";
        if (command.resourceRegistry_ != output.index()->registry()
            || !core::SharedOwnerProjection::sameOwner(
                command.nativeDevice_, output.producer()->native()->nativeDevice))
            return "RHI-METAL-INDIRECT-OUTPUT-RECORDING-ISSUER-MISMATCH";
        return nullptr;
    }

    void release() noexcept
    {
        if (output_) {
            output_->recordings_.release(1u);
            output_.reset();
        }
        command_.reset();
        generation_ = 0u;
        recording_ = 0u;
    }

    std::shared_ptr<const MetalRhiIndirectOutputPreparation> output_;
    std::weak_ptr<const MetalRhiNativeCommandPayload> command_;
    std::uint64_t generation_ = 0u, recording_ = 0u;
};

// endregion Metal indirect output recording lease

// region Metal closed consumer recording preparation

// Cold command-bound admission only. No packet/fault lease or GPU recording authority.
class MetalRhiClosedConsumerRecordingPreparation final : public core::SharedObjectIdentity {
    friend class MetalRhiClosedConsumerRecordingReservation;
    friend class MetalRhiNativeCommandPayload;

    class ConstructionKey {
        friend class MetalRhiClosedConsumerRecordingPreparation;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    using Builder = MetalRhiClosedConsumerRecordingInputs;
    using Issuance = Builder::Issuance;
    using Contract = Builder::Contract;
    using Buffers = Builder::Producer::Buffers;
    using Resources = Builder::Producer::Resources;
    static constexpr std::uint64_t minimumNewBytes = Builder::Producer::minimumNewBytes;

    struct Budget {
        Contract::Budget values;
        std::uint64_t maxNewBytes = 0u;
        std::uint32_t maxChildOwners = 0u;
    };

    MetalRhiClosedConsumerRecordingPreparation(ConstructionKey,
        std::shared_ptr<const Issuance> root,
        const std::shared_ptr<MetalRhiNativeCommandPayload>& command,
        Buffers buffers,
        Resources resources,
        rhi::CanonicalClosedConsumerRecordingAdmission admission,
        Budget budget,
        std::uint64_t bytes);
    MetalRhiClosedConsumerRecordingPreparation(
        const MetalRhiClosedConsumerRecordingPreparation&) = delete;
    static std::shared_ptr<const MetalRhiClosedConsumerRecordingPreparation> prepare(
        std::shared_ptr<const Issuance>,
        rhi::GpuDevice&,
        const std::shared_ptr<MetalRhiNativeCommandPayload>&,
        Budget,
        core::pmr::String&);
    const char* validate() const noexcept;

    const char* validateForCommand(
        const std::shared_ptr<MetalRhiNativeCommandPayload>& command) const noexcept
    {
        if (!core::SharedOwnerProjection::sameOwner(command, command_.lock()))
            return "RHI-METAL-CLOSED-CONSUMER-COMMAND-MISMATCH";
        return validate();
    }

    const auto& issuance() const noexcept { return root_; }

    const auto& buffers() const noexcept { return buffers_; }

    const auto& resources() const noexcept { return resources_; }

    const auto& facts() const noexcept { return admission_.facts(); }

    std::uint64_t ownedBytes() const noexcept { return ownedBytes_; }

    std::size_t childOwnerCount() const noexcept { return owners_.size(); }

    auto childOwnerAt(std::size_t i) const noexcept { return owners_.at(i); }

    static constexpr const char* executionDiagnostic() noexcept
    {
        return "RHI-METAL-CLOSED-CONSUMER-COMMAND-ADMISSION-ONLY";
    }

private:
    const char* validateStorage(Contract::StoragePhase, bool finished) const noexcept;
    static const char* validateCommand(
        const Issuance&, const MetalRhiNativeCommandPayload&) noexcept;

    template <class Visitor> void projectOwners(Visitor&& visit) const
    {
        visit(root_);
        for (std::size_t i = 0u; i < root_->childOwnerCount(); ++i)
            visit(root_->childOwnerAt(i));
        admission_.projectReferences(visit);
        for (std::size_t i = 0u; i < buffers_.size(); ++i) {
            visit(buffers_[i]);
            for (const auto& owner : MetalRhiResourceOwnerProjection::owners(
                     rhi::DescriptorResourceKind::Buffer, resources_[i].buffer, {}, {}, {}))
                if (owner)
                    visit(owner);
        }
    }

    std::shared_ptr<const Issuance> root_;
    std::weak_ptr<MetalRhiNativeCommandPayload> command_;
    std::array<std::weak_ptr<const void>, 3u> commandOwners_;
    const rhi::common::CommandRetentionStorage* retention_ = nullptr;
    std::weak_ptr<MetalRhiResourceRegistry> registry_;
    std::weak_ptr<core::CpuMemoryQuota> quota_;
    Buffers buffers_;
    Resources resources_;
    std::array<MetalRhiNativeResourceObservation, 2u> observations_;
    rhi::CanonicalClosedConsumerRecordingAdmission admission_;
    Budget budget_;
    std::uint64_t ownedBytes_ = 0u;
    core::SharedOwnerProjection owners_;
};

// endregion Metal closed consumer recording preparation

// region Metal indirect execution preparation

struct MetalRhiIndirectExecutionPreparationBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxChildOwners = 0u;

    const char* validate() const noexcept
    {
        return contractVersion == 1u && prototypeOptIn && maxChildOwners
            ? nullptr
            : "RHI-METAL-INDIRECT-EXECUTION-PREPARATION-BUDGET";
    }
};

// Prepared cold. A weak command observation avoids a command/root ownership
// cycle; the protected recorder transfers lease/root ownership at submission.
class MetalRhiIndirectExecutionPreparation final : public core::SharedObjectIdentity {
    friend class MetalRhiNativeCommandPayload;

    class ConstructionKey {
        friend class MetalRhiIndirectExecutionPreparation;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    using Status = rhi::GpuIndirectOutputLifetimeContract::Status;

    Status status() const noexcept { return status_.load(std::memory_order_acquire); }

    MetalRhiIndirectExecutionPreparation(ConstructionKey,
        std::shared_ptr<const MetalRhiIndirectOutputPreparation> output,
        std::shared_ptr<const MetalRhiPreparedInstanceAttributeConsumer> consumer,
        std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsBinding> binding,
        std::shared_ptr<const MetalRhiOrdinaryPreparedRenderPass> pass,
        const std::shared_ptr<MetalRhiNativeCommandPayload>& command,
        MetalRhiIndirectOutputRecordingLease&& lease,
        MetalRhiIndirectExecutionPreparationBudget budget);
    MetalRhiIndirectExecutionPreparation(const MetalRhiIndirectExecutionPreparation&) = delete;
    static std::shared_ptr<const MetalRhiIndirectExecutionPreparation> prepare(
        std::shared_ptr<const MetalRhiIndirectOutputPreparation>,
        std::shared_ptr<const MetalRhiPreparedInstanceAttributeConsumer>,
        std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsBinding>,
        std::shared_ptr<const MetalRhiOrdinaryPreparedRenderPass>,
        const std::shared_ptr<MetalRhiNativeCommandPayload>&,
        const MetalRhiIndirectExecutionPreparationBudget&,
        core::pmr::String&);
    const char* validate() const;

    const char* validateForCommand(
        const std::shared_ptr<MetalRhiNativeCommandPayload>& command) const
    {
        if (!core::SharedOwnerProjection::sameOwner(command, command_.lock()))
            return "RHI-METAL-INDIRECT-EXECUTION-COMMAND-MISMATCH";
        return validate();
    }

    const auto& output() const noexcept { return output_; }

    const auto& consumer() const noexcept { return consumer_; }

    const auto& binding() const noexcept { return binding_; }

    const auto& pass() const noexcept { return pass_; }

    const auto& quota() const noexcept { return output_->quota(); }

    const auto& identity() const noexcept { return identity_; }

    std::size_t childOwnerCount() const noexcept { return owners_.size(); }

    auto childOwnerAt(std::size_t i) const noexcept { return owners_.at(i); }

    static constexpr const char* executionDiagnostic() noexcept
    {
        return "RHI-METAL-INDIRECT-EXECUTION-PREPARATION-ONLY";
    }

private:
    static const char* validateInputs(
        const std::shared_ptr<const MetalRhiIndirectOutputPreparation>&,
        const std::shared_ptr<const MetalRhiPreparedInstanceAttributeConsumer>&,
        const std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsBinding>&,
        const std::shared_ptr<const MetalRhiOrdinaryPreparedRenderPass>&,
        const std::shared_ptr<MetalRhiNativeCommandPayload>&);

    template <class Visitor> void projectOwners(Visitor&& visit) const
    {
        const auto add = [&](const auto& root) {
            visit(root);
            for (std::size_t i = 0u; i < root->childOwnerCount(); ++i)
                visit(root->childOwnerAt(i));
        };
        add(output_);
        add(consumer_);
        add(binding_);
        add(pass_);
        visit(states_);
        visit(computeSlot_);
        visit(renderSlot_);
    }

    std::shared_ptr<const MetalRhiIndirectOutputPreparation> output_;
    std::shared_ptr<const MetalRhiPreparedInstanceAttributeConsumer> consumer_;
    std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsBinding> binding_;
    std::shared_ptr<const MetalRhiOrdinaryPreparedRenderPass> pass_;
    std::weak_ptr<MetalRhiNativeCommandPayload> command_;
    std::weak_ptr<const MetalRhiIndirectExecutionPreparation> self_;
    std::shared_ptr<core::MutationJournal> states_;
    std::shared_ptr<core::ExternalObjectSlot> computeSlot_, renderSlot_;
    rhi::GpuIndirectOutputRecordingIdentity identity_;
    mutable MetalRhiIndirectOutputRecordingLease lease_;
    mutable std::atomic<Status> status_{Status::Cold};
    MetalRhiIndirectExecutionPreparationBudget budget_;
    core::SharedOwnerProjection owners_;
};

// endregion Metal indirect execution preparation

// region Metal closed consumer recording reservation

// Original command-owned cold reservations. Encoding and completion are separate gates.
class MetalRhiClosedConsumerRecordingReservation final : public core::SharedObjectIdentity {
    friend class MetalRhiNativeCommandPayload;
    friend class MetalRhiClosedConsumerNativePreparation;

    class ConstructionKey {
        friend class MetalRhiClosedConsumerRecordingReservation;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    using Preparation = MetalRhiClosedConsumerRecordingPreparation;
    using Status = rhi::GpuIndirectOutputLifetimeContract::Status;
    // Two packet-budget owner roles and two independent fault-lease roles.
    static constexpr std::size_t reservationReferenceSlots = 2u + 2u;

    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint32_t maxChildOwners = 0u;
    };

    MetalRhiClosedConsumerRecordingReservation(ConstructionKey,
        std::shared_ptr<const Preparation> preparation,
        std::shared_ptr<core::AtomicCapacityBudget> packetBudget,
        Budget budget)
        : preparation_(std::move(preparation)), packetBudget_(std::move(packetBudget)),
          packetLease_(packetBudget_, 1u), budget_(budget)
    {}

    MetalRhiClosedConsumerRecordingReservation(
        const MetalRhiClosedConsumerRecordingReservation&) = delete;
    static std::shared_ptr<const MetalRhiClosedConsumerRecordingReservation> prepare(
        std::shared_ptr<const Preparation>, Budget, core::pmr::String&);
    const char* validate() const noexcept;

    const auto& preparation() const noexcept { return preparation_; }

    Status status() const noexcept { return status_.load(std::memory_order_acquire); }

    std::size_t childOwnerCount() const noexcept { return owners_.size(); }

    auto childOwnerAt(std::size_t i) const noexcept { return owners_.at(i); }

    static constexpr const char* executionDiagnostic() noexcept
    {
        return "RHI-METAL-CLOSED-CONSUMER-RECORDING-PAYLOAD-MISSING";
    }

private:
    const char* validateRecorded(bool finished) const noexcept;
    const char* validateStorage(bool recorded, bool finished) const noexcept;
    void cancel(bool failure) const noexcept;

    template <class Visitor> void projectOwners(Visitor&& visit) const
    {
        visit(preparation_);
        for (std::size_t i = 0u; i < preparation_->childOwnerCount(); ++i)
            visit(preparation_->childOwnerAt(i));
        // Original alias owner is held by both the stored budget and core lease.
        visit(packetBudget_);
        visit(packetBudget_);
        visit(preparation_->issuance()->cold()->producer()->faultDomain());
        visit(preparation_->issuance()->cold()->faultDomain());
    }

    std::shared_ptr<const Preparation> preparation_;
    std::shared_ptr<core::AtomicCapacityBudget> packetBudget_;
    mutable core::AtomicCapacityLease packetLease_;
    mutable rhi::BindlessFaultSubmissionLease producerLease_, consumerLease_;
    Budget budget_;
    core::SharedOwnerProjection owners_;
    mutable std::atomic<Status> status_{Status::Cold};
};
} // namespace arp::rhi_metal

// endregion Metal closed consumer recording reservation

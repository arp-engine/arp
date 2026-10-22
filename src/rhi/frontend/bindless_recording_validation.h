#pragma once
#include "core/encoding/unsigned_element_span.h"
#include "core/ownership/shared_owner_projection.h"
#include "rhi/frontend/bindless_fault_domain.h"
#include "rhi/frontend/bindless_image_and_indirect_validation.h"
#include "rhi/frontend/bindless_indirect_packet.h"
#include "rhi/frontend/bindless_snapshot_metadata.h"
#include "rhi/frontend/command_gpu_indirect_output_sequence.h"
#include "rhi/semantics/bindless_preparation_lowering.h"

// region canonical_zero_base_indirect_consumer_cold_binding
namespace arp::rhi {
// Shared values only, never native owner proof or a binding/execution ticket.
class CanonicalZeroBaseIndirectConsumerColdBindingContract final {
public:
    using Consumer = CanonicalZeroBaseIndirectConsumerContract;
    static constexpr std::uint32_t version = 1u;
    using Images = std::array<BindlessTextureInventoryResource, Consumer::capacity>;
    using Words = std::array<std::uint32_t, Consumer::metadataWordCount>;
    using Bytes = std::array<std::uint8_t, sizeof(Words)>;
    // Logical storage observations only; ReadbackReady proves no GPU completion.
    enum class StoragePhase { Cold, ReadbackReady };
    static constexpr std::uint64_t minimumOwnedBytes =
        sizeof(Bytes) + BindlessOwnedFaultDomain::minimumBytes;

    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint64_t maxOwnedBytes = 0u;
        std::uint32_t maxChildOwners = 0u;
    };

    struct Facts {
        std::uint32_t contractVersion = 0u;
        std::uint64_t consumerPackageIdentity = 0u, packetGeneration = 0u,
                      producerFaultGeneration = 0u, consumerFaultGeneration = 0u;
        GpuResourceId mapping;
        Images images;
        Words words{};
        Bytes bytes{};
    };

    struct Lowering {
        bool validatorOnly = false, prototypeColdResources = false, executable = false;
        const char* diagnosticId = "RHI-CANONICAL-CONSUMER-COLD-RESOURCES-BACKEND-UNKNOWN";
    };

    static constexpr const char* executionDiagnostic() noexcept
    {
        return "RHI-CANONICAL-CONSUMER-COLD-RESOURCES-PREPARATION-ONLY";
    }

    static constexpr Lowering lowering(Backend backend) noexcept
    {
        return semantics::BindlessPreparationLowering::
            CanonicalZeroBaseIndirectConsumerColdBinding_lowering<Lowering>(backend,
                &CanonicalZeroBaseIndirectConsumerColdBindingContract::executionDiagnostic);
    }

    static const char* validateBudget(
        Budget budget, std::uint64_t actualOwnedBytes, std::size_t actualOwners) noexcept
    {
        if (budget.contractVersion != version || !budget.prototypeOptIn || !budget.maxOwnedBytes
            || !budget.maxChildOwners)
            return "RHI-CANONICAL-CONSUMER-COLD-RESOURCES-BUDGET";
        if (budget.maxOwnedBytes < minimumOwnedBytes || actualOwnedBytes < minimumOwnedBytes
            || actualOwnedBytes > budget.maxOwnedBytes)
            return "RHI-CANONICAL-CONSUMER-COLD-RESOURCES-BYTE-BUDGET";
        if (!actualOwners || actualOwners > budget.maxChildOwners)
            return "RHI-CANONICAL-CONSUMER-COLD-RESOURCES-OWNER-BUDGET";
        return nullptr;
    }

    static const char* verify(const BindlessShaderPackage& package,
        const ZeroBaseIndirectSourceConsumptionContract& source,
        const BindlessIndirectPacket& packet,
        const GpuBuffer& mapping,
        const Images& images,
        const std::shared_ptr<const BindlessOwnedFaultDomain>& producerFault,
        const std::shared_ptr<const BindlessOwnedFaultDomain>& consumerFault,
        Facts& output,
        StoragePhase phase = StoragePhase::Cold) noexcept
    {
        output = {};
        Consumer::Facts roles;
        if (phase != StoragePhase::Cold && phase != StoragePhase::ReadbackReady)
            return "RHI-CANONICAL-CONSUMER-STORAGE-PHASE";
        const bool ready = phase == StoragePhase::ReadbackReady;
        if (const auto* id = Consumer::verify(package, source, roles))
            return id;
        if (const auto* id = packet.storageDiagnostic())
            return id;
        if (ready) {
            if (const auto* id =
                    BindlessIndirectPacketStorageContract::closedConsumerStructuralDiagnostic(
                        packet.issuance()))
                return id;
            if (packet.physicalArgs()->state() != ResourceState::ShaderRead)
                return "RHI-CANONICAL-CONSUMER-READY-ARGUMENTS-STATE";
        }
        if (&mapping != packet.instanceMappings()
            || mapping.state() != (ready ? ResourceState::ShaderRead : ResourceState::ShaderWrite))
            return "RHI-CANONICAL-CONSUMER-COLD-MAPPING-ORIGINAL-STORAGE";
        const auto& expected = source.parameters();
        const auto& actual = packet.parameters();
        if (actual.sourceWordOffset != expected.sourceWordOffset
            || actual.instanceCapacity != expected.instanceCapacity
            || actual.reserved != expected.reserved
            || actual.contractVersion != expected.contractVersion)
            return "RHI-CANONICAL-CONSUMER-COLD-SOURCE-PARAMETERS";
        if (!producerFault || !producerFault->sharedOwnerMatches(producerFault) || !consumerFault
            || !consumerFault->sharedOwnerMatches(consumerFault))
            return "RHI-CANONICAL-CONSUMER-COLD-FAULT-OWNER";
        if (const auto* id = producerFault->storageDiagnostic())
            return id;
        if (const auto* id = consumerFault->storageDiagnostic())
            return id;
        if (producerFault->generation() == consumerFault->generation()
            || producerFault->generation() == packet.generation()
            || consumerFault->generation() == packet.generation()
            || producerFault.get() == consumerFault.get())
            return "RHI-CANONICAL-CONSUMER-COLD-FAULT-GENERATION";
        if (consumerFault->faultLatchPriorState() != ResourceState::Undefined
            || consumerFault->faultLatchBuffer()->state()
                != (ready ? ResourceState::CopySrc : ResourceState::ShaderWrite)
            || consumerFault->faultReadbackBuffer()->state() != ResourceState::CopyDst)
            return "RHI-CANONICAL-CONSUMER-COLD-FAULT-PENDING-CLEAR";
        const std::array<const GpuBuffer*, 7u> buffers{packet.physicalArgs(),
            &mapping,
            packet.producerParameters(),
            producerFault->faultLatchBuffer(),
            producerFault->faultReadbackBuffer(),
            consumerFault->faultLatchBuffer(),
            consumerFault->faultReadbackBuffer()};
        for (std::size_t i = 0u; i < buffers.size(); ++i) {
            for (std::size_t j = 0u; j < i; ++j)
                if (buffers[i]->id() == buffers[j]->id())
                    return "RHI-CANONICAL-CONSUMER-COLD-RESOURCE-ALIAS";
            for (const auto& image : images)
                if (image.view == buffers[i]->id() || image.parent == buffers[i]->id())
                    return "RHI-CANONICAL-CONSUMER-COLD-RESOURCE-ALIAS";
        }
        BindlessTextureInventoryMetadataSource metadata(
            package.artifact().tables.front(), images.data(), images.size());
        if (const auto* id = metadata.diagnostic())
            return id;
        Facts facts;
        if (const auto* id = bindlessSnapshotMetadataDiagnostic(package.artifact().snapshotMetadata,
                consumerFault->generation(),
                metadata,
                facts.words.data(),
                facts.words.size()))
            return id;
        core::UnsignedElementSpan::encodeWordsLittleEndian(facts.words, facts.bytes);
        facts.contractVersion = version;
        facts.consumerPackageIdentity = roles.consumerPackageIdentity;
        facts.packetGeneration = packet.generation();
        facts.producerFaultGeneration = producerFault->generation();
        facts.consumerFaultGeneration = consumerFault->generation();
        facts.mapping = mapping.id();
        facts.images = images;
        output = facts;
        return nullptr;
    }
};

// endregion canonical_zero_base_indirect_consumer_cold_binding

// region canonical_zero_base_indirect_cold_binding

// Cold value correspondence only. The generation belongs to the real fault
// domain; it does not issue a consumer generation or packet writer ticket.
class CanonicalZeroBaseIndirectColdBindingContract final {
public:
    static constexpr std::uint32_t version = 1u;
    static constexpr std::size_t tableCount = 4u;
    static constexpr std::size_t metadataWordCount = 4u + tableCount * (4u + 1u);
    using Resources = std::array<BindlessMetadataBufferResource, tableCount>;
    using Words = std::array<std::uint32_t, metadataWordCount>;
    using Bytes = std::array<std::uint8_t, metadataWordCount * sizeof(std::uint32_t)>;

    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint64_t maxOwnedBytes = 0u;
        std::uint32_t maxChildOwners = 0u;
    };

    static constexpr std::uint64_t minimumOwnedBytes = sizeof(Bytes)
        + sizeof(DrawIndexedIndirectCommand) + sizeof(IndirectInstanceBase)
        + BindlessOwnedFaultDomain::minimumBytes;

    struct Lowering {
        bool validatorOnly = false;
        bool prototypeColdBindings = false;
        bool executable = false;
        const char* diagnosticId = "RHI-CANONICAL-COLD-BINDINGS-BACKEND-UNKNOWN";
    };

    static constexpr Lowering lowering(Backend backend) noexcept
    {
        return semantics::BindlessPreparationLowering::
            CanonicalZeroBaseIndirectColdBinding_lowering<Lowering>(
                backend, &CanonicalZeroBaseIndirectColdBindingContract::executionDiagnostic);
    }

    static constexpr const char* executionDiagnostic() noexcept
    {
        return "RHI-CANONICAL-COLD-BINDINGS-PREPARATION-ONLY";
    }

    static const char* buildMetadata(const BindlessShaderArtifact& artifact,
        std::uint64_t faultGeneration,
        const Resources& resources,
        Words& words,
        Bytes& bytes)
    {
        words = {};
        bytes = {};
        if (artifact.tables.size() != tableCount
            || artifact.snapshotMetadata.wordCount != metadataWordCount)
            return "RHI-CANONICAL-COLD-BINDINGS-METADATA-LAYOUT";
        BindlessSingleBufferMetadataSource source(
            artifact.tables.data(), artifact.tables.size(), resources.data(), resources.size());
        // The canonical package verifier proves exact operations/strides; the
        // builder independently proves offsets/extents and actual resource counts.
        if (bindlessSnapshotMetadataDiagnostic(
                artifact.snapshotMetadata, faultGeneration, source, words.data(), words.size()))
            return "RHI-CANONICAL-COLD-BINDINGS-METADATA-RESOURCE";
        core::UnsignedElementSpan::encodeWordsLittleEndian(words, bytes);
        return nullptr;
    }
};

// endregion canonical_zero_base_indirect_cold_binding

// region canonical_zero_base_indirect_recording

// Structural admission only: these values cannot grant privileged GPU writes.
class CanonicalZeroBaseIndirectProducerRecordingContract final {
public:
    static constexpr std::uint32_t version = 1u;
    using Producer = CanonicalZeroBaseIndirectSourceContract;
    using Cold = CanonicalZeroBaseIndirectColdBindingContract;
    using Consumer = CanonicalZeroBaseIndirectConsumerColdBindingContract;
    enum class Role : std::size_t {
        Metadata,
        Latch,
        Source,
        Arguments,
        Mapping,
        Parameters,
        ArgumentsReadback,
        MappingReadback,
        FaultReadback,
        Count
    };
    static constexpr std::size_t bufferCount = static_cast<std::size_t>(Role::Count);
    using Buffers = std::array<std::shared_ptr<const GpuBuffer>, bufferCount>;

    struct Inputs {
        std::shared_ptr<const BindlessShaderPackage> producerPackage, consumerPackage;
        std::shared_ptr<const ZeroBaseIndirectSourceConsumptionContract> source;
        std::shared_ptr<const BindlessIndirectPacket> packet;
        std::shared_ptr<const BindlessOwnedFaultDomain> producerFault, consumerFault;
        Buffers buffers;
        Consumer::Images images;
    };

    struct Identity {
        std::uint32_t contractVersion = 0u;
        std::uint64_t producerPackage = 0u, consumerPackage = 0u, packetGeneration = 0u,
                      producerGeneration = 0u, consumerGeneration = 0u, recording = 0u;
        std::array<GpuResourceId, bufferCount> resources{};
        std::array<GpuResourceId, 4u> protectedResources{};

        bool operator==(const Identity& b) const noexcept
        {
            return contractVersion == b.contractVersion && producerPackage == b.producerPackage
                && consumerPackage == b.consumerPackage && packetGeneration == b.packetGeneration
                && producerGeneration == b.producerGeneration
                && consumerGeneration == b.consumerGeneration && recording == b.recording
                && resources == b.resources && protectedResources == b.protectedResources;
        }

        bool valid() const noexcept
        {
            if (contractVersion != version || !producerPackage || !consumerPackage
                || !packetGeneration || !producerGeneration || !consumerGeneration || !recording
                || producerGeneration == consumerGeneration
                || producerGeneration == packetGeneration || consumerGeneration == packetGeneration)
                return false;
            for (std::size_t i = 0u; i < resources.size(); ++i) {
                if (!resources[i].isValid())
                    return false;
                for (std::size_t j = 0u; j < i; ++j)
                    if (resources[j] == resources[i])
                        return false;
            }
            for (std::size_t i = 0u; i < protectedResources.size(); ++i) {
                if (!protectedResources[i].isValid())
                    return false;
                for (const auto resource : resources)
                    if (resource == protectedResources[i])
                        return false;
                for (std::size_t j = 0u; j < i; ++j)
                    if (protectedResources[j] == protectedResources[i])
                        return false;
            }
            return true;
        }
    };
    enum class Command {
        ClearBegin,
        ClearZero,
        ClearEnd,
        ClearDependency,
        ComputeBegin,
        Dispatch,
        ComputeEnd,
        ArgumentsDependency,
        MappingDependency,
        FaultDependency,
        ReadbackBegin,
        ArgumentsCopy,
        MappingCopy,
        FaultCopy,
        ReadbackEnd
    };
    static constexpr std::array<Command, 15u> protocol{Command::ClearBegin,
        Command::ClearZero,
        Command::ClearEnd,
        Command::ClearDependency,
        Command::ComputeBegin,
        Command::Dispatch,
        Command::ComputeEnd,
        Command::ArgumentsDependency,
        Command::MappingDependency,
        Command::FaultDependency,
        Command::ReadbackBegin,
        Command::ArgumentsCopy,
        Command::MappingCopy,
        Command::FaultCopy,
        Command::ReadbackEnd};
    static constexpr std::uint32_t commandUnits = static_cast<std::uint32_t>(protocol.size());
    static constexpr std::uint64_t readbackBytes =
        sizeof(DrawIndexedIndirectCommand) + sizeof(IndirectInstanceBase) + sizeof(std::uint32_t);
    // Counts shared-reference slots, not deduplicated or native owner graphs.
    static constexpr std::size_t referenceSlots = 6u + bufferCount;

    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint32_t maxCommands = 0u;
        std::uint64_t maxReadbackBytes = 0u;
        std::size_t maxReferenceSlots = 0u;

        const char* validate() const noexcept
        {
            return contractVersion == version && prototypeOptIn && maxCommands >= commandUnits
                    && maxReadbackBytes >= readbackBytes && maxReferenceSlots >= referenceSlots
                ? nullptr
                : "RHI-CANONICAL-PRODUCER-RECORDING-BUDGET";
        }
    };
    enum class StoragePhase { Cold, ReadbackReady };
    enum class PrivateDisposition { ValidatorOnly, Prototype, DiagnosticFailure };

    struct PrivateLowering {
        std::uint32_t contractVersion = version;
        PrivateDisposition disposition = PrivateDisposition::DiagnosticFailure;
        const char* diagnosticId = "RHI-CANONICAL-PRODUCER-PRIVATE-BACKEND-UNKNOWN";

        bool allowsPrototypePayload() const noexcept
        {
            return disposition == PrivateDisposition::Prototype;
        }
    };

    static PrivateLowering privateRecordingLowering(Backend backend, Budget budget) noexcept
    {
        if (const auto* id = budget.validate())
            return {version, PrivateDisposition::DiagnosticFailure, id};
        return semantics::BindlessPreparationLowering::
            CanonicalZeroBaseIndirectProducerRecording_privateRecordingLowering<PrivateLowering,
                PrivateDisposition>(backend, version);
    }

    struct Facts {
        Identity identity;
        Cold::Words metadata{};
        Cold::Bytes bytes{};
        std::uint64_t actualReadbackBytes = 0u;
    };

    struct Lowering {
        bool validatorOnly = false, executable = false;
        const char* diagnosticId = "RHI-CANONICAL-PRODUCER-SEQUENCE-BACKEND-UNKNOWN";
    };

    static constexpr Lowering lowering(Backend backend) noexcept
    {
        return semantics::BindlessPreparationLowering::
            CanonicalZeroBaseIndirectProducerRecording_lowering<Lowering>(backend);
    }

    static constexpr const char* executionDiagnostic() noexcept
    {
        return "RHI-CANONICAL-PRODUCER-RECORDING-VALUES-ONLY";
    }

    static const char* verify(const Inputs& in,
        Budget budget,
        std::uint64_t recording,
        Facts& output,
        StoragePhase phase = StoragePhase::Cold) noexcept
    {
        return verifyStorage(in, budget, recording, output, phase, Consumer::StoragePhase::Cold);
    }

    static const char* verifyClosedReadbackReady(
        const Inputs& in, Budget budget, std::uint64_t recording, Facts& output) noexcept
    {
        return verifyStorage(in,
            budget,
            recording,
            output,
            StoragePhase::ReadbackReady,
            Consumer::StoragePhase::ReadbackReady);
    }

private:
    static const char* verifyStorage(const Inputs& in,
        Budget budget,
        std::uint64_t recording,
        Facts& output,
        StoragePhase phase,
        Consumer::StoragePhase consumerPhase) noexcept
    {
        output = {};
        if (const auto* id = budget.validate())
            return id;
        if (phase != StoragePhase::Cold && phase != StoragePhase::ReadbackReady)
            return "RHI-CANONICAL-PRODUCER-RECORDING-STORAGE-PHASE";
        const auto outputState =
            phase == StoragePhase::Cold ? ResourceState::ShaderWrite : ResourceState::CopySrc;
        if (!recording || !in.source || !in.producerPackage || !in.consumerPackage || !in.packet
            || !in.producerFault || !in.consumerFault
            || !in.producerPackage->sharedOwnerMatches(in.producerPackage)
            || !in.consumerPackage->sharedOwnerMatches(in.consumerPackage)
            || !in.packet->sharedOwnerMatches(in.packet)
            || !in.source->sharedOwnerMatches(in.source))
            return "RHI-CANONICAL-PRODUCER-RECORDING-ROOT";
        Producer::Facts producer;
        if (const auto* id = Producer::verify(
                *in.producerPackage, *in.source, Producer::selectedEntry, producer))
            return id;
        Consumer::Facts consumer;
        if (const auto* id = Consumer::verify(*in.consumerPackage,
                *in.source,
                *in.packet,
                *in.packet->instanceMappings(),
                in.images,
                in.producerFault,
                in.consumerFault,
                consumer,
                consumerPhase))
            return id;
        if (in.producerFault->faultLatchPriorState() != ResourceState::Undefined
            || in.producerFault->faultLatchBuffer()->state() != outputState
            || in.producerFault->faultReadbackBuffer()->state() != ResourceState::CopyDst
            || in.producerFault->faultLatchBuffer()->desc().initialState
                != ResourceState::ShaderWrite
            || in.producerFault->faultReadbackBuffer()->desc().initialState
                != ResourceState::CopyDst
            || in.packet->producerParameters()->state() != ResourceState::ShaderRead)
            return "RHI-CANONICAL-PRODUCER-RECORDING-PENDING-CLEAR";
        const auto at = [&](Role role) -> const auto& {
            return in.buffers[static_cast<std::size_t>(role)];
        };
        if (!core::SharedOwnerProjection::sameOwner(at(Role::Latch),
                std::shared_ptr<const GpuBuffer>(
                    in.producerFault, in.producerFault->faultLatchBuffer()))
            || !core::SharedOwnerProjection::sameOwner(at(Role::FaultReadback),
                std::shared_ptr<const GpuBuffer>(
                    in.producerFault, in.producerFault->faultReadbackBuffer()))
            || !core::SharedOwnerProjection::sameOwner(at(Role::Parameters),
                std::shared_ptr<const GpuBuffer>(in.packet, in.packet->producerParameters())))
            return "RHI-CANONICAL-PRODUCER-RECORDING-BUFFER-OWNER";
        Facts facts;
        for (std::size_t i = 0u; i < in.buffers.size(); ++i) {
            const auto& buffer = in.buffers[i];
            const auto role = static_cast<Role>(i);
            if (!buffer || !buffer->id().isValid())
                return "RHI-CANONICAL-PRODUCER-RECORDING-BUFFER";
            if (role != Role::Latch && role != Role::FaultReadback && role != Role::Parameters
                && !buffer->sharedOwnerMatches(buffer))
                return "RHI-CANONICAL-PRODUCER-RECORDING-BUFFER-OWNER";
            for (std::size_t j = 0u; j < i; ++j)
                if (in.buffers[j]->id() == buffer->id())
                    return "RHI-CANONICAL-PRODUCER-RECORDING-ALIAS";
            for (const auto* protectedBuffer : {in.packet->physicalArgs(),
                     in.packet->instanceMappings(),
                     in.consumerFault->faultLatchBuffer(),
                     in.consumerFault->faultReadbackBuffer()})
                if (buffer->id() == protectedBuffer->id())
                    return "RHI-CANONICAL-PRODUCER-RECORDING-ALIAS";
            for (const auto& image : in.images)
                if (image.view == buffer->id() || image.parent == buffer->id())
                    return "RHI-CANONICAL-PRODUCER-RECORDING-ALIAS";
            if (role == Role::Source) {
                if (const auto* id = in.source->validateSourceStorage(*buffer))
                    return id;
                if (buffer->desc().memory != MemoryUsage::CpuToGpu
                    || buffer->desc().initialState != ResourceState::ShaderRead)
                    return "RHI-CANONICAL-PRODUCER-RECORDING-PROTECTED-STORAGE";
            } else if (role == Role::Metadata || role == Role::Arguments || role == Role::Mapping) {
                const bool metadata = role == Role::Metadata;
                const auto usage = metadata ? ResourceUsage::StorageBuffer
                                            : ResourceUsage::StorageBuffer | ResourceUsage::CopySrc
                        | ResourceUsage::CopyDst;
                const auto bytes = metadata   ? sizeof(Cold::Bytes)
                    : role == Role::Arguments ? sizeof(DrawIndexedIndirectCommand)
                                              : sizeof(IndirectInstanceBase);
                if (buffer->desc().sizeBytes != bytes || buffer->desc().usage != usage
                    || buffer->desc().memory != MemoryUsage::CpuToGpu
                    || buffer->desc().initialState
                        != (metadata ? ResourceState::ShaderRead : ResourceState::ShaderWrite)
                    || buffer->state() != (metadata ? ResourceState::ShaderRead : outputState)
                    || !buffer->hostWritesSealed() || !buffer->gpuWritesSealed()
                    || !buffer->gpuTransferWritesSealed())
                    return "RHI-CANONICAL-PRODUCER-RECORDING-PROTECTED-STORAGE";
            } else if (role == Role::ArgumentsReadback || role == Role::MappingReadback
                || role == Role::FaultReadback) {
                const auto bytes = role == Role::ArgumentsReadback
                    ? sizeof(DrawIndexedIndirectCommand)
                    : role == Role::MappingReadback ? sizeof(IndirectInstanceBase)
                                                    : sizeof(std::uint32_t);
                if (buffer->desc().sizeBytes != bytes
                    || buffer->desc().usage != ResourceUsage::CopyDst
                    || buffer->desc().memory != MemoryUsage::GpuToCpu
                    || buffer->desc().initialState != ResourceState::CopyDst
                    || buffer->state() != ResourceState::CopyDst || !buffer->hostWritesSealed())
                    return "RHI-CANONICAL-PRODUCER-RECORDING-READBACK";
                const auto reported = buffer->allocation().estimatedSizeBytes;
                if (reported < bytes
                    || reported > budget.maxReadbackBytes - facts.actualReadbackBytes)
                    return "RHI-CANONICAL-PRODUCER-RECORDING-READBACK-BUDGET";
                facts.actualReadbackBytes += reported;
            }
            facts.identity.resources[i] = buffer->id();
        }
        Cold::Resources metadata{{{1u, at(Role::Source)->id(), at(Role::Source)->desc().sizeBytes},
            {2u, at(Role::Arguments)->id(), at(Role::Arguments)->desc().sizeBytes},
            {3u, at(Role::Mapping)->id(), at(Role::Mapping)->desc().sizeBytes},
            {4u, at(Role::Parameters)->id(), at(Role::Parameters)->desc().sizeBytes}}};
        if (const auto* id = Cold::buildMetadata(in.producerPackage->artifact(),
                in.producerFault->generation(),
                metadata,
                facts.metadata,
                facts.bytes))
            return id;
        facts.identity.contractVersion = version;
        facts.identity.producerPackage = producer.producerPackageIdentity;
        facts.identity.consumerPackage = consumer.consumerPackageIdentity;
        facts.identity.packetGeneration = in.packet->generation();
        facts.identity.producerGeneration = in.producerFault->generation();
        facts.identity.consumerGeneration = in.consumerFault->generation();
        facts.identity.recording = recording;
        facts.identity.protectedResources = {in.packet->physicalArgs()->id(),
            in.packet->instanceMappings()->id(),
            in.consumerFault->faultLatchBuffer()->id(),
            in.consumerFault->faultReadbackBuffer()->id()};
        if (!facts.identity.valid())
            return "RHI-CANONICAL-PRODUCER-RECORDING-IDENTITY";
        output = facts;
        return nullptr;
    }
};

// Fixed original shared-reference capture; it owns no native or execution token.
class CanonicalZeroBaseIndirectProducerRecordingAdmission final {
public:
    using Contract = CanonicalZeroBaseIndirectProducerRecordingContract;

    static const char* capture(const Contract::Inputs& inputs,
        Contract::Budget budget,
        std::uint64_t recording,
        CanonicalZeroBaseIndirectProducerRecordingAdmission& output) noexcept
    {
        output = {};
        Contract::Facts facts;
        if (const auto* id = Contract::verify(inputs, budget, recording, facts))
            return id;
        output.inputs_ = inputs;
        output.budget_ = budget;
        output.facts_ = facts;
        return nullptr;
    }

    const char* validate(const Contract::Inputs& inputs,
        std::uint64_t recording,
        Contract::StoragePhase phase = Contract::StoragePhase::Cold) const noexcept
    {
        return validateStorage(inputs, recording, phase, false);
    }

    const char* validateClosedReadbackReady(
        const Contract::Inputs& inputs, std::uint64_t recording) const noexcept
    {
        return validateStorage(inputs, recording, Contract::StoragePhase::ReadbackReady, true);
    }

private:
    const char* validateStorage(const Contract::Inputs& inputs,
        std::uint64_t recording,
        Contract::StoragePhase phase,
        bool closedReady) const noexcept
    {
        if (!facts_.identity.valid())
            return "RHI-CANONICAL-PRODUCER-RECORDING-IDENTITY";
        const auto same = [](const auto& a, const auto& b) {
            return core::SharedOwnerProjection::sameOwner(a, b);
        };
        if (!same(inputs.producerPackage, inputs_.producerPackage)
            || !same(inputs.consumerPackage, inputs_.consumerPackage)
            || !same(inputs.source, inputs_.source) || !same(inputs.packet, inputs_.packet)
            || !same(inputs.producerFault, inputs_.producerFault)
            || !same(inputs.consumerFault, inputs_.consumerFault))
            return "RHI-CANONICAL-PRODUCER-RECORDING-ORIGINAL-OWNER";
        for (std::size_t i = 0u; i < inputs.buffers.size(); ++i)
            if (!same(inputs.buffers[i], inputs_.buffers[i]))
                return "RHI-CANONICAL-PRODUCER-RECORDING-ORIGINAL-OWNER";
        for (std::size_t i = 0u; i < inputs.images.size(); ++i)
            if (!inputs.images[i].matches(inputs_.images[i]))
                return "RHI-CANONICAL-PRODUCER-RECORDING-IMAGE";
        Contract::Facts facts;
        const auto* id = closedReady
            ? Contract::verifyClosedReadbackReady(inputs, budget_, recording, facts)
            : Contract::verify(inputs, budget_, recording, facts, phase);
        if (id)
            return id;
        return facts.identity == facts_.identity && facts.metadata == facts_.metadata
                && facts.bytes == facts_.bytes
                && facts.actualReadbackBytes == facts_.actualReadbackBytes
            ? nullptr
            : "RHI-CANONICAL-PRODUCER-RECORDING-IDENTITY";
    }

public:
    template <class Visitor> void projectReferences(Visitor&& visit) const
    {
        visit(inputs_.producerPackage);
        visit(inputs_.consumerPackage);
        visit(inputs_.source);
        visit(inputs_.packet);
        visit(inputs_.producerFault);
        visit(inputs_.consumerFault);
        for (const auto& buffer : inputs_.buffers)
            visit(buffer);
    }

    const auto& facts() const noexcept { return facts_; }

    const auto& originalInputs() const noexcept { return inputs_; }

private:
    Contract::Inputs inputs_;
    Contract::Budget budget_;
    Contract::Facts facts_;
};

// Strict correlated-output composition. Failed commands leave every phase intact.
class CanonicalZeroBaseIndirectProducerSequence final {
public:
    using Contract = CanonicalZeroBaseIndirectProducerRecordingContract;
    using Command = Contract::Command;
    using Identity = Contract::Identity;
    using Status = GpuIndirectOutputLifetimeContract::Status;

    CanonicalZeroBaseIndirectProducerSequence(Contract::Budget budget, Identity identity) noexcept
        : budget_(budget), identity_(identity)
    {}

    const char* record(Command command, const Identity& identity) noexcept
    {
        auto next = *this;
        if (const auto* id = next.apply(command, identity))
            return id;
        *this = next;
        return nullptr;
    }

    const char* finish() const noexcept
    {
        if (const auto* id = budget_.validate())
            return id;
        if (!identity_.valid())
            return "RHI-CANONICAL-PRODUCER-RECORDING-IDENTITY";
        if (commands_ != Contract::protocol.size())
            return "RHI-CANONICAL-PRODUCER-SEQUENCE-INCOMPLETE";
        if (const auto* id = clear_.finish())
            return id;
        for (const auto& output : outputs_)
            if (const auto* id = output.finish())
                return id;
        return nullptr;
    }

    const char* transition(
        Status after, const Identity& identity, bool nativeCompletionProven = false) noexcept
    {
        if (!(identity == identity_) || !identity.valid())
            return "RHI-CANONICAL-PRODUCER-RECORDING-IDENTITY";
        if (status_ == Status::Cold && (after == Status::Failed || after == Status::Abandoned)) {
            status_ = after;
            return nullptr;
        }
        if (after == Status::Recorded)
            if (const auto* id = finish())
                return id;
        if (const auto* id = GpuIndirectOutputLifetimeContract::validateTransition(
                status_, after, nativeCompletionProven))
            return id;
        status_ = after;
        return nullptr;
    }

    std::uint32_t commands() const noexcept { return commands_; }

    Status status() const noexcept { return status_; }

private:
    const char* apply(Command command, const Identity& identity) noexcept
    {
        if (const auto* id = budget_.validate())
            return id;
        if (!(identity == identity_) || !identity.valid())
            return "RHI-CANONICAL-PRODUCER-RECORDING-IDENTITY";
        if (status_ != Status::Cold || commands_ >= Contract::protocol.size()
            || commands_ >= budget_.maxCommands || command != Contract::protocol[commands_])
            return "RHI-CANONICAL-PRODUCER-SEQUENCE-ORDER";
        const auto latch = identity_.resources[static_cast<std::size_t>(Contract::Role::Latch)];
        const std::array<GpuResourceId, 3u> ids{
            identity_.resources[static_cast<std::size_t>(Contract::Role::Arguments)],
            identity_.resources[static_cast<std::size_t>(Contract::Role::Mapping)],
            latch};
        const char* diagnostic = nullptr;
        switch (command) {
        case Command::ClearBegin:
            diagnostic = clear_.begin(latch);
            break;
        case Command::ClearZero:
            diagnostic = clear_.draw(latch);
            break;
        case Command::ClearEnd:
            diagnostic = clear_.end(latch);
            break;
        case Command::ClearDependency:
            if ((diagnostic = clear_.depend(latch)))
                return diagnostic;
            diagnostic = clear_.consume(latch);
            break;
        case Command::ComputeBegin:
        case Command::Dispatch:
        case Command::ComputeEnd:
            for (std::size_t i = 0u; i < outputs_.size(); ++i) {
                diagnostic = command == Command::ComputeBegin ? outputs_[i].begin(ids[i])
                    : command == Command::Dispatch            ? outputs_[i].draw(ids[i])
                                                              : outputs_[i].end(ids[i]);
                if (diagnostic)
                    return diagnostic;
            }
            break;
        case Command::ArgumentsDependency:
            diagnostic = outputs_[0].depend(ids[0]);
            break;
        case Command::MappingDependency:
            diagnostic = outputs_[1].depend(ids[1]);
            break;
        case Command::FaultDependency:
            diagnostic = outputs_[2].depend(ids[2]);
            break;
        case Command::ArgumentsCopy:
            diagnostic = outputs_[0].consume(ids[0]);
            break;
        case Command::MappingCopy:
            diagnostic = outputs_[1].consume(ids[1]);
            break;
        case Command::FaultCopy:
            diagnostic = outputs_[2].consume(ids[2]);
            break;
        case Command::ReadbackBegin:
        case Command::ReadbackEnd:
            break;
        }
        if (!diagnostic)
            ++commands_;
        return diagnostic;
    }

    Contract::Budget budget_;
    Identity identity_;
    GraphicsStorageWriteSequence clear_;
    std::array<GraphicsStorageWriteSequence, 3u> outputs_;
    std::uint32_t commands_ = 0u;
    Status status_ = Status::Cold;
};

// endregion canonical_zero_base_indirect_recording

// region canonical_closed_consumer_recording

// Original structural values only. No native revision, draw or completion ticket.
class CanonicalClosedConsumerRecordingContract final {
public:
    static constexpr std::uint32_t version = 1u;
    using Producer = CanonicalZeroBaseIndirectProducerRecordingContract;
    using Consumer = CanonicalZeroBaseIndirectConsumerColdBindingContract;
    using StoragePhase = Consumer::StoragePhase;
    using Geometry = CanonicalZeroBaseIndirectConsumerGeometryContract;
    using Content = CanonicalConsumerImageContentContract;
    using Contents = std::array<std::shared_ptr<const Content>, Consumer::Consumer::capacity>;
    using Revisions = std::array<Content::Revision, Consumer::Consumer::capacity>;

    struct Inputs {
        Producer::Inputs producer;
        std::shared_ptr<const Geometry> geometry;
        std::shared_ptr<const VertexInputConsumptionContract> vertex;
        std::shared_ptr<const GpuBuffer> geometryBuffer, metadata;
        Contents contents;
        Revisions revisions;
    };

    struct Identity {
        Producer::Identity producer;
        GpuResourceId geometry, metadata;
        Revisions revisions;

        bool operator==(const Identity& b) const noexcept
        {
            return producer == b.producer && geometry == b.geometry && metadata == b.metadata
                && revisions == b.revisions;
        }

        bool valid() const noexcept
        {
            if (!producer.valid() || !geometry.isValid() || !metadata.isValid()
                || geometry == metadata)
                return false;
            for (const auto id : {geometry, metadata}) {
                for (const auto resource : producer.resources)
                    if (id == resource)
                        return false;
                for (const auto resource : producer.protectedResources)
                    if (id == resource)
                        return false;
            }
            for (const auto& revision : revisions) {
                if (!revision.valid() || revision.view == geometry || revision.parent == geometry
                    || revision.view == metadata || revision.parent == metadata)
                    return false;
                for (const auto resource : producer.resources)
                    if (revision.view == resource || revision.parent == resource)
                        return false;
                for (const auto resource : producer.protectedResources)
                    if (revision.view == resource || revision.parent == resource)
                        return false;
            }
            return true;
        }
    };
    enum class Command {
        ClearBegin,
        ClearZero,
        ClearEnd,
        ClearDependency,
        ComputeBegin,
        Dispatch,
        ComputeEnd,
        ArgumentsDependency,
        MappingDependency,
        FaultDependency,
        PacketArgumentsPrepare,
        PacketMappingPrepare,
        PacketCopyBegin,
        PacketArgumentsCopy,
        PacketMappingCopy,
        PacketCopyEnd,
        PacketArgumentsDependency,
        PacketMappingDependency,
        ConsumerClearBegin,
        ConsumerClearZero,
        ConsumerClearEnd,
        ConsumerClearDependency,
        RenderBegin,
        Pipeline,
        Descriptors,
        Vertex,
        Index,
        Viewport,
        Scissor,
        Indirect,
        RenderEnd,
        ConsumerFaultDependency,
        ReadbackBegin,
        ArgumentsCopy,
        MappingCopy,
        ProducerFaultCopy,
        ConsumerFaultCopy,
        ReadbackEnd
    };
    static constexpr std::array<Command, 38u> protocol{Command::ClearBegin,
        Command::ClearZero,
        Command::ClearEnd,
        Command::ClearDependency,
        Command::ComputeBegin,
        Command::Dispatch,
        Command::ComputeEnd,
        Command::ArgumentsDependency,
        Command::MappingDependency,
        Command::FaultDependency,
        Command::PacketArgumentsPrepare,
        Command::PacketMappingPrepare,
        Command::PacketCopyBegin,
        Command::PacketArgumentsCopy,
        Command::PacketMappingCopy,
        Command::PacketCopyEnd,
        Command::PacketArgumentsDependency,
        Command::PacketMappingDependency,
        Command::ConsumerClearBegin,
        Command::ConsumerClearZero,
        Command::ConsumerClearEnd,
        Command::ConsumerClearDependency,
        Command::RenderBegin,
        Command::Pipeline,
        Command::Descriptors,
        Command::Vertex,
        Command::Index,
        Command::Viewport,
        Command::Scissor,
        Command::Indirect,
        Command::RenderEnd,
        Command::ConsumerFaultDependency,
        Command::ReadbackBegin,
        Command::ArgumentsCopy,
        Command::MappingCopy,
        Command::ProducerFaultCopy,
        Command::ConsumerFaultCopy,
        Command::ReadbackEnd};
    static constexpr std::uint32_t commandUnits = static_cast<std::uint32_t>(protocol.size());
    static constexpr std::uint64_t readbackBytes = Producer::readbackBytes + sizeof(std::uint32_t);
    // c11 roots/buffers + geometry/layout/storage/metadata + snapshots + packet
    // destination and consumer fault alias references (not deduplicated owners).
    static constexpr std::size_t referenceSlots =
        Producer::referenceSlots + 4u + std::tuple_size<Contents>::value + 4u;

    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint32_t maxCommands = 0u;
        std::uint64_t maxReadbackBytes = 0u;
        std::size_t maxReferenceSlots = 0u;

        const char* validate() const noexcept
        {
            return contractVersion == version && prototypeOptIn && maxCommands >= commandUnits
                    && maxReadbackBytes >= readbackBytes && maxReferenceSlots >= referenceSlots
                ? nullptr
                : "RHI-CLOSED-CONSUMER-RECORDING-BUDGET";
        }

        Producer::Budget producerBudget() const noexcept
        {
            return {contractVersion,
                prototypeOptIn,
                Producer::commandUnits,
                maxReadbackBytes >= sizeof(std::uint32_t) ? maxReadbackBytes - sizeof(std::uint32_t)
                                                          : 0u,
                Producer::referenceSlots};
        }
    };

    struct Facts {
        Identity identity;
        Consumer::Words metadata{};
        Consumer::Bytes bytes{};
        std::uint64_t actualReadbackBytes = 0u;
    };

    using PrivateDisposition = Producer::PrivateDisposition;
    using PrivateLowering = Producer::PrivateLowering;

    // Explicit private Prototype selection. Shared facts still grant no authority;
    // the selected executor must prove originals and consume all38 operations.
    static PrivateLowering privateRecordingLowering(Backend backend, Budget budget) noexcept
    {
        if (const auto* id = budget.validate())
            return {version, PrivateDisposition::DiagnosticFailure, id};
        return semantics::BindlessPreparationLowering::
            CanonicalClosedConsumerRecording_privateRecordingLowering<PrivateLowering,
                PrivateDisposition>(backend, version);
    }

    struct Lowering {
        bool validatorOnly = false, executable = false;
        const char* diagnosticId = "RHI-CLOSED-CONSUMER-RECORDING-BACKEND-UNKNOWN";
    };

    static constexpr Lowering lowering(Backend backend) noexcept
    {
        return semantics::BindlessPreparationLowering::CanonicalClosedConsumerRecording_lowering<
            Lowering>(backend);
    }

    static const char* verify(const Inputs& in,
        Budget budget,
        std::uint64_t recording,
        Facts& output,
        StoragePhase phase = StoragePhase::Cold) noexcept
    {
        output = {};
        if (const auto* id = budget.validate())
            return id;
        if (phase != StoragePhase::Cold && phase != StoragePhase::ReadbackReady)
            return "RHI-CLOSED-CONSUMER-RECORDING-STORAGE-PHASE";
        if (!in.producer.packet)
            return "RHI-CLOSED-CONSUMER-RECORDING-ROOT";
        // Never promote a previously issued private storage-only generation.
        if (const auto* id =
                BindlessIndirectPacketStorageContract::closedConsumerStructuralDiagnostic(
                    in.producer.packet->issuance()))
            return id;
        Producer::Facts producer;
        const auto* producerId = phase == StoragePhase::Cold
            ? Producer::verify(in.producer, budget.producerBudget(), recording, producer)
            : Producer::verifyClosedReadbackReady(
                  in.producer, budget.producerBudget(), recording, producer);
        if (producerId)
            return producerId;
        if (in.producer.packet->physicalArgs()->state()
            != (phase == StoragePhase::Cold ? ResourceState::ShaderWrite
                                            : ResourceState::ShaderRead))
            return "RHI-CLOSED-CONSUMER-PACKET-COLD-STATE";
        if (!in.geometry || !in.vertex || !in.geometryBuffer || !in.metadata)
            return "RHI-CLOSED-CONSUMER-RECORDING-ROOT";
        if (!in.geometry->sharedOwnerMatches(in.geometry)
            || !in.vertex->sharedOwnerMatches(in.vertex))
            return "RHI-CLOSED-CONSUMER-RECORDING-VALUE-OWNER";
        if (const auto* id = in.geometry->validate(
                *in.producer.consumerPackage, *in.producer.source, *in.vertex))
            return id;
        if (!in.geometryBuffer->sharedOwnerMatches(in.geometryBuffer)
            || !in.metadata->sharedOwnerMatches(in.metadata))
            return "RHI-CLOSED-CONSUMER-RECORDING-BUFFER-OWNER";
        if (const auto* id = in.geometry->validateStorage(*in.geometryBuffer))
            return id;
        const auto& desc = in.metadata->desc();
        if (!in.metadata->id().isValid() || desc.sizeBytes != sizeof(Consumer::Bytes)
            || desc.usage != ResourceUsage::StorageBuffer || desc.memory != MemoryUsage::CpuToGpu
            || desc.initialState != ResourceState::ShaderRead
            || in.metadata->state() != ResourceState::ShaderRead || !in.metadata->hostWritesSealed()
            || !in.metadata->gpuWritesSealed() || !in.metadata->gpuTransferWritesSealed())
            return "RHI-CLOSED-CONSUMER-RECORDING-METADATA";
        for (std::size_t i = 0u; i < in.contents.size(); ++i) {
            if (!in.contents[i] || !in.contents[i]->sharedOwnerMatches(in.contents[i]))
                return "RHI-CLOSED-CONSUMER-IMAGE-CONTENT-OWNER";
            if (const auto* id = in.contents[i]->validate(in.producer.images[i], in.revisions[i]))
                return id;
            for (std::size_t j = 0u; j < i; ++j) {
                const auto& a = in.producer.images[i];
                const auto& b = in.producer.images[j];
                // Distinct view IDs may select exactly the same parent subresource.
                if (a.parent == b.parent && a.range == b.range
                    && (!sameSelectedContent(*in.contents[i], *in.contents[j])
                        || in.revisions[i].revision != in.revisions[j].revision))
                    return "RHI-CLOSED-CONSUMER-IMAGE-ALIASED-REVISION";
            }
        }
        Consumer::Facts consumer;
        if (const auto* id = Consumer::verify(*in.producer.consumerPackage,
                *in.producer.source,
                *in.producer.packet,
                *in.producer.packet->instanceMappings(),
                in.producer.images,
                in.producer.producerFault,
                in.producer.consumerFault,
                consumer,
                phase))
            return id;
        Facts facts;
        facts.identity = {
            producer.identity, in.geometryBuffer->id(), in.metadata->id(), in.revisions};
        if (!facts.identity.valid())
            return "RHI-CLOSED-CONSUMER-RECORDING-IDENTITY";
        const auto bytes =
            in.producer.consumerFault->faultReadbackBuffer()->allocation().estimatedSizeBytes;
        if (bytes < sizeof(std::uint32_t)
            || bytes > budget.maxReadbackBytes - producer.actualReadbackBytes)
            return "RHI-CLOSED-CONSUMER-RECORDING-READBACK-BUDGET";
        facts.actualReadbackBytes = producer.actualReadbackBytes + bytes;
        facts.metadata = consumer.words;
        facts.bytes = consumer.bytes;
        output = facts;
        return nullptr;
    }

private:
    static bool sameSelectedContent(const Content& a, const Content& b) noexcept
    {
        return a.image().sameParent(b.image()) && a.image().range == b.image().range
            && a.image().format == b.image().format && a.bytesPerRow() == b.bytesPerRow()
            && a.rowsPerImage() == b.rowsPerImage() && a.bytes() == b.bytes();
    }
};

class CanonicalClosedConsumerRecordingAdmission final {
public:
    using Contract = CanonicalClosedConsumerRecordingContract;

    static const char* capture(const Contract::Inputs& inputs,
        Contract::Budget budget,
        std::uint64_t recording,
        CanonicalClosedConsumerRecordingAdmission& output) noexcept
    {
        output = {};
        Contract::Facts facts;
        if (const auto* id = Contract::verify(inputs, budget, recording, facts))
            return id;
        if (const auto* id = CanonicalZeroBaseIndirectProducerRecordingAdmission::capture(
                inputs.producer, budget.producerBudget(), recording, output.producer_))
            return id;
        output.inputs_ = inputs;
        output.inputs_.producer = {}; // c11 references are retained once by producer_.
        output.budget_ = budget;
        output.facts_ = facts;
        return nullptr;
    }

    const char* validate(const Contract::Inputs& inputs,
        std::uint64_t recording,
        Contract::StoragePhase phase = Contract::StoragePhase::Cold) const noexcept
    {
        if (!facts_.identity.valid())
            return "RHI-CLOSED-CONSUMER-RECORDING-IDENTITY";
        if (phase != Contract::StoragePhase::Cold && phase != Contract::StoragePhase::ReadbackReady)
            return "RHI-CLOSED-CONSUMER-RECORDING-STORAGE-PHASE";
        const auto* producerId = phase == Contract::StoragePhase::Cold
            ? producer_.validate(inputs.producer, recording)
            : producer_.validateClosedReadbackReady(inputs.producer, recording);
        if (producerId)
            return producerId;
        const auto same = [](const auto& a, const auto& b) {
            return core::SharedOwnerProjection::sameOwner(a, b);
        };
        if (!same(inputs.geometry, inputs_.geometry) || !same(inputs.vertex, inputs_.vertex)
            || !same(inputs.geometryBuffer, inputs_.geometryBuffer)
            || !same(inputs.metadata, inputs_.metadata))
            return "RHI-CLOSED-CONSUMER-RECORDING-ORIGINAL-OWNER";
        for (std::size_t i = 0u; i < inputs.contents.size(); ++i)
            if (!same(inputs.contents[i], inputs_.contents[i]))
                return "RHI-CLOSED-CONSUMER-RECORDING-ORIGINAL-OWNER";
        Contract::Facts current;
        if (const auto* id = Contract::verify(inputs, budget_, recording, current, phase))
            return id;
        return current.identity == facts_.identity && current.metadata == facts_.metadata
                && current.bytes == facts_.bytes
                && current.actualReadbackBytes == facts_.actualReadbackBytes
            ? nullptr
            : "RHI-CLOSED-CONSUMER-RECORDING-IDENTITY";
    }

    template <class Visitor> void projectReferences(Visitor&& visit) const
    {
        if (!facts_.identity.valid())
            return;
        producer_.projectReferences(visit);
        visit(inputs_.geometry);
        visit(inputs_.vertex);
        visit(inputs_.geometryBuffer);
        visit(inputs_.metadata);
        for (const auto& content : inputs_.contents)
            visit(content);
        const auto& original = producer_.originalInputs();
        visit(std::shared_ptr<const GpuBuffer>(original.packet, original.packet->physicalArgs()));
        visit(
            std::shared_ptr<const GpuBuffer>(original.packet, original.packet->instanceMappings()));
        visit(std::shared_ptr<const GpuBuffer>(
            original.consumerFault, original.consumerFault->faultLatchBuffer()));
        visit(std::shared_ptr<const GpuBuffer>(
            original.consumerFault, original.consumerFault->faultReadbackBuffer()));
    }

    const auto& facts() const noexcept { return facts_; }

private:
    CanonicalZeroBaseIndirectProducerRecordingAdmission producer_;
    Contract::Inputs inputs_;
    Contract::Budget budget_;
    Contract::Facts facts_;
};

// Reuses c11 producer and storage dependency state machines. Failed commands
// are transactional; terminal values cannot release actual backend leases.
class CanonicalClosedConsumerSequence final {
public:
    using Contract = CanonicalClosedConsumerRecordingContract;
    using Command = Contract::Command;
    using Identity = Contract::Identity;
    using Status = GpuIndirectOutputLifetimeContract::Status;

    CanonicalClosedConsumerSequence(Contract::Budget budget, Identity identity) noexcept
        : budget_(budget), identity_(identity),
          producer_(budget.producerBudget(), identity.producer)
    {}

    const char* record(Command command, const Identity& identity) noexcept
    {
        auto next = *this;
        if (const auto* id = next.apply(command, identity))
            return id;
        *this = next;
        return nullptr;
    }

    const char* finish() const noexcept
    {
        if (const auto* id = budget_.validate())
            return id;
        if (!identity_.valid())
            return "RHI-CLOSED-CONSUMER-RECORDING-IDENTITY";
        if (commands_ != Contract::commandUnits)
            return "RHI-CLOSED-CONSUMER-SEQUENCE-INCOMPLETE";
        if (const auto* id = producer_.finish())
            return id;
        if (const auto* id = clear_.finish())
            return id;
        if (const auto* id = fault_.finish())
            return id;
        for (const auto& packet : packet_)
            if (const auto* id = packet.finish())
                return id;
        return nullptr;
    }

    const char* transition(
        Status after, const Identity& identity, bool nativeCompletionProven = false) noexcept
    {
        if (!identity.valid() || !(identity == identity_))
            return "RHI-CLOSED-CONSUMER-RECORDING-IDENTITY";
        if (status_ == Status::Cold && (after == Status::Failed || after == Status::Abandoned)) {
            status_ = after;
            return nullptr;
        }
        if (after == Status::Recorded)
            if (const auto* id = finish())
                return id;
        if (const auto* id = GpuIndirectOutputLifetimeContract::validateTransition(
                status_, after, nativeCompletionProven))
            return id;
        status_ = after;
        return nullptr;
    }

    std::uint32_t commands() const noexcept { return commands_; }

    Status status() const noexcept { return status_; }

private:
    const char* apply(Command command, const Identity& identity) noexcept
    {
        if (const auto* id = budget_.validate())
            return id;
        if (!identity.valid() || !(identity == identity_))
            return "RHI-CLOSED-CONSUMER-RECORDING-IDENTITY";
        if (status_ != Status::Cold || commands_ >= Contract::commandUnits
            || command != Contract::protocol[commands_])
            return "RHI-CLOSED-CONSUMER-SEQUENCE-ORDER";
        using P = Contract::Producer::Command;
        // The c11 prefix has ten commands; its final readbacks stay deferred.
        if (commands_ < static_cast<std::size_t>(P::ReadbackBegin)) {
            if (const auto* id =
                    producer_.record(Contract::Producer::protocol[commands_], identity.producer))
                return id;
        } else {
            const auto latch = identity.producer.protectedResources[2u];
            const auto args = identity.producer.protectedResources[0u],
                       map = identity.producer.protectedResources[1u];
            const char* id = nullptr;
            switch (command) {
            case Command::PacketCopyBegin:
                id = packet_[0].begin(args);
                if (!id)
                    id = packet_[1].begin(map);
                break;
            case Command::PacketArgumentsCopy:
                id = packet_[0].draw(args);
                break;
            case Command::PacketMappingCopy:
                id = packet_[1].draw(map);
                break;
            case Command::PacketCopyEnd:
                id = packet_[0].end(args);
                if (!id)
                    id = packet_[1].end(map);
                break;
            case Command::PacketArgumentsDependency:
                id = packet_[0].depend(args);
                break;
            case Command::PacketMappingDependency:
                id = packet_[1].depend(map);
                break;
            case Command::ConsumerClearBegin:
                id = clear_.begin(latch);
                break;
            case Command::ConsumerClearZero:
                id = clear_.draw(latch);
                break;
            case Command::ConsumerClearEnd:
                id = clear_.end(latch);
                break;
            case Command::ConsumerClearDependency:
                id = clear_.depend(latch);
                if (!id)
                    id = clear_.consume(latch);
                break;
            case Command::RenderBegin:
                id = packet_[0].beginConsumer(args);
                if (!id)
                    id = packet_[1].beginConsumer(map);
                if (!id)
                    id = fault_.begin(latch);
                break;
            case Command::Indirect:
                id = packet_[0].drawConsumer(args);
                if (!id)
                    id = packet_[1].drawConsumer(map);
                if (!id)
                    id = fault_.draw(latch);
                break;
            case Command::RenderEnd:
                id = packet_[0].endConsumer(args);
                if (!id)
                    id = packet_[1].endConsumer(map);
                if (!id)
                    id = fault_.end(latch);
                break;
            case Command::ConsumerFaultDependency:
                id = fault_.depend(latch);
                break;
            case Command::ReadbackBegin:
                id = producer_.record(P::ReadbackBegin, identity.producer);
                break;
            case Command::ArgumentsCopy:
                id = producer_.record(P::ArgumentsCopy, identity.producer);
                break;
            case Command::MappingCopy:
                id = producer_.record(P::MappingCopy, identity.producer);
                break;
            case Command::ProducerFaultCopy:
                id = producer_.record(P::FaultCopy, identity.producer);
                break;
            case Command::ConsumerFaultCopy:
                id = fault_.consume(latch);
                break;
            case Command::ReadbackEnd:
                id = producer_.record(P::ReadbackEnd, identity.producer);
                break;
            default:
                break; // Fixed protocol admits only the required state commands.
            }
            if (id)
                return id;
        }
        ++commands_;
        return nullptr;
    }

    Contract::Budget budget_;
    Identity identity_;
    CanonicalZeroBaseIndirectProducerSequence producer_;
    std::array<GraphicsStorageWriteSequence, 2u> packet_;
    GraphicsStorageWriteSequence clear_, fault_;
    std::uint32_t commands_ = 0u;
    Status status_ = Status::Cold;
};

class CanonicalClosedConsumerOutcome final {
public:
    enum class Value {
        Unclassified,
        CleanProduced,
        ProducerSuppressed,
        ConsumerFault,
        ProducerAndConsumerFault
    };

    struct Readbacks {
        DrawIndexedIndirectCommand arguments;
        IndirectInstanceBase mapping;
        std::uint32_t producerFault = 0u, consumerFault = 0u;
    };

    // Numeric assertions only. Both original native readbacks and completion
    // are required separately; no lease release or clean-frame ticket here.
    static const char* classify(const ZeroBaseIndirectSourceConsumptionContract& source,
        const Readbacks& values,
        Value& output) noexcept
    {
        output = Value::Unclassified;
        ZeroBaseIndirectSourceConsumptionContract::Facts expected;
        if (const auto* id = source.observe(expected))
            return id;
        const bool suppressed =
            expected.outcome == ZeroBaseIndirectSourceConsumptionContract::Outcome::FaultSuppressed;
        if (values.producerFault > 1u || values.consumerFault > 1u
            || (values.producerFault != 0u) != suppressed
            || IndexedIndirectCommandSchema::words(values.arguments)
                != IndexedIndirectCommandSchema::words(expected.physical)
            || values.mapping.firstInstance != expected.mapping.firstInstance
            || values.mapping.instanceCount != expected.mapping.instanceCount
            || values.mapping.instanceCapacity != expected.mapping.instanceCapacity
            || values.mapping.contractVersion != expected.mapping.contractVersion
            || (values.producerFault
                && (values.arguments.instanceCount || values.mapping.firstInstance
                    || values.mapping.instanceCount || values.mapping.instanceCapacity
                    || values.mapping.contractVersion)))
            return "RHI-CLOSED-CONSUMER-OUTCOME-CORRESPONDENCE";
        output = values.producerFault
            ? (values.consumerFault ? Value::ProducerAndConsumerFault : Value::ProducerSuppressed)
            : (values.consumerFault ? Value::ConsumerFault : Value::CleanProduced);
        return nullptr;
    }
};
} // namespace arp::rhi

// endregion canonical_closed_consumer_recording

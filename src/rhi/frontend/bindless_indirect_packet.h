#pragma once
#include "rhi/frontend/bindless_image_and_indirect_validation.h"
#include "rhi/frontend/bindless_shader_package.h"
#include "rhi/frontend/bindless_snapshot.h"
#include "rhi/frontend/command_indirect_commands.h"
#include "rhi/gpu_bindless.h"

#include <array>

namespace arp::rhi_metal {
class MetalRhiZeroBaseIndirectPacketStorage;
class MetalRhiClosedConsumerIssuance;
} // namespace arp::rhi_metal

namespace arp::rhi_metal {
class MetalRhiClosedConsumerRecordingReservation;
}

namespace arp::rhi {

class BindlessIndirectProcessorIntent;

struct BindlessIndirectPacketBudget {
    std::uint32_t maxPackets = 0;
    std::uint64_t maxBytesPerPacket = 0;
    std::uint64_t maxResidentBytes = 0;
};

// Protected storage is not a zero-base draw ticket. A backend must separately
// prove trusted production, recording correspondence and completion retention.
class ARP_API BindlessIndirectPacket final : public core::SharedObjectIdentity {
    class ConstructionKey {
        friend class BindlessIndirectPacketPool;

        ConstructionKey() {}

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };
    friend class BindlessIndirectPacketPool;
    friend class BindlessIndirectPacketRecordingLease;
    friend class arp::rhi_metal::MetalRhiClosedConsumerRecordingReservation;

public:
    explicit BindlessIndirectPacket(ConstructionKey) {}

    BindlessIndirectPacket(const BindlessIndirectPacket&) = delete;
    ValidationResult validate() const;
    ValidationResult validateStorage() const;
    const char* storageDiagnostic() const noexcept;
    using Issuance = BindlessIndirectPacketStorageContract::Issuance;

    Issuance issuance() const noexcept { return issuance_; }

    bool prototypeStorageOnly() const noexcept
    {
        return issuance_ == Issuance::PrototypeStorageOnly;
    }

    bool prototypeClosedConsumerCold() const noexcept
    {
        return issuance_ == Issuance::PrototypeClosedConsumerCold;
    }

    ValidationResult validateConsumer(const BindlessSnapshot&,
        const BindlessLoweringPlan&,
        std::uint64_t snapshotGeneration) const;
    std::array<BindlessResourceAccess, 4> producerAccesses(
        const GpuBuffer& logicalSource) const noexcept;
    ValidationResult validateProducerAccess(
        const GpuBuffer& logicalSource, const BindlessAccessDeclarations&) const;

    const BindlessLoweringPlan& consumerPlan() const noexcept { return consumerPlan_; }

    const ZeroBaseIndirectProducerParams& parameters() const noexcept { return parameters_; }

    std::uint64_t generation() const noexcept { return generation_; }

    std::uint64_t reportedBufferBytes() const noexcept { return reportedBufferBytes_; }

    // Borrowed graph resources. Their storage and budget leases are retained by
    // the packet, so importers must keep the packet alive through completion.
    GpuBuffer* physicalArgs() const noexcept { return physicalArgs_.get(); }

    GpuBuffer* instanceMappings() const noexcept { return instanceMappings_.get(); }

    GpuBuffer* producerParameters() const noexcept { return producerParameters_.get(); }

private:
    core::UniquePtr<core::AtomicCapacityLease> countLease_;
    core::UniquePtr<core::AtomicCapacityLease> byteLease_;
    core::UniquePtr<GpuBuffer> physicalArgs_;
    core::UniquePtr<GpuBuffer> instanceMappings_;
    core::UniquePtr<GpuBuffer> producerParameters_;
    core::LifetimeEpoch::Ticket lifetime_;
    BindlessLoweringPlan consumerPlan_;
    ZeroBaseIndirectProducerParams parameters_;
    std::uint64_t generation_ = 0;
    std::uint64_t reportedBufferBytes_ = 0;
    Issuance issuance_ = Issuance::PublicConsumer;
    mutable core::AtomicCapacityBudget recordings_{1u};
};

// Ownership only, not a draw ticket. A backend transfers this move-only lease
// into completion-owned command storage before accepting submission. Dropping
// an unsubmitted recording cancels it; CPU frame retirement cannot release a
// backend-owned submitted lease.
class ARP_API BindlessIndirectPacketRecordingLease final {
public:
    BindlessIndirectPacketRecordingLease() = default;
    BindlessIndirectPacketRecordingLease(const BindlessIndirectPacketRecordingLease&) = delete;
    BindlessIndirectPacketRecordingLease& operator=(
        const BindlessIndirectPacketRecordingLease&) = delete;
    BindlessIndirectPacketRecordingLease(BindlessIndirectPacketRecordingLease&&) noexcept;
    BindlessIndirectPacketRecordingLease& operator=(
        BindlessIndirectPacketRecordingLease&&) noexcept;

    ~BindlessIndirectPacketRecordingLease() { release(); }

    static BindlessIndirectPacketRecordingLease acquire(
        std::shared_ptr<const BindlessIndirectPacket>,
        std::shared_ptr<const BindlessSnapshot>,
        const BindlessLoweringPlan&,
        std::uint64_t snapshotGeneration,
        std::uint64_t recordingIdentity,
        core::pmr::String& diagnostic);

    bool valid() const noexcept { return static_cast<bool>(packet_); }

    ValidationResult validateForRecord(const BindlessIndirectPacket&,
        const BindlessSnapshot&,
        const BindlessLoweringPlan&,
        std::uint64_t snapshotGeneration,
        std::uint64_t recordingIdentity) const;

private:
    void release() noexcept;
    std::shared_ptr<const BindlessIndirectPacket> packet_;
    std::shared_ptr<const BindlessSnapshot> snapshot_;
    std::uint64_t recordingIdentity_ = 0;
};

// Candidate preparation only, outside recording. No growth or completion wait.
// GPU backends remain unavailable until they own protected processor payloads.
class ARP_API BindlessIndirectPacketPool final {
public:
    BindlessIndirectPacketPool(GpuDevice&, BindlessIndirectPacketBudget);
    BindlessIndirectPacketPool(const BindlessIndirectPacketPool&) = delete;
    core::UniquePtr<BindlessIndirectPacket> prepare(const BindlessRequirements&,
        const BindlessShaderArtifact& consumer,
        const ZeroBaseIndirectProducerParams&,
        core::pmr::String& diagnostic);

    std::shared_ptr<BindlessIndirectPacket> prepareShared(const BindlessRequirements& request,
        const BindlessShaderArtifact& consumer,
        const ZeroBaseIndirectProducerParams& parameters,
        core::pmr::String& diagnostic)
    {
        return core::shareUnique(prepare(request, consumer, parameters, diagnostic));
    }

    std::uint64_t livePackets() const noexcept { return counts_->used(); }

    std::uint64_t chargedBytes() const noexcept { return bytes_->used(); }

private:
    friend class arp::rhi_metal::MetalRhiZeroBaseIndirectPacketStorage;
    friend class arp::rhi_metal::MetalRhiClosedConsumerIssuance;
    // Sole storage algorithm; explicit purpose and plan must agree. Private
    // Prototype purposes never carry a successful public execution plan.
    core::UniquePtr<BindlessIndirectPacket> prepareStorageCandidate(
        const ZeroBaseIndirectProducerParams&,
        const BindlessCrossExtension&,
        BindlessIndirectPacket::Issuance,
        const BindlessLoweringPlan*,
        core::pmr::String&);
    const char* validateBudget() const noexcept;
    GpuDevice& device_;
    BindlessIndirectPacketBudget budget_;
    std::shared_ptr<core::AtomicCapacityBudget> counts_;
    std::shared_ptr<core::AtomicCapacityBudget> bytes_;
};

struct BindlessIndirectProcessorIntentDesc {
    bool optIn = false;
    std::shared_ptr<const BindlessShaderPackage> producer;
    std::shared_ptr<const GpuBuffer> logicalSource;
    std::shared_ptr<const BindlessIndirectPacket> packet;
    std::shared_ptr<const BindlessSnapshot> consumerSnapshot;
    std::shared_ptr<const BindlessAuthorization> consumerAuthorization;
    BindlessLoweringPlan consumerPlan;
    std::uint64_t consumerGeneration = 0;
};

struct BindlessIndirectProcessorIntentBudget {
    std::uint32_t maxIntents = 0;
    std::uint64_t maxBytesPerIntent = 0;
    std::uint64_t maxResidentBytes = 0;
};

struct BindlessIndirectProcessorBudget {
    std::uint32_t maxProcessors = 0;
    std::uint64_t maxBytesPerProcessor = 0;
    std::uint64_t maxResidentBytes = 0;
};

// Executable payloads are issued only by the opted-in device extension.
// Backend command consumers must reject application-defined substitutes.
class ARP_API BindlessIndirectProcessor {
public:
    virtual ~BindlessIndirectProcessor() = default;
    virtual std::uint64_t identity() const noexcept = 0;
    virtual const BindlessIndirectProcessorIntent& intent() const noexcept = 0;
    virtual ValidationResult validate() const = 0;
    virtual std::uint64_t retainedBytes() const noexcept = 0;
};

class ARP_API BindlessIndirectProcessorPool {
public:
    virtual ~BindlessIndirectProcessorPool() = default;
    virtual std::shared_ptr<const BindlessIndirectProcessor> prepare(
        std::shared_ptr<const BindlessIndirectProcessorIntent>, core::pmr::String& diagnostic) = 0;
    virtual std::uint64_t liveProcessors() const noexcept = 0;
    virtual std::uint64_t chargedBytes() const noexcept = 0;
};

ARP_API ValidationResult validateBindlessIndirectProcessorConsumer(
    const BindlessIndirectProcessor&, const BindlessSnapshot&, const BindlessAuthorization&);

// Queried on the primary command encoder, outside an active pass. The same
// encoder must subsequently draw the exact packet in its graphics consumer.
class ARP_API BindlessCommandEncoderExtension {
public:
    virtual ~BindlessCommandEncoderExtension() = default;
    virtual CommandValidationResult recordIndirectProcessor(
        std::shared_ptr<const BindlessIndirectProcessor>,
        const BindlessAccessDeclarations& producerAccess,
        const BindlessAccessDeclarations& consumerAccess) = 0;
};

// Immutable preparation, never a GPU execution ticket. Accounting conservatively
// charges all retained source/package/packet/snapshot bytes, even when shared.
class ARP_API BindlessIndirectProcessorIntent final : public core::SharedObjectIdentity {
    class ConstructionKey {
        friend class BindlessIndirectProcessorIntentPool;

        ConstructionKey() {}

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };
    friend class BindlessIndirectProcessorIntentPool;

public:
    explicit BindlessIndirectProcessorIntent(ConstructionKey) {}

    BindlessIndirectProcessorIntent(const BindlessIndirectProcessorIntent&) = delete;
    ValidationResult validate() const;
    ValidationResult validateForRecord(const BindlessAccessDeclarations& producerAccess,
        const BindlessAccessDeclarations& consumerAccess) const;

    const BindlessIndirectProcessorIntentDesc& desc() const noexcept { return desc_; }

    std::uint64_t retainedBytes() const noexcept { return retainedBytes_; }

private:
    core::UniquePtr<core::AtomicCapacityLease> countLease_;
    core::UniquePtr<core::AtomicCapacityLease> byteLease_;
    core::LifetimeEpoch::Ticket lifetime_;
    BindlessIndirectProcessorIntentDesc desc_;
    std::uint64_t retainedBytes_ = 0;
};

class ARP_API BindlessIndirectProcessorIntentPool final {
public:
    BindlessIndirectProcessorIntentPool(GpuDevice&, BindlessIndirectProcessorIntentBudget);
    BindlessIndirectProcessorIntentPool(const BindlessIndirectProcessorIntentPool&) = delete;
    std::shared_ptr<const BindlessIndirectProcessorIntent> prepare(
        BindlessIndirectProcessorIntentDesc, core::pmr::String& diagnostic);

    std::uint64_t liveIntents() const noexcept { return counts_->used(); }

    std::uint64_t chargedBytes() const noexcept { return bytes_->used(); }

private:
    GpuDevice& device_;
    BindlessIndirectProcessorIntentBudget budget_;
    std::shared_ptr<core::AtomicCapacityBudget> counts_;
    std::shared_ptr<core::AtomicCapacityBudget> bytes_;
};

// A backend consumes this lease into command-owned storage before execution.
// It retains the source and verified packages as well as the packet/snapshot.
// It is not permission to bypass protected writes or the producer execution gate.
class ARP_API BindlessIndirectProcessorRecordingLease final {
public:
    BindlessIndirectProcessorRecordingLease() = default;
    BindlessIndirectProcessorRecordingLease(
        const BindlessIndirectProcessorRecordingLease&) = delete;
    BindlessIndirectProcessorRecordingLease& operator=(
        const BindlessIndirectProcessorRecordingLease&) = delete;
    BindlessIndirectProcessorRecordingLease(
        BindlessIndirectProcessorRecordingLease&&) noexcept = default;
    BindlessIndirectProcessorRecordingLease& operator=(
        BindlessIndirectProcessorRecordingLease&&) noexcept = default;
    static BindlessIndirectProcessorRecordingLease acquire(
        std::shared_ptr<const BindlessIndirectProcessorIntent>,
        std::uint64_t recordingIdentity,
        const BindlessAccessDeclarations& producerAccess,
        const BindlessAccessDeclarations& consumerAccess,
        core::pmr::String& diagnostic);

    bool valid() const noexcept { return static_cast<bool>(intent_) && packetLease_.valid(); }

    ValidationResult validateForRecord(const BindlessIndirectProcessorIntent&,
        std::uint64_t recordingIdentity,
        const BindlessAccessDeclarations& producerAccess,
        const BindlessAccessDeclarations& consumerAccess) const;

private:
    std::shared_ptr<const BindlessIndirectProcessorIntent> intent_;
    BindlessIndirectPacketRecordingLease packetLease_;
};

} // namespace arp::rhi

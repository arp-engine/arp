#include "rhi/frontend/bindless_indirect_packet.h"

#include "core/math/unsigned_range.h"
#include "rhi/frontend/bindless_image_and_indirect_validation.h"
#include "rhi/gpu_extensions.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace arp::rhi {

core::UniquePtr<BindlessIndirectProcessorPool> BindlessCrossExtension::createIndirectProcessorPool(
    GpuDevice&, const BindlessIndirectProcessorBudget&, core::pmr::String& diagnostic) const
{
    diagnostic = "RHI-BINDLESS-INDIRECT-PROCESSOR-EXECUTION-UNAVAILABLE";
    return {};
}

ValidationResult validateBindlessIndirectProcessorConsumer(
    const BindlessIndirectProcessor& processor,
    const BindlessSnapshot& snapshot,
    const BindlessAuthorization& authorization)
{
    const auto live = processor.validate();
    if (!live.success)
        return live;
    const auto& intent = processor.intent();
    const auto valid = intent.validate();
    if (!valid.success)
        return valid;
    const auto& desc = intent.desc();
    if (!processor.identity() || desc.consumerSnapshot.get() != &snapshot
        || desc.consumerAuthorization.get() != &authorization)
        return {false, "RHI-BINDLESS-INDIRECT-PROCESSOR-CONSUMER-MISMATCH"};
    return desc.packet->validateConsumer(snapshot, authorization.plan(), desc.consumerGeneration);
}

ValidationResult BindlessIndirectPacket::validate() const
{
    const auto storage = validateStorage();
    if (!storage.success)
        return storage;
    if (const auto* id = BindlessIndirectPacketStorageContract::publicConsumerDiagnostic(issuance_))
        return {false, id};
    return {true, {}};
}

ValidationResult BindlessIndirectPacket::validateStorage() const
{
    if (const auto* id = storageDiagnostic())
        return {false, id};
    return {true, {}};
}

const char* BindlessIndirectPacket::storageDiagnostic() const noexcept
{
    if (!BindlessIndirectPacketStorageContract::knownIssuance(issuance_))
        return "RHI-BINDLESS-INDIRECT-PACKET-ISSUANCE-UNKNOWN";
    if (issuance_ != Issuance::PublicConsumer
        && (consumerPlan_.success || consumerPlan_.executable || consumerPlan_.identity
            || consumerPlan_.deviceIdentity || consumerPlan_.shaderPackageIdentity))
        return "RHI-BINDLESS-INDIRECT-PACKET-PRIVATE-PLAN-FORBIDDEN";
    if (!lifetime_.isCurrent() || !generation_
        || (issuance_ == Issuance::PublicConsumer && !consumerPlan_.success) || !physicalArgs_
        || !instanceMappings_ || !producerParameters_)
        return "RHI-BINDLESS-INDIRECT-PACKET-STALE";
    if (!countLease_ || !countLease_->charged() || !byteLease_ || !byteLease_->charged())
        return "RHI-BINDLESS-INDIRECT-PACKET-STORAGE-ACCOUNTING";
    BindlessIndirectPacketStorageContract::Facts storage;
    if (const auto* id = BindlessIndirectPacketStorageContract::verify(
            {physicalArgs_.get(), instanceMappings_.get(), producerParameters_.get()},
            parameters_,
            reportedBufferBytes_,
            byteLease_->units(),
            storage))
        return id;
    return nullptr;
}

ValidationResult BindlessIndirectPacket::validateConsumer(const BindlessSnapshot& snapshot,
    const BindlessLoweringPlan& selected,
    std::uint64_t snapshotGeneration) const
{
    const auto live = validate();
    if (!live.success)
        return live;
    if (!snapshotGeneration || snapshotGeneration != snapshot.generation()
        || !bindlessPlansMatch(selected, consumerPlan_)
        || !bindlessPlansMatch(selected, snapshot.plan()))
        return {false, "RHI-BINDLESS-INDIRECT-PACKET-PLAN-MISMATCH"};
    return snapshot.validateIndirectInstanceMapping(
        *instanceMappings_, parameters_.instanceCapacity);
}

std::array<BindlessResourceAccess, 4> BindlessIndirectPacket::producerAccesses(
    const GpuBuffer& logicalSource) const noexcept
{
    std::array<BindlessResourceAccess, 4> accesses{};
    const GpuBuffer* buffers[] = {
        &logicalSource, producerParameters_.get(), physicalArgs_.get(), instanceMappings_.get()};
    for (std::size_t i = 0; i < accesses.size(); ++i) {
        if (!buffers[i])
            continue;
        auto& access = accesses[i];
        access.resource = buffers[i]->id();
        access.kind = BindlessAccessKind::Buffer;
        access.bufferSize = buffers[i]->desc().sizeBytes;
        access.stages = i < 2u ? ShaderStage::Compute : ShaderStage::None;
        access.pipelineStages = i < 2u ? PipelineStage::ComputeShader : PipelineStage::Transfer;
        access.access = i < 2u ? ResourceAccess::ShaderRead : ResourceAccess::TransferWrite;
        access.state = i < 2u ? ResourceState::ShaderRead : ResourceState::CopyDst;
    }
    return accesses;
}

ValidationResult BindlessIndirectPacket::validateProducerAccess(
    const GpuBuffer& source, const BindlessAccessDeclarations& declared) const
{
    const auto live = validate();
    if (!live.success)
        return live;
    constexpr std::uint64_t wordBytes = sizeof(std::uint32_t);
    constexpr auto recordWords = sizeof(DrawIndexedIndirectCommand) / wordBytes;
    const auto sourceBytes = source.desc().sizeBytes;
    const auto sourceWords = sourceBytes / wordBytes;
    if (!source.id().isValid()
        || (source.desc().usage & ResourceUsage::StorageBuffer) == ResourceUsage::None
        || sourceBytes % wordBytes || sourceWords > std::numeric_limits<std::uint32_t>::max()
        || !core::containsUnsignedRange(std::uint64_t{0u},
            sourceWords,
            std::uint64_t{parameters_.sourceWordOffset},
            std::uint64_t{recordWords}))
        return {false, "RHI-BINDLESS-INDIRECT-PRODUCER-SOURCE-INVALID"};
    for (const auto& access : producerAccesses(source)) {
        const auto result = validateBindlessBufferAccess(access, declared);
        if (!result.success) {
            if (result.diagnostics == "RHI-BINDLESS-ACCESS-SET-INCOMPLETE")
                return {false, "RHI-BINDLESS-INDIRECT-PRODUCER-ACCESS-INCOMPLETE"};
            return result;
        }
    }
    return {true, {}};
}

BindlessIndirectPacketRecordingLease::BindlessIndirectPacketRecordingLease(
    BindlessIndirectPacketRecordingLease&& other) noexcept
    : packet_(std::move(other.packet_)), snapshot_(std::move(other.snapshot_)),
      recordingIdentity_(std::exchange(other.recordingIdentity_, 0u))
{}

BindlessIndirectPacketRecordingLease& BindlessIndirectPacketRecordingLease::operator=(
    BindlessIndirectPacketRecordingLease&& other) noexcept
{
    if (this != &other) {
        release();
        packet_ = std::move(other.packet_);
        snapshot_ = std::move(other.snapshot_);
        recordingIdentity_ = std::exchange(other.recordingIdentity_, 0u);
    }
    return *this;
}

BindlessIndirectPacketRecordingLease BindlessIndirectPacketRecordingLease::acquire(
    std::shared_ptr<const BindlessIndirectPacket> packet,
    std::shared_ptr<const BindlessSnapshot> snapshot,
    const BindlessLoweringPlan& selected,
    std::uint64_t snapshotGeneration,
    std::uint64_t recordingIdentity,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    if (!packet || !snapshot || !recordingIdentity) {
        diagnostic = "RHI-BINDLESS-INDIRECT-PACKET-RECORDING-INVALID";
        return {};
    }
    if (!packet->sharedOwnerMatches(packet) || !snapshot->sharedOwnerMatches(snapshot)) {
        diagnostic = "RHI-BINDLESS-INDIRECT-PACKET-OWNER-MISMATCH";
        return {};
    }
    const auto consumer = packet->validateConsumer(*snapshot, selected, snapshotGeneration);
    if (!consumer.success) {
        diagnostic = consumer.diagnostics;
        return {};
    }
    if (!packet->recordings_.tryAcquire(1u)) {
        diagnostic = "RHI-BINDLESS-INDIRECT-PACKET-IN-FLIGHT";
        return {};
    }
    BindlessIndirectPacketRecordingLease lease;
    lease.packet_ = std::move(packet);
    lease.snapshot_ = std::move(snapshot);
    lease.recordingIdentity_ = recordingIdentity;
    return lease;
}

ValidationResult BindlessIndirectPacketRecordingLease::validateForRecord(
    const BindlessIndirectPacket& packet,
    const BindlessSnapshot& snapshot,
    const BindlessLoweringPlan& selected,
    std::uint64_t snapshotGeneration,
    std::uint64_t recordingIdentity) const
{
    if (packet_.get() != &packet || snapshot_.get() != &snapshot || !recordingIdentity
        || recordingIdentity != recordingIdentity_)
        return {false, "RHI-BINDLESS-INDIRECT-PACKET-RECORDING-MISMATCH"};
    return packet_->validateConsumer(snapshot, selected, snapshotGeneration);
}

void BindlessIndirectPacketRecordingLease::release() noexcept
{
    if (packet_) {
        packet_->recordings_.release(1u);
        packet_.reset();
    }
    snapshot_.reset();
    recordingIdentity_ = 0u;
}

BindlessIndirectPacketPool::BindlessIndirectPacketPool(
    GpuDevice& device, BindlessIndirectPacketBudget budget)
    : device_(device), budget_(budget),
      counts_(core::makeShared<core::AtomicCapacityBudget>(budget.maxPackets)),
      bytes_(core::makeShared<core::AtomicCapacityBudget>(budget.maxResidentBytes))
{}

const char* BindlessIndirectPacketPool::validateBudget() const noexcept
{
    if (!budget_.maxPackets || !budget_.maxBytesPerPacket || !budget_.maxResidentBytes
        || budget_.maxBytesPerPacket > budget_.maxResidentBytes)
        return "RHI-BINDLESS-INDIRECT-PACKET-BUDGET";
    return budget_.maxBytesPerPacket < BindlessIndirectPacketStorageContract::minimumBytes
        ? "RHI-BINDLESS-INDIRECT-PACKET-FOOTPRINT-EXCEEDED"
        : nullptr;
}

core::UniquePtr<BindlessIndirectPacket> BindlessIndirectPacketPool::prepare(
    const BindlessRequirements& request,
    const BindlessShaderArtifact& consumer,
    const ZeroBaseIndirectProducerParams& parameters,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    const auto* extension =
        static_cast<const BindlessCrossExtension*>(device_.queryInterface(kExtensionBindlessCross));
    if (!extension) {
        diagnostic = "RHI-BINDLESS-EXTENSION-UNAVAILABLE";
        return {};
    }
    if (extension->capabilities().indirectPacketStorageContractVersion != 1u) {
        diagnostic = "RHI-BINDLESS-INDIRECT-PACKET-STORAGE-UNAVAILABLE";
        return {};
    }
    if (const auto* id = validateBudget()) {
        diagnostic = id;
        return {};
    }
    const auto plan = extension->plan(request, consumer);
    if (!plan.success) {
        diagnostic = plan.diagnosticId;
        return {};
    }
    const auto& reader = consumer.indirectInstanceConsumer;
    if (reader.contractVersion != 1u || reader.recordIndex != 0u || !parameters.instanceCapacity
        || parameters.instanceCapacity != reader.instanceCapacity || parameters.reserved
        || parameters.contractVersion != 1u) {
        diagnostic = "RHI-BINDLESS-INDIRECT-PACKET-PARAMETERS";
        return {};
    }
    const auto issuance = extension->validateIndirectPacketCreation(plan);
    if (!issuance.success) {
        diagnostic = issuance.diagnostics;
        return {};
    }
    auto packet = prepareStorageCandidate(parameters,
        *extension,
        BindlessIndirectPacket::Issuance::PublicConsumer,
        &plan,
        diagnostic);
    if (!packet)
        return {};
    const auto payload = extension->validateIndirectPacketPayload(*packet);
    if (!payload.success) {
        diagnostic = payload.diagnostics;
        return {};
    }
    return packet;
}

core::UniquePtr<BindlessIndirectPacket> BindlessIndirectPacketPool::prepareStorageCandidate(
    const ZeroBaseIndirectProducerParams& parameters,
    const BindlessCrossExtension& issuer,
    BindlessIndirectPacket::Issuance purpose,
    const BindlessLoweringPlan* plan,
    core::pmr::String& diagnostic)
{
    if (!BindlessIndirectPacketStorageContract::knownIssuance(purpose)
        || (purpose == BindlessIndirectPacket::Issuance::PublicConsumer ? !plan || !plan->success
                                                                        : plan != nullptr)) {
        diagnostic = "RHI-BINDLESS-INDIRECT-PACKET-CREATION-PURPOSE";
        return {};
    }
    if (const auto* id = validateBudget()) {
        diagnostic = id;
        return {};
    }
    BindlessIndirectPacketStorageContract::ParameterBytes parameterBytes;
    if (const auto* id =
            BindlessIndirectPacketStorageContract::encodeParameters(parameters, parameterBytes)) {
        diagnostic = id;
        return {};
    }
    auto count = core::makeUnique<core::AtomicCapacityLease>(counts_, 1u);
    if (!count->charged()) {
        diagnostic = "RHI-BINDLESS-INDIRECT-PACKET-POOL-FULL";
        return {};
    }
    auto bytes = core::makeUnique<core::AtomicCapacityLease>(bytes_, budget_.maxBytesPerPacket);
    if (!bytes->charged()) {
        diagnostic = "RHI-BINDLESS-INDIRECT-PACKET-RESIDENT-BUDGET";
        return {};
    }
    auto packet =
        core::makeUnique<BindlessIndirectPacket>(BindlessIndirectPacket::ConstructionKey{});
    packet->countLease_ = std::move(count);
    packet->byteLease_ = std::move(bytes);
    if (plan)
        packet->consumerPlan_ = *plan;
    packet->issuance_ = purpose;
    packet->parameters_ = parameters;
    packet->lifetime_ = issuer.lifetime_.ticket();
    packet->generation_ = allocateGpuResourceId().value;
    if (!packet->generation_) {
        diagnostic = "RHI-BINDLESS-INDIRECT-PACKET-ID-EXHAUSTED";
        return {};
    }
    const auto createStorage = [&](BindlessIndirectPacketStorageContract::Role role) {
        const auto spec = BindlessIndirectPacketStorageContract::descriptor(role);
        return device_.createBuffer({core::pmr::String(spec.label),
            spec.sizeBytes,
            spec.usage,
            spec.memory,
            spec.initialState});
    };
    packet->physicalArgs_ =
        createStorage(BindlessIndirectPacketStorageContract::Role::PhysicalArgs);
    packet->instanceMappings_ =
        createStorage(BindlessIndirectPacketStorageContract::Role::InstanceMapping);
    packet->producerParameters_ =
        createStorage(BindlessIndirectPacketStorageContract::Role::Parameters);
    for (const auto* buffer : {packet->physicalArgs_.get(),
             packet->instanceMappings_.get(),
             packet->producerParameters_.get()}) {
        if (!buffer) {
            diagnostic = "RHI-BINDLESS-INDIRECT-PACKET-RESOURCE-CREATION-FAILED";
            return {};
        }
        const auto reported = buffer->allocation().estimatedSizeBytes;
        if (reported < buffer->desc().sizeBytes) {
            diagnostic = "RHI-BINDLESS-INDIRECT-PACKET-ACCOUNTING-MISSING";
            return {};
        }
        if (reported > budget_.maxBytesPerPacket - packet->reportedBufferBytes_) {
            diagnostic = "RHI-BINDLESS-INDIRECT-PACKET-FOOTPRINT-EXCEEDED";
            return {};
        }
        packet->reportedBufferBytes_ += reported;
    }
    // Until trusted GPU production, the command suppresses all instances and
    // the reader rejects the version-zero mapping. No stale bytes are usable.
    const DrawIndexedIndirectCommand suppressed{};
    const IndirectInstanceBase invalid{};
    if (!device_.writeBuffer(*packet->physicalArgs_, &suppressed, sizeof(suppressed)).success
        || !device_.writeBuffer(*packet->instanceMappings_, &invalid, sizeof(invalid)).success
        || !device_
            .writeBuffer(*packet->producerParameters_, parameterBytes.data(), parameterBytes.size())
            .success) {
        diagnostic = "RHI-BINDLESS-INDIRECT-PACKET-INITIALIZATION-FAILED";
        return {};
    }
    for (auto* buffer : {packet->physicalArgs_.get(),
             packet->instanceMappings_.get(),
             packet->producerParameters_.get()}) {
        buffer->sealHostWrites();
        buffer->sealGpuWrites();
    }
    const auto storage = packet->validateStorage();
    if (!storage.success) {
        diagnostic = storage.diagnostics;
        return {};
    }
    return packet;
}

ValidationResult BindlessIndirectProcessorIntent::validate() const
{
    if (!lifetime_.isCurrent() || !desc_.consumerAuthorization
        || !desc_.consumerAuthorization->current())
        return {false, "RHI-BINDLESS-INDIRECT-PROCESSOR-STALE"};
    if (!desc_.optIn || !desc_.producer || !desc_.logicalSource || !desc_.packet
        || !desc_.consumerSnapshot)
        return {false, "RHI-BINDLESS-INDIRECT-PROCESSOR-OWNERS-MISSING"};
    if (!desc_.producer->sharedOwnerMatches(desc_.producer)
        || !desc_.logicalSource->sharedOwnerMatches(desc_.logicalSource)
        || !desc_.packet->sharedOwnerMatches(desc_.packet)
        || !desc_.consumerSnapshot->sharedOwnerMatches(desc_.consumerSnapshot)
        || !desc_.consumerAuthorization->sharedOwnerMatches(desc_.consumerAuthorization))
        return {false, "RHI-BINDLESS-INDIRECT-PROCESSOR-OWNER-MISMATCH"};
    if (!bindlessPlansMatch(desc_.consumerAuthorization->plan(), desc_.consumerPlan))
        return {false, "RHI-BINDLESS-INDIRECT-PROCESSOR-PLAN-MISMATCH"};
    const auto& producer = desc_.producer->artifact();
    const auto& consumer = desc_.consumerAuthorization->package().artifact();
    if (producer.zeroBaseIndirectProducerContractVersion != 1u
        || producer.execution != consumer.execution
        || producer.indirectInstanceBaseContractVersion != 1u)
        return {false, "RHI-BINDLESS-INDIRECT-PROCESSOR-PACKAGE-MISMATCH"};
    // Version 1 consumes a graphics-only package. The protected mapping cannot
    // also appear in a caller-owned Compute writer or a mixed producer package.
    if (std::any_of(consumer.physicalLayout.bindings.begin(),
            consumer.physicalLayout.bindings.end(),
            [](const auto& binding) {
                return (binding.stages & ShaderStage::Compute) != ShaderStage::None;
            })
        || std::any_of(desc_.consumerAuthorization->package().modules().begin(),
            desc_.consumerAuthorization->package().modules().end(),
            [](const auto& module) {
                return std::any_of(module.reflection.reflection.entryPoints.begin(),
                    module.reflection.reflection.entryPoints.end(),
                    [](const auto& entry) {
                        return entry.stage == shader::ShaderStage::Compute;
                    });
            }))
        return {false, "RHI-BINDLESS-INDIRECT-PROCESSOR-CONSUMER-NOT-GRAPHICS-ONLY"};
    const auto consumerResult = desc_.packet->validateConsumer(
        *desc_.consumerSnapshot, desc_.consumerPlan, desc_.consumerGeneration);
    if (!consumerResult.success)
        return consumerResult;
    const auto accesses = desc_.packet->producerAccesses(*desc_.logicalSource);
    return desc_.packet->validateProducerAccess(
        *desc_.logicalSource, BindlessAccessArray(accesses.data(), accesses.size()));
}

ValidationResult BindlessIndirectProcessorIntent::validateForRecord(
    const BindlessAccessDeclarations& producerAccess,
    const BindlessAccessDeclarations& consumerAccess) const
{
    const auto live = validate();
    if (!live.success)
        return live;
    const auto producer =
        desc_.packet->validateProducerAccess(*desc_.logicalSource, producerAccess);
    if (!producer.success)
        return producer;
    const auto consumer = desc_.consumerSnapshot->validateForRecord(desc_.consumerPlan,
        desc_.consumerGeneration,
        consumerAccess,
        ShaderStage::Vertex | ShaderStage::Fragment);
    if (!consumer.success)
        return consumer;
    return validateBindlessIndirectDrawAccess(
        *desc_.packet->physicalArgs(), 0u, 1u, sizeof(DrawIndexedIndirectCommand), consumerAccess);
}

BindlessIndirectProcessorIntentPool::BindlessIndirectProcessorIntentPool(
    GpuDevice& device, BindlessIndirectProcessorIntentBudget budget)
    : device_(device), budget_(budget),
      counts_(core::makeShared<core::AtomicCapacityBudget>(budget.maxIntents)),
      bytes_(core::makeShared<core::AtomicCapacityBudget>(budget.maxResidentBytes))
{}

std::shared_ptr<const BindlessIndirectProcessorIntent> BindlessIndirectProcessorIntentPool::prepare(
    BindlessIndirectProcessorIntentDesc desc, core::pmr::String& diagnostic)
{
    diagnostic.clear();
    const auto* extension =
        static_cast<const BindlessCrossExtension*>(device_.queryInterface(kExtensionBindlessCross));
    if (!extension) {
        diagnostic = "RHI-BINDLESS-EXTENSION-UNAVAILABLE";
        return {};
    }
    if (!desc.optIn) {
        diagnostic = "RHI-BINDLESS-OPT-IN-REQUIRED";
        return {};
    }
    if (!budget_.maxIntents || !budget_.maxBytesPerIntent || !budget_.maxResidentBytes
        || budget_.maxBytesPerIntent > budget_.maxResidentBytes) {
        diagnostic = "RHI-BINDLESS-INDIRECT-PROCESSOR-BUDGET";
        return {};
    }
    if (!desc.consumerAuthorization || !desc.consumerAuthorization->issuedBy(extension)) {
        diagnostic = "RHI-BINDLESS-INDIRECT-PROCESSOR-AUTHORIZATION-INVALID";
        return {};
    }
    if (!desc.consumerAuthorization->sharedOwnerMatches(desc.consumerAuthorization)) {
        diagnostic = "RHI-BINDLESS-INDIRECT-PROCESSOR-OWNER-MISMATCH";
        return {};
    }
    auto intent = core::makeShared<BindlessIndirectProcessorIntent>(
        BindlessIndirectProcessorIntent::ConstructionKey{});
    intent->lifetime_ = extension->lifetime_.ticket();
    intent->desc_ = std::move(desc);
    const auto validation = intent->validate();
    if (!validation.success) {
        diagnostic = validation.diagnostics;
        return {};
    }
    const auto source = extension->validateIndirectProcessorSource(*intent->desc_.logicalSource);
    if (!source.success) {
        diagnostic = source.diagnostics;
        return {};
    }
    const auto& retained = intent->desc_;
    const auto sourceBytes = retained.logicalSource->allocation().estimatedSizeBytes;
    if (sourceBytes < retained.logicalSource->desc().sizeBytes) {
        diagnostic = "RHI-BINDLESS-INDIRECT-PROCESSOR-ACCOUNTING-MISSING";
        return {};
    }
    for (const auto size : {sourceBytes,
             retained.packet->reportedBufferBytes(),
             retained.consumerPlan.snapshotBytes,
             retained.producer->sourceBytes(),
             retained.consumerAuthorization->package().sourceBytes()}) {
        if (size > budget_.maxBytesPerIntent - intent->retainedBytes_) {
            diagnostic = "RHI-BINDLESS-INDIRECT-PROCESSOR-FOOTPRINT-EXCEEDED";
            return {};
        }
        intent->retainedBytes_ += size;
    }
    intent->countLease_ = core::makeUnique<core::AtomicCapacityLease>(counts_, 1u);
    if (!intent->countLease_->charged()) {
        diagnostic = "RHI-BINDLESS-INDIRECT-PROCESSOR-POOL-FULL";
        return {};
    }
    intent->byteLease_ =
        core::makeUnique<core::AtomicCapacityLease>(bytes_, budget_.maxBytesPerIntent);
    if (!intent->byteLease_->charged()) {
        diagnostic = "RHI-BINDLESS-INDIRECT-PROCESSOR-RESIDENT-BUDGET";
        return {};
    }
    return intent;
}

BindlessIndirectProcessorRecordingLease BindlessIndirectProcessorRecordingLease::acquire(
    std::shared_ptr<const BindlessIndirectProcessorIntent> intent,
    std::uint64_t recordingIdentity,
    const BindlessAccessDeclarations& producerAccess,
    const BindlessAccessDeclarations& consumerAccess,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    if (!intent || !recordingIdentity) {
        diagnostic = "RHI-BINDLESS-INDIRECT-PROCESSOR-RECORDING-INVALID";
        return {};
    }
    if (!intent->sharedOwnerMatches(intent)) {
        diagnostic = "RHI-BINDLESS-INDIRECT-PROCESSOR-OWNER-MISMATCH";
        return {};
    }
    const auto validation = intent->validateForRecord(producerAccess, consumerAccess);
    if (!validation.success) {
        diagnostic = validation.diagnostics;
        return {};
    }
    const auto& desc = intent->desc();
    auto packetLease = BindlessIndirectPacketRecordingLease::acquire(desc.packet,
        desc.consumerSnapshot,
        desc.consumerPlan,
        desc.consumerGeneration,
        recordingIdentity,
        diagnostic);
    if (!packetLease.valid())
        return {};
    BindlessIndirectProcessorRecordingLease lease;
    lease.intent_ = std::move(intent);
    lease.packetLease_ = std::move(packetLease);
    return lease;
}

ValidationResult BindlessIndirectProcessorRecordingLease::validateForRecord(
    const BindlessIndirectProcessorIntent& intent,
    std::uint64_t recordingIdentity,
    const BindlessAccessDeclarations& producerAccess,
    const BindlessAccessDeclarations& consumerAccess) const
{
    if (intent_.get() != &intent || !valid())
        return {false, "RHI-BINDLESS-INDIRECT-PROCESSOR-RECORDING-MISMATCH"};
    const auto& desc = intent.desc();
    const auto ownership = packetLease_.validateForRecord(*desc.packet,
        *desc.consumerSnapshot,
        desc.consumerPlan,
        desc.consumerGeneration,
        recordingIdentity);
    return ownership.success ? intent.validateForRecord(producerAccess, consumerAccess) : ownership;
}

} // namespace arp::rhi

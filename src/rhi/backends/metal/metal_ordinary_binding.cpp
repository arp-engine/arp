#include "rhi/backends/metal/metal_ordinary_binding.h"

#include "rhi/backends/metal/metal_backend_objects.h"
#include "rhi/backends/metal/metal_closed_consumer_geometry.h"
#include "rhi/backends/metal/metal_closed_consumer_native_preparation.h"
#include "rhi/backends/metal/metal_device.h"
#include "rhi/backends/metal/metal_graphics_preparation.h"
#include "rhi/backends/metal/metal_indirect_preparation.h"
#include "rhi/backends/metal/metal_instance_attribute_consumer.h"
#include "rhi/backends/metal/metal_native_pipelines.h"
#include "rhi/backends/metal/metal_native_runtime.h"
#include "rhi/backends/metal/metal_zero_base_indirect_cold_bindings.h"
#include "rhi/backends/metal/metal_zero_base_indirect_consumer_cold_bindings.h"
#include "rhi/backends/metal/metal_zero_base_indirect_packet_storage.h"
#include "rhi/backends/metal/metal_zero_base_indirect_source.h"
#include "rhi/frontend/descriptor_resource_validation.h"
#include "rhi/frontend/descriptor_set_validation.h"
#include "rhi/gpu_extensions.h"
#include "rhi/gpu_pipeline.h"
#include "rhi/semantics/push_constant_validation.h"

namespace arp::rhi_metal {
namespace {
constexpr MetalRhiSealedBufferUpload::Diagnostics closedGeometryDiagnostics{
    "RHI-METAL-CLOSED-GEOMETRY-CONTENT-MISMATCH",
    "RHI-METAL-CLOSED-GEOMETRY-WRITES-NOT-SEALED",
    "RHI-METAL-CLOSED-GEOMETRY-RESOURCE-MISMATCH",
    "RHI-METAL-CLOSED-GEOMETRY-NATIVE-IDENTITY-MISMATCH",
    "RHI-METAL-CLOSED-GEOMETRY-BUFFER-CREATION-FAILED",
    "RHI-METAL-CLOSED-GEOMETRY-UPLOAD-FAILED"};
constexpr MetalRhiSealedBufferUpload::Diagnostics closedAdmissionDiagnostics{
    "RHI-METAL-CLOSED-CONSUMER-READBACK-CONTENT",
    "RHI-METAL-CLOSED-CONSUMER-READBACK-SEAL",
    "RHI-METAL-CLOSED-CONSUMER-READBACK-RESOURCE",
    "RHI-METAL-CLOSED-CONSUMER-READBACK-NATIVE-IDENTITY",
    "RHI-METAL-CLOSED-CONSUMER-READBACK-CREATION-FAILED",
    "RHI-METAL-CLOSED-CONSUMER-READBACK-UPLOAD-FAILED"};
constexpr MetalRhiSealedBufferUpload::Diagnostics canonicalRecordingDiagnostics{
    "RHI-METAL-CANONICAL-PRODUCER-CONTENT-MISMATCH",
    "RHI-METAL-CANONICAL-PRODUCER-WRITES-NOT-SEALED",
    "RHI-METAL-CANONICAL-PRODUCER-RESOURCE-MISMATCH",
    "RHI-METAL-CANONICAL-PRODUCER-NATIVE-IDENTITY-MISMATCH",
    "RHI-METAL-CANONICAL-PRODUCER-BUFFER-CREATION-FAILED",
    "RHI-METAL-CANONICAL-PRODUCER-UPLOAD-FAILED"};
constexpr MetalRhiSealedBufferUpload::Diagnostics zeroBaseColdDiagnostics{
    "RHI-METAL-ZERO-BASE-COLD-CONTENT-MISMATCH",
    "RHI-METAL-ZERO-BASE-COLD-WRITES-NOT-SEALED",
    "RHI-METAL-ZERO-BASE-COLD-RESOURCE-MISMATCH",
    "RHI-METAL-ZERO-BASE-COLD-NATIVE-IDENTITY-MISMATCH",
    "RHI-METAL-ZERO-BASE-COLD-BUFFER-CREATION-FAILED",
    "RHI-METAL-ZERO-BASE-COLD-UPLOAD-FAILED"};
constexpr MetalRhiSealedBufferUpload::Diagnostics consumerColdDiagnostics{
    "RHI-METAL-CONSUMER-COLD-CONTENT-MISMATCH",
    "RHI-METAL-CONSUMER-COLD-WRITES-NOT-SEALED",
    "RHI-METAL-CONSUMER-COLD-RESOURCE-MISMATCH",
    "RHI-METAL-CONSUMER-COLD-NATIVE-IDENTITY-MISMATCH",
    "RHI-METAL-CONSUMER-COLD-BUFFER-CREATION-FAILED",
    "RHI-METAL-CONSUMER-COLD-UPLOAD-FAILED"};
constexpr std::array zeroBaseColdDomains{MetalRhiSealedBufferUpload::Domain::ImmutableStorage,
    MetalRhiSealedBufferUpload::Domain::ProtectedScratchArgs,
    MetalRhiSealedBufferUpload::Domain::ProtectedScratchMapping};
constexpr MetalRhiSealedBufferUpload::Diagnostics zeroBasePacketDiagnostics{
    "RHI-METAL-ZERO-BASE-PACKET-CONTENT-MISMATCH",
    "RHI-METAL-ZERO-BASE-PACKET-WRITES-NOT-SEALED",
    "RHI-METAL-ZERO-BASE-PACKET-RESOURCE-MISMATCH",
    "RHI-METAL-ZERO-BASE-PACKET-NATIVE-IDENTITY-MISMATCH",
    "RHI-METAL-ZERO-BASE-PACKET-BUFFER-CREATION-FAILED",
    "RHI-METAL-ZERO-BASE-PACKET-UPLOAD-FAILED"};
constexpr std::array zeroBasePacketDomains{MetalRhiSealedBufferUpload::Domain::ProtectedPacketArgs,
    MetalRhiSealedBufferUpload::Domain::ProtectedPacketMapping,
    MetalRhiSealedBufferUpload::Domain::ProtectedPacketParameters};
constexpr MetalRhiSealedBufferUpload::Diagnostics zeroBaseSourceDiagnostics{
    "RHI-METAL-ZERO-BASE-SOURCE-CONTENT-MISMATCH",
    "RHI-METAL-ZERO-BASE-SOURCE-WRITES-NOT-SEALED",
    "RHI-METAL-ZERO-BASE-SOURCE-RESOURCE-MISMATCH",
    "RHI-METAL-ZERO-BASE-SOURCE-NATIVE-IDENTITY-MISMATCH",
    "RHI-METAL-ZERO-BASE-SOURCE-BUFFER-CREATION-FAILED",
    "RHI-METAL-ZERO-BASE-SOURCE-UPLOAD-FAILED"};
} // namespace

const char* MetalRhiClosedConsumerGeometry::validate() const noexcept
{
    if (!issuer_.isCurrent())
        return "RHI-METAL-CLOSED-GEOMETRY-ISSUER-RETIRED";
    if (!pipeline_ || !pipeline_->sharedOwnerMatches(pipeline_))
        return "RHI-METAL-CLOSED-GEOMETRY-ORIGINAL-OWNER";
    if (const auto* id = pipeline_->validate())
        return id;
    const auto& source = pipeline_->source();
    const auto& device = pipeline_->native()->nativeDevice;
    if (!content_ || !content_->sharedOwnerMatches(content_))
        return "RHI-METAL-CLOSED-GEOMETRY-ORIGINAL-OWNER";
    if (const auto* id =
            content_->validate(*pipeline_->package(), source->content(), pipeline_->vertexInput()))
        return id;
    if (!upload_.buffer || !upload_.buffer->sharedOwnerMatches(upload_.buffer))
        return "RHI-METAL-CLOSED-GEOMETRY-ORIGINAL-OWNER";
    if (const auto* id = content_->validateStorage(*upload_.buffer))
        return id;
    if (!budget_.maxUploadBytes
        || upload_.buffer->allocation().estimatedSizeBytes > budget_.maxUploadBytes
        || !budget_.maxChildOwners || owners_.size() > budget_.maxChildOwners)
        return "RHI-METAL-CLOSED-GEOMETRY-BUDGET";
    if (const auto* id = Upload::validate(upload_.buffer,
            source->index()->registry(),
            upload_.resource,
            range_.nativeIdentity,
            device,
            content_->bytes().size(),
            rhi::ResourceUsage::VertexBuffer,
            closedGeometryDiagnostics))
        return id;
    if (!range_.fact.matches(pipeline_->facts().vertexStreamBuffer))
        return "RHI-METAL-CLOSED-GEOMETRY-STREAM-ABI";
    if (const auto* id = MetalRhiStageBufferRangeValidator::validate(
            pipeline_->native()->graphicsBufferFacts[0u],
            rhi::ShaderStage::Vertex,
            pipeline_->facts().vertexStreamBuffer.slot,
            0u,
            content_->bytes().size(),
            range_))
        return id;
    if (!owners_.matches([&](auto&& visit) {
            projectOwners(visit);
        }))
        return "RHI-METAL-CLOSED-GEOMETRY-ORIGINAL-OWNER";
    return nullptr;
}

std::shared_ptr<const MetalRhiClosedConsumerGeometry> MetalRhiClosedConsumerGeometry::prepare(
    std::shared_ptr<const Pipeline> pipeline,
    rhi::GpuDevice& device,
    Contract::Input input,
    Budget budget,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    const auto fail = [&](const char* id) -> std::shared_ptr<const MetalRhiClosedConsumerGeometry> {
        diagnostic = id;
        return {};
    };
    const auto decision = Contract::coldLowering(device.backend());
    if (!decision.prototypeUpload)
        return fail(decision.diagnosticId);
    if (!pipeline || !pipeline->sharedOwnerMatches(pipeline))
        return fail("RHI-METAL-CLOSED-GEOMETRY-ORIGINAL-OWNER");
    if (const auto* id = pipeline->validate())
        return fail(id);
    const auto& source = pipeline->source();
    const auto& registry = source->index()->registry();
    if (registry != metalRhiIssuingResourceRegistry(device))
        return fail("RHI-METAL-CLOSED-GEOMETRY-ISSUER-MISMATCH");
    const auto* issuer = static_cast<const rhi::BindlessCrossExtension*>(
        device.queryInterface(rhi::kExtensionBindlessCross));
    if (!issuer || !issuer->lifetime_.ticket().isCurrent())
        return fail("RHI-METAL-CLOSED-GEOMETRY-ISSUER-RETIRED");
    if (!budget.maxUploadBytes || !budget.maxChildOwners)
        return fail("RHI-METAL-CLOSED-GEOMETRY-BUDGET");
    if (input.bytes > budget.maxUploadBytes)
        return fail("RHI-METAL-CLOSED-GEOMETRY-UPLOAD-BUDGET");
    const auto required = 2u + pipeline->childOwnerCount() + 1u
        + MetalRhiResourceOwnerProjection::independentBufferOwnerCount;
    if (required > budget.maxChildOwners)
        return fail("RHI-METAL-CLOSED-GEOMETRY-CHILD-OWNER-BUDGET");
    try {
        core::CpuMemoryQuotaScope scope(source->quota());
        Contract content(
            *pipeline->package(), source->content(), pipeline->vertexInput(), input, budget.values);
        if (const auto* id = content.diagnostic())
            return fail(id);
        MetalRhiStageBufferRange range;
        if (const auto* id = MetalRhiStageBufferRangeValidator::capture(
                pipeline->native()->graphicsBufferFacts[0u],
                rhi::ShaderStage::Vertex,
                pipeline->facts().vertexStreamBuffer.slot,
                0u,
                content.bytes().size(),
                range))
            return fail(id);
        auto upload = Upload::prepare(device,
            registry,
            pipeline->native()->nativeDevice,
            content.bytes(),
            rhi::ResourceUsage::VertexBuffer,
            closedGeometryDiagnostics,
            diagnostic);
        if (!upload)
            return {};
        if (upload->buffer->allocation().estimatedSizeBytes > budget.maxUploadBytes)
            return fail("RHI-METAL-CLOSED-GEOMETRY-UPLOAD-BUDGET");
        range.nativeIdentity = MetalRhiNativeResourceObservation(upload->resource.buffer->resource);
        auto result = core::makeShared<MetalRhiClosedConsumerGeometry>(ConstructionKey{},
            pipeline,
            std::move(content),
            std::move(*upload),
            range,
            budget,
            issuer->lifetime_.ticket());
        if (result->childOwnerCount() != required)
            return fail("RHI-METAL-CLOSED-GEOMETRY-OWNER-PROJECTION");
        if (const auto* id = result->validate())
            return fail(id);
        return result;
    } catch (const std::bad_alloc&) {
        core::CpuMemoryQuotaScope diagnosticScope(nullptr);
        return fail("RHI-METAL-CLOSED-GEOMETRY-ALLOCATION-FAILED");
    }
}

const char* MetalRhiCanonicalProducerRecordingPreparation::validateCommand(
    const Cold& cold, const MetalRhiNativeCommandPayload& command, bool finished) noexcept
{
    const auto& source = cold.producer()->packet()->pipeline()->source();
    if (!command.recordingIdentity_ || !command.nativeCommandBuffer_ || !command.stats_
        || !command.stats_->nativeRuntimeBacked || command.stats_->nativeRuntimeDiagnosticOnly
        || command.abandoned_ || command.recordingPoisoned_
        || command.submitted_.load(std::memory_order_acquire)
        || command.stats_->nativeCommandBufferFinished != finished || !command.recordingRetention_
        || command.recordingRetention_->state()
            != (finished ? rhi::common::CommandRetentionStorage::State::Sealed
                         : rhi::common::CommandRetentionStorage::State::Recording))
        return "RHI-METAL-CANONICAL-PRODUCER-COMMAND-CLOSED";
    if (command.recordingRetention_->quota() != source->quota())
        return "RHI-COMMAND-RETENTION-CPU-QUOTA-MISMATCH";
    if (command.resourceRegistry_ != source->index()->registry()
        || !core::SharedOwnerProjection::sameOwner(
            command.nativeDevice_, cold.producer()->packet()->pipeline()->native()->nativeDevice))
        return "RHI-METAL-CANONICAL-PRODUCER-ISSUER-MISMATCH";
    return nullptr;
}

MetalRhiCanonicalProducerRecordingPreparation::Contract::Inputs
MetalRhiCanonicalProducerRecordingPreparation::inputs(
    const std::shared_ptr<const Cold>& cold, const Buffers& readbacks) noexcept
{
    const auto& producer = cold->producer();
    const auto& packet = producer->packet()->packet();
    const auto& source = producer->packet()->pipeline()->source();
    Contract::Inputs values;
    values.producerPackage = source->package();
    values.consumerPackage = cold->pipeline()->package();
    values.source = source->sharedContent();
    values.packet = packet;
    values.producerFault = producer->faultDomain();
    values.consumerFault = cold->faultDomain();
    values.buffers = {producer->uploads()[0u].buffer,
        std::shared_ptr<const rhi::GpuBuffer>(
            producer->faultDomain(), producer->faultDomain()->faultLatchBuffer()),
        source->sourceWrapper(),
        producer->uploads()[1u].buffer,
        producer->uploads()[2u].buffer,
        std::shared_ptr<const rhi::GpuBuffer>(packet, packet->producerParameters()),
        readbacks[0u],
        readbacks[1u],
        std::shared_ptr<const rhi::GpuBuffer>(
            producer->faultDomain(), producer->faultDomain()->faultReadbackBuffer())};
    for (std::size_t i = 0u; i < values.images.size(); ++i)
        values.images[i] = cold->images()[i].values();
    return values;
}

const char* MetalRhiCanonicalProducerRecordingPreparation::validate() const noexcept
{
    if (status() != Status::Cold)
        return "RHI-METAL-CANONICAL-PRODUCER-PREPARATION-TERMINAL";
    return validateForState(rhi::ResourceState::ShaderWrite, false);
}

const char* MetalRhiCanonicalProducerRecordingPreparation::validateForState(
    rhi::ResourceState state, bool finished) const noexcept
{
    const auto command = command_.lock();
    if (!cold_ || !cold_->sharedOwnerMatches(cold_) || !command
        || !command->sharedOwnerMatches(command))
        return "RHI-METAL-CANONICAL-PRODUCER-ORIGINAL-OWNER";
    if (const auto* id = cold_->validateForProducerState(state))
        return id;
    if (const auto* id = validateCommand(*cold_, *command, finished))
        return id;
    if (!core::SharedOwnerProjection::sameOwner(
            commandOwners_[0u].lock(), command->nativeCommandBuffer_)
        || !core::SharedOwnerProjection::sameOwner(
            commandOwners_[1u].lock(), command->nativeDevice_)
        || !core::SharedOwnerProjection::sameOwner(commandOwners_[2u].lock(), command->stats_)
        || retention_ != command->recordingRetention_.get())
        return "RHI-METAL-CANONICAL-PRODUCER-COMMAND-ORIGINAL-OWNER";
    if (const auto* id = budget_.values.validate())
        return id;
    if (!lease_.valid()
        || lease_.faultLatchBuffer() != cold_->producer()->faultDomain()->faultLatchBuffer())
        return "RHI-METAL-CANONICAL-PRODUCER-FAULT-LEASE";
    if (budget_.maxNewBytes < minimumNewBytes || !budget_.maxChildOwners
        || owners_.size() > budget_.maxChildOwners)
        return "RHI-METAL-CANONICAL-PRODUCER-PREPARATION-BUDGET";
    const auto& source = cold_->producer()->packet()->pipeline()->source();
    const auto& native = cold_->producer()->packet()->pipeline()->native()->nativeDevice;
    std::uint64_t owned = 0u;
    for (std::size_t i = 0u; i < buffers_.size(); ++i) {
        const auto bytes =
            i ? sizeof(rhi::IndirectInstanceBase) : sizeof(rhi::DrawIndexedIndirectCommand);
        const rhi::BufferDesc descriptor{"",
            bytes,
            rhi::ResourceUsage::CopyDst,
            rhi::MemoryUsage::GpuToCpu,
            rhi::ResourceState::CopyDst};
        if (const auto* id = MetalRhiSealedBufferUpload::validateRegistered(buffers_[i],
                source->index()->registry(),
                resources_[i],
                observations_[i],
                native,
                descriptor,
                metalRhiNativeStorageMode(MetalRhiMemoryPolicy::ManagedReadback),
                false,
                false,
                canonicalRecordingDiagnostics))
            return id;
        if (buffers_[i]->state() != rhi::ResourceState::CopyDst)
            return "RHI-METAL-CANONICAL-PRODUCER-READBACK-STATE";
        const auto resident = buffers_[i]->allocation().estimatedSizeBytes;
        if (resident < bytes || resident > budget_.maxNewBytes - owned)
            return "RHI-METAL-CANONICAL-PRODUCER-READBACK-BUDGET";
        owned += resident;
        for (const auto& resource : cold_->producer()->resources())
            if (resource.buffer->resource.nativeObject
                == resources_[i].buffer->resource.nativeObject)
                return "RHI-METAL-CANONICAL-PRODUCER-NATIVE-ALIAS";
        if (resources_[i].buffer->resource.nativeObject
                == cold_->producer()->readback().buffer->resource.nativeObject
            || resources_[i].buffer->resource.nativeObject
                == cold_->readback().buffer->resource.nativeObject)
            return "RHI-METAL-CANONICAL-PRODUCER-NATIVE-ALIAS";
        for (const auto& resource : cold_->resources())
            if (resource.buffer->resource.nativeObject
                == resources_[i].buffer->resource.nativeObject)
                return "RHI-METAL-CANONICAL-PRODUCER-NATIVE-ALIAS";
        for (std::size_t j = 0u; j < i; ++j)
            if (resources_[j].buffer->resource.nativeObject
                == resources_[i].buffer->resource.nativeObject)
                return "RHI-METAL-CANONICAL-PRODUCER-NATIVE-ALIAS";
    }
    if (const auto* id = admission_.validate(inputs(cold_, buffers_),
            command->recordingIdentity(),
            state == rhi::ResourceState::CopySrc ? Contract::StoragePhase::ReadbackReady
                                                 : Contract::StoragePhase::Cold))
        return id;
    const auto faultBytes =
        cold_->producer()->faultDomain()->faultReadbackBuffer()->allocation().estimatedSizeBytes;
    if (owned != ownedBytes_ || admission_.facts().actualReadbackBytes < faultBytes
        || owned != admission_.facts().actualReadbackBytes - faultBytes
        || !owners_.matches([&](auto&& visit) {
               projectOwners(visit);
           }))
        return "RHI-METAL-CANONICAL-PRODUCER-PROJECTION-MISMATCH";
    return nullptr;
}

std::shared_ptr<const MetalRhiCanonicalProducerRecordingPreparation>
MetalRhiCanonicalProducerRecordingPreparation::prepare(std::shared_ptr<const Cold> cold,
    rhi::GpuDevice& device,
    const std::shared_ptr<MetalRhiNativeCommandPayload>& command,
    Budget budget,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    const auto fail = [&](const char* id)
        -> std::shared_ptr<const MetalRhiCanonicalProducerRecordingPreparation> {
        diagnostic = id;
        return {};
    };
    if (!cold || !cold->sharedOwnerMatches(cold) || !command
        || !command->sharedOwnerMatches(command))
        return fail("RHI-METAL-CANONICAL-PRODUCER-ORIGINAL-OWNER");
    if (const auto* id = cold->validate())
        return fail(id);
    if (const auto* id = validateCommand(*cold, *command))
        return fail(id);
    if (const auto* id = budget.values.validate())
        return fail(id);
    if (budget.maxNewBytes < minimumNewBytes || !budget.maxChildOwners)
        return fail("RHI-METAL-CANONICAL-PRODUCER-PREPARATION-BUDGET");
    const auto& source = cold->producer()->packet()->pipeline()->source();
    const auto& registry = source->index()->registry();
    if (device.backend() != rhi::Backend::Metal
        || !core::SharedOwnerProjection::sameOwner(
            registry, metalRhiIssuingResourceRegistry(device)))
        return fail("RHI-METAL-CANONICAL-PRODUCER-ISSUER-MISMATCH");
    const auto expectedOwners = 1u + cold->childOwnerCount() + Contract::referenceSlots + 1u
        + Buffers{}.size() * (1u + MetalRhiResourceOwnerProjection::independentBufferOwnerCount)
        + (command->canonicalProducerBudget_ ? 1u + command->canonicalProducerSlots_.size() : 0u);
    if (expectedOwners > budget.maxChildOwners)
        return fail("RHI-METAL-CANONICAL-PRODUCER-CHILD-OWNER-BUDGET");
    try {
        core::CpuMemoryQuotaScope scope(source->quota());
        auto lease =
            rhi::BindlessFaultSubmissionLease::acquire(cold->producer()->faultDomain(), diagnostic);
        if (!lease.valid())
            return {};
        Buffers buffers;
        Resources resources;
        std::uint64_t owned = 0u;
        for (std::size_t i = 0u; i < buffers.size(); ++i) {
            const auto bytes =
                i ? sizeof(rhi::IndirectInstanceBase) : sizeof(rhi::DrawIndexedIndirectCommand);
            buffers[i] = core::shareUnique(device.createBuffer({"CanonicalProducerReadback",
                bytes,
                rhi::ResourceUsage::CopyDst,
                rhi::MemoryUsage::GpuToCpu,
                rhi::ResourceState::CopyDst}));
            if (!buffers[i])
                return fail("RHI-METAL-CANONICAL-PRODUCER-READBACK-CREATION-FAILED");
            buffers[i]->sealHostWrites();
            const auto resource = registry->find(buffers[i]->id());
            if (!resource || !resource->buffer)
                return fail("RHI-METAL-CANONICAL-PRODUCER-READBACK-RESOURCE");
            resources[i] = *resource;
            const auto resident = buffers[i]->allocation().estimatedSizeBytes;
            if (resident < bytes || resident > budget.maxNewBytes - owned)
                return fail("RHI-METAL-CANONICAL-PRODUCER-READBACK-BUDGET");
            owned += resident;
        }
        rhi::CanonicalZeroBaseIndirectProducerRecordingAdmission admission;
        if (const auto* id = rhi::CanonicalZeroBaseIndirectProducerRecordingAdmission::capture(
                inputs(cold, buffers), budget.values, command->recordingIdentity(), admission))
            return fail(id);
        auto root =
            core::makeShared<MetalRhiCanonicalProducerRecordingPreparation>(ConstructionKey{},
                std::move(cold),
                command,
                std::move(buffers),
                std::move(resources),
                std::move(lease),
                std::move(admission),
                budget,
                owned);
        if (root->childOwnerCount() != expectedOwners)
            return fail("RHI-METAL-CANONICAL-PRODUCER-PROJECTION-MISMATCH");
        if (const auto* id = root->validate())
            return fail(id);
        return root;
    } catch (const std::bad_alloc&) {
        core::CpuMemoryQuotaScope diagnosticScope(nullptr);
        return fail("RHI-METAL-CANONICAL-PRODUCER-PREPARATION-ALLOCATION-FAILED");
    }
}

MetalRhiClosedConsumerRecordingPreparation::MetalRhiClosedConsumerRecordingPreparation(
    ConstructionKey,
    std::shared_ptr<const Issuance> root,
    const std::shared_ptr<MetalRhiNativeCommandPayload>& command,
    Buffers buffers,
    Resources resources,
    rhi::CanonicalClosedConsumerRecordingAdmission admission,
    Budget budget,
    std::uint64_t bytes)
    : root_(std::move(root)), command_(command), retention_(command->recordingRetention_.get()),
      registry_(command->resourceRegistry_), quota_(command->recordingRetention_->quota()),
      buffers_(std::move(buffers)), resources_(std::move(resources)),
      admission_(std::move(admission)), budget_(budget), ownedBytes_(bytes)
{
    commandOwners_ = {command->nativeCommandBuffer_, command->nativeDevice_, command->stats_};
    for (std::size_t i = 0u; i < resources_.size(); ++i)
        observations_[i] = MetalRhiNativeResourceObservation(resources_[i].buffer->resource);
    owners_.capture([&](auto&& visit) {
        projectOwners(visit);
    });
}

const char* MetalRhiClosedConsumerRecordingPreparation::validateCommand(
    const Issuance& root, const MetalRhiNativeCommandPayload& command) noexcept
{
    if (const auto* id = Builder::Producer::validateCommand(*root.cold(), command))
        return id;
    const auto& source = root.cold()->producer()->packet()->pipeline()->source();
    if (!core::SharedOwnerProjection::sameOwner(
            command.resourceRegistry_, source->index()->registry())
        || !core::SharedOwnerProjection::sameOwner(
            command.recordingRetention_->quota(), source->quota()))
        return "RHI-METAL-CLOSED-CONSUMER-COMMAND-ISSUER";
    if (command.nativeRenderEncoder_ || command.nativeComputeEncoder_ || command.nativeBlitEncoder_
        || command.stats_->nativeDispatchEncoded || command.stats_->nativeDrawEncoded
        || command.stats_->nativeIndexedDrawEncoded || command.stats_->nativeBlitCopyEncoded
        || command.canonicalProducer_ || command.indirectExecution_
        || command.recordingRetention_->resourceOwnerCount())
        return "RHI-METAL-CLOSED-CONSUMER-COMMAND-NOT-COLD";
    return nullptr;
}

const char* MetalRhiClosedConsumerRecordingPreparation::validate() const noexcept
{
    return validateStorage(Contract::StoragePhase::Cold, false);
}

const char* MetalRhiClosedConsumerRecordingPreparation::validateStorage(
    Contract::StoragePhase phase, bool finished) const noexcept
{
    if (phase != Contract::StoragePhase::Cold && phase != Contract::StoragePhase::ReadbackReady)
        return "RHI-CLOSED-CONSUMER-RECORDING-STORAGE-PHASE";
    const bool recorded = phase == Contract::StoragePhase::ReadbackReady;
    const auto command = command_.lock();
    if (!root_ || !root_->sharedOwnerMatches(root_) || !command
        || !command->sharedOwnerMatches(command))
        return "RHI-METAL-CLOSED-CONSUMER-COMMAND-ORIGINAL-OWNER";
    const auto* rootId = recorded ? root_->validateClosedReadbackReady() : root_->validate();
    if (rootId)
        return rootId;
    if (recorded) {
        if (const auto* id = Builder::Producer::validateCommand(*root_->cold(), *command, finished))
            return id;
        if (!command->closedConsumerBudget_ || !command->closedConsumerLowering_
            || command->closedConsumerLowering_->contractVersion != Contract::version
            || !command->closedConsumerLowering_->allowsPrototypePayload()
            || !command->closedConsumerIndirectNative_ || !command->closedConsumerNative_
            || command->closedConsumerNative_->reservation()->preparation().get() != this
            || !sharedOwnerMatches(command->closedConsumerNative_->reservation()->preparation())
            || !command->closedConsumerSequence_ || command->closedConsumerSequence_->finish()
            || command->closedConsumerSequence_->status()
                != rhi::GpuIndirectOutputLifetimeContract::Status::Recorded
            || command->nativeRenderEncoder_ || command->nativeComputeEncoder_
            || command->nativeBlitEncoder_
            || command->stats_->closedConsumerCommandCount != Contract::commandUnits
            || !command->stats_->nativeDispatchEncoded || !command->stats_->nativeIndexedDrawEncoded
            || !command->stats_->nativeBlitCopyEncoded)
            return "RHI-METAL-CLOSED-CONSUMER-RECORDED-COMMAND";
        if (const auto* id = command->closedConsumerBudget_->validate())
            return id;
    } else if (const auto* id = validateCommand(*root_, *command))
        return id;
    if (!core::SharedOwnerProjection::sameOwner(
            commandOwners_[0u].lock(), command->nativeCommandBuffer_)
        || !core::SharedOwnerProjection::sameOwner(
            commandOwners_[1u].lock(), command->nativeDevice_)
        || !core::SharedOwnerProjection::sameOwner(commandOwners_[2u].lock(), command->stats_)
        || retention_ != command->recordingRetention_.get()
        || !core::SharedOwnerProjection::sameOwner(registry_.lock(), command->resourceRegistry_)
        || !core::SharedOwnerProjection::sameOwner(
            quota_.lock(), command->recordingRetention_->quota()))
        return "RHI-METAL-CLOSED-CONSUMER-COMMAND-IDENTITY";
    if (const auto* id = budget_.values.validate())
        return id;
    if (budget_.maxNewBytes < minimumNewBytes || !budget_.maxChildOwners
        || owners_.size() > budget_.maxChildOwners)
        return "RHI-METAL-CLOSED-CONSUMER-ADMISSION-BUDGET";
    const auto& cold = root_->cold();
    const auto& source = cold->producer()->packet()->pipeline()->source();
    const auto& device = root_->images()->geometry()->pipeline()->native()->nativeDevice;
    std::uint64_t owned = 0u;
    const auto aliases = [&](const auto& resource, const auto& other) {
        return resource.buffer->resource.nativeObject == other.buffer->resource.nativeObject;
    };
    for (std::size_t i = 0u; i < buffers_.size(); ++i) {
        if (!buffers_[i] || !buffers_[i]->sharedOwnerMatches(buffers_[i]))
            return "RHI-METAL-CLOSED-CONSUMER-READBACK-ORIGINAL-OWNER";
        const auto bytes =
            i ? sizeof(rhi::IndirectInstanceBase) : sizeof(rhi::DrawIndexedIndirectCommand);
        const rhi::BufferDesc descriptor{"",
            bytes,
            rhi::ResourceUsage::CopyDst,
            rhi::MemoryUsage::GpuToCpu,
            rhi::ResourceState::CopyDst};
        if (const auto* id = MetalRhiSealedBufferUpload::validateRegistered(buffers_[i],
                source->index()->registry(),
                resources_[i],
                observations_[i],
                device,
                descriptor,
                metalRhiNativeStorageMode(MetalRhiMemoryPolicy::ManagedReadback),
                false,
                false,
                closedAdmissionDiagnostics))
            return id;
        if (buffers_[i]->state() != rhi::ResourceState::CopyDst)
            return "RHI-METAL-CLOSED-CONSUMER-READBACK-STATE";
        const auto resident = buffers_[i]->allocation().estimatedSizeBytes;
        if (resident < bytes || resident > budget_.maxNewBytes - owned)
            return "RHI-METAL-CLOSED-CONSUMER-READBACK-BUDGET";
        owned += resident;
        for (const auto& other : cold->producer()->resources())
            if (aliases(resources_[i], other))
                return "RHI-METAL-CLOSED-CONSUMER-READBACK-NATIVE-ALIAS";
        for (const auto& other : cold->resources())
            if (aliases(resources_[i], other))
                return "RHI-METAL-CLOSED-CONSUMER-READBACK-NATIVE-ALIAS";
        for (const auto& other : cold->producer()->packet()->resources())
            if (aliases(resources_[i], other))
                return "RHI-METAL-CLOSED-CONSUMER-READBACK-NATIVE-ALIAS";
        if (aliases(resources_[i], cold->producer()->readback())
            || aliases(resources_[i], cold->readback())
            || aliases(resources_[i], root_->images()->geometry()->resource())
            || resources_[i].buffer->resource.nativeObject
                == source->sourceBuffer()->resource.nativeObject)
            return "RHI-METAL-CLOSED-CONSUMER-READBACK-NATIVE-ALIAS";
        for (std::size_t j = 0u; j < i; ++j)
            if (aliases(resources_[i], resources_[j]))
                return "RHI-METAL-CLOSED-CONSUMER-READBACK-NATIVE-ALIAS";
    }
    Contract::Inputs inputs;
    Contract::Facts facts;
    const auto* inputId = recorded
        ? Builder::observeClosedReadbackReady(
              root_, buffers_, budget_.values, command->recordingIdentity(), inputs, facts)
        : Builder::capture(
              root_, buffers_, budget_.values, command->recordingIdentity(), inputs, facts);
    if (inputId)
        return inputId;
    if (const auto* id = admission_.validate(inputs, command->recordingIdentity(), phase))
        return id;
    const auto faultBytes =
        cold->producer()->faultDomain()->faultReadbackBuffer()->allocation().estimatedSizeBytes
        + cold->faultDomain()->faultReadbackBuffer()->allocation().estimatedSizeBytes;
    if (owned != ownedBytes_ || facts.actualReadbackBytes < faultBytes
        || owned != facts.actualReadbackBytes - faultBytes || !owners_.matches([&](auto&& visit) {
               projectOwners(visit);
           }))
        return "RHI-METAL-CLOSED-CONSUMER-ADMISSION-PROJECTION";
    return nullptr;
}

std::shared_ptr<const MetalRhiClosedConsumerRecordingPreparation>
MetalRhiClosedConsumerRecordingPreparation::prepare(std::shared_ptr<const Issuance> root,
    rhi::GpuDevice& device,
    const std::shared_ptr<MetalRhiNativeCommandPayload>& command,
    Budget budget,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    const auto fail =
        [&](const char* id) -> std::shared_ptr<const MetalRhiClosedConsumerRecordingPreparation> {
        diagnostic = id;
        return {};
    };
    if (!root || !root->sharedOwnerMatches(root) || !command
        || !command->sharedOwnerMatches(command))
        return fail("RHI-METAL-CLOSED-CONSUMER-COMMAND-ORIGINAL-OWNER");
    if (const auto* id = root->validate())
        return fail(id);
    if (const auto* id = validateCommand(*root, *command))
        return fail(id);
    if (const auto* id = budget.values.validate())
        return fail(id);
    if (budget.maxNewBytes < minimumNewBytes || !budget.maxChildOwners)
        return fail("RHI-METAL-CLOSED-CONSUMER-ADMISSION-BUDGET");
    const auto& source = root->cold()->producer()->packet()->pipeline()->source();
    const auto& registry = source->index()->registry();
    if (device.backend() != rhi::Backend::Metal
        || !core::SharedOwnerProjection::sameOwner(
            registry, metalRhiIssuingResourceRegistry(device)))
        return fail("RHI-METAL-CLOSED-CONSUMER-ADMISSION-ISSUER");
    const auto expectedOwners = 1u + root->childOwnerCount() + Contract::referenceSlots
        + Buffers{}.size() * (1u + MetalRhiResourceOwnerProjection::independentBufferOwnerCount);
    if (expectedOwners > budget.maxChildOwners)
        return fail("RHI-METAL-CLOSED-CONSUMER-ADMISSION-CHILD-OWNER-BUDGET");
    try {
        core::CpuMemoryQuotaScope scope(source->quota());
        Buffers buffers;
        Resources resources;
        std::uint64_t owned = 0u;
        for (std::size_t i = 0u; i < buffers.size(); ++i) {
            const auto bytes =
                i ? sizeof(rhi::IndirectInstanceBase) : sizeof(rhi::DrawIndexedIndirectCommand);
            buffers[i] = core::shareUnique(device.createBuffer({"ClosedConsumerReadback",
                bytes,
                rhi::ResourceUsage::CopyDst,
                rhi::MemoryUsage::GpuToCpu,
                rhi::ResourceState::CopyDst}));
            if (!buffers[i])
                return fail("RHI-METAL-CLOSED-CONSUMER-READBACK-CREATION-FAILED");
            buffers[i]->sealHostWrites();
            const auto resource = registry->find(buffers[i]->id());
            if (!resource || !resource->buffer)
                return fail("RHI-METAL-CLOSED-CONSUMER-READBACK-RESOURCE");
            resources[i] = *resource;
            const auto resident = buffers[i]->allocation().estimatedSizeBytes;
            if (resident < bytes || resident > budget.maxNewBytes - owned)
                return fail("RHI-METAL-CLOSED-CONSUMER-READBACK-BUDGET");
            owned += resident;
        }
        Contract::Inputs inputs;
        Contract::Facts facts;
        if (const auto* id = Builder::capture(
                root, buffers, budget.values, command->recordingIdentity(), inputs, facts))
            return fail(id);
        rhi::CanonicalClosedConsumerRecordingAdmission admission;
        if (const auto* id = rhi::CanonicalClosedConsumerRecordingAdmission::capture(
                inputs, budget.values, command->recordingIdentity(), admission))
            return fail(id);
        auto result =
            core::makeShared<MetalRhiClosedConsumerRecordingPreparation>(ConstructionKey{},
                std::move(root),
                command,
                std::move(buffers),
                std::move(resources),
                std::move(admission),
                budget,
                owned);
        if (result->childOwnerCount() != expectedOwners)
            return fail("RHI-METAL-CLOSED-CONSUMER-ADMISSION-PROJECTION");
        if (const auto* id = result->validate())
            return fail(id);
        return result;
    } catch (const std::bad_alloc&) {
        core::CpuMemoryQuotaScope diagnosticScope(nullptr);
        return fail("RHI-METAL-CLOSED-CONSUMER-ADMISSION-ALLOCATION-FAILED");
    }
}

const char* MetalRhiClosedConsumerRecordingReservation::validate() const noexcept
{
    return validateStorage(false, false);
}

const char* MetalRhiClosedConsumerRecordingReservation::validateRecorded(
    bool finished) const noexcept
{
    return validateStorage(true, finished);
}

const char* MetalRhiClosedConsumerRecordingReservation::validateStorage(
    bool recorded, bool finished) const noexcept
{
    if (status() != (recorded ? Status::Recorded : Status::Cold))
        return "RHI-METAL-CLOSED-CONSUMER-RESERVATION-TERMINAL";
    if (!preparation_ || !preparation_->sharedOwnerMatches(preparation_))
        return "RHI-METAL-CLOSED-CONSUMER-RESERVATION-ORIGINAL-OWNER";
    if (const auto* id = preparation_->validateStorage(recorded
                ? Preparation::Contract::StoragePhase::ReadbackReady
                : Preparation::Contract::StoragePhase::Cold,
            finished))
        return id;
    const auto command = preparation_->command_.lock();
    if (!command || command->closedConsumerReservation_.get() != this
        || !sharedOwnerMatches(command->closedConsumerReservation_))
        return "RHI-METAL-CLOSED-CONSUMER-RESERVATION-COMMAND";
    const auto& issuance = preparation_->issuance();
    const auto& packet = issuance->packet();
    if (!budget_.prototypeOptIn || budget_.contractVersion != Preparation::Contract::version
        || !budget_.maxChildOwners || owners_.size() > budget_.maxChildOwners)
        return "RHI-METAL-CLOSED-CONSUMER-RESERVATION-BUDGET";
    if (!packetLease_.charged() || packetLease_.units() != 1u
        || packetBudget_.get() != &packet->recordings_
        || !core::SharedOwnerProjection::sameOwner(packetBudget_,
            std::shared_ptr<core::AtomicCapacityBudget>(packet, &packet->recordings_))
        || !producerLease_.valid() || !consumerLease_.valid()
        || producerLease_.faultLatchBuffer()
            != issuance->cold()->producer()->faultDomain()->faultLatchBuffer()
        || consumerLease_.faultLatchBuffer() != issuance->cold()->faultDomain()->faultLatchBuffer())
        return "RHI-METAL-CLOSED-CONSUMER-RESERVATION-LEASE";
    return owners_.matches([&](auto&& visit) {
        projectOwners(visit);
    })
        ? nullptr
        : "RHI-METAL-CLOSED-CONSUMER-RESERVATION-PROJECTION";
}

void MetalRhiClosedConsumerRecordingReservation::cancel(bool failure) const noexcept
{
    auto expected = status();
    while (expected == Status::Cold || expected == Status::Recorded) {
        if (status_.compare_exchange_weak(expected,
                failure ? Status::Failed : Status::Abandoned,
                std::memory_order_acq_rel)) {
            packetLease_.release();
            producerLease_.release();
            consumerLease_.release();
            return;
        }
    }
}

void MetalRhiNativeCommandPayload::cancelClosedConsumerReservation(bool failure) noexcept
{
    if (closedConsumerNative_) {
        closedConsumerNative_->states()->rollback();
        for (const auto& slot : closedConsumerNative_->slots())
            if (slot)
                slot->reset();
        if (closedConsumerSequence_)
            (void)closedConsumerSequence_->transition(failure
                    ? rhi::GpuIndirectOutputLifetimeContract::Status::Failed
                    : rhi::GpuIndirectOutputLifetimeContract::Status::Abandoned,
                closedConsumerNative_->reservation()->preparation()->facts().identity);
    }
    if (closedConsumerReservation_)
        closedConsumerReservation_->cancel(failure);
    closedConsumerNative_.reset();
    closedConsumerReservation_.reset();
}

std::shared_ptr<const MetalRhiClosedConsumerRecordingReservation>
MetalRhiClosedConsumerRecordingReservation::prepare(
    std::shared_ptr<const Preparation> preparation, Budget budget, core::pmr::String& diagnostic)
{
    diagnostic.clear();
    const auto fail =
        [&](const char* id) -> std::shared_ptr<const MetalRhiClosedConsumerRecordingReservation> {
        diagnostic = id;
        return {};
    };
    if (!preparation || !preparation->sharedOwnerMatches(preparation))
        return fail("RHI-METAL-CLOSED-CONSUMER-RESERVATION-ORIGINAL-OWNER");
    if (const auto* id = preparation->validate())
        return fail(id);
    if (!budget.prototypeOptIn || budget.contractVersion != Preparation::Contract::version
        || !budget.maxChildOwners)
        return fail("RHI-METAL-CLOSED-CONSUMER-RESERVATION-BUDGET");
    const auto expectedOwners = 1u + preparation->childOwnerCount() + reservationReferenceSlots;
    if (expectedOwners > budget.maxChildOwners)
        return fail("RHI-METAL-CLOSED-CONSUMER-RESERVATION-CHILD-OWNER-BUDGET");
    const auto command = preparation->command_.lock();
    if (command->closedConsumerReservation_)
        return fail("RHI-METAL-CLOSED-CONSUMER-COMMAND-RESERVATION-EXISTS");
    const auto& issuance = preparation->issuance();
    const auto& packet = issuance->packet();
    if (!packet->prototypeClosedConsumerCold() || packet->consumerPlan().success
        || packet->consumerPlan().executable || packet->consumerPlan().identity)
        return fail("RHI-METAL-CLOSED-CONSUMER-RESERVATION-PACKET-PURPOSE");
    try {
        core::CpuMemoryQuotaScope scope(
            issuance->cold()->producer()->packet()->pipeline()->source()->quota());
        auto packetBudget =
            std::shared_ptr<core::AtomicCapacityBudget>(packet, &packet->recordings_);
        auto root = core::makeShared<MetalRhiClosedConsumerRecordingReservation>(
            ConstructionKey{}, std::move(preparation), std::move(packetBudget), budget);
        if (!root->packetLease_.charged())
            return fail("RHI-BINDLESS-INDIRECT-PACKET-IN-FLIGHT");
        root->producerLease_ = rhi::BindlessFaultSubmissionLease::acquire(
            issuance->cold()->producer()->faultDomain(), diagnostic);
        if (!root->producerLease_.valid())
            return {};
        root->consumerLease_ =
            rhi::BindlessFaultSubmissionLease::acquire(issuance->cold()->faultDomain(), diagnostic);
        if (!root->consumerLease_.valid())
            return {};
        root->owners_.capture([&](auto&& visit) {
            root->projectOwners(visit);
        });
        if (root->childOwnerCount() != expectedOwners)
            return fail("RHI-METAL-CLOSED-CONSUMER-RESERVATION-PROJECTION");
        command->closedConsumerReservation_ = root;
        if (const auto* id = root->validate()) {
            command->cancelClosedConsumerReservation(true);
            return fail(id);
        }
        return root;
    } catch (const std::bad_alloc&) {
        core::CpuMemoryQuotaScope diagnosticScope(nullptr);
        return fail("RHI-METAL-CLOSED-CONSUMER-RESERVATION-ALLOCATION-FAILED");
    }
}

const char* MetalRhiZeroBaseIndirectConsumerColdBindings::validateRoots(
    const std::shared_ptr<const Producer>& producer,
    const std::shared_ptr<const Pipeline>& pipeline) noexcept
{
    if (!producer || !producer->sharedOwnerMatches(producer) || !pipeline
        || !pipeline->sharedOwnerMatches(pipeline))
        return "RHI-METAL-CONSUMER-COLD-OWNER-MISMATCH";
    if (const auto* id = producer->validate())
        return id;
    if (const auto* id = pipeline->validate())
        return id;
    if (!core::SharedOwnerProjection::sameOwner(
            producer->packet()->pipeline()->source(), pipeline->source()))
        return "RHI-METAL-CONSUMER-COLD-ORIGINAL-SOURCE";
    return nullptr;
}

const char* MetalRhiZeroBaseIndirectConsumerColdBindings::validate() const noexcept
{
    return validateForProducerState(rhi::ResourceState::ShaderWrite);
}

const char* MetalRhiZeroBaseIndirectConsumerColdBindings::validateForProducerState(
    rhi::ResourceState state) const noexcept
{
    return validateStorage(state, Contract::StoragePhase::Cold);
}

const char*
MetalRhiZeroBaseIndirectConsumerColdBindings::validateClosedReadbackReady() const noexcept
{
    return validateStorage(rhi::ResourceState::CopySrc, Contract::StoragePhase::ReadbackReady);
}

const char* MetalRhiZeroBaseIndirectConsumerColdBindings::validateStorage(
    rhi::ResourceState state, Contract::StoragePhase phase) const noexcept
{
    if (phase != Contract::StoragePhase::Cold && phase != Contract::StoragePhase::ReadbackReady)
        return "RHI-CANONICAL-CONSUMER-STORAGE-PHASE";
    const bool ready = phase == Contract::StoragePhase::ReadbackReady;
    if (!producer_ || !producer_->sharedOwnerMatches(producer_) || !pipeline_
        || !pipeline_->sharedOwnerMatches(pipeline_))
        return "RHI-METAL-CONSUMER-COLD-OWNER-MISMATCH";
    const auto* producerId =
        ready ? producer_->validateClosedReadbackReady() : producer_->validateForState(state);
    if (producerId)
        return producerId;
    if (const auto* id = pipeline_->validate())
        return id;
    if (!core::SharedOwnerProjection::sameOwner(
            producer_->packet()->pipeline()->source(), pipeline_->source()))
        return "RHI-METAL-CONSUMER-COLD-ORIGINAL-SOURCE";
    if (!fault_ || !fault_->sharedOwnerMatches(fault_))
        return "RHI-METAL-CONSUMER-COLD-OWNER-MISMATCH";
    if (const auto* id = Contract::validateBudget(budget_, ownedBytes_, owners_.size()))
        return id;
    Contract::Images inventory;
    for (std::size_t i = 0u; i < images_.size(); ++i) {
        if (const auto* id = images_[i].validate())
            return id;
        inventory[i] = images_[i].values();
    }
    const auto& source = pipeline_->source();
    const auto& packet = producer_->packet()->packet();
    Contract::Facts current;
    if (const auto* id = Contract::verify(*pipeline_->package(),
            source->content(),
            *packet,
            *packet->instanceMappings(),
            inventory,
            producer_->faultDomain(),
            fault_,
            current,
            phase))
        return id;
    if (current.contractVersion != facts_.contractVersion
        || current.consumerPackageIdentity != facts_.consumerPackageIdentity
        || current.packetGeneration != facts_.packetGeneration
        || current.producerFaultGeneration != facts_.producerFaultGeneration
        || current.consumerFaultGeneration != facts_.consumerFaultGeneration
        || current.mapping != facts_.mapping || current.words != facts_.words
        || current.bytes != facts_.bytes)
        return "RHI-METAL-CONSUMER-COLD-METADATA-GENERATION";
    for (std::size_t i = 0u; i < inventory.size(); ++i)
        if (!inventory[i].matches(facts_.images[i]))
            return "RHI-METAL-CONSUMER-COLD-IMAGE-INVENTORY";
    Consumer::Facts roles;
    if (const auto* id = Consumer::verify(*pipeline_->package(), source->content(), roles))
        return id;
    const auto& registry = source->index()->registry();
    const auto& device = pipeline_->native()->nativeDevice;
    const std::array<rhi::GpuBuffer*, 3u> logical{
        packet->instanceMappings(), metadata_.buffer.get(), fault_->faultLatchBuffer()};
    for (std::size_t i = 0u; i < logical.size(); ++i) {
        const auto& binding = buffers_[i];
        const auto& role = roles.bindings[i];
        if (!logical[i] || binding.resource != logical[i]->id() || binding.set != role.set
            || binding.binding != role.binding || binding.range.offset
            || !binding.range.fact.matches(pipeline_->facts().buffers[i]))
            return "RHI-METAL-CONSUMER-COLD-BINDING-MISMATCH";
        const auto& stage = pipeline_->native()->graphicsBufferFacts[i ? 1u : 0u];
        if (const auto* id = MetalRhiStageBufferRangeValidator::validate(stage,
                role.stages,
                binding.range.fact.slot,
                0u,
                logical[i]->desc().sizeBytes,
                binding.range))
            return id;
        const auto found = registry->find(logical[i]->id());
        if (!found || !core::SharedOwnerProjection::sameOwner(found->buffer, resources_[i].buffer)
            || !core::SharedOwnerProjection::sameOwner(
                found->buffer, metalRhiNativePayloadFor(*logical[i]))
            || !resources_[i].buffer
            || !binding.range.nativeIdentity.matches(resources_[i].buffer->resource))
            return "RHI-METAL-CONSUMER-COLD-RESOURCE-MISMATCH";
        for (std::size_t prior = 0u; prior < i; ++prior)
            if (resources_[prior].buffer->resource.nativeObject
                == resources_[i].buffer->resource.nativeObject)
                return "RHI-METAL-CONSUMER-COLD-NATIVE-ALIAS";
    }
    if (!core::SharedOwnerProjection::sameOwner(
            resources_[0u].buffer, producer_->packet()->resources()[1u].buffer)
        || !core::SharedOwnerProjection::sameOwner(
            resources_[1u].buffer, metadata_.resource.buffer))
        return "RHI-METAL-CONSUMER-COLD-ORIGINAL-MAPPING";
    if (metadata_.buffer->state() != rhi::ResourceState::ShaderRead)
        return "RHI-METAL-CONSUMER-COLD-STATE";
    if (const auto* id = Upload::validate(metadata_.buffer,
            registry,
            resources_[1u],
            buffers_[1u].range.nativeIdentity,
            device,
            sizeof(Contract::Bytes),
            Upload::Domain::ImmutableStorage,
            consumerColdDiagnostics))
        return id;
    std::uint64_t faultBytes = 0u;
    for (const auto role : {rhi::BindlessOwnedFaultDomain::Role::Latch,
             rhi::BindlessOwnedFaultDomain::Role::Readback}) {
        const bool latch = role == rhi::BindlessOwnedFaultDomain::Role::Latch;
        const auto* buffer = latch ? fault_->faultLatchBuffer() : fault_->faultReadbackBuffer();
        const auto spec = rhi::BindlessOwnedFaultDomain::descriptor(role);
        const auto bytes = buffer->allocation().estimatedSizeBytes;
        if (!bytes || faultBytes > fault_->reportedResidentBytes()
            || bytes > fault_->reportedResidentBytes() - faultBytes)
            return "RHI-METAL-CONSUMER-COLD-RESIDENT-BYTES";
        faultBytes += bytes;
        if (buffer->state() != (ready && latch ? rhi::ResourceState::CopySrc : spec.initialState))
            return "RHI-METAL-CONSUMER-COLD-STATE";
        if (const auto* id = Upload::validateRegistered(
                std::shared_ptr<rhi::GpuBuffer>(fault_, const_cast<rhi::GpuBuffer*>(buffer)),
                registry,
                latch ? resources_[2u] : readback_,
                latch ? buffers_[2u].range.nativeIdentity : readbackObservation_,
                device,
                spec,
                metalRhiNativeStorageMode(latch ? MetalRhiMemoryPolicy::PrivateStorage
                                                : MetalRhiMemoryPolicy::ManagedReadback),
                false,
                latch,
                consumerColdDiagnostics))
            return id;
    }
    const auto metadataBytes = metadata_.buffer->allocation().estimatedSizeBytes;
    if (faultBytes != fault_->reportedResidentBytes() || faultBytes > ownedBytes_
        || metadataBytes != ownedBytes_ - faultBytes)
        return "RHI-METAL-CONSUMER-COLD-RESIDENT-BYTES";
    for (std::size_t i = 0u; i < images_.size(); ++i) {
        const auto& binding = textures_[i];
        const auto& role = roles.bindings[3u + i];
        if (binding.resource != inventory[i].view || binding.set != role.set
            || binding.binding != role.binding
            || !binding.abi.matches(pipeline_->facts().textures[i])
            || binding.abi.stage != role.stages)
            return "RHI-METAL-CONSUMER-COLD-IMAGE-BINDING";
    }
    if (!owners_.matches([&](auto&& visit) {
            projectOwners(visit);
        }))
        return "RHI-METAL-CONSUMER-COLD-OWNER-MISMATCH";
    return nullptr;
}

std::shared_ptr<const MetalRhiZeroBaseIndirectConsumerColdBindings>
MetalRhiZeroBaseIndirectConsumerColdBindings::prepare(std::shared_ptr<const Producer> producer,
    std::shared_ptr<const Pipeline> pipeline,
    rhi::GpuDevice& device,
    const ImageIds& imageIds,
    Budget budget,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    const auto fail =
        [&](const char* id) -> std::shared_ptr<const MetalRhiZeroBaseIndirectConsumerColdBindings> {
        diagnostic = id;
        return {};
    };
    if (const auto* id = validateRoots(producer, pipeline))
        return fail(id);
    if (budget.contractVersion != Contract::version || !budget.prototypeOptIn
        || !budget.maxOwnedBytes || !budget.maxChildOwners)
        return fail("RHI-CANONICAL-CONSUMER-COLD-RESOURCES-BUDGET");
    if (budget.maxOwnedBytes < Contract::minimumOwnedBytes)
        return fail("RHI-CANONICAL-CONSUMER-COLD-RESOURCES-BYTE-BUDGET");
    const auto decision = Contract::lowering(device.backend());
    if (!decision.prototypeColdResources)
        return fail(decision.diagnosticId);
    const auto& source = pipeline->source();
    const auto& registry = source->index()->registry();
    const auto& nativeDevice = pipeline->native()->nativeDevice;
    if (!core::SharedOwnerProjection::sameOwner(registry, metalRhiIssuingResourceRegistry(device)))
        return fail("RHI-METAL-CONSUMER-COLD-ISSUER-MISMATCH");
    Images images;
    Contract::Images inventory;
    for (std::size_t i = 0u; i < images.size(); ++i) {
        if (const auto* id = MetalRhiTextureInventoryObservation::capture(
                registry, nativeDevice, imageIds[i], images[i]))
            return fail(id);
        inventory[i] = images[i].values();
    }
    rhi::BindlessTextureInventoryMetadataSource metadataSource(
        pipeline->package()->artifact().tables.front(), inventory.data(), inventory.size());
    if (const auto* id = metadataSource.diagnostic())
        return fail(id);
    std::size_t requiredOwners = 0u;
    projectBorrowed(producer, pipeline, images, [&](const std::shared_ptr<const void>& owner) {
        if (owner)
            ++requiredOwners;
    });
    // Exactly three independent buffers: one wrapper + four native owners per
    // role, plus the factory-owned fault domain. No guessed total owner count.
    requiredOwners += 1u + 3u * (1u + MetalRhiResourceOwnerProjection::independentBufferOwnerCount);
    if (requiredOwners > budget.maxChildOwners)
        return fail("RHI-CANONICAL-CONSUMER-COLD-RESOURCES-OWNER-BUDGET");
    try {
        core::CpuMemoryQuotaScope scope(source->quota());
        rhi::BindlessFaultDomainDesc faultDesc;
        faultDesc.maxResidentBytes = rhi::BindlessOwnedFaultDomain::minimumBytes;
        auto fault = rhi::BindlessOwnedFaultDomain::create(device, faultDesc, diagnostic);
        if (!fault)
            return {};
        Contract::Facts facts;
        const auto& packet = producer->packet()->packet();
        if (const auto* id = Contract::verify(*pipeline->package(),
                source->content(),
                *packet,
                *packet->instanceMappings(),
                inventory,
                producer->faultDomain(),
                fault,
                facts))
            return fail(id);
        core::ByteBuffer bytes(
            facts.bytes.begin(), facts.bytes.end(), &core::defaultCpuMemoryResource());
        auto uploaded = Upload::prepare(device,
            registry,
            nativeDevice,
            bytes,
            Upload::Domain::ImmutableStorage,
            consumerColdDiagnostics,
            diagnostic);
        if (!uploaded)
            return {};
        const auto faultBytes = fault->reportedResidentBytes(),
                   metadataBytes = uploaded->buffer->allocation().estimatedSizeBytes;
        if (faultBytes > budget.maxOwnedBytes || metadataBytes > budget.maxOwnedBytes - faultBytes)
            return fail("RHI-CANONICAL-CONSUMER-COLD-RESOURCES-BYTE-BUDGET");
        const auto ownedBytes = faultBytes + metadataBytes;
        if (const auto* id = Contract::validateBudget(budget, ownedBytes, requiredOwners))
            return fail(id);
        Resources resources;
        MetalRhiRegisteredResource readback;
        resources[0u] = producer->packet()->resources()[1u];
        resources[1u] = uploaded->resource;
        const auto latch = registry->find(fault->faultLatchBuffer()->id()),
                   read = registry->find(fault->faultReadbackBuffer()->id());
        if (!latch || !latch->buffer || !read || !read->buffer)
            return fail("RHI-METAL-CONSUMER-COLD-RESOURCE-MISMATCH");
        resources[2u] = *latch;
        readback = *read;
        Consumer::Facts roles;
        if (const auto* id = Consumer::verify(*pipeline->package(), source->content(), roles))
            return fail(id);
        Buffers buffers;
        Textures textures;
        for (std::size_t i = 0u; i < buffers.size(); ++i) {
            const auto& role = roles.bindings[i];
            auto& binding = buffers[i];
            binding.resource = i == 0u ? packet->instanceMappings()->id()
                : i == 1u              ? uploaded->buffer->id()
                                       : fault->faultLatchBuffer()->id();
            binding.set = role.set;
            binding.binding = role.binding;
            if (const auto* id = MetalRhiStageBufferRangeValidator::capture(
                    pipeline->native()->graphicsBufferFacts[i ? 1u : 0u],
                    role.stages,
                    pipeline->facts().buffers[i].slot,
                    0u,
                    role.requiredBytes,
                    binding.range))
                return fail(id);
            binding.range.nativeIdentity =
                MetalRhiNativeResourceObservation(resources[i].buffer->resource);
        }
        for (std::size_t i = 0u; i < textures.size(); ++i)
            textures[i] = {inventory[i].view,
                roles.bindings[3u + i].set,
                roles.bindings[3u + i].binding,
                pipeline->facts().textures[i]};
        auto root =
            core::makeShared<MetalRhiZeroBaseIndirectConsumerColdBindings>(ConstructionKey{},
                std::move(producer),
                std::move(pipeline),
                std::move(fault),
                std::move(*uploaded),
                std::move(images),
                std::move(resources),
                std::move(readback),
                facts,
                std::move(buffers),
                textures,
                budget,
                ownedBytes);
        if (root->childOwnerCount() != requiredOwners)
            return fail("RHI-METAL-CONSUMER-COLD-OWNER-PROJECTION");
        if (const auto* id = root->validate())
            return fail(id);
        return root;
    } catch (const std::bad_alloc&) {
        core::CpuMemoryQuotaScope diagnosticScope(nullptr);
        return fail("RHI-METAL-CONSUMER-COLD-ALLOCATION-FAILED");
    }
}

const char* MetalRhiZeroBaseIndirectColdBindings::validate() const noexcept
{
    return validateForState(rhi::ResourceState::ShaderWrite);
}

const char* MetalRhiZeroBaseIndirectColdBindings::validateForState(
    rhi::ResourceState outputState) const noexcept
{
    return validateStorage(outputState, false);
}

const char* MetalRhiZeroBaseIndirectColdBindings::validateClosedReadbackReady() const noexcept
{
    return validateStorage(rhi::ResourceState::CopySrc, true);
}

const char* MetalRhiZeroBaseIndirectColdBindings::validateStorage(
    rhi::ResourceState outputState, bool closedReady) const noexcept
{
    if (consumed_.load(std::memory_order_acquire))
        return "RHI-METAL-CANONICAL-PRODUCER-GENERATION-CONSUMED";
    if (outputState != rhi::ResourceState::ShaderWrite
        && outputState != rhi::ResourceState::CopySrc)
        return "RHI-METAL-CANONICAL-PRODUCER-OUTPUT-STATE";
    if (!packet_ || !packet_->sharedOwnerMatches(packet_) || !fault_
        || !fault_->sharedOwnerMatches(fault_))
        return "RHI-METAL-ZERO-BASE-COLD-OWNER-MISMATCH";
    const auto* packetId =
        closedReady ? packet_->validateClosedReadbackReady() : packet_->validate();
    if (packetId)
        return packetId;
    if (const auto* id = fault_->storageDiagnostic())
        return id;
    if (budget_.contractVersion != Contract::version || !budget_.prototypeOptIn
        || !budget_.maxChildOwners || budget_.maxOwnedBytes < Contract::minimumOwnedBytes
        || ownedBytes_ > budget_.maxOwnedBytes)
        return "RHI-METAL-ZERO-BASE-COLD-BUDGET";
    if (!generation_ || generation_ != fault_->generation()
        || fault_->faultLatchPriorState() != rhi::ResourceState::Undefined)
        return "RHI-METAL-ZERO-BASE-COLD-GENERATION-MISMATCH";
    const auto& pipeline = packet_->pipeline();
    const auto& source = pipeline->source();
    const auto& registry = source->index()->registry();
    const auto& device = pipeline->native()->nativeDevice;
    const std::array<rhi::GpuBuffer*, 6> buffers{uploads_[0].buffer.get(),
        fault_->faultLatchBuffer(),
        const_cast<rhi::GpuBuffer*>(&source->sourceResource()),
        uploads_[1].buffer.get(),
        uploads_[2].buffer.get(),
        packet_->packet()->producerParameters()};
    rhi::CanonicalZeroBaseIndirectSourceContract::Facts logical;
    if (const auto* id = rhi::CanonicalZeroBaseIndirectSourceContract::verify(*source->package(),
            source->content(),
            rhi::CanonicalZeroBaseIndirectSourceContract::selectedEntry,
            logical))
        return id;
    for (std::size_t i = 0u; i < buffers.size(); ++i) {
        if (!buffers[i] || bindings_[i].resource != buffers[i]->id()
            || bindings_[i].set != logical.bindings[i].set
            || bindings_[i].binding != logical.bindings[i].binding
            || !bindings_[i].abi.matches(pipeline->facts()[i])
            || buffers[i]->desc().sizeBytes != logical.bindings[i].requiredBytes
            || bindings_[i].abi.requiredBytes > buffers[i]->desc().sizeBytes)
            return "RHI-METAL-ZERO-BASE-COLD-BINDING-MISMATCH";
        const auto registered = registry->find(buffers[i]->id());
        if (!registered
            || !core::SharedOwnerProjection::sameOwner(registered->buffer, resources_[i].buffer)
            || !core::SharedOwnerProjection::sameOwner(
                registered->buffer, metalRhiNativePayloadFor(*buffers[i]))
            || !observations_[i].matches(resources_[i].buffer->resource))
            return "RHI-METAL-ZERO-BASE-COLD-RESOURCE-MISMATCH";
        for (std::size_t previous = 0u; previous < i; ++previous)
            if (buffers[previous]->id() == buffers[i]->id()
                || core::SharedOwnerProjection::sameOwner(
                    resources_[previous].buffer, resources_[i].buffer)
                || resources_[previous].buffer->resource.nativeObject
                    == resources_[i].buffer->resource.nativeObject)
                return "RHI-METAL-ZERO-BASE-COLD-RESOURCE-ALIAS";
    }
    const std::array<std::size_t, 3> uploadRoles{static_cast<std::size_t>(Role::SnapshotMetadata),
        static_cast<std::size_t>(Role::PhysicalArguments),
        static_cast<std::size_t>(Role::InstanceMapping)};
    std::uint64_t accounted = fault_->reportedResidentBytes();
    for (std::size_t i = 0u; i < uploads_.size(); ++i) {
        const auto& upload = uploads_[i];
        const auto role = uploadRoles[i];
        if (!core::SharedOwnerProjection::sameOwner(
                upload.resource.buffer, resources_[role].buffer))
            return "RHI-METAL-ZERO-BASE-COLD-RESOURCE-MISMATCH";
        const auto state = i == 0u ? rhi::ResourceState::ShaderRead : outputState;
        if (upload.buffer->state() != state)
            return "RHI-METAL-ZERO-BASE-COLD-STATE-MISMATCH";
        if (const auto* id = Upload::validate(upload.buffer,
                registry,
                upload.resource,
                observations_[role],
                device,
                logical.bindings[role].requiredBytes,
                zeroBaseColdDomains[i],
                zeroBaseColdDiagnostics))
            return id;
        const auto bytes = upload.buffer->allocation().estimatedSizeBytes;
        if (accounted > budget_.maxOwnedBytes || bytes > budget_.maxOwnedBytes - accounted)
            return "RHI-METAL-ZERO-BASE-COLD-BYTE-BUDGET";
        accounted += bytes;
    }
    std::uint64_t faultBytes = 0u;
    for (const auto faultRole : {rhi::BindlessOwnedFaultDomain::Role::Latch,
             rhi::BindlessOwnedFaultDomain::Role::Readback}) {
        const bool latch = faultRole == rhi::BindlessOwnedFaultDomain::Role::Latch;
        const auto* buffer = latch ? fault_->faultLatchBuffer() : fault_->faultReadbackBuffer();
        const auto spec = rhi::BindlessOwnedFaultDomain::descriptor(faultRole);
        const auto reported = buffer->allocation().estimatedSizeBytes;
        if (reported > rhi::BindlessOwnedFaultDomain::minimumBytes - faultBytes)
            return "RHI-METAL-ZERO-BASE-COLD-BYTE-BUDGET";
        faultBytes += reported;
        if (buffer->state() != (latch ? outputState : spec.initialState))
            return "RHI-METAL-ZERO-BASE-COLD-STATE-MISMATCH";
        if (const auto* id = Upload::validateRegistered(
                std::shared_ptr<rhi::GpuBuffer>(fault_, const_cast<rhi::GpuBuffer*>(buffer)),
                registry,
                latch ? resources_[static_cast<std::size_t>(Role::FaultLatch)] : readback_,
                latch ? observations_[static_cast<std::size_t>(Role::FaultLatch)]
                      : readbackObservation_,
                device,
                spec,
                metalRhiNativeStorageMode(latch ? MetalRhiMemoryPolicy::PrivateStorage
                                                : MetalRhiMemoryPolicy::ManagedReadback),
                false,
                latch,
                zeroBaseColdDiagnostics))
            return id;
    }
    if (faultBytes != fault_->reportedResidentBytes())
        return "RHI-METAL-ZERO-BASE-COLD-BYTE-BUDGET";
    for (const auto& resource : resources_)
        if (resource.buffer->resource.nativeObject == readback_.buffer->resource.nativeObject)
            return "RHI-METAL-ZERO-BASE-COLD-RESOURCE-ALIAS";
    for (const auto role : {Role::PhysicalArguments, Role::InstanceMapping}) {
        const auto i = static_cast<std::size_t>(role);
        const auto destination = role == Role::PhysicalArguments ? 0u : 1u;
        if (buffers[i]->id() == packet_->buffers()[destination]->id()
            || resources_[i].buffer->resource.nativeObject
                == packet_->resources()[destination].buffer->resource.nativeObject)
            return "RHI-METAL-ZERO-BASE-COLD-RESOURCE-ALIAS";
    }
    Contract::Resources metadataResources{{{1u, buffers[2]->id(), buffers[2]->desc().sizeBytes},
        {2u, buffers[3]->id(), buffers[3]->desc().sizeBytes},
        {3u, buffers[4]->id(), buffers[4]->desc().sizeBytes},
        {4u, buffers[5]->id(), buffers[5]->desc().sizeBytes}}};
    Contract::Words words;
    Contract::Bytes bytes;
    if (const auto* id = Contract::buildMetadata(
            source->package()->artifact(), generation_, metadataResources, words, bytes))
        return id;
    if (accounted != ownedBytes_ || owners_.size() > budget_.maxChildOwners
        || !owners_.matches([&](auto&& visit) {
               projectOwners(visit);
           }))
        return "RHI-METAL-ZERO-BASE-COLD-OWNER-MISMATCH";
    return nullptr;
}

std::shared_ptr<const MetalRhiZeroBaseIndirectColdBindings>
MetalRhiZeroBaseIndirectColdBindings::prepare(
    std::shared_ptr<const MetalRhiZeroBaseIndirectPacketStorage> packet,
    rhi::GpuDevice& device,
    Budget budget,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    const auto fail =
        [&](const char* id) -> std::shared_ptr<const MetalRhiZeroBaseIndirectColdBindings> {
        diagnostic = id;
        return {};
    };
    if (!packet || !packet->sharedOwnerMatches(packet))
        return fail("RHI-METAL-ZERO-BASE-COLD-OWNER-MISMATCH");
    if (const auto* id = packet->validate())
        return fail(id);
    if (budget.contractVersion != Contract::version || !budget.prototypeOptIn
        || !budget.maxChildOwners || !budget.maxOwnedBytes)
        return fail("RHI-METAL-ZERO-BASE-COLD-BUDGET");
    if (budget.maxOwnedBytes < Contract::minimumOwnedBytes)
        return fail("RHI-METAL-ZERO-BASE-COLD-BYTE-BUDGET");
    const auto decision = Contract::lowering(device.backend());
    if (!decision.prototypeColdBindings)
        return fail(decision.diagnosticId);
    const auto& pipeline = packet->pipeline();
    const auto& source = pipeline->source();
    const auto& registry = source->index()->registry();
    if (!core::SharedOwnerProjection::sameOwner(registry, metalRhiIssuingResourceRegistry(device)))
        return fail("RHI-METAL-ZERO-BASE-COLD-ISSUER-MISMATCH");
    const auto requiredOwners = 1u + packet->childOwnerCount() + 1u
        + 5u * (1u + MetalRhiResourceOwnerProjection::independentBufferOwnerCount);
    if (requiredOwners > budget.maxChildOwners)
        return fail("RHI-METAL-ZERO-BASE-COLD-CHILD-OWNER-BUDGET");
    try {
        core::CpuMemoryQuotaScope scope(source->quota());
        rhi::BindlessFaultDomainDesc faultDesc;
        faultDesc.maxResidentBytes = rhi::BindlessOwnedFaultDomain::minimumBytes;
        auto fault = rhi::BindlessOwnedFaultDomain::create(device, faultDesc, diagnostic);
        if (!fault)
            return {};
        const auto lookup = [&](rhi::GpuBuffer& buffer, MetalRhiRegisteredResource& output) {
            const auto found = registry->find(buffer.id());
            if (!found || !found->buffer)
                return false;
            output = *found;
            return true;
        };
        Resources resources;
        MetalRhiRegisteredResource readback;
        if (!lookup(
                *fault->faultLatchBuffer(), resources[static_cast<std::size_t>(Role::FaultLatch)])
            || !lookup(*fault->faultReadbackBuffer(), readback)
            || !lookup(const_cast<rhi::GpuBuffer&>(source->sourceResource()),
                resources[static_cast<std::size_t>(Role::LogicalSource)])
            || !lookup(*packet->packet()->producerParameters(),
                resources[static_cast<std::size_t>(Role::Parameters)]))
            return fail("RHI-METAL-ZERO-BASE-COLD-RESOURCE-MISMATCH");
        std::array<Upload::Result, 3> uploads;
        const auto& nativeDevice = pipeline->native()->nativeDevice;
        std::uint64_t ownedBytes = fault->reportedResidentBytes();
        for (std::size_t i = 1u; i < uploads.size(); ++i) {
            core::ByteBuffer zero(i == 1u ? sizeof(rhi::DrawIndexedIndirectCommand)
                                          : sizeof(rhi::IndirectInstanceBase),
                0u,
                &core::defaultCpuMemoryResource());
            auto uploaded = Upload::prepare(device,
                registry,
                nativeDevice,
                zero,
                zeroBaseColdDomains[i],
                zeroBaseColdDiagnostics,
                diagnostic);
            if (!uploaded)
                return {};
            uploads[i] = std::move(*uploaded);
        }
        Contract::Resources metadataResources{
            {{1u, source->sourceResource().id(), source->sourceResource().desc().sizeBytes},
                {2u, uploads[1].buffer->id(), uploads[1].buffer->desc().sizeBytes},
                {3u, uploads[2].buffer->id(), uploads[2].buffer->desc().sizeBytes},
                {4u,
                    packet->packet()->producerParameters()->id(),
                    packet->packet()->producerParameters()->desc().sizeBytes}}};
        Contract::Words words;
        Contract::Bytes bytes;
        if (const auto* id = Contract::buildMetadata(source->package()->artifact(),
                fault->generation(),
                metadataResources,
                words,
                bytes))
            return fail(id);
        core::ByteBuffer encoded(bytes.begin(), bytes.end(), &core::defaultCpuMemoryResource());
        auto metadata = Upload::prepare(device,
            registry,
            nativeDevice,
            encoded,
            zeroBaseColdDomains[0],
            zeroBaseColdDiagnostics,
            diagnostic);
        if (!metadata)
            return {};
        uploads[0] = std::move(*metadata);
        const std::array<std::size_t, 3> uploadRoles{
            static_cast<std::size_t>(Role::SnapshotMetadata),
            static_cast<std::size_t>(Role::PhysicalArguments),
            static_cast<std::size_t>(Role::InstanceMapping)};
        for (std::size_t i = 0u; i < uploads.size(); ++i) {
            resources[uploadRoles[i]] = uploads[i].resource;
            const auto reported = uploads[i].buffer->allocation().estimatedSizeBytes;
            if (ownedBytes > budget.maxOwnedBytes || reported > budget.maxOwnedBytes - ownedBytes)
                return fail("RHI-METAL-ZERO-BASE-COLD-BYTE-BUDGET");
            ownedBytes += reported;
        }
        rhi::CanonicalZeroBaseIndirectSourceContract::Facts logical;
        if (const auto* id =
                rhi::CanonicalZeroBaseIndirectSourceContract::verify(*source->package(),
                    source->content(),
                    rhi::CanonicalZeroBaseIndirectSourceContract::selectedEntry,
                    logical))
            return fail(id);
        Bindings bindings;
        const std::array<rhi::GpuResourceId, 6> ids{uploads[0].buffer->id(),
            fault->faultLatchBuffer()->id(),
            source->sourceResource().id(),
            uploads[1].buffer->id(),
            uploads[2].buffer->id(),
            packet->packet()->producerParameters()->id()};
        for (std::size_t i = 0u; i < bindings.size(); ++i)
            bindings[i] = {
                ids[i], logical.bindings[i].set, logical.bindings[i].binding, pipeline->facts()[i]};
        auto root = core::makeShared<MetalRhiZeroBaseIndirectColdBindings>(ConstructionKey{},
            std::move(packet),
            std::move(fault),
            std::move(uploads),
            std::move(resources),
            std::move(readback),
            bindings,
            budget,
            ownedBytes);
        if (const auto* id = root->validate())
            return fail(id);
        return root;
    } catch (const std::bad_alloc&) {
        core::CpuMemoryQuotaScope diagnosticScope(nullptr);
        return fail("RHI-METAL-ZERO-BASE-COLD-ALLOCATION-FAILED");
    }
}

const char* MetalRhiZeroBaseIndirectPacketStorage::validate() const noexcept
{
    return validateForState(rhi::ResourceState::ShaderWrite);
}

const char* MetalRhiZeroBaseIndirectPacketStorage::validateClosedReadbackReady() const noexcept
{
    return validateForState(rhi::ResourceState::ShaderRead);
}

const char* MetalRhiZeroBaseIndirectPacketStorage::validateForState(
    rhi::ResourceState state) const noexcept
{
    if (state != rhi::ResourceState::ShaderWrite && state != rhi::ResourceState::ShaderRead)
        return "RHI-METAL-ZERO-BASE-PACKET-STORAGE-PHASE";
    if (!pipeline_ || !pipeline_->sharedOwnerMatches(pipeline_) || !packet_
        || !packet_->sharedOwnerMatches(packet_))
        return "RHI-METAL-ZERO-BASE-PACKET-OWNER-MISMATCH";
    if (const auto* id = pipeline_->validate())
        return id;
    if (budget_.contractVersion != Contract::version || !budget_.prototypeOptIn
        || !budget_.maxPacketBytes || !budget_.maxChildOwners)
        return "RHI-METAL-ZERO-BASE-PACKET-BUDGET";
    if ((purpose_ != rhi::BindlessIndirectPacket::Issuance::PrototypeStorageOnly
            && purpose_ != rhi::BindlessIndirectPacket::Issuance::PrototypeClosedConsumerCold)
        || packet_->issuance() != purpose_ || packet_->consumerPlan().success
        || packet_->consumerPlan().executable || packet_->consumerPlan().identity
        || packet_->consumerPlan().deviceIdentity || packet_->consumerPlan().shaderPackageIdentity)
        return "RHI-METAL-ZERO-BASE-PACKET-CONSUMER-PLAN-UNISSUED-REQUIRED";
    if (state == rhi::ResourceState::ShaderRead
        && purpose_ != rhi::BindlessIndirectPacket::Issuance::PrototypeClosedConsumerCold)
        return "RHI-METAL-CLOSED-CONSUMER-READY-ORIGINAL-PURPOSE";
    if (const auto* id = packet_->storageDiagnostic())
        return id;
    if (packet_->reportedBufferBytes() > budget_.maxPacketBytes)
        return "RHI-METAL-ZERO-BASE-PACKET-BYTE-BUDGET";
    if (const auto* id = pipeline_->source()->content().validateParameters(packet_->parameters()))
        return id;
    const std::array<rhi::GpuBuffer*, 3> expected{
        packet_->physicalArgs(), packet_->instanceMappings(), packet_->producerParameters()};
    const auto& index = pipeline_->source()->index();
    const auto& nativeDevice = pipeline_->native()->nativeDevice;
    for (std::size_t i = 0u; i < buffers_.size(); ++i) {
        const std::shared_ptr<rhi::GpuBuffer> canonical(packet_, expected[i]);
        if (!core::SharedOwnerProjection::sameOwner(buffers_[i], canonical))
            return "RHI-METAL-ZERO-BASE-PACKET-OWNER-MISMATCH";
        const auto spec = Contract::descriptor(Contract::roles[i]);
        if (buffers_[i]->state() != (i < 2u ? state : spec.initialState))
            return "RHI-METAL-ZERO-BASE-PACKET-STATE-MISMATCH";
        if (const auto* id = MetalRhiSealedBufferUpload::validate(buffers_[i],
                index->registry(),
                resources_[i],
                observations_[i],
                nativeDevice,
                spec.sizeBytes,
                zeroBasePacketDomains[i],
                zeroBasePacketDiagnostics))
            return id;
    }
    if (owners_.size() > budget_.maxChildOwners)
        return "RHI-METAL-ZERO-BASE-PACKET-CHILD-OWNER-BUDGET";
    return owners_.matches([&](auto&& visit) {
        projectOwners(visit);
    })
        ? nullptr
        : "RHI-METAL-ZERO-BASE-PACKET-OWNER-MISMATCH";
}

std::shared_ptr<const MetalRhiZeroBaseIndirectPacketStorage>
MetalRhiZeroBaseIndirectPacketStorage::prepare(
    std::shared_ptr<const MetalRhiZeroBaseIndirectPipeline> pipeline,
    rhi::BindlessIndirectPacketPool& pool,
    Budget budget,
    core::pmr::String& diagnostic)
{
    return prepareImpl(std::move(pipeline),
        pool,
        budget,
        rhi::BindlessIndirectPacket::Issuance::PrototypeStorageOnly,
        diagnostic);
}

std::shared_ptr<const MetalRhiZeroBaseIndirectPacketStorage>
MetalRhiZeroBaseIndirectPacketStorage::prepareImpl(
    std::shared_ptr<const MetalRhiZeroBaseIndirectPipeline> pipeline,
    rhi::BindlessIndirectPacketPool& pool,
    Budget budget,
    rhi::BindlessIndirectPacket::Issuance purpose,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    const auto fail =
        [&](core::StringView id) -> std::shared_ptr<const MetalRhiZeroBaseIndirectPacketStorage> {
        diagnostic.assign(id.data(), id.size());
        return {};
    };
    if (!pipeline || !pipeline->sharedOwnerMatches(pipeline))
        return fail("RHI-METAL-ZERO-BASE-PACKET-OWNER-MISMATCH");
    if (const auto* id = pipeline->validate())
        return fail(id);
    if (budget.contractVersion != Contract::version || !budget.prototypeOptIn
        || !budget.maxPacketBytes || !budget.maxChildOwners)
        return fail("RHI-METAL-ZERO-BASE-PACKET-BUDGET");
    if (const auto* id = pool.validateBudget())
        return fail(id);
    if (pool.budget_.maxBytesPerPacket > budget.maxPacketBytes)
        return fail("RHI-METAL-ZERO-BASE-PACKET-BYTE-BUDGET");
    if (purpose == rhi::BindlessIndirectPacket::Issuance::PrototypeStorageOnly) {
        const auto decision = Contract::coldLowering(pool.device_.backend());
        if (!decision.prototypeStorage)
            return fail(decision.diagnosticId);
    } else if (purpose == rhi::BindlessIndirectPacket::Issuance::PrototypeClosedConsumerCold) {
        const auto decision = Contract::closedConsumerLowering(
            pool.device_.backend(), budget.contractVersion, budget.prototypeOptIn);
        if (!decision.prototypeColdIntent)
            return fail(decision.diagnosticId);
    } else
        return fail("RHI-BINDLESS-INDIRECT-PACKET-CREATION-PURPOSE");
    const auto& index = pipeline->source()->index();
    if (!core::SharedOwnerProjection::sameOwner(
            index->registry(), metalRhiIssuingResourceRegistry(pool.device_)))
        return fail("RHI-METAL-ZERO-BASE-PACKET-ISSUER-MISMATCH");
    // Pipeline root/projection, original packet, and three exact packet aliases
    // plus their four mandatory independent native allocation owners.
    const auto requiredOwners = 1u + pipeline->childOwnerCount() + 1u
        + Contract::roles.size()
            * (1u + MetalRhiResourceOwnerProjection::independentBufferOwnerCount);
    if (requiredOwners > budget.maxChildOwners)
        return fail("RHI-METAL-ZERO-BASE-PACKET-CHILD-OWNER-BUDGET");
    const auto* issuer = static_cast<const rhi::BindlessCrossExtension*>(
        pool.device_.queryInterface(rhi::kExtensionBindlessCross));
    if (!issuer)
        return fail("RHI-BINDLESS-EXTENSION-UNAVAILABLE");
    try {
        core::CpuMemoryQuotaScope scope(index->quota());
        auto packet = core::shareUnique(pool.prepareStorageCandidate(
            pipeline->source()->content().parameters(), *issuer, purpose, nullptr, diagnostic));
        if (!packet)
            return {};
        Buffers buffers{std::shared_ptr<rhi::GpuBuffer>(packet, packet->physicalArgs()),
            std::shared_ptr<rhi::GpuBuffer>(packet, packet->instanceMappings()),
            std::shared_ptr<rhi::GpuBuffer>(packet, packet->producerParameters())};
        Resources resources;
        for (std::size_t i = 0u; i < buffers.size(); ++i) {
            const auto actual = index->registry()->find(buffers[i]->id());
            if (!actual || !actual->buffer
                || actual->buffer != metalRhiNativePayloadFor(*buffers[i]))
                return fail("RHI-METAL-ZERO-BASE-PACKET-RESOURCE-MISMATCH");
            resources[i] = *actual;
        }
        auto root = core::makeShared<MetalRhiZeroBaseIndirectPacketStorage>(ConstructionKey{},
            pipeline,
            packet,
            std::move(buffers),
            std::move(resources),
            budget,
            purpose);
        if (const auto* id = root->validate())
            return fail(id);
        return root;
    } catch (const std::bad_alloc&) {
        core::CpuMemoryQuotaScope diagnosticScope(nullptr);
        return fail("RHI-METAL-ZERO-BASE-PACKET-ALLOCATION-FAILED");
    }
}

const char* MetalRhiZeroBaseIndirectSource::validate() const noexcept
{
    if (!package_ || !package_->sharedOwnerMatches(package_) || !index_
        || !index_->sharedOwnerMatches(index_) || !content_
        || !content_->sharedOwnerMatches(content_))
        return "RHI-METAL-ZERO-BASE-SOURCE-OWNER-MISMATCH";
    if (const auto* id = index_->validate())
        return id;
    rhi::CanonicalZeroBaseIndirectSourceContract::Facts facts;
    if (const auto* id = rhi::CanonicalZeroBaseIndirectSourceContract::verify(*package_,
            *content_,
            rhi::CanonicalZeroBaseIndirectSourceContract::selectedEntry,
            facts))
        return id;
    if (budget_.contractVersion != 1u || !budget_.prototypeOptIn || !budget_.maxChildOwners
        || !budget_.maxSourceBytes || !budget_.maxParameterBytes)
        return "RHI-METAL-ZERO-BASE-SOURCE-PREPARATION-BUDGET";
    if (content_->sourceBytes().size() > budget_.maxSourceBytes
        || sizeof(rhi::ZeroBaseIndirectProducerParams) > budget_.maxParameterBytes)
        return "RHI-METAL-ZERO-BASE-SOURCE-BYTE-BUDGET";
    if (const auto* id = content_->validateSourceStorage(*source_.buffer))
        return id;
    if (parameters_.buffer->state() != rhi::ResourceState::ShaderRead)
        return "RHI-METAL-ZERO-BASE-SOURCE-PARAMETER-STATE";
    const auto& device = index_->provider()->native()->nativeDevice;
    if (const auto* id = Upload::validate(source_.buffer,
            index_->registry(),
            source_.resource,
            sourceObservation_,
            device,
            content_->sourceBytes().size(),
            Upload::Domain::ImmutableStorage,
            zeroBaseSourceDiagnostics))
        return id;
    if (const auto* id = Upload::validate(parameters_.buffer,
            index_->registry(),
            parameters_.resource,
            parameterObservation_,
            device,
            sizeof(rhi::ZeroBaseIndirectProducerParams),
            Upload::Domain::ImmutableUniform,
            zeroBaseSourceDiagnostics))
        return id;
    if (owners_.size() > budget_.maxChildOwners)
        return "RHI-METAL-ZERO-BASE-SOURCE-CHILD-OWNER-BUDGET";
    if (!owners_.matches([&](auto&& visit) {
            projectOwners(visit);
        }))
        return "RHI-METAL-ZERO-BASE-SOURCE-OWNER-MISMATCH";
    return nullptr;
}

std::shared_ptr<const MetalRhiZeroBaseIndirectSource> MetalRhiZeroBaseIndirectSource::prepare(
    std::shared_ptr<const rhi::BindlessShaderPackage> package,
    std::shared_ptr<const MetalRhiOrdinaryPreparedIndexBinding> index,
    rhi::GpuDevice& device,
    Content::Arguments::Source source,
    rhi::ZeroBaseIndirectProducerParams parameters,
    rhi::BindlessIndirectInstanceConsumerLayout consumer,
    Content::Budget contentBudget,
    Budget budget,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    const auto fail = [&](const char* id) -> std::shared_ptr<const MetalRhiZeroBaseIndirectSource> {
        diagnostic = id;
        return {};
    };
    if (budget.contractVersion != 1u || !budget.prototypeOptIn || !budget.maxSourceBytes
        || !budget.maxParameterBytes || !budget.maxChildOwners)
        return fail("RHI-METAL-ZERO-BASE-SOURCE-PREPARATION-BUDGET");
    if (source.byteExtent > budget.maxSourceBytes
        || sizeof(rhi::ZeroBaseIndirectProducerParams) > budget.maxParameterBytes)
        return fail("RHI-METAL-ZERO-BASE-SOURCE-BYTE-BUDGET");
    if (!package || !package->sharedOwnerMatches(package) || !index
        || !index->sharedOwnerMatches(index))
        return fail("RHI-METAL-ZERO-BASE-SOURCE-OWNER-MISMATCH");
    if (const auto* id = index->validate())
        return fail(id);
    if (index->registry() != metalRhiIssuingResourceRegistry(device))
        return fail("RHI-METAL-ZERO-BASE-SOURCE-ISSUER-MISMATCH");
    // Package + index root + canonical content, index child projection and two uploads
    // (each wrapper + mandatory resource owners). Refuse before registry/native
    // allocation; the budget cannot trigger a partial cold registry expansion.
    const auto requiredOwners = 3u + index->childOwnerCount()
        + 2u * (1u + MetalRhiResourceOwnerProjection::independentBufferOwnerCount);
    if (requiredOwners > budget.maxChildOwners)
        return fail("RHI-METAL-ZERO-BASE-SOURCE-CHILD-OWNER-BUDGET");
    if (!describeMetalRhiNativeRuntimeBoundary().runtimeAvailableOnThisPlatform)
        return fail("RHI-METAL-ZERO-BASE-SOURCE-NATIVE-RUNTIME-UNAVAILABLE");
    try {
        core::CpuMemoryQuotaScope scope(index->quota());
        Content content(source, parameters, consumer, contentBudget, index->content());
        rhi::CanonicalZeroBaseIndirectSourceContract::Facts facts;
        if (const auto* id = rhi::CanonicalZeroBaseIndirectSourceContract::verify(*package,
                content,
                rhi::CanonicalZeroBaseIndirectSourceContract::selectedEntry,
                facts))
            return fail(id);
        const auto& nativeDevice = index->provider()->native()->nativeDevice;
        auto uploaded = Upload::prepare(device,
            index->registry(),
            nativeDevice,
            content.sourceBytes(),
            Upload::Domain::ImmutableStorage,
            zeroBaseSourceDiagnostics,
            diagnostic);
        if (!uploaded)
            return {};
        rhi::BindlessIndirectPacketStorageContract::ParameterBytes encodedParameters;
        if (const auto* id = rhi::BindlessIndirectPacketStorageContract::encodeParameters(
                parameters, encodedParameters))
            return fail(id);
        core::ByteBuffer parameterBytes(
            encodedParameters.begin(), encodedParameters.end(), &core::defaultCpuMemoryResource());
        auto params = Upload::prepare(device,
            index->registry(),
            nativeDevice,
            parameterBytes,
            Upload::Domain::ImmutableUniform,
            zeroBaseSourceDiagnostics,
            diagnostic);
        if (!params)
            return {};
        auto result = core::makeShared<MetalRhiZeroBaseIndirectSource>(ConstructionKey{},
            std::move(package),
            std::move(index),
            std::move(content),
            std::move(*uploaded),
            std::move(*params),
            budget);
        if (const auto* id = result->validate())
            return fail(id);
        return result;
    } catch (const std::bad_alloc&) {
        core::CpuMemoryQuotaScope diagnosticScope(nullptr);
        return fail("RHI-METAL-ZERO-BASE-SOURCE-PREPARATION-ALLOCATION-FAILED");
    }
}

MetalRhiIndirectExecutionPreparation::MetalRhiIndirectExecutionPreparation(ConstructionKey,
    std::shared_ptr<const MetalRhiIndirectOutputPreparation> output,
    std::shared_ptr<const MetalRhiPreparedInstanceAttributeConsumer> consumer,
    std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsBinding> binding,
    std::shared_ptr<const MetalRhiOrdinaryPreparedRenderPass> pass,
    const std::shared_ptr<MetalRhiNativeCommandPayload>& command,
    MetalRhiIndirectOutputRecordingLease&& lease,
    MetalRhiIndirectExecutionPreparationBudget budget)
    : output_(std::move(output)), consumer_(std::move(consumer)), binding_(std::move(binding)),
      pass_(std::move(pass)), command_(command), states_(command->indirectOutputStates_),
      computeSlot_(command->preparedIndirectComputeEncoderSlot_),
      renderSlot_(command->preparedRenderEncoderSlot_),
      identity_{output_->resource().id(), output_->generation(), command->recordingIdentity()},
      lease_(std::move(lease)), budget_(budget)
{
    owners_.capture([&](auto&& visit) {
        projectOwners(visit);
    });
}

const char* MetalRhiIndirectExecutionPreparation::validateInputs(
    const std::shared_ptr<const MetalRhiIndirectOutputPreparation>& output,
    const std::shared_ptr<const MetalRhiPreparedInstanceAttributeConsumer>& consumer,
    const std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsBinding>& binding,
    const std::shared_ptr<const MetalRhiOrdinaryPreparedRenderPass>& pass,
    const std::shared_ptr<MetalRhiNativeCommandPayload>& command)
{
    if (!output || !consumer || !binding || !pass || !command)
        return "RHI-METAL-INDIRECT-EXECUTION-ROOT-MISSING";
    if (!output->sharedOwnerMatches(output) || !consumer->sharedOwnerMatches(consumer)
        || !binding->sharedOwnerMatches(binding) || !pass->sharedOwnerMatches(pass)
        || !command->sharedOwnerMatches(command))
        return "RHI-METAL-INDIRECT-EXECUTION-OWNER-MISMATCH";
    if (!command->indirectOutputBudget_ || !command->indirectOutputStates_
        || !command->preparedIndirectComputeEncoderSlot_ || !command->preparedRenderEncoderSlot_
        || !command->submissionRetention_ || !command->recordingRetention_
        || command->ordinaryIndirectRecording_ || command->ordinaryWriteStates_
        || command->nativeRenderEncoder_ || command->nativeComputeEncoder_
        || command->nativeBlitEncoder_ || command->ordinaryGraphicsPass_
        || command->ordinaryComputeContract_ || command->pendingGraphicsBinding_
        || command->activeGraphicsBinding_ || command->recordingRetention_->resourceOwnerCount()
        || !command->indirectOutputStates_->empty()
        || command->preparedIndirectComputeEncoderSlot_->get()
        || command->preparedRenderEncoderSlot_->get())
        return "RHI-METAL-INDIRECT-EXECUTION-COMMAND-PREPARATION-MISMATCH";
    if (output->quota() != consumer->quota() || output->quota() != binding->quota()
        || output->quota() != pass->quota()
        || output->quota() != command->recordingRetention_->quota())
        return "RHI-COMMAND-RETENTION-CPU-QUOTA-MISMATCH";
    const auto& provider = output->index()->provider();
    if (!core::SharedOwnerProjection::sameOwner(provider, consumer->provider())
        || !core::SharedOwnerProjection::sameOwner(provider, binding->provider())
        || !core::SharedOwnerProjection::sameOwner(provider, pass->provider())
        || output->index()->registry() != binding->registry()
        || binding->registry() != pass->registry()
        || pass->registry() != command->resourceRegistry_)
        return "RHI-METAL-INDIRECT-EXECUTION-PROVIDER-ISSUER-MISMATCH";
    if (const auto* id = output->validate())
        return id;
    if (const auto* id = consumer->validateForProvider(provider))
        return id;
    // Reuse the exact finite budget admission path, including stream/end reserve.
    const rhi::GpuIndirectOutputRecordingIdentity identity{
        output->resource().id(), output->generation(), command->recordingIdentity()};
    rhi::GpuIndirectOutputSequence sequence(*command->indirectOutputBudget_, identity);
    using C = rhi::GpuIndirectOutputSequence::Command;
    for (const auto operation :
        {C::ProducerBegin, C::Dispatch, C::ProducerEnd, C::Dependency, C::ConsumerBegin})
        if (const auto* id = sequence.record(operation, identity))
            return id;
    for (const auto& stream : output->streams()) {
        (void)stream;
        if (const auto* id = sequence.record(C::StreamBinding, identity))
            return id;
    }
    for (const auto operation : {C::Indirect, C::ConsumerEnd})
        if (const auto* id = sequence.record(operation, identity))
            return id;
    return sequence.finish();
}

const char* MetalRhiIndirectExecutionPreparation::validate() const
{
    if (status() != Status::Cold)
        return "RHI-METAL-INDIRECT-EXECUTION-ROOT-NOT-COLD";
    if (const auto* id = budget_.validate())
        return id;
    const auto command = command_.lock();
    if (const auto* id = validateInputs(output_, consumer_, binding_, pass_, command))
        return id;
    if (const auto* id = lease_.validateForRecord(*output_, *command, identity_.generation))
        return id;
    if (const auto valid = binding_->validateObservation(); !valid.success)
        return "RHI-METAL-INDIRECT-EXECUTION-BINDING-OBSERVATION";
    if (const auto valid = pass_->validateObservation(); !valid.success)
        return "RHI-METAL-INDIRECT-EXECUTION-PASS-OBSERVATION";
    if (identity_.resource != output_->resource().id()
        || identity_.recording != command->recordingIdentity()
        || !core::SharedOwnerProjection::sameOwner(states_, command->indirectOutputStates_)
        || !core::SharedOwnerProjection::sameOwner(
            computeSlot_, command->preparedIndirectComputeEncoderSlot_)
        || !core::SharedOwnerProjection::sameOwner(
            renderSlot_, command->preparedRenderEncoderSlot_))
        return "RHI-METAL-INDIRECT-EXECUTION-COMMAND-MISMATCH";
    if (owners_.size() > budget_.maxChildOwners)
        return "RHI-METAL-INDIRECT-EXECUTION-CHILD-OWNER-BUDGET";
    if (!owners_.matches([&](auto&& visit) {
            projectOwners(visit);
        }))
        return "RHI-METAL-INDIRECT-EXECUTION-OWNER-MISMATCH";
    const auto self = self_.lock();
    if (!self || !sharedOwnerMatches(self))
        return "RHI-METAL-INDIRECT-EXECUTION-OWNER-MISMATCH";
    return command->recordingRetention_->preflightResourceProjection(
        owners_.size() + 1u, [&](std::size_t i) noexcept -> std::shared_ptr<const void> {
            return i ? owners_.at(i - 1u) : self;
        });
}

std::shared_ptr<const MetalRhiIndirectExecutionPreparation>
MetalRhiIndirectExecutionPreparation::prepare(
    std::shared_ptr<const MetalRhiIndirectOutputPreparation> output,
    std::shared_ptr<const MetalRhiPreparedInstanceAttributeConsumer> consumer,
    std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsBinding> binding,
    std::shared_ptr<const MetalRhiOrdinaryPreparedRenderPass> pass,
    const std::shared_ptr<MetalRhiNativeCommandPayload>& command,
    const MetalRhiIndirectExecutionPreparationBudget& budget,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    const auto fail =
        [&](const char* id) -> std::shared_ptr<const MetalRhiIndirectExecutionPreparation> {
        diagnostic = id;
        return {};
    };
    if (const auto* id = budget.validate())
        return fail(id);
    if (const auto* id = validateInputs(output, consumer, binding, pass, command))
        return fail(id);
    core::AtomicCapacityBudget projectionCapacity(budget.maxChildOwners);
    const auto fits = [&](const auto& root) noexcept {
        return projectionCapacity.tryAcquire(1u)
            && projectionCapacity.tryAcquire(root->childOwnerCount());
    };
    if (!fits(output) || !fits(consumer) || !fits(binding) || !fits(pass)
        || !projectionCapacity.tryAcquire(3u))
        return fail("RHI-METAL-INDIRECT-EXECUTION-CHILD-OWNER-BUDGET");
    const char* id = nullptr;
    auto lease = MetalRhiIndirectOutputRecordingLease::acquire(output, command, id);
    if (!lease.valid())
        return fail(id);
    try {
        core::CpuMemoryQuotaScope scope(output->quota());
        auto result = core::makeShared<MetalRhiIndirectExecutionPreparation>(
            ConstructionKey{}, output, consumer, binding, pass, command, std::move(lease), budget);
        result->self_ = result;
        if (const auto* invalid = result->validate())
            return fail(invalid);
        return result;
    } catch (const std::bad_alloc&) {
        core::CpuMemoryQuotaScope diagnosticScope(nullptr);
        return fail("RHI-METAL-INDIRECT-EXECUTION-PREPARATION-ALLOCATION-FAILED");
    }
}

const char* MetalRhiPreparedInstanceAttributeConsumer::validate() const noexcept
{
    if (!provider_ || !provider_->sharedOwnerMatches(provider_)
        || !provider_->vertexMappingMatches())
        return "RHI-METAL-INSTANCE-ATTRIBUTE-PROVIDER-MISMATCH";
    if (const auto* id = budget_.validate(provider_->quota()))
        return id;
    if (const auto* id =
            rhi::InstanceAttributeFetchContract::validate(intent_, provider_->vertexInput()))
        return id;
    const auto& native = provider_->native();
    if (!native->nativeRuntimeBacked || native->nativeRuntimeDiagnosticOnly
        || !native->renderPipelinePayloadPresent || !native->shaderModulePayloadPresent
        || !native->vertexFunction || !native->pipelineState)
        return "RHI-METAL-INSTANCE-ATTRIBUTE-NATIVE-PAYLOAD-MISSING";
    if (const auto* id =
            native->vertexInputFacts.validate(provider_->vertexInput(), native->vertexStreams))
        return id;
    if (!(native->vertexInputFacts == facts_))
        return "RHI-METAL-INSTANCE-ATTRIBUTE-FACTS-CAPTURE-MISMATCH";
    const auto& artifact = provider_->artifacts()[0];
    if (!artifact || artifact->stage() != shader::ShaderStage::Vertex
        || artifact->quota() != provider_->quota())
        return "RHI-METAL-INSTANCE-ATTRIBUTE-ARTIFACT-MISMATCH";
    const auto& binary = artifact->bytes(shader::ShaderArtifactRole::Spirv);
    shader::VertexLocationInputProof::Facts current;
    if (const auto* id = shader::VertexLocationInputProof::verify(binary.data(),
            binary.size(),
            artifact->entry(),
            artifact->module().reflection.vertexAttributes,
            current))
        return id;
    if (!(current == proof_))
        return "RHI-METAL-INSTANCE-ATTRIBUTE-PROOF-CAPTURE-MISMATCH";
    if (!owners_.matches([&](auto&& visit) {
            projectOwners(visit);
        }))
        return "RHI-METAL-INSTANCE-ATTRIBUTE-OWNER-MISMATCH";
    return nullptr;
}

std::shared_ptr<const MetalRhiPreparedInstanceAttributeConsumer>
MetalRhiPreparedInstanceAttributeConsumer::prepare(
    std::shared_ptr<const MetalRhiOrdinaryGraphicsProvider> provider,
    const rhi::InstanceAttributeFetchIntent& intent,
    const MetalRhiOrdinaryBindingBudget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    const auto fail =
        [&](const char* id) -> std::shared_ptr<const MetalRhiPreparedInstanceAttributeConsumer> {
        diagnostic = id;
        return {};
    };
    if (const auto* id = budget.validate(quota))
        return fail(id);
    if (!provider || !provider->sharedOwnerMatches(provider) || provider->quota() != quota
        || !provider->vertexMappingMatches())
        return fail("RHI-METAL-INSTANCE-ATTRIBUTE-PROVIDER-MISMATCH");
    if (const auto* id =
            rhi::InstanceAttributeFetchContract::validate(intent, provider->vertexInput()))
        return fail(id);
    try {
        core::CpuMemoryQuotaScope scope(quota);
        const auto& artifact = provider->artifacts()[0];
        if (!artifact || artifact->stage() != shader::ShaderStage::Vertex
            || artifact->quota() != quota)
            return fail("RHI-METAL-INSTANCE-ATTRIBUTE-ARTIFACT-MISMATCH");
        shader::VertexLocationInputProof::Facts proof;
        const auto& binary = artifact->bytes(shader::ShaderArtifactRole::Spirv);
        if (const auto* id = shader::VertexLocationInputProof::verify(binary.data(),
                binary.size(),
                artifact->entry(),
                artifact->module().reflection.vertexAttributes,
                proof))
            return fail(id);
        const auto interface =
            rhi::validateSingleEntryVertexInput(provider->vertexInput(), *artifact);
        if (!interface.success)
            return fail(interface.diagnostics.c_str());
        if (const auto* id = provider->native()->vertexInputFacts.validate(
                provider->vertexInput(), provider->native()->vertexStreams))
            return fail(id);
        auto result = core::makeShared<MetalRhiPreparedInstanceAttributeConsumer>(
            ConstructionKey{}, provider, intent, proof, budget);
        if (const auto* id = result->validate())
            return fail(id);
        return result;
    } catch (const std::bad_alloc&) {
        core::CpuMemoryQuotaScope diagnosticScope(nullptr);
        return fail("RHI-METAL-INSTANCE-ATTRIBUTE-PREPARATION-ALLOCATION-FAILED");
    }
}

namespace {
constexpr MetalRhiImmutableBufferUpload::Diagnostics indexUploadDiagnostics{
    "RHI-METAL-ORDINARY-INDEX-CONTENT-MISMATCH",
    "RHI-METAL-ORDINARY-INDEX-WRITES-NOT-SEALED",
    "RHI-METAL-ORDINARY-INDEX-RESOURCE-MISMATCH",
    "RHI-METAL-ORDINARY-INDEX-NATIVE-IDENTITY-MISMATCH",
    "RHI-METAL-ORDINARY-INDEX-BUFFER-CREATION-FAILED",
    "RHI-METAL-ORDINARY-INDEX-UPLOAD-FAILED"};
constexpr MetalRhiImmutableBufferUpload::Diagnostics indirectUploadDiagnostics{
    "RHI-METAL-ORDINARY-INDIRECT-CONTENT-MISMATCH",
    "RHI-METAL-ORDINARY-INDIRECT-WRITES-NOT-SEALED",
    "RHI-METAL-ORDINARY-INDIRECT-RESOURCE-MISMATCH",
    "RHI-METAL-ORDINARY-INDIRECT-NATIVE-IDENTITY-MISMATCH",
    "RHI-METAL-ORDINARY-INDIRECT-BUFFER-CREATION-FAILED",
    "RHI-METAL-ORDINARY-INDIRECT-UPLOAD-FAILED"};
constexpr MetalRhiSealedBufferUpload::Diagnostics protectedOutputDiagnostics{
    "RHI-METAL-INDIRECT-OUTPUT-CONTENT-MISMATCH",
    "RHI-METAL-INDIRECT-OUTPUT-WRITES-NOT-SEALED",
    "RHI-METAL-INDIRECT-OUTPUT-RESOURCE-MISMATCH",
    "RHI-METAL-INDIRECT-OUTPUT-NATIVE-IDENTITY-MISMATCH",
    "RHI-METAL-INDIRECT-OUTPUT-BUFFER-CREATION-FAILED",
    "RHI-METAL-INDIRECT-OUTPUT-UPLOAD-FAILED"};
} // namespace

const char* MetalRhiIndirectOutputPreparation::validateInputs(
    const std::shared_ptr<const MetalRhiIndirectProducerPipeline>& producer,
    const std::shared_ptr<const MetalRhiOrdinaryPreparedIndexBinding>& index,
    const Stream* streams,
    std::size_t count,
    const rhi::IndirectArgumentConsumptionContract& expectation) noexcept
{
    if (!producer || !index)
        return "RHI-METAL-INDIRECT-OUTPUT-ROOT-MISSING";
    if (!producer->sharedOwnerMatches(producer) || !index->sharedOwnerMatches(index))
        return "RHI-METAL-INDIRECT-OUTPUT-ROOT-OWNER-MISMATCH";
    if (producer->quota() != index->quota())
        return "RHI-COMMAND-RETENTION-CPU-QUOTA-MISMATCH";
    if (const auto* id = producer->validate())
        return id;
    if (const auto* id = index->validate())
        return id;
    if (!core::SharedOwnerProjection::sameOwner(
            producer->native()->nativeDevice, index->provider()->native()->nativeDevice))
        return "RHI-METAL-INDIRECT-OUTPUT-DEVICE-MISMATCH";
    if (expectation.diagnostic())
        return expectation.diagnostic();
    if (expectation.purpose()
            != rhi::IndirectArgumentConsumptionContract::Purpose::GpuOutputExpectation
        || expectation.drawCount() != 1u)
        return "RHI-METAL-INDIRECT-OUTPUT-EXPECTATION-MISMATCH";
    const auto& vertex = index->provider()->vertexInput();
    if (count != vertex.streams().size() || (count && !streams))
        return "RHI-METAL-INDIRECT-OUTPUT-STREAM-COUNT";
    for (std::size_t i = 0u; i < count; ++i) {
        const auto& stream = streams[i];
        if (!stream.binding || !stream.wrapper || stream.binding->provider() != index->provider()
            || stream.binding->registry() != index->registry()
            || stream.binding->quota() != producer->quota())
            return "RHI-METAL-INDIRECT-OUTPUT-STREAM-ROOT-MISMATCH";
        if (const auto* id = stream.binding->validate())
            return id;
        if (!stream.binding->sharedOwnerMatches(stream.binding))
            return "RHI-METAL-INDIRECT-OUTPUT-STREAM-ROOT-OWNER-MISMATCH";
        if (!stream.wrapper->sharedOwnerMatches(stream.wrapper))
            return "RHI-METAL-INDIRECT-OUTPUT-STREAM-WRAPPER-OWNER-MISMATCH";
        if (const auto* id =
                rhi::GpuIndirectOutputPreparationContract::validateFetchSource(*stream.wrapper))
            return id;
        for (std::size_t j = 0u; j < i; ++j)
            if (streams[j].binding->slot() == stream.binding->slot())
                return "RHI-METAL-INDIRECT-OUTPUT-STREAM-DUPLICATE";
        const auto registered = index->registry()->find(stream.wrapper->id());
        const auto native = metalRhiNativePayloadFor(*stream.wrapper);
        if (!registered || registered->buffer != stream.binding->buffer()
            || native != stream.binding->buffer() || !registered->gpuWritesSealed
            || !registered->gpuTransferWritesSealed
            || stream.wrapper->desc().sizeBytes != stream.binding->bufferBytes()
            || registered->bufferUsage != stream.wrapper->desc().usage
            || registered->bufferSizeBytes != stream.wrapper->desc().sizeBytes)
            return "RHI-METAL-INDIRECT-OUTPUT-STREAM-WRAPPER-MISMATCH";
        if (const auto* id = validateMetalRhiTrackedNativeBufferObject(
                native->resource, stream.wrapper->desc().sizeBytes))
            return id;
        if (const auto* id = expectation.validateStream(0u,
                vertex,
                stream.binding->slot(),
                stream.binding->bufferBytes(),
                stream.binding->offset()))
            return id;
    }
    return nullptr;
}

const char* MetalRhiIndirectOutputPreparation::validate() const noexcept
{
    if (generationConsumed())
        return "RHI-METAL-INDIRECT-OUTPUT-GENERATION-CONSUMED";
    return validateForState(rhi::ResourceState::ShaderWrite);
}

const char* MetalRhiIndirectOutputPreparation::validateForState(
    rhi::ResourceState expectedState) const noexcept
{
    if (!generation_ || generation_ != storage_.buffer->id().value)
        return "RHI-METAL-INDIRECT-OUTPUT-GENERATION-MISMATCH";
    if (const auto* id = budget_.validate(streams_.size()))
        return id;
    if (const auto* id =
            validateInputs(producer_, index_, streams_.data(), streams_.size(), expectation_))
        return id;
    if (const auto* id =
            rhi::GpuIndirectOutputPreparationContract::validateStorage(storage_.buffer->desc()))
        return id;
    if (const auto* id = MetalRhiSealedBufferUpload::validate(storage_.buffer,
            index_->registry(),
            storage_.resource,
            observation_,
            producer_->native()->nativeDevice,
            sizeof(rhi::DrawIndexedIndirectCommand),
            MetalRhiSealedBufferUpload::Domain::SuppressedGpuIndirect,
            protectedOutputDiagnostics))
        return id;
    if (storage_.buffer->state() != expectedState)
        return "RHI-METAL-INDIRECT-OUTPUT-STATE-MISMATCH";
    if (owners_.size() > budget_.maxChildOwners)
        return "RHI-GPU-INDIRECT-OUTPUT-OWNER-BUDGET-EXCEEDED";
    if (!owners_.matches([&](auto&& visit) {
            projectOwners(visit);
        }))
        return "RHI-METAL-INDIRECT-OUTPUT-OWNER-MISMATCH";
    return nullptr;
}

std::shared_ptr<const MetalRhiIndirectOutputPreparation> MetalRhiIndirectOutputPreparation::prepare(
    std::shared_ptr<const MetalRhiIndirectProducerPipeline> producer,
    std::shared_ptr<const MetalRhiOrdinaryPreparedIndexBinding> index,
    rhi::GpuDevice& device,
    const Stream* streams,
    std::size_t count,
    const rhi::GpuIndirectOutputPreparationBudget& budget,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    const auto fail =
        [&](const char* id) -> std::shared_ptr<const MetalRhiIndirectOutputPreparation> {
        diagnostic = id;
        return {};
    };
    if (const auto* id = budget.validate(count))
        return fail(id);
    if (!producer || !index)
        return fail("RHI-METAL-INDIRECT-OUTPUT-ROOT-MISSING");
    if (producer->quota() != index->quota())
        return fail("RHI-COMMAND-RETENTION-CPU-QUOTA-MISMATCH");
    if (index->registry() != metalRhiIssuingResourceRegistry(device))
        return fail("RHI-METAL-INDIRECT-OUTPUT-ISSUER-MISMATCH");
    if (!producer->sharedOwnerMatches(producer) || !index->sharedOwnerMatches(index))
        return fail("RHI-METAL-INDIRECT-OUTPUT-ROOT-OWNER-MISMATCH");
    if (const auto* id = producer->validate())
        return fail(id);
    if (const auto* id = index->validate())
        return fail(id);
    try {
        core::CpuMemoryQuotaScope scope(producer->quota());
        rhi::IndirectArgumentConsumptionContract expectation(
            rhi::IndirectArgumentConsumptionContract::ExpectedGpuWords{
                producer->proof().fixedWords},
            budget.expectation,
            index->content());
        if (const auto* id = validateInputs(producer, index, streams, count, expectation))
            return fail(id);
        core::pmr::Vector<Stream> retained{&core::defaultCpuMemoryResource()};
        if (count)
            retained.assign(streams, streams + count);
        core::ByteBuffer suppressed{&core::defaultCpuMemoryResource()};
        suppressed.resize(sizeof(rhi::DrawIndexedIndirectCommand), 0u);
        auto storage = MetalRhiSealedBufferUpload::prepare(device,
            index->registry(),
            producer->native()->nativeDevice,
            suppressed,
            MetalRhiSealedBufferUpload::Domain::SuppressedGpuIndirect,
            protectedOutputDiagnostics,
            diagnostic);
        if (!storage)
            return {};
        auto result = core::makeShared<MetalRhiIndirectOutputPreparation>(ConstructionKey{},
            producer,
            index,
            std::move(retained),
            std::move(expectation),
            std::move(*storage),
            budget);
        if (const auto* id = result->validate())
            return fail(id);
        return result;
    } catch (const std::bad_alloc&) {
        core::CpuMemoryQuotaScope diagnosticScope(nullptr);
        return fail("RHI-METAL-INDIRECT-OUTPUT-PREPARATION-ALLOCATION-FAILED");
    }
}

bool MetalRhiSealedBufferUpload::policy(
    Domain domain, rhi::ResourceUsage& usage, rhi::ResourceState& state) noexcept
{
    switch (domain) {
    case Domain::ImmutableIndex:
        usage = rhi::ResourceUsage::IndexBuffer;
        state = rhi::ResourceState::ShaderRead;
        return true;
    case Domain::ImmutableIndirect:
        usage = rhi::ResourceUsage::IndirectBuffer;
        state = rhi::ResourceState::ShaderRead;
        return true;
    case Domain::ImmutableVertex:
        usage = rhi::ResourceUsage::VertexBuffer;
        state = rhi::ResourceState::ShaderRead;
        return true;
    case Domain::SuppressedGpuIndirect:
        usage = rhi::GpuIndirectOutputPreparationContract::usage();
        state = rhi::ResourceState::ShaderWrite;
        return true;
    case Domain::ImmutableStorage:
        usage = rhi::ResourceUsage::StorageBuffer;
        state = rhi::ResourceState::ShaderRead;
        return true;
    case Domain::ImmutableUniform:
        usage = rhi::ResourceUsage::UniformBuffer;
        state = rhi::ResourceState::ShaderRead;
        return true;
    case Domain::ProtectedScratchArgs:
    case Domain::ProtectedScratchMapping:
        usage = rhi::ResourceUsage::StorageBuffer | rhi::ResourceUsage::CopySrc
            | rhi::ResourceUsage::CopyDst;
        state = rhi::ResourceState::ShaderWrite;
        return true;
    case Domain::ProtectedPacketArgs:
    case Domain::ProtectedPacketMapping:
    case Domain::ProtectedPacketParameters: {
        using Schema = rhi::BindlessIndirectPacketStorageContract;
        const auto role = domain == Domain::ProtectedPacketArgs ? Schema::Role::PhysicalArgs
            : domain == Domain::ProtectedPacketMapping          ? Schema::Role::InstanceMapping
                                                                : Schema::Role::Parameters;
        const auto desc = Schema::descriptor(role);
        usage = desc.usage;
        state = desc.initialState;
        return true;
    }
    }
    return false;
}

const char* MetalRhiSealedBufferUpload::validate(const std::shared_ptr<rhi::GpuBuffer>& buffer,
    const std::shared_ptr<MetalRhiResourceRegistry>& registry,
    const MetalRhiRegisteredResource& resource,
    const MetalRhiNativeResourceObservation& observation,
    const std::shared_ptr<void>& nativeDevice,
    std::uint64_t exactBytes,
    Domain domain,
    const Diagnostics& ids) noexcept
{
    rhi::ResourceUsage usage;
    rhi::ResourceState initialState;
    if (!policy(domain, usage, initialState))
        return ids.contentMismatch;
    if (!buffer || !exactBytes || buffer->desc().sizeBytes != exactBytes
        || buffer->desc().usage != usage || buffer->desc().memory != rhi::MemoryUsage::CpuToGpu
        || buffer->desc().initialState != initialState
        || (domain == Domain::SuppressedGpuIndirect
            && exactBytes != sizeof(rhi::DrawIndexedIndirectCommand)))
        return ids.contentMismatch;
    if (!buffer->hostWritesSealed() || !buffer->gpuWritesSealed()
        || !buffer->gpuTransferWritesSealed())
        return ids.writesNotSealed;
    rhi::BufferDesc expected;
    expected.sizeBytes = exactBytes;
    expected.usage = usage;
    expected.memory = rhi::MemoryUsage::CpuToGpu;
    expected.initialState = initialState;
    return validateRegistered(buffer,
        registry,
        resource,
        observation,
        nativeDevice,
        expected,
        MetalRhiNativeStorageMode::Shared,
        true,
        true,
        ids);
}

const char* MetalRhiSealedBufferUpload::validateRegistered(
    const std::shared_ptr<rhi::GpuBuffer>& buffer,
    const std::shared_ptr<MetalRhiResourceRegistry>& registry,
    const MetalRhiRegisteredResource& resource,
    const MetalRhiNativeResourceObservation& observation,
    const std::shared_ptr<void>& nativeDevice,
    const rhi::BufferDesc& expected,
    MetalRhiNativeStorageMode storage,
    bool gpuSealed,
    bool transferSealed,
    const Diagnostics& ids) noexcept
{
    if (!buffer || !expected.sizeBytes || buffer->desc().sizeBytes != expected.sizeBytes
        || buffer->desc().usage != expected.usage || buffer->desc().memory != expected.memory
        || buffer->desc().initialState != expected.initialState
        || buffer->allocation().estimatedSizeBytes < expected.sizeBytes)
        return ids.contentMismatch;
    if (!buffer->hostWritesSealed() || buffer->gpuWritesSealed() != gpuSealed
        || buffer->gpuTransferWritesSealed() != transferSealed)
        return ids.writesNotSealed;
    const auto actual = registry ? registry->find(buffer->id()) : std::nullopt;
    if (!actual || actual->kind != rhi::DescriptorResourceKind::Buffer || !actual->buffer
        || !core::SharedOwnerProjection::sameOwner(actual->buffer, resource.buffer)
        || !core::SharedOwnerProjection::sameOwner(
            actual->buffer, metalRhiNativePayloadFor(*buffer))
        || actual->bufferUsage != expected.usage || actual->bufferSizeBytes != expected.sizeBytes
        || actual->gpuWritesSealed != gpuSealed || actual->gpuTransferWritesSealed != transferSealed
        || !actual->payloadOwner())
        return ids.resourceMismatch;
    const auto& native = actual->buffer->resource;
    if (!native.nativeRuntimeBacked || native.nativeRuntimeDiagnosticOnly
        || !native.allocationReleaseToken || !observation.matches(native)
        || !core::SharedOwnerProjection::sameOwner(native.nativeDevice, nativeDevice)
        || native.storageMode != storage
        || validateMetalRhiTrackedNativeBufferObject(native, expected.sizeBytes))
        return ids.nativeIdentityMismatch;
    return nullptr;
}

std::optional<MetalRhiSealedBufferUpload::Result> MetalRhiSealedBufferUpload::prepare(
    rhi::GpuDevice& device,
    const std::shared_ptr<MetalRhiResourceRegistry>& registry,
    const std::shared_ptr<void>& nativeDevice,
    const core::ByteBuffer& bytes,
    Domain domain,
    const Diagnostics& ids,
    core::pmr::String& diagnostic)
{
    const auto fail = [&](const char* id) -> std::optional<Result> {
        diagnostic = id;
        return {};
    };
    rhi::ResourceUsage usage;
    rhi::ResourceState initialState;
    if (bytes.empty() || !policy(domain, usage, initialState)
        || (domain == Domain::SuppressedGpuIndirect
            && (bytes.size() != sizeof(rhi::DrawIndexedIndirectCommand)
                || std::any_of(bytes.begin(), bytes.end(), [](auto byte) {
                       return byte != 0u;
                   }))))
        return fail(ids.contentMismatch);
    if (!registry || registry != metalRhiIssuingResourceRegistry(device))
        return fail(ids.resourceMismatch);
    rhi::BufferDesc desc;
    desc.sizeBytes = bytes.size();
    desc.usage = usage;
    desc.memory = rhi::MemoryUsage::CpuToGpu;
    desc.initialState = initialState;
    auto buffer = device.createBuffer(desc);
    if (!buffer)
        return fail(ids.creationFailed);
    const auto native = metalRhiNativePayloadFor(*buffer);
    if (!native || !native->resource.nativeRuntimeBacked
        || native->resource.nativeRuntimeDiagnosticOnly
        || native->resource.nativeDevice != nativeDevice)
        return fail(ids.nativeIdentityMismatch);
    if (!device.writeBuffer(*buffer, bytes.data(), bytes.size()).success)
        return fail(ids.uploadFailed);
    buffer->sealWrites();
    const auto registered = registry->find(buffer->id());
    if (!registered || registered->buffer != native)
        return fail(ids.resourceMismatch);
    Result result{core::shareUnique(std::move(buffer)), *registered};
    if (const auto* id = validate(result.buffer,
            registry,
            result.resource,
            MetalRhiNativeResourceObservation(native->resource),
            nativeDevice,
            bytes.size(),
            domain,
            ids))
        return fail(id);
    return result;
}

const char* MetalRhiImmutableBufferUpload::validate(const std::shared_ptr<rhi::GpuBuffer>& buffer,
    const std::shared_ptr<MetalRhiResourceRegistry>& registry,
    const MetalRhiRegisteredResource& resource,
    const MetalRhiNativeResourceObservation& observation,
    const std::shared_ptr<void>& device,
    std::uint64_t bytes,
    rhi::ResourceUsage usage,
    const Diagnostics& ids) noexcept
{
    MetalRhiSealedBufferUpload::Domain selected;
    if (!domain(usage, selected))
        return ids.contentMismatch;
    return MetalRhiSealedBufferUpload::validate(
        buffer, registry, resource, observation, device, bytes, selected, ids);
}

bool MetalRhiImmutableBufferUpload::domain(
    rhi::ResourceUsage usage, MetalRhiSealedBufferUpload::Domain& selected) noexcept
{
    switch (usage) {
    case rhi::ResourceUsage::IndexBuffer:
        selected = MetalRhiSealedBufferUpload::Domain::ImmutableIndex;
        return true;
    case rhi::ResourceUsage::IndirectBuffer:
        selected = MetalRhiSealedBufferUpload::Domain::ImmutableIndirect;
        return true;
    case rhi::ResourceUsage::VertexBuffer:
        selected = MetalRhiSealedBufferUpload::Domain::ImmutableVertex;
        return true;
    default:
        return false;
    }
}

std::optional<MetalRhiImmutableBufferUpload::Result> MetalRhiImmutableBufferUpload::prepare(
    rhi::GpuDevice& device,
    const std::shared_ptr<MetalRhiResourceRegistry>& registry,
    const std::shared_ptr<void>& nativeDevice,
    const core::ByteBuffer& bytes,
    rhi::ResourceUsage usage,
    const Diagnostics& ids,
    core::pmr::String& diagnostic)
{
    MetalRhiSealedBufferUpload::Domain selected;
    if (!domain(usage, selected)) {
        diagnostic = ids.contentMismatch;
        return {};
    }
    return MetalRhiSealedBufferUpload::prepare(
        device, registry, nativeDevice, bytes, selected, ids, diagnostic);
}

const char* MetalRhiOrdinaryPreparedIndirectArguments::validate() const noexcept
{
    if (!index_)
        return "RHI-METAL-ORDINARY-INDIRECT-INDEX-MISSING";
    if (const auto* id = budget_.validate(index_->quota()))
        return id;
    if (const auto* id = index_->validate())
        return id;
    if (content_.diagnostic() || !content_.drawCount()
        || content_.purpose()
            != rhi::IndirectArgumentConsumptionContract::Purpose::ImmutableHostUpload)
        return "RHI-METAL-ORDINARY-INDIRECT-CONTENT-MISMATCH";
    if (const auto* id = MetalRhiImmutableBufferUpload::validate(upload_.buffer,
            index_->registry(),
            upload_.resource,
            observation_,
            index_->provider()->native()->nativeDevice,
            content_.bytes().size(),
            rhi::ResourceUsage::IndirectBuffer,
            indirectUploadDiagnostics))
        return id;
    if (upload_.buffer->state() != rhi::ResourceState::ShaderRead)
        return "RHI-METAL-ORDINARY-INDIRECT-STATE-MISMATCH";
    if (!owners_.matches([&](auto&& visitor) {
            projectOwners(visitor);
        }))
        return "RHI-METAL-ORDINARY-INDIRECT-OWNER-MISMATCH";
    return nullptr;
}

std::shared_ptr<const MetalRhiOrdinaryPreparedIndirectArguments>
MetalRhiOrdinaryPreparedIndirectArguments::prepare(
    std::shared_ptr<const MetalRhiOrdinaryPreparedIndexBinding> index,
    rhi::GpuDevice& device,
    rhi::IndirectArgumentConsumptionContract::Source source,
    rhi::IndirectArgumentConsumptionContract::Budget argumentBudget,
    const MetalRhiOrdinaryBindingBudget& budget,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    const auto fail =
        [&](const char* id) -> std::shared_ptr<const MetalRhiOrdinaryPreparedIndirectArguments> {
        diagnostic = id;
        return {};
    };
    if (!index)
        return fail("RHI-METAL-ORDINARY-INDIRECT-INDEX-MISSING");
    if (const auto* id = budget.validate(index->quota()))
        return fail(id);
    if (const auto* id = index->validate())
        return fail(id);
    if (index->registry() != metalRhiIssuingResourceRegistry(device))
        return fail("RHI-METAL-ORDINARY-INDIRECT-ISSUER-MISMATCH");
    if (!describeMetalRhiNativeRuntimeBoundary().runtimeAvailableOnThisPlatform)
        return fail("RHI-METAL-ORDINARY-INDIRECT-NATIVE-RUNTIME-UNAVAILABLE");
    try {
        core::CpuMemoryQuotaScope scope(index->quota());
        rhi::IndirectArgumentConsumptionContract content(source, argumentBudget, index->content());
        if (const auto* id = content.diagnostic())
            return fail(id);
        auto upload = MetalRhiImmutableBufferUpload::prepare(device,
            index->registry(),
            index->provider()->native()->nativeDevice,
            content.bytes(),
            rhi::ResourceUsage::IndirectBuffer,
            indirectUploadDiagnostics,
            diagnostic);
        if (!upload)
            return {};
        auto result = core::makeShared<MetalRhiOrdinaryPreparedIndirectArguments>(
            ConstructionKey{}, index, std::move(content), std::move(*upload), budget);
        if (const auto* id = result->validate())
            return fail(id);
        return result;
    } catch (const std::bad_alloc&) {
        core::CpuMemoryQuotaScope diagnosticScope(nullptr);
        return fail("RHI-METAL-ORDINARY-INDIRECT-PREPARATION-ALLOCATION-FAILED");
    }
}

const char* MetalRhiOrdinaryPreparedIndexBinding::validate() const noexcept
{
    if (const auto* id = budget_.validate(quota_))
        return id;
    if (!provider_ || !provider_->sharedOwnerMatches(provider_) || provider_->quota() != quota_
        || !provider_->vertexMappingMatches() || !registry_ || !buffer_ || content_.diagnostic()
        || !content_.elementBytes() || content_.bytes().empty()
        || buffer_->desc().sizeBytes != content_.bytes().size()
        || buffer_->desc().usage != rhi::ResourceUsage::IndexBuffer)
        return "RHI-METAL-ORDINARY-INDEX-CONTENT-MISMATCH";
    if (const auto* id = MetalRhiImmutableBufferUpload::validate(buffer_,
            registry_,
            resource_,
            observation_,
            provider_->native()->nativeDevice,
            content_.bytes().size(),
            rhi::ResourceUsage::IndexBuffer,
            indexUploadDiagnostics))
        return id;
    if (!owners_.matches([&](auto&& visitor) {
            projectOwners(visitor);
        }))
        return "RHI-METAL-ORDINARY-INDEX-OWNER-MISMATCH";
    return nullptr;
}

std::shared_ptr<const MetalRhiOrdinaryPreparedIndexBinding>
MetalRhiOrdinaryPreparedIndexBinding::prepare(
    std::shared_ptr<const MetalRhiOrdinaryGraphicsProvider> provider,
    rhi::GpuDevice& device,
    const void* data,
    std::size_t bytes,
    rhi::IndexFormat format,
    core::UnsignedElementSpan::ByteOrder order,
    rhi::IndexInputConsumptionContract::PrimitiveRestart restart,
    std::shared_ptr<MetalRhiResourceRegistry> registry,
    const MetalRhiOrdinaryBindingBudget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    const auto fail =
        [&](const char* id) -> std::shared_ptr<const MetalRhiOrdinaryPreparedIndexBinding> {
        diagnostic = id;
        return {};
    };
    if (const auto* id = budget.validate(quota))
        return fail(id);
    if (!provider || !provider->sharedOwnerMatches(provider) || provider->quota() != quota
        || !provider->vertexMappingMatches() || !registry
        || registry != metalRhiIssuingResourceRegistry(device))
        return fail("RHI-METAL-ORDINARY-INDEX-ISSUER-MISMATCH");
    if (!describeMetalRhiNativeRuntimeBoundary().runtimeAvailableOnThisPlatform)
        return fail("RHI-METAL-ORDINARY-INDEX-NATIVE-RUNTIME-UNAVAILABLE");
    try {
        core::CpuMemoryQuotaScope scope(quota);
        rhi::IndexInputConsumptionContract content(data, bytes, format, order, restart);
        if (const auto* id = content.diagnostic())
            return fail(id);
        auto uploaded = MetalRhiImmutableBufferUpload::prepare(device,
            registry,
            provider->native()->nativeDevice,
            content.bytes(),
            rhi::ResourceUsage::IndexBuffer,
            indexUploadDiagnostics,
            diagnostic);
        if (!uploaded)
            return {};
        auto result = core::makeShared<MetalRhiOrdinaryPreparedIndexBinding>(ConstructionKey{},
            provider,
            registry,
            std::move(content),
            std::move(uploaded->buffer),
            std::move(uploaded->resource),
            budget,
            quota);
        if (const auto* id = result->validate())
            return fail(id);
        return result;
    } catch (const std::bad_alloc&) {
        core::CpuMemoryQuotaScope diagnosticScope(nullptr);
        return fail("RHI-METAL-ORDINARY-INDEX-PREPARATION-ALLOCATION-FAILED");
    }
}

std::shared_ptr<const MetalRhiOrdinaryPreparedIndexedDraw>
MetalRhiOrdinaryPreparedIndexedDraw::prepare(
    std::shared_ptr<const MetalRhiOrdinaryPreparedIndexBinding> index,
    std::uint64_t bindingOffset,
    std::uint32_t first,
    std::uint32_t count,
    std::int32_t base,
    std::uint32_t instances,
    std::uint32_t firstInstance,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    const auto fail =
        [&](const char* id) -> std::shared_ptr<const MetalRhiOrdinaryPreparedIndexedDraw> {
        diagnostic = id;
        return {};
    };
    if (!index)
        return fail("RHI-METAL-ORDINARY-INDEX-BINDING-MISSING");
    if (const auto* id = index->validate())
        return fail(id);
    const auto width = index->content().elementBytes();
    if (!instances || bindingOffset % width || bindingOffset >= index->content().bytes().size())
        return fail("RHI-METAL-ORDINARY-INDEX-DRAW-ARGUMENTS-INVALID");
    const auto skipped = bindingOffset / width;
    if (!core::containsUnsignedRange(skipped,
            index->content().elementCount() - skipped,
            std::uint64_t{first} + skipped,
            std::uint64_t{count}))
        return fail("RHI-METAL-ORDINARY-INDEX-DRAW-RANGE-INVALID");
    rhi::IndexInputConsumptionContract::DrawRange range;
    if (const auto* id = index->content().analyzeDraw(skipped + first, count, base, range))
        return fail(id);
    try {
        core::CpuMemoryQuotaScope scope(index->quota());
        return core::makeShared<MetalRhiOrdinaryPreparedIndexedDraw>(
            ConstructionKey{}, index, range, range.firstIndex * width, instances, firstInstance);
    } catch (const std::bad_alloc&) {
        core::CpuMemoryQuotaScope diagnosticScope(nullptr);
        return fail("RHI-METAL-ORDINARY-INDEX-DRAW-PREPARATION-ALLOCATION-FAILED");
    }
}

const char* MetalRhiOrdinaryPreparedVertexBinding::validate() const noexcept
{
    if (const auto* id = budget_.validate(quota_))
        return id;
    if (!provider_ || !provider_->sharedOwnerMatches(provider_) || provider_->quota() != quota_
        || !registry_ || !resource_.buffer || !provider_->vertexMappingMatches()
        || slot_ >= rhi::semantics::MetalVertexStreamMapping::capacity
        || provider_->native()->vertexStreams.slots[slot_] != physicalSlot_
        || physicalSlot_ == UINT32_MAX)
        return "RHI-METAL-ORDINARY-VERTEX-MAPPING-MISMATCH";
    const auto actual = registry_->find(id_);
    if (!actual || actual->kind != rhi::DescriptorResourceKind::Buffer
        || actual->buffer != resource_.buffer || actual->buffer.owner_before(resource_.buffer)
        || resource_.buffer.owner_before(actual->buffer)
        || actual->bufferUsage != resource_.bufferUsage
        || actual->bufferSizeBytes != resource_.bufferSizeBytes || !actual->payloadOwner()
        || actual->texture || actual->textureView || actual->sampler)
        return "RHI-METAL-ORDINARY-VERTEX-RESOURCE-MISMATCH";
    const auto& native = actual->buffer->resource;
    if (!native.nativeRuntimeBacked || native.nativeRuntimeDiagnosticOnly
        || !native.allocationReleaseToken || !observation_.matches(native)
        || native.nativeDevice != provider_->native()->nativeDevice)
        return "RHI-METAL-ORDINARY-VERTEX-NATIVE-IDENTITY-MISMATCH";
    if (!owners_.matches([&](auto&& visitor) {
            projectOwners(visitor);
        }))
        return "RHI-METAL-ORDINARY-VERTEX-OWNER-MISMATCH";
    return nullptr;
}

std::shared_ptr<const MetalRhiOrdinaryPreparedVertexBinding>
MetalRhiOrdinaryPreparedVertexBinding::prepare(
    std::shared_ptr<const MetalRhiOrdinaryGraphicsProvider> provider,
    const rhi::GpuBuffer& buffer,
    std::uint32_t slot,
    std::uint64_t offset,
    std::shared_ptr<MetalRhiResourceRegistry> registry,
    const MetalRhiOrdinaryBindingBudget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    const auto fail =
        [&](const char* id) -> std::shared_ptr<const MetalRhiOrdinaryPreparedVertexBinding> {
        diagnostic = id;
        return {};
    };
    if (const auto* id = budget.validate(quota))
        return fail(id);
    if (!provider || !provider->sharedOwnerMatches(provider) || provider->quota() != quota
        || !registry || !provider->vertexMappingMatches()
        || slot >= rhi::semantics::MetalVertexStreamMapping::capacity
        || !provider->vertexInput().findStream(slot)
        || provider->native()->vertexStreams.slots[slot] == UINT32_MAX)
        return fail("RHI-METAL-ORDINARY-VERTEX-MAPPING-MISMATCH");
    const auto actual = registry->find(buffer.id());
    if (!actual || actual->kind != rhi::DescriptorResourceKind::Buffer || !actual->buffer
        || actual->buffer != metalRhiNativePayloadFor(buffer) || !actual->payloadOwner()
        || actual->bufferSizeBytes != buffer.desc().sizeBytes
        || actual->bufferUsage != buffer.desc().usage
        || (actual->bufferUsage & rhi::ResourceUsage::VertexBuffer) == rhi::ResourceUsage::None)
        return fail("RHI-METAL-ORDINARY-VERTEX-RESOURCE-MISMATCH");
    // Metal's vertex buffer offset contract requires four-byte alignment.
    constexpr std::uint64_t offsetAlignment = sizeof(std::uint32_t);
    if (offset % offsetAlignment)
        return fail("RHI-METAL-ORDINARY-VERTEX-OFFSET-ALIGNMENT");
    if (const auto* id = provider->vertexInput().validateRange(
            slot, actual->bufferSizeBytes, offset, 1u, 0u, 1u, 0u))
        return fail(id);
    try {
        core::CpuMemoryQuotaScope scope(quota);
        auto result = core::makeShared<MetalRhiOrdinaryPreparedVertexBinding>(ConstructionKey{},
            provider,
            registry,
            buffer.id(),
            *actual,
            slot,
            offset,
            budget,
            quota);
        if (const auto* id = result->validate())
            return fail(id);
        return result;
    } catch (const std::bad_alloc&) {
        core::CpuMemoryQuotaScope diagnosticScope(nullptr);
        return fail("RHI-METAL-ORDINARY-VERTEX-PREPARATION-ALLOCATION-FAILED");
    }
}

MetalRhiOrdinaryPreparedRenderPass::MetalRhiOrdinaryPreparedRenderPass(ConstructionKey,
    std::shared_ptr<const MetalRhiOrdinaryGraphicsProvider> provider,
    MetalRhiNativeRenderPassPreparation native,
    std::shared_ptr<MetalRhiResourceRegistry> registry,
    std::shared_ptr<core::CpuMemoryQuota> quota)
    : provider_(std::move(provider)), native_(std::move(native)), registry_(std::move(registry)),
      quota_(std::move(quota))
{
    const auto add = [this](std::shared_ptr<const void> owner) {
        if (owner)
            owners_.push_back(std::move(owner));
    };
    MetalRhiOrdinaryGraphicsProvider::appendOwnerProjection(provider_, owners_);
    add(registry_);
    add(native_.descriptor);
    for (std::size_t i = 0; i < native_.attachmentCount; ++i) {
        const auto& snapshot = native_.attachments[i];
        const auto& attachment = snapshot.resource;
        add(attachment.payloadOwner());
        add(snapshot.nativeDevice);
        add(snapshot.nativeObject);
        add(snapshot.allocationToken);
        if (attachment.texture)
            add(attachment.texture->aliasHeap);
    }
}

const char* validateMetalRhiNativeRenderPassResources(
    const MetalRhiNativeRenderPassPreparation& native,
    const std::shared_ptr<MetalRhiResourceRegistry>& registry,
    const std::shared_ptr<void>& device) noexcept
{
    if (!registry || !device || !native.descriptor || !native.attachmentCount
        || native.attachmentCount > native.attachments.size())
        return "RHI-METAL-ORDINARY-PASS-PREPARATION-INVALID";
    for (std::size_t i = 0; i < native.attachmentCount; ++i) {
        const auto& snapshot = native.attachments[i];
        const auto actual = registry->find(snapshot.id);
        const auto& expected = snapshot.resource;
        if (!actual || (actual->textureView && !actual->parentTextureLive)
            || actual->gpuWritesSealed || actual->gpuTransferWritesSealed
            || actual->kind != rhi::DescriptorResourceKind::TextureView
            || !actual->nativeResource())
            return "RHI-METAL-ORDINARY-PASS-ATTACHMENT-NOT-LIVE-OR-SEALED";
        const auto owner = actual->payloadOwner();
        const auto captured = expected.payloadOwner();
        const auto& resource = *actual->nativeResource();
        if (!owner || owner != captured || owner.owner_before(captured)
            || captured.owner_before(owner)
            || !core::SharedOwnerProjection::sameOwner(resource.nativeDevice, device)
            || !core::SharedOwnerProjection::sameOwner(resource.nativeDevice, snapshot.nativeDevice)
            || !core::SharedOwnerProjection::sameOwner(resource.nativeObject, snapshot.nativeObject)
            || !core::SharedOwnerProjection::sameOwner(
                resource.allocationReleaseToken, snapshot.allocationToken)
            || actual->textureFormat != expected.textureFormat
            || actual->textureUsage != expected.textureUsage
            || actual->textureExtent.width != expected.textureExtent.width
            || actual->textureExtent.height != expected.textureExtent.height
            || actual->textureExtent.depth != expected.textureExtent.depth
            || actual->textureSampleCount != expected.textureSampleCount
            || actual->textureMipLevelCount != expected.textureMipLevelCount
            || actual->textureArrayLayerCount != expected.textureArrayLayerCount
            || actual->textureViewDimension != expected.textureViewDimension
            || actual->textureViewAspect != expected.textureViewAspect)
            return "RHI-METAL-ORDINARY-PASS-ATTACHMENT-IDENTITY-MISMATCH";
    }
    return nullptr;
}

rhi::ValidationResult MetalRhiOrdinaryPreparedRenderPass::validateObservation() const
{
    if (!provider_ || !quota_ || provider_->quota() != quota_ || !registry_ || !native_.descriptor
        || native_.attachmentCount == 0u || native_.attachmentCount > native_.attachments.size())
        return {false, "RHI-METAL-ORDINARY-PASS-PREPARATION-INVALID"};
    const auto provider = validateProvider(*provider_);
    if (!provider.success)
        return provider;
    if (const auto* id = validateMetalRhiNativeRenderPassResources(
            native_, registry_, provider_->native()->nativeDevice))
        return {false, id};
    return {true, {}};
}

rhi::ValidationResult MetalRhiOrdinaryPreparedRenderPass::validateProvider(
    const MetalRhiOrdinaryGraphicsProvider& provider) const
{
    const auto& pipeline = *provider.native();
    if (provider.quota() != quota_ || native_.colorCount != pipeline.colorFormats.size()
        || native_.depthFormat != pipeline.depthStencilFormat
        || native_.sampleCount != pipeline.sampleCount || native_.layered
        || native_.subpassCount != 1u)
        return {false, "RHI-METAL-ORDINARY-PASS-PROVIDER-MISMATCH"};
    for (std::size_t i = 0; i < native_.colorCount; ++i)
        if (native_.colorFormats[i] != pipeline.colorFormats[i])
            return {false, "RHI-METAL-ORDINARY-PASS-PROVIDER-MISMATCH"};
    return {true, {}};
}

MetalRhiOrdinaryPreparedGraphicsBinding::MetalRhiOrdinaryPreparedGraphicsBinding(ConstructionKey,
    std::shared_ptr<const MetalRhiOrdinaryGraphicsProvider> provider,
    std::shared_ptr<const MetalRhiNativePreparedGraphicsBindings> payload,
    const rhi::DescriptorSet& descriptor,
    std::shared_ptr<MetalRhiResourceRegistry> registry,
    MetalRhiOrdinaryBindingBudget budget,
    std::shared_ptr<core::CpuMemoryQuota> quota)
    : provider_(std::move(provider)), payload_(std::move(payload)),
      observation_(descriptor, std::move(registry)), budget_(budget), quota_(std::move(quota))
{
    owners_.capture([&](auto&& visitor) {
        projectOwners(visitor);
    });
}

rhi::ValidationResult MetalRhiOrdinaryPreparedGraphicsBinding::validateObservation() const
{
    if (const auto* id = budget_.validate(quota_))
        return {false, id};
    if (!provider_ || provider_->quota() != quota_ || !payload_ || !payload_->descriptors
        || payload_->pipeline != provider_->native())
        return {false, "RHI-METAL-ORDINARY-GRAPHICS-PROVIDER-IDENTITY-MISMATCH"};
    if (!provider_->consumption().matchesBindings(provider_->interface().reflection))
        return {false, "RHI-METAL-ORDINARY-GRAPHICS-REFLECTION-CAPTURE-MISMATCH"};
    if (!provider_->vertexMappingMatches())
        return {false, "RHI-METAL-ORDINARY-VERTEX-MAPPING-MISMATCH"};
    const auto observed = observation_.validate(*payload_->descriptors);
    if (!observed.success)
        return observed;
    for (const auto& stage : payload_->stages)
        if (stage && (!stage->pipeline || !stage->descriptors))
            return {false, "RHI-METAL-ORDINARY-OWNER-PROJECTION-MISMATCH"};
    if (!owners_.matches([&](auto&& visitor) {
            projectOwners(visitor);
        }))
        return {false, "RHI-METAL-ORDINARY-OWNER-PROJECTION-MISMATCH"};
    const auto layout = provider_->setLayouts().validate(payload_->set, *descriptor());
    if (!layout.success)
        return layout;
    for (std::size_t i = 0; i < payload_->layouts.size(); ++i) {
        const auto& expected = provider_->stages()[i];
        const auto& actual = payload_->layouts[i];
        if (static_cast<bool>(expected) != static_cast<bool>(actual)
            || (expected && !expected->matches(*actual)))
            return {false, "RHI-METAL-ORDINARY-GRAPHICS-STAGE-MAP-IDENTITY-MISMATCH"};
        if (!actual)
            continue;
        for (const auto& binding : payload_->descriptors->bindings) {
            if (!MetalRhiOrdinaryGraphicsResourceDomain::isReadOnlyBuffer(binding.type))
                continue;
            if ((binding.type == rhi::DescriptorType::ReadOnlyStorageBuffer
                    || binding.arrayCount > 1u)
                && binding.bufferOffsetBytes)
                return {false, "RHI-METAL-ORDINARY-OFFSET-DOMAIN-UNIMPLEMENTED"};
            MetalRhiPreparedBindingLocation location;
            const auto mapped = actual->resolve(
                binding, payload_->set, payload_->pipeline->nativeBindingStages, location);
            if (!mapped.success)
                return mapped;
            const auto& direct = binding.graphicsBufferRanges[i];
            const auto& pointee = binding.graphicsBufferPointeeRanges[i];
            if (!location.active) {
                if (direct.stage != rhi::ShaderStage::None
                    || pointee.stage != rhi::ShaderStage::None)
                    return {false, "RHI-METAL-ORDINARY-POINTEE-INACTIVE-CAPTURE-MISMATCH"};
                continue;
            }
            if (location.array) {
                if (!MetalRhiOrdinaryGraphicsResourceDomain::isReadOnlyBufferArray(
                        binding.type, binding.arrayCount)
                    || pointee.stage != actual->stage || direct.stage != rhi::ShaderStage::None)
                    return {false, "RHI-METAL-ORDINARY-POINTEE-CAPTURE-MISMATCH"};
                continue;
            }
            if (binding.arrayCount != 1u || direct.stage != actual->stage
                || pointee.stage != rhi::ShaderStage::None)
                return {false, "RHI-METAL-ORDINARY-OFFSET-CAPTURE-MISMATCH"};
        }
    }
    if (payload_->argumentCount > budget_.maxArgumentBuffers
        || payload_->argumentBytes > budget_.maxArgumentBufferBytes
        || payload_->encodedArgumentBytes > payload_->argumentBytes)
        return {false, "RHI-METAL-ORDINARY-GRAPHICS-ARGUMENT-BUDGET-MISMATCH"};
    core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
    if (!validateMetalRhiNativeGraphicsBindings(payload_, diagnostic))
        return {false, std::move(diagnostic)};
    return {true, {}};
}

MetalRhiOrdinaryPushBlock::MetalRhiOrdinaryPushBlock(const rhi::PushConstantsDesc& desc,
    MetalRhiOrdinaryBindingBudget budget,
    std::shared_ptr<core::CpuMemoryQuota> quota)
    : bytes_(static_cast<const std::uint8_t*>(desc.data),
          static_cast<const std::uint8_t*>(desc.data) + desc.sizeBytes,
          &core::defaultCpuMemoryResource()),
      stages_(desc.stages), budget_(budget), quota_(std::move(quota))
{}

rhi::ValidationResult MetalRhiOrdinaryPushBlock::validate() const
{
    if (const auto* id = budget_.validate(quota_))
        return {false, id};
    return rhi::semantics::validatePushConstantCommand(
        stages_, 0u, static_cast<std::uint32_t>(bytes_.size()), bytes_.data());
}

MetalRhiOrdinaryPreparedComputePush::MetalRhiOrdinaryPreparedComputePush(ConstructionKey,
    std::shared_ptr<MetalRhiNativePipelinePayload> pipeline,
    std::uint32_t slot,
    std::uint64_t alignment,
    const rhi::PushConstantsDesc& desc,
    MetalRhiOrdinaryBindingBudget budget,
    std::shared_ptr<core::CpuMemoryQuota> quota)
    : pipeline_(std::move(pipeline)), function_(pipeline_->computeFunction),
      block_(desc, budget, std::move(quota)), slot_(slot), alignment_(alignment)
{}

rhi::ValidationResult MetalRhiOrdinaryPreparedComputePush::validate() const
{
    const auto block = block_.validate();
    if (!block.success)
        return block;
    if (!pipeline_ || !pipeline_->nativeRuntimeBacked || !pipeline_->computePipelinePayloadPresent
        || !function_ || function_ != pipeline_->computeFunction || !pipeline_->pipelineState
        || slot_ != pipeline_->pushConstantMetalBufferIndex
        || !rhi::semantics::validMetalDirectBindingSlot(rhi::DescriptorType::UniformBuffer, slot_)
        || (pipeline_->nativeBindingStages.stages(rhi::DescriptorType::UniformBuffer, slot_)
               & rhi::ShaderStage::Compute)
            == rhi::ShaderStage::None)
        return {false, "[PC-E020] RHI-METAL-ORDINARY-PUSH-SLOT-PIPELINE-MISMATCH"};
    if (block_.stages() != rhi::ShaderStage::Compute)
        return {false, "[PC-E016] RHI-METAL-ORDINARY-PUSH-STAGE-MISMATCH"};
    const MetalRhiBufferBindingFact* fact = nullptr;
    if (const auto* id = validateMetalRhiBufferOffset(pipeline_->computeBufferFacts,
            pipeline_->computeBufferFactsVersion,
            slot_,
            0u,
            bytes().size(),
            fact)) {
        core::pmr::String message{&core::defaultCpuMemoryResource()};
        message = rhi::semantics::PushConstantDiagCode::kNativeSlot;
        message += id;
        return {false, std::move(message)};
    }
    if (fact->requiredBytes != bytes().size() || fact->alignment != alignment_)
        return {false, "[PC-E020] RHI-METAL-ORDINARY-PUSH-NATIVE-SIZE-MISMATCH"};
    return {true, {}};
}

namespace {
rhi::ValidationResult captureGraphicsPushRanges(const MetalRhiOrdinaryGraphicsProvider& provider,
    rhi::ShaderStage stages,
    std::uint32_t size,
    std::array<MetalRhiStageBufferRange, 2>& ranges)
{
    const auto& reflection = provider.interface().reflection;
    const auto semantic =
        rhi::semantics::validatePushConstantBinding(provider.consumption().pushRanges(),
            provider.native()->preparedGraphicsStages,
            stages,
            0u,
            size,
            &reflection);
    if (!semantic.success)
        return semantic;
    const auto active = rhi::parseShaderReflectionPushConstantStages(reflection.pushConstantStages);
    if (!active || *active != stages)
        return {false, "[PC-E016] RHI-METAL-ORDINARY-GRAPHICS-PUSH-STAGE-MISMATCH"};
    const auto& native = *provider.native();
    if (!native.nativeRuntimeBacked || !native.renderPipelinePayloadPresent || !native.pipelineState
        || !native.nativeDevice)
        return {false, "[PC-E011] RHI-METAL-ORDINARY-GRAPHICS-PUSH-PAYLOAD-MISSING"};
    for (std::size_t i = 0; i < ranges.size(); ++i) {
        const auto stage = i ? rhi::ShaderStage::Fragment : rhi::ShaderStage::Vertex;
        if ((stages & stage) == rhi::ShaderStage::None)
            continue;
        const auto& layout = provider.stages()[i];
        const auto& artifact = provider.artifacts()[i];
        const auto function = i ? native.fragmentFunction : native.vertexFunction;
        if (!layout || !artifact || !function || layout->stage != stage
            || layout->entry != artifact->entry()
            || artifact->module().reflection.pushConstantSizeBytes != size
            || layout->pushSlot != artifact->module().reflection.pushConstantMetalBufferIndex
            || layout->pushSlot != native.pushConstantMetalBufferIndex
            || !rhi::semantics::validMetalDirectBindingSlot(
                rhi::DescriptorType::UniformBuffer, layout->pushSlot)
            || (native.nativeBindingStages.stages(
                    rhi::DescriptorType::UniformBuffer, layout->pushSlot)
                   & stage)
                == rhi::ShaderStage::None)
            return {false, "[PC-E020] RHI-METAL-ORDINARY-GRAPHICS-PUSH-SLOT-PIPELINE-MISMATCH"};
        if (const auto* id = MetalRhiStageBufferRangeValidator::capture(
                native.graphicsBufferFacts[i], stage, layout->pushSlot, 0u, size, ranges[i])) {
            core::pmr::String message{&core::defaultCpuMemoryResource()};
            message = rhi::semantics::PushConstantDiagCode::kNativeSlot;
            message += id;
            return {false, std::move(message)};
        }
        if (ranges[i].fact.requiredBytes != size)
            return {false, "[PC-E020] RHI-METAL-ORDINARY-GRAPHICS-PUSH-NATIVE-SIZE-MISMATCH"};
    }
    return {true, {}};
}
} // namespace

MetalRhiOrdinaryPreparedGraphicsPush::MetalRhiOrdinaryPreparedGraphicsPush(ConstructionKey,
    std::shared_ptr<const MetalRhiOrdinaryGraphicsProvider> provider,
    const rhi::PushConstantsDesc& desc,
    MetalRhiOrdinaryBindingBudget budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    std::array<MetalRhiStageBufferRange, 2> ranges)
    : provider_(std::move(provider)), block_(desc, budget, std::move(quota)),
      ranges_(std::move(ranges))
{
    MetalRhiOrdinaryGraphicsProvider::appendOwnerProjection(provider_, owners_);
}

std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsPush>
MetalRhiOrdinaryPreparedGraphicsPush::prepare(
    std::shared_ptr<const MetalRhiOrdinaryGraphicsProvider> provider,
    const rhi::PushConstantsDesc& desc,
    const MetalRhiOrdinaryBindingBudget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    if (const auto* id = budget.validate(quota)) {
        diagnostic = id;
        return {};
    }
    if (!provider || provider->quota() != quota) {
        diagnostic = "RHI-METAL-ORDINARY-GRAPHICS-PUSH-PROVIDER-MISMATCH";
        return {};
    }
    const auto command = rhi::semantics::validatePushConstantCommand(
        desc.stages, desc.offsetBytes, desc.sizeBytes, desc.data);
    if (!command.success) {
        diagnostic = command.diagnostics;
        return {};
    }
    std::array<MetalRhiStageBufferRange, 2> ranges;
    const auto captured = captureGraphicsPushRanges(*provider, desc.stages, desc.sizeBytes, ranges);
    if (!captured.success) {
        diagnostic = captured.diagnostics;
        return {};
    }
    try {
        core::CpuMemoryQuotaScope scope(quota);
        return core::makeShared<MetalRhiOrdinaryPreparedGraphicsPush>(ConstructionKey{},
            std::move(provider),
            desc,
            budget,
            std::move(quota),
            std::move(ranges));
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-COMMAND-RETENTION-PREPARATION-ALLOCATION-FAILED";
        return {};
    }
}

rhi::ValidationResult MetalRhiOrdinaryPreparedGraphicsPush::validate() const
{
    const auto block = block_.validate();
    if (!block.success)
        return block;
    if (!provider_ || provider_->quota() != quota())
        return {false, "RHI-METAL-ORDINARY-GRAPHICS-PUSH-PROVIDER-MISMATCH"};
    std::size_t index = 0;
    for (const auto& owner : MetalRhiOrdinaryGraphicsProvider::ownerProjection(provider_)) {
        if (!owner)
            continue;
        if (index >= owners_.size() || owner != owners_[index] || owner.owner_before(owners_[index])
            || owners_[index].owner_before(owner))
            return {false, "[PC-E020] RHI-METAL-ORDINARY-GRAPHICS-PUSH-NATIVE-IDENTITY-MISMATCH"};
        ++index;
    }
    if (index != owners_.size())
        return {false, "[PC-E020] RHI-METAL-ORDINARY-GRAPHICS-PUSH-NATIVE-IDENTITY-MISMATCH"};
    std::array<MetalRhiStageBufferRange, 2> current;
    const auto captured = captureGraphicsPushRanges(
        *provider_, block_.stages(), static_cast<std::uint32_t>(bytes().size()), current);
    if (!captured.success)
        return captured;
    for (std::size_t i = 0; i < current.size(); ++i) {
        const auto& a = current[i];
        const auto& b = ranges_[i];
        if (a.stage != b.stage || a.fact.slot != b.fact.slot || a.fact.alignment != b.fact.alignment
            || a.fact.requiredBytes != b.fact.requiredBytes || a.offset != b.offset)
            return {false, "[PC-E020] RHI-METAL-ORDINARY-GRAPHICS-PUSH-CAPTURE-MISMATCH"};
    }
    return {true, {}};
}

MetalRhiOrdinaryPreparedComputeBinding::MetalRhiOrdinaryPreparedComputeBinding(ConstructionKey,
    std::shared_ptr<const MetalRhiNativePreparedComputeBindings> function,
    const rhi::ComputePipeline& pipeline,
    const rhi::DescriptorSet& descriptor,
    std::shared_ptr<MetalRhiResourceRegistry> registry,
    MetalRhiOrdinaryBindingBudget budget,
    std::shared_ptr<core::CpuMemoryQuota> quota)
    : function_(std::move(function)),
      consumption_(pipeline.layout()->desc(), pipeline.shaderReflection()),
      observation_(descriptor, std::move(registry)), budget_(budget), quota_(std::move(quota)),
      pushBytes_(pipeline.shaderReflection().pushConstantByteCount)
{
    owners_.capture([&](auto&& visitor) {
        MetalRhiPreparedFunctionOwnership::visit(function_, observation_.registry(), visitor);
    });
}

rhi::ValidationResult MetalRhiOrdinaryPreparedComputeBinding::validateObservation() const
{
    if (const auto* id = budget_.validate(quota_))
        return {false, id};
    if (!function_ || !function_->pipeline || !function_->descriptors)
        return {false, "RHI-METAL-ORDINARY-OWNER-PROJECTION-MISMATCH"};
    const auto resources = observation_.validate(*function_->descriptors);
    if (!resources.success)
        return resources;
    if (!owners_.matches([&](auto&& visitor) {
            MetalRhiPreparedFunctionOwnership::visit(function_, observation_.registry(), visitor);
        }))
        return {false, "RHI-METAL-ORDINARY-OWNER-PROJECTION-MISMATCH"};
    return {true, {}};
}

rhi::ValidationResult MetalRhiOrdinaryDescriptorObservation::validate(
    const MetalRhiNativeDescriptorGroupPayload& snapshot) const
{
    if (const auto* id = MetalRhiPreparedDescriptorIdentityValidator::validate(snapshot))
        return {false, id};
    const auto* object = descriptor_.get();
    if (!object)
        return {false, "RHI-DESCRIPTOR-OBJECT-NOT-LIVE: ordinary prepared binding"};
    if (!registry_ || !metalRhiDescriptorRegistryMatches(*object, registry_))
        return {false, "RHI-METAL-ORDINARY-PREPARED-REGISTRY-MISMATCH"};
    const auto live = rhi::validateDescriptorResources(
        *object, rhi::DescriptorContentRequirement::WrittenElements);
    if (!live.success)
        return live;
    if (object->contentRevision() != revision_)
        return {false, "RHI-DESCRIPTOR-CONTENT-CHANGED: prepare a new ordinary binding"};
    for (const auto& binding : snapshot.bindings) {
        const auto resource = registry_->find(binding.resourceId);
        const auto expected = binding.payloadOwner();
        const auto resolved = resource ? resource->payloadOwner() : nullptr;
        if (!expected || !resolved || expected != resolved || expected.owner_before(resolved)
            || resolved.owner_before(expected) || resource->kind != binding.resourceKind)
            return {false, "RHI-METAL-ORDINARY-PREPARED-RESOURCE-IDENTITY-MISMATCH"};
        if (binding.type == rhi::DescriptorType::StorageTexture) {
            const auto* declared = rhi::findDescriptorSetBinding(object->layout(), binding.binding);
            if (!declared || !binding.textureView)
                return {false, "RHI-DESCRIPTOR-STORAGE-FORMAT-MISSING"};
            if (const auto* id = rhi::validateStorageTextureViewFormat(
                    declared->storageTextureFormat, resource->textureFormat))
                return {false, id};
            if (const auto* id = rhi::validateStorageTextureViewFormat(
                    declared->storageTextureFormat, binding.textureView->format))
                return {false, id};
        }
    }
    return {true, {}};
}
} // namespace arp::rhi_metal

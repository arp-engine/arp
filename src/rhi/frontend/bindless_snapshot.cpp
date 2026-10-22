#include "rhi/frontend/bindless_snapshot.h"

#include "core/math/unsigned_range.h"
#include "rhi/frontend/bindless_snapshot_allocation.h"
#include "rhi/frontend/bindless_snapshot_metadata.h"
#include "rhi/frontend/command_indirect_commands.h"
#include "rhi/frontend/completed_depth_texture_source.h"
#include "rhi/frontend/descriptor_resource_validation.h"
#include "rhi/gpu_extensions.h"
#include "rhi/semantics/bindless_original_preparation_lowering.h"
#include "rhi/semantics/command_lowering.h"

#include <cassert>
#include <limits>

namespace arp::rhi {

std::uint64_t BindlessCrossExtension::queryOriginalMixedSnapshotAllocation(
    const BindlessShaderArtifact& artifact, std::uint32_t version, const char*& diagnostic) const
{
    diagnostic = BindlessSnapshotAllocationContract::validate(artifact, version);
    if (diagnostic)
        return 0u;
    diagnostic = "RHI-BINDLESS-SNAPSHOT-ALLOCATION-UNAVAILABLE";
    const auto& caps = capabilities();
    if ((version == 1u
            && !semantics::OriginalBindlessPreparationLowering::capabilityAvailable(caps, 9u))
        || (version == 2u && (!caps.limitsQueried || !caps.deviceIdentity)))
        return 0u;
    std::uint64_t total = 0u;
    for (const auto& desc : BindlessSnapshotAllocationContract::buffers(artifact)) {
        const auto bytes = queryOriginalSnapshotBufferAllocation(desc, diagnostic);
        if (diagnostic)
            return 0u;
        if (bytes < desc.sizeBytes || bytes > std::numeric_limits<std::uint64_t>::max() - total) {
            diagnostic = "RHI-BINDLESS-SNAPSHOT-ALLOCATION-ACCOUNTING";
            return 0u;
        }
        total += bytes;
    }
    return std::max(total, artifact.snapshotBytes);
}

GpuBuffer* BindlessSnapshot::internalBuffer(OriginalBufferRole role) const noexcept
{
    const auto index = static_cast<std::size_t>(role);
    if (index >= originalBufferSources_.size())
        return nullptr;
    if (const auto& source = originalBufferSources_[index])
        // The lease adopted this snapshot's non-const original UniquePtr. Keep
        // the existing borrowed API; mutation restrictions remain on GpuBuffer.
        return const_cast<GpuBuffer*>(source->originalObject());
    switch (role) {
    case OriginalBufferRole::Metadata:
        return metadataBuffer_.get();
    case OriginalBufferRole::FaultLatch:
        return faultLatchBuffer_.get();
    case OriginalBufferRole::FaultReadback:
        return faultReadbackBuffer_.get();
    case OriginalBufferRole::Count:
        return nullptr;
    }
    return nullptr;
}

const std::shared_ptr<const OriginalCommandBufferLease>& BindlessSnapshot::originalBufferSource(
    OriginalBufferRole role) const noexcept
{
    static const std::shared_ptr<const OriginalCommandBufferLease> absent;
    const auto index = static_cast<std::size_t>(role);
    return index < originalBufferSources_.size() ? originalBufferSources_[index] : absent;
}

const char* BindlessSnapshot::prepareOriginalBufferSources(
    const std::shared_ptr<BindlessSnapshot>& self,
    const GpuDevice& device,
    const OriginalCommandResourceLeaseBudget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota) noexcept
{
    if (const auto* id =
            semantics::OriginalBindlessBufferSourceLowering::lowering(device.backend()).diagnostic)
        return id;
    if (const auto* id = OriginalCommandBufferLease::validateBudget(budget))
        return id;
    if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes)
        return "RHI-ORIGINAL-SOURCE-LEASE-QUOTA";
    if (!self || !self->sharedOwnerMatches(self))
        return "RHI-BINDLESS-ORIGINAL-BUFFERS-SNAPSHOT-OWNER";
    if (!self->sealed_ || !self->set_)
        return "RHI-BINDLESS-SNAPSHOT-NOT-SEALED";
    if (!self->set_->isCurrentAllocation())
        return "RHI-BINDLESS-ORIGINAL-BUFFERS-ALLOCATION-STALE";
    if (const auto* id = device.originalResourceRegistrationDiagnostic())
        return id;
    if (!self->countLease_ || !self->byteLease_ || !self->countLease_->charged()
        || !self->byteLease_->charged())
        return "RHI-BINDLESS-ORIGINAL-BUFFERS-CAPACITY-RELEASED";
    const OriginalCommandBufferLease::CapacityOwners capacityOwners{
        self->countLease_, self->byteLease_};
    std::array<core::UniquePtr<GpuBuffer>*, 3u> candidates{
        &self->metadataBuffer_, &self->faultLatchBuffer_, &self->faultReadbackBuffer_};
    std::array<std::shared_ptr<OriginalCommandBufferLease>, 3u> cold{};
    // Validate the entire original device/quota closure before allocating or
    // moving a child. Repeated preparation can only reuse the same quota owner.
    for (std::size_t i = 0u; i < candidates.size(); ++i) {
        if (const auto& original = self->originalBufferSources_[i]) {
            if (const auto* id = OriginalCommandBufferLease::validateOriginal(original))
                return id;
            if (!core::SharedOwnerProjection::sameOwner(quota, original->originalMetadataQuota()))
                return "RHI-BINDLESS-ORIGINAL-BUFFERS-QUOTA-OWNER";
        }
        if (const auto* buffer = self->internalBuffer(static_cast<OriginalBufferRole>(i)))
            if (const auto* id = device.validateOriginalResourceRegistration(*buffer))
                return id;
    }
    for (std::size_t i = 0u; i < candidates.size(); ++i) {
        if (!*candidates[i])
            continue;
        const char* diagnostic = nullptr;
        cold[i] = OriginalCommandBufferLease::prepareColdOriginal(
            *candidates[i], budget, quota, diagnostic, capacityOwners);
        if (!cold[i])
            return diagnostic;
    }
    for (std::size_t i = 0u; i < candidates.size(); ++i)
        if (cold[i])
            if (const auto* id =
                    OriginalCommandBufferLease::validateColdOriginal(cold[i], **candidates[i]))
                return id;
    // No fallible work remains. Externally serialized snapshot preparation
    // preserves every preflighted candidate identity throughout this commit.
    for (std::size_t i = 0u; i < candidates.size(); ++i)
        if (cold[i]) {
            const auto* diagnostic =
                OriginalCommandBufferLease::adoptOriginal(cold[i], *candidates[i]);
            assert(!diagnostic);
            self->originalBufferSources_[i] = std::move(cold[i]);
        }
    return nullptr;
}

core::UniquePtr<BindlessSnapshot> BindlessSnapshot::create(GpuDevice& device,
    const BindlessRequirements& request,
    const BindlessShaderArtifact& artifact,
    const BindlessLoweringPlan& selected,
    core::pmr::String& diagnostic,
    bool originalDescriptors,
    bool retainFaultReadback)
{
    const auto* extension =
        static_cast<const BindlessCrossExtension*>(device.queryInterface(kExtensionBindlessCross));
    if (!extension) {
        diagnostic = "RHI-BINDLESS-EXTENSION-UNAVAILABLE";
        return {};
    }
    const auto currentPlan = extension->plan(request, artifact);
    if (!currentPlan.success) {
        diagnostic = currentPlan.diagnosticId;
        return {};
    }
    if (!bindlessPlansMatch(selected, currentPlan)) {
        diagnostic = "RHI-BINDLESS-PLAN-STALE";
        return {};
    }
    const auto& plan = selected;
    const auto issuance = extension->validateSnapshotCreation(plan);
    if (!issuance.success) {
        diagnostic = issuance.diagnostics;
        return {};
    }
    std::uint64_t total = 0;
    for (const auto& binding : artifact.physicalLayout.bindings)
        total += binding.arrayCount;
    if (total > std::numeric_limits<std::uint32_t>::max()) {
        diagnostic = "RHI-BINDLESS-DESCRIPTOR-COUNT-OVERFLOW";
        return {};
    }
    auto result = core::makeUnique<BindlessSnapshot>();
    result->originalBufferRanges_.resize(static_cast<std::size_t>(total));
    result->metadataLayout_ = artifact.snapshotMetadata;
    result->faultLatchLayout_ = artifact.faultLatch;
    result->indirectInstanceConsumer_ = artifact.indirectInstanceConsumer;
    const auto internalBuffers = BindlessSnapshotAllocationContract::buffers(artifact);
    if (artifact.snapshotMetadata.contractVersion) {
        result->metadataWords_.resize(artifact.snapshotMetadata.wordCount);
        // Metadata is built before publication and uploaded once at seal.
        // Native snapshot issuance remains a separate execution gate.
        result->metadataBuffer_ = device.createBuffer(internalBuffers[0u]);
        if (!result->metadataBuffer_) {
            diagnostic = "RHI-BINDLESS-METADATA-CREATION-FAILED";
            return {};
        }
    }
    if (artifact.faultLatch.contractVersion) {
        result->faultLatchBuffer_ = device.createBuffer(internalBuffers[1u]);
        if (!result->faultLatchBuffer_) {
            diagnostic = "RHI-BINDLESS-FAULT-LATCH-CREATION-FAILED";
            return {};
        }
    }
    if (retainFaultReadback || extension->capabilities().faultReadbackContractVersion == 1u) {
        if (artifact.faultLatch.contractVersion != 1u) {
            diagnostic = "RHI-BINDLESS-FAULT-READBACK-LATCH-MISSING";
            return {};
        }
        result->faultReadbackBuffer_ = device.createBuffer(internalBuffers[2u]);
        if (!result->faultReadbackBuffer_) {
            diagnostic = "RHI-BINDLESS-FAULT-READBACK-CREATION-FAILED";
            return {};
        }
    }
    for (const auto* buffer : {result->metadataBuffer_.get(),
             result->faultLatchBuffer_.get(),
             result->faultReadbackBuffer_.get()}) {
        if (!buffer)
            continue;
        const auto reported = buffer->allocation().estimatedSizeBytes;
        if (reported < buffer->desc().sizeBytes) {
            diagnostic = "RHI-BINDLESS-INTERNAL-RESOURCE-ACCOUNTING-MISSING";
            return {};
        }
        if (reported > artifact.snapshotBytes - result->reportedInternalBufferBytes_) {
            diagnostic = "RHI-BINDLESS-RESIDENT-FOOTPRINT-EXCEEDED";
            return {};
        }
        result->reportedInternalBufferBytes_ += reported;
    }
    const DescriptorArenaDesc arenaDesc{
        artifact.physicalLayout.name, 1u, static_cast<std::uint32_t>(total)};
    result->arena_ =
        core::shareUnique(originalDescriptors ? device.createOriginalDescriptorArena(arenaDesc)
                                              : device.createDescriptorArena(arenaDesc));
    if (!result->arena_) {
        diagnostic = "RHI-BINDLESS-ARENA-CREATION-FAILED";
        return {};
    }
    const DescriptorSetDesc setDesc{artifact.physicalLayout.name, artifact.physicalLayout};
    result->set_ = originalDescriptors
        ? device.allocateOriginalDescriptorSet(result->arena_, setDesc)
        : result->arena_->allocateDescriptorSet(setDesc);
    if (!result->set_) {
        diagnostic = "RHI-BINDLESS-SET-CREATION-FAILED";
        return {};
    }
    result->tables_.reserve(request.tables.size());
    for (const auto& table : request.tables)
        for (const auto& mapping : artifact.tables)
            if (table.tableId == mapping.tableId) {
                const auto& caps = extension->capabilities();
                const auto maxSize = table.binding.type == DescriptorType::UniformBuffer
                    ? caps.maxUniformBufferBindingSize
                    : caps.maxStorageBufferBindingSize;
                result->tables_.push_back({mapping,
                    table.binding.type,
                    table.resourceCount,
                    table.binding.stages,
                    maxSize});
            }
    result->accesses_.resize(static_cast<std::size_t>(total));
    result->writes_.resize(static_cast<std::size_t>(total));
    result->completedTextures_.resize(static_cast<std::size_t>(total));
    result->plan_ = plan;
    result->requirements_ = request;
    result->maxUniformBufferBindingSize_ = extension->capabilities().maxUniformBufferBindingSize;
    result->maxStorageBufferBindingSize_ = extension->capabilities().maxStorageBufferBindingSize;
    result->setIndex_ = artifact.set;
    result->maxUpdates_ = request.budget.maxUpdatesPerSnapshot;
    result->generation_ = allocateGpuResourceId().value;
    if (!result->generation_) {
        diagnostic = "RHI-BINDLESS-GENERATION-EXHAUSTED";
        return {};
    }
    diagnostic.clear();
    return result;
}

BindlessResourceHandle BindlessSnapshot::handle(
    std::uint32_t tableId, std::uint32_t index) const noexcept
{
    for (const auto& table : tables_)
        if (table.mapping.tableId == tableId && index < table.resourceCount)
            return {generation_, tableId, index};
    return {};
}

ValidationResult BindlessSnapshot::write(
    BindlessResourceHandle handle, const DescriptorBindingWrite& write)
{
    return writeImpl(handle, write, nullptr, {}, nullptr, nullptr);
}

ValidationResult BindlessSnapshot::writeImpl(BindlessResourceHandle handle,
    const DescriptorBindingWrite& write,
    const GpuDevice* device,
    const std::shared_ptr<const OriginalCommandTextureViewSource>& source,
    const GpuTexture::ContentExpectation* content,
    const OriginalCompletedTextureDescriptorRequest* request,
    const std::shared_ptr<const OriginalCommandBufferLease>& buffer,
    const OriginalBufferRangeDescriptorRequest* rangeRequest,
    GpuDevice* rangeDevice,
    const std::shared_ptr<const OriginalCompletedDepthTextureSource>& depth,
    const GpuTexture::ContentRangeExpectation* cellIdentity,
    const OriginalTextureCellDescriptorRequest* cellRequest)
{
    if (sealed_)
        return {false, "RHI-BINDLESS-SNAPSHOT-IMMUTABLE"};
    if (updates_ >= maxUpdates_)
        return {false, "RHI-BINDLESS-UPDATE-BUDGET"};
    if (!set_ || !generation_ || handle.generation != generation_)
        return {false, "RHI-BINDLESS-HANDLE-STALE"};
    for (const auto& table : tables_) {
        if (table.mapping.tableId != handle.tableId)
            continue;
        if (handle.index >= table.resourceCount || write.type != table.type
            || write.physicalLowering.enabled)
            return {false, "RHI-BINDLESS-WRITE-INVALID"};
        auto physical = write;
        const bool expanded = plan_.bindingModel == BindlessBindingModel::ScalarExpansion;
        physical.binding = table.mapping.firstBinding + (expanded ? handle.index : 0u);
        physical.arrayElement = expanded ? 0u : handle.index;
        auto result = device && cellIdentity && cellRequest
            ? set_->writeOriginalTextureCell(*device, physical, source, *cellIdentity, *cellRequest)
            : device && depth && request
            ? set_->writeOriginalCompletedTexture(*device, physical, depth, *request)
            : device && content && request
            ? set_->writeOriginalCompletedTexture(*device, physical, source, *content, *request)
            : rangeDevice && buffer && rangeRequest
            ? set_->writeOriginalBufferRange(*rangeDevice,
                  physical,
                  buffer,
                  table.mapping.bufferLayout.minBindingSizeBytes,
                  *rangeRequest)
            : set_->write(physical);
        if (result.success) {
            const auto element = physicalElement(table, handle.index);
            auto& publication = completedTextures_[element];
            publication.source.reset();
            publication.content.reset();
            publication.depth.reset();
            publication.request = {};
            publication.cellIdentity.reset();
            publication.cellRequest = {};
            auto& range = originalBufferRanges_[element];
            range.source = buffer;
            range.request = rangeRequest ? *rangeRequest : OriginalBufferRangeDescriptorRequest{};
            if (device && request && (content || depth)) {
                publication.source = source;
                if (content)
                    publication.content.emplace(*content);
                publication.depth = depth;
                publication.request = *request;
            }
            if (device && cellIdentity && cellRequest) {
                publication.source = source;
                publication.cellIdentity.emplace(*cellIdentity);
                publication.cellRequest = *cellRequest;
            }
            ++updates_;
            accesses_[physicalElement(table, handle.index)] = {};
            writes_[physicalElement(table, handle.index)] = physical;
        }
        return result;
    }
    return {false, "RHI-BINDLESS-TABLE-UNKNOWN"};
}

const BindlessSnapshot::Table* BindlessSnapshot::findTable(
    BindlessResourceHandle handle) const noexcept
{
    if (!generation_ || handle.generation != generation_)
        return nullptr;
    for (const auto& table : tables_)
        if (table.mapping.tableId == handle.tableId && handle.index < table.resourceCount)
            return &table;
    return nullptr;
}

std::uint32_t BindlessSnapshot::physicalElement(
    const Table& table, std::uint32_t index) const noexcept
{
    const bool expanded = plan_.bindingModel == BindlessBindingModel::ScalarExpansion;
    return descriptorElementIndex(
        set_->layout(), table.mapping.firstBinding + (expanded ? index : 0), expanded ? 0 : index);
}

namespace {
ResourceAccess operationAccess(BindlessOperation operations)
{
    const auto bits = static_cast<std::uint32_t>(operations);
    auto access = ResourceAccess::None;
    if (bits & ~static_cast<std::uint32_t>(BindlessOperation::Store))
        access = access | ResourceAccess::ShaderRead;
    if (bits & static_cast<std::uint32_t>(BindlessOperation::Store | BindlessOperation::Atomic))
        access = access | ResourceAccess::ShaderWrite;
    return access;
}
} // namespace

ValidationResult BindlessSnapshot::writeBuffer(
    BindlessResourceHandle handle, const GpuBuffer& buffer)
{
    const auto* table = findTable(handle);
    if (!table)
        return {false, "RHI-BINDLESS-HANDLE-STALE"};
    if (table->type != DescriptorType::UniformBuffer && table->type != DescriptorType::StorageBuffer
        && table->type != DescriptorType::ReadOnlyStorageBuffer)
        return {false, "RHI-BINDLESS-WRITE-INVALID"};
    const auto& shape = table->mapping.bufferLayout;
    const auto size = buffer.desc().sizeBytes;
    const auto usage = table->type == DescriptorType::UniformBuffer ? ResourceUsage::UniformBuffer
                                                                    : ResourceUsage::StorageBuffer;
    if ((buffer.desc().usage & usage) != usage)
        return {false, "RHI-BINDLESS-BUFFER-USAGE-MISMATCH"};
    if (size < shape.minBindingSizeBytes || size > table->maxBufferBindingSize
        || size % shape.elementStrideBytes
        || size / shape.elementStrideBytes > std::numeric_limits<std::uint32_t>::max())
        return {false, "RHI-BINDLESS-BUFFER-RANGE-INVALID"};
    const auto access = operationAccess(table->mapping.operations);
    const auto state = (access & ResourceAccess::ShaderWrite) != ResourceAccess::None
        ? ResourceState::ShaderWrite
        : ResourceState::ShaderRead;
    DescriptorBindingWrite payload;
    payload.type = table->type;
    payload.resource = buffer.id();
    payload.resourceKind = DescriptorResourceKind::Buffer;
    payload.resourceState = state;
    auto result = write(handle, payload);
    if (result.success) {
        auto& record = accesses_[physicalElement(*table, handle.index)];
        record.resource = buffer.id();
        record.kind = BindlessAccessKind::Buffer;
        record.stages = table->stages;
        record.pipelineStages = bindlessPipelineStagesForShader(table->stages);
        record.access = access;
        record.state = state;
        record.bufferSize = buffer.desc().sizeBytes;
    }
    return result;
}

ValidationResult BindlessSnapshot::writeOriginalBufferRange(BindlessResourceHandle handle,
    GpuDevice& device,
    const std::shared_ptr<const OriginalCommandBufferLease>& source,
    const DescriptorBufferBindingRange& range,
    const OriginalBufferRangeDescriptorRequest& request)
{
    const auto* table = findTable(handle);
    if (!table)
        return {false, "RHI-BINDLESS-HANDLE-STALE"};
    if (table->type != DescriptorType::UniformBuffer && table->type != DescriptorType::StorageBuffer
        && table->type != DescriptorType::ReadOnlyStorageBuffer)
        return {false, "RHI-BINDLESS-WRITE-INVALID"};
    if (const auto* id = OriginalBufferRangeDescriptorContract::validateIntent(request))
        return {false, id};
    if (const auto* id = OriginalCommandBufferLease::validateOriginal(source))
        return {false, id};
    const auto& shape = table->mapping.bufferLayout;
    if (range.contractVersion != 2u || range.sizeBytes < shape.minBindingSizeBytes
        || range.sizeBytes > table->maxBufferBindingSize || !shape.elementStrideBytes
        || !shape.elementAlignmentBytes || range.offsetBytes % shape.elementAlignmentBytes
        || range.sizeBytes % shape.elementStrideBytes
        || range.sizeBytes / shape.elementStrideBytes > UINT32_MAX)
        return {false, "RHI-BINDLESS-BUFFER-RANGE-INVALID"};
    const auto access = operationAccess(table->mapping.operations);
    const auto state = (access & ResourceAccess::ShaderWrite) != ResourceAccess::None
        ? ResourceState::ShaderWrite
        : ResourceState::ShaderRead;
    DescriptorBindingWrite payload;
    payload.type = table->type;
    payload.resource = source->resource();
    payload.resourceKind = DescriptorResourceKind::Buffer;
    payload.resourceState = state;
    payload.bufferBindingRange = range;
    auto result =
        writeImpl(handle, payload, &device, {}, nullptr, nullptr, source, &request, &device);
    if (result.success) {
        auto& captured = accesses_[physicalElement(*table, handle.index)];
        captured.resource = source->resource();
        captured.kind = BindlessAccessKind::Buffer;
        captured.stages = table->stages;
        captured.pipelineStages = bindlessPipelineStagesForShader(table->stages);
        captured.access = access;
        captured.state = state;
        captured.bufferOffset = range.offsetBytes;
        captured.bufferSize = range.sizeBytes;
    }
    return result;
}

ValidationResult BindlessSnapshot::writeTexture(
    BindlessResourceHandle handle, const GpuTextureView& view, ResourceState state)
{
    const auto* table = findTable(handle);
    if (!table)
        return {false, "RHI-BINDLESS-HANDLE-STALE"};
    if (table->type != DescriptorType::SampledTexture
        && table->type != DescriptorType::StorageTexture)
        return {false, "RHI-BINDLESS-WRITE-INVALID"};
    if (table->type == DescriptorType::StorageTexture) {
        const auto physicalBinding = table->mapping.firstBinding
            + (plan_.bindingModel == BindlessBindingModel::ScalarExpansion ? handle.index : 0u);
        for (const auto& binding : set_->layout().bindings)
            if (binding.binding == physicalBinding
                && (binding.storageTextureFormat == TextureFormat::Unknown
                    || binding.storageTextureFormat != view.desc().format))
                return {false, "RHI-BINDLESS-STORAGE-FORMAT-MISMATCH"};
    }
    const auto access = operationAccess(table->mapping.operations);
    if (((access & ResourceAccess::ShaderWrite) != ResourceAccess::None
            && state != ResourceState::ShaderWrite)
        || (state != ResourceState::ShaderRead && state != ResourceState::ShaderWrite))
        return {false, "RHI-BINDLESS-ACCESS-STATE"};
    DescriptorBindingWrite payload;
    payload.type = table->type;
    payload.resource = view.id();
    payload.resourceKind = DescriptorResourceKind::TextureView;
    payload.resourceState = state;
    auto result = write(handle, payload);
    if (result.success) {
        captureTextureAccess(*table, handle, view, state, access);
    }
    return result;
}

ValidationResult BindlessSnapshot::writeOriginalCompletedTexture(BindlessResourceHandle handle,
    const GpuDevice& device,
    const std::shared_ptr<const OriginalCommandTextureViewSource>& source,
    const GpuTexture::ContentExpectation& content,
    const OriginalCompletedTextureDescriptorRequest& request)
{
    const auto* table = findTable(handle);
    if (!table)
        return {false, "RHI-BINDLESS-HANDLE-STALE"};
    if (table->type != DescriptorType::SampledTexture
        || operationAccess(table->mapping.operations) != ResourceAccess::ShaderRead)
        return {false, "RHI-COMPLETED-TEXTURE-DESCRIPTOR-TABLE"};
    if (const auto* id = OriginalCompletedTextureDescriptorContract::validateSource(
            source, content, request.contractVersion))
        return {false, id};
    DescriptorBindingWrite payload;
    payload.type = DescriptorType::SampledTexture;
    payload.resource = source->resource();
    payload.resourceKind = DescriptorResourceKind::TextureView;
    payload.resourceState = ResourceState::ShaderRead;
    auto result = writeImpl(handle, payload, &device, source, &content, &request);
    if (result.success)
        captureTextureAccess(*table,
            handle,
            *source->originalObject(),
            ResourceState::ShaderRead,
            ResourceAccess::ShaderRead);
    return result;
}

ValidationResult BindlessSnapshot::writeOriginalCompletedTexture(BindlessResourceHandle handle,
    const GpuDevice& device,
    const std::shared_ptr<const OriginalCompletedDepthTextureSource>& depth,
    const OriginalCompletedTextureDescriptorRequest& request)
{
    const auto* table = findTable(handle);
    if (!table)
        return {false, "RHI-BINDLESS-HANDLE-STALE"};
    if (table->type != DescriptorType::SampledTexture
        || operationAccess(table->mapping.operations) != ResourceAccess::ShaderRead)
        return {false, "RHI-COMPLETED-TEXTURE-DESCRIPTOR-TABLE"};
    if (const auto* id = OriginalCompletedTextureDescriptorContract::validateSource(
            depth, request.contractVersion))
        return {false, id};
    const auto& source = depth->originalViewSource();
    DescriptorBindingWrite payload;
    payload.type = DescriptorType::SampledTexture;
    payload.resource = source->resource();
    payload.resourceKind = DescriptorResourceKind::TextureView;
    payload.resourceState = ResourceState::ShaderRead;
    auto result =
        writeImpl(handle, payload, &device, source, nullptr, &request, {}, nullptr, nullptr, depth);
    if (result.success)
        captureTextureAccess(*table,
            handle,
            *source->originalObject(),
            ResourceState::ShaderRead,
            ResourceAccess::ShaderRead);
    return result;
}

ValidationResult BindlessSnapshot::writeOriginalTextureCell(BindlessResourceHandle handle,
    const GpuDevice& device,
    const std::shared_ptr<const OriginalCommandTextureViewSource>& source,
    const GpuTexture::ContentRangeExpectation& identity,
    const OriginalTextureCellDescriptorRequest& request)
{
    if (!semantics::OriginalBindlessPreparationLowering::hasPlan(plan_, 7u)
        && !semantics::OriginalBindlessPreparationLowering::hasImageCellBindingPlan(plan_)
        && !semantics::OriginalBindlessPreparationLowering::hasMixedImageSnapshotPlan(plan_))
        return {false, "RHI-TEXTURE-CELL-SNAPSHOT-PROFILE"};
    if (semantics::OriginalBindlessPreparationLowering::hasProducedImageReadPlan(plan_)
        != OriginalTextureCellDescriptorContract::isProducedImageRead(request.contractVersion))
        return {false, "RHI-BINDLESS-PRODUCED-IMAGE-DESCRIPTOR-PROFILE"};
    const auto* table = findTable(handle);
    if (!table)
        return {false, "RHI-BINDLESS-HANDLE-STALE"};
    const bool storage = table->type == DescriptorType::StorageTexture;
    const auto access = storage ? ResourceAccess::ShaderWrite : ResourceAccess::ShaderRead;
    const auto operations = static_cast<std::uint32_t>(table->mapping.operations);
    const auto storageAllowed =
        static_cast<std::uint32_t>(BindlessOperation::Store | BindlessOperation::Query);
    const bool operationMatches = storage
        ? (operations & static_cast<std::uint32_t>(BindlessOperation::Store))
            && !(operations & ~storageAllowed)
        : operationAccess(table->mapping.operations) == access;
    if ((!storage && table->type != DescriptorType::SampledTexture)
        || table->stages != ShaderStage::Compute || !operationMatches)
        return {false, "RHI-TEXTURE-CELL-DESCRIPTOR-TABLE"};
    if (const auto* id = OriginalTextureCellDescriptorContract::validateSource(
            source, identity, request.contractVersion))
        return {false, id};
    DescriptorBindingWrite payload;
    payload.type = table->type;
    payload.resource = source->resource();
    payload.resourceKind = DescriptorResourceKind::TextureView;
    payload.resourceState = storage ? ResourceState::ShaderWrite : ResourceState::ShaderRead;
    auto result = writeImpl(handle,
        payload,
        &device,
        source,
        nullptr,
        nullptr,
        {},
        nullptr,
        nullptr,
        {},
        &identity,
        &request);
    if (result.success)
        captureTextureAccess(
            *table, handle, *source->originalObject(), payload.resourceState, access);
    return result;
}

void BindlessSnapshot::captureTextureAccess(const Table& table,
    BindlessResourceHandle handle,
    const GpuTextureView& view,
    ResourceState state,
    ResourceAccess access) noexcept
{
    auto& record = accesses_[physicalElement(table, handle.index)];
    const auto& shape = view.desc();
    record.resource = view.textureId();
    record.kind = BindlessAccessKind::Texture;
    record.stages = table.stages;
    record.pipelineStages = bindlessPipelineStagesForShader(table.stages);
    record.access = access;
    record.state = state;
    record.textureRange = {shape.baseMipLevel,
        shape.mipLevelCount,
        shape.baseArrayLayer,
        shape.arrayLayerCount,
        shape.aspect};
}

ValidationResult BindlessSnapshot::writeSampler(
    BindlessResourceHandle handle, const GpuSampler& sampler)
{
    const auto* table = findTable(handle);
    if (!table)
        return {false, "RHI-BINDLESS-HANDLE-STALE"};
    if (table->type != DescriptorType::Sampler)
        return {false, "RHI-BINDLESS-WRITE-INVALID"};
    DescriptorBindingWrite payload;
    payload.type = DescriptorType::Sampler;
    payload.resource = sampler.id();
    payload.resourceKind = DescriptorResourceKind::Sampler;
    payload.resourceState = ResourceState::Undefined;
    return write(handle, payload);
}

bool BindlessSnapshot::mappedBinding(std::uint32_t binding) const noexcept
{
    for (const auto& table : tables_) {
        const auto first = table.mapping.firstBinding;
        const auto count = plan_.bindingModel == BindlessBindingModel::ScalarExpansion
            ? table.mapping.capacity
            : 1u;
        if (binding >= first && binding - first < count)
            return true;
    }
    return false;
}

ValidationResult BindlessSnapshot::writeOrdinary(const DescriptorBindingWrite& write)
{
    if (sealed_)
        return {false, "RHI-BINDLESS-SNAPSHOT-IMMUTABLE"};
    if (updates_ >= maxUpdates_)
        return {false, "RHI-BINDLESS-UPDATE-BUDGET"};
    if (!set_ || mappedBinding(write.binding) || write.physicalLowering.enabled)
        return {false, "RHI-BINDLESS-ORDINARY-WRITE-INVALID"};
    if (metadataLayout_.contractVersion && write.binding == metadataLayout_.binding)
        return {false, "RHI-BINDLESS-METADATA-RESERVED"};
    if (faultLatchLayout_.contractVersion && write.binding == faultLatchLayout_.binding)
        return {false, "RHI-BINDLESS-FAULT-LATCH-RESERVED"};
    auto result = set_->write(write);
    if (result.success) {
        ++updates_;
        accesses_[descriptorElementIndex(set_->layout(), write.binding, write.arrayElement)] = {};
    }
    return result;
}

ValidationResult BindlessSnapshot::writeOrdinaryBuffer(
    std::uint32_t binding, const GpuBuffer& buffer)
{
    if (!set_)
        return {false, "RHI-BINDLESS-SNAPSHOT-NOT-PREPARED"};
    for (const auto& desc : set_->layout().bindings) {
        if (desc.binding != binding)
            continue;
        if (desc.arrayCount != 1
            || (desc.type != DescriptorType::UniformBuffer
                && desc.type != DescriptorType::ReadOnlyStorageBuffer
                && desc.type != DescriptorType::StorageBuffer))
            return {false, "RHI-BINDLESS-ORDINARY-WRITE-INVALID"};
        const auto usage = desc.type == DescriptorType::UniformBuffer
            ? ResourceUsage::UniformBuffer
            : ResourceUsage::StorageBuffer;
        const auto maxSize = desc.type == DescriptorType::UniformBuffer
            ? maxUniformBufferBindingSize_
            : maxStorageBufferBindingSize_;
        if ((buffer.desc().usage & usage) != usage)
            return {false, "RHI-BINDLESS-BUFFER-USAGE-MISMATCH"};
        if (!buffer.desc().sizeBytes || !maxSize || buffer.desc().sizeBytes > maxSize)
            return {false, "RHI-BINDLESS-BUFFER-RANGE-INVALID"};
        if (indirectInstanceConsumer_.contractVersion
            && binding == indirectInstanceConsumer_.binding
            && (buffer.desc().sizeBytes % sizeof(IndirectInstanceBase)
                || buffer.desc().sizeBytes
                    < (std::uint64_t(indirectInstanceConsumer_.recordIndex) + 1u)
                        * sizeof(IndirectInstanceBase)))
            return {false, "RHI-BINDLESS-INDIRECT-INSTANCE-CONSUMER-RANGE"};
        DescriptorBindingWrite payload;
        payload.binding = binding;
        payload.type = desc.type;
        payload.resource = buffer.id();
        payload.resourceKind = DescriptorResourceKind::Buffer;
        payload.resourceState = desc.type == DescriptorType::StorageBuffer
            ? ResourceState::ShaderWrite
            : ResourceState::ShaderRead;
        auto result = writeOrdinary(payload);
        if (result.success) {
            auto& record = accesses_[descriptorElementIndex(set_->layout(), binding, 0)];
            record.resource = buffer.id();
            record.kind = BindlessAccessKind::Buffer;
            record.stages = desc.stages;
            record.state = payload.resourceState;
            record.pipelineStages = bindlessPipelineStagesForShader(desc.stages);
            record.access = ResourceAccess::ShaderRead
                | (desc.type == DescriptorType::StorageBuffer ? ResourceAccess::ShaderWrite
                                                              : ResourceAccess::None);
            record.bufferSize = buffer.desc().sizeBytes;
        }
        return result;
    }
    return {false, "RHI-BINDLESS-ORDINARY-WRITE-INVALID"};
}

ValidationResult BindlessSnapshot::seal(GpuDevice& device)
{
    const auto* extension =
        static_cast<const BindlessCrossExtension*>(device.queryInterface(kExtensionBindlessCross));
    if (!extension || extension->capabilities().deviceIdentity != plan_.deviceIdentity
        || (!plan_.originalPreparationContractVersion && !plan_.originalExecutionContractVersion
            && extension->capabilities().execution != plan_.execution))
        return {false, "RHI-BINDLESS-SEAL-DEVICE-MISMATCH"};
    const auto issuance = extension->validateSnapshotCreation(plan_);
    if (!issuance.success)
        return issuance;
    if (sealed_)
        return validate();
    if (!set_)
        return {false, "RHI-BINDLESS-SNAPSHOT-NOT-PREPARED"};
    const std::uint32_t reservedWrites =
        (metadataBuffer_ ? 1u : 0u) + (faultLatchBuffer_ ? 1u : 0u);
    if (reservedWrites > maxUpdates_ - updates_)
        return {false, "RHI-BINDLESS-UPDATE-BUDGET"};
    // Validate application entries before publishing either reserved binding.
    for (const auto& binding : set_->layout().bindings) {
        if ((metadataBuffer_ && binding.binding == metadataLayout_.binding)
            || (faultLatchBuffer_ && binding.binding == faultLatchLayout_.binding))
            continue;
        for (std::uint32_t i = 0; i < binding.arrayCount; ++i) {
            const auto valid = set_->validateBindingElement(binding.binding, i);
            if (!valid.success)
                return valid;
        }
    }
    if (metadataBuffer_) {
        class Source final : public BindlessSnapshotMetadataSource {
        public:
            explicit Source(const BindlessSnapshot& snapshot) : snapshot_(snapshot) {}

            std::size_t tableCount() const noexcept override { return snapshot_.tables_.size(); }

            bool readTable(std::size_t index, BindlessMetadataTableSource& out) const override
            {
                if (index >= tableCount())
                    return false;
                const auto& table = snapshot_.tables_[index];
                out = {&table.mapping, table.resourceCount};
                return true;
            }

            bool readElementCount(
                std::size_t index, std::uint32_t element, std::uint32_t& count) const override
            {
                if (index >= tableCount() || element >= snapshot_.tables_[index].resourceCount)
                    return false;
                count = snapshot_.bufferElementCount(
                    snapshot_.handle(snapshot_.tables_[index].mapping.tableId, element));
                return true;
            }

        private:
            const BindlessSnapshot& snapshot_;
        } source(*this);

        const auto built = buildBindlessSnapshotMetadata(
            metadataLayout_, generation_, source, metadataWords_.data(), metadataWords_.size());
        if (!built.success)
            return built;
        if (!device
                .writeBuffer(*metadataBuffer_,
                    metadataWords_.data(),
                    std::uint64_t(metadataWords_.size()) * sizeof(std::uint32_t))
                .success)
            return {false, "RHI-BINDLESS-METADATA-UPLOAD-FAILED"};
        DescriptorBindingWrite write;
        write.binding = metadataLayout_.binding;
        write.type = DescriptorType::ReadOnlyStorageBuffer;
        write.resource = metadataBuffer_->id();
        write.resourceKind = DescriptorResourceKind::Buffer;
        write.resourceState = ResourceState::ShaderRead;
        const auto written = set_->write(write);
        if (!written.success)
            return written;
        ++updates_;
        accesses_[descriptorElementIndex(set_->layout(), write.binding, 0)] =
            metadataAccessUnchecked();
    }
    if (faultLatchBuffer_) {
        DescriptorBindingWrite write;
        write.binding = faultLatchLayout_.binding;
        write.type = DescriptorType::StorageBuffer;
        write.resource = faultLatchBuffer_->id();
        write.resourceKind = DescriptorResourceKind::Buffer;
        write.resourceState = ResourceState::ShaderWrite;
        const auto written = set_->write(write);
        if (!written.success)
            return written;
        ++updates_;
        accesses_[descriptorElementIndex(set_->layout(), write.binding, 0)] =
            faultLatchAccessUnchecked();
    }
    const auto valid =
        validateDescriptorResources(*set_, DescriptorContentRequirement::CompleteLayout);
    if (!valid.success)
        return valid;
    if (metadataBuffer_) {
        metadataBuffer_->sealHostWrites();
        metadataBuffer_->sealGpuWrites();
    }
    if (faultLatchBuffer_) {
        faultLatchBuffer_->sealHostWrites();
        faultLatchBuffer_->sealGpuTransferWrites();
    }
    if (faultReadbackBuffer_)
        faultReadbackBuffer_->sealHostWrites();
    sealed_ = true;
    return {true, {}};
}

BindlessResourceAccess BindlessSnapshot::metadataAccess() const noexcept
{
    return sealed_ ? metadataAccessUnchecked() : BindlessResourceAccess{};
}

BindlessResourceAccess BindlessSnapshot::metadataAccessUnchecked() const noexcept
{
    BindlessResourceAccess access;
    if (const auto* buffer = internalBuffer(OriginalBufferRole::Metadata)) {
        access.resource = buffer->id();
        access.kind = BindlessAccessKind::Buffer;
        access.stages = metadataLayout_.stages;
        access.pipelineStages = bindlessPipelineStagesForShader(metadataLayout_.stages);
        access.access = ResourceAccess::ShaderRead;
        access.state = ResourceState::ShaderRead;
        access.bufferSize = buffer->desc().sizeBytes;
    }
    return access;
}

BindlessResourceAccess BindlessSnapshot::faultLatchAccess() const noexcept
{
    return sealed_ ? faultLatchAccessUnchecked() : BindlessResourceAccess{};
}

BindlessResourceAccess BindlessSnapshot::faultLatchAccessUnchecked() const noexcept
{
    BindlessResourceAccess access;
    if (const auto* buffer = internalBuffer(OriginalBufferRole::FaultLatch)) {
        access.resource = buffer->id();
        access.kind = BindlessAccessKind::Buffer;
        access.stages = faultLatchLayout_.stages;
        access.pipelineStages = bindlessPipelineStagesForShader(faultLatchLayout_.stages);
        access.access = ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite;
        access.state = ResourceState::ShaderWrite;
        access.bufferSize = buffer->desc().sizeBytes;
    }
    return access;
}

BindlessResourceAccess BindlessSnapshot::faultReadbackAccess() const noexcept
{
    return sealed_ ? faultReadbackAccessUnchecked() : BindlessResourceAccess{};
}

BindlessResourceAccess BindlessSnapshot::faultReadbackAccessUnchecked() const noexcept
{
    BindlessResourceAccess access;
    if (const auto* buffer = internalBuffer(OriginalBufferRole::FaultReadback)) {
        access.resource = buffer->id();
        access.kind = BindlessAccessKind::Buffer;
        access.pipelineStages = PipelineStage::Transfer;
        access.access = ResourceAccess::TransferWrite;
        access.state = ResourceState::CopyDst;
        access.bufferSize = buffer->desc().sizeBytes;
    }
    return access;
}

std::uint32_t BindlessSnapshot::activeCount(std::uint32_t tableId) const noexcept
{
    for (const auto& table : tables_)
        if (table.mapping.tableId == tableId)
            return table.resourceCount;
    return 0u;
}

std::uint32_t BindlessSnapshot::bufferElementCount(BindlessResourceHandle handle) const noexcept
{
    const auto* table = findTable(handle);
    if (!table || !table->mapping.bufferLayout.elementStrideBytes)
        return 0u;
    const auto& access = accesses_[physicalElement(*table, handle.index)];
    return static_cast<std::uint32_t>(
        access.bufferSize / table->mapping.bufferLayout.elementStrideBytes);
}

ValidationResult BindlessSnapshot::initializeInactiveSlots(BindlessResourceHandle source)
{
    return initializeInactiveSlotsImpl(source, nullptr);
}

ValidationResult BindlessSnapshot::initializeInactiveSlots(
    BindlessResourceHandle source, const GpuDevice& device)
{
    return initializeInactiveSlotsImpl(source, &device);
}

ValidationResult BindlessSnapshot::initializeInactiveSlots(
    BindlessResourceHandle source, GpuDevice& device)
{
    return initializeInactiveSlotsImpl(source, &device, &device);
}

ValidationResult BindlessSnapshot::initializeInactiveSlotsImpl(
    BindlessResourceHandle source, const GpuDevice* device, GpuDevice* rangeDevice)
{
    if (sealed_)
        return {false, "RHI-BINDLESS-SNAPSHOT-IMMUTABLE"};
    const auto* table = findTable(source);
    if (!table)
        return {false, "RHI-BINDLESS-HANDLE-STALE"};
    const auto sourceElement = physicalElement(*table, source.index);
    auto payload = writes_[sourceElement];
    const auto valid = set_->validateBindingElement(payload.binding, payload.arrayElement);
    if (!payload.resource.isValid() || !valid.success)
        return {false, "RHI-BINDLESS-PADDING-SOURCE-INVALID"};
    const auto count = table->mapping.capacity - table->resourceCount;
    if (count > maxUpdates_ - updates_)
        return {false, "RHI-BINDLESS-UPDATE-BUDGET"};
    const auto& publication = completedTextures_[sourceElement];
    const auto& bufferRange = originalBufferRanges_[sourceElement];
    if (count && publication.source && !device)
        return {false, "RHI-COMPLETED-TEXTURE-DESCRIPTOR-PADDING-DEVICE"};
    if (count && bufferRange.source && !rangeDevice)
        return {false, "RHI-BUFFER-RANGE-DESCRIPTOR-PADDING-DEVICE"};
    const bool expanded = plan_.bindingModel == BindlessBindingModel::ScalarExpansion;
    for (auto index = table->resourceCount; index < table->mapping.capacity; ++index) {
        payload.binding = table->mapping.firstBinding + (expanded ? index : 0u);
        payload.arrayElement = expanded ? 0u : index;
        const auto written = publication.cellIdentity ? set_->writeOriginalTextureCell(*device,
                                                            payload,
                                                            publication.source,
                                                            *publication.cellIdentity,
                                                            publication.cellRequest)
            : publication.depth                       ? set_->writeOriginalCompletedTexture(
                                      *device, payload, publication.depth, publication.request)
            : publication.source
            ? set_->writeOriginalCompletedTexture(
                  *device, payload, publication.source, *publication.content, publication.request)
            : bufferRange.source ? set_->writeOriginalBufferRange(*rangeDevice,
                                       payload,
                                       bufferRange.source,
                                       table->mapping.bufferLayout.minBindingSizeBytes,
                                       bufferRange.request)
                                 : set_->write(payload);
        if (!written.success)
            return written;
        ++updates_;
        writes_[physicalElement(*table, index)] = payload;
        auto& alias = completedTextures_[physicalElement(*table, index)];
        alias.source = publication.source;
        alias.depth = publication.depth;
        alias.content.reset();
        if (publication.content)
            alias.content.emplace(*publication.content);
        alias.request = publication.request;
        alias.cellIdentity.reset();
        if (publication.cellIdentity)
            alias.cellIdentity.emplace(*publication.cellIdentity);
        alias.cellRequest = publication.cellRequest;
        originalBufferRanges_[physicalElement(*table, index)] = bufferRange;
        // Keep a conservative graph declaration for every physical alias too.
        accesses_[physicalElement(*table, index)] = accesses_[sourceElement];
    }
    return {true, {}};
}

BindlessIndirectDeclarations::BindlessIndirectDeclarations(std::uint32_t capacity)
    : BindlessRetainedDeclarations(capacity, BindlessAccessRetention::Indirect)
{}

BindlessRetainedDeclarations::BindlessRetainedDeclarations(
    std::uint32_t capacity, BindlessAccessRetention selection)
    : capacity_(capacity), selection_(selection)
{
    accesses_.reserve(capacity);
}

ValidationResult BindlessRetainedDeclarations::capture(const BindlessAccessDeclarations& source)
{
    if (&source == this)
        return {true, {}};
    accesses_.clear();
    if (!source.valid())
        return {false, "RHI-BINDLESS-ACCESS-SET-INVALID"};
    for (std::size_t i = 0; i < source.size(); ++i) {
        BindlessResourceAccess access;
        if (!source.read(i, access)) {
            accesses_.clear();
            return {false, "RHI-BINDLESS-ACCESS-SET-INVALID"};
        }
        if (selection_ == BindlessAccessRetention::GraphicsPass
            || (access.pipelineStages & PipelineStage::DrawIndirect) != PipelineStage::None
            || (access.access & ResourceAccess::IndirectCommandRead) != ResourceAccess::None) {
            if (accesses_.size() == capacity_) {
                accesses_.clear();
                return {false,
                    selection_ == BindlessAccessRetention::GraphicsPass
                        ? "RHI-BINDLESS-GRAPHICS-PASS-ACCESS-BUDGET"
                        : "RHI-BINDLESS-INDIRECT-ACCESS-BUDGET"};
            }
            accesses_.push_back(access);
        }
    }
    return {true, {}};
}

bool BindlessRetainedDeclarations::matches(const BindlessAccessDeclarations& source) const
{
    if (!source.valid() || source.size() != accesses_.size())
        return false;
    for (std::size_t i = 0u; i < accesses_.size(); ++i) {
        BindlessResourceAccess access;
        if (!source.read(i, access))
            return false;
        const auto& stored = accesses_[i];
        if (stored.resource != access.resource || stored.kind != access.kind
            || stored.stages != access.stages || stored.pipelineStages != access.pipelineStages
            || stored.access != access.access || stored.state != access.state
            || stored.bufferOffset != access.bufferOffset || stored.bufferSize != access.bufferSize
            || !(stored.textureRange == access.textureRange))
            return false;
    }
    return true;
}

bool BindlessRetainedDeclarations::read(std::size_t index, BindlessResourceAccess& out) const
{
    if (index >= accesses_.size())
        return false;
    out = accesses_[index];
    return true;
}

ValidationResult validateBindlessIndirectDrawAccess(const GpuBuffer& buffer,
    std::uint64_t offset,
    std::uint32_t drawCount,
    std::uint32_t stride,
    const BindlessAccessDeclarations& declared)
{
    constexpr std::uint64_t recordSize = sizeof(DrawIndexedIndirectCommand);
    const auto size = buffer.desc().sizeBytes;
    if (!drawCount || stride < recordSize || (stride & 3u) || (offset & 3u)
        || (buffer.desc().usage & ResourceUsage::IndirectBuffer) == ResourceUsage::None
        || offset > size || size - offset < recordSize
        || drawCount - 1u > (size - offset - recordSize) / stride)
        return {false, "RHI-BINDLESS-INDIRECT-ARGS-INVALID"};
    const auto range = (static_cast<std::uint64_t>(drawCount) - 1u) * stride + recordSize;
    BindlessResourceAccess required;
    required.resource = buffer.id();
    required.kind = BindlessAccessKind::Buffer;
    required.state = ResourceState::ShaderRead;
    required.pipelineStages = PipelineStage::DrawIndirect;
    required.access = ResourceAccess::IndirectCommandRead;
    required.bufferOffset = offset;
    required.bufferSize = range;
    const auto result = validateBindlessBufferAccess(required, declared);
    if (!result.success && result.diagnostics == "RHI-BINDLESS-ACCESS-SET-INCOMPLETE")
        return {false, "RHI-BINDLESS-INDIRECT-ACCESS-INCOMPLETE"};
    return result;
}

ValidationResult validateBindlessBufferAccess(
    const BindlessResourceAccess& required, const BindlessAccessDeclarations& declared)
{
    if (!required.resource.isValid() || required.kind != BindlessAccessKind::Buffer
        || !required.bufferSize || required.state == ResourceState::Undefined
        || required.pipelineStages == PipelineStage::None
        || required.access == ResourceAccess::None)
        return {false, "RHI-BINDLESS-REQUIRED-ACCESS-INVALID"};
    if (!declared.valid())
        return {false, "RHI-BINDLESS-ACCESS-SET-INVALID"};
    for (std::size_t i = 0; i < declared.size(); ++i) {
        BindlessResourceAccess candidate;
        if (!declared.read(i, candidate))
            return {false, "RHI-BINDLESS-ACCESS-SET-INVALID"};
        if (candidate.resource != required.resource || candidate.kind != required.kind
            || candidate.state != required.state
            || (candidate.stages & required.stages) != required.stages
            || (candidate.pipelineStages & required.pipelineStages) != required.pipelineStages
            || (candidate.access & required.access) != required.access)
            continue;
        if (core::containsUnsignedRange(candidate.bufferOffset,
                candidate.bufferSize,
                required.bufferOffset,
                required.bufferSize))
            return {true, {}};
    }
    return {false, "RHI-BINDLESS-ACCESS-SET-INCOMPLETE"};
}

ValidationResult BindlessSnapshot::validate() const
{
    if (!sealed_ || !set_)
        return {false, "RHI-BINDLESS-SNAPSHOT-NOT-SEALED"};
    for (std::size_t i = 0u; i < originalBufferRanges_.size(); ++i) {
        const auto& source = originalBufferRanges_[i].source;
        if (!source)
            continue;
        if (const auto* id = OriginalCommandBufferLease::validateOriginal(source))
            return {false, id};
        DescriptorBufferBindingRangeContract::Resolved range;
        if (const auto* id = DescriptorBufferBindingRangeContract::resolve(
                writes_[i].bufferBindingRange, source->layout().sizeBytes, 0u, range))
            return {false, id};
        if (writes_[i].resource != source->resource() || accesses_[i].resource != source->resource()
            || accesses_[i].bufferOffset != range.offsetBytes
            || accesses_[i].bufferSize != range.sizeBytes)
            return {false, "RHI-BINDLESS-BUFFER-RANGE-SOURCE"};
    }
    return validateDescriptorResources(*set_, DescriptorContentRequirement::CompleteLayout);
}

const DescriptorSet* BindlessSnapshot::descriptorSet() const
{
    return validate().success ? set_.get() : nullptr;
}

ValidationResult BindlessSnapshot::validateIndirectInstanceMapping(
    const GpuBuffer& mapping, std::uint32_t instanceCapacity) const
{
    const auto live = validate();
    if (!live.success)
        return live;
    const auto& reader = indirectInstanceConsumer_;
    if (reader.contractVersion != 1u || reader.recordIndex || !instanceCapacity
        || reader.instanceCapacity != instanceCapacity
        || mapping.desc().sizeBytes != sizeof(IndirectInstanceBase))
        return {false, "RHI-BINDLESS-INDIRECT-PACKET-CONSUMER-MISMATCH"};
    for (const auto& binding : set_->layout().bindings) {
        if (binding.binding != reader.binding)
            continue;
        const auto& access = accesses_[descriptorElementIndex(set_->layout(), binding.binding, 0u)];
        if (binding.type != DescriptorType::ReadOnlyStorageBuffer || binding.arrayCount != 1u
            || (binding.stages & ShaderStage::Vertex) == ShaderStage::None
            || access.resource != mapping.id() || access.kind != BindlessAccessKind::Buffer
            || access.bufferOffset || access.bufferSize != sizeof(IndirectInstanceBase)
            || access.state != ResourceState::ShaderRead
            || access.access != ResourceAccess::ShaderRead
            || (access.stages & ShaderStage::Vertex) == ShaderStage::None
            || (access.pipelineStages & PipelineStage::VertexShader) == PipelineStage::None)
            return {false, "RHI-BINDLESS-INDIRECT-PACKET-CONSUMER-MISMATCH"};
        return {true, {}};
    }
    return {false, "RHI-BINDLESS-INDIRECT-PACKET-CONSUMER-MISMATCH"};
}

ValidationResult BindlessSnapshot::validateForRecord(const BindlessLoweringPlan& selected,
    std::uint64_t resourceGeneration,
    const BindlessResourceAccess* declared,
    std::size_t declaredCount) const
{
    return validateForRecord(
        selected, resourceGeneration, BindlessAccessArray(declared, declaredCount));
}

ValidationResult BindlessSnapshot::validateForRecord(const BindlessLoweringPlan& selected,
    std::uint64_t resourceGeneration,
    const BindlessAccessDeclarations& declared) const
{
    return validateForRecord(selected,
        resourceGeneration,
        declared,
        ShaderStage::Vertex | ShaderStage::Fragment | ShaderStage::Compute);
}

ValidationResult BindlessSnapshot::validateForRecord(const BindlessLoweringPlan& selected,
    std::uint64_t resourceGeneration,
    const BindlessAccessDeclarations& declared,
    ShaderStage activeStages) const
{
    const auto live = validate();
    if (!live.success)
        return live;
    if (resourceGeneration != generation_ || !bindlessPlansMatch(selected, plan_))
        return {false, "RHI-BINDLESS-RECORD-PLAN-MISMATCH"};
    if (!declared.valid())
        return {false, "RHI-BINDLESS-ACCESS-SET-INVALID"};
    const auto stages = static_cast<std::uint32_t>(activeStages);
    if (!stages || (stages & ~7u))
        return {false, "RHI-BINDLESS-ACTIVE-STAGES-INVALID"};
    for (const auto& binding : set_->layout().bindings) {
        if (binding.type == DescriptorType::Sampler)
            continue;
        const auto requiredStages = binding.stages & activeStages;
        if (requiredStages == ShaderStage::None)
            continue;
        for (std::uint32_t element = 0; element < binding.arrayCount; ++element) {
            const auto& required =
                accesses_[descriptorElementIndex(set_->layout(), binding.binding, element)];
            if (!required.resource.isValid())
                return {false, "RHI-BINDLESS-ACCESS-METADATA-MISSING"};
            const auto requiredPipelineStages = bindlessPipelineStagesForShader(requiredStages);
            bool covered = false;
            for (std::size_t i = 0; i < declared.size(); ++i) {
                BindlessResourceAccess candidate;
                if (!declared.read(i, candidate))
                    return {false, "RHI-BINDLESS-ACCESS-SET-INVALID"};
                if (candidate.resource != required.resource || candidate.kind != required.kind
                    || candidate.state != required.state
                    || (candidate.stages & requiredStages) != requiredStages
                    || (static_cast<std::uint32_t>(candidate.pipelineStages)
                           & static_cast<std::uint32_t>(requiredPipelineStages))
                        != static_cast<std::uint32_t>(requiredPipelineStages)
                    || (candidate.access & required.access) != required.access)
                    continue;
                if (required.kind == BindlessAccessKind::Buffer) {
                    covered = core::containsUnsignedRange(candidate.bufferOffset,
                        candidate.bufferSize,
                        required.bufferOffset,
                        required.bufferSize);
                } else {
                    const auto& a = candidate.textureRange;
                    const auto& b = required.textureRange;
                    covered = a.aspect == b.aspect
                        && core::containsUnsignedRange(
                            a.baseMipLevel, a.mipLevelCount, b.baseMipLevel, b.mipLevelCount)
                        && core::containsUnsignedRange(a.baseArrayLayer,
                            a.arrayLayerCount,
                            b.baseArrayLayer,
                            b.arrayLayerCount);
                }
                if (covered)
                    break;
            }
            if (!covered)
                return {false, "RHI-BINDLESS-ACCESS-SET-INCOMPLETE"};
        }
    }
    if (internalBuffer(OriginalBufferRole::FaultReadback)) {
        const auto required = faultReadbackAccessUnchecked();
        bool covered = false;
        for (std::size_t i = 0; i < declared.size(); ++i) {
            BindlessResourceAccess candidate;
            if (!declared.read(i, candidate))
                return {false, "RHI-BINDLESS-ACCESS-SET-INVALID"};
            if (candidate.resource != required.resource || candidate.kind != required.kind
                || candidate.state != required.state
                || (static_cast<std::uint32_t>(candidate.pipelineStages)
                       & static_cast<std::uint32_t>(required.pipelineStages))
                    != static_cast<std::uint32_t>(required.pipelineStages)
                || (candidate.access & required.access) != required.access)
                continue;
            covered = core::containsUnsignedRange(candidate.bufferOffset,
                candidate.bufferSize,
                required.bufferOffset,
                required.bufferSize);
            if (covered)
                break;
        }
        if (!covered)
            return {false, "RHI-BINDLESS-FAULT-READBACK-ACCESS-INCOMPLETE"};
    }
    return {true, {}};
}

BindlessSnapshotPool::BindlessSnapshotPool(GpuDevice& device, BindlessBudget budget)
    : device_(device), budget_(budget),
      counts_(core::makeShared<core::AtomicCapacityBudget>(budget.maxSnapshots)),
      bytes_(core::makeShared<core::AtomicCapacityBudget>(budget.maxResidentBytes))
{}

core::UniquePtr<BindlessSnapshot> BindlessSnapshotPool::prepare(const BindlessRequirements& request,
    const BindlessShaderArtifact& artifact,
    core::pmr::String& diagnostic)
{
    return prepareImpl(request, artifact, diagnostic, false);
}

core::UniquePtr<BindlessSnapshot> BindlessSnapshotPool::prepareOriginalDescriptors(
    const BindlessRequirements& request,
    const BindlessShaderArtifact& artifact,
    BindlessSnapshotOriginalDescriptorRequest original,
    core::pmr::String& diagnostic)
{
    if (original.contractVersion != 1u && original.contractVersion != 2u) {
        diagnostic = "RHI-BINDLESS-ORIGINAL-DESCRIPTORS-VERSION";
        return {};
    }
    if (!original.prototypeOptIn) {
        diagnostic = "RHI-BINDLESS-ORIGINAL-DESCRIPTORS-OPT-IN";
        return {};
    }
    if (original.contractVersion == 1u && original.retainFaultReadback) {
        diagnostic = "RHI-BINDLESS-ORIGINAL-DESCRIPTORS-LEGACY-SCHEMA";
        return {};
    }
    if (original.contractVersion == 2u && !original.retainFaultReadback) {
        diagnostic = "RHI-BINDLESS-ORIGINAL-DESCRIPTORS-FAULT-OPT-IN";
        return {};
    }
    if (original.retainFaultReadback
        && (artifact.faultLatch.contractVersion != 1u
            || artifact.snapshotBytes
                < std::uint64_t(artifact.snapshotMetadata.wordCount) * sizeof(std::uint32_t)
                    + 2u * sizeof(std::uint32_t))) {
        diagnostic = "RHI-BINDLESS-ORIGINAL-DESCRIPTORS-FAULT-BUDGET";
        return {};
    }
    if (const auto* id =
            semantics::OriginalBindlessRootSourceLowering::lowering(device_.backend()).diagnostic) {
        diagnostic = id;
        return {};
    }
    if (const auto* id = device_.originalResourceRegistrationDiagnostic()) {
        diagnostic = id;
        return {};
    }
    return prepareImpl(request, artifact, diagnostic, true, original.retainFaultReadback);
}

core::UniquePtr<BindlessSnapshot> BindlessSnapshotPool::prepareImpl(
    const BindlessRequirements& request,
    const BindlessShaderArtifact& artifact,
    core::pmr::String& diagnostic,
    bool originalDescriptors,
    bool retainFaultReadback)
{
    if (!request.originalPreparation.empty() && (!originalDescriptors || !retainFaultReadback)) {
        diagnostic = "RHI-BINDLESS-ORIGINAL-PREPARATION-DESCRIPTORS-REQUIRED";
        return {};
    }
    if (!request.originalExecution.empty() && (!originalDescriptors || !retainFaultReadback)) {
        diagnostic = "RHI-BINDLESS-ORIGINAL-EXECUTION-DESCRIPTORS-REQUIRED";
        return {};
    }
    if (request.budget.maxSnapshots != budget_.maxSnapshots
        || request.budget.maxUpdatesPerSnapshot != budget_.maxUpdatesPerSnapshot
        || request.budget.maxResidentBytes != budget_.maxResidentBytes
        || request.budget.maxSwitchCases != budget_.maxSwitchCases) {
        diagnostic = "RHI-BINDLESS-POOL-BUDGET-MISMATCH";
        return {};
    }
    const auto* extension =
        static_cast<const BindlessCrossExtension*>(device_.queryInterface(kExtensionBindlessCross));
    if (!extension) {
        diagnostic = "RHI-BINDLESS-EXTENSION-UNAVAILABLE";
        return {};
    }
    const auto plan = extension->plan(request, artifact);
    if (!plan.success) {
        diagnostic = plan.diagnosticId;
        return {};
    }
    auto count = core::makeShared<core::AtomicCapacityLease>(counts_, 1u);
    if (!count->charged()) {
        diagnostic = "RHI-BINDLESS-SNAPSHOT-POOL-FULL";
        return {};
    }
    auto bytes = core::makeShared<core::AtomicCapacityLease>(bytes_, plan.snapshotBytes);
    if (!bytes->charged()) {
        diagnostic = "RHI-BINDLESS-RESIDENT-BUDGET";
        return {};
    }
    auto snapshot = BindlessSnapshot::create(
        device_, request, artifact, plan, diagnostic, originalDescriptors, retainFaultReadback);
    if (!snapshot)
        return {};
    snapshot->countLease_ = std::move(count);
    snapshot->byteLease_ = std::move(bytes);
    return snapshot;
}

} // namespace arp::rhi

#include "rhi/backends/metal/metal_original_geometry_content.h"

#include "rhi/backends/metal/metal_backend_objects.h"
#include "rhi/backends/metal/metal_device.h"
#include "rhi/backends/metal/metal_native_runtime.h"
#include "rhi/frontend/command_recording.h"

namespace arp::rhi_metal {
using namespace rhi;

MetalOriginalGeometryContent::MetalOriginalGeometryContent(ConstructionKey,
    std::shared_ptr<const Lease> lease,
    std::shared_ptr<MetalRhiNativeBufferPayload> payload,
    std::shared_ptr<void> runtime,
    core::pmr::Vector<std::uint8_t> bytes,
    Budget budget,
    Quotas quotas,
    GpuBuffer::CommittedStateObservation state)
    : lease_(std::move(lease)), payload_(std::move(payload)), runtime_(std::move(runtime)),
      bytes_(std::move(bytes)), budget_(budget), quotas_(std::move(quotas)),
      state_(std::move(state)), nativeIdentity_(payload_->resource)
{}

std::shared_ptr<const MetalOriginalGeometryContent> MetalOriginalGeometryContent::prepareOriginal(
    GpuDevice& device,
    const BufferDesc& desc,
    const void* data,
    std::size_t bytes,
    const Budget& budget,
    const Quotas& quotas,
    const char*& diagnostic)
{
    const auto reject = [&](const char* id) -> std::shared_ptr<const MetalOriginalGeometryContent> {
        diagnostic = id;
        return {};
    };
    if (const auto* id = Contract::validateDeclaration(desc, bytes, budget))
        return reject(id);
    if (!data)
        return reject("RHI-HOST-GEOMETRY-CONTENT-PAYLOAD");
    const auto lower = Contract::lowering(device.backend(), budget.contractVersion);
    if (!lower.initializationAvailable || lower.diagnostic)
        return reject(lower.diagnostic);
    if (const auto* id = Contract::validateQuotas(device, budget, quotas))
        return reject(id);
    const auto registry = metalRhiIssuingResourceRegistry(device);
    const auto runtime = metalRhiNativeDeviceIdentity();
    if (device.backend() != Backend::Metal || !registry || !runtime)
        return reject("RHI-METAL-GEOMETRY-CONTENT-AUTHENTIC-RUNTIME");
    try {
        core::pmr::Vector<std::uint8_t> frozen;
        {
            core::CpuMemoryQuotaScope scope(quotas.payload);
            const auto* source = static_cast<const std::uint8_t*>(data);
            frozen.assign(source, source + bytes);
        }
        core::UniquePtr<GpuBuffer> object;
        {
            core::CpuMemoryQuotaScope scope(quotas.resource);
            object = device.createBuffer(desc);
        }
        if (!object)
            return reject("RHI-METAL-GEOMETRY-CONTENT-CREATE");
        if (const auto* id = device.validateOriginalResourceRegistration(*object))
            return reject(id);
        if (!object->allocation().estimatedSizeBytes
            || object->allocation().estimatedSizeBytes > budget.maxRetainedBufferBytes)
            return reject("RHI-HOST-GEOMETRY-CONTENT-BYTE-CAPACITY");
        const auto state = object->observeOriginalCommittedState();
        if (!state || state->state() != ResourceState::ShaderRead)
            return reject("RHI-METAL-GEOMETRY-CONTENT-STATE");
        auto payload = metalRhiNativePayloadFor(*object);
        if (!payload || !payload->resource.nativeRuntimeBacked
            || payload->resource.nativeRuntimeDiagnosticOnly || !payload->resource.nativeObject
            || !payload->resource.allocationReleaseToken
            || !core::SharedOwnerProjection::sameOwner(payload->resource.nativeDevice, runtime)
            || (payload->resource.storageMode != MetalRhiNativeStorageMode::Shared
                && payload->resource.storageMode != MetalRhiNativeStorageMode::Managed))
            return reject("RHI-METAL-GEOMETRY-CONTENT-NATIVE-BUFFER");
        const auto registered = registry->find(object->id());
        if (!registered || !core::SharedOwnerProjection::sameOwner(registered->buffer, payload))
            return reject("RHI-METAL-GEOMETRY-CONTENT-NATIVE-REGISTRATION");
        const auto actual =
            observeMetalRhiNativeBufferAllocation(payload->resource, desc.sizeBytes, diagnostic);
        if (diagnostic || !actual)
            return {};
        if (actual > budget.maxRetainedBufferBytes
            || actual != object->allocation().estimatedSizeBytes)
            return reject("RHI-HOST-GEOMETRY-CONTENT-BYTE-CAPACITY");
        // Real shared/managed write and didModifyRange occur while the
        // object is still private. Failure publishes no initialized actor.
        const auto written = device.writeBuffer(*object, frozen.data(), frozen.size());
        if (!written.success)
            return reject("RHI-METAL-GEOMETRY-CONTENT-WRITE-FLUSH");
        object->sealWrites();
        auto lease = Lease::prepareOriginal(object,
            {1u, true, Lease::retainedOwnerCount, budget.maxCpuLeaseBytes},
            quotas.lease,
            diagnostic);
        if (!lease)
            return {};
        core::CpuMemoryQuotaScope scope(quotas.metadata);
        auto result =
            shareOriginal(core::makeUnique<MetalOriginalGeometryContent>(ConstructionKey{},
                std::move(lease),
                std::move(payload),
                std::move(runtime),
                std::move(frozen),
                budget,
                quotas,
                *state));
        diagnostic = result ? result->validate() : "RHI-METAL-GEOMETRY-CONTENT-ALLOCATION";
        return diagnostic ? std::shared_ptr<const MetalOriginalGeometryContent>{} : result;
    } catch (const std::bad_alloc&) {
        return reject("RHI-METAL-GEOMETRY-CONTENT-ALLOCATION");
    }
}

const char* MetalOriginalGeometryContent::validateOriginal(
    const std::shared_ptr<const MetalOriginalGeometryContent>& self) noexcept
{
    return !self || !self->hasQuotaBoundSharedOwner(self) ? "RHI-METAL-GEOMETRY-CONTENT-OWNER"
                                                          : self->validate();
}

const char* MetalOriginalGeometryContent::validate() const noexcept
{
    if (const auto* id = validateIdentity())
        return id;
    // Immutable bytes survive read-only journal epochs. This current capture
    // is not borrowed by Pending: each native closure freezes its exact cold
    // observation and the original seal authenticates that capture separately.
    const auto current = lease_->originalObject()->observeOriginalCommittedState();
    if (!current || current->resourceGeneration() != state_.resourceGeneration()
        || current->state() != ResourceState::ShaderRead)
        return "RHI-METAL-GEOMETRY-CONTENT-STATE";
    return lease_->originalObject()->validateOriginalCommittedState(*current);
}

const char* MetalOriginalGeometryContent::validateOriginalPending(
    const std::shared_ptr<const MetalOriginalGeometryContent>& self,
    const std::shared_ptr<const OriginalCommandRecordingSeal>& seal,
    const GpuBuffer::CommittedStateObservation& captured) noexcept
{
    if (!self || !self->hasQuotaBoundSharedOwner(self))
        return "RHI-METAL-GEOMETRY-CONTENT-OWNER";
    if (!seal || !seal->hasQuotaBoundSharedOwner(seal))
        return "RHI-METAL-GEOMETRY-CONTENT-PENDING-SEAL";
    if (const auto* id = self->validateIdentity())
        return id;
    if (captured.resourceGeneration() != self->state_.resourceGeneration())
        return "RHI-METAL-GEOMETRY-CONTENT-PENDING-GENERATION";
    return seal->validateInitializedBufferPending(*self->lease_->originalObject(),
        captured,
        self->budget_.contractVersion == 2u
            ? OriginalInitializedBufferPendingUse::ImmutableBindlessShaderRead6
            : OriginalInitializedBufferPendingUse::ImmutableBindlessGeometryRead5,
        self->budget_.contractVersion == 2u ? 6u : 5u);
}

const char* MetalOriginalGeometryContent::validateIdentity() const noexcept
{
    if (const auto* id = Lease::validateOriginal(lease_))
        return id;
    const auto* object = lease_->originalObject();
    if (const auto* id = Contract::validateDeclaration(object->desc(), bytes_.size(), budget_))
        return id;
    if (!originalMetadataQuota()
        || !core::SharedOwnerProjection::sameOwner(originalMetadataQuota(), quotas_.metadata)
        || !core::SharedOwnerProjection::sameOwner(lease_->originalMetadataQuota(), quotas_.lease))
        return "RHI-HOST-GEOMETRY-CONTENT-QUOTA";
    if (!runtime_ || !payload_
        || !core::SharedOwnerProjection::sameOwner(payload_, metalRhiNativePayloadFor(*object))
        || !core::SharedOwnerProjection::sameOwner(runtime_, payload_->resource.nativeDevice)
        || !core::SharedOwnerProjection::sameOwner(runtime_, metalRhiNativeDeviceIdentity())
        || !payload_->resource.nativeRuntimeBacked || payload_->resource.nativeRuntimeDiagnosticOnly
        || !payload_->resource.allocationReleaseToken
        || !nativeIdentity_.matches(payload_->resource))
        return "RHI-METAL-GEOMETRY-CONTENT-NATIVE-IDENTITY";
    const char* allocationId = nullptr;
    const auto actual = observeMetalRhiNativeBufferAllocation(
        payload_->resource, object->desc().sizeBytes, allocationId);
    if (allocationId)
        return allocationId;
    if (!actual || actual != object->allocation().estimatedSizeBytes
        || actual > budget_.maxRetainedBufferBytes)
        return "RHI-HOST-GEOMETRY-CONTENT-BYTE-CAPACITY";
    if (!object->hostWritesSealed() || !object->gpuWritesSealed()
        || !object->gpuTransferWritesSealed())
        return "RHI-METAL-GEOMETRY-CONTENT-WRITES-UNSEALED";
    if (!object->allocation().estimatedSizeBytes
        || object->allocation().estimatedSizeBytes > budget_.maxRetainedBufferBytes)
        return "RHI-HOST-GEOMETRY-CONTENT-BYTE-CAPACITY";
    return nullptr;
}

const char* MetalOriginalGeometryContent::validateLease(
    const std::shared_ptr<const MetalOriginalGeometryContent>& self,
    const std::shared_ptr<const Lease>& lease) noexcept
{
    if (const auto* id = validateOriginal(self))
        return id;
    if (const auto* id = Lease::validateOriginal(lease))
        return id;
    return core::SharedOwnerProjection::sameOwner(lease, self->lease_)
        ? nullptr
        : "RHI-METAL-GEOMETRY-CONTENT-LEASE-JOIN";
}

const char* MetalOriginalGeometryContent::analyzeInitializedIndices(
    const std::shared_ptr<const MetalOriginalGeometryContent>& self,
    const std::shared_ptr<const Lease>& lease,
    IndexFormat format,
    std::uint64_t offset,
    std::uint64_t first,
    std::uint64_t count,
    std::int32_t base,
    IndexInputConsumptionContract::DrawRange& output) noexcept
{
    output = {};
    if (const auto* id = validateLease(self, lease))
        return id;
    return Contract::analyzeIndexBytes(self->budget_.definition,
        self->bytes_.data(),
        self->bytes_.size(),
        format,
        offset,
        first,
        count,
        base,
        output);
}

const char* MetalOriginalGeometryContent::validateInitializedVertices(
    const std::shared_ptr<const MetalOriginalGeometryContent>& self,
    const std::shared_ptr<const Lease>& lease,
    const VertexInputConsumptionContract::Stream& stream,
    std::uint64_t offset,
    std::uint64_t vertices,
    std::uint64_t firstVertex,
    std::uint64_t instances,
    std::uint64_t firstInstance) noexcept
{
    if (const auto* id = validateLease(self, lease))
        return id;
    return Contract::validateVertexBytes(self->budget_.definition,
        self->bytes_.size(),
        stream,
        offset,
        vertices,
        firstVertex,
        instances,
        firstInstance);
}
} // namespace arp::rhi_metal

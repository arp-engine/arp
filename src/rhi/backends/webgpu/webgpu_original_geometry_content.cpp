#include "rhi/backends/webgpu/webgpu_original_geometry_content.h"

#include "rhi/backends/webgpu/webgpu_backend_objects.h"
#include "rhi/backends/webgpu/webgpu_device.h"
#include "rhi/backends/webgpu/webgpu_native_runtime.h"
#include "rhi/frontend/command_recording.h"

#include <cstring>

namespace arp::rhi_webgpu {
using namespace rhi;

WebGpuOriginalGeometryContent::WebGpuOriginalGeometryContent(ConstructionKey,
    std::shared_ptr<const Lease> lease,
    std::shared_ptr<WebGpuNativeBufferPayload> payload,
    WebGpuTransferResourceObservation identity,
    core::pmr::Vector<std::uint8_t> bytes,
    Budget budget,
    Quotas quotas,
    GpuBuffer::CommittedStateObservation state)
    : lease_(std::move(lease)), payload_(std::move(payload)), bytes_(std::move(bytes)),
      budget_(budget), quotas_(std::move(quotas)), state_(std::move(state)),
      nativeIdentity_(std::move(identity))
{}

std::shared_ptr<const WebGpuOriginalGeometryContent> WebGpuOriginalGeometryContent::prepareOriginal(
    GpuDevice& device,
    const BufferDesc& desc,
    const void* data,
    std::size_t bytes,
    const Budget& budget,
    const Quotas& quotas,
    const char*& diagnostic)
{
    const auto reject =
        [&](const char* id) -> std::shared_ptr<const WebGpuOriginalGeometryContent> {
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
    if (device.backend() != Backend::WebGPU || !webGpuNativeDeviceAvailable()
        || webGpuNativeDeviceLost())
        return reject("RHI-WEBGPU-GEOMETRY-CONTENT-AUTHENTIC-RUNTIME");
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
            const WebGpuNativeBufferInitializer initializer{
                &frozen, [](const void* context, void* mapped, std::uint64_t extent) noexcept {
                    const auto& bytes =
                        *static_cast<const core::pmr::Vector<std::uint8_t>*>(context);
                    if (!mapped || extent != bytes.size())
                        return false;
                    std::memcpy(mapped, bytes.data(), bytes.size());
                    return true;
                }};
            object = createWebGpuRhiOriginalGeometryBuffer(device, desc, initializer, diagnostic);
        }
        if (!object)
            return {};
        if (const auto* id = device.validateOriginalResourceRegistration(*object))
            return reject(id);
        if (!object->allocation().estimatedSizeBytes
            || object->allocation().estimatedSizeBytes > budget.maxRetainedBufferBytes)
            return reject("RHI-HOST-GEOMETRY-CONTENT-BYTE-CAPACITY");
        const auto state = object->observeOriginalCommittedState();
        if (!state || state->state() != ResourceState::ShaderRead)
            return reject("RHI-WEBGPU-GEOMETRY-CONTENT-STATE");
        auto payload = webGpuNativePayloadFor(*object);
        if (!payload || !payload->sharedOwnerMatches(payload) || !payload->allocationReleaseToken
            || payload->sizeBytes != desc.sizeBytes
            || payload->creationState() != desc.initialState)
            return reject("RHI-WEBGPU-GEOMETRY-CONTENT-NATIVE-BUFFER");
        object->sealWrites();
        WebGpuTransferResourceObservation identity;
        if (const auto* id = captureWebGpuRhiOriginalGeometryBuffer(device, *object, identity))
            return reject(id);
        auto lease = Lease::prepareOriginal(object,
            {1u, true, Lease::retainedOwnerCount, budget.maxCpuLeaseBytes},
            quotas.lease,
            diagnostic);
        if (!lease)
            return {};
        core::CpuMemoryQuotaScope scope(quotas.metadata);
        auto result =
            shareOriginal(core::makeUnique<WebGpuOriginalGeometryContent>(ConstructionKey{},
                std::move(lease),
                std::move(payload),
                std::move(identity),
                std::move(frozen),
                budget,
                quotas,
                *state));
        diagnostic = result ? result->validate() : "RHI-WEBGPU-GEOMETRY-CONTENT-ALLOCATION";
        return diagnostic ? std::shared_ptr<const WebGpuOriginalGeometryContent>{} : result;
    } catch (const std::bad_alloc&) {
        return reject("RHI-WEBGPU-GEOMETRY-CONTENT-ALLOCATION");
    }
}

const char* WebGpuOriginalGeometryContent::validateOriginal(
    const std::shared_ptr<const WebGpuOriginalGeometryContent>& self) noexcept
{
    return !self || !self->hasQuotaBoundSharedOwner(self) ? "RHI-WEBGPU-GEOMETRY-CONTENT-OWNER"
                                                          : self->validate();
}

const char* WebGpuOriginalGeometryContent::validate() const noexcept
{
    if (const auto* id = validateIdentity())
        return id;
    // Immutable bytes survive read-only journal epochs. This current capture
    // is not borrowed by Pending: each native closure freezes its exact cold
    // observation and the original seal authenticates that capture separately.
    const auto current = lease_->originalObject()->observeOriginalCommittedState();
    if (!current || current->resourceGeneration() != state_.resourceGeneration()
        || current->state() != ResourceState::ShaderRead)
        return "RHI-WEBGPU-GEOMETRY-CONTENT-STATE";
    return lease_->originalObject()->validateOriginalCommittedState(*current);
}

const char* WebGpuOriginalGeometryContent::validateOriginalPending(
    const std::shared_ptr<const WebGpuOriginalGeometryContent>& self,
    const std::shared_ptr<const OriginalCommandRecordingSeal>& seal,
    const GpuBuffer::CommittedStateObservation& captured) noexcept
{
    if (!self || !self->hasQuotaBoundSharedOwner(self))
        return "RHI-WEBGPU-GEOMETRY-CONTENT-OWNER";
    if (!seal || !seal->hasQuotaBoundSharedOwner(seal))
        return "RHI-WEBGPU-GEOMETRY-CONTENT-PENDING-SEAL";
    if (const auto* id = self->validateIdentity())
        return id;
    if (captured.resourceGeneration() != self->state_.resourceGeneration())
        return "RHI-WEBGPU-GEOMETRY-CONTENT-PENDING-GENERATION";
    return seal->validateInitializedBufferPending(*self->lease_->originalObject(),
        captured,
        self->budget_.contractVersion == 2u
            ? OriginalInitializedBufferPendingUse::ImmutableBindlessShaderRead6
            : OriginalInitializedBufferPendingUse::ImmutableBindlessGeometryRead5,
        self->budget_.contractVersion == 2u ? 6u : 5u);
}

const char* WebGpuOriginalGeometryContent::validateIdentity() const noexcept
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
    if (!payload_ || !payload_->sharedOwnerMatches(payload_)
        || !core::SharedOwnerProjection::sameOwner(payload_, webGpuNativePayloadFor(*object))
        || !nativeIdentity_.matches() || !webGpuNativeDeviceAvailable() || webGpuNativeDeviceLost()
        || !payload_->allocationReleaseToken || payload_->sizeBytes != bytes_.size()
        || nativeIdentity_.allocationBytes != object->allocation().estimatedSizeBytes)
        return "RHI-WEBGPU-GEOMETRY-CONTENT-NATIVE-IDENTITY";
    if (!object->hostWritesSealed() || !object->gpuWritesSealed()
        || !object->gpuTransferWritesSealed())
        return "RHI-WEBGPU-GEOMETRY-CONTENT-WRITES-UNSEALED";
    if (!object->allocation().estimatedSizeBytes
        || object->allocation().estimatedSizeBytes > budget_.maxRetainedBufferBytes)
        return "RHI-HOST-GEOMETRY-CONTENT-BYTE-CAPACITY";
    return nullptr;
}

const char* WebGpuOriginalGeometryContent::validateLease(
    const std::shared_ptr<const WebGpuOriginalGeometryContent>& self,
    const std::shared_ptr<const Lease>& lease) noexcept
{
    if (const auto* id = validateOriginal(self))
        return id;
    if (const auto* id = Lease::validateOriginal(lease))
        return id;
    return core::SharedOwnerProjection::sameOwner(lease, self->lease_)
        ? nullptr
        : "RHI-WEBGPU-GEOMETRY-CONTENT-LEASE-JOIN";
}

const char* WebGpuOriginalGeometryContent::analyzeInitializedIndices(
    const std::shared_ptr<const WebGpuOriginalGeometryContent>& self,
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

const char* WebGpuOriginalGeometryContent::validateInitializedVertices(
    const std::shared_ptr<const WebGpuOriginalGeometryContent>& self,
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
} // namespace arp::rhi_webgpu

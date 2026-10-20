#include "rhi/backends/vulkan/vulkan_original_geometry_content.h"

#include "rhi/backends/vulkan/vulkan_backend_objects.h"
#include "rhi/backends/vulkan/vulkan_bindless.h"
#include "rhi/frontend/command_recording.h"

namespace arp::rhi_vulkan {
using namespace rhi;

std::shared_ptr<const OriginalInitializedGeometrySource>
VulkanBindlessCrossExtension::prepareOriginalInitializedBufferPayload(GpuDevice& device,
    const OriginalInitializedBufferRequest& request,
    const char*& diagnostic) const
{
    return VulkanOriginalGeometryContent::prepareOriginal(device,
        request.desc,
        request.bytes,
        request.byteCount,
        request.budget,
        request.quotas,
        diagnostic);
}

VulkanOriginalGeometryContent::VulkanOriginalGeometryContent(ConstructionKey,
    std::shared_ptr<const Lease> lease,
    std::shared_ptr<VulkanRhiNativeResourcePayload> payload,
    std::shared_ptr<VulkanRhiRuntime> runtime,
    core::pmr::Vector<std::uint8_t> bytes,
    Budget budget,
    Quotas quotas,
    GpuBuffer::CommittedStateObservation state)
    : lease_(std::move(lease)), payload_(std::move(payload)), runtime_(std::move(runtime)),
      bytes_(std::move(bytes)), budget_(budget), quotas_(std::move(quotas)),
      state_(std::move(state)), device_(payload_->backendVulkanDeviceHandle()),
      buffer_(payload_->backendVulkanBufferHandle())
{}

std::uint64_t VulkanOriginalGeometryContent::queryOriginalAllocationBytes(
    GpuDevice& device, const BufferDesc& desc, const char*& diagnostic)
{
    const auto lower = Contract::lowering(device.backend());
    diagnostic = lower.diagnostic;
    if (!lower.initializationAvailable || diagnostic)
        return 0u;
    auto* extension = dynamic_cast<VulkanBindlessCrossExtension*>(
        static_cast<BindlessCrossExtension*>(device.queryInterface(kExtensionBindlessCross)));
    diagnostic = "RHI-VULKAN-GEOMETRY-CONTENT-DEVICE";
    if (!extension || !extension->runtime_ || !extension->nativeResources_)
        return 0u;
    return extension->nativeResources_->queryBufferAllocationBytes(desc, diagnostic);
}

std::shared_ptr<const VulkanOriginalGeometryContent> VulkanOriginalGeometryContent::prepareOriginal(
    GpuDevice& device,
    const BufferDesc& desc,
    const void* data,
    std::size_t bytes,
    const Budget& budget,
    const Quotas& quotas,
    const char*& diagnostic)
{
    const auto reject =
        [&](const char* id) -> std::shared_ptr<const VulkanOriginalGeometryContent> {
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
    auto* extension = dynamic_cast<VulkanBindlessCrossExtension*>(
        static_cast<BindlessCrossExtension*>(device.queryInterface(kExtensionBindlessCross)));
    if (!extension || !extension->runtime_ || !extension->nativeResources_)
        return reject("RHI-VULKAN-GEOMETRY-CONTENT-DEVICE");
    auto runtime = extension->nativeResources_->runtime();
    if (!runtime || runtime.get() != extension->runtime_ || !runtime->valid()
        || runtime->syntheticRuntime())
        return reject("RHI-VULKAN-GEOMETRY-CONTENT-AUTHENTIC-RUNTIME");
    const auto nativeBytes =
        extension->nativeResources_->queryBufferAllocationBytes(desc, diagnostic);
    if (diagnostic || !nativeBytes)
        return {};
    if (nativeBytes > budget.maxRetainedBufferBytes)
        return reject("RHI-HOST-GEOMETRY-CONTENT-BYTE-CAPACITY");
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
            return reject("RHI-VULKAN-GEOMETRY-CONTENT-CREATE");
        if (const auto* id = device.validateOriginalResourceRegistration(*object))
            return reject(id);
        if (!object->allocation().estimatedSizeBytes
            || object->allocation().estimatedSizeBytes > budget.maxRetainedBufferBytes)
            return reject("RHI-HOST-GEOMETRY-CONTENT-BYTE-CAPACITY");
        const auto state = object->observeOriginalCommittedState();
        if (!state || state->state() != ResourceState::ShaderRead)
            return reject("RHI-VULKAN-GEOMETRY-CONTENT-STATE");
        auto payload = vulkanRhiNativePayload(*object);
        if (!payload || !payload->backendMappedData() || !payload->backendVulkanBufferHandle()
            || payload->backendVulkanDeviceHandle() != runtime->logicalDevice())
            return reject("RHI-VULKAN-GEOMETRY-CONTENT-NATIVE-BUFFER");
        // writeBuffer performs the actual write and Vulkan flush. The adopted
        // object is still private here, so failed admission publishes nothing.
        const auto written = device.writeBuffer(*object, frozen.data(), frozen.size());
        if (!written.success)
            return reject("RHI-VULKAN-GEOMETRY-CONTENT-WRITE-FLUSH");
        object->sealWrites();
        auto lease = Lease::prepareOriginal(object,
            {1u, true, Lease::retainedOwnerCount, budget.maxCpuLeaseBytes},
            quotas.lease,
            diagnostic);
        if (!lease)
            return {};
        core::CpuMemoryQuotaScope scope(quotas.metadata);
        auto result =
            shareOriginal(core::makeUnique<VulkanOriginalGeometryContent>(ConstructionKey{},
                std::move(lease),
                std::move(payload),
                std::move(runtime),
                std::move(frozen),
                budget,
                quotas,
                *state));
        diagnostic = result ? result->validate() : "RHI-VULKAN-GEOMETRY-CONTENT-ALLOCATION";
        return diagnostic ? std::shared_ptr<const VulkanOriginalGeometryContent>{} : result;
    } catch (const std::bad_alloc&) {
        return reject("RHI-VULKAN-GEOMETRY-CONTENT-ALLOCATION");
    }
}

const char* VulkanOriginalGeometryContent::validateOriginal(
    const std::shared_ptr<const VulkanOriginalGeometryContent>& self) noexcept
{
    return !self || !self->hasQuotaBoundSharedOwner(self) ? "RHI-VULKAN-GEOMETRY-CONTENT-OWNER"
                                                          : self->validate();
}

const char* VulkanOriginalGeometryContent::validate() const noexcept
{
    if (const auto* id = validateIdentity())
        return id;
    // Immutable bytes survive read-only journal epochs. This current capture
    // is not borrowed by Pending: each native closure freezes its exact cold
    // observation and the original seal authenticates that capture separately.
    const auto current = lease_->originalObject()->observeOriginalCommittedState();
    if (!current || current->resourceGeneration() != state_.resourceGeneration()
        || current->state() != ResourceState::ShaderRead)
        return "RHI-VULKAN-GEOMETRY-CONTENT-STATE";
    return lease_->originalObject()->validateOriginalCommittedState(*current);
}

const char* VulkanOriginalGeometryContent::validateOriginalPending(
    const std::shared_ptr<const VulkanOriginalGeometryContent>& self,
    const std::shared_ptr<const OriginalCommandRecordingSeal>& seal,
    const GpuBuffer::CommittedStateObservation& captured) noexcept
{
    if (!self || !self->hasQuotaBoundSharedOwner(self))
        return "RHI-VULKAN-GEOMETRY-CONTENT-OWNER";
    if (!seal || !seal->hasQuotaBoundSharedOwner(seal))
        return "RHI-VULKAN-GEOMETRY-CONTENT-PENDING-SEAL";
    if (const auto* id = self->validateIdentity())
        return id;
    if (captured.resourceGeneration() != self->state_.resourceGeneration())
        return "RHI-VULKAN-GEOMETRY-CONTENT-PENDING-GENERATION";
    return seal->validateInitializedBufferPending(*self->lease_->originalObject(),
        captured,
        self->budget_.contractVersion == 2u
            ? OriginalInitializedBufferPendingUse::ImmutableBindlessShaderRead6
            : OriginalInitializedBufferPendingUse::ImmutableBindlessGeometryRead5,
        self->budget_.contractVersion == 2u ? 6u : 5u);
}

const char* VulkanOriginalGeometryContent::validateIdentity() const noexcept
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
    if (!runtime_ || !runtime_->valid() || runtime_->syntheticRuntime()
        || device_ != runtime_->logicalDevice() || !payload_
        || !core::SharedOwnerProjection::sameOwner(payload_, vulkanRhiNativePayload(*object))
        || payload_->backendVulkanDeviceHandle() != device_ || !buffer_
        || payload_->backendVulkanBufferHandle() != buffer_)
        return "RHI-VULKAN-GEOMETRY-CONTENT-NATIVE-IDENTITY";
    if (!object->hostWritesSealed() || !object->gpuWritesSealed()
        || !object->gpuTransferWritesSealed())
        return "RHI-VULKAN-GEOMETRY-CONTENT-WRITES-UNSEALED";
    if (!object->allocation().estimatedSizeBytes
        || object->allocation().estimatedSizeBytes > budget_.maxRetainedBufferBytes)
        return "RHI-HOST-GEOMETRY-CONTENT-BYTE-CAPACITY";
    return nullptr;
}

const char* VulkanOriginalGeometryContent::validateLease(
    const std::shared_ptr<const VulkanOriginalGeometryContent>& self,
    const std::shared_ptr<const Lease>& lease) noexcept
{
    if (const auto* id = validateOriginal(self))
        return id;
    if (const auto* id = Lease::validateOriginal(lease))
        return id;
    return core::SharedOwnerProjection::sameOwner(lease, self->lease_)
        ? nullptr
        : "RHI-VULKAN-GEOMETRY-CONTENT-LEASE-JOIN";
}

const char* VulkanOriginalGeometryContent::analyzeInitializedIndices(
    const std::shared_ptr<const VulkanOriginalGeometryContent>& self,
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

const char* VulkanOriginalGeometryContent::validateInitializedVertices(
    const std::shared_ptr<const VulkanOriginalGeometryContent>& self,
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
} // namespace arp::rhi_vulkan

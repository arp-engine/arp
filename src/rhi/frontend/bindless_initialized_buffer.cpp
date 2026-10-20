#include "rhi/frontend/command_geometry_content.h"
#include "rhi/gpu_bindless.h"
#include "rhi/gpu_extensions.h"

namespace arp::rhi {
std::uint64_t BindlessCrossExtension::queryOriginalInitializedBufferAllocation(GpuDevice& device,
    const BufferDesc& desc,
    std::uint32_t contentVersion,
    const char*& diagnostic) const
{
    diagnostic = nullptr;
    const auto lowering =
        OriginalHostGeometryContentContract::lowering(device.backend(), contentVersion);
    if (!lowering.initializationAvailable || lowering.diagnostic) {
        diagnostic = lowering.diagnostic;
        return 0u;
    }
    if (device.queryInterface(kExtensionBindlessCross) != this) {
        diagnostic = "RHI-HOST-INITIALIZED-BUFFER-DEVICE-ISSUER";
        return 0u;
    }
    if (!desc.sizeBytes || desc.memory != MemoryUsage::CpuToGpu
        || desc.initialState != ResourceState::ShaderRead) {
        diagnostic = "RHI-HOST-GEOMETRY-CONTENT-LAYOUT";
        return 0u;
    }
    const auto bytes = queryOriginalSnapshotBufferAllocation(desc, diagnostic);
    if (diagnostic)
        return 0u;
    if (bytes < desc.sizeBytes) {
        diagnostic = "RHI-HOST-INITIALIZED-BUFFER-ALLOCATION-QUERY";
        return 0u;
    }
    return bytes;
}

std::shared_ptr<const OriginalInitializedGeometrySource>
BindlessCrossExtension::prepareOriginalInitializedBuffer(GpuDevice& device,
    const OriginalInitializedBufferRequest& request,
    const char*& diagnostic) const
{
    diagnostic = OriginalHostGeometryContentContract::validateDeclaration(
        request.desc, request.byteCount, request.budget);
    if (diagnostic)
        return {};
    if (!request.bytes) {
        diagnostic = "RHI-HOST-GEOMETRY-CONTENT-PAYLOAD";
        return {};
    }
    const auto allocation = queryOriginalInitializedBufferAllocation(
        device, request.desc, request.budget.contractVersion, diagnostic);
    if (diagnostic)
        return {};
    if (allocation > request.budget.maxRetainedBufferBytes) {
        diagnostic = "RHI-HOST-GEOMETRY-CONTENT-BYTE-CAPACITY";
        return {};
    }
    if ((diagnostic = OriginalHostGeometryContentContract::validateQuotas(
             device, request.budget, request.quotas)))
        return {};
    auto actor = prepareOriginalInitializedBufferPayload(device, request, diagnostic);
    if (diagnostic)
        return {};
    if (!actor || !actor->hasQuotaBoundSharedOwner(actor)
        || actor->initializedBytes() != request.byteCount) {
        diagnostic = "RHI-HOST-INITIALIZED-BUFFER-ACTOR";
        return {};
    }
    if ((diagnostic = OriginalCommandBufferLease::validateOriginal(actor->originalLease())))
        return {};
    if ((diagnostic = device.validateOriginalResourceRegistration(
             *actor->originalLease()->originalObject())))
        return {};
    return actor;
}

std::shared_ptr<const OriginalInitializedGeometrySource>
BindlessCrossExtension::prepareOriginalInitializedBufferPayload(
    GpuDevice&, const OriginalInitializedBufferRequest&, const char*& diagnostic) const
{
    diagnostic = "RHI-HOST-INITIALIZED-BUFFER-PAYLOAD-UNAVAILABLE";
    return {};
}
} // namespace arp::rhi

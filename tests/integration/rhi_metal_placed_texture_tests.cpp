#include "rhi/backends/metal/metal_backend_objects.h"
#include "rhi/backends/metal/metal_device.h"
#include "rhi/backends/metal/metal_native_resources.h"
#include "rhi/semantics/native_backend_capabilities.h"

#include <cassert>
#include <cstdio>

namespace {

void verifyDescriptorDoesNotRetainDestroyedPlacedResources()
{
    using namespace arp;
    using namespace arp::rhi;
    using namespace arp::rhi_metal;

    MetalRhiDeviceDesc deviceDesc;
    deviceDesc.capabilityContract = semantics::nativeBackendCapabilityContract(Backend::Metal);
    deviceDesc.capabilityContract.queriedFromNativeDevice = true;
    deviceDesc.capabilityContract.queriedExtendedFeatures = true;
    deviceDesc.memoryBudgetBytes = 4u * 1024u * 1024u;
    deviceDesc.uploadStagingPageSizeBytes = 4096u;
    deviceDesc.uploadStagingMaxPageCount = 1u;
    auto device = createMetalRhiDevice(deviceDesc);
    assert(device);
    const auto baseline = device->memoryStats();

    TextureDesc textureDesc{"DescriptorLivenessPlacedTexture",
        {64u, 64u, 1u},
        TextureFormat::Rgba16Float,
        TextureUsage::RenderAttachment | TextureUsage::Sampled,
        MemoryUsage::Transient,
        ResourceState::Undefined};
    const auto requirements = device->queryTexturePlacementRequirements(textureDesc);
    assert(requirements.sizeBytes != 0u && requirements.alignmentBytes != 0u);
    auto heap = device->createMemoryHeap(
        requirements.sizeBytes, requirements.alignmentBytes, MemoryUsage::Transient);
    assert(heap);
    auto texture = device->createPlacedTexture(textureDesc, *heap, 0u);
    assert(texture);
    auto view = device->createTextureView(*texture,
        {"DescriptorLivenessPlacedView", TextureFormat::Rgba16Float, TextureUsage::Sampled});
    assert(view);

    DescriptorLayoutDesc layout;
    layout.name = "DescriptorResourceLivenessPlacedLayout";
    layout.bindings.push_back({0u, DescriptorType::SampledTexture, ShaderStage::Fragment});
    auto arena = device->createDescriptorArena({"DescriptorResourceLivenessPlacedArena", 1u, 1u});
    assert(arena);
    auto set = arena->allocateDescriptorSet({"DescriptorResourceLivenessPlacedSet", layout});
    assert(set);
    const DescriptorBindingWrite write{
        0u, DescriptorType::SampledTexture, view->id(), DescriptorResourceKind::TextureView};
    assert(set->write(write).success);
    assert(set->validateBinding(0u).success);
    assert(device->memoryStats().allocatedBytes > baseline.allocatedBytes);

    // A descriptor owns the L2 ID only. It must not retain the native view,
    // placed texture, or heap after their public wrappers are destroyed.
    view.reset();
    texture.reset();
    heap.reset();
    assert(device->memoryStats().allocatedBytes == baseline.allocatedBytes);
    const auto validation = set->validateBinding(0u);
    assert(!validation.success);
    assert(validation.diagnostics.find("RHI-DESCRIPTOR-RESOURCE-NOT-LIVE") == 0u);
    const auto revision = set->contentRevision();
    assert(!set->write(write).success);
    assert(set->contentRevision() == revision);

    set.reset();
    arena.reset();
    assert(device->memoryStats().allocatedBytes == baseline.allocatedBytes);
    std::puts("DescriptorResourceLiveness/1 placed texture release and stale bind rejection: PASS");
}

} // namespace

int main()
{
    using namespace arp::rhi;
    using namespace arp::rhi_metal;
    assert(metalRhiNativePlacementAliasingAvailable());
    TextureDesc desc{"PlacedNormal",
        {64u, 64u, 1u},
        TextureFormat::Rgba16Float,
        TextureUsage::RenderAttachment | TextureUsage::Sampled | TextureUsage::Storage,
        MemoryUsage::Transient,
        ResourceState::Undefined};
    TexturePlacementRequirements requirements;
    assert(queryMetalRhiNativeTexturePlacementRequirements(desc, requirements));
    auto heap = createMetalRhiNativeHeap(requirements.sizeBytes, requirements.alignmentBytes);
    assert(heap);
    assert(heap->allocationSizeBytes >= heap->sizeBytes);
    assert(heap->hazardsTracked);
    auto payload = createPlacedMetalRhiNativeTexturePayload(*heap, desc, 0u);
    assert(payload && payload->aliasHeap && payload->resource.nativeRuntimeBacked);
    ResourceAllocation allocation{
        ResourceMemoryClass::DeviceLocal, requirements.sizeBytes, false, true};
    auto texture = createMetalRhiTexture({1u}, desc, allocation, payload, nullptr);
    assert(texture);
    assert(texture->desc().memory == MemoryUsage::Transient);
    assert(texture->desc().usage == desc.usage);
    assert(
        inspectMetalRhiNativePayload(*texture).storageMode == MetalRhiNativeStorageMode::Private);
    assert(!texture->allocation().nativeHandleExposed);
    assert(!createPlacedMetalRhiNativeTexturePayload(*heap, desc, heap->sizeBytes));
    texture.reset();
    auto savedHeap = payload->aliasHeap;
    payload->aliasHeap.reset();
    assert(!createMetalRhiTexture({2u}, desc, allocation, payload, nullptr));
    payload->aliasHeap = savedHeap;
    payload->resource.storageMode = MetalRhiNativeStorageMode::Memoryless;
    assert(!createMetalRhiTexture({3u}, desc, allocation, payload, nullptr));
    // Ordinary Transient textures still reject sampled/storage memoryless use.
    assert(!createMetalRhiTexture({4u}, desc, allocation, std::shared_ptr<void>{}, nullptr));
    desc.memory = MemoryUsage::GpuOnly;
    auto ordinary = createMetalRhiNativeTexturePayload(desc, MetalRhiMemoryPolicy::PrivateStorage);
    assert(ordinary && !ordinary->aliasHeap);
    assert(createMetalRhiTexture({5u}, desc, allocation, ordinary, nullptr));
    // Attachment compression metadata yields requirements that need not be
    // a native heap page multiple. Logical bounds still use the request.
    desc.extent = {64u, 32u, 1u};
    desc.memory = MemoryUsage::Transient;
    desc.usage = TextureUsage::RenderAttachment | TextureUsage::CopySrc;
    assert(queryMetalRhiNativeTexturePlacementRequirements(desc, requirements));
    auto roundedHeap =
        createMetalRhiNativeHeap(requirements.sizeBytes, requirements.alignmentBytes);
    assert(roundedHeap && roundedHeap->sizeBytes == requirements.sizeBytes);
    assert(roundedHeap->allocationSizeBytes >= roundedHeap->sizeBytes);
    assert(roundedHeap->hazardsTracked);
    assert(createPlacedMetalRhiNativeTexturePayload(*roundedHeap, desc, 0u));
    assert(!createPlacedMetalRhiNativeTexturePayload(*roundedHeap, desc, roundedHeap->sizeBytes));
    roundedHeap->hazardsTracked = false;
    assert(!createPlacedMetalRhiNativeTexturePayload(*roundedHeap, desc, 0u));
    roundedHeap->hazardsTracked = true;
    --roundedHeap->allocationSizeBytes;
    assert(!createPlacedMetalRhiNativeTexturePayload(*roundedHeap, desc, 0u));
    verifyDescriptorDoesNotRetainDestroyedPlacedResources();
    return 0;
}

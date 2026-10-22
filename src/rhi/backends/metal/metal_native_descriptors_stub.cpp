#include "core/memory/cpu_allocator.h"
#include "rhi/backends/metal/metal_native_descriptors.h"

namespace arp::rhi_metal {

namespace {

MetalRhiArgumentKind argumentKindFor(rhi::DescriptorType type)
{
    switch (type) {
    case rhi::DescriptorType::UniformBuffer:
    case rhi::DescriptorType::StorageBuffer:
    case rhi::DescriptorType::ReadOnlyStorageBuffer:
        return MetalRhiArgumentKind::Buffer;
    case rhi::DescriptorType::SampledTexture:
    case rhi::DescriptorType::StorageTexture:
    case rhi::DescriptorType::InputAttachment:
        return MetalRhiArgumentKind::Texture;
    case rhi::DescriptorType::Sampler:
        return MetalRhiArgumentKind::Sampler;
    }
    return MetalRhiArgumentKind::Texture;
}

} // namespace

std::shared_ptr<MetalRhiNativeDescriptorGroupPayload> createMetalRhiNativeDescriptorGroupPayload(
    const rhi::DescriptorLayoutDesc&,
    const std::pmr::vector<MetalRhiNativeDescriptorBindingInput>& bindings)
{
    auto payload = arp::core::makeShared<MetalRhiNativeDescriptorGroupPayload>();
    payload->nativeRuntimeDiagnosticOnly = true;
    payload->diagnostics = "Metal RHI descriptor argument bindings are diagnostic-only outside "
                           "Apple Objective-C++ builds";
    payload->bindings.reserve(bindings.size());
    for (const auto& binding : bindings) {
        payload->bindings.push_back({
            binding.binding.binding,
            binding.binding.arrayCount,
            binding.arrayElement,
            binding.binding.type,
            binding.binding.stages,
            binding.binding.depthTexture,
            binding.write.resourceKind,
            argumentKindFor(binding.binding.type),
            binding.buffer,
            binding.textureView,
            binding.sampler,
            binding.dynamicOffsetIndex,
            binding.write.resource,
        });
    }
    return payload;
}

std::shared_ptr<MetalRhiNativeDescriptorTablePayload> createMetalRhiNativeDescriptorTablePayload(
    const rhi::DescriptorTableDesc& desc,
    const std::pmr::vector<MetalRhiNativeDescriptorTableEntry>& entries)
{
    auto payload = arp::core::makeShared<MetalRhiNativeDescriptorTablePayload>();
    payload->desc = desc;
    payload->entries = entries;
    payload->diagnostics = "Metal RHI descriptor-table argument buffers are diagnostic-only "
                           "outside Apple Objective-C++ builds";
    return payload;
}

bool metalRhiNativeArgumentBuffersAvailable()
{
    return false;
}

} // namespace arp::rhi_metal

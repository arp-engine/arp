#include "rhi/backends/metal/metal_native_descriptors.h"

#include "core/cpu_allocator.h"
#include "rhi/backends/metal/metal_native_runtime.h"

#include <Metal/Metal.h>
#include <algorithm>

namespace arp::rhi_metal {

namespace {

id<MTLDevice> defaultMetalDevice()
{
    return reinterpret_cast<id<MTLDevice>>(metalRhiNativeDeviceHandle());
}

MetalRhiArgumentKind argumentKindFor(rhi::DescriptorType type)
{
    switch (type) {
    case rhi::DescriptorType::UniformBuffer:
    case rhi::DescriptorType::StorageBuffer:
    case rhi::DescriptorType::ReadOnlyStorageBuffer:
        return MetalRhiArgumentKind::Buffer;
    case rhi::DescriptorType::SampledTexture:
    case rhi::DescriptorType::StorageTexture:
        return MetalRhiArgumentKind::Texture;
    case rhi::DescriptorType::Sampler:
        return MetalRhiArgumentKind::Sampler;
    }
    return MetalRhiArgumentKind::Texture;
}

rhi::DescriptorResourceKind resourceKindFor(rhi::DescriptorType type)
{
    switch (type) {
    case rhi::DescriptorType::UniformBuffer:
    case rhi::DescriptorType::StorageBuffer:
    case rhi::DescriptorType::ReadOnlyStorageBuffer:
        return rhi::DescriptorResourceKind::Buffer;
    case rhi::DescriptorType::SampledTexture:
    case rhi::DescriptorType::StorageTexture:
    case rhi::DescriptorType::InputAttachment:
        return rhi::DescriptorResourceKind::TextureView;
    case rhi::DescriptorType::Sampler:
        return rhi::DescriptorResourceKind::Sampler;
    }
    return rhi::DescriptorResourceKind::TextureView;
}

} // namespace

std::shared_ptr<MetalRhiNativeDescriptorGroupPayload> createMetalRhiNativeDescriptorGroupPayload(
    const rhi::DescriptorLayoutDesc&,
    const std::pmr::vector<MetalRhiNativeDescriptorBindingInput>& bindings)
{
    auto payload = arp::core::makeShared<MetalRhiNativeDescriptorGroupPayload>();
    payload->nativeDevice = metalRhiNativeDeviceIdentity();
    payload->bindings.reserve(bindings.size());

    bool allResourcesNative = true;
    const auto device = payload->nativeDevice;
    for (const auto& binding : bindings) {
        const auto argumentKind = argumentKindFor(binding.binding.type);
        payload->bindings.push_back({
            binding.binding.binding,
            binding.binding.arrayCount,
            binding.arrayElement,
            binding.binding.type,
            binding.binding.stages,
            binding.binding.depthTexture,
            binding.write.resourceKind,
            argumentKind,
            binding.buffer,
            binding.textureView,
            binding.sampler,
            binding.dynamicOffsetIndex,
            binding.write.resource,
        });

        allResourcesNative = allResourcesNative
            && (argumentKind == MetalRhiArgumentKind::Buffer
                    ? binding.buffer && binding.buffer->resource.nativeRuntimeBacked
                        && binding.buffer->resource.nativeDevice.get() == device.get()
                    : argumentKind == MetalRhiArgumentKind::Texture
                    ? binding.textureView && binding.textureView->resource.nativeRuntimeBacked
                        && binding.textureView->resource.nativeDevice.get() == device.get()
                    : binding.sampler && binding.sampler->resource.nativeRuntimeBacked
                        && binding.sampler->resource.nativeDevice.get() == device.get());
    }
    // This payload uses direct encoder setBuffer/setTexture/setSampler calls.
    // Argument-buffer encoding is a different lowering and must not be mixed
    // with the direct binding contract.
    payload->nativeRuntimeBacked = allResourcesNative && defaultMetalDevice() != nil;
    payload->nativeRuntimeDiagnosticOnly = !payload->nativeRuntimeBacked;
    if (!payload->nativeRuntimeBacked) {
        payload->diagnostics = "Metal RHI failed to create native direct descriptor bindings";
    }
    return payload;
}

std::shared_ptr<MetalRhiNativeDescriptorTablePayload> createMetalRhiNativeDescriptorTablePayload(
    const rhi::DescriptorTableDesc& desc,
    const std::pmr::vector<MetalRhiNativeDescriptorTableEntry>& entries)
{
    auto payload = arp::core::makeShared<MetalRhiNativeDescriptorTablePayload>();
    payload->nativeDevice = metalRhiNativeDeviceIdentity();
    payload->desc = desc;
    payload->entries = entries;
    if (desc.label.empty() || desc.capacity == 0u || desc.stages == rhi::ShaderStage::None
        || desc.type == rhi::DescriptorType::InputAttachment
        || payload->entries.size() != desc.capacity) {
        payload->diagnostics = "RHI-METAL-DESCRIPTOR-TABLE-ABI-INCOMPLETE";
        return payload;
    }
    bool allResourcesNative = true;
    const auto device = payload->nativeDevice;
    for (std::size_t index = 0u; index < payload->entries.size(); ++index) {
        const auto& entry = payload->entries[index];
        if (entry.index != index || entry.type != desc.type
            || entry.resourceKind != resourceKindFor(desc.type)) {
            payload->diagnostics = "RHI-METAL-DESCRIPTOR-TABLE-ENTRY-INVALID";
            return payload;
        }
        switch (entry.resourceKind) {
        case rhi::DescriptorResourceKind::Buffer:
            allResourcesNative = allResourcesNative && entry.buffer
                && entry.buffer->resource.nativeRuntimeBacked
                && entry.buffer->resource.nativeDevice.get() == device.get();
            break;
        case rhi::DescriptorResourceKind::TextureView:
            allResourcesNative = allResourcesNative && entry.textureView
                && entry.textureView->resource.nativeRuntimeBacked
                && entry.textureView->resource.nativeDevice.get() == device.get();
            break;
        case rhi::DescriptorResourceKind::Sampler:
            allResourcesNative = allResourcesNative && entry.sampler
                && entry.sampler->resource.nativeRuntimeBacked
                && entry.sampler->resource.nativeDevice.get() == device.get();
            break;
        }
    }
    payload->nativeRuntimeBacked = allResourcesNative && defaultMetalDevice() != nil;
    payload->nativeRuntimeDiagnosticOnly = !payload->nativeRuntimeBacked;
    if (!payload->nativeRuntimeBacked) {
        payload->diagnostics = "RHI-METAL-DESCRIPTOR-TABLE-NATIVE-PAYLOAD-MISSING";
    }
    return payload;
}

bool metalRhiNativeArgumentBuffersAvailable()
{
    if (!@available(macOS 10.13, iOS 11.0, *))
        return false;
    id<MTLDevice> device = defaultMetalDevice();
    if (device == nil)
        return false;

    // The encoder is obtained from a real argument-buffer function rather
    // than inferred from a feature bit.  This probe only establishes the
    // native substrate; the shader artifact still has to carry
    // MetalArgumentBuffer/1 metadata before a pipeline can use a table.
    NSString* source = @"#include <metal_stdlib>\n"
                        "using namespace metal;\n"
                        "struct ProbeArgs { device uint* value [[id(0)]]; };\n"
                        "kernel void probe(constant ProbeArgs& args [[buffer(0)]], "
                        "uint3 tid [[thread_position_in_grid]]) { (void)args; (void)tid; }";
    NSError* error = nil;
    id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
    if (library == nil)
        return false;
    id<MTLFunction> function = [library newFunctionWithName:@"probe"];
    id<MTLArgumentEncoder> encoder =
        function != nil ? [function newArgumentEncoderWithBufferIndex:0] : nil;
    const bool available = encoder != nil && encoder.encodedLength > 0;
    [encoder release];
    [function release];
    [library release];
    return available;
}

} // namespace arp::rhi_metal

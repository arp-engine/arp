#include "core/cpu_allocator.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/gpu_descriptors.h"
#include "rhi/gpu_resources.h"

#include <cassert>

int main()
{
    using namespace arp::rhi;

    auto device = createNullDevice();
    assert(device);

    BufferDesc vertexBufferDesc;
    vertexBufferDesc.label = "StaticMeshVertices";
    vertexBufferDesc.sizeBytes = 4096;
    vertexBufferDesc.usage = ResourceUsage::VertexBuffer | ResourceUsage::CopyDst;
    vertexBufferDesc.memory = MemoryUsage::GpuOnly;

    auto vertexBuffer = device->createBuffer(vertexBufferDesc);
    assert(vertexBuffer);
    assert(vertexBuffer->id().isValid());
    assert(vertexBuffer->desc().sizeBytes == 4096u);
    assert(vertexBuffer->allocation().memoryClass == ResourceMemoryClass::DeviceLocal);
    assert(!vertexBuffer->allocation().nativeHandleExposed);

    BufferDesc invalidBufferDesc;
    invalidBufferDesc.label = "Invalid";
    invalidBufferDesc.sizeBytes = 0;
    invalidBufferDesc.usage = ResourceUsage::UniformBuffer;
    invalidBufferDesc.memory = MemoryUsage::CpuToGpu;
    assert(!device->createBuffer(invalidBufferDesc));

    TextureDesc colorDesc;
    colorDesc.label = "SceneColor";
    colorDesc.extent = {64, 64, 1};
    colorDesc.format = TextureFormat::Bgra8Unorm;
    colorDesc.usage = TextureUsage::RenderAttachment | TextureUsage::Sampled;
    colorDesc.memory = MemoryUsage::GpuOnly;

    auto colorTexture = device->createTexture(colorDesc);
    assert(colorTexture);
    assert(colorTexture->id().isValid());
    assert(colorTexture->desc().extent.width == 64u);
    assert(colorTexture->allocation().estimatedSizeBytes == 64u * 64u * 4u);

    const auto memory = device->memoryStats();
    assert(memory.liveResourceCount == 2u);
    assert(memory.allocatedBytes >= 4096u + 64u * 64u * 4u);
    assert(memory.backendFallbacksHiddenFromUpperLayer);

    DescriptorLayoutDesc layout;
    layout.name = "FrameDescriptors";
    layout.bindings.push_back({0, DescriptorType::UniformBuffer, ShaderStage::Vertex});
    layout.bindings.push_back({1, DescriptorType::SampledTexture, ShaderStage::Fragment});

    auto arena = device->createDescriptorArena({"FrameArena", 2, 4});
    assert(arena);

    const auto firstSet = arena->allocateSet(layout);
    assert(firstSet.success);
    assert(firstSet.setIndex == 0u);
    assert(firstSet.descriptorCapacity == 2u);

    const auto secondSet = arena->allocateSet(layout);
    assert(secondSet.success);
    assert(secondSet.setIndex == 1u);

    const auto exhausted = arena->allocateSet(layout);
    assert(!exhausted.success);
    assert(exhausted.diagnostics.find("descriptor arena") != std::pmr::string::npos);
    assert(arena->stats().allocatedSets == 2u);

    auto accountingDevice = createNullDevice();
    assert(accountingDevice);
    auto maxSizeBuffer = accountingDevice->createBuffer({
        "MaxSizeBuffer",
        0xffffffffffffffffull,
        ResourceUsage::UniformBuffer,
        MemoryUsage::CpuToGpu,
        ResourceState::Undefined,
    });
    assert(maxSizeBuffer);
    assert(accountingDevice->memoryStats().allocatedBytes == 0xffffffffffffffffull);
    assert(accountingDevice->memoryStats().liveResourceCount == 1u);

    auto overflowingBuffer = accountingDevice->createBuffer({
        "OverflowingBuffer",
        1u,
        ResourceUsage::UniformBuffer,
        MemoryUsage::CpuToGpu,
        ResourceState::Undefined,
    });
    assert(!overflowingBuffer);
    assert(accountingDevice->memoryStats().allocatedBytes == 0xffffffffffffffffull);
    assert(accountingDevice->memoryStats().liveResourceCount == 1u);

    auto textureAccountingDevice = createNullDevice();
    assert(textureAccountingDevice);
    auto overflowingTexture = textureAccountingDevice->createTexture({
        "OverflowingTexture",
        {0xffffffffu, 0xffffffffu, 0xffffffffu},
        TextureFormat::Rgba16Float,
        TextureUsage::Sampled,
        MemoryUsage::GpuOnly,
        ResourceState::Undefined,
    });
    assert(!overflowingTexture);
    assert(textureAccountingDevice->memoryStats().allocatedBytes == 0u);
    assert(textureAccountingDevice->memoryStats().liveResourceCount == 0u);

    return 0;
}

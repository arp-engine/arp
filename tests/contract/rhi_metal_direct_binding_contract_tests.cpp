#include "rhi/semantics/metal_direct_binding_validation.h"
#include "rhi/semantics/metal_vertex_stream_lowering.h"

#include <cassert>

int main()
{
    using namespace arp::rhi;
    using namespace arp::rhi::semantics;
    for (const auto type : {DescriptorType::UniformBuffer,
             DescriptorType::StorageBuffer,
             DescriptorType::ReadOnlyStorageBuffer}) {
        assert(metalIndirectCommandSupportsDirectBinding(type));
    }
    for (const auto type : {DescriptorType::SampledTexture,
             DescriptorType::StorageTexture,
             DescriptorType::Sampler,
             DescriptorType::InputAttachment,
             static_cast<DescriptorType>(255)}) {
        assert(!metalIndirectCommandSupportsDirectBinding(type));
    }
    MetalPhysicalBindingStages footprint;
    assert(footprint.stages(DescriptorType::UniformBuffer, 1u) == ShaderStage::None);
    assert(footprint.record(DescriptorType::UniformBuffer, 1u, ShaderStage::Vertex));
    assert(footprint.record(DescriptorType::StorageBuffer, 1u, ShaderStage::Fragment));
    assert(footprint.stages(DescriptorType::ReadOnlyStorageBuffer, 1u)
        == (ShaderStage::Vertex | ShaderStage::Fragment));
    assert(footprint.record(DescriptorType::SampledTexture, 1u, ShaderStage::Compute));
    assert(footprint.stages(DescriptorType::StorageTexture, 1u) == ShaderStage::Compute);
    assert(footprint.stages(DescriptorType::Sampler, 1u) == ShaderStage::None);
    assert(!footprint.record(DescriptorType::UniformBuffer, 31u, ShaderStage::Vertex));
    assert(!footprint.record(DescriptorType::SampledTexture, 128u, ShaderStage::Fragment));
    assert(!footprint.record(DescriptorType::Sampler, 16u, ShaderStage::Compute));
    assert(!footprint.record(DescriptorType::InputAttachment, 0u, ShaderStage::Fragment));
    assert(!footprint.record(DescriptorType::Sampler, 0u, ShaderStage::None));
    assert(!footprint.record(
        DescriptorType::Sampler, 0u, ShaderStage::Vertex | ShaderStage::Fragment));
    assert(footprint.stages(DescriptorType::Sampler, UINT32_MAX) == ShaderStage::None);
    for (const auto type : {DescriptorType::UniformBuffer,
             DescriptorType::StorageBuffer,
             DescriptorType::ReadOnlyStorageBuffer}) {
        assert(validMetalDirectBindingSlot(type, 0u));
        assert(validMetalDirectBindingSlot(type, 30u));
        assert(!validMetalDirectBindingSlot(type, 31u));
        assert(!validMetalDirectBindingSlot(type, UINT32_MAX));
    }
    for (const auto type : {DescriptorType::SampledTexture, DescriptorType::StorageTexture}) {
        for (const auto slot : {0u, 30u, 31u, 32u, 127u}) {
            assert(validMetalDirectBindingSlot(type, slot));
        }
        assert(!validMetalDirectBindingSlot(type, 128u));
        assert(!validMetalDirectBindingSlot(type, UINT32_MAX));
    }
    assert(validMetalDirectBindingSlot(DescriptorType::Sampler, 15u));
    assert(!validMetalDirectBindingSlot(DescriptorType::Sampler, 16u));
    assert(!validMetalDirectBindingSlot(DescriptorType::Sampler, UINT32_MAX));
    assert(!validMetalDirectBindingSlot(DescriptorType::InputAttachment, 0u));
    assert(!validMetalDirectBindingSlot(static_cast<DescriptorType>(255), 0u));

    arp::core::pmr::Vector<VertexBufferLayoutDesc> streams(2);
    streams[0].slot = 0u;
    streams[1].slot = 30u;
    // PC, direct buffer, argument buffer, tessellation, counted-indirect slots.
    const auto reserved = (1u << 0u) | (1u << 2u) | (1u << 4u) | (1u << 28u) | (1u << 30u);
    const auto mapping = lowerMetalVertexStreams(streams, reserved);
    assert(mapping.success && mapping.slots[0] == 1u && mapping.slots[30] == 3u);
    assert(mapping.slots[1] == UINT32_MAX);
    std::swap(streams[0], streams[1]);
    assert(lowerMetalVertexStreams(streams, reserved).slots == mapping.slots);
    assert(!lowerMetalVertexStreams(streams, 0x7fffffffu).success);
    assert(!lowerMetalVertexStreams(streams, 0x80000000u).success);
    assert(lowerMetalVertexStreams({}, 0x7fffffffu).success);
    streams[1].slot = 30u;
    assert(!lowerMetalVertexStreams(streams, 0u).success);
    streams[1].slot = 31u;
    assert(!lowerMetalVertexStreams(streams, 0u).success);
    streams[1].slot = UINT32_MAX;
    assert(!lowerMetalVertexStreams(streams, 0u).success);
    streams[1].slot = 0u;
    assert(lowerMetalVertexStreams(streams, 0x1fffffffu).success);
    assert(!lowerMetalVertexStreams(streams, 0x3fffffffu).success);
}

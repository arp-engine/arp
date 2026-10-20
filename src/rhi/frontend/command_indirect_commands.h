#pragma once

#include <cstddef>
#include <cstdint>

namespace arp::rhi {

/// GPU-side indirect draw command record for drawIndexedIndirect /
/// drawIndexedIndirectCount. The layout matches the native Vulkan
/// VkDrawIndexedIndirectCommand (and the equivalent Metal / WebGPU
/// indirect-draw argument structures): 5 × uint32/int32, 20 bytes, no
/// padding. A compute shader writes an array of these into a buffer created
/// with ResourceUsage::StorageBuffer | ResourceUsage::IndirectBuffer, then a
/// subsequent render pass reads them via drawIndexedIndirect[Count].
///
/// P5 GPU-driven vegetation cull: the compute-cull shader populates one
/// record per draw group (templateId × sectionIndex), setting instanceCount
/// via atomicAdd. firstInstance points into the global instance buffer.
struct DrawIndexedIndirectCommand {
    std::uint32_t indexCount;    ///< Number of indices to draw (from template section).
    std::uint32_t instanceCount; ///< Number of visible instances (GPU-written).
    std::uint32_t firstIndex;    ///< Index buffer offset / sizeof(uint32).
    std::int32_t vertexOffset;   ///< Vertex buffer offset (typically 0).
    std::uint32_t firstInstance; ///< Offset into the global instance buffer.
};

static_assert(sizeof(DrawIndexedIndirectCommand) == 20,
    "DrawIndexedIndirectCommand must be 20 bytes (native layout)");
static_assert(
    offsetof(DrawIndexedIndirectCommand, indexCount) == 0, "indexCount must be at offset 0");
static_assert(
    offsetof(DrawIndexedIndirectCommand, instanceCount) == 4, "instanceCount must be at offset 4");
static_assert(
    offsetof(DrawIndexedIndirectCommand, firstIndex) == 8, "firstIndex must be at offset 8");
static_assert(
    offsetof(DrawIndexedIndirectCommand, vertexOffset) == 12, "vertexOffset must be at offset 12");
static_assert(offsetof(DrawIndexedIndirectCommand, firstInstance) == 16,
    "firstInstance must be at offset 16");

// IndirectInstanceBase/1 accompanies a physical firstInstance=0 record.
// The shader checks the complete logical range before adding the instance ID.
struct IndirectInstanceBase {
    std::uint32_t firstInstance = 0;
    std::uint32_t instanceCount = 0;
    std::uint32_t instanceCapacity = 0;
    std::uint32_t contractVersion = 0;
};

static_assert(sizeof(IndirectInstanceBase) == 16);
static_assert(offsetof(IndirectInstanceBase, firstInstance) == 0);
static_assert(offsetof(IndirectInstanceBase, instanceCount) == 4);
static_assert(offsetof(IndirectInstanceBase, instanceCapacity) == 8);
static_assert(offsetof(IndirectInstanceBase, contractVersion) == 12);

struct ZeroBaseIndirectProducerParams {
    std::uint32_t sourceWordOffset = 0;
    std::uint32_t instanceCapacity = 0;
    std::uint32_t reserved = 0;
    std::uint32_t contractVersion = 1;
};

static_assert(sizeof(ZeroBaseIndirectProducerParams) == 16);
static_assert(offsetof(ZeroBaseIndirectProducerParams, contractVersion) == 12);

} // namespace arp::rhi

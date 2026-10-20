#include "renderer/render_graph/frame_graph_uniform_ring_buffer.h"

#include "core/diagnostic_error.h"
#include "rhi/gpu_device.h"

#include <stdexcept>

namespace arp::render_graph {

FrameGraphUniformRingBuffer::FrameGraphUniformRingBuffer(
    rhi::GpuDevice& device, std::uint32_t frameSlotCount, std::uint32_t bytesPerSlot)
    : FrameGraphUniformRingBuffer(
          device, frameSlotCount, bytesPerSlot, rhi::ResourceState::Undefined)
{}

FrameGraphUniformRingBuffer::FrameGraphUniformRingBuffer(rhi::GpuDevice& device,
    std::uint32_t frameSlotCount,
    std::uint32_t bytesPerSlot,
    rhi::ResourceState initialState)
    : device_(device)
{
    slots_.reserve(frameSlotCount);
    for (std::uint32_t slot = 0; slot < frameSlotCount; ++slot) {
        Slot s;
        s.buffer = device.createBuffer({
            "FrameGraphUniformRingSlot" + arp::core::toPmrString(std::to_string(slot)),
            bytesPerSlot,
            rhi::ResourceUsage::UniformBuffer | rhi::ResourceUsage::CopyDst,
            rhi::MemoryUsage::CpuToGpu,
            initialState,
        });
        if (!s.buffer) {
            throw std::runtime_error(
                "FrameGraphUniformRingBuffer: failed to create backing buffer for slot "
                + std::to_string(slot));
        }
        s.capacity = bytesPerSlot;
        slots_.push_back(std::move(s));
        ++bufferCreationCount_;
    }
}

FrameGraphUniformRingBuffer::~FrameGraphUniformRingBuffer() = default;

FrameGraphUniformRingBuffer::Allocation FrameGraphUniformRingBuffer::allocate(
    std::uint32_t frameSlot, std::uint32_t sizeBytes)
{
    if (frameSlot >= slots_.size()) {
        throw std::runtime_error("FrameGraphUniformRingBuffer: frame slot out of range");
    }
    auto& slot = slots_[frameSlot];
    // Align to 256 bytes (uniform buffer offset alignment).
    constexpr std::uint64_t alignment = 256u;
    const auto alignedOffset = (slot.cursor + alignment - 1u) & ~(alignment - 1u);
    if (alignedOffset + sizeBytes > slot.capacity) {
        throw std::runtime_error("FrameGraphUniformRingBuffer: backing buffer exhausted for slot "
            + std::to_string(frameSlot));
    }
    Allocation result;
    result.buffer = slot.buffer.get();
    result.offsetBytes = alignedOffset;
    slot.cursor = alignedOffset + sizeBytes;
    ++allocationCount_;
    return result;
}

void FrameGraphUniformRingBuffer::onFrameComplete(std::uint32_t frameSlot)
{
    (void)frameSlot;
    throw core::DiagnosticError("RHI-FRAME-GRAPH-UNIFORM-RING-COMPLETION-UNAUTHENTICATED");
}

void FrameGraphUniformRingBuffer::resetOrdered(
    std::uint32_t frameSlot, const rhi::OriginalOrderedGraphicsExecution& issuer)
{
    if (const auto* id = issuer.diagnostic(device_))
        throw core::DiagnosticError(id);
    if (frameSlot >= slots_.size())
        throw core::DiagnosticError("RG-ORDERED-UNIFORM-SLOT-RANGE");
    auto& slot = slots_[frameSlot];
    if (slot.buffer->desc().memory != rhi::MemoryUsage::CpuToGpu)
        throw core::DiagnosticError("RG-ORDERED-UNIFORM-STAGING-INTENT");
    if (const auto* id = issuer.payload_->reuseDiagnostic(*slot.buffer))
        throw core::DiagnosticError(id);
    slot.cursor = 0;
}

} // namespace arp::render_graph

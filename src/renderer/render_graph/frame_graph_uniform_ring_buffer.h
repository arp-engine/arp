#pragma once

#include "core/api.h"
#include "core/memory/cpu_allocator.h"
#include "rhi/frontend/ordered_graphics_execution.h"
#include "rhi/gpu_resources.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace arp::rhi {
class GpuDevice;
}

namespace arp::render_graph {

/// Per-frame-slot uniform ring buffer that eliminates per-pass per-frame
/// createBuffer(256) calls. Each frame slot owns a single large GpuBuffer;
/// allocations within a frame are bump-allocated from a cursor. The cursor
/// cannot be reset through an unauthenticated CPU frame notification. The
/// legacy executor stays rejected until an original completion issuer is bound.
///
/// Backing resources stay device-owned; native handles are not exposed.
class ARP_API FrameGraphUniformRingBuffer {
public:
    FrameGraphUniformRingBuffer(
        rhi::GpuDevice& device, std::uint32_t frameSlotCount, std::uint32_t bytesPerSlot = 4096u);
    FrameGraphUniformRingBuffer(rhi::GpuDevice& device,
        std::uint32_t frameSlotCount,
        std::uint32_t bytesPerSlot,
        rhi::ResourceState initialState);
    ~FrameGraphUniformRingBuffer();

    FrameGraphUniformRingBuffer(const FrameGraphUniformRingBuffer&) = delete;
    FrameGraphUniformRingBuffer& operator=(const FrameGraphUniformRingBuffer&) = delete;

    /// Allocate `sizeBytes` within the given frame slot's backing buffer.
    /// Returns the buffer and the byte offset within it. The cursor advances
    /// by sizeBytes (aligned to 256). Throws std::runtime_error if the slot's
    /// buffer is exhausted.
    struct Allocation {
        rhi::GpuBuffer* buffer = nullptr;
        std::uint64_t offsetBytes = 0;
    };

    Allocation allocate(std::uint32_t frameSlot, std::uint32_t sizeBytes);

    /// Legacy signature retained; throws a stable diagnostic because it has no
    /// original completion issuer. Never authorizes reuse or resets a cursor.
    void onFrameComplete(std::uint32_t frameSlot);
    /// Reclaims only original CPU staging after recorder readers retire.
    /// Neither this call nor its success is a completion credential.
    void resetOrdered(std::uint32_t frameSlot, const rhi::OriginalOrderedGraphicsExecution&);

    /// Number of per-frame allocations served without calling createBuffer
    /// (i.e. from the pre-allocated backing buffer).
    std::uint32_t allocationCount() const noexcept { return allocationCount_; }

    /// Number of createBuffer calls made during construction (one per slot).
    std::uint32_t bufferCreationCount() const noexcept { return bufferCreationCount_; }

private:
    struct Slot {
        arp::core::UniquePtr<rhi::GpuBuffer> buffer;
        std::uint64_t cursor = 0;
        std::uint64_t capacity = 0;
    };

    rhi::GpuDevice& device_;
    core::pmr::Vector<Slot> slots_{&core::defaultCpuMemoryResource()};
    std::uint32_t allocationCount_ = 0;
    std::uint32_t bufferCreationCount_ = 0;
};

} // namespace arp::render_graph

#pragma once

#include "core/cpu_allocator.h"
#include "rhi/gpu_resources.h"

#include <cstdint>
#include <string>
#include <vector>

namespace arp::rhi {

constexpr std::uint32_t kMaxDeferredFrameResourceDestroys = 1024;
constexpr std::uint32_t kMaxTransientFrameResources = 1024;

enum class FrameResourceKind {
    Buffer,
    Texture,
    DescriptorSet,
    CommandBuffer,
    SwapchainImage,
    UploadBlock
};

enum class FrameResourceScope { Device, Surface, Transient };

struct FrameResourceRecord {
    GpuResourceId id;
    std::uint64_t sizeBytes = 0;
    FrameResourceKind kind = FrameResourceKind::Buffer;
    FrameResourceScope scope = FrameResourceScope::Device;
    bool nativeHandleExposed = false;
};

struct FrameResourceRecyclerDesc {
    std::uint32_t logicalFrameSlots = 2;
    std::uint32_t maxDeferredDestroys = 0;
    std::uint32_t maxTransientResources = 0;
};

struct FrameResourceOperationResult {
    bool success = false;
    std::pmr::string diagnostics;
    std::uint32_t frameSlot = 0;
    std::uint32_t releasedCount = 0;
    std::uint64_t releasedBytes = 0;
};

struct FrameResourceRecyclerStats {
    std::uint32_t logicalFrameSlots = 2;
    std::uint32_t pendingDestroyCount = 0;
    std::uint32_t transientResourceCount = 0;
    std::uint64_t pendingDestroyBytes = 0;
    std::uint64_t transientBytes = 0;
    std::uint64_t releasedBytes = 0;
    bool backendFallbacksHiddenFromUpperLayer = true;
    bool nativeHandleExposedToUpperLayer = false;
};

class FrameResourceRecycler {
public:
    /// Construct with an explicit CPU memory resource. When omitted, the
    /// process-wide default resource is used. Threaded through to all pmr
    /// members so a caller can bind a per-instance intent/budget instead of
    /// silently falling back to the global default.
    explicit FrameResourceRecycler(const FrameResourceRecyclerDesc& desc,
        std::pmr::memory_resource* memoryResource = &core::defaultCpuMemoryResource());

    std::uint32_t logicalFrameSlots() const;

    FrameResourceOperationResult deferDestroy(
        const FrameResourceRecord& record, std::uint64_t lastUsedFrameSerial);

    FrameResourceOperationResult retireCompletedFrames(std::uint64_t completedFrameSerial);

    FrameResourceOperationResult trackTransient(
        const FrameResourceRecord& record, std::uint64_t frameSerial);

    FrameResourceOperationResult releaseTransientForFrameSlot(std::uint32_t frameSlot);
    FrameResourceOperationResult releaseAllTransientResources();
    FrameResourceOperationResult releaseSurfaceScopedResources();

    FrameResourceRecyclerStats stats() const;

private:
    struct DeferredResource {
        FrameResourceRecord record;
        std::uint64_t retireAfterFrameSerial = 0;
        std::uint32_t frameSlot = 0;
    };

    struct TransientResource {
        FrameResourceRecord record;
        std::uint32_t frameSlot = 0;
    };

    FrameResourceOperationResult validateConfiguration() const;
    FrameResourceOperationResult validateRecord(const FrameResourceRecord& record) const;
    FrameResourceOperationResult validateFrameSlot(std::uint32_t frameSlot) const;
    std::uint32_t frameSlotForSerial(std::uint64_t frameSerial) const;

    FrameResourceRecyclerDesc desc_;
    std::pmr::memory_resource* memoryResource_;
    std::pmr::vector<DeferredResource> deferred_;
    std::pmr::vector<TransientResource> transient_;
    std::uint64_t releasedBytes_ = 0;
    std::pmr::string configurationDiagnostics_;
};

} // namespace arp::rhi

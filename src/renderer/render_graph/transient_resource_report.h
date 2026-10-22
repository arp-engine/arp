#pragma once

#include "core/memory/cpu_allocator.h"
#include "renderer/render_graph/render_graph_types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace arp::render_graph {

struct FrameGraphTransientAssignment {
    std::pmr::string resourceName;
    TextureHandle handle;
    std::uint32_t aliasGroup = 0;
    std::uint32_t physicalAllocationIndex = 0;
};

/// A real physical allocation backing one or more transient textures that
/// share an alias group. Members within the same allocation are offset-
/// packed into the same backing memory region. On backends that support
/// aliasing (Vulkan VK_IMAGE_CREATE_ALIAS_BIT, Metal heap aliasing), all
/// members share one native allocation. On backends without aliasing
/// (WebGPU, Null), each member gets a separate allocation but the report
/// still carries the computed size/alignment for diagnostics.
struct TransientPhysicalAllocation {
    std::uint32_t aliasGroup = 0;
    std::uint64_t sizeBytes = 0;
    std::uint64_t alignmentBytes = 0;

    struct Member {
        std::uint32_t handleId = 0;
        std::uint64_t offsetBytes = 0;
        std::uint64_t memberSizeBytes = 0;
    };

    std::pmr::vector<Member> members;
};

struct FrameGraphTransientAllocationReport {
    bool success = false;
    std::pmr::string diagnostics;
    std::uint32_t transientTextureCount = 0;
    std::uint32_t physicalTextureAllocationCount = 0;
    std::uint32_t aliasReuseCount = 0;
    bool backendFallbacksHiddenFromUpperLayer = true;
    bool nativeHandleExposedToUpperLayer = false;

    /// Capability flag: true if the backend can share physical memory
    /// across aliased transient textures (Vulkan alias bit, Metal heap).
    /// False on WebGPU and Null (separate allocations per texture).
    /// When false, execution still succeeds but with higher memory usage;
    /// the degradation is explicit, not a silent compatibility hack.
    bool aliasedMemory = false;
    /// True only after every alias-group member has a known lifetime and
    /// members sharing a physical allocation have non-overlapping intervals.
    bool lifetimeProofValidated = false;
    /// True only after the physical allocation peak is bounded by the
    /// compiler-provided transient budget.
    bool budgetProofValidated = false;
    /// True only when every physical allocation member had backend/resource
    /// size metadata. A report produced without size metadata is useful for
    /// alias-group diagnostics but cannot authorize heap creation.
    bool sizeMetadataValidated = false;
    /// Native aliasing requires completion-gated retirement of the physical
    /// allocation. This is a requirement carried to the execution layer, not
    /// permission to recycle before its fence signals.
    bool completionGatedRetirementRequired = false;

    std::pmr::vector<FrameGraphTransientAssignment> assignments;

    /// Real physical allocations, one per alias group. Each carries the
    /// group's size (max of member sizes), alignment (max of member
    /// alignments), and member offsets (interval bin-packed).
    std::pmr::vector<TransientPhysicalAllocation> physicalAllocations;
};

/// Size and alignment metadata for a transient texture, used by the
/// allocator to compute physical allocation dimensions.
struct TransientTextureSizeInfo {
    std::uint32_t handleId = 0;
    std::uint64_t sizeBytes = 0;
    std::uint64_t alignmentBytes = 0;
};

} // namespace arp::render_graph

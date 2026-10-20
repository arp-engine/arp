#pragma once

#include "core/api.h"
#include "core/cpu_allocator.h"
#include "renderer/render_graph/compiled_frame_graph.h"
#include "renderer/render_graph/transient_resource_report.h"

namespace arp::render_graph {

/// Computes the physical allocation plan for transient textures based on
/// the compiled alias groups. Each alias group becomes one
/// TransientPhysicalAllocation with:
///   - sizeBytes = max(member sizeBytes)
///   - alignmentBytes = max(member alignmentBytes)
///   - members[handleId -> offsetBytes] (interval bin-packed within group)
///
/// The aliasedMemory capability defaults to false (Null/WebGPU behavior).
/// Backend-specific allocators override this to report true when the
/// backend supports real memory sharing.
class ARP_API FrameGraphTransientResourceAllocator final {
public:
    FrameGraphTransientAllocationReport allocate(const CompiledFrameGraph& compiled) const;

    /// Overload that accepts per-texture size/alignment metadata. The size
    /// info is typically computed by the FrameGraph from TextureDesc after
    /// compile, since the CompiledFrameGraph does not store TextureDesc.
    FrameGraphTransientAllocationReport allocate(const CompiledFrameGraph& compiled,
        const std::pmr::vector<TransientTextureSizeInfo>& sizeInfos) const;

    /// Set the aliasedMemory capability. Default: false (Null/WebGPU
    /// degradation). Backend-specific code sets this to true for
    /// Vulkan/Metal where real aliasing is supported.
    void setAliasedMemoryCapability(bool enabled) { aliasedMemory_ = enabled; }

    bool aliasedMemoryCapability() const { return aliasedMemory_; }

private:
    bool aliasedMemory_ = false;
};

} // namespace arp::render_graph

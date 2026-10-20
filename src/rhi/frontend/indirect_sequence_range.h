#pragma once
#include "core/strided_index_range.h"
#include "rhi/frontend/command_indirect_commands.h"
#include "rhi/gpu_resource_types.h"

namespace arp::rhi {
// SingleRecordIndirectSequence/1: validates all argument addresses before
// recording, without inspecting GPU-generated record contents.
class IndexedIndirectSequenceRange final {
public:
    IndexedIndirectSequenceRange(
        const BufferDesc& buffer, std::uint64_t offset, std::uint32_t count, std::uint32_t stride)
        : offsets_(offset,
              count,
              stride,
              buffer.sizeBytes >= sizeof(DrawIndexedIndirectCommand)
                  ? buffer.sizeBytes - sizeof(DrawIndexedIndirectCommand)
                  : 0u)
    {
        valid_ = buffer.sizeBytes >= sizeof(DrawIndexedIndirectCommand)
            && static_cast<std::uint32_t>(buffer.usage & ResourceUsage::IndirectBuffer) != 0u
            && (offset & 3u) == 0u && (stride & 3u) == 0u
            && stride >= sizeof(DrawIndexedIndirectCommand) && offsets_.valid();
    }

    bool valid() const noexcept { return valid_; }

    bool offset(std::uint32_t ordinal, std::uint64_t& value) const noexcept
    {
        return valid_ && offsets_.index(ordinal, value);
    }

private:
    core::StridedIndexRange offsets_;
    bool valid_ = false;
};
} // namespace arp::rhi

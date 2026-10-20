#pragma once
#include <cstdint>

namespace arp::rhi {
// Copied member-layout metadata, never an original owner or execution receipt.
// The producer must have proved member interpretation; size equality is not
// evidence. Static ordinary buffers only in the version1 query contract.
struct ShaderBufferMemberMemory {
    enum class Evidence { None, NativeBinaryIdentity, ValidatedTargetMembers };
    std::uint64_t sourceOccupiedBytes = 0u, targetMinimumBytes = 0u;
    Evidence evidence = Evidence::None;
    // Query2 declaration facts. Query1 leaves these zero. Pipeline queries never
    // issue capacity; the original Root copies it from canonical Range2 only.
    std::uint64_t sourceMinimumBytes = 0u;
    bool hasRuntimeArray = false;
    std::uint64_t sourceRuntimeArrayOffsetBytes = 0u, sourceRuntimeArrayStrideBytes = 0u;
    std::uint64_t targetRuntimeArrayOffsetBytes = 0u, targetRuntimeArrayStrideBytes = 0u;
    std::uint64_t sourceElementCapacity = 0u;
};
} // namespace arp::rhi

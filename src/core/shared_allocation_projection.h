#pragma once

#include "core/shared_owner_projection.h"

#include <cstdint>

namespace arp::core {

// Immutable owner projection, externally serialized. The accounting token is
// the identity of one allocation; the backing owner and byte charge must agree
// in every occurrence. Empty entries represent objects without an allocation.
// The mapper must be allocation-free, nonthrowing and immutable for inspection.
class SharedAllocationProjection final {
public:
    struct Record {
        std::shared_ptr<const void> allocation;
        std::shared_ptr<const void> backing;
        std::uint64_t bytes = 0u;
    };
    enum class Status { Complete, CountCapacity, MissingOwner, Alias, ByteCapacity };

    struct Result {
        Status status;
        std::uint64_t bytes;
    };

    template <class RecordAt>
    static Result inspect(std::size_t count,
        std::size_t maxRecords,
        std::uint64_t maxBytes,
        RecordAt&& recordAt) noexcept
    {
        if (count > maxRecords)
            return {Status::CountCapacity, 0u};
        std::uint64_t bytes = 0u;
        for (std::size_t i = 0u; i < count; ++i) {
            const auto current = recordAt(i);
            if (!current.allocation && !current.backing && !current.bytes)
                continue;
            if (!current.allocation || !current.backing || !current.bytes)
                return {Status::MissingOwner, 0u};
            bool shared = false;
            for (std::size_t j = 0u; j < i; ++j) {
                const auto earlier = recordAt(j);
                if (current.allocation != earlier.allocation
                    && !SharedOwnerProjection::sameControlBlock(
                        current.allocation, earlier.allocation))
                    continue;
                if (!SharedOwnerProjection::sameOwner(current.allocation, earlier.allocation)
                    || !SharedOwnerProjection::sameOwner(current.backing, earlier.backing)
                    || current.bytes != earlier.bytes)
                    return {Status::Alias, 0u};
                shared = true;
            }
            if (shared)
                continue;
            if (current.bytes > maxBytes - bytes)
                return {Status::ByteCapacity, 0u};
            bytes += current.bytes;
        }
        return {Status::Complete, bytes};
    }
};

} // namespace arp::core

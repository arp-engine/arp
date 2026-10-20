#pragma once
#include "core/shared_owner_projection.h"

#include <cstdint>

namespace arp::core {

// Inspect an immutable, serialized source inventory without materializing a
// second table. Key, domain, original pointer/control block and stable identity
// must agree in every duplicate. All-empty records are explicitly identity-free.
class SharedIdentityProjection final {
public:
    struct Record {
        std::uint64_t key = 0u;
        std::uint32_t domain = 0u;
        std::shared_ptr<const void> owner;
        const void* identity = nullptr;
        std::uint32_t keyNamespace = 0u;
    };
    enum class Status { Complete, ReferenceCapacity, MissingIdentity, Alias, IdentityCapacity };

    struct Result {
        Status status;
        std::size_t identities;
    };

    template <class RecordAt>
    static Result inspect(std::size_t count,
        std::size_t maxReferences,
        std::size_t maxIdentities,
        RecordAt&& recordAt) noexcept
    {
        if (count > maxReferences)
            return {Status::ReferenceCapacity, 0u};
        std::size_t identities = 0u;
        for (std::size_t i = 0u; i < count; ++i) {
            const auto current = recordAt(i);
            if (!current.key && !current.domain && !current.owner && !current.identity
                && !current.keyNamespace)
                continue;
            if (!current.key || !current.owner || !current.identity)
                return {Status::MissingIdentity, 0u};
            bool shared = false;
            for (std::size_t j = 0u; j < i; ++j) {
                const auto previous = recordAt(j);
                const bool sameKey =
                    current.key == previous.key && current.keyNamespace == previous.keyNamespace;
                if (!sameKey && current.owner != previous.owner
                    && !SharedOwnerProjection::sameControlBlock(current.owner, previous.owner)
                    && current.identity != previous.identity)
                    continue;
                if (!sameKey || current.domain != previous.domain
                    || current.identity != previous.identity
                    || !SharedOwnerProjection::sameOwner(current.owner, previous.owner))
                    return {Status::Alias, 0u};
                shared = true;
            }
            if (shared)
                continue;
            if (identities == maxIdentities)
                return {Status::IdentityCapacity, 0u};
            ++identities;
        }
        return {Status::Complete, identities};
    }
};
} // namespace arp::core

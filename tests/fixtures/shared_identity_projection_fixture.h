#pragma once
#include "core/shared_identity_projection.h"

#include <array>
#include <cassert>
#include <cstdio>

namespace arp::test {
class SharedIdentityProjectionFixture final {
public:
    static void verify()
    {
        using Projection = core::SharedIdentityProjection;
        using Status = Projection::Status;
        const auto first = core::makeShared<std::array<std::uint32_t, 2>>();
        const auto second = core::makeShared<std::uint32_t>();
        std::array<Projection::Record, 4> records{{{1u, 2u, first, first.get()},
            {1u, 2u, first, first.get()},
            {2u, 3u, second, second.get()},
            {}}};
        const auto inspect = [&](std::size_t refs = 4u, std::size_t ids = 2u) {
            return Projection::inspect(records.size(), refs, ids, [&](std::size_t i) {
                return records[i];
            });
        };
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(inspect().status == Status::Complete && inspect().identities == 2u);
        assert(inspect(3u).status == Status::ReferenceCapacity
            && inspect(4u, 1u).status == Status::IdentityCapacity);
        const auto original = records;
        records[1].key = 3u;
        assert(inspect().status == Status::Alias);
        records = original;
        records[1].domain = 7u;
        assert(inspect().status == Status::Alias);
        records = original;
        records[1].identity = &(*first)[1];
        assert(inspect().status == Status::Alias);
        records = original;
        records[1].owner = second;
        assert(inspect().status == Status::Alias);
        records = original;
        records[1].owner = std::shared_ptr<const void>(second, first.get());
        assert(inspect().status == Status::Alias);
        records = original;
        records[1].owner = std::shared_ptr<const void>(first, &(*first)[1]);
        assert(inspect().status == Status::Alias);
        records = original;
        records[2].identity = first.get();
        assert(inspect().status == Status::Alias);
        records = original;
        for (std::size_t field = 0u; field < 3u; ++field) {
            if (!field)
                records[2].key = 0u;
            else if (field == 1u)
                records[2].owner.reset();
            else
                records[2].identity = nullptr;
            assert(inspect().status == Status::MissingIdentity);
            records = original;
        }
        records[3].domain = 9u;
        assert(inspect().status == Status::MissingIdentity);
        records = original;
        records[2].key = 1u;
        records[2].keyNamespace = 1u;
        assert(inspect().status == Status::Complete && inspect().identities == 2u);
        records = original;
        records[1].keyNamespace = 1u;
        assert(inspect().status == Status::Alias);
        records = original;
        records[3].keyNamespace = 1u;
        assert(inspect().status == Status::MissingIdentity);
        records = original;
        assert(Projection::inspect(0u,
                   0u,
                   0u,
                   [](std::size_t) {
                       return Projection::Record{};
                   })
                   .status
            == Status::Complete);
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        std::puts(
            "BINDLESS_RHI_SHARED_IDENTITY_OK counts key domain control-block pointer alias allocation0");
        std::puts(
            "BINDLESS_RHI_SHARED_IDENTITY_NAMESPACE_OK independent-key-pairs shared-owner-namespace-alias-reject empty-namespace-reject default-preserved allocation0");
    }
};
} // namespace arp::test

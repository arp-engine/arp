#include "core/cpu_allocator.h"
#include "core/versioned_shared_owner.h"

#include <cassert>

namespace {
struct Request {
    std::uint32_t contractVersion = 0u;
    std::uint64_t byteBudget = 0u;
};

struct Ordinary {
    explicit Ordinary(bool* value) : destroyed(value) {}

    bool* destroyed;

    ~Ordinary() { *destroyed = true; }
};

struct Expanded {
    explicit Expanded(bool* value) : destroyed(value) {}

    bool* destroyed;

    ~Expanded() { *destroyed = true; }
};

using Owner = arp::core::VersionedSharedOwner<Request, Ordinary, Expanded, 3u>;

struct Pair {
    std::shared_ptr<const Ordinary> first, second;
};

using Value = arp::core::VersionedOwnedValue<Request, Pair, arp::core::UniquePtr<Expanded>, 3u>;
} // namespace

int main()
{
    using namespace arp::core;
    auto quota = makeShared<CpuMemoryQuota>(65536u);
    CpuMemoryQuotaScope scope(quota);
    for (const auto version : {1u, 2u, 3u}) {
        bool destroyed = false;
        if (version == 3u) {
            auto input = makeShared<Expanded>(&destroyed);
            Owner owner({version, 71u}, std::shared_ptr<const Expanded>(input));
            assert(&owner.expanded() == &owner.expanded() && owner.expanded().get() == input.get()
                && !owner.ordinary());
            input.reset();
            assert(!destroyed && owner.expanded().use_count() == 1u);
            assert(&owner.visit([](const auto& active) noexcept -> const bool& {
                return *active->destroyed;
            }) == &destroyed);
            const auto before = defaultCpuAllocator().stats().allocationCount;
            auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
            for (unsigned i = 0u; i < 100u; ++i) {
                assert(owner.expanded().use_count() == 1u && !owner.ordinary()
                    && owner.request().byteBudget == 71u);
                assert(&owner.visit([](const auto& active) noexcept -> const bool& {
                    return *active->destroyed;
                }) == &destroyed);
            }
            std::pmr::set_default_resource(previous);
            assert(defaultCpuAllocator().stats().allocationCount == before);
        } else {
            auto input = makeShared<Ordinary>(&destroyed);
            Owner owner({version, 71u}, std::shared_ptr<const Ordinary>(input));
            assert(&owner.ordinary() == &owner.ordinary() && owner.ordinary().get() == input.get()
                && !owner.expanded());
            input.reset();
            assert(!destroyed && owner.ordinary().use_count() == 1u);
            assert(&owner.visit([](const auto& active) noexcept -> const bool& {
                return *active->destroyed;
            }) == &destroyed);
        }
        assert(destroyed && !quota->liveBytes());
    }
    for (const auto version : {1u, 3u}) {
        bool destroyed = false, failed = false;
        try {
            if (version == 3u) {
                auto input = makeShared<Ordinary>(&destroyed);
                Owner invalid({version, 0u}, std::shared_ptr<const Ordinary>(input));
            } else {
                auto input = makeShared<Expanded>(&destroyed);
                Owner invalid({version, 0u}, std::shared_ptr<const Expanded>(input));
            }
        } catch (const VersionedSharedOwnerContractError& error) {
            failed = true;
            assert(error.diagnostic);
        }
        assert(failed && destroyed && !quota->liveBytes());
    }
    bool firstDestroyed = false, secondDestroyed = false;
    {
        auto first = makeShared<Ordinary>(&firstDestroyed),
             second = makeShared<Ordinary>(&secondDestroyed);
        Value value({1u, 71u}, Pair{first, second});
        first.reset();
        second.reset();
        assert(value.ordinary().first.use_count() == 1u && value.ordinary().second.use_count() == 1u
            && !value.expanded());
        const auto before = defaultCpuAllocator().stats().allocationCount;
        CpuMemoryQuotaLease pressure(quota, quota->limitBytes() - quota->liveBytes());
        assert(pressure.charged());
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        assert(&value.ordinary() == &value.ordinary() && value.ordinary().first.use_count() == 1u);
        assert(&value.visit([](const auto& active) noexcept -> const bool& {
            if constexpr (std::is_same_v<std::decay_t<decltype(active)>, Pair>)
                return *active.first->destroyed;
            else
                return *active->destroyed;
        }) == &firstDestroyed);
        std::pmr::set_default_resource(previous);
        assert(defaultCpuAllocator().stats().allocationCount == before);
    }
    assert(firstDestroyed && secondDestroyed && !quota->liveBytes());
    bool uniqueDestroyed = false;
    {
        Value value({3u, 71u}, makeUnique<Expanded>(&uniqueDestroyed));
        assert(value.expanded() && !value.ordinary().first);
    }
    assert(uniqueDestroyed && !quota->liveBytes());
    bool rejected = false, wrongDestroyed = false;
    try {
        Value invalid({1u, 0u}, makeUnique<Expanded>(&wrongDestroyed));
    } catch (const VersionedOwnedValueContractError&) {
        rejected = true;
    }
    assert(rejected && wrongDestroyed && !quota->liveBytes());
}

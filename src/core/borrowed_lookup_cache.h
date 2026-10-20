#pragma once

#include <array>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace arp::core {

// Fixed storage for lookups during one explicitly stable lookup relation. The
// owner retains every pointed-to object and clears the cache before rebinding.
// Keys, lookup results and object addresses must remain unchanged; pointed-to
// payloads may change through their owner. Null results are cacheable; eviction
// changes only hit rate. No allocation/ownership or cached payload copy.
template <class Key, class Value, std::size_t Capacity> class BorrowedLookupCache final {
    static_assert(std::is_trivially_copyable_v<Key>);

    struct Entry {
        Key key;
        const Value* value;
    };

public:
    static constexpr std::size_t entryBytes = sizeof(Entry);

    void clear() noexcept { count_ = next_ = 0u; }

    bool find(const Key& key, const Value*& value) const
        noexcept(noexcept(std::declval<const Key&>() == std::declval<const Key&>()))
    {
        for (std::size_t i = 0u; i < count_; ++i) {
            if (entries_[i].key == key) {
                value = entries_[i].value;
                return true;
            }
        }
        return false;
    }

    void insert(const Key& key, const Value* value) noexcept(
        noexcept(std::declval<const Key&>() == std::declval<const Key&>()))
    {
        if constexpr (Capacity != 0u) {
            for (std::size_t i = 0u; i < count_; ++i) {
                if (entries_[i].key == key) {
                    entries_[i].value = value;
                    return;
                }
            }
            if (count_ < Capacity)
                entries_[count_++] = {key, value};
            else {
                entries_[next_] = {key, value};
                if (++next_ == Capacity)
                    next_ = 0u;
            }
        }
    }

private:
    std::array<Entry, Capacity> entries_;
    std::size_t count_ = 0u, next_ = 0u;
};

} // namespace arp::core

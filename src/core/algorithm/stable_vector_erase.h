#pragma once

#include "core/memory/cpu_allocator.h"

#include <type_traits>
#include <utility>

namespace arp::core {

// Stable linear erase with a partial-commit exception guarantee. A throwing
// predicate must leave the current element's ownership intact. Earlier true
// results remain removed; the failed element and unvisited suffix survive.
// Predicate side effects are not rolled back. No allocation or retry occurs.
class StableVectorErase final {
public:
    template <class T, class Predicate>
    static std::size_t eraseIf(pmr::Vector<T>& values, Predicate&& predicate)
    {
        static_assert(std::is_nothrow_move_constructible_v<T>);
        static_assert(std::is_nothrow_destructible_v<T>);
        const auto size = values.size();
        std::size_t read = 0u;
        std::size_t write = 0u;
        try {
            for (; read < size; ++read) {
                if (predicate(values[read]))
                    continue;
                if (write != read)
                    replaceCpuObjectByMove(values[write], values[read]);
                ++write;
            }
        } catch (...) {
            for (; read < size; ++read, ++write) {
                if (write != read)
                    replaceCpuObjectByMove(values[write], values[read]);
            }
            while (values.size() > write)
                values.pop_back();
            throw;
        }
        while (values.size() > write)
            values.pop_back();
        return size - write;
    }
};

} // namespace arp::core

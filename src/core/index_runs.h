#pragma once

#include <cstddef>

namespace arp::core {

struct IndexRun {
    std::size_t first = 0;
    std::size_t count = 0;
};

// Emit maximal contiguous selected runs without reordering or merging holes.
// Allocation and capacity are owned by the caller's sink.
template <class Predicate, class Sink>
void forEachSelectedIndexRun(std::size_t count, Predicate&& selected, Sink&& sink)
{
    std::size_t first = 0, length = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (selected(i)) {
            if (length == 0)
                first = i;
            ++length;
        } else if (length != 0) {
            sink(IndexRun{first, length});
            length = 0;
        }
    }
    if (length != 0)
        sink(IndexRun{first, length});
}

} // namespace arp::core

#pragma once

#include "core/diagnostics/diagnostic_error.h"

#include <cstddef>
#include <limits>

namespace arp::core {

struct IndexRun {
    std::size_t first = 0;
    std::size_t count = 0;
};

// Validated, non-owning ordered subranges of one parent interval. Construct
// before recording any command; malformed plans cannot partially execute.
class CheckedIndexRunSpan final {
    const IndexRun* data_ = nullptr;
    std::size_t count_ = 0;

public:
    CheckedIndexRunSpan(const IndexRun* stream,
        std::size_t streamCount,
        IndexRun selection,
        IndexRun parent,
        std::size_t maximum = std::numeric_limits<std::size_t>::max())
    {
        if ((streamCount && !stream) || selection.first > streamCount
            || selection.count > streamCount - selection.first || parent.first > maximum
            || parent.count > maximum - parent.first)
            throw DiagnosticError("INDEX-RUN-SPAN-INVALID");
        auto previousEnd = parent.first;
        const auto parentEnd = parent.first + parent.count;
        for (std::size_t i = 0; i < selection.count; ++i) {
            const auto& run = stream[selection.first + i];
            if (!run.count || run.first < previousEnd || run.first > parentEnd
                || run.count > parentEnd - run.first)
                throw DiagnosticError("INDEX-RUN-SPAN-RANGE-INVALID");
            previousEnd = run.first + run.count;
        }
        data_ = stream ? stream + selection.first : nullptr;
        count_ = selection.count;
    }

    bool empty() const { return count_ == 0; }

    const IndexRun* begin() const { return data_; }

    const IndexRun* end() const { return count_ ? data_ + count_ : data_; }
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

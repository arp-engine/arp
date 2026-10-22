#pragma once

#include "scriptvm/runtime/execution_slot.h"

#include <array>
#include <utility>

namespace arp::scriptvm {

// A bounded Value ABI view. Its carriers live until the enclosing call returns;
// imported references retain their exact identity. Only the requested interval
// is materialized, so inactive operand slots never allocate at a call boundary.
template <std::size_t Capacity> class ExecutionSlotValues final {
public:
    ExecutionSlotValues(const std::array<ExecutionSlot, Capacity>& slots,
        std::size_t begin,
        std::size_t count,
        bool rootsOnly = false)
        : values_(emptyValues())
    {
        assert(begin <= Capacity && count <= Capacity - begin);
        for (std::size_t index = 0; index < count; ++index) {
            const auto& slot = slots[begin + index];
            if (!rootsOnly)
                values_[index] = slot.boxed();
            else if (const auto* carrier = slot.boxedCarrier())
                values_[index] = *carrier;
        }
    }

    const Value* data() const noexcept { return values_.data(); }

    static std::array<Value, Capacity> emptyValues()
    {
        return undefinedValues(std::make_index_sequence<Capacity>{});
    }

private:
    template <std::size_t... Index>
    static std::array<Value, Capacity> undefinedValues(std::index_sequence<Index...>)
    {
        return {{(static_cast<void>(Index), Value::undefined())...}};
    }

    std::array<Value, Capacity> values_;
};

template <std::size_t Capacity>
ExecutionSlotValues<Capacity> materializeExecutionSlots(
    const std::array<ExecutionSlot, Capacity>& slots, std::size_t begin, std::size_t count)
{
    return {slots, begin, count};
}

} // namespace arp::scriptvm

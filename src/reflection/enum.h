#pragma once

// enumeration -- mirrors ae::hybrid/enum.h, adapted for arp::reflection.
// Uses a simpler PMR Map-based storage (arp does not use the original's
// enumeration_wrapper virtual pattern; it stores name->int directly).

#include "core/api.h"
#include "core/memory/cpu_allocator.h"
#include "reflection/def.h"

#include <initializer_list>
#include <utility>

namespace arp::reflection {

template <typename Enum> using enum_pair = std::pair<const char*, Enum>;

template <typename Enum> enum_pair<Enum> enum_value(const char* name, Enum value)
{
    return std::make_pair(name, value);
}

class ARP_API enumeration final {
public:
    enumeration();

    template <typename Enum>
    enumeration(NameView name, std::initializer_list<enum_pair<Enum>> values)
        : name_(detail::makeString(name)), values_(&core::defaultCpuMemoryResource())
    {
        for (const auto& item : values) {
            values_.emplace(detail::makeString(item.first), static_cast<int>(item.second));
        }
    }

    template <typename Enum>
    enumeration(NameView name, const core::pmr::Vector<enum_pair<Enum>>& values)
        : name_(detail::makeString(name)), values_(&core::defaultCpuMemoryResource())
    {
        for (const auto& item : values) {
            values_.emplace(detail::makeString(item.first), static_cast<int>(item.second));
        }
    }

    int get_enum_value(NameView name) const;
    const core::pmr::String& name() const;

    const core::pmr::Map<core::pmr::String, int>& values() const noexcept { return values_; }

private:
    core::pmr::String name_{&core::defaultCpuMemoryResource()};
    core::pmr::Map<core::pmr::String, int> values_{&core::defaultCpuMemoryResource()};
};

} // namespace arp::reflection

// enum.cpp -- enumeration non-template implementation.

#include "reflection/enum.h"

namespace arp::reflection {

enumeration::enumeration() = default;

int enumeration::get_enum_value(NameView name) const
{
    auto found = values_.find(detail::makeString(name));
    if (found == values_.end()) {
        return 0;
    }
    return found->second;
}

const core::pmr::String& enumeration::name() const
{
    return name_;
}

} // namespace arp::reflection

// instance.cpp -- mirrors ae::hybrid/instance.h, adapted for arp::reflection.

#include "reflection/instance.h"

namespace arp::reflection {

instance::instance(const class_info* cls) : cls_(cls)
{}

const class_info* instance::class_() const
{
    return cls_;
}

object instance::__getitem__(NameView key)
{
    auto found = dict_.find(detail::makeString(key));
    return found == dict_.end() ? object::undefined() : found->second;
}

object instance::__setitem__(NameView key, const object& value)
{
    dict_[detail::makeString(key)] = value;
    return object::undefined();
}

object instance::operator[](NameView key)
{
    return __getitem__(key);
}

object& instance::slot(NameView key)
{
    return dict_.try_emplace(detail::makeString(key)).first->second;
}

} // namespace arp::reflection

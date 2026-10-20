// property.cpp -- mirrors ae::hybrid property + arp indirect value extensions.

#include "reflection/property.h"

#include <utility>

namespace arp::reflection {

property::property(NameView name,
    const typeinfo* type,
    NameView doc,
    const object& getter,
    const object& setter,
    const object& pointerSetter,
    const object& indirectGetter,
    const object& indirectSetter,
    const class_info* indirectClass,
    bool indirectAllowDerived,
    bool indirectResultOwned,
    std::uint8_t hints)
    : type_(type), hints_(hints), name_(detail::makeString(name)), doc_(detail::makeString(doc)),
      getter_(getter), setter_(setter), pointerSetter_(pointerSetter),
      indirectGetter_(indirectGetter), indirectSetter_(indirectSetter),
      indirectClass_(indirectClass), indirectAllowDerived_(indirectAllowDerived),
      indirectResultOwned_(indirectResultOwned)
{}

property::property(NameView name,
    const typeinfo* type,
    NameView doc,
    const object& getter,
    const object& setter,
    std::uint8_t hints)
    : property(name,
          type,
          doc,
          getter,
          setter,
          object::undefined(),
          object::undefined(),
          object::undefined(),
          nullptr,
          false,
          false,
          hints)
{}

property::property(
    NameView name, const typeinfo* type, NameView doc, const object& getter, std::uint8_t hints)
    : property(name, type, doc, getter, object::undefined(), hints)
{}

bool property::is_readonly() const
{
    return setter_.is_undefined();
}

bool property::can_set_pointer_value() const
{
    return !pointerSetter_.is_undefined();
}

bool property::has_indirect_value() const
{
    return !indirectGetter_.is_undefined() && indirectClass_ != nullptr;
}

bool property::indirect_allows_derived() const
{
    return indirectAllowDerived_;
}

bool property::indirect_uses_result_ownership() const
{
    return indirectResultOwned_;
}

object property::get_value(const object& inst) const
{
    return getter_.cast<function&>().call(inst);
}

object property::set_value(const object& inst, const object& value) const
{
    if (is_readonly()) {
        return object::undefined();
    }
    return setter_.cast<function&>().call(inst, value);
}

object property::set_pointer_value(const object& inst, void* value) const
{
    if (pointerSetter_.is_undefined()) {
        return object::undefined();
    }
    return pointerSetter_.cast<function&>().call(inst, value);
}

object property::get_indirect_value(const object& inst) const
{
    if (indirectGetter_.is_undefined()) {
        return object::undefined();
    }
    return indirectGetter_.cast<function&>().call(inst);
}

object property::set_indirect_value(const object& inst, const object& value) const
{
    if (indirectSetter_.is_undefined()) {
        return object::undefined();
    }
    return indirectSetter_.cast<function&>().call(inst, value);
}

void property::set_hints(std::uint8_t value)
{
    hints_ |= value;
}

bool property::check_hints(std::uint8_t value) const
{
    return (hints_ & value) == value;
}

const typeinfo* property::type() const
{
    return type_;
}

const class_info* property::indirect_class() const
{
    return indirectClass_;
}

const core::pmr::String& property::name() const
{
    return name_;
}

const core::pmr::String& property::doc() const
{
    return doc_;
}

} // namespace arp::reflection

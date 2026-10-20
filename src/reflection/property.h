#pragma once

// property -- mirrors ae::hybrid/property.h + arp indirect value extensions.
//
// The original property has: type_, hints_, name_, doc_, getter_, setter_.
// arp adds: pointerSetter_, indirectGetter_, indirectSetter_, indirectClass_,
// indirectAllowDerived_, indirectResultOwned_ -- to support pointer/smart-ptr
// properties (indirectValueTraits).

#include "core/api.h"
#include "core/cpu_allocator.h"
#include "reflection/def.h"
#include "reflection/function.h"
#include "reflection/object.h"

#include <cstdint>

namespace arp::reflection {

class typeinfo;

enum class PropertyHint : std::uint8_t {
    None = 0,
    Editor = 1u << 0,
    Serialize = 1u << 1,
    Runtime = 1u << 2,
};

constexpr PropertyHint operator|(PropertyHint lhs, PropertyHint rhs) noexcept
{
    return static_cast<PropertyHint>(
        static_cast<std::uint8_t>(lhs) | static_cast<std::uint8_t>(rhs));
}

class ARP_API property final {
public:
    property(NameView name,
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
        std::uint8_t hints);
    property(NameView name,
        const typeinfo* type,
        NameView doc,
        const object& getter,
        const object& setter,
        std::uint8_t hints);
    property(NameView name,
        const typeinfo* type,
        NameView doc,
        const object& getter,
        std::uint8_t hints);

    bool is_readonly() const;
    bool can_set_pointer_value() const;
    bool has_indirect_value() const;
    bool indirect_allows_derived() const;
    bool indirect_uses_result_ownership() const;
    object get_value(const object& inst) const;
    object set_value(const object& inst, const object& value) const;
    object set_pointer_value(const object& inst, void* value) const;
    object get_indirect_value(const object& inst) const;
    object set_indirect_value(const object& inst, const object& value) const;
    void set_hints(std::uint8_t value);
    bool check_hints(std::uint8_t value) const;
    const typeinfo* type() const;
    const class_info* indirect_class() const;
    const core::pmr::String& name() const;
    const core::pmr::String& doc() const;

private:
    const typeinfo* type_ = nullptr;
    std::uint8_t hints_ = 0;
    core::pmr::String name_{&core::defaultCpuMemoryResource()};
    core::pmr::String doc_{&core::defaultCpuMemoryResource()};
    object getter_;
    object setter_;
    // arp extensions for indirect (pointer/smart-ptr) properties.
    object pointerSetter_;
    object indirectGetter_;
    object indirectSetter_;
    const class_info* indirectClass_ = nullptr;
    bool indirectAllowDerived_ = false;
    bool indirectResultOwned_ = false;
};

} // namespace arp::reflection

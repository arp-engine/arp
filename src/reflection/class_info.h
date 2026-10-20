#pragma once

// class_info -- mirrors ae::hybrid/class.h (renamed from class.h because
// 'class' is a C++ keyword), adapted for arp::reflection.
//
// Uses PMR containers (core::pmr::Vector/Map) instead of std::vector/map.

#include "core/api.h"
#include "core/cpu_allocator.h"
#include "reflection/def.h"
#define ARP_REFLECTION_CLASS_INFO_HEADER_ACTIVE 1
#include "reflection/object.h"
#undef ARP_REFLECTION_CLASS_INFO_HEADER_ACTIVE

#include <cstddef>

namespace arp::reflection {

class typeinfo;
class instance;
class RegistrationTransaction;

class ARP_API class_info final {
public:
    explicit class_info(NameView name);
    class_info(NameView name, value_t type, std::size_t size, std::size_t alignment);
    class_info(NameView name,
        value_t type,
        std::size_t size,
        std::size_t alignment,
        get_derived_info_func getDerivedInfo);

    bool is_base_of(const class_info* other) const;
    bool is_derived_from(const class_info* other) const;
    bool is_type(value_t type) const;
    void* up_cast(const class_info* dst, void* ptr) const;
    void* dynamic_cast_(const class_info* dst, void* ptr) const;

    class_info& def(NameView name, const object& value);
    class_info& def_property(NameView name,
        const typeinfo* type,
        const object& getter,
        const object& setter,
        const object& pointerSetter,
        const object& indirectGetter,
        const object& indirectSetter,
        const class_info* indirectClass,
        bool indirectAllowDerived,
        bool indirectResultOwned,
        NameView doc,
        std::uint8_t hints);
    class_info& def_property(NameView name,
        const typeinfo* type,
        const object& getter,
        const object& setter,
        NameView doc,
        std::uint8_t hints);
    class_info& inherit(class_info* cls, cast_func func);

    object get_field(NameView name) const;
    core::pmr::Map<core::pmr::String, object> get_all_fields() const;
    core::pmr::Map<core::pmr::String, object> get_all_properties() const;
    object get_attr(const object& inst, NameView name) const;
    object set_attr(const object& inst, NameView name, const object& value) const;

    template <typename... Args>
    object call(const object& inst, NameView func, const Args&... args) const;

    object call(const object& inst, NameView func, core::pmr::Vector<object>& args) const;

    template <typename... Args> object new_object(const Args&... args) const;

    bool malloc(void*& ptr) const;
    void free(void* ptr) const;

    template <typename... Args> void construct(void* ptr, const Args&... args) const;

    void destruct(void* ptr) const;
    object operator[](NameView name) const;
    const core::pmr::String& name() const;
    void set_name(NameView name);
    value_t valueType() const;
    std::size_t size() const;
    std::size_t alignment() const;

    const core::pmr::Vector<class_info*>& bases() const noexcept { return bases_; }

    // Invalidated by method replacement/inheritance together with object ordering.
    bool hasBuiltinOrdering() const noexcept { return builtinOrdering_; }

private:
    friend class object;
    friend class db;
    friend class RegistrationTransaction;
    friend class typeinfo;
    friend void registerBuiltinReflectionTypes();

    // Installed only with stock builtin methods. Redefinition invalidates the
    // corresponding direct path, preserving dynamic reflection overrides.
    void invalidateBuiltinComparisons(NameView name);
    bool builtinEquality_ = false;
    bool builtinOrdering_ = false;

    value_t type_ = others_t;
    std::size_t size_ = 0;
    std::size_t alignment_ = 0;
    object new_;
    object init_;
    object del_;
    object getitem_;
    object setitem_;
    core::pmr::String name_{&core::defaultCpuMemoryResource()};
    core::pmr::String doc_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<class_info*> bases_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<cast_func> castFuncs_{&core::defaultCpuMemoryResource()};
    get_derived_info_func getDerivedInfo_ = nullptr;
    core::pmr::Map<core::pmr::String, object> dict_{&core::defaultCpuMemoryResource()};
};

} // namespace arp::reflection

#include "reflection/internal/class_impl.h"
#include "reflection/internal/object_impl.h"

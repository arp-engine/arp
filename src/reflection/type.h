#pragma once

// typeinfo -- mirrors ae::hybrid/type.h, adapted for arp::reflection.

#include "core/api.h"
#include "reflection/def.h"

#include <cstddef>
#include <cstdint>

namespace arp::reflection {

class class_info;

// Container access is registered from the actual C++ property type. It owns
// no domain state and lets the archive use the same traversal for typed values.
struct CollectionAccess {
    bool mapping = false;
    bool optional = false;
    object (*make)() = nullptr;
    const typeinfo* (*elementType)() = nullptr;
    const typeinfo* (*keyType)() = nullptr;
    std::size_t (*size)(const object&) = nullptr;
    object (*element)(const object&, std::size_t) = nullptr;
    object (*key)(const object&, std::size_t) = nullptr;
    bool (*insert)(object&, const object&, const object&) = nullptr;
};

class ARP_API typeinfo final {
public:
    typeinfo(const class_info* cls, std::uint64_t mask);

    bool is_array() const;
    bool is_class() const;
    bool is_enum() const;
    bool is_pointer() const;
    bool is_function_pointer() const;
    bool is_member_object_pointer() const;
    bool is_member_function_pointer() const;
    bool is_lvalue_reference() const;
    bool is_rvalue_reference() const;
    bool is_reference() const;
    bool is_sequential_container() const;
    bool is_map_container() const;
    bool is_wrapper() const;
    bool is_type(type_trait_mask mask) const;

    const class_info* class_() const { return cls_; }

    std::size_t sizeof_() const;
    std::size_t alignof_() const;

    template <typename T> bool is() const { return type_of<T>() == this; }

    bool operator==(const typeinfo& rhs) const;

    const CollectionAccess* collection() const { return collection_; }

    void setCollection(const CollectionAccess* value) const;
    void setEnumValues(const core::pmr::Map<core::pmr::String, int>& values) const;
    bool containsEnumValue(int value) const;

private:
    friend class db;
    friend class RegistrationTransaction;
    type_mask mask_;
    const class_info* cls_ = nullptr;
    mutable const CollectionAccess* collection_ = nullptr;
    mutable core::pmr::Vector<int> enumValues_{&core::defaultCpuMemoryResource()};
};

} // namespace arp::reflection

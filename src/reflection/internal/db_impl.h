#pragma once

// db_impl -- mirrors ae::hybrid/internal/db_impl.h, adapted for arp::reflection.
//
// Contains:
//  - upcast_impl<Dest, Source>
//  - get_derived_info_impl<T> (hasDerivedInfo SFINAE)
//  - is_wrapper / is_sequential_container / is_map_container traits
//  - class_of<T>() / class_of_instance<T*>() / type_of<T>() implementations
//
// arp uses the generalized isWrapper/isSequentialContainer/isMapContainer
// from def.h (which support indirectValueTraits for smart pointers).
// The original used separate traits here; arp consolidates them in def.h
// and db_impl.h just references those.

#include "core/api.h"
#include "reflection/db.h"
#include "reflection/def.h"
#include "reflection/internal/type_name.h"
#include "reflection/registration_transaction.h"

#include <cstdint>
#include <type_traits>

namespace arp::reflection {

namespace detail {

// get_derived_info_impl: if T has get_derived_info(), call it; otherwise
// return { nullptr, class_of<T>() }. The shared trait and upcast helper are
// declared in def.h so this module does not define them twice.
template <typename T>
std::enable_if_t<hasDerivedInfo<T>::value, derived_info> getDerivedInfoImpl(void* ptr)
{
    return reinterpret_cast<raw_type_t<T>*>(ptr)->get_derived_info();
}

template <typename T>
std::enable_if_t<!hasDerivedInfo<T>::value, derived_info> getDerivedInfoImpl(void*)
{
    return {nullptr, class_of<T>()};
}

} // namespace detail

// ---------------------------------------------------------------------------
// class_of<T>(): find or register class_info for T.
// ---------------------------------------------------------------------------
template <typename T> std::enable_if_t<!std::is_void_v<T>, class_info*> class_of_raw()
{
    using Raw = detail::raw_type_t<T>;
    // Raw has no cv/reference qualifiers. The canonical type spelling is
    // already a static view; lookup must not rebuild an owned PMR string.
    const auto name = detail::getTypeName<Raw>();
    auto* transaction = detail::currentRegistrationTransaction();
    if (transaction != nullptr) {
        auto* cls = transaction->findStagedClass(name);
        if (cls == nullptr)
            cls = db::rawInstance().findNativeClass(name);
        return cls != nullptr ? cls
                              : transaction->registerReferencedClass(name,
                                    value_type_v<Raw>,
                                    sizeof(Raw),
                                    alignof(Raw),
                                    detail::getDerivedInfoImpl<Raw>);
    }
    // Published classes, like typeinfo, have database/process lifetime. Never
    // populate this cache with staged metadata that a transaction can discard.
    static class_info* published = [] {
        const auto key = detail::getTypeName<Raw>();
        auto* cls = db::rawInstance().findNativeClass(key);
        return cls != nullptr ? cls
                              : db::rawInstance().register_class(key,
                                    value_type_v<Raw>,
                                    sizeof(Raw),
                                    alignof(Raw),
                                    detail::getDerivedInfoImpl<Raw>);
    }();
    return published;
}

namespace detail {

template <typename T> class_info* registrationClassOf()
{
    using Raw = raw_type_t<T>;
    (void)db::instance();
    auto* transaction = currentRegistrationTransaction();
    if (transaction == nullptr) {
        auto* cls = class_of_raw<T>();
        db::rawInstance().markExplicitClassDeclaration(cls);
        return cls;
    }
    const auto name = typeName<Raw>();
    return transaction->declareClass(
        name, value_type_v<Raw>, sizeof(Raw), alignof(Raw), getDerivedInfoImpl<Raw>);
}

} // namespace detail

template <typename T> std::enable_if_t<std::is_void_v<T>, class_info*> class_of_raw()
{
    auto name = detail::typeName<void>();
    class_info* cls = db::rawInstance().findNativeClass(name);
    if (!cls) {
        cls = db::rawInstance().register_class(name, undefined_t, 0, 0, nullptr);
    }
    return cls;
}

template <typename T> std::enable_if_t<!std::is_void_v<T>, class_info*> class_of()
{
    // Public class registration keeps the historical builtin bootstrap, while
    // type_of<T>() can use class_of_raw<T>() during that bootstrap without
    // recursively entering it.
    (void)db::instance();
    return class_of_raw<T>();
}

template <typename T> std::enable_if_t<std::is_void_v<T>, class_info*> class_of()
{
    (void)db::instance();
    return class_of_raw<T>();
}

template <typename T> const class_info* class_of_instance(T*)
{
    return class_of<T>();
}

// ---------------------------------------------------------------------------
// type_of<T>(): find or register typeinfo for T.
// Builds the trait bitmask from std::type_traits + arp's generalized traits.
// ---------------------------------------------------------------------------
template <typename T> const typeinfo* type_of()
{
    if (auto* transaction = detail::currentRegistrationTransaction()) {
        auto name = detail::typeName<T>();
        if (const auto* staged = transaction->findStagedType(name))
            return staged;
        if (const auto* existing = db::rawInstance().find_type(name))
            return existing;
        using Raw = detail::raw_type_t<T>;
        std::uint64_t mask = 0;
        auto add = [&mask](type_trait_mask bit, bool enabled) {
            if (enabled)
                mask |= std::uint64_t{1} << static_cast<std::size_t>(bit);
        };
        add(type_trait_mask::is_class, std::is_class_v<Raw>);
        add(type_trait_mask::is_enum, std::is_enum_v<Raw>);
        add(type_trait_mask::is_array, std::is_array_v<std::remove_reference_t<T>>);
        add(type_trait_mask::is_pointer, std::is_pointer_v<std::remove_reference_t<T>>);
        add(type_trait_mask::is_arithmetic, std::is_arithmetic_v<Raw>);
        add(type_trait_mask::is_function_pointer,
            detail::is_function_pointer_v<std::remove_reference_t<T>>);
        add(type_trait_mask::is_member_object_pointer,
            std::is_member_object_pointer_v<std::remove_reference_t<T>>);
        add(type_trait_mask::is_member_function_pointer,
            std::is_member_function_pointer_v<std::remove_reference_t<T>>);
        add(type_trait_mask::is_lvalue_reference, std::is_lvalue_reference_v<T>);
        add(type_trait_mask::is_rvalue_reference, std::is_rvalue_reference_v<T>);
        add(type_trait_mask::is_reference, std::is_reference_v<T>);
        add(type_trait_mask::is_wrapper, detail::isWrapper<Raw>::value);
        add(type_trait_mask::is_map_container, detail::isMapContainer<Raw>::value);
        add(type_trait_mask::is_sequential_container, detail::isSequentialContainer<Raw>::value);
        return transaction->registerType(name, class_of_raw<Raw>(), mask);
    }
    // Type identity is stable for the lifetime of the reflection database.
    // Cache the registered pointer per C++ type so failed RTTI probes in
    // variant::is<T>() do not rebuild a PMR type name on every VM operation.
    static const typeinfo* info = [] {
        auto name = detail::typeName<T>();
        const typeinfo* result = db::rawInstance().find_type(name);
        if (result)
            return result;

        using Raw = detail::raw_type_t<T>;
        std::uint64_t mask = 0;
        auto add = [&mask](type_trait_mask bit, bool enabled) {
            if (enabled) {
                mask |= std::uint64_t{1} << static_cast<std::size_t>(bit);
            }
        };
        add(type_trait_mask::is_class, std::is_class_v<Raw>);
        add(type_trait_mask::is_enum, std::is_enum_v<Raw>);
        add(type_trait_mask::is_array, std::is_array_v<std::remove_reference_t<T>>);
        add(type_trait_mask::is_pointer, std::is_pointer_v<std::remove_reference_t<T>>);
        add(type_trait_mask::is_arithmetic, std::is_arithmetic_v<Raw>);
        add(type_trait_mask::is_function_pointer,
            detail::is_function_pointer_v<std::remove_reference_t<T>>);
        add(type_trait_mask::is_member_object_pointer,
            std::is_member_object_pointer_v<std::remove_reference_t<T>>);
        add(type_trait_mask::is_member_function_pointer,
            std::is_member_function_pointer_v<std::remove_reference_t<T>>);
        add(type_trait_mask::is_lvalue_reference, std::is_lvalue_reference_v<T>);
        add(type_trait_mask::is_rvalue_reference, std::is_rvalue_reference_v<T>);
        add(type_trait_mask::is_reference, std::is_reference_v<T>);
        add(type_trait_mask::is_wrapper, detail::isWrapper<Raw>::value);
        add(type_trait_mask::is_map_container, detail::isMapContainer<Raw>::value);
        add(type_trait_mask::is_sequential_container, detail::isSequentialContainer<Raw>::value);
        return db::rawInstance().register_type(name, class_of_raw<Raw>(), mask);
    }();
    return info;
}

} // namespace arp::reflection

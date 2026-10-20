#pragma once

// object template implementation.
//
// Key differences from the original:
//  - std::make_shared -> core::makeShared (PMR constraint)
//  - alog_fatal -> throw std::bad_cast (scriptvm tryGet* depends on this)
//  - can_safe_cast: no alogf_debug, just return false
//  - ptr<T>(): no void special-case (original doesn't have one)
//  - object specializations for is<>/cast<> retained

#include "core/api.h"
#include "core/cpu_allocator.h"
#include "reflection/class_info.h"
#include "reflection/db.h"
#include "reflection/object.h"

#include <type_traits>
#include <utility>

namespace arp::reflection {

// ---------------------------------------------------------------------------
// Template constructor: wrap any value in a shared variant.
// ---------------------------------------------------------------------------
template <typename T,
    std::enable_if_t<!std::is_same_v<std::remove_cv_t<std::remove_reference_t<T>>, object>
            && !detail::isValueCarrier<std::remove_cv_t<std::remove_reference_t<T>>>::value
            && !std::is_same_v<std::remove_cv_t<std::remove_reference_t<T>>, variant>
            && !std::is_same_v<std::remove_cv_t<std::remove_reference_t<T>>,
                std::pmr::memory_resource*>,
        int>>
object::object(T&& value) : value_(detail::ObjectRef<variant>::make(std::forward<T>(value)))
{}

template <typename T> object object::native_reference(T* pointer)
{
    static_assert(
        !std::is_const_v<T>, "native reflection references require mutable engine objects");
    if (pointer == nullptr) {
        return object::undefined();
    }
    return object(detail::NativeObjectReference::make(
        static_cast<void*>(pointer), class_of_instance(pointer), type_of<T*>()));
}

// ---------------------------------------------------------------------------
// call: pack args into a Vector<object> and dispatch to class_info::call.
// ---------------------------------------------------------------------------
template <typename... Args> object object::call(NameView name, const Args&... args) const
{
    auto argv = core::pmr::Vector<object>(
        core::pmr::Vector<object>::allocator_type(&core::defaultCpuMemoryResource()));
    argv.reserve(sizeof...(Args));
    (argv.emplace_back(object(args)), ...);
    return class_()->call(*this, name, argv);
}

// ---------------------------------------------------------------------------
// is<T>: delegate to variant::is<T>().
// ---------------------------------------------------------------------------
template <typename T> constexpr bool object::is() const
{
    return value_->template is<T>();
}

// ---------------------------------------------------------------------------
// can_safe_cast: check if a cast<T> would succeed.
// ---------------------------------------------------------------------------
template <typename T> bool object::can_safe_cast() const
{
    using Raw = detail::raw_type_t<T>;
    if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<T>>, object>) {
        return true;
    }
    if (auto* control = native_control()) {
        if constexpr (std::is_pointer_v<T> || std::is_reference_v<T>) {
            return control->pointerFor(class_of<Raw>()) != nullptr;
        } else {
            return false;
        }
    }
    if constexpr (std::is_enum_v<Raw>) {
        return is<T>() || type() == integer_t;
    } else if constexpr (std::is_arithmetic_v<Raw> && !std::is_pointer_v<T>) {
        // Script numeric literals are represented as doubles while reflected
        // engine APIs commonly use float/int/bool parameters. Treat the
        // numeric family as a checked conversion domain instead of requiring
        // exact C++ storage types at every generated call site.
        return type() == boolean_t || type() == integer_t || type() == float_t;
    } else if (is<T>() || is<Raw>()) {
        return true;
    } else if constexpr (std::is_pointer_v<T> || std::is_reference_v<T>) {
        return is_base_of(class_of<Raw>()) || is_derived_from(class_of<Raw>());
    } else {
        return false;
    }
}

inline bool object::tryGetEnumInt(int& value) const noexcept
{
    return value_->tryGetEnumInt(value);
}

// ---------------------------------------------------------------------------
// ptr<T>: get a mutable pointer to the stored value.
// No void special-case (original doesn't have one).
// ---------------------------------------------------------------------------
template <typename T> T* object::ptr() const
{
    if constexpr (!std::is_pointer_v<T>) {
        if (auto* control = native_control()) {
            using Raw = std::remove_cv_t<T>;
            return static_cast<T*>(control->pointerFor(class_of<Raw>()));
        }
    }
    return const_cast<T*>(value_->template cast<T>());
}

// ---------------------------------------------------------------------------
// cast<T&>: reference cast. Tries direct, then pointer, then caster.
// Throws std::bad_cast on failure (arp decision; original uses alog_fatal).
// ---------------------------------------------------------------------------
template <typename T> std::enable_if_t<std::is_reference_v<T>, T> object::cast() const
{
    using Raw = std::remove_reference_t<T>;
    using RawNoCv = std::remove_cv_t<Raw>;
    if constexpr (std::is_same_v<RawNoCv, object>) {
        return *const_cast<object*>(this);
    } else if constexpr (std::is_enum_v<RawNoCv>) {
        if (is<RawNoCv>())
            return *ptr<RawNoCv>();
        // Reflected setters conventionally take const Enum&. Keep the
        // converted value alive through the immediate invocation without
        // manufacturing a dangling reference to a temporary object.
        if (is<int>()) {
            static thread_local RawNoCv converted;
            converted = static_cast<RawNoCv>(*ptr<int>());
            return converted;
        }
        if (is<std::int64_t>()) {
            static thread_local RawNoCv converted;
            converted = static_cast<RawNoCv>(*ptr<std::int64_t>());
            return converted;
        }
        throw std::bad_cast();
    } else if constexpr (std::is_arithmetic_v<RawNoCv>) {
        if (is<RawNoCv>())
            return *ptr<RawNoCv>();
        static thread_local RawNoCv converted;
        if (is<bool>())
            converted = static_cast<RawNoCv>(*ptr<bool>());
        if (is<int>())
            converted = static_cast<RawNoCv>(*ptr<int>());
        if (is<std::int64_t>())
            converted = static_cast<RawNoCv>(*ptr<std::int64_t>());
        if (is<float>())
            converted = static_cast<RawNoCv>(*ptr<float>());
        if (is<double>())
            converted = static_cast<RawNoCv>(*ptr<double>());
        if (is<bool>() || is<int>() || is<std::int64_t>() || is<float>() || is<double>())
            return converted;
        object magicConverted = detail::caster<RawNoCv>::from(*this);
        if (magicConverted.template is<RawNoCv>()) {
            converted = magicConverted.template cast<RawNoCv>();
            return converted;
        }
        throw std::bad_cast();
    } else if (native_control() != nullptr) {
        auto* pointer = ptr<RawNoCv>();
        if (pointer != nullptr) {
            return *pointer;
        }
        throw std::bad_cast();
    } else if (is<Raw>()) {
        return *ptr<Raw>();
    }
    if (is<Raw*>()) {
        return *(*ptr<Raw*>());
    }
    object converted = detail::caster<Raw*>::from(*this);
    if (converted.template is<Raw*>()) {
        return *converted.template cast<Raw*>();
    }
    throw std::bad_cast();
}

// ---------------------------------------------------------------------------
// cast<T>: value cast. Tries direct, then caster.
// Throws std::bad_cast on failure.
// ---------------------------------------------------------------------------
template <typename T> std::enable_if_t<!std::is_reference_v<T>, T> object::cast() const
{
    using Raw = detail::raw_type_t<T>;
    if constexpr (std::is_same_v<std::remove_cv_t<T>, object>) {
        return *this;
    } else if constexpr (std::is_enum_v<Raw> && !std::is_pointer_v<T>) {
        if (is<T>())
            return *ptr<T>();
        if (is<int>())
            return static_cast<T>(*ptr<int>());
        if (is<std::int64_t>())
            return static_cast<T>(*ptr<std::int64_t>());
        throw std::bad_cast();
    } else if constexpr (std::is_arithmetic_v<Raw> && !std::is_pointer_v<T>) {
        if (is<Raw>())
            return static_cast<T>(*ptr<Raw>());
        if (is<bool>())
            return static_cast<T>(*ptr<bool>());
        if (is<int>())
            return static_cast<T>(*ptr<int>());
        if (is<std::int64_t>())
            return static_cast<T>(*ptr<std::int64_t>());
        if (is<float>())
            return static_cast<T>(*ptr<float>());
        if (is<double>())
            return static_cast<T>(*ptr<double>());
        object magicConverted = detail::caster<T>::from(*this);
        if (magicConverted.template is<T>())
            return magicConverted.template cast<T>();
        throw std::bad_cast();
    } else if (auto* control = native_control()) {
        if constexpr (std::is_pointer_v<T>) {
            return static_cast<T>(control->pointerFor(class_of<Raw>()));
        }
        (void)control;
        throw std::bad_cast();
    } else if (is<T>()) {
        return *ptr<T>();
    } else {
        if constexpr (std::is_pointer_v<T> && std::is_same_v<T, Raw*>) {
            // The existing caster boxes this same address and immediately
            // unboxes it. Exact owned payloads need no temporary carrier;
            // native, derived and qualified-pointer conversions stay below.
            if (is<Raw>())
                return ptr<Raw>();
        }
        object converted = detail::caster<T>::from(*this);
        if (converted.template is<T>()) {
            return converted.template cast<T>();
        }
        throw std::bad_cast();
    }
}

// ---------------------------------------------------------------------------
// Specializations for object itself.
// ---------------------------------------------------------------------------
template <> constexpr bool object::is<object>() const
{
    return true;
}

template <> constexpr bool object::is<object&>() const
{
    return true;
}

template <> constexpr bool object::is<const object&>() const
{
    return true;
}

template <> constexpr bool object::is<detail::ObjectRef<variant>&>() const
{
    return true;
}

template <> inline object object::cast<object>() const
{
    return *this;
}

template <> inline object& object::cast<object&>() const
{
    return *const_cast<object*>(this);
}

template <> inline const object& object::cast<const object&>() const
{
    return *this;
}

template <> inline detail::ObjectRef<variant>& object::cast<detail::ObjectRef<variant>&>() const
{
    return *const_cast<detail::ObjectRef<variant>*>(&value_);
}

template <>
inline const detail::ObjectRef<variant>& object::cast<const detail::ObjectRef<variant>&>() const
{
    return value_;
}

} // namespace arp::reflection

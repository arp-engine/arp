#pragma once

// class_info template implementation + caster -- mirrors ae::hybrid/internal/class_impl.h.
//
// caster<T> provides from(object) for implicit conversion when direct cast fails.
// The original uses alog_fatal for failure; arp uses return + throw std::bad_cast
// in the cast<T> path (handled in object_impl.h).

#include "core/api.h"
#include "core/memory/cpu_allocator.h"
#include "reflection/db.h"
#include "reflection/def.h"
#include "reflection/function.h"
#include "reflection/internal/type_name.h"

#include <cstring>
#include <utility>

namespace arp::reflection {

// ---------------------------------------------------------------------------
// Template methods: call, new_object, construct.
// ---------------------------------------------------------------------------
template <typename... Args>
object class_info::call(const object& inst, NameView func, const Args&... args) const
{
    auto argv = core::pmr::Vector<object>(
        core::pmr::Vector<object>::allocator_type(&core::defaultCpuMemoryResource()));
    argv.reserve(sizeof...(Args));
    (argv.emplace_back(object(args)), ...);
    return call(inst, func, argv);
}

template <typename... Args> object class_info::new_object(const Args&... args) const
{
    if (new_.is_function()) {
        return new_.cast<function&>().call(args...);
    }
    return object::undefined();
}

template <typename... Args> void class_info::construct(void* ptr, const Args&... args) const
{
    if (init_.is_function()) {
        init_.cast<function&>().call(ptr, args...);
    }
}

namespace detail {

// ---------------------------------------------------------------------------
// caster<T>: implicit conversion from object to T.
// Tries T* first, then __Tname__ converter method, then new_object.
// ---------------------------------------------------------------------------
template <typename T> struct caster {
    static object from(const object& value)
    {
        if constexpr (!std::is_pointer_v<T>) {
            if (value.template is<T*>()) {
                T* ptr = *value.template ptr<T*>();
                return ptr != nullptr ? object(*ptr) : object::undefined();
            }
            core::pmr::String conversionName("__", &core::defaultCpuMemoryResource());
            conversionName += class_of<T>()->name();
            conversionName += "__";
            object converter = value.class_()->get_field(conversionName);
            if (converter.is_function()) {
                object converted = converter.template cast<function&>().call(value);
                if (!converted.is_undefined()) {
                    return converted;
                }
            }
            return class_of<T>()->new_object(value);
        } else {
            using Raw = raw_type_t<T>;
            if (value.template is<Raw>()) {
                return object(value.template ptr<Raw>());
            }
            void* sourcePtr = nullptr;
            if (value.typeinfo_()->is_pointer()) {
                std::memcpy(&sourcePtr, value.template ptr<void>(), sizeof(sourcePtr));
            } else {
                sourcePtr = value.template ptr<void>();
            }
            void* ptr = value.class_()->dynamic_cast_(class_of<Raw>(), sourcePtr);
            return ptr != nullptr ? object(static_cast<T>(ptr)) : object::undefined();
        }
    }
};

template <> struct caster<undefined_> {
    static object from(const object&) { return object::undefined(); }
};

template <> struct caster<null_> {
    static object from(const object&) { return object::undefined(); }
};

} // namespace detail
} // namespace arp::reflection

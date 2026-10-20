#pragma once

// function_invoker + method_accessor -- mirrors ae::hybrid/internal/function_invoker.h.
//
// The original design uses 4 invoker specializations based on return policy:
//   - return_member_func: non-void member function pointer
//   - void_member_func:   void member function pointer
//   - return_func:        non-void free function / callable
//   - void_func:           void free function / callable
//
// Each specialization has a static invoke() that unpacks object args via
// can_safe_cast + cast. The acheck(0) assertions are replaced with
// return object::undefined() (arp decision: no fatal abort in production).

#include "core/api.h"
#include "reflection/def.h"
#include "reflection/object.h"

#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>

namespace arp::reflection::detail {

struct return_member_func {};

struct void_member_func {};

struct return_func {};

struct void_func {};

template <typename F, typename Policy, typename Index> struct function_invoker;

template <typename T, typename = void> struct has_native_reflection_object : std::false_type {};

template <typename T>
struct has_native_reflection_object<T,
    std::void_t<decltype(std::declval<const T*>()->nativeReflectionObject())>> : std::true_type {};

// Native engine objects must remain references when returned through a
// reflected method. Boxing a class pointer as a regular variant would retain
// a raw address and bypass the shared invalidation control.
template <typename T> object box_function_return(T&& value)
{
    using Value = std::remove_reference_t<T>;
    using Raw = std::remove_cv_t<Value>;
    if constexpr (std::is_pointer_v<Raw>
        && std::is_class_v<std::remove_cv_t<std::remove_pointer_t<Raw>>>) {
        using Pointee = std::remove_cv_t<std::remove_pointer_t<Raw>>;
        if (value == nullptr) {
            return object::undefined();
        }
        if constexpr (has_native_reflection_object<Pointee>::value) {
            return value->nativeReflectionObject();
        } else {
            // A reflected class pointer without a canonical engine object is
            // not a safe script value. Refuse it instead of manufacturing a
            // second invalidation control around a raw address.
            return object::undefined();
        }
    } else if constexpr (std::is_lvalue_reference_v<T>
        && std::is_class_v<std::remove_cv_t<Value>>) {
        using Pointee = std::remove_cv_t<Value>;
        if constexpr (has_native_reflection_object<Pointee>::value) {
            return value.nativeReflectionObject();
        } else {
            // Ordinary const/container properties are values, not borrowed
            // engine objects. Copy them instead of manufacturing a mutable
            // native reference to a const object.
            return object(std::forward<T>(value));
        }
    } else {
        return object(std::forward<T>(value));
    }
}

// -- return_member_func --
template <typename F, std::size_t... ArgCount>
struct function_invoker<F, return_member_func, std::index_sequence<ArgCount...>> {
    template <typename... TArgs>
    static object invoke(const F& f, const object& inst, const TArgs&... args)
    {
        using Class = typename function_traits<F>::class_type;
        if (!inst.can_safe_cast<Class*>()) {
            return object::undefined();
        }
        auto* ptr = inst.cast<Class*>();
        if (ptr
            && checkAllTrue(args.template can_safe_cast<
                typename function_traits<F>::template arg_t<ArgCount>>()...)) {
            return box_function_return((ptr->*f)(
                args.template cast<typename function_traits<F>::template arg_t<ArgCount>>()...));
        }
        return object::undefined();
    }
};

// -- void_member_func --
template <typename F, std::size_t... ArgCount>
struct function_invoker<F, void_member_func, std::index_sequence<ArgCount...>> {
    template <typename... TArgs>
    static object invoke(const F& f, const object& inst, const TArgs&... args)
    {
        auto* ptr = inst.cast<typename function_traits<F>::class_type*>();
        if (ptr
            && checkAllTrue(args.template can_safe_cast<
                typename function_traits<F>::template arg_t<ArgCount>>()...)) {
            (ptr->*f)(
                args.template cast<typename function_traits<F>::template arg_t<ArgCount>>()...);
        }
        return object::undefined();
    }
};

// -- return_func --
template <typename F, std::size_t... ArgCount>
struct function_invoker<F, return_func, std::index_sequence<ArgCount...>> {
    template <typename... TArgs>
    static object invoke(const F& f, const object& inst, const TArgs&... args)
    {
        if (checkAllTrue(args.template can_safe_cast<
                typename function_traits<F>::template arg_t<ArgCount>>()...)) {
            return box_function_return(
                f(args.template cast<typename function_traits<F>::template arg_t<ArgCount>>()...));
        }
        return object::undefined();
    }
};

// -- void_func --
template <typename F, std::size_t... ArgCount>
struct function_invoker<F, void_func, std::index_sequence<ArgCount...>> {
    template <typename... TArgs>
    static object invoke(const F& f, const object& inst, const TArgs&... args)
    {
        if (checkAllTrue(args.template can_safe_cast<
                typename function_traits<F>::template arg_t<ArgCount>>()...)) {
            f(args.template cast<typename function_traits<F>::template arg_t<ArgCount>>()...);
        }
        return object::undefined();
    }
};

// ---------------------------------------------------------------------------
// Policy selector: choose invoker based on F's return type and member-ness.
// ---------------------------------------------------------------------------
template <typename F>
constexpr bool isVoidFunc = std::is_void_v<typename function_traits<F>::return_type>;

template <typename F>
using func_return_policy_t = std::conditional_t<std::is_member_function_pointer_v<F>,
    std::conditional_t<isVoidFunc<F>, void_member_func, return_member_func>,
    std::conditional_t<isVoidFunc<F>, void_func, return_func>>;

template <typename F, typename... TArgs>
constexpr bool isArgumentMatched = (sizeof...(TArgs) == function_traits<F>::arg_count);

// ---------------------------------------------------------------------------
// method_accessor: dispatches invoke/invoke_variadic to the right invoker.
// ---------------------------------------------------------------------------
template <typename F> struct method_accessor {
    using return_policy = func_return_policy_t<F>;
    using arg_indices = typename function_traits<F>::arg_indices;
    using invoker_type = function_invoker<F, return_policy, arg_indices>;

    template <typename... TArgs>
    static std::enable_if_t<isArgumentMatched<F, TArgs...>, object> invoke(
        const F& f, const object& inst, const TArgs&... args)
    {
        return invoker_type::invoke(f, inst, args...);
    }

    template <typename... TArgs>
    static std::enable_if_t<!isArgumentMatched<F, TArgs...>, object> invoke(
        const F& f, const object& inst, const TArgs&... args)
    {
        return object::undefined();
    }

    template <std::size_t... ArgCount>
    static object invoke_variadic_impl(const F& f,
        const object& inst,
        const core::pmr::Vector<object>& argv,
        std::index_sequence<ArgCount...>)
    {
        return invoker_type::invoke(f, inst, argv[ArgCount]...);
    }

    static object invoke_variadic(const F& f,
        const object& inst,
        core::pmr::Vector<object>& argv,
        const core::pmr::Vector<object>& defaults)
    {
        std::size_t argc = function_traits<F>::arg_count;
        if (argv.size() < argc && argv.size() + defaults.size() >= argc) {
            argv.insert(argv.end(), defaults.end() - (argc - argv.size()), defaults.end());
        }
        if (argv.size() == argc) {
            return invoke_variadic_impl(f, inst, argv, arg_indices{});
        }
        return object::undefined();
    }
};

} // namespace arp::reflection::detail

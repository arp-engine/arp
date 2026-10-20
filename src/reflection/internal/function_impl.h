#pragma once

// function template implementation -- mirrors ae::hybrid/internal/function_impl.h.
//
// Template constructors create a basic_function_wrapper<F> and store it in
// a SharedPtr. call(args...) dispatches to the fixed-arity invoke() virtuals.
// call_v(inst, argv) dispatches to invoke_variadic().

#include "core/api.h"
#include "core/cpu_allocator.h"
#include "reflection/internal/function_wrapper.h"

#include <array>
#include <utility>

namespace arp::reflection {

namespace detail {
template <std::size_t Offset, std::size_t Size, std::size_t... Indices>
object invokeFixedFunction(function_wrapper& wrapper,
    const object& receiver,
    const std::array<object, Size>& arguments,
    std::index_sequence<Indices...>)
{
    return wrapper.invoke(receiver, arguments[Offset + Indices]...);
}
} // namespace detail

// ---------------------------------------------------------------------------
// Template constructors.
// ---------------------------------------------------------------------------
template <typename F>
function::function(F&& func)
    : wrapper_(
          core::makeShared<detail::basic_function_wrapper<std::decay_t<F>>>(std::forward<F>(func)))
{}

template <typename F>
function::function(NameView name, F&& func)
    : name_(detail::makeString(name)),
      wrapper_(
          core::makeShared<detail::basic_function_wrapper<std::decay_t<F>>>(std::forward<F>(func)))
{}

template <typename F>
function::function(NameView name, F&& func, NameView doc)
    : name_(detail::makeString(name)), doc_(detail::makeString(doc)),
      wrapper_(
          core::makeShared<detail::basic_function_wrapper<std::decay_t<F>>>(std::forward<F>(func)))
{}

// ---------------------------------------------------------------------------
// set_default_arguments: variadic version.
// ---------------------------------------------------------------------------
template <typename... Args> void function::set_default_arguments(Args&&... args)
{
    wrapper_->default_args_.clear();
    (wrapper_->default_args_.emplace_back(object(std::forward<Args>(args))), ...);
}

// ---------------------------------------------------------------------------
// call: exact small calls use the existing fixed-arity wrapper ABI. Static
// callables receive all supplied values as arguments; member callables use
// the first supplied value as their instance. Defaults and larger calls retain
// the variadic path, including its argument completion and rejection rules.
// ---------------------------------------------------------------------------
template <typename... Args> object function::call(const Args&... args) const
{
    if (!wrapper_) {
        return object::undefined();
    }
    constexpr auto count = sizeof...(Args);
    if constexpr (count <= 5u) {
        if (wrapper_->is_static() && wrapper_->params_info().size() == count) {
            // List initialization preserves the variadic path's left-to-right
            // boxing order without allocating a temporary argument vector.
            const std::array<object, count> boxed{object(args)...};
            return detail::invokeFixedFunction<0u>(
                *wrapper_, object::undefined(), boxed, std::make_index_sequence<count>{});
        }
    }
    if constexpr (count >= 1u && count <= 6u) {
        if (!wrapper_->is_static() && wrapper_->params_info().size() == count - 1u) {
            const std::array<object, count> boxed{object(args)...};
            return detail::invokeFixedFunction<1u>(
                *wrapper_, boxed[0], boxed, std::make_index_sequence<count - 1u>{});
        }
    }
    auto argv = core::pmr::Vector<object>(
        core::pmr::Vector<object>::allocator_type(&core::defaultCpuMemoryResource()));
    argv.reserve(sizeof...(Args));
    (argv.emplace_back(object(args)), ...);
    if (wrapper_->is_static()) {
        return wrapper_->invoke_variadic(object::undefined(), argv);
    }
    if (argv.empty()) {
        return object::undefined();
    }
    object inst = argv.front();
    argv.erase(argv.begin());
    return wrapper_->invoke_variadic(inst, argv);
}

} // namespace arp::reflection

#pragma once

// function_wrapper -- mirrors ae::hybrid/internal/function_wrapper.h.
//
// Virtual base with 6 fixed-arity invoke() methods + invoke_variadic().
// This is the original design (arp's simplified 2-virtual version was
// part of the performance regression and is being reverted).

#include "core/api.h"
#include "core/cpu_allocator.h"
#include "reflection/def.h"
#include "reflection/internal/function_invoker.h"
#include "reflection/object.h"

namespace arp::reflection {

namespace detail {

class ARP_API function_wrapper {
public:
    virtual ~function_wrapper() = default;

    virtual bool is_static() = 0;

    virtual object invoke(const object& arg0) = 0;
    virtual object invoke(const object& arg0, const object& arg1) = 0;
    virtual object invoke(const object& arg0, const object& arg1, const object& arg2) = 0;
    virtual object invoke(
        const object& arg0, const object& arg1, const object& arg2, const object& arg3) = 0;
    virtual object invoke(const object& arg0,
        const object& arg1,
        const object& arg2,
        const object& arg3,
        const object& arg4) = 0;
    virtual object invoke(const object& arg0,
        const object& arg1,
        const object& arg2,
        const object& arg3,
        const object& arg4,
        const object& arg5) = 0;

    virtual object invoke_variadic(const object& inst, core::pmr::Vector<object>& argv) = 0;

    virtual const core::pmr::Vector<parameter>& params_info() const = 0;
    virtual const typeinfo* return_type() const = 0;

    std::size_t default_argument_count() const { return default_args_.size(); }

    core::pmr::Vector<object> default_args_ = core::pmr::Vector<object>(
        core::pmr::Vector<object>::allocator_type(&core::defaultCpuMemoryResource()));
};

// ---------------------------------------------------------------------------
// parameter_info: build a Vector<parameter> from a tuple of arg types.
// ---------------------------------------------------------------------------
template <typename T> struct parameter_info;

template <typename... Args> struct parameter_info<std::tuple<Args...>> {
    static core::pmr::Vector<parameter> types()
    {
        core::pmr::Vector<parameter> params{&core::defaultCpuMemoryResource()};
        params.reserve(sizeof...(Args));
        (params.push_back(parameter{
             type_of<Args>(),
             std::is_const_v<std::remove_reference_t<Args>>,
             std::is_volatile_v<std::remove_reference_t<Args>>,
         }),
            ...);
        return params;
    }
};

// ---------------------------------------------------------------------------
// basic_function_wrapper<F>: concrete wrapper holding a callable F.
// Dispatches to method_accessor<F> for invoke/invoke_variadic.
// ---------------------------------------------------------------------------
template <typename F> class basic_function_wrapper : public function_wrapper {
public:
    using accessor = method_accessor<F>;

    basic_function_wrapper(F func)
        : func_(func),
          params_info_(parameter_info<typename function_traits<F>::arg_types>::types()),
          return_type_(type_of<typename function_traits<F>::return_type>())
    {}

    bool is_static() override { return !std::is_member_function_pointer_v<F>; }

    object invoke(const object& arg0) override { return accessor::invoke(func_, arg0); }

    object invoke(const object& arg0, const object& arg1) override
    {
        return accessor::invoke(func_, arg0, arg1);
    }

    object invoke(const object& arg0, const object& arg1, const object& arg2) override
    {
        return accessor::invoke(func_, arg0, arg1, arg2);
    }

    object invoke(
        const object& arg0, const object& arg1, const object& arg2, const object& arg3) override
    {
        return accessor::invoke(func_, arg0, arg1, arg2, arg3);
    }

    object invoke(const object& arg0,
        const object& arg1,
        const object& arg2,
        const object& arg3,
        const object& arg4) override
    {
        return accessor::invoke(func_, arg0, arg1, arg2, arg3, arg4);
    }

    object invoke(const object& arg0,
        const object& arg1,
        const object& arg2,
        const object& arg3,
        const object& arg4,
        const object& arg5) override
    {
        return accessor::invoke(func_, arg0, arg1, arg2, arg3, arg4, arg5);
    }

    object invoke_variadic(const object& inst, core::pmr::Vector<object>& argv) override
    {
        return accessor::invoke_variadic(func_, inst, argv, default_args_);
    }

    const core::pmr::Vector<parameter>& params_info() const override { return params_info_; }

    const typeinfo* return_type() const override { return return_type_; }

private:
    F func_;
    core::pmr::Vector<parameter> params_info_{&core::defaultCpuMemoryResource()};
    const typeinfo* return_type_ = nullptr;
};

} // namespace detail
} // namespace arp::reflection

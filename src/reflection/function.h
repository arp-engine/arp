#pragma once

// function -- mirrors ae::hybrid/function.h, adapted for arp::reflection.
//
// Wraps a SharedPtr<detail::function_wrapper>. The wrapper provides 6
// fixed-arity invoke() virtuals + invoke_variadic() for dynamic dispatch.

#include "core/api.h"
#include "core/cpu_allocator.h"
#include "reflection/def.h"
#define ARP_REFLECTION_FUNCTION_HEADER_ACTIVE 1
#include "reflection/object.h"
#undef ARP_REFLECTION_FUNCTION_HEADER_ACTIVE

namespace arp::reflection {

namespace detail {
class function_wrapper;
}

class ARP_API function final {
public:
    function(const function& rhs);
    function(function&& rhs) noexcept;
    function& operator=(const function& rhs);
    function& operator=(function&& rhs) noexcept;
    ~function() noexcept;

    template <typename F> explicit function(F&& func);

    template <typename F> function(NameView name, F&& func);

    template <typename F> function(NameView name, F&& func, NameView doc);

    bool is_static() const;
    void set_default_arguments(const core::pmr::Vector<object>& defaults);

    template <typename... Args> void set_default_arguments(Args&&... args);

    template <typename... Args> object call(const Args&... args) const;

    object call_v(const object& inst, core::pmr::Vector<object>& args) const;
    const core::pmr::String& name() const;
    const core::pmr::String& doc() const;
    core::pmr::Vector<parameter> params_info() const;
    std::size_t parameter_count() const;
    const typeinfo* return_type() const;
    std::size_t default_argument_count() const;
    // Stable only for copies of the same registered function wrapper. This
    // intentionally exposes no invocation internals; contract registries use
    // it to distinguish same-name reflection functions without guessing.
    const void* identity() const noexcept;

private:
    core::pmr::String name_{&core::defaultCpuMemoryResource()};
    core::pmr::String doc_{&core::defaultCpuMemoryResource()};
    detail::SharedPtr<detail::function_wrapper> wrapper_;
};

} // namespace arp::reflection

#include "reflection/internal/function_impl.h"
#include "reflection/internal/object_impl.h"

// function.cpp -- mirrors ae::hybrid/src/function.cpp, adapted for arp::reflection.

#include "reflection/function.h"

#include "reflection/internal/function_wrapper.h"

#include <utility>

namespace arp::reflection {

function::function(const function& rhs) : name_(rhs.name_), doc_(rhs.doc_), wrapper_(rhs.wrapper_)
{}

function::function(function&& rhs) noexcept
{
    std::swap(name_, rhs.name_);
    std::swap(doc_, rhs.doc_);
    std::swap(wrapper_, rhs.wrapper_);
}

function& function::operator=(const function& rhs)
{
    if (this == &rhs) {
        return *this;
    }
    name_ = rhs.name_;
    doc_ = rhs.doc_;
    wrapper_ = rhs.wrapper_;
    return *this;
}

function& function::operator=(function&& rhs) noexcept
{
    if (this == &rhs) {
        return *this;
    }
    name_ = std::move(rhs.name_);
    doc_ = std::move(rhs.doc_);
    wrapper_ = std::move(rhs.wrapper_);
    return *this;
}

function::~function() noexcept = default;

bool function::is_static() const
{
    return wrapper_ ? wrapper_->is_static() : false;
}

void function::set_default_arguments(const core::pmr::Vector<object>& defaults)
{
    if (wrapper_) {
        wrapper_->default_args_ = defaults;
    }
}

object function::call_v(const object& inst, core::pmr::Vector<object>& args) const
{
    return wrapper_ ? wrapper_->invoke_variadic(inst, args) : object::undefined();
}

const core::pmr::String& function::name() const
{
    return name_;
}

const core::pmr::String& function::doc() const
{
    return doc_;
}

core::pmr::Vector<parameter> function::params_info() const
{
    if (!wrapper_) {
        return core::pmr::Vector<parameter>{&core::defaultCpuMemoryResource()};
    }
    return core::pmr::Vector<parameter>(wrapper_->params_info(), &core::defaultCpuMemoryResource());
}

const typeinfo* function::return_type() const
{
    return wrapper_ ? wrapper_->return_type() : nullptr;
}

std::size_t function::parameter_count() const
{
    return wrapper_ ? wrapper_->params_info().size() : 0u;
}

std::size_t function::default_argument_count() const
{
    return wrapper_ ? wrapper_->default_argument_count() : 0u;
}

const void* function::identity() const noexcept
{
    return wrapper_.get();
}

} // namespace arp::reflection

// object.cpp -- mirrors ae::hybrid/src/object.cpp, adapted for arp::reflection.
//
// Key decisions (reverted to original):
//  - operator== : class_()->call("__eq__", rhs).cast<bool>() -- no fallback
//  - operator<  : class_()->call("__lt__", rhs).cast<bool>() -- no fallback
//  - class_()    : is<instance>() ? cast<instance>()->class_() : type()->class_()
//  - PMR: core::makeShared instead of std::make_shared

#include "reflection/object.h"

#include "reflection/class_info.h"
#include "reflection/instance.h"
#include "reflection/type.h"

namespace arp::reflection {

namespace detail {

void* NativeObjectControl::pointerFor(const class_info* requestedClass) const noexcept
{
    void* pointer = this->pointer();
    if (pointer == nullptr || requestedClass == nullptr || dynamicClass_ == nullptr)
        return nullptr;
    if (requestedClass == dynamicClass_)
        return pointer;
    return dynamicClass_->dynamic_cast_(requestedClass, pointer);
}

} // namespace detail

namespace detail {

core::pmr::String makeString(NameView text)
{
    return core::pmr::String(text.data(), text.size(), &core::defaultCpuMemoryResource());
}

core::pmr::String makeString(const char* text)
{
    if (text == nullptr) {
        return core::pmr::String(&core::defaultCpuMemoryResource());
    }
    return core::pmr::String(text, &core::defaultCpuMemoryResource());
}

core::pmr::String typeNameFromParts(
    NameView nativeName, bool isConst, bool isVolatile, bool isLvalueRef, bool isRvalueRef)
{
    core::pmr::String result{&core::defaultCpuMemoryResource()};
    if (isConst) {
        result += "const ";
    }
    if (isVolatile) {
        result += "volatile ";
    }
    result += makeString(nativeName);
    if (isLvalueRef) {
        result += "&";
    }
    if (isRvalueRef) {
        result += "&&";
    }
    return result;
}

} // namespace detail

const object& object::undefined()
{
    static object value;
    return value;
}

object::object() : value_(detail::ObjectRef<variant>::make())
{}

object::object(const object& rhs) : value_(rhs.value_)
{}

object::object(object&& rhs) noexcept : value_(std::move(rhs.value_))
{}

object::object(const detail::SharedPtr<variant>& value)
    : value_(value ? detail::ObjectRef<variant>::make(*value) : detail::ObjectRef<variant>::make())
{}

object::object(detail::SharedPtr<variant>&& value)
    : value_(value ? detail::ObjectRef<variant>::make(std::move(*value))
                   : detail::ObjectRef<variant>::make())
{}

object::object(const variant& value) : value_(detail::ObjectRef<variant>::make(value))
{}

object::object(variant&& value) : value_(detail::ObjectRef<variant>::make(std::move(value)))
{}

object::object(const char* text) : value_(detail::ObjectRef<variant>::make(text))
{}

object object::native_reference_dynamic(
    void* pointer, const class_info* dynamicClass, const typeinfo* pointerType)
{
    if (pointer == nullptr || dynamicClass == nullptr || pointerType == nullptr) {
        return object::undefined();
    }
    return object(detail::NativeObjectReference::make(pointer, dynamicClass, pointerType));
}

object& object::operator=(const object& rhs)
{
    if (this == &rhs) {
        return *this;
    }
    value_ = rhs.value_;
    return *this;
}

object& object::operator=(object&& rhs) noexcept
{
    if (this == &rhs) {
        return *this;
    }
    value_ = std::move(rhs.value_);
    return *this;
}

value_t object::type() const
{
    if (is_native_reference()) {
        return instance_t;
    }
    if (is<instance>()) {
        return instance_t;
    }
    return class_()->valueType();
}

const typeinfo* object::typeinfo_() const
{
    if (auto* control = native_control()) {
        return control->pointerType();
    }
    return value_->type();
}

bool object::is(value_t reflectedType) const
{
    return type() == reflectedType;
}

bool object::is_undefined() const
{
    // Undefined is represented by the default variant's void type. Avoid the
    // reflected type lookup here; this predicate is used on VM hot paths.
    return value_->template is<void>();
}

bool object::is_null() const
{
    return value_->template is<std::nullptr_t>();
}

bool object::is_function() const
{
    return is(function_t);
}

bool object::is_class() const
{
    return is(class_t);
}

bool object::is_property() const
{
    return is(property_t);
}

bool object::is_object() const
{
    return is(instance_t);
}

bool object::is_list() const
{
    return is(list_t);
}

bool object::is_dict() const
{
    return is(dict_t);
}

const class_info* object::class_() const
{
    if (auto* control = native_control()) {
        return control->dynamicClass();
    }
    if (is<instance>()) {
        return value_->cast<instance>()->class_();
    }
    return value_->type()->class_();
}

bool object::is_native_reference() const
{
    return value_->template is<detail::NativeObjectReference>();
}

const void* object::storageIdentity() const noexcept
{
    return value_.identity();
}

bool object::native_reference_alive() const
{
    auto* control = native_control();
    return control != nullptr && control->alive();
}

void object::invalidate_native_reference() const
{
    if (auto* control = native_control()) {
        control->invalidate();
    }
}

detail::NativeObjectControl* object::native_control() const noexcept
{
    if (!value_->template is<detail::NativeObjectReference>()) {
        return nullptr;
    }
    return const_cast<detail::NativeObjectControl*>(
        value_->template cast<detail::NativeObjectReference>()->control());
}

bool object::is_base_of(const class_info* cls) const
{
    return class_()->is_base_of(cls);
}

bool object::is_derived_from(const class_info* cls) const
{
    return class_()->is_derived_from(cls);
}

void object::set(NameView name, const object& value) const
{
    class_()->set_attr(*this, name, value);
}

object object::operator-() const
{
    return class_()->call(*this, "__neg__");
}

object object::operator+(const object& rhs) const
{
    return class_()->call(*this, "__add__", rhs);
}

object object::operator-(const object& rhs) const
{
    return class_()->call(*this, "__sub__", rhs);
}

object object::operator*(const object& rhs) const
{
    return class_()->call(*this, "__mul__", rhs);
}

object object::operator/(const object& rhs) const
{
    return class_()->call(*this, "__div__", rhs);
}

object object::operator%(const object& rhs) const
{
    return class_()->call(*this, "__mod__", rhs);
}

object object::operator^(const object& rhs) const
{
    return class_()->call(*this, "__xor__", rhs);
}

object object::operator|(const object& rhs) const
{
    return class_()->call(*this, "__or__", rhs);
}

object object::operator&(const object& rhs) const
{
    return class_()->call(*this, "__and__", rhs);
}

bool object::operator==(const object& rhs) const
{
    bool scalar = false;
    if (tryBuiltinScalarComparison(rhs, true, scalar))
        return scalar;
    auto result = class_()->call(*this, "__eq__", rhs);
    if (result.is<bool>()) {
        return result.cast<bool>();
    }
    // arp fallback: when __eq__ is not registered (e.g. comparing undefined),
    // fall back to shared identity. This is needed because the VM uses
    // operator== on arbitrary value types that may not have __eq__.
    return value_.get() == rhs.value_.get();
}

bool object::operator<(const object& rhs) const
{
    bool scalar = false;
    if (tryBuiltinScalarComparison(rhs, false, scalar))
        return scalar;
    auto result = class_()->call(*this, "__lt__", rhs);
    if (result.is<bool>()) {
        return result.cast<bool>();
    }
    return value_.get() < rhs.value_.get();
}

namespace {
template <class T>
bool compareSameScalar(const variant& lhs, const variant& rhs, bool equality, bool& result)
{
    if (!lhs.is<T>() || !rhs.is<T>())
        return false;
    const auto& left = *lhs.cast<T>();
    const auto& right = *rhs.cast<T>();
    result = equality ? left == right : left < right;
    return true;
}
} // namespace

bool object::tryBuiltinScalarComparison(const object& rhs, bool equality, bool& result) const
{
    const auto* cls = value_->class_();
    if (cls == nullptr || !(equality ? cls->builtinEquality_ : cls->builtinOrdering_))
        return false;
    // The stock-method gate belongs to this exact registered class. Its
    // value category selects candidates; both exact payload types are still
    // checked (integer_t includes int/int64, and native references can share
    // class metadata). Overridden methods continue through reflected dispatch.
    switch (cls->type_) {
    case integer_t:
        return compareSameScalar<int>(*value_.get(), *rhs.value_.get(), equality, result)
            || compareSameScalar<std::int64_t>(*value_.get(), *rhs.value_.get(), equality, result);
    case float_t:
        return compareSameScalar<double>(*value_.get(), *rhs.value_.get(), equality, result);
    case boolean_t:
        return compareSameScalar<bool>(*value_.get(), *rhs.value_.get(), equality, result);
    case string_t:
        return compareSameScalar<core::pmr::String>(
            *value_.get(), *rhs.value_.get(), equality, result);
    default:
        return false;
    }
}

object object::operator[](NameView name) const
{
    return class_()->get_attr(*this, name);
}

} // namespace arp::reflection

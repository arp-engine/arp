#pragma once

// object -- mirrors ae::hybrid/object.h, adapted for arp::reflection.
//
// object wraps an ObjectRef<variant> (shared ownership). It dispatches
// operators and type queries through the variant's class_info.

#include "core/api.h"
#include "core/memory/cpu_allocator.h"
#include "reflection/def.h"
#include "reflection/native_reference.h"
#include "reflection/variant.h"

namespace arp::reflection {

class class_info;

class ARP_API object final {
public:
    object();
    object(const object& rhs);
    object(object&& rhs) noexcept;

    // arp extensions: construct directly from a shared variant.
    explicit object(const detail::SharedPtr<variant>& value);
    explicit object(detail::SharedPtr<variant>&& value);
    explicit object(const variant& value);
    explicit object(variant&& value);
    explicit object(const char* text);

    template <std::size_t Size>
    explicit object(const char (&text)[Size]) : object(static_cast<const char*>(text))
    {}

    // Construct from any value type (template, implemented in object_impl.h).
    // Not explicit: allows implicit conversion for return-type coercion in
    // function_invoker (return f(args...) -> object).
    template <typename T,
        std::enable_if_t<!std::is_same_v<std::remove_cv_t<std::remove_reference_t<T>>, object>
                && !detail::isValueCarrier<std::remove_cv_t<std::remove_reference_t<T>>>::value
                && !std::is_same_v<std::remove_cv_t<std::remove_reference_t<T>>, variant>
                && !std::is_same_v<std::remove_cv_t<std::remove_reference_t<T>>,
                    std::pmr::memory_resource*>,
            int> = 0>
    object(T&& value);

    ~object() = default;

    object& operator=(const object& rhs);
    object& operator=(object&& rhs) noexcept;

    value_t type() const;
    const typeinfo* typeinfo_() const;
    bool tryGetEnumInt(int& value) const noexcept;
    bool is(value_t type) const;
    bool is_undefined() const;
    bool is_null() const;
    bool is_function() const;
    bool is_class() const;
    bool is_property() const;
    bool is_object() const;
    bool is_list() const;
    bool is_dict() const;
    const class_info* class_() const;
    bool is_base_of(const class_info* cls) const;
    bool is_derived_from(const class_info* cls) const;
    void set(NameView name, const object& value) const;

    object operator-() const;
    object operator+(const object& rhs) const;
    object operator-(const object& rhs) const;
    object operator*(const object& rhs) const;
    object operator/(const object& rhs) const;
    object operator%(const object& rhs) const;
    object operator^(const object& rhs) const;
    object operator|(const object& rhs) const;
    object operator&(const object& rhs) const;
    bool operator==(const object& rhs) const;
    bool operator<(const object& rhs) const;

    bool operator!=(const object& rhs) const { return !((*this) == rhs); }

    bool operator>(const object& rhs) const { return !((*this) == rhs || (*this) < rhs); }

    bool operator<=(const object& rhs) const { return ((*this) < rhs || (*this) == rhs); }

    bool operator>=(const object& rhs) const { return !((*this) < rhs); }

    object operator[](NameView name) const;

    static const object& undefined();

    // Construct a reflection value that refers to an engine-owned native
    // object. The native object is not copied or owned by this value.
    template <typename T> static object native_reference(T* pointer);

    // Engine-only binding path for a polymorphic object returned through a
    // base pointer. `pointer` must be the most-derived address and
    // `dynamicClass` must come from generated reflection registration. The
    // overload is intentionally not reflected or exposed to scripts.
    static object native_reference_dynamic(
        void* pointer, const class_info* dynamicClass, const typeinfo* pointerType);

    bool is_native_reference() const;
    bool native_reference_alive() const;
    void invalidate_native_reference() const;
    // Stable for the lifetime of the shared reflection value. This is the
    // exact core allocation identity, not the address of the inline variant.
    const void* storageIdentity() const noexcept;

    // Carrier ownership only; callers must separately prove payload ownership
    // and that an in-place operation preserves the language's alias contract.
    bool hasUniqueStorage() const noexcept { return value_.unique(); }

    template <typename... Args> object call(NameView name, const Args&... args) const;

    template <typename T> constexpr bool is() const;

    template <typename T> bool can_safe_cast() const;

    template <typename T> T* ptr() const;

    template <typename T> std::enable_if_t<std::is_reference_v<T>, T> cast() const;

    template <typename T> std::enable_if_t<!std::is_reference_v<T>, T> cast() const;

private:
    detail::NativeObjectControl* native_control() const noexcept;
    bool tryBuiltinScalarComparison(const object& rhs, bool equality, bool& result) const;

    detail::ObjectRef<variant> value_;
};

// Specializations declared (implemented in object_impl.h).
template <> constexpr bool object::is<object>() const;
template <> constexpr bool object::is<object&>() const;
template <> constexpr bool object::is<const object&>() const;
template <> constexpr bool object::is<detail::ObjectRef<variant>&>() const;

template <> object object::cast<object>() const;
template <> object& object::cast<object&>() const;
template <> const object& object::cast<const object&>() const;
template <> detail::ObjectRef<variant>& object::cast<detail::ObjectRef<variant>&>() const;
template <>
const detail::ObjectRef<variant>& object::cast<const detail::ObjectRef<variant>&>() const;

// Container type aliases (arp PMR variants of std::vector/std::map).
using hlist = core::pmr::Vector<object>;
using hdict = core::pmr::Map<object, object>;

inline hlist makeEmptyHList()
{
    return hlist(hlist::allocator_type(&core::defaultCpuMemoryResource()));
}

} // namespace arp::reflection

#if !defined(ARP_REFLECTION_CLASS_INFO_HEADER_ACTIVE)                                              \
    && !defined(ARP_REFLECTION_FUNCTION_HEADER_ACTIVE)
#include "reflection/internal/object_impl.h"
#endif

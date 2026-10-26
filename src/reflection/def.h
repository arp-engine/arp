#pragma once

#include "core/api.h"
#include "core/memory/cpu_allocator.h"
#include "reflection/internal/type_name.h"

#include <atomic>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>

namespace arp::reflection {

namespace detail {
// Value adapters must use their declared conversion edge, never become an
// unrelated reflected payload through object's catch-all boxing constructor.
template <class T, class = void> struct isValueCarrier : std::false_type {};

template <class T>
struct isValueCarrier<T, std::void_t<typename T::ReflectionValueCarrier>> : std::true_type {};
} // namespace detail

class class_info;
class typeinfo;
class object;
class variant;
class function;
class property;
class enumeration;
class instance;
class db;

// parameter struct -- declared here because it's needed by both function.h
// and internal/function_wrapper.h (which must avoid circular includes).
struct parameter {
    const typeinfo* ti = nullptr;
    bool is_const = false;
    bool is_volatile = false;
};

using NameView = std::basic_string_view<char>;
using ByteBuffer = core::pmr::Vector<std::uint8_t>;

template <typename T> std::enable_if_t<!std::is_void_v<T>, class_info*> class_of();

template <typename T> std::enable_if_t<std::is_void_v<T>, class_info*> class_of();

template <typename T> const class_info* class_of_instance(T*);

template <typename T> const typeinfo* type_of();

// ---------------------------------------------------------------------------
// value_t -- mirrors the original ae::hybrid value_t enum
// ---------------------------------------------------------------------------
enum value_t : std::uint8_t {
    undefined_t,
    null_t,
    boolean_t,
    integer_t,
    float_t,
    string_t,
    list_t,
    instance_t,
    dict_t,
    buffer_t,
    function_t,
    class_t,
    property_t,
    enumeration_t,
    others_t,
};

// ---------------------------------------------------------------------------
// type_trait_mask -- mirrors the original ae::hybrid type_trait_mask
// ---------------------------------------------------------------------------
enum class type_trait_mask : std::size_t {
    is_class = 0,
    is_enum,
    is_array,
    is_pointer,
    is_arithmetic,
    is_function_pointer,
    is_member_object_pointer,
    is_member_function_pointer,
    is_lvalue_reference,
    is_rvalue_reference,
    is_reference,
    is_wrapper,
    is_map_container,
    is_sequential_container,
    type_trait_count,
};

using type_mask = std::bitset<static_cast<std::size_t>(type_trait_mask::type_trait_count)>;

struct undefined_ {};

struct null_ {};

struct derived_info {
    void* ptr = nullptr;
    const class_info* cls = nullptr;
};

using cast_func = void* (*)(void*);
using get_derived_info_func = derived_info (*)(void*);

// ---------------------------------------------------------------------------
// value_type -- generalized version of the original explicit specializations.
// The original used one explicit specialization per known type; arp uses
// SFINAE traits so any type is mapped without manual registration.
// ---------------------------------------------------------------------------
namespace detail {

template <typename T> struct isBasicText : std::false_type {};

template <typename Traits, typename Allocator>
struct isBasicText<std::basic_string<char, Traits, Allocator>> : std::true_type {};

template <typename T> struct isSequentialContainer : std::false_type {};

template <typename T, typename Allocator>
struct isSequentialContainer<std::vector<T, Allocator>> : std::true_type {};

template <typename T> struct isMapContainer : std::false_type {};

template <typename Key, typename Value, typename Compare, typename Allocator>
struct isMapContainer<std::map<Key, Value, Compare, Allocator>> : std::true_type {};

template <typename Key, typename Value, typename Hash, typename Equal, typename Allocator>
struct isMapContainer<std::unordered_map<Key, Value, Hash, Equal, Allocator>> : std::true_type {};

} // namespace detail

template <typename T> struct value_type {
    using Raw = std::remove_cv_t<T>;
    static constexpr value_t value = std::is_void_v<Raw>          ? undefined_t
        : std::is_same_v<Raw, std::nullptr_t>                     ? null_t
        : std::is_same_v<Raw, bool>                               ? boolean_t
        : (std::is_integral_v<Raw> && !std::is_same_v<Raw, bool>) ? integer_t
        : std::is_floating_point_v<Raw>                           ? float_t
        : detail::isBasicText<Raw>::value                         ? string_t
        : detail::isSequentialContainer<Raw>::value               ? list_t
        : detail::isMapContainer<Raw>::value                      ? dict_t
        : std::is_same_v<Raw, ByteBuffer>                         ? buffer_t
        : std::is_same_v<Raw, function>                           ? function_t
        : std::is_same_v<Raw, class_info>                         ? class_t
        : std::is_same_v<Raw, property>                           ? property_t
        : std::is_same_v<Raw, enumeration>                        ? enumeration_t
                                                                  : others_t;
};

template <typename T> constexpr value_t value_type_v = value_type<std::remove_cv_t<T>>::value;

// ---------------------------------------------------------------------------
// detail namespace -- shared utilities (traits, helpers, allocators)
// ---------------------------------------------------------------------------
namespace detail {

template <class T> using SharedPtr = decltype(core::makeShared<T>());

// Separate owners may retain immutable values after registration has finished.
// The count protects lifetime only: payload writes, registry changes and access
// to the same owner still require their existing external synchronization.
template <class T> class ObjectRef final {
private:
    struct Control {
        std::atomic<std::uint32_t> references{1};
        T value;

        template <class... Args>
        explicit Control(Args&&... args) : value(std::forward<Args>(args)...)
        {}
    };

    struct AdoptTag {};

    explicit ObjectRef(Control* control, AdoptTag) noexcept : control_(control) {}

    void release() noexcept
    {
        Control* control = std::exchange(control_, nullptr);
        if (control == nullptr)
            return;
        if (control->references.fetch_sub(1, std::memory_order_acq_rel) == 1u)
            core::destroyCpuAllocatedUnique<Control>(control);
    }

public:
    ObjectRef() noexcept = default;

    ObjectRef(const ObjectRef& other) noexcept : control_(other.control_)
    {
        if (control_ != nullptr)
            control_->references.fetch_add(1, std::memory_order_relaxed);
    }

    ObjectRef(ObjectRef&& other) noexcept : control_(std::exchange(other.control_, nullptr)) {}

    ~ObjectRef() { release(); }

    ObjectRef& operator=(const ObjectRef& other) noexcept
    {
        if (this == &other)
            return *this;
        Control* next = other.control_;
        if (next != nullptr)
            next->references.fetch_add(1, std::memory_order_relaxed);
        release();
        control_ = next;
        return *this;
    }

    ObjectRef& operator=(ObjectRef&& other) noexcept
    {
        if (this == &other)
            return *this;
        release();
        control_ = std::exchange(other.control_, nullptr);
        return *this;
    }

    static ObjectRef make()
    {
        auto owner = core::makeUnique<Control>();
        auto* control = owner.release();
        return ObjectRef(control, AdoptTag{});
    }

    template <class... Args> static ObjectRef make(Args&&... args)
    {
        auto owner = core::makeUnique<Control>(std::forward<Args>(args)...);
        auto* control = owner.release();
        return ObjectRef(control, AdoptTag{});
    }

    void swap(ObjectRef& other) noexcept { std::swap(control_, other.control_); }

    T* get() noexcept { return control_ != nullptr ? &control_->value : nullptr; }

    const T* get() const noexcept { return control_ != nullptr ? &control_->value : nullptr; }

    T* operator->() noexcept { return get(); }

    const T* operator->() const noexcept { return get(); }

    const void* identity() const noexcept { return control_; }

    // A momentary observation, never an exclusive payload-write permission.
    bool unique() const noexcept
    {
        return control_ != nullptr && control_->references.load(std::memory_order_acquire) == 1u;
    }

private:
    Control* control_ = nullptr;
};

ARP_API core::pmr::String makeString(NameView text);
ARP_API core::pmr::String makeString(const char* text);
ARP_API core::pmr::String typeNameFromParts(
    NameView nativeName, bool isConst, bool isVolatile, bool isLvalueRef, bool isRvalueRef);

// -- function_traits: mirrors the original core/type_traits.h --
template <typename T> struct function_traits;

template <typename R, typename... Args> struct function_traits<R(Args...)> {
    static constexpr std::size_t arg_count = sizeof...(Args);
    using return_type = R;
    using arg_types = std::tuple<Args...>;
    using arg_indices = std::index_sequence_for<Args...>;

    template <std::size_t Index> using arg_t = typename std::tuple_element<Index, arg_types>::type;
};

template <typename R, typename... Args>
struct function_traits<R (*)(Args...)> : function_traits<R(Args...)> {};

template <typename R, typename... Args>
struct function_traits<std::function<R(Args...)>> : function_traits<R(Args...)> {};

template <typename Callable>
struct function_traits : function_traits<decltype(&Callable::operator())> {};

template <typename R, typename C, typename... Args>
struct function_traits<R (C::*)(Args...)> : function_traits<R(Args...)> {
    using class_type = C;
};

template <typename R, typename C, typename... Args>
struct function_traits<R (C::*)(Args...) const> : function_traits<R(Args...)> {
    using class_type = C;
};

template <typename R, typename C, typename... Args>
struct function_traits<R (C::*)(Args...) volatile> : function_traits<R(Args...)> {
    using class_type = C;
};

template <typename R, typename C, typename... Args>
struct function_traits<R (C::*)(Args...) const volatile> : function_traits<R(Args...)> {
    using class_type = C;
};

template <typename T>
struct is_function_pointer
    : std::bool_constant<std::is_pointer_v<T> && std::is_function_v<std::remove_pointer_t<T>>> {};

template <typename T> constexpr bool is_function_pointer_v = is_function_pointer<T>::value;

template <typename T, typename = void> struct raw_type {
    using type = std::remove_cv_t<T>;
};

template <typename T>
struct raw_type<T, std::enable_if_t<std::is_pointer_v<T> && !is_function_pointer_v<T>>> {
    using type = typename raw_type<std::remove_pointer_t<T>>::type;
};

template <typename T> struct raw_type<T, std::enable_if_t<std::is_reference_v<T>>> {
    using type = typename raw_type<std::remove_reference_t<T>>::type;
};

template <typename T> using raw_type_t = typename raw_type<T>::type;

template <typename T> core::pmr::String typeName()
{
    using NoRef = std::remove_reference_t<T>;
    using RawNoCv = std::remove_cv_t<NoRef>;
    const auto nativeName = detail::getTypeName<RawNoCv>();
    return typeNameFromParts(NameView(nativeName.data(), nativeName.size()),
        std::is_const_v<NoRef>,
        std::is_volatile_v<NoRef>,
        std::is_lvalue_reference_v<T>,
        std::is_rvalue_reference_v<T>);
}

constexpr bool checkAllTrue()
{
    return true;
}

template <typename... Rest> constexpr bool checkAllTrue(bool value, Rest... rest)
{
    return value && checkAllTrue(rest...);
}

// -- arp extensions: indirect value traits --
template <typename T, typename = void> struct indirectValueTraits {
    static constexpr bool enabled = false;
    static constexpr bool smart = false;
    static constexpr bool resultOwned = false;
    static constexpr bool allowDerived = false;
};

template <typename T>
struct indirectValueTraits<T, std::enable_if_t<std::is_pointer_v<T> && !is_function_pointer_v<T>>> {
    using element_type = raw_type_t<T>;
    static constexpr bool enabled = true;
    static constexpr bool smart = false;
    static constexpr bool resultOwned = true;
    static constexpr bool allowDerived = true;
};

template <typename T> struct indirectValueTraits<std::shared_ptr<T>> {
    using element_type = raw_type_t<T>;
    static constexpr bool enabled = true;
    static constexpr bool smart = true;
    static constexpr bool resultOwned = false;
    static constexpr bool allowDerived = false;
};

template <typename T> struct indirectValueTraits<core::UniquePtr<T>> {
    using element_type = raw_type_t<T>;
    static constexpr bool enabled = true;
    static constexpr bool smart = true;
    static constexpr bool resultOwned = false;
    static constexpr bool allowDerived = false;
};

template <typename T> struct isWrapper : std::bool_constant<indirectValueTraits<T>::smart> {};

template <typename T, typename Deleter>
struct isWrapper<std::unique_ptr<T, Deleter>> : std::true_type {};

template <typename T> struct isWrapper<std::weak_ptr<T>> : std::true_type {};

template <typename T> struct dependentFalse : std::false_type {};

// Defined by internal/class_impl.h. The declaration here avoids an include
// cycle between object_impl.h and class_info.h.
template <typename T> struct caster;

template <typename DestType, typename SourceType> void* upcastImpl(void* ptr)
{
    using Dest = raw_type_t<DestType>;
    using Source = raw_type_t<SourceType>;
    return reinterpret_cast<void*>(static_cast<Dest*>(reinterpret_cast<Source*>(ptr)));
}

template <typename T, typename = void> struct hasDerivedInfo : std::false_type {};

template <typename T>
struct hasDerivedInfo<T, std::void_t<decltype(std::declval<T>().get_derived_info())>>
    : std::true_type {};

} // namespace detail

// ---------------------------------------------------------------------------
// OBJECT_RTTI_IMPL macro -- mirrors the original, namespace adapted
// ---------------------------------------------------------------------------
#define ARP_REFLECTION_OBJECT_RTTI_IMPL(CLASS)                                                     \
    virtual void* get_ptr()                                                                        \
    {                                                                                              \
        return reinterpret_cast<void*>(this);                                                      \
    }                                                                                              \
    virtual ::arp::reflection::derived_info get_derived_info()                                     \
    {                                                                                              \
        return ::arp::reflection::derived_info{                                                    \
            reinterpret_cast<void*>(this), ::arp::reflection::class_of_instance(this)};            \
    }

#ifndef OBJECT_RTTI_IMPL
#define OBJECT_RTTI_IMPL(CLASS) ARP_REFLECTION_OBJECT_RTTI_IMPL(CLASS)
#endif

} // namespace arp::reflection

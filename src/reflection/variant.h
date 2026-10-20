#pragma once

// variant -- mirrors ae::hybrid/variant.h, adapted for arp::reflection.
//
// The variant holds a vtable (by value, not pointer) and an inline storage
// buffer. The default constructor sets ctype = &typeid(void) and all function
// pointers to nullptr (except typefunc which points to type_of<void>).

#include "core/api.h"
#include "reflection/def.h"
#include "reflection/internal/variant_data.h"

namespace arp::reflection {

class object;
class class_info;

class ARP_API variant final {
public:
    variant();
    variant(const variant& rhs);
    variant(variant&& rhs) noexcept;
    explicit variant(const char* text);

    template <std::size_t Size>
    explicit variant(const char (&text)[Size]) : variant(static_cast<const char*>(text))
    {}

    // Construct from any value type (template, implemented in variant_impl.h).
    template <typename T,
        std::enable_if_t<!std::is_same_v<std::remove_cv_t<std::remove_reference_t<T>>, variant>,
            int> = 0>
    variant(T&& value);

    ~variant() noexcept;

    variant& operator=(const variant& rhs);
    variant& operator=(variant&& rhs) noexcept;

    const typeinfo* type() const;
    const class_info* class_() const;
    void* ptr() const;
    bool tryGetEnumInt(int& value) const noexcept;

    template <typename T> constexpr bool is() const;

    // Sufficient proof of this exact owned storage representation. A selector
    // mismatch (including a distinct module's selector) returns nullptr; callers
    // retain is/cast/native conversion when this narrower proof is unavailable.
    // No reflection registration, conversion or native pointer resolution.
    template <typename T> const T* exactPayloadPointer() const noexcept;

    template <typename T> constexpr const std::enable_if_t<!std::is_array_v<T>, T*> cast() const;

    template <typename T>
    constexpr const std::enable_if_t<std::is_array_v<T>, std::decay_t<T>> cast() const;

    void swap(variant& rhs) noexcept;

    // Delete object-related constructors (mirrors original).
    variant(const object&) = delete;
    variant(object&) = delete;
    variant(object&&) = delete;

private:
    void copy_assign(const variant& rhs);
    void move_assign(variant&& rhs) noexcept;

    variant_vtable vtable_;
    storage_t data_;
};

} // namespace arp::reflection

#include "reflection/internal/variant_impl.h"

// variant.cpp -- mirrors ae::hybrid/src/variant.cpp, adapted for arp::reflection.
//
// Non-template implementation of variant. The default constructor sets
// ctype = &typeid(void), typefunc = &type_of<void>, and destroy/copy/move
// to nullptr. This is the KEY difference from the previous arp version
// which used a nullptr vtable pointer.

#include "reflection/variant.h"

#include "reflection/class_info.h"
#include "reflection/type.h"

namespace arp::reflection {

variant::variant()
{
    vtable_.ctype = &typeid(void);
    vtable_.typefunc = &type_of<void>;
    vtable_.enumToInt = nullptr;
    vtable_.copy = nullptr;
    vtable_.move = nullptr;
    vtable_.destroy = nullptr;
    vtable_.ptr = nullptr;
}

variant::variant(const variant& rhs)
{
    vtable_.ctype = &typeid(void);
    vtable_.typefunc = &type_of<void>;
    vtable_.enumToInt = nullptr;
    vtable_.copy = nullptr;
    vtable_.move = nullptr;
    vtable_.destroy = nullptr;
    vtable_.ptr = nullptr;
    copy_assign(rhs);
}

variant::variant(variant&& rhs) noexcept
{
    vtable_.ctype = &typeid(void);
    vtable_.typefunc = &type_of<void>;
    vtable_.enumToInt = nullptr;
    vtable_.copy = nullptr;
    vtable_.move = nullptr;
    vtable_.destroy = nullptr;
    vtable_.ptr = nullptr;
    move_assign(std::move(rhs));
}

variant::variant(const char* text)
    : variant(core::pmr::String(text, &core::defaultCpuMemoryResource()))
{}

variant::~variant() noexcept
{
    if (vtable_.destroy) {
        vtable_.destroy(data_);
    }
}

const typeinfo* variant::type() const
{
    return vtable_.typefunc();
}

const class_info* variant::class_() const
{
    return vtable_.typefunc()->class_();
}

void* variant::ptr() const
{
    if (vtable_.ptr) {
        return vtable_.ptr(const_cast<storage_t&>(data_));
    }
    return nullptr;
}

bool variant::tryGetEnumInt(int& value) const noexcept
{
    return vtable_.enumToInt != nullptr && vtable_.enumToInt(const_cast<storage_t&>(data_), value);
}

variant& variant::operator=(const variant& rhs)
{
    copy_assign(rhs);
    return *this;
}

variant& variant::operator=(variant&& rhs) noexcept
{
    move_assign(std::move(rhs));
    return *this;
}

void variant::swap(variant& rhs) noexcept
{
    std::swap(vtable_, rhs.vtable_);
    std::swap(data_, rhs.data_);
}

void variant::copy_assign(const variant& rhs)
{
    if (!is<void>()) {
        if (vtable_.destroy) {
            vtable_.destroy(data_);
        }
    }
    vtable_ = rhs.vtable_;
    if (!rhs.is<void>()) {
        if (vtable_.copy) {
            vtable_.copy(data_, rhs.data_);
        }
    }
}

void variant::move_assign(variant&& rhs) noexcept
{
    if (!is<void>()) {
        if (vtable_.destroy) {
            vtable_.destroy(data_);
        }
    }
    vtable_ = std::move(rhs.vtable_);
    if (!rhs.is<void>()) {
        if (vtable_.move) {
            vtable_.move(data_, std::move(rhs.data_));
        }
    }
    // Reset rhs to void state.
    rhs.vtable_.ctype = &typeid(void);
    rhs.vtable_.typefunc = &type_of<void>;
    rhs.vtable_.copy = nullptr;
    rhs.vtable_.move = nullptr;
    rhs.vtable_.destroy = nullptr;
    rhs.vtable_.ptr = nullptr;
}

} // namespace arp::reflection

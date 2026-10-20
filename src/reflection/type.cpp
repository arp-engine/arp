// type.cpp -- typeinfo non-template implementation.

#include "reflection/type.h"

#include "reflection/class_info.h"

namespace arp::reflection {

typeinfo::typeinfo(const class_info* cls, std::uint64_t mask) : mask_(mask), cls_(cls)
{}

bool typeinfo::is_array() const
{
    return is_type(type_trait_mask::is_array);
}

bool typeinfo::is_class() const
{
    return is_type(type_trait_mask::is_class);
}

bool typeinfo::is_enum() const
{
    return is_type(type_trait_mask::is_enum);
}

bool typeinfo::is_pointer() const
{
    return is_type(type_trait_mask::is_pointer);
}

bool typeinfo::is_function_pointer() const
{
    return is_type(type_trait_mask::is_function_pointer);
}

bool typeinfo::is_member_object_pointer() const
{
    return is_type(type_trait_mask::is_member_object_pointer);
}

bool typeinfo::is_member_function_pointer() const
{
    return is_type(type_trait_mask::is_member_function_pointer);
}

bool typeinfo::is_lvalue_reference() const
{
    return is_type(type_trait_mask::is_lvalue_reference);
}

bool typeinfo::is_rvalue_reference() const
{
    return is_type(type_trait_mask::is_rvalue_reference);
}

bool typeinfo::is_reference() const
{
    return is_type(type_trait_mask::is_reference);
}

bool typeinfo::is_sequential_container() const
{
    return is_type(type_trait_mask::is_sequential_container);
}

bool typeinfo::is_map_container() const
{
    return is_type(type_trait_mask::is_map_container);
}

bool typeinfo::is_wrapper() const
{
    return is_type(type_trait_mask::is_wrapper);
}

bool typeinfo::is_type(type_trait_mask mask) const
{
    return mask_.test(static_cast<std::size_t>(mask));
}

std::size_t typeinfo::sizeof_() const
{
    if (is_pointer()) {
        return sizeof(void*);
    }
    if (is_function_pointer()) {
        return 0;
    }
    return cls_ != nullptr ? cls_->size() : 0;
}

std::size_t typeinfo::alignof_() const
{
    if (is_pointer()) {
        return alignof(void*);
    }
    if (is_function_pointer()) {
        return 0;
    }
    return cls_ != nullptr ? cls_->alignment() : 0;
}

bool typeinfo::operator==(const typeinfo& rhs) const
{
    return mask_ == rhs.mask_ && cls_ == rhs.cls_;
}

} // namespace arp::reflection

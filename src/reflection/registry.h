#pragma once

// registry -- mirrors ae::hybrid/registry.h, adapted for arp::reflection.
//
// registration::class_<C> is the user-facing API for registering types.
// It wraps class_info and provides method/property/construct/destruct/inherit.
//
// arp extensions: indirect property support (pointerSetter, indirectGetter,
// indirectSetter) via indirectValueTraits.

#include "core/api.h"
#include "core/memory/cpu_allocator.h"
#include "reflection/class_info.h"
#include "reflection/db.h"
#include "reflection/enum.h"
#include "reflection/function.h"
#include "reflection/internal/collection_access.h"
#include "reflection/object.h"
#include "reflection/property.h"
#include "reflection/type.h"

#include <memory>
#include <type_traits>
#include <utility>

namespace arp::reflection::registration {

struct none_struct {};

template <typename C> class class_ {
public:
    class_() : cls_(detail::registrationClassOf<C>()) {}

    explicit class_(NameView name) : cls_(detail::registrationClassOf<C>())
    {
        if (cls_ != nullptr && !name.empty()) {
            cls_->set_name(name);
        }
    }

    class_& def(NameView name, const object& value)
    {
        if (cls_ != nullptr)
            cls_->def(name, value);
        return *this;
    }

    class_info* info() const { return cls_; }

    class_& reference(NameView name, ReferenceKind kind, NameView target, bool required = true)
    {
        auto value = cls_->get_field(name);
        if (value.template is<reflection::property>())
            value.template cast<reflection::property&>().setReference(kind, target, required);
        return *this;
    }

    class_& def_property(NameView name,
        const typeinfo* type,
        const object& getter,
        const object& setter,
        const object& pointerSetter,
        const object& indirectGetter,
        const object& indirectSetter,
        const class_info* indirectClass,
        bool indirectAllowDerived,
        bool indirectResultOwned,
        NameView doc,
        std::uint8_t hints)
    {
        if (cls_ != nullptr) {
            cls_->def_property(name,
                type,
                getter,
                setter,
                pointerSetter,
                indirectGetter,
                indirectSetter,
                indirectClass,
                indirectAllowDerived,
                indirectResultOwned,
                doc,
                hints);
        }
        return *this;
    }

    class_& def_property(NameView name,
        const typeinfo* type,
        const object& getter,
        const object& setter,
        NameView doc,
        std::uint8_t hints)
    {
        if (cls_ != nullptr)
            cls_->def_property(name, type, getter, setter, doc, hints);
        return *this;
    }

    template <typename Getter, typename Setter>
    class_& accessor_property(
        NameView name, Getter&& getter, Setter&& setter, NameView doc = {}, std::uint8_t hints = 0)
    {
        if (cls_ == nullptr)
            return *this;
        using GetterResult = std::decay_t<std::invoke_result_t<Getter, C*>>;
        detail::registerCollectionAccess<GetterResult>();
        return def_property(name,
            type_of<GetterResult>(),
            object(function(std::forward<Getter>(getter))),
            object(function(std::forward<Setter>(setter))),
            doc,
            hints);
    }

    template <typename F, typename... Defaults>
    class_& method(NameView name, F&& func, Defaults&&... defaults)
    {
        if (cls_ == nullptr)
            return *this;
        function reflected(name, std::forward<F>(func));
        reflected.set_default_arguments(std::forward<Defaults>(defaults)...);
        return def(name, object(std::move(reflected)));
    }

    template <typename... Args> class_& construct()
    {
        if (cls_ == nullptr)
            return *this;
        method("__new__", [](const Args&... args) -> object {
            return object(C(args...));
        });
        method("__init__", [](void* ptr, const Args&... args) {
            using Allocator = std::pmr::polymorphic_allocator<C>;
            Allocator allocator{&core::defaultCpuMemoryResource()};
            std::allocator_traits<Allocator>::construct(allocator, static_cast<C*>(ptr), args...);
        });
        return *this;
    }

    class_& destruct()
    {
        if (cls_ == nullptr)
            return *this;
        return method("__del__", [](void* ptr) {
            using Allocator = std::pmr::polymorphic_allocator<C>;
            Allocator allocator{&core::defaultCpuMemoryResource()};
            std::allocator_traits<Allocator>::destroy(allocator, static_cast<C*>(ptr));
        });
    }

    template <typename T, typename D = std::conditional_t<std::is_class_v<C>, C, none_struct>>
    class_& property(NameView name, T D::* member, NameView doc = {}, std::uint8_t hints = 0)
    {
        if (cls_ == nullptr)
            return *this;
        using Indirect = detail::indirectValueTraits<T>;
        detail::registerCollectionAccess<T>();
        object getter;
        object setter;
        object pointerSetter;
        object indirectGetter;
        object indirectSetter;
        const class_info* indirectClass = nullptr;
        bool indirectAllowDerived = false;
        bool indirectResultOwned = false;
        if constexpr (Indirect::enabled) {
            using Element = typename Indirect::element_type;
            getter = object(function([member](C* value) -> object {
                auto* ptr = [&]() -> Element* {
                    if constexpr (std::is_pointer_v<T>) {
                        return value->*member;
                    } else {
                        return (value->*member).get();
                    }
                }();
                return ptr != nullptr ? object(ptr) : object::undefined();
            }));
            indirectGetter = getter;
            indirectSetter = object(function([member](C* value, const object& rhs) {
                if (rhs.is_undefined()) {
                    if constexpr (std::is_pointer_v<T>) {
                        value->*member = nullptr;
                    } else {
                        (value->*member).reset();
                    }
                    return object::undefined();
                }
                auto* ptr = rhs.template cast<Element*>();
                if (ptr == nullptr) {
                    return object::undefined();
                }
                if constexpr (std::is_pointer_v<T>) {
                    value->*member = ptr;
                } else if constexpr (std::is_same_v<T, std::shared_ptr<Element>>) {
                    value->*member = core::makeShared<Element>(*ptr);
                } else {
                    value->*member = core::makeUnique<Element>(*ptr);
                }
                return object::undefined();
            }));
            setter = indirectSetter;
            indirectClass = class_of<Element>();
            indirectAllowDerived = Indirect::allowDerived;
            indirectResultOwned = Indirect::resultOwned;
        } else {
            getter = object(function([member](C* value) -> const T& {
                return value->*member;
            }));
            if constexpr (std::is_copy_assignable_v<T>) {
                setter = object(function([member](C* value, const T& rhs) {
                    value->*member = rhs;
                }));
            }
        }
        if constexpr (std::is_pointer_v<T> && !detail::is_function_pointer_v<T>) {
            pointerSetter = object(function([member](C* value, void* rhs) {
                value->*member = static_cast<T>(rhs);
            }));
        }
        return def_property(name,
            type_of<T>(),
            getter,
            setter,
            pointerSetter,
            indirectGetter,
            indirectSetter,
            indirectClass,
            indirectAllowDerived,
            indirectResultOwned,
            doc,
            hints);
    }

    template <typename T, typename D = std::conditional_t<std::is_class_v<C>, C, none_struct>>
    class_& property(NameView name, const T D::* member, NameView doc = {}, std::uint8_t hints = 0)
    {
        if (cls_ == nullptr)
            return *this;
        object getter(function([member](C* value) -> const T& {
            return value->*member;
        }));
        return def_property(name, type_of<const T>(), getter, object::undefined(), doc, hints);
    }

    template <typename Enum>
    class_& constant(NameView name, std::initializer_list<enum_pair<Enum>> values)
    {
        if (cls_ == nullptr)
            return *this;
        enumeration members(name, values);
        type_of<Enum>()->setEnumValues(members.values());
        type_of<const Enum>()->setEnumValues(members.values());
        return def(name, object(std::move(members)));
    }

    template <typename Enum>
    class_& constant(NameView name, const core::pmr::Vector<enum_pair<Enum>>& values)
    {
        if (cls_ == nullptr)
            return *this;
        enumeration members(name, values);
        type_of<Enum>()->setEnumValues(members.values());
        type_of<const Enum>()->setEnumValues(members.values());
        return def(name, object(std::move(members)));
    }

    template <typename B> class_& inherit()
    {
        if (cls_ != nullptr)
            cls_->inherit(class_of<B>(), detail::upcastImpl<B, C>);
        return *this;
    }

private:
    class_info* cls_ = nullptr;
};

} // namespace arp::reflection::registration

#pragma once

#include "reflection/object.h"
#include "reflection/type.h"

#include <optional>

namespace arp::reflection::detail {

template <class T> struct OptionalElement {
    static constexpr bool enabled = false;
};

template <class T> struct OptionalElement<std::optional<T>> {
    static constexpr bool enabled = true;
    using type = T;
};

template <class T> void registerCollectionAccess()
{
    using Raw = raw_type_t<T>;
    if constexpr (isSequentialContainer<Raw>::value && !std::is_same_v<Raw, hlist>) {
        using Element = typename Raw::value_type;
        registerCollectionAccess<Element>();
        static const CollectionAccess access{false,
            false,
            [] {
                return object(Raw{&core::defaultCpuMemoryResource()});
            },
            [] {
                return type_of<Element>();
            },
            nullptr,
            [](const object& value) {
                return value.cast<const Raw&>().size();
            },
            [](const object& value, std::size_t index) {
                return object(value.cast<const Raw&>()[index]);
            },
            nullptr,
            [](object& value, const object&, const object& element) {
                value.cast<Raw&>().push_back(element.cast<Element>());
                return true;
            }};
        type_of<T>()->setCollection(&access);
        type_of<const Raw>()->setCollection(&access);
    } else if constexpr (isMapContainer<Raw>::value) {
        using Key = typename Raw::key_type;
        using Element = typename Raw::mapped_type;
        registerCollectionAccess<Key>();
        registerCollectionAccess<Element>();
        static const CollectionAccess access{true,
            false,
            [] {
                return object(Raw{&core::defaultCpuMemoryResource()});
            },
            [] {
                return type_of<Element>();
            },
            [] {
                return type_of<Key>();
            },
            [](const object& value) {
                return value.cast<const Raw&>().size();
            },
            [](const object& value, std::size_t index) {
                auto it = value.cast<const Raw&>().begin();
                std::advance(it, index);
                return object(it->second);
            },
            [](const object& value, std::size_t index) {
                auto it = value.cast<const Raw&>().begin();
                std::advance(it, index);
                return object(it->first);
            },
            [](object& value, const object& key, const object& element) {
                return value.cast<Raw&>().emplace(key.cast<Key>(), element.cast<Element>()).second;
            }};
        type_of<T>()->setCollection(&access);
        type_of<const Raw>()->setCollection(&access);
    } else if constexpr (OptionalElement<Raw>::enabled) {
        using Element = typename OptionalElement<Raw>::type;
        registerCollectionAccess<Element>();
        static const CollectionAccess access{false,
            true,
            [] {
                return object(Raw{});
            },
            [] {
                return type_of<Element>();
            },
            nullptr,
            [](const object& value) -> std::size_t {
                return value.cast<const Raw&>().has_value();
            },
            [](const object& value, std::size_t) {
                return object(*value.cast<const Raw&>());
            },
            nullptr,
            [](object& value, const object&, const object& element) {
                value.cast<Raw&>() = element.cast<Element>();
                return true;
            }};
        type_of<T>()->setCollection(&access);
        type_of<const Raw>()->setCollection(&access);
    }
}
} // namespace arp::reflection::detail

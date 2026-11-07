#pragma once
#include "core/encoding/base64.h"
#include "core/memory/cpu_allocator.h"

#include <array>
#include <charconv>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <type_traits>
#include <utility>

namespace arp::core {
// Strict field access shared by text authoring and binary value records.
template <class T, class = void> struct ValueHasUnsigned : std::false_type {};

template <class T>
struct ValueHasUnsigned<T, std::void_t<decltype(std::declval<T>().is_unsigned())>>
    : std::true_type {};

template <class Node, class Error> class ValueContract {
public:
    static void onlyKeys(const Node& node, std::initializer_list<StringView> keys)
    {
        if (!node.is_mapping())
            fail();
        for (const auto& item : node.template get_value_ref<const typename Node::mapping_type&>()) {
            const auto name = text(item.first);
            bool allowed = false;
            for (const auto key : keys)
                allowed = allowed || name == key;
            if (!allowed)
                fail();
        }
    }

    static const Node& required(const Node& node, const char* key)
    {
        if (!node.is_mapping() || !node.contains(key))
            fail();
        return node[key];
    }

    static pmr::String text(const Node& node)
    {
        if (!node.is_string())
            fail();
        const auto& value = node.template get_value_ref<const typename Node::string_type&>();
        return pmr::String(value.data(), value.size(), &defaultCpuMemoryResource());
    }

    static pmr::String text(const Node& node, const char* key) { return text(required(node, key)); }

    static std::uint64_t uint64(const Node& node)
    {
        if constexpr (ValueHasUnsigned<Node>::value)
            if (node.is_unsigned())
                return node.template get_value<std::uint64_t>();
        if (!node.is_integer())
            fail();
        const auto value = node.template get_value<typename Node::integer_type>();
        if (value < 0)
            fail();
        return static_cast<std::uint64_t>(value);
    }

    static std::uint32_t uint32(const Node& node, const char* key)
    {
        const auto value = uint64(required(node, key));
        if (value > UINT32_MAX)
            fail();
        return static_cast<std::uint32_t>(value);
    }

    static std::int32_t int32(const Node& node, const char* key)
    {
        const auto& value = required(node, key);
        if (!value.is_integer())
            fail();
        const auto number = value.template get_value<typename Node::integer_type>();
        if (number < INT32_MIN || number > INT32_MAX)
            fail();
        return static_cast<std::int32_t>(number);
    }

    static double number(const Node& node, const char* key) { return number(required(node, key)); }

    static double number(const Node& value)
    {
        if (!value.is_integer() && !value.is_float_number())
            fail();
        const double result = value.template get_value<double>();
        if (!std::isfinite(result))
            fail();
        return result;
    }

    template <std::size_t N>
    static std::array<float, N> floatArray(const Node& node, const char* key)
    {
        const auto& values = sequence(node, key);
        if (values.size() != N)
            fail();
        std::array<float, N> result{};
        for (std::size_t i = 0; i < N; ++i) {
            const auto value = number(values[i]);
            if (value < -std::numeric_limits<float>::max()
                || value > std::numeric_limits<float>::max())
                fail();
            result[i] = static_cast<float>(value);
            if (value != 0 && result[i] == 0)
                fail();
        }
        return result;
    }

    template <std::size_t N>
    static std::array<std::uint8_t, N> byteArray(const Node& node, const char* key)
    {
        const auto& values = sequence(node, key);
        if (values.size() != N)
            fail();
        std::array<std::uint8_t, N> result{};
        for (std::size_t i = 0; i < N; ++i) {
            const auto value = uint64(values[i]);
            if (value > UINT8_MAX)
                fail();
            result[i] = static_cast<std::uint8_t>(value);
        }
        return result;
    }

    static std::uint64_t hex64(const Node& node, const char* key)
    {
        const auto value = text(node, key);
        if (value.size() != 16u)
            fail();
        std::uint64_t result = 0;
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result, 16);
        if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
            fail();
        return result;
    }

    static bool boolean(const Node& node, const char* key)
    {
        const auto& value = required(node, key);
        if (!value.is_boolean())
            fail();
        return value.template get_value<bool>();
    }

    static const typename Node::sequence_type& sequence(const Node& node, const char* key)
    {
        const auto& value = required(node, key);
        if (!value.is_sequence())
            fail();
        return value.template get_value_ref<const typename Node::sequence_type&>();
    }

    [[noreturn]] static void fail() { throw Error{Error::diagnosticId}; }
};
} // namespace arp::core

#pragma once

#include "core/base64.h"
#include "core/cpu_allocator.h"
#include "core/number_format.h"

#include <array>
#include <charconv>
#include <cmath>
#include <fkYAML/node.hpp>
#include <limits>
#include <utility>

namespace arp::core {

// Strict, reusable accessors for versioned YAML contracts. Missing and wrongly
// typed fields never acquire guessed defaults. The third-party parser is used
// only during bounded preparation; owned results use the core allocator.
struct YamlContractError {
    const char* diagnostic;
};

class YamlContract final {
public:
    using Node = fkyaml::node;

    static Node parseSingleDocument(StringView source)
    {
        auto documents = Node::deserialize_docs(source.begin(), source.end());
        if (documents.size() != 1u)
            fail();
        return std::move(documents.front());
    }

    // fkYAML's block writer emits an empty string as an empty scalar (null on
    // reload). Keep scalar escaping in the parser library, but preserve empty
    // strings explicitly in this common, deterministic document writer.
    static pmr::String serialize(const Node& node)
    {
        pmr::String result{&defaultCpuMemoryResource()};
        appendDocument(node, result, 0);
        return result;
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
        const auto& value = node.get_value_ref<const Node::string_type&>();
        return pmr::String(value.data(), value.size(), &defaultCpuMemoryResource());
    }

    static pmr::String text(const Node& node, const char* key) { return text(required(node, key)); }

    static std::uint64_t uint64(const Node& node)
    {
        if (!node.is_integer())
            fail();
        const auto value = node.get_value<Node::integer_type>();
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
        const auto number = value.get_value<Node::integer_type>();
        if (number < INT32_MIN || number > INT32_MAX)
            fail();
        return static_cast<std::int32_t>(number);
    }

    static double number(const Node& node, const char* key) { return number(required(node, key)); }

    static double number(const Node& value)
    {
        if (!value.is_integer() && !value.is_float_number())
            fail();
        const double result = value.get_value<double>();
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
        return value.get_value<bool>();
    }

    static const Node::sequence_type& sequence(const Node& node, const char* key)
    {
        const auto& value = required(node, key);
        if (!value.is_sequence())
            fail();
        return value.get_value_ref<const Node::sequence_type&>();
    }

    [[noreturn]] static void fail() { throw YamlContractError{"YAML-CONTRACT-FIELD-INVALID"}; }

private:
    static void appendScalar(const Node& node, pmr::String& output)
    {
        if (node.is_float_number()) {
            if (!appendFloating(output, node.get_value<Node::float_number_type>()))
                fail();
        } else if (node.is_string() && node.get_value_ref<const Node::string_type&>().empty())
            output += "\"\"";
        else
            output += Node::serialize(node);
    }

    static void appendDocument(const Node& node, pmr::String& output, std::size_t indent)
    {
        if (node.is_scalar() || node.empty()) {
            output.append(indent, ' ');
            appendScalar(node, output);
            if (output.empty() || output.back() != '\n')
                output += '\n';
            return;
        }
        auto appendValue = [&](const Node& value) {
            if (value.is_scalar() || value.empty()) {
                output += ' ';
                appendScalar(value, output);
                if (output.back() != '\n')
                    output += '\n';
            } else {
                output += '\n';
                appendDocument(value, output, indent + 2);
            }
        };
        if (node.is_mapping()) {
            for (const auto& entry : node.get_value_ref<const Node::mapping_type&>()) {
                if (!entry.first.is_scalar())
                    fail();
                output.append(indent, ' ');
                appendScalar(entry.first, output);
                output += ':';
                appendValue(entry.second);
            }
        } else {
            for (const auto& entry : node.get_value_ref<const Node::sequence_type&>()) {
                output.append(indent, ' ');
                output += '-';
                appendValue(entry);
            }
        }
    }
};

} // namespace arp::core

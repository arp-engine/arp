#pragma once

#include "core/encoding/base64.h"
#include "core/encoding/value_contract.h"
#include "core/memory/cpu_allocator.h"
#include "core/text/number_format.h"

#include <array>
#include <charconv>
#include <cmath>
#include <fkYAML/node.hpp>
#include <initializer_list>
#include <limits>
#include <utility>

namespace arp::core {

// Strict, reusable accessors for versioned YAML contracts. Missing and wrongly
// typed fields never acquire guessed defaults. The third-party parser is used
// only during bounded preparation; owned results use the core allocator.
struct YamlContractError {
    static constexpr const char* diagnosticId = "YAML-CONTRACT-FIELD-INVALID";
    const char* diagnostic;
};

class YamlContract final : public ValueContract<fkyaml::node, YamlContractError> {
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

private:
    static void appendScalar(const Node& node, pmr::String& output, bool mappingKey = false)
    {
        if (node.is_float_number()) {
            if (!appendFloating(output, node.get_value<Node::float_number_type>()))
                fail();
        } else if (node.is_string() && !mappingKey) {
            const auto& value = node.get_value_ref<const Node::string_type&>();
            bool escaped = false;
            const auto encoded = fkyaml::detail::yaml_escaper::escape(
                value.data(), value.data() + value.size(), escaped);
            output += '\"';
            output.append(encoded.data(), encoded.size());
            output += '\"';
        } else
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
                appendScalar(entry.first, output, true);
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

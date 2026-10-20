#pragma once
#include "core/callback.h"
#include "core/yaml_contract.h"
#include "scriptvm/compile/ast.h"
#include "scriptvm/runtime/value.h"

namespace arp::scriptvm {
// A bounded typed-value codec shared by component/persistence tooling. Class
// reference semantics belong to the caller, never to YAML shape heuristics.
class ScriptValueYaml final {
public:
    using Node = core::YamlContract::Node;
    using EncodeReference = core::SharedCallback<bool(const TypeNode&, const Value&, Node&)>;
    using DecodeReference = core::SharedCallback<bool(const TypeNode&, const Node&, Value&)>;

    ScriptValueYaml(std::size_t values, std::size_t depth) : remaining_(values), depth_(depth) {}

    // Explicit YAML node kinds map to the corresponding script value kind.
    // This is for schema-validated configuration, never reference restoration.
    bool decodeData(const Node& node, Value& value)
    {
        if (!take())
            return false;
        if (node.is_null()) {
            value = makeNil();
            return true;
        }
        if (node.is_boolean()) {
            value = makeBool(node.get_value<bool>());
            return true;
        }
        if (node.is_integer()) {
            auto n = node.get_value<Node::integer_type>();
            value = n >= INT32_MIN && n <= INT32_MAX ? makeInt(int(n)) : makeInt64(n);
            return true;
        }
        if (node.is_float_number()) {
            auto n = node.get_value<double>();
            if (!std::isfinite(n))
                return false;
            value = makeFloat(n);
            return true;
        }
        if (node.is_string()) {
            value = makeString(core::YamlContract::text(node));
            return true;
        }
        if (!depth_)
            return false;
        --depth_;
        if (node.is_sequence()) {
            reflection::hlist list;
            const auto& children = node.get_value_ref<const Node::sequence_type&>();
            if (children.size() > remaining_)
                return false;
            list.reserve(children.size());
            for (const auto& child : children) {
                Value item;
                if (!decodeData(child, item))
                    return false;
                list.push_back(std::move(item));
            }
            value = Value{std::move(list)};
        } else if (node.is_mapping()) {
            reflection::hdict dict;
            for (const auto& pair : node.get_value_ref<const Node::mapping_type&>()) {
                Value item;
                if (!decodeData(pair.second, item))
                    return false;
                dict.emplace(makeString(core::YamlContract::text(pair.first)), std::move(item));
            }
            value = Value{std::move(dict)};
        } else
            return false;
        ++depth_;
        return true;
    }

    bool encode(
        const TypeNode& type, const Value& value, Node& node, const EncodeReference& reference)
    {
        if (!take())
            return false;
        if (type.kind == TypeKind::Optional) {
            if (isNil(value)) {
                node = Node();
                return true;
            }
            return type.elem && encode(*type.elem, value, node, reference);
        }
        if (type.kind == TypeKind::Class)
            return reference(type, value, node);
        if (type.kind == TypeKind::List) {
            if (!type.elem || !value.is<reflection::hlist>() || !depth_)
                return false;
            --depth_;
            node = Node::sequence();
            for (const auto& item : value.cast<const reflection::hlist&>()) {
                Node child;
                if (!encode(*type.elem, item, child, reference))
                    return false;
                node.get_value_ref<Node::sequence_type&>().push_back(std::move(child));
            }
            ++depth_;
            return true;
        }
        if (type.name == "int" && value.is<int>()) {
            node = value.cast<int>();
            return true;
        }
        if (type.name == "int64" && value.is<std::int64_t>()) {
            node = value.cast<std::int64_t>();
            return true;
        }
        if (type.name == "float" && value.is<double>() && std::isfinite(value.cast<double>())) {
            node = value.cast<double>();
            return true;
        }
        if (type.name == "bool" && value.is<bool>()) {
            node = value.cast<bool>();
            return true;
        }
        if (type.name == "string" && value.is<core::pmr::String>()) {
            node = value.cast<const core::pmr::String&>().c_str();
            return true;
        }
        return false;
    }

    bool decode(
        const TypeNode& type, const Node& node, Value& value, const DecodeReference& reference)
    {
        if (!take())
            return false;
        if (type.kind == TypeKind::Optional) {
            if (node.is_null()) {
                value = makeNil();
                return true;
            }
            return type.elem && decode(*type.elem, node, value, reference);
        }
        if (type.kind == TypeKind::Class)
            return reference(type, node, value);
        if (type.kind == TypeKind::List) {
            if (!type.elem || !node.is_sequence() || !depth_)
                return false;
            --depth_;
            reflection::hlist list;
            const auto& children = node.get_value_ref<const Node::sequence_type&>();
            if (children.size() > remaining_)
                return false;
            list.reserve(children.size());
            for (const auto& child : children) {
                Value item;
                if (!decode(*type.elem, child, item, reference))
                    return false;
                list.push_back(std::move(item));
            }
            ++depth_;
            value = Value{std::move(list)};
            return true;
        }
        if (type.name == "int" && node.is_integer()) {
            const auto n = node.get_value<Node::integer_type>();
            if (n < INT32_MIN || n > INT32_MAX)
                return false;
            value = makeInt(int(n));
            return true;
        }
        if (type.name == "int64" && node.is_integer()) {
            value = makeInt64(node.get_value<Node::integer_type>());
            return true;
        }
        if (type.name == "float" && (node.is_integer() || node.is_float_number())) {
            const double n = node.get_value<double>();
            if (!std::isfinite(n))
                return false;
            value = makeFloat(n);
            return true;
        }
        if (type.name == "bool" && node.is_boolean()) {
            value = makeBool(node.get_value<bool>());
            return true;
        }
        if (type.name == "string" && node.is_string()) {
            value = makeString(core::YamlContract::text(node));
            return true;
        }
        return false;
    }

private:
    bool take()
    {
        if (!remaining_)
            return false;
        --remaining_;
        return true;
    }

    std::size_t remaining_, depth_;
};
} // namespace arp::scriptvm

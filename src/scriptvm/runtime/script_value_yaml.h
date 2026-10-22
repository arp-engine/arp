#pragma once
#include "core/concurrency/callback.h"
#include "core/encoding/yaml_contract.h"
#include "scriptvm/compile/ast.h"
#include "scriptvm/runtime/serialization_descriptor.h"
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
        const auto descriptor = serializationDescriptor(type);
        if (!descriptor)
            return false;
        ReferenceContext context(reference, {});
        reflection::SerializationArchive archive(
            {reflection::SerializationProfile::Authoring, depth_, remaining_, remaining_},
            &context);
        const bool ok = archive.writeValue(*descriptor, value, node);
        remaining_ -= std::min(remaining_, archive.valuesConsumed());
        return ok;
    }

    bool decode(
        const TypeNode& type, const Node& node, Value& value, const DecodeReference& reference)
    {
        const auto descriptor = serializationDescriptor(type);
        if (!descriptor)
            return false;
        ReferenceContext context({}, reference);
        reflection::SerializationArchive archive(
            {reflection::SerializationProfile::Authoring, depth_, remaining_, remaining_},
            &context);
        const bool ok = archive.readValue(*descriptor, node, value);
        remaining_ -= std::min(remaining_, archive.valuesConsumed());
        return ok;
    }

private:
    class ReferenceContext final : public reflection::SerializationContext {
    public:
        ReferenceContext(EncodeReference encode, DecodeReference decode)
            : encode_(std::move(encode)), decode_(std::move(decode))
        {}

        bool writeReference(
            const reflection::property&, const Value&, Node&, core::pmr::String&) const override
        {
            return false;
        }

        bool readReference(
            const reflection::property&, const Node&, Value&, core::pmr::String&) const override
        {
            return false;
        }

        bool writeReference(const Descriptor& descriptor,
            const Value& value,
            Node& node,
            core::pmr::String&) const override
        {
            TypeNode type;
            type.kind = TypeKind::Class;
            type.name = descriptor.referenceType;
            type.reflectedClass = descriptor.referenceClass;
            return encode_ && encode_(type, value, node);
        }

        bool readReference(const Descriptor& descriptor,
            const Node& node,
            Value& value,
            core::pmr::String&) const override
        {
            TypeNode type;
            type.kind = TypeKind::Class;
            type.name = descriptor.referenceType;
            type.reflectedClass = descriptor.referenceClass;
            return decode_ && decode_(type, node, value);
        }

    private:
        EncodeReference encode_;
        DecodeReference decode_;
    };

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

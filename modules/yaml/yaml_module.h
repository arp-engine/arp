#pragma once

#include "core/memory/cpu_allocator.h"
#include "reflection/reflection.h"
#include "reflection/reflection_macros.h"
#include "scriptvm/runtime/value.h"

#include <cstdint>
#include <fkYAML/node.hpp>
#include <memory>

namespace arp::scriptvm::plugins::yaml {

using YamlNativeNode = fkyaml::node;
using arp::core::pmr::String;
using arp::reflection::hlist;

ASTRUCT(Name = "YamlDocument")

struct YamlNode final {
    std::shared_ptr<YamlNativeNode> owner;
    std::shared_ptr<YamlNativeNode> documentOwner;
    APROP()
    bool opaque = true;

    YamlNode();
    explicit YamlNode(YamlNativeNode node);
    YamlNode(YamlNativeNode node, std::shared_ptr<YamlNativeNode> document);

    YamlNativeNode& native() { return *owner; }

    const YamlNativeNode& native() const { return *owner; }
    AFUNC(Name = "kind")
    String kind() const;
    AFUNC(Name = "tag")
    String tag() const;
    AFUNC(Name = "string_value")
    String stringValue() const;
    AFUNC(Name = "anchor")
    String anchor() const;
    AFUNC(Name = "size")
    int size() const;
    AFUNC(Name = "get")
    YamlNode get(const String& key) const;
    AFUNC(Name = "at")
    YamlNode at(int index) const;
    AFUNC(Name = "keys")
    hlist keys() const;
    AFUNC(Name = "scalar")
    Value scalar() const;
    AFUNC(Name = "is_string")
    bool isString() const;
    AFUNC(Name = "is_integer")
    bool isInteger() const;
    AFUNC(Name = "is_float")
    bool isFloat() const;
    AFUNC(Name = "is_boolean")
    bool isBoolean() const;
    AFUNC(Name = "integer_value")
    int integerValue() const;
    AFUNC(Name = "float_value")
    double floatValue() const;
    AFUNC(Name = "boolean_value")
    bool booleanValue() const;
    AFUNC(Name = "set")
    void set(const String& key, const YamlNode& child);
    AFUNC(Name = "append")
    void append(const YamlNode& child);
    AFUNC(Name = "set_tag")
    void setTag(const String& valueText);
    AFUNC(Name = "set_anchor")
    void setAnchor(const String& valueText);
};

ASTRUCT()

struct YamlParseResult final {
    APROP()
    bool success = false;
    APROP()
    hlist documents = hlist(hlist::allocator_type(&arp::core::defaultCpuMemoryResource()));
    APROP()
    String diagnostics{&arp::core::defaultCpuMemoryResource()};
};

ASTRUCT()

struct YamlTextResult final {
    APROP()
    bool success = false;
    APROP()
    String text{&arp::core::defaultCpuMemoryResource()};
    APROP()
    String diagnostics{&arp::core::defaultCpuMemoryResource()};
};

AFUNC(Name = "parse")
YamlParseResult parseText(const String& text);

AFUNC(Name = "stringify")
YamlTextResult stringifyNode(const YamlNode& node);

AFUNC(Name = "is_node")
bool isNode(const YamlNode& node);

AFUNC(Name = "kind")
String nodeKind(const YamlNode& node);

AFUNC(Name = "kind_code")
int nodeKindCode(const YamlNode& node);

AFUNC(Name = "document_at")
YamlNode documentAt(const YamlParseResult& result, int index);

AFUNC(Name = "first_document")
YamlNode firstDocument(const String& text);

AFUNC(Name = "mapping")
YamlNode mapping();

AFUNC(Name = "sequence")
YamlNode sequence();

AFUNC(Name = "string")
YamlNode stringNode(const String& text);

AFUNC(Name = "integer")
YamlNode integerNode(std::int64_t value);

AFUNC(Name = "int")
YamlNode intNode(int value);

AFUNC(Name = "float")
YamlNode floatNode(double value);

AFUNC(Name = "number_float")
YamlNode numberFloatNode(double value);

AFUNC(Name = "number")
YamlNode numberNode(const String& value);

AFUNC(Name = "boolean")
YamlNode booleanNode(bool value);

} // namespace arp::scriptvm::plugins::yaml

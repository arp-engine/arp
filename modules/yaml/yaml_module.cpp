#include "yaml_module.h"

#include "core/cpu_allocator.h"
#include "scriptvm/runtime/cpp_plugin_api.h"
#include "scriptvm/runtime/value.h"

#include <cstdint>
#include <cstdlib>
#include <fkYAML/node.hpp>
#include <stdexcept>
#include <string>
#include <utility>

namespace arp::scriptvm::plugins::yaml {

using arp::core::pmr::String;
using arp::reflection::hlist;
using arp::scriptvm::Value;

String normalizeUnityDocumentHeaders(const String& text)
{
    String normalized{&arp::core::defaultCpuMemoryResource()};
    std::size_t lineStart = 0;
    while (lineStart < text.size()) {
        const std::size_t lineEnd = text.find('\n', lineStart);
        std::size_t contentEnd = lineEnd == String::npos ? text.size() : lineEnd;
        if (contentEnd > lineStart && text[contentEnd - 1u] == '\r') {
            --contentEnd;
        }
        const auto line = std::string_view(text.data() + lineStart, contentEnd - lineStart);
        std::size_t cursor = 0;
        while (cursor < line.size() && (line[cursor] == ' ' || line[cursor] == '\t')) {
            ++cursor;
        }
        bool isHeader = line.substr(cursor, 3u) == "---";
        cursor += isHeader ? 3u : 0u;
        while (isHeader && cursor < line.size() && (line[cursor] == ' ' || line[cursor] == '\t')) {
            ++cursor;
        }

        std::string_view unityType;
        if (isHeader && line.substr(cursor, 3u) == "!u!") {
            const std::size_t typeStart = cursor + 3u;
            cursor = typeStart;
            while (cursor < line.size() && line[cursor] >= '0' && line[cursor] <= '9') {
                ++cursor;
            }
            if (cursor == typeStart) {
                isHeader = false;
            } else {
                unityType = line.substr(typeStart, cursor - typeStart);
            }
            while (
                isHeader && cursor < line.size() && (line[cursor] == ' ' || line[cursor] == '\t')) {
                ++cursor;
            }
        }

        const std::size_t fileIdStart =
            cursor + (isHeader && cursor < line.size() && line[cursor] == '&' ? 1u : 0u);
        if (isHeader && fileIdStart != cursor && fileIdStart < line.size()) {
            cursor = fileIdStart;
            while (cursor < line.size() && line[cursor] >= '0' && line[cursor] <= '9') {
                ++cursor;
            }
            if (cursor == fileIdStart) {
                isHeader = false;
            }
            while (
                isHeader && cursor < line.size() && (line[cursor] == ' ' || line[cursor] == '\t')) {
                ++cursor;
            }
        } else {
            isHeader = false;
        }

        if (isHeader && cursor == line.size()) {
            const auto fileIdPrefix = std::string_view("__arp_unity_fileID: ");
            normalized.append("---\n", 4u);
            normalized.append(fileIdPrefix.data(), fileIdPrefix.size());
            normalized.append(line.data() + fileIdStart, cursor - fileIdStart);
            normalized.push_back('\n');
            if (!unityType.empty()) {
                const auto typePrefix = std::string_view("__arp_unity_type: ");
                normalized.append(typePrefix.data(), typePrefix.size());
                normalized.append(unityType.data(), unityType.size());
                normalized.push_back('\n');
            }
        } else {
            normalized.append(line.data(), line.size());
            normalized.push_back('\n');
        }

        if (lineEnd == String::npos) {
            break;
        }
        lineStart = lineEnd + 1u;
    }
    return normalized;
}

YamlNode::YamlNode() : owner(arp::core::makeShared<YamlNativeNode>()), documentOwner(owner)
{}

YamlNode::YamlNode(YamlNativeNode node)
    : owner(arp::core::makeShared<YamlNativeNode>(std::move(node))), documentOwner(owner)
{}

YamlNode::YamlNode(YamlNativeNode node, std::shared_ptr<YamlNativeNode> document)
    : owner(arp::core::makeShared<YamlNativeNode>(std::move(node))),
      documentOwner(document ? std::move(document) : owner)
{}

YamlNode boxYamlNode(YamlNativeNode node)
{
    return YamlNode(std::move(node));
}

YamlNode boxYamlChild(
    const YamlNativeNode& node, const std::shared_ptr<YamlNativeNode>& documentOwner)
{
    return YamlNode(node, documentOwner);
}

String YamlNode::kind() const
{
    const auto text = fkyaml::to_string(native().get_type());
    return String(text, &arp::core::defaultCpuMemoryResource());
}

String YamlNode::tag() const
{
    return native().has_tag_name()
        ? String(native().get_tag_name(), &arp::core::defaultCpuMemoryResource())
        : String(&arp::core::defaultCpuMemoryResource());
}

String YamlNode::stringValue() const
{
    if (!native().is_string()) {
        return String(&arp::core::defaultCpuMemoryResource());
    }
    return String(native().get_value<std::string>(), &arp::core::defaultCpuMemoryResource());
}

String YamlNode::anchor() const
{
    return native().has_anchor_name()
        ? String(native().get_anchor_name(), &arp::core::defaultCpuMemoryResource())
        : String(&arp::core::defaultCpuMemoryResource());
}

int YamlNode::size() const
{
    return native().is_sequence() || native().is_mapping() ? static_cast<int>(native().size()) : 0;
}

YamlNode YamlNode::get(const String& key) const
{
    if (!native().is_mapping()) {
        return boxYamlNode(YamlNativeNode{});
    }
    const auto& map = native().get_value_ref<const YamlNativeNode::mapping_type&>();
    const auto found = map.find(YamlNativeNode(key.c_str()));
    return found == map.end() ? boxYamlNode(YamlNativeNode{})
                              : boxYamlChild(found->second, documentOwner);
}

YamlNode YamlNode::at(int index) const
{
    if (!native().is_sequence() || index < 0 || index >= static_cast<int>(native().size())) {
        return boxYamlNode(YamlNativeNode{});
    }
    return boxYamlChild(native()[static_cast<std::size_t>(index)], documentOwner);
}

hlist YamlNode::keys() const
{
    hlist result(hlist::allocator_type(&arp::core::defaultCpuMemoryResource()));
    if (!native().is_mapping()) {
        return result;
    }
    for (const auto& item : native().map_items()) {
        if (item.key().is_string()) {
            const auto key = item.key().get_value<std::string>();
            result.emplace_back(
                String(key.data(), key.size(), &arp::core::defaultCpuMemoryResource()));
        } else {
            result.emplace_back(String(
                fkyaml::node::serialize(item.key()), &arp::core::defaultCpuMemoryResource()));
        }
    }
    return result;
}

Value YamlNode::scalar() const
{
    switch (native().get_type()) {
    case fkyaml::node_type::NULL_OBJECT:
        return Value::undefined();
    case fkyaml::node_type::BOOLEAN:
        return Value{native().get_value<bool>()};
    case fkyaml::node_type::INTEGER:
        return arp::scriptvm::makeInt64(native().get_value<std::int64_t>());
    case fkyaml::node_type::FLOAT:
        return arp::scriptvm::makeFloat(native().get_value<double>());
    case fkyaml::node_type::STRING:
        return Value{
            String(native().get_value<std::string>(), &arp::core::defaultCpuMemoryResource())};
    default:
        return Value::undefined();
    }
}

bool YamlNode::isString() const
{
    return native().is_string();
}

bool YamlNode::isInteger() const
{
    return native().is_integer();
}

bool YamlNode::isFloat() const
{
    return native().get_type() == fkyaml::node_type::FLOAT;
}

bool YamlNode::isBoolean() const
{
    return native().is_boolean();
}

int YamlNode::integerValue() const
{
    return native().is_integer() ? static_cast<int>(native().get_value<std::int64_t>()) : 0;
}

double YamlNode::floatValue() const
{
    if (native().get_type() == fkyaml::node_type::FLOAT) {
        return native().get_value<double>();
    }
    if (native().is_integer()) {
        return static_cast<double>(native().get_value<std::int64_t>());
    }
    return 0.0f;
}

bool YamlNode::booleanValue() const
{
    return native().is_boolean() && native().get_value<bool>();
}

void YamlNode::set(const String& key, const YamlNode& child)
{
    if (native().is_mapping()) {
        native()[YamlNativeNode(key.c_str())] = child.native();
    }
}

void YamlNode::append(const YamlNode& child)
{
    if (native().is_sequence()) {
        native().get_value_ref<YamlNativeNode::sequence_type&>().push_back(child.native());
    }
}

void YamlNode::setTag(const String& valueText)
{
    native().add_tag_name(std::string(valueText.data(), valueText.size()));
}

void YamlNode::setAnchor(const String& valueText)
{
    native().add_anchor_name(std::string(valueText.data(), valueText.size()));
}

YamlTextResult makeTextResult(bool success, const String& text = {}, const String& diagnostics = {})
{
    YamlTextResult output;
    output.success = success;
    output.text = text;
    output.diagnostics = diagnostics;
    return output;
}

YamlParseResult parseResult(bool success, const String& diagnostics = {})
{
    YamlParseResult output;
    output.success = success;
    output.diagnostics = diagnostics;
    return output;
}

YamlParseResult parseText(const String& text)
{
    try {
        const String normalized = normalizeUnityDocumentHeaders(text);
        auto documents =
            YamlNativeNode::deserialize_docs(std::string(normalized.data(), normalized.size()));
        YamlParseResult output;
        output.success = true;
        hlist nodes(hlist::allocator_type(&arp::core::defaultCpuMemoryResource()));
        nodes.reserve(documents.size());
        for (auto& document : documents) {
            nodes.emplace_back(boxYamlNode(std::move(document)));
        }
        output.documents.swap(nodes);
        return output;
    } catch (const std::exception& error) {
        return parseResult(false, String(error.what(), &arp::core::defaultCpuMemoryResource()));
    }
}

YamlTextResult stringifyNode(const YamlNode& node)
{
    try {
        return makeTextResult(true,
            String(
                YamlNativeNode::serialize(node.native()), &arp::core::defaultCpuMemoryResource()));
    } catch (const std::exception& error) {
        return makeTextResult(
            false, {}, String(error.what(), &arp::core::defaultCpuMemoryResource()));
    }
}

bool isNode(const YamlNode&)
{
    return true;
}

String nodeKind(const YamlNode& node)
{
    return node.kind();
}

int nodeKindCode(const YamlNode& node)
{
    switch (node.native().get_type()) {
    case fkyaml::node_type::MAPPING:
        return 1;
    case fkyaml::node_type::SEQUENCE:
        return 2;
    case fkyaml::node_type::STRING:
        return 3;
    case fkyaml::node_type::INTEGER:
        return 4;
    case fkyaml::node_type::FLOAT:
        return 5;
    case fkyaml::node_type::BOOLEAN:
        return 6;
    case fkyaml::node_type::NULL_OBJECT:
        return 0;
    }
    return -1;
}

YamlNode documentAt(const YamlParseResult& result, int index)
{
    if (index < 0 || index >= static_cast<int>(result.documents.size())) {
        return boxYamlNode(YamlNativeNode{});
    }
    return result.documents[static_cast<std::size_t>(index)].cast<YamlNode>();
}

YamlNode firstDocument(const String& text)
{
    try {
        const String normalized = normalizeUnityDocumentHeaders(text);
        auto documents =
            YamlNativeNode::deserialize_docs(std::string(normalized.data(), normalized.size()));
        return documents.empty() ? boxYamlNode(YamlNativeNode{})
                                 : boxYamlNode(std::move(documents.front()));
    } catch (...) {
        return boxYamlNode(YamlNativeNode{});
    }
}

YamlNode mapping()
{
    return boxYamlNode(YamlNativeNode::mapping());
}

YamlNode sequence()
{
    return boxYamlNode(YamlNativeNode::sequence());
}

YamlNode stringNode(const String& text)
{
    return boxYamlNode(YamlNativeNode(text.c_str()));
}

YamlNode integerNode(std::int64_t value)
{
    return boxYamlNode(YamlNativeNode(value));
}

YamlNode intNode(int value)
{
    return boxYamlNode(YamlNativeNode(value));
}

YamlNode numberFloatNode(double value)
{
    const auto text = std::to_string(value);
    return numberNode(String(text, &arp::core::defaultCpuMemoryResource()));
}

YamlNode floatNode(double value)
{
    return numberFloatNode(value);
}

YamlNode numberNode(const String& value)
{
    char* end = nullptr;
    const double parsed = std::strtod(value.c_str(), &end);
    if (end == value.c_str() || *end != '\0') {
        throw std::invalid_argument("yaml.number requires a numeric string");
    }
    return boxYamlNode(YamlNativeNode(parsed));
}

YamlNode booleanNode(bool value)
{
    return boxYamlNode(YamlNativeNode(value));
}

} // namespace arp::scriptvm::plugins::yaml

namespace arp::reflection::generated {
bool register_yaml_reflection(arp::scriptvm::ModuleRegistration* module);
} // namespace arp::reflection::generated

extern "C" ARP_SCRIPT_PLUGIN_EXPORT int arp_register_yaml(ArpScriptPluginHostV2* host)
{
    if (host == nullptr || host->abiVersion != ARP_SCRIPT_PLUGIN_ABI_VERSION_V2
        || host->registrar == nullptr) {
        return 0;
    }

    return arp::reflection::generated::register_yaml_reflection(host->registrar->reflectionModule())
        ? 1
        : 0;
}

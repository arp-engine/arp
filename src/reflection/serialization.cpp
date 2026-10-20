#include "reflection/serialization.h"

#include <cstring>
#include <exception>
#include <fkYAML/Node.hpp>
#include <limits>

namespace arp::reflection {

namespace {

using YamlNode = fkyaml::node;

struct ReadContext {
    core::pmr::Vector<object> ownedObjects = core::pmr::Vector<object>(
        core::pmr::Vector<object>::allocator_type(&core::defaultCpuMemoryResource()));
};

core::pmr::String makeDiagnostic(const char* text)
{
    return core::pmr::String(text, &core::defaultCpuMemoryResource());
}

core::pmr::String makeDiagnostic(const char* prefix, NameView value)
{
    core::pmr::String result(prefix, &core::defaultCpuMemoryResource());
    result.append(value.data(), value.size());
    return result;
}

core::pmr::String toPmrString(NameView text)
{
    return core::pmr::String(text.data(), text.size(), &core::defaultCpuMemoryResource());
}

ReflectionYamlResult yamlOk(NameView yaml)
{
    ReflectionYamlResult result;
    result.success = true;
    result.yaml = toPmrString(yaml);
    return result;
}

ReflectionYamlResult yamlFail(const core::pmr::String& diagnostics)
{
    ReflectionYamlResult result;
    result.success = false;
    result.diagnostics = diagnostics;
    return result;
}

ReflectionObjectResult objectOk(const object& value, core::pmr::Vector<object>&& ownedObjects)
{
    ReflectionObjectResult result;
    result.success = true;
    result.value = value;
    result.ownedObjects = std::move(ownedObjects);
    return result;
}

ReflectionObjectResult objectFail(const core::pmr::String& diagnostics)
{
    ReflectionObjectResult result;
    result.success = false;
    result.diagnostics = diagnostics;
    return result;
}

bool readStoredPointer(const object& value, void*& out, core::pmr::String& diagnostics)
{
    const typeinfo* reflectedType = value.typeinfo_();
    if (reflectedType == nullptr || !reflectedType->is_pointer()) {
        diagnostics = makeDiagnostic("expected reflected pointer value");
        return false;
    }
    void* pointerStorage = value.ptr<void>();
    if (pointerStorage == nullptr) {
        out = nullptr;
        return true;
    }
    std::memcpy(&out, pointerStorage, sizeof(out));
    return true;
}

bool classMatches(const class_info* actual, const class_info* expected, bool allowDerived)
{
    if (expected == nullptr) {
        return true;
    }
    if (actual == expected) {
        return true;
    }
    return allowDerived && actual != nullptr && actual->is_derived_from(expected);
}

bool toYamlValue(const object& value, YamlNode& out, core::pmr::String& diagnostics);
bool toYamlObject(const object& value, YamlNode& out, core::pmr::String& diagnostics);

bool toYamlList(const hlist& values, YamlNode& out, core::pmr::String& diagnostics)
{
    out = YamlNode::sequence();
    auto& sequence = out.get_value_ref<YamlNode::sequence_type&>();
    for (const object& item : values) {
        YamlNode itemNode;
        core::pmr::String itemDiagnostics{&core::defaultCpuMemoryResource()};
        if (!toYamlValue(item, itemNode, itemDiagnostics)) {
            diagnostics = makeDiagnostic("failed to serialize list item: ");
            diagnostics += itemDiagnostics;
            return false;
        }
        sequence.push_back(std::move(itemNode));
    }
    return true;
}

bool toYamlValue(const object& value, YamlNode& out, core::pmr::String& diagnostics)
{
    const typeinfo* reflectedType = value.typeinfo_();
    if (reflectedType != nullptr && reflectedType->is_pointer()) {
        void* pointer = nullptr;
        if (!readStoredPointer(value, pointer, diagnostics)) {
            return false;
        }
        if (pointer == nullptr) {
            out = YamlNode();
            return true;
        }
        return toYamlObject(value, out, diagnostics);
    }
    if (value.is<bool>()) {
        out = YamlNode(value.cast<bool>());
        return true;
    }
    if (value.is<int>()) {
        out = YamlNode(value.cast<int>());
        return true;
    }
    if (value.is<std::uint32_t>()) {
        out = YamlNode(static_cast<int>(value.cast<std::uint32_t>()));
        return true;
    }
    if (value.is<float>()) {
        out = YamlNode(static_cast<double>(value.cast<float>()));
        return true;
    }
    if (value.is<double>()) {
        out = YamlNode(value.cast<double>());
        return true;
    }
    if (value.is<core::pmr::String>()) {
        const auto& text = value.cast<core::pmr::String&>();
        out = YamlNode(core::toStdString(text));
        return true;
    }
    if (value.is<hlist>()) {
        return toYamlList(value.cast<hlist&>(), out, diagnostics);
    }
    if (reflectedType != nullptr && reflectedType->is_class()) {
        return toYamlObject(value, out, diagnostics);
    }
    const class_info* cls = value.class_();
    diagnostics = cls != nullptr
        ? makeDiagnostic("unsupported reflected YAML value type: ", cls->name())
        : makeDiagnostic("unsupported reflected YAML value type");
    return false;
}

bool toYamlObject(const object& value, YamlNode& out, core::pmr::String& diagnostics)
{
    const class_info* cls = value.class_();
    if (cls == nullptr || value.is_undefined()) {
        diagnostics = makeDiagnostic("cannot serialize undefined reflection object");
        return false;
    }

    auto properties = cls->get_all_properties();
    if (properties.empty()) {
        diagnostics = makeDiagnostic(
            "unsupported reflected object without serializable properties: ", cls->name());
        return false;
    }

    YamlNode fields = YamlNode::mapping();
    for (const auto& entry : properties) {
        const property& prop = entry.second.cast<property&>();
        core::pmr::String fieldDiagnostics{&core::defaultCpuMemoryResource()};
        YamlNode fieldNode;
        if (prop.has_indirect_value()) {
            object indirectValue = prop.get_indirect_value(value);
            if (indirectValue.is_undefined()) {
                fieldNode = YamlNode();
            } else if (!toYamlValue(indirectValue, fieldNode, fieldDiagnostics)) {
                diagnostics = makeDiagnostic("failed to serialize field ");
                diagnostics += entry.first;
                diagnostics += ": ";
                diagnostics += fieldDiagnostics;
                return false;
            }
            fields[core::toStdString(entry.first)] = std::move(fieldNode);
            continue;
        }

        object fieldValue = prop.get_value(value);
        if (!toYamlValue(fieldValue, fieldNode, fieldDiagnostics)) {
            diagnostics = makeDiagnostic("failed to serialize field ");
            diagnostics += entry.first;
            diagnostics += ": ";
            diagnostics += fieldDiagnostics;
            return false;
        }
        fields[core::toStdString(entry.first)] = std::move(fieldNode);
    }

    out = YamlNode::mapping();
    out["type"] = core::toStdString(cls->name());
    out["fields"] = std::move(fields);
    return true;
}

const YamlNode* findMappingValue(const YamlNode& mapping, const char* key)
{
    if (!mapping.is_mapping()) {
        return nullptr;
    }
    const auto& values = mapping.get_value_ref<const YamlNode::mapping_type&>();
    auto found = values.find(YamlNode(key));
    return found == values.end() ? nullptr : &found->second;
}

bool readYamlDynamicValue(
    const YamlNode& node, object& out, ReadContext& context, core::pmr::String& diagnostics);

bool readYamlObject(const YamlNode& node,
    const class_info* expectedClass,
    bool allowDerived,
    object& out,
    ReadContext& context,
    core::pmr::String& diagnostics);

bool readYamlList(
    const YamlNode& node, object& out, ReadContext& context, core::pmr::String& diagnostics)
{
    if (!node.is_sequence()) {
        diagnostics = makeDiagnostic("expected list");
        return false;
    }

    // Keep the allocator construction out of braces: hlist has an
    // initializer_list<object> overload, and the allocator can otherwise be
    // misinterpreted as the first list element.
    auto values = hlist(hlist::allocator_type(&core::defaultCpuMemoryResource()));
    const auto& sequence = node.get_value_ref<const YamlNode::sequence_type&>();
    for (const YamlNode& item : sequence) {
        object itemValue;
        core::pmr::String itemDiagnostics{&core::defaultCpuMemoryResource()};
        if (!readYamlDynamicValue(item, itemValue, context, itemDiagnostics)) {
            diagnostics = makeDiagnostic("failed to deserialize list item: ");
            diagnostics += itemDiagnostics;
            return false;
        }
        values.push_back(itemValue);
    }

    out = object(values);
    return true;
}

bool readYamlInteger(const YamlNode& node, object& out, core::pmr::String& diagnostics)
{
    if (!node.is_integer()) {
        diagnostics = makeDiagnostic("expected int");
        return false;
    }
    auto value = node.get_value<YamlNode::integer_type>();
    if (value < static_cast<YamlNode::integer_type>((std::numeric_limits<int>::min)())
        || value > static_cast<YamlNode::integer_type>((std::numeric_limits<int>::max)())) {
        diagnostics = makeDiagnostic("int value out of range");
        return false;
    }
    out = object(static_cast<int>(value));
    return true;
}

bool readYamlDynamicValue(
    const YamlNode& node, object& out, ReadContext& context, core::pmr::String& diagnostics)
{
    try {
        if (node.is_boolean()) {
            out = object(node.get_value<bool>());
            return true;
        }
        if (node.is_integer()) {
            return readYamlInteger(node, out, diagnostics);
        }
        if (node.is_float_number()) {
            out = object(node.get_value<double>());
            return true;
        }
        if (node.is_string()) {
            out = object(core::toPmrString(node.get_value<std::string>()));
            return true;
        }
        if (node.is_sequence()) {
            return readYamlList(node, out, context, diagnostics);
        }
        if (node.is_mapping()) {
            return readYamlObject(node, nullptr, true, out, context, diagnostics);
        }
    } catch (const std::exception& err) {
        diagnostics = makeDiagnostic("YAML value conversion failed: ");
        diagnostics += err.what();
        return false;
    }

    diagnostics = makeDiagnostic("unsupported YAML value");
    return false;
}

bool readScalarForProperty(const property& prop,
    const YamlNode& node,
    object& out,
    ReadContext& context,
    core::pmr::String& diagnostics)
{
    const typeinfo* type = prop.type();
    if (type == nullptr) {
        diagnostics = makeDiagnostic("property has no reflected type");
        return false;
    }
    try {
        if (type->is<bool>()) {
            if (!node.is_boolean()) {
                diagnostics = makeDiagnostic("expected bool");
                return false;
            }
            out = object(node.get_value<bool>());
            return true;
        }
        if (type->is<int>()) {
            return readYamlInteger(node, out, diagnostics);
        }
        if (type->is<std::uint32_t>()) {
            if (!node.is_integer()) {
                diagnostics = makeDiagnostic("expected uint32");
                return false;
            }
            auto v = node.get_value<YamlNode::integer_type>();
            if (v < 0) {
                diagnostics = makeDiagnostic("uint32 value out of range (negative)");
                return false;
            }
            out = object(static_cast<std::uint32_t>(v));
            return true;
        }
        if (type->is<float>()) {
            if (!node.is_float_number() && !node.is_integer()) {
                diagnostics = makeDiagnostic("expected float");
                return false;
            }
            out = object(static_cast<float>(node.get_value<YamlNode::float_number_type>()));
            return true;
        }
        if (type->is<double>()) {
            if (!node.is_float_number() && !node.is_integer()) {
                diagnostics = makeDiagnostic("expected float");
                return false;
            }
            out = object(node.get_value<double>());
            return true;
        }
        if (type->is<core::pmr::String>()) {
            if (node.is_null()) {
                out = object(core::pmr::String(&core::defaultCpuMemoryResource()));
                return true;
            }
            if (!node.is_string()) {
                diagnostics = makeDiagnostic("expected string");
                return false;
            }
            out = object(core::toPmrString(node.get_value<std::string>()));
            return true;
        }
        if (type->is<hlist>()) {
            return readYamlList(node, out, context, diagnostics);
        }
        if (type->is_class()) {
            return readYamlObject(node, type->class_(), false, out, context, diagnostics);
        }
    } catch (const std::exception& err) {
        diagnostics = makeDiagnostic("YAML scalar conversion failed: ");
        diagnostics += err.what();
        return false;
    }

    diagnostics = makeDiagnostic("unsupported reflected property type");
    return false;
}

bool readYamlObject(const YamlNode& node,
    const class_info* expectedClass,
    bool allowDerived,
    object& out,
    ReadContext& context,
    core::pmr::String& diagnostics)
{
    if (!node.is_mapping()) {
        diagnostics = makeDiagnostic("expected typed object mapping");
        return false;
    }

    const YamlNode* typeNode = findMappingValue(node, "type");
    if (typeNode == nullptr || !typeNode->is_string()) {
        diagnostics = makeDiagnostic("missing required type string");
        return false;
    }

    core::pmr::String typeName = core::toPmrString(typeNode->get_value<std::string>());
    class_info* cls = db::instance().find_class(typeName);
    if (cls == nullptr) {
        diagnostics = makeDiagnostic("unregistered type: ", typeName);
        return false;
    }

    if (!classMatches(cls, expectedClass, allowDerived)) {
        diagnostics = makeDiagnostic("typed object does not match expected field type: ", typeName);
        return false;
    }

    const YamlNode* fieldsNode = findMappingValue(node, "fields");
    if (fieldsNode == nullptr || !fieldsNode->is_mapping()) {
        diagnostics = makeDiagnostic("missing required fields mapping");
        return false;
    }

    object value = cls->new_object();
    if (value.is_undefined()) {
        diagnostics = makeDiagnostic("type has no default constructor: ", typeName);
        return false;
    }

    auto properties = cls->get_all_properties();
    const auto& fields = fieldsNode->get_value_ref<const YamlNode::mapping_type&>();
    for (const auto& field : fields) {
        if (!field.first.is_string()) {
            diagnostics = makeDiagnostic("field name must be a string");
            return false;
        }
        core::pmr::String fieldName = core::toPmrString(field.first.get_value<std::string>());
        auto propertyIt = properties.find(fieldName);
        if (propertyIt == properties.end()) {
            diagnostics = makeDiagnostic("unknown reflected field: ");
            diagnostics += fieldName;
            return false;
        }

        property& prop = propertyIt->second.cast<property&>();
        if (prop.is_readonly()) {
            diagnostics = makeDiagnostic("readonly reflected field: ");
            diagnostics += fieldName;
            return false;
        }

        if (prop.has_indirect_value()) {
            if (field.second.is_null()) {
                prop.set_indirect_value(value, object::undefined());
                continue;
            }

            object pointedValue;
            core::pmr::String pointerDiagnostics{&core::defaultCpuMemoryResource()};
            if (!readYamlObject(field.second,
                    prop.indirect_class(),
                    prop.indirect_allows_derived(),
                    pointedValue,
                    context,
                    pointerDiagnostics)) {
                diagnostics = makeDiagnostic("failed to deserialize field ");
                diagnostics += fieldName;
                diagnostics += ": ";
                diagnostics += pointerDiagnostics;
                return false;
            }

            if (prop.indirect_uses_result_ownership()) {
                context.ownedObjects.push_back(pointedValue);
            }
            prop.set_indirect_value(value, pointedValue);
            continue;
        }

        const typeinfo* propType = prop.type();
        object fieldValue;
        core::pmr::String fieldDiagnostics{&core::defaultCpuMemoryResource()};
        if (!readScalarForProperty(prop, field.second, fieldValue, context, fieldDiagnostics)) {
            diagnostics = makeDiagnostic("failed to deserialize field ");
            diagnostics += fieldName;
            diagnostics += ": ";
            diagnostics += fieldDiagnostics;
            return false;
        }
        value.set(fieldName, fieldValue);
    }

    out = value;
    return true;
}

} // namespace

ReflectionYamlResult serialize_to_yaml(const object& value)
{
    YamlNode root = YamlNode::mapping();
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    if (!toYamlObject(value, root, diagnostics)) {
        return yamlFail(diagnostics);
    }
    try {
        const auto serialized = YamlNode::serialize(root);
        return yamlOk(NameView(serialized.data(), serialized.size()));
    } catch (const std::exception& err) {
        core::pmr::String failure("YAML serialization failed: ", &core::defaultCpuMemoryResource());
        failure += err.what();
        return yamlFail(failure);
    }
}

ReflectionObjectResult deserialize_from_yaml(NameView yamlText)
{
    YamlNode root;
    try {
        root = YamlNode::deserialize(yamlText.data(), yamlText.data() + yamlText.size());
    } catch (const std::exception& err) {
        core::pmr::String failure("YAML parse failed: ", &core::defaultCpuMemoryResource());
        failure += err.what();
        return objectFail(failure);
    }

    object value;
    ReadContext context;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    if (!readYamlObject(root, nullptr, true, value, context, diagnostics)) {
        if (diagnostics == "expected typed object mapping") {
            return objectFail(makeDiagnostic("YAML root must be a mapping"));
        }
        return objectFail(diagnostics);
    }

    return objectOk(value, std::move(context.ownedObjects));
}

} // namespace arp::reflection

#include "reflection/serialization.h"

#include "core/text/number_format.h"

#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>

namespace arp::reflection {
namespace {
using Node = core::YamlContract::Node;

class PathScope final {
public:
    PathScope(core::pmr::String& path, NameView name) : path_(path), size_(path.size())
    {
        path_ += ".";
        path_.append(name);
    }

    ~PathScope() { path_.resize(size_); }

private:
    core::pmr::String& path_;
    std::size_t size_;
};

bool selected(const property& prop, SerializationProfile profile)
{
    return profile == SerializationProfile::LegacyAllPropertiesV1
        || prop.check_hints(static_cast<std::uint8_t>(PropertyHint::Serialize));
}

template <class T> bool integer(const Node& node, object& value)
{
    if (!node.is_integer())
        return false;
    const auto number = node.get_value<Node::integer_type>();
    if constexpr (std::is_signed_v<T>) {
        if (number < (std::numeric_limits<T>::min)() || number > (std::numeric_limits<T>::max)())
            return false;
    } else {
        if (number < 0 || static_cast<std::uint64_t>(number) > (std::numeric_limits<T>::max)())
            return false;
    }
    value = object(static_cast<T>(number));
    return true;
}
} // namespace

SerializationArchive::SerializationArchive(
    SerializationOptions options, const SerializationContext* context)
    : options_(options), context_(context)
{}

bool SerializationArchive::fail(NameView message)
{
    if (diagnostic_.message.empty()) {
        diagnostic_.path = path_;
        diagnostic_.message.assign(message);
    }
    return false;
}

core::pmr::String SerializationArchive::diagnostics() const
{
    if (diagnostic_.message.empty())
        return {};
    auto result = diagnostic_.path;
    result += ": ";
    result += diagnostic_.message;
    return result;
}

bool SerializationArchive::visit(std::size_t depth)
{
    return depth <= options_.maxDepth && ++values_ <= options_.maxValues
        ? true
        : fail("ARP-REFLECTION-SERIALIZATION-BUDGET");
}

bool SerializationArchive::writeFields(const object& value, Node& node)
{
    try {
        return fields(value, node, nullptr, 0);
    } catch (const std::exception& error) {
        return fail(error.what());
    }
}

bool SerializationArchive::readFields(const Node& node, const object& value)
{
    try {
        Node unused;
        return fields(value, unused, &node, 0);
    } catch (const std::exception& error) {
        return fail(error.what());
    }
}

bool SerializationArchive::writeValue(const object& value, Node& node)
{
    try {
        return write(value, node, 0);
    } catch (const std::exception& error) {
        return fail(error.what());
    }
}

bool SerializationArchive::readValue(const Node& node, const typeinfo* type, object& value)
{
    try {
        return read(node, type, value, 0);
    } catch (const std::exception& error) {
        return fail(error.what());
    }
}

bool SerializationArchive::fields(
    const object& value, Node& output, const Node* input, std::size_t depth)
{
    if (!visit(depth))
        return false;
    const auto* cls = value.class_();
    if (!cls || value.is_undefined())
        return fail("cannot serialize undefined reflection object");
    const auto properties = cls->get_all_properties();
    if (properties.empty())
        return fail("unsupported reflected object without serializable properties");
    if (input && !input->is_mapping())
        return fail("expected fields mapping");
    if (input) {
        for (const auto& entry : input->get_value_ref<const Node::mapping_type&>()) {
            if (!entry.first.is_string())
                return fail("field name must be a string");
            const auto name =
                core::toPmrString(entry.first.get_value_ref<const Node::string_type&>());
            const auto it = properties.find(name);
            PathScope path(path_, name);
            if (it == properties.end()
                || !selected(it->second.cast<const property&>(), options_.profile))
                return fail("unknown or non-serializable reflected field");
        }
    } else {
        output = Node::mapping();
    }

    struct Pending {
        const property* prop;
        object value;
        bool indirect;
    };

    core::pmr::Vector<Pending> pending{&core::defaultCpuMemoryResource()};
    for (const auto& entry : properties) {
        const auto& prop = entry.second.cast<const property&>();
        if (!selected(prop, options_.profile))
            continue;
        PathScope path(path_, entry.first);
        const std::string name = core::toStdString(entry.first);
        if (input && !input->contains(name)) {
            if (options_.profile == SerializationProfile::Authoring)
                return fail("missing required reflected field");
            continue;
        }
        if (input && prop.is_readonly())
            return fail("readonly reflected field");
        Node field;
        object fieldValue;
        if (prop.reference().kind != ReferenceKind::None) {
            if (!context_)
                return fail("declared reference requires serialization context");
            core::pmr::String error{&core::defaultCpuMemoryResource()};
            const bool ok = input
                ? context_->readReference(prop, (*input)[name], fieldValue, error)
                : context_->writeReference(prop, prop.get_value(value), field, error);
            if (!ok)
                return fail(error.empty() ? NameView("invalid reference") : NameView(error));
        } else if (prop.has_indirect_value()) {
            if (input) {
                const auto& node = (*input)[name];
                if (!node.is_null()) {
                    if (!readObject(node,
                            prop.indirect_class(),
                            prop.indirect_allows_derived(),
                            fieldValue,
                            depth + 1))
                        return false;
                    if (prop.indirect_uses_result_ownership())
                        ownedObjects_.push_back(fieldValue);
                }
            } else {
                fieldValue = prop.get_indirect_value(value);
                if (!fieldValue.is_undefined()) {
                    field = Node::mapping();
                    field["type"] = core::toStdString(fieldValue.class_()->name());
                    if (!fields(fieldValue, field["fields"], nullptr, depth + 1))
                        return false;
                }
            }
        } else if (input) {
            if (!read((*input)[name], prop.type(), fieldValue, depth + 1))
                return false;
        } else if (!write(prop.get_value(value), field, depth + 1)) {
            return false;
        }
        if (input)
            pending.push_back({&prop, std::move(fieldValue), prop.has_indirect_value()});
        else
            output[name] = std::move(field);
    }
    // Conversions finish before any property setter executes. Engine-owned
    // objects still use a candidate authoring state and domain commit hook.
    for (const auto& entry : pending) {
        if (entry.indirect)
            entry.prop->set_indirect_value(value, entry.value);
        else
            entry.prop->set_value(value, entry.value);
    }
    return true;
}

bool SerializationArchive::write(const object& value, Node& node, std::size_t depth)
{
    if (!visit(depth))
        return false;
    const auto* type = value.typeinfo_();
    if (!type)
        return fail("undefined value");
    if (const auto* collection = type->collection()) {
        const auto size = collection->size(value);
        if (size > options_.maxCollectionLength)
            return fail("collection length exceeds budget");
        if (collection->optional)
            return !size ? (node = Node(), true)
                         : write(collection->element(value, 0), node, depth + 1);
        node = collection->mapping ? Node::mapping() : Node::sequence();
        for (std::size_t i = 0; i < size; ++i) {
            const auto index = core::formatUnsigned(i);
            PathScope path(path_, index);
            Node child;
            if (!write(collection->element(value, i), child, depth + 1))
                return false;
            if (collection->mapping) {
                Node key;
                if (!write(collection->key(value, i), key, depth + 1) || !key.is_scalar())
                    return fail("map key must be scalar");
                node[key] = std::move(child);
            } else
                node.get_value_ref<Node::sequence_type&>().push_back(std::move(child));
        }
        return true;
    }
    if (type->is_pointer()) {
        void* pointer = nullptr;
        std::memcpy(&pointer, value.ptr<void>(), sizeof(pointer));
        if (!pointer) {
            node = Node();
            return true;
        }
        return fail("pointer requires declared property ownership");
    }
    if (value.is<bool>()) {
        node = value.cast<bool>();
        return true;
    }
#define ARP_WRITE_INTEGER(T)                                                                       \
    if (value.is<T>()) {                                                                           \
        node = static_cast<Node::integer_type>(value.cast<T>());                                   \
        return true;                                                                               \
    }
    ARP_WRITE_INTEGER(std::int8_t)
    ARP_WRITE_INTEGER(std::int16_t)
    ARP_WRITE_INTEGER(std::int32_t)
    ARP_WRITE_INTEGER(std::int64_t)
    ARP_WRITE_INTEGER(std::uint8_t)
    ARP_WRITE_INTEGER(std::uint16_t)
    ARP_WRITE_INTEGER(std::uint32_t)
#undef ARP_WRITE_INTEGER
    if (value.is<std::uint64_t>()) {
        node = core::toStdString(core::formatUnsigned(value.cast<std::uint64_t>()));
        return true;
    }
    if (type->is_enum()) {
        int numeric = 0;
        if (!value.tryGetEnumInt(numeric)
            || (options_.profile == SerializationProfile::Authoring
                && !type->containsEnumValue(numeric)))
            return fail("unknown enum member or unsupported enum width");
        node = numeric;
        return true;
    }
    if (value.is<float>() || value.is<double>()) {
        const double number = value.is<float>() ? value.cast<float>() : value.cast<double>();
        if (!std::isfinite(number))
            return fail("number must be finite");
        node = number;
        return true;
    }
    if (value.is<core::pmr::String>()) {
        node = core::toStdString(value.cast<const core::pmr::String&>());
        return true;
    }
    if (value.is<hlist>()) {
        const auto& list = value.cast<const hlist&>();
        if (list.size() > options_.maxCollectionLength)
            return fail("collection length exceeds budget");
        node = Node::sequence();
        for (std::size_t i = 0; i < list.size(); ++i) {
            PathScope path(path_, core::formatUnsigned(i));
            Node child;
            if (!write(list[i], child, depth + 1))
                return false;
            node.get_value_ref<Node::sequence_type&>().push_back(std::move(child));
        }
        return true;
    }
    if (type->is_class()) {
        node = Node::mapping();
        node["type"] = core::toStdString(value.class_()->name());
        return fields(value, node["fields"], nullptr, depth + 1);
    }
    return fail("unsupported reflected value type");
}

bool SerializationArchive::readObject(
    const Node& node, const class_info* expected, bool derived, object& value, std::size_t depth)
{
    if (!node.is_mapping())
        return fail("expected typed object mapping");
    if (!node.contains("type") || !node["type"].is_string())
        return fail("missing required type string");
    const auto name = core::toPmrString(node["type"].get_value_ref<const Node::string_type&>());
    const auto* cls = db::instance().find_class(name);
    if (!cls)
        return fail("unregistered type");
    if (expected && cls != expected && !(derived && cls->is_derived_from(expected)))
        return fail("typed object does not match expected field type");
    if (!node.contains("fields") || !node["fields"].is_mapping())
        return fail("missing required fields mapping");
    if (node.size() != 2)
        return fail("unknown typed object envelope field");
    value = cls->new_object();
    if (value.is_undefined())
        return fail("type has no default constructor; use readFields on staged state");
    Node unused;
    return fields(value, unused, &node["fields"], depth + 1);
}

bool SerializationArchive::read(
    const Node& node, const typeinfo* type, object& value, std::size_t depth)
{
    if (!visit(depth))
        return false;
    if (!type) {
        if (node.is_boolean())
            type = type_of<bool>();
        else if (node.is_integer())
            type = type_of<int>();
        else if (node.is_float_number())
            type = type_of<double>();
        else if (node.is_string())
            type = type_of<core::pmr::String>();
        else if (node.is_sequence())
            type = type_of<hlist>();
        else if (node.is_mapping())
            return readObject(node, nullptr, true, value, depth + 1);
        else
            return fail("unsupported dynamic value");
    }
    if (const auto* collection = type->collection()) {
        value = collection->make();
        if (collection->optional) {
            if (node.is_null())
                return true;
            object element;
            return read(node, collection->elementType(), element, depth + 1)
                && collection->insert(value, {}, element);
        }
        if (collection->mapping ? !node.is_mapping() : !node.is_sequence())
            return fail("expected typed collection");
        if (node.size() > options_.maxCollectionLength)
            return fail("collection length exceeds budget");
        if (collection->mapping) {
            for (const auto& entry : node.get_value_ref<const Node::mapping_type&>()) {
                object key, element;
                if (!read(entry.first, collection->keyType(), key, depth + 1)
                    || !read(entry.second, collection->elementType(), element, depth + 1))
                    return false;
                if (!collection->insert(value, key, element))
                    return fail("duplicate collection key");
            }
        } else {
            for (std::size_t i = 0; i < node.size(); ++i) {
                PathScope path(path_, core::formatUnsigned(i));
                object element;
                if (!read(node[i], collection->elementType(), element, depth + 1)
                    || !collection->insert(value, {}, element))
                    return false;
            }
        }
        return true;
    }
    if (type->is<bool>()) {
        if (!node.is_boolean())
            return fail("expected bool");
        value = object(node.get_value<bool>());
        return true;
    }
#define ARP_READ_INTEGER(T)                                                                        \
    if (type->is<T>())                                                                             \
        return integer<T>(node, value) || fail("integer type or range error");
    ARP_READ_INTEGER(std::int8_t)
    ARP_READ_INTEGER(std::int16_t)
    ARP_READ_INTEGER(std::int32_t)
    ARP_READ_INTEGER(std::int64_t)
    ARP_READ_INTEGER(std::uint8_t)
    ARP_READ_INTEGER(std::uint16_t)
    ARP_READ_INTEGER(std::uint32_t)
#undef ARP_READ_INTEGER
    if (type->is<std::uint64_t>()) {
        if (!node.is_string())
            return fail("uint64 requires canonical decimal string");
        const auto& text = node.get_value_ref<const Node::string_type&>();
        std::uint64_t number = 0;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), number);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()
            || (text.size() > 1 && text.front() == '0'))
            return fail("uint64 type or range error");
        value = object(number);
        return true;
    }
    if (type->is_enum()) {
        if (!integer<int>(node, value))
            return fail("enum integer range error");
        return options_.profile == SerializationProfile::LegacyAllPropertiesV1
            || type->containsEnumValue(value.cast<int>()) || fail("unknown enum member");
    }
    if (type->is<float>() || type->is<double>()) {
        if (!node.is_integer() && !node.is_float_number())
            return fail("expected float");
        const double number = node.get_value<double>();
        if (!std::isfinite(number)
            || (type->is<float>()
                && (std::abs(number) > (std::numeric_limits<float>::max)()
                    || (number != 0 && static_cast<float>(number) == 0))))
            return fail("number must be finite and in range");
        value = type->is<float>() ? object(static_cast<float>(number)) : object(number);
        return true;
    }
    if (type->is<core::pmr::String>()) {
        if (!node.is_string())
            return fail("expected string");
        value = object(core::toPmrString(node.get_value_ref<const Node::string_type&>()));
        return true;
    }
    if (type->is<hlist>()) {
        if (!node.is_sequence())
            return fail("expected list");
        if (node.size() > options_.maxCollectionLength)
            return fail("collection length exceeds budget");
        auto list = hlist(hlist::allocator_type(&core::defaultCpuMemoryResource()));
        for (std::size_t i = 0; i < node.size(); ++i) {
            PathScope path(path_, core::formatUnsigned(i));
            object element;
            if (!read(node[i], nullptr, element, depth + 1))
                return false;
            list.push_back(std::move(element));
        }
        value = object(std::move(list));
        return true;
    }
    if (type->is_class())
        return readObject(node, type->class_(), false, value, depth + 1);
    return fail("unsupported reflected property type");
}

bool SerializationArchive::writeValue(
    const SerializationDescriptor& descriptor, const object& value, Node& node)
{
    try {
        object receiver = value;
        return described(descriptor, receiver, node, nullptr, 0);
    } catch (const std::exception& error) {
        return fail(error.what());
    }
}

bool SerializationArchive::readValue(
    const SerializationDescriptor& descriptor, const Node& node, object& value)
{
    try {
        Node unused;
        return described(descriptor, value, unused, &node, 0);
    } catch (const std::exception& error) {
        return fail(error.what());
    }
}

bool SerializationArchive::described(const SerializationDescriptor& descriptor,
    object& value,
    Node& output,
    const Node* input,
    std::size_t depth)
{
    if (!visit(depth))
        return false;
    using Kind = SerializationDescriptor::Kind;
    if (descriptor.kind == Kind::Scalar) {
        if (!descriptor.scalarType)
            return fail("unsupported descriptor scalar type");
        if (!input && value.typeinfo_()->class_() != descriptor.scalarType->class_())
            return fail("value does not match declared descriptor type");
        return input ? read(*input, descriptor.scalarType, value, depth + 1)
                     : write(value, output, depth + 1);
    }
    if (descriptor.kind == Kind::Reference) {
        if (!context_)
            return fail("reference requires serialization context");
        core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
        const bool ok = input ? context_->readReference(descriptor, *input, value, diagnostic)
                              : context_->writeReference(descriptor, value, output, diagnostic);
        return ok
            || fail(diagnostic.empty() ? NameView("invalid reference") : NameView(diagnostic));
    }
    if (descriptor.kind == Kind::Optional) {
        if (!descriptor.element)
            return fail("optional descriptor has no element");
        if (input && input->is_null()) {
            value = object::undefined();
            return true;
        }
        if (!input && value.is_undefined()) {
            output = Node();
            return true;
        }
        return described(*descriptor.element, value, output, input, depth + 1);
    }
    if (descriptor.kind == Kind::Sequence) {
        if (!descriptor.element || (input ? !input->is_sequence() : !value.is<hlist>()))
            return fail("expected declared sequence");
        const auto size = input ? input->size() : value.cast<const hlist&>().size();
        if (size > options_.maxCollectionLength)
            return fail("collection length exceeds budget");
        auto list = hlist(hlist::allocator_type(&core::defaultCpuMemoryResource()));
        if (!input)
            output = Node::sequence();
        for (std::size_t i = 0; i < size; ++i) {
            PathScope path(path_, core::formatUnsigned(i));
            object child = input ? object{} : value.cast<const hlist&>()[i];
            Node node;
            if (!described(
                    *descriptor.element, child, node, input ? &(*input)[i] : nullptr, depth + 1))
                return false;
            if (input)
                list.push_back(std::move(child));
            else
                output.get_value_ref<Node::sequence_type&>().push_back(std::move(node));
        }
        if (input)
            value = object(std::move(list));
        return true;
    }
    if (!descriptor.get || !descriptor.set || (input && !input->is_mapping()))
        return fail("invalid record descriptor or value");
    if (descriptor.fields.size() > options_.maxCollectionLength)
        return fail("record field count exceeds budget");
    if (input && input->size() != descriptor.fields.size())
        return fail("unknown or missing record field");
    core::pmr::Vector<object> pending{&core::defaultCpuMemoryResource()};
    if (!input)
        output = Node::mapping();
    for (const auto& field : descriptor.fields) {
        PathScope path(path_, field.name);
        const auto key = core::toStdString(field.name);
        if (!field.type || (input && !input->contains(key)))
            return fail("missing required record field");
        object child = input ? object{} : descriptor.get(value, field.name);
        Node node;
        if (!described(*field.type, child, node, input ? &(*input)[key] : nullptr, depth + 1))
            return false;
        if (input)
            pending.push_back(std::move(child));
        else
            output[key] = std::move(node);
    }
    if (input)
        for (std::size_t i = 0; i < pending.size(); ++i)
            descriptor.set(value, descriptor.fields[i].name, pending[i]);
    return true;
}

ReflectionYamlResult serialize_to_yaml(const object& value, SerializationOptions options)
{
    ReflectionYamlResult result;
    SerializationArchive archive(options);
    Node root;
    result.success = archive.writeValue(value, root);
    if (result.success) {
        try {
            result.yaml = core::YamlContract::serialize(root);
        } catch (const std::exception& error) {
            result.success = false;
            result.diagnostics = error.what();
        }
    } else
        result.diagnostics = archive.diagnostics();
    return result;
}

ReflectionObjectResult deserialize_from_yaml(NameView text, SerializationOptions options)
{
    ReflectionObjectResult result;
    SerializationArchive archive(options);
    try {
        const auto root = Node::deserialize(text.begin(), text.end());
        // The text API is a typed object document; embedded values use archive.
        if (!root.is_mapping()) {
            result.diagnostics = "YAML root must be a mapping";
            return result;
        }
        if (!root.contains("type")) {
            result.diagnostics = "missing required type string";
            return result;
        }
        result.success = archive.readValue(root, nullptr, result.value);
        result.diagnostics = archive.diagnostics();
        if (result.success)
            result.ownedObjects = archive.releaseOwnedObjects();
    } catch (const std::exception& error) {
        result.diagnostics = error.what();
    }
    return result;
}
} // namespace arp::reflection

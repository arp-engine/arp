#include "core/text/number_format.h"
#include "reflection/serialization.h"

namespace arp::reflection {
namespace {
using Y = core::YamlContract::Node;

SerializationValue fromYaml(const Y& n,
    const SerializationOptions& options,
    std::size_t depth,
    std::size_t& count,
    std::size_t& bytes)
{
    const auto expanded = [](std::size_t v) {
        return v > (SIZE_MAX - 3) / 3 ? SIZE_MAX : v * 3 + 3;
    };
    if (depth > expanded(options.maxDepth) || count >= expanded(options.maxValues))
        throw std::runtime_error("ARP-REFLECTION-SERIALIZATION-BUDGET");
    ++count;
    if (n.is_null())
        return {};
    if (n.is_boolean())
        return SerializationValue(n.get_value<bool>());
    if (n.is_integer()) {
        const auto value = n.get_value<Y::integer_type>();
        return value >= INT32_MIN && value <= INT32_MAX
            ? SerializationValue(static_cast<int>(value))
            : SerializationValue(value);
    }
    if (n.is_float_number())
        return SerializationValue(n.get_value<double>());
    if (n.is_string()) {
        const auto& text = n.get_value_ref<const Y::string_type&>();
        if (text.size() > options.maxBytes - bytes)
            throw std::runtime_error("ARP-REFLECTION-SERIALIZATION-BYTE-BUDGET");
        bytes += text.size();
        return SerializationValue(text);
    }
    if (n.size() > options.maxCollectionLength)
        throw std::runtime_error("ARP-REFLECTION-SERIALIZATION-COLLECTION-BUDGET");
    SerializationValue v =
        n.is_mapping() ? SerializationValue::mapping() : SerializationValue::sequence();
    if (n.is_mapping())
        for (const auto& e : n.get_value_ref<const Y::mapping_type&>())
            v[fromYaml(e.first, options, depth + 1, count, bytes)] =
                fromYaml(e.second, options, depth + 1, count, bytes);
    else
        for (const auto& e : n.get_value_ref<const Y::sequence_type&>())
            v.get_value_ref<SerializationValue::sequence_type&>().push_back(
                fromYaml(e, options, depth + 1, count, bytes));
    return v;
}

SerializationValue fromYaml(const Y& n, SerializationOptions options = {})
{
    std::size_t count = 0, bytes = 0;
    return fromYaml(n, options, 0, count, bytes);
}

Y toYaml(const SerializationValue& v)
{
    using K = SerializationValue::Kind;
    switch (v.kind()) {
    case K::Null:
        return Y();
    case K::Boolean:
        return Y(v.get_value<bool>());
    case K::Integer:
        return Y(v.get_value<std::int64_t>());
    case K::Unsigned:
        if (v.width() == 8)
            return Y(core::toStdString(core::formatUnsigned(v.get_value<std::uint64_t>())));
        else
            return Y(static_cast<std::int64_t>(v.get_value<std::uint64_t>()));
    case K::Real:
        return Y(v.get_value<double>());
    case K::String:
        return Y(core::toStdString(v.get_value_ref<const SerializationValue::string_type&>()));
    case K::Sequence: {
        auto n = Y::sequence();
        for (const auto& e : v.get_value_ref<const SerializationValue::sequence_type&>())
            n.get_value_ref<Y::sequence_type&>().push_back(toYaml(e));
        return n;
    }
    case K::Mapping: {
        auto n = Y::mapping();
        for (const auto& e : v.get_value_ref<const SerializationValue::mapping_type&>())
            n[toYaml(e.first)] = toYaml(e.second);
        return n;
    }
    }
    throw std::runtime_error("invalid archive value");
}
} // namespace

SerializationValue serialization_value_from_yaml(const Y& n, SerializationOptions o)
{
    return fromYaml(n, o);
}

#define ARP_REFERENCE_BRIDGE(T)                                                                    \
    bool SerializationContext::writeReferenceValue(                                                \
        const T& p, const object& value, SerializationValue& n, core::pmr::String& error) const    \
    {                                                                                              \
        Y y;                                                                                       \
        if (!writeReference(p, value, y, error))                                                   \
            return false;                                                                          \
        n = fromYaml(y);                                                                           \
        return true;                                                                               \
    }                                                                                              \
    bool SerializationContext::readReferenceValue(                                                 \
        const T& p, const SerializationValue& n, object& value, core::pmr::String& error) const    \
    {                                                                                              \
        return readReference(p, toYaml(n), value, error);                                          \
    }
ARP_REFERENCE_BRIDGE(property)
ARP_REFERENCE_BRIDGE(SerializationDescriptor)
#undef ARP_REFERENCE_BRIDGE

bool SerializationArchive::writeFields(const object& v, Y& n)
{
    try {
        SerializationValue a;
        if (!writeFields(v, a))
            return false;
        n = toYaml(a);
        return true;
    } catch (const std::exception& e) {
        return fail(e.what());
    }
}

bool SerializationArchive::readFields(const Y& n, const object& v)
{
    try {
        return readFields(fromYaml(n, options_), v);
    } catch (const std::exception& e) {
        return fail(e.what());
    }
}

bool SerializationArchive::writeValue(const object& v, Y& n)
{
    try {
        SerializationValue a;
        if (!writeValue(v, a))
            return false;
        n = toYaml(a);
        return true;
    } catch (const std::exception& e) {
        return fail(e.what());
    }
}

bool SerializationArchive::readValue(const Y& n, const typeinfo* t, object& v)
{
    try {
        return readValue(fromYaml(n, options_), t, v);
    } catch (const std::exception& e) {
        return fail(e.what());
    }
}

bool SerializationArchive::writeValue(const SerializationDescriptor& d, const object& v, Y& n)
{
    try {
        SerializationValue a;
        if (!writeValue(d, v, a))
            return false;
        n = toYaml(a);
        return true;
    } catch (const std::exception& e) {
        return fail(e.what());
    }
}

bool SerializationArchive::readValue(const SerializationDescriptor& d, const Y& n, object& v)
{
    try {
        return readValue(d, fromYaml(n, options_), v);
    } catch (const std::exception& e) {
        return fail(e.what());
    }
}

ReflectionYamlResult serialize_to_yaml(const object& value, SerializationOptions options)
{
    ReflectionYamlResult result;
    SerializationArchive archive(options);
    Y root;
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
        if (text.size() > options.maxBytes)
            throw std::runtime_error("ARP-REFLECTION-SERIALIZATION-BYTE-BUDGET");
        const auto root = Y::deserialize(text.begin(), text.end());
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

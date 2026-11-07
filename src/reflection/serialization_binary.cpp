#include "reflection/serialization_binary.h"

#include "core/encoding/binary_stream.h"
#include "core/hash/stable_hash.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace arp::reflection {
namespace {
using V = SerializationValue;
using K = V::Kind;
using Writer = core::encoding::BinaryWriter;
using Reader = core::encoding::BinaryReader;
constexpr std::uint64_t valueMagic = 0x314c4156505241ull;  // ARPVAL1
constexpr std::uint64_t objectMagic = 0x31464552505241ull; // ARPREF1

void require(bool ok, const char* error)
{
    if (!ok)
        throw std::runtime_error(error);
}

void visit(std::size_t depth, std::size_t& count, const SerializationOptions& o)
{
    const auto expanded = [](std::size_t n) {
        return n > (SIZE_MAX - 3) / 3 ? SIZE_MAX : n * 3 + 3;
    };
    require(depth <= expanded(o.maxDepth) && count < expanded(o.maxValues),
        "ARP-REFLECTION-SERIALIZATION-BUDGET");
    ++count;
}

void encode(
    Writer& w, const V& v, const SerializationOptions& o, std::size_t depth, std::size_t& count)
{
    visit(depth, count, o);
    w.integer(static_cast<unsigned>(v.kind()), 1);
    switch (v.kind()) {
    case K::Null:
        break;
    case K::Boolean:
        w.integer(v.get_value<bool>(), 1);
        break;
    case K::Integer:
    case K::Unsigned:
        w.integer(v.width(), 1);
        w.integer(v.kind() == K::Integer ? static_cast<std::uint64_t>(v.get_value<std::int64_t>())
                                         : v.get_value<std::uint64_t>(),
            v.width());
        break;
    case K::Real: {
        require(std::isfinite(v.get_value<double>()), "BINARY-NONFINITE");
        w.integer(v.width(), 1);
        if (v.width() == 4) {
            auto value = v.get_value<float>();
            std::uint32_t bits;
            std::memcpy(&bits, &value, 4);
            w.integer(bits, 4);
        } else {
            auto value = v.get_value<double>();
            std::uint64_t bits;
            std::memcpy(&bits, &value, 8);
            w.integer(bits);
        }
        break;
    }
    case K::String:
        w.text(v.get_value_ref<const V::string_type&>());
        break;
    case K::Sequence:
        require(v.size() <= o.maxCollectionLength, "BINARY-COLLECTION-BUDGET");
        w.integer(v.size());
        for (const auto& e : v.get_value_ref<const V::sequence_type&>())
            encode(w, e, o, depth + 1, count);
        break;
    case K::Mapping:
        require(v.size() <= o.maxCollectionLength, "BINARY-COLLECTION-BUDGET");
        w.integer(v.size());
        for (const auto& e : v.get_value_ref<const V::mapping_type&>()) {
            require(e.first.is_scalar(), "BINARY-MAP-KEY");
            encode(w, e.first, o, depth + 1, count);
            encode(w, e.second, o, depth + 1, count);
        }
        break;
    }
}

V decode(Reader& r, const SerializationOptions& o, std::size_t depth, std::size_t& count)
{
    visit(depth, count, o);
    const auto kind = static_cast<K>(r.integer(1));
    switch (kind) {
    case K::Null:
        return {};
    case K::Boolean: {
        auto b = r.integer(1);
        require(b <= 1, "BINARY-BOOLEAN");
        return V(b != 0);
    }
    case K::Integer:
    case K::Unsigned: {
        const auto width = r.integer(1);
        require(width == 1 || width == 2 || width == 4 || width == 8, "BINARY-INTEGER-WIDTH");
        auto n = r.integer(static_cast<unsigned>(width));
        if (kind == K::Unsigned) {
            if (width == 1)
                return V(static_cast<std::uint8_t>(n));
            if (width == 2)
                return V(static_cast<std::uint16_t>(n));
            if (width == 4)
                return V(static_cast<std::uint32_t>(n));
            return V(n);
        }
        if (width < 8 && (n & (std::uint64_t(1) << (width * 8 - 1))))
            n |= (~std::uint64_t(0)) << (width * 8);
        std::int64_t signedValue;
        std::memcpy(&signedValue, &n, 8);
        if (width == 1)
            return V(static_cast<std::int8_t>(signedValue));
        if (width == 2)
            return V(static_cast<std::int16_t>(signedValue));
        if (width == 4)
            return V(static_cast<std::int32_t>(signedValue));
        return V(signedValue);
    }
    case K::Real: {
        auto width = r.integer(1);
        require(width == 4 || width == 8, "BINARY-REAL-WIDTH");
        auto bits = r.integer(static_cast<unsigned>(width));
        if (width == 4) {
            auto b = static_cast<std::uint32_t>(bits);
            float f;
            std::memcpy(&f, &b, 4);
            require(std::isfinite(f), "BINARY-NONFINITE");
            return V(f);
        }
        double f;
        std::memcpy(&f, &bits, 8);
        require(std::isfinite(f), "BINARY-NONFINITE");
        return V(f);
    }
    case K::String:
        return V(r.text(o.maxBytes));
    case K::Sequence:
    case K::Mapping: {
        auto size = r.integer();
        require(size <= o.maxCollectionLength, "BINARY-COLLECTION-BUDGET");
        require(size <= r.remaining(), "BINARY-TRUNCATED");
        V v = kind == K::Sequence ? V::sequence() : V::mapping();
        V previous;
        bool hasPrevious = false;
        for (std::uint64_t i = 0; i < size; ++i) {
            auto e = decode(r, o, depth + 1, count);
            if (kind == K::Sequence)
                v.get_value_ref<V::sequence_type&>().push_back(std::move(e));
            else {
                require(e.is_scalar() && (!hasPrevious || previous < e),
                    "BINARY-MAP-ORDER-OR-DUPLICATE");
                previous = e;
                hasPrevious = true;
                auto value = decode(r, o, depth + 1, count);
                v.get_value_ref<V::mapping_type&>().emplace(std::move(e), std::move(value));
            }
        }
        return v;
    }
    }
    throw std::runtime_error("BINARY-VALUE-TAG");
}

void hashText(core::StableHash64& h, NameView text)
{
    h.appendU64(text.size());
    h.appendString(text);
}

void schema(core::StableHash64& h,
    const typeinfo* type,
    SerializationOptions o,
    core::pmr::Vector<const class_info*>& active,
    std::size_t depth,
    std::size_t& count)
{
    require(type && depth <= o.maxDepth && count < o.maxValues, "BINARY-SCHEMA-BUDGET");
    ++count;
    if (const auto* c = type->collection()) {
        hashText(h, c->optional ? "optional" : c->mapping ? "mapping" : "sequence");
        if (c->mapping)
            schema(h, c->keyType(), o, active, depth + 1, count);
        schema(h, c->elementType(), o, active, depth + 1, count);
        return;
    }
#define SCALAR(T, name)                                                                            \
    if (type->is<T>()) {                                                                           \
        hashText(h, name);                                                                         \
        return;                                                                                    \
    }
    SCALAR(bool, "bool");
    SCALAR(std::int8_t, "i8");
    SCALAR(std::int16_t, "i16");
    SCALAR(std::int32_t, "i32");
    SCALAR(std::int64_t, "i64");
    SCALAR(std::uint8_t, "u8");
    SCALAR(std::uint16_t, "u16");
    SCALAR(std::uint32_t, "u32");
    SCALAR(std::uint64_t, "u64");
    SCALAR(float, "f32");
    SCALAR(double, "f64");
    SCALAR(core::pmr::String, "utf8");
    SCALAR(hlist, "dynamic-list");
#undef SCALAR
    const auto* cls = type->class_();
    require(cls, "BINARY-SCHEMA-TYPE");
    hashText(h, cls->name());
    h.appendU64(type->is_pointer());
    h.appendU64(type->is_enum());
    if (type->is_enum()) {
        auto values = type->enumValues();
        std::sort(values.begin(), values.end());
        h.appendU64(values.size());
        for (auto n : values)
            h.appendU64(static_cast<std::uint64_t>(n));
        return;
    }
    if (std::find(active.begin(), active.end(), cls) != active.end()) {
        hashText(h, "recursive");
        return;
    }
    active.push_back(cls);
    h.appendU64(cls->bases().size());
    for (const auto* base : cls->bases())
        hashText(h, base->name());
    for (const auto& entry : cls->get_all_properties()) {
        const auto& p = entry.second.cast<const property&>();
        if (o.profile != SerializationProfile::LegacyAllPropertiesV1
            && !p.check_hints(static_cast<std::uint8_t>(PropertyHint::Serialize)))
            continue;
        hashText(h, entry.first);
        h.appendU64(p.is_readonly());
        h.appendU64(p.has_indirect_value());
        h.appendU64(p.indirect_allows_derived());
        h.appendU64(static_cast<unsigned>(p.reference().kind));
        hashText(h, p.reference().targetType);
        h.appendU64(p.reference().required);
        schema(h, p.type(), o, active, depth + 1, count);
    }
    active.pop_back();
}

// Each actual polymorphic object contributes its own schema as well as the root's declared schema.
void objectSchemas(Writer& w, const V& v, SerializationOptions o)
{
    if (v.is_mapping()) {
        if (v.size() == 2 && v.contains("type") && v.contains("fields") && v["type"].is_string()
            && v["fields"].is_mapping()) {
            const auto& name = v["type"].get_value_ref<const V::string_type&>();
            const auto* cls = db::instance().find_class(name);
            require(cls, "BINARY-UNKNOWN-TYPE");
            auto value = cls->new_object();
            require(!value.is_undefined(), "BINARY-TYPE-CONSTRUCTOR");
            w.text(name);
            w.integer(serialization_schema(value.typeinfo_(), o));
        }
        for (const auto& e : v.get_value_ref<const V::mapping_type&>())
            objectSchemas(w, e.second, o);
    } else if (v.is_sequence())
        for (const auto& e : v.get_value_ref<const V::sequence_type&>())
            objectSchemas(w, e, o);
}
} // namespace

std::uint64_t serialization_schema(const typeinfo* type, SerializationOptions o)
{
    core::StableHash64 h;
    hashText(h, "arp.reflection.binary/v1");
    h.appendU64(static_cast<unsigned>(o.profile));
    core::pmr::Vector<const class_info*> active{&core::defaultCpuMemoryResource()};
    std::size_t count = 0;
    schema(h, type, o, active, 0, count);
    return h.value();
}

ReflectionBinaryResult encode_serialization_value(const V& v, SerializationOptions o)
{
    ReflectionBinaryResult result;
    try {
        Writer w(o.maxBytes);
        w.integer(valueMagic);
        w.integer(1, 4);
        std::size_t count = 0;
        encode(w, v, o, 0, count);
        result.bytes = w.take();
        result.success = true;
    } catch (const std::exception& e) {
        result.diagnostics = e.what();
    }
    return result;
}

SerializationValueResult decode_serialization_value(
    core::ArrayView<const std::uint8_t> bytes, SerializationOptions o)
{
    SerializationValueResult result;
    try {
        require(bytes.size() <= o.maxBytes, "BINARY-BYTE-BUDGET");
        Reader r(bytes);
        require(r.integer() == valueMagic && r.integer(4) == 1, "BINARY-FORMAT-RECOOK");
        std::size_t count = 0;
        auto v = decode(r, o, 0, count);
        require(!r.remaining(), "BINARY-TRAILING-BYTES");
        result.value = std::move(v);
        result.success = true;
    } catch (const std::exception& e) {
        result.diagnostics = e.what();
    }
    return result;
}

ReflectionBinaryResult serialize_to_binary(
    const object& value, SerializationOptions o, const SerializationContext* context)
{
    ReflectionBinaryResult result;
    try {
        SerializationArchive archive(o, context);
        V v;
        const auto written = archive.writeValue(value, v);
        require(written, archive.diagnostics().c_str());
        auto body = encode_serialization_value(v, o);
        require(body.success, body.diagnostics.c_str());
        Writer types(o.maxBytes);
        objectSchemas(types, v, o);
        Writer w(o.maxBytes);
        w.integer(objectMagic);
        w.integer(1, 4);
        w.integer(static_cast<unsigned>(o.profile), 1);
        w.integer(serialization_schema(value.typeinfo_(), o));
        w.integer(types.bytes().size());
        w.raw({types.bytes().data(), types.bytes().size()});
        w.raw({body.bytes.data(), body.bytes.size()});
        result.bytes = w.take();
        result.success = true;
    } catch (const std::exception& e) {
        result.diagnostics = e.what();
    }
    return result;
}

ReflectionObjectResult deserialize_from_binary(core::ArrayView<const std::uint8_t> bytes,
    SerializationOptions o,
    const SerializationContext* context,
    const typeinfo* expected)
{
    ReflectionObjectResult result;
    try {
        require(bytes.size() <= o.maxBytes, "BINARY-BYTE-BUDGET");
        Reader r(bytes);
        require(r.integer() == objectMagic && r.integer(4) == 1, "BINARY-FORMAT-RECOOK");
        require(r.integer(1) == static_cast<unsigned>(o.profile), "BINARY-PROFILE-RECOOK");
        const auto rootSchema = r.integer();
        auto types = r.raw(r.integer());
        Reader schemas(types);
        std::size_t schemaCount = 0;
        while (schemas.remaining()) {
            require(++schemaCount <= o.maxValues, "BINARY-SCHEMA-BUDGET");
            const auto name = schemas.text(o.maxBytes);
            const auto fingerprint = schemas.integer();
            const auto* cls = db::instance().find_class(name);
            require(cls, "BINARY-UNKNOWN-TYPE-RECOOK");
            auto staged = cls->new_object();
            require(!staged.is_undefined()
                    && serialization_schema(staged.typeinfo_(), o) == fingerprint,
                "BINARY-SCHEMA-RECOOK");
        }
        auto value = decode_serialization_value(r.raw(r.remaining()), o);
        require(value.success, value.diagnostics.c_str());
        Writer actual(o.maxBytes);
        objectSchemas(actual, value.value, o);
        require(actual.bytes().size() == types.size()
                && std::equal(actual.bytes().begin(), actual.bytes().end(), types.begin()),
            "BINARY-SCHEMA-TABLE");
        // Reject a mismatched root fingerprint before any property setter or
        // external reference resolver is called on the candidate.
        if (!(expected && expected->collection()) && value.value.is_mapping()
            && value.value.size() == 2 && value.value.contains("type")
            && value.value.contains("fields") && value.value["type"].is_string()
            && value.value["fields"].is_mapping()) {
            const auto name = value.value["type"].get_value<V::string_type>();
            const auto* cls = db::instance().find_class(name);
            require(cls, "BINARY-UNKNOWN-TYPE-RECOOK");
            const auto probe = cls->new_object();
            require(
                !probe.is_undefined() && serialization_schema(probe.typeinfo_(), o) == rootSchema,
                "BINARY-SCHEMA-RECOOK");
        } else if (expected)
            require(serialization_schema(expected, o) == rootSchema, "BINARY-SCHEMA-RECOOK");
        SerializationArchive archive(o, context);
        object candidate;
        const auto read = archive.readValue(value.value, expected, candidate);
        require(read, archive.diagnostics().c_str());
        require(
            serialization_schema(candidate.typeinfo_(), o) == rootSchema, "BINARY-SCHEMA-RECOOK");
        result.value = std::move(candidate);
        result.ownedObjects = archive.releaseOwnedObjects();
        result.success = true;
    } catch (const std::exception& e) {
        result.diagnostics = e.what();
    }
    return result;
}
} // namespace arp::reflection

#include "reflection/serialization_binary.h"

#include <cassert>
#include <iostream>
#include <optional>
using namespace arp;
using namespace reflection;

namespace {
constexpr auto flag = static_cast<std::uint8_t>(PropertyHint::Serialize);

struct Child {
    int value = 7;
};
enum class Mode { Idle = 1, Playing = 7 };

struct Base {
    int inherited = 42;
};

struct Derived : Base {
    Mode mode = Mode::Playing;
    core::pmr::Vector<core::pmr::Vector<int>> nested{&core::defaultCpuMemoryResource()};
};

struct Record {
    std::uint64_t high = UINT64_MAX;
    std::int64_t low = INT64_MIN;
    std::uint8_t byte = 255;
    core::pmr::String name{"test", &core::defaultCpuMemoryResource()};
    core::pmr::Vector<Child> children{&core::defaultCpuMemoryResource()};
    core::pmr::Map<core::pmr::String, std::uint64_t> map{&core::defaultCpuMemoryResource()};
    std::optional<int> maybe;
    hlist dynamic = makeEmptyHList();
    int ignored = 19;
};

struct Ref {
    core::pmr::String id{&core::defaultCpuMemoryResource()};
};

class Context final : public SerializationContext {
public:
    mutable unsigned reads = 0;
    bool reject = false;

    bool writeReferenceValue(const property& p,
        const object& v,
        SerializationValue& n,
        core::pmr::String&) const override
    {
        n = SerializationValue::mapping();
        n["id"] = v.cast<const core::pmr::String&>();
        n["type"] = p.reference().targetType;
        return true;
    }

    bool readReferenceValue(const property& p,
        const SerializationValue& n,
        object& v,
        core::pmr::String& diagnostic) const override
    {
        ++reads;
        if (reject) {
            diagnostic = "BINARY-TEST-EXTERNAL-REFERENCE-RESOLVER-REJECTED";
            return false;
        }
        if (n["type"].get_value<core::pmr::String>() != p.reference().targetType)
            return false;
        v = object(n["id"].get_value<core::pmr::String>());
        return true;
    }
};

void run()
{
    Derived derived;
    derived.nested.push_back({1, 2, 3});
    auto inherited = serialize_to_binary(object(derived));
    assert(inherited.success);
    auto derivedRead = deserialize_from_binary({inherited.bytes.data(), inherited.bytes.size()});
    assert(derivedRead.success);
    const auto& derivedValue = derivedRead.value.cast<const Derived&>();
    assert(derivedValue.inherited == 42 && derivedValue.mode == Mode::Playing
        && derivedValue.nested[0][2] == 3);
    SerializationOptions unlimited;
    unlimited.maxDepth = SIZE_MAX;
    unlimited.maxValues = SIZE_MAX;
    auto expanded = serialize_to_binary(object(derived), unlimited);
    assert(expanded.success);
    unlimited.maxDepth = 0;
    assert(!deserialize_from_binary({expanded.bytes.data(), expanded.bytes.size()}, unlimited)
            .success);
    unlimited = {};
    unlimited.maxValues = 0;
    assert(!serialize_to_binary(object(derived), unlimited).success);
    auto tooLong = SerializationValue::sequence();
    tooLong.get_value_ref<SerializationValue::sequence_type&>().push_back(1);
    auto wire = encode_serialization_value(tooLong);
    assert(wire.success);
    // Corrupt the u64 collection length at the first value, before allocation.
    for (std::size_t i = 13; i < 21; ++i)
        wire.bytes[i] = 255;
    assert(!decode_serialization_value({wire.bytes.data(), wire.bytes.size()}).success);
    Record r;
    r.children.push_back({});
    r.map.emplace("x", UINT64_MAX);
    r.maybe = 4;
    r.dynamic.push_back(object(std::int64_t(INT64_MAX)));
    r.dynamic.push_back(object(std::uint64_t(UINT64_MAX)));
    r.dynamic.push_back(object(1.25f));
    const auto a = serialize_to_binary(object(r));
    if (!a.success)
        std::cerr << a.diagnostics << '\n';
    assert(a.success);
    const auto b = serialize_to_binary(object(r));
    assert(b.success && a.bytes == b.bytes);
    auto read = deserialize_from_binary({a.bytes.data(), a.bytes.size()});
    if (!read.success)
        std::cerr << read.diagnostics << '\n';
    assert(read.success);
    const auto& out = read.value.cast<const Record&>();
    assert(out.high == UINT64_MAX && out.low == INT64_MIN && out.byte == 255
        && out.children[0].value == 7 && out.map.at("x") == UINT64_MAX && out.maybe == 4
        && out.name == "test");
    assert(out.dynamic[0].is<std::int64_t>() && out.dynamic[0].cast<std::int64_t>() == INT64_MAX);
    assert(out.dynamic[1].is<std::uint64_t>() && out.dynamic[1].cast<std::uint64_t>() == UINT64_MAX
        && out.dynamic[2].is<float>());
    for (std::size_t i = 0; i < a.bytes.size(); ++i)
        assert(!deserialize_from_binary({a.bytes.data(), i}).success);
    auto corrupt = a.bytes;
    corrupt[0] ^= 1;
    assert(!deserialize_from_binary({corrupt.data(), corrupt.size()}).success);
    corrupt = a.bytes;
    corrupt[13] ^= 1;
    assert(!deserialize_from_binary({corrupt.data(), corrupt.size()}).success);
    corrupt = a.bytes;
    corrupt.push_back(0);
    assert(!deserialize_from_binary({corrupt.data(), corrupt.size()}).success);
    SerializationOptions small;
    small.maxBytes = a.bytes.size() - 1;
    assert(!deserialize_from_binary({a.bytes.data(), a.bytes.size()}, small).success);
    small = {};
    small.maxCollectionLength = 1;
    assert(!serialize_to_binary(object(r), small).success);
    auto before = serialization_schema(type_of<Record>());
    registration::class_<Child>().property("changed", &Child::value, {}, flag);
    assert(before != serialization_schema(type_of<Record>()));
    assert(!deserialize_from_binary({a.bytes.data(), a.bytes.size()}).success);
    const auto p = class_of<Ref>()->get_all_properties().at("id");
    p.cast<property&>().setReference(ReferenceKind::Resource, "TextureResource");
    Ref ref;
    ref.id = "165b5ee8-2514-46e7-8472-1bfa17c1e226";
    Context ctx;
    auto encoded = serialize_to_binary(object(ref), {}, &ctx);
    assert(encoded.success);
    auto restored = deserialize_from_binary({encoded.bytes.data(), encoded.bytes.size()}, {}, &ctx);
    assert(restored.success && restored.value.cast<const Ref&>().id == ref.id);
    ctx.reject = true;
    auto rejected = deserialize_from_binary({encoded.bytes.data(), encoded.bytes.size()}, {}, &ctx);
    assert(!rejected.success && rejected.value.is_undefined()
        && rejected.diagnostics.find("BINARY-TEST-EXTERNAL-REFERENCE-RESOLVER-REJECTED")
            != core::pmr::String::npos);
    ctx.reject = false;
    const auto reads = ctx.reads;
    encoded.bytes[13] ^= 1;
    auto incompatible =
        deserialize_from_binary({encoded.bytes.data(), encoded.bytes.size()}, {}, &ctx);
    assert(!incompatible.success && incompatible.value.is_undefined() && ctx.reads == reads);
}
} // namespace

int main(int argc, char**)
{
    registration::class_<Base>("BinaryBase")
        .construct<>()
        .property("inherited", &Base::inherited, {}, flag);
    registration::class_<Derived>("BinaryDerived")
        .construct<>()
        .inherit<Base>()
        .constant<Mode>("Mode", {{"Idle", Mode::Idle}, {"Playing", Mode::Playing}})
        .property("mode", &Derived::mode, {}, flag)
        .property("nested", &Derived::nested, {}, flag);
    registration::class_<Child>("BinaryChild")
        .construct<>()
        .property("value", &Child::value, {}, flag);
    registration::class_<Record>("BinaryRecord")
        .construct<>()
        .property("high", &Record::high, {}, flag)
        .property("low", &Record::low, {}, flag)
        .property("byte", &Record::byte, {}, flag)
        .property("name", &Record::name, {}, flag)
        .property("children", &Record::children, {}, flag)
        .property("map", &Record::map, {}, flag)
        .property("maybe", &Record::maybe, {}, flag)
        .property("dynamic", &Record::dynamic, {}, flag)
        .property("ignored", &Record::ignored);
    registration::class_<Ref>("BinaryRef").construct<>().property("id", &Ref::id, {}, flag);
    run();
    if (argc > 1)
        for (int i = 0; i < 20; ++i) {
            auto bytes = serialize_to_binary(object(std::uint64_t(UINT64_MAX)));
            assert(bytes.success);
            assert(deserialize_from_binary({bytes.bytes.data(), bytes.bytes.size()}).success);
        }
    std::cout << "reflection binary contracts passed\n";
}

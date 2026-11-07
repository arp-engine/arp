#pragma once
#include "core/container/array_view.h"
#include "reflection/serialization.h"

namespace arp::reflection {
struct ReflectionBinaryResult {
    bool success = false;
    ByteBuffer bytes{&core::defaultCpuMemoryResource()};
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
};

struct SerializationValueResult {
    bool success = false;
    SerializationValue value;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
};

ARP_API std::uint64_t serialization_schema(const typeinfo*, SerializationOptions options = {});
ARP_API ReflectionBinaryResult serialize_to_binary(const object&,
    SerializationOptions options = {},
    const SerializationContext* context = nullptr);
ARP_API ReflectionObjectResult deserialize_from_binary(core::ArrayView<const std::uint8_t>,
    SerializationOptions options = {},
    const SerializationContext* context = nullptr,
    const typeinfo* expected = nullptr);
// Domain codecs can persist validated non-C++ records using the same bounded wire codec.
ARP_API ReflectionBinaryResult encode_serialization_value(
    const SerializationValue&, SerializationOptions options = {});
ARP_API SerializationValueResult decode_serialization_value(
    core::ArrayView<const std::uint8_t>, SerializationOptions options = {});
} // namespace arp::reflection

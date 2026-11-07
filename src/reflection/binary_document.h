#pragma once
#include "core/encoding/value_contract.h"
#include "reflection/serialization_binary.h"

namespace arp::reflection {
struct BinaryDocumentError : std::exception {
    static constexpr const char* diagnosticId = "BINARY-DOCUMENT-FIELD-INVALID";

    explicit BinaryDocumentError(const char* message) : diagnostic(message) {}

    const char* what() const noexcept override { return diagnostic; }

    const char* diagnostic;
};

// Binary-only versioned value documents. No text decoder or YAML parser is used.
// Domain readers validate their explicit type/contractVersion and field schemas.
class BinaryDocument final : public core::ValueContract<SerializationValue, BinaryDocumentError> {
public:
    using Node = SerializationValue;

    static Node parseSingleDocument(core::StringView bytes)
    {
        SerializationOptions o;
        o.maxBytes = 64u * 1024u * 1024u;
        o.maxCollectionLength = 1024u * 1024u;
        o.maxValues = 4u * 1024u * 1024u;
        auto r = decode_serialization_value(
            {reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()}, o);
        if (!r.success)
            throw BinaryDocumentError{"BINARY-DOCUMENT-INVALID-RECOOK"};
        return std::move(r.value);
    }

    static core::pmr::String serialize(const Node& node)
    {
        SerializationOptions o;
        o.maxCollectionLength = 1024u * 1024u;
        o.maxValues = 4u * 1024u * 1024u;
        auto r = encode_serialization_value(node, o);
        if (!r.success)
            throw BinaryDocumentError{"BINARY-DOCUMENT-BUDGET"};
        return {reinterpret_cast<const char*>(r.bytes.data()),
            r.bytes.size(),
            &core::defaultCpuMemoryResource()};
    }
};
} // namespace arp::reflection

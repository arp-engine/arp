#include "core/encoding/base64.h"
#include "core/hash/sha1.h"

#include <cassert>

namespace {

arp::core::pmr::Vector<std::uint8_t> bytesFromText(arp::core::StringView text)
{
    arp::core::pmr::Vector<std::uint8_t> bytes{&arp::core::defaultCpuMemoryResource()};
    bytes.reserve(text.size());
    for (char c : text) {
        bytes.push_back(static_cast<std::uint8_t>(c));
    }
    return bytes;
}

void encodesAndDecodesBase64()
{
    const auto encoded = arp::core::Base64::encode(bytesFromText("hello"));
    assert(encoded == "aGVsbG8=");

    arp::core::pmr::Vector<std::uint8_t> decoded{&arp::core::defaultCpuMemoryResource()};
    assert(arp::core::Base64::decode(encoded, decoded));
    assert(decoded == bytesFromText("hello"));
    assert(!arp::core::Base64::decode("aGVsbG8", decoded));
}

void hashesWebSocketAcceptSource()
{
    const arp::core::StringView source =
        "dGhlIHNhbXBsZSBub25jZQ==258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    const auto accept = arp::core::Base64::encode(arp::core::Sha1::digest(source));
    assert(accept == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}

} // namespace

int main()
{
    encodesAndDecodesBase64();
    hashesWebSocketAcceptSource();
    return 0;
}

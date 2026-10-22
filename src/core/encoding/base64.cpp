#include "core/encoding/base64.h"

namespace arp::core {

namespace {

constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int decodeValue(char c) noexcept
{
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9') {
        return c - '0' + 52;
    }
    if (c == '+') {
        return 62;
    }
    if (c == '/') {
        return 63;
    }
    return -1;
}

} // namespace

pmr::String Base64::encode(const ByteBuffer& bytes)
{
    pmr::String out{&defaultCpuMemoryResource()};
    out.reserve(((bytes.size() + 2) / 3) * 4);
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const std::uint32_t b0 = bytes[i];
        const std::uint32_t b1 = (i + 1 < bytes.size()) ? bytes[i + 1] : 0;
        const std::uint32_t b2 = (i + 2 < bytes.size()) ? bytes[i + 2] : 0;
        const std::uint32_t triple = (b0 << 16) | (b1 << 8) | b2;
        out.push_back(kAlphabet[(triple >> 18) & 0x3f]);
        out.push_back(kAlphabet[(triple >> 12) & 0x3f]);
        out.push_back(i + 1 < bytes.size() ? kAlphabet[(triple >> 6) & 0x3f] : '=');
        out.push_back(i + 2 < bytes.size() ? kAlphabet[triple & 0x3f] : '=');
    }
    return out;
}

bool Base64::decode(StringView text, ByteBuffer& out)
{
    out.clear();
    if (text.empty() || (text.size() % 4) != 0) {
        return false;
    }
    out.reserve((text.size() / 4) * 3);
    for (std::size_t i = 0; i < text.size(); i += 4) {
        const char c0 = text[i];
        const char c1 = text[i + 1];
        const char c2 = text[i + 2];
        const char c3 = text[i + 3];
        const int v0 = decodeValue(c0);
        const int v1 = decodeValue(c1);
        const int v2 = c2 == '=' ? 0 : decodeValue(c2);
        const int v3 = c3 == '=' ? 0 : decodeValue(c3);
        if (v0 < 0 || v1 < 0 || v2 < 0 || v3 < 0) {
            out.clear();
            return false;
        }
        if (c2 == '=' && c3 != '=') {
            out.clear();
            return false;
        }
        if ((c2 == '=' || c3 == '=') && i + 4 != text.size()) {
            out.clear();
            return false;
        }
        const std::uint32_t triple = (static_cast<std::uint32_t>(v0) << 18)
            | (static_cast<std::uint32_t>(v1) << 12) | (static_cast<std::uint32_t>(v2) << 6)
            | static_cast<std::uint32_t>(v3);
        out.push_back(static_cast<std::uint8_t>((triple >> 16) & 0xff));
        if (c2 != '=') {
            out.push_back(static_cast<std::uint8_t>((triple >> 8) & 0xff));
        }
        if (c3 != '=') {
            out.push_back(static_cast<std::uint8_t>(triple & 0xff));
        }
    }
    return true;
}

} // namespace arp::core

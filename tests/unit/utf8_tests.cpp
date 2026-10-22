#include "core/text/utf8.h"

#include <array>
#include <cassert>

int main()
{
    using namespace arp::core;
    const std::string_view canonical{"\0A\xC2\xA2\xE4\xB8\xAD\xF0\x9F\x98\x80", 11u};
    const std::array<std::uint32_t, 5> expected{0u, 65u, 0xA2u, 0x4E2Du, 0x1F600u};
    std::array<std::uint32_t, 5> observed{};
    std::size_t count = 0u;
    const auto allocations = defaultCpuAllocator().stats().allocationCount;
    auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
    assert(!Utf8Decoder::visit(canonical, [&](std::uint32_t value) noexcept {
        observed[count++] = value;
    }));
    assert(count == expected.size() && observed == expected);
    assert(!Utf8Decoder::validate({}));
    const std::array<std::string_view, 8> invalid{"\x80",
        "\xFF",
        "\xE4\xB8",
        "\xC2\x41",
        "\xC0\x80",
        "\xED\xA0\x80",
        "\xF4\x90\x80\x80",
        "\xF0\x80\x80\x80"};
    for (auto value : invalid)
        assert(Utf8Decoder::validate(value));
    assert(defaultCpuAllocator().stats().allocationCount == allocations);
    std::pmr::set_default_resource(previous);
    pmr::String text(canonical);
    const auto decoded = decodeUtf8(text);
    assert(decoded.success && decoded.codepoints.size() == expected.size());
    pmr::String encoded, diagnostic;
    for (std::size_t i = 0; i < expected.size(); ++i) {
        assert(decoded.codepoints[i] == expected[i]);
        assert(appendUtf8Codepoint(encoded, expected[i], diagnostic));
    }
    assert(encoded == text);
    for (auto value : invalid) {
        const auto result = decodeUtf8(pmr::String(value));
        assert(!result.success && result.diagnostics == Utf8Decoder::validate(value));
    }
    for (auto value : {0xD800u, 0xDFFFu, 0x110000u}) {
        assert(!appendUtf8Codepoint(encoded, value, diagnostic));
        assert(encoded == text && !diagnostic.empty());
    }
}

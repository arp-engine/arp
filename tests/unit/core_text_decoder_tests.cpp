#include "core/text/text_decoder.h"

#include <cassert>
#include <cstdio>

using namespace arp::core;

int main()
{
    TextDecodeDesc desc;
    assert(TextDecoder::validate(desc) == TextDecodeStatus::InvalidDescriptor);
    desc.maxOutputBytes = 64;
    const auto raw = std::string_view("\xEF\xBB\xBFhello\r\n");
    auto result = TextDecoder::decode(raw, desc);
    assert(result.status == TextDecodeStatus::Success && result.text == raw);
    assert(result.sourceBytes == raw.size() && result.hadBom);
    desc.bom = TextBomPolicy::Strip;
    result = TextDecoder::decode(raw, desc);
    assert(result.status == TextDecodeStatus::Success && result.text == "hello\r\n");
    assert(result.sourceBytes == raw.size() && result.hadBom);
    desc.bom = TextBomPolicy::Reject;
    result = TextDecoder::decode(raw, desc);
    assert(result.status == TextDecodeStatus::UnexpectedBom && result.text.empty());
    desc.bom = TextBomPolicy::RequireAndStrip;
    assert(TextDecoder::decode("hello", desc).status == TextDecodeStatus::MissingBom);
    assert(TextDecoder::decode(raw, desc).text == "hello\r\n");
    desc.bom = TextBomPolicy::Strip;
    desc.maxOutputBytes = 7;
    assert(TextDecoder::decode(raw, desc).status == TextDecodeStatus::Success);
    desc.maxOutputBytes = 6;
    assert(TextDecoder::decode(raw, desc).status == TextDecodeStatus::ByteBudgetExceeded);
    desc.maxOutputBytes = 64;
    const std::string_view embedded("a\0b", 3);
    assert(TextDecoder::decode(embedded, desc).status == TextDecodeStatus::EmbeddedNul);
    desc.nul = TextNulPolicy::Preserve;
    assert(TextDecoder::decode(embedded, desc).text.size() == 3);
    for (const auto bytes : {std::string_view("\xFF\xFE"),
             std::string_view("\xE4\xB8"),
             std::string_view("\xED\xA0\x80")}) {
        result = TextDecoder::decode(bytes, desc);
        assert(result.status == TextDecodeStatus::InvalidUtf8 && result.text.empty());
        assert(result.diagnosticId && result.detail);
    }
    assert(TextDecoder::decode(u8"路径\r\n", desc).text == u8"路径\r\n");
    desc.maxOutputBytes = 0;
    assert(TextDecoder::decode({}, desc).status == TextDecodeStatus::Success);
    assert(TextDecoder::decode("\xEF\xBB\xBF", desc).status == TextDecodeStatus::Success);
    desc.encoding = static_cast<TextEncoding>(255);
    assert(TextDecoder::decode({}, desc).status == TextDecodeStatus::UnsupportedEncoding);
    desc.encoding = TextEncoding::Utf8;
    desc.contractVersion = 2;
    assert(TextDecoder::decode({}, desc).status == TextDecodeStatus::InvalidDescriptor);
    std::puts("Explicit UTF-8/BOM/NUL/output budget and exact newline contract PASS");
}

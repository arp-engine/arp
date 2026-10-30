#include "core/text/text_decoder.h"

#include "core/text/utf8.h"

#include <new>

namespace arp::core {

TextDecodeStatus TextDecoder::validate(const TextDecodeDesc& desc) noexcept
{
    if (desc.contractVersion != 1 || desc.maxOutputBytes == UINT64_MAX
        || desc.maxOutputBytes > SIZE_MAX)
        return TextDecodeStatus::InvalidDescriptor;
    if (desc.encoding != TextEncoding::Utf8)
        return TextDecodeStatus::UnsupportedEncoding;
    if (desc.bom != TextBomPolicy::Preserve && desc.bom != TextBomPolicy::Strip
        && desc.bom != TextBomPolicy::Reject && desc.bom != TextBomPolicy::RequireAndStrip)
        return TextDecodeStatus::InvalidDescriptor;
    if (desc.nul != TextNulPolicy::Reject && desc.nul != TextNulPolicy::Preserve)
        return TextDecodeStatus::InvalidDescriptor;
    return TextDecodeStatus::Success;
}

TextDecodeResult TextDecoder::decode(std::string_view source, const TextDecodeDesc& desc) noexcept
{
    TextDecodeResult result;
    result.sourceBytes = source.size();
    result.status = validate(desc);
    if (result.status != TextDecodeStatus::Success) {
        result.diagnosticId = result.status == TextDecodeStatus::UnsupportedEncoding
            ? "TEXT-ENCODING-UNSUPPORTED"
            : "TEXT-DESCRIPTOR";
        return result;
    }
    constexpr std::string_view bom = "\xEF\xBB\xBF";
    result.hadBom = source.substr(0, bom.size()) == bom;
    if (result.hadBom && desc.bom == TextBomPolicy::Reject) {
        result.status = TextDecodeStatus::UnexpectedBom;
        result.diagnosticId = "TEXT-BOM-REJECTED";
        return result;
    }
    if (!result.hadBom && desc.bom == TextBomPolicy::RequireAndStrip) {
        result.status = TextDecodeStatus::MissingBom;
        result.diagnosticId = "TEXT-BOM-REQUIRED";
        return result;
    }
    if (result.hadBom
        && (desc.bom == TextBomPolicy::Strip || desc.bom == TextBomPolicy::RequireAndStrip))
        source.remove_prefix(bom.size());
    if (source.size() > desc.maxOutputBytes) {
        result.status = TextDecodeStatus::ByteBudgetExceeded;
        result.diagnosticId = "TEXT-OUTPUT-BUDGET";
        return result;
    }
    if (desc.nul == TextNulPolicy::Reject && source.find('\0') != source.npos) {
        result.status = TextDecodeStatus::EmbeddedNul;
        result.diagnosticId = "TEXT-NUL-REJECTED";
        return result;
    }
    if (const auto* diagnostic = Utf8Decoder::validate(source)) {
        result.status = TextDecodeStatus::InvalidUtf8;
        result.diagnosticId = "TEXT-INVALID-UTF8";
        result.detail = diagnostic;
        return result;
    }
    try {
        if (!source.empty())
            result.text.assign(source.data(), source.size());
    } catch (...) {
        result.status = TextDecodeStatus::AllocationFailed;
        result.diagnosticId = "TEXT-ALLOCATION";
    }
    return result;
}

} // namespace arp::core

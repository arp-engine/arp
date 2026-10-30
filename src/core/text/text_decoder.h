#pragma once

#include "core/memory/cpu_allocator.h"

#include <cstdint>
#include <string_view>

namespace arp::core {

enum class TextEncoding : std::uint8_t { Utf8 };
enum class TextBomPolicy : std::uint8_t { Preserve, Strip, Reject, RequireAndStrip };
enum class TextNulPolicy : std::uint8_t { Reject, Preserve };
enum class TextDecodeStatus : std::uint8_t {
    Success,
    InvalidDescriptor,
    UnsupportedEncoding,
    ByteBudgetExceeded,
    InvalidUtf8,
    UnexpectedBom,
    MissingBom,
    EmbeddedNul,
    AllocationFailed,
};

struct TextDecodeDesc {
    std::uint32_t contractVersion = 1;
    TextEncoding encoding = TextEncoding::Utf8;
    TextBomPolicy bom = TextBomPolicy::Preserve;
    TextNulPolicy nul = TextNulPolicy::Reject;
    // Explicit output-byte budget. Zero accepts empty output; UINT64_MAX is
    // unconfigured and rejected. CR/LF bytes are always preserved in version1.
    std::uint64_t maxOutputBytes = UINT64_MAX;
};

struct TextDecodeResult {
    TextDecodeStatus status = TextDecodeStatus::Success;
    const char* diagnosticId = nullptr;
    const char* detail = nullptr;
    pmr::String text{&defaultCpuMemoryResource()};
    std::uint64_t sourceBytes = 0;
    bool hadBom = false;
};

// Pure byte-to-text conversion. Reuses Utf8Decoder, never probes a file,
// guesses an encoding, normalizes newlines or replaces malformed scalars.
class ARP_API TextDecoder final {
public:
    static TextDecodeStatus validate(const TextDecodeDesc& desc) noexcept;
    static TextDecodeResult decode(std::string_view source, const TextDecodeDesc& desc) noexcept;
};

} // namespace arp::core

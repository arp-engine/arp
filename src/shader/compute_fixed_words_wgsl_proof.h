#pragma once
#include "core/ascii_token_cursor.h"
#include "shader/compute_indirect_producer_proof.h"

namespace arp::shader {
// Complete, allocation-free WGSL acceptance language paired with the existing
// complete five-word SPIR-V producer language. Unknown programs fail closed.
class ComputeFixedWordsWgslProof final {
public:
    struct Facts {
        std::uint32_t contractVersion = 0u, group = 0u, binding = 0u;
        std::uint64_t byteOffset = 0u, byteCount = 0u;
        std::array<std::uint32_t, ComputeIndirectProducerProof::wordCount> words{};
    };

    static const char* verify(
        core::StringView source, core::StringView entry, Facts& output) noexcept
    {
        output = {};
        ComputeFixedWordsWgslProof parser(source);
        Facts facts;
        if (!parser.run(entry, facts))
            return parser.tokens_.diagnostic() ? parser.tokens_.diagnostic()
                                               : "SHADER-WGSL-COMPUTE-FIXED-WORDS";
        facts.contractVersion = 1u;
        facts.byteCount = sizeof(std::uint32_t) * facts.words.size();
        output = facts;
        return nullptr;
    }

private:
    explicit ComputeFixedWordsWgslProof(core::StringView source) noexcept : tokens_(source) {}

    bool take(std::initializer_list<core::StringView> sequence) noexcept
    {
        for (const auto token : sequence)
            if (!tokens_.consume(token))
                return false;
        return true;
    }

    bool run(core::StringView entry, Facts& facts) noexcept
    {
        core::StringView resource, actualEntry;
        if (entry.empty() || !take({"@", "binding", "("}) || !tokens_.decimalUint32(facts.binding)
            || !take({")", "@", "group", "("}) || !tokens_.decimalUint32(facts.group)
            || !take({")", "var", "<", "storage", ",", "read_write", ">"})
            || !tokens_.identifier(resource)
            || !take({":",
                "array",
                "<",
                "u32",
                ">",
                ";",
                "@",
                "compute",
                "@",
                "workgroup_size",
                "(",
                "1",
                ",",
                "1",
                ",",
                "1",
                ")",
                "fn"})
            || !tokens_.identifier(actualEntry) || actualEntry != entry || !take({"(", ")", "{"}))
            return false;
        for (std::uint32_t index = 0u; index < facts.words.size(); ++index) {
            std::uint32_t actualIndex = 0u;
            if (!tokens_.consume(resource) || !take({"[", "i32", "("})
                || !tokens_.decimalUint32(actualIndex) || actualIndex != index
                || !take({")", "]", "=", "u32", "("}) || !tokens_.decimalUint32(facts.words[index])
                || !take({")", ";"}))
                return false;
        }
        return take({"return", ";", "}"}) && tokens_.atEnd();
    }

    core::AsciiTokenCursor tokens_;
};
} // namespace arp::shader

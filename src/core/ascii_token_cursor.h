#pragma once
#include "core/string_utils.h"

#include <charconv>
#include <cstdint>

namespace arp::core {
// Borrowed ASCII lexical profile: identifiers, decimal digits and single
// punctuation characters. Whitespace, // comments and nested /* */ comments
// are trivia. Strings, escaped identifiers, non-ASCII tokens and number
// suffixes are unsupported in IntegerOnly. FixedDecimalFloat explicitly admits
// digits '.' digits with optional lowercase f; no exponent/other suffix guessing.
class AsciiTokenCursor final {
public:
    enum class Profile { IntegerOnly, FixedDecimalFloat };
    enum class Kind { End, Identifier, Decimal, FixedDecimal, Punctuation, Invalid };

    struct Token {
        Kind kind = Kind::End;
        std::string_view text;
    };

    explicit AsciiTokenCursor(
        std::string_view source, Profile profile = Profile::IntegerOnly) noexcept
        : source_(source), profile_(profile)
    {}

    const char* diagnostic() const noexcept { return diagnostic_; }

    std::size_t offset() const noexcept { return offset_; }

    Token next() noexcept
    {
        if (!trivia())
            return {Kind::Invalid, {}};
        if (offset_ == source_.size())
            return {};
        const auto begin = offset_++;
        const auto first = source_[begin];
        Kind kind = Kind::Punctuation;
        if (identifierStart(first)) {
            kind = Kind::Identifier;
            while (offset_ < source_.size() && identifierContinue(source_[offset_]))
                ++offset_;
        } else if (digit(first)) {
            kind = Kind::Decimal;
            while (offset_ < source_.size() && digit(source_[offset_]))
                ++offset_;
            if (profile_ == Profile::FixedDecimalFloat && offset_ < source_.size()
                && source_[offset_] == '.') {
                kind = Kind::FixedDecimal;
                ++offset_;
                const auto fraction = offset_;
                while (offset_ < source_.size() && digit(source_[offset_]))
                    ++offset_;
                if (offset_ == fraction)
                    return reject("CORE-ASCII-TOKEN-FIXED-DECIMAL");
                if (offset_ < source_.size() && source_[offset_] == 'f')
                    ++offset_;
            }
            if (offset_ < source_.size() && identifierStart(source_[offset_]))
                return reject("CORE-ASCII-TOKEN-NUMBER-SUFFIX");
        } else if (static_cast<unsigned char>(first) < 0x21u
            || static_cast<unsigned char>(first) > 0x7eu || first == '"' || first == '\''
            || first == '\\' || first == '`')
            return reject("CORE-ASCII-TOKEN-UNSUPPORTED");
        return {kind, source_.substr(begin, offset_ - begin)};
    }

    bool consume(std::string_view expected) noexcept
    {
        const auto token = next();
        return token.kind != Kind::Invalid && token.kind != Kind::End && token.text == expected;
    }

    bool identifier(std::string_view& output) noexcept
    {
        output = {};
        const auto token = next();
        if (token.kind != Kind::Identifier)
            return false;
        output = token.text;
        return true;
    }

    bool decimalUint32(std::uint32_t& output, bool canonical = true) noexcept
    {
        output = 0u;
        const auto token = next();
        if (token.kind != Kind::Decimal
            || (canonical && token.text.size() > 1u && token.text.front() == '0'))
            return false;
        std::uint32_t value = 0u;
        const auto parsed =
            std::from_chars(token.text.data(), token.text.data() + token.text.size(), value, 10);
        if (parsed.ec != std::errc{} || parsed.ptr != token.text.data() + token.text.size())
            return false;
        output = value;
        return true;
    }

    bool atEnd() noexcept { return next().kind == Kind::End; }

    // Declaration readers may leave executable expressions opaque. These
    // operations check trivia and delimiter framing, not expression grammar.
    // The opening delimiter has already been consumed by the caller.
    bool skipBalanced(char opening, char closing) noexcept
    {
        std::size_t depth = 1u;
        while (depth) {
            if (!trivia())
                return false;
            if (offset_ == source_.size()) {
                reject("CORE-ASCII-TOKEN-DELIMITER-UNTERMINATED");
                return false;
            }
            const auto c = source_[offset_++];
            if (!opaqueCharacter(c))
                return false;
            if (c == opening)
                ++depth;
            else if (c == closing)
                --depth;
        }
        return true;
    }

    bool skipOpaqueUntil(char terminal) noexcept
    {
        while (true) {
            if (!trivia())
                return false;
            if (offset_ == source_.size()) {
                reject("CORE-ASCII-TOKEN-DELIMITER-UNTERMINATED");
                return false;
            }
            const auto c = source_[offset_++];
            if (!opaqueCharacter(c))
                return false;
            if (c == terminal)
                return true;
        }
    }

private:
    bool opaqueCharacter(char c) noexcept
    {
        if ((static_cast<unsigned char>(c) >= 0x21u && static_cast<unsigned char>(c) <= 0x7eu)
            && c != '"' && c != '\'' && c != '\\' && c != '`')
            return true;
        reject("CORE-ASCII-TOKEN-UNSUPPORTED");
        return false;
    }

    static bool digit(char c) noexcept { return c >= '0' && c <= '9'; }

    static bool identifierStart(char c) noexcept
    {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    }

    static bool identifierContinue(char c) noexcept { return identifierStart(c) || digit(c); }

    Token reject(const char* id) noexcept
    {
        diagnostic_ = id;
        return {Kind::Invalid, {}};
    }

    bool trivia() noexcept
    {
        if (diagnostic_)
            return false;
        while (offset_ < source_.size()) {
            if (isAsciiWhitespace(source_[offset_])) {
                ++offset_;
                continue;
            }
            if (source_[offset_] != '/' || source_.size() - offset_ < 2u)
                return true;
            if (source_[offset_ + 1u] == '/') {
                offset_ += 2u;
                while (offset_ < source_.size() && source_[offset_] != '\n'
                    && source_[offset_] != '\r')
                    ++offset_;
            } else if (source_[offset_ + 1u] == '*') {
                offset_ += 2u;
                std::size_t depth = 1u;
                while (depth && offset_ < source_.size()) {
                    if (source_.size() - offset_ >= 2u && source_[offset_] == '/'
                        && source_[offset_ + 1u] == '*') {
                        ++depth;
                        offset_ += 2u;
                    } else if (source_.size() - offset_ >= 2u && source_[offset_] == '*'
                        && source_[offset_ + 1u] == '/') {
                        --depth;
                        offset_ += 2u;
                    } else
                        ++offset_;
                }
                if (depth) {
                    reject("CORE-ASCII-TOKEN-COMMENT-UNTERMINATED");
                    return false;
                }
            } else
                return true;
        }
        return true;
    }

    const std::string_view source_;
    const Profile profile_;
    std::size_t offset_ = 0u;
    const char* diagnostic_ = nullptr;
};
} // namespace arp::core

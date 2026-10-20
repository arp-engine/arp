#pragma once

#include "core/cpu_allocator.h"
#include "core/string_utils.h"

#include <algorithm>
#include <string_view>
#include <utility>

namespace arp::tests {

// Source-contract evidence keeps original offsets and literal bytes. Only
// whitespace between complete tokens is ignored; this is not a C++ parser.
class CppSourceEvidence final : public core::pmr::String {
    using String = core::pmr::String;

    class Cursor final {
    public:
        explicit Cursor(
            std::string_view text, std::size_t offset = 0, bool comments = false) noexcept
            : text_(text), offset_(offset), comments_(comments)
        {}

        std::size_t offset() const noexcept { return offset_; }

        std::string_view next() noexcept
        {
            while (offset_ < text_.size()) {
                if (core::isAsciiWhitespace(text_[offset_])) {
                    ++offset_;
                    continue;
                }
                if (text_.substr(offset_, 2) == "//") {
                    const auto begin = offset_;
                    const auto end = text_.find('\n', offset_);
                    offset_ = end == text_.npos ? text_.size() : end;
                    if (comments_)
                        return text_.substr(begin, offset_ - begin);
                    continue;
                }
                if (text_.substr(offset_, 2) == "/*") {
                    const auto begin = offset_;
                    const auto end = text_.find("*/", offset_ + 2);
                    offset_ = end == text_.npos ? text_.size() : end + 2;
                    if (comments_)
                        return text_.substr(begin, offset_ - begin);
                    continue;
                }
                break;
            }
            if (offset_ == text_.size())
                return {};
            const auto begin = offset_;
            const auto remaining = text_.substr(begin);
            if (remaining.substr(0, 2) == "R\"") {
                const auto opening = text_.find('(', begin + 2);
                if (opening == text_.npos)
                    offset_ = text_.size();
                else {
                    const auto delimiter = text_.substr(begin + 2, opening - begin - 2);
                    offset_ = opening + 1;
                    while (offset_ < text_.size()) {
                        if (text_[offset_] == ')'
                            && text_.substr(offset_ + 1, delimiter.size()) == delimiter
                            && offset_ + 1 + delimiter.size() < text_.size()
                            && text_[offset_ + 1 + delimiter.size()] == '"') {
                            offset_ += delimiter.size() + 2;
                            break;
                        }
                        ++offset_;
                    }
                }
            } else if (text_[begin] == '"' || text_[begin] == '\'') {
                const auto quote = text_[offset_++];
                while (offset_ < text_.size()) {
                    const auto value = text_[offset_++];
                    if (value == '\\' && offset_ < text_.size())
                        ++offset_;
                    else if (value == quote)
                        break;
                }
            } else if (word(text_[begin])) {
                const bool number = text_[begin] >= '0' && text_[begin] <= '9';
                do {
                    ++offset_;
                } while (offset_ < text_.size()
                    && (word(text_[offset_])
                        || (number && (text_[offset_] == '.' || text_[offset_] == '\''))));
            } else {
                ++offset_;
                for (const std::string_view operation : {"===",
                         "!==",
                         "?"
                         "?=",
                         "**=",
                         "<=>",
                         "<<=",
                         ">>=",
                         "->*",
                         "...",
                         "=>",
                         "??",
                         "?.",
                         "**",
                         "::",
                         "->",
                         "++",
                         "--",
                         "&&",
                         "||",
                         "==",
                         "!=",
                         "<=",
                         ">=",
                         "<<",
                         ">>",
                         "+=",
                         "-=",
                         "*=",
                         "/=",
                         "%=",
                         "&=",
                         "|=",
                         "^="}) {
                    if (remaining.substr(0, operation.size()) == operation) {
                        offset_ = begin + operation.size();
                        break;
                    }
                }
            }
            return text_.substr(begin, offset_ - begin);
        }

    private:
        static bool word(char value) noexcept
        {
            return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z')
                || (value >= '0' && value <= '9') || value == '_';
        }

        const std::string_view text_;
        std::size_t offset_ = 0;
        const bool comments_ = false;
    };

public:
    CppSourceEvidence(String text) : String(std::move(text)) {}

    using String::find;

    std::size_t find(const char* expected, std::size_t start = 0) const
    {
        return find(std::string_view(expected), start);
    }

    std::size_t find(std::string_view expected, std::size_t start = 0) const
    {
        const auto exact = String::find(expected, start);
        if (expected.empty() || start >= size())
            return exact;
        // Line/brace delimiters select existing scopes. Do not turn a newline
        // into a match on an earlier initializer or nested block boundary.
        Cursor count(expected, 0, true);
        const auto first = count.next();
        bool singleToken = true;
        for (auto token = first; !token.empty(); token = count.next()) {
            if (token.substr(0, 2) == "//" || token.substr(0, 2) == "/*")
                return exact;
            if (token.data() != first.data())
                singleToken = false;
        }
        if (singleToken
            && (core::isAsciiWhitespace(expected.front())
                || core::isAsciiWhitespace(expected.back())))
            return exact;
        Cursor candidates(*this, start);
        for (;;) {
            const auto token = candidates.next();
            if (token.empty())
                return exact;
            const auto position = candidates.offset() - token.size();
            if (exact != npos && position >= exact)
                return exact;
            if (token != first)
                continue;
            Cursor source(*this, candidates.offset());
            Cursor wanted(expected);
            wanted.next();
            for (;;) {
                const auto next = wanted.next();
                if (next.empty())
                    return position;
                if (source.next() != next)
                    break;
            }
        }
    }

    CppSourceEvidence substr(std::size_t position = 0, std::size_t count = npos) const
    {
        return CppSourceEvidence(String::substr(position, count));
    }
};

} // namespace arp::tests

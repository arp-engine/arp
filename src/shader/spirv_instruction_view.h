#pragma once

#include "core/encoding/unsigned_element_span.h"

namespace arp::shader {

// Borrowed, alignment-independent little-endian SPIR-V instruction framing.
// This verifies framing only; consuming contracts own opcode/operand semantics.
class SpirvInstructionView final {
public:
    static constexpr std::uint64_t headerWords = 5u;

    class Instruction final {
        friend class SpirvInstructionView;

    public:
        std::uint32_t opcode() const noexcept { return opcode_; }

        std::uint32_t operands() const noexcept { return count_ ? count_ - 1u : 0u; }

        std::uint32_t operand(std::uint32_t index) const noexcept
        {
            std::uint32_t value = 0u;
            if (words_ && index < operands())
                words_->read(offset_ + 1u + index, value);
            return value;
        }

        std::uint64_t next() const noexcept { return offset_ + count_; }

        bool hasLiteralString(
            std::uint32_t firstOperand, std::uint32_t trailingOperands = 0u) const noexcept
        {
            if (firstOperand >= operands() || trailingOperands >= operands() - firstOperand)
                return false;
            const auto last = operands() - trailingOperands;
            for (auto word = firstOperand; word < last; ++word) {
                const auto value = operand(word);
                for (unsigned byte = 0u; byte < sizeof(std::uint32_t); ++byte) {
                    if (((value >> (byte * 8u)) & 0xffu) != 0u)
                        continue;
                    // The terminator and padding occupy only the last literal word.
                    if (word + 1u != last)
                        return false;
                    return byte + 1u == sizeof(std::uint32_t) || !(value >> ((byte + 1u) * 8u));
                }
            }
            return false;
        }

        // Exact null-terminated/padded literal string and explicit trailing IDs.
        bool stringEquals(std::uint32_t firstOperand,
            core::StringView expected,
            std::uint32_t trailingOperands = 0u) const noexcept
        {
            if (firstOperand > operands() || trailingOperands > operands() - firstOperand
                || expected.size() >= std::uint64_t{operands() - firstOperand - trailingOperands}
                        * sizeof(std::uint32_t))
                return false;
            const auto words = (expected.size() + sizeof(std::uint32_t)) / sizeof(std::uint32_t);
            if (std::uint64_t{firstOperand} + words + trailingOperands != operands())
                return false;
            for (std::size_t byte = 0u; byte < words * sizeof(std::uint32_t); ++byte) {
                const auto character =
                    static_cast<std::uint8_t>(operand(firstOperand + byte / sizeof(std::uint32_t))
                        >> ((byte % sizeof(std::uint32_t)) * 8u));
                if (character
                    != (byte < expected.size() ? static_cast<std::uint8_t>(expected[byte]) : 0u))
                    return false;
            }
            return true;
        }

    private:
        const core::UnsignedElementSpan* words_ = nullptr;
        std::uint64_t offset_ = 0u;
        std::uint32_t opcode_ = 0u, count_ = 0u;
    };

    SpirvInstructionView(const void* data, std::size_t bytes) noexcept
        : words_(data,
              bytes,
              core::UnsignedElementSpan::Width::Uint32,
              core::UnsignedElementSpan::ByteOrder::LittleEndian)
    {
        std::uint32_t magic = 0u, version = 0u, schema = 0u;
        if (words_.diagnostic() || words_.size() < headerWords || !words_.read(0u, magic)
            || magic != 0x07230203u) {
            diagnostic_ = "SHADER-SPIRV-BINARY";
            return;
        }
        words_.read(1u, version);
        words_.read(3u, bound_);
        words_.read(4u, schema);
        if (version < 0x00010000u || version > 0x00010600u || (version & 0xffu) || !bound_
            || schema) {
            diagnostic_ = "SHADER-SPIRV-HEADER";
            return;
        }
        for (auto offset = headerWords; offset < words_.size();) {
            Instruction instruction;
            if (!read(offset, instruction)) {
                diagnostic_ = "SHADER-SPIRV-INSTRUCTION";
                return;
            }
            offset = instruction.next();
        }
    }

    const char* diagnostic() const noexcept { return diagnostic_; }

    std::uint32_t idBound() const noexcept { return bound_; }

    std::uint64_t sizeWords() const noexcept { return words_.size(); }

    bool read(std::uint64_t offset, Instruction& result) const noexcept
    {
        result = {};
        std::uint32_t header = 0u;
        if (diagnostic_ || offset < headerWords || !words_.read(offset, header))
            return false;
        const auto count = header >> 16u;
        if (!core::containsUnsignedRange(
                std::uint64_t{0}, words_.size(), offset, std::uint64_t{count}))
            return false;
        result.words_ = &words_;
        result.offset_ = offset;
        result.count_ = count;
        result.opcode_ = header & 0xffffu;
        return true;
    }

    // Grammar policy belongs to the consumer. The shared resolver checks the
    // complete module for duplicate definitions, without allocating an ID map.
    template <class ResultOperand>
    bool definition(
        std::uint32_t id, ResultOperand&& resultOperand, Instruction& out) const noexcept
    {
        out = {};
        if (diagnostic_ || !id || id >= bound_)
            return false;
        bool found = false;
        for (auto offset = headerWords; offset < sizeWords();) {
            Instruction i;
            if (!read(offset, i))
                return false;
            offset = i.next();
            const auto index = resultOperand(i);
            if (index < 0 || static_cast<std::uint32_t>(index) >= i.operands()
                || i.operand(index) != id)
                continue;
            if (found)
                return false;
            out = i;
            found = true;
        }
        return found;
    }

    // Core OpDecorate grammar. Marker/value arity is explicit; duplicates and
    // malformed matches reject even if the first decoration was plausible.
    bool decoration(std::uint32_t id,
        std::uint32_t kind,
        std::uint32_t& value,
        bool marker = false) const noexcept
    {
        value = 0u;
        if (diagnostic_ || !id || id >= bound_)
            return false;
        bool found = false;
        for (auto offset = headerWords; offset < sizeWords();) {
            Instruction i;
            if (!read(offset, i))
                return false;
            offset = i.next();
            if (i.opcode() != 71u || i.operand(0) != id || i.operand(1) != kind)
                continue;
            if (found || i.operands() != (marker ? 2u : 3u))
                return false;
            found = true;
            value = marker ? 0u : i.operand(2);
        }
        return found;
    }
    enum class DecorationStatus { Missing, Valid, Invalid };

    DecorationStatus memberDecoration(std::uint32_t id,
        std::uint32_t member,
        std::uint32_t kind,
        std::uint32_t& value,
        bool marker = false) const noexcept
    {
        value = 0u;
        if (diagnostic_ || !id || id >= bound_)
            return DecorationStatus::Invalid;
        bool found = false;
        for (auto offset = headerWords; offset < sizeWords();) {
            Instruction i;
            if (!read(offset, i))
                return DecorationStatus::Invalid;
            offset = i.next();
            if (i.opcode() != 72u || i.operand(0) != id || i.operand(1) != member
                || i.operand(2) != kind)
                continue;
            if (found || i.operands() != (marker ? 3u : 4u))
                return DecorationStatus::Invalid;
            found = true;
            value = marker ? 0u : i.operand(3);
        }
        return found ? DecorationStatus::Valid : DecorationStatus::Missing;
    }

private:
    core::UnsignedElementSpan words_;
    const char* diagnostic_ = nullptr;
    std::uint32_t bound_ = 0u;
};

} // namespace arp::shader

#pragma once
#include "shader/spirv_instruction_view.h"

#include <algorithm>
#include <cassert>

namespace arp::test {
class SpirvBinaryMutationFixture {
public:
    explicit SpirvBinaryMutationFixture(core::ByteBuffer source)
        : original_(std::move(source)), bytes_(original_)
    {}

    void reset() { bytes_ = original_; }

    const auto& bytes() const noexcept { return bytes_; }

    std::uint64_t locate(std::uint32_t opcode,
        int operand = -1,
        std::uint32_t value = 0u,
        std::size_t ordinal = 0u) const
    {
        shader::SpirvInstructionView view(bytes_.data(), bytes_.size());
        assert(!view.diagnostic());
        for (auto offset = shader::SpirvInstructionView::headerWords; offset < view.sizeWords();) {
            shader::SpirvInstructionView::Instruction i;
            assert(view.read(offset, i));
            if (i.opcode() == opcode && (operand < 0 || i.operand(operand) == value)) {
                if (!ordinal)
                    return offset;
                --ordinal;
            }
            offset = i.next();
        }
        assert(false);
        return 0u;
    }

    std::uint32_t operand(std::uint64_t offset, std::uint32_t index) const
    {
        shader::SpirvInstructionView view(bytes_.data(), bytes_.size());
        shader::SpirvInstructionView::Instruction i;
        assert(view.read(offset, i));
        return i.operand(index);
    }

    void word(std::uint64_t index, std::uint32_t value)
    {
        assert(index * 4u + 4u <= bytes_.size());
        for (unsigned j = 0u; j < 4u; ++j)
            bytes_[index * 4u + j] = static_cast<std::uint8_t>(value >> (j * 8u));
    }

    void patch(std::uint32_t opcode,
        std::uint32_t index,
        std::uint32_t value,
        int match = -1,
        std::uint32_t matchValue = 0u)
    {
        word(locate(opcode, match, matchValue) + 1u + index, value);
    }

    void remove(std::uint32_t opcode, int match = -1, std::uint32_t matchValue = 0u)
    {
        const auto offset = locate(opcode, match, matchValue);
        shader::SpirvInstructionView view(bytes_.data(), bytes_.size());
        shader::SpirvInstructionView::Instruction i;
        assert(view.read(offset, i));
        bytes_.erase(bytes_.begin() + offset * 4u, bytes_.begin() + i.next() * 4u);
    }

    void duplicateDecoration(std::uint32_t decoration)
    {
        const auto offset = locate(71u, 1, decoration);
        shader::SpirvInstructionView view(bytes_.data(), bytes_.size());
        shader::SpirvInstructionView::Instruction i;
        assert(view.read(offset, i));
        core::ByteBuffer duplicate(bytes_.begin() + offset * 4u,
            bytes_.begin() + i.next() * 4u,
            &core::defaultCpuMemoryResource());
        const auto function = locate(54u);
        bytes_.insert(bytes_.begin() + function * 4u, duplicate.begin(), duplicate.end());
    }

    void replaceInstruction(
        std::uint64_t offset, std::uint32_t opcode, std::initializer_list<std::uint32_t> operands)
    {
        shader::SpirvInstructionView view(bytes_.data(), bytes_.size());
        shader::SpirvInstructionView::Instruction old;
        assert(view.read(offset, old) && operands.size() < 65535u);
        auto encoded = encode(opcode, operands);
        bytes_.erase(bytes_.begin() + offset * 4u, bytes_.begin() + old.next() * 4u);
        bytes_.insert(bytes_.begin() + offset * 4u, encoded.begin(), encoded.end());
    }

    void swapAdjacentInstructions(std::uint64_t offset)
    {
        shader::SpirvInstructionView view(bytes_.data(), bytes_.size());
        shader::SpirvInstructionView::Instruction first, second;
        assert(view.read(offset, first) && view.read(first.next(), second));
        std::rotate(bytes_.begin() + offset * 4u,
            bytes_.begin() + first.next() * 4u,
            bytes_.begin() + second.next() * 4u);
    }

    void insertInstruction(
        std::uint64_t offset, std::uint32_t opcode, std::initializer_list<std::uint32_t> operands)
    {
        shader::SpirvInstructionView view(bytes_.data(), bytes_.size());
        shader::SpirvInstructionView::Instruction next;
        assert(view.read(offset, next) && operands.size() < 65535u);
        auto encoded = encode(opcode, operands);
        bytes_.insert(bytes_.begin() + offset * 4u, encoded.begin(), encoded.end());
    }

private:
    static core::ByteBuffer encode(
        std::uint32_t opcode, std::initializer_list<std::uint32_t> operands)
    {
        core::ByteBuffer bytes{&core::defaultCpuMemoryResource()};
        const auto append = [&](std::uint32_t value) {
            for (unsigned byte = 0u; byte < 4u; ++byte)
                bytes.push_back(static_cast<std::uint8_t>(value >> (byte * 8u)));
        };
        append((static_cast<std::uint32_t>(operands.size() + 1u) << 16u) | opcode);
        for (const auto value : operands)
            append(value);
        return bytes;
    }

    core::ByteBuffer original_, bytes_;
};
} // namespace arp::test

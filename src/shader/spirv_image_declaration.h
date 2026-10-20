#pragma once
#include "shader/spirv_instruction_view.h"

namespace arp::shader {
// Cold, allocation-free image declaration proof for a direct image or one
// fixed descriptor array (the finite table ABI). These are binary shape facts;
// they grant no device, view, sampling or execution capability. Reuses the
// common strict framing/definition/decoration reader rather than an ID parser.
struct SpirvImageDeclaration {
    std::uint32_t descriptorCount = 0u, dimension = 0u, depth = 0u, arrayed = 0u, multisampled = 0u,
                  sampled = 0u, format = 0u, scalarWidth = 0u;
    bool floatScalar = false;
};

class SpirvImageDeclarationVerifier final {
    using I = SpirvInstructionView::Instruction;

    static int resultOperand(const I& i) noexcept
    {
        if (i.opcode() >= 19u && i.opcode() <= 33u)
            return 0;
        return i.opcode() == 43u || i.opcode() == 50u || i.opcode() == 59u ? 1 : -1;
    }

    static const char* inspectResource(const void* data,
        std::size_t bytes,
        std::uint32_t set,
        std::uint32_t binding,
        SpirvImageDeclaration& output,
        bool sampler) noexcept
    {
        output = {};
        SpirvInstructionView module(data, bytes);
        if (const auto* id = module.diagnostic())
            return id;
        I variable;
        bool found = false;
        for (auto offset = SpirvInstructionView::headerWords; offset < module.sizeWords();) {
            I i;
            if (!module.read(offset, i))
                return "SHADER-IMAGE-DECLARATION-INSTRUCTION";
            offset = i.next();
            if (i.opcode() != 59u || i.operand(2) != 0u)
                continue;
            std::uint32_t declaredSet = 0u, declaredBinding = 0u;
            if ((i.operands() != 3u && i.operands() != 4u)
                || !module.decoration(i.operand(1), 34u, declaredSet)
                || !module.decoration(i.operand(1), 33u, declaredBinding))
                return "SHADER-IMAGE-DECLARATION-VARIABLE";
            if (declaredSet != set || declaredBinding != binding)
                continue;
            if (found)
                return "SHADER-IMAGE-DECLARATION-DUPLICATE";
            variable = i;
            found = true;
        }
        if (!found)
            return "SHADER-IMAGE-DECLARATION-MISSING";
        I definition;
        if (!module.definition(variable.operand(1), resultOperand, definition)
            || definition.opcode() != 59u)
            return "SHADER-IMAGE-DECLARATION-DEFINITION";
        I pointer;
        if (!module.definition(variable.operand(0), resultOperand, pointer)
            || pointer.opcode() != 32u || pointer.operands() != 3u || pointer.operand(1) != 0u)
            return "SHADER-IMAGE-DECLARATION-POINTER";
        auto type = pointer.operand(2);
        std::uint32_t count = 1u;
        // The table ABI has one array dimension. Reject nested/cyclic arrays
        // within two lookups; a hostile ID bound cannot extend this budget.
        constexpr std::uint32_t maxDescriptorArrayDepth = 1u;
        for (std::uint32_t visits = 0u;; ++visits) {
            if (visits > maxDescriptorArrayDepth
                || !module.definition(type, resultOperand, definition))
                return "SHADER-IMAGE-DECLARATION-DEFINITION";
            if (definition.opcode() != 28u)
                break;
            if (definition.operands() != 3u)
                return "SHADER-IMAGE-DECLARATION-ARRAY";
            I literal, integer;
            if (!module.definition(definition.operand(2), resultOperand, literal)
                || literal.opcode() != 43u || literal.operands() != 3u
                || !module.definition(literal.operand(0), resultOperand, integer)
                || integer.opcode() != 21u || integer.operands() != 3u || integer.operand(1) != 32u
                || integer.operand(2) > 1u || !literal.operand(2)
                || (integer.operand(2) && (literal.operand(2) & 0x80000000u))
                || count > UINT32_MAX / literal.operand(2))
                return "SHADER-IMAGE-DECLARATION-ARRAY";
            count *= literal.operand(2);
            type = definition.operand(1);
        }
        if (sampler) {
            if (definition.opcode() != 26u || definition.operands() != 1u)
                return "SHADER-SAMPLER-DECLARATION-SHAPE";
            output.descriptorCount = count;
            return nullptr;
        }
        if (definition.opcode() != 25u
            || (definition.operands() != 8u && definition.operands() != 9u)
            || definition.operand(2) > 6u || definition.operand(3) > 2u
            || definition.operand(4) > 1u || definition.operand(5) > 1u
            || (definition.operand(6) != 1u && definition.operand(6) != 2u)
            || (definition.operands() == 9u && definition.operand(8) > 2u))
            return "SHADER-IMAGE-DECLARATION-SHAPE";
        I scalar;
        if (!module.definition(definition.operand(1), resultOperand, scalar)
            || !((scalar.opcode() == 22u && scalar.operands() == 2u)
                || (scalar.opcode() == 21u && scalar.operands() == 3u && scalar.operand(2) <= 1u))
            || (scalar.operand(1) != 16u && scalar.operand(1) != 32u && scalar.operand(1) != 64u))
            return "SHADER-IMAGE-DECLARATION-SCALAR";
        output = {count,
            definition.operand(2),
            definition.operand(3),
            definition.operand(4),
            definition.operand(5),
            definition.operand(6),
            definition.operand(7),
            scalar.operand(1),
            scalar.opcode() == 22u};
        return nullptr;
    }

public:
    static const char* inspect(const void* data,
        std::size_t bytes,
        std::uint32_t set,
        std::uint32_t binding,
        SpirvImageDeclaration& output) noexcept
    {
        return inspectResource(data, bytes, set, binding, output, false);
    }

    // OpTypeSampler proves only kind and descriptor count; SPIR-V does not
    // encode comparison/filtering in this declaration. Never infer those uses.
    static const char* inspectSampler(const void* data,
        std::size_t bytes,
        std::uint32_t set,
        std::uint32_t binding,
        std::uint32_t& descriptorCount) noexcept
    {
        descriptorCount = 0u;
        SpirvImageDeclaration facts;
        if (const auto* id = inspectResource(data, bytes, set, binding, facts, true))
            return id;
        descriptorCount = facts.descriptorCount;
        return nullptr;
    }
};
} // namespace arp::shader

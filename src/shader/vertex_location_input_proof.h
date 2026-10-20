#pragma once
#include "shader/shader_reflection_cache.h"
#include "shader/spirv_instruction_view.h"

namespace arp::shader {
// VertexLocationFloatInput/1: one pure float location-input entry. No builtin,
// resource, alias/group decoration, indirect address, branch or helper call.
// This proves the selected interface only, not any native instance lowering.
class VertexLocationInputProof final {
    using I = SpirvInstructionView::Instruction;

    enum Op : std::uint32_t {
        Source = 3,
        Name = 5,
        MemoryModel = 14,
        EntryPoint = 15,
        Capability = 17,
        TypeVoid = 19,
        TypeFloat = 22,
        TypeVector = 23,
        TypePointer = 32,
        TypeFunction = 33,
        Constant = 43,
        ConstantComposite = 44,
        Function = 54,
        FunctionEnd = 56,
        Variable = 59,
        Load = 61,
        Store = 62,
        Decorate = 71,
        CompositeConstruct = 80,
        FAdd = 129,
        Label = 248,
        Return = 253
    };

    enum : std::uint32_t { Input = 1, Output = 3, Location = 30, BuiltIn = 11, Position = 0 };

public:
    struct Facts {
        std::uint32_t contractVersion = 0u, inputCount = 0u, outputCount = 0u, entryFunction = 0u;
        std::uint64_t binaryBytes = 0u;

        bool operator==(const Facts& b) const noexcept
        {
            return contractVersion == b.contractVersion && inputCount == b.inputCount
                && outputCount == b.outputCount && entryFunction == b.entryFunction
                && binaryBytes == b.binaryBytes;
        }
    };

    static const char* verify(const void* data,
        std::size_t bytes,
        core::StringView entry,
        const core::pmr::Vector<ShaderVertexAttribute>& expected,
        Facts& output) noexcept
    {
        output = {};
        VertexLocationInputProof proof(data, bytes, entry, expected);
        Facts facts;
        if (const auto* id = proof.run(facts))
            return id;
        facts.contractVersion = 1u;
        facts.binaryBytes = bytes;
        output = facts;
        return nullptr;
    }

private:
    VertexLocationInputProof(const void* data,
        std::size_t bytes,
        core::StringView entry,
        const core::pmr::Vector<ShaderVertexAttribute>& expected)
        : module_(data, bytes), entry_(entry), expected_(expected)
    {}

    static int resultOperand(const I& i) noexcept
    {
        switch (i.opcode()) {
        case TypeVoid:
        case TypeFloat:
        case TypeVector:
        case TypePointer:
        case TypeFunction:
        case Label:
            return 0;
        case Constant:
        case ConstantComposite:
        case Variable:
        case Function:
        case Load:
        case CompositeConstruct:
        case FAdd:
            return 1;
        default:
            return -1;
        }
    }

    bool definition(std::uint32_t id, I& out) const noexcept
    {
        return module_.definition(id, resultOperand, out);
    }

    bool typed(std::uint32_t id, Op opcode, std::uint32_t operands, I& out) const noexcept
    {
        return definition(id, out) && out.opcode() == opcode && out.operands() == operands;
    }

    std::uint32_t floatComponents(std::uint32_t id) const noexcept
    {
        I type, scalar;
        if (typed(id, TypeFloat, 2u, type) && type.operand(1) == 32u)
            return 1u;
        if (typed(id, TypeVector, 3u, type) && type.operand(2) >= 2u && type.operand(2) <= 4u
            && typed(type.operand(1), TypeFloat, 2u, scalar) && scalar.operand(1) == 32u)
            return type.operand(2);
        return 0u;
    }

    static std::uint32_t components(VertexAttributeFormat format) noexcept
    {
        switch (format) {
        case VertexAttributeFormat::Float:
            return 1u;
        case VertexAttributeFormat::Float2:
            return 2u;
        case VertexAttributeFormat::Float3:
            return 3u;
        case VertexAttributeFormat::Float4:
            return 4u;
        default:
            return 0u;
        }
    }

    bool valueType(std::uint32_t value, std::uint32_t& type) const noexcept
    {
        I declaration;
        type = 0u;
        if (!definition(value, declaration))
            return false;
        switch (declaration.opcode()) {
        case Constant:
        case ConstantComposite:
        case Load:
        case FAdd:
        case CompositeConstruct:
            type = declaration.operand(0);
            return floatComponents(type) != 0u;
        default:
            return false;
        }
    }

    bool interfaceContains(const I& entry, std::uint32_t first, std::uint32_t id) const noexcept
    {
        unsigned count = 0u;
        for (auto i = first; i < entry.operands(); ++i)
            if (entry.operand(i) == id)
                ++count;
        return count == 1u;
    }

    const char* run(Facts& facts) const noexcept
    {
        if (const auto* id = module_.diagnostic())
            return id;
        if (entry_.empty())
            return "SHADER-VERTEX-LOCATION-ENTRY";
        const auto stringWords = (entry_.size() + sizeof(std::uint32_t)) / sizeof(std::uint32_t);
        if (stringWords > UINT32_MAX - 2u)
            return "SHADER-VERTEX-LOCATION-ENTRY";
        const auto interfaceFirst = 2u + static_cast<std::uint32_t>(stringWords);
        I entry, function;
        std::uint32_t entries = 0u, functions = 0u, memories = 0u, capabilities = 0u, labels = 0u,
                      returns = 0u, ends = 0u;
        bool inFunction = false, ended = false, returned = false, bodyStarted = false;
        for (auto offset = SpirvInstructionView::headerWords; offset < module_.sizeWords();) {
            I i;
            if (!module_.read(offset, i))
                return "SHADER-SPIRV-INSTRUCTION";
            offset = i.next();
            const auto n = i.operands(), op = i.opcode();
            const auto result = resultOperand(i);
            if (result >= 0) {
                I unique;
                if (static_cast<std::uint32_t>(result) >= n
                    || !definition(i.operand(result), unique))
                    return "SHADER-VERTEX-LOCATION-ID";
            }
            if (op == Function) {
                if (n != 4u || inFunction || ended)
                    return "SHADER-VERTEX-LOCATION-FUNCTION";
                inFunction = true;
                function = i;
                ++functions;
                continue;
            }
            if (inFunction) {
                if (op == Label && n == 1u && !bodyStarted && !returned) {
                    ++labels;
                    bodyStarted = true;
                    continue;
                }
                if (!bodyStarted)
                    return "SHADER-VERTEX-LOCATION-BODY";
                if (op == Return && !n && !returned) {
                    ++returns;
                    returned = true;
                    continue;
                }
                if (op == FunctionEnd && !n && returned) {
                    ++ends;
                    inFunction = false;
                    ended = true;
                    continue;
                }
                if (returned)
                    return "SHADER-VERTEX-LOCATION-BODY";
                switch (op) {
                case Load: {
                    I variable, pointer;
                    if (n != 3u || !typed(i.operand(2), Variable, 3u, variable)
                        || variable.operand(2) != Input
                        || !typed(variable.operand(0), TypePointer, 3u, pointer)
                        || pointer.operand(2) != i.operand(0))
                        return "SHADER-VERTEX-LOCATION-LOAD";
                    break;
                }
                case Store: {
                    I variable, pointer;
                    std::uint32_t type = 0u;
                    if (n != 2u || !typed(i.operand(0), Variable, 3u, variable)
                        || variable.operand(2) != Output
                        || !typed(variable.operand(0), TypePointer, 3u, pointer)
                        || !valueType(i.operand(1), type) || pointer.operand(2) != type)
                        return "SHADER-VERTEX-LOCATION-STORE";
                    break;
                }
                case FAdd: {
                    std::uint32_t a = 0u, b = 0u;
                    if (n != 4u || !floatComponents(i.operand(0)) || !valueType(i.operand(2), a)
                        || !valueType(i.operand(3), b) || a != i.operand(0) || b != a)
                        return "SHADER-VERTEX-LOCATION-EXPRESSION";
                    break;
                }
                case CompositeConstruct: {
                    I type;
                    if (n < 4u || !typed(i.operand(0), TypeVector, 3u, type))
                        return "SHADER-VERTEX-LOCATION-EXPRESSION";
                    std::uint32_t total = 0u;
                    for (std::uint32_t v = 2u; v < n; ++v) {
                        std::uint32_t value = 0u;
                        if (!valueType(i.operand(v), value))
                            return "SHADER-VERTEX-LOCATION-EXPRESSION";
                        total += floatComponents(value);
                    }
                    if (total != type.operand(2))
                        return "SHADER-VERTEX-LOCATION-EXPRESSION";
                    break;
                }
                default:
                    return "SHADER-VERTEX-LOCATION-INSTRUCTION-DOMAIN";
                }
                continue;
            }
            if (ended)
                return "SHADER-VERTEX-LOCATION-MODULE-DOMAIN";
            switch (op) {
            case Capability:
                if (n != 1u || i.operand(0) != 1u)
                    return "SHADER-VERTEX-LOCATION-CAPABILITY";
                ++capabilities;
                break;
            case MemoryModel:
                if (n != 2u || i.operand(0) != 0u || i.operand(1) != 1u)
                    return "SHADER-VERTEX-LOCATION-MEMORY";
                ++memories;
                break;
            case EntryPoint:
                if (n < interfaceFirst || i.operand(0) != 0u
                    || !i.stringEquals(2u, entry_, n - interfaceFirst))
                    return "SHADER-VERTEX-LOCATION-ENTRY";
                entry = i;
                ++entries;
                break;
            case Source:
                if (n < 2u)
                    return "SHADER-VERTEX-LOCATION-DECLARATION";
                break;
            case Name:
                if (n < 2u)
                    return "SHADER-VERTEX-LOCATION-DECLARATION";
                break;
            case TypeVoid:
                if (n != 1u)
                    return "SHADER-VERTEX-LOCATION-DECLARATION";
                break;
            case TypeFloat:
            case TypeVector:
                if (!floatComponents(i.operand(0)))
                    return "SHADER-VERTEX-LOCATION-TYPE";
                break;
            case TypePointer:
                if (n != 3u || (i.operand(1) != Input && i.operand(1) != Output)
                    || !floatComponents(i.operand(2)))
                    return "SHADER-VERTEX-LOCATION-POINTER";
                break;
            case TypeFunction: {
                I type;
                if (n != 2u || !typed(i.operand(1), TypeVoid, 1u, type))
                    return "SHADER-VERTEX-LOCATION-DECLARATION";
                break;
            }
            case Constant: {
                I type;
                if (n != 3u || !typed(i.operand(0), TypeFloat, 2u, type))
                    return "SHADER-VERTEX-LOCATION-CONSTANT";
                break;
            }
            case ConstantComposite: {
                I type;
                if (!typed(i.operand(0), TypeVector, 3u, type) || n != type.operand(2) + 2u)
                    return "SHADER-VERTEX-LOCATION-CONSTANT";
                for (std::uint32_t v = 2u; v < n; ++v) {
                    I scalar;
                    if (!typed(i.operand(v), Constant, 3u, scalar)
                        || scalar.operand(0) != type.operand(1))
                        return "SHADER-VERTEX-LOCATION-CONSTANT";
                }
                break;
            }
            case Variable: {
                I pointer;
                if (n != 3u || !typed(i.operand(0), TypePointer, 3u, pointer)
                    || pointer.operand(1) != i.operand(2))
                    return "SHADER-VERTEX-LOCATION-VARIABLE";
                if (i.operand(2) == Input)
                    ++facts.inputCount;
                else
                    ++facts.outputCount;
                break;
            }
            case Decorate: {
                I variable;
                if (n != 3u || !typed(i.operand(0), Variable, 3u, variable))
                    return "SHADER-VERTEX-LOCATION-DECORATION";
                if (i.operand(1) == Location)
                    break;
                if (i.operand(1) != BuiltIn || variable.operand(2) != Output
                    || i.operand(2) != Position)
                    return "SHADER-VERTEX-LOCATION-BUILTIN-OR-DECORATION";
                break;
            }
            default:
                return "SHADER-VERTEX-LOCATION-INSTRUCTION-DOMAIN";
            }
        }
        I voidType, functionType;
        if (inFunction || !ended || entries != 1u || functions != 1u || memories != 1u
            || capabilities != 1u || labels != 1u || returns != 1u || ends != 1u
            || !facts.outputCount || function.operand(1) != entry.operand(1)
            || function.operand(2) != 0u || !typed(function.operand(0), TypeVoid, 1u, voidType)
            || !typed(function.operand(3), TypeFunction, 2u, functionType)
            || functionType.operand(1) != function.operand(0)
            || entry.operands() - interfaceFirst != facts.inputCount + facts.outputCount
            || facts.inputCount != expected_.size())
            return "SHADER-VERTEX-LOCATION-MODULE-DOMAIN";
        for (std::size_t a = 0u; a < expected_.size(); ++a) {
            if (!components(expected_[a].format))
                return "SHADER-VERTEX-LOCATION-EXPECTED-TYPE";
            for (std::size_t b = 0u; b < a; ++b)
                if (expected_[a].location == expected_[b].location)
                    return "SHADER-VERTEX-LOCATION-EXPECTED-DUPLICATE";
        }
        // All interface operands name distinct global variables; every global
        // variable is present exactly once. Decorations cannot target aliases.
        for (std::uint32_t a = interfaceFirst; a < entry.operands(); ++a) {
            I variable;
            if (!typed(entry.operand(a), Variable, 3u, variable))
                return "SHADER-VERTEX-LOCATION-INTERFACE";
            for (auto b = interfaceFirst; b < a; ++b)
                if (entry.operand(a) == entry.operand(b))
                    return "SHADER-VERTEX-LOCATION-INTERFACE";
        }
        std::uint32_t positions = 0u;
        for (auto offset = SpirvInstructionView::headerWords; offset < module_.sizeWords();) {
            I i;
            module_.read(offset, i);
            offset = i.next();
            if (i.opcode() != Variable)
                continue;
            if (!interfaceContains(entry, interfaceFirst, i.operand(1)))
                return "SHADER-VERTEX-LOCATION-INTERFACE";
            std::uint32_t location = 0u, builtin = 0u;
            const bool located = module_.decoration(i.operand(1), Location, location);
            const bool built = module_.decoration(i.operand(1), BuiltIn, builtin);
            // Explicitly count decorations, so duplicate/malformed decoration
            // cannot be mistaken for the absence of an optional builtin.
            unsigned decorations = 0u;
            for (auto at = SpirvInstructionView::headerWords; at < module_.sizeWords();) {
                I d;
                module_.read(at, d);
                at = d.next();
                if (d.opcode() == Decorate && d.operand(0) == i.operand(1))
                    ++decorations;
            }
            if (decorations != 1u || located == built)
                return "SHADER-VERTEX-LOCATION-DECORATION";
            I pointer;
            definition(i.operand(0), pointer);
            const auto width = floatComponents(pointer.operand(2));
            if (i.operand(2) == Input) {
                if (!located)
                    return "SHADER-VERTEX-LOCATION-BUILTIN-OR-DECORATION";
                const auto found =
                    std::find_if(expected_.begin(), expected_.end(), [&](const auto& a) {
                        return a.location == location;
                    });
                if (found == expected_.end() || components(found->format) != width)
                    return "SHADER-VERTEX-LOCATION-EXPECTED-MISMATCH";
                for (auto at = SpirvInstructionView::headerWords; at < module_.sizeWords();) {
                    I other;
                    module_.read(at, other);
                    at = other.next();
                    std::uint32_t otherLocation = 0u;
                    if (other.opcode() == Variable && other.operand(2) == Input
                        && other.operand(1) != i.operand(1)
                        && module_.decoration(other.operand(1), Location, otherLocation)
                        && otherLocation == location)
                        return "SHADER-VERTEX-LOCATION-LOCATION-DUPLICATE";
                }
            } else if (built) {
                if (builtin != Position || width != 4u)
                    return "SHADER-VERTEX-LOCATION-POSITION";
                ++positions;
            }
        }
        if (positions != 1u)
            return "SHADER-VERTEX-LOCATION-POSITION";
        facts.entryFunction = entry.operand(1);
        return nullptr;
    }

    SpirvInstructionView module_;
    core::StringView entry_;
    const core::pmr::Vector<ShaderVertexAttribute>& expected_;
};
} // namespace arp::shader

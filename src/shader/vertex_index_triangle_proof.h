#pragma once
#include "shader/spirv_instruction_view.h"
#include "shader/triangle_position_bits.h"

#include <utility>

namespace arp::shader {
// Complete VertexIndexTriangle/1 structured binary language. The declared
// input is uint32(bitcast(signed VertexIndex - signed BaseVertex)), not WGSL's
// raw vertex_index. Cross-target consumers must enforce the zero-base domain.
class VertexIndexTriangleProof final {
    using I = SpirvInstructionView::Instruction;

    enum Op : std::uint32_t {
        Source = 3,
        Name = 5,
        MemoryModel = 14,
        EntryPoint = 15,
        Capability = 17,
        TypeVoid = 19,
        TypeBool = 20,
        TypeInt = 21,
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
        Bitcast = 124,
        ISub = 130,
        IEqual = 170,
        SelectionMerge = 247,
        Label = 248,
        Branch = 249,
        BranchConditional = 250,
        Return = 253
    };

    enum : std::uint32_t {
        Input = 1,
        Output = 3,
        FunctionStorage = 7,
        BuiltIn = 11,
        Position = 0,
        VertexIndex = 42,
        BaseVertex = 4424,
        ShaderCapability = 1,
        DrawParameters = 4427
    };

    // Explicit complete 35-instruction structured function profile, including
    // both nested merge blocks. No IDs or data/control edges are inferred.
    static constexpr std::size_t bodySize = 35u;

public:
    enum class InputSemantics { SubtractBaseVertex };

    struct Facts {
        std::uint32_t contractVersion = 0u;
        InputSemantics input = InputSemantics::SubtractBaseVertex;
        bool requiresDrawParameters = false;
        TrianglePositionBits::Positions positions{};
    };

    static const char* verify(
        const void* bytes, std::size_t count, core::StringView entry, Facts& output) noexcept
    {
        output = {};
        VertexIndexTriangleProof proof(bytes, count, entry);
        Facts facts;
        if (const auto* id = proof.run(facts))
            return id;
        facts.contractVersion = 1u;
        facts.requiresDrawParameters = true;
        output = facts;
        return nullptr;
    }

private:
    VertexIndexTriangleProof(const void* bytes, std::size_t count, core::StringView entry) noexcept
        : module_(bytes, count), entry_(entry)
    {}

    static int resultOperand(const I& i) noexcept
    {
        switch (i.opcode()) {
        case TypeVoid:
        case TypeBool:
        case TypeInt:
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
        case Bitcast:
        case ISub:
        case IEqual:
            return 1;
        default:
            return -1;
        }
    }

    bool definition(std::uint32_t id, I& out) const noexcept
    {
        return module_.definition(id, resultOperand, out);
    }

    bool typed(std::uint32_t id, Op op, std::uint32_t n, I& out) const noexcept
    {
        return definition(id, out) && out.opcode() == op && out.operands() == n;
    }

    bool integer(std::uint32_t id, bool signedType) const noexcept
    {
        I i;
        return typed(id, TypeInt, 3u, i) && i.operand(1) == 32u
            && i.operand(2) == static_cast<unsigned>(signedType);
    }

    bool floatType(std::uint32_t id) const noexcept
    {
        I i;
        return typed(id, TypeFloat, 2u, i) && i.operand(1) == 32u;
    }

    bool vectorType(std::uint32_t id, std::uint32_t width) const noexcept
    {
        I i;
        return typed(id, TypeVector, 3u, i) && floatType(i.operand(1)) && i.operand(2) == width;
    }

    bool pointer(std::uint32_t id, std::uint32_t storage, std::uint32_t type) const noexcept
    {
        I i;
        return typed(id, TypePointer, 3u, i) && i.operand(1) == storage && i.operand(2) == type;
    }

    bool floatLiteral(std::uint32_t id, std::uint32_t type, std::uint32_t& bits) const noexcept
    {
        I i;
        if (!typed(id, Constant, 3u, i) || i.operand(0) != type || !floatType(type))
            return false;
        bits = i.operand(2);
        return true;
    }

    bool uintLiteral(std::uint32_t id, std::uint32_t type, std::uint32_t value) const noexcept
    {
        I i;
        return typed(id, Constant, 3u, i) && i.operand(0) == type && integer(type, false)
            && i.operand(2) == value;
    }

    bool vec2Literal(
        std::uint32_t id, std::uint32_t type, std::array<std::uint32_t, 4>& output) const noexcept
    {
        I i, v;
        return typed(id, ConstantComposite, 4u, i) && i.operand(0) == type
            && typed(type, TypeVector, 3u, v) && vectorType(type, 2u)
            && floatLiteral(i.operand(2), v.operand(1), output[0])
            && floatLiteral(i.operand(3), v.operand(1), output[1]);
    }

    bool declaration(const I& i) const noexcept
    {
        const auto n = i.operands();
        I type, child;
        switch (i.opcode()) {
        case Source:
            return n == 2u;
        case Name:
            return n >= 2u && definition(i.operand(0), child) && i.hasLiteralString(1u);
        case MemoryModel:
            return n == 2u && !i.operand(0) && i.operand(1) == 1u;
        case EntryPoint:
            return n >= 6u && i.operand(0) == 0u;
        case Capability:
            return n == 1u && (i.operand(0) == ShaderCapability || i.operand(0) == DrawParameters);
        case TypeVoid:
        case TypeBool:
            return n == 1u;
        case TypeInt:
            return n == 3u && i.operand(1) == 32u && i.operand(2) <= 1u;
        case TypeFloat:
            return n == 2u && i.operand(1) == 32u;
        case TypeVector:
            return n == 3u && floatType(i.operand(1)) && (i.operand(2) == 2u || i.operand(2) == 4u);
        case TypePointer:
            return n == 3u
                && ((i.operand(1) == Input && integer(i.operand(2), true))
                    || (i.operand(1) == Output && vectorType(i.operand(2), 4u))
                    || (i.operand(1) == FunctionStorage && vectorType(i.operand(2), 2u)));
        case TypeFunction:
            return n == 2u && typed(i.operand(1), TypeVoid, 1u, type);
        case Constant:
            return n == 3u
                && (floatType(i.operand(0)) || integer(i.operand(0), false)
                    || integer(i.operand(0), true));
        case ConstantComposite:
            return n == 4u && typed(i.operand(0), TypeVector, 3u, type)
                && vectorType(i.operand(0), 2u) && typed(i.operand(2), Constant, 3u, child)
                && child.operand(0) == type.operand(1) && typed(i.operand(3), Constant, 3u, child)
                && child.operand(0) == type.operand(1);
        case Variable:
            return n == 3u && (i.operand(2) == Input || i.operand(2) == Output)
                && typed(i.operand(0), TypePointer, 3u, type) && type.operand(1) == i.operand(2);
        case Decorate:
            return n == 3u && i.operand(1) == BuiltIn && typed(i.operand(0), Variable, 3u, child)
                && (i.operand(2) == Position || i.operand(2) == VertexIndex
                    || i.operand(2) == BaseVertex);
        default:
            return false;
        }
    }

    bool condition(const std::array<I, bodySize>& b,
        std::size_t at,
        std::uint32_t vertex,
        std::uint32_t base,
        std::uint32_t expected) const noexcept
    {
        // Both loads are required; their scheduling order does not change the
        // proven VertexIndex - BaseVertex data edge. Reject wrong sources and
        // reversed subtraction below, independently of instruction ordering.
        const bool vertexFirst = b[at].operand(2) == vertex;
        const auto& vl = b[at + (vertexFirst ? 0u : 1u)];
        const auto& bl = b[at + (vertexFirst ? 1u : 0u)];
        const auto& sub = b[at + 2u];
        const auto& cast = b[at + 3u];
        const auto& eq = b[at + 4u];
        I boolType;
        return vl.operand(2) == vertex && bl.operand(2) == base && integer(vl.operand(0), true)
            && vl.operand(0) == bl.operand(0) && sub.operand(0) == vl.operand(0)
            && sub.operand(2) == vl.operand(1) && sub.operand(3) == bl.operand(1)
            && integer(cast.operand(0), false) && cast.operand(2) == sub.operand(1)
            && typed(eq.operand(0), TypeBool, 1u, boolType) && eq.operand(2) == cast.operand(1)
            && uintLiteral(eq.operand(3), cast.operand(0), expected);
    }

    const char* run(Facts& facts) const noexcept
    {
        if (const auto* id = module_.diagnostic())
            return id;
        I entry;
        std::array<I, bodySize> b{};
        std::uint32_t shader = 0u, drawParameters = 0u, memory = 0u, entries = 0u, decorations = 0u;
        std::uint32_t vertex = 0u, base = 0u, position = 0u;
        std::size_t bodyCount = 0u;
        bool inFunction = false, ended = false;
        for (auto offset = SpirvInstructionView::headerWords; offset < module_.sizeWords();) {
            I i;
            if (!module_.read(offset, i))
                return "SHADER-SPIRV-INSTRUCTION";
            offset = i.next();
            const auto r = resultOperand(i);
            I unique;
            if (r >= 0
                && (static_cast<std::uint32_t>(r) >= i.operands()
                    || !definition(i.operand(r), unique)))
                return "SHADER-VERTEX-TRIANGLE-ID";
            if (i.opcode() == Function) {
                if (inFunction || ended)
                    return "SHADER-VERTEX-TRIANGLE-FUNCTION";
                inFunction = true;
            }
            if (inFunction) {
                if (bodyCount == b.size())
                    return "SHADER-VERTEX-TRIANGLE-BODY";
                b[bodyCount++] = i;
                if (i.opcode() == FunctionEnd) {
                    inFunction = false;
                    ended = true;
                }
                continue;
            }
            if (ended || !declaration(i))
                return "SHADER-VERTEX-TRIANGLE-DECLARATION";
            switch (i.opcode()) {
            case Capability:
                if (i.operand(0) == ShaderCapability)
                    ++shader;
                else
                    ++drawParameters;
                break;
            case MemoryModel:
                ++memory;
                break;
            case EntryPoint:
                entry = i;
                ++entries;
                break;
            case Decorate:
                ++decorations;
                break;
            case Variable: {
                std::uint32_t builtin = 0u;
                if (!module_.decoration(i.operand(1), BuiltIn, builtin))
                    return "SHADER-VERTEX-TRIANGLE-INTERFACE";
                auto* target = builtin == VertexIndex ? &vertex
                    : builtin == BaseVertex           ? &base
                    : builtin == Position             ? &position
                                                      : nullptr;
                if (!target || *target || i.operand(2) != (builtin == Position ? Output : Input))
                    return "SHADER-VERTEX-TRIANGLE-INTERFACE";
                *target = i.operand(1);
                break;
            }
            default:
                break;
            }
        }
        if (inFunction || !ended || bodyCount != bodySize || shader != 1u || drawParameters != 1u
            || memory != 1u || entries != 1u || decorations != 3u || !vertex || !base || !position)
            return "SHADER-VERTEX-TRIANGLE-MODULE";
        if (entry_.empty() || !entry.stringEquals(2u, entry_, 3u))
            return "SHADER-VERTEX-TRIANGLE-ENTRY";
        const auto first = entry.operands() - 3u;
        for (auto id : {vertex, base, position}) {
            unsigned found = 0u;
            for (auto index = first; index < entry.operands(); ++index)
                if (entry.operand(index) == id)
                    ++found;
            if (found != 1u)
                return "SHADER-VERTEX-TRIANGLE-INTERFACE";
        }
        using Shape = std::pair<Op, std::uint32_t>;
        constexpr std::array<Shape, bodySize> shape{{{Function, 4u},
            {Label, 1u},
            {Variable, 3u},
            {Load, 3u},
            {Load, 3u},
            {ISub, 4u},
            {Bitcast, 3u},
            {IEqual, 4u},
            {SelectionMerge, 2u},
            {BranchConditional, 3u},
            {Label, 1u},
            {Store, 2u},
            {Branch, 1u},
            {Label, 1u},
            {Load, 3u},
            {Load, 3u},
            {ISub, 4u},
            {Bitcast, 3u},
            {IEqual, 4u},
            {SelectionMerge, 2u},
            {BranchConditional, 3u},
            {Label, 1u},
            {Store, 2u},
            {Branch, 1u},
            {Label, 1u},
            {Store, 2u},
            {Branch, 1u},
            {Label, 1u},
            {Branch, 1u},
            {Label, 1u},
            {Load, 3u},
            {CompositeConstruct, 5u},
            {Store, 2u},
            {Return, 0u},
            {FunctionEnd, 0u}}};
        for (std::size_t i = 0u; i < bodySize; ++i)
            if (b[i].opcode() != shape[i].first || b[i].operands() != shape[i].second)
                return "SHADER-VERTEX-TRIANGLE-BODY";
        I voidType, fnType, inputVar, positionVar, positionPointer, vec4, vec2;
        if (b[0].operand(1) != entry.operand(1) || b[0].operand(2) != 0u
            || !typed(b[0].operand(0), TypeVoid, 1u, voidType)
            || !typed(b[0].operand(3), TypeFunction, 2u, fnType)
            || fnType.operand(1) != b[0].operand(0) || !typed(vertex, Variable, 3u, inputVar)
            || !pointer(inputVar.operand(0), Input, b[3].operand(0))
            || !typed(base, Variable, 3u, inputVar)
            || !pointer(inputVar.operand(0), Input, b[4].operand(0))
            || b[14].operand(0) != b[3].operand(0) || b[15].operand(0) != b[4].operand(0)
            || b[2].operand(2) != FunctionStorage
            || !pointer(b[2].operand(0), FunctionStorage, b[30].operand(0))
            || !typed(b[30].operand(0), TypeVector, 3u, vec2) || !vectorType(b[30].operand(0), 2u)
            || !typed(position, Variable, 3u, positionVar)
            || !typed(positionVar.operand(0), TypePointer, 3u, positionPointer)
            || !typed(positionPointer.operand(2), TypeVector, 3u, vec4)
            || !vectorType(positionPointer.operand(2), 4u) || vec4.operand(1) != vec2.operand(1))
            return "SHADER-VERTEX-TRIANGLE-TYPES";
        if (!condition(b, 3u, vertex, base, 0u) || !condition(b, 14u, vertex, base, 1u))
            return "SHADER-VERTEX-TRIANGLE-CONDITION";
        if (b[8].operand(0) != b[29].operand(0) || b[8].operand(1)
            || b[9].operand(0) != b[7].operand(1) || b[9].operand(1) != b[10].operand(0)
            || b[9].operand(2) != b[13].operand(0) || b[12].operand(0) != b[29].operand(0)
            || b[19].operand(0) != b[27].operand(0) || b[19].operand(1)
            || b[20].operand(0) != b[18].operand(1) || b[20].operand(1) != b[21].operand(0)
            || b[20].operand(2) != b[24].operand(0) || b[23].operand(0) != b[27].operand(0)
            || b[26].operand(0) != b[27].operand(0) || b[28].operand(0) != b[29].operand(0))
            return "SHADER-VERTEX-TRIANGLE-CONTROL-EDGE";
        const auto local = b[2].operand(1), vec2Type = b[30].operand(0);
        constexpr std::array<std::size_t, 3> stores{11u, 22u, 25u};
        for (std::size_t v = 0u; v < stores.size(); ++v) {
            if (b[stores[v]].operand(0) != local
                || !vec2Literal(b[stores[v]].operand(1), vec2Type, facts.positions[v]))
                return "SHADER-VERTEX-TRIANGLE-POSITION";
            if (!floatLiteral(b[31].operand(3), vec4.operand(1), facts.positions[v][2])
                || !floatLiteral(b[31].operand(4), vec4.operand(1), facts.positions[v][3]))
                return "SHADER-VERTEX-TRIANGLE-POSITION";
        }
        if (b[30].operand(2) != local || b[31].operand(0) != positionPointer.operand(2)
            || b[31].operand(2) != b[30].operand(1) || b[32].operand(0) != position
            || b[32].operand(1) != b[31].operand(1))
            return "SHADER-VERTEX-TRIANGLE-DATA-EDGE";
        return nullptr;
    }

    SpirvInstructionView module_;
    core::StringView entry_;
};
} // namespace arp::shader

#pragma once
#include "core/ascii_token_cursor.h"
#include "shader/triangle_position_bits.h"

namespace arp::shader {
// ClosedGraphicsTriangleStorageRead/1 complete combined WGSL language. Native
// validation remains mandatory for WGSL legality; this proves reference edges,
// complete control/data flow and exact finite literal semantics independently.
class GraphicsTriangleStorageWgslProof final {
public:
    struct Facts {
        std::uint32_t contractVersion = 0u, group = 0u, binding = 0u, comparison = 0u;
        std::uint64_t byteOffset = 0u, byteCount = 0u;
        TrianglePositionBits::Positions positions{};
    };

    static const char* verify(core::StringView source,
        core::StringView vertexEntry,
        core::StringView fragmentEntry,
        Facts& output) noexcept
    {
        output = {};
        GraphicsTriangleStorageWgslProof proof(source);
        Facts facts;
        if (!proof.run(vertexEntry, fragmentEntry, facts))
            return proof.tokens_.diagnostic() ? proof.tokens_.diagnostic()
                                              : "SHADER-WGSL-GRAPHICS-TRIANGLE-STORAGE";
        facts.contractVersion = 1u;
        facts.byteCount = sizeof(std::uint32_t);
        output = facts;
        return nullptr;
    }

private:
    explicit GraphicsTriangleStorageWgslProof(core::StringView source) noexcept
        : tokens_(source, core::AsciiTokenCursor::Profile::FixedDecimalFloat)
    {}

    bool take(std::initializer_list<core::StringView> sequence) noexcept
    {
        for (const auto token : sequence)
            if (!tokens_.consume(token))
                return false;
        return true;
    }

    bool vectorType(core::StringView width) noexcept { return take({width, "<", "f32", ">"}); }

    bool name(core::StringView& out) noexcept
    {
        if (!tokens_.identifier(out))
            return false;
        for (const auto builtin : {"array", "u32", "i32", "f32", "vec2", "vec4", "select"})
            if (out == builtin)
                return false;
        return true;
    }

    // Explicit finite spelling table; no locale, floating conversion or epsilon.
    bool literal(std::uint32_t& bits) noexcept
    {
        auto token = tokens_.next();
        bool negative = false;
        if (token.text == "-") {
            negative = true;
            token = tokens_.next();
        }
        if (token.kind != core::AsciiTokenCursor::Kind::FixedDecimal)
            return false;
        if (!negative && (token.text == "0.0f" || token.text == "0.0"))
            bits = TrianglePositionBits::zero;
        else if (token.text == "1.0f" || token.text == "1.0")
            bits = negative ? TrianglePositionBits::minusOne : TrianglePositionBits::one;
        else if (!negative && (token.text == "3.0f" || token.text == "3.0"))
            bits = TrianglePositionBits::three;
        else
            return false;
        return true;
    }

    bool vec2(std::array<std::uint32_t, 4>& position) noexcept
    {
        return vectorType("vec2") && take({"("}) && literal(position[0]) && take({","})
            && literal(position[1]) && take({")", ";"});
    }

    bool color(const std::array<std::uint32_t, 4>& expected) noexcept
    {
        if (!vectorType("vec4") || !take({"("}))
            return false;
        for (std::size_t i = 0; i < expected.size(); ++i) {
            std::uint32_t bits = 0u;
            if (!literal(bits) || bits != expected[i] || (i + 1u < expected.size() && !take({","})))
                return false;
        }
        return take({")"});
    }

    bool interface(
        core::StringView attribute, core::StringView value, core::StringView& type) noexcept
    {
        core::StringView member;
        return take({"struct"}) && name(type) && take({"{", "@", attribute, "(", value, ")"})
            && name(member) && take({":"}) && vectorType("vec4") && take({",", "}", ";"});
    }

    bool run(core::StringView vertexEntry, core::StringView fragmentEntry, Facts& facts) noexcept
    {
        // Fixed-size borrowed symbol tables; globals cannot duplicate, and no
        // parameter/local may shadow a global or another local in its function.
        std::array<core::StringView, 5> globals{};
        std::array<core::StringView, 3> vertexLocals{};
        std::array<core::StringView, 2> fragmentLocals{};
        auto& resource = globals[0];
        auto& vertexType = globals[1];
        auto& vertex = globals[2];
        auto& fragmentType = globals[3];
        auto& fragment = globals[4];
        auto& index = vertexLocals[0];
        auto& position = vertexLocals[1];
        auto& vertexResult = vertexLocals[2];
        auto& fragmentPosition = fragmentLocals[0];
        auto& fragmentResult = fragmentLocals[1];
        if (vertexEntry.empty() || fragmentEntry.empty() || !take({"@", "binding", "("})
            || !tokens_.decimalUint32(facts.binding) || !take({")", "@", "group", "("})
            || !tokens_.decimalUint32(facts.group)
            || !take({")", "var", "<", "storage", ",", "read", ">"}) || !name(resource)
            || !take({":", "array", "<", "u32", ">", ";"})
            || !interface("builtin", "position", vertexType) || !take({"@", "vertex", "fn"})
            || !name(vertex) || vertex != vertexEntry
            || !take({"(", "@", "builtin", "(", "vertex_index", ")"}) || !name(index)
            || !take({":", "u32", ")", "-", ">", vertexType, "{", "var"}) || !name(position)
            || !take({":"}) || !vectorType("vec2")
            || !take(
                {";", "if", "(", index, "=", "=", "u32", "(", "0", ")", ")", "{", position, "="})
            || !vec2(facts.positions[0])
            || !take({"}",
                "else",
                "{",
                "if",
                "(",
                index,
                "=",
                "=",
                "u32",
                "(",
                "1",
                ")",
                ")",
                "{",
                position,
                "="})
            || !vec2(facts.positions[1]) || !take({"}", "else", "{", position, "="})
            || !vec2(facts.positions[2]) || !take({"}", "}", "var"}) || !name(vertexResult)
            || !take({":", vertexType, "=", vertexType, "("}) || !vectorType("vec4")
            || !take({"(", position, ","}))
            return false;
        std::uint32_t z = 0u, w = 0u;
        if (!literal(z) || !take({","}) || !literal(w)
            || !take({")", ")", ";", "return", vertexResult, ";", "}"})
            || !interface("location", "0", fragmentType) || !take({"@", "fragment", "fn"})
            || !name(fragment) || fragment != fragmentEntry
            || !take({"(", "@", "builtin", "(", "position", ")"}) || !name(fragmentPosition)
            || !take({":"}) || !vectorType("vec4")
            || !take({")", "-", ">", fragmentType, "{", "var"}) || !name(fragmentResult)
            || !take({":", fragmentType, "=", fragmentType, "(", "select", "("})
            || !color({TrianglePositionBits::one,
                TrianglePositionBits::zero,
                TrianglePositionBits::zero,
                TrianglePositionBits::one})
            || !take({","})
            || !color({TrianglePositionBits::zero,
                TrianglePositionBits::one,
                TrianglePositionBits::zero,
                TrianglePositionBits::one})
            || !take({",", resource, "[", "i32", "(", "0", ")", "]", "=", "=", "u32", "("})
            || !tokens_.decimalUint32(facts.comparison)
            || !take({")", ")", ")", ";", "return", fragmentResult, ";", "}"}) || !tokens_.atEnd())
            return false;
        for (std::size_t i = 0; i < globals.size(); ++i)
            for (std::size_t j = 0; j < i; ++j)
                if (globals[i] == globals[j])
                    return false;
        const auto validLocals = [&](const auto& locals) {
            for (std::size_t i = 0; i < locals.size(); ++i) {
                for (const auto global : globals)
                    if (locals[i] == global)
                        return false;
                for (std::size_t j = 0; j < i; ++j)
                    if (locals[i] == locals[j])
                        return false;
            }
            return true;
        };
        if (!validLocals(vertexLocals) || !validLocals(fragmentLocals))
            return false;
        for (auto& p : facts.positions) {
            p[2] = z;
            p[3] = w;
        }
        return true;
    }

    core::AsciiTokenCursor tokens_;
};
} // namespace arp::shader

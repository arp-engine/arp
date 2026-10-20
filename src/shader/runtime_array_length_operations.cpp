#include "shader/runtime_array_length_operations.h"

#include "shader/shader_artifact_files.h"

namespace arp::shader {
int RuntimeArrayLengthOperations::resultOperand(const I& i) noexcept
{
    switch (i.opcode()) {
    case 19u:
    case 20u:
    case 21u:
    case 22u:
    case 23u:
    case 24u:
    case 28u:
    case 29u:
    case 30u:
    case 32u:
    case 33u:
    case 248u:
        return 0;
    case 43u:
    case 44u:
    case 54u:
    case 55u:
    case 59u:
    case 61u:
    case 65u:
    case 68u:
        return 1;
    default:
        return -1;
    }
}

const char* RuntimeArrayLengthOperations::verify(const core::ByteBuffer& binary,
    core::StringView target,
    const ShaderReflection& reflection,
    std::uint32_t maxBindings,
    const TargetBufferMemoryLoadRequest& request,
    Budget budget,
    core::pmr::Vector<Facts>& output)
{
    using Tokens = core::AsciiTokenCursor;
    output.clear();
    const auto require = [](bool condition, const char* id) {
        requireShaderArtifact(condition, id);
    };
    try {
        require(budget.maxOperations && budget.maxTokens, "SHADER-RUNTIME-LENGTH-BUDGET");
        core::pmr::Vector<SpirvBufferDeclarationLayout> declarations{
            &core::defaultCpuMemoryResource()};
        if (const auto* id = SpirvBufferDeclarationLayoutVerifier::verify(
                binary.data(), binary.size(), maxBindings, declarations))
            return id;
        TargetBufferMemoryDeclarations memory;
        if (const auto* id =
                memory.build(binary, target, reflection, declarations, maxBindings, request))
            return id;
        if (const auto* id = memory.diagnostic())
            return id;
        core::pmr::Vector<WgslBufferRuntimeReference> references{&core::defaultCpuMemoryResource()};
        if (const auto* id = WgslBufferDeclarationLayoutVerifier::verifyWithRuntimeReferences(
                target, static_cast<std::uint32_t>(request.targetTypes.maxRoots), references))
            return id;
        SpirvInstructionView source(binary.data(), binary.size());
        if (const auto* id = source.diagnostic())
            return id;
        const auto typed =
            [&](std::uint32_t id, std::uint32_t opcode, std::uint32_t operands, I& out) {
                return source.definition(id, resultOperand, out) && out.opcode() == opcode
                    && out.operands() == operands;
            };
        core::pmr::Vector<Facts> facts{&core::defaultCpuMemoryResource()};
        bool function = false;
        for (auto offset = SpirvInstructionView::headerWords; offset < source.sizeWords();) {
            I i;
            require(source.read(offset, i), "SHADER-RUNTIME-LENGTH-BINARY-FRAMING");
            offset = i.next();
            if (i.opcode() == 54u) {
                require(!function, "SHADER-RUNTIME-LENGTH-FUNCTION");
                function = true;
            }
            if (i.opcode() == 56u) {
                require(function, "SHADER-RUNTIME-LENGTH-FUNCTION");
                function = false;
            }
            if (i.opcode() == 59u && i.operands() >= 3u && i.operand(2) == 12u)
                require(!function, "SHADER-RUNTIME-LENGTH-DIRECT-ROOT");
            if (i.opcode() != 68u)
                continue;
            require(function && i.operands() == 4u, "SHADER-RUNTIME-LENGTH-BINARY-OPERATION");
            require(facts.size() < budget.maxOperations, "SHADER-RUNTIME-LENGTH-OPERATION-BUDGET");
            I result, type, variable, pointer, block, array;
            require(typed(i.operand(0), 21u, 3u, type) && type.operand(1) == 32u && !type.operand(2)
                    && typed(i.operand(1), 68u, 4u, result),
                "SHADER-RUNTIME-LENGTH-RESULT-TYPE");
            require(typed(i.operand(2), 59u, 3u, variable) && variable.operand(2) == 12u
                    && typed(variable.operand(0), 32u, 3u, pointer) && pointer.operand(1) == 12u,
                "SHADER-RUNTIME-LENGTH-DIRECT-ROOT");
            require(source.definition(pointer.operand(2), resultOperand, block)
                    && block.opcode() == 30u && block.operands() >= 2u
                    && i.operand(3) == block.operands() - 2u
                    && typed(block.operand(i.operand(3) + 1u), 29u, 2u, array),
                "SHADER-RUNTIME-LENGTH-FINAL-MEMBER");
            const TargetBufferMemoryDeclarations::Record* record = nullptr;
            for (const auto& r : memory.records())
                if (r.facts.source.spirvId == variable.operand(1))
                    record = &r;
            require(record && !record->diagnostic && record->facts.source.hasRuntimeArray
                    && record->facts.target.hasRuntimeArray,
                "SHADER-RUNTIME-LENGTH-MEMORY");
            facts.push_back({record->facts.set,
                record->facts.binding,
                record->facts.target.group,
                record->facts.target.binding,
                i.operand(1),
                i.operand(3),
                record->facts.target.runtimeArrayOffsetBytes,
                record->facts.target.runtimeArrayStrideBytes,
                SIZE_MAX});
        }
        require(!function, "SHADER-RUNTIME-LENGTH-FUNCTION");
        core::pmr::Vector<Tokens::Token> tokens{&core::defaultCpuMemoryResource()};
        Tokens cursor(target, Tokens::Profile::FixedDecimalFloat);
        while (true) {
            const auto token = cursor.next();
            require(token.kind != Tokens::Kind::Invalid, "SHADER-RUNTIME-LENGTH-TARGET-LEXICAL");
            if (token.kind == Tokens::Kind::End)
                break;
            require(tokens.size() < budget.maxTokens, "SHADER-RUNTIME-LENGTH-TOKEN-BUDGET");
            tokens.push_back(token);
        }
        const auto protectedName = [&](core::StringView name) {
            if (name == "arrayLength")
                return true;
            for (const auto& r : references)
                if (r.variable == name)
                    return true;
            return false;
        };
        const auto token = [&](std::size_t index) -> core::StringView {
            return index < tokens.size() ? tokens[index].text : core::StringView{};
        };
        std::size_t braces = 0u, calls = 0u;
        for (std::size_t index = 0u; index < tokens.size(); ++index) {
            const auto text = token(index);
            if (text == "{") {
                ++braces;
                continue;
            }
            if (text == "}") {
                require(braces, "SHADER-RUNTIME-LENGTH-TARGET-FRAMING");
                --braces;
                continue;
            }
            if (text == "fn") {
                require(!protectedName(token(index + 1u)), "SHADER-RUNTIME-LENGTH-SHADOW");
                require(token(index + 2u) == "(", "SHADER-RUNTIME-LENGTH-TARGET-FRAMING");
                std::size_t depth = 1u, parameter = index + 3u;
                for (; parameter < tokens.size() && depth; ++parameter) {
                    if (token(parameter) == "(")
                        ++depth;
                    if (token(parameter) == ")")
                        --depth;
                    require(!protectedName(token(parameter)), "SHADER-RUNTIME-LENGTH-SHADOW");
                }
                require(!depth, "SHADER-RUNTIME-LENGTH-TARGET-FRAMING");
            }
            if (text == "let" || text == "const" || text == "override" || text == "var") {
                auto name = index + 1u;
                if (text == "var" && token(name) == "<") {
                    while (name < tokens.size() && token(name) != ">")
                        ++name;
                    require(name < tokens.size(), "SHADER-RUNTIME-LENGTH-TARGET-FRAMING");
                    ++name;
                }
                require(token(name) != "arrayLength" && (!braces || !protectedName(token(name))),
                    "SHADER-RUNTIME-LENGTH-SHADOW");
            }
            if (text != "arrayLength")
                continue;
            require(braces && (!index || token(index - 1u) != ".") && token(index + 1u) == "("
                    && token(index + 2u) == "&",
                "SHADER-RUNTIME-LENGTH-TARGET-OPERATION");
            require(++calls <= budget.maxOperations, "SHADER-RUNTIME-LENGTH-OPERATION-BUDGET");
            auto next = index + 3u;
            const auto name = token(next++);
            const WgslBufferRuntimeReference* reference = nullptr;
            for (const auto& r : references)
                if (r.variable == name)
                    reference = &r;
            require(reference && reference->declaration.hasRuntimeArray,
                "SHADER-RUNTIME-LENGTH-TARGET-ROOT");
            if (!reference->runtimeMember.empty()) {
                require(token(next) == "." && token(next + 1u) == reference->runtimeMember,
                    "SHADER-RUNTIME-LENGTH-TARGET-MEMBER");
                next += 2u;
            }
            if (token(next) == ",")
                ++next;
            require(token(next) == ")", "SHADER-RUNTIME-LENGTH-TARGET-OPERATION");
            Facts* match = nullptr;
            for (auto& f : facts)
                if (f.group == reference->declaration.group
                    && f.targetBinding == reference->declaration.binding
                    && f.targetByteOffset == SIZE_MAX) {
                    match = &f;
                    break;
                }
            require(match, "SHADER-RUNTIME-LENGTH-TARGET-COVERAGE");
            match->targetByteOffset =
                static_cast<std::size_t>(tokens[index].text.data() - target.data());
            index = next;
        }
        require(!braces && calls == facts.size(), "SHADER-RUNTIME-LENGTH-TARGET-COVERAGE");
        for (const auto& f : facts)
            require(f.targetByteOffset != SIZE_MAX, "SHADER-RUNTIME-LENGTH-TARGET-COVERAGE");
        output = std::move(facts);
        return nullptr;
    } catch (const ShaderArtifactLoadError& error) {
        output.clear();
        return error.diagnostic;
    } catch (const std::bad_alloc&) {
        output.clear();
        return "SHADER-RUNTIME-LENGTH-ALLOCATION";
    }
}
} // namespace arp::shader

#include "scriptvm/aot/aot_codegen_v5.h"

#include "core/sha256.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace arp::scriptvm {
namespace {

using String = core::pmr::String;

String quote(const String& value)
{
    String result("\"", &core::defaultCpuMemoryResource());
    for (const char c : value) {
        switch (c) {
        case '\\':
            result += "\\\\";
            break;
        case '\"':
            result += "\\\"";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            result += c;
            break;
        }
    }
    result += "\"";
    return result;
}

String symbolPart(const String& value)
{
    String result(&core::defaultCpuMemoryResource());
    for (const unsigned char c : value) {
        result += std::isalnum(c) ? static_cast<char>(c) : '_';
    }
    if (result.empty() || std::isdigit(static_cast<unsigned char>(result[0]))) {
        result.insert(result.begin(), '_');
    }
    return result;
}

String sha256(const String& value)
{
    static constexpr char digits[] = "0123456789abcdef";
    const auto digest = core::Sha256::digest(core::StringView(value.data(), value.size()));
    String result("sha256:", &core::defaultCpuMemoryResource());
    result.reserve(71u);
    for (const auto byte : digest) {
        result += digits[(byte >> 4u) & 0xfu];
        result += digits[byte & 0xfu];
    }
    return result;
}

ScriptResult unsupported(
    const TypedIrFunction& function, const TypedIrOperationRecord* operation, const char* reason)
{
    String message("DirectNative CFG proof failed: function=", &core::defaultCpuMemoryResource());
    message += function.id;
    if (operation != nullptr) {
        message += " opcode=";
        message += typedIrOpcodeName(operation->opcode);
        if (!operation->targetId.empty()) {
            message += " target=";
            message += operation->targetId;
        }
        if (!operation->callableBindingId.empty()) {
            message += " binding=";
            message += operation->callableBindingId;
        }
    }
    message += " reason=";
    message += reason;
    ScriptDiagnostic diagnostic{ScriptDiagnosticCode::AotArtifactInvalid, std::move(message)};
    if (operation != nullptr) {
        diagnostic.file = operation->source.file;
        diagnostic.line = operation->source.line;
        diagnostic.column = operation->source.column;
    }
    return ScriptResult::failure(std::move(diagnostic));
}

core::pmr::Vector<const TypedIrOperationRecord*> flatten(const TypedIrFunction& function);

int resolveFunctionIndex(const TypedIrModule& module, const core::pmr::String& targetId)
{
    const auto target = std::find_if(
        module.functions.begin(), module.functions.end(), [&targetId](const auto& function) {
            return function.id == targetId;
        });
    return target == module.functions.end() ? -1
                                            : static_cast<int>(target - module.functions.begin());
}

bool staticallyProvenCollection(const TypedIrFunction& function)
{
    if (function.isGenerator)
        return false;
    if (function.signature.result.kind == TypedIrType::List)
        return true;
    if (function.signature.result.kind != TypedIrType::Any)
        return false;

    bool sawReturn = false;
    const auto operations = flatten(function);
    for (std::size_t pc = 0u; pc < operations.size(); ++pc) {
        if (operations[pc]->opcode != TypedIrOpcode::Return)
            continue;
        sawReturn = true;
        if (operations[pc]->a != 1 || pc == 0u
            || operations[pc - 1u]->opcode != TypedIrOpcode::MakeList) {
            return false;
        }
    }
    return sawReturn;
}

bool isDynamicDelegationSource(
    const TypedIrFunction& function, const TypedIrOperationRecord& operation)
{
    if (operation.opcode != TypedIrOpcode::LoadLocal || operation.a < 0)
        return false;
    for (const auto& local : function.locals) {
        if (local.slot == operation.a)
            return local.type.kind == TypedIrType::Any;
    }
    return false;
}

ScriptResult validateNativeYieldFromShape(
    const TypedIrModule& module, const TypedIrFunction& function)
{
    if (!function.hasYieldFrom)
        return ScriptResult::success();
    if (!function.isGenerator)
        return unsupported(
            function, nullptr, "yield-from metadata is only valid on generator functions");

    const auto operations = flatten(function);
    std::size_t validatedSiteCount = 0u;
    for (const auto& site : function.delegationSites) {
        if (site.sourcePc < 0 || site.sourcePc >= static_cast<int>(operations.size())
            || site.rangePc < 0 || site.rangePc >= static_cast<int>(operations.size())) {
            return unsupported(
                function, nullptr, "yield-from source metadata is outside the function");
        }
        const auto* operation = operations[static_cast<std::size_t>(site.rangePc)];
        const auto* call = operations[static_cast<std::size_t>(site.sourcePc)];
        ++validatedSiteCount;
        if (call->opcode == TypedIrOpcode::Call) {
            const int childIndex = resolveFunctionIndex(module, call->targetId);
            if (childIndex < 0)
                return unsupported(function, call, "yield-from child is not statically proven");
            const auto& child = module.functions[static_cast<std::size_t>(childIndex)];
            if (!child.isGenerator && !staticallyProvenCollection(child))
                return unsupported(function,
                    call,
                    "yield-from child is not a statically proven generator or collection");
        } else if (call->opcode == TypedIrOpcode::CallValue && !call->callableBindingId.empty()) {
            const auto binding = std::find_if(module.callableBindings.begin(),
                module.callableBindings.end(),
                [&call](const auto& candidate) {
                    return candidate.bindingId == call->callableBindingId;
                });
            if (binding == module.callableBindings.end() || !binding->generatorBridge
                || binding->origin != TypedIrCallableOrigin::ScriptFunction) {
                return unsupported(function,
                    call,
                    "yield-from CallValue requires an explicit generator bridge binding");
            }
        } else if (isDynamicDelegationSource(function, *call)) {
            // An explicitly Any local is a runtime delegation contract. The
            // generated RangeNext path validates DirectNative/V3 identity or
            // performs the canonical collection operation; all other values
            // fail deterministically through the semantic runtime.
        } else {
            return unsupported(
                function, operation, "yield-from source must have an explicit generator contract");
        }
    }
    if (validatedSiteCount == 0u)
        return unsupported(function, nullptr, "yield-from metadata has no delegated range site");
    return ScriptResult::success();
}

bool supportedOperation(TypedIrOpcode opcode)
{
    return opcode != TypedIrOpcode::OpcodeCount && opcode != TypedIrOpcode::CallHost
        && opcode != TypedIrOpcode::AddrIndex && opcode != TypedIrOpcode::AddrField;
}

String binaryOperation(TypedIrOpcode opcode)
{
    if (opcode == TypedIrOpcode::Add || opcode == TypedIrOpcode::IntAdd)
        return "Add";
    if (opcode == TypedIrOpcode::Sub || opcode == TypedIrOpcode::IntSub)
        return "Sub";
    if (opcode == TypedIrOpcode::Mul || opcode == TypedIrOpcode::IntMul)
        return "Mul";
    if (opcode == TypedIrOpcode::Div || opcode == TypedIrOpcode::IntDiv)
        return "Div";
    if (opcode == TypedIrOpcode::Mod || opcode == TypedIrOpcode::IntMod)
        return "Mod";
    if (opcode == TypedIrOpcode::Pow)
        return "Pow";
    if (opcode == TypedIrOpcode::FloorDiv)
        return "FloorDiv";
    if (opcode == TypedIrOpcode::BitAnd)
        return "BitAnd";
    if (opcode == TypedIrOpcode::BitOr)
        return "BitOr";
    if (opcode == TypedIrOpcode::BitXor)
        return "BitXor";
    if (opcode == TypedIrOpcode::BitClear)
        return "BitClear";
    if (opcode == TypedIrOpcode::ShiftLeft)
        return "ShiftLeft";
    if (opcode == TypedIrOpcode::ShiftRight)
        return "ShiftRight";
    return {};
}

String compareOperation(TypedIrOpcode opcode)
{
    if (opcode == TypedIrOpcode::CmpEq || opcode == TypedIrOpcode::IntCmpEq)
        return "Equal";
    if (opcode == TypedIrOpcode::CmpNe || opcode == TypedIrOpcode::IntCmpNe)
        return "NotEqual";
    if (opcode == TypedIrOpcode::CmpLt || opcode == TypedIrOpcode::IntCmpLt)
        return "Less";
    if (opcode == TypedIrOpcode::CmpLe || opcode == TypedIrOpcode::IntCmpLe)
        return "LessEqual";
    if (opcode == TypedIrOpcode::CmpGt || opcode == TypedIrOpcode::IntCmpGt)
        return "Greater";
    if (opcode == TypedIrOpcode::CmpGe || opcode == TypedIrOpcode::IntCmpGe)
        return "GreaterEqual";
    return {};
}

String conversionOperation(TypedIrOpcode opcode)
{
    if (opcode == TypedIrOpcode::ConvertInt)
        return "Int";
    if (opcode == TypedIrOpcode::ConvertInt64)
        return "Int64";
    if (opcode == TypedIrOpcode::ConvertFloat)
        return "Float";
    if (opcode == TypedIrOpcode::ConvertString)
        return "String";
    if (opcode == TypedIrOpcode::ConvertBool)
        return "Bool";
    if (opcode == TypedIrOpcode::ConvertBytes)
        return "Bytes";
    return {};
}

core::pmr::Vector<const TypedIrOperationRecord*> flatten(const TypedIrFunction& function)
{
    core::pmr::Vector<const TypedIrOperationRecord*> result{&core::defaultCpuMemoryResource()};
    for (const auto& block : function.blocks) {
        for (const auto& operation : block.operations)
            result.push_back(&operation);
    }
    return result;
}

bool scalarValueType(const TypedIrTypeRef& type)
{
    return type.kind == TypedIrType::Int || type.kind == TypedIrType::Bool;
}

bool scalarOperation(TypedIrOpcode opcode)
{
    switch (opcode) {
    case TypedIrOpcode::LoadBool:
    case TypedIrOpcode::LoadInt:
    case TypedIrOpcode::LoadLocal:
    case TypedIrOpcode::StoreLocal:
    case TypedIrOpcode::Pop:
    case TypedIrOpcode::Add:
    case TypedIrOpcode::Sub:
    case TypedIrOpcode::Mul:
    case TypedIrOpcode::Div:
    case TypedIrOpcode::Mod:
    case TypedIrOpcode::IntAdd:
    case TypedIrOpcode::IntSub:
    case TypedIrOpcode::IntMul:
    case TypedIrOpcode::IntDiv:
    case TypedIrOpcode::IntMod:
    case TypedIrOpcode::CmpEq:
    case TypedIrOpcode::CmpNe:
    case TypedIrOpcode::CmpLt:
    case TypedIrOpcode::CmpLe:
    case TypedIrOpcode::CmpGt:
    case TypedIrOpcode::CmpGe:
    case TypedIrOpcode::IntCmpEq:
    case TypedIrOpcode::IntCmpNe:
    case TypedIrOpcode::IntCmpLt:
    case TypedIrOpcode::IntCmpLe:
    case TypedIrOpcode::IntCmpGt:
    case TypedIrOpcode::IntCmpGe:
    case TypedIrOpcode::Jump:
    case TypedIrOpcode::JumpIfFalse:
    case TypedIrOpcode::JumpIfTrue:
    case TypedIrOpcode::JumpIfLocalIntLtConstFalse:
    case TypedIrOpcode::LocalAddIntConst:
    case TypedIrOpcode::LocalSubIntConst:
    case TypedIrOpcode::LocalAddLocal:
    case TypedIrOpcode::Return:
    case TypedIrOpcode::ReturnInt:
    case TypedIrOpcode::Call:
        return true;
    default:
        return false;
    }
}

bool scalarFunctionShape(const TypedIrFunction& function)
{
    if (function.isGenerator || function.captureCount != 0 || !function.exceptionRegions.empty()
        || !scalarValueType(function.signature.result)) {
        return false;
    }
    for (const auto& parameter : function.signature.parameters) {
        if (!scalarValueType(parameter))
            return false;
    }
    for (const auto& local : function.locals) {
        if (!scalarValueType(local.type))
            return false;
    }
    for (const auto* operation : flatten(function)) {
        if (!scalarOperation(operation->opcode))
            return false;
    }
    return true;
}

core::pmr::Vector<std::uint8_t> scalarFunctionCandidates(const TypedIrModule& module)
{
    core::pmr::Vector<std::uint8_t> candidates{&core::defaultCpuMemoryResource()};
    candidates.resize(module.functions.size(), 0u);
    for (std::size_t index = 0; index < module.functions.size(); ++index) {
        if (scalarFunctionShape(module.functions[index]))
            candidates[index] = 1u;
    }
    for (std::size_t index = 0; index < module.functions.size(); ++index) {
        if (candidates[index] == 0u)
            continue;
        for (const auto* operation : flatten(module.functions[index])) {
            if (operation->opcode != TypedIrOpcode::Call)
                continue;
            const int targetIndex = resolveFunctionIndex(module, operation->targetId);
            if (targetIndex < 0 || candidates[static_cast<std::size_t>(targetIndex)] == 0u) {
                candidates[index] = 0u;
                break;
            }
        }
    }
    return candidates;
}

const char* scalarResultKind(const TypedIrFunction& function)
{
    return function.signature.result.kind == TypedIrType::Bool ? "bool" : "int";
}

void emitScalarFunction(String& source,
    const TypedIrModule& module,
    std::size_t functionIndex,
    const core::pmr::Vector<String>& symbols,
    const core::pmr::Vector<String>& scalarSymbols,
    const core::pmr::Vector<String>& scalarNativeSymbols,
    const core::pmr::Vector<std::uint8_t>& scalarCandidates)
{
    const auto& function = module.functions[functionIndex];
    const auto operations = flatten(function);
    const auto symbol = scalarSymbols[functionIndex];
    const auto nativeSymbol = scalarNativeSymbols[functionIndex];
    source += "static bool ";
    source += nativeSymbol;
    source +=
        "(DirectNativeExecutionContext& context, const std::int64_t* args, std::size_t argCount, std::int64_t* result, VmError* error) {\n";
    source += "    if (argCount != ";
    source += std::to_string(function.parameterCount);
    source +=
        "u || (argCount != 0u && args == nullptr) || result == nullptr) { if (error != nullptr) { error->code = ScriptDiagnosticCode::WrongArgCount; error->message = \"DirectNative scalar argument count mismatch\"; } return false; }\n";
    source += "    if (!context.enterFunction(";
    source += quote(function.id);
    source += ", error)) return false;\n";
    source +=
        "    struct Scope { DirectNativeExecutionContext& context; ~Scope() { context.leaveFunction(); } } scope { context };\n";
    source += "    std::int64_t locals[";
    source += std::to_string(std::max(1, function.localCount));
    source += "]{};\n";
    source += "    std::uint8_t localKinds[";
    source += std::to_string(std::max(1, function.localCount));
    source += "]{};\n";
    source += "    std::int64_t stack[";
    source += std::to_string(std::max(1, function.maxStack));
    source += "]{};\n";
    source += "    std::uint8_t stackKinds[";
    source += std::to_string(std::max(1, function.maxStack));
    source += "]{};\n";
    source += "    std::size_t sp = 0u;\n";
    for (int index = 0; index < function.parameterCount; ++index) {
        source += "    locals[";
        source += std::to_string(index);
        source += "] = args[";
        source += std::to_string(index);
        source += "];\n";
        source += "    localKinds[";
        source += std::to_string(index);
        source += "] = ";
        source +=
            function.signature.parameters[static_cast<std::size_t>(index)].kind == TypedIrType::Bool
            ? "1u;\n"
            : "0u;\n";
    }
    source += "    goto aot_scalar_pc_0;\n";
    for (std::size_t pc = 0; pc < operations.size(); ++pc) {
        const auto& operation = *operations[pc];
        const auto opcode = operation.opcode;
        source += "aot_scalar_pc_";
        source += std::to_string(pc);
        source += ": {\n";
        source +=
            "        if (context.remainingInstructions == 0u) { if (error != nullptr) { error->code = ScriptDiagnosticCode::ExecutionBudgetExceeded; error->message = \"DirectNative instruction budget exceeded\"; } return false; } --context.remainingInstructions;\n";
        if (opcode == TypedIrOpcode::LoadBool || opcode == TypedIrOpcode::LoadInt) {
            source += "        stack[sp] = ";
            source += opcode == TypedIrOpcode::LoadBool ? (operation.a ? "1" : "0")
                                                        : std::to_string(operation.i64);
            source += "; stackKinds[sp++] = ";
            source += opcode == TypedIrOpcode::LoadBool ? "1u;\n" : "0u;\n";
        } else if (opcode == TypedIrOpcode::LoadLocal) {
            source += "        stack[sp] = locals[";
            source += std::to_string(operation.a);
            source += "]; stackKinds[sp++] = localKinds[";
            source += std::to_string(operation.a);
            source += "];\n";
        } else if (opcode == TypedIrOpcode::StoreLocal) {
            source += "        --sp; locals[";
            source += std::to_string(operation.a);
            source += "] = stack[sp]; localKinds[";
            source += std::to_string(operation.a);
            source += "] = stackKinds[sp];\n";
        } else if (opcode == TypedIrOpcode::Pop) {
            source += "        --sp;\n";
        } else if (opcode == TypedIrOpcode::Add || opcode == TypedIrOpcode::Sub
            || opcode == TypedIrOpcode::Mul || opcode == TypedIrOpcode::Div
            || opcode == TypedIrOpcode::Mod || opcode == TypedIrOpcode::IntAdd
            || opcode == TypedIrOpcode::IntSub || opcode == TypedIrOpcode::IntMul
            || opcode == TypedIrOpcode::IntDiv || opcode == TypedIrOpcode::IntMod) {
            source +=
                "        { const std::int64_t rhs = stack[--sp]; const std::int64_t lhs = stack[--sp];";
            if (opcode == TypedIrOpcode::Div || opcode == TypedIrOpcode::Mod
                || opcode == TypedIrOpcode::IntDiv || opcode == TypedIrOpcode::IntMod) {
                source +=
                    " if (rhs == 0) { if (error != nullptr) { error->code = ScriptDiagnosticCode::DivisionByZero; error->message = \"division by zero\"; } return false; } if (lhs == std::numeric_limits<int>::min() && rhs == -1) { if (error != nullptr) { error->code = ScriptDiagnosticCode::IntegerOverflow; error->message = \"int arithmetic overflow\"; } return false; }";
            }
            source += " const std::int64_t value = lhs ";
            if (opcode == TypedIrOpcode::Add || opcode == TypedIrOpcode::IntAdd)
                source += "+";
            else if (opcode == TypedIrOpcode::Sub || opcode == TypedIrOpcode::IntSub)
                source += "-";
            else if (opcode == TypedIrOpcode::Mul || opcode == TypedIrOpcode::IntMul)
                source += "*";
            else if (opcode == TypedIrOpcode::Div || opcode == TypedIrOpcode::IntDiv)
                source += "/";
            else
                source += "%";
            source +=
                " rhs; if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max()) { if (error != nullptr) { error->code = ScriptDiagnosticCode::IntegerOverflow; error->message = \"int arithmetic overflow\"; } return false; } stack[sp] = value; stackKinds[sp++] = 0u; }\n";
        } else if (opcode == TypedIrOpcode::CmpEq || opcode == TypedIrOpcode::CmpNe
            || opcode == TypedIrOpcode::CmpLt || opcode == TypedIrOpcode::CmpLe
            || opcode == TypedIrOpcode::CmpGt || opcode == TypedIrOpcode::CmpGe
            || opcode == TypedIrOpcode::IntCmpEq || opcode == TypedIrOpcode::IntCmpNe
            || opcode == TypedIrOpcode::IntCmpLt || opcode == TypedIrOpcode::IntCmpLe
            || opcode == TypedIrOpcode::IntCmpGt || opcode == TypedIrOpcode::IntCmpGe) {
            source +=
                "        { const std::int64_t rhs = stack[--sp]; const std::int64_t lhs = stack[--sp]; stack[sp] = ";
            if (opcode == TypedIrOpcode::CmpEq || opcode == TypedIrOpcode::IntCmpEq)
                source += "lhs == rhs";
            else if (opcode == TypedIrOpcode::CmpNe || opcode == TypedIrOpcode::IntCmpNe)
                source += "lhs != rhs";
            else if (opcode == TypedIrOpcode::CmpLt || opcode == TypedIrOpcode::IntCmpLt)
                source += "lhs < rhs";
            else if (opcode == TypedIrOpcode::CmpLe || opcode == TypedIrOpcode::IntCmpLe)
                source += "lhs <= rhs";
            else if (opcode == TypedIrOpcode::CmpGt || opcode == TypedIrOpcode::IntCmpGt)
                source += "lhs > rhs";
            else
                source += "lhs >= rhs";
            source += " ? 1 : 0; stackKinds[sp++] = 1u; }\n";
        } else if (opcode == TypedIrOpcode::Jump) {
            source += "        goto aot_scalar_pc_";
            source += std::to_string(operation.a);
            source += ";\n";
        } else if (opcode == TypedIrOpcode::JumpIfFalse || opcode == TypedIrOpcode::JumpIfTrue) {
            source += "        if (";
            source +=
                opcode == TypedIrOpcode::JumpIfFalse ? "stack[--sp] == 0" : "stack[--sp] != 0";
            source += ") goto aot_scalar_pc_";
            source += std::to_string(operation.a);
            source += ";\n";
        } else if (opcode == TypedIrOpcode::JumpIfLocalIntLtConstFalse) {
            source += "        if (!(locals[";
            source += std::to_string(operation.b);
            source += "] < ";
            source += std::to_string(operation.i64);
            source += ")) goto aot_scalar_pc_";
            source += std::to_string(operation.a);
            source += ";\n";
        } else if (opcode == TypedIrOpcode::LocalAddIntConst
            || opcode == TypedIrOpcode::LocalSubIntConst
            || opcode == TypedIrOpcode::LocalAddLocal) {
            source += "        { const std::int64_t value = locals[";
            source += std::to_string(operation.a);
            if (opcode == TypedIrOpcode::LocalAddLocal) {
                source += "] + locals[";
                source += std::to_string(operation.b);
                source += "];";
            } else {
                source += "] ";
                source += opcode == TypedIrOpcode::LocalAddIntConst ? "+" : "-";
                source += " ";
                source += std::to_string(operation.i64);
                source += ";";
            }
            source +=
                " if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max()) { if (error != nullptr) { error->code = ScriptDiagnosticCode::IntegerOverflow; error->message = \"int arithmetic overflow\"; } return false; } locals[";
            source += std::to_string(operation.a);
            source += "] = value; localKinds[";
            source += std::to_string(operation.a);
            source += "] = 0u; }\n";
        } else if (opcode == TypedIrOpcode::Call) {
            const auto targetIndex =
                static_cast<std::size_t>(resolveFunctionIndex(module, operation.targetId));
            source += "        { constexpr std::size_t count = ";
            source += std::to_string(operation.b);
            source +=
                "u; const std::size_t base = sp - count; std::array<std::int64_t, count> callArgs {}; for (std::size_t index = 0; index < count; ++index) callArgs[index] = stack[base + index]; sp = base; std::int64_t value = 0; if (!";
            source += scalarNativeSymbols[targetIndex];
            source +=
                "(context, callArgs.data(), count, &value, error)) return false; stack[sp] = value; stackKinds[sp++] = ";
            source += module.functions[targetIndex].signature.result.kind == TypedIrType::Bool
                ? "1u; }\n"
                : "0u; }\n";
        } else if (opcode == TypedIrOpcode::Return || opcode == TypedIrOpcode::ReturnInt) {
            source += "        { *result = stack[--sp]; return true; }\n";
        }
        source += "    }\n";
    }
    source +=
        "    if (error != nullptr) { error->code = ScriptDiagnosticCode::AotArtifactInvalid; error->message = \"DirectNative scalar control flow fell through\"; } return false;\n}\n";
    source += "Value ";
    source += symbol;
    source +=
        "(DirectNativeExecutionContext& context, const Value* args, std::size_t argCount, VmError* error) {\n";
    source += "    if (argCount != ";
    source += std::to_string(function.parameterCount);
    source +=
        "u || (argCount != 0u && args == nullptr)) { if (error != nullptr) { error->code = ScriptDiagnosticCode::WrongArgCount; error->message = \"DirectNative scalar argument count mismatch\"; } return makeNil(); }\n";
    source += "    std::array<std::int64_t, ";
    source += std::to_string(std::max(1, function.parameterCount));
    source += "> scalarArgs {};\n";
    for (int index = 0; index < function.parameterCount; ++index) {
        source += "    { ";
        if (function.signature.parameters[static_cast<std::size_t>(index)].kind
            == TypedIrType::Bool) {
            source += "bool value = false; if (!tryGetBool(args[";
            source += std::to_string(index);
            source +=
                "], value)) { if (error != nullptr) { error->code = ScriptDiagnosticCode::BadCast; error->message = \"DirectNative scalar argument type mismatch\"; } return makeNil(); } scalarArgs[";
            source += std::to_string(index);
            source += "] = value ? 1 : 0; }\n";
        } else {
            source += "int value = 0; if (!tryGetInt(args[";
            source += std::to_string(index);
            source +=
                "], value)) { if (error != nullptr) { error->code = ScriptDiagnosticCode::BadCast; error->message = \"DirectNative scalar argument type mismatch\"; } return makeNil(); } scalarArgs[";
            source += std::to_string(index);
            source += "] = value; }\n";
        }
    }
    source += "    std::int64_t scalarResult = 0; if (!";
    source += nativeSymbol;
    source += "(context, scalarArgs.data(), ";
    source += std::to_string(function.parameterCount);
    source += "u, &scalarResult, error)) return makeNil();\n";
    if (function.signature.result.kind == TypedIrType::Bool)
        source += "    return makeBool(scalarResult != 0);\n";
    else
        source += "    return makeInt(static_cast<int>(scalarResult));\n";
    source += "}\n";
    (void)symbols;
    (void)scalarCandidates;
}

void emitFailureCheck(String& source, const char* resultName)
{
    source += "        if (!";
    source += resultName;
    source += ".ok()) { if (error != nullptr) { error->code = ";
    source += resultName;
    source += ".diagnostic.code; error->message = ";
    source += resultName;
    source += ".diagnostic.message; } goto aot_exception_dispatch; }\n";
}

void emitFunction(String& source,
    const TypedIrModule& module,
    std::size_t functionIndex,
    const core::pmr::Vector<String>& symbols,
    const core::pmr::Vector<String>& scalarSymbols,
    const core::pmr::Vector<std::uint8_t>& scalarCandidates,
    const String& functionIdentity)
{
    const auto& function = module.functions[functionIndex];
    const auto operations = flatten(function);
    const auto& delegatedSites = function.delegationSites;
    const bool hasDelegatedSites = !delegatedSites.empty();
    const bool hasHandlerState = !function.exceptionRegions.empty();
    const bool hasCaptureState = function.captureCount != 0;
    const bool hasContinuationState =
        std::any_of(operations.begin(),
            operations.end(),
            [](const auto* operation) {
                return operation->opcode == TypedIrOpcode::Call
                    || operation->opcode == TypedIrOpcode::CallValue
                    || operation->opcode == TypedIrOpcode::MakeInstance
                    || operation->opcode == TypedIrOpcode::Await;
            })
        || hasDelegatedSites;
    const bool hasAwaitState =
        std::any_of(operations.begin(), operations.end(), [](const auto* operation) {
            return operation->opcode == TypedIrOpcode::Await;
        });
    source += "Value ";
    source += symbols[functionIndex];
    source +=
        "(DirectNativeExecutionContext& context, const Value* args, std::size_t argCount, DirectNativeResumeStateV5* suppliedResume, VmError* error) {\n";
    if (function.isGenerator) {
        source +=
            "    if (suppliedResume == nullptr) return makeDirectNativeGeneratorV5(context, &";
        source += symbols[functionIndex];
        source += ", args, argCount, error);\n";
    }
    if (scalarCandidates[functionIndex] != 0u) {
        source +=
            "    if (suppliedResume == nullptr && context.operationBoundary == nullptr) return ";
        source += scalarSymbols[functionIndex];
        source += "(context, args, argCount, error);\n";
    }
    source +=
        "    DirectNativeResumeStateV5 localResume; auto& resume = suppliedResume != nullptr ? *suppliedResume : localResume;\n";
    source +=
        "    if (resume.ownershipTransferred) { if (error != nullptr) { error->code = ScriptDiagnosticCode::AotSwitchUnsupported; error->message = \"DirectNative continuation ownership was transferred\"; } return makeNil(); }\n";
    source +=
        "    struct Frame final : DirectNativeFrameV5 { explicit Frame(DirectNativeExecutionContext* owner = nullptr, bool retainCharge = true) : DirectNativeFrameV5(";
    source += quote(function.id);
    source += ", ";
    source += quote(functionIdentity);
    source +=
        "), owner(owner), ownsContinuationCharge(owner != nullptr && retainCharge) { if (ownsContinuationCharge) owner->retainContinuationFrame(sizeof(Frame)); } ~Frame() override { ";
    if (hasDelegatedSites)
        source +=
            "if (isDirectNativeGeneratorV5(delegatedChild)) { if (auto* child = asDirectNativeGeneratorV5(delegatedChild); child != nullptr) closeDirectNativeGeneratorV5(*child); } else if (const auto* bridge = optionalGeneratorBridgeV3(); bridge != nullptr && bridge->isGenerator != nullptr && bridge->isGenerator(delegatedChild) && bridge->close != nullptr) bridge->close(delegatedChild); ";
    source +=
        "if (owner != nullptr && ownsContinuationCharge) owner->releaseContinuationFrame(continuationChargeBytes); if (owner != nullptr) owner->releaseMemoryRoots(this); } bool continuationFrameCharge(const DirectNativeExecutionContext& expected, std::size_t& bytes) const noexcept override { if (owner != &expected || !ownsContinuationCharge) return false; bytes = continuationChargeBytes; return bytes != 0u; } bool relinquishContinuationFrameCharge(DirectNativeExecutionContext& expected, std::size_t& bytes) noexcept override { if (!continuationFrameCharge(expected, bytes)) return false; ownsContinuationCharge = false; return true; } void relinquishDelegatedChildForTransfer() noexcept override { ";
    if (hasDelegatedSites)
        source += "delegatedChild = makeNil(); ";
    source +=
        "} void adoptContinuationFrameCharge(std::size_t bytes) noexcept { continuationChargeBytes = bytes; ownsContinuationCharge = true; } DirectNativeExecutionContext* owner = nullptr; std::size_t continuationChargeBytes = sizeof(Frame); bool ownsContinuationCharge = false; std::array<Value, ";
    source += std::to_string(std::max(1, function.localCount));
    source += "> locals {}; std::array<Value, ";
    source += std::to_string(std::max(1, function.maxStack));
    source +=
        "> stack {}; std::size_t sp = 0u; struct Handler { std::uint32_t pc = 0u; std::size_t stackDepth = 0u; ScriptDiagnosticCode filter = ScriptDiagnosticCode::None; }; ";
    if (hasHandlerState)
        source +=
            "arp::core::pmr::Vector<Handler> handlers { &arp::core::defaultCpuMemoryResource() }; ";
    if (hasCaptureState)
        source +=
            "arp::core::pmr::Vector<std::shared_ptr<ScriptCell>> captures { &arp::core::defaultCpuMemoryResource() }; ";
    if (hasContinuationState)
        source +=
            "DirectNativeResumeStateV5 childResume; std::uint32_t childPc = 0xffffffffu; Value pendingValue; ";
    if (hasDelegatedSites)
        source += "Value delegatedChild; ";
    source += "};\n";
    source +=
        "    Frame localFrame(&context, false); Frame* frame = &localFrame; const bool resuming = resume.frame != nullptr;\n";
    source += "    if (resuming) { if (std::strcmp(resume.frame->functionId(), ";
    source += quote(function.id);
    source += ") != 0 || std::strcmp(resume.frame->functionIdentity(), ";
    source += quote(functionIdentity);
    source +=
        ") != 0) { if (error != nullptr) { error->code = ScriptDiagnosticCode::AotArtifactInvalid; error->message = \"DirectNative resume frame identity mismatch\"; } return makeNil(); } if (auto* nativeFrame = dynamic_cast<Frame*>(resume.frame.get())) { frame = nativeFrame; } else { auto* canonical = resume.frame.get(); if (!canonical->hasCanonicalState() || canonical->canonicalPc() != resume.pc || canonical->canonicalLocals().size() != frame->locals.size() || canonical->canonicalStack().size() > frame->stack.size() || canonical->canonicalHandlerPcs().size() != canonical->canonicalHandlerDepths().size()";
    if (hasCaptureState)
        source += " || canonical->canonicalCaptureCells().size() != ";
    else
        source += " || !canonical->canonicalCaptureCells().empty() || ";
    if (hasCaptureState)
        source += std::to_string(function.captureCount) + "u";
    else
        source += "false";
    source +=
        ") { if (error != nullptr) { error->code = ScriptDiagnosticCode::AotSwitchUnsupported; error->message = \"DirectNative canonical frame shape is not admissible\"; } return makeNil(); } ";
    if (hasHandlerState) {
        source +=
            " for (std::size_t index = 0; index < canonical->canonicalHandlerPcs().size(); ++index) { if (canonical->canonicalHandlerPcs()[index] >= ";
        source += std::to_string(operations.size());
        source +=
            " || canonical->canonicalHandlerDepths()[index] > frame->stack.size()) { if (error != nullptr) { error->code = ScriptDiagnosticCode::AotSwitchUnsupported; error->message = \"DirectNative canonical handler shape is not admissible\"; } return makeNil(); } } ";
    } else {
        source +=
            " if (!canonical->canonicalHandlerPcs().empty()) { if (error != nullptr) { error->code = ScriptDiagnosticCode::AotSwitchUnsupported; error->message = \"DirectNative canonical handler state is not admissible\"; } return makeNil(); } ";
    }
    if (hasAwaitState)
        source +=
            " if (canonical->canonicalAwaiting() && !isScriptAwaitable(canonical->canonicalSuspendedValue())) { if (error != nullptr) { error->code = ScriptDiagnosticCode::AotSwitchUnsupported; error->message = \"DirectNative canonical await state is not admissible\"; } return makeNil(); } ";
    source +=
        " std::size_t canonicalChargeBytes = 0u; if (!canonical->continuationFrameCharge(context, canonicalChargeBytes) || !context.canReplaceContinuationFrame(canonicalChargeBytes, sizeof(Frame), error)) return makeNil(); auto persistent = arp::core::makeQuotaDeferredUnique<Frame>(&context, false); frame = persistent.get(); for (std::size_t index = 0; index < canonical->canonicalLocals().size(); ++index) frame->locals[index] = canonical->canonicalLocals()[index]; frame->sp = canonical->canonicalStack().size(); for (std::size_t index = 0; index < frame->sp; ++index) frame->stack[index] = canonical->canonicalStack()[index]; ";
    if (hasAwaitState)
        source +=
            " if (canonical->canonicalAwaiting()) { frame->pendingValue = canonical->canonicalSuspendedValue(); frame->childPc = canonical->canonicalPc(); } ";
    if (hasCaptureState)
        source += " frame->captures = canonical->canonicalCaptureCells(); ";
    if (hasHandlerState)
        source +=
            " for (std::size_t index = 0; index < canonical->canonicalHandlerPcs().size(); ++index) frame->handlers.push_back(Frame::Handler { static_cast<std::uint32_t>(canonical->canonicalHandlerPcs()[index]), canonical->canonicalHandlerDepths()[index], ScriptDiagnosticCode::None }); ";
    if (hasDelegatedSites) {
        source +=
            " if (canonical->canonicalDelegatedActive()) { const bool canonicalDelegatedSite = false";
        for (const auto& site : delegatedSites) {
            source += " || (canonical->canonicalDelegatedSourceSlot() == ";
            source += std::to_string(site.sourceSlot);
            source += " && canonical->canonicalDelegatedRangePc() == ";
            source += std::to_string(site.rangePc);
            source += " && canonical->canonicalDelegatedYieldPc() == ";
            source += std::to_string(site.yieldPc);
            source +=
                " && (canonical->canonicalDelegatedChildAwaiting() ? canonical->canonicalDelegatedPendingYieldPc() == ";
            source += std::to_string(site.yieldPc);
            source += " : canonical->canonicalDelegatedPendingYieldPc() < 0))";
        }
        source +=
            "; if (!canonicalDelegatedSite) { if (error != nullptr) { error->code = ScriptDiagnosticCode::AotSwitchUnsupported; error->message = \"DirectNative canonical delegated metadata is not admissible\"; } return makeNil(); } const Value delegatedSource = frame->locals[static_cast<std::size_t>(canonical->canonicalDelegatedSourceSlot())];";
        source +=
            " if (isDirectNativeGeneratorV5(delegatedSource)) { auto* child = asDirectNativeGeneratorV5(delegatedSource); if (child == nullptr || child->state == nullptr || child->state->context == nullptr || child->state->done) { if (error != nullptr) { error->code = ScriptDiagnosticCode::AotSwitchUnsupported; error->message = \"DirectNative canonical delegated child state is unavailable\"; } return makeNil(); } GeneratorIdentityV3 childIdentity; childIdentity.moduleId = child->state->context->moduleId.c_str(); childIdentity.moduleVersion = child->state->context->moduleVersion; childIdentity.artifactIdentity = child->state->context->artifactIdentity.c_str(); if (!context.acceptsGeneratorIdentity(childIdentity)) { if (error != nullptr) { error->code = ScriptDiagnosticCode::AotSwitchUnsupported; error->message = \"DirectNative canonical delegated child artifact identity negotiation failed\"; } return makeNil(); } if (canonical->canonicalDelegatedChildAwaiting() && (child->state->resume.suspendKind != DirectNativeSuspendKindV5::Await || child->state->resume.frame == nullptr)) { if (error != nullptr) { error->code = ScriptDiagnosticCode::AotSwitchUnsupported; error->message = \"DirectNative canonical delegated child await state is unavailable\"; } return makeNil(); } frame->delegatedChild = delegatedSource; if (canonical->canonicalDelegatedChildAwaiting()) { resume.suspendKind = DirectNativeSuspendKindV5::Await; resume.awaitInjectionSupported = child->state->resume.awaitInjectionSupported; } } else if (const auto* bridge = optionalGeneratorBridgeV3(); bridge != nullptr && bridge->isGenerator != nullptr && bridge->isGenerator(delegatedSource)) { GeneratorIdentityV3 childIdentity; if (bridge->queryIdentity == nullptr || !bridge->queryIdentity(delegatedSource, childIdentity) || !context.acceptsGeneratorIdentity(childIdentity)) { if (error != nullptr) { error->code = ScriptDiagnosticCode::AotSwitchUnsupported; error->message = \"DirectNative canonical delegated child artifact identity negotiation failed\"; } return makeNil(); } if (canonical->canonicalDelegatedChildAwaiting() && (bridge->isSuspended == nullptr || !bridge->isSuspended(delegatedSource))) { if (error != nullptr) { error->code = ScriptDiagnosticCode::AotSwitchUnsupported; error->message = \"DirectNative canonical delegated child await state is unavailable\"; } return makeNil(); } frame->delegatedChild = delegatedSource; if (canonical->canonicalDelegatedChildAwaiting()) { const auto* capability = optionalGeneratorBridgeV3Capability(); resume.suspendKind = DirectNativeSuspendKindV5::Await; resume.awaitInjectionSupported = capability != nullptr && capability->isGenerator != nullptr && capability->isGenerator(delegatedSource) && capability->supportsPendingAwaitInjection != nullptr && capability->supportsPendingAwaitInjection(delegatedSource); } } else if (canonical->canonicalDelegatedChildAwaiting()) { if (error != nullptr) { error->code = ScriptDiagnosticCode::AotSwitchUnsupported; error->message = \"DirectNative canonical delegated child is not resumable\"; } return makeNil(); } } ";
        source +=
            " frame->childPc = static_cast<std::uint32_t>(canonical->canonicalDelegatedRangePc()); ";
    }
    source +=
        " if (!arp::core::transferCpuAllocationCharge(canonical, persistent.get())) { if (error != nullptr) { error->code = ScriptDiagnosticCode::ResourceLimitExceeded; error->message = \"DirectNative canonical continuation allocation charge transfer failed\"; } return makeNil(); } if (!canonical->relinquishContinuationFrameCharge(context, canonicalChargeBytes)) { if (error != nullptr) { error->code = ScriptDiagnosticCode::AotArtifactInvalid; error->message = \"DirectNative canonical continuation charge handoff failed\"; } return makeNil(); } context.replaceContinuationFrameCharge(canonicalChargeBytes, sizeof(Frame)); frame->adoptContinuationFrameCharge(sizeof(Frame)); resume.frame = std::move(persistent); } }\n";
    source +=
        "    else { if (resume.pc != 0u || resume.commitState != 0u) { if (error != nullptr) { error->code = ScriptDiagnosticCode::AotArtifactInvalid; error->message = \"DirectNative resume state has no frame\"; } return makeNil(); } if (suppliedResume != nullptr && ";
    source += function.isGenerator ? "true" : "context.operationBoundary != nullptr";
    source +=
        ") { if (!context.beginContinuationFrame(sizeof(Frame), error)) return makeNil(); arp::core::CpuMemoryQuotaLease frameLease; if (!frameLease.acquire(context.memoryQuota, sizeof(Frame))) { if (error != nullptr) { error->code = ScriptDiagnosticCode::ResourceLimitExceeded; error->message = \"DirectNative continuation frame memory budget exceeded\"; } return makeNil(); } auto persistent = arp::core::makeQuotaPrechargedUnique<Frame>(std::move(frameLease), &context); frame = persistent.get(); resume.frame = std::move(persistent); } }\n";
    source +=
        "    if (resume.commitState == 1u) { if (error != nullptr) { error->code = ScriptDiagnosticCode::AotSwitchUnsupported; error->message = \"DirectNative side effect has prepared but uncommitted state\"; } return makeNil(); }\n";
    source += "    if (resume.pc >= ";
    source += std::to_string(operations.size());
    source +=
        "u) { if (error != nullptr) { error->code = ScriptDiagnosticCode::AotArtifactInvalid; error->message = \"DirectNative resume pc is outside the function\"; } return makeNil(); }\n";
    source += "    if (!resuming && (argCount != ";
    source += std::to_string(function.parameterCount);
    source +=
        "u || (argCount != 0u && args == nullptr))) { if (error != nullptr) { error->code = ScriptDiagnosticCode::WrongArgCount; error->message = \"DirectNative argument count mismatch\"; } return makeNil(); }\n";
    source += "    if (!context.enterFunction(";
    source += quote(function.id);
    source += ", error)) return makeNil();\n";
    source +=
        "    struct Scope { DirectNativeExecutionContext& context; ~Scope() { context.leaveFunction(); } } scope { context };\n";
    source += "    if (!context.ensureGlobals(";
    source += std::to_string(module.globalCount);
    source += "u, error)) return makeNil();\n";
    source +=
        "    auto& locals = frame->locals; auto& stack = frame->stack; auto& sp = frame->sp;\n";
    if (function.captureCount != 0) {
        source +=
            "    if (!resuming) { if (context.activeCaptures == nullptr || context.activeCaptures->size() != ";
        source += std::to_string(function.captureCount);
        source +=
            "u) { if (error != nullptr) { error->code = ScriptDiagnosticCode::AotArtifactInvalid; error->message = \"DirectNative closure capture identity mismatch\"; } return makeNil(); } frame->captures = *context.activeCaptures; }\n";
    }
    source +=
        "    if (!resuming) for (std::size_t index = 0; index < argCount; ++index) locals[index] = args[index];\n";
    source +=
        "    auto synchronizeFrameRoots = [&]() { ScriptValueSpan memoryRoots[] = {{locals.data(), locals.size()}, {stack.data(), sp}";
    if (hasContinuationState)
        source += ", {&frame->pendingValue, 1u}";
    if (hasDelegatedSites)
        source += ", {&frame->delegatedChild, 1u}";
    source +=
        "}; return context.synchronizeMemoryRootsForOwner(frame, memoryRoots, sizeof(memoryRoots) / sizeof(memoryRoots[0]), ";
    if (hasCaptureState)
        source += "&frame->captures";
    else
        source += "nullptr";
    source += ", error); };\n";
    source += "    if (!synchronizeFrameRoots()) return makeNil();\n";
    if (hasAwaitState) {
        source +=
            "    if (resuming && resume.injectionKind != DirectNativeInjectionKindV5::None && frame->pendingValue.is<ScriptAwaitable>()) { auto* pendingAwait = frame->pendingValue.ptr<ScriptAwaitable>(); if (pendingAwait == nullptr || !pendingAwait->supportsInjection()) { if (error != nullptr) { error->code = ScriptDiagnosticCode::AotSwitchUnsupported; error->message = \"DirectNative pending await has no injection contract\"; } resume.injectionKind = DirectNativeInjectionKindV5::None; resume.injectionValue = makeNil(); return makeNil(); } Value injected; arp::core::pmr::String injectionDiagnostics { &arp::core::defaultCpuMemoryResource() }; const auto injectionStatus = pendingAwait->injectResult(resume.injectionKind == DirectNativeInjectionKindV5::Send ? ScriptAwaitInjectionKind::Send : ScriptAwaitInjectionKind::Throw, resume.injectionValue, injected, injectionDiagnostics); resume.injectionKind = DirectNativeInjectionKindV5::None; resume.injectionValue = makeNil(); if (injectionStatus == ScriptAwaitInjectionStatus::Rejected) { if (error != nullptr) { error->code = ScriptDiagnosticCode::AotSwitchUnsupported; error->message = injectionDiagnostics.empty() ? arp::core::pmr::String(\"DirectNative pending await rejected injection\", &arp::core::defaultCpuMemoryResource()) : std::move(injectionDiagnostics); } return makeNil(); } if (injectionStatus == ScriptAwaitInjectionStatus::Pending) { resume.suspendKind = DirectNativeSuspendKindV5::Await; if (!synchronizeFrameRoots()) return makeNil(); if (error != nullptr) { error->code = ScriptDiagnosticCode::Suspended; error->message = \"generator await injection remains pending\"; } return makeNil(); } if (injectionStatus == ScriptAwaitInjectionStatus::Failed) { if (error != nullptr) { error->code = ScriptDiagnosticCode::HostError; error->message = injectionDiagnostics.empty() ? arp::core::pmr::String(\"generator await injection failed\", &arp::core::defaultCpuMemoryResource()) : std::move(injectionDiagnostics); } goto aot_exception_dispatch; } resume.awaitInjectionReady = true; resume.injectionValue = std::move(injected); }\n";
    }
    if (hasDelegatedSites) {
        source += "    const bool resumingDelegatedYield = ";
        for (std::size_t index = 0u; index < delegatedSites.size(); ++index) {
            if (index != 0u)
                source += " || ";
            source += "resume.pc == ";
            source += std::to_string(delegatedSites[index].yieldPc + 1);
            source += "u";
        }
        source += ";\n";
        source +=
            "    if (resuming && resumingDelegatedYield && resume.injectionKind == DirectNativeInjectionKindV5::Send && isNil(frame->delegatedChild) && !(frame->pendingValue.is<ScriptAwaitable>()) && !isNil(resume.injectionValue)) { if (error != nullptr) { error->code = ScriptDiagnosticCode::AotSwitchUnsupported; error->message = \"DirectNative yield-from collection accepts only nil send()\"; } resume.injectionKind = DirectNativeInjectionKindV5::None; resume.injectionValue = makeNil(); return makeNil(); }\n";
    }
    source += "    if (resuming && resume.injectionKind == DirectNativeInjectionKindV5::Throw && ";
    source += hasDelegatedSites ? "isNil(frame->delegatedChild)" : "true";
    source +=
        ") { if (error != nullptr) { error->code = ScriptDiagnosticCode::UserRaise; if (!tryGetString(resume.injectionValue, error->message)) error->message = \"script raised a non-string value\"; } resume.injectionKind = DirectNativeInjectionKindV5::None; resume.injectionValue = makeNil(); goto aot_exception_dispatch; }\n";
    source += "    if (resuming && resume.injectionKind == DirectNativeInjectionKindV5::Send && ";
    source += hasDelegatedSites ? "isNil(frame->delegatedChild)" : "true";
    source +=
        ") { resume.injectionKind = DirectNativeInjectionKindV5::None; resume.injectionValue = makeNil(); }\n";
    source +=
        "    if (resuming) { const std::uint32_t dispatchPc = resume.pc; resume.commitState = 0u; switch (dispatchPc) {";
    for (std::size_t pc = 0; pc < operations.size(); ++pc) {
        source += " case ";
        source += std::to_string(pc);
        source += "u: goto aot_pc_";
        source += std::to_string(pc);
        source += ";";
    }
    source += " default: break; } }\n";
    for (std::size_t pc = 0; pc < operations.size(); ++pc) {
        const auto& operation = *operations[pc];
        const auto opcode = operation.opcode;
        source += "aot_pc_";
        source += std::to_string(pc);
        source += ":\n";
        source +=
            "    if (context.operationBoundary != nullptr && !synchronizeFrameRoots()) return makeNil();\n";
        // Capture the logical state before the operation-boundary hook. The
        // generated frame is already persistent whenever a migration hook is
        // active, so this is the exact pre-operation state and never a native
        // instruction-pointer snapshot.
        source += "    if (frame != &localFrame) { frame->captureCanonicalState(";
        source += std::to_string(pc);
        source += "u, locals.data(), locals.size(), stack.data(), sp, ";
        if (hasCaptureState)
            source += "frame->captures.data(), frame->captures.size(), ";
        else
            source += "nullptr, 0u, ";
        source += "nullptr, nullptr, 0u, false, makeNil()); ";
        String canonicalSourceLabel("aot:", &core::defaultCpuMemoryResource());
        canonicalSourceLabel += function.id;
        canonicalSourceLabel += ":pc:";
        canonicalSourceLabel += std::to_string(pc);
        source += "frame->setCanonicalSource(arp::core::pmr::String(";
        source += quote(canonicalSourceLabel);
        source += ", &arp::core::defaultCpuMemoryResource()), ";
        source += std::to_string(operation.source.line);
        source += ", ";
        source += std::to_string(operation.source.column);
        source += "); ";
        if (hasHandlerState)
            source += "frame->captureCanonicalHandlers(frame->handlers); ";
        source += "}\n";
        source +=
            "    if (context.operationBoundary == nullptr) { if (context.remainingInstructions == 0u) { if (error != nullptr) { error->code = ScriptDiagnosticCode::ExecutionBudgetExceeded; error->message = \"DirectNative instruction budget exceeded\"; } return makeNil(); } --context.remainingInstructions; } else if (!context.beginOperation(";
        source += quote(function.id);
        source += ", ";
        source += std::to_string(pc);
        source += "u, resume, error)) return makeNil();\n";
        if (opcode == TypedIrOpcode::LoadNil) {
            source += "    stack[sp++] = makeNil();\n";
        } else if (opcode == TypedIrOpcode::LoadBool) {
            source += "    stack[sp++] = makeBool(";
            source += operation.a ? "true" : "false";
            source += ");\n";
        } else if (opcode == TypedIrOpcode::LoadInt) {
            source += "    stack[sp++] = makeInt(";
            source += std::to_string(operation.i64);
            source += ");\n";
        } else if (opcode == TypedIrOpcode::LoadInt64) {
            source += "    stack[sp++] = makeInt64(";
            source += std::to_string(operation.i64);
            source += "LL);\n";
        } else if (opcode == TypedIrOpcode::LoadFloat) {
            source += "    { const std::uint64_t bits = ";
            source += std::to_string(static_cast<std::uint64_t>(operation.i64));
            source +=
                "ULL; double value = 0.0; std::memcpy(&value, &bits, sizeof(value)); stack[sp++] = makeFloat(value); }\n";
        } else if (opcode == TypedIrOpcode::LoadString) {
            source += "    stack[sp++] = makeString(std::string_view(";
            source += quote(operation.text);
            source += "));\n";
        } else if (opcode == TypedIrOpcode::LoadLocal) {
            source += "    stack[sp++] = locals[";
            source += std::to_string(operation.a);
            source += "];\n";
        } else if (opcode == TypedIrOpcode::StoreLocal) {
            source += "    locals[";
            source += std::to_string(operation.a);
            source += "] = stack[--sp];\n";
        } else if (opcode == TypedIrOpcode::LoadCapture) {
            source += "    if (frame->captures[";
            source += std::to_string(operation.a);
            source +=
                "] == nullptr) { if (error != nullptr) { error->code = ScriptDiagnosticCode::NilDeref; error->message = \"DirectNative capture cell is null\"; } return makeNil(); } stack[sp++] = frame->captures[";
            source += std::to_string(operation.a);
            source += "]->value;\n";
        } else if (opcode == TypedIrOpcode::StoreCapture) {
            source += "    if (frame->captures[";
            source += std::to_string(operation.a);
            source +=
                "] == nullptr) { if (error != nullptr) { error->code = ScriptDiagnosticCode::NilDeref; error->message = \"DirectNative capture cell is null\"; } return makeNil(); } frame->captures[";
            source += std::to_string(operation.a);
            source += "]->value = stack[--sp];\n";
        } else if (opcode == TypedIrOpcode::AddrLocal) {
            source += "    { Value& slot = locals[";
            source += std::to_string(operation.a);
            source +=
                "]; if (!slot.is<ScriptPointer>()) { auto cell = arp::core::makeShared<ScriptCell>(slot); slot = Value { ScriptPointer { cell } }; } stack[sp++] = slot; }\n";
        } else if (opcode == TypedIrOpcode::AddrCapture) {
            source += "    if (frame->captures[";
            source += std::to_string(operation.a);
            source +=
                "] == nullptr) { if (error != nullptr) { error->code = ScriptDiagnosticCode::NilDeref; error->message = \"DirectNative capture cell is null\"; } return makeNil(); } stack[sp++] = Value { ScriptPointer { frame->captures[";
            source += std::to_string(operation.a);
            source += "] } };\n";
        } else if (opcode == TypedIrOpcode::AddrGlobal) {
            source += "    { Value& slot = context.globals[";
            source += std::to_string(operation.a);
            source +=
                "]; if (!slot.is<ScriptPointer>()) { auto cell = arp::core::makeShared<ScriptCell>(slot); slot = Value { ScriptPointer { cell } }; } stack[sp++] = slot; }\n";
        } else if (opcode == TypedIrOpcode::Deref) {
            source +=
                "    { const Value pointerValue = stack[--sp]; const auto* pointer = pointerValue.ptr<ScriptPointer>(); if (pointer == nullptr) { if (error != nullptr) { error->code = ScriptDiagnosticCode::NilDeref; error->message = \"DirectNative dereference requires a script pointer\"; } return makeNil(); } auto semantic = ScriptSemanticRuntime::dereference(*pointer);\n";
            emitFailureCheck(source, "semantic");
            source += "        stack[sp++] = std::move(semantic.value); }\n";
        } else if (opcode == TypedIrOpcode::StoreIndirect) {
            source +=
                "    { Value pointerValue = stack[--sp]; const Value value = stack[--sp]; auto* pointer = pointerValue.ptr<ScriptPointer>(); if (pointer == nullptr) { if (error != nullptr) { error->code = ScriptDiagnosticCode::NilDeref; error->message = \"DirectNative indirect store requires a script pointer\"; } return makeNil(); } context.prepareSideEffect(resume, ";
            source += std::to_string(pc);
            source += "u); auto semantic = ScriptSemanticRuntime::storePointer(*pointer, value);\n";
            emitFailureCheck(source, "semantic");
            source += "        context.commitSideEffect(resume, ";
            source += std::to_string(pc);
            source += "u); }\n";
        } else if (opcode == TypedIrOpcode::LoadGlobal) {
            source += "    stack[sp++] = context.globals[";
            source += std::to_string(operation.a);
            source += "];\n";
        } else if (opcode == TypedIrOpcode::StoreGlobal) {
            source += "    context.globals[";
            source += std::to_string(operation.a);
            source += "] = stack[--sp];\n";
        } else if (opcode == TypedIrOpcode::LoadModule) {
            source +=
                "    stack[sp++] = Value { DirectNativeModuleReferenceV5 { arp::core::pmr::String(";
            source += quote(operation.text);
            source += ", &arp::core::defaultCpuMemoryResource()) } };\n";
        } else if (opcode == TypedIrOpcode::LoadModuleExport
            && !operation.callableBindingId.empty()) {
            source += "    /* module reference remains in the callable operand slot */\n";
        } else if (opcode == TypedIrOpcode::LoadModuleExport) {
            source +=
                "    { const Value moduleValue = stack[--sp]; if (!moduleValue.is<DirectNativeModuleReferenceV5>()) { if (error != nullptr) { error->code = ScriptDiagnosticCode::AotArtifactInvalid; error->message = \"DirectNative module export base is invalid\"; } return makeNil(); } Value exported; if (!context.resolveModuleExport(moduleValue.ptr<DirectNativeModuleReferenceV5>()->moduleId.c_str(), ";
            source += quote(operation.text);
            source +=
                ", exported, error)) goto aot_exception_dispatch; stack[sp++] = std::move(exported); }\n";
        } else if (opcode == TypedIrOpcode::LoadField
            && (!operation.callableBindingId.empty()
                || (pc + 1u < operations.size()
                    && operations[pc + 1u]->opcode == TypedIrOpcode::CallValue
                    && !operations[pc + 1u]->closureTargetFunctionId.empty()))) {
            source += "    /* receiver remains in the callable operand slot */\n";
        } else if (opcode == TypedIrOpcode::LoadField) {
            source +=
                "    { const Value base = stack[--sp]; if (base.is<DirectNativeModuleReferenceV5>()) { Value exported; if (!context.resolveModuleExport(base.ptr<DirectNativeModuleReferenceV5>()->moduleId.c_str(), ";
            source += quote(operation.text);
            source +=
                ", exported, error)) goto aot_exception_dispatch; stack[sp++] = std::move(exported); } else { auto semantic = ScriptSemanticRuntime::loadField(base, ";
            source += quote(operation.text);
            source += ");\n";
            emitFailureCheck(source, "semantic");
            source += "        stack[sp++] = std::move(semantic.value); } }\n";
        } else if (opcode == TypedIrOpcode::StoreField) {
            source +=
                "    { const Value value = stack[--sp]; const Value base = stack[--sp]; context.prepareSideEffect(resume, ";
            source += std::to_string(pc);
            source += "u); auto semantic = ScriptSemanticRuntime::storeField(base, ";
            source += quote(operation.text);
            source += ", value);\n";
            emitFailureCheck(source, "semantic");
            source += "        context.commitSideEffect(resume, ";
            source += std::to_string(pc);
            source += "u); }\n";
        } else if (opcode == TypedIrOpcode::LoadIndex) {
            source +=
                "    { Value index = std::move(stack[--sp]); Value base = std::move(stack[--sp]); auto semantic = ScriptSemanticRuntime::loadIndex(std::move(base), std::move(index));\n";
            emitFailureCheck(source, "semantic");
            source += "        stack[sp++] = std::move(semantic.value); }\n";
        } else if (opcode == TypedIrOpcode::StoreIndex) {
            source +=
                "    { Value value = std::move(stack[--sp]); Value index = std::move(stack[--sp]); Value base = std::move(stack[--sp]); context.prepareSideEffect(resume, ";
            source += std::to_string(pc);
            source +=
                "u); auto semantic = ScriptSemanticRuntime::storeIndex(std::move(base), std::move(index), std::move(value));\n";
            emitFailureCheck(source, "semantic");
            source += "        context.commitSideEffect(resume, ";
            source += std::to_string(pc);
            source += "u); }\n";
        } else if (opcode == TypedIrOpcode::MakeList || opcode == TypedIrOpcode::MakeDict) {
            const int operandCount =
                opcode == TypedIrOpcode::MakeDict ? operation.b * 2 : operation.b;
            source +=
                "    { arp::core::pmr::Vector<Value> operands { &arp::core::defaultCpuMemoryResource() }; operands.reserve(";
            source += std::to_string(std::max(0, operandCount));
            source += "u); constexpr std::size_t count = ";
            source += std::to_string(std::max(0, operandCount));
            source +=
                "u; sp -= count; for (std::size_t index = 0; index < count; ++index) operands.push_back(stack[sp + index]); auto semantic = ScriptSemanticRuntime::";
            source += opcode == TypedIrOpcode::MakeList ? "makeList" : "makeDict";
            source += "(operands, ";
            source += std::to_string(operation.b);
            source += ", context.budget.maxContainerValues);\n";
            emitFailureCheck(source, "semantic");
            source += "        stack[sp++] = std::move(semantic.value); }\n";
        } else if (opcode == TypedIrOpcode::MakeRange) {
            const auto start =
                static_cast<int>((static_cast<std::uint64_t>(operation.i64) >> 32u) & 0xffffffffu);
            const auto end =
                static_cast<int>(static_cast<std::uint64_t>(operation.i64) & 0xffffffffu);
            source += "    { auto semantic = ScriptSemanticRuntime::makeRange(";
            source += std::to_string(start);
            source += ", ";
            source += std::to_string(end);
            source += ", context.budget.maxContainerValues);\n";
            emitFailureCheck(source, "semantic");
            source += "        stack[sp++] = std::move(semantic.value); }\n";
        } else if (opcode == TypedIrOpcode::MakeRangeFromStack) {
            source +=
                "    { arp::core::pmr::Vector<Value> operands { &arp::core::defaultCpuMemoryResource() }; constexpr std::size_t count = ";
            source += std::to_string(operation.b);
            source +=
                "u; sp -= count; for (std::size_t index = 0; index < count; ++index) operands.push_back(stack[sp + index]); auto semantic = ScriptSemanticRuntime::makeRangeFromStack(operands, ";
            source += std::to_string(operation.b);
            source += ", context.budget.maxContainerValues);\n";
            emitFailureCheck(source, "semantic");
            source += "        stack[sp++] = std::move(semantic.value); }\n";
        } else if (opcode == TypedIrOpcode::MakeInstance) {
            const auto schema = std::find_if(module.scriptClassSchemas.begin(),
                module.scriptClassSchemas.end(),
                [&operation](const auto& candidate) {
                    return candidate.className == operation.text;
                });
            source += "    { constexpr std::size_t count = ";
            source += std::to_string(operation.b);
            source += "u;";
            if (schema == module.scriptClassSchemas.end()) {
                // Reflected classes return the engine's canonical object from
                // their registered __new__ function. Keep the operation
                // synchronous and use the same semantic helper as VM.
                source +=
                    " arp::core::pmr::Vector<Value> operands { &arp::core::defaultCpuMemoryResource() }; operands.reserve(count); for (std::size_t index = 0; index < count; ++index) operands.push_back(stack[sp - count + index]); sp -= count; auto semantic = ScriptSemanticRuntime::makeReflectedInstance(operands, ";
                source += quote(operation.text);
                source += ", static_cast<int>(count), context.budget.maxCallArguments);\n";
                emitFailureCheck(source, "semantic");
                source += "        stack[sp++] = std::move(semantic.value); }\n";
            } else {
                const bool hasConstructor = !schema->constructorFunctionId.empty();
                if (hasConstructor) {
                    source += " if (frame->childPc != ";
                    source += std::to_string(pc);
                    source += "u) { sp -= count; frame->childPc = ";
                    source += std::to_string(pc);
                    source += "u;";
                } else {
                    source += " sp -= count;";
                }
                source +=
                    " arp::core::pmr::Vector<arp::core::pmr::String> fields { &arp::core::defaultCpuMemoryResource() };";
                for (const auto& field : schema->fields) {
                    source += " fields.emplace_back(";
                    source += quote(field);
                    source += ");";
                }
                source += " auto semantic = ScriptSemanticRuntime::makeScriptInstance(";
                source += quote(schema->className);
                source += ", fields);\n";
                emitFailureCheck(source, "semantic");
                if (hasConstructor)
                    source +=
                        "        frame->pendingValue = std::move(semantic.value); } Value instance = frame->pendingValue;";
                else
                    source += "        Value instance = std::move(semantic.value);";
                if (hasConstructor) {
                    const auto constructor = std::find_if(module.functions.begin(),
                        module.functions.end(),
                        [&schema](const auto& candidate) {
                            return candidate.id == schema->constructorFunctionId;
                        });
                    const auto constructorIndex = static_cast<std::size_t>(
                        std::distance(module.functions.begin(), constructor));
                    source +=
                        " std::array<Value, count + 1u> constructorArgs {}; constructorArgs[0] = instance; for (std::size_t index = 0; index < count; ++index) constructorArgs[index + 1u] = stack[sp + index]; const Value constructorResult = ";
                    source += symbols[constructorIndex];
                    source +=
                        "(context, constructorArgs.data(), count + 1u, &frame->childResume, error); (void)constructorResult; if (error != nullptr && error->code != ScriptDiagnosticCode::None) return makeNil(); frame->childPc = 0xffffffffu; frame->pendingValue = Value::undefined();";
                }
                source += " stack[sp++] = std::move(instance); }\n";
            }
        } else if (opcode == TypedIrOpcode::RangeInit) {
            source += "    locals[";
            source += std::to_string(operation.a);
            source += "] = stack[--sp]; locals[";
            source += std::to_string(operation.b);
            source += "] = makeInt(-1);\n";
        } else if (opcode == TypedIrOpcode::RangeNext) {
            const int keySlot = static_cast<int>(
                static_cast<std::int32_t>(static_cast<std::uint64_t>(operation.i64) >> 32u));
            const int valueSlot = static_cast<int>(
                static_cast<std::int32_t>(static_cast<std::uint64_t>(operation.i64) & 0xffffffffu));
            if (operation.delegatedGeneratorRange) {
                const auto site = std::find_if(
                    delegatedSites.begin(), delegatedSites.end(), [pc](const auto& value) {
                        return value.rangePc == static_cast<int>(pc);
                    });
                const int pendingYieldPc = site == delegatedSites.end() ? -1 : site->yieldPc;
                source += "    { Value source = locals[";
                source += std::to_string(operation.a);
                source +=
                    "]; if (source.is<DirectNativeGeneratorV5>()) { auto* child = source.ptr<DirectNativeGeneratorV5>(); if (child == nullptr || child->state == nullptr || child->state->context == nullptr) { if (error != nullptr) { error->code = ScriptDiagnosticCode::AotSwitchUnsupported; error->message = \"DirectNative yield-from child owner identity is unavailable\"; } goto aot_exception_dispatch; } if (context.lifetimeOwner.lock().get() != child->state->context.get()) { GeneratorIdentityV3 childIdentity; childIdentity.moduleId = child->state->context->moduleId.c_str(); childIdentity.moduleVersion = child->state->context->moduleVersion; childIdentity.artifactIdentity = child->state->context->artifactIdentity.c_str(); if (!context.acceptsGeneratorIdentity(childIdentity)) { if (error != nullptr) { error->code = ScriptDiagnosticCode::AotSwitchUnsupported; error->message = \"DirectNative yield-from child artifact identity negotiation failed\"; } goto aot_exception_dispatch; } } if (isNil(frame->delegatedChild)) { frame->delegatedChild = source; frame->childPc = ";
                source += std::to_string(pc);
                source +=
                    "u; } VmError childError; Value yielded; if (resume.injectionKind == DirectNativeInjectionKindV5::Send) { yielded = sendDirectNativeGeneratorV5(*child, resume.injectionValue, &childError); } else if (resume.injectionKind == DirectNativeInjectionKindV5::Throw) { yielded = throwDirectNativeGeneratorV5(*child, resume.injectionValue, &childError); } else { yielded = resumeDirectNativeGeneratorV5(*child, &childError); } resume.injectionKind = DirectNativeInjectionKindV5::None; resume.injectionValue = makeNil(); if (childError.code == ScriptDiagnosticCode::Suspended) { int delegatedIndex = 0; if (tryGetInt(locals[";
                source += std::to_string(operation.b);
                source += "], delegatedIndex)) locals[";
                source += std::to_string(operation.b);
                source +=
                    "] = makeInt(delegatedIndex - 1); if (frame != &localFrame) frame->captureCanonicalDelegation(";
                source += std::to_string(operation.a);
                source += ", ";
                source += std::to_string(pc);
                source += ", ";
                source += std::to_string(pendingYieldPc);
                source += ", true, ";
                source += std::to_string(pendingYieldPc);
                source += "); resume.pc = ";
                source += std::to_string(pc);
                source +=
                    "u; resume.awaitInjectionSupported = child->state->resume.awaitInjectionSupported; resume.suspendKind = DirectNativeSuspendKindV5::Await; if (!synchronizeFrameRoots()) return makeNil(); if (error != nullptr) { *error = std::move(childError); } return makeNil(); } if (childError.code != ScriptDiagnosticCode::None) { if (error != nullptr) *error = std::move(childError); goto aot_exception_dispatch; } resume.awaitInjectionSupported = false; if (child->state->done) { frame->delegatedChild = makeNil(); frame->childPc = 0xffffffffu; stack[sp++] = makeBool(false); } else { locals[";
                source += std::to_string(valueSlot);
                source +=
                    "] = std::move(yielded); stack[sp++] = makeBool(true); } } else if (const auto* bridge = optionalGeneratorBridgeV3(); bridge != nullptr && bridge->isGenerator != nullptr && bridge->isGenerator(source)) { GeneratorIdentityV3 childIdentity; if (bridge->queryIdentity == nullptr || !bridge->queryIdentity(source, childIdentity) || !context.acceptsGeneratorIdentity(childIdentity)) { if (error != nullptr) { error->code = ScriptDiagnosticCode::AotSwitchUnsupported; error->message = \"DirectNative yield-from child artifact identity negotiation failed\"; } goto aot_exception_dispatch; } if (isNil(frame->delegatedChild)) { frame->delegatedChild = source; frame->childPc = ";
                source += std::to_string(pc);
                source +=
                    "u; } VmError childError; Value yielded; if (resume.injectionKind == DirectNativeInjectionKindV5::Send) { yielded = bridge->resumeSend(source, resume.injectionValue, &childError); } else if (resume.injectionKind == DirectNativeInjectionKindV5::Throw) { yielded = bridge->resumeThrow(source, resume.injectionValue, &childError); } else { yielded = bridge->resume(source, &childError); } resume.injectionKind = DirectNativeInjectionKindV5::None; resume.injectionValue = makeNil(); if (childError.code == ScriptDiagnosticCode::Suspended) { int delegatedIndex = 0; if (tryGetInt(locals[";
                source += std::to_string(operation.b);
                source += "], delegatedIndex)) locals[";
                source += std::to_string(operation.b);
                source +=
                    "] = makeInt(delegatedIndex - 1); if (frame != &localFrame) frame->captureCanonicalDelegation(";
                source += std::to_string(operation.a);
                source += ", ";
                source += std::to_string(pc);
                source += ", ";
                source += std::to_string(pendingYieldPc);
                source += ", true, ";
                source += std::to_string(pendingYieldPc);
                source += "); resume.pc = ";
                source += std::to_string(pc);
                source +=
                    "u; { const auto* capability = optionalGeneratorBridgeV3Capability(); resume.awaitInjectionSupported = capability != nullptr && capability->isGenerator != nullptr && capability->isGenerator(source) && capability->supportsPendingAwaitInjection != nullptr && capability->supportsPendingAwaitInjection(source); } resume.suspendKind = DirectNativeSuspendKindV5::Await; if (!synchronizeFrameRoots()) return makeNil(); if (error != nullptr) *error = std::move(childError); return makeNil(); } if (childError.code != ScriptDiagnosticCode::None) { if (error != nullptr) *error = std::move(childError); goto aot_exception_dispatch; } resume.awaitInjectionSupported = false; if (bridge->isDone != nullptr && bridge->isDone(source)) { frame->delegatedChild = makeNil(); frame->childPc = 0xffffffffu; stack[sp++] = makeBool(false); } else { locals[";
                source += std::to_string(valueSlot);
                source +=
                    "] = std::move(yielded); stack[sp++] = makeBool(true); } } else { if (resume.injectionKind != DirectNativeInjectionKindV5::None) { if (error != nullptr) { error->code = ScriptDiagnosticCode::AotSwitchUnsupported; error->message = \"DirectNative yield-from injection requires a DirectNative child\"; } resume.injectionKind = DirectNativeInjectionKindV5::None; resume.injectionValue = makeNil(); goto aot_exception_dispatch; } int index = 0; if (!tryGetInt(locals[";
                source += std::to_string(operation.b);
                source +=
                    "], index)) { if (error != nullptr) { error->code = ScriptDiagnosticCode::BadCast; error->message = \"range index corrupted\"; } goto aot_exception_dispatch; } ++index; locals[";
                source += std::to_string(operation.b);
                source +=
                    "] = makeInt(index); auto semantic = ScriptSemanticRuntime::rangeNextCollection(source, index);\n";
                emitFailureCheck(source, "semantic");
                if (keySlot >= 0) {
                    source += "        if (semantic.hasMore) locals[";
                    source += std::to_string(keySlot);
                    source += "] = semantic.key;\n";
                }
                if (valueSlot >= 0) {
                    source += "        if (semantic.hasMore) locals[";
                    source += std::to_string(valueSlot);
                    source += "] = semantic.value;\n";
                }
                source += "        stack[sp++] = makeBool(semantic.hasMore); } }\n";
            } else {
                source += "    { int index = 0; if (!tryGetInt(locals[";
                source += std::to_string(operation.b);
                source +=
                    "], index)) { if (error != nullptr) { error->code = ScriptDiagnosticCode::BadCast; error->message = \"range index corrupted\"; } return makeNil(); } ++index; locals[";
                source += std::to_string(operation.b);
                source +=
                    "] = makeInt(index); auto semantic = ScriptSemanticRuntime::rangeNextCollection(locals[";
                source += std::to_string(operation.a);
                source += "], index);\n";
                emitFailureCheck(source, "semantic");
                if (keySlot >= 0) {
                    source += "        if (semantic.hasMore) locals[";
                    source += std::to_string(keySlot);
                    source += "] = semantic.key;\n";
                }
                if (valueSlot >= 0) {
                    source += "        if (semantic.hasMore) locals[";
                    source += std::to_string(valueSlot);
                    source += "] = semantic.value;\n";
                }
                source += "        stack[sp++] = makeBool(semantic.hasMore); }\n";
            }
        } else if (opcode == TypedIrOpcode::BuiltinAppend) {
            source +=
                "    { const Value value = stack[--sp]; const Value list = stack[--sp]; context.prepareSideEffect(resume, ";
            source += std::to_string(pc);
            source +=
                "u); auto semantic = ScriptSemanticRuntime::append(list, value, context.budget.maxContainerValues);\n";
            emitFailureCheck(source, "semantic");
            source += "        context.commitSideEffect(resume, ";
            source += std::to_string(pc);
            source += "u); stack[sp++] = std::move(semantic.value); }\n";
        } else if (opcode == TypedIrOpcode::BuiltinDelete) {
            source +=
                "    { const Value key = stack[--sp]; const Value dictionary = stack[--sp]; context.prepareSideEffect(resume, ";
            source += std::to_string(pc);
            source += "u); auto semantic = ScriptSemanticRuntime::erase(dictionary, key);\n";
            emitFailureCheck(source, "semantic");
            source += "        context.commitSideEffect(resume, ";
            source += std::to_string(pc);
            source += "u); }\n";
        } else if (opcode == TypedIrOpcode::BuiltinClear) {
            source +=
                "    { const Value container = stack[--sp]; context.prepareSideEffect(resume, ";
            source += std::to_string(pc);
            source += "u); auto semantic = ScriptSemanticRuntime::clear(container);\n";
            emitFailureCheck(source, "semantic");
            source += "        context.commitSideEffect(resume, ";
            source += std::to_string(pc);
            source += "u); }\n";
        } else if (opcode == TypedIrOpcode::BuiltinLen) {
            source += "    { auto semantic = ScriptSemanticRuntime::length(stack[--sp]);\n";
            emitFailureCheck(source, "semantic");
            source += "        stack[sp++] = std::move(semantic.value); }\n";
        } else if (opcode == TypedIrOpcode::Pop) {
            source += "    --sp;\n";
        } else if (!binaryOperation(opcode).empty()) {
            source += "    { const Value rhs = stack[--sp]; const Value lhs = stack[--sp];";
            if (opcode == TypedIrOpcode::Add || opcode == TypedIrOpcode::IntAdd
                || opcode == TypedIrOpcode::Sub || opcode == TypedIrOpcode::IntSub
                || opcode == TypedIrOpcode::Mul || opcode == TypedIrOpcode::IntMul) {
                source +=
                    " if (lhs.is<int>() && rhs.is<int>()) { const std::int64_t left = *lhs.ptr<int>(); const std::int64_t right = *rhs.ptr<int>(); const std::int64_t result = ";
                if (opcode == TypedIrOpcode::Add || opcode == TypedIrOpcode::IntAdd)
                    source += "left + right";
                else if (opcode == TypedIrOpcode::Sub || opcode == TypedIrOpcode::IntSub)
                    source += "left - right";
                else
                    source += "left * right";
                source +=
                    "; if (result < std::numeric_limits<int>::min() || result > std::numeric_limits<int>::max()) { if (error != nullptr) { error->code = ScriptDiagnosticCode::IntegerOverflow; error->message = \"int arithmetic overflow\"; } goto aot_exception_dispatch; } stack[sp++] = makeInt(static_cast<int>(result)); } else {";
            } else {
                source += " {";
            }
            source += " auto semantic = ScriptSemanticRuntime::binary(SemanticBinaryOperation::";
            source += binaryOperation(opcode);
            source += ", lhs, rhs);\n";
            emitFailureCheck(source, "semantic");
            source += "        stack[sp++] = std::move(semantic.value); } }\n";
        } else if (!compareOperation(opcode).empty()) {
            source +=
                "    { const Value rhs = stack[--sp]; const Value lhs = stack[--sp]; if (lhs.is<int>() && rhs.is<int>()) { const int left = *lhs.ptr<int>(); const int right = *rhs.ptr<int>(); stack[sp++] = makeBool(";
            if (opcode == TypedIrOpcode::CmpEq || opcode == TypedIrOpcode::IntCmpEq)
                source += "left == right";
            else if (opcode == TypedIrOpcode::CmpNe || opcode == TypedIrOpcode::IntCmpNe)
                source += "left != right";
            else if (opcode == TypedIrOpcode::CmpLt || opcode == TypedIrOpcode::IntCmpLt)
                source += "left < right";
            else if (opcode == TypedIrOpcode::CmpLe || opcode == TypedIrOpcode::IntCmpLe)
                source += "left <= right";
            else if (opcode == TypedIrOpcode::CmpGt || opcode == TypedIrOpcode::IntCmpGt)
                source += "left > right";
            else
                source += "left >= right";
            source +=
                "); } else { auto semantic = ScriptSemanticRuntime::compare(SemanticCompareOperation::";
            source += compareOperation(opcode);
            source += ", lhs, rhs);\n";
            emitFailureCheck(source, "semantic");
            source += "        stack[sp++] = std::move(semantic.value); } }\n";
        } else if (opcode == TypedIrOpcode::Neg || opcode == TypedIrOpcode::Not) {
            source += "    { auto semantic = ScriptSemanticRuntime::";
            source += opcode == TypedIrOpcode::Neg ? "negate" : "logicalNot";
            source += "(stack[--sp]);\n";
            emitFailureCheck(source, "semantic");
            source += "        stack[sp++] = std::move(semantic.value); }\n";
        } else if (!conversionOperation(opcode).empty()) {
            source +=
                "    { auto semantic = ScriptSemanticRuntime::convert(SemanticConversionOperation::";
            source += conversionOperation(opcode);
            source += ", stack[--sp]);\n";
            emitFailureCheck(source, "semantic");
            source += "        stack[sp++] = std::move(semantic.value); }\n";
        } else if (opcode == TypedIrOpcode::Jump) {
            source += "    goto aot_pc_";
            source += std::to_string(operation.a);
            source += ";\n";
        } else if (opcode == TypedIrOpcode::JumpIfFalse || opcode == TypedIrOpcode::JumpIfTrue) {
            source +=
                "    { const Value conditionValue = stack[--sp]; bool condition = false; if (conditionValue.is<bool>()) condition = *conditionValue.ptr<bool>(); else { auto semantic = ScriptSemanticRuntime::convert(SemanticConversionOperation::Bool, conditionValue);\n";
            emitFailureCheck(source, "semantic");
            source +=
                "        if (!tryGetBool(semantic.value, condition)) return makeNil(); } if (";
            source += opcode == TypedIrOpcode::JumpIfFalse ? "!condition" : "condition";
            source += ") goto aot_pc_";
            source += std::to_string(operation.a);
            source += "; }\n";
        } else if (opcode == TypedIrOpcode::JumpIfLocalIntLtConstFalse) {
            source += "    { int value = 0; if (!tryGetInt(locals[";
            source += std::to_string(operation.b);
            source += "], value)) return makeNil(); if (!(value < ";
            source += std::to_string(operation.i64);
            source += ")) goto aot_pc_";
            source += std::to_string(operation.a);
            source += "; }\n";
        } else if (opcode == TypedIrOpcode::PushHandler) {
            source += "    { ScriptDiagnosticCode filter = ScriptDiagnosticCode::None;";
            if (!operation.text.empty()) {
                source += " if (!scriptDiagnosticCodeFromName(";
                source += quote(operation.text);
                source +=
                    ", filter)) { if (error != nullptr) { error->code = ScriptDiagnosticCode::AotArtifactInvalid; error->message = \"DirectNative handler filter is invalid\"; } return makeNil(); }";
            }
            source += " frame->handlers.push_back({ ";
            source += std::to_string(operation.a);
            source += "u, sp, filter }); }\n";
        } else if (opcode == TypedIrOpcode::PopHandler) {
            source +=
                "    if (frame->handlers.empty()) { if (error != nullptr) { error->code = ScriptDiagnosticCode::AotArtifactInvalid; error->message = \"DirectNative handler stack underflow\"; } return makeNil(); } frame->handlers.pop_back();\n";
        } else if (opcode == TypedIrOpcode::LocalAddIntConst
            || opcode == TypedIrOpcode::LocalSubIntConst
            || opcode == TypedIrOpcode::LocalAddInt64Const
            || opcode == TypedIrOpcode::LocalSubInt64Const) {
            source +=
                "    { auto semantic = ScriptSemanticRuntime::binary(SemanticBinaryOperation::";
            source += (opcode == TypedIrOpcode::LocalAddIntConst
                          || opcode == TypedIrOpcode::LocalAddInt64Const)
                ? "Add"
                : "Sub";
            source += ", locals[";
            source += std::to_string(operation.a);
            source += "], ";
            source += (opcode == TypedIrOpcode::LocalAddInt64Const
                          || opcode == TypedIrOpcode::LocalSubInt64Const)
                ? "makeInt64("
                : "makeInt(";
            source += std::to_string(operation.i64);
            source += "));\n";
            emitFailureCheck(source, "semantic");
            source += "        locals[";
            source += std::to_string(operation.a);
            source += "] = std::move(semantic.value); }\n";
        } else if (opcode == TypedIrOpcode::LocalAddLocal) {
            source +=
                "    { auto semantic = ScriptSemanticRuntime::binary(SemanticBinaryOperation::Add, locals[";
            source += std::to_string(operation.a);
            source += "], locals[";
            source += std::to_string(operation.b);
            source += "]);\n";
            emitFailureCheck(source, "semantic");
            source += "        locals[";
            source += std::to_string(operation.a);
            source += "] = std::move(semantic.value); }\n";
        } else if (opcode == TypedIrOpcode::LocalAddFloatConst
            || opcode == TypedIrOpcode::LocalSubFloatConst) {
            source += "    { const std::uint64_t bits = ";
            source += std::to_string(static_cast<std::uint64_t>(operation.i64));
            source +=
                "ULL; double value = 0.0; std::memcpy(&value, &bits, sizeof(value)); auto semantic = ScriptSemanticRuntime::binary(SemanticBinaryOperation::";
            source += opcode == TypedIrOpcode::LocalAddFloatConst ? "Add" : "Sub";
            source += ", locals[";
            source += std::to_string(operation.a);
            source += "], makeFloat(value));\n";
            emitFailureCheck(source, "semantic");
            source += "        locals[";
            source += std::to_string(operation.a);
            source += "] = std::move(semantic.value); }\n";
        } else if (opcode == TypedIrOpcode::Call) {
            const auto targetIndex =
                static_cast<std::size_t>(resolveFunctionIndex(module, operation.targetId));
            source += "    { constexpr std::size_t count = ";
            source += std::to_string(operation.b);
            if (module.functions[targetIndex].isGenerator) {
                source += "u; sp -= count; const Value result = ";
                source += symbols[targetIndex];
                source +=
                    "(context, stack.data() + sp, count, nullptr, error); if (error != nullptr && error->code != ScriptDiagnosticCode::None) goto aot_exception_dispatch; stack[sp++] = result; }\n";
            } else {
                source += "u; if (frame->childPc != ";
                source += std::to_string(pc);
                source += "u) { sp -= count; frame->childPc = ";
                source += std::to_string(pc);
                source += "u; } const Value result = ";
                source += symbols[targetIndex];
                source +=
                    "(context, stack.data() + sp, count, &frame->childResume, error); if (error != nullptr && error->code != ScriptDiagnosticCode::None) { if (error->code == ScriptDiagnosticCode::Suspended) { if (!synchronizeFrameRoots()) return makeNil(); return result; } goto aot_exception_dispatch; } frame->childPc = 0xffffffffu; stack[sp++] = result; }\n";
            }
        } else if (opcode == TypedIrOpcode::MakeFuncValue) {
            const int targetIndex = resolveFunctionIndex(module, operation.targetId);
            source +=
                "    { arp::core::pmr::Vector<std::shared_ptr<ScriptCell>> captures { &arp::core::defaultCpuMemoryResource() }; ScriptClosure closure { ";
            source += std::to_string(targetIndex);
            source += ", std::move(captures) }; closure.functionId = ";
            source += quote(operation.targetId);
            source += "; stack[sp++] = Value { std::move(closure) }; }\n";
        } else if (opcode == TypedIrOpcode::MakeClosure) {
            const int targetIndex = resolveFunctionIndex(module, operation.targetId);
            source += "    { constexpr std::size_t count = ";
            source += std::to_string(operation.b);
            source +=
                "u; sp -= count; arp::core::pmr::Vector<std::shared_ptr<ScriptCell>> captures { &arp::core::defaultCpuMemoryResource() }; captures.reserve(count); for (std::size_t index = 0; index < count; ++index) { const auto* pointer = stack[sp + index].ptr<ScriptPointer>(); if (pointer == nullptr || pointer->kind != ScriptPointerKind::Cell || pointer->cell == nullptr) { if (error != nullptr) { error->code = ScriptDiagnosticCode::AotArtifactInvalid; error->message = \"DirectNative closure capture is not a canonical cell\"; } return makeNil(); } captures.push_back(pointer->cell); } ScriptClosure closure { ";
            source += std::to_string(targetIndex);
            source += ", std::move(captures) }; closure.functionId = ";
            source += quote(operation.targetId);
            source += "; stack[sp++] = Value { std::move(closure) }; }\n";
        } else if (opcode == TypedIrOpcode::MakeMethodValue) {
            source += "    /* receiver remains in the operand slot */\n";
        } else if (opcode == TypedIrOpcode::CallValue
            && operation.closureCaptureCount == kScriptMethodProvenanceCaptureCount
            && !operation.text.empty()) {
            struct DynamicMethodCandidate {
                const TypedIrScriptClassSchema* schema = nullptr;
                const TypedIrFunction* function = nullptr;
                std::size_t functionIndex = 0u;
            };

            core::pmr::Vector<DynamicMethodCandidate> candidates{&core::defaultCpuMemoryResource()};
            for (const auto& schema : module.scriptClassSchemas) {
                const auto method = schema.methods.find(operation.text);
                if (method == schema.methods.end())
                    continue;
                const auto target = std::find_if(module.functions.begin(),
                    module.functions.end(),
                    [&method](const auto& candidate) {
                        return candidate.id == method->second;
                    });
                if (target == module.functions.end() || target->parameterCount != operation.b + 1)
                    continue;
                candidates.push_back(DynamicMethodCandidate{&schema,
                    &*target,
                    static_cast<std::size_t>(std::distance(module.functions.begin(), target))});
            }
            source += "    { constexpr std::size_t count = ";
            source += std::to_string(operation.b);
            source += "u; if (frame->childPc != ";
            source += std::to_string(pc);
            source += "u) { sp -= count + 1u; frame->childPc = ";
            source += std::to_string(pc);
            source +=
                "u; } const std::size_t base = sp; const Value& receiver = stack[base + count]; if (!receiver.is<arp::reflection::instance>() || !receiver.ptr<arp::reflection::instance>()->alive() || receiver.ptr<arp::reflection::instance>()->class_() == nullptr) { if (error != nullptr) { error->code = ScriptDiagnosticCode::AotArtifactInvalid; error->message = \"DirectNative dynamic method receiver is invalid\"; } return makeNil(); } const auto& actualClass = receiver.ptr<arp::reflection::instance>()->class_()->name(); std::array<Value, count + 1u> callArgs {}; callArgs[0] = receiver; for (std::size_t index = 0; index < count; ++index) callArgs[index + 1u] = stack[base + index]; Value result = makeNil();";
            for (std::size_t index = 0; index < candidates.size(); ++index) {
                source += index == 0u ? " if (actualClass == " : " else if (actualClass == ";
                source += quote(candidates[index].schema->className);
                source += ") result = ";
                source += symbols[candidates[index].functionIndex];
                source += "(context, callArgs.data(), count + 1u, ";
                source +=
                    candidates[index].function->isGenerator ? "nullptr" : "&frame->childResume";
                source += ", error);";
            }
            source +=
                " else { if (error != nullptr) { error->code = ScriptDiagnosticCode::AotArtifactInvalid; error->message = \"DirectNative dynamic method class is not in the static dispatch set\"; } return makeNil(); } if (error != nullptr && error->code != ScriptDiagnosticCode::None) { if (error->code == ScriptDiagnosticCode::Suspended) { if (!synchronizeFrameRoots()) return makeNil(); return result; } goto aot_exception_dispatch; } frame->childPc = 0xffffffffu; stack[sp++] = result; }\n";
        } else if (opcode == TypedIrOpcode::CallValue
            && !operation.closureTargetFunctionId.empty()) {
            const auto target = std::find_if(module.functions.begin(),
                module.functions.end(),
                [&operation](const auto& candidate) {
                    return candidate.id == operation.closureTargetFunctionId;
                });
            const auto targetIndex =
                static_cast<std::size_t>(std::distance(module.functions.begin(), target));
            if (operation.closureCaptureCount == kScriptMethodProvenanceCaptureCount) {
                source += "    { constexpr std::size_t count = ";
                source += std::to_string(operation.b);
                source += "u; if (frame->childPc != ";
                source += std::to_string(pc);
                source += "u) { sp -= count + 1u; frame->childPc = ";
                source += std::to_string(pc);
                source +=
                    "u; } const std::size_t base = sp; const Value& receiver = stack[base + count]; if (!receiver.is<arp::reflection::instance>() || !receiver.ptr<arp::reflection::instance>()->alive()) { if (error != nullptr) { error->code = ScriptDiagnosticCode::AotArtifactInvalid; error->message = \"DirectNative method receiver is invalid\"; } return makeNil(); } std::array<Value, count + 1u> callArgs {}; callArgs[0] = receiver; for (std::size_t index = 0; index < count; ++index) callArgs[index + 1u] = stack[base + index]; const Value result = ";
                source += symbols[targetIndex];
                source += "(context, callArgs.data(), count + 1u, ";
                source += target->isGenerator ? "nullptr" : "&frame->childResume";
                source +=
                    ", error); if (error != nullptr && error->code != ScriptDiagnosticCode::None) { if (error->code == ScriptDiagnosticCode::Suspended) { if (!synchronizeFrameRoots()) return makeNil(); return result; } goto aot_exception_dispatch; } frame->childPc = 0xffffffffu; stack[sp++] = result; }\n";
            } else {
                source += "    { constexpr std::size_t count = ";
                source += std::to_string(operation.b);
                source += "u; if (frame->childPc != ";
                source += std::to_string(pc);
                source += "u) { sp -= count + 1u; frame->childPc = ";
                source += std::to_string(pc);
                source +=
                    "u; } const auto* closure = stack[sp + count].ptr<ScriptClosure>(); if (closure == nullptr || closure->captures.size() != ";
                source += std::to_string(operation.closureCaptureCount);
                source +=
                    "u) { if (error != nullptr) { error->code = ScriptDiagnosticCode::AotArtifactInvalid; error->message = \"DirectNative closure value does not match proven target\"; } return makeNil(); } struct CaptureScope { DirectNativeExecutionContext& context; const arp::core::pmr::Vector<std::shared_ptr<ScriptCell>>* previous; ~CaptureScope() { context.activeCaptures = previous; } } captureScope { context, context.activeCaptures }; context.activeCaptures = &closure->captures; const Value result = ";
                source += symbols[targetIndex];
                source += "(context, stack.data() + sp, count, ";
                source += target->isGenerator ? "nullptr" : "&frame->childResume";
                source +=
                    ", error); if (error != nullptr && error->code != ScriptDiagnosticCode::None) { if (error->code == ScriptDiagnosticCode::Suspended) { if (!synchronizeFrameRoots()) return makeNil(); return result; } goto aot_exception_dispatch; } frame->childPc = 0xffffffffu; stack[sp++] = result; }\n";
            }
        } else if (opcode == TypedIrOpcode::CallValue) {
            const auto binding = std::find_if(module.callableBindings.begin(),
                module.callableBindings.end(),
                [&operation](const auto& candidate) {
                    return candidate.bindingId == operation.callableBindingId;
                });
            source += "    { constexpr std::size_t count = ";
            source += std::to_string(operation.b);
            source +=
                "u; const Value callableBase = stack[--sp]; sp -= count; Value result = makeNil(); context.prepareSideEffect(resume, ";
            source += std::to_string(pc);
            source += "u); if (!context.invokeBoundCall(";
            source += quote(binding->bindingId);
            source += ", ";
            source += quote(binding->targetId);
            source += binding->origin == TypedIrCallableOrigin::ReflectionMember ? ", &callableBase"
                                                                                 : ", nullptr";
            source +=
                ", stack.data() + sp, count, result, error)) goto aot_exception_dispatch; context.commitSideEffect(resume, ";
            source += std::to_string(pc);
            source += "u); stack[sp++] = std::move(result); }\n";
        } else if (opcode == TypedIrOpcode::Await) {
            source += "    { if (resume.awaitInjectionReady) { if (resume.pc != ";
            source += std::to_string(pc);
            source +=
                "u) { if (error != nullptr) { error->code = ScriptDiagnosticCode::AotArtifactInvalid; error->message = \"DirectNative await injection pc mismatch\"; } return makeNil(); } resume.awaitInjectionReady = false; resume.suspendKind = DirectNativeSuspendKindV5::None; frame->childPc = 0xffffffffu; frame->pendingValue = makeNil(); stack[sp++] = std::move(resume.injectionValue); resume.injectionValue = makeNil(); } else { if (frame->childPc != ";
            source += std::to_string(pc);
            source += "u) { frame->pendingValue = stack[--sp]; frame->childPc = ";
            source += std::to_string(pc);
            source +=
                "u; } if (!isScriptAwaitable(frame->pendingValue)) { if (error != nullptr) { error->code = ScriptDiagnosticCode::HostError; error->message = \"await expects an awaitable value\"; } goto aot_exception_dispatch; } Value awaited; arp::core::pmr::String diagnostics { &arp::core::defaultCpuMemoryResource() }; const auto status = asScriptAwaitable(frame->pendingValue)->pollResult(awaited, diagnostics); if (status == ScriptAwaitStatus::Pending) { if (frame != &localFrame) frame->markCanonicalAwait(frame->pendingValue); resume.pc = ";
            source += std::to_string(pc);
            source +=
                "u; resume.awaitInjectionSupported = asScriptAwaitable(frame->pendingValue)->supportsInjection(); resume.suspendKind = DirectNativeSuspendKindV5::Await; if (!synchronizeFrameRoots()) return makeNil(); if (error != nullptr) { error->code = ScriptDiagnosticCode::Suspended; error->message = \"generator await remains pending\"; } return makeNil(); } if (status == ScriptAwaitStatus::Failed) { if (error != nullptr) { error->code = ScriptDiagnosticCode::HostError; error->message = diagnostics.empty() ? arp::core::pmr::String(\"generator await failed\", &arp::core::defaultCpuMemoryResource()) : std::move(diagnostics); } goto aot_exception_dispatch; } resume.awaitInjectionSupported = false; frame->childPc = 0xffffffffu; frame->pendingValue = makeNil(); stack[sp++] = std::move(awaited); } }\n";
        } else if (opcode == TypedIrOpcode::Yield) {
            source += "    { ";
            source += "Value yielded = stack[--sp]; ";
            if (hasDelegatedSites) {
                source += "if (frame != &localFrame) { frame->captureCanonicalState(";
                source += std::to_string(pc + 1u);
                source += "u, locals.data(), locals.size(), stack.data(), sp, ";
                if (hasCaptureState)
                    source += "frame->captures.data(), frame->captures.size(), ";
                else
                    source += "nullptr, 0u, ";
                source += "nullptr, nullptr, 0u, false, yielded); ";
                if (hasHandlerState)
                    source += "frame->captureCanonicalHandlers(frame->handlers); ";
                source +=
                    "if (!isNil(frame->delegatedChild)) { bool canonicalDelegationCaptured = false; switch (frame->childPc) {";
                for (const auto& site : delegatedSites) {
                    source += " case ";
                    source += std::to_string(site.rangePc);
                    source += "u: frame->captureCanonicalDelegation(";
                    source += std::to_string(site.sourceSlot);
                    source += ", ";
                    source += std::to_string(site.rangePc);
                    source += ", ";
                    source += std::to_string(site.yieldPc);
                    source += ", false, -1); canonicalDelegationCaptured = true; break;";
                }
                source +=
                    " default: break; } if (!canonicalDelegationCaptured) { if (error != nullptr) { error->code = ScriptDiagnosticCode::AotArtifactInvalid; error->message = \"DirectNative delegated child has no canonical site\"; } return makeNil(); } } } ";
            }
            source += "resume.pc = ";
            source += std::to_string(pc + 1u);
            source +=
                "u; resume.suspendKind = DirectNativeSuspendKindV5::Yield; if (!synchronizeFrameRoots()) return makeNil(); if (error != nullptr) { error->code = ScriptDiagnosticCode::Suspended; error->message = \"yielded\"; } return yielded; }\n";
        } else if (opcode == TypedIrOpcode::Throw) {
            source +=
                "    { const Value raised = stack[--sp]; if (error != nullptr) { error->code = ScriptDiagnosticCode::UserRaise; if (!tryGetString(raised, error->message)) error->message = \"script raised a non-string value\"; } goto aot_exception_dispatch; }\n";
        } else if (opcode == TypedIrOpcode::Return || opcode == TypedIrOpcode::ReturnInt) {
            source += "    { Value result = ";
            source += (opcode == TypedIrOpcode::ReturnInt || operation.a == 1) ? "stack[--sp]"
                                                                               : "makeNil()";
            source +=
                "; resume.pc = 0u; resume.commitState = 0u; resume.frame.reset(); return result; }\n";
        }
    }
    source +=
        "    if (error != nullptr) { error->code = ScriptDiagnosticCode::AotArtifactInvalid; error->message = \"DirectNative control flow fell through\"; } goto aot_terminal_cleanup;\n";
    source += "aot_exception_dispatch:\n";
    if (hasHandlerState) {
        source +=
            "    while (!frame->handlers.empty() && error != nullptr && error->code != ScriptDiagnosticCode::None) { const auto handler = frame->handlers.back(); frame->handlers.pop_back(); if (handler.filter != ScriptDiagnosticCode::None && handler.filter != error->code) continue; sp = handler.stackDepth; stack[sp++] = makeString(error->message); error->traceback.clear(); error->code = ScriptDiagnosticCode::None; error->message.clear(); switch (handler.pc) {";
        for (std::size_t pc = 0; pc < operations.size(); ++pc) {
            source += " case ";
            source += std::to_string(pc);
            source += "u: goto aot_pc_";
            source += std::to_string(pc);
            source += ";";
        }
        source +=
            " default: if (error != nullptr) { error->code = ScriptDiagnosticCode::AotArtifactInvalid; error->message = \"DirectNative exception handler pc is outside the function\"; } break; } }\n";
    }
    source += "aot_terminal_cleanup:\n";
    source +=
        "    resume.frame.reset(); resume.pc = 0u; resume.commitState = 0u; resume.injectionKind = DirectNativeInjectionKindV5::None; resume.injectionValue = makeNil(); resume.awaitInjectionSupported = false; resume.awaitInjectionReady = false; return makeNil();\n}\n";
}

String platformYaml(AotPlatform platforms)
{
    String result("[", &core::defaultCpuMemoryResource());
    bool separator = false;
    if (hasAotPlatform(platforms, AotPlatform::Windows)) {
        result += "windows";
        separator = true;
    }
    if (hasAotPlatform(platforms, AotPlatform::WebWasm)) {
        if (separator)
            result += ", ";
        result += "web-wasm";
        separator = true;
    }
    if (hasAotPlatform(platforms, AotPlatform::MacOS)) {
        if (separator)
            result += ", ";
        result += "macos";
    }
    result += "]";
    return result;
}

} // namespace

ScriptResult verifyDirectNativeControlFlowV5(const TypedIrModule& module)
{
    const auto validation = validateTypedIr(module);
    if (!validation.ok())
        return validation;
    for (const auto& function : module.functions) {
        if (function.signature.variadic)
            return unsupported(function, nullptr, "variadic ABI is not proven");
        if (!function.safepoints.empty() && !function.isGenerator)
            return unsupported(function, nullptr, "await requires a generated generator frame");
        const auto proof = analyzeTypedIrScalarControlFlow(function);
        if (proof.status != TypedIrScalarStackStatus::Valid) {
            core::pmr::String reason(
                "stack or definite-assignment proof failed", &core::defaultCpuMemoryResource());
            if (proof.status == TypedIrScalarStackStatus::Invalid) {
                reason += " at pc ";
                reason += std::to_string(proof.failureOperationIndex);
                reason += " (";
                reason += proof.failureOperation;
                reason += "): ";
                reason += proof.failureReason;
            }
            return unsupported(function, nullptr, reason.c_str());
        }
        for (const auto& block : function.blocks) {
            for (const auto& operation : block.operations) {
                if (!supportedOperation(operation.opcode))
                    return unsupported(
                        function, &operation, "opcode has no generated v5 control flow");
                if (operation.opcode == TypedIrOpcode::Call
                    && (resolveFunctionIndex(module, operation.targetId) < 0
                        || module
                                .functions[static_cast<std::size_t>(
                                    resolveFunctionIndex(module, operation.targetId))]
                                .captureCount
                            != 0)) {
                    return unsupported(
                        function, &operation, "direct call target is not statically proven");
                }
                if (operation.opcode == TypedIrOpcode::CallValue) {
                    const auto binding = std::find_if(module.callableBindings.begin(),
                        module.callableBindings.end(),
                        [&operation](const auto& candidate) {
                            return candidate.bindingId == operation.callableBindingId;
                        });
                    const auto target = std::find_if(module.functions.begin(),
                        module.functions.end(),
                        [&operation](const auto& candidate) {
                            return candidate.id == operation.closureTargetFunctionId;
                        });
                    const bool directFunction = target != module.functions.end()
                        && ((operation.closureCaptureCount >= 0
                                && target->captureCount == operation.closureCaptureCount
                                && target->parameterCount == operation.b)
                            || (operation.closureCaptureCount == kScriptMethodProvenanceCaptureCount
                                && target->parameterCount == operation.b + 1));
                    const bool boundOrigin = binding != module.callableBindings.end()
                        && (binding->origin == TypedIrCallableOrigin::HostCapability
                            || binding->origin == TypedIrCallableOrigin::ReflectionMember
                            || binding->origin == TypedIrCallableOrigin::ScriptFunction);
                    // A generator bridge is an explicit ScriptFunction
                    // contract for a value-producing call: it returns a
                    // generator object synchronously, while
                    // resume/send/throw/close are handled by the yield-from
                    // shape below. A plain may-suspend bound call still has
                    // no v5 lowering.
                    const bool boundFunction = !operation.callableBindingId.empty() && boundOrigin
                        && binding->fixedArity == operation.b
                        && ((!binding->maySuspend)
                            || (binding->generatorBridge
                                && binding->origin == TypedIrCallableOrigin::ScriptFunction));
                    if (!directFunction && !boundFunction) {
                        return unsupported(function,
                            &operation,
                            "indirect call has no synchronous bound-call contract");
                    }
                }
                if (operation.opcode == TypedIrOpcode::MakeClosure
                    && (resolveFunctionIndex(module, operation.targetId) < 0 || operation.b < 0
                        || module
                                .functions[static_cast<std::size_t>(
                                    resolveFunctionIndex(module, operation.targetId))]
                                .captureCount
                            != operation.b)) {
                    return unsupported(function,
                        &operation,
                        "closure target/capture identity is not statically proven");
                }
                if (operation.opcode == TypedIrOpcode::MakeInstance) {
                    const auto schema = std::find_if(module.scriptClassSchemas.begin(),
                        module.scriptClassSchemas.end(),
                        [&operation](const auto& candidate) {
                            return candidate.className == operation.text;
                        });
                    if (schema != module.scriptClassSchemas.end()
                        && !schema->constructorFunctionId.empty()) {
                        const auto constructor = std::find_if(module.functions.begin(),
                            module.functions.end(),
                            [&schema](const auto& candidate) {
                                return candidate.id == schema->constructorFunctionId;
                            });
                        if (constructor == module.functions.end() || constructor->captureCount != 0
                            || constructor->parameterCount != operation.b + 1) {
                            return unsupported(function,
                                &operation,
                                "script constructor target is not statically proven");
                        }
                    }
                }
                if (operation.opcode == TypedIrOpcode::CallValue
                    && operation.closureCaptureCount == kScriptMethodProvenanceCaptureCount
                    && !operation.text.empty()) {
                    bool foundCandidate = false;
                    for (const auto& schema : module.scriptClassSchemas) {
                        const auto method = schema.methods.find(operation.text);
                        if (method == schema.methods.end())
                            continue;
                        const auto target = std::find_if(module.functions.begin(),
                            module.functions.end(),
                            [&method](const auto& candidate) {
                                return candidate.id == method->second;
                            });
                        // Same-named methods on unrelated classes may have
                        // different arities. They cannot enter this call's
                        // runtime dispatch set; unknown actual classes fail.
                        if (target != module.functions.end()
                            && target->parameterCount != operation.b + 1)
                            continue;
                        if (target == module.functions.end() || target->captureCount != 0) {
                            return unsupported(function,
                                &operation,
                                "dynamic script method target is not statically proven");
                        }
                        foundCandidate = true;
                    }
                    if (!foundCandidate) {
                        return unsupported(
                            function, &operation, "dynamic script method dispatch set is empty");
                    }
                }
            }
        }
    }
    return ScriptResult::success();
}

DirectNativeGeneratedModuleV5 generateDirectNativeModuleV5(
    const TypedIrModule& module, const DirectNativeCodegenOptionsV5& options)
{
    DirectNativeGeneratedModuleV5 result;
    const auto proof = verifyDirectNativeControlFlowV5(module);
    if (!proof.ok()) {
        result.error = proof.diagnostic();
        return result;
    }
    if (options.packageId.empty() || options.packageVersion == 0u || options.packageIdentity.empty()
        || options.platforms == AotPlatform::None) {
        result.error = ScriptDiagnostic{ScriptDiagnosticCode::AotArtifactInvalid,
            String(
                "DirectNative package options are incomplete", &core::defaultCpuMemoryResource())};
        return result;
    }
    if (options.budget.instructionLimit == 0u || options.budget.recursionLimit == 0u
        || options.budget.maxFrameValues == 0u || options.budget.maxContainerValues == 0u
        || options.budget.maxCallArguments == 0u || options.budget.maxContinuationFrames == 0u
        || options.budget.maxMemoryBytes == 0u) {
        result.error = ScriptDiagnostic{ScriptDiagnosticCode::AotArtifactInvalid,
            String("DirectNative codegen requires explicit non-zero budgets",
                &core::defaultCpuMemoryResource())};
        return result;
    }
    // Delegation graphs may be cyclic. The runtime enters every generated
    // function through DirectNativeExecutionContext::enterFunction, so the
    // declared recursion budget is the authoritative bound. Rejecting a
    // statically discovered cycle here duplicated that runtime contract and
    // incorrectly excluded budgeted recursive generators.
    for (const auto& function : module.functions) {
        const auto yieldFromProof = validateNativeYieldFromShape(module, function);
        if (!yieldFromProof.ok()) {
            result.error = yieldFromProof.diagnostic();
            return result;
        }
        if (function.localCount < 0 || function.maxStack < 0
            || static_cast<std::size_t>(function.localCount) > options.budget.maxFrameValues
            || static_cast<std::size_t>(function.maxStack) > options.budget.maxFrameValues) {
            String message("DirectNative frame shape exceeds the configured frame-values budget: ",
                &core::defaultCpuMemoryResource());
            message += function.id;
            result.error =
                ScriptDiagnostic{ScriptDiagnosticCode::ResourceLimitExceeded, std::move(message)};
            return result;
        }
        for (const auto* operation : flatten(function)) {
            std::size_t callArguments = 0u;
            if (operation->opcode == TypedIrOpcode::Call
                || operation->opcode == TypedIrOpcode::CallValue) {
                if (operation->b < 0)
                    continue;
                callArguments = static_cast<std::size_t>(operation->b);
            } else if (operation->opcode == TypedIrOpcode::MakeInstance && operation->b >= 0) {
                const auto schema = std::find_if(module.scriptClassSchemas.begin(),
                    module.scriptClassSchemas.end(),
                    [operation](const auto& candidate) {
                        return candidate.className == operation->text;
                    });
                if (schema != module.scriptClassSchemas.end()
                    && !schema->constructorFunctionId.empty()) {
                    callArguments = static_cast<std::size_t>(operation->b) + 1u;
                }
            }
            if (callArguments > options.budget.maxCallArguments) {
                String message("DirectNative call shape exceeds the configured argument budget: ",
                    &core::defaultCpuMemoryResource());
                message += function.id;
                result.error = ScriptDiagnostic{
                    ScriptDiagnosticCode::ResourceLimitExceeded, std::move(message)};
                return result;
            }
            if ((operation->opcode == TypedIrOpcode::MakeList
                    || operation->opcode == TypedIrOpcode::MakeDict
                    || operation->opcode == TypedIrOpcode::MakeRangeFromStack)
                && operation->b >= 0
                && static_cast<std::size_t>(operation->b) > options.budget.maxContainerValues) {
                String message(
                    "DirectNative container shape exceeds the configured container budget: ",
                    &core::defaultCpuMemoryResource());
                message += function.id;
                result.error = ScriptDiagnostic{
                    ScriptDiagnosticCode::ResourceLimitExceeded, std::move(message)};
                return result;
            }
        }
    }

    result.manifest.packageId = options.packageId;
    result.manifest.packageVersion = options.packageVersion;
    result.manifest.packageIdentity = options.packageIdentity;
    result.manifest.platforms = options.platforms;
    AotPackageManifestModuleV5 manifestModule;
    manifestModule.moduleId = module.moduleId;
    manifestModule.moduleVersion = module.moduleVersion;
    String artifactSeed(options.packageIdentity, &core::defaultCpuMemoryResource());
    artifactSeed += "\n";
    artifactSeed += module.fingerprint;
    artifactSeed += "\n";
    artifactSeed += options.sourceHash;
    artifactSeed += "\n";
    artifactSeed += options.dependencyGraphHash;
    artifactSeed += "\n";
    artifactSeed += options.reflectionRegistryHash;
    artifactSeed += "\n";
    artifactSeed += options.hostContractHash;
    manifestModule.artifactIdentity = sha256(artifactSeed);
    manifestModule.sourceHash = options.sourceHash;
    manifestModule.dependencyGraphHash = options.dependencyGraphHash;
    manifestModule.reflectionRegistryHash = options.reflectionRegistryHash;
    manifestModule.hostContractHash = options.hostContractHash;
    manifestModule.semanticAbiVersion = module.semanticAbiVersion;
    manifestModule.platforms = options.platforms;
    manifestModule.budget = options.budget;
    manifestModule.pluginDependencies = options.pluginDependencies;
    manifestModule.globalCount = static_cast<std::size_t>(module.globalCount);
    manifestModule.moduleInitFunctionId = module.moduleInitFunctionId;

    core::pmr::Vector<String> symbols{&core::defaultCpuMemoryResource()};
    const auto moduleSymbol = symbolPart(module.moduleId);
    for (std::size_t index = 0; index < module.functions.size(); ++index) {
        String symbol("arp_aot_v5_", &core::defaultCpuMemoryResource());
        symbol += moduleSymbol;
        symbol += "_";
        symbol += std::to_string(index);
        symbols.push_back(symbol);
        AotPackageManifestFunctionV5 function;
        function.functionId = module.functions[index].id;
        String identitySeed(manifestModule.artifactIdentity, &core::defaultCpuMemoryResource());
        identitySeed += "\n";
        identitySeed += module.functions[index].id;
        identitySeed += "\n";
        function.signatureHash = typedIrSignatureHash(module.functions[index].signature);
        identitySeed += function.signatureHash;
        function.functionIdentity = sha256(identitySeed);
        function.entrySymbol = symbol;
        function.osrCapability =
            AotOsrCapability::FunctionBoundary | AotOsrCapability::OperationBoundary;
        if (module.functions[index].isGenerator)
            function.osrCapability = function.osrCapability | AotOsrCapability::Resumable;
        manifestModule.functions.push_back(std::move(function));
    }
    String exportSeed(manifestModule.artifactIdentity, &core::defaultCpuMemoryResource());
    for (const auto& function : manifestModule.functions) {
        exportSeed += "\n";
        exportSeed += function.functionId;
        exportSeed += "\n";
        exportSeed += function.signatureHash;
    }
    manifestModule.exportContractHash = sha256(exportSeed);
    result.manifest.modules.push_back(std::move(manifestModule));
    String manifestSeed(options.packageIdentity, &core::defaultCpuMemoryResource());
    manifestSeed += "\n";
    manifestSeed += result.manifest.modules[0].artifactIdentity;
    result.manifest.manifestHash = sha256(manifestSeed);

    String descriptorSymbol(options.moduleDescriptorSymbol, &core::defaultCpuMemoryResource());
    if (descriptorSymbol.empty()) {
        descriptorSymbol = "arp_aot_v5_module_descriptor_";
        descriptorSymbol += moduleSymbol;
    }

    result.header = "#pragma once\n#include \"scriptvm/aot/aot_v5.h\"\n\nextern \"C\" int ";
    result.header += options.registrationSymbol;
    result.header += "(arp::scriptvm::AotRegistrationHostV5* host);\n";
    result.header += "extern \"C\" const arp::scriptvm::AotModuleDescriptorV5* ";
    result.header += descriptorSymbol;
    result.header += "();\n";
    result.source = "#include ";
    result.source += quote(options.generatedHeader);
    result.source +=
        "\n#include \"scriptvm/runtime/semantic_runtime.h\"\n#include <array>\n#include <cstring>\n#include <limits>\n#include <string_view>\n#include <utility>\n\nnamespace {\nusing namespace arp::scriptvm;\n";
    const auto scalarCandidates = scalarFunctionCandidates(module);
    core::pmr::Vector<String> scalarSymbols{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<String> scalarNativeSymbols{&core::defaultCpuMemoryResource()};
    for (std::size_t index = 0; index < module.functions.size(); ++index) {
        result.source += "Value ";
        result.source += symbols[index];
        result.source +=
            "(DirectNativeExecutionContext&, const Value*, std::size_t, DirectNativeResumeStateV5*, VmError*);\n";
        String scalarSymbol = symbols[index];
        scalarSymbol += "_scalar";
        scalarSymbols.push_back(std::move(scalarSymbol));
        String scalarNativeSymbol = symbols[index];
        scalarNativeSymbol += "_scalar_native";
        scalarNativeSymbols.push_back(std::move(scalarNativeSymbol));
    }
    for (std::size_t index = 0; index < module.functions.size(); ++index) {
        if (scalarCandidates[index] == 0u)
            continue;
        result.source += "Value ";
        result.source += scalarSymbols[index];
        result.source += "(DirectNativeExecutionContext&, const Value*, std::size_t, VmError*);\n";
        result.source += "static bool ";
        result.source += scalarNativeSymbols[index];
        result.source +=
            "(DirectNativeExecutionContext&, const std::int64_t*, std::size_t, std::int64_t*, VmError*);\n";
    }
    for (std::size_t index = 0; index < module.functions.size(); ++index) {
        if (scalarCandidates[index] != 0u)
            emitScalarFunction(result.source,
                module,
                index,
                symbols,
                scalarSymbols,
                scalarNativeSymbols,
                scalarCandidates);
    }
    for (std::size_t index = 0; index < module.functions.size(); ++index)
        emitFunction(result.source,
            module,
            index,
            symbols,
            scalarSymbols,
            scalarCandidates,
            result.manifest.modules[0].functions[index].functionIdentity);
    result.source += "static const AotFunctionDescriptorV5 functions[] = {\n";
    for (std::size_t index = 0; index < module.functions.size(); ++index) {
        const auto& function = result.manifest.modules[0].functions[index];
        result.source += "    {";
        result.source += quote(function.functionId);
        result.source += ", ";
        result.source += quote(function.functionIdentity);
        result.source += ", ";
        result.source += quote(function.signatureHash);
        result.source += ", ";
        result.source += quote(function.entrySymbol);
        result.source += ", &";
        result.source += symbols[index];
        result.source +=
            ", kDirectNativeEntryAbiVersion, AotFunctionExecutionKind::DirectNative, AotOsrCapability::FunctionBoundary | AotOsrCapability::OperationBoundary";
        if (module.functions[index].isGenerator)
            result.source += " | AotOsrCapability::Resumable";
        result.source += "},\n";
    }
    if (!module.globals.empty()) {
        result.source += "};\nstatic const AotModuleExportV5 exports[] = {\n";
        for (const auto& global : module.globals) {
            result.source += "    {";
            result.source += quote(global.name);
            result.source += ", ";
            result.source += std::to_string(global.slot);
            result.source += "u},\n";
        }
    }
    const auto& manifestModuleRef = result.manifest.modules[0];
    result.source += "};\n";
    if (!manifestModuleRef.pluginDependencies.empty()) {
        for (std::size_t index = 0; index < manifestModuleRef.pluginDependencies.size(); ++index) {
            const auto& dependency = manifestModuleRef.pluginDependencies[index];
            result.source += "static const AotServiceDependencyV5 plugin_services_";
            result.source += std::to_string(index);
            result.source += "[] = {\n";
            for (const auto& service : dependency.services) {
                result.source += "    {";
                result.source += quote(service.serviceId);
                result.source += ", ";
                result.source += std::to_string(service.version);
                result.source += "u},\n";
            }
            result.source += "};\n";
        }
        result.source += "static const AotPluginDependencyV5 plugin_dependencies[] = {\n";
        for (std::size_t index = 0; index < manifestModuleRef.pluginDependencies.size(); ++index) {
            const auto& dependency = manifestModuleRef.pluginDependencies[index];
            result.source += "    {";
            result.source += quote(dependency.moduleId);
            result.source += ", ";
            result.source += std::to_string(dependency.moduleVersion);
            result.source += "u, ";
            result.source += quote(dependency.exportContractHash);
            result.source += ", ";
            result.source += quote(dependency.reflectionRegistryHash);
            result.source += ", ";
            if (dependency.services.empty()) {
                result.source += "nullptr, 0u";
            } else {
                result.source += "plugin_services_";
                result.source += std::to_string(index);
                result.source += ", ";
                result.source += std::to_string(dependency.services.size());
                result.source += "u";
            }
            result.source += "},\n";
        }
        result.source += "};\n";
    }
    result.source += "static const AotModuleDescriptorV5 modules[] = {{kAotContractVersion, ";
    result.source += quote(options.packageIdentity);
    result.source += ", ";
    result.source += quote(module.moduleId);
    result.source += ", ";
    result.source += std::to_string(module.moduleVersion);
    result.source += "u, ";
    result.source += quote(manifestModuleRef.artifactIdentity);
    result.source += ", ";
    result.source += quote(options.sourceHash);
    result.source += ", ";
    result.source += quote(options.dependencyGraphHash);
    result.source += ", ";
    result.source += quote(options.reflectionRegistryHash);
    result.source += ", ";
    result.source += quote(options.hostContractHash);
    result.source += ", ";
    result.source += std::to_string(module.semanticAbiVersion);
    result.source += "u, kDirectNativeEntryAbiVersion, static_cast<AotPlatform>(";
    result.source += std::to_string(static_cast<std::uint32_t>(options.platforms));
    result.source += "u), {";
    result.source += std::to_string(options.budget.instructionLimit);
    result.source += "u, ";
    result.source += std::to_string(options.budget.recursionLimit);
    result.source += "u, ";
    result.source += std::to_string(options.budget.maxFrameValues);
    result.source += "u, ";
    result.source += std::to_string(options.budget.maxContainerValues);
    result.source += "u, ";
    result.source += std::to_string(options.budget.maxCallArguments);
    result.source += "u, ";
    result.source += std::to_string(options.budget.maxContinuationFrames);
    result.source += "u, ";
    result.source += std::to_string(options.budget.maxMemoryBytes);
    result.source += "u}, nullptr, 0u, functions, ";
    result.source += std::to_string(module.functions.size());
    result.source += "u, ";
    result.source += std::to_string(module.globalCount);
    result.source += "u, ";
    result.source += manifestModuleRef.moduleInitFunctionId.empty()
        ? "nullptr"
        : quote(manifestModuleRef.moduleInitFunctionId);
    result.source += ", ";
    result.source += quote(manifestModuleRef.exportContractHash);
    result.source += ", ";
    result.source += module.globals.empty() ? "nullptr" : "exports";
    result.source += ", ";
    result.source += std::to_string(module.globals.size());
    result.source += "u, ";
    if (manifestModuleRef.pluginDependencies.empty()) {
        result.source += "nullptr, 0u";
    } else {
        result.source += "plugin_dependencies, ";
        result.source += std::to_string(manifestModuleRef.pluginDependencies.size());
        result.source += "u";
    }
    result.source += "}};\n";
    result.source +=
        "static const AotPackageDescriptorV5 package = {kAotContractVersion, kAotPackageManifestType, ";
    result.source += quote(options.packageId);
    result.source += ", ";
    result.source += std::to_string(options.packageVersion);
    result.source += "u, ";
    result.source += quote(options.packageIdentity);
    result.source += ", ";
    result.source += quote(result.manifest.manifestHash);
    result.source += ", static_cast<AotPlatform>(";
    result.source += std::to_string(static_cast<std::uint32_t>(options.platforms));
    result.source += "u), modules, 1u};\n}\nextern \"C\" int ";
    result.source += options.registrationSymbol;
    result.source +=
        "(arp::scriptvm::AotRegistrationHostV5* host) { if (host == nullptr || host->contractVersion != kAotContractVersion || host->registerPackage == nullptr) return 0; return host->registerPackage(&package, host->userData) ? 1 : 0; }\n";
    result.source += "extern \"C\" const arp::scriptvm::AotModuleDescriptorV5* ";
    result.source += descriptorSymbol;
    result.source += "() { return &modules[0]; }\n";

    result.manifestYaml = "type: arp.script.aot-package/v1\ncontract-version: 5\npackage: ";
    result.manifestYaml += options.packageId;
    result.manifestYaml += "\npackage-version: ";
    result.manifestYaml += std::to_string(options.packageVersion);
    result.manifestYaml += "\npackage-identity: ";
    result.manifestYaml += options.packageIdentity;
    result.manifestYaml += "\nmanifest-hash: ";
    result.manifestYaml += result.manifest.manifestHash;
    result.manifestYaml += "\nplatforms: ";
    result.manifestYaml += platformYaml(options.platforms);
    result.manifestYaml += "\nmodules:\n  - id: ";
    result.manifestYaml += module.moduleId;
    result.manifestYaml += "\n    version: ";
    result.manifestYaml += std::to_string(module.moduleVersion);
    result.manifestYaml += "\n    artifact-identity: ";
    result.manifestYaml += manifestModuleRef.artifactIdentity;
    result.manifestYaml += "\n    source-hash: ";
    result.manifestYaml += options.sourceHash;
    result.manifestYaml += "\n    dependency-graph-hash: ";
    result.manifestYaml += options.dependencyGraphHash;
    result.manifestYaml += "\n    reflection-registry-hash: ";
    result.manifestYaml += options.reflectionRegistryHash;
    result.manifestYaml += "\n    host-contract-hash: ";
    result.manifestYaml += options.hostContractHash;
    result.manifestYaml += "\n    semantic-abi-version: ";
    result.manifestYaml += std::to_string(module.semanticAbiVersion);
    result.manifestYaml += "\n    entry-abi-version: ";
    result.manifestYaml += std::to_string(kDirectNativeEntryAbiVersion);
    result.manifestYaml += "\n    platforms: ";
    result.manifestYaml += platformYaml(options.platforms);
    result.manifestYaml += "\n    budget:\n      instructions: ";
    result.manifestYaml += std::to_string(options.budget.instructionLimit);
    result.manifestYaml += "\n      memory-bytes: ";
    result.manifestYaml += std::to_string(options.budget.maxMemoryBytes);
    result.manifestYaml += "\n      recursion: ";
    result.manifestYaml += std::to_string(options.budget.recursionLimit);
    result.manifestYaml += "\n      frame-values: ";
    result.manifestYaml += std::to_string(options.budget.maxFrameValues);
    result.manifestYaml += "\n      container-values: ";
    result.manifestYaml += std::to_string(options.budget.maxContainerValues);
    result.manifestYaml += "\n      call-arguments: ";
    result.manifestYaml += std::to_string(options.budget.maxCallArguments);
    result.manifestYaml += "\n      continuation-frames: ";
    result.manifestYaml += std::to_string(options.budget.maxContinuationFrames);
    result.manifestYaml += "\n    global-count: ";
    result.manifestYaml += std::to_string(module.globalCount);
    result.manifestYaml += "\n    module-init: ";
    result.manifestYaml += manifestModuleRef.moduleInitFunctionId.empty()
        ? "none"
        : manifestModuleRef.moduleInitFunctionId;
    result.manifestYaml += "\n    export-contract-hash: ";
    result.manifestYaml += manifestModuleRef.exportContractHash;
    if (!manifestModuleRef.pluginDependencies.empty()) {
        result.manifestYaml += "\n    plugin-dependencies:\n";
        for (const auto& dependency : manifestModuleRef.pluginDependencies) {
            result.manifestYaml += "      - id: ";
            result.manifestYaml += dependency.moduleId;
            result.manifestYaml += "\n        version: ";
            result.manifestYaml += std::to_string(dependency.moduleVersion);
            result.manifestYaml += "\n        export-contract-hash: ";
            result.manifestYaml += dependency.exportContractHash;
            result.manifestYaml += "\n        reflection-registry-hash: ";
            result.manifestYaml += dependency.reflectionRegistryHash;
            result.manifestYaml += "\n";
            if (!dependency.services.empty()) {
                result.manifestYaml += "\n        services:\n";
                for (const auto& service : dependency.services) {
                    result.manifestYaml += "          - id: ";
                    result.manifestYaml += service.serviceId;
                    result.manifestYaml += "\n            version: ";
                    result.manifestYaml += std::to_string(service.version);
                    result.manifestYaml += "\n";
                }
            }
        }
    }
    result.manifestYaml += "\n    functions:\n";
    for (const auto& function : manifestModuleRef.functions) {
        result.manifestYaml += "      - id: ";
        result.manifestYaml += function.functionId;
        result.manifestYaml += "\n        identity: ";
        result.manifestYaml += function.functionIdentity;
        result.manifestYaml += "\n        signature-hash: ";
        result.manifestYaml += function.signatureHash;
        result.manifestYaml += "\n        entry: ";
        result.manifestYaml += function.entrySymbol;
        result.manifestYaml += "\n        entry-abi-version: ";
        result.manifestYaml += std::to_string(function.entryAbiVersion);
        result.manifestYaml += "\n        execution-kind: DirectNative";
        result.manifestYaml += "\n        osr: function-boundary,operation-boundary";
        if ((static_cast<std::uint32_t>(function.osrCapability)
                & static_cast<std::uint32_t>(AotOsrCapability::Resumable))
            != 0u) {
            result.manifestYaml += ",resumable";
        }
        result.manifestYaml += "\n";
    }
    result.report = "AOT Production Target: Windows/Web/macOS\nDirectNative functions: ";
    result.report += std::to_string(module.functions.size());
    result.report += "\nRejected functions: 0\nGeneric executor entries: 0\n";
    return result;
}

} // namespace arp::scriptvm

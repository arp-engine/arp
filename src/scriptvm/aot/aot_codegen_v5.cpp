#include "scriptvm/aot/aot_codegen_v5.h"

#include "core/hash/sha256.h"

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

class NativeLoopScalarBindings final {
public:
    explicit NativeLoopScalarBindings(const TypedIrFunction& function)
        : kinds(function.localCount, TypedIrType::Any, &core::defaultCpuMemoryResource()),
          writes(function.localCount, 0u, &core::defaultCpuMemoryResource())
    {}

    bool contains(int slot) const
    {
        return slot >= 0 && static_cast<std::size_t>(slot) < kinds.size()
            && kinds[slot] != TypedIrType::Any;
    }

    static String local(int slot)
    {
        String name("native_loop_local_", &core::defaultCpuMemoryResource());
        name += std::to_string(slot);
        return name;
    }

    static String dirty(int slot)
    {
        String name("native_loop_dirty_", &core::defaultCpuMemoryResource());
        name += std::to_string(slot);
        return name;
    }

    static String operand(int slot)
    {
        String result("(", &core::defaultCpuMemoryResource());
        result += dirty(slot);
        result += " ? ExecutionSlot(ScriptScalar(";
        result += local(slot);
        result += ")) : locals[";
        result += std::to_string(slot);
        result += "])";
        return result;
    }

    void publish(String& source) const
    {
        for (std::size_t slot = 0; slot < kinds.size(); ++slot) {
            if (!writes[slot])
                continue;
            source += "if (";
            source += dirty(static_cast<int>(slot));
            source += ") locals[";
            source += std::to_string(slot);
            source += "] = ScriptScalar(";
            source += local(static_cast<int>(slot));
            source += "); ";
        }
    }

    core::pmr::Vector<TypedIrType> kinds;
    core::pmr::Vector<std::uint8_t> writes;
    core::pmr::Vector<std::pair<std::size_t, int>> methodSites{&core::defaultCpuMemoryResource()};
};

struct DynamicMethodCandidate {
    const TypedIrScriptClassSchema* schema = nullptr;
    const TypedIrFunction* function = nullptr;
    std::size_t functionIndex = 0u;
};

core::pmr::Vector<DynamicMethodCandidate> nativeMethodCandidates(
    const TypedIrModule& module, const TypedIrOperationRecord& operation)
{
    core::pmr::Vector<DynamicMethodCandidate> candidates{&core::defaultCpuMemoryResource()};
    for (const auto& schema : module.scriptClassSchemas) {
        const auto method = schema.methods.find(operation.text);
        if (method == schema.methods.end())
            continue;
        const auto target = std::find_if(
            module.functions.begin(), module.functions.end(), [&method](const auto& candidate) {
                return candidate.id == method->second;
            });
        if (target == module.functions.end() || target->parameterCount != operation.b + 1)
            continue;
        candidates.push_back(DynamicMethodCandidate{&schema,
            &*target,
            static_cast<std::size_t>(std::distance(module.functions.begin(), target))});
    }
    return candidates;
}

// import is guarded before execution; callbacks, suspension, handler changes
// and CFG entry points retain the canonical operation-by-operation lowering.
class NativeScalarSequence final {
    struct Operand {
        String name;
        TypedIrType kind = TypedIrType::Int;
        String recovery;
        bool boxedRecovery = false;
        bool integerConstant = false;
        int integerValue = 0;
        bool inlineInteger = false;
    };

public:
    NativeScalarSequence(const TypedIrFunction& function,
        const core::pmr::Vector<const TypedIrOperationRecord*>& operations,
        const core::pmr::Vector<std::uint8_t>& targets,
        std::size_t start,
        bool leaf = false,
        const core::pmr::Vector<int>* indexedSources = nullptr,
        const NativeLoopScalarBindings* loopBindings = nullptr,
        const TypedIrModule* inlineModule = nullptr,
        bool budgetProven = false)
        : start_(start), end_(start), leaf_(leaf), loop_(loopBindings != nullptr),
          budgetProven_(budgetProven)
    {
        for (std::size_t pc = start; pc < operations.size(); ++pc) {
            if (pc != start && targets[pc] != 0u)
                break;
            const auto& operation = *operations[pc];
            const auto opcode = operation.opcode;
            String line;
            String name("native_", &core::defaultCpuMemoryResource());
            name += std::to_string(pc);
            TypedIrType kind = TypedIrType::Int;
            if (opcode == TypedIrOpcode::LoadInt || opcode == TypedIrOpcode::LoadBool) {
                kind = opcode == TypedIrOpcode::LoadBool ? TypedIrType::Bool : TypedIrType::Int;
                line = "const ";
                line += cppType(kind);
                line += " ";
                line += name;
                line += " = ";
                line += kind == TypedIrType::Bool ? (operation.a ? "true" : "false")
                                                  : std::to_string(operation.i64);
                line += ";";
                operands_.push_back({name,
                    kind,
                    {},
                    false,
                    opcode == TypedIrOpcode::LoadInt,
                    static_cast<int>(operation.i64)});
            } else if (opcode == TypedIrOpcode::LoadFloat) {
                kind = TypedIrType::Float;
                line = "const std::uint64_t ";
                line += name;
                line += "_bits = ";
                line += std::to_string(static_cast<std::uint64_t>(operation.i64));
                line += "ULL; double ";
                line += name;
                line += "; std::memcpy(&";
                line += name;
                line += ", &";
                line += name;
                line += "_bits, sizeof(double));";
                operands_.push_back({name, kind});
            } else if (opcode == TypedIrOpcode::LoadLocal) {
                if (inlineModule && loop_ && pc + 1u < operations.size()
                    && (operations[pc + 1u]->opcode == TypedIrOpcode::CallValue
                        || operations[pc + 1u]->opcode == TypedIrOpcode::MakeMethodValue)) {
                    String receiver("locals[", &core::defaultCpuMemoryResource());
                    receiver += std::to_string(operation.a);
                    receiver += "]";
                    operands_.push_back({receiver, TypedIrType::Any, receiver});
                    end_ = pc + 1u;
                    continue;
                }
                if (pc + 1u < operations.size()
                    && (operations[pc + 1u]->opcode == TypedIrOpcode::CallValue
                        || operations[pc + 1u]->opcode == TypedIrOpcode::MakeMethodValue))
                    break;
                const auto local = std::find_if(function.locals.begin(),
                    function.locals.end(),
                    [&operation](const auto& candidate) {
                        return candidate.slot == operation.a;
                    });
                if ((leaf && operation.a >= function.parameterCount)
                    || local == function.locals.end() || local->type.nullable)
                    break;
                kind = local->type.kind;
                // Dynamic locals may join an integer expression only after
                // the same actual-payload guard as a typed imported scalar.
                if (kind == TypedIrType::Any)
                    kind = TypedIrType::Int;
                String recovery("locals[", &core::defaultCpuMemoryResource());
                recovery += std::to_string(operation.a);
                recovery += "]";
                if (!leaf && indexedSources
                    && (kind == TypedIrType::List || kind == TypedIrType::Dict)
                    && std::find(indexedSources->begin(), indexedSources->end(), operation.a)
                        != indexedSources->end()) {
                    String slot(
                        std::to_string(operation.a).c_str(), &core::defaultCpuMemoryResource());
                    guards_ += "const bool ";
                    guards_ += name;
                    guards_ += "_ready = frame->indexBorrowReady_";
                    guards_ += slot;
                    guards_ += " || (frame->indexBorrowReady_";
                    guards_ += slot;
                    guards_ += " = frame->indexBorrow_";
                    guards_ += slot;
                    guards_ += ".bind(";
                    guards_ += recovery;
                    guards_ += ")); ";
                    if (!condition_.empty())
                        condition_ += " && ";
                    condition_ += name;
                    condition_ += "_ready";
                    operands_.push_back({recovery, kind, recovery});
                } else if (kind == TypedIrType::Int || kind == TypedIrType::Float
                    || kind == TypedIrType::Bool) {
                    if (loopBindings && loopBindings->contains(operation.a)) {
                        line = "const ";
                        line += cppType(kind);
                        line += " ";
                        line += name;
                        line += " = ";
                        line += NativeLoopScalarBindings::local(operation.a);
                        line += ";";
                        Operand operand{name, kind, NativeLoopScalarBindings::operand(operation.a)};
                        operand.inlineInteger =
                            kind == TypedIrType::Int && loopBindings->writes[operation.a];
                        operands_.push_back(std::move(operand));
                    } else {
                        guards_ += "const auto* ";
                        guards_ += name;
                        guards_ += leaf ? "_ptr = args[" : "_ptr = locals[";
                        guards_ += std::to_string(operation.a);
                        guards_ += "].scalarPointer<";
                        guards_ += cppType(kind);
                        guards_ += ">(); ";
                        if (!condition_.empty())
                            condition_ += " && ";
                        condition_ += name;
                        condition_ += "_ptr != nullptr";
                        line = "const ";
                        line += cppType(kind);
                        line += " ";
                        line += name;
                        line += " = *";
                        line += name;
                        line += "_ptr;";
                        operands_.push_back({name, kind, leaf ? String{} : recovery});
                    }
                } else if (kind == TypedIrType::String && loop_) {
                    guards_ += "std::string_view ";
                    guards_ += name;
                    guards_ += "; ";
                    if (!condition_.empty())
                        condition_ += " && ";
                    condition_ += recovery + ".stringView(" + name + ")";
                    operands_.push_back({name, kind, recovery});
                } else
                    break;
            } else if (opcode == TypedIrOpcode::LoadString && !leaf) {
                kind = TypedIrType::String;
                line = "const std::string_view ";
                line += name;
                line += " = ";
                line += quote(operation.text);
                line += ";";
                String recovery(
                    "ExecutionSlot::borrowLiteral([]() -> const arp::core::pmr::String& { static const auto literal = [] { arp::core::CpuMemoryQuotaScope scope(nullptr); return arp::core::pmr::String(",
                    &core::defaultCpuMemoryResource());
                recovery += quote(operation.text);
                recovery += ", &arp::core::defaultCpuMemoryResource()); }(); return literal; }())";
                operands_.push_back({name, kind, recovery});
            } else if (opcode == TypedIrOpcode::BuiltinLen && !operands_.empty()
                && operands_.back().kind == TypedIrType::String) {
                line = "const int ";
                line += name;
                line += " = static_cast<int>(";
                line += operands_.back().name;
                line += ".size()); if (";
                line += name;
                line += " < 0) { ";
                for (const auto& operand : operands_) {
                    line += "stack[sp++] = " + slotExpression(operand) + "; ";
                }
                line += "context.remainingInstructions += nativeSequenceEnd - ";
                line += std::to_string(pc);
                line += loop_ ? "u; goto aot_replay_pc_" : "u; goto aot_pc_";
                line += std::to_string(pc);
                line += "; }";
                operands_.pop_back();
                operands_.push_back({name, TypedIrType::Int});
            } else if (opcode == TypedIrOpcode::LoadIndex && indexedSources && !leaf
                && inputs_ == 0u && operands_.size() >= 2u && (*indexedSources)[pc] >= 0) {
                const auto& base = operands_[operands_.size() - 2u];
                const auto& key = operands_.back();
                if ((base.kind != TypedIrType::List && base.kind != TypedIrType::Dict)
                    || (key.kind != TypedIrType::Int && key.kind != TypedIrType::String))
                    break;
                const bool string = pc + 1u < operations.size()
                    && (operations[pc + 1u]->opcode == TypedIrOpcode::LoadIndex
                        || operations[pc + 1u]->opcode == TypedIrOpcode::LoadString);
                kind = string ? TypedIrType::String : TypedIrType::Int;
                line = "const auto* ";
                line += name;
                const bool stableString =
                    loop_ && key.boxedRecovery && key.kind == TypedIrType::String;
                line += "_carrier = ";
                if (stableString) {
                    line += "native_string_index_";
                    line += std::to_string(pc);
                    line += ".read(";
                } else {
                    line += "frame->indexBorrow_";
                    line += std::to_string((*indexedSources)[pc]);
                }
                const bool nativeIndex =
                    key.kind == TypedIrType::Int && (key.recovery.empty() || key.inlineInteger);
                line += stableString                                       ? ""
                    : nativeIndex                                          ? ".readIntegerCarrier("
                    : key.boxedRecovery && key.kind == TypedIrType::String ? ".readCarrier("
                                                                           : ".read(ExecutionSlot(";
                line += nativeIndex ? key.name : slotExpression(key);
                line += nativeIndex || (key.boxedRecovery && key.kind == TypedIrType::String)
                    ? "); const auto* "
                    : ")); const auto* ";
                line += name;
                line += "_ptr = ";
                line += name;
                line += "_carrier != nullptr ? exactValuePayload<";
                line += string ? "arp::core::pmr::String" : "int";
                line += ">(*";
                line += name;
                line += "_carrier) : nullptr; if (!";
                line += name;
                line += "_ptr) { ";
                for (const auto& operand : operands_) {
                    line += "stack[sp++] = ";
                    line += slotExpression(operand);
                    line += "; ";
                }
                // Replay only the failing index operation. Its completed pure
                // prefix and exact carriers are reconstructed above.
                line += "context.remainingInstructions += nativeSequenceEnd - ";
                line += std::to_string(pc);
                line += loop_ ? "u; goto aot_replay_pc_" : "u; goto aot_pc_";
                line += std::to_string(pc);
                line += "; } const ";
                line += string ? "std::string_view " : "int ";
                line += name;
                line += " = *";
                line += name;
                line += "_ptr;";
                String recovery("*", &core::defaultCpuMemoryResource());
                recovery += name;
                recovery += "_carrier";
                operands_.resize(operands_.size() - 2u);
                operands_.push_back({name, kind, recovery, true});
            } else if (opcode == TypedIrOpcode::MakeMethodValue && inlineModule && loop_) {
                // The canonical AOT method marker retains the real receiver.
            } else if (opcode == TypedIrOpcode::CallValue && inlineModule && loop_
                && operation.b >= 0
                && operands_.size() >= static_cast<std::size_t>(operation.b) + 1u) {
                const auto count = static_cast<std::size_t>(operation.b);
                const auto base = operands_.size() - count - 1u;
                const auto& callable = operands_.back();
                if (callable.recovery.empty())
                    break;
                core::pmr::Vector<Operand> arguments{&core::defaultCpuMemoryResource()};
                String invocation;
                invocation +=
                    "ExecutionSlot " + name + "_result; bool " + name + "_taken = false; { ";
                bool admitted = false;
                if (operation.closureCaptureCount == kScriptMethodProvenanceCaptureCount
                    && !operation.text.empty()) {
                    arguments.push_back(callable);
                    for (std::size_t index = 0; index < count; ++index)
                        arguments.push_back(operands_[base + index]);
                    const auto boundSite = std::find_if(loopBindings->methodSites.begin(),
                        loopBindings->methodSites.end(),
                        [pc](const auto& site) {
                            return site.first == pc;
                        });
                    const bool boundDispatch = boundSite != loopBindings->methodSites.end();
                    if (!boundDispatch) {
                        invocation +=
                            "const auto* instance = exactValuePayload<arp::reflection::instance>(";
                        invocation += callable.recovery;
                        invocation +=
                            ".boxed()); if (instance && instance->alive() && instance->class_()) { const auto& actualClass = instance->class_()->name(); ";
                    }
                    std::size_t selector = 0u;
                    for (const auto& candidate : nativeMethodCandidates(*inlineModule, operation)) {
                        ++selector;
                        const auto code = flatten(*candidate.function);
                        core::pmr::Vector<std::uint8_t> entries(
                            code.size(), 0u, &core::defaultCpuMemoryResource());
                        NativeScalarSequence child(*candidate.function, code, entries, 0u, true);
                        String expression;
                        if (!child.emitInlineInvocation(expression,
                                *candidate.function,
                                code.size(),
                                arguments,
                                "nullptr",
                                name))
                            continue;
                        invocation +=
                            boundDispatch ? "if (native_loop_method_" : "if (actualClass == ";
                        if (boundDispatch) {
                            invocation += std::to_string(pc);
                            invocation += " == ";
                            invocation += std::to_string(selector);
                            invocation += "u";
                        } else
                            invocation += quote(candidate.schema->className);
                        invocation += ") { ";
                        invocation += expression;
                        invocation += " } ";
                        admitted = true;
                    }
                    if (!boundDispatch)
                        invocation += "} ";
                } else if (!operation.closureTargetFunctionId.empty()) {
                    const auto target =
                        resolveFunctionIndex(*inlineModule, operation.closureTargetFunctionId);
                    if (target >= 0) {
                        for (std::size_t index = 0; index < count; ++index)
                            arguments.push_back(operands_[base + index]);
                        const auto& callee = inlineModule->functions[target];
                        const auto code = flatten(callee);
                        core::pmr::Vector<std::uint8_t> entries(
                            code.size(), 0u, &core::defaultCpuMemoryResource());
                        NativeScalarSequence child(callee, code, entries, 0u, true);
                        String expression;
                        if (child.emitInlineInvocation(expression,
                                callee,
                                code.size(),
                                arguments,
                                "&closure->captures",
                                name)) {
                            invocation += "const auto* closure = " + callable.recovery
                                + ".closure(); if (closure && closure->captures.size() == ";
                            invocation += std::to_string(operation.closureCaptureCount);
                            invocation += "u) { " + expression + " } ";
                            admitted = true;
                        }
                    }
                }
                if (!admitted)
                    break;
                invocation += "} if (!" + name + "_taken) { ";
                for (const auto& operand : operands_)
                    invocation += "stack[sp++] = " + slotExpression(operand) + "; ";
                invocation += "context.remainingInstructions += nativeSequenceEnd - ";
                invocation += std::to_string(pc);
                invocation += "u; goto aot_replay_pc_";
                invocation += std::to_string(pc);
                invocation += "; } const auto* " + name + "_ptr = " + name
                    + "_result.scalarPointer<int>(); if (!" + name
                    + "_ptr || (error && error->code != ScriptDiagnosticCode::None)) { context.remainingInstructions += nativeSequenceEnd - ";
                invocation += std::to_string(pc + 1u);
                invocation +=
                    "u; goto aot_exception_dispatch; } const int " + name + " = *" + name + "_ptr;";
                line = std::move(invocation);
                operands_.resize(base);
                operands_.push_back({name, TypedIrType::Int});
            } else if (opcode == TypedIrOpcode::LoadCapture && leaf) {
                kind = TypedIrType::Int;
                guards_ += "const auto* ";
                guards_ += name;
                guards_ += "_cell = captures != nullptr && captures->size() > ";
                guards_ += std::to_string(operation.a);
                guards_ += "u ? (*captures)[";
                guards_ += std::to_string(operation.a);
                guards_ += "].get() : nullptr; const auto* ";
                guards_ += name;
                guards_ += "_ptr = ";
                guards_ += name;
                guards_ += "_cell != nullptr ? checkedValuePayload<int>(";
                guards_ += name;
                guards_ += "_cell->value) : nullptr; ";
                if (!condition_.empty())
                    condition_ += " && ";
                condition_ += name;
                condition_ += "_ptr != nullptr";
                line = "const int ";
                line += name;
                line += " = *";
                line += name;
                line += "_ptr;";
                operands_.push_back({name, kind});
            } else if (!binaryOperation(opcode).empty() || !compareOperation(opcode).empty()) {
                if (opcode == TypedIrOpcode::ShiftLeft || opcode == TypedIrOpcode::ShiftRight)
                    break;
                if (leaf && operands_.size() < 2u)
                    break;
                const auto oldInputs = inputs_;
                const auto oldGuards = guards_.size(), oldCondition = condition_.size(),
                           oldImports = imports_.size();
                const core::pmr::Vector<Operand> oldOperands(
                    operands_, &core::defaultCpuMemoryResource());
                const auto rollbackInputs = [&] {
                    inputs_ = oldInputs;
                    guards_.resize(oldGuards);
                    condition_.resize(oldCondition);
                    imports_.resize(oldImports);
                    operands_ = oldOperands;
                };
                while (operands_.size() < 2u) {
                    String input("native_input_", &core::defaultCpuMemoryResource());
                    input += std::to_string(inputs_);
                    ++inputs_;
                    guards_ += "const auto* ";
                    guards_ += input;
                    guards_ += "_ptr = stack[sp - ";
                    guards_ += std::to_string(inputs_);
                    guards_ += "u].scalarPointer<int>(); ";
                    if (!condition_.empty())
                        condition_ += " && ";
                    condition_ += input;
                    condition_ += "_ptr != nullptr";
                    imports_ += "const int ";
                    imports_ += input;
                    imports_ += " = *";
                    imports_ += input;
                    imports_ += "_ptr; ";
                    operands_.insert(operands_.begin(), Operand{input, TypedIrType::Int});
                }
                const auto& left = operands_[operands_.size() - 2u];
                const auto& right = operands_.back();
                const bool compare = !compareOperation(opcode).empty();
                const bool strings =
                    left.kind == TypedIrType::String && right.kind == TypedIrType::String;
                if ((left.kind == TypedIrType::String || right.kind == TypedIrType::String
                        || left.kind == TypedIrType::List || left.kind == TypedIrType::Dict
                        || right.kind == TypedIrType::List || right.kind == TypedIrType::Dict)
                    && !(strings && compare)) {
                    rollbackInputs();
                    break;
                }
                if (!compare
                    && (left.kind == TypedIrType::Bool || right.kind == TypedIrType::Bool)) {
                    rollbackInputs();
                    break;
                }
                if (compare
                    && ((left.kind == TypedIrType::Bool) != (right.kind == TypedIrType::Bool))) {
                    rollbackInputs();
                    break;
                }
                const bool floating =
                    left.kind == TypedIrType::Float || right.kind == TypedIrType::Float;
                const bool integerPower = opcode == TypedIrOpcode::Pow
                    && left.kind == TypedIrType::Int && right.kind == TypedIrType::Int
                    && right.integerConstant && right.integerValue >= 0;
                if (!compare
                    && ((opcode == TypedIrOpcode::Pow && !floating && !integerPower)
                        || opcode == TypedIrOpcode::ShiftLeft
                        || opcode == TypedIrOpcode::ShiftRight)) {
                    rollbackInputs();
                    break;
                } // Negative/dynamic integer powers can change the result kind.
                if (floating
                    && (opcode == TypedIrOpcode::BitAnd || opcode == TypedIrOpcode::BitOr
                        || opcode == TypedIrOpcode::BitXor || opcode == TypedIrOpcode::BitClear)) {
                    rollbackInputs();
                    break;
                }
                kind = compare ? TypedIrType::Bool
                    : floating ? TypedIrType::Float
                               : TypedIrType::Int;
                line = "const auto ";
                line += name;
                if (compare) {
                    if (strings) {
                        line +=
                            " = ScriptSemanticRuntime::comparePayload<SemanticCompareOperation::";
                        line += compareOperation(opcode);
                        line += ">(";
                        line += left.name;
                        line += ", ";
                        line += right.name;
                        line += ");";
                    } else {
                        line +=
                            " = ScriptSemanticRuntime::comparePayload<SemanticCompareOperation::";
                        line += compareOperation(opcode);
                        line += ">(";
                        if (floating)
                            line += "static_cast<double>(";
                        line += left.name;
                        if (floating)
                            line += ")";
                        line += ", ";
                        if (floating)
                            line += "static_cast<double>(";
                        line += right.name;
                        if (floating)
                            line += ")";
                        line += ");";
                    }
                } else {
                    line += "_result = ScriptSemanticRuntime::";
                    if (floating)
                        line += "floatBinary<SemanticBinaryOperation::";
                    else if (opcode == TypedIrOpcode::FloorDiv || integerPower)
                        line += "scalarBinary(SemanticBinaryOperation::";
                    else
                        line += "intBinary<SemanticBinaryOperation::";
                    line += binaryOperation(opcode);
                    if ((opcode == TypedIrOpcode::FloorDiv || integerPower) && !floating) {
                        // Keep the result type explicit without copying a Value carrier.
                        line = "ScalarOperationResult ";
                        line += name;
                        line +=
                            "_result; ScriptSemanticRuntime::scalarBinary(SemanticBinaryOperation::";
                        line += binaryOperation(opcode);
                        line += ", ScriptScalar(";
                        line += left.name;
                        line += "), ScriptScalar(";
                        line += right.name;
                        line += "), ";
                        line += name;
                        line += "_result);";
                    } else {
                        line += ">(";
                        line += left.name;
                        line += ", ";
                        line += right.name;
                        line += ");";
                    }
                    line += " if (!";
                    line += name;
                    line += "_result.ok()) { context.remainingInstructions += nativeSequenceEnd - ";
                    line += std::to_string(pc + 1u);
                    line += "u; if (error != nullptr) { error->code = ";
                    line += name;
                    line += "_result.code; error->message = ";
                    line += name;
                    line += leaf ? "_result.message; } return true; } const "
                                 : "_result.message; } goto aot_exception_dispatch; } const ";
                    line += cppType(kind);
                    line += " ";
                    line += name;
                    line += " = ";
                    if (floating) {
                        line += name;
                        line += "_result.value.floating();";
                    } else {
                        line += "static_cast<int>(";
                        line += name;
                        line += "_result.value.integer());";
                    }
                }
                operands_.resize(operands_.size() - 2u);
                operands_.push_back({name, kind});
            } else if ((opcode == TypedIrOpcode::Return || opcode == TypedIrOpcode::ReturnInt)
                && leaf && (operation.a == 1 || opcode == TypedIrOpcode::ReturnInt)
                && operands_.size() == 1u && pc > start
                && (!binaryOperation(operations[pc - 1u]->opcode).empty()
                    || !compareOperation(operations[pc - 1u]->opcode).empty())) {
                line = "result = ScriptScalar(";
                line += operands_.back().name;
                line += "); return true;";
                returnKind_ = operands_.back().kind;
                operands_.clear();
                terminal_ = true;
                returned_ = true;
            } else if (opcode == TypedIrOpcode::StoreLocal && !leaf) {
                if (operands_.empty())
                    break;
                const auto destination = std::find_if(function.locals.begin(),
                    function.locals.end(),
                    [&operation](const auto& candidate) {
                        return candidate.slot == operation.a;
                    });
                if (destination == function.locals.end()
                    || (destination->type.kind != operands_.back().kind
                        && destination->type.kind != TypedIrType::Any)
                    || (operands_.back().kind != TypedIrType::Int
                        && operands_.back().kind != TypedIrType::Bool
                        && operands_.back().kind != TypedIrType::Float))
                    break;
                if (loopBindings && loopBindings->contains(operation.a)) {
                    if (!operands_.back().recovery.empty())
                        break;
                    line = NativeLoopScalarBindings::local(operation.a);
                    line += " = ";
                    line += operands_.back().name;
                    line += "; ";
                    line += NativeLoopScalarBindings::dirty(operation.a);
                    line += " = true;";
                } else {
                    line = "locals[";
                    line += std::to_string(operation.a);
                    line += "] = ";
                    line += slotExpression(operands_.back());
                    line += ";";
                }
                operands_.pop_back();
                terminal_ = true;
            } else if (!leaf
                && (opcode == TypedIrOpcode::JumpIfFalse || opcode == TypedIrOpcode::JumpIfTrue)) {
                if (operands_.size() != 1u || operands_.back().kind != TypedIrType::Bool)
                    break;
                line = "if (";
                if (opcode == TypedIrOpcode::JumpIfFalse)
                    line += "!";
                line += operands_.back().name;
                line += ") goto aot_pc_";
                line += std::to_string(operation.a);
                line += ";";
                operands_.pop_back();
                // Branches are only admitted when the expression has no other
                // stack results, so the target sees the original stack prefix.
                if (!operands_.empty())
                    break;
                terminal_ = true;
            } else {
                break;
            }
            body_ += line;
            body_ += "\n";
            end_ = pc + 1u;
            if (terminal_)
                break;
        }
    }

    std::size_t emit(String& source) const
    {
        if (end_ - start_ < (loop_ ? 1u : 3u))
            return start_;
        source += budgetProven_ ? " { "
            : loop_
            ? "    if (context.remainingInstructions >= "
            : "    if (context.operationBoundary == nullptr && frame == &localFrame && context.remainingInstructions >= ";
        if (!budgetProven_) {
            source += std::to_string(end_ - start_);
            source += "u) { ";
        }
        source += guards_;
        source += " if (";
        source += condition_.empty() ? "true" : condition_;
        source += ") {\n";
        source += imports_;
        if (inputs_ != 0u) {
            source += " for (std::size_t index = sp - ";
            source += std::to_string(inputs_);
            source += "u; index < sp; ++index) stack[index] = ScriptScalar();\n";
        }
        source += " sp -= ";
        source += std::to_string(inputs_);
        source += "u;\n";
        emitBody(source);
        for (const auto& operand : operands_) {
            source += " stack[sp++] = ";
            source += slotExpression(operand);
            source += ";\n";
        }
        source += " goto aot_pc_";
        source += std::to_string(end_);
        source += "; } }\n";
        if (loop_) {
            source += "goto aot_replay_pc_";
            source += std::to_string(start_);
            source += ";\n";
        }
        return end_;
    }

    bool emitLeaf(String& source,
        const String& symbol,
        const TypedIrFunction& function,
        std::size_t operationCount) const
    {
        if (!leaf_ || !returned_ || end_ != operationCount || function.isGenerator
            || !function.exceptionRegions.empty())
            return false;
        source += "static bool ";
        source += symbol;
        source +=
            "_slot_leaf(DirectNativeExecutionContext& context, const ExecutionSlot* args, const arp::core::pmr::Vector<std::shared_ptr<ScriptCell>>* captures, ExecutionSlot& result, VmError* error) { ";
        if (function.captureCount != 0) {
            source += "if (captures == nullptr || captures->size() != ";
            source += std::to_string(function.captureCount);
            source += "u) return false; ";
        }
        source += guards_;
        source += " if (!(";
        source += condition_.empty() ? "true" : condition_;
        source += ") || context.remainingInstructions < ";
        source += std::to_string(operationCount);
        source += "u) return false; ";
        source += "if (!context.enterLeafFunction(";
        source += quote(function.id);
        source +=
            ", error)) return true; struct Scope { DirectNativeExecutionContext& context; ~Scope() { context.leaveLeafFunction(); } } scope { context };\n";
        emitBody(source);
        source += " return false; }\n";
        return true;
    }

private:
    bool emitInlineInvocation(String& source,
        const TypedIrFunction& function,
        std::size_t operationCount,
        const core::pmr::Vector<Operand>& arguments,
        const char* capturesExpression,
        const String& name) const
    {
        if (!leaf_ || !returned_ || end_ != operationCount || function.isGenerator
            || !function.exceptionRegions.empty()
            || function.signature.result.kind != TypedIrType::Int
            || function.signature.result.nullable || returnKind_ != TypedIrType::Int)
            return false;
        String guards = guards_;
        for (std::size_t index = 0; index < arguments.size(); ++index) {
            String needle("args[", &core::defaultCpuMemoryResource());
            needle += std::to_string(index);
            needle += "].scalarPointer<";
            const auto begin = guards.find(needle);
            if (begin == String::npos)
                continue;
            const auto end = guards.find(">()", begin);
            if (end == String::npos
                || (arguments[index].kind != TypedIrType::Int
                    && arguments[index].kind != TypedIrType::Float
                    && arguments[index].kind != TypedIrType::Bool))
                return false;
            const String expression = needle + cppType(arguments[index].kind) + ">()";
            if (guards.compare(begin, expression.size(), expression) != 0)
                return false;
            const String replacement = "&(" + arguments[index].name + ")";
            for (auto pos = guards.find(expression); pos != String::npos;
                pos = guards.find(expression, pos + replacement.size()))
                guards.replace(pos, expression.size(), replacement);
        }
        if (guards.find("args[") != String::npos)
            return false;
        // A selector miss replays the original call, including the general
        // reflected capture path. The inline expression only borrows actual ints.
        const String broad = "checkedValuePayload<int>";
        for (auto pos = guards.find(broad); pos != String::npos; pos = guards.find(broad, pos + 1u))
            guards.replace(pos, broad.size(), "exactValuePayload<int>");
        source += name
            + "_taken = [&](ExecutionSlot& result) -> bool { const arp::core::pmr::Vector<std::shared_ptr<ScriptCell>>* captures = ";
        source += capturesExpression;
        source += "; ";
        if (function.captureCount != 0) {
            source += "if (!captures || captures->size() != ";
            source += std::to_string(function.captureCount);
            source += "u) return false; ";
        }
        source += guards;
        source += "if (!(";
        source += condition_.empty() ? "true" : condition_;
        source += ") || context.remainingInstructions < ";
        source += std::to_string(operationCount);
        source += "u) return false; if (!context.enterLeafFunction(";
        source += quote(function.id);
        source +=
            ", error)) return true; struct Scope { DirectNativeExecutionContext& context; ~Scope() { context.leaveLeafFunction(); } } scope { context };\n";
        emitBody(source);
        source += "return false; }(" + name + "_result);\n";
        return true;
    }

    void emitBody(String& source) const
    {
        source += "constexpr std::size_t nativeSequenceEnd = ";
        source += std::to_string(end_);
        source += "u; context.remainingInstructions -= ";
        source += std::to_string(end_ - start_);
        source += "u;\n";
        source += body_;
    }

    static String slotExpression(const Operand& operand)
    {
        if (!operand.recovery.empty())
            return operand.recovery;
        String result("ScriptScalar(", &core::defaultCpuMemoryResource());
        result += operand.name;
        result += ")";
        return result;
    }

    static const char* cppType(TypedIrType kind)
    {
        return kind == TypedIrType::Float ? "double" : kind == TypedIrType::Bool ? "bool" : "int";
    }

    std::size_t start_, end_;
    bool terminal_ = false, leaf_ = false, returned_ = false, loop_ = false;
    bool budgetProven_ = false;
    TypedIrType returnKind_ = TypedIrType::Any;
    std::size_t inputs_ = 0u;
    String guards_, condition_, body_, imports_;
    core::pmr::Vector<Operand> operands_{&core::defaultCpuMemoryResource()};
};

bool privateStringLocal(
    const core::pmr::Vector<const TypedIrOperationRecord*>& operations, int slot);

bool scalarValueType(const TypedIrTypeRef& type)
{
    return type.kind == TypedIrType::Int || type.kind == TypedIrType::Bool;
}

// A private string may only be observed by len() or by its own append. This
// excludes returns, captures, addresses, arguments and assignments to aliases.
bool privateStringLocal(
    const core::pmr::Vector<const TypedIrOperationRecord*>& operations, int slot)
{
    for (std::size_t pc = 0; pc < operations.size(); ++pc) {
        const auto& operation = *operations[pc];
        if (operation.opcode == TypedIrOpcode::AddrLocal && operation.a == slot)
            return false;
        if (operation.opcode != TypedIrOpcode::LoadLocal || operation.a != slot)
            continue;
        if (pc + 1u < operations.size() && operations[pc + 1u]->opcode == TypedIrOpcode::BuiltinLen)
            continue;
        if (pc + 3u < operations.size() && operations[pc + 1u]->opcode == TypedIrOpcode::LoadString
            && operations[pc + 2u]->opcode == TypedIrOpcode::Add
            && operations[pc + 3u]->opcode == TypedIrOpcode::StoreLocal
            && operations[pc + 3u]->a == slot)
            continue;
        return false;
    }
    return true;
}

bool privateIndexedLocal(
    const core::pmr::Vector<const TypedIrOperationRecord*>& operations, int slot)
{
    for (std::size_t pc = 0; pc < operations.size(); ++pc) {
        const auto& operation = *operations[pc];
        if (operation.opcode == TypedIrOpcode::AddrLocal && operation.a == slot)
            return false;
        if (operation.opcode != TypedIrOpcode::LoadLocal || operation.a != slot)
            continue;
        if (pc + 1u < operations.size() && operations[pc + 1u]->opcode == TypedIrOpcode::BuiltinLen)
            continue;
        if (pc + 2u < operations.size() && operations[pc + 1u]->opcode == TypedIrOpcode::LoadInt
            && operations[pc + 2u]->opcode == TypedIrOpcode::LoadIndex)
            continue;
        return false;
    }
    return true;
}

class NativeIndexUpdateSpan final {
public:
    static void emit(String& source,
        const core::pmr::Vector<const TypedIrOperationRecord*>& operations,
        std::size_t pc,
        bool borrowed,
        const String& index,
        bool loop = false,
        bool budgetProven = false,
        bool inlineIndex = false)
    {
        source += budgetProven ? "{ "
            : loop
            ? "if (context.remainingInstructions >= 8u) { "
            : "if (context.operationBoundary == nullptr && frame == &localFrame && context.remainingInstructions >= 8u) { ";
        source += "ScriptSemanticRuntime::IntegerIndexUpdate update; int previous; ";
        const String owner(
            std::to_string(operations[pc]->a).c_str(), &core::defaultCpuMemoryResource());
        if (borrowed) {
            source += "if (!frame->indexBorrowReady_" + owner + ") frame->indexBorrowReady_" + owner
                + " = frame->indexBorrow_" + owner + ".bind(locals[" + owner
                + "]); if (frame->indexBorrowReady_" + owner + " && frame->indexBorrow_" + owner
                + (inlineIndex ? ".prepareIntegerUpdate(" : ".prepareUpdate(") + index
                + ", update, previous)) { ";
        } else
            source += "if (update.bind(locals[" + owner + "], " + index + ", previous)) { ";
        source += "const auto changed = ScriptSemanticRuntime::intBinary<SemanticBinaryOperation::";
        source += binaryOperation(operations[pc + 6u]->opcode);
        source += ">(previous, ";
        source += std::to_string(operations[pc + 5u]->i64);
        source +=
            "); if (changed.ok()) { context.remainingInstructions -= 8u; update.commit(static_cast<int>(changed.value.integer())); context.commitSideEffect(resume, ";
        source += std::to_string(pc + 7u);
        source += "u); goto aot_pc_";
        source += std::to_string(pc + 8u);
        source += "; } } }\n";
        if (loop) {
            source += "goto aot_replay_pc_";
            source += std::to_string(pc);
            source += ";\n";
        }
    }

    static bool matches(
        const core::pmr::Vector<const TypedIrOperationRecord*>& operations, std::size_t pc)
    {
        if (pc + 7u >= operations.size())
            return false;
        const auto arithmetic = operations[pc + 6u]->opcode;
        return operations[pc]->opcode == TypedIrOpcode::LoadLocal
            && operations[pc + 1u]->opcode == TypedIrOpcode::LoadLocal
            && operations[pc + 2u]->opcode == TypedIrOpcode::LoadLocal
            && operations[pc + 2u]->a == operations[pc]->a
            && operations[pc + 3u]->opcode == TypedIrOpcode::LoadLocal
            && operations[pc + 3u]->a == operations[pc + 1u]->a
            && operations[pc + 4u]->opcode == TypedIrOpcode::LoadIndex
            && operations[pc + 5u]->opcode == TypedIrOpcode::LoadInt
            && (arithmetic == TypedIrOpcode::Add || arithmetic == TypedIrOpcode::Sub
                || arithmetic == TypedIrOpcode::Mul || arithmetic == TypedIrOpcode::IntAdd
                || arithmetic == TypedIrOpcode::IntSub || arithmetic == TypedIrOpcode::IntMul)
            && operations[pc + 7u]->opcode == TypedIrOpcode::StoreIndex;
    }
};

class NativeIndexReadPlan final {
public:
    explicit NativeIndexReadPlan(const TypedIrFunction& function,
        const core::pmr::Vector<const TypedIrOperationRecord*>& operations)
        : sources(operations.size(), -1, &core::defaultCpuMemoryResource()),
          locals(function.localCount, 0u, &core::defaultCpuMemoryResource())
    {
        if (function.isGenerator || function.captureCount != 0
            || !function.exceptionRegions.empty())
            return;
        for (const auto* operation : operations) {
            const auto opcode = operation->opcode;
            const auto& descriptor = typedIrOperationDescriptor(opcode);
            if (descriptor.callKind != TypedIrCallKind::None || descriptor.suspension
                || opcode == TypedIrOpcode::LoadField || opcode == TypedIrOpcode::StoreField
                || opcode == TypedIrOpcode::Deref || opcode == TypedIrOpcode::StoreIndirect
                || opcode == TypedIrOpcode::LoadGlobal || opcode == TypedIrOpcode::StoreGlobal
                || opcode == TypedIrOpcode::LoadModule || opcode == TypedIrOpcode::LoadModuleExport
                || opcode == TypedIrOpcode::AddrCapture || opcode == TypedIrOpcode::AddrGlobal)
                return;
        }
        for (const auto& local : function.locals) {
            if (local.slot < function.parameterCount || local.slot < 0
                || (local.type.kind != TypedIrType::List && local.type.kind != TypedIrType::Dict))
                continue;
            core::pmr::Vector<std::size_t> reads{&core::defaultCpuMemoryResource()};
            bool valid = true;
            for (std::size_t pc = 0; pc < operations.size() && valid; ++pc) {
                const auto& operation = *operations[pc];
                // Iteration writes are implicit owner replacements, not stable
                // mapped-value updates. They must never retain a prior topology.
                if (operation.opcode == TypedIrOpcode::RangeNext) {
                    const auto packed = static_cast<std::uint64_t>(operation.i64);
                    const int key = static_cast<std::int32_t>(packed >> 32u);
                    const int value = static_cast<std::int32_t>(packed);
                    if (local.slot == key || local.slot == value) {
                        valid = false;
                        break;
                    }
                }
                if (operation.opcode == TypedIrOpcode::RangeInit
                    && (local.slot == operation.a || local.slot == operation.b)) {
                    valid = false;
                    break;
                }
                // Cache only a fresh, nonescaping owner. Imported aliases and
                // arbitrary replacement values retain ordinary indexed access.
                if (operation.opcode == TypedIrOpcode::StoreLocal && operation.a == local.slot
                    && (pc == 0u
                        || (operations[pc - 1u]->opcode != TypedIrOpcode::MakeList
                            && operations[pc - 1u]->opcode != TypedIrOpcode::MakeDict))) {
                    valid = false;
                    break;
                }
                if (operation.opcode == TypedIrOpcode::AddrLocal && operation.a == local.slot) {
                    valid = false;
                    break;
                }
                if (operation.opcode != TypedIrOpcode::LoadLocal || operation.a != local.slot)
                    continue;
                if (NativeIndexUpdateSpan::matches(operations, pc)) {
                    reads.push_back(pc + 4u);
                    continue;
                }
                bool found = false;
                std::size_t depth = 1u;
                for (std::size_t next = pc + 1u; next < operations.size(); ++next) {
                    const auto& index = *operations[next];
                    if (index.opcode == TypedIrOpcode::LoadIndex && depth == 2u) {
                        reads.push_back(next);
                        found = true;
                        break;
                    }
                    if (index.opcode == TypedIrOpcode::LoadIndex && depth > 2u) {
                        --depth;
                        continue;
                    }
                    if (index.opcode == TypedIrOpcode::LoadInt
                        || index.opcode == TypedIrOpcode::LoadString) {
                        ++depth;
                        continue;
                    }
                    if (index.opcode == TypedIrOpcode::LoadLocal) {
                        const auto scalar = std::find_if(function.locals.begin(),
                            function.locals.end(),
                            [&index](const auto& candidate) {
                                return candidate.slot == index.a;
                            });
                        if (scalar == function.locals.end())
                            break;
                        ++depth;
                        continue;
                    }
                    if (!binaryOperation(index.opcode).empty() && depth >= 3u) {
                        --depth;
                        continue;
                    }
                    break;
                }
                if (!found)
                    valid = false;
            }
            if (valid && !reads.empty()) {
                locals[local.slot] = 1u;
                for (const auto pc : reads)
                    sources[pc] = local.slot;
            }
        }
    }

    core::pmr::Vector<int> sources;
    core::pmr::Vector<std::uint8_t> locals;
};

// Retain real container nodes only inside a call-free, nonescaping function.
// Every ordinary assignment invalidates all sites for that owner before replay.
class NativeConstructionPlan final {
public:
    NativeConstructionPlan(const core::pmr::Vector<const TypedIrOperationRecord*>& operations,
        const NativeIndexReadPlan& reads)
        : owners(operations.size(), -1, &core::defaultCpuMemoryResource())
    {
        for (std::size_t pc = 0; pc + 1u < operations.size(); ++pc) {
            const auto opcode = operations[pc]->opcode;
            const auto& store = *operations[pc + 1u];
            if ((opcode == TypedIrOpcode::MakeList || opcode == TypedIrOpcode::MakeDict)
                && store.opcode == TypedIrOpcode::StoreLocal && store.a >= 0
                && static_cast<std::size_t>(store.a) < reads.locals.size()
                && reads.locals[store.a] != 0u && privateIndexedLocal(operations, store.a))
                owners[pc] = store.a;
        }
    }

    void emitReset(String& source, int owner) const
    {
        for (std::size_t pc = 0; pc < owners.size(); ++pc) {
            if (owners[pc] != owner)
                continue;
            source += "frame->constructionBorrow_";
            source += std::to_string(pc);
            source += ".reset(); frame->constructionReady_";
            source += std::to_string(pc);
            source += " = false; ";
        }
    }

    core::pmr::Vector<int> owners;
};

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
    const bool localBudget =
        std::none_of(operations.begin(), operations.end(), [](const auto* operation) {
            return operation->opcode == TypedIrOpcode::Call;
        });
    source += "static bool ";
    source += nativeSymbol;
    source +=
        "(DirectNativeExecutionContext& context, const std::int64_t* args, std::size_t argCount, std::int64_t* result, VmError* error) {\n";
    source += "    if (argCount != ";
    source += std::to_string(function.parameterCount);
    source +=
        "u || (argCount != 0u && args == nullptr) || result == nullptr) { if (error != nullptr) { error->code = ScriptDiagnosticCode::WrongArgCount; error->message = \"DirectNative scalar argument count mismatch\"; } return false; }\n";
    source += "    if (!context.enterNativeFunction(";
    source += quote(function.id);
    source += ", error)) return false;\n";
    source +=
        "    struct Scope { DirectNativeExecutionContext& context; ~Scope() { context.leaveNativeFunction(); } } scope { context };\n";
    if (localBudget)
        source +=
            "std::uint64_t nativeBudget = context.remainingInstructions; struct BudgetScope { DirectNativeExecutionContext& context; std::uint64_t& remaining; ~BudgetScope() { context.remainingInstructions = remaining; } } budgetScope { context, nativeBudget };\n";
    source += "    int locals[";
    source += std::to_string(std::max(1, function.localCount));
    source += "]{};\n";
    source += "    std::uint8_t localKinds[";
    source += std::to_string(std::max(1, function.localCount));
    source += "]{};\n";
    source += "    int stack[";
    source += std::to_string(std::max(1, function.maxStack));
    source += "]{};\n";
    source += "    std::uint8_t stackKinds[";
    source += std::to_string(std::max(1, function.maxStack));
    source += "]{};\n";
    source += "    std::size_t sp = 0u;\n";
    for (int index = 0; index < function.parameterCount; ++index) {
        source += "    locals[";
        source += std::to_string(index);
        source += "] = static_cast<int>(args[";
        source += std::to_string(index);
        source += "]);\n";
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
        const char* counter = localBudget ? "nativeBudget" : "context.remainingInstructions";
        source += "        if (";
        source += counter;
        source +=
            " == 0u) { if (error != nullptr) { error->code = ScriptDiagnosticCode::ExecutionBudgetExceeded; error->message = \"DirectNative instruction budget exceeded\"; } return false; } --";
        source += counter;
        source += ";\n";
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
                "        { const int rhs = static_cast<int>(stack[--sp]); const int lhs = static_cast<int>(stack[--sp]); const auto value = ScriptSemanticRuntime::intBinary<SemanticBinaryOperation::";
            source += binaryOperation(opcode);
            source +=
                ">(lhs, rhs); if (!value.ok()) { if (error) { error->code = value.code; error->message = value.message; } return false; } stack[sp] = value.value.integer(); stackKinds[sp++] = 0u; }\n";
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
            source += "        { const std::int64_t value = static_cast<std::int64_t>(locals[";
            source += std::to_string(operation.a);
            if (opcode == TypedIrOpcode::LocalAddLocal) {
                source += "]) + locals[";
                source += std::to_string(operation.b);
                source += "];";
            } else {
                source += "]) ";
                source += opcode == TypedIrOpcode::LocalAddIntConst ? "+" : "-";
                source += " ";
                source += std::to_string(operation.i64);
                source += ";";
            }
            source +=
                " if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max()) { if (error != nullptr) { error->code = ScriptDiagnosticCode::IntegerOverflow; error->message = \"integer arithmetic overflow\"; } return false; } locals[";
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

void emitFailureCheck(String& source, const char* resultName, bool collectionStep = false)
{
    source += "        if (!";
    source += resultName;
    source += ".ok()) { if (error != nullptr) { error->code = ";
    source += resultName;
    if (collectionStep) {
        source +=
            ".code; error->message = \"range over non-iterable\"; } goto aot_exception_dispatch; }\n";
    } else {
        source += ".diagnostic.code; error->message = ";
        source += resultName;
        source += ".diagnostic.message; } goto aot_exception_dispatch; }\n";
    }
}

void emitLeafCallResult(String& source,
    const String& symbol,
    const char* arguments,
    const char* captures,
    std::size_t nextPc)
{
    source += "ExecutionSlot leafResult; if (";
    source += symbol;
    source += "_slot_leaf(context, ";
    source += arguments;
    source += ", ";
    source += captures;
    source +=
        ", leafResult, error)) { if (error != nullptr && error->code != ScriptDiagnosticCode::None) goto aot_exception_dispatch; for (std::size_t index = leafBase; index < sp; ++index) stack[index] = ScriptScalar(); sp = leafBase; stack[sp++] = std::move(leafResult); goto aot_pc_";
    source += std::to_string(nextPc);
    source += "; } ";
}

const char* pristineLeafCallGate()
{
    return "context.operationBoundary == nullptr && frame == &localFrame && frame->childPc == 0xffffffffu && frame->childResume.frame == nullptr && frame->childResume.pc == 0u && frame->childResume.commitState == 0u && !frame->childResume.ownershipTransferred && frame->childResume.injectionKind == DirectNativeInjectionKindV5::None && frame->childResume.suspendKind == DirectNativeSuspendKindV5::None && !frame->childResume.awaitInjectionReady && !frame->childResume.awaitInjectionSupported";
}

class NativeMethodLeafSequence final {
public:
    static std::size_t emit(String& source,
        const TypedIrModule& module,
        const core::pmr::Vector<const TypedIrOperationRecord*>& operations,
        const core::pmr::Vector<std::uint8_t>& targets,
        std::size_t pc,
        const core::pmr::Vector<std::uint8_t>& leaves,
        const core::pmr::Vector<String>& symbols)
    {
        if (pc + 2u >= operations.size() || operations[pc]->opcode != TypedIrOpcode::LoadLocal
            || operations[pc + 1u]->opcode != TypedIrOpcode::MakeMethodValue
            || operations[pc + 2u]->opcode != TypedIrOpcode::CallValue || targets[pc + 1u]
            || targets[pc + 2u])
            return pc;
        const auto& call = *operations[pc + 2u];
        if (call.closureCaptureCount != kScriptMethodProvenanceCaptureCount || call.text.empty())
            return pc;
        auto candidates = nativeMethodCandidates(module, call);
        candidates.erase(
            std::remove_if(candidates.begin(),
                candidates.end(),
                [&](const auto& candidate) {
                    if (!leaves[candidate.functionIndex])
                        return true;
                    const auto code = flatten(*candidate.function);
                    // If a leaf never reads self, dispatch needs the real receiver but
                    // the expression does not need a second owning copy of its carrier.
                    return std::any_of(code.begin(), code.end(), [](const auto* operation) {
                        return (operation->opcode == TypedIrOpcode::LoadLocal
                                   || operation->opcode == TypedIrOpcode::AddrLocal)
                            && operation->a == 0;
                    });
                }),
            candidates.end());
        if (candidates.empty())
            return pc;
        source += "    if (";
        source += pristineLeafCallGate();
        source += " && context.remainingInstructions >= 3u) { const Value& receiver = locals[";
        source += std::to_string(operations[pc]->a);
        source +=
            "].boxed(); const auto* instance = checkedValuePayload<arp::reflection::instance>(receiver); if (instance && instance->alive() && instance->class_()) { const auto& actualClass = instance->class_()->name(); constexpr std::size_t count = ";
        source += std::to_string(call.b);
        source +=
            "u; const std::size_t leafBase = sp - count; std::array<ExecutionSlot, count + 1u> leafArgs {}; for (std::size_t index = 0; index < count; ++index) leafArgs[index + 1u] = stack[leafBase + index]; ";
        for (const auto& candidate : candidates) {
            source += "if (actualClass == ";
            source += quote(candidate.schema->className);
            source += ") { context.remainingInstructions -= 3u; ";
            emitLeafCallResult(
                source, symbols[candidate.functionIndex], "leafArgs.data()", "nullptr", pc + 3u);
            source += "context.remainingInstructions += 3u; } ";
        }
        source += "} }\n";
        return pc + 3u;
    }
};

class NativeClosureLeafSequence final {
public:
    static std::size_t emit(String& source,
        const TypedIrModule& module,
        const core::pmr::Vector<const TypedIrOperationRecord*>& operations,
        const core::pmr::Vector<std::uint8_t>& targets,
        std::size_t pc,
        const core::pmr::Vector<std::uint8_t>& leaves,
        const core::pmr::Vector<String>& symbols)
    {
        if (pc + 1u >= operations.size() || operations[pc]->opcode != TypedIrOpcode::LoadLocal
            || operations[pc + 1u]->opcode != TypedIrOpcode::CallValue || targets[pc + 1u])
            return pc;
        const auto& call = *operations[pc + 1u];
        if (call.closureTargetFunctionId.empty()
            || call.closureCaptureCount == kScriptMethodProvenanceCaptureCount)
            return pc;
        const int target = resolveFunctionIndex(module, call.closureTargetFunctionId);
        if (target < 0 || !leaves[target])
            return pc;
        source += "if (";
        source += pristineLeafCallGate();
        source += " && context.remainingInstructions >= 2u) { const auto* closure = locals[";
        source += std::to_string(operations[pc]->a);
        source += "].closure(); if (closure && closure->captures.size() == ";
        source += std::to_string(call.closureCaptureCount);
        source += "u) { constexpr std::size_t count = ";
        source += std::to_string(call.b);
        source +=
            "u; const std::size_t leafBase = sp - count; context.remainingInstructions -= 2u; ";
        emitLeafCallResult(
            source, symbols[target], "stack.data() + leafBase", "&closure->captures", pc + 2u);
        source += "context.remainingInstructions += 2u; } }\n";
        return pc + 2u;
    }
};

// Promote scalars across an entire natural loop only when every operation can
// be lowered without callbacks, mutation of carriers, handlers or suspension.
// A failed proof leaves the original generated operations intact.
class NativeScalarLoop final {
public:
    static bool emit(String& source,
        const TypedIrModule& module,
        const TypedIrFunction& function,
        const core::pmr::Vector<const TypedIrOperationRecord*>& operations,
        const core::pmr::Vector<std::uint8_t>& targets,
        const core::pmr::Vector<int>& indexedSources,
        const core::pmr::Vector<std::uint8_t>& leaves,
        const core::pmr::Vector<String>& symbols,
        std::size_t header)
    {
        if (function.isGenerator || function.captureCount != 0
            || !function.exceptionRegions.empty())
            return false;
        std::size_t last = header;
        for (std::size_t pc = header + 1u; pc < operations.size(); ++pc)
            if (operations[pc]->opcode == TypedIrOpcode::Jump
                && operations[pc]->a == static_cast<int>(header))
                last = pc;
        if (last == header)
            return false;
        for (std::size_t pc = 0; pc < operations.size(); ++pc) {
            if (pc >= header && pc <= last)
                continue;
            const auto& operation = *operations[pc];
            if ((typedIrOperationDescriptor(operation.opcode).conditionalBranch
                    || operation.opcode == TypedIrOpcode::Jump)
                && operation.a > static_cast<int>(header) && operation.a <= static_cast<int>(last))
                return false;
        }
        NativeLoopScalarBindings bindings(function);
        for (std::size_t pc = header + 2u; pc <= last; ++pc) {
            if (operations[pc]->opcode == TypedIrOpcode::CallValue
                && operations[pc]->closureCaptureCount == kScriptMethodProvenanceCaptureCount
                && operations[pc - 1u]->opcode == TypedIrOpcode::MakeMethodValue
                && operations[pc - 2u]->opcode == TypedIrOpcode::LoadLocal) {
                const int receiver = operations[pc - 2u]->a;
                if (std::any_of(operations.begin() + header,
                        operations.begin() + last + 1u,
                        [receiver](const auto* operation) {
                            return operation->opcode == TypedIrOpcode::StoreLocal
                                && operation->a == receiver;
                        }))
                    return false;
                bindings.methodSites.emplace_back(pc, receiver);
            }
        }
        core::pmr::Vector<int> stringDestinations{&core::defaultCpuMemoryResource()};
        for (std::size_t pc = header; pc <= last; ++pc) {
            const auto& operation = *operations[pc];
            if (operation.opcode != TypedIrOpcode::LoadLocal
                && operation.opcode != TypedIrOpcode::StoreLocal)
                continue;
            if (operation.opcode == TypedIrOpcode::LoadLocal && pc + 1u < operations.size()
                && (operations[pc + 1u]->opcode == TypedIrOpcode::CallValue
                    || operations[pc + 1u]->opcode == TypedIrOpcode::MakeMethodValue))
                continue;
            const auto local = std::find_if(
                function.locals.begin(), function.locals.end(), [&](const auto& candidate) {
                    return candidate.slot == operation.a;
                });
            if (local == function.locals.end() || local->type.nullable)
                return false;
            const auto kind =
                local->type.kind == TypedIrType::Any ? TypedIrType::Int : local->type.kind;
            if (kind == TypedIrType::Int || kind == TypedIrType::Bool
                || kind == TypedIrType::Float) {
                bindings.kinds[operation.a] = kind;
                if (operation.opcode == TypedIrOpcode::StoreLocal)
                    bindings.writes[operation.a] = 1u;
            }
        }
        // Collection elements keep their original carriers. They are guarded
        // at each expression use instead of being cached across iterator steps.
        for (std::size_t pc = header; pc <= last; ++pc) {
            if (operations[pc]->opcode != TypedIrOpcode::RangeNext)
                continue;
            const auto packed = static_cast<std::uint64_t>(operations[pc]->i64);
            const int key = static_cast<std::int32_t>(packed >> 32u);
            const int value = static_cast<std::int32_t>(packed);
            if (key >= 0)
                bindings.kinds[key] = TypedIrType::Any;
            if (value >= 0)
                bindings.kinds[value] = TypedIrType::Any;
        }
        String body;
        bool quotaReplayRequired = false;
        const bool localBudget = std::none_of(
            operations.begin() + header, operations.begin() + last + 1u, [](const auto* operation) {
                return operation->opcode == TypedIrOpcode::Call
                    || operation->opcode == TypedIrOpcode::CallValue;
            });
        bool iterationBudget = localBudget;
        for (std::size_t pc = header; pc <= last && iterationBudget; ++pc) {
            const auto& operation = *operations[pc];
            if ((operation.opcode == TypedIrOpcode::Jump
                    || typedIrOperationDescriptor(operation.opcode).conditionalBranch)
                && operation.a > static_cast<int>(header) && operation.a <= static_cast<int>(pc))
                iterationBudget = false;
        }
        for (std::size_t pc = header; pc <= last;) {
            body += "aot_pc_";
            body += std::to_string(pc);
            body += ":;\n";
            if (pc == header && iterationBudget) {
                body += "if (context.remainingInstructions < ";
                body += std::to_string(last - header + 1u);
                body += "u) goto aot_replay_pc_";
                body += std::to_string(header);
                body += ";\n";
            }
            if (NativeIndexUpdateSpan::matches(operations, pc) && pc + 7u <= last
                && indexedSources[pc + 4u] == operations[pc]->a
                && bindings.contains(operations[pc + 1u]->a)
                && std::none_of(
                    targets.begin() + pc + 1u, targets.begin() + pc + 8u, [](auto entry) {
                        return entry != 0u;
                    })) {
                NativeIndexUpdateSpan::emit(body,
                    operations,
                    pc,
                    true,
                    bindings.writes[operations[pc + 1u]->a]
                        ? NativeLoopScalarBindings::local(operations[pc + 1u]->a)
                        : NativeLoopScalarBindings::operand(operations[pc + 1u]->a),
                    true,
                    iterationBudget,
                    bindings.writes[operations[pc + 1u]->a] != 0u);
                quotaReplayRequired = true;
                pc += 8u;
            } else if (pc + 3u <= last && operations[pc]->opcode == TypedIrOpcode::LoadLocal
                && operations[pc + 1u]->opcode == TypedIrOpcode::LoadString
                && operations[pc + 2u]->opcode == TypedIrOpcode::Add
                && operations[pc + 3u]->opcode == TypedIrOpcode::StoreLocal && !targets[pc + 1u]
                && !targets[pc + 2u] && !targets[pc + 3u]
                && privateStringLocal(operations, operations[pc + 3u]->a)) {
                const int destination = operations[pc + 3u]->a;
                quotaReplayRequired = true;
                const int input = operations[pc]->a;
                if (std::find(stringDestinations.begin(), stringDestinations.end(), destination)
                    == stringDestinations.end())
                    stringDestinations.push_back(destination);
                const auto site = std::to_string(destination);
                if (!iterationBudget) {
                    body += "if (context.remainingInstructions < 4u) goto aot_replay_pc_";
                    body += std::to_string(pc);
                    body += "; ";
                }
                body += "{ ";
                if (input != destination) {
                    // An independent prefix must remain bound to the same
                    // immutable carrier for the duration of this region.
                    for (std::size_t check = header; check <= last; ++check)
                        if (operations[check]->opcode == TypedIrOpcode::StoreLocal
                            && operations[check]->a == input)
                            return false;
                    body += "std::string_view prefix; if (!locals[";
                    body += std::to_string(input);
                    body += "].stringView(prefix)) goto aot_replay_pc_";
                    body += std::to_string(pc);
                    body += "; if (!native_string_" + site + ".assign(prefix, ";
                } else
                    body += "if (!native_string_" + site + ".append(";
                body += quote(operations[pc + 1u]->text);
                body += ")) goto aot_replay_pc_";
                body += std::to_string(pc);
                body += "; } context.remainingInstructions -= 4u; goto aot_pc_";
                body += std::to_string(pc + 4u);
                body += ";\n";
                pc += 4u;
            } else if (operations[pc]->opcode == TypedIrOpcode::RangeNext
                && !operations[pc]->delegatedGeneratorRange && pc + 1u <= last
                && operations[pc + 1u]->opcode == TypedIrOpcode::JumpIfFalse && !targets[pc + 1u]) {
                const auto& operation = *operations[pc];
                const auto packed = static_cast<std::uint64_t>(operation.i64);
                const int key = static_cast<std::int32_t>(packed >> 32u);
                const int value = static_cast<std::int32_t>(packed);
                if (!iterationBudget) {
                    body += "if (context.remainingInstructions < 2u) goto aot_replay_pc_";
                    body += std::to_string(pc);
                    body += "; ";
                }
                body += "{ int index = 0; if (!DirectNativeSlotOperations::integer(locals[";
                body += std::to_string(operation.b);
                body +=
                    "], index) || index == std::numeric_limits<int>::max()) goto aot_replay_pc_";
                body += std::to_string(pc);
                body += "; ++index; locals[";
                body += std::to_string(operation.b);
                body +=
                    "] = ScriptScalar(index); --context.remainingInstructions; auto step = ScriptSemanticRuntime::rangeNextCollectionStep(locals[";
                body += std::to_string(operation.a);
                body +=
                    "], index); if (!step.ok()) { if (error) { error->code = step.code; error->message = \"range over non-iterable\"; } goto aot_exception_dispatch; } ";
                if (key >= 0) {
                    body += "if (step.hasMore) locals[";
                    body += std::to_string(key);
                    body += "] = std::move(step.key); ";
                }
                if (value >= 0) {
                    body += "if (step.hasMore) locals[";
                    body += std::to_string(value);
                    body += "] = std::move(step.value); ";
                }
                body += "--context.remainingInstructions; if (!step.hasMore) goto aot_pc_";
                body += std::to_string(operations[pc + 1u]->a);
                body += "; } goto aot_pc_";
                body += std::to_string(pc + 2u);
                body += ";\n";
                pc += 2u;
            } else if (operations[pc]->opcode == TypedIrOpcode::Jump) {
                if (!iterationBudget) {
                    body += "if (context.remainingInstructions == 0u) goto aot_replay_pc_";
                    body += std::to_string(pc);
                    body += "; ";
                }
                body += "--context.remainingInstructions; goto aot_pc_";
                body += std::to_string(operations[pc]->a);
                body += ";\n";
                ++pc;
            } else if (const auto next = NativeMethodLeafSequence::emit(
                           body, module, operations, targets, pc, leaves, symbols);
                next != pc) {
                body += "goto aot_replay_pc_";
                body += std::to_string(pc);
                body += ";\n";
                pc = next;
            } else if (const auto next = NativeClosureLeafSequence::emit(
                           body, module, operations, targets, pc, leaves, symbols);
                next != pc) {
                body += "goto aot_replay_pc_";
                body += std::to_string(pc);
                body += ";\n";
                pc = next;
            } else {
                NativeScalarSequence sequence(function,
                    operations,
                    targets,
                    pc,
                    false,
                    &indexedSources,
                    &bindings,
                    &module,
                    iterationBudget);
                const auto scalarEnd = sequence.emit(body);
                if (scalarEnd == pc || scalarEnd > last + 1u)
                    return false;
                pc = scalarEnd;
            }
        }
        String prefix("native_loop_", &core::defaultCpuMemoryResource());
        prefix += std::to_string(header);
        prefix += "_";
        // Rewrite generated transfers, retaining distinct canonical replay
        // exits even when the failed operation is inside the promoted region.
        core::pmr::Vector<String> exits{&core::defaultCpuMemoryResource()};
        for (std::size_t pos = 0; pos < body.size();) {
            if (body[pos] == '"') {
                ++pos;
                while (pos < body.size()) {
                    if (body[pos++] == '\\' && pos < body.size())
                        ++pos;
                    else if (body[pos - 1u] == '"')
                        break;
                }
                continue;
            }
            if (localBudget && body.compare(pos, 29u, "context.remainingInstructions") == 0) {
                body.replace(pos, 29u, "native_loop_remaining");
                pos += 21u;
                continue;
            }
            if (body.compare(pos, 7u, "aot_pc_") != 0
                && body.compare(pos, 14u, "aot_replay_pc_") != 0
                && body.compare(pos, 22u, "aot_exception_dispatch") != 0) {
                ++pos;
                continue;
            }
            const auto found = pos;
            auto end = found;
            while (end < body.size()
                && (std::isalnum(static_cast<unsigned char>(body[end])) || body[end] == '_'))
                ++end;
            const String label = body.substr(found, end - found);
            bool internal = false;
            if (label.compare(0u, 7u, "aot_pc_") == 0) {
                const auto pc = static_cast<std::size_t>(std::stoull(label.substr(7u).c_str()));
                internal = pc >= header && pc <= last;
            }
            if (!internal && std::find(exits.begin(), exits.end(), label) == exits.end())
                exits.push_back(label);
            const String replacement = prefix + label;
            body.replace(found, end - found, replacement);
            pos = found + replacement.size();
        }
        source += "if (context.operationBoundary == nullptr && frame == &localFrame";
        if (quotaReplayRequired)
            source += " && !arp::core::currentCpuMemoryQuota()";
        source += ") {\n";
        String condition;
        for (const auto& site : bindings.methodSites) {
            const auto id = std::to_string(site.first);
            source += "std::size_t native_loop_method_";
            source += id;
            source +=
                " = 0u; { const auto* instance = exactValuePayload<arp::reflection::instance>(locals[";
            source += std::to_string(site.second);
            source +=
                "].boxed()); if (instance && instance->alive() && instance->class_()) { const auto& actualClass = instance->class_()->name(); ";
            std::size_t selector = 0u;
            for (const auto& candidate : nativeMethodCandidates(module, *operations[site.first])) {
                ++selector;
                source += "if (actualClass == ";
                source += quote(candidate.schema->className);
                source += ") native_loop_method_";
                source += id;
                source += " = ";
                source += std::to_string(selector);
                source += "u; ";
            }
            source += "} }\n";
            if (!condition.empty())
                condition += " && ";
            condition += "native_loop_method_";
            condition += id;
            condition += " != 0u";
        }
        for (const auto destination : stringDestinations) {
            const auto id = std::to_string(destination);
            source += "auto native_string_" + id
                + " = ScriptSemanticRuntime::UniqueStringAccess::borrow(locals[" + id + "]);\n";
            if (!condition.empty())
                condition += " && ";
            condition += "static_cast<bool>(native_string_" + id + ")";
        }
        for (std::size_t slot = 0; slot < bindings.kinds.size(); ++slot) {
            if (!bindings.contains(static_cast<int>(slot)))
                continue;
            const auto id = std::to_string(slot);
            const char* type = bindings.kinds[slot] == TypedIrType::Float ? "double"
                : bindings.kinds[slot] == TypedIrType::Bool               ? "bool"
                                                                          : "int";
            source += "const auto* input_";
            source += id;
            source += " = locals[";
            source += id;
            source += "].scalarPointer<";
            source += type;
            source += ">();\n";
            if (!condition.empty())
                condition += " && ";
            condition += "input_";
            condition += id;
            condition += " != nullptr";
            if (bindings.writes[slot]) {
                condition += " && locals[";
                condition += id;
                condition += "].inlineScalar() != nullptr";
            }
        }
        source += "if (";
        source += condition.empty() ? "true" : condition;
        source += ") {\n";
        for (std::size_t pc = header + 1u; pc <= last; ++pc) {
            if (operations[pc]->opcode != TypedIrOpcode::LoadIndex
                || operations[pc - 1u]->opcode != TypedIrOpcode::LoadIndex)
                continue;
            source += "ScriptSemanticRuntime::StableStringIndexBorrow native_string_index_";
            source += std::to_string(pc);
            source += "(frame->indexBorrow_";
            source += std::to_string(indexedSources[pc]);
            source += ");\n";
        }
        if (localBudget) {
            source +=
                "std::uint64_t native_loop_remaining = context.remainingInstructions; struct NativeLoopBudgetScope { DirectNativeExecutionContext& context; std::uint64_t& remaining; ~NativeLoopBudgetScope() { context.remainingInstructions = remaining; } } native_loop_budget_scope { context, native_loop_remaining };\n";
        }
        for (std::size_t slot = 0; slot < bindings.kinds.size(); ++slot) {
            if (!bindings.contains(static_cast<int>(slot)))
                continue;
            source += bindings.kinds[slot] == TypedIrType::Float ? "double "
                : bindings.kinds[slot] == TypedIrType::Bool      ? "bool "
                                                                 : "int ";
            source += NativeLoopScalarBindings::local(static_cast<int>(slot));
            source += " = *input_";
            source += std::to_string(slot);
            source += "; bool ";
            source += NativeLoopScalarBindings::dirty(static_cast<int>(slot));
            source += " = false;\n";
        }
        source += body;
        for (const auto& label : exits) {
            source += prefix + label;
            source += ":; ";
            bindings.publish(source);
            source += "goto ";
            if (label.compare(0u, 14u, "aot_replay_pc_") == 0) {
                const auto pc = label.substr(14u);
                if (pc == std::to_string(header).c_str()) {
                    source += prefix + "fallback";
                } else {
                    source += "aot_pc_";
                    source += pc;
                }
            } else
                source += label;
            source += ";\n";
        }
        source += "} }\n";
        source += prefix + "fallback:;\n";
        return true;
    }
};

void emitFunction(String& source,
    const TypedIrModule& module,
    std::size_t functionIndex,
    const core::pmr::Vector<String>& symbols,
    const core::pmr::Vector<String>& scalarSymbols,
    const core::pmr::Vector<std::uint8_t>& scalarCandidates,
    const core::pmr::Vector<std::uint8_t>& leafCandidates,
    const String& functionIdentity)
{
    const auto& function = module.functions[functionIndex];
    const auto operations = flatten(function);
    const NativeIndexReadPlan indexReads(function, operations);
    const NativeConstructionPlan constructions(operations, indexReads);
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
    const bool hasDictionaryConstruction =
        std::any_of(operations.begin(), operations.end(), [](const auto* operation) {
            return operation->opcode == TypedIrOpcode::MakeDict;
        });
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
    if (leafCandidates[functionIndex] != 0u) {
        source +=
            "    if (context.operationBoundary == nullptr && (suppliedResume == nullptr || (suppliedResume->frame == nullptr && suppliedResume->pc == 0u && suppliedResume->commitState == 0u && !suppliedResume->ownershipTransferred && suppliedResume->injectionKind == DirectNativeInjectionKindV5::None && suppliedResume->suspendKind == DirectNativeSuspendKindV5::None && !suppliedResume->awaitInjectionReady && !suppliedResume->awaitInjectionSupported)) && argCount == ";
        source += std::to_string(function.parameterCount);
        source += "u && (argCount == 0u || args != nullptr)) { std::array<ExecutionSlot, ";
        source += std::to_string(function.parameterCount);
        source +=
            "> imports {}; for (std::size_t index = 0; index < argCount; ++index) imports[index] = args[index]; ExecutionSlot result; if (";
        source += symbols[functionIndex];
        source +=
            "_slot_leaf(context, imports.data(), context.activeCaptures, result, error)) return error != nullptr && error->code != ScriptDiagnosticCode::None ? makeNil() : result.takeValue(); }\n";
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
        "} void adoptContinuationFrameCharge(std::size_t bytes) noexcept { continuationChargeBytes = bytes; ownsContinuationCharge = true; } DirectNativeExecutionContext* owner = nullptr; std::size_t continuationChargeBytes = sizeof(Frame); bool ownsContinuationCharge = false; std::array<ExecutionSlot, ";
    source += std::to_string(std::max(1, function.localCount));
    source += "> locals {}; std::array<ExecutionSlot, ";
    source += std::to_string(std::max(1, function.maxStack));
    source +=
        "> stack {}; std::size_t sp = 0u; struct Handler { std::uint32_t pc = 0u; std::size_t stackDepth = 0u; ScriptDiagnosticCode filter = ScriptDiagnosticCode::None; }; ";
    for (std::size_t slot = 0; slot < indexReads.locals.size(); ++slot) {
        if (indexReads.locals[slot] == 0u)
            continue;
        source += "ScriptSemanticRuntime::ReadonlyIndexBorrow indexBorrow_";
        source += std::to_string(slot);
        source += "; bool indexBorrowReady_";
        source += std::to_string(slot);
        source += " = false; ";
    }
    if (hasDictionaryConstruction)
        source +=
            "PrivateDictionaryWriteScratch constructionScratch { &arp::core::defaultCpuMemoryResource() }; ";
    for (std::size_t pc = 0; pc < constructions.owners.size(); ++pc) {
        if (constructions.owners[pc] < 0)
            continue;
        const auto site = std::to_string(pc);
        source += "PrivateDictionaryWriteScratch constructionTargets_";
        source += site;
        source +=
            " { &arp::core::defaultCpuMemoryResource() }; ScriptSemanticRuntime::PrivateConstructionBorrow constructionBorrow_";
        source += site;
        source += "; bool constructionReady_";
        source += site;
        source += " = false; ";
    }
    if (hasHandlerState)
        source +=
            "arp::core::pmr::Vector<Handler> handlers { &arp::core::defaultCpuMemoryResource() }; ";
    if (hasCaptureState)
        source +=
            "arp::core::pmr::Vector<std::shared_ptr<ScriptCell>> captures { &arp::core::defaultCpuMemoryResource() }; ";
    if (hasContinuationState)
        source +=
            "DirectNativeResumeStateV5 childResume; std::uint32_t childPc = 0xffffffffu; Value pendingValue = Value::undefined(); ";
    if (hasDelegatedSites)
        source += "Value delegatedChild = Value::undefined(); ";
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
        "    auto synchronizeFrameRoots = [&]() { ExecutionSlotValues localRoots(locals, 0u, locals.size(), true); ExecutionSlotValues stackRoots(stack, 0u, sp, true); ScriptValueSpan memoryRoots[] = {{localRoots.data(), locals.size()}, {stackRoots.data(), sp}";
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
    core::pmr::Vector<std::uint8_t> scalarEntryTargets(
        operations.size(), 0u, &core::defaultCpuMemoryResource());
    for (const auto* operation : operations) {
        const auto& descriptor = typedIrOperationDescriptor(operation->opcode);
        if ((descriptor.conditionalBranch || operation->opcode == TypedIrOpcode::Jump
                || operation->opcode == TypedIrOpcode::PushHandler)
            && operation->a >= 0 && static_cast<std::size_t>(operation->a) < operations.size())
            scalarEntryTargets[static_cast<std::size_t>(operation->a)] = 1u;
    }
    std::size_t nativeSequenceEnd = 0u;
    for (std::size_t pc = 0; pc < operations.size(); ++pc) {
        const auto& operation = *operations[pc];
        const auto opcode = operation.opcode;
        source += "aot_pc_";
        source += std::to_string(pc);
        source += ":\n";
        source +=
            "    if (context.operationBoundary != nullptr && !synchronizeFrameRoots()) return makeNil();\n";
        NativeScalarLoop::emit(source,
            module,
            function,
            operations,
            scalarEntryTargets,
            indexReads.sources,
            leafCandidates,
            symbols,
            pc);
        if (!hasHandlerState && !function.isGenerator
            && NativeIndexUpdateSpan::matches(operations, pc)
            && std::none_of(scalarEntryTargets.begin() + pc + 1u,
                scalarEntryTargets.begin() + pc + 8u,
                [](auto target) {
                    return target != 0u;
                })) {
            String index("locals[", &core::defaultCpuMemoryResource());
            index += std::to_string(operations[pc + 1u]->a);
            index += "]";
            NativeIndexUpdateSpan::emit(source,
                operations,
                pc,
                indexReads.locals[static_cast<std::size_t>(operation.a)] != 0u,
                index);
            nativeSequenceEnd = pc + 8u;
        }
        if (!hasHandlerState && !function.isGenerator && pc + 1u < operations.size()
            && (opcode == TypedIrOpcode::MakeList || opcode == TypedIrOpcode::MakeDict)
            && operations[pc + 1u]->opcode == TypedIrOpcode::StoreLocal
            && scalarEntryTargets[pc + 1u] == 0u
            && privateIndexedLocal(operations, operations[pc + 1u]->a)) {
            source +=
                "    if (context.operationBoundary == nullptr && frame == &localFrame && context.remainingInstructions >= 2u) { ExecutionSlotStackView operands(stack.data(), sp, stack.size()); ";
            const auto destination = operations[pc + 1u]->a;
            if (constructions.owners[pc] >= 0) {
                const auto site = std::to_string(pc);
                source += "if (!frame->constructionReady_";
                source += site;
                source += ") frame->constructionReady_";
                source += site;
                source += " = frame->constructionBorrow_";
                source += site;
                source += ".bind(locals[";
                source += std::to_string(destination);
                source += "], operands, ";
                source += std::to_string(operation.b);
                source += ", context.budget.maxContainerValues, ";
                source += opcode == TypedIrOpcode::MakeDict ? "true" : "false";
                source += ", frame->constructionTargets_";
                source += site;
                source += "); if (frame->constructionReady_";
                source += site;
                source += " && frame->constructionBorrow_";
                source += site;
                source += ".commit(operands)) { context.remainingInstructions -= 2u; goto aot_pc_";
                source += std::to_string(pc + 2u);
                source += "; } ";
            }
            constructions.emitReset(source, destination);
            source += "if (ScriptSemanticRuntime::";
            source +=
                opcode == TypedIrOpcode::MakeList ? "reuseUniqueList" : "reuseUniqueDictionary";
            source += "(locals[";
            source += std::to_string(operations[pc + 1u]->a);
            source += "], operands, ";
            source += std::to_string(operation.b);
            source += ", context.budget.maxContainerValues";
            if (opcode == TypedIrOpcode::MakeDict)
                source += ", frame->constructionScratch";
            source += ")) { ";
            if (indexReads.locals[static_cast<std::size_t>(destination)] != 0u) {
                source += "frame->indexBorrowReady_";
                source += std::to_string(destination);
                source += " = false; ";
            }
            source += "context.remainingInstructions -= 2u; goto aot_pc_";
            source += std::to_string(pc + 2u);
            source += "; } }\n";
            nativeSequenceEnd = pc + 2u;
        }
        if (!hasHandlerState && !function.isGenerator && pc + 3u < operations.size()
            && opcode == TypedIrOpcode::LoadLocal
            && operations[pc + 1u]->opcode == TypedIrOpcode::LoadString
            && operations[pc + 2u]->opcode == TypedIrOpcode::Add
            && operations[pc + 3u]->opcode == TypedIrOpcode::StoreLocal
            && scalarEntryTargets[pc + 1u] == 0u && scalarEntryTargets[pc + 2u] == 0u
            && scalarEntryTargets[pc + 3u] == 0u
            && privateStringLocal(operations, operations[pc + 3u]->a)) {
            const int destination = operations[pc + 3u]->a;
            source +=
                "    if (context.operationBoundary == nullptr && frame == &localFrame && context.remainingInstructions >= 4u) { ";
            if (destination != operation.a) {
                source += "std::string_view prefix; if (locals[";
                source += std::to_string(operation.a);
                source +=
                    "].stringView(prefix) && ScriptSemanticRuntime::assignUniqueString(locals[";
            } else {
                source += "if (ScriptSemanticRuntime::appendUniqueString(locals[";
            }
            source += std::to_string(destination);
            source += destination != operation.a ? "], prefix, " : "], ";
            source += quote(operations[pc + 1u]->text);
            source += ")) { context.remainingInstructions -= 4u; goto aot_pc_";
            source += std::to_string(pc + 4u);
            source += "; } }\n";
            nativeSequenceEnd = pc + 4u;
        }
        if (!function.isGenerator && pc >= nativeSequenceEnd) {
            nativeSequenceEnd = NativeMethodLeafSequence::emit(
                source, module, operations, scalarEntryTargets, pc, leafCandidates, symbols);
        }
        if (!function.isGenerator && pc >= nativeSequenceEnd) {
            nativeSequenceEnd = NativeClosureLeafSequence::emit(
                source, module, operations, scalarEntryTargets, pc, leafCandidates, symbols);
        }
        if (!function.isGenerator && pc >= nativeSequenceEnd) {
            NativeScalarSequence sequence(
                function, operations, scalarEntryTargets, pc, false, &indexReads.sources);
            nativeSequenceEnd = sequence.emit(source);
        }
        // Capture the logical state before the operation-boundary hook. The
        // generated frame is already persistent whenever a migration hook is
        // active, so this is the exact pre-operation state and never a native
        // instruction-pointer snapshot.
        source += "    if (frame != &localFrame) { frame->captureCanonicalState(";
        source += std::to_string(pc);
        source +=
            "u, materializeExecutionSlots(locals, 0u, locals.size()).data(), locals.size(), materializeExecutionSlots(stack, 0u, sp).data(), sp, ";
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
        if (opcode == TypedIrOpcode::Call
            || (opcode == TypedIrOpcode::CallValue && !operation.closureTargetFunctionId.empty()
                && (operation.closureCaptureCount != kScriptMethodProvenanceCaptureCount
                    || operation.text.empty()))) {
            const auto& targetId = opcode == TypedIrOpcode::Call
                ? operation.targetId
                : operation.closureTargetFunctionId;
            const int targetIndex = resolveFunctionIndex(module, targetId);
            if (targetIndex >= 0 && leafCandidates[static_cast<std::size_t>(targetIndex)] != 0u) {
                source += "    if (";
                source += pristineLeafCallGate();
                source += ") { constexpr std::size_t count = ";
                source += std::to_string(operation.b);
                source += "u; const std::size_t leafBase = sp - count";
                if (opcode == TypedIrOpcode::CallValue)
                    source += " - 1u";
                source += "; ";
                if (opcode == TypedIrOpcode::Call) {
                    emitLeafCallResult(source,
                        symbols[static_cast<std::size_t>(targetIndex)],
                        "stack.data() + leafBase",
                        "nullptr",
                        pc + 1u);
                } else if (operation.closureCaptureCount == kScriptMethodProvenanceCaptureCount) {
                    source +=
                        "const Value& receiver = stack[leafBase + count].boxed(); const auto* leafInstance = checkedValuePayload<arp::reflection::instance>(receiver); if (leafInstance != nullptr && leafInstance->alive()) { std::array<ExecutionSlot, count + 1u> leafArgs {}; leafArgs[0] = receiver; for (std::size_t index = 0; index < count; ++index) leafArgs[index + 1u] = stack[leafBase + index]; ";
                    emitLeafCallResult(source,
                        symbols[static_cast<std::size_t>(targetIndex)],
                        "leafArgs.data()",
                        "nullptr",
                        pc + 1u);
                    source += "} ";
                } else {
                    source +=
                        "const auto* closure = stack[leafBase + count].closure(); if (closure != nullptr && closure->captures.size() == ";
                    source += std::to_string(operation.closureCaptureCount);
                    source += "u) { ";
                    emitLeafCallResult(source,
                        symbols[static_cast<std::size_t>(targetIndex)],
                        "stack.data() + leafBase",
                        "&closure->captures",
                        pc + 1u);
                    source += "} ";
                }
                source += "}\n";
            }
        }
        if (opcode == TypedIrOpcode::LoadNil) {
            source += "    stack[sp++] = ScriptScalar();\n";
        } else if (opcode == TypedIrOpcode::LoadBool) {
            source += "    stack[sp++] = ScriptScalar(";
            source += operation.a ? "true" : "false";
            source += ");\n";
        } else if (opcode == TypedIrOpcode::LoadInt) {
            source += "    stack[sp++] = ScriptScalar(static_cast<int>(";
            source += std::to_string(operation.i64);
            source += "));\n";
        } else if (opcode == TypedIrOpcode::LoadInt64) {
            source += "    stack[sp++] = ScriptScalar(static_cast<std::int64_t>(";
            source += std::to_string(operation.i64);
            source += "LL));\n";
        } else if (opcode == TypedIrOpcode::LoadFloat) {
            source += "    { const std::uint64_t bits = ";
            source += std::to_string(static_cast<std::uint64_t>(operation.i64));
            source +=
                "ULL; double value = 0.0; std::memcpy(&value, &bits, sizeof(value)); stack[sp++] = ScriptScalar(value); }\n";
        } else if (opcode == TypedIrOpcode::LoadString) {
            source +=
                "    { static const auto literal = [] { arp::core::CpuMemoryQuotaScope literalScope(nullptr); return arp::core::pmr::String(";
            source += quote(operation.text);
            source +=
                ", &arp::core::defaultCpuMemoryResource()); }(); stack[sp++] = ExecutionSlot::borrowLiteral(literal); }\n";
        } else if (opcode == TypedIrOpcode::LoadLocal) {
            source += "    stack[sp++] = locals[";
            source += std::to_string(operation.a);
            source += "];\n";
        } else if (opcode == TypedIrOpcode::StoreLocal) {
            constructions.emitReset(source, operation.a);
            source += "    locals[";
            source += std::to_string(operation.a);
            source += "] = std::move(stack[--sp]);\n";
            if (indexReads.locals[static_cast<std::size_t>(operation.a)] != 0u) {
                source += "    frame->indexBorrowReady_";
                source += std::to_string(operation.a);
                source += " = false;\n";
            }
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
                "    { ExecutionSlot index = std::move(stack[--sp]); ExecutionSlot base = std::move(stack[--sp]); ";
            const int sourceSlot = indexReads.sources[pc];
            if (sourceSlot >= 0) {
                const auto slot = std::to_string(sourceSlot);
                source +=
                    "if (context.operationBoundary == nullptr && frame == &localFrame) { if (!frame->indexBorrowReady_";
                source += slot;
                source += ") frame->indexBorrowReady_";
                source += slot;
                source += " = frame->indexBorrow_";
                source += slot;
                source += ".bind(locals[";
                source += slot;
                source += "]); if (frame->indexBorrowReady_";
                source += slot;
                source += ") { ";
                const bool arithmeticNext = pc + 1u < operations.size()
                    && (!binaryOperation(operations[pc + 1u]->opcode).empty()
                        || !compareOperation(operations[pc + 1u]->opcode).empty());
                if (arithmeticNext) {
                    source += "int payload = 0; if (frame->indexBorrow_";
                    source += slot;
                    source +=
                        ".readInteger(index, payload)) { stack[sp++] = ScriptScalar(payload); goto aot_pc_";
                } else {
                    source += "if (const auto* value = frame->indexBorrow_";
                    source += slot;
                    source += ".read(index)) { stack[sp++] = *value; goto aot_pc_";
                }
                source += std::to_string(pc + 1u);
                source += "; } } } ";
            }
            source +=
                "auto semantic = ScriptSemanticRuntime::loadIndexSlots(base.boxed(), index);\n";
            emitFailureCheck(source, "semantic");
            source += "        stack[sp++] = std::move(semantic.value); }\n";
        } else if (opcode == TypedIrOpcode::StoreIndex) {
            // General writes may insert a key or invoke general conversion.
            // Rebind every borrowed relation before the next optimized read.
            for (std::size_t slot = 0; slot < indexReads.locals.size(); ++slot) {
                if (indexReads.locals[slot] == 0u)
                    continue;
                source += "    frame->indexBorrowReady_";
                source += std::to_string(slot);
                source += " = false;\n";
            }
            source +=
                "    { Value value = stack[--sp].takeValue(); Value index = stack[--sp].takeValue(); Value base = stack[--sp].takeValue(); context.prepareSideEffect(resume, ";
            source += std::to_string(pc);
            source +=
                "u); auto semantic = ScriptSemanticRuntime::storeIndex(std::move(base), std::move(index), std::move(value));\n";
            emitFailureCheck(source, "semantic");
            source += "        context.commitSideEffect(resume, ";
            source += std::to_string(pc);
            source += "u); }\n";
        } else if (opcode == TypedIrOpcode::MakeList || opcode == TypedIrOpcode::MakeDict) {
            source +=
                "    { ExecutionSlotStackView operands(stack.data(), sp, stack.size()); auto semantic = ScriptSemanticRuntime::";
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
                        " auto constructorArgs = ExecutionSlotValues<count + 1u>::emptyValues(); constructorArgs[0] = instance; for (std::size_t index = 0; index < count; ++index) constructorArgs[index + 1u] = stack[sp + index]; const Value constructorResult = ";
                    source += symbols[constructorIndex];
                    source +=
                        "(context, constructorArgs.data(), count + 1u, &frame->childResume, error); (void)constructorResult; if (error != nullptr && error->code != ScriptDiagnosticCode::None) return makeNil(); frame->childPc = 0xffffffffu; frame->pendingValue = Value::undefined();";
                }
                source += " stack[sp++] = std::move(instance); }\n";
            }
        } else if (opcode == TypedIrOpcode::RangeInit) {
            source += "    locals[";
            source += std::to_string(operation.a);
            source += "] = std::move(stack[--sp]); locals[";
            source += std::to_string(operation.b);
            source += "] = ScriptScalar(-1);\n";
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
                    "u; } VmError childError; Value yielded; if (resume.injectionKind == DirectNativeInjectionKindV5::Send) { yielded = sendDirectNativeGeneratorV5(*child, resume.injectionValue, &childError); } else if (resume.injectionKind == DirectNativeInjectionKindV5::Throw) { yielded = throwDirectNativeGeneratorV5(*child, resume.injectionValue, &childError); } else { yielded = resumeDirectNativeGeneratorV5(*child, &childError); } resume.injectionKind = DirectNativeInjectionKindV5::None; resume.injectionValue = makeNil(); if (childError.code == ScriptDiagnosticCode::Suspended) { int delegatedIndex = 0; if (DirectNativeSlotOperations::integer(locals[";
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
                    "u; resume.awaitInjectionSupported = child->state->resume.awaitInjectionSupported; resume.suspendKind = DirectNativeSuspendKindV5::Await; if (!synchronizeFrameRoots()) return makeNil(); if (error != nullptr) { *error = std::move(childError); } return makeNil(); } if (childError.code != ScriptDiagnosticCode::None) { if (error != nullptr) *error = std::move(childError); goto aot_exception_dispatch; } resume.awaitInjectionSupported = false; if (child->state->done) { frame->delegatedChild = makeNil(); frame->childPc = 0xffffffffu; stack[sp++] = ScriptScalar(false); } else { locals[";
                source += std::to_string(valueSlot);
                source +=
                    "] = std::move(yielded); stack[sp++] = ScriptScalar(true); } } else if (const auto* bridge = optionalGeneratorBridgeV3(); bridge != nullptr && bridge->isGenerator != nullptr && bridge->isGenerator(source)) { GeneratorIdentityV3 childIdentity; if (bridge->queryIdentity == nullptr || !bridge->queryIdentity(source, childIdentity) || !context.acceptsGeneratorIdentity(childIdentity)) { if (error != nullptr) { error->code = ScriptDiagnosticCode::AotSwitchUnsupported; error->message = \"DirectNative yield-from child artifact identity negotiation failed\"; } goto aot_exception_dispatch; } if (isNil(frame->delegatedChild)) { frame->delegatedChild = source; frame->childPc = ";
                source += std::to_string(pc);
                source +=
                    "u; } VmError childError; Value yielded; if (resume.injectionKind == DirectNativeInjectionKindV5::Send) { yielded = bridge->resumeSend(source, resume.injectionValue, &childError); } else if (resume.injectionKind == DirectNativeInjectionKindV5::Throw) { yielded = bridge->resumeThrow(source, resume.injectionValue, &childError); } else { yielded = bridge->resume(source, &childError); } resume.injectionKind = DirectNativeInjectionKindV5::None; resume.injectionValue = makeNil(); if (childError.code == ScriptDiagnosticCode::Suspended) { int delegatedIndex = 0; if (DirectNativeSlotOperations::integer(locals[";
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
                    "u; { const auto* capability = optionalGeneratorBridgeV3Capability(); resume.awaitInjectionSupported = capability != nullptr && capability->isGenerator != nullptr && capability->isGenerator(source) && capability->supportsPendingAwaitInjection != nullptr && capability->supportsPendingAwaitInjection(source); } resume.suspendKind = DirectNativeSuspendKindV5::Await; if (!synchronizeFrameRoots()) return makeNil(); if (error != nullptr) *error = std::move(childError); return makeNil(); } if (childError.code != ScriptDiagnosticCode::None) { if (error != nullptr) *error = std::move(childError); goto aot_exception_dispatch; } resume.awaitInjectionSupported = false; if (bridge->isDone != nullptr && bridge->isDone(source)) { frame->delegatedChild = makeNil(); frame->childPc = 0xffffffffu; stack[sp++] = ScriptScalar(false); } else { locals[";
                source += std::to_string(valueSlot);
                source +=
                    "] = std::move(yielded); stack[sp++] = ScriptScalar(true); } } else { if (resume.injectionKind != DirectNativeInjectionKindV5::None) { if (error != nullptr) { error->code = ScriptDiagnosticCode::AotSwitchUnsupported; error->message = \"DirectNative yield-from injection requires a DirectNative child\"; } resume.injectionKind = DirectNativeInjectionKindV5::None; resume.injectionValue = makeNil(); goto aot_exception_dispatch; } int index = 0; if (!DirectNativeSlotOperations::integer(locals[";
                source += std::to_string(operation.b);
                source +=
                    "], index)) { if (error != nullptr) { error->code = ScriptDiagnosticCode::BadCast; error->message = \"range index corrupted\"; } goto aot_exception_dispatch; } ++index; locals[";
                source += std::to_string(operation.b);
                source +=
                    "] = ScriptScalar(index); auto semantic = ScriptSemanticRuntime::rangeNextCollectionStep(source, index);\n";
                emitFailureCheck(source, "semantic", true);
                if (keySlot >= 0) {
                    source += "        if (semantic.hasMore) locals[";
                    source += std::to_string(keySlot);
                    source += "] = std::move(semantic.key);\n";
                }
                if (valueSlot >= 0) {
                    source += "        if (semantic.hasMore) locals[";
                    source += std::to_string(valueSlot);
                    source += "] = std::move(semantic.value);\n";
                }
                source += "        stack[sp++] = ScriptScalar(semantic.hasMore); } }\n";
            } else {
                source += "    { int index = 0; if (!DirectNativeSlotOperations::integer(locals[";
                source += std::to_string(operation.b);
                source +=
                    "], index)) { if (error != nullptr) { error->code = ScriptDiagnosticCode::BadCast; error->message = \"range index corrupted\"; } return makeNil(); } ++index; locals[";
                source += std::to_string(operation.b);
                source +=
                    "] = ScriptScalar(index); auto semantic = ScriptSemanticRuntime::rangeNextCollectionStep(locals[";
                source += std::to_string(operation.a);
                source += "], index);\n";
                emitFailureCheck(source, "semantic", true);
                if (keySlot >= 0) {
                    source += "        if (semantic.hasMore) locals[";
                    source += std::to_string(keySlot);
                    source += "] = std::move(semantic.key);\n";
                }
                if (valueSlot >= 0) {
                    source += "        if (semantic.hasMore) locals[";
                    source += std::to_string(valueSlot);
                    source += "] = std::move(semantic.value);\n";
                }
                source += "        stack[sp++] = ScriptScalar(semantic.hasMore); }\n";
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
            source +=
                "    { auto scalar = ScriptSemanticRuntime::lengthScalar(stack[--sp]); SemanticSlotOperationResult semantic; semantic.value = scalar.value; semantic.diagnostic.code = scalar.code; if (!scalar.ok()) semantic.diagnostic.message = scalar.message;\n";
            emitFailureCheck(source, "semantic");
            source += "        stack[sp++] = std::move(semantic.value); }\n";
        } else if (opcode == TypedIrOpcode::Pop) {
            source += "    --sp;\n";
        } else if (!binaryOperation(opcode).empty()) {
            source +=
                "    { ExecutionSlot rhs = std::move(stack[--sp]); ExecutionSlot lhs = std::move(stack[--sp]); auto semantic = DirectNativeSlotOperations::binary<SemanticBinaryOperation::";
            source += binaryOperation(opcode);
            source += ">(std::move(lhs), std::move(rhs));\n";
            emitFailureCheck(source, "semantic");
            source += "        stack[sp++] = std::move(semantic.value); }\n";
        } else if (!compareOperation(opcode).empty()) {
            source +=
                "    { ExecutionSlot rhs = std::move(stack[--sp]); ExecutionSlot lhs = std::move(stack[--sp]); auto semantic = DirectNativeSlotOperations::compare<SemanticCompareOperation::";
            source += compareOperation(opcode);
            source += ">(std::move(lhs), std::move(rhs));\n";
            emitFailureCheck(source, "semantic");
            source += "        stack[sp++] = std::move(semantic.value); }\n";
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
            source += "    { auto semantic = DirectNativeSlotOperations::condition(stack[--sp]);\n";
            emitFailureCheck(source, "semantic");
            source += "        const bool condition = *semantic.value.scalarPointer<bool>(); if (";
            source += opcode == TypedIrOpcode::JumpIfFalse ? "!condition" : "condition";
            source += ") goto aot_pc_";
            source += std::to_string(operation.a);
            source += "; }\n";
        } else if (opcode == TypedIrOpcode::JumpIfLocalIntLtConstFalse) {
            source += "    { int value = 0; if (!DirectNativeSlotOperations::integer(locals[";
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
                "    { auto semantic = DirectNativeSlotOperations::binary<SemanticBinaryOperation::";
            source += (opcode == TypedIrOpcode::LocalAddIntConst
                          || opcode == TypedIrOpcode::LocalAddInt64Const)
                ? "Add"
                : "Sub";
            source += ">(locals[";
            source += std::to_string(operation.a);
            source += "], ";
            source += (opcode == TypedIrOpcode::LocalAddInt64Const
                          || opcode == TypedIrOpcode::LocalSubInt64Const)
                ? "ExecutionSlot(ScriptScalar(static_cast<std::int64_t>("
                : "ExecutionSlot(ScriptScalar(static_cast<int>(";
            source += std::to_string(operation.i64);
            source += "))));\n";
            emitFailureCheck(source, "semantic");
            source += "        locals[";
            source += std::to_string(operation.a);
            source += "] = std::move(semantic.value); }\n";
        } else if (opcode == TypedIrOpcode::LocalAddLocal) {
            source +=
                "    { auto semantic = DirectNativeSlotOperations::binary<SemanticBinaryOperation::Add>(locals[";
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
                "ULL; double value = 0.0; std::memcpy(&value, &bits, sizeof(value)); auto semantic = DirectNativeSlotOperations::binary<SemanticBinaryOperation::";
            source += opcode == TypedIrOpcode::LocalAddFloatConst ? "Add" : "Sub";
            source += ">(locals[";
            source += std::to_string(operation.a);
            source += "], ExecutionSlot(ScriptScalar(value)));\n";
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
                    "(context, materializeExecutionSlots(stack, sp, count).data(), count, nullptr, error); if (error != nullptr && error->code != ScriptDiagnosticCode::None) goto aot_exception_dispatch; stack[sp++] = result; }\n";
            } else {
                source += "u; if (frame->childPc != ";
                source += std::to_string(pc);
                source += "u) { sp -= count; frame->childPc = ";
                source += std::to_string(pc);
                source += "u; } const Value result = ";
                source += symbols[targetIndex];
                source +=
                    "(context, materializeExecutionSlots(stack, sp, count).data(), count, &frame->childResume, error); if (error != nullptr && error->code != ScriptDiagnosticCode::None) { if (error->code == ScriptDiagnosticCode::Suspended) { if (!synchronizeFrameRoots()) return makeNil(); return result; } goto aot_exception_dispatch; } frame->childPc = 0xffffffffu; stack[sp++] = result; }\n";
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
            const auto candidates = nativeMethodCandidates(module, operation);
            const bool hasLeafCandidate = std::any_of(
                candidates.begin(), candidates.end(), [&leafCandidates](const auto& candidate) {
                    return leafCandidates[candidate.functionIndex] != 0u;
                });
            if (hasLeafCandidate) {
                source += "    if (";
                source += pristineLeafCallGate();
                source += ") { constexpr std::size_t count = ";
                source += std::to_string(operation.b);
                source +=
                    "u; const std::size_t leafBase = sp - count - 1u; const Value& receiver = stack[leafBase + count].boxed(); const auto* leafInstance = checkedValuePayload<arp::reflection::instance>(receiver); if (leafInstance != nullptr && leafInstance->alive() && leafInstance->class_() != nullptr) { const auto& leafClass = leafInstance->class_()->name(); std::array<ExecutionSlot, count + 1u> leafArgs {}; leafArgs[0] = receiver; for (std::size_t index = 0; index < count; ++index) leafArgs[index + 1u] = stack[leafBase + index]; ";
                for (const auto& candidate : candidates) {
                    if (leafCandidates[candidate.functionIndex] == 0u)
                        continue;
                    source += "if (leafClass == ";
                    source += quote(candidate.schema->className);
                    source += ") { ";
                    emitLeafCallResult(source,
                        symbols[candidate.functionIndex],
                        "leafArgs.data()",
                        "nullptr",
                        pc + 1u);
                    source += "} ";
                }
                source += "} }\n";
            }
            source += "    { constexpr std::size_t count = ";
            source += std::to_string(operation.b);
            source += "u; if (frame->childPc != ";
            source += std::to_string(pc);
            source += "u) { sp -= count + 1u; frame->childPc = ";
            source += std::to_string(pc);
            source +=
                "u; } const std::size_t base = sp; const Value& receiver = stack[base + count]; if (!receiver.is<arp::reflection::instance>() || !receiver.ptr<arp::reflection::instance>()->alive() || receiver.ptr<arp::reflection::instance>()->class_() == nullptr) { if (error != nullptr) { error->code = ScriptDiagnosticCode::AotArtifactInvalid; error->message = \"DirectNative dynamic method receiver is invalid\"; } return makeNil(); } const auto& actualClass = receiver.ptr<arp::reflection::instance>()->class_()->name(); auto callArgs = ExecutionSlotValues<count + 1u>::emptyValues(); callArgs[0] = receiver; for (std::size_t index = 0; index < count; ++index) callArgs[index + 1u] = stack[base + index]; Value result = makeNil();";
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
                    "u; } const std::size_t base = sp; const Value& receiver = stack[base + count]; if (!receiver.is<arp::reflection::instance>() || !receiver.ptr<arp::reflection::instance>()->alive()) { if (error != nullptr) { error->code = ScriptDiagnosticCode::AotArtifactInvalid; error->message = \"DirectNative method receiver is invalid\"; } return makeNil(); } auto callArgs = ExecutionSlotValues<count + 1u>::emptyValues(); callArgs[0] = receiver; for (std::size_t index = 0; index < count; ++index) callArgs[index + 1u] = stack[base + index]; const Value result = ";
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
                source += "(context, materializeExecutionSlots(stack, sp, count).data(), count, ";
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
                ", materializeExecutionSlots(stack, sp, count).data(), count, result, error)) goto aot_exception_dispatch; context.commitSideEffect(resume, ";
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
                source +=
                    "u, materializeExecutionSlots(locals, 0u, locals.size()).data(), locals.size(), materializeExecutionSlots(stack, 0u, sp).data(), sp, ";
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
                "    { const auto& raised = stack[--sp]; if (error != nullptr) { error->code = ScriptDiagnosticCode::UserRaise; std::string_view text; if (raised.stringView(text)) error->message.assign(text.data(), text.size()); else if (!tryGetString(raised.boxed(), error->message)) error->message = \"script raised a non-string value\"; } goto aot_exception_dispatch; }\n";
        } else if (opcode == TypedIrOpcode::Return || opcode == TypedIrOpcode::ReturnInt) {
            source += "    { Value result = ";
            source += (opcode == TypedIrOpcode::ReturnInt || operation.a == 1)
                ? "stack[--sp].takeValue()"
                : "makeNil()";
            source +=
                "; resume.pc = 0u; resume.commitState = 0u; resume.frame.reset(); return result; }\n";
        }
    }
    source += "aot_pc_";
    source += std::to_string(operations.size());
    source += ":\n";
    source +=
        "    if (error != nullptr) { error->code = ScriptDiagnosticCode::AotArtifactInvalid; error->message = \"DirectNative control flow fell through\"; } goto aot_terminal_cleanup;\n";
    source += "aot_exception_dispatch:\n";
    if (hasHandlerState) {
        String discardedCatch("false", &core::defaultCpuMemoryResource());
        for (std::size_t pc = 0; pc < operations.size(); ++pc) {
            const auto& store = *operations[pc];
            if (store.opcode != TypedIrOpcode::StoreLocal)
                continue;
            const bool unread =
                std::none_of(operations.begin(), operations.end(), [&](const auto* op) {
                    return (op->opcode == TypedIrOpcode::LoadLocal
                               || op->opcode == TypedIrOpcode::AddrLocal)
                        && op->a == store.a;
                });
            if (unread) {
                discardedCatch += " || handler.pc == ";
                discardedCatch += std::to_string(pc);
                discardedCatch += "u";
            }
        }
        // A dead catch binding has no observable carrier. Observer/canonical
        // frames still receive the real error string and full state.
        source +=
            "    while (!frame->handlers.empty() && error != nullptr && error->code != ScriptDiagnosticCode::None) { const auto handler = frame->handlers.back(); frame->handlers.pop_back(); if (handler.filter != ScriptDiagnosticCode::None && handler.filter != error->code) continue; sp = handler.stackDepth; if (frame == &localFrame && context.operationBoundary == nullptr && (";
        source += discardedCatch;
        source +=
            ")) stack[sp++] = ScriptScalar(); else stack[sp++] = makeString(error->message); error->traceback.clear(); error->code = ScriptDiagnosticCode::None; error->message.clear(); switch (handler.pc) {";
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
    artifactSeed += kDirectNativeLoweringRevisionV5;
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
        "\n#include \"scriptvm/runtime/semantic_runtime.h\"\n#include \"scriptvm/aot/native_slot_operations.h\"\n#include \"scriptvm/runtime/execution_slot_values.h\"\n#include <array>\n#include <cstring>\n#include <limits>\n#include <string_view>\n#include <utility>\n\nnamespace {\nusing namespace arp::scriptvm;\n";
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
    core::pmr::Vector<std::uint8_t> leafCandidates(
        module.functions.size(), 0u, &core::defaultCpuMemoryResource());
    for (std::size_t index = 0; index < module.functions.size(); ++index) {
        const auto& function = module.functions[index];
        const auto operations = flatten(function);
        core::pmr::Vector<std::uint8_t> targets(
            operations.size(), 0u, &core::defaultCpuMemoryResource());
        NativeScalarSequence sequence(function, operations, targets, 0u, true);
        if (sequence.emitLeaf(result.source, symbols[index], function, operations.size()))
            leafCandidates[index] = 1u;
    }
    for (std::size_t index = 0; index < module.functions.size(); ++index)
        emitFunction(result.source,
            module,
            index,
            symbols,
            scalarSymbols,
            scalarCandidates,
            leafCandidates,
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

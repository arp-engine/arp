#include "scriptvm/interpreter/bytecode.h"

#include "core/sha256.h"
#include "scriptvm/interpreter/bytecode_lowerer.h"
#include "scriptvm/ir/typed_ir.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <string_view>

namespace arp::scriptvm {

// Keep repeated spans out of the large dispatcher translation unit.
bool ScalarExpressionPlan::repeatIndexed(const Entry& prefix,
    ExecutionSlotBuffer& locals,
    std::int64_t& remaining,
    int& pc,
    int& lastPc) const
{
    assert(prefix.terminal == Terminal::IndexedLoop && prefix.rangeBody >= 0
        && prefix.conditionalTail >= 0);
    const int startPc = pc;
    const auto& updateEntry = entries[static_cast<std::size_t>(prefix.rangeBody)];
    const auto& tail = entries[static_cast<std::size_t>(prefix.conditionalTail)];
    assert(updateEntry.terminal == Terminal::IndexedStore && tail.nextPc == startPc);
    if (remaining >= 0 && remaining < prefix.cost)
        return false;
    const auto safeDestination = [&](int slot) {
        const auto& value = locals[static_cast<std::size_t>(slot)];
        if (const auto* scalar = value.inlineScalar())
            return scalar->ptr<int>() != nullptr;
        return value.is<void>() || (value.is<int>() && !value.boxed().is_native_reference());
    };
    // Replacing a native/owned object local could release a host callback;
    // such a destination must never be overwritten while topology is borrowed.
    if (!safeDestination(prefix.destination) || !safeDestination(tail.destination)
        || !safeDestination(tail.stepLocal))
        return false;
    ScriptSemanticRuntime::IntegerIndexBorrow borrow;
    if (!borrow.bind(locals[static_cast<std::size_t>(updateEntry.destination)]))
        return false;
    std::array<Node, 5> boundNodes;
    std::copy_n(nodes.data() + tail.offset, tail.count, boundNodes.data());
    std::array<int, 3> localSlots{};
    std::size_t sourceCount = 0;
    for (int i = 0; i != tail.count; ++i) {
        auto& node = boundNodes[static_cast<std::size_t>(i)];
        if (node.operation != Operation::Local && node.operation != Operation::Index)
            continue;
        localSlots[sourceCount] = node.operation == Operation::Index ? -1 : node.operand;
        node.operation = Operation::Local;
        node.operand = static_cast<int>(sourceCount++);
    }
    // Reuse the same precompiled checked evaluator with borrowed operands.
    // Tail eligibility was proved independently; no code is generated here.
    Entry handler = tail;
    specializeTyped<int, true>(handler);
    if (!handler.boundIntEvaluator)
        return false;
    int value = 0;
    BoundLoop tailLoop(*this, tail, locals, &value);
    if (!tailLoop.valid())
        return false;
    const auto affordable = [&](int cost) {
        return remaining < 0 || remaining >= cost;
    };
    const auto charge = [&](const Entry& entry) {
        if (remaining > 0)
            remaining -= entry.cost;
        lastPc = entry.lastPc;
        pc = entry.nextPc;
    };
    bool progressed = false;
    do {
        int index;
        if (!affordable(prefix.cost)
            || !prefix.evaluator(nodes.data() + prefix.offset, locals, index))
            break;
        charge(prefix);
        locals[static_cast<std::size_t>(prefix.destination)] = ScriptScalar(index);
        progressed = true;
        ScriptSemanticRuntime::IntegerIndexUpdate update;
        int source;
        const auto& indexSlot = locals[static_cast<std::size_t>(prefix.destination)];
        const auto* expression = nodes.data() + updateEntry.offset;
        if (!affordable(updateEntry.cost) || !update.bind(borrow, indexSlot, source)
            || !applyOperation(
                expression[1].operation, source, expression[0].operand, expression[0], value))
            break;
        charge(updateEntry);
        update.commit(value);
        // Read after commit: makeInt may return a host-modified cached payload,
        // and list read/write indices may name different actual destinations.
        BoundIntSlots sources;
        int element;
        bool valid = affordable(tail.cost);
        for (std::size_t i = 0; valid && i != sourceCount; ++i) {
            if (localSlots[i] < 0) {
                valid = update.readSource(element);
                sources.pointers[i] = &element;
            } else {
                sources.pointers[i] =
                    locals[static_cast<std::size_t>(localSlots[i])].scalarPointer<int>();
                valid = sources.pointers[i] != nullptr;
            }
        }
        int step;
        bool repeat;
        if (!valid || !handler.boundIntEvaluator(boundNodes.data(), sources, value)
            || !tailLoop.prepare(step, repeat))
            break;
        charge(tail);
        locals[static_cast<std::size_t>(tail.destination)] = ScriptScalar(value);
        locals[static_cast<std::size_t>(tail.stepLocal)] = ScriptScalar(step);
        if (!repeat)
            pc = entries[static_cast<std::size_t>(tail.loopCondition)].destination;
    } while (pc == startPc);
    // Partial progress is intentional. Destroy all borrowed nodes before the
    // VM replays from the current stage, never an already committed update.
    return progressed;
}

bool ScalarExpressionPlan::repeatInteger(const Entry& entry,
    ExecutionSlotBuffer& locals,
    std::int64_t& remaining,
    int& pc,
    int& lastPc) const
{
    if (std::any_of(nodes.begin() + entry.offset,
            nodes.begin() + entry.offset + entry.count,
            [](const Node& node) {
                return node.operation == Operation::StringKeyIndex;
            }))
        return repeatBorrowedInteger<StringKeyIndexAccess, Operation::StringKeyIndex>(
            entry, locals, remaining, pc, lastPc);
    return repeatBorrowedInteger<IntegerIndexAccess, Operation::Index>(
        entry, locals, remaining, pc, lastPc);
}

template <class Access, ScalarExpressionPlan::Operation Indexed>
bool ScalarExpressionPlan::repeatBorrowedInteger(const Entry& entry,
    ExecutionSlotBuffer& locals,
    std::int64_t& remaining,
    int& pc,
    int& lastPc) const
{
    assert(entry.boundIntEvaluator && entry.terminal == Terminal::Store && entry.loopCondition >= 0
        && entry.nextPc == pc && entry.stepLocal >= 0 && entry.leafFrameValues == 0);
    if (remaining >= 0 && remaining < entry.cost)
        return false;
    int* destination = inlineInteger(locals, entry.destination);
    int value = 0;
    BoundLoop loop(*this, entry, locals, &value);
    if (!destination || !loop.valid())
        return false;
    assert(entry.destination != entry.stepLocal && (entry.count == 3 || entry.count == 5));
    std::array<Node, 5> boundNodes;
    std::copy_n(nodes.data() + entry.offset, entry.count, boundNodes.data());
    BoundIntSlots sources;
    std::array<Access, 3> indices;
    std::array<std::size_t, 3> indexSources{};
    std::size_t indexCount = 0;
    std::size_t source = 0;
    for (int index = 0; index < entry.count; ++index) {
        auto& node = boundNodes[static_cast<std::size_t>(index)];
        if (node.operation == Indexed) {
            if (!indices[indexCount].bindReadonly(nodes[entry.offset + index], locals))
                return false;
            indexSources[indexCount++] = source;
            node.operation = Operation::Local;
        } else if (node.operation == Operation::Local) {
            const auto* pointer =
                locals[static_cast<std::size_t>(node.operand)].scalarPointer<int>();
            if (!pointer)
                return false;
            sources.pointers[source] = pointer;
        } else
            continue;
        node.operand = static_cast<int>(source++);
    }
    return loop.run(
        remaining,
        pc,
        lastPc,
        [&] {
            for (std::size_t i = 0; i < indexCount; ++i) {
                const auto* pointer = indices[i].read();
                if (!pointer)
                    return false;
                sources.pointers[indexSources[i]] = pointer;
            }
            return entry.boundIntEvaluator(boundNodes.data(), sources, value);
        },
        [&] {
            *destination = value;
            return true;
        });
}

bool ScalarExpressionPlan::repeatString(const Entry& entry,
    ExecutionSlotBuffer& locals,
    std::string_view suffix,
    std::int64_t& remaining,
    int& pc,
    int& lastPc) const
{
    assert(entry.boundString && entry.terminal == Terminal::AppendString && entry.loopCondition >= 0
        && entry.nextPc == pc && entry.stepLocal >= 0);
    if (remaining >= 0 && remaining < entry.cost)
        return false;
    BoundLoop loop(*this, entry, locals, nullptr);
    if (!loop.valid())
        return false;
    auto access = ScriptSemanticRuntime::UniqueStringAccess::borrow(
        locals[static_cast<std::size_t>(entry.appendLocal)]);
    if (!access)
        return false;
    if (entry.stringSource < 0) {
        return loop.run(
            remaining,
            pc,
            lastPc,
            [] {
                return true;
            },
            [&] {
                lastPc = entry.appendLiteralPc + 1;
                return access.append(suffix);
            });
    }
    std::string_view prefix;
    if (!locals[static_cast<std::size_t>(entry.stringSource)].stringView(prefix))
        return false;
    return loop.run(
        remaining,
        pc,
        lastPc,
        [] {
            return true;
        },
        [&] {
            lastPc = entry.appendLiteralPc + 1;
            return access.assign(prefix, suffix);
        });
}

namespace {
const char* opcodeName(Opcode op)
{
    switch (op) {
    case Opcode::LoadNil:
        return "LoadNil";
    case Opcode::LoadBool:
        return "LoadBool";
    case Opcode::LoadInt:
        return "LoadInt";
    case Opcode::LoadInt64:
        return "LoadInt64";
    case Opcode::LoadFloat:
        return "LoadFloat";
    case Opcode::LoadString:
        return "LoadString";
    case Opcode::LoadLocal:
        return "LoadLocal";
    case Opcode::LoadCapture:
        return "LoadCapture";
    case Opcode::LoadGlobal:
        return "LoadGlobal";
    case Opcode::StoreLocal:
        return "StoreLocal";
    case Opcode::StoreCapture:
        return "StoreCapture";
    case Opcode::StoreGlobal:
        return "StoreGlobal";
    case Opcode::Pop:
        return "Pop";
    case Opcode::Return:
        return "Return";
    case Opcode::Add:
        return "Add";
    case Opcode::Sub:
        return "Sub";
    case Opcode::Mul:
        return "Mul";
    case Opcode::Div:
        return "Div";
    case Opcode::Mod:
        return "Mod";
    case Opcode::Neg:
        return "Neg";
    case Opcode::Not:
        return "Not";
    case Opcode::BitAnd:
        return "BitAnd";
    case Opcode::BitOr:
        return "BitOr";
    case Opcode::BitXor:
        return "BitXor";
    case Opcode::BitClear:
        return "BitClear";
    case Opcode::ShiftLeft:
        return "ShiftLeft";
    case Opcode::ShiftRight:
        return "ShiftRight";
    case Opcode::CmpEq:
        return "CmpEq";
    case Opcode::CmpNe:
        return "CmpNe";
    case Opcode::CmpLt:
        return "CmpLt";
    case Opcode::CmpLe:
        return "CmpLe";
    case Opcode::CmpGt:
        return "CmpGt";
    case Opcode::CmpGe:
        return "CmpGe";
    case Opcode::Jump:
        return "Jump";
    case Opcode::JumpIfFalse:
        return "JumpIfFalse";
    case Opcode::JumpIfTrue:
        return "JumpIfTrue";
    case Opcode::Call:
        return "Call";
    case Opcode::CallValue:
        return "CallValue";
    case Opcode::CallHost:
        return "CallHost";
    case Opcode::MakeFuncValue:
        return "MakeFuncValue";
    case Opcode::MakeClosure:
        return "MakeClosure";
    case Opcode::MakeMethodValue:
        return "MakeMethodValue";
    case Opcode::MakeList:
        return "MakeList";
    case Opcode::MakeDict:
        return "MakeDict";
    case Opcode::MakeInstance:
        return "MakeInstance";
    case Opcode::MakeRange:
        return "MakeRange";
    case Opcode::MakeRangeFromStack:
        return "MakeRangeFromStack";
    case Opcode::LoadIndex:
        return "LoadIndex";
    case Opcode::StoreIndex:
        return "StoreIndex";
    case Opcode::LoadField:
        return "LoadField";
    case Opcode::StoreField:
        return "StoreField";
    case Opcode::AddrIndex:
        return "AddrIndex";
    case Opcode::AddrField:
        return "AddrField";
    case Opcode::AddrLocal:
        return "AddrLocal";
    case Opcode::AddrCapture:
        return "AddrCapture";
    case Opcode::AddrGlobal:
        return "AddrGlobal";
    case Opcode::Deref:
        return "Deref";
    case Opcode::StoreIndirect:
        return "StoreIndirect";
    case Opcode::BuiltinLen:
        return "BuiltinLen";
    case Opcode::BuiltinAppend:
        return "BuiltinAppend";
    case Opcode::BuiltinDelete:
        return "BuiltinDelete";
    case Opcode::BuiltinClear:
        return "BuiltinClear";
    case Opcode::RangeInit:
        return "RangeInit";
    case Opcode::RangeNext:
        return "RangeNext";
    case Opcode::ConvertInt:
        return "ConvertInt";
    case Opcode::ConvertInt64:
        return "ConvertInt64";
    case Opcode::ConvertFloat:
        return "ConvertFloat";
    case Opcode::ConvertString:
        return "ConvertString";
    case Opcode::ConvertBool:
        return "ConvertBool";
    case Opcode::ConvertBytes:
        return "ConvertBytes";
    case Opcode::LocalAddIntConst:
        return "LocalAddIntConst";
    case Opcode::LocalSubIntConst:
        return "LocalSubIntConst";
    case Opcode::LocalAddInt64Const:
        return "LocalAddInt64Const";
    case Opcode::LocalSubInt64Const:
        return "LocalSubInt64Const";
    case Opcode::LocalAddLocal:
        return "LocalAddLocal";
    case Opcode::LocalAddFloatConst:
        return "LocalAddFloatConst";
    case Opcode::LocalSubFloatConst:
        return "LocalSubFloatConst";
    case Opcode::JumpIfLocalIntLtConstFalse:
        return "JumpIfLocalIntLtConstFalse";
    case Opcode::IntAdd:
        return "IntAdd";
    case Opcode::IntSub:
        return "IntSub";
    case Opcode::IntMul:
        return "IntMul";
    case Opcode::IntDiv:
        return "IntDiv";
    case Opcode::IntMod:
        return "IntMod";
    case Opcode::IntCmpEq:
        return "IntCmpEq";
    case Opcode::IntCmpNe:
        return "IntCmpNe";
    case Opcode::IntCmpLt:
        return "IntCmpLt";
    case Opcode::IntCmpLe:
        return "IntCmpLe";
    case Opcode::IntCmpGt:
        return "IntCmpGt";
    case Opcode::IntCmpGe:
        return "IntCmpGe";
    case Opcode::ReturnInt:
        return "ReturnInt";
    case Opcode::Yield:
        return "Yield";
    case Opcode::Await:
        return "Await";
    case Opcode::PushHandler:
        return "PushHandler";
    case Opcode::PopHandler:
        return "PopHandler";
    case Opcode::Throw:
        return "Throw";
    case Opcode::LoadModule:
        return "LoadModule";
    case Opcode::LoadModuleExport:
        return "LoadModuleExport";
    case Opcode::Pow:
        return "Pow";
    case Opcode::FloorDiv:
        return "FloorDiv";
    case Opcode::OpcodeCount:
        return "OpcodeCount";
    }
    return "Unknown";
}

void appendField(core::pmr::String& output, std::string_view name, std::string_view value)
{
    output.append(name.data(), name.size());
    output.push_back(':');
    const auto size = std::to_string(value.size());
    output.append(size.data(), size.size());
    output.push_back(':');
    output.append(value.data(), value.size());
    output.push_back('\n');
}

template <typename T> void appendNumber(core::pmr::String& output, std::string_view name, T value)
{
    const auto text = std::to_string(value);
    appendField(output, name, text);
}

void appendBool(core::pmr::String& output, std::string_view name, bool value)
{
    appendField(output, name, value ? std::string_view("1") : std::string_view("0"));
}

void appendSource(core::pmr::String& output, std::string_view prefix, const SourcePos& source)
{
    core::pmr::String field(prefix.data(), prefix.size(), &core::defaultCpuMemoryResource());
    field += "-file";
    appendField(output, field, source.file);
    field.resize(prefix.size());
    field += "-line";
    appendNumber(output, field, source.line);
    field.resize(prefix.size());
    field += "-column";
    appendNumber(output, field, source.column);
}
} // namespace

core::pmr::String formatOpcode(Opcode op)
{
    return core::pmr::String(opcodeName(op), &core::defaultCpuMemoryResource());
}

core::pmr::String formatProgram(const Program& program)
{
    auto& res = core::defaultCpuMemoryResource();
    core::pmr::String out{&res};
    appendNumber(out, "bytecode-version", program.bytecodeVersion);
    appendField(out, "module-id", program.artifactIdentity.moduleId);
    appendNumber(out, "module-version", program.artifactIdentity.moduleVersion);
    appendNumber(out, "semantic-abi-version", program.artifactIdentity.semanticAbiVersion);
    appendNumber(out, "typed-ir-version", program.artifactIdentity.typedIrVersion);
    appendField(out, "source-hash", program.artifactIdentity.sourceHash);
    appendField(out, "dependency-graph-hash", program.artifactIdentity.dependencyGraphHash);
    appendField(out, "reflection-registry-hash", program.artifactIdentity.reflectionRegistryHash);
    appendField(out, "typed-ir-fingerprint", program.artifactIdentity.typedIrFingerprint);
    appendNumber(out, "entry", program.entry);
    appendNumber(out, "init", program.init);
    appendNumber(out, "global-count", program.globalCount);
    for (const auto& [name, slot] : program.globalSlots) {
        appendField(out, "global-name", name);
        appendNumber(out, "global-slot", slot);
    }
    for (std::size_t fi = 0; fi < program.functions.size(); ++fi) {
        const auto& fn = program.functions[fi];
        appendNumber(out, "function-index", fi);
        appendField(out, "function-id", fn.id);
        appendField(out, "function-name", fn.name);
        appendNumber(out, "function-params", fn.params);
        appendNumber(out, "function-fixed-params", fn.fixedParams);
        appendBool(out, "function-variadic", fn.isVariadic);
        appendBool(out, "function-generator", fn.isGenerator);
        appendBool(out, "function-yield-from", fn.hasYieldFrom);
        appendNumber(out, "function-results", fn.results);
        appendNumber(out, "function-locals", fn.locals);
        appendNumber(out, "function-captures", fn.captureCount);
        appendNumber(out, "function-max-stack", fn.maxStack);
        appendNumber(out, "function-result-type", static_cast<unsigned>(fn.resultType));
        for (std::size_t index = 0u; index < fn.paramTypes.size(); ++index) {
            appendNumber(out, "parameter-index", index);
            appendNumber(out, "parameter-type", static_cast<unsigned>(fn.paramTypes[index]));
            appendNumber(out, "parameter-nullable", fn.nullableParams[index]);
            appendField(out, "parameter-class", fn.paramClassNames[index]);
        }
        appendNumber(out, "variadic-element-type", static_cast<unsigned>(fn.variadicElementType));
        appendBool(out, "variadic-element-nullable", fn.variadicElementNullable);
        appendField(out, "variadic-element-class", fn.variadicElementClassName);
        for (std::size_t ci = 0; ci < fn.code.size(); ++ci) {
            const auto& inst = fn.code[ci];
            appendNumber(out, "instruction-pc", ci);
            appendNumber(out, "instruction-op", static_cast<unsigned>(inst.op));
            appendNumber(out, "instruction-a", inst.a);
            appendNumber(out, "instruction-b", inst.b);
            appendNumber(out, "instruction-i64", inst.i64);
            appendField(out, "instruction-text", inst.s);
            appendSource(out, "instruction-source", inst.pos);
        }
        for (const auto& local : fn.debugLocals) {
            appendField(out, "local-name", local.name);
            appendNumber(out, "local-slot", local.slot);
            appendNumber(out, "local-kind", static_cast<unsigned>(local.kind));
            appendField(out, "local-type", local.typeName);
            appendNumber(out, "local-start-pc", local.startPc);
            appendNumber(out, "local-end-pc", local.endPc);
        }
        for (const auto& safepoint : fn.safepoints) {
            appendField(out, "safepoint-id", safepoint.id);
            appendNumber(out, "safepoint-pc", safepoint.pc);
            appendField(out, "safepoint-resume-label", safepoint.resumeLabel);
            appendSource(out, "safepoint-source", safepoint.source);
            for (const auto slot : safepoint.canonicalSlots)
                appendNumber(out, "safepoint-slot", slot);
        }
        for (const auto& region : fn.exceptionRegions) {
            appendField(out, "exception-id", region.id);
            appendNumber(out, "exception-start-pc", region.startPc);
            appendNumber(out, "exception-end-pc", region.endPc);
            appendNumber(out, "exception-handler-pc", region.handlerPc);
            appendField(out, "exception-handler-block", region.handlerBlockId);
            appendField(out, "exception-catch-type", region.catchType);
            appendSource(out, "exception-source", region.source);
        }
        for (const auto& site : fn.delegationSites) {
            appendNumber(out, "delegation-source-pc", site.sourcePc);
            appendNumber(out, "delegation-init-pc", site.initPc);
            appendNumber(out, "delegation-range-pc", site.rangePc);
            appendNumber(out, "delegation-branch-pc", site.branchPc);
            appendNumber(out, "delegation-load-pc", site.loadPc);
            appendNumber(out, "delegation-yield-pc", site.yieldPc);
            appendNumber(out, "delegation-back-edge-pc", site.backEdgePc);
            appendNumber(out, "delegation-exit-pc", site.exitPc);
            appendNumber(out, "delegation-source-slot", site.sourceSlot);
            appendNumber(out, "delegation-index-slot", site.indexSlot);
            appendNumber(out, "delegation-value-slot", site.valueSlot);
        }
    }
    for (const auto& [name, index] : program.funcIndex) {
        appendField(out, "function-name-index-name", name);
        appendNumber(out, "function-name-index-value", index);
    }
    for (const auto& [id, index] : program.functionIdIndex) {
        appendField(out, "function-id-index-id", id);
        appendNumber(out, "function-id-index-value", index);
    }
    for (const auto& [className, fields] : program.classSchemas) {
        appendField(out, "class-schema-name", className);
        for (const auto& field : fields)
            appendField(out, "class-schema-field", field);
    }
    for (const auto& [className, methods] : program.methodSets) {
        appendField(out, "method-set-class", className);
        for (const auto& [methodName, index] : methods) {
            appendField(out, "method-name", methodName);
            appendNumber(out, "method-function-index", index);
        }
    }
    for (const auto& [className, index] : program.constructorIndex) {
        appendField(out, "constructor-class", className);
        appendNumber(out, "constructor-function-index", index);
    }
    return out;
}

core::pmr::String computeInterpreterBytecodeFingerprint(const Program& program)
{
    const auto text = formatProgram(program);
    const auto digest = core::Sha256::digest(core::StringView(text.data(), text.size()));
    static constexpr char digits[] = "0123456789abcdef";
    core::pmr::String result("sha256:", &core::defaultCpuMemoryResource());
    result.reserve(71u);
    for (const auto byte : digest) {
        result.push_back(digits[(byte >> 4u) & 0x0fu]);
        result.push_back(digits[byte & 0x0fu]);
    }
    return result;
}

ScriptResult validateInterpreterBytecode(const Program& program)
{
    auto invalid = [](const char* message) {
        return ScriptResult::failure(ScriptDiagnosticCode::TypedIrInvalid,
            core::pmr::String(message, &core::defaultCpuMemoryResource()));
    };
    if (program.bytecodeVersion != kInterpreterBytecodeVersion)
        return invalid("unsupported interpreter bytecode version");
    if (!program.artifactIdentity.complete())
        return invalid("interpreter bytecode artifact identity is incomplete");
    if (program.artifactIdentity.typedIrVersion != kTypedIrVersion
        || program.artifactIdentity.semanticAbiVersion != kSemanticAbiVersion)
        return invalid("interpreter bytecode artifact contract version is unsupported");
    if (!program.hostFunctions.empty() || !program.hostIndex.empty())
        return invalid("versioned interpreter bytecode cannot store host registry indices");
    if (program.globalCount < 0
        || program.globalSlots.size() != static_cast<std::size_t>(program.globalCount)
        || program.init < -1 || program.entry < -1
        || program.init >= static_cast<int>(program.functions.size())
        || program.entry >= static_cast<int>(program.functions.size())) {
        return invalid("interpreter bytecode program header is invalid");
    }
    for (const auto& [name, slot] : program.globalSlots) {
        if (name.empty() || slot < 0 || slot >= program.globalCount)
            return invalid("interpreter bytecode global slot is invalid");
    }
    if (program.funcIndex.size() != program.functions.size()
        || program.functionIdIndex.size() != program.functions.size())
        return invalid("interpreter bytecode function index is incomplete");
    for (std::size_t functionIndex = 0u; functionIndex < program.functions.size();
        ++functionIndex) {
        const auto& function = program.functions[functionIndex];
        if (function.id.empty() || function.name.empty() || function.locals < 0
            || function.captureCount < 0 || function.captureCount > function.locals
            || function.maxStack < 0)
            return invalid("interpreter bytecode function header is invalid");
        const auto indexed = program.funcIndex.find(function.name);
        const auto idIndexed = program.functionIdIndex.find(function.id);
        if (indexed == program.funcIndex.end() || indexed->second != static_cast<int>(functionIndex)
            || idIndexed == program.functionIdIndex.end()
            || idIndexed->second != static_cast<int>(functionIndex))
            return invalid("interpreter bytecode function index disagrees with functions");
        if (function.params < 0 || function.params > function.locals || function.fixedParams < 0
            || function.fixedParams > function.params
            || (function.isVariadic ? function.params != function.fixedParams + 1
                                    : function.params != function.fixedParams)
            || function.paramTypes.size() != static_cast<std::size_t>(function.fixedParams)
            || function.nullableParams.size() != function.paramTypes.size()
            || function.paramClassNames.size() != function.paramTypes.size())
            return invalid("interpreter bytecode parameter contract is incomplete");
        if (function.hasYieldFrom != !function.delegationSites.empty())
            return invalid("interpreter bytecode delegation contract is inconsistent");
        for (const auto& instruction : function.code) {
            if (instruction.op >= Opcode::OpcodeCount)
                return invalid("interpreter bytecode opcode is invalid");
            const auto validLocal = [&function](int slot) {
                return slot >= 0 && slot < function.locals;
            };
            switch (instruction.op) {
            case Opcode::LoadLocal:
            case Opcode::StoreLocal:
            case Opcode::AddrLocal:
            case Opcode::LocalAddIntConst:
            case Opcode::LocalSubIntConst:
            case Opcode::LocalAddInt64Const:
            case Opcode::LocalSubInt64Const:
            case Opcode::LocalAddFloatConst:
            case Opcode::LocalSubFloatConst:
                if (!validLocal(instruction.a))
                    return invalid("interpreter bytecode local operand is invalid");
                break;
            case Opcode::LocalAddLocal:
            case Opcode::RangeInit:
            case Opcode::RangeNext:
                if (!validLocal(instruction.a) || !validLocal(instruction.b))
                    return invalid("interpreter bytecode local operands are invalid");
                if (instruction.op == Opcode::RangeNext) {
                    int key, value;
                    unpackRangeTargets(instruction.i64, key, value);
                    if (key < -1 || value < -1 || (key >= 0 && !validLocal(key))
                        || (value >= 0 && !validLocal(value)))
                        return invalid("interpreter bytecode range target is invalid");
                }
                break;
            case Opcode::JumpIfLocalIntLtConstFalse:
                if (!validLocal(instruction.b))
                    return invalid("interpreter bytecode branch local is invalid");
                break;
            case Opcode::LoadCapture:
            case Opcode::StoreCapture:
            case Opcode::AddrCapture:
                if (instruction.a < 0 || instruction.a >= function.captureCount)
                    return invalid("interpreter bytecode capture operand is invalid");
                break;
            case Opcode::LoadGlobal:
            case Opcode::StoreGlobal:
            case Opcode::AddrGlobal:
                if (instruction.a < 0 || instruction.a >= program.globalCount)
                    return invalid("interpreter bytecode global operand is invalid");
                break;
            default:
                break;
            }
            if ((instruction.op == Opcode::LocalAddIntConst
                    || instruction.op == Opcode::LocalSubIntConst
                    || instruction.op == Opcode::JumpIfLocalIntLtConstFalse)
                && (instruction.i64 < std::numeric_limits<int>::min()
                    || instruction.i64 > std::numeric_limits<int>::max()))
                return invalid("interpreter bytecode int constant is out of range");
            const bool branch = instruction.op == Opcode::Jump
                || instruction.op == Opcode::JumpIfFalse || instruction.op == Opcode::JumpIfTrue
                || instruction.op == Opcode::JumpIfLocalIntLtConstFalse
                || instruction.op == Opcode::PushHandler;
            const bool branchAllowsEnd = instruction.op != Opcode::PushHandler;
            if (branch
                && (instruction.a < 0
                    || (branchAllowsEnd
                            ? instruction.a > static_cast<int>(function.code.size())
                            : instruction.a >= static_cast<int>(function.code.size())))) {
                return invalid("interpreter bytecode branch target is invalid");
            }
            const bool functionTarget = instruction.op == Opcode::Call
                || instruction.op == Opcode::MakeFuncValue || instruction.op == Opcode::MakeClosure
                || instruction.op == Opcode::MakeMethodValue;
            if (functionTarget
                && (instruction.a < 0
                    || instruction.a >= static_cast<int>(program.functions.size()))) {
                return invalid("interpreter bytecode function target is invalid");
            }
            if (instruction.op == Opcode::CallHost
                && (instruction.s.empty() || instruction.a != -1))
                return invalid("interpreter bytecode host binding id is missing");
        }
        for (const auto& local : function.debugLocals) {
            if (local.name.empty() || local.slot < 0 || local.slot >= function.locals
                || local.startPc < 0 || local.endPc < local.startPc
                || local.endPc > static_cast<int>(function.code.size())) {
                return invalid("interpreter bytecode debug local is invalid");
            }
        }
        for (std::size_t index = 0u; index < function.safepoints.size(); ++index) {
            const auto& safepoint = function.safepoints[index];
            if (safepoint.id.empty() || safepoint.resumeLabel.empty() || !safepoint.source.valid()
                || safepoint.pc < 0 || safepoint.pc >= static_cast<int>(function.code.size())
                || (function.code[static_cast<std::size_t>(safepoint.pc)].op != Opcode::Yield
                    && function.code[static_cast<std::size_t>(safepoint.pc)].op != Opcode::Await)
                || safepoint.canonicalSlots.size() != static_cast<std::size_t>(function.locals)) {
                return invalid("interpreter bytecode safepoint is invalid");
            }
            for (const auto slot : safepoint.canonicalSlots) {
                if (slot < 0 || slot >= function.locals
                    || std::count(
                           safepoint.canonicalSlots.begin(), safepoint.canonicalSlots.end(), slot)
                        != 1) {
                    return invalid("interpreter bytecode safepoint slot is invalid");
                }
            }
            for (std::size_t other = index + 1u; other < function.safepoints.size(); ++other) {
                if (safepoint.id == function.safepoints[other].id)
                    return invalid("interpreter bytecode safepoint id is duplicated");
            }
        }
        for (std::size_t index = 0u; index < function.exceptionRegions.size(); ++index) {
            const auto& region = function.exceptionRegions[index];
            if (region.id.empty() || region.handlerBlockId.empty() || !region.source.valid()
                || region.startPc <= 0 || region.endPc <= region.startPc
                || region.endPc >= static_cast<int>(function.code.size()) || region.handlerPc < 0
                || region.handlerPc >= static_cast<int>(function.code.size())
                || function.code[static_cast<std::size_t>(region.startPc - 1)].op
                    != Opcode::PushHandler
                || function.code[static_cast<std::size_t>(region.startPc - 1)].a != region.handlerPc
                || function.code[static_cast<std::size_t>(region.endPc)].op != Opcode::PopHandler) {
                return invalid("interpreter bytecode exception region is invalid");
            }
            for (std::size_t other = index + 1u; other < function.exceptionRegions.size();
                ++other) {
                const auto& candidate = function.exceptionRegions[other];
                const bool disjoint =
                    region.endPc <= candidate.startPc || candidate.endPc <= region.startPc;
                const bool contains =
                    region.startPc <= candidate.startPc && candidate.endPc <= region.endPc;
                const bool contained =
                    candidate.startPc <= region.startPc && region.endPc <= candidate.endPc;
                if (region.id == candidate.id || (!disjoint && !contains && !contained)) {
                    return invalid("interpreter bytecode exception regions conflict");
                }
            }
        }
        for (const auto& site : function.delegationSites) {
            const auto inBounds = [&function](int pc, bool allowEnd = false) {
                return pc >= 0
                    && (allowEnd ? pc <= static_cast<int>(function.code.size())
                                 : pc < static_cast<int>(function.code.size()));
            };
            if (!inBounds(site.sourcePc) || !inBounds(site.initPc) || !inBounds(site.rangePc)
                || !inBounds(site.branchPc) || !inBounds(site.loadPc) || !inBounds(site.yieldPc)
                || !inBounds(site.backEdgePc) || !inBounds(site.exitPc, true)
                || !(site.sourcePc < site.initPc && site.initPc < site.rangePc
                    && site.rangePc < site.branchPc && site.branchPc < site.loadPc
                    && site.loadPc < site.yieldPc && site.yieldPc < site.backEdgePc
                    && site.backEdgePc < site.exitPc)
                || function.code[static_cast<std::size_t>(site.initPc)].op != Opcode::RangeInit
                || function.code[static_cast<std::size_t>(site.rangePc)].op != Opcode::RangeNext
                || function.code[static_cast<std::size_t>(site.branchPc)].op != Opcode::JumpIfFalse
                || function.code[static_cast<std::size_t>(site.yieldPc)].op != Opcode::Yield
                || function.code[static_cast<std::size_t>(site.backEdgePc)].op != Opcode::Jump
                || function.code[static_cast<std::size_t>(site.branchPc)].a != site.exitPc
                || function.code[static_cast<std::size_t>(site.loadPc)].a != site.valueSlot
                || function.code[static_cast<std::size_t>(site.backEdgePc)].a != site.rangePc
                || function.code[static_cast<std::size_t>(site.rangePc)].a != site.sourceSlot
                || function.code[static_cast<std::size_t>(site.rangePc)].b != site.indexSlot
                || function.code[static_cast<std::size_t>(site.initPc)].a != site.sourceSlot
                || function.code[static_cast<std::size_t>(site.initPc)].b != site.indexSlot
                || site.sourceSlot < 0 || site.sourceSlot >= function.locals || site.indexSlot < 0
                || site.indexSlot >= function.locals || site.valueSlot < 0
                || site.valueSlot >= function.locals) {
                return invalid("interpreter bytecode delegation metadata is invalid");
            }
            int keySlot = -1;
            int valueSlot = -1;
            unpackRangeTargets(
                function.code[static_cast<std::size_t>(site.rangePc)].i64, keySlot, valueSlot);
            if (keySlot != site.valueSlot || valueSlot != site.valueSlot)
                return invalid("interpreter bytecode delegation slots are invalid");
        }
    }
    for (const auto& [className, fields] : program.classSchemas) {
        if (className.empty())
            return invalid("interpreter bytecode class schema is invalid");
        for (std::size_t index = 0u; index < fields.size(); ++index) {
            if (fields[index].empty()
                || std::count(fields.begin(), fields.end(), fields[index]) != 1)
                return invalid("interpreter bytecode class field is invalid");
        }
    }
    for (const auto& [className, methods] : program.methodSets) {
        if (className.empty() || program.classSchemas.find(className) == program.classSchemas.end())
            return invalid("interpreter bytecode method set has no class schema");
        for (const auto& [methodName, index] : methods) {
            if (methodName.empty() || index < 0
                || index >= static_cast<int>(program.functions.size()))
                return invalid("interpreter bytecode method target is invalid");
        }
    }
    for (const auto& [className, index] : program.constructorIndex) {
        if (className.empty() || program.classSchemas.find(className) == program.classSchemas.end()
            || index < 0 || index >= static_cast<int>(program.functions.size()))
            return invalid("interpreter bytecode constructor target is invalid");
    }
    const auto expected = computeInterpreterBytecodeFingerprint(program);
    if (program.bytecodeFingerprint != expected)
        return invalid("interpreter bytecode fingerprint does not match program");
    return ScriptResult::success();
}

namespace {
class PrivateLocalUseAnalysis final {
    using Boundaries = core::pmr::Vector<std::uint8_t>;

    static bool appendAt(
        const Function& function, int pc, int slot, const Boundaries& boundary, bool self = true)
    {
        const auto& code = function.code;
        return pc >= 0 && pc + 3 < static_cast<int>(code.size()) && code[pc].op == Opcode::LoadLocal
            && (!self || code[pc].a == slot) && code[pc + 1].op == Opcode::LoadString
            && code[pc + 2].op == Opcode::Add && code[pc + 3].op == Opcode::StoreLocal
            && code[pc + 3].a == slot && !boundary[pc + 1] && !boundary[pc + 2]
            && !boundary[pc + 3];
    }

    static bool indexAt(const Function& function, int pc, int slot)
    {
        const auto& code = function.code;
        const auto indexOperand = [&](int at) {
            return at < static_cast<int>(code.size())
                && (code[at].op == Opcode::LoadInt
                    || (code[at].op == Opcode::LoadLocal && code[at].a != slot));
        };
        if (!indexOperand(pc + 1))
            return false;
        if (pc + 2 < static_cast<int>(code.size()) && code[pc + 2].op == Opcode::LoadIndex)
            return true;
        return pc + 4 < static_cast<int>(code.size()) && indexOperand(pc + 2)
            && (code[pc + 3].op == Opcode::IntMod || code[pc + 3].op == Opcode::Mod)
            && code[pc + 4].op == Opcode::LoadIndex;
    }

    static bool doesNotEscape(const Function& function,
        int slot,
        const Boundaries& boundary,
        bool sequence = false,
        bool dictionary = false,
        bool unobservedCatch = false)
    {
        if (slot < function.params || !function.safepoints.empty()
            || (!unobservedCatch && !function.exceptionRegions.empty()))
            return false;
        const auto& code = function.code;
        for (int pc = 0; pc != static_cast<int>(code.size()); ++pc) {
            const auto& instruction = code[pc];
            // Explicit allowlist: adding a new opcode cannot silently create
            // an unexamined use, local write, capture or observation edge.
            switch (instruction.op) {
            case Opcode::LoadLocal:
                if (unobservedCatch && instruction.a == slot)
                    return false;
                if (instruction.a == slot
                    && !(sequence ? indexAt(function, pc, slot)
                                  : appendAt(function, pc, slot, boundary))
                    && !(pc + 1 < static_cast<int>(code.size())
                        && code[pc + 1].op == Opcode::BuiltinLen))
                    return false;
                break;
            case Opcode::StoreLocal:
                if (instruction.a == slot) {
                    if (unobservedCatch)
                        break;
                    else if (sequence) {
                        if (!(pc > 0 && !boundary[pc]
                                && code[pc - 1].op
                                    == (dictionary ? Opcode::MakeDict : Opcode::MakeList)))
                            return false;
                    } else if (!(pc > 0 && !boundary[pc] && code[pc - 1].op == Opcode::LoadString)
                        && !appendAt(function, pc - 3, slot, boundary, false))
                        return false;
                }
                break;
            case Opcode::LocalAddIntConst:
            case Opcode::LocalSubIntConst:
            case Opcode::LocalAddInt64Const:
            case Opcode::LocalSubInt64Const:
            case Opcode::LocalAddFloatConst:
            case Opcode::LocalSubFloatConst:
                if (instruction.a == slot)
                    return false;
                break;
            case Opcode::LocalAddLocal:
                if (instruction.a == slot || instruction.b == slot)
                    return false;
                break;
            case Opcode::JumpIfLocalIntLtConstFalse:
                if (instruction.b == slot)
                    return false;
                break;
            case Opcode::LoadNil:
            case Opcode::LoadBool:
            case Opcode::LoadInt:
            case Opcode::LoadInt64:
            case Opcode::LoadFloat:
            case Opcode::LoadString:
            case Opcode::Pop:
            case Opcode::Return:
            case Opcode::ReturnInt:
            case Opcode::Add:
            case Opcode::Sub:
            case Opcode::Mul:
            case Opcode::Div:
            case Opcode::Mod:
            case Opcode::Neg:
            case Opcode::Not:
            case Opcode::BitAnd:
            case Opcode::BitOr:
            case Opcode::BitXor:
            case Opcode::BitClear:
            case Opcode::ShiftLeft:
            case Opcode::ShiftRight:
            case Opcode::CmpEq:
            case Opcode::CmpNe:
            case Opcode::CmpLt:
            case Opcode::CmpLe:
            case Opcode::CmpGt:
            case Opcode::CmpGe:
            case Opcode::Jump:
            case Opcode::JumpIfFalse:
            case Opcode::JumpIfTrue:
            case Opcode::BuiltinLen:
            case Opcode::ConvertInt:
            case Opcode::ConvertInt64:
            case Opcode::ConvertFloat:
            case Opcode::ConvertString:
            case Opcode::ConvertBool:
            case Opcode::ConvertBytes:
            case Opcode::IntAdd:
            case Opcode::IntSub:
            case Opcode::IntMul:
            case Opcode::IntDiv:
            case Opcode::IntMod:
            case Opcode::IntCmpEq:
            case Opcode::IntCmpNe:
            case Opcode::IntCmpLt:
            case Opcode::IntCmpLe:
            case Opcode::IntCmpGt:
            case Opcode::IntCmpGe:
            case Opcode::Pow:
            case Opcode::FloorDiv:
                break;
            case Opcode::MakeDict:
                if (!dictionary)
                    return false;
                break;
            case Opcode::MakeList:
            case Opcode::LoadIndex:
                if (!sequence)
                    return false;
                break;
            case Opcode::PushHandler:
            case Opcode::PopHandler:
            case Opcode::Throw:
                if (!unobservedCatch)
                    return false;
                break;
            default:
                return false;
            }
        }
        return true;
    }

public:
    static int unobservedCatchLocal(const Function& function, int handlerPc)
    {
        if (function.isGenerator || function.isVariadic || handlerPc < 0
            || handlerPc >= static_cast<int>(function.code.size()))
            return -1;
        const auto& store = function.code[handlerPc];
        if (store.op != Opcode::StoreLocal || store.a < 0 || store.a >= function.locals)
            return -1;
        // This policy rejects every read, address/capture, call, suspension and
        // unexamined opcode. No boundary vector is needed: stores are the only
        // permitted uses of this private slot, including other handler entries.
        const Boundaries unused{&core::defaultCpuMemoryResource()};
        return doesNotEscape(function, store.a, unused, false, false, true) ? store.a : -1;
    }

    static bool hasLiteralIntegerKeys(
        const Function& function, int slot, const Boundaries& boundary)
    {
        if (!doesNotEscape(function, slot, boundary, true, true))
            return false;
        const auto& code = function.code;
        for (int pc = 1; pc < static_cast<int>(code.size()); ++pc) {
            if (code[pc].op != Opcode::StoreLocal || code[pc].a != slot)
                continue;
            int cursor = pc - 1;
            if (boundary[cursor])
                return false;
            for (int pair = 0; pair < code[pc - 1].b; ++pair) {
                int needed = 1;
                do {
                    if (--cursor < 0 || boundary[cursor])
                        return false;
                    const auto canonical =
                        InterpreterBytecodeLowerer::canonicalOpcode(code[cursor].op);
                    if (canonical == TypedIrOpcode::OpcodeCount)
                        return false;
                    const auto& effect = typedIrOperationDescriptor(canonical);
                    if (effect.stackInputs < 0 || effect.stackOutputs != 1 || effect.terminator
                        || effect.suspension)
                        return false;
                    needed = needed - 1 + effect.stackInputs;
                } while (needed > 0);
                if (--cursor < 0 || boundary[cursor] || code[cursor].op != Opcode::LoadInt
                    || code[cursor].i64 < std::numeric_limits<int>::min()
                    || code[cursor].i64 > std::numeric_limits<int>::max())
                    return false;
            }
        }
        return true;
    }

    static bool prepareContainers(
        const Function& function, const Boundaries& boundary, ScalarExpressionPlan& plan)
    {
        bool any = false;
        core::pmr::Vector<std::int8_t> qualified(
            static_cast<std::size_t>(function.locals), -1, &core::defaultCpuMemoryResource());
        for (int pc = 0; pc + 1 < static_cast<int>(function.code.size()); ++pc) {
            const bool dictionary = function.code[pc].op == Opcode::MakeDict;
            if ((!dictionary && function.code[pc].op != Opcode::MakeList)
                || function.code[pc + 1].op != Opcode::StoreLocal || boundary[pc + 1])
                continue;
            const int slot = function.code[pc + 1].a;
            auto& status = qualified[slot];
            if (status < 0)
                status = doesNotEscape(function, slot, boundary, true, dictionary) ? 1 : 0;
            if (status == 0)
                continue;
            if (dictionary)
                plan.entries[pc].dictionaryDestination = slot;
            else
                plan.entries[pc].listDestination = slot;
            any = true;
        }
        return any;
    }

    static void prepareConstructionOperands(
        const Function& function, const Boundaries& boundary, ScalarExpressionPlan& plan)
    {
        const auto& code = function.code;
        for (int pc = 0; pc < static_cast<int>(code.size()); ++pc) {
            const auto& construction = plan.entries[pc];
            if (construction.listDestination < 0 && construction.dictionaryDestination < 0)
                continue;
            const auto count64 = static_cast<std::int64_t>(code[pc].b)
                * (construction.dictionaryDestination >= 0 ? 2 : 1);
            if (count64 <= 0 || count64 > std::numeric_limits<int>::max())
                continue;
            int cursor = pc, needed = static_cast<int>(count64), operations = 0;
            while (needed > 0) {
                if (cursor == 0 || boundary[cursor])
                    break;
                const auto& instruction = code[--cursor];
                const auto canonical = InterpreterBytecodeLowerer::canonicalOpcode(instruction.op);
                const bool local = canonical == TypedIrOpcode::LoadLocal;
                const bool constant = canonical == TypedIrOpcode::LoadInt
                    && instruction.i64 >= std::numeric_limits<int>::min()
                    && instruction.i64 <= std::numeric_limits<int>::max();
                const bool arithmetic =
                    (canonical >= TypedIrOpcode::Add && canonical <= TypedIrOpcode::Mod)
                    || (canonical >= TypedIrOpcode::IntAdd && canonical <= TypedIrOpcode::IntMod);
                if (!local && !constant && !arithmetic)
                    break;
                const auto& effect = typedIrOperationDescriptor(canonical);
                if (effect.stackOutputs != 1 || effect.stackInputs < 0 || effect.terminator
                    || effect.suspension)
                    break;
                needed += effect.stackInputs - 1;
                operations += arithmetic;
            }
            if (needed != 0 || operations == 0)
                continue;
            ScalarExpressionPlan::Entry entry;
            entry.terminal = ScalarExpressionPlan::Terminal::ConstructionOperands;
            entry.offset = static_cast<int>(plan.nodes.size());
            entry.count = pc - cursor;
            entry.cost = entry.count;
            entry.nextPc = pc;
            entry.lastPc = pc - 1;
            entry.calculationOffset = static_cast<int>(plan.calculations.size());
            for (int producer = cursor; producer < pc; ++producer) {
                const auto& instruction = code[producer];
                const auto canonical = InterpreterBytecodeLowerer::canonicalOpcode(instruction.op);
                ScalarExpressionPlan::Node node;
                if (canonical == TypedIrOpcode::LoadLocal) {
                    node.operation = ScalarExpressionPlan::Operation::Local;
                    node.operand = instruction.a;
                } else if (canonical == TypedIrOpcode::LoadInt) {
                    node.operation = ScalarExpressionPlan::Operation::Constant;
                    node.operand = static_cast<int>(instruction.i64);
                    node.divisor = core::Int32Divisor(node.operand);
                } else {
                    node.operation =
                        static_cast<ScalarExpressionPlan::Operation>(static_cast<int>(canonical)
                            - static_cast<int>(canonical >= TypedIrOpcode::IntAdd
                                    ? TypedIrOpcode::IntAdd
                                    : TypedIrOpcode::Add)
                            + static_cast<int>(ScalarExpressionPlan::Operation::Add));
                }
                plan.nodes.push_back(node);
            }
            core::pmr::Vector<ScalarExpressionPlan::Node> roots{&core::defaultCpuMemoryResource()};
            plan.lowerOperands(entry, roots);
            assert(roots.size() == static_cast<std::size_t>(count64));
            // Construction has no scalar destination or generic expression;
            // reuse that terminal's payload without expanding per-PC metadata.
            plan.nodes.resize(static_cast<std::size_t>(entry.offset));
            entry.destination = static_cast<int>(roots.size());
            entry.count = 0;
            plan.nodes.insert(plan.nodes.end(), roots.begin(), roots.end());
            plan.entries[cursor] = entry;
        }
    }

    template <class Finish>
    static bool prepare(const Function& function,
        const Boundaries& boundary,
        ScalarExpressionPlan& plan,
        Finish&& finish)
    {
        bool any = false;
        core::pmr::Vector<std::int8_t> qualified(
            static_cast<std::size_t>(function.locals), -1, &core::defaultCpuMemoryResource());
        for (int pc = 0; pc + 3 < static_cast<int>(function.code.size()); ++pc) {
            const auto& instruction = function.code[pc];
            if (instruction.op != Opcode::LoadLocal
                || function.code[pc + 3].op != Opcode::StoreLocal)
                continue;
            const int destination = function.code[pc + 3].a;
            if (!appendAt(function, pc, destination, boundary, false))
                continue;
            auto& status = qualified[destination];
            if (status < 0)
                status = doesNotEscape(function, destination, boundary) ? 1 : 0;
            if (status == 0)
                continue;
            ScalarExpressionPlan::Entry entry;
            entry.destination = destination;
            entry.appendLocal = destination;
            entry.stringSource = instruction.a == destination ? -1 : instruction.a;
            entry.appendLiteralPc = pc + 1;
            entry.terminal = ScalarExpressionPlan::Terminal::AppendString;
            entry.cost = 4;
            entry.lastPc = pc + 3;
            entry.nextPc = pc + 4;
            finish(pc, pc + 3, entry);
            any = true;
        }
        return any;
    }
};

ScalarExpressionPlan prepareScalarExpressions(
    const Function& function, const Program& program, bool independentStackSpans = false)
{
    using Operation = ScalarExpressionPlan::Operation;
    using Terminal = ScalarExpressionPlan::Terminal;
    ScalarExpressionPlan plan;
    const auto& code = function.code;
    core::pmr::Vector<std::uint8_t> boundary(
        code.size() + 1u, 0u, &core::defaultCpuMemoryResource());
    const auto mark = [&](int pc) {
        if (pc >= 0 && pc <= static_cast<int>(code.size()))
            boundary[pc] = 1u;
    };
    for (const auto& instruction : code) {
        switch (instruction.op) {
        case Opcode::Jump:
        case Opcode::JumpIfFalse:
        case Opcode::JumpIfTrue:
        case Opcode::JumpIfLocalIntLtConstFalse:
        case Opcode::PushHandler:
            mark(instruction.a);
            break;
        default:
            break;
        }
    }
    for (const auto& site : function.safepoints)
        mark(site.pc);
    for (const auto& region : function.exceptionRegions) {
        mark(region.startPc);
        mark(region.endPc);
        mark(region.handlerPc);
    }
    plan.entries.resize(code.size());
    core::pmr::Vector<std::int8_t> integerDictionaries(
        static_cast<std::size_t>(function.locals), -1, &core::defaultCpuMemoryResource());
    const auto finish = [&](int start, int terminalPc, ScalarExpressionPlan::Entry entry) {
        int end = terminalPc + 1;
        const bool closureSpan = std::any_of(plan.nodes.begin() + entry.offset,
            plan.nodes.begin() + entry.offset + entry.count,
            [](const auto& node) {
                return node.operation == Operation::ClosureLeaf;
            });
        // A trailing independent integer update and unconditional edge have
        // no observable effects before commit. Do not consume an incoming edge,
        // exception boundary or safepoint, even when the stack proof is valid.
        if ((!independentStackSpans || closureSpan)
            && (entry.terminal == Terminal::Store || entry.terminal == Terminal::AppendString)
            && end < static_cast<int>(code.size()) && !boundary[end]) {
            const auto& step = code[end];
            if ((step.op == Opcode::LocalAddIntConst || step.op == Opcode::LocalSubIntConst)
                && step.a != entry.destination && step.i64 >= std::numeric_limits<int>::min()
                && step.i64 <= std::numeric_limits<int>::max()) {
                entry.stepLocal = step.a;
                entry.stepConstant = static_cast<int>(step.i64);
                entry.subtractStep = step.op == Opcode::LocalSubIntConst;
                ++end;
            } else if (end + 3 < static_cast<int>(code.size()) && !boundary[end + 1]
                && !boundary[end + 2] && !boundary[end + 3] && step.op == Opcode::LoadLocal
                && step.a != entry.destination && code[end + 1].op == Opcode::LoadInt
                && code[end + 1].i64 >= std::numeric_limits<int>::min()
                && code[end + 1].i64 <= std::numeric_limits<int>::max()
                && (code[end + 2].op == Opcode::IntAdd || code[end + 2].op == Opcode::Add
                    || code[end + 2].op == Opcode::IntSub || code[end + 2].op == Opcode::Sub)
                && code[end + 3].op == Opcode::StoreLocal && code[end + 3].a == step.a) {
                entry.stepLocal = step.a;
                entry.stepConstant = static_cast<int>(code[end + 1].i64);
                entry.subtractStep =
                    code[end + 2].op == Opcode::IntSub || code[end + 2].op == Opcode::Sub;
                end += 4;
            }
            if (end < static_cast<int>(code.size()) && !boundary[end]
                && code[end].op == Opcode::Jump) {
                entry.nextPc = code[end].a;
                ++end;
            } else
                entry.nextPc = end;
        }
        entry.cost = end - start + entry.leafCost;
        entry.lastPc = end - 1;
        // A backward edge may consume its already-proven scalar condition.
        // This executes the original edge and condition once per iteration;
        // it does not summarize or replace the loop's work.
        if ((entry.stepLocal >= 0 || entry.terminal == Terminal::Store) && entry.nextPc < start
            && function.safepoints.empty() && function.exceptionRegions.empty()) {
            const auto& condition = plan.entries[static_cast<std::size_t>(entry.nextPc)];
            if (condition.count == 3 && condition.terminal == Terminal::BranchFalse) {
                const auto* nodes = plan.nodes.data() + condition.offset;
                if (nodes[0].operation == Operation::Local && nodes[2].operation == Operation::Less
                    && (nodes[1].operation == Operation::Constant
                        || nodes[1].operation == Operation::Local)) {
                    entry.loopCondition = entry.nextPc;
                    entry.nextPc = condition.nextPc;
                    entry.cost += condition.cost;
                    entry.lastPc = condition.lastPc;
                }
            }
        }
        // Call leaves are atomic postfix operands of exact argument/callee
        // instruction spans. The actual callable and leaf qualification are
        // resolved at runtime; these nodes never enter ordinary evaluators.
        const auto callBegin = plan.nodes.begin() + entry.offset;
        const auto callEnd = callBegin + entry.count;
        const auto isCall = [](const auto& node) {
            return node.operation == Operation::MethodLeaf
                || node.operation == Operation::ClosureLeaf;
        };
        const auto call = std::find_if(callBegin, callEnd, isCall);
        if (call != callEnd) {
            const bool closed = (!independentStackSpans || closureSpan)
                && entry.terminal == Terminal::Store && !entry.floating
                && entry.leafFrameValues == 0 && entry.leafCost == 0 && entry.loopCondition >= 0
                && entry.nextPc == start && entry.stepLocal >= 0 && function.safepoints.empty()
                && function.exceptionRegions.empty()
                && std::none_of(code.begin(),
                    code.end(),
                    [](const auto& instruction) {
                        return instruction.op == Opcode::PushHandler;
                    })
                && std::count_if(callBegin, callEnd, isCall) == 1
                && std::all_of(callBegin,
                    call,
                    [](const auto& node) {
                        return node.operation == Operation::Local
                            || node.operation == Operation::Constant;
                    })
                && call->call.targetLocal != entry.destination
                && call->call.targetLocal != entry.stepLocal
                && std::all_of(callBegin, callEnd, [](const auto& node) {
                       return node.operation <= Operation::GreaterEqual
                           || node.operation == Operation::MethodLeaf
                           || node.operation == Operation::ClosureLeaf;
                   });
            if (closed) {
                entry.rangeBody = static_cast<int>(call - plan.nodes.begin());
                core::pmr::Vector<ScalarExpressionPlan::Node> operands{
                    &core::defaultCpuMemoryResource()};
                entry.calculationOffset = static_cast<int>(plan.calculations.size());
                plan.lowerOperands<true>(entry, operands);
                assert(operands.size() == 1);
                entry.terminal = closureSpan ? Terminal::ClosureLoop : Terminal::MethodLoop;
                if (closureSpan)
                    plan.hasClosureLoop = true;
                else
                    plan.hasMethodLoop = true;
                entry.count = 0;
            } else
                entry = {};
            plan.entries[start] = entry;
            return end - 1;
        }
        if (entry.count != 0)
            plan.specialize(entry);
        // Repeat eligibility is immutable plan metadata, not a runtime guard.
        const bool repeatable = entry.loopCondition >= 0 && entry.nextPc == start
            && entry.stepLocal >= 0 && entry.leafFrameValues == 0;
        entry.calculationLoop = repeatable && entry.terminal == Terminal::Store && !entry.floating
            && !entry.evaluator && entry.calculationCount > 0 && entry.leafCost == 0
            && std::all_of(plan.nodes.begin() + entry.offset,
                plan.nodes.begin() + entry.offset + entry.count,
                [](const auto& node) {
                    return node.operation <= Operation::GreaterEqual;
                });
        // Nested string-key reads have no generic expression shortcut. They
        // borrow only a closed, independently proved self-edge. The two
        // container locals cannot be replaced by its scalar commits.
        const auto firstNode = plan.nodes.begin() + entry.offset;
        const auto lastNode = firstNode + entry.count;
        const bool stringKey = std::any_of(firstNode, lastNode, [](const auto& node) {
            return node.operation == Operation::StringKeyIndex;
        });
        if (stringKey
            && (!repeatable || entry.terminal != Terminal::Store
                || std::any_of(code.begin(),
                    code.end(),
                    [](const auto& instruction) {
                        return instruction.op == Opcode::PushHandler;
                    })
                || std::any_of(firstNode, lastNode, [&](const auto& node) {
                       return node.operation == Operation::Index
                           || (node.operation == Operation::StringKeyIndex
                               && (node.operand == entry.destination
                                   || node.operand == entry.stepLocal
                                   || node.stringKey.keys == entry.destination
                                   || node.stringKey.keys == entry.stepLocal));
                   }))) {
            entry.count = entry.calculationCount = 0;
            entry.evaluator = nullptr;
            entry.boundIntEvaluator = nullptr;
        }
        if (entry.terminal != Terminal::Store || !repeatable)
            entry.boundIntEvaluator = nullptr;
        entry.boundString = entry.terminal == Terminal::AppendString && repeatable;
        plan.entries[start] = entry;
        return end - 1;
    };
    for (int start = 0; start < static_cast<int>(code.size()); ++start) {
        const auto& first = code[start];
        if (first.op == Opcode::LocalAddIntConst || first.op == Opcode::LocalSubIntConst
            || first.op == Opcode::LocalAddLocal
            || first.op == Opcode::JumpIfLocalIntLtConstFalse) {
            if (first.op != Opcode::LocalAddLocal
                && (first.i64 < std::numeric_limits<int>::min()
                    || first.i64 > std::numeric_limits<int>::max()))
                continue;
            const bool branch = first.op == Opcode::JumpIfLocalIntLtConstFalse;
            ScalarExpressionPlan::Entry entry;
            entry.offset = static_cast<int>(plan.nodes.size());
            entry.count = 3;
            entry.nextPc = start + 1;
            entry.destination = first.a;
            entry.terminal = branch ? Terminal::BranchFalse : Terminal::Store;
            plan.nodes.push_back({Operation::Local, branch ? first.b : first.a});
            plan.nodes.push_back(
                {first.op == Opcode::LocalAddLocal ? Operation::Local : Operation::Constant,
                    first.op == Opcode::LocalAddLocal ? first.b : static_cast<int>(first.i64)});
            plan.nodes.push_back({branch                   ? Operation::Less
                    : first.op == Opcode::LocalSubIntConst ? Operation::Sub
                                                           : Operation::Add});
            plan.scratchSize = std::max(plan.scratchSize, std::size_t(2));
            start = finish(start, start, entry);
            continue;
        }
        if (code[start].op != Opcode::LoadLocal && code[start].op != Opcode::LoadInt
            && code[start].op != Opcode::LoadFloat)
            continue;
        const int offset = static_cast<int>(plan.nodes.size());
        int depth = 0, maxDepth = 0;
        bool boolean = false, arithmetic = false, accepted = false;
        bool floating = false, integerConstant = false, typedInteger = false;
        int leafCost = 0, leafFrameValues = 0;
        ScalarExpressionPlan::Entry pushCandidate;
        int pushEnd = -1, pushScratch = 0;
        for (int pc = start; pc < static_cast<int>(code.size()); ++pc) {
            if (pc != start && boundary[pc])
                break;
            const auto& instruction = code[pc];
            if (depth == 1 && arithmetic) {
                ScalarExpressionPlan::Entry entry;
                entry.offset = offset;
                entry.count = static_cast<int>(plan.nodes.size()) - offset;
                entry.destination = instruction.a;
                entry.nextPc = pc + 1;
                entry.floating = floating;
                entry.leafCost = leafCost;
                entry.leafFrameValues = leafFrameValues;
                if (!boolean && !(floating && (integerConstant || typedInteger))) {
                    pushCandidate = entry;
                    pushCandidate.terminal = Terminal::Push;
                    pushCandidate.nextPc = pc;
                    pushEnd = pc - 1;
                    pushScratch = maxDepth;
                }
                if (instruction.op == Opcode::StoreLocal && !boolean)
                    entry.terminal = Terminal::Store;
                else if (instruction.op == Opcode::JumpIfFalse && boolean)
                    entry.terminal = Terminal::BranchFalse;
                else if (instruction.op == Opcode::JumpIfTrue && boolean)
                    entry.terminal = Terminal::BranchTrue;
                else if ((instruction.op == Opcode::ReturnInt
                             || (instruction.op == Opcode::Return && instruction.a == 1))
                    && !boolean) {
                    entry.terminal = Terminal::Return;
                } else
                    entry.count = 0;
                // Only homogeneous floating expressions qualify. Integer
                // constants or typed-int operations can have intermediate
                // width/overflow semantics that float arithmetic cannot replace.
                if (floating && (integerConstant || typedInteger))
                    entry.count = 0;
                if (entry.count != 0) {
                    plan.scratchSize =
                        std::max(plan.scratchSize, static_cast<std::size_t>(maxDepth));
                    start = finish(start, pc, entry);
                    accepted = true;
                    break;
                }
            }
            if (boolean)
                break;
            ScalarExpressionPlan::Node node;
            if (instruction.op >= Opcode::IntAdd && instruction.op <= Opcode::IntCmpGe)
                typedInteger = true;
            switch (instruction.op) {
            case Opcode::Call: {
                if (depth < 1 || instruction.b != 1 || instruction.a < 0
                    || instruction.a >= static_cast<int>(program.functions.size())
                    || program.functions[instruction.a].params != 1
                    || !InterpreterBytecodeLowerer::isScalarArgumentLeaf(
                        program.functions[instruction.a])) {
                    depth = -1;
                    break;
                }
                const auto& leaf = program.functions[instruction.a];
                ScalarExpressionPlan::Node constant;
                constant.operation = Operation::Constant;
                constant.operand = static_cast<int>(leaf.code[1].i64);
                constant.divisor = core::Int32Divisor(constant.operand);
                plan.nodes.push_back(constant);
                maxDepth = std::max(maxDepth, depth + 1);
                node.operation = static_cast<Operation>(static_cast<int>(leaf.code[2].op)
                    - static_cast<int>(Opcode::IntAdd) + static_cast<int>(Operation::Add));
                arithmetic = typedInteger = integerConstant = true;
                leafCost += static_cast<int>(leaf.code.size());
                leafFrameValues = std::max({leafFrameValues, leaf.locals, leaf.maxStack});
                break;
            }
            case Opcode::LoadLocal: {
                node.operation = Operation::Local;
                node.operand = instruction.a;
                const auto unbroken = [&](int end) {
                    if (end >= static_cast<int>(code.size()))
                        return false;
                    for (int at = pc + 1; at <= end; ++at)
                        if (boundary[at])
                            return false;
                    return true;
                };
                const auto narrow = [](std::int64_t value) {
                    return value >= std::numeric_limits<int>::min()
                        && value <= std::numeric_limits<int>::max();
                };
                if (!independentStackSpans && unbroken(pc + 3)
                    && code[pc + 1].op == Opcode::LoadLocal
                    && code[pc + 2].op == Opcode::MakeMethodValue
                    && code[pc + 3].op == Opcode::CallValue && code[pc + 3].b == 1) {
                    node.operation = Operation::MethodLeaf;
                    node.call = {code[pc + 1].a, pc + 2};
                    arithmetic = typedInteger = true;
                    pc += 3;
                } else if (independentStackSpans && unbroken(pc + 2)
                    && code[pc + 1].op == Opcode::LoadLocal && code[pc + 2].op == Opcode::CallValue
                    && code[pc + 2].b == 1) {
                    node.operation = Operation::ClosureLeaf;
                    node.call = {code[pc + 1].a, pc + 2};
                    // The original argument and callee coexist above this
                    // span's prefix, even though its postfix node is atomic.
                    maxDepth = std::max(maxDepth, depth + 2);
                    arithmetic = typedInteger = true;
                    pc += 2;
                } else if (unbroken(pc + 6) && code[pc + 1].op == Opcode::LoadLocal
                    && code[pc + 2].op == Opcode::LoadLocal && code[pc + 3].op == Opcode::LoadInt
                    && narrow(code[pc + 3].i64)
                    && (code[pc + 4].op == Opcode::IntMod || code[pc + 4].op == Opcode::Mod)
                    && code[pc + 5].op == Opcode::LoadIndex
                    && code[pc + 6].op == Opcode::LoadIndex) {
                    node.operation = Operation::StringKeyIndex;
                    node.stringKey = {code[pc + 1].a, code[pc + 2].a};
                    node.divisor = core::Int32Divisor(static_cast<int>(code[pc + 3].i64));
                    integerConstant = true;
                    pc += 6;
                } else if (unbroken(pc + 1) && code[pc + 1].op == Opcode::BuiltinLen) {
                    node.operation = Operation::Length;
                    arithmetic = integerConstant = true;
                    ++pc;
                } else if (unbroken(pc + 2) && code[pc + 2].op == Opcode::LoadIndex
                    && ((code[pc + 1].op == Opcode::LoadInt && narrow(code[pc + 1].i64))
                        || code[pc + 1].op == Opcode::LoadLocal)) {
                    node.operation = Operation::Index;
                    node.index.kind = code[pc + 1].op == Opcode::LoadLocal
                        ? ScalarExpressionPlan::Node::IndexKind::Local
                        : ScalarExpressionPlan::Node::IndexKind::Constant;
                    node.index.operand = code[pc + 1].op == Opcode::LoadLocal
                        ? code[pc + 1].a
                        : static_cast<int>(code[pc + 1].i64);
                    integerConstant = true;
                    pc += 2;
                } else if (unbroken(pc + 4) && code[pc + 1].op == Opcode::LoadLocal
                    && code[pc + 2].op == Opcode::LoadInt && narrow(code[pc + 2].i64)
                    && (code[pc + 3].op == Opcode::IntMod || code[pc + 3].op == Opcode::Mod)
                    && code[pc + 4].op == Opcode::LoadIndex) {
                    node.operation = Operation::Index;
                    node.index.kind = ScalarExpressionPlan::Node::IndexKind::LocalRemainder;
                    node.index.operand = code[pc + 1].a;
                    node.divisor = core::Int32Divisor(static_cast<int>(code[pc + 2].i64));
                    integerConstant = true;
                    pc += 4;
                }
                if (node.operation == Operation::Index) {
                    auto& proof = integerDictionaries[static_cast<std::size_t>(instruction.a)];
                    if (proof < 0)
                        proof = PrivateLocalUseAnalysis::hasLiteralIntegerKeys(
                                    function, instruction.a, boundary)
                            ? 1
                            : 0;
                    node.index.dictionary = proof != 0;
                }
                ++depth;
                break;
            }
            case Opcode::LoadInt:
                if (instruction.i64 < std::numeric_limits<int>::min()
                    || instruction.i64 > std::numeric_limits<int>::max()) {
                    depth = -1;
                    break;
                }
                node.operation = Operation::Constant;
                node.operand = static_cast<int>(instruction.i64);
                integerConstant = true;
                node.divisor = core::Int32Divisor(node.operand);
                ++depth;
                break;
            case Opcode::LoadFloat:
                node.operation = Operation::FloatConstant;
                static_assert(sizeof(node.floating) == sizeof(instruction.i64));
                std::memcpy(&node.floating, &instruction.i64, sizeof(node.floating));
                floating = true;
                ++depth;
                break;
            case Opcode::LoadString:
                // The exact following comparison must consume this borrowed
                // literal; string construction/conversion remains on its own
                // ownership/allocator path.
                if (depth != 1 || plan.nodes.size() != static_cast<std::size_t>(offset + 1)
                    || (plan.nodes[offset].operation != Operation::Local
                        && plan.nodes[offset].operation != Operation::Index)
                    || pc + 1 >= static_cast<int>(code.size()) || code[pc + 1].op < Opcode::CmpEq
                    || code[pc + 1].op > Opcode::CmpGe) {
                    depth = -1;
                    break;
                }
                node.operation = Operation::StringConstant;
                node.string = &instruction.s;
                ++depth;
                break;
            case Opcode::IntAdd:
            case Opcode::Add:
                node.operation = Operation::Add;
                --depth;
                arithmetic = true;
                break;
            case Opcode::IntSub:
            case Opcode::Sub:
                node.operation = Operation::Sub;
                --depth;
                arithmetic = true;
                break;
            case Opcode::IntMul:
            case Opcode::Mul:
                node.operation = Operation::Mul;
                --depth;
                arithmetic = true;
                break;
            case Opcode::IntDiv:
            case Opcode::Div:
                node.operation = Operation::Div;
                --depth;
                arithmetic = true;
                break;
            case Opcode::IntMod:
            case Opcode::Mod:
                node.operation = Operation::Mod;
                --depth;
                arithmetic = true;
                break;
            case Opcode::BitAnd:
                node.operation = Operation::BitAnd;
                --depth;
                arithmetic = typedInteger = true;
                break;
            case Opcode::BitOr:
                node.operation = Operation::BitOr;
                --depth;
                arithmetic = typedInteger = true;
                break;
            case Opcode::BitXor:
                node.operation = Operation::BitXor;
                --depth;
                arithmetic = typedInteger = true;
                break;
            case Opcode::BitClear:
                node.operation = Operation::BitClear;
                --depth;
                arithmetic = typedInteger = true;
                break;
            case Opcode::ShiftLeft:
                node.operation = Operation::ShiftLeft;
                --depth;
                arithmetic = typedInteger = true;
                break;
            case Opcode::ShiftRight:
                node.operation = Operation::ShiftRight;
                --depth;
                arithmetic = typedInteger = true;
                break;
            case Opcode::Pow:
                node.operation = Operation::Pow;
                --depth;
                arithmetic = true;
                break;
            case Opcode::FloorDiv:
                node.operation = Operation::FloorDiv;
                --depth;
                arithmetic = true;
                break;
            case Opcode::IntCmpEq:
            case Opcode::CmpEq:
                node.operation = Operation::Equal;
                --depth;
                boolean = arithmetic = true;
                break;
            case Opcode::IntCmpNe:
            case Opcode::CmpNe:
                node.operation = Operation::NotEqual;
                --depth;
                boolean = arithmetic = true;
                break;
            case Opcode::IntCmpLt:
            case Opcode::CmpLt:
                node.operation = Operation::Less;
                --depth;
                boolean = arithmetic = true;
                break;
            case Opcode::IntCmpLe:
            case Opcode::CmpLe:
                node.operation = Operation::LessEqual;
                --depth;
                boolean = arithmetic = true;
                break;
            case Opcode::IntCmpGt:
            case Opcode::CmpGt:
                node.operation = Operation::Greater;
                --depth;
                boolean = arithmetic = true;
                break;
            case Opcode::IntCmpGe:
            case Opcode::CmpGe:
                node.operation = Operation::GreaterEqual;
                --depth;
                boolean = arithmetic = true;
                break;
            default:
                depth = -1;
                break;
            }
            if (depth <= 0)
                break;
            maxDepth = std::max(maxDepth, depth);
            plan.nodes.push_back(node);
        }
        if (!accepted && pushEnd >= 0) {
            plan.nodes.resize(static_cast<std::size_t>(offset + pushCandidate.count));
            plan.scratchSize = std::max(plan.scratchSize, static_cast<std::size_t>(pushScratch));
            start = finish(start, pushEnd, pushCandidate);
            accepted = true;
        }
        if (!accepted)
            plan.nodes.resize(static_cast<std::size_t>(offset));
    }
    if (independentStackSpans) {
        // Reuse only pure, self-contained specialized integer handlers. No
        // return ABI, container access, mutation or joined scalar tails can use
        // a whole-frame proof here. Closed closure entries have their own
        // empty-stack, live carrier and original call-buffer qualification.
        bool any = false;
        for (auto& entry : plan.entries) {
            if (entry.terminal == Terminal::ClosureLoop && !function.isGenerator
                && function.safepoints.empty()) {
                any = true;
                continue;
            }
            const bool terminal = entry.terminal == Terminal::Store
                || entry.terminal == Terminal::Push || entry.terminal == Terminal::BranchFalse
                || entry.terminal == Terminal::BranchTrue;
            const bool scalarOnly = entry.count > 0
                && std::all_of(plan.nodes.begin() + entry.offset,
                    plan.nodes.begin() + entry.offset + entry.count,
                    [](const auto& node) {
                        return node.operation <= Operation::GreaterEqual;
                    });
            if (!terminal || !scalarOnly || !entry.evaluator || entry.floating
                || entry.leafCost != 0 || entry.leafFrameValues != 0 || entry.stepLocal >= 0
                || entry.loopCondition >= 0)
                entry = {};
            else
                any = true;
        }
        plan.independentStackSpans = any;
        plan.hasClosureLoop =
            std::any_of(plan.entries.begin(), plan.entries.end(), [](const auto& entry) {
                return entry.terminal == Terminal::ClosureLoop;
            });
        if (!any)
            plan.entries.clear();
        return plan;
    }
    const bool hasStringAppends =
        PrivateLocalUseAnalysis::prepare(function, boundary, plan, finish);
    const bool hasListReuse = PrivateLocalUseAnalysis::prepareContainers(function, boundary, plan);
    // Same-base, same-index read/modify/write, with no incoming interior edge,
    // handler or suspension boundary. Dynamic payloads are qualified per span;
    // declarations alone never prove the container, key or element kind.
    for (int pc = 0; pc + 7 < static_cast<int>(code.size()); ++pc) {
        if (code[pc].op != Opcode::LoadLocal || code[pc + 1].op != Opcode::LoadLocal
            || code[pc + 2].op != Opcode::LoadLocal || code[pc + 2].a != code[pc].a
            || code[pc + 3].op != Opcode::LoadLocal || code[pc + 3].a != code[pc + 1].a
            || code[pc + 4].op != Opcode::LoadIndex || code[pc + 5].op != Opcode::LoadInt
            || code[pc + 7].op != Opcode::StoreIndex
            || code[pc + 5].i64 < std::numeric_limits<int>::min()
            || code[pc + 5].i64 > std::numeric_limits<int>::max()
            || std::any_of(
                boundary.begin() + pc + 1, boundary.begin() + pc + 8, [](std::uint8_t incoming) {
                    return incoming != 0u;
                }))
            continue;
        Operation operation;
        switch (code[pc + 6].op) {
        case Opcode::IntAdd:
        case Opcode::Add:
            operation = Operation::Add;
            break;
        case Opcode::IntSub:
        case Opcode::Sub:
            operation = Operation::Sub;
            break;
        case Opcode::IntMul:
        case Opcode::Mul:
            operation = Operation::Mul;
            break;
        case Opcode::IntDiv:
        case Opcode::Div:
            operation = Operation::Div;
            break;
        case Opcode::IntMod:
        case Opcode::Mod:
            operation = Operation::Mod;
            break;
        default:
            continue;
        }
        auto& entry = plan.entries[static_cast<std::size_t>(pc)];
        entry = {};
        entry.terminal = Terminal::IndexedStore;
        entry.destination = code[pc].a;
        entry.stepLocal = code[pc + 1].a;
        entry.offset = static_cast<int>(plan.nodes.size());
        ScalarExpressionPlan::Node constant{
            Operation::Constant, static_cast<int>(code[pc + 5].i64)};
        constant.divisor = core::Int32Divisor(constant.operand);
        plan.nodes.push_back(constant);
        plan.nodes.push_back({operation});
        entry.cost = 8;
        entry.lastPc = pc + 7;
        entry.nextPc = pc + 8;
    }
    // Join only the independently proved pure scalar body of a collection
    // iteration. RangeNext itself retains its original source/bridge/slot
    // semantics. No source borrow spans a generator-provider callback.
    if (function.safepoints.empty() && function.exceptionRegions.empty()
        && std::none_of(code.begin(), code.end(), [](const Instruction& instruction) {
               return instruction.op == Opcode::PushHandler;
           })) {
        for (int pc = 0; pc + 2 < static_cast<int>(code.size()); ++pc) {
            if (code[pc].op != Opcode::RangeNext || code[pc + 1].op != Opcode::JumpIfFalse)
                continue;
            const auto& body = plan.entries[static_cast<std::size_t>(pc + 2)];
            if (body.count == 0 || body.terminal != Terminal::Store || body.floating
                || body.stepLocal >= 0 || body.loopCondition >= 0 || body.leafFrameValues != 0
                || body.nextPc != pc || body.lastPc <= pc + 2
                || code[body.lastPc].op != Opcode::Jump || code[pc + 1].a != body.lastPc + 1)
                continue;
            if (std::any_of(boundary.begin() + pc + 1,
                    boundary.begin() + body.lastPc + 1,
                    [](std::uint8_t incoming) {
                        return incoming != 0u;
                    }))
                continue;
            // Pure integer operands and operations only; no index, length,
            // string, call, carrier mutation or independent tail to replay.
            if (std::any_of(plan.nodes.begin() + body.offset,
                    plan.nodes.begin() + body.offset + body.count,
                    [](const auto& node) {
                        return node.operation == Operation::Index
                            || node.operation == Operation::Length
                            || node.operation == Operation::StringKeyIndex
                            || node.operation == Operation::FloatConstant
                            || node.operation == Operation::StringConstant;
                    }))
                continue;
            int key, value;
            unpackRangeTargets(code[pc].i64, key, value);
            const int source = code[pc].a, index = code[pc].b;
            if (source == index || source == key || source == value || index == key
                || index == value || body.destination == source || body.destination == index
                || body.destination == key || body.destination == value)
                continue;
            plan.entries[static_cast<std::size_t>(pc)].rangeBody = pc + 2;
        }
    }
    // Exact conditional update followed by an independent step and the original
    // backedge/condition. All spans already have independent expression proofs;
    // only their control-flow relation is joined here. The two paths retain
    // their different original instruction costs.
    if (function.safepoints.empty() && function.exceptionRegions.empty()) {
        for (std::size_t pc = 0; pc < plan.entries.size(); ++pc) {
            auto& branch = plan.entries[pc];
            if (branch.count != 3 || branch.terminal != Terminal::BranchFalse)
                continue;
            const bool text = branch.evaluator
                && plan.nodes[branch.offset + 1].operation == Operation::StringConstant;
            const bool floating = branch.floatEvaluator
                && plan.nodes[branch.offset].operation == Operation::Local
                && plan.nodes[branch.offset + 1].operation == Operation::FloatConstant;
            if (!text && !floating)
                continue;
            const auto& body = plan.entries[static_cast<std::size_t>(branch.nextPc)];
            const auto& tail = plan.entries[static_cast<std::size_t>(branch.destination)];
            const auto update = [&](const ScalarExpressionPlan::Entry& entry) {
                if (entry.count != 3 || !entry.evaluator || entry.floating
                    || entry.leafFrameValues != 0 || entry.terminal != Terminal::Store
                    || entry.stepLocal >= 0)
                    return false;
                const auto* expression = plan.nodes.data() + entry.offset;
                return expression[0].operation == Operation::Local
                    && expression[0].operand == entry.destination
                    && expression[1].operation == Operation::Constant
                    && (expression[2].operation == Operation::Add
                        || expression[2].operation == Operation::Sub);
            };
            if (!update(body) || !update(tail) || body.loopCondition >= 0
                || body.nextPc != branch.destination || tail.loopCondition < 0
                || (!floating && tail.nextPc != static_cast<int>(pc))
                || (floating && tail.nextPc >= static_cast<int>(pc))
                || body.destination == tail.destination)
                continue;
            const auto& source = plan.nodes[branch.offset];
            if ((source.operation != Operation::Index && source.operation != Operation::Local)
                || source.operand == body.destination || source.operand == tail.destination)
                continue;
            branch.conditionalBody = branch.nextPc;
            branch.conditionalTail = branch.destination;
            if (floating) {
                // An adjacent pure float store supplies the comparison's LHS.
                // Its virtual result is tested before the whole iteration commits.
                branch.conditionalBody = branch.conditionalTail = -1;
                {
                    const int before = tail.nextPc;
                    auto& prefix = plan.entries[static_cast<std::size_t>(before)];
                    if (!boundary[pc] && prefix.terminal == Terminal::Store && prefix.floating
                        && prefix.floatEvaluator && prefix.destination == source.operand
                        && prefix.nextPc == static_cast<int>(pc) && prefix.stepLocal < 0
                        && prefix.loopCondition < 0 && prefix.leafFrameValues == 0
                        && tail.nextPc == static_cast<int>(before)) {
                        prefix.conditionalBody = branch.nextPc;
                        prefix.conditionalTail = branch.destination;
                        prefix.conditionalBranch = static_cast<int>(pc);
                    }
                }
            }
        }
    }
    if (function.safepoints.empty() && function.exceptionRegions.empty()
        && std::none_of(code.begin(), code.end(), [](const Instruction& instruction) {
               return instruction.op == Opcode::PushHandler;
           })) {
        for (int pc = 0; pc < static_cast<int>(code.size()); ++pc) {
            auto& prefix = plan.entries[static_cast<std::size_t>(pc)];
            if (prefix.terminal != Terminal::Store || !prefix.evaluator || prefix.floating
                || prefix.leafFrameValues != 0 || prefix.stepLocal >= 0 || prefix.loopCondition >= 0
                || (prefix.count != 3 && prefix.count != 5) || prefix.conditionalBody >= 0
                || prefix.nextPc <= pc || prefix.nextPc >= static_cast<int>(code.size()))
                continue;
            const auto& update = plan.entries[static_cast<std::size_t>(prefix.nextPc)];
            if (update.terminal != Terminal::IndexedStore || update.stepLocal != prefix.destination
                || update.destination == prefix.destination
                || update.nextPc >= static_cast<int>(code.size()))
                continue;
            const auto& tail = plan.entries[static_cast<std::size_t>(update.nextPc)];
            if (tail.terminal != Terminal::Store || !tail.evaluator || tail.floating
                || tail.leafFrameValues != 0 || tail.loopCondition < 0 || tail.nextPc != pc
                || tail.stepLocal < 0 || tail.destination == prefix.destination
                || tail.destination == update.destination || tail.stepLocal == prefix.destination
                || tail.stepLocal == update.destination || (tail.count != 3 && tail.count != 5))
                continue;
            const auto& condition = plan.entries[static_cast<std::size_t>(tail.loopCondition)];
            const auto* conditionNodes = plan.nodes.data() + condition.offset;
            // BoundLoop virtualizes the tail destination and step. A prefix
            // destination may change boxed/inline representation at its store,
            // so it cannot supply an independently bound condition pointer.
            if (conditionNodes[0].operand == prefix.destination
                || (conditionNodes[1].operation == Operation::Local
                    && conditionNodes[1].operand == prefix.destination))
                continue;
            const int end = update.nextPc + tail.cost - condition.cost;
            if (end > static_cast<int>(code.size()) || end <= update.nextPc
                || std::any_of(
                    boundary.begin() + pc + 1, boundary.begin() + end, [](std::uint8_t incoming) {
                        return incoming != 0u;
                    }))
                continue;
            const auto scalar = [](const ScalarExpressionPlan::Node& node) {
                return node.operation <= Operation::GreaterEqual;
            };
            if (!std::all_of(plan.nodes.begin() + prefix.offset,
                    plan.nodes.begin() + prefix.offset + prefix.count,
                    scalar))
                continue;
            int indices = 0;
            bool valid = true;
            for (int i = tail.offset; i != tail.offset + tail.count; ++i) {
                const auto& node = plan.nodes[static_cast<std::size_t>(i)];
                if (node.operation == Operation::Index) {
                    ++indices;
                    valid = valid && node.operand == update.destination
                        && node.index.kind == ScalarExpressionPlan::Node::IndexKind::Local
                        && node.index.operand == prefix.destination;
                } else
                    valid = valid && scalar(node);
            }
            if (!valid || indices != 1)
                continue;
            // Mutually exclusive with RangeNext's rangeBody and conditional
            // branch composition; no per-PC metadata growth.
            prefix.rangeBody = prefix.nextPc;
            prefix.conditionalTail = update.nextPc;
            prefix.terminal = Terminal::IndexedLoop;
        }
    }
    PrivateLocalUseAnalysis::prepareConstructionOperands(function, boundary, plan);
    // Join only the exact private construction and already-proved pure tail.
    // Incoming edges, suspension and structural writes terminate the borrow.
    if (function.safepoints.empty() && function.exceptionRegions.empty()) {
        for (int pc = 0; pc < static_cast<int>(code.size()); ++pc) {
            auto& prefix = plan.entries[pc];
            if (prefix.terminal != Terminal::ConstructionOperands)
                continue;
            const int constructorPc = prefix.nextPc, tailPc = constructorPc + 2;
            if (tailPc >= static_cast<int>(code.size()))
                continue;
            const auto& constructor = plan.entries[constructorPc];
            const bool dictionary = constructor.dictionaryDestination >= 0;
            const int container =
                dictionary ? constructor.dictionaryDestination : constructor.listDestination;
            auto& tail = plan.entries[tailPc];
            if (container < 0 || tail.terminal != Terminal::Store || tail.count == 0
                || tail.floating || tail.leafCost != 0 || tail.leafFrameValues != 0
                || tail.stepLocal < 0 || tail.loopCondition < 0 || tail.nextPc != pc
                || tail.destination == container || tail.stepLocal == container
                || tail.conditionalBody >= 0 || tail.rangeBody >= 0)
                continue;
            const int end = tailPc + tail.cost - plan.entries[tail.loopCondition].cost;
            if (end <= tailPc || end > static_cast<int>(code.size())
                || std::any_of(
                    boundary.begin() + pc + 1, boundary.begin() + end, [](std::uint8_t incoming) {
                        return incoming != 0u;
                    }))
                continue;
            const auto qualifies = [&](const auto& node) {
                return (node.operation <= Operation::GreaterEqual
                           && (node.operation != Operation::Local || node.operand != container))
                    || (node.operation == Operation::Index && node.operand == container);
            };
            if (!std::all_of(plan.nodes.begin() + tail.offset,
                    plan.nodes.begin() + tail.offset + tail.count,
                    qualifies))
                continue;
            if (dictionary && [&] {
                    for (int i = 0; i < prefix.destination; i += 2) {
                        const auto& root = plan.nodes[prefix.offset + i];
                        if (root.operation != Operation::Constant
                            || !intValueUsesCache(root.operand))
                            return true;
                    }
                    return false;
                }())
                continue;
            if (tail.calculationCount == 0) {
                tail.calculationOffset = static_cast<int>(plan.calculations.size());
                core::pmr::Vector<ScalarExpressionPlan::Node> roots{
                    &core::defaultCpuMemoryResource()};
                plan.lowerOperands(tail, roots);
                assert(roots.size() == 1);
            }
            prefix.terminal = Terminal::ConstructionLoop;
            prefix.rangeBody = tailPc;
            prefix.conditionalTail = container;
            prefix.conditionalBranch = dictionary ? 1 : 0;
        }
    }
    if (plan.nodes.empty() && !hasStringAppends && !hasListReuse)
        plan.entries.clear();
    return plan;
}
} // namespace

InterpreterHandlerFilterPlan InterpreterHandlerFilterPlan::prepare(const Function& function)
{
    InterpreterHandlerFilterPlan plan;
    const auto count =
        std::count_if(function.code.begin(), function.code.end(), [](const auto& instruction) {
            return instruction.op == Opcode::PushHandler;
        });
    plan.entries_.reserve(static_cast<std::size_t>(count));
    for (const auto& instruction : function.code) {
        if (instruction.op != Opcode::PushHandler)
            continue;
        const auto position = std::lower_bound(plan.entries_.begin(),
            plan.entries_.end(),
            instruction.a,
            [](const Entry& entry, int pc) {
                return entry.handlerPc < pc;
            });
        if (position != plan.entries_.end() && position->handlerPc == instruction.a)
            continue;
        ScriptDiagnosticCode code = ScriptDiagnosticCode::None;
        const bool valid =
            instruction.s.empty() || scriptDiagnosticCodeFromName(instruction.s, code);
        const int unobservedLocal =
            valid ? PrivateLocalUseAnalysis::unobservedCatchLocal(function, instruction.a) : -1;
        plan.entries_.insert(position, Entry{instruction.a, code, valid, unobservedLocal});
    }
    return plan;
}

ScriptDiagnosticCode InterpreterHandlerFilterPlan::lookup(
    int handlerPc, bool& valid, int* unobservedLocal) const noexcept
{
    const auto position = std::lower_bound(
        entries_.begin(), entries_.end(), handlerPc, [](const Entry& entry, int pc) {
            return entry.handlerPc < pc;
        });
    valid = position != entries_.end() && position->handlerPc == handlerPc && position->valid;
    if (unobservedLocal)
        *unobservedLocal = valid ? position->unobservedLocal : -1;
    return valid ? position->code : ScriptDiagnosticCode::None;
}

ScriptResult ValidatedInterpreterProgram::create(
    const Program& source, ValidatedInterpreterProgram& out)
{
    out = {};
    try {
        auto snapshot = core::makeShared<Program>(source);
        const auto validation = validateInterpreterBytecode(*snapshot);
        if (!validation.ok())
            return validation;
        const auto stackProofs = InterpreterBytecodeLowerer::analyzeProgramStacks(*snapshot);
        for (std::size_t index = 0; index < snapshot->functions.size(); ++index) {
            auto& function = snapshot->functions[index];
            function.handlerFilters_ = InterpreterHandlerFilterPlan::prepare(function);
            const auto& proof = stackProofs[index];
            // This is a qualification proof, not a second language validator:
            // unsupported CFGs retain the fully checked operand-stack path.
            function.verifiedStackBound_ = proof.status == TypedIrScalarStackStatus::Valid;
            function.scalarDispatch_ = function.verifiedStackBound_
                && InterpreterBytecodeLowerer::isScalarDispatch(function, snapshot.get());
            // A pure one-shot span certifies only its own operands and effects.
            // It can run between suspension/call/handler boundaries without
            // claiming a stack proof for the containing resumable function.
            const bool independentSpans = !function.verifiedStackBound_ && !function.isVariadic;
            function.scalarExpressions_ = function.verifiedStackBound_
                ? prepareScalarExpressions(function, *snapshot)
                : independentSpans ? prepareScalarExpressions(function, *snapshot, true)
                                   : ScalarExpressionPlan{};
            function.scalarExpressions_.argumentLeaf = function.verifiedStackBound_
                && InterpreterBytecodeLowerer::isScalarArgumentLeaf(function);
            if (function.scalarDispatch_)
                function.scalarExpressions_.guardedReturn =
                    ScalarExpressionPlan::GuardedReturn::prepare(
                        function, function.scalarExpressions_);
            if (function.verifiedStackBound_
                && InterpreterBytecodeLowerer::isScalarCaptureLeaf(function)) {
                // Generic frame dispatch still indexes one entry per original
                // PC. Capture leaves have no ordinary local-expression nodes.
                if (function.scalarExpressions_.entries.empty())
                    function.scalarExpressions_.entries.resize(function.code.size());
                auto& leaf = function.scalarExpressions_.captureLeaf;
                const auto& code = function.code;
                leaf.capturedFirst = code[0].op == Opcode::LoadCapture;
                leaf.argument = code[leaf.capturedFirst ? 1 : 0].a;
                leaf.capture = code[leaf.capturedFirst ? 0 : 1].a;
                leaf.cost = static_cast<int>(code.size());
                const auto first = code[2].op >= Opcode::IntAdd ? Opcode::IntAdd : Opcode::Add;
                leaf.operation = static_cast<ScalarExpressionPlan::Operation>(
                    static_cast<int>(code[2].op) - static_cast<int>(first)
                    + static_cast<int>(ScalarExpressionPlan::Operation::Add));
            }
        }
        out.program_ = std::move(snapshot);
        return ScriptResult::success();
    } catch (const std::bad_alloc&) {
        core::CpuMemoryQuotaScope diagnosticScope(nullptr);
        return ScriptResult::failure(ScriptDiagnosticCode::ResourceLimitExceeded,
            core::pmr::String("immutable bytecode snapshot allocation failed",
                &core::defaultCpuMemoryResource()));
    }
}

} // namespace arp::scriptvm

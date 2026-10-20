#include "scriptvm/interpreter/bytecode_lowerer.h"

#include <algorithm>
#include <limits>

namespace arp::scriptvm {

namespace {

ScriptDiagnostic failure(const char* message)
{
    return ScriptDiagnostic{ScriptDiagnosticCode::TypedIrInvalid,
        core::pmr::String(message, &core::defaultCpuMemoryResource())};
}

RuntimeType runtimeType(const TypedIrTypeRef& type)
{
    switch (type.kind) {
    case TypedIrType::Any:
        return RuntimeType::Any;
    case TypedIrType::Int:
        return RuntimeType::Int;
    case TypedIrType::Int64:
        return RuntimeType::Int64;
    case TypedIrType::Float:
        return RuntimeType::Float;
    case TypedIrType::Bool:
        return RuntimeType::Bool;
    case TypedIrType::String:
        return RuntimeType::String;
    case TypedIrType::Bytes:
        return RuntimeType::Bytes;
    case TypedIrType::Object:
        return RuntimeType::Object;
    case TypedIrType::List:
        return RuntimeType::List;
    case TypedIrType::Dict:
        return RuntimeType::Dict;
    case TypedIrType::Function:
        return RuntimeType::Function;
    case TypedIrType::Class:
        return RuntimeType::Class;
    }
    return RuntimeType::Any;
}

Opcode bytecodeOpcode(TypedIrOpcode opcode)
{
#define ARP_MAP_TYPED_IR_OPCODE(name)                                                              \
    case TypedIrOpcode::name:                                                                      \
        return Opcode::name
    switch (opcode) {
        ARP_MAP_TYPED_IR_OPCODE(LoadNil);
        ARP_MAP_TYPED_IR_OPCODE(LoadBool);
        ARP_MAP_TYPED_IR_OPCODE(LoadInt);
        ARP_MAP_TYPED_IR_OPCODE(LoadInt64);
        ARP_MAP_TYPED_IR_OPCODE(LoadFloat);
        ARP_MAP_TYPED_IR_OPCODE(LoadString);
        ARP_MAP_TYPED_IR_OPCODE(LoadLocal);
        ARP_MAP_TYPED_IR_OPCODE(LoadCapture);
        ARP_MAP_TYPED_IR_OPCODE(LoadGlobal);
        ARP_MAP_TYPED_IR_OPCODE(StoreLocal);
        ARP_MAP_TYPED_IR_OPCODE(StoreCapture);
        ARP_MAP_TYPED_IR_OPCODE(StoreGlobal);
        ARP_MAP_TYPED_IR_OPCODE(Pop);
        ARP_MAP_TYPED_IR_OPCODE(Return);
        ARP_MAP_TYPED_IR_OPCODE(Add);
        ARP_MAP_TYPED_IR_OPCODE(Sub);
        ARP_MAP_TYPED_IR_OPCODE(Mul);
        ARP_MAP_TYPED_IR_OPCODE(Div);
        ARP_MAP_TYPED_IR_OPCODE(Mod);
        ARP_MAP_TYPED_IR_OPCODE(Neg);
        ARP_MAP_TYPED_IR_OPCODE(Not);
        ARP_MAP_TYPED_IR_OPCODE(BitAnd);
        ARP_MAP_TYPED_IR_OPCODE(BitOr);
        ARP_MAP_TYPED_IR_OPCODE(BitXor);
        ARP_MAP_TYPED_IR_OPCODE(BitClear);
        ARP_MAP_TYPED_IR_OPCODE(ShiftLeft);
        ARP_MAP_TYPED_IR_OPCODE(ShiftRight);
        ARP_MAP_TYPED_IR_OPCODE(CmpEq);
        ARP_MAP_TYPED_IR_OPCODE(CmpNe);
        ARP_MAP_TYPED_IR_OPCODE(CmpLt);
        ARP_MAP_TYPED_IR_OPCODE(CmpLe);
        ARP_MAP_TYPED_IR_OPCODE(CmpGt);
        ARP_MAP_TYPED_IR_OPCODE(CmpGe);
        ARP_MAP_TYPED_IR_OPCODE(Jump);
        ARP_MAP_TYPED_IR_OPCODE(JumpIfFalse);
        ARP_MAP_TYPED_IR_OPCODE(JumpIfTrue);
        ARP_MAP_TYPED_IR_OPCODE(Call);
        ARP_MAP_TYPED_IR_OPCODE(CallValue);
        ARP_MAP_TYPED_IR_OPCODE(CallHost);
        ARP_MAP_TYPED_IR_OPCODE(MakeFuncValue);
        ARP_MAP_TYPED_IR_OPCODE(MakeClosure);
        ARP_MAP_TYPED_IR_OPCODE(MakeMethodValue);
        ARP_MAP_TYPED_IR_OPCODE(MakeList);
        ARP_MAP_TYPED_IR_OPCODE(MakeDict);
        ARP_MAP_TYPED_IR_OPCODE(MakeInstance);
        ARP_MAP_TYPED_IR_OPCODE(MakeRange);
        ARP_MAP_TYPED_IR_OPCODE(MakeRangeFromStack);
        ARP_MAP_TYPED_IR_OPCODE(LoadIndex);
        ARP_MAP_TYPED_IR_OPCODE(StoreIndex);
        ARP_MAP_TYPED_IR_OPCODE(LoadField);
        ARP_MAP_TYPED_IR_OPCODE(StoreField);
        ARP_MAP_TYPED_IR_OPCODE(AddrIndex);
        ARP_MAP_TYPED_IR_OPCODE(AddrField);
        ARP_MAP_TYPED_IR_OPCODE(AddrLocal);
        ARP_MAP_TYPED_IR_OPCODE(AddrCapture);
        ARP_MAP_TYPED_IR_OPCODE(AddrGlobal);
        ARP_MAP_TYPED_IR_OPCODE(Deref);
        ARP_MAP_TYPED_IR_OPCODE(StoreIndirect);
        ARP_MAP_TYPED_IR_OPCODE(BuiltinLen);
        ARP_MAP_TYPED_IR_OPCODE(BuiltinAppend);
        ARP_MAP_TYPED_IR_OPCODE(BuiltinDelete);
        ARP_MAP_TYPED_IR_OPCODE(BuiltinClear);
        ARP_MAP_TYPED_IR_OPCODE(RangeInit);
        ARP_MAP_TYPED_IR_OPCODE(RangeNext);
        ARP_MAP_TYPED_IR_OPCODE(ConvertInt);
        ARP_MAP_TYPED_IR_OPCODE(ConvertInt64);
        ARP_MAP_TYPED_IR_OPCODE(ConvertFloat);
        ARP_MAP_TYPED_IR_OPCODE(ConvertString);
        ARP_MAP_TYPED_IR_OPCODE(ConvertBool);
        ARP_MAP_TYPED_IR_OPCODE(ConvertBytes);
        ARP_MAP_TYPED_IR_OPCODE(LocalAddIntConst);
        ARP_MAP_TYPED_IR_OPCODE(LocalSubIntConst);
        ARP_MAP_TYPED_IR_OPCODE(LocalAddLocal);
        ARP_MAP_TYPED_IR_OPCODE(LocalAddFloatConst);
        ARP_MAP_TYPED_IR_OPCODE(LocalSubFloatConst);
        ARP_MAP_TYPED_IR_OPCODE(JumpIfLocalIntLtConstFalse);
        ARP_MAP_TYPED_IR_OPCODE(IntAdd);
        ARP_MAP_TYPED_IR_OPCODE(IntSub);
        ARP_MAP_TYPED_IR_OPCODE(IntMul);
        ARP_MAP_TYPED_IR_OPCODE(IntDiv);
        ARP_MAP_TYPED_IR_OPCODE(IntMod);
        ARP_MAP_TYPED_IR_OPCODE(IntCmpEq);
        ARP_MAP_TYPED_IR_OPCODE(IntCmpNe);
        ARP_MAP_TYPED_IR_OPCODE(IntCmpLt);
        ARP_MAP_TYPED_IR_OPCODE(IntCmpLe);
        ARP_MAP_TYPED_IR_OPCODE(IntCmpGt);
        ARP_MAP_TYPED_IR_OPCODE(IntCmpGe);
        ARP_MAP_TYPED_IR_OPCODE(ReturnInt);
        ARP_MAP_TYPED_IR_OPCODE(Yield);
        ARP_MAP_TYPED_IR_OPCODE(Await);
        ARP_MAP_TYPED_IR_OPCODE(PushHandler);
        ARP_MAP_TYPED_IR_OPCODE(PopHandler);
        ARP_MAP_TYPED_IR_OPCODE(Throw);
        ARP_MAP_TYPED_IR_OPCODE(LoadModule);
        ARP_MAP_TYPED_IR_OPCODE(LoadModuleExport);
        ARP_MAP_TYPED_IR_OPCODE(Pow);
        ARP_MAP_TYPED_IR_OPCODE(FloorDiv);
        ARP_MAP_TYPED_IR_OPCODE(LocalAddInt64Const);
        ARP_MAP_TYPED_IR_OPCODE(LocalSubInt64Const);
    case TypedIrOpcode::OpcodeCount:
        break;
    }
#undef ARP_MAP_TYPED_IR_OPCODE
    return Opcode::OpcodeCount;
}

LocalKind localKind(TypedIrLocalKind kind)
{
    switch (kind) {
    case TypedIrLocalKind::Param:
        return LocalKind::Param;
    case TypedIrLocalKind::Result:
        return LocalKind::Result;
    case TypedIrLocalKind::Local:
        return LocalKind::Local;
    case TypedIrLocalKind::Capture:
        return LocalKind::Capture;
    }
    return LocalKind::Local;
}

int resultCount(const TypedIrFunction& function)
{
    int count = -1;
    for (const auto& block : function.blocks) {
        for (const auto& operation : block.operations) {
            if (operation.opcode != TypedIrOpcode::Return
                && operation.opcode != TypedIrOpcode::ReturnInt)
                continue;
            const int current = operation.opcode == TypedIrOpcode::ReturnInt ? 1 : operation.a;
            if (current < 0 || current > 1 || (count >= 0 && count != current))
                return -2;
            count = current;
        }
    }
    return count < 0 ? 0 : count;
}

bool identityMatches(const TypedIrModule& module, const ScriptArtifactIdentity& identity)
{
    return identity.complete() && identity.moduleId == module.moduleId
        && identity.moduleVersion == module.moduleVersion
        && identity.semanticAbiVersion == module.semanticAbiVersion
        && identity.typedIrVersion == module.typedIrVersion
        && identity.typedIrFingerprint == module.fingerprint;
}

} // namespace

TypedIrOpcode InterpreterBytecodeLowerer::canonicalOpcode(Opcode opcode)
{
    for (int index = 0; index < static_cast<int>(TypedIrOpcode::OpcodeCount); ++index) {
        const auto candidate = static_cast<TypedIrOpcode>(index);
        if (bytecodeOpcode(candidate) == opcode)
            return candidate;
    }
    return TypedIrOpcode::OpcodeCount;
}

namespace {
class ScalarBinaryLeafQualification final {
public:
    static bool common(const Function& function)
    {
        return !(function.isGenerator || function.isVariadic || function.params < 1
                   || function.results != 1 || function.locals < function.params
                   || function.maxStack < 2 || !function.safepoints.empty()
                   || !function.exceptionRegions.empty() || function.code.size() != 4)
            && function.code[3].op == Opcode::ReturnInt;
    }

    static bool parameter(const Function& function, const Instruction& instruction)
    {
        return instruction.op == Opcode::LoadLocal && instruction.a >= 0
            && instruction.a < function.params;
    }

    static bool capture(const Function& function, const Instruction& instruction)
    {
        return instruction.op == Opcode::LoadCapture && instruction.a >= 0
            && instruction.a < function.captureCount;
    }
};
} // namespace

bool InterpreterBytecodeLowerer::isScalarArgumentLeaf(const Function& function)
{
    if (!ScalarBinaryLeafQualification::common(function) || function.captureCount != 0)
        return false;
    const auto& code = function.code;
    if (!ScalarBinaryLeafQualification::parameter(function, code[0])
        || code[1].op != Opcode::LoadInt || code[1].i64 < std::numeric_limits<int>::min()
        || code[1].i64 > std::numeric_limits<int>::max())
        return false;
    return code[2].op >= Opcode::IntAdd && code[2].op <= Opcode::IntMod;
}

bool InterpreterBytecodeLowerer::isScalarCaptureLeaf(const Function& function)
{
    if (!ScalarBinaryLeafQualification::common(function) || function.captureCount < 1)
        return false;
    const auto& code = function.code;
    const bool operands = (ScalarBinaryLeafQualification::parameter(function, code[0])
                              && ScalarBinaryLeafQualification::capture(function, code[1]))
        || (ScalarBinaryLeafQualification::capture(function, code[0])
            && ScalarBinaryLeafQualification::parameter(function, code[1]));
    return operands
        && ((code[2].op >= Opcode::IntAdd && code[2].op <= Opcode::IntMod)
            || (code[2].op >= Opcode::Add && code[2].op <= Opcode::Mod));
}

bool InterpreterBytecodeLowerer::isScalarDispatch(const Function& function, const Program* program)
{
    if (function.isGenerator || function.isVariadic || function.captureCount != 0
        || function.results != 1 || !function.safepoints.empty()
        || !function.exceptionRegions.empty())
        return false;
    bool calls = false, backEdge = false;
    for (std::size_t pc = 0; pc < function.code.size(); ++pc) {
        const auto& instruction = function.code[pc];
        switch (instruction.op) {
        case Opcode::Call:
            // A factory has a fixed one-value stack effect, but this compact
            // driver executes ordinary synchronous calls only. Select the
            // full factory handler at snapshot preparation, never at runtime.
            if (program == nullptr || instruction.a < 0
                || instruction.a >= static_cast<int>(program->functions.size())
                || program->functions[instruction.a].isGenerator)
                return false;
            calls = true;
            break;
        case Opcode::Jump:
        case Opcode::JumpIfFalse:
        case Opcode::JumpIfTrue:
        case Opcode::JumpIfLocalIntLtConstFalse:
            backEdge |= instruction.a <= static_cast<int>(pc);
            break;
        case Opcode::Return:
            if (instruction.a != 1)
                return false;
            break;
        case Opcode::LoadInt:
            if (instruction.i64 < std::numeric_limits<int>::min()
                || instruction.i64 > std::numeric_limits<int>::max())
                return false;
            break;
        case Opcode::LoadFloat:
            break;
        case Opcode::LoadLocal:
        case Opcode::StoreLocal:
        case Opcode::Pop:
        case Opcode::LocalAddIntConst:
        case Opcode::LocalSubIntConst:
        case Opcode::ReturnInt:
        case Opcode::IntAdd:
        case Opcode::IntSub:
        case Opcode::IntMul:
        case Opcode::IntDiv:
        case Opcode::IntMod:
        case Opcode::Add:
        case Opcode::Sub:
        case Opcode::Mul:
        case Opcode::Div:
        case Opcode::Mod:
        case Opcode::Pow:
        case Opcode::FloorDiv:
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
        case Opcode::IntCmpEq:
        case Opcode::IntCmpNe:
        case Opcode::IntCmpLt:
        case Opcode::IntCmpLe:
        case Opcode::IntCmpGt:
        case Opcode::IntCmpGe:
            break;
        default:
            return false;
        }
    }
    // Call-bearing loops retain the existing expression/leaf/bound-loop
    // dispatcher; call-free scalar control flow uses the same scalar handlers
    // here. Eligibility follows bytecode, never script names or input values.
    return !calls || !backEdge;
}

namespace {
// A bound method is produced by this exact bytecode predecessor, rather than
// imported from a host/capture/local. Its runtime dispatch can select only the
// fallback or an immutable methodSets override. Record every possible target
// so the existing one-result dependency proof must certify all of them.
class BoundMethodStackQualification final {
public:
    using Dependencies = core::pmr::Vector<int>;

    static bool collect(const Function& function,
        const Program& program,
        std::size_t pc,
        Dependencies* dependencies)
    {
        const auto& call = function.code[pc];
        if (pc == 0u || call.b < 0 || call.b == std::numeric_limits<int>::max()
            || function.code[pc - 1u].op != Opcode::MakeMethodValue)
            return false;
        if (std::any_of(function.code.begin(), function.code.end(), [pc](const auto& instruction) {
                return (instruction.op == Opcode::Jump || instruction.op == Opcode::JumpIfFalse
                           || instruction.op == Opcode::JumpIfTrue
                           || instruction.op == Opcode::JumpIfLocalIntLtConstFalse
                           || instruction.op == Opcode::PushHandler)
                    && instruction.a == static_cast<int>(pc);
            }))
            return false;
        const auto target = [&](int index) {
            if (index < 0 || index >= static_cast<int>(program.functions.size()))
                return false;
            const auto& callee = program.functions[static_cast<std::size_t>(index)];
            if (callee.isVariadic || callee.isGenerator || callee.captureCount != 0
                || callee.params != call.b + 1)
                return false;
            if (dependencies)
                dependencies->push_back(index);
            return true;
        };
        const auto& method = function.code[pc - 1u];
        if (!target(method.a))
            return false;
        if (!method.s.empty()) {
            for (const auto& [className, methods] : program.methodSets) {
                const auto override = methods.find(method.s);
                if (override != methods.end() && !target(override->second))
                    return false;
            }
        }
        return true;
    }
};

TypedIrScalarControlFlowAnalysis analyzeLocalStack(const Function& function,
    const Program* program,
    BoundMethodStackQualification::Dependencies* dependencies = nullptr)
{
    for (std::size_t pc = 0u; pc != function.code.size(); ++pc) {
        const auto& instruction = function.code[pc];
        switch (instruction.op) {
        case Opcode::Call:
            if (program == nullptr || instruction.b < 0 || instruction.a < 0
                || instruction.a >= static_cast<int>(program->functions.size())
                || program->functions[instruction.a].isVariadic
                || instruction.b != program->functions[instruction.a].params)
                return {};
            // Call constructs a generator without executing its body. On
            // success the original factory always pushes exactly one real
            // generator; yield/await/handler CFG in that body cannot change
            // the caller's operand-stack effect. Ordinary calls still need
            // the complete transitive one-result dependency proof.
            if (dependencies && !program->functions[instruction.a].isGenerator)
                dependencies->push_back(instruction.a);
            break;
        case Opcode::CallValue:
            if (program == nullptr
                || !BoundMethodStackQualification::collect(function, *program, pc, dependencies))
                return {};
            break;
        case Opcode::MakeInstance:
            // Native constructors and script __init__ can suspend or execute
            // unknown code. Only a schema-backed default script instance has
            // the unconditional one-value stack contract used here.
            if (program == nullptr || instruction.b != 0
                || program->classSchemas.find(instruction.s) == program->classSchemas.end()
                || program->constructorIndex.find(instruction.s) != program->constructorIndex.end())
                return {};
            break;
        case Opcode::CallHost:
        case Opcode::Yield:
        case Opcode::Await:
        case Opcode::PushHandler:
        case Opcode::PopHandler:
        case Opcode::Throw:
            return {};
        default:
            break;
        }
    }
    TypedIrFunction canonical;
    canonical.parameterCount = function.params;
    canonical.localCount = function.locals;
    canonical.captureCount = function.captureCount;
    canonical.maxStack = function.maxStack;
    canonical.blocks.emplace_back();
    auto& operations = canonical.blocks.back().operations;
    operations.reserve(function.code.size());
    for (const auto& instruction : function.code) {
        // Shared CFG analysis checks the real Call arity. Its one-result
        // assumption is certified for the entire static dependency graph below.
        // This canonical copy is never executed or serialized.
        const auto opcode = InterpreterBytecodeLowerer::canonicalOpcode(instruction.op);
        if (opcode == TypedIrOpcode::OpcodeCount)
            return {};
        operations.emplace_back();
        auto& operation = operations.back();
        operation.opcode = opcode;
        operation.a = instruction.a;
        operation.b = instruction.b;
        operation.i64 = instruction.i64;
        operation.text = instruction.s;
        operation.source = instruction.pos;
    }
    return analyzeTypedIrScalarControlFlow(canonical);
}

} // namespace

core::pmr::Vector<TypedIrScalarControlFlowAnalysis>
InterpreterBytecodeLowerer::analyzeProgramStacks(const Program& program)
{
    core::pmr::Vector<TypedIrScalarControlFlowAnalysis> proofs{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::uint8_t> oneResult(
        program.functions.size(), 0u, &core::defaultCpuMemoryResource());
    core::pmr::Vector<BoundMethodStackQualification::Dependencies> dependencies{
        &core::defaultCpuMemoryResource()};
    proofs.reserve(program.functions.size());
    dependencies.reserve(program.functions.size());
    for (std::size_t index = 0; index < program.functions.size(); ++index) {
        const auto& function = program.functions[index];
        dependencies.emplace_back();
        proofs.push_back(analyzeLocalStack(function, &program, &dependencies.back()));
        bool returnsOne = function.results == 1 && !function.isGenerator && !function.isVariadic
            && function.captureCount == 0 && function.safepoints.empty()
            && function.exceptionRegions.empty()
            && proofs.back().status == TypedIrScalarStackStatus::Valid;
        for (const auto& instruction : function.code)
            if (instruction.op == Opcode::Return && instruction.a != 1)
                returnsOne = false;
        oneResult[index] = returnsOne;
    }
    // Greatest fixed point: every admitted dependency has a complete local CFG
    // with no fallthrough and only one-result returns. Recursive components may
    // return one value or fail their existing budget/recursion/error gates; they
    // cannot succeed with a different stack effect. Disqualifications propagate
    // through all static edges, including mutual recursion.
    bool changed;
    do {
        changed = false;
        for (std::size_t index = 0; index < program.functions.size(); ++index) {
            if (!oneResult[index])
                continue;
            for (const int target : dependencies[index]) {
                if (!oneResult[static_cast<std::size_t>(target)]) {
                    oneResult[index] = 0u;
                    changed = true;
                    break;
                }
            }
        }
    } while (changed);
    for (std::size_t index = 0; index < program.functions.size(); ++index) {
        if (proofs[index].status != TypedIrScalarStackStatus::Valid)
            continue;
        for (const int target : dependencies[index]) {
            if (!oneResult[static_cast<std::size_t>(target)]) {
                proofs[index] = {};
                break;
            }
        }
    }
    return proofs;
}

TypedIrScalarControlFlowAnalysis InterpreterBytecodeLowerer::analyzeStack(
    const Function& function, const Program* program)
{
    if (program) {
        for (std::size_t index = 0; index < program->functions.size(); ++index)
            if (&function == &program->functions[index])
                return analyzeProgramStacks(*program)[index];
    }
    return analyzeLocalStack(function, nullptr);
}

BytecodeLoweringResult InterpreterBytecodeLowerer::lower(
    const TypedIrModule& module, const InterpreterBytecodeOptions& options)
{
    BytecodeLoweringResult result;
    const auto validation = validateTypedIr(module);
    if (!validation.ok()) {
        result.error = validation.diagnostic();
        return result;
    }
    if (!identityMatches(module, options.artifactIdentity)) {
        result.error = ScriptDiagnostic{ScriptDiagnosticCode::AotFingerprintMismatch,
            core::pmr::String("interpreter bytecode identity does not match typed IR",
                &core::defaultCpuMemoryResource())};
        return result;
    }

    core::pmr::Map<core::pmr::String, int> functionIndices{&core::defaultCpuMemoryResource()};
    for (std::size_t index = 0; index < module.functions.size(); ++index)
        functionIndices[module.functions[index].id] = static_cast<int>(index);

    Program program;
    program.bytecodeVersion = kInterpreterBytecodeVersion;
    program.artifactIdentity = options.artifactIdentity;
    program.globalCount = module.globalCount;
    for (const auto& global : module.globals)
        program.globalSlots.emplace(global.name, global.slot);
    program.init = -1;
    if (!module.moduleInitFunctionId.empty()) {
        const auto initializer = functionIndices.find(module.moduleInitFunctionId);
        if (initializer == functionIndices.end()) {
            result.error = failure("interpreter module initializer target is unresolved");
            return result;
        }
        program.init = initializer->second;
    }
    program.functions.reserve(module.functions.size());

    for (const auto& source : module.functions) {
        Function function(source.name);
        function.id = source.id;
        function.params = source.parameterCount;
        function.fixedParams = source.signature.fixedParameterCount;
        function.isVariadic = source.signature.variadic;
        function.isGenerator = source.isGenerator;
        function.hasYieldFrom = source.hasYieldFrom;
        function.locals = source.localCount;
        function.captureCount = source.captureCount;
        function.maxStack = source.maxStack;
        function.resultType = runtimeType(source.signature.result);
        function.results = resultCount(source);
        if (function.results < 0) {
            result.error = failure("typed IR function has inconsistent result counts");
            return result;
        }
        std::size_t operationCount = 0u;
        for (const auto& block : source.blocks)
            operationCount += block.operations.size();
        core::pmr::Vector<int> operationPcs(
            operationCount + 1u, -1, &core::defaultCpuMemoryResource());
        for (std::size_t index = 0u; index <= operationCount; ++index)
            operationPcs[index] = static_cast<int>(index);
        const auto resolveOperationPc = [&operationPcs, operationCount](
                                            int operation, bool allowEnd = false) {
            if (operation < 0 || (!allowEnd && operation >= static_cast<int>(operationCount))
                || (allowEnd && operation > static_cast<int>(operationCount)))
                return -1;
            return operationPcs[static_cast<std::size_t>(operation)];
        };
        for (const auto& parameter : source.signature.parameters) {
            function.paramTypes.push_back(runtimeType(parameter));
            function.nullableParams.push_back(parameter.nullable ? 1u : 0u);
            function.paramClassNames.push_back(parameter.className);
        }
        function.variadicElementType = runtimeType(source.signature.variadicElement);
        function.variadicElementNullable = source.signature.variadicElement.nullable;
        function.variadicElementClassName = source.signature.variadicElement.className;
        for (const auto& local : source.locals) {
            DebugLocal debug;
            debug.name = local.name;
            debug.slot = local.slot;
            debug.kind = localKind(local.kind);
            debug.typeName = local.type.className.empty()
                ? core::pmr::String(std::to_string(static_cast<int>(local.type.kind)),
                      &core::defaultCpuMemoryResource())
                : local.type.className;
            debug.startPc = resolveOperationPc(local.startOperation, true);
            debug.endPc = resolveOperationPc(local.endOperation, true);
            if (debug.startPc < 0 || debug.endPc < debug.startPc) {
                result.error = failure("typed IR debug-local range cannot map to bytecode");
                return result;
            }
            function.debugLocals.push_back(std::move(debug));
        }

        for (const auto& safepoint : source.safepoints) {
            BytecodeSafepoint mapped;
            mapped.id = safepoint.id;
            mapped.pc = resolveOperationPc(safepoint.operationIndex);
            mapped.resumeLabel = safepoint.resumeLabel;
            mapped.source = safepoint.source;
            mapped.canonicalSlots = safepoint.canonicalSlots;
            if (mapped.pc < 0) {
                result.error = failure("typed IR safepoint cannot map to bytecode");
                return result;
            }
            function.safepoints.push_back(std::move(mapped));
        }
        for (const auto& region : source.exceptionRegions) {
            BytecodeExceptionRegion mapped;
            mapped.id = region.id;
            mapped.startPc = resolveOperationPc(region.startOperation, true);
            mapped.endPc = resolveOperationPc(region.endOperation, true);
            mapped.handlerPc = resolveOperationPc(region.handlerOperation);
            mapped.handlerBlockId = region.handlerBlockId;
            mapped.catchType = region.catchType;
            mapped.source = region.source;
            if (mapped.startPc < 0 || mapped.endPc < mapped.startPc || mapped.handlerPc < 0) {
                result.error = failure("typed IR exception region cannot map to bytecode");
                return result;
            }
            function.exceptionRegions.push_back(std::move(mapped));
        }

        for (const auto& block : source.blocks) {
            for (const auto& operation : block.operations) {
                const auto opcode = bytecodeOpcode(operation.opcode);
                if (opcode == Opcode::OpcodeCount) {
                    result.error =
                        failure("interpreter bytecode lowering has no typed IR opcode mapping");
                    return result;
                }
                int a = operation.a;
                const bool branch = opcode == Opcode::Jump || opcode == Opcode::JumpIfFalse
                    || opcode == Opcode::JumpIfTrue || opcode == Opcode::JumpIfLocalIntLtConstFalse
                    || opcode == Opcode::PushHandler;
                if (branch) {
                    a = resolveOperationPc(operation.a, opcode != Opcode::PushHandler);
                    if (a < 0) {
                        result.error = failure("typed IR branch target cannot map to bytecode");
                        return result;
                    }
                }
                if (opcode == Opcode::Call || opcode == Opcode::MakeFuncValue
                    || opcode == Opcode::MakeClosure || opcode == Opcode::MakeMethodValue) {
                    const auto target = functionIndices.find(operation.targetId);
                    if (target == functionIndices.end()) {
                        result.error =
                            failure("interpreter bytecode function target is unresolved");
                        return result;
                    }
                    a = target->second;
                }
                core::pmr::String text = operation.text;
                if (opcode == Opcode::CallHost) {
                    if (operation.callableBindingId.empty() || operation.targetId.empty()) {
                        result.error =
                            failure("interpreter host call has no stable callable binding");
                        return result;
                    }
                    a = -1;
                    text = operation.callableBindingId;
                }
                function.code.emplace_back(
                    opcode, a, operation.b, operation.i64, std::move(text), operation.source);
            }
        }
        for (const auto& site : source.delegationSites) {
            DelegationSiteMetadata mapped{resolveOperationPc(site.sourcePc),
                resolveOperationPc(site.initPc),
                resolveOperationPc(site.rangePc),
                resolveOperationPc(site.branchPc),
                resolveOperationPc(site.loadPc),
                resolveOperationPc(site.yieldPc),
                resolveOperationPc(site.backEdgePc),
                resolveOperationPc(site.exitPc, true),
                site.sourceSlot,
                site.indexSlot,
                site.valueSlot};
            if (mapped.sourcePc < 0 || mapped.initPc < 0 || mapped.rangePc < 0
                || mapped.branchPc < 0 || mapped.loadPc < 0 || mapped.yieldPc < 0
                || mapped.backEdgePc < 0 || mapped.exitPc < 0) {
                result.error = failure("typed IR delegation site cannot map to bytecode");
                return result;
            }
            function.delegationSites.push_back(std::move(mapped));
        }
        program.funcIndex[function.name] = static_cast<int>(program.functions.size());
        program.functionIdIndex[function.id] = static_cast<int>(program.functions.size());
        program.functions.push_back(std::move(function));
    }

    for (const auto& schema : module.scriptClassSchemas) {
        program.classSchemas[schema.className] = schema.fields;
        auto& methods = program.methodSets[schema.className];
        for (const auto& [name, functionId] : schema.methods) {
            const auto target = functionIndices.find(functionId);
            if (target == functionIndices.end()) {
                result.error = failure("script class method target is unresolved");
                return result;
            }
            methods[name] = target->second;
        }
        if (!schema.constructorFunctionId.empty()) {
            const auto constructor = functionIndices.find(schema.constructorFunctionId);
            if (constructor == functionIndices.end()) {
                result.error = failure("script class constructor target is unresolved");
                return result;
            }
            program.constructorIndex[schema.className] = constructor->second;
        }
    }

    if (!options.entryFunctionId.empty()) {
        const auto entry = functionIndices.find(options.entryFunctionId);
        if (entry == functionIndices.end()) {
            result.error = failure("interpreter entry function target is unresolved");
            return result;
        }
        program.entry = entry->second;
    }
    program.bytecodeFingerprint = computeInterpreterBytecodeFingerprint(program);
    const auto bytecodeValidation = validateInterpreterBytecode(program);
    if (!bytecodeValidation.ok()) {
        result.error = bytecodeValidation.diagnostic();
        return result;
    }
    result.receipt.typedIrFingerprint = module.fingerprint;
    result.receipt.bytecodeFingerprint = program.bytecodeFingerprint;
    result.receipt.bytecodeVersion = program.bytecodeVersion;
    result.program = std::move(program);
    return result;
}

} // namespace arp::scriptvm

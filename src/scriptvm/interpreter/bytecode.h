#pragma once

#include "scriptvm/interpreter/scalar_expression.h"

// ArpScript: bytecode IR.
// Instruction format mirrors goes: a struct (not compact byte stream) with
// Op + A + B + I64 + S fields, plus a SourcePos for debugging.
// Program/Function layout follows goes but adapts to the thick-bridge model
// (no separate HostFunctions table -- host calls route through the reflection db).

#include "core/memory/cpu_allocator.h"
#include "scriptvm/runtime/artifact_identity.h"
#include "scriptvm/runtime/diagnostics.h"
#include "scriptvm/runtime/source_pos.h"
#include "scriptvm/runtime/type_contract.h"

#include <cstdint>
#include <functional>

namespace arp::scriptvm {

inline constexpr std::uint32_t kInterpreterBytecodeVersion = 1u;

enum class Opcode : std::uint8_t {
    // --- literals / load ---
    LoadNil = 0,
    LoadBool,    // A = 0/1
    LoadInt,     // I64 = value
    LoadInt64,   // I64 = value
    LoadFloat,   // I64 = bits(double) -- avoids string parse at runtime
    LoadString,  // S = value
    LoadLocal,   // A = slot
    LoadCapture, // A = capture slot
    LoadGlobal,  // A = global slot

    // --- store ---
    StoreLocal,   // A = slot
    StoreCapture, // A = capture slot
    StoreGlobal,  // A = global slot
    Pop,
    Return, // A = result count

    // --- arithmetic (route through reflection::object operators) ---
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    Neg,
    Not,

    // --- bitwise ---
    BitAnd,
    BitOr,
    BitXor,
    BitClear, // &^
    ShiftLeft,
    ShiftRight,

    // --- comparison ---
    CmpEq,
    CmpNe,
    CmpLt,
    CmpLe,
    CmpGt,
    CmpGe,

    // --- jumps ---
    Jump,        // A = target
    JumpIfFalse, // A = target
    JumpIfTrue,  // A = target

    // --- calls ---
    Call,      // A = func index, B = arg count
    CallValue, // B = arg count (callee on stack top)
    CallHost,  // S = stable binding ID, B = arg count (A is v0 test-only)

    // --- function values ---
    MakeFuncValue,   // A = func index
    MakeClosure,     // A = func index, B = capture count
    MakeMethodValue, // A = method index

    // --- container construction ---
    MakeList,           // B = element count
    MakeDict,           // B = pair count (key,value,key,value...)
    MakeInstance,       // S = class name, B = constructor argument count
    MakeRange,          // pushes a range iterable [I64_a, I64_b)
    MakeRangeFromStack, // B = arg count (1=end, 2=start,end); pops and builds list

    // --- container access ---
    LoadIndex,     // pop index, pop base, push elem
    StoreIndex,    // pop value, pop index, pop base
    LoadField,     // A = field index, pop struct, push field
    StoreField,    // A = field index, pop value, pop struct
    AddrIndex,     // pop index, pop base, push pointer
    AddrField,     // A = field index, pop struct, push pointer
    AddrLocal,     // A = slot, push pointer
    AddrCapture,   // A = capture slot, push pointer
    AddrGlobal,    // A = global slot, push pointer
    Deref,         // pop pointer, push value
    StoreIndirect, // pop value, pop pointer, store

    // --- builtins ---
    BuiltinLen,
    BuiltinAppend, // pop value(s), pop list, append
    BuiltinDelete, // pop key, pop container
    BuiltinClear,  // pop container, clear
    RangeInit,     // A = source slot, B = index slot
    RangeNext,     // A = source slot, B = index slot, I64 = packed(keySlot,valueSlot)

    // --- conversions ---
    ConvertInt,
    ConvertInt64,
    ConvertFloat,
    ConvertString,
    ConvertBool,
    ConvertBytes,

    // --- fast-path (int in-place, Phase 7) ---
    LocalAddIntConst,           // A = slot, I64 = const
    LocalSubIntConst,           // A = slot, I64 = const
    LocalAddLocal,              // A = dst, B = src
    LocalAddFloatConst,         // A = slot, I64 = IEEE-754 double bits
    LocalSubFloatConst,         // A = slot, I64 = IEEE-754 double bits
    JumpIfLocalIntLtConstFalse, // A = target, B = slot, I64 = const
    IntAdd,                     // stack: int, int -> int
    IntSub,                     // stack: int, int -> int
    IntMul,                     // stack: int, int -> int
    IntDiv,                     // stack: int, int -> int
    IntMod,                     // stack: int, int -> int
    IntCmpEq,                   // stack: int, int -> bool
    IntCmpNe,                   // stack: int, int -> bool
    IntCmpLt,                   // stack: int, int -> bool
    IntCmpLe,                   // stack: int, int -> bool
    IntCmpGt,                   // stack: int, int -> bool
    IntCmpGe,                   // stack: int, int -> bool
    ReturnInt,                  // stack: int -> typed int return

    // --- host yield ---
    Yield,
    Await,       // pop awaitable; suspend or push its completed value
    PushHandler, // A = catch handler PC
    PopHandler,
    Throw, // pop raised value and enter error unwinding

    // --- module system ---
    LoadModule,       // S = module name; pushes a ModuleObject onto the stack
    LoadModuleExport, // S = export name; pops ModuleObject, pushes the exported Value

    // --- extended numeric arithmetic ---
    // Kept at the end of the enum so existing opcode values remain stable.
    Pow,                // numeric power
    FloorDiv,           // numeric floor division
    LocalAddInt64Const, // A = slot, I64 = const
    LocalSubInt64Const, // A = slot, I64 = const

    OpcodeCount,
};

struct Instruction {
    Opcode op = Opcode::LoadNil;
    int a = 0;
    int b = 0;
    std::int64_t i64 = 0;
    core::pmr::String s{&core::defaultCpuMemoryResource()};
    SourcePos pos;

    Instruction() = default;

    explicit Instruction(Opcode o) : op(o) {}

    Instruction(Opcode o,
        int a_,
        int b_ = 0,
        std::int64_t i = 0,
        core::pmr::String str = {},
        SourcePos p = {})
        : op(o), a(a_), b(b_), i64(i), s(std::move(str)), pos(std::move(p))
    {}
};

enum class LocalKind : std::uint8_t {
    Param,
    Result,
    Local,
    Capture,
};

struct DebugLocal {
    core::pmr::String name{&core::defaultCpuMemoryResource()};
    int slot = 0;
    LocalKind kind = LocalKind::Local;
    core::pmr::String typeName{&core::defaultCpuMemoryResource()};
    int startPc = 0;
    int endPc = 0;
};

struct DelegationSiteMetadata {
    int sourcePc = -1;
    int initPc = -1;
    int rangePc = -1;
    int branchPc = -1;
    int loadPc = -1;
    int yieldPc = -1;
    int backEdgePc = -1;
    int exitPc = -1;
    int sourceSlot = -1;
    int indexSlot = -1;
    int valueSlot = -1;
};

struct BytecodeSafepoint {
    core::pmr::String id{&core::defaultCpuMemoryResource()};
    int pc = -1;
    core::pmr::String resumeLabel{&core::defaultCpuMemoryResource()};
    SourcePos source;
    core::pmr::Vector<int> canonicalSlots{&core::defaultCpuMemoryResource()};
};

struct BytecodeExceptionRegion {
    core::pmr::String id{&core::defaultCpuMemoryResource()};
    int startPc = -1;
    int endPc = -1;
    int handlerPc = -1;
    core::pmr::String handlerBlockId{&core::defaultCpuMemoryResource()};
    core::pmr::String catchType{&core::defaultCpuMemoryResource()};
    SourcePos source;
};

struct Function;

// Sparse, private-snapshot metadata. Invalid names remain runtime failures at
// the original PushHandler PC; the first instruction for a target wins.
class ARP_API InterpreterHandlerFilterPlan final {
public:
    static InterpreterHandlerFilterPlan prepare(const Function& function);
    ScriptDiagnosticCode lookup(
        int handlerPc, bool& valid, int* unobservedLocal = nullptr) const noexcept;

private:
    struct Entry {
        int handlerPc;
        ScriptDiagnosticCode code;
        bool valid;
        int unobservedLocal;
    };

    core::pmr::Vector<Entry> entries_{&core::defaultCpuMemoryResource()};
};

struct Function {
    core::pmr::String id{&core::defaultCpuMemoryResource()};
    core::pmr::String name{&core::defaultCpuMemoryResource()};
    int params = 0;
    int fixedParams = 0;
    bool isVariadic = false;
    bool isGenerator = false;
    // The function contains the bounded iterator-value `yield from` lowering.
    // Generator send() is conservatively rejected for this function until
    // child-protocol forwarding is implemented.
    bool hasYieldFrom = false;
    int results = 0;
    int locals = 0; // total slot count (params + results + locals + captures)
    int captureCount = 0;
    int maxStack = 0; // computed statically by the compiler
    RuntimeType resultType = RuntimeType::Any;
    core::pmr::Vector<RuntimeType> paramTypes{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::uint8_t> nullableParams{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<core::pmr::String> paramClassNames{&core::defaultCpuMemoryResource()};
    RuntimeType variadicElementType = RuntimeType::Any;
    bool variadicElementNullable = false;
    core::pmr::String variadicElementClassName{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<Instruction> code{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<DebugLocal> debugLocals{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<BytecodeSafepoint> safepoints{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<BytecodeExceptionRegion> exceptionRegions{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<DelegationSiteMetadata> delegationSites{&core::defaultCpuMemoryResource()};

    Function() = default;

    explicit Function(core::pmr::String n) : name(std::move(n)) {}

private:
    friend class ValidatedInterpreterProgram;
    // Never serialized or trusted from input. Snapshot creation overwrites it.
    bool verifiedStackBound_ = false;
    bool scalarDispatch_ = false;
    ScalarExpressionPlan scalarExpressions_;
    InterpreterHandlerFilterPlan handlerFilters_;
};

// Pack two int slot indices into one int64 for RangeNext.
inline std::int64_t packRangeTargets(int keySlot, int valueSlot)
{
    return (static_cast<std::int64_t>(static_cast<std::int32_t>(keySlot)) << 32)
        | (static_cast<std::uint64_t>(static_cast<std::uint32_t>(valueSlot)));
}

inline void unpackRangeTargets(std::int64_t packed, int& keySlot, int& valueSlot)
{
    keySlot = static_cast<std::int32_t>((packed >> 32) & 0xFFFFFFFF);
    valueSlot = static_cast<std::int32_t>(packed & 0xFFFFFFFF);
}

struct HostFunctionEntry {
    core::pmr::String name{&core::defaultCpuMemoryResource()};
    // Stored as a reflection::function wrapped in object; populated at registration time.
    // We keep an opaque pointer here to avoid pulling reflection into the bytecode header;
    // the VM casts it as needed.
    void* functionObject = nullptr;
};

struct Program {
    std::uint32_t bytecodeVersion = 0u;
    core::pmr::String bytecodeFingerprint{&core::defaultCpuMemoryResource()};
    // Assigned by the production typed-IR lowering pipeline. Compatibility
    // bytecode producers leave it empty and require explicit Legacy opt-in
    // before Hybrid may pair them with an AOT descriptor.
    ScriptArtifactIdentity artifactIdentity;
    core::pmr::Vector<Function> functions{&core::defaultCpuMemoryResource()};
    core::pmr::Map<core::pmr::String, int> funcIndex{&core::defaultCpuMemoryResource()};
    core::pmr::Map<core::pmr::String, int> functionIdIndex{&core::defaultCpuMemoryResource()};
    // typeName -> (methodName -> funcIndex)
    core::pmr::Map<core::pmr::String, core::pmr::Map<core::pmr::String, int>> methodSets{
        &core::defaultCpuMemoryResource()};
    // typeName -> explicit constructor function index, if declared
    core::pmr::Map<core::pmr::String, int> constructorIndex{&core::defaultCpuMemoryResource()};
    int entry = -1; // main function index, -1 if none
    int init = -1;  // package-level var init function index, -1 if none
    int globalCount = 0;
    core::pmr::Map<core::pmr::String, int> globalSlots{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<HostFunctionEntry> hostFunctions{&core::defaultCpuMemoryResource()};
    core::pmr::Map<core::pmr::String, int> hostIndex{&core::defaultCpuMemoryResource()};
    // typeName -> field name list (for MakeInstance)
    core::pmr::Map<core::pmr::String, core::pmr::Vector<core::pmr::String>> classSchemas{
        &core::defaultCpuMemoryResource()};

    Program() = default;
};

// Human-readable dump for debugging.
core::pmr::String formatOpcode(Opcode op);
core::pmr::String formatProgram(const Program& program);
ARP_API core::pmr::String computeInterpreterBytecodeFingerprint(const Program& program);
ARP_API ScriptResult validateInterpreterBytecode(const Program& program);

// A private deep snapshot is the validation boundary. A shared_ptr<const Program>
// supplied by callers alone is insufficient: it may alias a mutable owner.
class ARP_API ValidatedInterpreterProgram final {
public:
    static ScriptResult create(const Program& source, ValidatedInterpreterProgram& out);

    bool valid() const noexcept { return program_ != nullptr; }

    const Program& program() const { return *program_; }

    const std::shared_ptr<const Program>& owner() const noexcept { return program_; }

    bool ownsFunction(const Function* function) const noexcept
    {
        if (!program_ || program_->functions.empty() || !function)
            return false;
        const auto* begin = program_->functions.data();
        const auto* end = begin + program_->functions.size();
        const std::less<const Function*> less;
        return !less(function, begin) && less(function, end);
    }

    bool hasVerifiedStackBound(const Function* function) const noexcept
    {
        return ownsFunction(function) && hasVerifiedStackBoundOwned(function);
    }

    bool hasScalarDispatch(const Function* function) const noexcept
    {
        return ownsFunction(function) && hasScalarDispatchOwned(function);
    }

    const ScalarExpressionPlan* scalarExpressions(const Function* function) const noexcept
    {
        return ownsFunction(function) ? scalarExpressionsOwned(function) : nullptr;
    }

    const InterpreterHandlerFilterPlan* handlerFilters(const Function* function) const noexcept
    {
        return ownsFunction(function) ? handlerFiltersOwned(function) : nullptr;
    }

    const ScalarExpressionPlan* independentScalarExpressions(
        const Function* function) const noexcept
    {
        const auto* plan = scalarExpressions(function);
        return plan && plan->independentStackSpans ? plan : nullptr;
    }

private:
    friend class Vm;

    // Only VM paths that already proved exact snapshot/function provenance
    // may borrow this metadata. Public callers retain the checked boundary.
    bool hasVerifiedStackBoundOwned(const Function* function) const noexcept
    {
        assert(ownsFunction(function));
        return function->verifiedStackBound_;
    }

    bool hasScalarDispatchOwned(const Function* function) const noexcept
    {
        assert(ownsFunction(function));
        return function->scalarDispatch_;
    }

    const ScalarExpressionPlan* scalarExpressionsOwned(const Function* function) const noexcept
    {
        assert(ownsFunction(function));
        return function->scalarExpressions_.entries.empty() ? nullptr
                                                            : &function->scalarExpressions_;
    }

    const InterpreterHandlerFilterPlan* handlerFiltersOwned(const Function* function) const noexcept
    {
        assert(ownsFunction(function));
        return &function->handlerFilters_;
    }

    const ScalarExpressionPlan* independentScalarExpressionsOwned(
        const Function* function) const noexcept
    {
        const auto* plan = scalarExpressionsOwned(function);
        return plan && plan->independentStackSpans ? plan : nullptr;
    }

    std::shared_ptr<const Program> program_;
};

} // namespace arp::scriptvm

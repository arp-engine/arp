#pragma once

// ArpScript: versioned, backend-neutral typed IR contract.
//
// This contract is independent from every backend-private representation.

#include "core/cpu_allocator.h"
#include "scriptvm/runtime/diagnostics.h"
#include "scriptvm/runtime/source_pos.h"

#include <cstdint>

namespace arp::scriptvm {

// A CallValue provenance target normally stores its closure capture count.
// This reserved value identifies a zero-capture ScriptMethod target, whose
// receiver remains in the runtime ScriptMethod value rather than Typed IR.
constexpr int kScriptMethodProvenanceCaptureCount = -2;

inline constexpr std::uint32_t kTypedIrVersion = 7;
inline constexpr std::uint32_t kSemanticAbiVersion = 1;

enum class TypedIrType : std::uint8_t {
    Any,
    Int,
    Int64,
    Float,
    Bool,
    String,
    Bytes,
    Object,
    List,
    Dict,
    Function,
    Class,
};

enum class TypedIrOperation : std::uint8_t {
    Load,
    Store,
    Arithmetic,
    Bitwise,
    Compare,
    Convert,
    ControlFlow,
    Call,
    Container,
    Access,
    Host,
    Suspend,
    Exception,
    Return,
    Other,
};

enum class TypedIrOpcode : std::uint8_t {
    LoadNil = 0,
    LoadBool,
    LoadInt,
    LoadInt64,
    LoadFloat,
    LoadString,
    LoadLocal,
    LoadCapture,
    LoadGlobal,
    StoreLocal,
    StoreCapture,
    StoreGlobal,
    Pop,
    Return,
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    Neg,
    Not,
    BitAnd,
    BitOr,
    BitXor,
    BitClear,
    ShiftLeft,
    ShiftRight,
    CmpEq,
    CmpNe,
    CmpLt,
    CmpLe,
    CmpGt,
    CmpGe,
    Jump,
    JumpIfFalse,
    JumpIfTrue,
    Call,
    CallValue,
    CallHost,
    MakeFuncValue,
    MakeClosure,
    MakeMethodValue,
    MakeList,
    MakeDict,
    MakeInstance,
    MakeRange,
    MakeRangeFromStack,
    LoadIndex,
    StoreIndex,
    LoadField,
    StoreField,
    AddrIndex,
    AddrField,
    AddrLocal,
    AddrCapture,
    AddrGlobal,
    Deref,
    StoreIndirect,
    BuiltinLen,
    BuiltinAppend,
    BuiltinDelete,
    BuiltinClear,
    RangeInit,
    RangeNext,
    ConvertInt,
    ConvertInt64,
    ConvertFloat,
    ConvertString,
    ConvertBool,
    ConvertBytes,
    LocalAddIntConst,
    LocalSubIntConst,
    LocalAddLocal,
    LocalAddFloatConst,
    LocalSubFloatConst,
    JumpIfLocalIntLtConstFalse,
    IntAdd,
    IntSub,
    IntMul,
    IntDiv,
    IntMod,
    IntCmpEq,
    IntCmpNe,
    IntCmpLt,
    IntCmpLe,
    IntCmpGt,
    IntCmpGe,
    ReturnInt,
    Yield,
    Await,
    PushHandler,
    PopHandler,
    Throw,
    LoadModule,
    LoadModuleExport,
    Pow,
    FloorDiv,
    LocalAddInt64Const,
    LocalSubInt64Const,
    OpcodeCount,
};

inline std::int64_t packTypedIrRangeTargets(int keySlot, int valueSlot)
{
    return (static_cast<std::int64_t>(static_cast<std::int32_t>(keySlot)) << 32)
        | static_cast<std::uint64_t>(static_cast<std::uint32_t>(valueSlot));
}

enum class TypedIrCallKind : std::uint8_t {
    None,
    Direct,
    Indirect,
    Method,
    Reflection,
    Host,
};

enum class TypedIrCallableOrigin : std::uint8_t {
    ModuleGlobal,
    HostCapability,
    ReflectionMember,
    ScriptFunction,
};

enum class TypedIrSwitchability : std::uint8_t {
    FunctionBoundary,
    Safepoint,
    // The artifact supplies a complete operation-boundary deoptimization map
    // for ordinary functions. This is stronger than yield/await safepoints.
    ArbitraryPc,
};

enum class TypedIrEffect : std::uint32_t {
    None = 0,
    ReadsGlobal = 1u << 0,
    WritesGlobal = 1u << 1,
    ReadsHost = 1u << 2,
    WritesHost = 1u << 3,
    MayRaise = 1u << 4,
    MaySuspend = 1u << 5,
};

inline constexpr std::uint32_t operator|(TypedIrEffect lhs, TypedIrEffect rhs)
{
    return static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs);
}

enum class TypedIrOperandField : std::uint16_t {
    None = 0,
    A = 1u << 0,
    B = 1u << 1,
    I64 = 1u << 2,
    Text = 1u << 3,
    BranchTarget = 1u << 4,
    FunctionTargetId = 1u << 5,
    CallableBindingId = 1u << 6,
};

inline constexpr std::uint16_t operator|(TypedIrOperandField lhs, TypedIrOperandField rhs)
{
    return static_cast<std::uint16_t>(lhs) | static_cast<std::uint16_t>(rhs);
}

inline constexpr std::uint16_t operator|(std::uint16_t lhs, TypedIrOperandField rhs)
{
    return lhs | static_cast<std::uint16_t>(rhs);
}

inline constexpr std::int8_t kTypedIrDynamicStackEffect = -1;

struct TypedIrOperationDescriptor {
    const char* name = "Unknown";
    TypedIrOperation operation = TypedIrOperation::Other;
    TypedIrCallKind callKind = TypedIrCallKind::None;
    std::uint32_t requiredEffects = 0u;
    std::uint16_t operandFields = 0u;
    std::int8_t stackInputs = 0;
    std::int8_t stackOutputs = 0;
    bool terminator = false;
    bool conditionalBranch = false;
    bool suspension = false;
};

struct TypedIrTypeRef {
    TypedIrType kind = TypedIrType::Any;
    bool nullable = false;
    core::pmr::String className{&core::defaultCpuMemoryResource()};
};

struct TypedIrSignature {
    core::pmr::Vector<TypedIrTypeRef> parameters{&core::defaultCpuMemoryResource()};
    TypedIrTypeRef result;
    bool variadic = false;
    int fixedParameterCount = 0;
    TypedIrTypeRef variadicElement;
};

struct TypedIrOperationRecord {
    TypedIrOpcode opcode = TypedIrOpcode::LoadNil;
    TypedIrOperation operation = TypedIrOperation::Other;
    // These operands are semantic slots, not backend storage indices.
    int a = 0;
    int b = 0;
    std::int64_t i64 = 0;
    core::pmr::String text{&core::defaultCpuMemoryResource()};
    SourcePos source;
    TypedIrCallKind callKind = TypedIrCallKind::None;
    core::pmr::String targetId{&core::defaultCpuMemoryResource()};
    core::pmr::String callableBindingId{&core::defaultCpuMemoryResource()};
    // A CallValue contract emitted only after provenance dataflow has proved
    // an exact same-module closure origin, or a ScriptMethod origin encoded
    // by kScriptMethodProvenanceCaptureCount.
    core::pmr::String closureTargetFunctionId{&core::defaultCpuMemoryResource()};
    int closureCaptureCount = -1;
    // `yield from` is not recoverable from the RangeNext CFG shape: a plain
    // generator for-loop can be byte-for-byte identical. Only the typed
    // frontend may assert this source-level delegation contract.
    bool delegatedGeneratorRange = false;
    std::uint32_t effectFlags = static_cast<std::uint32_t>(TypedIrEffect::None);
};

struct TypedIrCallableBinding {
    core::pmr::String bindingId{&core::defaultCpuMemoryResource()};
    TypedIrCallableOrigin origin = TypedIrCallableOrigin::HostCapability;
    core::pmr::String targetId{&core::defaultCpuMemoryResource()};
    core::pmr::String signatureHash{&core::defaultCpuMemoryResource()};
    int fixedArity = -1;
    bool maySuspend = false;
    bool generatorBridge = false;
    std::uint32_t lifetimeVersion = 0;
};

// Versioned script-class metadata retained in the canonical IR rather than
// inferred from MakeInstance text. Constructor IDs refer to the same
// module's Typed IR function IDs; an empty ID denotes the language default
// constructor.
struct TypedIrScriptClassSchema {
    core::pmr::String className{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<core::pmr::String> fields{&core::defaultCpuMemoryResource()};
    // Method names are part of the artifact contract. The function id is
    // stable across function-vector reordering and is resolved by the AOT
    // runtime against the same module descriptor.
    core::pmr::Map<core::pmr::String, core::pmr::String> methods{&core::defaultCpuMemoryResource()};
    core::pmr::String constructorFunctionId{&core::defaultCpuMemoryResource()};
};

struct TypedIrGlobalSlot {
    core::pmr::String name{&core::defaultCpuMemoryResource()};
    int slot = -1;
};

struct TypedIrBasicBlock {
    core::pmr::String id{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<TypedIrOperationRecord> operations{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<core::pmr::String> successors{&core::defaultCpuMemoryResource()};
};

enum class TypedIrLocalKind : std::uint8_t {
    Param,
    Result,
    Local,
    Capture,
};

struct TypedIrLocal {
    core::pmr::String name{&core::defaultCpuMemoryResource()};
    int slot = -1;
    TypedIrLocalKind kind = TypedIrLocalKind::Local;
    TypedIrTypeRef type;
    int startOperation = 0;
    int endOperation = 0;
};

struct TypedIrSafepoint {
    core::pmr::String id{&core::defaultCpuMemoryResource()};
    int operationIndex = -1;
    core::pmr::String resumeLabel{&core::defaultCpuMemoryResource()};
    SourcePos source;
    core::pmr::Vector<int> canonicalSlots{&core::defaultCpuMemoryResource()};
};

struct TypedIrExceptionRegion {
    core::pmr::String id{&core::defaultCpuMemoryResource()};
    // The protected range is [startOperation, endOperation). The handler
    // operation is the first operation executed after unwinding.
    int startOperation = -1;
    int endOperation = -1;
    int handlerOperation = -1;
    core::pmr::String handlerBlockId{&core::defaultCpuMemoryResource()};
    core::pmr::String catchType{&core::defaultCpuMemoryResource()};
    SourcePos source;
};

struct TypedIrDelegationSite {
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

struct TypedIrFunction {
    core::pmr::String id{&core::defaultCpuMemoryResource()};
    core::pmr::String name{&core::defaultCpuMemoryResource()};
    TypedIrSignature signature;
    int parameterCount = 0;
    int localCount = 0;
    int captureCount = 0;
    int maxStack = 0;
    bool isGenerator = false;
    // The frontend lowers `yield from` to RangeInit/RangeNext/Yield.  Keep
    // the source-level protocol bit in Typed IR so native lowering can apply
    // child-generator ownership and injection rules without guessing from
    // an arbitrary opcode sequence.
    bool hasYieldFrom = false;
    std::uint32_t effectFlags = static_cast<std::uint32_t>(TypedIrEffect::None);
    TypedIrSwitchability switchability = TypedIrSwitchability::FunctionBoundary;
    core::pmr::Vector<TypedIrLocal> locals{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<TypedIrBasicBlock> blocks{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<TypedIrSafepoint> safepoints{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<TypedIrExceptionRegion> exceptionRegions{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<TypedIrDelegationSite> delegationSites{&core::defaultCpuMemoryResource()};
};

struct TypedIrModule {
    core::pmr::String moduleId{&core::defaultCpuMemoryResource()};
    std::uint32_t moduleVersion = 1;
    std::uint32_t semanticAbiVersion = kSemanticAbiVersion;
    std::uint32_t typedIrVersion = kTypedIrVersion;
    int globalCount = 0;
    core::pmr::Vector<TypedIrGlobalSlot> globals{&core::defaultCpuMemoryResource()};
    // Explicit module lifecycle entry. Empty means the module has no top-level
    // initializer; consumers must not infer one from a function name or index.
    core::pmr::String moduleInitFunctionId{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<core::pmr::String> dependencies{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<core::pmr::String> reflectionDependencies{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<core::pmr::String> hostCapabilities{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<TypedIrCallableBinding> callableBindings{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<TypedIrScriptClassSchema> scriptClassSchemas{
        &core::defaultCpuMemoryResource()};
    core::pmr::Vector<TypedIrFunction> functions{&core::defaultCpuMemoryResource()};
    // Deterministic FNV-1a fingerprint for the in-memory contract. AOT
    // artifacts will add the required cryptographic source/dependency hashes.
    core::pmr::String fingerprint{&core::defaultCpuMemoryResource()};
};

struct TypedIrBuildResult {
    TypedIrModule module;
    ScriptDiagnostic error;

    bool ok() const { return error.code == ScriptDiagnosticCode::None; }
};

// The native scalar lowering owns a fixed-size operand stack. This analysis
// proves the stack bound for the complete straight-line scalar subset. Other
// IR remains well-formed for analysis, but cannot claim a DirectNative proof.
enum class TypedIrScalarStackStatus : std::uint8_t {
    NotApplicable,
    Valid,
    Invalid,
};

struct TypedIrScalarStackAnalysis {
    TypedIrScalarStackStatus status = TypedIrScalarStackStatus::NotApplicable;
    int peakStack = 0;
};

// Proof for the native scalar subset across explicit jump edges. A valid
// result establishes a single stack depth at every reachable operation and
// definite initialization for every local read; unsupported operations remain
// eligible only for an explicitly supported DirectNative lowering; unsupported
// operations remain deterministic diagnostic failures.
struct TypedIrScalarControlFlowAnalysis {
    TypedIrScalarStackStatus status = TypedIrScalarStackStatus::NotApplicable;
    int peakStack = 0;
    int failureOperationIndex = -1;
    core::pmr::String failureOperation{&core::defaultCpuMemoryResource()};
    core::pmr::String failureReason{&core::defaultCpuMemoryResource()};
    SourcePos failureSource;
};

// Function ids are derived from explicit input, never from vector position.
ARP_API core::pmr::String makeTypedIrFunctionId(
    const core::pmr::String& moduleId, const core::pmr::String& functionName);

ARP_API core::pmr::String typedIrSignatureHash(const TypedIrSignature& signature);
ARP_API const TypedIrOperationDescriptor& typedIrOperationDescriptor(TypedIrOpcode opcode);
ARP_API core::pmr::String typedIrOpcodeName(TypedIrOpcode opcode);
ARP_API core::pmr::String computeTypedIrFingerprint(const TypedIrModule& module);
ARP_API void annotateModuleInitClosureCallProvenance(TypedIrModule& module);
ARP_API TypedIrScalarStackAnalysis analyzeTypedIrScalarStack(const TypedIrFunction& function);
ARP_API TypedIrScalarControlFlowAnalysis analyzeTypedIrScalarControlFlow(
    const TypedIrFunction& function);

// Validate versions, identity, signatures, CFG metadata and the stored
// fingerprint before a backend consumes the module.
ARP_API ScriptResult validateTypedIr(const TypedIrModule& module);

} // namespace arp::scriptvm
